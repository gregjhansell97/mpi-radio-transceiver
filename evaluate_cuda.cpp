#include "mpi.h"
#include <stdlib.h>
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "radio_transceiver.h"


using std::cout;
using std::cerr;
using std::endl;
using std::thread;


#define LATENCY 0 // ideal time delay between send and recv
#define SAMPLES 100 // number of comm is sampled
#define NUM_TRXS 16384
#define THREADS_PER_BLOCK 32

static void flush_recvs(
        RadioTransceiver* trxs, size_t start, size_t end, bool* running) {
    char* msg;
    while(*running) {
        for(size_t i = start; i < end; ++i) {
            // keep receiving till you can't 
            while(trxs[i].recv(&msg, 0) > 0); 
        }
    }
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
    if(num_ranks > 1) {
        cout << "this evaluation is for cuda, not mpi. We only want one" 
             << "rank!" << endl;
        MPI_Finalize();
        return 1;
    }

    // take in arguments
    const size_t num_trxs = NUM_TRXS; //atoi(argv[1]);
    const size_t num_threads_per_block = THREADS_PER_BLOCK; //atoi(argv[2]);
    if(rank == 0) {
        cout << "starting evaluate_cuda (num-trxs: " << num_trxs 
             << ", thread-per-block: " << num_threads_per_block << ")" << endl;
    }

    auto trxs = RadioTransceiver::transceivers(num_trxs, LATENCY, num_threads_per_block);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }


    // everything is in communcation with each other
    for(size_t i = 0; i < num_trxs; ++i) {
        auto& t = trxs[i];
        // setting parameters for t
        t.device_data->x = 0.0;
        t.device_data->y = 0.0;
        t.device_data->send_range = 0.25;
        t.device_data->recv_range = 0.25;
    }

    bool running = true;
    thread helper1(flush_recvs, trxs, 0, num_trxs/4, &running);
    thread helper2(flush_recvs, trxs, num_trxs/4, num_trxs/2, &running);
    thread helper3(flush_recvs, trxs, num_trxs/2, 3*num_trxs/4, &running);
    thread helper4(flush_recvs, trxs, 3*num_trxs/4, num_trxs, &running);

#ifdef TRX_CUDA_EVALUATION_MODE
    ticks sum = 0;
    ticks diff;
    MPI_Status _status;
#endif
    MPI_Barrier(MPI_COMM_WORLD); 
    for(size_t i = 0; i < SAMPLES; ++i) {
        const char* msg = "cuda evaluation\0";
        trxs[0].send(msg, 16, 0);
        // get diff sample
#ifdef TRX_CUDA_EVALUATION_MODE
        MPI_Recv(&diff, sizeof(ticks), MPI_BYTE,
                    0, 3, MPI_COMM_WORLD, &_status);
        sum += diff;
# endif
    }
    running = false;
    helper1.join();
    helper2.join();
    helper3.join();
    helper4.join();

    RadioTransceiver::close_transceivers(trxs);

    // after closing the transceivers grab timing information from mpi


#ifdef TRX_CUDA_EVALUATION_MODE
    double average = ((double)sum)/SAMPLES;
    // iterate through and grab info from rank 0
    if(rank == 0) cout << "average cuda time: " << average << endl;
#endif

    if(rank == 0) cout << "done with evaluate_cuda" << endl;
    MPI_Finalize();
    return 0;
}



