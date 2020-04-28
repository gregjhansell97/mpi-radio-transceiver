#ifndef MPI_RADIO_TRANSCEIVER_H
#define MPI_RADIO_TRANSCEIVER_H

#include <assert.h>
#include <unistd.h>

#include "communicator.h"


class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    MPIRadioTransceiver();
    ~MPIRadioTransceiver();

    void set_x(const double x) { m_x = x; };
    void set_y(const double y) { m_y = y; };
    void set_send_duration(const double sd) { m_send_duration = sd; };
    void set_recv_duration(const double rd) { m_recv_duration = rd; };
    void set_send_radius(const double r) { m_send_radius = r; };
    void set_recv_radius(const double r) { m_recv_radius = r; };
    void set_buffer(char* buffer, const size_t size) {
        m_buffer = buffer;
        m_buffer_size = size;
    };
   
    ssize_t send(
            const char* data, const size_t size, const int timeout) override;

    ssize_t recv(char** data, const int timeout) override;

    void close() override;

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
    static bool open_mpi_thread(
            MPIRadioTransceiver* trxs, const size_t trxs_size);
    /**
     * Stops the mpi listener
     */
    static void close_mpi_thread();

private:
    double m_x;
    double m_y;
    double m_send_duration;
    double m_recv_duration;
    double m_send_radius;
    double m_recv_radius;
    char* m_buffer;
    size_t m_buffer_size;

};

#endif // MPI_RADIO_TRANSCEIVER_H
