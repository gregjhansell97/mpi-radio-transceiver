#ifndef MPI_RADIO_TRANSCEIVER_H
#define MPI_RADIO_TRANSCEIVER_H

// C
#include <assert.h>
#include <string.h>
#include <unistd.h>

// STDLIB
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

// MPI
#include "mpi.h"

// HIVE-MAP
#include "communicator.h"


template<size_t B>
class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    const double get_x() { return m_x; };
    const double get_y() { return m_y; };
    void set_x(const double x) { m_x = x; };
    void set_y(const double y) { m_y = y; };
    void set_send_duration(const double sd) { m_send_duration = sd; };
    void set_recv_duration(const double rd) { m_recv_duration = rd; };
    void set_send_range(const double r) { m_send_range = r; };
    void set_recv_range(const double r) { m_recv_range = r; };
   
    ssize_t send(
            char* data, const size_t size, const int timeout) override {
        if(size > B) {
            return Communicator::error;
        }

        MPIMsg mpi_msg {m_rank, m_id, m_x, m_y, m_send_range, size};
        // (dest, src, n):
        memcpy(mpi_msg.data, data, B);

        // iterate through all ranks and send data
        // NOTE MPI_Bcast does not work for this because you need to know the 
        // root of the broadcaster ( all nodes are currently)
        for(size_t i = 0; i < m_num_ranks; ++i) {
            MPI_Send(
                    data, sizeof(MPIMsg), MPI_BYTE, 
                    i, RECV_CHANNEL, MPI_COMM_WORLD);
        }
        return size;
    }

    ssize_t recv(char** data, const int timeout) {
        m_receiving = true;
        if(m_buffer_size == 0) {
            std::mutex mtx;
            std::unique_lock<std::mutex> lk(mtx);
            m_buffer_flag.wait_for(
                    lk, // lock to block on  
                    std::chrono::milliseconds(timeout), // time to wait on
                    [this]{ return m_buffer_size > 0; }); // wait till data
        }
        m_receiving = false;
        // now see if there is data in the buffer
        if(m_buffer_size > 0) { // there is data in the mpi msg buffer
            const size_t data_size = *((size_t*)m_buffer_items);
            memcpy(m_rcvd, m_buffer_items + sizeof(size_t), data_size);
            *data = m_rcvd;
            {
                std::lock_guard<std::mutex> buffer_lock(m_buffer_mtx);
                // shrink the buffer
                // get size of item in buffer_items
                const size_t buffer_item_size = sizeof(size_t) + data_size;
                // update buffer items size and buffer size
                m_buffer_items_size -= buffer_item_size;
                m_buffer_size -= data_size;
                // move (not copy) over data
                memmove(m_buffer_items, 
                        m_buffer_items + buffer_item_size, 
                        m_buffer_items_size);
            }
            return data_size;
        } else { // timeout occured
            std::cout << "Nothing in buffer." << std::endl;
            *data = nullptr;
            return 0; // nothing in buffer and timeout reached
        }
    }

    void close() override { }

    /**
     * Called only once at the begining to obtain transceivers
     *
     * Template Args:
     *     N: number of transceivers
     *     B: max elements in buffer
     */
    template<size_t N>
    static MPIRadioTransceiver* transceivers() {
        // confirm proper thread support is available
        int provided_thread_support;
        MPI_Query_thread(&provided_thread_support);
        if(provided_thread_support != MPI_THREAD_MULTIPLE) {
            std::cerr << "ERROR: need " << MPI_THREAD_MULTIPLE
            << "-thread-level support but only have"
            << provided_thread_support << "-thread-level support" << std::endl;
            return nullptr; // something went wrong
        } 

        static MPIRadioTransceiver trxs[N];

        // Initializes each transceiver.
        for(size_t i = 0; i < N; ++i) {
            trxs[i].m_id = i;
        }
        // creates a new thread and starts it up
        mpi_listener_thread = new std::thread(
                MPIRadioTransceiver::mpi_listener<N>, trxs);
        return trxs;
    }


    /**
     * Closes transceivers created by the transceivers template function above
     * and is called only once at the end of the program
     *
     * Args:
     *     trxs: array of transceivers returned by MPIRadioTransceiver
     */
    template<size_t N>
    static void close_transceivers(MPIRadioTransceiver* trxs) {
        // this is where the close message is sent
        // MPI meta information
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        // 0 is for success (only option atm)
        char closing_status = 0;
        MPI_Send(
                &closing_status, 1, MPI_BYTE, 
                rank, CLOSE_CHANNEL, MPI_COMM_WORLD);

        // block until mpi_listener_thread dies
        mpi_listener_thread->join();
        delete mpi_listener_thread;
        mpi_listener_thread = nullptr;

        // close all transceivers
        for(size_t i = 0; i < N; ++i) {
            auto& t = trxs[i];
            t.close();
        }
    }


