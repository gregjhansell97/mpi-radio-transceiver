#ifndef MPI_RADIO_TRANSCEIVER_H
#define MPI_RADIO_TRANSCEIVER_H

// STDLIB
#include <condition_variable>
#include <mutex>
#include <thread>

// MPI
#include "mpi.h"

// HIVE-MAP
#include "communicator.h"

// LOCAL
#include "cuda_structs.h"

#if defined(HMAP_COMM_EVALUATION) || defined(HMAP_CUDA_EVALUATION)
//typedef unsigned long long ticks;
typedef double ticks;
#endif

class RadioTransceiver : public hmap::interface::Communicator {
public:

    typedef struct SendLogItem {
        double x, y, time;
        double send_range;
        size_t size;
        char data;
    } SendLogItem;

       
    ssize_t send(
            const char* data, 
            const size_t size, 
            const double timeout) override;

    ssize_t recv(char** data, const double timeout) override;

    void close() override { };

    /**
     * Called only once at the begining to obtain transceivers and start up
     * listener thread
     */
    static RadioTransceiver* transceivers(
            const size_t num_trxs,
            const size_t max_buffer_size,
            const size_t packet_size,
            const double latency,
            const ushort threads_per_block=1,
            MPI_File* send_file_ptr=nullptr, MPI_File* recv_file_ptr=nullptr);
    /**
     * Closes transceivers created by the transceivers template function above
     * and is called only once at the end of the program
     *
     * Args:
     *     trxs: array of transceivers returned by RadioTransceiver
     */
    static void close_transceivers(RadioTransceiver* trxs);

    static std::mutex device_mtx;
    DeviceData* device_data;


private:
    char* m_rcvd;
    
    static int rank;
    static size_t num_trxs;
    static size_t max_buffer_size;
    static size_t packet_size;
    static double latency;
    static size_t mail_size;
    static size_t device_data_size; 
    // cuda specs
    static unsigned long blocks_count;
    static ushort threads_per_block; 

    // listener spins up and on a loop of MPI receives until close_mpi_listener
    // is called
    static std::thread* mpi_listener_thread;

    // File I/O
    static MPI_File* send_file_ptr;
    static MPI_File* recv_file_ptr;

    // can only create transceivers through RadioTransceiver::transceivers
    RadioTransceiver() { }

    static std::condition_variable* mailbox_flag; 
    static void mpi_listener(
            RadioTransceiver* trxs);
};

#endif // MPI_RADIO_TRANSCEIVER_H
