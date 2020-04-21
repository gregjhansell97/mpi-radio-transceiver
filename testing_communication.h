#ifndef HMAP_MODULES_TESTING_COMMUNICATION_H
#define HMAP_MODULES_TESTING_COMMUNICATION_H

#include <assert.h>
#include <unistd.h>

#include "communicator.h"

namespace hmap {
namespace testing {
namespace communication {


/**
 * Tests basic send and receive amongst several communicators
 *
 * Args:
 *     connected: array of 2 or more communicators all able to directly
 *         communicate with one another
 *     isolated: array of 1 or more communicators that cannot communicate
 *         with the connected communicators
 */
void send_recv(
        hmap::interface::Communicator** connected, const size_t c_size,
        hmap::interface::Communicator** isolated, const size_t i_size, 
        const int timeout=1);

} // communication
} // testing
} // hmap


#endif // HMAP_MODULES_TESTING_COMMUNICATION_H