private:
    // Identifier unique to each transceiver.
    // Prevents transceivers from receiving their own messages.
    size_t m_id;

    // transceiver parameters
    double m_x;
    double m_y;
    double m_send_duration;
    double m_recv_duration;
    double m_send_range;
    double m_recv_range;

    // Buffer information (size_t + raw-msg)
    char m_buffer_items[B + B*sizeof(size_t)];
    // VARIABLES THAT SUPPORT THE BUFFER:
    // Tracking variables
    // amount of data in the MPI msgs buffer currently
    size_t m_buffer_size = 0;
    size_t m_buffer_items_size = 0;
    // largest amount of data possible in the buffer, if this
    // high-water mark is reached then messages will be dropped.
    size_t m_max_buffer_size = B;
    char m_rcvd[B];
    // Synchronization
    std::mutex m_buffer_mtx; // serializes changes to the array
    // conditional that fires when an MPI message has been received.
    std::condition_variable m_buffer_flag; 
    // triggered when the transceiver is receiving information
    bool m_receiving = false;

    // MPI meta information
    int m_rank;
    int m_num_ranks;

    // lister spins up and on a loop of MPI receives until close_mpi_listener
    // is called
    static std::thread* mpi_listener_thread;

    // mpi channels used
    static const int RECV_CHANNEL = 0;
    static const int CLOSE_CHANNEL = 1;

    // Pointer used to siphon off a received MPI message off the buffer.
    //MPI_Msg* m_recvd_msg;

    // can only create transcievers through template transceivers
    MPIRadioTransceiver() { 
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        int num_ranks = 0;
        MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
        m_rank = rank;
        m_num_ranks = num_ranks;
    }

    /**
     * Starts the mpi listener on a separate thread
     *
     * Args:
     *     trxs: tranceivers that are connected accross ranks
     *     trxs_size: number of transceivers
     *
     * Returns:
     *     False if mpi threading capabilities are not provided, need
     *      threadlevel of MPI_THREAD_MULTIPLE, and True on successful spinning
     *      of thread 
     */
    /*
    template<size_t N>
    static bool open_mpi_listener(MPIRadioTransceiver* trxs) {
    }*/

    template<size_t N>
    static void mpi_listener(MPIRadioTransceiver* trxs) {
        // mpi stats
        int rank = trxs[0].m_rank;

        // max_msg_size is the largest expected message
        const int max_msg_size = trxs[0].m_max_buffer_size;

        // mpi_msg will take in values from MPI calls
        //MPI_Msg* mpi_msg = new MPI_Msg;
        
        
        /*
    typedef struct RecvHeader {
        // Required such that the sending transceiver does not accidentally send a
        // message to itself.
        int sender_rank;       // Sending transceiver's rank.
        size_t sender_id;         // Sending transceiver's unique ID.
        double sent_x, sent_y; // The location of the sending transceiver.
        double send_range;     // How far the sending transceiver can send.
        char* data;            // Actual message contents.
    } RecvHeader;


        struct RecvMsg {
            Recv
        //struct {
*/
        char raw_msg[B];




        // Set up creation of the MPI_Msg MPI Datatype.
        //const int block_counts = 3; // Number of blocks.
        // Data types contained in the struct.
        /*
        MPI_Datatype block_types[block_counts] = {
            MPI_INT, MPI_DOUBLE, MPI_CHAR};
        int block_lengths[block_counts] = { // Elements per block.
            2, 3, max_msg_size};
        MPI_Aint block_offsets[block_counts] = { // Byte displacement per block.
            offsetof(MPI_Msg, sender_rank),
            offsetof(MPI_Msg, sent_x),
            offsetof(MPI_Msg, data)
        };
        // Creates the MPI Datatype and commits.
        MPI_Datatype MPI_MSG_DT; // MPI wrapper for the MPI_Msg struct.
        MPI_Type_create_struct(
            block_counts, block_lengths, block_offsets, block_types, &MPI_MSG_DT);
        MPI_Type_commit(&MPI_MSG_DT);*/

        // set up Irecv requests
        const int RECV_INDEX = 0; // index for recv requests
        const int CLOSE_INDEX = 1; // index for close requests
        // Two types of requests: recv-requests and close-requests
        MPI_Request requests[2];

        // MPI received meta information
        MPI_Status status; // was the receive successful?
        int msg_size = 0; // how many bytes were received?
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
                raw_msg, // would-be received data
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
                break;
            } else if(channel == RECV_CHANNEL) {
                // get size of the MPI msg
                MPI_Get_count(&status, MPI_BYTE, &msg_size);
                // Iterate through transceivers and load their mpi_msg buffers up
                // with new information (if it pertains to them, and their buffer
                // is not maxed out)
                // MPI_Msg* mpi_msg = (MPI_Msg*)(mpi_msg);
                for(size_t i = 0; i < N; ++i) {
                    auto& t = trxs[i];
                    // Check if mpi_msg buffer is maxed out; if so, drop message.
                    // If the sender is itself, drop message.
                    //if (t.m_buffer_size + msg_size > t.m_max_buffer_size
                    //    || mpi_msg->sender_id == i) {
                    //    continue;
                    //} else 
                    { // MPI_MSG BUFFER LOCK
                        // NOTE: this section is locked because the t.m_buffer
                        // can be modified (albeit only shrunken) by t.
                        // A potential optimization would be to skip for later 
                        // if can't get lock.
                        std::lock_guard<std::mutex> buffer_lock(t.m_buffer_mtx);
                        // move mpi_msg data to buffer on transceiver
                        memcpy(t.m_buffer_items + t.m_buffer_size, raw_msg, msg_size);
                        // adjust size
                        t.m_buffer_size += msg_size;
                        std::cout << "size: "<< t.m_buffer_size << std::endl;
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
 
    typedef struct MPIMsg {
        // sender_id and sender_rank used to identify the 'source' of the msg
        int sender_rank;
        size_t sender_id; // unique id (in the scope of rank)
        double send_x, send_y; //location of sending transceiver
        double send_range; // how far sending transceiver can send
        size_t size;
        char data[B]; // message contents
    } MPIMsg;

};

template<size_t B>
std::thread* MPIRadioTransceiver<B>::mpi_listener_thread;

#endif // MPI_RADIO_TRANSCEIVER_H
