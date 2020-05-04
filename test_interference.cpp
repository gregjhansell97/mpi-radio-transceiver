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
#define PACKET_SIZE 32 // dont send data past this size
#define LATENCY 0.5 // latency is half a second

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
    if(rank == 0) cout << "starting test_interference" << endl;

    // create two transceivers
    auto trxs = RadioTransceiver::transceivers(
            2, BUFFER_SIZE, PACKET_SIZE, LATENCY);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    // set initial values for transceivers, all transceivers in range
    auto& t0 = trxs[0];
    t0.device_data->x = 0;
    t0.device_data->y = 0;
    t0.device_data->send_range = 1;
    t0.device_data->recv_range = 1;
    auto& t1 = trxs[1];
    t1.device_data->x = 0;
    t1.device_data->y = 0;
    t1.device_data->send_range = 1;
    t1.device_data->recv_range = 1;

    // ENSURES: ranks are done with adjusting their t0/t1 parameters
    MPI_Barrier(MPI_COMM_WORLD); 
    
    // all ranks attempt to broadcast simultaneously
    const char* msg = "inteference\0";
    t0.send(msg, 12, 0);
    // check that t2 did not receive message 
    MPI_Barrier(MPI_COMM_WORLD);
    // they do it again
    t0.send(msg, 12, 0);
    MPI_Barrier(MPI_COMM_WORLD);
    char* rcvd; 
    assert(t1.recv(&rcvd, 1) == 0); // no message received
    assert(rcvd == nullptr);
    assert(t0.recv(&rcvd, 1) == 0); // no message received
    assert(rcvd == nullptr);


    // Shuts down all the transceivers
    RadioTransceiver::close_transceivers(trxs);

    if(rank == 0) cout << "test_interference success!" << endl;
    MPI_Finalize();
    return 0;
}



