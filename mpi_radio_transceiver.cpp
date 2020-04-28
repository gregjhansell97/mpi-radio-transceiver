#include "mpi_radio_transceiver.h"

#include "mpi.h"
#include <iostream>

using std::cout;
using std::endl;

using hmap::interface::Communicator;


bool MPIRadioTransceiver::open_mpi_thread(
        MPIRadioTransceiver* trxs,
        const size_t trxs_size) {
    int provided_thread_support;
    MPI_Query_thread(&provided_thread_support);
    if(provided_thread_support != MPI_THREAD_MULTIPLE) {
        cout << "ERROR: need MPI_THREAD_MULTIPLE support" << endl;
        return false; // something went wrong
    } 
    cout << "SPINNING MPI THREAD" << endl;
}

void MPIRadioTransceiver::close_mpi_thread() {
    cout << "CLOSING MPI THREAD" << endl;
}


MPIRadioTransceiver::MPIRadioTransceiver() { }

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

