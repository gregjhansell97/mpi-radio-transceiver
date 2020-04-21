#include "mpi_radio_transceiver.h"

using hmap::interface::Communicator;

MPIRadioTransceiver::MPIRadioTransceiver(
        void* ctx,
        const double send_duration,
        const double recv_duration,
        const double send_radius,
        const double recv_radius,
        const size_t max_buffer_size) { 
    // TODO
}
MPIRadioTransceiver::~MPIRadioTransceiver() { 
    this->close();
    // TODO
}
ssize_t MPIRadioTransceiver::send(
        const char* data, const size_t size, const int timeout) {
    return Communicator::error;
}

ssize_t MPIRadioTransceiver::recv(char** data, const int timeout) {
    return Communicator::error;
}

void MPIRadioTransceiver::close() {
    // TODO
}

