#include "mpi_radio_transceiver.h"

// C
#include <string.h> // for copying raw memory

// STDLIB
#include <chrono>
#include <iostream>

// MPI
#include "mpi.h"


// std namespace usage
using std::chrono::milliseconds;
using std::cout;
using std::endl;
using std::lock_guard;
using std::mutex;
using std::thread;
using std::unique_lock;

// hmap namespace usage
using hmap::interface::Communicator;

thread* MPIRadioTransceiver::mpi_listener_thread{nullptr};

void MPIRadioTransceiver::mpi_listener(
        MPIRadioTransceiver* trxs, 
        const size_t trxs_size) {


    // TODO move "set_buffers" here for all transceivers


    // MPI meta information
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // max_msg_size is the largest expected message
    const int max_msg_size = trxs[0].m_max_buffer_size;
    // mpi_msg will take in values from MPI calls
    char* mpi_msg = new char[max_msg_size];
    
    // set up Irecv requests
    const int RECV_INDEX = 0; // index for recv requests
    const int CLOSE_INDEX = 1; // index for close requests
    // Two types of requests: recv-requests and close-requests
    MPI_Request requests[2];

    // MPI received meta information
    MPI_Status status; // was the receive successful?
    int mpi_msg_size = 0; // how many bytes were received?
    char close_status = 0; // was the close clean?
    int channel; // the channel that became unblocked
    // Set up a non-blocking receive for the thread ending
    MPI_Irecv(
        &close_status, // was the close clean?
        1, // the close status is a 1-byte piece of data
        MPI_BYTE, // type of data being received
        rank, // the process will be receiving it from itself
        CLOSE_CHANNEL, 
        MPI_COMM_WORLD, // mpi-communicator used
        &requests[CLOSE_INDEX]); // request to block later on
    while(true) {
        // Each MPI message is going to need:
        // ========================================== HEADER
        // 1. a unique id (so you don't send to yourself)
        // 2. an x and a y location of the message sender
        // 3. the send-range
        // 4. length of message
        // ========================================== BODY
        // 5. the actual message
        MPI_Irecv(
            mpi_msg, // would-be received data
            max_msg_size, // max size the received data can be
            MPI_BYTE, // type of data being received
            MPI_ANY_SOURCE, // Source of data being received (any communicator)
            RECV_CHANNEL, 
            MPI_COMM_WORLD, // mpi-communicator used
            &requests[RECV_INDEX]); // return to block later on
        MPI_Waitany(2, requests, &channel, &status);
        if(channel == CLOSE_CHANNEL) {
            // This is where clean up of local variables happen and any other
            // necessary closing down operations. This channel is messaged when
            // the rank is ending
            delete [] mpi_msg;
            break;
        } else if(channel == RECV_CHANNEL) {
            // get size of the MPI msg
            MPI_Get_count(&status, MPI_BYTE, &mpi_msg_size);
            // Iterate through transceivers and load their mpi_msg buffers up
            // with new information (if it pertains to them, and their buffer
            // is not maxed out)
            for(size_t i = 0; i < trxs_size; ++i) {
                auto& t = trxs[i];
                // check if mpi_msg buffer is maxed out, if so drop message
                { // MPI_MSG BUFFER LOCK
                    // NOTE: this section is locked because the t.m_buffer
                    // can be modified (albeit only shrunken) by t.
                    // A potential optimization would be to skip for later 
                    // if can't get lock.
                    lock_guard<mutex> buffer_lock(t.m_buffer_mtx);
                    // move memory to buffer on transceiver
                    memcpy(t.m_buffer + t.m_buffer_size, mpi_msg, mpi_msg_size);
                    // adjust size
                    t.m_buffer_size += mpi_msg_size;
                    cout << "size: "<< t.m_buffer_size << endl;
                }
                // SHORT BUSY WAIT
                while(t.m_receiving) {
                    // ends any blocking mutexes
                    t.m_buffer_flag.notify_all();
                }
            }
        }
    }
}

bool MPIRadioTransceiver::open_mpi_listener(
        MPIRadioTransceiver* trxs,
        const size_t trxs_size) {
    int provided_thread_support;
    MPI_Query_thread(&provided_thread_support);
    if(provided_thread_support != MPI_THREAD_MULTIPLE) {
        cout << provided_thread_support << endl; 
        cout << "ERROR: need MPI_THREAD_MULTIPLE support" << endl;
        return false; // something went wrong
    } 
    // creates a new thread and starts it up
    mpi_listener_thread = new std::thread(
            MPIRadioTransceiver::mpi_listener, trxs, trxs_size);

    return true;
}

void MPIRadioTransceiver::close_mpi_listener() {
    // this is where the close message is sent
    // MPI meta information
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    // 0 is for success (only option atm)
    char closing_status = 0;
    MPI_Send(&closing_status, 1, MPI_BYTE, rank, CLOSE_CHANNEL, MPI_COMM_WORLD);
    // block until mpi_listener_thread dies
    mpi_listener_thread->join();
    delete mpi_listener_thread;
    mpi_listener_thread = nullptr;
}

void MPIRadioTransceiver::close_transceivers(
        MPIRadioTransceiver* trxs, const size_t trxs_size) {
    MPIRadioTransceiver::close_mpi_listener();
    for(size_t i = 0; i < trxs_size; ++i) {
        auto& t = trxs[i];
        t.close();
    }
}
    

MPIRadioTransceiver::MPIRadioTransceiver() { 
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int num_ranks = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
    m_rank = rank;
    m_num_ranks = num_ranks;

}

ssize_t MPIRadioTransceiver::send(
        char* data, const size_t size, const int timeout) {
    if(size > m_max_buffer_size) {
        return Communicator::error;
    }
    // iterate through all ranks and send data
    // NOTE MPI_Bcast does not work for this because you need to know the 
    // root of the broadcaster ( all nodes are currently)
    for(size_t i = 0; i < m_num_ranks; ++i) {
        MPI_Send(data, size, MPI_BYTE, i, RECV_CHANNEL, MPI_COMM_WORLD);
    }
    return size;
}

ssize_t MPIRadioTransceiver::recv(char** data, const int timeout) {
    if(m_buffer_size == 0) {
        m_receiving = true;
        mutex mtx;
        unique_lock<mutex> lk(mtx);
        m_buffer_flag.wait_for(
                lk, // lock to block on  
                milliseconds(timeout), // time to wait on
                [this]{ return m_buffer_size > 0; }); // conditional to wait for
        m_receiving = false;
    }
    // now see if there is data in the buffer
    if(m_buffer_size > 0) { // there is data in the mpi msg buffer
        *data = m_buffer; // this will do for now...
        m_receiving = false;
        return 1;
        // TODO Calculate actual size from data offsets
        // TODO Shrink buffer size (need buffer_mtx for that)
        // TODO copy over buffer to newly re-allocated buffer memory
    } else {
        return 0; // nothing in buffer and timeout reached
    }
}

void MPIRadioTransceiver::close() {
    // TODO
}

