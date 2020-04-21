#ifndef MPI_RADIO_TRANSCEIVER_H
#define MPI_RADIO_TRANSCEIVER_H

#include <assert.h>
#include <unistd.h>

#include "communicator.h"


class MPIRadioTransceiver : public hmap::interface::Communicator {
public:
    MPIRadioTransceiver(
            void* ctx,
            const double send_duration,
            const double recv_duration,
            const double send_radius,
            const double recv_radius,
            const size_t max_buffer_size); 
    ~MPIRadioTransceiver();

    ssize_t send(
            const char* data, const size_t size, const int timeout) override;

    ssize_t recv(char** data, const int timeout) override;

    void close() override;
private:
    void* ctx;
    double m_send_duration;
    double m_recv_duration;
    double m_send_radius;
    double m_recv_radius;
    size_t m_max_buffer_size;

};

#endif // MPI_RADIO_TRANSCEIVER_H
