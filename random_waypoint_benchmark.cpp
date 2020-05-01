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

#define RAND_SEED 52021
#define MAP_SIZE 10 // Square world with no wrap-around
#define NUM_TRXS 4
#define MAX_BUFFER_SIZE 2048   // bytes
#define SIMULATION_DURATION 15 // secs
#define TIME_STEP 5          // secs
#define PERIOD 
#define COMM_RANGE 2     // send and recv range equal TODO do they have to be equal?
#define SEND_DELAY 20    // ms
#define RECV_DELAY 30    // ms
#define SPEED_MAX 3
#define SPEED_MIN 1

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
 std::queue<Point> plot_path(std::mt19937 gen) {
    std::queue<Point> path; // Collection of Points dictating trx id's path.
    // Transforms gen's random uint into a double in [0, MAP_SIZE).
    std::uniform_real_distribution<> dis_loc(0, MAP_SIZE);
    Point point = {dis_loc(gen), dis_loc(gen), 0};
    path.push(point);

    // Transforms gen's random uint into a double in [SPEED_MIN, SPEED_MAX).
    std::uniform_real_distribution<> dis_sp(SPEED_MIN, SPEED_MAX);
    // Populate the Path at a random point in time for the entirety simulation
    // duration.
    while (path.back().time <= SIMULATION_DURATION + TIME_STEP) {
        double speed = dis_sp(gen);
        const Point prev = path.back();
        double next_x = dis_loc(gen);
        double next_y = dis_loc(gen);
        double dist = sqrt(pow((prev.x - next_x), 2) + pow((prev.y - next_y), 2));
        double next_time = dist/speed + prev.time;
        Point next = {next_x, next_y, next_time};
        path.push(next);
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
    std::map<size_t, std::queue<Point>> trx_to_path;
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
    * and runs for a speciied duration. At intervals of a pre-defined time
    * step, every transceiver moves following the random waypoint pattern.
    * 
    * 
    */
    // TODO accummulate sums and avg across # of transceivers
    double sum_of_all_latencies = 0;

    /**
     * Close transceivers and sync information across ranks.
     * Calculate average, median, and std dev latencies across all trxs in all
     * ranks, which are output to files for debug and performance analysis.
     */
    // TODO MPI I/O write 
    MPIRadioTransceiver<MAX_BUFFER_SIZE>::close_transceivers<NUM_TRXS>(trxs);
    MPI_Finalize();
    return 0;
}
