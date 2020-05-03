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
#define LATENCY 0.0 // ideal time delay between send and recv

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


    // OPEN MPI FILES
    /*
    MPI_File send_file;
    MPI_File_open(
            MPI_COMM_WORLD, "send_file.log", 
            MPI_MODE_CREATE | MPI_MODE_RDWR,
            MPI_INFO_NULL, &send_file);  
    MPI_File recv_file;
    MPI_File_open(
            MPI_COMM_WORLD, "recv_file.log", 
            MPI_MODE_CREATE | MPI_MODE_RDWR,
            MPI_INFO_NULL, &recv_file);  
    */

    // create two transceivers
    auto trxs = RadioTransceiver::transceivers(
            2, BUFFER_SIZE, PACKET_SIZE, LATENCY);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    // set initial values for transceivers
    auto& t0 = trxs[0];
    auto& t1 = trxs[1];
    t0.device_data->x = 21.2;
    t0.device_data->y = 21.2;
    // put t1 & t2 out of range
    t0.device_data->send_range = 0.25;
    t0.device_data->recv_range = 0.25;

    t1.device_data->x = 10.5;
    t1.device_data->y = 10.5;
    t1.device_data->send_range = 0.25;
    t1.device_data->recv_range = 0.25;
    // ENSURES: ranks are done with adjusting their t0/t1 parameters
    MPI_Barrier(MPI_COMM_WORLD); 

   if(rank == 0) {
        const char* msg0 = "hello 0's\0";
        const char* msg1 = "hello 1's\0";
        //cout << "rank0-t0 sent " << 
        t0.send(msg0, 10, 0.1);// << " bytes" << endl; //(msg, size, timeou)
        //cout << "rank0-t1 sent " << 
        t1.send(msg1, 10, 0.1);// << " bytes" << endl;
    } else {
        char* msg;
        // one second timeout
        ssize_t size = t0.recv(&msg, 1);
        cout << "rank" << rank << "-t0 received " << msg << endl;
        size = t1.recv(&msg, 1);
        cout << "rank" << rank << "-t1 received " << msg << endl;
    }

    // Shuts down all the transceivers
    RadioTransceiver::close_transceivers(trxs);
    /*
    MPI_File_close(&send_file);
    MPI_File_close(&recv_file);
    */
    MPI_Finalize();
    return 0;
}



