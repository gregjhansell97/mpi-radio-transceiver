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
double RadioTransceiver::latency = 0;
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
    if(size > TRX_PACKET_SIZE) {
        cerr << "ERROR: message size is too big" << endl;
        return -1;
    } else if(size <= 0) {
        return 0;
    }

    MPIMsg mpi_msg;

    double current_time = MPI_Wtime();
    mpi_msg.sender_rank = device_data->rank;
    mpi_msg.sender_id = device_data->id;
    mpi_msg.send_x = device_data->x;
    mpi_msg.send_y = device_data->y;
    mpi_msg.send_time = current_time;
    mpi_msg.send_range = device_data->send_range;
    mpi_msg.size = size;
    // (dest, source, num bytes)
    memcpy(mpi_msg.data, data, size);
    device_data->last_send_time = MPI_Wtime();

    // send message to root 
#ifdef TRX_COMM_EVALUATION_MODE
    ticks t = getticks();
#endif
    int status = MPI_Send(&mpi_msg, sizeof(MPIMsg), MPI_BYTE,
            0, 0, MPI_COMM_WORLD);
    //cerr << "[" << device_data->rank << ", " << device_data->id << "]: " 
    //     << "sent " << size << " bytes" << endl;

    if(status != 0) {
        cerr << "Send failed, MPI failed to send message to leader " 
             << "status code: " << status << endl;
        return -1; 
    }
    if(send_file_ptr != nullptr) {
        SendLogItem item;
        item.x = mpi_msg.send_x;
        item.y = mpi_msg.send_y;
        item.time = mpi_msg.send_time;
        item.send_range = mpi_msg.send_range;
        item.size = mpi_msg.size;
        memcpy(item.data, mpi_msg.data, size);
        MPI_Status fstatus;
        MPI_File_write_shared(*send_file_ptr, &item, sizeof(SendLogItem), 
                MPI_BYTE, &fstatus);
        int count; 
        MPI_Get_count(&fstatus, MPI_BYTE, &count);
        if(count != sizeof(SendLogItem)) {
            cerr << "ERROR: failed to write item to file" << endl;
        }
    }

#ifdef TRX_COMM_EVALUATION_MODE
    status = MPI_Send((char*)(&t), sizeof(ticks), MPI_BYTE,
            0, 1, MPI_COMM_WORLD);
#endif
    double sleep = latency;

    while(sleep > 0.0) {
        current_time = MPI_Wtime();
        std::this_thread::sleep_for(milliseconds((size_t)(sleep*1000)));
        sleep -= (MPI_Wtime() - current_time);
    }

    return size;
}

ssize_t RadioTransceiver::recv(char** data, const double timeout) {
#ifdef TRX_DEBUG_MODE
    cerr << "[" << device_data->rank << ", " << device_data->id 
         << " ]: buffer size: " << device_data->buffer_size << endl;
#endif
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
        Mail& head = device_data->_mailbox[device_data->_head];
        if(device_data->buffer_size > 0) { // messages available
#ifdef TRX_DEBUG_MODE
            cerr << "[" << device_data->rank << ", " << device_data->id 
                 << "]: message is available" << endl;
#endif
            const double next_recv_time = head.send_time + latency;
            if(current_time < next_recv_time) {
                // need to wait
#ifdef TRX_DEBUG_MODE
                cerr << "[" << device_data->rank << ", " << device_data->id 
                    << "]: I need to wait until " << next_recv_time 
                    << " but it is currently " << current_time << endl;
#endif
                double delay = next_recv_time - current_time;
                if(delay > sleep) {
#ifdef TRX_DEBUG_MODE
                    cerr << "[" << device_data->rank << ", " << device_data->id 
                        << "]: delay " << delay << " sleep " << sleep << endl;
#endif
                    break;
                } else {
                    // sleep for the delay
                    do {
                        std::this_thread::sleep_for(
                                milliseconds((int)((delay*1000 + 0.5))));
                        delay = next_recv_time - MPI_Wtime();
                    } while(delay > 0);
                }
            }
            // if I got here I can remove message
            const size_t next_head = (device_data->_head + 1) % TRX_BUFFER_SIZE;
            if(head.interference) {
#ifdef TRX_DEBUG_MODE
                cerr << "[" << device_data->rank << ", " << device_data->id 
                     << " ]: interference detected" << endl;
#endif
                lock_guard<mutex> device_lock(device_mtx);
                device_data->buffer_size -= head.size;
                device_data->_head = next_head; // move to next
            } else {
                const size_t data_size = head.size;
#ifdef TRX_DEBUG_MODE
                cerr << "[" << device_data->rank << ", " << device_data->id 
                     << " ]: received data (" << data_size << ")" << endl;
#endif
                memcpy(m_rcvd, head.data, head.size);
                {
                    lock_guard<mutex> device_lock(device_mtx);
                    device_data->buffer_size -= head.size;
                    device_data->_head = next_head;
                }
                *data = m_rcvd;
                if(recv_file_ptr != nullptr) {
                    RecvLogItem item;
                    item.x = device_data->x;
                    item.y = device_data->y;
                    item.time = MPI_Wtime();
                    item.recv_range = device_data->recv_range;
                    item.size = data_size;
                    memcpy(item.data, m_rcvd, data_size);
                    MPI_Status fstatus;
                    MPI_File_write_shared(
                            *send_file_ptr, &item, sizeof(RecvLogItem), 
                            MPI_BYTE, &fstatus);
                    int count; 
                    MPI_Get_count(&fstatus, MPI_BYTE, &count);
                    if(count != sizeof(SendLogItem)) {
                        cerr << "ERROR: failed to write item to file" << endl;
                    }
                }
                return data_size;
            }
            sleep -= (MPI_Wtime() - current_time);
#ifdef TRX_DEBUG_MODE
            cerr << "[" << device_data->rank << ", " << device_data->id 
                 << " ]: looping again-sleep: " << sleep << endl;
#endif
        }
    }
    *data = nullptr;
    return 0;
}

