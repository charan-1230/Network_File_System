#ifndef STORAGE_H
#define STORAGE_H

#include "../headers.h"
#define BUFFER_SIZE 1024
#define SYNC_THRESHOLD 1000
#define MAX_PACKET_LENGTH 100000
#define BUFFER_LENGTH 100006

typedef struct thread_args
{
    char server_ip[16];
    int port;
    int execute_flag;
} thread_args_t;

#endif
