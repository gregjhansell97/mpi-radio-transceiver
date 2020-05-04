#include "radio_transceiver.h"

// C
#include <assert.h>
#include <string.h>
#include <unistd.h>

// STDLIB
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

// MPI
#include "mpi.h"

// CUDA
#include "cuda_wrapper.h"
#include "cuda_structs.h"
//#include queda.h

// HIVE-MAP
#include "communicator.h"

using std::condition_variable;
using std::thread;
using std::chrono::milliseconds;
using std::cerr;
using std::cout;
using std::endl;
using std::lock_guard;
using std::mutex;
using std::unique_lock;

unsigned long RadioTransceiver::blocks_count = 0;
ushort RadioTransceiver::threads_per_block = 0; 
size_t RadioTransceiver::num_trxs = 0;
size_t RadioTransceiver::max_buffer_size = 0;
size_t RadioTransceiver::packet_size = 0;
double RadioTransceiver::latency = 0;
size_t RadioTransceiver::mail_size;
size_t RadioTransceiver::device_data_size = 0; 
int RadioTransceiver::rank = 0; 
std::mutex RadioTransceiver::device_mtx;

condition_variable* RadioTransceiver::mailbox_flag;
thread* RadioTransceiver::mpi_listener_thread = nullptr;
// MPI Files
MPI_File* RadioTransceiver::send_file_ptr = nullptr;
MPI_File* RadioTransceiver::recv_file_ptr = nullptr;


#if defined(HMAP_COMM_EVALUATION) || defined(HMAP_CUDA_EVALUATION)
static __inline__ ticks getticks(void)
{
    return MPI_Wtime();
    /*
    unsigned int tbl, tbu0, tbu1;
    do {
        __asm__ __volatile__ ("mftbu %0": "=r"(tbu0));
        __asm__ __volatile__ ("mftb %0": "=r"(tbl));
        __asm__ __volatile__ ("mftbu %0": "=r"(tbu1));
    } while (tbu0 != tbu1);
    return ((((unsigned long long)tbu0) << 32) | tbl);
    */
}
#endif

ssize_t RadioTransceiver::send(
        const char* data, const size_t size, const double timeout) {
    // valid packet size
    if(size > packet_size) {
        return -1;
    } else if(size <= 0) {
        return 0;
    }
    // message size is the MPIMsg and remaining characters
    const size_t mpi_msg_size = sizeof(MPIMsg) + (size - 1);
    char* raw_data = new char[mpi_msg_size];
    // cast raw data to mpi message
    MPIMsg* mpi_msg = (MPIMsg*)raw_data;

    double current_time = MPI_Wtime();
    mpi_msg->sender_rank = device_data->rank;
    mpi_msg->sender_id = device_data->id;
    mpi_msg->send_x = device_data->x;
    mpi_msg->send_y = device_data->y;
    mpi_msg->send_time = current_time;
    mpi_msg->send_range = device_data->send_range;
    mpi_msg->size = size;
    // (dest, source, num bytes)
    memcpy(&mpi_msg->data, data, size);
    device_data->last_send_time = MPI_Wtime();

    // send message to root 
    #ifdef HMAP_COMM_EVALUATION
    ticks t = getticks();
    #endif
    int status = MPI_Send(mpi_msg, mpi_msg_size, MPI_BYTE,
            0, 0, MPI_COMM_WORLD);
    //cerr << "[" << device_data->rank << ", " << device_data->id << "]: " 
    //     << "sent " << size << " bytes" << endl;



    if(status != 0) {
        cerr << "Send failed, MPI failed to send message to leader " 
             << "status code: " << status << endl;
        return -1; 
    }

    #ifdef HMAP_COMM_EVALUATION
    status = MPI_Send((char*)(&t), sizeof(ticks), MPI_BYTE,
            0, 1, MPI_COMM_WORLD);
    #endif

    double sleep = latency;

    /*
    // recycle old raw data
    const int offset = sizeof(MPIMsg) - sizeof(SendLogItem);
    assert(offset > 0);
    SendLogItem* item = (SendLogItem*)(raw_data + offset);
    // re-populate information
    item->x = device->x;
    item->y = device->y;
    item->time = current_time;
    item->send_range = device->send_range;
    const size_t send_log_item = sizeof(SendLogItem) + (size - 1);
    char* raw_data = new char[mpi_msg_size];
    cerr << "rank: " << rank << endl;
    cerr << "x: " << item->x << endl;
    cerr << "y: " << item->x << endl;
    cerr << "time: " << item->time << endl;
    cerr << "send_range: " << item->send_range << endl;
    cerr << "size: " << item->size << endl;
*/
    delete [] raw_data; // free up raw data

    while(sleep > 0.0) {
        current_time = MPI_Wtime();
        std::this_thread::sleep_for(milliseconds((size_t)(sleep*1000)));
        sleep -= (MPI_Wtime() - current_time);
    }

    return size;
}

