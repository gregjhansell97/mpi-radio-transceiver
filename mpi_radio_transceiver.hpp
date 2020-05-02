#ifndef MPI_RADIO_TRANSCEIVER_HPP
#define MPI_RADIO_TRANSCEIVER_HPP

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


// B is the max-buffer-size
// P is packet size
// L is latency in milliseconds
template<size_t B, size_t P, size_t L>
class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    const double get_x() { return m_x; };
    const double get_y() { return m_y; };
    void set_x(const double x) { m_x = x; };
    void set_y(const double y) { m_y = y; };
    void set_send_range(const double r) { m_send_range = r; };
    void set_recv_range(const double r) { m_recv_range = r; };
   
    ssize_t send(
            const char* data, const size_t size, const double timeout) override {
        if(size > P) {
            return Communicator::error;
        }

        MPIMsg mpi_msg {m_rank, m_id, m_x, m_y, m_send_range, MPI_Wtime(), false, size};
        // (dest, src, n):
        memcpy(mpi_msg.data, data, P);

        // iterate through all ranks and send data
        // NOTE MPI_Bcast does not work for this because you need to know the 
        // root of the broadcaster ( all nodes are currently)
        m_last_send_time = MPI_Wtime();
        int status = MPI_Send(&mpi_msg, sizeof(MPIMsg), MPI_BYTE, 
                0, 0, MPI_COMM_WORLD);
        
        double fixed_time = MPI_Wtime();
        double current_time;
        double sleep = (L/1000.0);
        while(sleep > 0.0) {
            current_time = MPI_Wtime();
            std::this_thread::sleep_for(std::chrono::milliseconds((size_t)(sleep*1000)));
            sleep -= (MPI_Wtime() - current_time);
        }
        return size;
    }

    ssize_t recv(char** data, const double timeout) {
        double sleep = timeout;
        double current_time = MPI_Wtime();
        while(sleep >= 0) {
            m_receiving = true;
            if(m_mailbox.empty()) {
                // wait for mailbox to not be empty
                std::mutex mtx;
                std::unique_lock<std::mutex> lk(mtx);
                m_mailbox_flag.wait_for(
                        lk, // lock to block on 
                        std::chrono::milliseconds((int)(sleep*1000.0)),
                        [this]{ return !m_mailbox.empty(); }); // wait till data
            }

            m_receiving = false;
            // decrement amount of sleep remaining
            sleep -= (MPI_Wtime() - current_time);
            current_time = MPI_Wtime(); // reset current time
            if(!m_mailbox.empty()) { // mpi-message available
                const double next_recv_time = m_mailbox.front()->send_time + L/1000.0;
                if(current_time < next_recv_time) {
                    // need to wait
                    const double delay = next_recv_time - current_time;
                    if(delay > sleep) { // not enough time
                        break;
                    } else {
                        // sleep for the delay
                        std::this_thread::sleep_for(std::chrono::milliseconds(
                                    (int)(delay*1000.0)));
                    }
                }
                // if I got to here, I can remove message
                auto mpi_msg = m_mailbox.front();
                const size_t data_size = mpi_msg->size;
                memcpy(m_rcvd, mpi_msg->data, std::min(mpi_msg->size, P));
                {
                    // remove item from mailbox and adjust size
                    std::lock_guard<std::mutex> mailbox_lock(m_mailbox_mtx); 
                    m_buffer_size -= mpi_msg->size;
                    m_mailbox.pop(); // remove item from mail
                }
                if(mpi_msg->interference) {
                    // interference
                    sleep -= (MPI_Wtime() - current_time); 
                } else {
                    // not interference, message received successfully
                    *data = m_rcvd;
                    return data_size;
                }
            }
        }

        // ran out of time
        // sleep for remainder of time
        if(sleep > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                        (int)(sleep*1000.0)));
        }
        *data = nullptr;
        return 0;
    }

    void close() override { }

    /**
     * Called only once at the begining to obtain transceivers and start up
     * listener thread
     *
     * Template Args:
     *     N: number of transceivers
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
        MPI_Barrier(MPI_COMM_WORLD); // wait till all ranks want to close
        // this is where the close message is sent
        // MPI meta information
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        // 0 is for success (only option atm)
        if(rank == 0) {
            MPIMsg poison_pill {0, 0, 0, 0, 0, 0};
            MPI_Send(&poison_pill, sizeof(MPIMsg), MPI_BYTE,
                    0, 0, MPI_COMM_WORLD);
        }

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
        double send_time;
        bool interference; // did message get flagged as interference
        size_t size; // how much data is there
        char data[P]; // message contents
    } MPIMsg;

    // Identifier unique to each transceiver.
    // Prevents transceivers from receiving their own messages.
    size_t m_id;

    // transceiver parameters
    double m_x;
    double m_y;
    double m_send_range;
    double m_recv_range;

    
    // mailbox variables
    std::queue<std::shared_ptr<MPIMsg>> m_mailbox; // where MPIMsgs are put
    std::mutex m_mailbox_mtx;
    bool m_receiving = false;
    double m_last_send_time = -(L/1000.0);
    size_t m_buffer_size = 0;
    char m_rcvd[P];
    std::condition_variable m_mailbox_flag; 

    // MPI meta information
    int m_rank;
    int m_num_ranks;

    // listener spins up and on a loop of MPI receives until close_mpi_listener
    // is called
    static std::thread* mpi_listener_thread;

    // mpi channels used
    static const int RECV_CHANNEL = 0;
    static const int CLOSE_CHANNEL = 1;

    // can only create transcievers through template transceivers
    MPIRadioTransceiver() { 
        int rank = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        int num_ranks = 0;
        MPI_Comm_size(MPI_COMM_WORLD, &num_ranks);
        m_rank = rank;
        m_num_ranks = num_ranks;
    }

    template<size_t N>
    static void mpi_listener(MPIRadioTransceiver* trxs) {
        // Checks to make sure N is larger than 0, otherwise exit with cerr.
        //if (N <= 0) {
        //    std::cerr << "Number of transceivers must be positive." << std::endl;
        //    return;
        //}

        // mpi stats
        int rank = trxs[0].m_rank;
        MPI_Status status; // was the receive successful?

        while(true) {
            // create a new shared pointer
            std::shared_ptr<MPIMsg> mpi_msg = std::make_shared<MPIMsg>();
            if(rank == 0) {
                // waiting on messages
                MPI_Recv(
                        mpi_msg.get(),
                        sizeof(MPIMsg),
                        MPI_BYTE,
                        MPI_ANY_SOURCE, // recv channel
                        0,
                        MPI_COMM_WORLD,
                        &status);
                // send to rest of group
                MPI_Bcast(
                        mpi_msg.get(),
                        sizeof(MPIMsg),
                        MPI_BYTE,
                        0,
                        MPI_COMM_WORLD);
                //mpi_msg->send_time = MPI_Wtime();
            } else {
                // rest of group receives
                MPI_Bcast(
                        mpi_msg.get(),
                        sizeof(MPIMsg),
                        MPI_BYTE,
                        0,
                        MPI_COMM_WORLD);
                //mpi_msg->send_time = MPI_Wtime();
            }
            if(mpi_msg->size == 0) {
                // indicates the process should exit
                break;
            }
            for(size_t i = 0; i < N; ++i) {
                auto& t = trxs[i];
                // Check if mpi_msg buffer is maxed out; if so, 
                // drop message. If the sender is itself, drop message.
                if (t.m_buffer_size + mpi_msg->size > B) {
                    // BUFFER OVERFLOW
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
                    // check for interference:
                    // s                 s + L
                    // |-----------------|            
                    //          |---------------------|
                    if(!t.m_mailbox.empty() &&
                            mpi_msg->send_time - t.m_mailbox.front()->send_time < (L/(1000.0))) {
                        std::cout << "INTERFERENCE" << std::endl;
                        //std::cout << mpi_msg->send_time - t.m_mailbox.front()->send_time << std::endl;
                        // interference!
                        // grow buffer
                        t.m_buffer_size += mpi_msg->size; 
                        // grow leading msg size in mail to absorb other msg
                        t.m_mailbox.front()->size += mpi_msg->size;
                        // set leading msg pointer to have interference
                        t.m_mailbox.front()->interference = true; // 
                    } else {
                        // there is interference if a node receives a message
                        // when it is sending a message
                        mpi_msg->interference = (t.m_last_send_time + (L/1000.0) > MPI_Wtime());
                        t.m_buffer_size += mpi_msg->size;
                        t.m_mailbox.push(mpi_msg);
                    }
                }
                // SHORT BUSY WAIT
                while(t.m_receiving) {
                    // ends any blocking mutexes
                    t.m_mailbox_flag.notify_all();
                }
            }
        }
    }
};


template<size_t B, size_t P, size_t L>
std::thread* MPIRadioTransceiver<B, P, L>::mpi_listener_thread;

#endif // MPI_RADIO_TRANSCEIVER_HPP
