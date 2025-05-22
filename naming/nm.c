// #include "../headers.h"
#include "nm.h"

char naming_ip[16];
char delim = '\n';

ss_t sss[MAXCONS][3];
int num_clients = 0, num_storages = 0, num_main_sss = 0;
int nm_for_client, nm_for_storage;
struct sockaddr_in address_for_client, address_for_storage;
int addrlen = sizeof(address_for_client);

cache_t cache;
int one = 1;
int zero = 0;

pthread_mutex_t LogLock = PTHREAD_MUTEX_INITIALIZER; ///////////////

void logMsg(char *msg)
{
    pthread_mutex_lock(&LogLock);
    FILE *logFile = fopen("../logfile.txt", "a+");
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    fprintf(logFile, "time:[%02d-%02d-%04d %02d:%02d:%02d]\n",
            local->tm_mday, local->tm_mon + 1, local->tm_year + 1900,
            local->tm_hour, local->tm_min, local->tm_sec);
    if (logFile != NULL)
    {
        fprintf(logFile, "%s\n", msg);
        fclose(logFile);
    }
    else
    {
        printf("error opening log file\n");
    }
    pthread_mutex_unlock(&LogLock);
} //////////////////


bool path_in_acc_paths(char paths[], char path[]) {
    char *dup_path = strdup(paths);
    char *save_token;
    char *token = strtok_r(dup_path, "#", &save_token);
    while (token != NULL) {
        printf("token: %s\n", token);
        if (strcmp(token, path) == 0) {
            free(dup_path);
            return true;
        }
        token = strtok_r(NULL, "#", &save_token);
    }
    free(dup_path);
    return false;
}

ss_t *search(char path[]) {
    printf("num_main_sss: %d\n", num_main_sss);
    for (int i = 0; i < num_main_sss; i++) {
        if (path_in_acc_paths(sss[i][0].accessible_paths, path)) {
            return &sss[i][0];
        }
    }
    return NULL;
}

void replace_chars(char a, char b, char str[]) {
    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        if (str[i] == a) {
            str[i] = b;
        }
    }
}

void send_all_paths(int client_sock) {
    for (int i = 0; i < num_main_sss; i++) {
        // replace_chars('#', '\n', sss[i][0].accessible_paths);
        send(client_sock, sss[i][0].accessible_paths, sizeof(sss[i][0].accessible_paths), 0);
    }
    // send(client_sock, "STOP", 4, 0);
}

void print_all_paths() {
    for (int i = 0; i < num_main_sss; i++) {
        replace_chars(delim, '\n', sss[i][0].accessible_paths);
        printf("%s\n%s, %d\n", sss[i][0].accessible_paths, sss[i][0].ip, sss[i][0].port_for_client);
    }
}

char *getModifiedRelativePath(char *basePath, char *fullPath)
{
    size_t basePathLen = strlen(basePath);
    if (strncmp(basePath, fullPath, basePathLen) != 0)
    {
        return NULL;
    }
    char *lastSlash = strrchr(basePath, '/');
    if (!lastSlash)
    {
        return NULL;
    }
    size_t includeLength = lastSlash - basePath + 1;
    char *relativePath = fullPath + includeLength;
    return strdup(relativePath);
}

void get_dest_path(char curr_src_path[], char curr_dest_path[], char abs_src_path[], char abs_dest_path[]) {
    char *answer = getModifiedRelativePath(abs_src_path, curr_src_path);
    strcpy(curr_dest_path, abs_dest_path);
    strcat(curr_dest_path, "/");
    strcat(curr_dest_path, answer);
}