ssize_t RadioTransceiver::recv(char** data, const double timeout) {
    cerr << "[" << device_data->rank << ", " << device_data->id 
         << " ]: buffer size: " << device_data->buffer_size << endl;
    double sleep = timeout;
    double current_time = MPI_Wtime();
    while(sleep >= 0) {
        mutex mtx;
        unique_lock<mutex> lk(mtx);
        mailbox_flag->wait_for(
                lk,
                milliseconds((int)(sleep*1000.0)),
                [this]{ return device_data->buffer_size > 0; }); 
        // decrement amount of slep
        sleep -= (MPI_Wtime() - current_time);
        current_time = MPI_Wtime(); // recet current time
        char* raw_mailbox = (char*)(&device_data->_mailbox);
        Mail* head = (Mail*)(raw_mailbox + (device_data->_head)*mail_size);
        //Mail* tail = (Mail*)(raw_mailbox + (device_data->_tail)*mail_size);
        if(device_data->buffer_size > 0) { // messages available
            cerr << "[" << device_data->rank << ", " << device_data->id 
                 << "]: message is available" << endl;
            const double next_recv_time = head->send_time + latency;
            if(current_time < next_recv_time) {
                // need to wait
                cerr << "[" << device_data->rank << ", " << device_data->id 
                    << "]: I need to wait until " << next_recv_time 
                    << " but it is currently " << current_time << endl;
                const double delay = next_recv_time - current_time;
                if(delay > sleep) {
                    cerr << "[" << device_data->rank << ", " << device_data->id 
                        << "]: delay " << delay << " sleep " << sleep << endl;
                    break;
                } else {
                    // sleep for the delay
                    std::this_thread::sleep_for(
                            milliseconds((int)(delay*1000)));
                }
            }
            // if I got here I can remove message
            const size_t next_head = (device_data->_head + 1) % max_buffer_size;
            if(head->interference) {
                cerr << "[" << device_data->rank << ", " << device_data->id 
                     << " ]: interference detected" << endl;
                lock_guard<mutex> device_lock(device_mtx);
                device_data->buffer_size -= head->size;
                device_data->_head = next_head;
            } else {
                const size_t data_size = head->size;
                cerr << "[" << device_data->rank << ", " << device_data->id 
                     << " ]: received data (" << data_size << ")" << endl;
                memcpy(m_rcvd, &head->data, head->size);
                {
                    lock_guard<mutex> device_lock(device_mtx);
                    device_data->buffer_size -= head->size;
                    device_data->_head = next_head;
                }
                *data = m_rcvd;
                return data_size;
            }
            sleep -= (MPI_Wtime() - current_time);
            cerr << "[" << device_data->rank << ", " << device_data->id 
                 << " ]: looping again-sleep: " << sleep << endl;
        }
    }
    return 0;
}

