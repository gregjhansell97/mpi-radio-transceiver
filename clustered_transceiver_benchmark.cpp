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
std::mutex m;
std::condition_variable cv;
bool ready = false;

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
// All of rank 0's clusters' trx[0] sends out a message
void cluster_benchmark(
    std::promise<double> && elapsed_time,
    size_t rank, // rank this cluser belongs to
    size_t cluster, // cluster this trx thread belongs to
    size_t i, // cluster's ith trx
    size_t index, // global trx index
    RadioTransceiver* trx) { // trx in question
    try {
        std::pair<double, double> loc;
        auto& t = *trx;
        // setting params for trx on this thread's cluster
        loc = generate_point(cluster, cluster, .4);
        t.device_data->x = loc.first;
        t.device_data->y = loc.second;
        t.device_data->send_range = .3;
        t.device_data->recv_range = .3;

        // repeatedly send messages of bytes to those in its cluster
        // until the max buffer size is reached.
        if (rank == 0 && i == 0) {
            std::cout << "sending" << std::endl;
            size_t msgs_sent;
            for (msgs_sent = 0; msgs_sent < TRX_BUFFER_SIZE/sizeof(double);
            ++msgs_sent) {
                const char* msg = std::to_string(MPI_Wtime()).c_str();
                t.send(msg, sizeof(double), 0.0);
            }
            cout << "cluster sent " << msgs_sent << endl;
        } else { // otherwise, wait to receive msgs from its rank0 cluster
            // need to a wait condition variable that starts as soon as rank0's clusters
            // send msg
            std::this_thread::sleep_for(std::chrono::microseconds(1)); // subtract this off later
            char* raw_msg; // tmp holder for received message
            size_t rcvd_msgs = 0; // keep track of cluster's total rcvd messages
            while (rcvd_msgs != TRX_BUFFER_SIZE/sizeof(double)) {
                if (t.recv(&raw_msg, 0.001) == 8) {
                    rcvd_msgs++;
                }
            }
            std::cout<< "index " << index << " " << " done" << endl;
            elapsed_time.set_value(MPI_Wtime() - atof(raw_msg));
        }
    }
    catch(std::exception& e)
    {
        std::cout << e.what() << std::endl;
    }
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
    if (argc == 6 && (file_io = atoi(argv[5]) == 0)) { // file i/o needed
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

    // create num_clusters clusters, 1 trx per each thread
    // RadioTransceiver* clusters[num_clusters];
    auto trxs = RadioTransceiver::transceivers(
        num_clusters * num_trxs, latency, num_threads_per_block,
        send_file_ptr, recv_file_ptr
    );
    if (trxs == nullptr) { // Unable to retrieve transceivers.
        std::cerr << "Couldn't retrieve trxs" << std::endl;
        MPI_Finalize();
        return -1;
    }
    std::vector<std::thread> th; // this rank's cluster's threads
    std::promise<double> promises[num_clusters * num_trxs]; 
    std::future<double> th_elapsed_times[num_clusters * num_trxs]; // when trxs finish
    for (size_t i = 0; i < num_clusters * num_trxs; ++i) {
        th_elapsed_times[i] = promises[i].get_future();
    }

    // max time it took for ith cluster to finish
    double elapsed_times[num_clusters] = {0};
    for (size_t i = 0; i < num_clusters; ++i) {
        for (size_t j = 0; j < num_trxs; ++j) {
            size_t index = i * j;
            th.emplace_back(std::thread(
                cluster_benchmark,
                std::move(promises[index]),
                rank, // this rank
                i, // which cluster this trx belongs to
                j, // cluster i's jth trx
                index,
                &trxs[index] // the trx this thread is for
            ));
        }
    }
    for (size_t i = 0; i < num_clusters; ++i) {
        double max_elapsed_time = -1.0;
        for (size_t j = 0; j < num_trxs; ++j) {
            th.at(i * j).join();
            double curr_time = th_elapsed_times[i*j].get();
            if (curr_time > max_elapsed_time) {
                max_elapsed_time = curr_time;
            }
        }
        elapsed_times[i] = max_elapsed_time;
    }

    std::cout << "rank " << rank << " done" << endl;
    // wait for all ranks to finish before cleaning up threads
    MPI_Barrier(MPI_COMM_WORLD);
    std::cout << "ALL THREADS JOINED" << std::endl;
    // This clustering transceiver benchmark tests as follows:
    // - each cluster has N numbers of trxs (N = total # of trxs/X)
    // - 1 trxs/cluster transmits msgs accummulating to the max buffer size
    // - wait for how long it takes for all trxs to receive all the msgs
    // - reduces and spits out average latency time across clusters
    double global_latencies[num_clusters];   // latencies later
    // send local end times to rank 0
    for (size_t i = 0; i < num_clusters; ++i) {
        MPI_Reduce(
            &elapsed_times[i], // sent var cluster i's max elapsed time
            &global_latencies[i], // global rcv var
            1,  // # of obj sent
            MPI_DOUBLE, // datatype sent
            MPI_MAX, // operation
            0, // rank 0 collects data
            MPI_COMM_WORLD
        );
    }

    // calculate and record all elapsed times across clusters
    if (rank == 0) {
        for (size_t i = 0; i < num_clusters; ++i) {
            std::cout << "Cluster " << i << " took " <<
            global_latencies[i]<<
            " s to receive all msgs" << endl;
        }
    }

    // Shuts down all transceivers in all clusters/threads and join threads
    RadioTransceiver::close_transceivers(trxs);
    // shut down all threads
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