void processNode(char *path, int src_socket_for_client, int dest_socket_for_client, int dest_socket_for_nm, char abs_src_path[], char abs_dest_path[], int client_sock) {
    client_req_t req_data;
    int is_dir = -1;
    
    // read
    req_data.clientsocket = 0;
    strcpy(req_data.destpath, "");
    strcpy(req_data.filename, "");
    req_data.sync = 1;
    strcpy(req_data.oper_name, "Read");
    strcpy(req_data.srcpath, path);
    send(src_socket_for_client, &req_data, sizeof(req_data), 0);

    // recv data and store it in a buffer
    char buf[1024];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int received = recv(src_socket_for_client, buf, sizeof(buf) - 1, 0); // Reserve space for null
        if (received > 0) {
            buf[received] = '\0'; // Null-terminate the string
        }
        int n = strlen(buf);

        if (buf[n - 4] == 'S' && buf[n - 3] == 'T' && buf[n - 2] == 'O' && buf[n - 1] == 'P') {
            buf[n - 4] = '\0';
            printf("%s\n", buf);
            break;
        }
        printf("%s\n", buf);
    }

    // read data/ack fill is_dir
    int code;
    recv(src_socket_for_client, &code, sizeof(int), 0);

    if (code == 103) {
        int err = CFF;
        send(client_sock, &err, sizeof(int), 0);
        close(client_sock);
    }
    
    // create
    char curr_dest_path[1024];
    get_dest_path(path, curr_dest_path, abs_src_path, abs_dest_path);
    strcpy(req_data.oper_name, "Create");
    char *last = strrchr(path, '/');
    strcpy(req_data.filename, last + 1);
    if (code == 112) { // get it from the ack from read
        strcat(req_data.filename, "/");
    }
    char *new_str = strdup(path);
    int index = strrchr(path, '/') - path;
    new_str[index] = '\0';
    strcpy(req_data.srcpath, new_str);
    send(dest_socket_for_nm, &req_data, sizeof(req_data), 0);
    free(new_str);
    
    if (code != 112) {
        // write
        strcpy(req_data.oper_name, "Write");
        strcpy(req_data.srcpath, curr_dest_path);
        req_data.sync = 1;
        send(dest_socket_for_client, &req_data, sizeof(req_data), 0);
    }
}

void copy_function(char src_path[], char dest_path[], char all_paths[], int src_socket_for_client, int dest_socket_for_client, int dest_socket_for_nm, int client_sock) {
    char *save_token;
    char *token = strtok_r(all_paths, "\n", &save_token);
    while (token != NULL) {
        printf("token: %s\n", token);
        if ((strlen(token) >= strlen(src_path)) && (strncmp(token, src_path, strlen(src_path)) == 0)) {
            // handle processNode
            processNode(token, src_socket_for_client, dest_socket_for_client, dest_socket_for_nm, src_path, dest_path, client_sock);
        }
        token = strtok_r(NULL, "\n", &save_token);
    }
}

ss_t *backup_for(ss_t *storage_server) {
    printf("ssi in backup_for: %d\n", storage_server->idx);
    if ((num_storages % 3 == 1 && num_storages != 4) || num_storages == 2) {
        // not a backup
        sss[num_main_sss][0] = *storage_server;
        storage_server->idx = num_main_sss;
        printf("1: %d\n", storage_server->idx);
        sss[num_main_sss][0].idx = num_main_sss;
        num_main_sss++;
        return NULL;
    }
    else {
        // is a backup
        for (int j = 1; j < 3; j++) {
            for (int i = 0; i < num_main_sss; i++) {
                if (sss[i][j].port_for_nm == 0) {
                    // that space is empty
                    sss[i][j] = *storage_server;
                    strcpy(sss[i][j].accessible_paths, sss[i][0].accessible_paths);
                    sss[i][j].idx = i;
                    storage_server->idx = i;

                    // take the first path and copy from main to backup (call copy)
                    return &sss[i][0];
                }
            }
        }
    }
    return NULL;
}

bool was_already_there(ss_t ss) {
    for (int i = 0; i < num_main_sss; i++) {
        if (strcmp(ss.ip, sss[i][0].ip) == 0) {
            sss[i][0].port_for_client = ss.port_for_client;
            sss[i][0].port_for_nm = ss.port_for_nm;
            sss[i][0].port_for_ss = ss.port_for_ss;
            return true;
        }
    }
    return false;
}

void init_sss() {
    for (int i = 0; i < MAXCONS; i++) {
        for (int j = 0; j < 3; j++) {
            strcpy(sss[i][j].ip, "");
            sss[i][j].port_for_client = -1;
            sss[i][j].port_for_ss = 0;
            sss[i][j].port_for_nm = 0;
            strcpy(sss[i][j].accessible_paths, "");
            sss[i][j].idx = i;
        }
    }
}

void init_cache() {
    for (int i = 0; i < LRU_CACHE_SIZE; i++) {
        strcpy(cache.cache_entries[i].path, "");
        cache.cache_entries[i].ss = NULL;
        cache.cache_entries[i].last_used_time = 0;
    }
    cache.num_entries = 0;
}

int is_socket_connected(int sockfd) {
    int error = 0;
    socklen_t len = sizeof(error);

    // Check the socket's error status
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) == 0) {
        if (error == 0) {
            return 1; // Socket is connected
        }
    }
    return 0; // Not connected
}

