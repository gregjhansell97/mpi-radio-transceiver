/*
File: clustered_transceiver_benchmark.cpp
Description:
  A performance benchmark of the MPI Radio Transceiver's performance in a
  cluster of transceivers.
Use: run.exe <# of trxs/cluster> <latency>
            <# of threads/block> (OPTIONAL: <file-I/O flag = 1>)
*/

// C/C++libraries
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

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

// Creates isolated clusters of transceivers.
// Each transceiver is given a location located with radius .4 of location
// (t_id, t_id), and has a send/recv distance of .8.
// All ith transceivers are in the same cluster as each other across ranks.
void create_clusters(std::promise<RadioTransceiver*> && prms,
    size_t num_trxs, size_t num_threads_per_block,
    double latency, MPI_File* send_file_ptr = nullptr,
    MPI_File* recv_file_ptr = nullptr) {
    auto trxs = RadioTransceiver::transceivers(
        num_trxs, latency, num_threads_per_block,
        send_file_ptr, recv_file_ptr
    );
    std::pair<double, double> loc;
    for (size_t i = 0; i < num_trxs; ++i) {
        auto& t = trxs[i];
        // setting params for trx on cluster i
        loc = generate_point(i, i, .4);
        t.device_data->x = loc.first;
        t.device_data->y = loc.second;
        t.device_data->send_range = .3;
        t.device_data->recv_range = .3;
    }
    prms.set_value(trxs);
}

int main(int argc, char** argv) {
    // make sure all arguments exist
    if (argc != 5 && argc != 6) {
        std::cout << "Usage: \
        run.exe <# of trxs/cluster> \
        <latency> <# of threads/block> <# of clusters/rank>\
        <optional: file-I/O? (0 or 1)>" <<
        std::endl;
        return 1;
    }

    // Initialize MPI/CUDA environment variables.
    size_t num_trxs = atoi(argv[1]); // number of trxs per cluster
    size_t max_buffer_size = TRX_BUFFER_SIZE;
    size_t max_packet_size = TRX_PACKET_SIZE;
    size_t latency = atof(argv[2]); // double
    size_t num_threads_per_block = atoi(argv[3]); // later, for CUDA
    size_t num_clusters = atoi(argv[4]); // # clusters/rank
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
    if (argc == 5 && (file_io = atoi(argv[4]) == 0)) { // file i/o needed
        // set file name based on the current config
        std::string send_file_name = "send_logs_trxs=" + std::to_string(num_trxs) \
         + "_buff=" + std::to_string(max_buffer_size) + "_packet=" + \
         std::to_string(max_packet_size) + "_latency=" + std::to_string(latency) \
        + "_#threads_per=" + std::to_string(num_threads_per_block) + ".out";
        std::string recv_file_name = "recv_logs_trxs=" + std::to_string(num_trxs) \
         + "_buff=" + std::to_string(max_buffer_size) + "_packet=" + \
         std::to_string(max_packet_size) + "_latency=" + std::to_string(latency) \
        + "_#threads_per=" + std::to_string(num_threads_per_block) + ".out";
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

    // create num_clusters clusters, 1 per each thread
    std::vector<RadioTransceiver*> clusters;
    std::thread th[num_clusters]; // keep track of the rank's threads
    for (size_t i = 0; i < num_clusters; ++i) {
        // grab this thread's cluster of trxs
        std::promise<RadioTransceiver*> prms;
        auto ftr_trxs = prms.get_future();

        th[i] = std::thread(&create_clusters, std::move(prms), num_trxs,
        num_threads_per_block, latency, send_file_ptr, recv_file_ptr);
        auto trxs = ftr_trxs.get();
        if (trxs == nullptr) { // Unable to retrieve transceivers.
            MPI_Finalize();
            return 1;
        }
        // std::thread::id curr_id = th[i].get_id
        clusters.push_back(trxs);
    }

    // Ensures all clusters of trxs in all ranks are ready before test starts.
    MPI_Barrier(MPI_COMM_WORLD);

    // // This clustering transceiver benchmark tests as follows:
    // // - create X isolated clusters specified in the CLI args
    // // - each cluster has N numbers of trxs (N = total # of trxs/X)
    // // - 1 trxs/cluster transmits msgs accummulating to the max buffer size
    // // - wait for how long it takes for all trxs to receive all the msgs
    // // - reduces and spits out average latency time across clusters
    // double global_start_time[num_trxs]; // used to calculate
    // double global_end_time[num_trxs];   // latencies later
    // double local_end_time[num_trxs] = {0.0}; // end time for each rank's clusters
    // if (rank == 0) { // each cluster's rank0 trx sends out 1 msg
    //     const char* msg = "Hey from rnk0";
    //     // keep sending messages until trx buffers are maxed out
    //     // max # of messages that can be sent is buffer_size/msg_size
    //     for (size_t i = 0; i < num_trxs; ++i) {
    //         auto& t = trxs[i];
    //         for (size_t msgs_sent = 0; msgs_sent < max_buffer_size/14;
    //         ++msgs_sent) {
    //             t.send(msg, 14, 0.1);
    //         }
    //         cout << "Sent " << msg << " 14 bytes" << endl;
    //         global_start_time[i] = MPI_Wtime(); // start time of cluster i
    //     }
    // } else { // all other ranks iterate through clusters
    //     char* raw_msg; // tmp holder for rcvd msg
    //     size_t rcvd_msgs[num_trxs] = {0}; // keep track of msgs rcvd/cluster i
    //     size_t total_msgs_sent = max_buffer_size/14;
    //     size_t local_rcvd_msgs = 0;
    //     while (local_rcvd_msgs != total_msgs_sent * num_trxs) {
    //         for (size_t i = 0; i < num_trxs; ++i) {
    //             if (rcvd_msgs[i] != total_msgs_sent) {
    //                 auto& t = trxs[i];
    //                 if (t.recv(&raw_msg, 0.1) == 14) {
    //                     cout << "rank" << rank << "-clus" << i << " rcvd " <<
    //                     raw_msg << endl;
    //                     rcvd_msgs[i]++;
    //                     cout << ", " << rcvd_msgs[i] << " total" << endl;
    //                     local_rcvd_msgs++;
    //                     if (rcvd_msgs[i] == total_msgs_sent) {
    //                         // record cluster's end time
    //                         local_end_time[i] = MPI_Wtime();
    //                         cout << "rank" << rank << "-clus" << i <<
    //                         "finished" << endl;
    //                     }
    //                 }
    //             }
    //         }
    //     }
    // }
    
    // // send local end times to rank 0
    // for (size_t i = 0; i < num_trxs; ++i) {
    //     MPI_Reduce(
    //         &local_end_time[i], // sent var
    //         &global_end_time[i], // global rcv var
    //         1,  // # of obj sent
    //         MPI_DOUBLE, // datatype sent
    //         MPI_MAX, // operation
    //         0, // rank 0 collects data
    //         MPI_COMM_WORLD
    //     );
    // }

    // // calculate and record all elapsed times across clusters
    // if (rank == 0) {
    //     for (size_t i = 0; i < num_trxs; ++i) {
    //         std::cout << "Cluster " << i << " took " <<
    //         global_end_time[i] - global_start_time[i] <<
    //         " s to receive all msgs" << endl;
    //     }
    // }

    // Shuts down all transceivers in all clusters/threads
    for (size_t i = 0; i < num_clusters; ++i) {
        RadioTransceiver::close_transceivers(clusters.at(i));
    }

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
