#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#include <string.h> // TODO remove for cuda impl

#include <iostream> // TODO remove for cuda version

#include "./cuda_structs.h"

using std::cout;
using std::cerr;
using std::endl;

// Counts the number of cuda devices
// Returns: the number of cuda devices
int get_cuda_device_count() {
    return 1;
}

// Sets the current cuda device
void set_cuda_device(int rank, int cuda_device) {
    return;
}

// allocates memory for cuda
void allocate_cuda_memory(char** data, const size_t size) {
    *data = new char[size];
}

// wait for all parallel gpu calculations to finish
void synchronize_cuda_devices() { }

// frees allocated cuda memory
void free_cuda_memory(char* data) {
    delete [] data;
}

void deliver_mpi_msg_kernel(
        const size_t num_trxs,
        const double latency,
        const double current_time,
        MPIMsg* mpi_msg, DeviceData* device_data) {
    // this is where things get fast!
    size_t i = 0;
    const size_t step = 1;
    double mag;
    double dx;
    double dy;
    Mail* tail; // last open space in queue
    Mail* last; // last element in queue
    for(; i < num_trxs; i += step) {
        DeviceData* d = &device_data[i];
        assert(d->buffer_size <= TRX_BUFFER_SIZE);
        if(d->buffer_size + mpi_msg->size > TRX_BUFFER_SIZE) {
            // buffer overflow
            continue;
        }
        if(mpi_msg->sender_rank == d->rank &&
                mpi_msg->sender_id == d->id) {
            // don't send to self
            continue;
        }
        // calculate distance
        mag = mpi_msg->send_range + d->recv_range;
        dx = mpi_msg->send_x - d->x;
        dy = mpi_msg->send_y - d->y;
        if(mag*mag < dx*dx + dy*dy) {
            //  nodes too far away 
            continue;
        }
        tail = &d->_mailbox[d->_tail];
        last = &d->_mailbox[(d->_tail + (TRX_BUFFER_SIZE - 1))%TRX_BUFFER_SIZE];
        // not empty and inteference 
        if(d->buffer_size > 0
                && mpi_msg->send_time - last->send_time < latency) {
            // grow leading msg pointer to absorb other msg
            last->size += mpi_msg->size;
            // set end pointer to have interference
            last->interference = true;
            d->buffer_size += mpi_msg->size;
        } else {
            tail->send_time = mpi_msg->send_time;
            tail->interference = (d->last_send_time + latency > current_time);
            tail->size = mpi_msg->size;
            // copy over data from mpi message to tail
            memcpy(tail->data, mpi_msg->data, mpi_msg->size);
            // adjust tail to next open spot
            d->_tail = (d->_tail + 1)%TRX_BUFFER_SIZE;
            d->buffer_size += mpi_msg->size;
        }
    }
}

void deliver_mpi_msg(
        const unsigned long blocks_count,
        const ushort threads_per_block,
        const size_t num_trxs,
        const double latency,
        const double current_time,
        MPIMsg* mpi_msg, DeviceData* device_data) {
    deliver_mpi_msg_kernel(
            num_trxs,
            latency,
            current_time,
            mpi_msg, device_data);
}
