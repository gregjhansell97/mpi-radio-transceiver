/*
File: random_waypoint_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance
  trasmitting messages while moving through a finite-space following a random
  waypoint model.
Usage:
  mpic++ ./random_waypoint_benchmark.cpp -o run.exe -std=c++11
  ./run.exe
*/

// C/C++libraries
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>

// MPI
#include "mpi.h"

// MPI RADIO TRANSCEIVER
#include "mpi_radio_transceiver.hpp"

using std::cout;
using std::cerr;
using std::endl;

#define FILE_SIZE 3000 // change as # trxs/procs scale up
#define FILE_NAME "latencies.out"

// RANDOM WAYPOINT SIMULATION PARAMS
#define RAND_SEED 52021
#define MAP_SIZE 1000 // Square world with no wrap-around.
#define NUM_TRXS 2
#define MAX_BUFFER_SIZE 2048   // bytes
#define SIMULATION_DURATION  5 // s
#define TIME_STEP 100          // interval between transceiver moves
#define DATA_PERIOD 180         // Intervals btw data transmission
#define COMM_RANGE 125     // send and recv ranges.
#define SEND_DELAY .01    // ms
#define RECV_DELAY .001    // ms
#define SPEED_MAX 20
#define SPEED_MIN 10
#define LATENCY_PRECISION 100000 // decimal places to measure latency

// Singular component making up a trx's path.
typedef struct Point {
    double x, y;    // New location of a trx
    double time;    // at a specified time.
} Point;

/**
 * Plots the random waypoint path a trx travels.
 * 
 * Args:
 *      gen: random number generator seeded with RAND_SEED
 * Returns:
 *      Collection of Points in chronological order.
 */
 std::deque<Point> plot_path(std::mt19937 gen) {
    std::deque<Point> path; // Collection of Points dictating trx id's path.
    // Transforms gen's random uint into a double in [0, MAP_SIZE).
    std::uniform_real_distribution<> dis_loc(0, MAP_SIZE);
    Point point = {dis_loc(gen), dis_loc(gen), 0};
    path.push_back(point);

    // Transforms gen's random uint into a double in [SPEED_MIN, SPEED_MAX).
    std::uniform_real_distribution<> dis_sp(SPEED_MIN, SPEED_MAX);
    // Populate the Path at a random point in time for the entirety simulation
    // duration.
    while (path.back().time <= (SIMULATION_DURATION * 1000) + TIME_STEP) {
        double speed = dis_sp(gen);
        const Point prev = path.back();
        double next_x = dis_loc(gen);
        double next_y = dis_loc(gen);
        double dist =
            sqrt(pow((prev.x - next_x), 2) + pow((prev.y - next_y), 2));
        double next_time = dist/speed + prev.time;
        Point next = {next_x, next_y, next_time};
        path.push_back(next);
    }
    return path;
 }

