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


#define BUFFER_SIZE 2048 // buffer gets to full messages dropped
#define PACKET_SIZE 32 // dont send data past this size
#define LATENCY 0 // ideal time delay between send and recv

int main(int argc, char** argv) {
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

    // set initial values for transceivers
    auto& t0 = trxs[0];
    t0.set_x(0);
    t0.set_y(0);
    // puts t1 & t2 out of range
    t0.set_send_range(0.25);
    t0.set_recv_range(0.25);

    auto& t1 = trxs[1];
    t1.set_x(1);
    t1.set_y(1);
    // puts t1 & t2 out of range
    t1.set_send_range(0.25);
    t1.set_recv_range(0.25);

    // ENSURES: ranks are done with adjusting their t0/t1 parameters
    MPI_Barrier(MPI_COMM_WORLD); 

    if(rank == 0) {
        const char* msg0 = "hello 0's\0";
        const char* msg1 = "hello 1's\0";
        cout << "rank0-t0 sent " << 
        t0.send(msg0, 10, 100) << " bytes" << endl; //(msg, size, timeou)
        cout << "rank0-t1 sent " << 
        t1.send(msg1, 10, 100) << " bytes" << endl;
    } else {
        char* msg;
        // 1000 millisecond timeout
        ssize_t size = t0.recv(&msg, 1000);
        cout << "rank" << rank << "-t0 received " << msg << endl;
        size = t1.recv(&msg, 1000);
        cout << "rank" << rank << "-t1 received " << msg << endl;
    }

    // Shuts down all the transceivers
    MPIRadioTransceiver<
        BUFFER_SIZE,
        PACKET_SIZE,
        LATENCY>::close_transceivers<2>(trxs);

    MPI_Finalize();
    return 0;
}



