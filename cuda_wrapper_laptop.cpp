#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<stdbool.h>

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
    /*
    int device_count;
    cudaError_t cuda_status = cudaGetDeviceCount(&device_count);
    // check for device count failure
    if(cuda_status != cudaSuccess) {
        printf("Unable to determine cuda device count, error is %d, count is %d\n", 
                cuda_status, device_count);
        exit(-1);
    }
    return device_count;*/
}

// Sets the current cuda device
void set_cuda_device(int rank, int cuda_device) {
    return;
    /*
    cudaError_t cuda_status =  cudaSetDevice(cuda_device);
    // check for device count failure
    if( cuda_status != cudaSuccess) {
        printf("Unable to have rank %d set to cuda device %d, error is %d \n",
                rank, cuda_device, cuda_status);
        exit(-1);
    }*/
}

// allocates memory for cuda
void allocate_cuda_memory(char** data, const size_t size) {
    *data = new char[size];
}

// wait for all parallel gpu calculations to finish
void synchronize_cuda_devices() {
    //cudaDeviceSynchronize();
}

// frees allocated cuda memory
void free_cuda_memory(char* data) {
    delete [] data;
    //cudaFree(data);
}

//__gobal__ (for cuda)
void deliver_mpi_msg_kernel(
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
    size_t i = 0;
    const size_t step = 1;
    for(; i < num_trxs; i += step) {
        DeviceData* d = (DeviceData*)(raw_device_data);
        //if(d->rank != 0) cerr << d->id << "-" << d->rank <<endl;

        if(d->buffer_size + mpi_msg->size > max_buffer_size) {
            // buffer overflow
            raw_device_data += step*device_data_size;
            continue;
        }
        if(mpi_msg->sender_rank == d->rank &&
                mpi_msg->sender_id == d->id) {
            // don't send to self
            raw_device_data += step*device_data_size;
            continue;
        }
        // calculate distance
        const double mag = mpi_msg->send_range + d->recv_range;
        const double dx = mpi_msg->send_x - d->x;
        const double dy = mpi_msg->send_y - d->y;
        if(mag*mag < dx*dx + dy*dy) {
            //  nodes too far away 
            raw_device_data += step*device_data_size;
            continue;
        }
        Mail* head = (Mail*)((char*)(&d->_mailbox) + (d->_head)*mail_size);
        Mail* tail = (Mail*)((char*)(&d->_mailbox) + (d->_tail)*mail_size);
        // not empty and inteference 
        if(d->buffer_size > 0
                && mpi_msg->send_time - head->send_time < latency) {
            // grow leading msg pointer to absorb other msg
            head->size += mpi_msg->size;
            // set head pointer to have interference
            head->interference = true;
            d->buffer_size += mpi_msg->size;
        } else {
            d->buffer_size += mpi_msg->size;
            tail->send_time = mpi_msg->send_time;
            tail->interference = (d->last_send_time + latency > current_time);
            tail->size = mpi_msg->size;
            // copy over data from mpi message to tail
            memcpy(&tail->data, &mpi_msg->data, mpi_msg->size);
            // adjust tail to next open spot
            d->_tail = (d->_tail + 1)%max_buffer_size;
        }

        raw_device_data += step*device_data_size;
    }
}

void deliver_mpi_msg(
        const size_t num_trxs,
        const size_t device_data_size,
        const size_t mail_size,
        const size_t max_buffer_size,
        const size_t packet_size,
        const double latency,
        const double current_time,
        char* raw_mpi_msg, char* raw_device_data) {
    // THIS IS THE DRIVER FOR THE CUDA KERNEL
    deliver_mpi_msg_kernel(
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
