#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

int get_cuda_device_count();
void set_cuda_device(int rank, int cuda_device);
void allocate_cuda_memory(char** data, const size_t size);
void synchronize_cuda_devices();
void free_cuda_memory(char* data);
void deliver_mpi_msg(
        const unsigned long blocks_count,
        const ushort threads_per_block, 
        const size_t num_trxs,
        const size_t device_data_size,
        const size_t mail_size,
        const size_t buffer_size,
        const size_t packet_size,
        const double latency,
        const double current_time,
        char* mpi_msg, char* raw_device_data);
