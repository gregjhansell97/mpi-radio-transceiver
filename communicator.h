#ifndef HMAP_MODULES_INTERFACE_COMMUNICATION_H
#define HMAP_MODULES_INTERFACE_COMMUNICATION_H

#include <assert.h>
#include <unistd.h>

// Passive object that carries data between communicators.
typedef struct MPIRadioTransceiverMessage{
    // Required such that the sending transceiver does not accidentally send a
    // message to itself.
    int sender_rank;       // Sending transceiver's rank.
    int sender_id;         // Sending transceiver's unique ID.
    double sent_x, sent_y; // The location of the sending transceiver.
    double send_range;     // How far the sending transceiver can send.
    char* data;            // Actual message contents.
} MPI_Msg;


namespace hmap {
namespace interface {

class Communicator {
public:
    static const ssize_t error = -1; // returned on error for send/recv
    static const ssize_t closed = -2; // returned if closed during send/recv
    /**
     * Sends data to one or more communicators
     *
     * Args:
     *     data: the raw-bytes of data being sent
     *     size: number of bytes of the data being sent
     *     timeout: if send is blocking, how long should it wait to send
     *
     * Returns:
     *     status value regarding the success of sending, if less than zero
     *     then either an error occurred (Communicator::error) or the
     *     communicator closed during the send operation
     */
    virtual ssize_t send(
            MPI_Msg* data, const size_t size, const int timeout) = 0;

    /**
     * Receives bytes of data from another communicator
     *
     * Args: 
     *     data: raw-bytes received (pointer to a pointer), memory is valid
     *      until next receive operation
     *     size: number of bytes received (passed-by-reference)
     *     timeout: blocks on receive
     *
     * Returns:
     *     status value regarding success of sending. if zero then nothing was
     *     received. If less than zero then either an error occurred
     *     (Communicator::error) or the communicator closed during the recv
     *     operation
     */
    virtual ssize_t recv(MPI_Msg** data, const int timeout) = 0;

    /**
     * Idempotent method that ends send and recv capabilities and wraps up any
     * concurrency black magic going on
     */
    virtual void close() = 0;
};

} // interface
} // hmap


#endif // HMAP_MODULES_INTERFACE_COMMUNICATION_H
