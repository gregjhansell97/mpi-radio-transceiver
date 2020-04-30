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
#include <fstream>
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
#define SND_RCV_RANGE 2
#define MAX_BUFFER_SIZE 2048
#define NUM_TRXS 40
#define RECV_TIMEOUT_MS 50

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
        t.set_send_range(SND_RCV_RANGE);
        t.set_recv_range(SND_RCV_RANGE);
    }

    // Ensures all transceivers finish initializing.
    MPI_Barrier(MPI_COMM_WORLD);

    // This clustering transceiver benchmark tests as follows:
    // - rank 0 trx 0 sends a message
    // - all other trxs across the ranks block waiting for the msg for 50ms
    int counter = 0;    
    if (rank == 0) {
        auto& sender = trxs[0];
        const char* msg = "Hey from r0 trx0\0";
        cout << "r0-trx0 sent " <<
        sender.send(msg, 17, 1) << " bytes" << endl;
        // Iterate through all other transceivers on rank 0 to see if they've
        // received the message.
        char* rcvd_msg;
        for (size_t i = 1; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            size_t bytes = t.recv(&rcvd_msg, RECV_TIMEOUT_MS);
            if (bytes == 17) { // If the message was received, count + 1.
                // Check that the message is only what was originally sent.
                assert(strcmp(rcvd_msg, "Hey from r0 trx0") == 0);
                assert(t.recv(&rcvd_msg, 0) == 0);
                counter++;
            } else { // Check that no information was received.
                assert(bytes == 0);
            }
        }
        // Check that the sender never received its own message.
        assert(sender.recv(&rcvd_msg, 0) == 0);
    } else { // All other ranks should have received the message.
        for (size_t i = 0; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            char* rcvd_msg;
            // Receive data within timeout bounds.
            size_t bytes = t.recv(&rcvd_msg, RECV_TIMEOUT_MS);
            if (bytes == 17) { // Received message.
                assert(strcmp(rcvd_msg, "Hey from r0 trx0") == 0);
                assert(t.recv(&rcvd_msg, 0) == 0);
                counter++;
            } else { // Check that no information was received.
                assert(bytes == 0);
            }
        }
    }

    // Record rank's total # of trxs that received the msg.
    std::string filename = "rank_" + std::to_string(rank) + ".txt";
    std::ofstream file(filename);
    file << counter << std::endl;

    // Shuts down all transceivers.
    MPIRadioTransceiver<MAX_BUFFER_SIZE>::close_transceivers<NUM_TRXS>(trxs);

    // Synchronize MPI ranks.
    MPI_Finalize();
    return 0;
}
