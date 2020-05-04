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


#define NUM_TRXS 2048
#define LATENCY 0 // ideal time delay between send and recv
#define SAMPLES 100 // number of comm is sampled

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

    // take in arguments
    const size_t num_threads_per_block = atoi(argv[1]);


    if(rank == 0) {
        cout << "starting evaluate_cuda (" 
             << num_threads_per_block << ")" << endl;
    }

    auto trxs = RadioTransceiver::transceivers(NUM_TRXS, LATENCY, num_threads_per_block);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }


    // everything is in communcation with each other
    for(size_t i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // setting parameters for t
        t.device_data->x = 0.0;
        t.device_data->y = 0.0;
        t.device_data->send_range = 0.25;
        t.device_data->recv_range = 0.25;
    }
    // ENSURES: all transceivers done with adjusting their locations

#ifdef TRX_CUDA_EVALUATION_MODE
    ticks sum = 0;
    unsigned long long count = 0;
    ticks diff;
    MPI_Status _status;
    int have_messages;
#endif
    MPI_Barrier(MPI_COMM_WORLD); 
    ssize_t size;
    char* msg;
    for(size_t i = 0; i < SAMPLES; ++i) {
        if(rank == 0) {
            const char* msg = "cuda evaluation\0";
            trxs[0].send(msg, 16, 0); 
            // collection
#ifdef TRX_CUDA_EVALUATION_MODE
            MPI_Iprobe(MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &have_messages, &_status); 
            while(have_messages) {

                MPI_Recv(&diff, sizeof(ticks), MPI_BYTE, 
                            0, 3, MPI_COMM_WORLD, &_status);
                sum += diff;
                count++;

                MPI_Iprobe(MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &have_messages, &_status); 
            }
#endif
        } 
        // flushout transceivers
        for(size_t i = 0; i < NUM_TRXS; ++i) {
            size = trxs[i].recv(&msg, 0);
            while(size != 0) {
                size = trxs[i].recv(&msg, 0);
            }
        }
    }

    RadioTransceiver::close_transceivers(trxs);

    // after closing the transceivers grab timing information from mpi


#ifdef TRX_CUDA_EVALUATION_MODE
    double average = ((double)sum)/count;
    // iterate through and grab info from rank 0
    if(rank == 0) cout << "average cuda time: " << average << endl;
#endif

    if(rank == 0) cout << "done with evaluate_cuda" << endl;
    MPI_Finalize();
    return 0;
}



