#include "mpi.h"
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


#define BUFFER_SIZE 2048 // buffer gets to full messages dropped
#define PACKET_SIZE 16 // dont send data past this size
#define LATENCY 0 // ideal time delay between send and recv
#define SAMPLES 1000 // number of comm is sampled

#define THREADS_PER_BLOCK 1 // cuda is not the conscern here

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
    if(rank == 0) cout << "starting evaluate_comm" << endl;

    auto trxs = RadioTransceiver::transceivers(
            2, BUFFER_SIZE, PACKET_SIZE, LATENCY, THREADS_PER_BLOCK);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }
    auto& t1 = trxs[0];
    // setting parameters for t1
    t1.device_data->x = 0.0;
    t1.device_data->y = 0.0;
    t1.device_data->send_range = 1;
    t1.device_data->recv_range = 1;
    auto& t2 = trxs[1];
    // setting parameters for t2
    t2.device_data->x = 0.0;
    t2.device_data->y = 0.0;
    t2.device_data->send_range = 1;
    t2.device_data->recv_range = 1;

    // ENSURES: all transceivers done with adjusting their locations
    #ifdef HMAP_COMM_EVALUATION
    ticks sum = 0;
    #endif
    MPI_Barrier(MPI_COMM_WORLD); 
    ssize_t size;
    for(size_t i = 0; i < SAMPLES; ++i) {
        if(rank == 0) {
            const char* msg = "comm evaluation\0";
            t1.send(msg, 16, 0); 
            // collection
            #ifdef HMAP_COMM_EVALUATION
            ticks diff;
            MPI_Status _status;
            MPI_Recv(&diff, sizeof(ticks), MPI_BYTE, 
                        0, 2, MPI_COMM_WORLD, &_status);
            sum += diff;
            #endif
        } else {
            // just flush out as many messages as possible
            char* msg;
            size = t1.recv(&msg, 0);
            while(size != 0) {
                size = t1.recv(&msg, 0);
            }
        }
        // just flush out as many messages as possible
        char* msg;
        size = t2.recv(&msg, 0);
        while(size != 0) {
            size = t1.recv(&msg, 0);
        }
    }

    RadioTransceiver::close_transceivers(trxs);

    // after closing the transceivers grab timing information from mpi

    #ifdef HMAP_COMM_EVALUATION
    double average = sum/SAMPLES;
    // iterate through and grab info from rank 0
    if(rank == 0) cout << "average mpi time: " << average << endl;
    #endif

    if(rank == 0) cout << "done with evaluate_comm" << endl;
    MPI_Finalize();
    return 0;
}