RadioTransceiver* RadioTransceiver::transceivers(
        const size_t num_trxs,
        const double latency,
        const ushort threads_per_block,
        MPI_File* send_file_ptr, MPI_File* recv_file_ptr) {
    // verify proper inputs
    if(TRX_BUFFER_SIZE <= 0) {
        cerr << "ERROR: buffer needs to be atleast 1" << endl; 
        return nullptr;
    } else if(TRX_PACKET_SIZE <= 0) {
        cerr << "ERROR: packet size needs to be atleast 1" << endl;
        return nullptr;
    } else if (TRX_PACKET_SIZE > TRX_BUFFER_SIZE) {
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

    char* raw_device_data = nullptr;
    allocate_cuda_memory(&raw_device_data, num_trxs*sizeof(DeviceData)); 
    assert(raw_device_data != nullptr);
    
    DeviceData* device_data = (DeviceData*)raw_device_data;
    for(size_t i = 0; i < num_trxs; ++i) {
        // device data of a transceiver
        DeviceData* d = &device_data[i];
        d->rank = rank;
        d->id = i;
        d->x = 0.0;
        d->y = 0.0;
        d->last_send_time = MPI_Wtime() - 2*latency;
        d->send_range = 0.0;
        d->recv_range = 0.0;
        d->buffer_size = 0;
        d->_head = 0;
        d->_tail = 0; 
        trxs[i].device_data = d;
    }

    mailbox_flag = new condition_variable;
    mpi_listener_thread = new thread(mpi_listener, trxs);
    // start thread
    return trxs;
}

void RadioTransceiver::close_transceivers(RadioTransceiver* trxs) {
    // this is where the close message is sent
    if(rank == 0) {
        MPIMsg poison_pill;
        poison_pill.size = 0;
        // send poison pill
        int status = MPI_Send(&poison_pill, sizeof(MPIMsg), MPI_BYTE,
                0, 0, MPI_COMM_WORLD);
        if(status != 0) {
            cerr << "Close down error, MPI failed to send poison pill " 
                 << "status code: " << status << endl;
        }
    }
    MPI_Barrier(MPI_COMM_WORLD); // wait till all ranks want to close
    // join the thread;
    mpi_listener_thread->join();
    delete mpi_listener_thread;
    mpi_listener_thread = nullptr;

    char* raw_device_data = (char*)trxs[0].device_data;
    free_cuda_memory(raw_device_data); 
    delete [] trxs;
    delete mailbox_flag;
}



void RadioTransceiver::mpi_listener(RadioTransceiver* trxs) {
    // allocate data 
    char* raw_mpi_msg;
    // get from cuda memory
    allocate_cuda_memory(&raw_mpi_msg, sizeof(MPIMsg));//max_mpi_msg_size);
    MPIMsg* mpi_msg = (MPIMsg*)(raw_mpi_msg);

    MPI_Status status;
    while(true) {
        // create a new shared pointer
        if(rank == 0) {
            // waiting on messages
            MPI_Recv(
                    mpi_msg,
                    sizeof(MPIMsg),
                    MPI_BYTE,
                    MPI_ANY_SOURCE, 
                    0,
                    MPI_COMM_WORLD,
                    &status);
            // send to rest of group
            MPI_Bcast(
                    mpi_msg,
                    sizeof(MPIMsg),
                    MPI_BYTE,
                    0,
                    MPI_COMM_WORLD);
#ifdef TRX_COMM_EVALUATION_MODE
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
                    sizeof(MPIMsg),
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
#ifdef TRX_CUDA_EVALUATION_MODE
            ticks cuda_start = getticks();
#endif
            deliver_mpi_msg(
                    blocks_count, threads_per_block,
                    num_trxs,
                    latency,
                    MPI_Wtime(),
                    mpi_msg, trxs[0].device_data);
            synchronize_cuda_devices();
#ifdef TRX_CUDA_EVALUATION_MODE
            ticks cuda_finish = getticks();
            ticks diff = cuda_finish - cuda_start;
            MPI_Send((char*)&diff, sizeof(ticks), MPI_BYTE, 
                    0, 3, MPI_COMM_WORLD);
#endif
        }

#ifdef TRX_DEBUG_MODE
        cerr << "msg size: " << mpi_msg->size << endl;
        //cerr << "[0] buffer size: " << trxs[0].device_data->buffer_size << endl;
        cerr << "[1] buffer size: " << trxs[1].device_data->buffer_size << endl;
        cerr << "[1] head: " << trxs[1].device_data->_head << endl;
        cerr << "[1] tail: " << trxs[1].device_data->_tail << endl;
        //cerr << "[" << rank << "]: "<< "notifying all transceivers" << endl;
#endif
        mailbox_flag->notify_all();
    }
}

