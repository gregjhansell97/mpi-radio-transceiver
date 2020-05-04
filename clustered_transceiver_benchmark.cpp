/*
File: clustered_transceiver_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance in a
  cluster of transceivers.
Use: run.exe <# of trxs/cluster> <max buffer size> <max packet size> <latency>
            <# of threads/block> <# of clusters>
            (<file-I/O flag = 1>
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
#include "radio_transceiver.h"

using std::cout;
using std::cerr;
using std::endl;

#define _USE_MATH_DEFINES

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
    // make sure all arguments exist
    if (argc != 5 && argc != 6) {
        std::cout << "Usage: \
        run.exe <# of trxs/cluster> \
        <latency> <# of threads/block> <# of clusters> \
        <optional: file-I/O? (0 or 1)>" <<
        std::endl;
        return 1;
    }

    // Initialize MPI/CUDA environment variables.
    size_t num_trxs = atoi(argv[1]); // number of trxs per cluster
    size_t max_buffer_size = TRX_BUFFER_SIZE; //atoi(argv[2]);
    size_t max_packet_size = TRX_PACKET_SIZE; //atoi(argv[3]);
    size_t latency = atof(argv[2]); // double
    size_t num_threads_per_block = atoi(argv[3]);
    size_t num_clusters_per_thread = atoi(argv[4]);
    bool file_io = 1; // Default behavior is assume file i/o is false
    MPI_File send_file;
    MPI_File* send_file_ptr = &send_file;
    MPI_File recv_file;
    MPI_File* recv_file_ptr = &recv_file;

    // Initializes MPI environment.
    if (!MPI_WTIME_IS_GLOBAL) {
        std::cerr << "Unable to initialize, wall clock time needs to be \
        global" << std::endl;
        return 1;
    }

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

    // initialize the trxs depending on if file i/o is needed
    if (argc == 6 && (file_io = atoi(argv[5]) == 0)) { // file i/o needed
        // set file name based on the current config
        std::string send_file_name = "send_logs_trxs=" + std::to_string(num_trxs) \
         + "_buff=" + std::to_string(max_buffer_size) + "_packet=" + \
         std::to_string(max_packet_size) + "_latency=" + std::to_string(latency) \
        + "_#threads_per=" + std::to_string(num_threads_per_block) + "#clusters=" + \
        std::to_string(num_clusters_per_thread) + ".out";
        std::string recv_file_name = "recv_logs_trxs=" + std::to_string(num_trxs) \
         + "_buff=" + std::to_string(max_buffer_size) + "_packet=" + \
         std::to_string(max_packet_size) + "_latency=" + std::to_string(latency) \
        + "_#threads_per=" + std::to_string(num_threads_per_block) + "#clusters=" + \
        std::to_string(num_clusters_per_thread) + ".out";
        MPI_File_open(
            MPI_COMM_WORLD,
            send_file_name.c_str(),
            MPI_MODE_CREATE|MPI_MODE_WRONLY,
            MPI_INFO_NULL,
            &send_file
        );
        MPI_File_open(
            MPI_COMM_WORLD,
            recv_file_name.c_str(),
            MPI_MODE_CREATE|MPI_MODE_WRONLY,
            MPI_INFO_NULL,
            &recv_file
        );
    } else {
        send_file_ptr = nullptr;
        recv_file_ptr = nullptr;
    }
    auto trxs = RadioTransceiver::transceivers(
        num_trxs, latency, num_threads_per_block, send_file_ptr, recv_file_ptr
    );
    if (trxs == nullptr) { // Unable to retrieve transceivers.
        MPI_Finalize();
        return 1;
    }

    // Initialize the transceivers.
    // Each transceiver is given a location located with radius .5 of location
    // (1, 1), and has a send/recv distance of .9.
    // All transceivers across all ranks are within communication range of each
    // other.
    std::pair<double, double> loc;
    for (size_t i = 0; i < num_trxs; ++i) {
        auto& t = trxs[i];
        // Setting parameters for transceiver t.
        loc = generate_point(rank, rank, .4);
        t.device_data->x = loc.first;
        t.device_data->y = loc.second;
        t.device_data->send_range = .3;
        t.device_data->recv_range = .3;
    }

    // Ensures all transceivers in all ranks are ready before test starts.
    MPI_Barrier(MPI_COMM_WORLD);

    // This clustering transceiver benchmark tests as follows:
    // - create X isolated clusters specified in the CLI args
    // - each cluster has N numbers of trxs (N = total # of trxs/X)
    // - 1 trxs/cluster transmits msgs accummulating to the max buffer size
    // - wait for how long it takes for all trxs to receive all the msgs
    // - reduces and spits out average latency time across clusters

    size_t total_bytes_sent = 0;
    auto& sender = trxs[0];
    double start_time = MPI_Wtime(); // track how long it takes to rcv all msgs
    const char* msg = "Hey from trx0";
    // keep sending messages until trxs are maxed out
    while (total_bytes_sent < max_buffer_size) {
        sender.send(msg, 14, 0);
        total_bytes_sent += 14;
    }
    
    char* raw_msg;
    for (size_t i = 1; i < num_trxs; ++i) {
        auto& t = trxs[i];
        while (t.recv(&raw_msg, 0) == 14) {
            assert(strcmp(raw_msg, "Hey from trx0") == 0);
        }
    }

    // use for average latency calcs later
    double time_elapsed = MPI_Wtime() - start_time;
    double global_elapsed_time = 0.0;
    MPI_Reduce(
        &time_elapsed, &global_elapsed_time, 1, MPI_DOUBLE,
        MPI_SUM, 0, MPI_COMM_WORLD
    );

    // Record the world's total # of trxs that received the msg.
    if (rank == 0) {
        std::cout << global_elapsed_time/(double)num_ranks <<
        "(s)" << std::endl;
    }

    // Shuts down all transceivers.
    RadioTransceiver::close_transceivers(trxs);

    // Ensure fps are closed if not nullptr
    if (send_file_ptr != nullptr) {
        MPI_File_close(
            send_file_ptr
        );
        MPI_File_close(
            recv_file_ptr
        );
        send_file_ptr = nullptr;
        recv_file_ptr = nullptr;
    }

    // Synchronize MPI ranks.
    MPI_Finalize();
    return 0;
}
