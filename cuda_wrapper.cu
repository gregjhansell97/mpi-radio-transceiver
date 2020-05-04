#include<assert.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<stdbool.h>

#include "./cuda_structs.h"

// cuda
#include <cuda.h>
#include <cuda_runtime.h>

// Counts the number of cuda devices
// Returns: the number of cuda devices
int get_cuda_device_count() {
    int device_count;
    cudaError_t cuda_status = cudaGetDeviceCount(&device_count);
    // check for device count failure
    if(cuda_status != cudaSuccess) {
        printf("Unable to determine cuda device count, error is %d, count is %d\n", 
                cuda_status, device_count);
        exit(-1);
    }
    return device_count;
}

// Sets the current cuda device
void set_cuda_device(int rank, int cuda_device) {
    cudaError_t cuda_status =  cudaSetDevice(cuda_device);
    // check for device count failure
    if( cuda_status != cudaSuccess) {
        printf("Unable to have rank %d set to cuda device %d, error is %d \n",
                rank, cuda_device, cuda_status);
        exit(-1);
    }
}

// allocates memory for cuda
void allocate_cuda_memory(char** data, const size_t size) {
    cudaMallocManaged(data, size);
}

// wait for all parallel gpu calculations to finish
void synchronize_cuda_devices() {
    cudaDeviceSynchronize();
}

// frees allocated cuda memory
void free_cuda_memory(char* data) {
    cudaFree(data);
}

__global__ void deliver_mpi_msg_kernel(
        const size_t num_trxs,
        const size_t device_data_size,
        const size_t mail_size,
        const size_t max_buffer_size,
        const size_t packet_size,
        const double latency,
        const double current_time,
        char* raw_mpi_msg, char* raw_device_data) {
    // this is where things get fast!
    MPIMsg* mpi_msg = (MPIMsg*)(raw_mpi_msg);
    size_t i = (blockIdx.x * blockDim.x) + threadIdx.x;
    const size_t step = blockDim.x * gridDim.x; // total threads in process
    double mag;
    double dx;
    double dy;
    Mail* head;
    Mail* tail;
    for(; i < num_trxs; i += step) {
        if(i == 1) {
            printf("ITERATING THROUGH\n");
        }

        DeviceData* d = (DeviceData*)(raw_device_data + i*device_data_size);
        //sanity check

        if(d->buffer_size + mpi_msg->size > max_buffer_size) {
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
        // head and tail of queue
        head = (Mail*)((char*)(&d->_mailbox) + (d->_head)*mail_size);
        tail = (Mail*)((char*)(&d->_mailbox) + (d->_tail)*mail_size);
        // not empty and inteference 
        if(d->buffer_size > 0
                && mpi_msg->send_time - head->send_time < latency) {
            // NOTE: ^^ should be one before tail not head
            // grow leading msg pointer to absorb other msg
            head->size += mpi_msg->size;
            // set head pointer to have interference
            head->interference = true;
            d->buffer_size += mpi_msg->size;
        } else {
            printf("got a new message\n");
            tail->send_time = mpi_msg->send_time;
            tail->interference = (d->last_send_time + latency > current_time);
            tail->size = mpi_msg->size;
            // copy over data from mpi message to tail
            memcpy(&tail->data, &mpi_msg->data, mpi_msg->size);
            // adjust tail to next open spot
            d->_tail = (d->_tail + 1)%max_buffer_size;
            d->buffer_size = d->buffer_size + mpi_msg->size;
        }
    }
}

void deliver_mpi_msg(
        const unsigned long blocks_count,
        const ushort threads_per_block,
        const size_t num_trxs,
        const size_t device_data_size,
        const size_t mail_size,
        const size_t max_buffer_size,
        const size_t packet_size,
        const double latency,
        const double current_time,
        char* raw_mpi_msg, char* raw_device_data) {
    deliver_mpi_msg_kernel<<<blocks_count, threads_per_block>>>(
            num_trxs,
            device_data_size,
            mail_size,
            max_buffer_size,
            packet_size,
            latency,
            current_time,
            raw_mpi_msg, raw_device_data);
    synchronize_cuda_devices();
}