int create_socket(ss_t *ss, int nm_or_client, int *modify) { // nm_or_client: 0 -> nm, 1 -> client
    int sockfd;
    struct sockaddr_in servaddr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    servaddr.sin_family = AF_INET;
    int i = 0, j = ss->idx;
    // for (i = 0; i < 3; i++) {
    //     if (strcmp(ss->ip, sss[ss->idx][i].ip) == 0) {
    //         break;
    //     }
    // }

    // i = 0;
    printf("ip in create_socket1: %s, %d, %d\n", ss->ip, ss->idx, ss->port_for_nm);
    while (i < 3) {
        if (nm_or_client == 0) {
            servaddr.sin_port = htons(sss[j][i].port_for_nm);
        }
        else {
            servaddr.sin_port = htons(sss[j][i].port_for_client);
        }

        printf("ip in create_socket2: %s, %d\n", sss[i][j].ip, ss->port_for_nm);
        if (inet_pton(AF_INET, sss[j][i].ip, &servaddr.sin_addr) <= 0) {
            perror("Invalid address/address format");
            exit(EXIT_FAILURE);
        }

        if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            if (errno == EISCONN) {
                printf("Connection already established.\n");
                break;
            }
            i++;
            if (i == 3) {
                // all out
                return -1;
            }
            continue;
        }
        printf("test1\n");
        break;
    }

    // remove modify access if it is a backup
    if (i > 0) {
        *modify = 0;
    }
    return sockfd;
}

ss_t *find_ss(char accessible_paths[]) {
    for (int i = 0; i < num_storages; i++) {
        if (sss[i] && strcmp(sss[i]->accessible_paths, accessible_paths) == 0) {
            return sss[i];
        }
    }
    return NULL;
}

ss_t *check_cache(char path[]) {
    for (int i = 0; i < LRU_CACHE_SIZE; i++) {
        if (strcmp(cache.cache_entries[i].path, path) == 0) {
            cache.cache_entries[i].last_used_time = time(0);
            return cache.cache_entries[i].ss;
        }
    }
    printf("Cache miss\n");
    return NULL;
}

void add_to_cache(ss_t *ss, char path[]) {
    // check if there's more space in the cache
    if (cache.num_entries < LRU_CACHE_SIZE) {
        strcpy(cache.cache_entries[cache.num_entries].path, path);
        cache.cache_entries[cache.num_entries].ss = ss;
        cache.cache_entries[cache.num_entries].last_used_time = time(0);
        cache.num_entries++;
    }
    
    // if no space, remove the lru entry
    int min_index = 0;
    for (int i = 1; i < LRU_CACHE_SIZE; i++) {
        if (cache.cache_entries[i].last_used_time < cache.cache_entries[min_index].last_used_time) {
            min_index = i;
        }
    }
    strcpy(cache.cache_entries[min_index].path, path);
    cache.cache_entries[min_index].ss = ss;
    cache.cache_entries[min_index].last_used_time = time(0);
    cache.num_entries++;
}

void create_delete(int ss_socket_for_nm, client_req_t incoming_data, int client_sock) {
    // send this request to corresponding storage server
    send(ss_socket_for_nm, &incoming_data, sizeof(incoming_data), 0);
     logMsg("Request was sent to corresponding storage server");
    // wait for storage server's acknowledgement
    int res_code;
    recv(ss_socket_for_nm, &res_code, sizeof(&res_code) - 1, 0);
    // logMsg();
    char* str;  // Buffer to hold the string
    sprintf(str, "%d", res_code);  // Convert integer to string
    char* str1="ACK sent from the storage server was ";
    strcat(str1,str);
    logMsg(str1);
    printf("res_code: %d\n", res_code);

    // send the received acknowledgement to client
    send(client_sock, &res_code, sizeof(int), 0);
}


