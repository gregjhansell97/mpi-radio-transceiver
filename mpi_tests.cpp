#include "mpi.h"
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "mpi_radio_transceiver.h"


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

    auto trxs = MPIRadioTransceiver::transceivers<NUM_TRXS, MAX_BUFFER_SIZE>();
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

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
    }

    // ENSURES: all transceivers done with adjusting their locations
    MPI_Barrier(MPI_COMM_WORLD); 

    // This test selects moves around the first transceiver index and sees if
    // where it broadcasts changes
    if(rank == 0) {
        MPI_Msg mpi_msg;
        char msg_contents = 'H';
        mpi_msg.data = (char*) (&msg_contents);
        mpi_msg.send_range = 1;
        mpi_msg.sender_id = 0;
        mpi_msg.sender_rank = rank;
        mpi_msg.sent_x = trxs[0].get_x();
        mpi_msg.sent_y = trxs[0].get_y();
        trxs[0].send(&mpi_msg, sizeof(mpi_msg), 0);
        MPI_Msg* recv_msg = (MPI_Msg*) &mpi_msg;
        trxs[0].recv(&recv_msg, 1000); // wait for 1 second, TODO switch to float
        cout << "Data: " << recv_msg->data << endl;
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
        MPI_Msg mpi_msg;
        char p = 'a';
        mpi_msg.data = (char*)(&p);
        mpi_msg.send_range = 1;
        mpi_msg.sender_id = 0;
        mpi_msg.sender_rank = rank;
        mpi_msg.sent_x = trxs[0].get_x();
        mpi_msg.sent_y = trxs[0].get_y();
        MPI_Msg* recv_msg = (MPI_Msg*) (&mpi_msg);
        trxs[0].recv(&recv_msg, 1000);
        cout << "Data: " << recv_msg->data << endl;
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
    MPIRadioTransceiver::close_transceivers(trxs, NUM_TRXS);


    MPI_Finalize();
    return 0;
}



