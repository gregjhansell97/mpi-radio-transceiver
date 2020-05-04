#include "mpi.h"
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <set>

#include "radio_transceiver.h"


using std::cout;
using std::cerr;
using std::endl;


#define THREADS_PER_BLOCK 1
#define LATENCY 0// latency is half a second

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

    // open mpi files
    MPI_File send_file;
    MPI_File recv_file;
    MPI_File_open(
            MPI_COMM_WORLD,
            "io_send_test.ord", 
            MPI_MODE_RDWR | MPI_MODE_CREATE | MPI_MODE_DELETE_ON_CLOSE,
            MPI_INFO_NULL,
            &send_file); 
    MPI_File_open(
            MPI_COMM_WORLD,
            "io_recv_test.ord", 
            MPI_MODE_RDWR | MPI_MODE_CREATE | MPI_MODE_DELETE_ON_CLOSE,
            MPI_INFO_NULL,
            &recv_file); 


    // create two transceivers
    auto trxs = RadioTransceiver::transceivers(
            2, LATENCY, THREADS_PER_BLOCK, &send_file, &recv_file);
    if(trxs == nullptr) {
        // could not get transceivers
        MPI_Finalize();
        return 1;
    }

    // set initial values for transceivers, all transceivers of different
    // ranks are out of range
    auto& t0 = trxs[0];
    t0.device_data->x = rank;
    t0.device_data->y = 0;
    t0.device_data->send_range = 0.1;
    t0.device_data->recv_range = 0.1;
    auto& t1 = trxs[1];
    t1.device_data->x = rank;
    t1.device_data->y = 0;
    t1.device_data->send_range = 0.1;
    t1.device_data->recv_range = 0.1;

    // ENSURES: ranks are done with adjusting their t0/t1 parameters
    MPI_Barrier(MPI_COMM_WORLD); 
    
    // all t0's attempt to broadcast simultaneously
    const char* msg = "file-io\0";
    t0.send(msg, 8, 0);
    char* rcvd;
    assert(t1.recv(&rcvd, 1) == 8);
    assert(rcvd[0] == 'f');
    assert(t1.recv(&rcvd, 0.1) == 0);

    RadioTransceiver::close_transceivers(trxs);

    if(rank == 0) {
        std::set<double> x_positions;
        // check that the output is good
        for(int i = 0; i < num_ranks; ++i) {
            RadioTransceiver::SendLogItem item;
            item.time = -5; // check that this changes
            MPI_Status status;
            MPI_File_read(send_file, &item, sizeof(item), MPI_BYTE, &status);
            assert(item.time > 0);
            assert(item.x >= 0 && item.x < num_ranks);
            assert(item.y == 0);
            assert(item.send_range = 0.1); 
            assert(item.size == 8);
            assert(item.data[2] == 'l');
            x_positions.insert(item.x);
        }
        assert((int)x_positions.size() == num_ranks); // no duplicates
    }


    // Shuts down all the transceivers

    if(rank == 0) cout << "test_file_io success!" << endl;

    MPI_File_close(&send_file);
    MPI_File_close(&recv_file);
    MPI_Finalize();
    return 0;
}



