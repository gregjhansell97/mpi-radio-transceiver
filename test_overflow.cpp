#include "mpi.h"
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "mpi_radio_transceiver.hpp"


using std::cout;
using std::cerr;
using std::endl;


#define BUFFER_SIZE 16 // buffer gets to full messages dropped
#define PACKET_SIZE 32 // dont send data past this size
#define LATENCY 10 // 10 millisecond pause

int main(int argc, char** argv) {
    // Initialize MPI Environment
    if(!MPI_WTIME_IS_GLOBAL) {
        cerr << "Unable to initialize, wall clock time needs to be global" << endl;
        return 1;
    }
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

    // create two transceivers
    auto trxs = MPIRadioTransceiver<
        BUFFER_SIZE,
        PACKET_SIZE,
        LATENCY>::transceivers<2>();
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    // set initial values for transceivers, all transceivers in range
    auto& t0 = trxs[0];
    t0.set_x(0);
    t0.set_y(0);
    t0.set_send_range(1);
    t0.set_recv_range(1);
    auto& t1 = trxs[1];
    t1.set_x(0);
    t1.set_y(0);
    t1.set_send_range(1);
    t1.set_recv_range(1);

    // ENSURES: ranks are done with adjusting their t0/t1 parameters
    MPI_Barrier(MPI_COMM_WORLD); 
    
    char* rcvd;
    if(rank == 0) {
        for(char i = 0; i < BUFFER_SIZE; ++i) {
            t0.send(&i, 1, 0);
        }
        char overflow = 'a';
        t0.send(&overflow, 1, 0);
    }
    // must wait for rank 0 to finish (pile up the buffers)
    MPI_Barrier(MPI_COMM_WORLD); 
    for(char i = 0; i < BUFFER_SIZE; ++i) {
        if(rank != 0) {
            assert(t0.recv(&rcvd, 1) == 1);
            assert(*rcvd == i);
        }
        ssize_t s = t1.recv(&rcvd, 1);
        //if(rank == 0) std::cerr << (int)(*rcvd) <<  " == " << (int)i << std::endl;
        assert(s == 1);
        assert(*rcvd == i);
    }
    assert(t0.recv(&rcvd, 0.5) == 0);
    assert(t1.recv(&rcvd, 0.5) == 0);


    // Shuts down all the transceivers
    MPIRadioTransceiver<
        BUFFER_SIZE,
        PACKET_SIZE,
        LATENCY>::close_transceivers<2>(trxs);

    MPI_Finalize();
    return 0;
}



