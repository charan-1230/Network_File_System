#ifndef NM_H
#define NM_H

#include "../headers.h"

#define ALPHABET_SIZE 26 // Number of letters in the English alphabet
#define LRU_CACHE_SIZE 10

// for nm.c
#define CLIENT_PORT 8080
#define STORAGE_PORT 8081
#define MAXCONS 50 // Maximum number of connections to the server

void *handle_client(void* args);
void *client_connection_handler(void *args);
void *handle_storage_server(void *args);
void *storage_connection_handler(void *args);

typedef struct cache_node {
    char path[1024];
    ss_t *ss;
    time_t last_used_time;
} cache_node_t;

typedef struct cache {
    cache_node_t cache_entries[LRU_CACHE_SIZE];
    int num_entries;
} cache_t;

typedef struct dest_ss_details {
    char ip[16];
    int port;
} dest_ss_details_t;

TrieNode *createNode(const char *name, ss_t *storage_server);
TrieNode *findChild(TrieNode *node, const char *name);
void insert(const char *path, struct ss *storage_server);
TrieNode *search_node(const char *path);
void display(TrieNode *node, char *prefix, int client_socket);
void print_trie(TrieNode *node);
void freeTrie(TrieNode *node);

#endif
