#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#include "cuda_structs.h"

int get_cuda_device_count();
void set_cuda_device(int rank, int cuda_device);
void allocate_cuda_memory(char** data, const size_t size);
void synchronize_cuda_devices();
void free_cuda_memory(char* data);
void deliver_mpi_msg(
        const unsigned long blocks_count,
        const ushort threads_per_block, 
        const size_t num_trxs,
        const double latency,
        const double current_time,
        MPIMsg* mpi_msg, DeviceData* device_data);
