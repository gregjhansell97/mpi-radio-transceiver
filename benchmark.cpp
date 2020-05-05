#include "mpi.h"
#include <stdlib.h>
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <queue>
#include <random>

#include "radio_transceiver.h"


using std::cout;
using std::cerr;
using std::endl;
using std::thread;
using std::condition_variable;
using std::queue;
using std::mutex;
using std::unique_lock;
using std::vector; 


#define NUM_TRXS 16384
#define LATENCY 0 // ideal time delay between send and recv
#define NUM_THREADS_PER_BLOCK 32

int main(int argc, char** argv) {
    if(!MPI_WTIME_IS_GLOBAL) {
        cerr << "Unable to initialize, wall clock time needs to be global" << endl;
        return 1;
    }
    // Initialize MPI Environment
    int provided_thread_support;
    int ret = MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided_thread_support);
    if(ret != 0 || provided_thread_support != MPI_THREAD_MULTIPLE) {
        cout << "Unable to initialize the MPI execution environment" << endl;
        return 1;
    }

    // GRAB MPI SPECS
    int rank = 0;
    if(MPI_Comm_rank(MPI_COMM_WORLD, &rank)) {
        cout << "Unable to retrieve the current rank's ID" << endl;
        return 1;
    }
    int num_ranks;
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);

    if(rank == 0) {
        cout << "starting benchmark (num-trxs per rank: " << NUM_TRXS
             << ", thread-per-block: " << NUM_THREADS_PER_BLOCK 
             << ", ranks: " << num_ranks 
             << ")" << endl;
    }

    auto trxs = RadioTransceiver::transceivers(
            NUM_TRXS, LATENCY, NUM_THREADS_PER_BLOCK);

    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    queue<RadioTransceiver*> q;
    for(size_t i = 0; i < NUM_TRXS; ++i) {
        RadioTransceiver* t = &trxs[i];
        t->device_data->x = 0.0;  // in within communicatin range
        t->device_data->y = 0.0;
        t->device_data->send_range = 1;
        t->device_data->recv_range = 1;
        q.push(t);
    }

    ssize_t s; 
    RadioTransceiver* t;
    char* msg;
    MPI_Barrier(MPI_COMM_WORLD);
    // start benchmark
    const double start = MPI_Wtime();
    if(rank == 0) q.front()->send("front", 5, 0);
    while(q.size() > 1) { // one remaining is the sender
        t = q.front();
        s = t->recv(&msg, 0);
        q.pop();
        if(s == 0) { // didn't get a message yet
            q.push(t); // add back to end
        }
    }
    double latency = (MPI_Wtime() - start);

    RadioTransceiver::close_transceivers(trxs);

    double total_latency;
    MPI_Reduce(
            &latency, // local global cluster
            &total_latency, 
            1, // # of obj sent
            MPI_DOUBLE, // datatype sent
            MPI_SUM, // operation
            0, // rank 0 collects data
            MPI_COMM_WORLD);
    if(rank == 0) {
        const double average_latency = total_latency/num_ranks;
        cout << "average: " << average_latency << endl;
    }

    if(rank == 0) cout << "done with benchmark" << endl;
    MPI_Finalize();
    return 0;
}



