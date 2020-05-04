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

#ifdef TRX_LAPTOP_MODE
typedef double ticks;
static __inline__ ticks getticks(void)
{
    return MPI_Wtime();
}
#endif

#ifndef TRX_LAPTOP_MODE
typedef unsigned long long ticks;
static __inline__ ticks getticks(void)
{
    unsigned int tbl, tbu0, tbu1;
    do {
        __asm__ __volatile__ ("mftbu %0": "=r"(tbu0));
        __asm__ __volatile__ ("mftb %0": "=r"(tbl));
        __asm__ __volatile__ ("mftbu %0": "=r"(tbu1));
    } while (tbu0 != tbu1);
    return ((((unsigned long long)tbu0) << 32) | tbl);
}
#endif

class RadioTransceiver : public hmap::interface::Communicator {
public:

    typedef struct SendLogItem {
        double x, y, time;
        double send_range;
        size_t size;
        char data[TRX_PACKET_SIZE];
    } SendLogItem;

    typedef struct RecvLogItem {
        double x, y, time;
        double recv_range;
        size_t size;
        char data[TRX_PACKET_SIZE];
    } RecvLogItem;

       
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
            const double latency,
            const ushort threads_per_block=1,
            MPI_File* send_file_ptr=nullptr, MPI_File* recv_file_ptr=nullptr);

    static void synchronize_ranks();

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
    char m_rcvd[TRX_PACKET_SIZE];
    
    static int num_ranks;
    static int rank;
    static size_t num_trxs;
    static double latency;
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
    static void mpi_listener(RadioTransceiver* trxs);
};

#endif // MPI_RADIO_TRANSCEIVER_H
