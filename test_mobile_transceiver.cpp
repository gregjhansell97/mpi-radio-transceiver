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


#define LOCATION_OFFSET 0.02
#define NUM_TRXS 2048 //  number of transceivers per rank

#define BUFFER_SIZE 2048 // buffer gets to full messages dropped
#define PACKET_SIZE 32 // dont send data past this size
#define LATENCY 0 // ideal time delay between send and recv

#define THREADS_PER_BLOCK 32

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
    if(rank == 0) cout << "starting test_mobile_transceiver" << endl;

    auto trxs = RadioTransceiver::transceivers(
            NUM_TRXS, BUFFER_SIZE, PACKET_SIZE, LATENCY, THREADS_PER_BLOCK);
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
    for(size_t i = 0; i < NUM_TRXS; ++i) {
        auto& t = trxs[i];
        // setting parameters for t
        t.device_data->x = i + 0.01;
        t.device_data->y = 0.0;
        t.device_data->send_range = 0.25;
        t.device_data->recv_range = 0.25;
    }

    // ENSURES: all transceivers done with adjusting their locations
    MPI_Barrier(MPI_COMM_WORLD); 

    // This Test selects moves around the first transceiver index and sees if
    // where it broadcasts changes
    if(rank == 0) {
        auto& sender = trxs[0];
        for(size_t i = 0; i < NUM_TRXS; ++i) {
            sender.device_data->x = i + LOCATION_OFFSET; 
            const char* msg = (char*)(&i);
            // send message with size, and a timeout of 0 
            assert(sender.send(msg, sizeof(i), 1) == sizeof(i));
            // shift x to another location and publish again
        }
        // shift back
        for(size_t i = 0; i < NUM_TRXS; ++i) {
            sender.device_data->x = i + LOCATION_OFFSET; 
            const char* msg = (char*)(&i);
            // send message with size, and a timeout of 0 
            assert(sender.send(msg, sizeof(i), 1) == sizeof(i));
            // shift x to another location and publish again
        }
        // go through others and verify that they received a message
        char* raw_msg;
        for(size_t i = 1; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            // receive data with a certain timeout
            assert(t.recv(&raw_msg, 100) == sizeof(i));
            size_t rcvd_msg = *(size_t*)(raw_msg);
            // message received better be only i
            assert(rcvd_msg == i);
            // receive data with a certain timeout
            assert(t.recv(&raw_msg, 100) == sizeof(i));
            rcvd_msg = *(size_t*)(raw_msg);
            // message received better be only i
            assert(rcvd_msg == i);
            // no more data to receive
            assert(t.recv(&raw_msg, 0) == 0);
        }
        assert(sender.recv(&raw_msg, 0) == 0);
    } else { // another rank make sure messages get received
        for(size_t i = 0; i < NUM_TRXS; ++i) {
            auto& t = trxs[i];
            char* raw_msg;
            // receive data with a certain timeout
            assert(t.recv(&raw_msg, 100) == sizeof(i));
            size_t rcvd_msg = *(size_t*)(raw_msg);
            // message received better be only i
            assert(rcvd_msg == i);
            // receive data with a certain timeout
            
            ssize_t size = t.recv(&raw_msg, 100);
            assert(size == sizeof(i));
            rcvd_msg = *(size_t*)(raw_msg);
            // message received better be only i
            assert(rcvd_msg == i);
            // no more data to receive
            assert(t.recv(&raw_msg, 0) == 0);
        }
    }

    // may need sleep if operations are not blocking and not done yet
    //std::this_thread::sleep_for(std::chrono::seconds(1));
    RadioTransceiver::close_transceivers(trxs);

    if(rank == 0) cout << "test_mobile_transceiver success!" << endl;

    MPI_Finalize();
    return 0;
}