void* handle_client(void* args) {
    logMsg("Client connected\n");
    printf("Client connected\n");
    // print_all_paths();
    int sock = *(int *)args;
    client_req_t incoming_data;
    int num_bytes = recv(sock, &incoming_data, sizeof(incoming_data), 0);
    if (num_bytes < 0) {
        perror("Receive failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    logMsg("Received data from client\n");
    printf("Received data from client\n");

    // send ack to client that it's request has been considered for processing
    send(sock, "Your request has been considered for processing\n", sizeof("Your request has been considered for processing\n"), 0);

    ss_t *src_ss;
    int modify_src = 1;
    int src_socket_for_client, src_socket_for_nm;
    client_response_t cr;
    printf("operation1: %s\n", incoming_data.oper_name);
    if (strcasecmp(incoming_data.oper_name, "List") != 0) {
        src_ss = check_cache(incoming_data.srcpath);
        if (src_ss == NULL) {
            printf("src_path: %s\n", incoming_data.srcpath);
            src_ss = search(incoming_data.srcpath);
            if (src_ss == NULL) {
                // path not found
                int err = FNF;
                send(sock, &err, sizeof(int), 0); 
                close(sock);
                pthread_exit(NULL);
            }
            printf("hc1: ip: %s, port: %d\n", src_ss->ip, src_ss->port_for_client);
            
            // add this to cache
            add_to_cache(src_ss, incoming_data.srcpath);
        }

        // connect to ss via the info provided to nm by ss
        src_socket_for_nm = create_socket(src_ss, 0, &modify_src);
        src_socket_for_client = create_socket(src_ss, 1, &modify_src);

        // if either of the sockets = -1 then bye bye
        if (src_socket_for_client == -1 || src_socket_for_nm == -1) {
            int err = ECS;
            send(sock, &err, sizeof(int), 0); 
            close(sock);
            pthread_exit(NULL);
        }
        
        strcpy(cr.ip, src_ss->ip);
        cr.port = src_ss->port_for_client;
    }

    // printf("operation2: %s\n", incoming_data.oper_name);

    // printf("operation3: %s\n", incoming_data.oper_name);
    if((strcasecmp(incoming_data.oper_name, "Read") == 0) || (strcasecmp(incoming_data.oper_name, "Stream") == 0) || (strcasecmp(incoming_data.oper_name, "GetSP") == 0)) { // read operations
        int err = OK;
        send(sock, &err, sizeof(int), 0);
        printf("hc2: ip: %s, port: %d\n", cr.ip, cr.port);
        send(sock, &cr, sizeof(cr), 0);
    }
    else if (strcasecmp(incoming_data.oper_name, "Write") == 0) {
        if (modify_src) {
            // send the ip and port to client
            int err = OK;
            send(sock, &err, sizeof(int), 0); 
            send(sock, &cr, sizeof(cr), 0);
        }
        else {
            // send ack and close
            int err = PERM;
            send(sock, &err, sizeof(int), 0); 
            close(sock);
            pthread_exit(NULL);
        }
    }
    else if(strcasecmp(incoming_data.oper_name, "Create") == 0) {
        if (modify_src) {
            create_delete(src_socket_for_nm, incoming_data, sock);
        }
        else {
            // send ack and close
            int err = PERM;
            send(sock, &err, sizeof(int), 0); 
            close(sock);
            pthread_exit(NULL);
        }
    }
    else if (strcasecmp(incoming_data.oper_name, "Delete") == 0) {
        if (modify_src) {
            create_delete(src_socket_for_nm, incoming_data, sock);
        }
        else {
            // send ack and close
            int err = PERM;
            send(sock, &err, sizeof(int), 0); 
            close(sock);
            pthread_exit(NULL);
        }
    }
    else if(strcasecmp(incoming_data.oper_name, "Copy") == 0) {
        ss_t* dest_ss = check_cache(incoming_data.destpath);
        if (dest_ss == NULL) {
            dest_ss = search(incoming_data.destpath);
            if (src_ss == NULL) {
                // path not found
                int err = FNF;
                send(sock, &err, sizeof(int), 0); 
                close(sock);
                pthread_exit(NULL);
            }
            
            // add this to cache
            add_to_cache(src_ss, incoming_data.destpath);
        }

        int modify_dest = 1;
        int dest_socket_for_nm = create_socket(dest_ss, 0, &modify_dest);
        int dest_socket_for_client = create_socket(dest_ss, 1, &modify_dest);

        if (modify_dest == 0) {
            // send ack to client
            int err = PERM;
            send(sock, &err, sizeof(int), 0); // decide what error code to return
            close(sock);
            pthread_exit(NULL);
        }

        // if either of the sockets = -1 then bye bye
        if (dest_socket_for_client == -1 || dest_socket_for_nm == -1) {
            int err = ECS;
            send(sock, &err, sizeof(int), 0); 
            close(sock);
            pthread_exit(NULL);
        }
    }
    else if(strcasecmp(incoming_data.oper_name, "List") == 0) { // read  operation
        // new function
        send_all_paths(sock);

        // send '\n' to the client to indicate the end of the list
        send(sock, "STOP\0", 5, 0);
    }

    // receiving closing ack from client
    char final_ack[100];
    recv(sock, final_ack, sizeof(final_ack), 0);
    logMsg(final_ack);
    printf("final_ack: %s\n",final_ack);

    // close connection
    close(sock); // thinking

    pthread_exit(NULL);
    // return NULL;
}

void *client_connection_handler(void *args) {
    pthread_t client_threads[MAXCONS];
    int new_socket1;
    
    // Accept an incoming connection
    while (1) {
        if ((new_socket1 = accept(nm_for_client, (struct sockaddr *)&address_for_client, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            close(nm_for_client);
            exit(EXIT_FAILURE);
        }
        logMsg("New client detected\n");
        pthread_create(&client_threads[num_clients], NULL, handle_client, (void *)&new_socket1);
        num_clients++;
    }

    return NULL;
}

void *handle_storage_server(void *args) {
    logMsg("Storage server connected\n");
    printf("Storage server connected\n");
    int sock = *(int *)args;
    ss_t incoming_data;
    int num_bytes = recv(sock, &incoming_data, sizeof(incoming_data), 0);
    if (num_bytes < 0) {
        perror("Receive failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    logMsg("Received data from storage server\n");
    printf("Received data from storage server\n");

    printf("hss: ip: %s, port: %d\n", incoming_data.ip, incoming_data.port_for_client);

    ss_t *finding;
    // check if it was a previously existing one
    if (!was_already_there(incoming_data)) {
        // determine the role of the current server
        if ((finding = backup_for(&incoming_data)) != NULL) {
            // it is a backup
        }
        send(sock, "SS Initialisation successful\n", strlen("SS Initialisation successful\n"), 0);
        logMsg("SS Initialisation successful\n");
        printf("SS Initialisation successful\n");
        print_all_paths();
    }
    close(sock);
    pthread_exit(NULL);
}

void *storage_connection_handler(void *args) {
    pthread_t storage_server_threads[MAXCONS];
    int new_socket2;
    
    // Accept an incoming connection
    while (1) {
        if ((new_socket2 = accept(nm_for_storage, (struct sockaddr *)&address_for_storage, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            close(nm_for_storage);
            exit(EXIT_FAILURE);
        }
        logMsg("New storage server connected\n");
        printf("New storage server connected\n");
        pthread_create(&storage_server_threads[num_storages], NULL, handle_storage_server, (void *)&new_socket2);
        num_storages++;
    }

    return NULL;
}

int main(int argc, char* argv[]) {

    // take from cmd line interface
    if (argc == 2) {
        strcpy(naming_ip, argv[1]);
    }
    else {
        strcpy(naming_ip, "127.0.0.1");
    }

    // For client connections
    
    // Create socket file descriptor
    if ((nm_for_client = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Configure the address structure
    memset(&address_for_client, 0, addrlen);
    address_for_client.sin_family = AF_INET;
    address_for_client.sin_port = htons(CLIENT_PORT);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, naming_ip, &address_for_client.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(nm_for_client);
        exit(EXIT_FAILURE);
    }

    if (bind(nm_for_client, (struct sockaddr *)&address_for_client, addrlen) < 0) {
        perror("Bind failed");
        close(nm_for_client);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(nm_for_client, MAXCONS) < 0) {
        perror("Listen failed");
        close(nm_for_client);
        exit(EXIT_FAILURE);
    }

    // For storage connections
    if ((nm_for_storage = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Configure the address structure
    memset(&address_for_storage, 0, addrlen);
    address_for_storage.sin_family = AF_INET;
    address_for_storage.sin_port = htons(STORAGE_PORT);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, naming_ip, &address_for_storage.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(nm_for_storage);
        exit(EXIT_FAILURE);
    }

    if (bind(nm_for_storage, (struct sockaddr *)&address_for_storage, addrlen) < 0) {
        perror("Bind failed");
        close(nm_for_storage);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(nm_for_storage, MAXCONS) < 0) {
        perror("Listen failed");
        close(nm_for_storage);
        exit(EXIT_FAILURE);
    }

    init_cache(); // initialize the LRU cache

    printf("Server listening for client on port %d\n", CLIENT_PORT);
    printf("Server listening for storage on port %d\n", STORAGE_PORT);

    pthread_t client_connections, storage_connections;
    pthread_create(&client_connections, NULL, client_connection_handler, NULL);
    pthread_create(&storage_connections, NULL, storage_connection_handler, NULL);

    pthread_join(client_connections, NULL);
    pthread_join(storage_connections, NULL);


    // Close sockets when done
    close(nm_for_client);
    close(nm_for_storage);

    return 0;
}