RadioTransceiver* RadioTransceiver::transceivers(
        const size_t num_trxs,
        const size_t max_buffer_size,
        const size_t packet_size,
        const double latency,
        const ushort threads_per_block,
        MPI_File* send_file_ptr, MPI_File* recv_file_ptr) {
    // verify proper inputs
    if(max_buffer_size <= 0) {
        cerr << "ERROR: buffer needs to be atleast 1" << endl; 
        return nullptr;
    } else if(packet_size <= 0) {
        cerr << "ERROR: packet size needs to be atleast 1" << endl;
        return nullptr;
    } else if (packet_size > max_buffer_size) {
        cerr << "ERROR: packet size should not be larger than"
             << " buffer size" << endl;
        return nullptr;
    } else if(num_trxs < 1) {
        cerr << "ERROR: invalid number of transceivers" << endl;
        return nullptr;
    } else if(latency < 0.0) {
        cerr << "ERROR: invalid latency" << endl;
        return nullptr;
    } else if(threads_per_block > num_trxs) {
        cerr << "ERROR: threads_per_block must be smaller than "
             << "the number of transceivers" << endl;
    }
    
    RadioTransceiver::threads_per_block = threads_per_block;
    blocks_count = (unsigned long)(((num_trxs)/((double)threads_per_block)) + 0.5);
    RadioTransceiver::num_trxs = num_trxs;
    RadioTransceiver::max_buffer_size = max_buffer_size;
    RadioTransceiver::packet_size = packet_size;
    RadioTransceiver::latency = latency;
    // confirm proper thread support is available
    int provided_thread_support;
    MPI_Query_thread(&provided_thread_support);
    if(provided_thread_support != MPI_THREAD_MULTIPLE) {
        std::cerr << "ERROR: need " << MPI_THREAD_MULTIPLE
        << "-thread-level support but only have"
        << provided_thread_support << "-thread-level support" << std::endl;
        return nullptr; // something went wrong
    } 
    // get mpi rank
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    // CUDA INTIAL CONFIGURATIONS
    const int cuda_device_count =  get_cuda_device_count();
    // attempt to set device based on mpi_rank
    const int cuda_device = rank % cuda_device_count;
    set_cuda_device(rank, cuda_device);

    // SETS GLOBAL FILE POINTERS
    RadioTransceiver::send_file_ptr = send_file_ptr;
    RadioTransceiver::recv_file_ptr = recv_file_ptr; 

    RadioTransceiver* trxs = new RadioTransceiver[num_trxs];

    // each mpi message consists of the header information 
    // and at most "packet_size" characters
    mail_size = sizeof(Mail) + (packet_size - 1);
    // each device data has a mailbox of a list of mpi_messages, each 
    // message is of size mpi_msg_size
    device_data_size = 
        sizeof(DeviceData) - sizeof(Mail) + max_buffer_size*(mail_size);

    char* raw_device_data = nullptr;
    allocate_cuda_memory(&raw_device_data, num_trxs*device_data_size);
    assert(raw_device_data != nullptr);

    for(size_t i = 0; i < num_trxs; ++i) {
        // device data of a transceiver
        DeviceData* device_data = (DeviceData*)raw_device_data;
        device_data->rank = rank;
        device_data->id = i;
        device_data->x = 0.0;
        device_data->y = 0.0;
        device_data->last_send_time = MPI_Wtime() - 2*latency;
        device_data->send_range = 0.0;
        device_data->recv_range = 0.0;
        device_data->buffer_size = 0;
        device_data->_head = 0;
        device_data->_tail = 0; 
        trxs[i].m_rcvd = new char[packet_size];
        trxs[i].device_data = device_data;
        // next device
        raw_device_data += device_data_size;
    }

    mailbox_flag = new condition_variable;
    mpi_listener_thread = new thread(mpi_listener, trxs);
    // start thread
    return trxs;
}

