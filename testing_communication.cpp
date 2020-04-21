#include "testing_communication.h"


void hmap::testing::communication::send_recv(
        hmap::interface::Communicator** connected, const size_t c_size,
        hmap::interface::Communicator** isolated, const size_t i_size, 
        const int timeout) {
    const char* msg = "h";
    // sends message of size 1 with 0 timeout to all 
    connected[0]->send(msg, 1, 0);
    for(size_t i = 1; i < c_size; ++i) {
        char* rcvd = nullptr;
        auto c = connected[i];
        // receive data from a communicator in range
        assert(c->recv(&rcvd, timeout));
        assert(rcvd != nullptr);
        assert(rcvd[0] == 'h');
        delete [] rcvd;
    }
    for(size_t i = 0; i < i_size; ++i) {
        char* rcvd = nullptr;
        assert(isolated[i]->recv(&rcvd, 0) == 0);
        assert(rcvd == nullptr);
    }
}

