#include "mpi.h"
#include <iostream>

#include "mpi_radio_transceiver.h"
#include <assert.h>


using std::cout;
using std::endl;


#define NUM_TRXS 1
#define MAX_BUFFER_SIZE 2048

int main(int argc, char** argv) {
    // Initialize MPI Environment
    int provided_thread_support;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided_thread_support);

    // GRAB MPI SPECS
    int world_rank = 0;
    //MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    //int world_size;
    //MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // CREATE CONTEXT: INITIAL LOCATIONS ARE ZERO
    MPIRadioTransceiver trxs[NUM_TRXS];
    char buffers[NUM_TRXS][MAX_BUFFER_SIZE];

    // each context is one away from i
    // VISUALIZATION:
    // x-location:  0.....1.....2......3......4
    //            t[0]  t[1]  t[2]   t[3]   t[4]
    // each transceiver is '1' unit away from the others
    for(int i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // setting parameters for t
        t.set_x(i + 0.01);
        t.set_y(0.0);
        t.set_send_duration(0.0);
        t.set_recv_duration(0.0);
        t.set_send_radius(0.25);
        t.set_recv_radius(0.25);
        t.set_buffer(buffers[i], MAX_BUFFER_SIZE);
    }

    if(!MPIRadioTransceiver::open_mpi_thread(trxs, NUM_TRXS)) {
        // could not open mpi in a separate thread
        MPI_Finalize();
        return 1;
    }
    


    // This Test selects moves around the first transceiver index and sees if
    // where it broadcasts changes

    if(world_rank == 0) {
        auto& t = trxs[0];
        for(int i = 0; i < NUM_TRXS; ++i) {
            const char* msg = (char*)(&i);
            // send message with size, and a timeout of 0 
            assert(t.send(msg, sizeof(i), 1) == sizeof(i));
            // shift x to another location and publish again
            t.set_x(i + 0.02); 
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
        }
    } else { // another rank make sure messages get received
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
        }
    }

    MPIRadioTransceiver::close_mpi_thread();

    MPI_Finalize();
    return 0;
}



