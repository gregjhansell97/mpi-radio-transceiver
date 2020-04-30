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

// Passive object that carries data between transceivers.
typedef struct MPIRadioTransceiverMessage{
    // Required such that the sending transceiver does not accidentally send a
    // message to itself.
    int sender_rank;       // Sending transceiver's rank.
    int sender_id;         // Sending transceiver's unique ID.
    double sent_x, sent_y; // The location of the sending transceiver.
    double send_range;      // How far the sending transceiver can send.
    char* data;            // Actual message contents.
} MPI_Msg;

class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    const double get_x() { return m_x; };
    const double get_y() { return m_y; };
    void set_x(const double x) { m_x = x; };
    void set_y(const double y) { m_y = y; };
    void set_send_duration(const double sd) { m_send_duration = sd; };
    void set_recv_duration(const double rd) { m_recv_duration = rd; };
    void set_send_radius(const double r) { m_send_radius = r; };
    void set_recv_radius(const double r) { m_recv_radius = r; };
   
    ssize_t send(
        char* mpi_msg, const size_t size, const int timeout) override;

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
        static char buffers[N][B + (B * sizeof(MPI_Msg))];

        // Initializes each transceiver.
        for(size_t i = 0; i < N; ++i) {
            auto& t = trxs[i];
            t.m_max_buffer_size = B + (B * sizeof(MPI_Msg));
            t.m_buffer_size = 0;
            t.m_buffer = buffers[i];
        }
        if(!open_mpi_listener(trxs, N)) {
            // could not start listener, something's wrong
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
    size_t m_id;

    // transceiver parameters
    double m_x;
    double m_y;
    double m_send_duration;
    double m_recv_duration;
    double m_send_radius;
    double m_recv_radius;

    // Buffer variables.
    char* m_buffer; // buffer information gets packed into
    std::mutex m_buffer_mtx; // serializes changes to the array
    // conditional that fires when an MPI message has been received.
    std::condition_variable m_buffer_flag; 
    // amount of data in the MPI msgs buffer currently
    size_t m_buffer_size = 0;
    // largest amount of data possible in the buffer, if this
    // high-water mark is reached then messages will be dropped.
    size_t m_max_buffer_size;
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
