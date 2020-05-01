/*
File: random_waypoint_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance
  trasmitting messages while moving through a finite-space following a random
  waypoint model.
Usage:
  mpic++ ./random_waypoint_benchmark.cpp -o run.exe -std=c++11
  ./run.exe \
    <random-seed> \
    <map-size> \
    <# of trxs/rank> \
    <buffer size/trx (bytes)> \
    <simulation duration (ms)> \
    <time-step to adjust velocity (ms)> \
    <period of transmitted data> \
    <communication range/each trx> \
    <send time delay/trx (ms)> \
    <rcv time delay/trx (ms)> \
    <lower bound on trx move speed (m/s)> \
    <upper bound on trx move speed (m/s)>
*/

// C/C++libraries
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

// MPI
#include "mpi.h"

// MPI RADIO TRANSCEIVER
#include "mpi_radio_transceiver.hpp"

using std::cout;
using std::cerr;
using std::endl;

#define RAND_SEED 52021
#define MAP_SIZE 10
#define NUM_TRXS 4
#define MAX_BUFFER_SIZE 2048   // bytes
#define SIMULATION_DURATION 15 // secs
#define TIME_STEP 5          // secs
#define PERIOD 
#define COMM_RANGE
#define SEND_DELAY 20    // ms
#define RECV_DELAY 30    // ms
#define SPEED_MAX 
#define SPEED_MIN

// Singular component making up a trx's path.
typedef struct Point {
    double curr_x, curr_y;    // New location of a trx
    double curr_time;         // at time curr_time.
} Point;

/**
 * Plots the random waypoint path a trx travels.
 * 
 * Args:
 *      rank: Current rank, used so trxs with same IDs across ranks don't
 *            have the same path defined.
 *      id: Unique m_id of the trx whose path is calculated.
 * Returns:
 *      The collection of Points in chronological order the trx specified by
 *      the args follows.
 */
 std::queue<Point> plot_path(int rank, size_t id) {
     std::queue<Point> path;
     
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

    srand(RAND_SEED);
    // Transceiver and path initialization.
    std::map<size_t, std::queue<Point>> trx_to_path;
    for (size_t i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // Create paths for transceiver moves at a certain point in time using
        // the random waypoint pattern.
    }

    /**
    * This test places all the transceivers on a finitely-sized world, and
    * and runs for a speciied duration. At intervals of a pre-defined time
    * step, every transceiver moves following the random waypoint pattern.
    */  

    // Shuts down all transceivers.
    MPIRadioTransceiver<MAX_BUFFER_SIZE>::close_transceivers<NUM_TRXS>(trxs);

    // Synchronize MPI ranks.
    MPI_Finalize();
    return 0;
}