void RadioTransceiver::close_transceivers(RadioTransceiver* trxs) {
    MPI_Barrier(MPI_COMM_WORLD); // wait till all ranks want to close
    // this is where the close message is sent
    if(rank == 0) {
        char* raw_data = new char[sizeof(MPIMsg)];
        MPIMsg* poison_pill = (MPIMsg*)raw_data;
        poison_pill->size = 0;
        // send poison pill
        int status = MPI_Send(poison_pill, sizeof(MPIMsg), MPI_BYTE,
                0, 0, MPI_COMM_WORLD);
        if(status != 0) {
            cerr << "Close down error, MPI failed to send poison pill " 
                 << "status code: " << status << endl;
        }

        delete [] raw_data;
        // send closed message
    }
    // join the thread;
    mpi_listener_thread->join();
    delete mpi_listener_thread;
    mpi_listener_thread = nullptr;

    char* raw_device_data = (char*)trxs[0].device_data;
    free_cuda_memory(raw_device_data); 
    for(size_t i = 0; i < num_trxs; ++i) {
        delete [] trxs[i].m_rcvd;
    }
    delete [] trxs;
    delete mailbox_flag;
}



void RadioTransceiver::mpi_listener(RadioTransceiver* trxs) {
    // allocate data 
    const size_t max_mpi_msg_size = sizeof(MPIMsg) + (packet_size - 1);
    char* raw_mpi_msg;
    // get from cuda memory
    allocate_cuda_memory(&raw_mpi_msg, max_mpi_msg_size);

    MPIMsg* mpi_msg = (MPIMsg*)(raw_mpi_msg);
    MPI_Status status;
    while(true) {
        // create a new shared pointer
        if(rank == 0) {
            // waiting on messages
            MPI_Recv(
                    mpi_msg,
                    max_mpi_msg_size,
                    MPI_BYTE,
                    MPI_ANY_SOURCE, 
                    0,
                    MPI_COMM_WORLD,
                    &status);
            // send to rest of group
            MPI_Bcast(
                    mpi_msg,
                    max_mpi_msg_size,
                    MPI_BYTE,
                    0,
                    MPI_COMM_WORLD);
            //cerr << "message broadcasted: " << mpi_msg->size << endl;
            #ifdef HMAP_COMM_EVALUATION
            ticks t = getticks();
            MPI_Status _status;
            if(mpi_msg->size > 0) {
                ticks send_time;
                MPI_Recv(&send_time, sizeof(ticks), MPI_BYTE, 
                        0, 1, MPI_COMM_WORLD, &_status);
                ticks diff = t - send_time;
                MPI_Send(
                        (char*)&diff, sizeof(ticks), MPI_BYTE, 
                        0, 2, MPI_COMM_WORLD); 
            }
            #endif
        } else {
            // rest of group receives
            MPI_Bcast(
                    mpi_msg,
                    max_mpi_msg_size,
                    MPI_BYTE,
                    0,
                    MPI_COMM_WORLD);
        }
        if(mpi_msg->size == 0) {
            // indicates the process should exit
            // release cuda memory
            free_cuda_memory(raw_mpi_msg); 
            break;
        }
        // need to aquire a shared lock
        {
            //TODO replace with read-write lock
            std::lock_guard<std::mutex> device_lock(device_mtx); 
            #ifdef HMAP_CUDA_EVALUATION
            ticks cuda_start = getticks();
            #endif
            deliver_mpi_msg(
                    blocks_count, threads_per_block,
                    num_trxs,
                    device_data_size,
                    mail_size,
                    max_buffer_size,
                    packet_size,
                    latency,
                    MPI_Wtime(),
                    (char*)(mpi_msg), (char*)(trxs[0].device_data));
            #ifdef HMAP_CUDA_EVALUATION
            ticks cuda_finish = getticks();
            ticks diff = cuda_finish - cuda_start;
            MPI_Send((char*)&diff, sizeof(ticks), MPI_BYTE, 
                    0, 3, MPI_COMM_WORLD);
            #endif
        }

        cerr << "msg size: " << mpi_msg->size << endl;
        //cerr << "[0] buffer size: " << trxs[0].device_data->buffer_size << endl;
        cerr << "[1] buffer size: " << trxs[1].device_data->buffer_size << endl;
        cerr << "[1] head: " << trxs[1].device_data->_head << endl;
        cerr << "[1] tail: " << trxs[1].device_data->_tail << endl;
        //cerr << "[" << rank << "]: "<< "notifying all transceivers" << endl;
        mailbox_flag->notify_all();
        // notify 

    }

    
}

