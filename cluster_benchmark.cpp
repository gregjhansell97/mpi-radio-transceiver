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
#define NUM_CLUSTERS 8
#define NUM_THREADS_PER_BLOCK 32

static void spin_cluster(
        queue<RadioTransceiver*>* q,
        condition_variable* on_start,
        bool* sending,
        double* latency,
        int rank) {

    assert(q->size() > 1);
    mutex mtx;
    unique_lock<mutex> lk(mtx);
    on_start->wait(lk, [sending]{ return *sending; });
    if(rank == 0) cerr << "STARTING" << endl; 
    char* msg;
    RadioTransceiver* t;
    ssize_t s;
    //-------------------------------------------------------------------------
    //TIME STARTS HERE
    //RadioTransceiver* head = q->front();
    const double start = MPI_Wtime();
    // every loop around send a new message
    if(rank == 0) q->front()->send("front", 5, 0);
    while(q->size() > 1) { // one remaining is the sender
        t = q->front();
        s = t->recv(&msg, 0);
        q->pop();
        if(s == 0) { // didn't get a message yet
            q->push(t); // add back to end
        }
    }
    *latency = (MPI_Wtime() - start);
    //-------------------------------------------------------------------------
}

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
        cout << "starting cluster benchmark (num-trxs per rank: " << NUM_TRXS
             << ", thread-per-block: " << NUM_THREADS_PER_BLOCK 
             << ", clusters: "<< NUM_CLUSTERS << ", ranks: " << num_ranks 
             << ")" << endl;
    }

    auto trxs = RadioTransceiver::transceivers(
            NUM_TRXS, LATENCY, NUM_THREADS_PER_BLOCK);

    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }


    queue<RadioTransceiver*> qs[NUM_CLUSTERS];
    for(size_t i = 0; i < NUM_TRXS; ++i) {
        // each cluster is isolated
        auto& t = trxs[i];
        t.device_data->x = i%NUM_CLUSTERS; 
        t.device_data->y = 0.0;
        t.device_data->send_range = 0.1;
        t.device_data->recv_range = 0.1;
        qs[i%NUM_CLUSTERS].push(&t);
    }

    double latencies[NUM_CLUSTERS];
    condition_variable start;
    bool sending = false;
    // create thread
    vector<thread> threads;
    for(size_t c = 0; c < NUM_CLUSTERS; ++c) {
        threads.emplace_back(
                spin_cluster,
                &qs[c],
                &start,
                &sending,
                &latencies[c],
                rank);
    }


    MPI_Barrier(MPI_COMM_WORLD);
    sending = true;
    start.notify_all();


    for(auto& t : threads) {
        t.join();
    }

    cerr << "joined threads" << endl;

    RadioTransceiver::close_transceivers(trxs);
    
    cerr << "closed transceivers" << endl;
    double sum = 0; 
    for(size_t c = 0; c < NUM_CLUSTERS; ++c) {
        sum += latencies[c];
    }
    double local_cluster_average = sum/NUM_CLUSTERS;
    double global_sum_cluster_average;
    MPI_Reduce(
            &local_cluster_average, // local global cluster
            &global_sum_cluster_average, 
            1, // # of obj sent
            MPI_DOUBLE, // datatype sent
            MPI_SUM, // operation
            0, // rank 0 collects data
            MPI_COMM_WORLD);
    if(rank == 0) {
        const double global_average = global_sum_cluster_average/num_ranks;
        cout << "average: " << global_average << endl;
    }

    if(rank == 0) cout << "done with cluster benchmark" << endl;
    MPI_Finalize();
    return 0;
}



