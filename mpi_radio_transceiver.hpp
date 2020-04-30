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
#include <queue>
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
            const char* data, const size_t size, const int timeout) override {
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
                    &mpi_msg, sizeof(MPIMsg), MPI_BYTE, 
                    i, RECV_CHANNEL, MPI_COMM_WORLD);
        }
        return size;
    }

    ssize_t recv(char** data, const int timeout) {
        m_receiving = true;
        if(m_mailbox.empty()) {
            // wait for mailbox to not be empty
            std::mutex mtx;
            std::unique_lock<std::mutex> lk(mtx);
            m_mailbox_flag.wait_for(
                    lk, // lock to block on  
                    std::chrono::milliseconds(timeout), // time to wait on
                    [this]{ return !m_mailbox.empty(); }); // wait till data
        }
        m_receiving = false;
        if(!m_mailbox.empty()) { // mpi-message available
            auto& mpi_msg = m_mailbox.front();
            const size_t data_size = mpi_msg->size;
            memcpy(m_rcvd, mpi_msg->data, mpi_msg->size);
            {
                // remove item from mailbox and adjust size
                std::lock_guard<std::mutex> mailbox_lock(m_mailbox_mtx); 
                m_buffer_size -= mpi_msg->size;
                m_mailbox.pop(); // remove item from mail
            }
            *data = m_rcvd;
            return data_size;
        } else {
            // not message was received, timeout must have been hit
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
        // TODO send copy of trxs back, not this array^^ because
        // This array may swap around its indexes and we don't
        // want that to interfere with iterating through them

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
    typedef struct MPIMsg {
        // sender_id and sender_rank used to identify the 'source' of the msg
        int sender_rank;
        size_t sender_id; // unique id (in the scope of rank)
        double send_x, send_y; //location of sending transceiver
        double send_range; // how far sending transceiver can send
        size_t size;
        char data[B]; // message contents
    } MPIMsg;

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

    
    // mailbox variables
    std::queue<std::shared_ptr<MPIMsg>> m_mailbox;
    std::mutex m_mailbox_mtx;
    bool m_receiving = false;
    size_t m_buffer_size = 0;
    char m_rcvd[B];
    std::condition_variable m_mailbox_flag; 

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
        // TODO check to make sure N is larger than 0, otherwise exit with cerr

        // mpi stats
        int rank = trxs[0].m_rank;

        // set up Irecv requests
        const int RECV_INDEX = 0; // index for recv requests
        const int CLOSE_INDEX = 1; // index for close requests
        // Two types of requests: recv-requests and close-requests
        MPI_Request requests[2];

        // MPI received meta information
        MPI_Status status; // was the receive successful?
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
            // create a new shared pointer
            std::shared_ptr<MPIMsg> mpi_msg = 
                std::make_shared<MPIMsg>();
            // receive data
            MPI_Irecv(
                mpi_msg.get(), // would-be received data
                sizeof(MPIMsg), // max size the received data can be
                MPI_BYTE, // type of data being received
                MPI_ANY_SOURCE, // Source of data being received
                RECV_CHANNEL, 
                MPI_COMM_WORLD, // mpi-communicator used
                &requests[RECV_INDEX]); // return to block later on
            MPI_Waitany(2, requests, &channel, &status);
            if(channel == CLOSE_CHANNEL) {
                // the rank is ending
                // do any clean-up here
                break;
            } else if(channel == RECV_CHANNEL) {
                // Iterate through transceivers and load their mailboxes
                // with new the new message (if it pertains to them)
                for(size_t i = 0; i < N; ++i) {
                    auto& t = trxs[i];
                    // Check if mpi_msg buffer is maxed out; if so, 
                    // drop message. If the sender is itself, drop message.
                    if (t.m_buffer_size + mpi_msg->size > B) {
                        std::cerr << "message too large" << std::endl;
                        std::cerr << mpi_msg->size << std::endl;
                        continue;
                    } else if(mpi_msg->sender_rank == t.m_rank && 
                            mpi_msg->sender_id == t.m_id) {
                        continue;
                    }
                    // calculate distance
                    const double mag = mpi_msg->send_range + t.m_recv_range;
                    const double dx = mpi_msg->send_x - t.m_x;
                    const double dy = mpi_msg->send_y - t.m_y;
                    if(mag*mag < dx*dx + dy*dy) {
                        // nodes too far away
                        continue;
                    }
                    { 
                        // MPI_MSG BUFFER LOCK
                        // A potential optimization would be to skip for later 
                        // if can't get lock:
                        //    swap it with node at end of list,
                        //    decrement i and continue;
                        std::lock_guard<std::mutex> mailbox_lock(
                                t.m_mailbox_mtx);
                        t.m_mailbox.push(mpi_msg);
                        t.m_buffer_size += mpi_msg->size;
                    }
                    // SHORT BUSY WAIT
                    while(t.m_receiving) {
                        // ends any blocking mutexes
                        t.m_mailbox_flag.notify_all();
                    }
                }
            }
        }
    }
};

template<size_t B>
std::thread* MPIRadioTransceiver<B>::mpi_listener_thread;

#endif // MPI_RADIO_TRANSCEIVER_H
