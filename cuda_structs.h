#ifndef CUDA_STRUCTS_H
#define CUDA_STRUCTS_H



#define TRX_BUFFER_SIZE 1024
#define TRX_PACKET_SIZE 16


typedef struct MPIMsg {
    // sender_id and sender_rank used to identify the 'source' of the msg
    int sender_rank;
    unsigned int sender_id; // unique id (in the scope of rank)
    double send_x, send_y; //location of sending transceiver
    double send_range; // how far sending transceiver can send
    double send_time;
    unsigned int size; // how much data is there
    char data[TRX_PACKET_SIZE]; // message contents
} MPIMsg;

typedef struct Mail {
    double send_time;
    bool interference;
    unsigned int size;
    char data[TRX_PACKET_SIZE];
} Mail;

typedef struct DeviceData {
    int rank;
    unsigned int id;
    double x, y, send_range, recv_range;
    double last_send_time;
    unsigned int buffer_size; // current size of buffer <= TRX_BUFFER_SIZE
    // meta information for mailbox
    unsigned int _head;
    unsigned int _tail;
    Mail _mailbox[TRX_BUFFER_SIZE]; // worse case this many in mail
} DeviceData;


#endif // CUDA_STRUCTS_H
