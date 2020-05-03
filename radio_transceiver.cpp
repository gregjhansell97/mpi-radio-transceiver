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

    // set msg information
    mpi_msg->sender_rank = device_data->rank;
    mpi_msg->sender_id = device_data->id;
    mpi_msg->send_x = device_data->x;
    mpi_msg->send_y = device_data->y;
    mpi_msg->send_range = device_data->send_range;
    mpi_msg->size = size;
    // (dest, source, num bytes)
    memcpy(&mpi_msg->data, data, size);
    device_data->last_send_time = MPI_Wtime();

    // send message to root 
    int status = MPI_Send(mpi_msg, mpi_msg_size, MPI_BYTE,
            0, 0, MPI_COMM_WORLD);

    delete [] raw_data; // free up raw data

    double current_time;
    double sleep = latency;
    while(sleep > 0.0) {
        current_time = MPI_Wtime();
        std::this_thread::sleep_for(milliseconds((size_t)(sleep*1000)));
        sleep -= (MPI_Wtime() - current_time);
    }

    return size;
}

ssize_t RadioTransceiver::recv(char** data, const double timeout) {
    double sleep = timeout;
    double current_time = MPI_Wtime();
    while(sleep >= 0) {
        mutex mtx;
        unique_lock<mutex> lk(mtx);
        mailbox_flag->wait_for(
                lk,
                milliseconds((int)(sleep*1000.0)),
                [this]{ return device_data->_head != device_data->_tail; }); 
        // decrement amount of slep
        sleep -= (MPI_Wtime() - current_time);
        current_time = MPI_Wtime(); // recet current time
        char* raw_mailbox = (char*)(&device_data->_mailbox);
        Mail* head = (Mail*)(raw_mailbox + (device_data->_head)*mail_size);
        Mail* tail = (Mail*)(raw_mailbox + (device_data->_tail)*mail_size);
        if(head != tail) { // messages available
            const double next_recv_time = head->send_time + latency;
            if(current_time < next_recv_time) {
                // need to wait
                const double delay = next_recv_time - current_time;
                if(delay > sleep) {
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
                lock_guard<mutex> device_lock(device_mtx);
                device_data->buffer_size -= head->size;
                device_data->_head = next_head;
            } else {
                const size_t data_size = head->size;
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
        }
    }
    return 0;
}

RadioTransceiver* RadioTransceiver::transceivers(
        const size_t num_trxs,
        const size_t max_buffer_size,
        const size_t packet_size,
        const double latency,
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
        cerr << "Error: invalid latency" << endl;
        return nullptr;
    }

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
    send_file_ptr = send_file_ptr;
    recv_file_ptr = recv_file_ptr; 

    RadioTransceiver* trxs = new RadioTransceiver[num_trxs];

    // each mpi message consists of the header information 
    // and at most "packet_size" characters
    mail_size = sizeof(Mail) + (packet_size - 1);
    // each device data has a mailbox of a list of mpi_messages, each 
    // message is of size mpi_msg_size
    device_data_size = 
        sizeof(DeviceData) - sizeof(Mail) + max_buffer_size*(mail_size);

    char* raw_device_data;
    allocate_cuda_memory(&raw_device_data, num_trxs*device_data_size);

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
                    MPI_ANY_SOURCE, // recv channel
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
        //std::cerr << &mpi_msg->data << std::endl;
        // need to aquire a shared lock
        {
            //TODO replace with read-write lock
            std::lock_guard<std::mutex> device_lock(device_mtx); 
            deliver_mpi_msg(
                    num_trxs,
                    device_data_size,
                    mail_size,
                    max_buffer_size,
                    packet_size,
                    latency,
                    MPI_Wtime(),
                    (char*)(mpi_msg), (char*)(trxs[0].device_data));
        }
        mailbox_flag->notify_all();
        // notify 

    }

    
}