int main(int argc, char** argv) {
    // Initializes MPI environment.
    int prov;
    int ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &prov);
    if (ret != MPI_SUCCESS || prov != MPI_THREAD_MULTIPLE) {
        cout << "Unable to initialize the MPI execution environment." << endl;
    return 1;
    }

    // Grab MPI specs.
    int rank;
    if (MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS) {
        cout << "Unable to retrieve the current rank's ID." << endl;
        return 1;
    }
    int num_ranks;
    if (MPI_Comm_size(MPI_COMM_WORLD, &num_ranks) != MPI_SUCCESS) {
        cout << "Unable to retireve the total number of ranks." << endl;
        return 1;
    }

    auto trxs = MPIRadioTransceiver<MAX_BUFFER_SIZE>::transceivers<NUM_TRXS>();
    if (trxs == nullptr) { // Unable to retrieve transceivers.
        MPI_Finalize();
        return 1;
    }

    std::mt19937 gen(RAND_SEED);
    // Transceiver and path initialization.
    std::map<size_t, std::deque<Point>> trx_to_path;
    for (size_t i = 0; i < NUM_TRXS; ++i) {
        // Create paths for transceiver moves at a certain point in time using
        // the random waypoint pattern.
        trx_to_path[i] = plot_path(gen);
        // Set parameters for trx t.
        // Location is dealt with later in simulation.
        auto& t = trxs[i];
        t.set_send_range(COMM_RANGE);
        t.set_recv_range(COMM_RANGE);
        t.set_send_duration(SEND_DELAY);
        t.set_recv_duration(RECV_DELAY);
    }

    // Make sure all ranks' transceivers initialized before proceeding.
    MPI_Barrier(MPI_COMM_WORLD);

    /**
    * This test places all the transceivers on a finitely-sized world, and
    * and runs for a specified duration. At intervals of a pre-defined time
    * step, every transceiver moves following the random waypoint pattern.
    * 
    * A subset of transceivers periodically transmit data. This data is
    * curr_time. The receivers (which are not the senders, and
    * waiting on separate threads) that are within communication range record
    * the latency for further performance calculation and measures later.
    */
    double start_time; // Simulation start time.
    double op_time = 0.0; // Time spent on operations, subtracted off. is this needed?
    double curr_time; // Current time spent in simulation (ms).

    // Total elapsed time disregarding time spent on operations.
    start_time = MPI_Wtime();
    uint msgs_rcvd = 0; // Track of how many msgs were rcvd/rank
    std::vector<double> latencies; // Track all latencies for perf metrics
    // Prefer ms for data transmission period and time steps.
    while ( 1000 * (curr_time = MPI_Wtime() - start_time - op_time)
        < SIMULATION_DURATION) {
        double op_start = MPI_Wtime();
        // Check if trxs are moving this time interval
        if ((int)std::fmod(10000 * curr_time, TIME_STEP) == 0) {
            // All trxs are moving!
            for (size_t i = 0; i < NUM_TRXS; ++i) {
                std::deque<Point> path = trx_to_path.at(i);
                Point p1 = path[1];
                while (curr_time > p1.time) { // Pop off Points until curr_time
                    path.pop_front();
                    p1 = path[1];
                }
                Point p0 = path.front();
                double offset = (curr_time - p0.time)/(p1.time - p0.time);
                double x = p0.x + offset * (p1.x - p0.x);
                double y = p0.y + offset * (p1.y - p0.y);
                // Access trx corresponding with this move and set new loc
                auto& t = trxs[i];
                t.set_x(x);
                t.set_y(y);
            }
        }

        // check if sending out msgs right now
        if (
        (int)std::fmod(10000 * (curr_time + op_time), DATA_PERIOD) == 0) {
            // all rank's trx[0] send out msg of curr_time
            
            for (size_t i = 0; i < NUM_TRXS; ++i) {
                auto& t = trxs[i];
                // grabs to 100-thousandth place for latency calcs.
                // Add back the operations time for true clock time
                // edge case rcv threads finished?
                std::string cpp_time = std::to_string(MPI_Wtime());
                const char* msg = cpp_time.c_str();
                cout << "rank0-t" << i << " sent " <<
                t.send(msg, 13, SEND_DELAY) <<
                " bytes" << endl;
            }
        }
        } else { // receives on separate threads wait for info
            for (size_t i = 0; i < NUM_TRXS; ++i) {
                auto& t = trxs[i];
                char* raw_msg;
                // wait for msg w/ timeout
                size_t bytes = t.recv(&raw_msg, 0);
                if (bytes == 13) { // you've got mail!
                    // grab time immediately after recv unblocks
                    double rcvd_time = MPI_Wtime();
                    // individual times needed for median calcs
                    latencies.push_back(rcvd_time - atof(raw_msg));
                    msgs_rcvd++;
                    std::cout << "first: rank" << rank << "trx" <<
                    i << " rcvd " << raw_msg;
                    if ((bytes = t.recv(&raw_msg, 0)) != 0) {
                        std::cout << " second: rank" << rank << "trx" <<
                        i << " rcvd " << raw_msg << endl;
                        exit(-1);
                    } // No other info rcvd
                } else { // No other info rcvd
                    assert(bytes == 0);
                }
            }
        }
        double op_end = MPI_Wtime(); // Take end time of operations
        op_time += op_end - op_start; // op_time will miss this line (small?)
    }

    int global_msgs_rcvd = 0; // amass total # of msgs
    MPI_Reduce(
        &msgs_rcvd, // send this rank's # of rcvd msgs
        &global_msgs_rcvd, // rcv in this var
        1, // expected # of msgs sent/rank
        MPI_INT, // msg type
        MPI_SUM,
        0, // root rank
        MPI_COMM_WORLD
    );
    // TODO optimization: collective i/o only rank 1 writes to file
    MPI_File fh; // file ptr to latencies
    if (rank != 0) { // rcving ranks write latencies to file
        int bufsize = FILE_SIZE/(num_ranks - 1); // data chunk available/rank
        for (const auto& l : latencies) {
            MPI_File_open(
                MPI_COMM_WORLD, FILE_NAME,
                MPI_MODE_CREATE|MPI_MODE_WRONLY,
                MPI_INFO_NULL, &fh
            );
            // MPI_File_write_at(fh, rank * bufsize, )
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    cout << "TEST SUCCEEDED!" << endl;
    // rank0 reads from file, min/max-heap median, avg, std dev
    // output to screen?
    // TODO min/max-heap find median from stream of data
    // TODO stddev streaming fxn
    // TODO create ds of latencies and msg_counter per rank
    MPIRadioTransceiver<MAX_BUFFER_SIZE>::close_transceivers<NUM_TRXS>(trxs);
    MPI_Finalize();
    return 0;
}
