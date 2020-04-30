/*
File: clustered_transceiver_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance in a
  cluster of transceivers.
*/

// C/C++libraries
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

// RANDOM POINT GENERATION
#include <cmath>

// MPI
#include "mpi.h"

// MPI RADIO TRANSCEIVER
#include "mpi_radio_transceiver.hpp"

using std::cout;
using std::cerr;
using std::endl;

#define _USE_MATH_DEFINES
#define MAX_SEND_RANGE 2
#define MAX_RECV_RANGE 2
#define MAX_BUFFER_SIZE 2048
#define NUM_TRXS 2

/**
 * Generates a random point within a circle centered at (x_center, y_center)
 * of radius r.
 * 
 * Returns:
 *     A random point within the circle stipulated by the args.
 */
std::pair<double, double> generate_point(
    const double x_center, const double y_center, const double r) {
    double theta = ((double)rand()/RAND_MAX) * 2 * M_PI;
    return std::make_pair<double, double>(
        x_center + r * cos(theta), y_center + r * sin(theta));
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

    // Initialize the transceivers.
    // Each transceiver is given a location located with radius .5 of location
    // (1, 1), and has a send/recv distance of 2. All transceivers across all
    // ranks are within communication range of each other.
    std::pair<double, double> loc;
    for (size_t i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // Setting parameters for transceiver t.
        t.set_m_id(i);
        loc = generate_point(1, 1, .5);
        t.set_x(loc.first);
        t.set_y(loc.second);
        t.set_send_duration(0.0);
        t.set_recv_duration(0.0);
        t.set_send_range(0.25);
        t.set_recv_range(0.25);
    }

    // Ensures all transceivers finish initializing.
    MPI_Barrier(MPI_COMM_WORLD);

    // This clustering transceiver benchmark tests as follows:
    // - rank 0 trx 0 sends a message
    // - all other trxs across the ranks block waiting for the msg for 50ms
    // - ranks shut down transceivers and sync, and record across ranks total
    //   # of trxs that received the msg
    MPI_Finalize();
    return 0;
}