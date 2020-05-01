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

#define NUM_ARGS 13
// TODO create paths
// std::map<size_t > 

int main(int argc, char** argv) {
    if (argc != NUM_ARGS) {
        cout << 
        "Usage: \
        ./run.exe <random-seed> <map-size> <# of trxs/rank> <buffer size/trx> \
        <simulation duration in ms> <time-step to adjust velocity (ms)> \
        <period of transmitted data> <communication range/each trx> \
        <send time delay/trx (ms)> <rcv time delay/trx (ms)> \
        <lower bound on trx move speed (m/s)> \
        <upper bound on trx move speed (m/s)>" << endl;
        return 0;
    }
    // User's responsibility to send in right types.
    static const float random_seed = atoi(argv[1]);    
    static const size_t map_size = atoi(argv[2]);
    static const size_t num_trxs = atoi(argv[3]); 
    static const size_t max_buffer_size = atoi(argv[4]);
    static const double simulation_duration = atoi(argv[5]);

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

    auto trxs = MPIRadioTransceiver<max_buffer_size>::transceivers<num_trxs>();
    if (trxs == nullptr) { // Unable to retrieve transceivers.
        MPI_Finalize();
        return 1;
    }

    // Create paths for transceiver moves at a certain point in time using the
    // random waypoint pattern.

    // Transceiver initialization.
    //

    // Shuts down all transceivers.
    MPIRadioTransceiver<max_buffer_size>::close_transceivers(num_trxs>(trxs);

    // Synchronize MPI ranks.
    MPI_Finalize();
    return 0;
}
