#include "mpi.h"
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "mpi_radio_transceiver.hpp"


using std::cout;
using std::endl;


#define LOCATION_OFFSET 0.02
#define NUM_TRXS 1
#define MAX_BUFFER_SIZE 2048

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

    // each context is one away from i
    // VISUALIZATION:
    // x-location:  0.....1.....2......3......4
    //            t[0]  t[1]  t[2]   t[3]   t[4]
    // each transceiver is '1' unit away from the others
    auto trxs = MPIRadioTransceiver<MAX_BUFFER_SIZE>::transceivers<NUM_TRXS>();
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    for(int i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // setting parameters for t
        t.set_x(i + 0.01);
        t.set_y(0.0);
        t.set_send_duration(0.0);
        t.set_recv_duration(0.0);
        t.set_send_range(0.25);
        t.set_recv_range(0.25);
    }

    // ENSURES: all transceivers done with adjusting their locations
    MPI_Barrier(MPI_COMM_WORLD); 

    // This Test selects moves around the first transceiver index and sees if
    // where it broadcasts changes
    if(rank == 0) {
        char m = 'h';
        trxs[0].send(&m, 1, 0);
        char* j = &m;
        trxs[0].recv(&j, 1000); // wait for 1 second, TODO switch to float
        cout << j[0] << endl;
        /*
        auto& t = trxs[0];
        for(int i = 0; i < NUM_TRXS; ++i) {
            const char* msg = (char*)(&i);
            // send message with size, and a timeout of 0 
            assert(t.send(msg, sizeof(i), 1) == sizeof(i));
            // shift x to another location and publish again
            t.set_x(i + LOCATION_OFFSET); 
        }
        // go through others and verify that they received a message
        for(int i = 1; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            char* raw_msg;
            // receive data with a certain timeout
            assert(t.recv(&raw_msg, 1) == sizeof(i));
            int msg = *(int*)(raw_msg);
            // message received better be only i
            assert(msg == i);
            // no more data to receive
            assert(t.recv(&raw_msg, 0) == 0);
        }*/
    } else { // another rank make sure messages get received
        char p = 'a';
        char* m = &p;
        trxs[0].recv(&m, 1000);
        cout << m[0] << endl;
        /*
        for(int i = 0; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            char* raw_msg;
            // receive data with a certain timeout
            assert(t.recv(&raw_msg, 1) == sizeof(i));
            int msg = *(int*)(raw_msg);
            // message received better be only i
            assert(msg == i);
            // no more data to receive
            assert(t.recv(&raw_msg, 0) == 0);
        }*/
    }

    // may need sleep if operations are not blocking and not done yet
    //std::this_thread::sleep_for(std::chrono::seconds(1));
    MPIRadioTransceiver<MAX_BUFFER_SIZE>::close_transceivers<NUM_TRXS>(trxs);


    MPI_Finalize();
    return 0;
}



