#ifndef CUDA_STRUCTS_H
#define CUDA_STRUCTS_H

typedef struct MPIMsg {
    // sender_id and sender_rank used to identify the 'source' of the msg
    int sender_rank;
    unsigned int sender_id; // unique id (in the scope of rank)
    double send_x, send_y; //location of sending transceiver
    double send_range; // how far sending transceiver can send
    double send_time;
    unsigned int size; // how much data is there
    char data[256]; // message contents
} MPIMsg;

typedef struct Mail {
    double send_time;
    bool interference;
    unsigned int size;
    char data[256];
} Mail;

typedef struct DeviceData {
    int rank;
    unsigned int id;
    double x, y, send_range, recv_range;
    double last_send_time;
    unsigned int buffer_size;
    // meta information for mailbox
    unsigned int _head;
    unsigned int _tail;
    Mail _mailbox[10];
} DeviceData;



// allocate data for the entire mpi pointer
// (MPIMsg*)(new char[sizeof(MPIHeader) + amount of data];)
// free it afterwards

// this is where you could put the queue operations...



#endif // CUDA_STRUCTS_H
