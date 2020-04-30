#ifndef MPI_RADIO_TRANSCEIVER_H
#define MPI_RADIO_TRANSCEIVER_H

// C
#include <assert.h>
#include <unistd.h>

// STDLIB
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

// HIVE-MAP
#include "communicator.h"



class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    void set_x(const double x) { m_x = x; };
    void set_y(const double y) { m_y = y; };
    void set_send_duration(const double sd) { m_send_duration = sd; };
    void set_recv_duration(const double rd) { m_recv_duration = rd; };
    void set_send_radius(const double r) { m_send_radius = r; };
    void set_recv_radius(const double r) { m_recv_radius = r; };
   
    ssize_t send(
            char* data, const size_t size, const int timeout) override;

    ssize_t recv(char** data, const int timeout) override;

    void close() override;

    /**
     * Called only once at the begining to obtain transceivers
     *
     * Template Args:
     *     N: number of transceivers
     *     B: max buffer size
     */
    template<size_t N, size_t B>
    static MPIRadioTransceiver* transceivers() {
        static MPIRadioTransceiver trxs[N];
        static char mpi_msgs[N][B]; // TODO GROW OUT TO ACTUAL MAX BUFFER

        for(int i = 0; i < N; ++i) {
            auto& t = trxs[i];
            t.m_id = i;
            t.m_max_mpi_msgs_size = B;
            t.m_mpi_msgs_size = 0;
            t.m_mpi_msgs = mpi_msgs[i];
        }
        if(!open_mpi_listener(trxs, N)) {
            // could not start listener, somethings wrong
            return nullptr;
        }
        return trxs;
    }
    /**
     * Closes transceivers created by the transceivers template function above
     *
     * Args:
     *     trxs: array of transceivers to close
     *     trxs_size: number of transceivers to close
     */
    static void close_transceivers(
            MPIRadioTransceiver* trxs, const size_t trxs_size);


private:
    // Identifier unique to each transceiver.
    // Prevents transceivers from receiving their own messages.
    int m_id;

    // transceiver parameters
    double m_x;
    double m_y;
    double m_send_duration;
    double m_recv_duration;
    double m_send_radius;
    double m_recv_radius;

    // mpi_msg buffer variables
    char* m_mpi_msgs; // buffer information gets packed into
    std::mutex m_mpi_msgs_mtx; // serializes changes to the array
    // conditional that fires when an MPI message has been received.
    std::condition_variable m_mpi_msgs_flag; 
    // amount of data in the MPI msgs buffer currently
    size_t m_mpi_msgs_size = 0;
    // largest amount of data possible in the mpi msgs buffer, if this
    // high-water mark is reached then messages will be dropped.
    size_t m_max_mpi_msgs_size;
    // triggered when the transceiver is receiving information
    bool m_receiving = false;

    // MPI meta information
    int m_rank;
    int m_num_ranks;

    // lister spins up and on a loop of MPI receives until close_mpi_listener
    // is called
    static std::thread* mpi_listener_thread;
    static void mpi_listener(
            MPIRadioTransceiver* trxs, const size_t trxs_size);

    // mpi channels used
    static const int RECV_CHANNEL = 0;
    static const int CLOSE_CHANNEL = 1;

    // can only create transcievers through template transceivers
    MPIRadioTransceiver();

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
    static bool open_mpi_listener(
            MPIRadioTransceiver* trxs, const size_t trxs_size);
    /**
     * Stops the mpi listener
     */
    static void close_mpi_listener();
};

#endif // MPI_RADIO_TRANSCEIVER_H
