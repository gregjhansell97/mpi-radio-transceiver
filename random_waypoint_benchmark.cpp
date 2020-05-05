/*
File: random_waypoint_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance
  trasmitting messages while moving through a finite-space following a random
  waypoint model.
Usage:
  mpic++ ./random_waypoint_benchmark.cpp -o run.exe -std=c++11
  ./run.exe (OPTIONAL: <file-I/O flag = 1>)
*/

// C/C++libraries
#include <algorithm>
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <thread>

// MPI
#include "mpi.h"

// MPI RADIO TRANSCEIVER
#include "radio_transceiver.h"

using std::cout;
using std::cerr;
using std::endl;

// #define FILE_SIZE 120000 // change as # trxs/procs scale up
#define FILE_NAME "latencies.txt"

// TRXS INIT PARAMS
#define LATENCY 0 // (s) mock time delay between send and rcv

// RANDOM WAYPOINT SIMULATION PARAMS
#define RAND_SEED 52021 // keeps rand way-point paths consistently random
#define MAP_SIZE 1000 // Square world with no wrap-around.
#define NUM_TRXS 2 // trxs/rank
#define SIMULATION_DURATION  2 // s
#define TIME_STEP 100          // interval between transceiver moves
#define DATA_PERIOD 180         // Intervals btw data transmission
#define COMM_RANGE 125     // send and recv ranges.
#define SEND_DELAY .01    // ms
#define RECV_DELAY .001    // ms
#define SPEED_MAX 20
#define SPEED_MIN 10
#define LATENCY_PRECISION 100000 // decimal places to measure latency
#define NUM_THREADS_PER_BLOCK 2 // for CUDA, later

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
    // ========================================================================
    // ENVIRONMENT SETUP
    // ========================================================================
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
        cout << "Unable to retrieve the total number of ranks." << endl;
        return 1;
    }

    // Used if i/o is specified.
    MPI_File latency_file;
    MPI_File send_file;
    MPI_File recv_file;
    MPI_File* latency_file_ptr = &latency_file;
    MPI_File* send_file_ptr = &send_file;
    MPI_File* recv_file_ptr = &recv_file;
    MPI_File_open(
        MPI_COMM_WORLD,
        FILE_NAME,
        MPI_MODE_CREATE|MPI_MODE_RDWR,
        MPI_INFO_NULL,
        &latency_file
    );

    // opens up if file if i/o specified otherwise, doesn't
    bool file_io;
    if (argc == 2 && (file_io = atoi(argv[1]) == 0)) { // yes i/o
        MPI_File_open(
            MPI_COMM_WORLD,
            "send_logs.out",
            MPI_MODE_CREATE|MPI_MODE_RDWR,
            MPI_INFO_NULL,
            &send_file
        );
        MPI_File_open(
            MPI_COMM_WORLD,
            "recv_logs.out",
            MPI_MODE_CREATE|MPI_MODE_RDWR,
            MPI_INFO_NULL,
            &recv_file
        );
    } else {
        send_file_ptr = nullptr;
        recv_file_ptr = nullptr;
    }

    auto trxs = RadioTransceiver::transceivers(
        NUM_TRXS, LATENCY, NUM_THREADS_PER_BLOCK,
        send_file_ptr, recv_file_ptr
    );
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
        t.device_data->send_range = COMM_RANGE;
        t.device_data->recv_range =COMM_RANGE;
    }

    // Make sure all ranks' transceivers initialized before proceeding.
    MPI_Barrier(MPI_COMM_WORLD);

    // ========================================================================
    // SIMULATION START
    // ========================================================================
    /**
    * This test places all the transceivers on a finitely-sized world, and
    * and runs for a specified duration. At intervals of a pre-defined time
    * step, every transceiver moves following the random waypoint pattern.
    * 
    * A subset of transceivers periodically transmit data. This data is
    * curr_time. The receivers (which are not the senders, and
    * waiting on separate ranks) that are within communication range record
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
                t.device_data->x = x;
                t.device_data->y = y;
            }
        }

        if (rank == 0) { // If rank 0, check if sending out msgs right now
            if (
            (int)std::fmod(10000 * (curr_time + op_time), DATA_PERIOD) == 0) {
                // rank 0's trx[0] send out msg of curr_time 
                for (size_t i = 0; i < NUM_TRXS; ++i) {
                    auto& sender = trxs[i];
                    // grabs to 100-thousandth place for latency calcs.
                    // Add back the operations time for true clock time
                    // edge case rcv threads finished?
                    std::string cpp_time = std::to_string(MPI_Wtime());
                    const char* msg = cpp_time.c_str();
                    sender.send(msg, 13, SEND_DELAY);
                }
            }
        } else { // receives on separate ranks wait for info
            for (size_t i = 0; i < NUM_TRXS; ++i) {
                auto& t = trxs[i];
                char* raw_msg; // rcv 1msg/trxs
                // wait for msg
                size_t bytes = t.recv(&raw_msg, 0.1);
                if (bytes == 13) { // you've got mail!
                    // grab time immediately after recv unblocks
                    // individual times needed for median calcs
                    latencies.push_back(MPI_Wtime() - atof(raw_msg));
                    msgs_rcvd++;
                    for (size_t j = 1; j < NUM_TRXS; ++j) {
                        if ((bytes = t.recv(&raw_msg, 0)) == 13) {
                            latencies.push_back(MPI_Wtime() - atof(raw_msg));
                            msgs_rcvd++;
                        }
                    }
                }
            }
        }
        double op_end = MPI_Wtime(); // Take end time of operations
        op_time += op_end - op_start; // op_time will miss this line (small?)
    }
    // ========================================================================
    // SIMULATION END
    // ========================================================================

    // ========================================================================
    // METRICS, I/O, & CUDA
    // ========================================================================
    // Send msg counts to rank 0
    uint all_msgs_rcvd[num_ranks-1]; // rank 0 track msgs rcvd by other ranks
    if (rank != 0) {
        MPI_Send(&msgs_rcvd, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
    } else {
        int rcvs[num_ranks - 1];
        MPI_Request reqs[num_ranks - 1];
        MPI_Status stats[num_ranks - 1];
        for (size_t i = 0; i < num_ranks - 1; ++i) {
            rcvs[i] = MPI_Irecv(&all_msgs_rcvd[i], 1, MPI_INT, i + 1,
                1, MPI_COMM_WORLD, &reqs[i]);
        }
        MPI_Waitall(num_ranks-1, reqs, stats);
        // Error check all rcv operations were successful.
        for (size_t i = 0; i < num_ranks - 1; ++i) {
            if (rcvs[i] != MPI_SUCCESS) {
                std::cerr << "Receiving latencies  not succesful" << endl;
                RadioTransceiver::close_transceivers(trxs);
                MPI_Finalize();
                return 1;
            }
        }
    }

    // wait for all latencies to be written
    MPI_Barrier(MPI_COMM_WORLD);

    std::cout << "done" << std::endl;
    // METRICS
    // - average, min, and max latencies
    // know from msg counts size of incoming #s from each rank
    double sum_latencies = 0.0; // rank's summed latencies
    double global_sum_latencies; // global latency sum
    double global_min_latency; // global min latency
    double global_max_latency; // global max latency
    double min_latency = INFINITY; // rank's min_latency
    double max_latency = -1.0;  // rank's max latency
    int total_msgs_rcvd = 0.0;        // global msgs rcvd
    if (rank != 0) {
        min_latency =
            *std::min_element(std::begin(latencies), std::end(latencies));
        max_latency =
            *std::max_element(std::begin(latencies), std::end(latencies));
        for (const auto& l : latencies) {
            sum_latencies += l;
        }
    } else {
        for (const auto& msg : all_msgs_rcvd) {
            total_msgs_rcvd += msg;
        }
    }
    MPI_Reduce(
        &sum_latencies, &global_sum_latencies, 1, MPI_DOUBLE, MPI_SUM, 0,
        MPI_COMM_WORLD
    );
    MPI_Reduce(
        &min_latency, &global_min_latency, 1, 
        MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD
    );
    MPI_Reduce(
        &max_latency, &global_max_latency, 1,
        MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD
    );

    // rank0 calculates avg and prints out
    if (rank == 0) {
        double avg = global_sum_latencies/(double)total_msgs_rcvd;
        std::cout << "Average latency (deciseconds): " << avg << endl <<
        "Min latency (deciseconds): " << global_min_latency << endl <<
        "Max latency (deciseconds): " << global_max_latency << endl;
    }

    // TODO calculate standard dev?
    RadioTransceiver::close_transceivers(trxs);
    MPI_Finalize();
    return 0;
}
