#ifndef CUDA_STRUCTS_H
#define CUDA_STRUCTS_H

typedef struct MPIMsg {
    // sender_id and sender_rank used to identify the 'source' of the msg
    int sender_rank;
    size_t sender_id; // unique id (in the scope of rank)
    double send_x, send_y; //location of sending transceiver
    double send_range; // how far sending transceiver can send
    double send_time;
    size_t size; // how much data is there
    char data; // message contents
} MPIMsg;

typedef struct Mail {
    double send_time;
    bool interference;
    size_t size;
    char data;
} Mail;

typedef struct DeviceData {
    size_t id, rank;
    double x, y, send_range, recv_range;
    double last_send_time;
    size_t buffer_size;
    // meta information for mailbox
    size_t _head;
    size_t _tail;
    Mail _mailbox;
} DeviceData;



// allocate data for the entire mpi pointer
// (MPIMsg*)(new char[sizeof(MPIHeader) + amount of data];)
// free it afterwards

// this is where you could put the queue operations...



#endif // CUDA_STRUCTS_H
