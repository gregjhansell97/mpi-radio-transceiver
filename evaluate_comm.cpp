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



#define LATENCY 0 // ideal time delay between send and recv
#define SAMPLES 250 // number of comm is sampled

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


    auto trxs = RadioTransceiver::transceivers(2, LATENCY, THREADS_PER_BLOCK);
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
#ifdef TRX_COMM_EVALUATION_MODE
    ticks sum = 0;
#endif
    MPI_Barrier(MPI_COMM_WORLD); 
    ssize_t size;
    // each loop, sends must be synchronized
    for(size_t i = 0; i < SAMPLES; ++i) {
        MPI_Barrier(MPI_COMM_WORLD);
        if(rank == 0) {
            const char* msg = "comm evaluation\0";
            t1.send(msg, 16, 0); 
#ifdef TRX_COMM_EVALUATION_MODE
            ticks start;
            MPI_Status _status;
            MPI_Recv(&start, sizeof(ticks), MPI_BYTE,
                    0, 1, MPI_COMM_WORLD, &_status);
            for(int i = 0; i < num_ranks; ++i) {
                ticks end;
                MPI_Recv(&end, sizeof(ticks), MPI_BYTE, 
                        i, 2, MPI_COMM_WORLD, &_status);
                sum += (end - start); // sum difference
            }
#endif
        } else {
            char* msg;
            size = t1.recv(&msg, 1);
            assert(size == 16);
        }
        char* msg;
        size = t2.recv(&msg, 1);
        assert(size == 16);
        // just flush out as many messages as possible
    }

    RadioTransceiver::close_transceivers(trxs);
    MPI_Barrier(MPI_COMM_WORLD);
#ifdef TRX_COMM_EVALUATION_MODE
    if(rank == 0) {
        double average = 0;
        average = (((double)(sum))/(SAMPLES*num_ranks));
        // iterate through and grab info from rank 0
        cout << "average mpi time: " << average << endl;
    }
#endif

    if(rank == 0) cout << "done with evaluate_comm" << endl;
    MPI_Finalize();
    return 0;
}



