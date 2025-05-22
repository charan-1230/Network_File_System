#include "storage_server.h"

pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER;    // Lock for mutual exclusion for readers and writers
pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER; // Lock for reader count updates
pthread_cond_t rw_cond = PTHREAD_COND_INITIALIZER;

int reader_count = 0; // Number of active readers
int writer_active = 0;

int g_client_socket, g_nm_socket;
int num_clients = 0, num_nm = 0;
struct sockaddr_in address_for_client, address_for_nm;
int addrlen = sizeof(address_for_client);

// size_t size_of_SS(ss_t S){
//     size_t size_ss = sizeof(S.port_for_nm);
//     size_ss += sizeof(S.port_for_ss);
//     size_ss += sizeof(S.ip);
//     size_ss += sizeof(S.accessible_paths);
//     size_ss += sizeof(S.port_for_ss);
//     return size_ss;
// }

int get_available_port()
{
    int sock;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        perror("socket");
        return -1;
    }

    // Bind the socket to port 0 (let OS assign an available port)
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // Bind to any local address
    addr.sin_port = htons(0);          // Let the OS choose the port
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind");
        close(sock);
        return -1;
    }

    // Get the assigned port
    if (getsockname(sock, (struct sockaddr *)&addr, &addr_len) == -1)
    {
        perror("getsockname");
        close(sock);
        return -1;
    }

    int available_port = ntohs(addr.sin_port); // Convert port from network byte order
    close(sock);                               // Close the socket (port will be freed)

    return available_port;
}

int get_ss_ip(char ss_ip[])
{
    int wifi_flag = 0;
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
            continue;
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            if (strncmp(ifa->ifa_name, "wlo1", 4) == 0)
            {
                if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                                ss_ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0)
                {
                    wifi_flag = 1;
                    break;
                }
            }
        }
    }
    if (wifi_flag == 0)
        printf("Device is not connected to Wi-Fi.\n");

    freeifaddrs(ifaddr);
    return wifi_flag;
}

void list_files_from_dir(const char *base_path, ss_t *ss)
{
    struct dirent *entry;
    struct stat statbuf;
    char path[1024], abs_path[1024];

    DIR *dir = opendir(base_path);
    if (dir == NULL)
    {
        perror("Unable to open directory");
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", base_path, entry->d_name);
        if (realpath(path, abs_path) == NULL)
        {
            perror("realpath");
            continue;
        }
        if (stat(path, &statbuf) == -1)
        {
            perror("stat");
            continue;
        }
        if (S_ISDIR(statbuf.st_mode))
        {
            strcat(ss->accessible_paths, abs_path);
            strcat(ss->accessible_paths, "#");
            // printf("Directory: %s\n", path);
            list_files_from_dir(path, ss);
        }
        else if (S_ISREG(statbuf.st_mode))
        {
            strcat(ss->accessible_paths, abs_path);
            strcat(ss->accessible_paths, "#");
            // printf("File: %s\n", path);
        }
    }
    closedir(dir);
}

void get_list_of_accesible_paths(ss_t *ss)
{
    char input_buffer[100], absol_path[1024];
    ss->accessible_paths[0] = '\0';
    printf("Enter folder paths that you wish to use for network storage (type 'quit' to quit):\n");
    struct stat statbuf;
    while (1)
    {
        printf("> ");
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
        {
            perror("Error reading input");
            return;
        }
        if (strlen(input_buffer) == 1)
        {
            continue;
        }
        else if (strncmp(input_buffer, "quit", 4) == 0)
        {
            break;
        }
        input_buffer[strcspn(input_buffer, "\n")] = '\0';
        if (realpath(input_buffer, absol_path) == NULL)
        {
            perror("Error: Invalid path or path does not exist");
            continue;
        }
        if (stat(input_buffer, &statbuf) == -1)
        {
            perror("Error: Path does not exist or is inaccessible");
            return;
        }
        if (S_ISDIR(statbuf.st_mode))
        {
            strcat(ss->accessible_paths, absol_path);
            strcat(ss->accessible_paths, "#");
            // printf("Directory: %s\n", input_buffer);
            list_files_from_dir(input_buffer, ss);
        }
        else if (S_ISREG(statbuf.st_mode))
        {
            strcat(ss->accessible_paths, absol_path);
            strcat(ss->accessible_paths, "#");
            // printf("File: %s\n", input_buffer);
        }
        else
        {
            printf("The path is neither a regular file nor a directory.\n");
        }
    }
}

void read_or_stream_file(const char *file_path, int client_socket, int is_binary)
{
    pthread_mutex_lock(&rw_mutex);
    while (writer_active)
    {
        pthread_cond_wait(&rw_cond, &rw_mutex);
    }
    pthread_mutex_lock(&count_mutex);
    reader_count++;
    pthread_mutex_unlock(&count_mutex);

    pthread_mutex_unlock(&rw_mutex);

    FILE *file;
    char buffer[BUFFER_SIZE];
    char stop[10] = "STOP\0";
    size_t bytes_read;

    struct stat statbuf;
    if (stat(file_path, &statbuf) == -1)
    {
        printf("error at: %s\n", file_path);
        perror("Error: stat failed");
        send(client_socket, stop, sizeof(stop), 0);
        int ack = 103;
        send(client_socket, &ack, sizeof(int), 0);
        return;
    }
    if (S_ISDIR(statbuf.st_mode))
    {
        send(client_socket, stop, sizeof(stop), 0);
        int dir_ack = 112;
        send(client_socket, &dir_ack, sizeof(dir_ack), 0);
        printf("error: requested read for directory\n");
        return;
    }

    // Open the file in the appropriate mode
    if (is_binary)
    {
        file = fopen(file_path, "rb");
    }
    else
    {
        file = fopen(file_path, "r");
    }

    if (file == NULL)
    {
        perror("Error opening file");
        send(client_socket, stop, sizeof(stop), 0);
        int ack = 103;
        send(client_socket, &ack, sizeof(int), 0);
        return;
    }
    int cnt = 0;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0)
    {
        cnt += bytes_read;
        if (send(client_socket, buffer, bytes_read, 0) == -1)
        {
            perror("Error sending data to client");
            break;
        }
    }
    printf("%d\n", cnt);
    send(client_socket, stop, strlen(stop), 0);
    int success_ack = 0;
    send(client_socket, &success_ack, sizeof(int), 0);
    if (feof(file))
    {
        printf("File transfer complete.\n");
    }
    else if (ferror(file))
    {
        perror("Error reading file");
    }

    fclose(file);

    pthread_mutex_lock(&count_mutex);
    reader_count--;
    if (reader_count == 0)
    {
        pthread_cond_signal(&rw_cond); // Signal waiting writers if no readers left
    }
    pthread_mutex_unlock(&count_mutex);
}

// add write function
void write_fn(char *path, int client_socket, int sync)
{

    pthread_mutex_lock(&rw_mutex);

    // Wait if there are active readers or another writer
    while (reader_count > 0 || writer_active)
    {
        pthread_cond_wait(&rw_cond, &rw_mutex);
    }

    // Mark writer as active
    writer_active = 1;

    char buffer[MAX_PACKET_LENGTH];
    memset(buffer, 0, MAX_PACKET_LENGTH);

    // Open the file for writing
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("Error opening file\n");
        exit(1);
    }

    if (sync == 1) // client specified to write synchronously
    {
        recv(client_socket, buffer, sizeof(buffer), 0);
        while (strncmp(buffer, "stop", 4) != 0)
        {
            // Write data to the file
            ssize_t bytes_written = write(fd, buffer, strlen(buffer));
            if (bytes_written == -1)
            {
                perror("Error writing to file");
                close(fd);
                exit(1);
            }
            // data is written to storage immediately
            if (fsync(fd) < 0)
            {
                perror("fsync");
            }
            memset(buffer, 0, MAX_PACKET_LENGTH);
            recv(client_socket, buffer, sizeof(buffer), 0);
        }

        // remember to close the file descriptor
        close(fd);
        memset(buffer, 0, MAX_PACKET_LENGTH);
        strcpy(buffer, "data is successfully written synchronusly as requested");
        send(client_socket, buffer, strlen(buffer), 0);
        close(client_socket);
    }
    else if (sync == 0)
    {
        recv(client_socket, buffer, sizeof(buffer), 0);

        // ss judged to write synchronously
        if (strlen(buffer) <= SYNC_THRESHOLD)
        {
            while (strncmp(buffer, "stop", 4) != 0)
            {
                // Write data to the file
                ssize_t bytes_written = write(fd, buffer, strlen(buffer));
                if (bytes_written == -1)
                {
                    perror("Error writing to file");
                    close(fd);
                    exit(1);
                }
                // data is written to storage immediately
                if (fsync(fd) < 0)
                {
                    perror("fsync");
                }
                memset(buffer, 0, MAX_PACKET_LENGTH);
                recv(client_socket, buffer, sizeof(buffer), 0);
            }
            char buffer_2[BUFFER_LENGTH];
            memset(buffer_2, 0, sizeof(buffer_2));
            strcpy(buffer_2, "storage server judged the data to be written synchronously");
            send(client_socket, buffer_2, strlen(buffer_2), 0);

            // remember to close the file descriptor
            close(fd);
            memset(buffer, 0, MAX_PACKET_LENGTH);
            strcpy(buffer, "data is successfully written synchronusly as judged by storage server");
            send(client_socket, buffer, strlen(buffer), 0);
            close(client_socket);
        }
        else
        {
            // ss judged to write asynchronously
            const char *filename = "dummy.txt";
            int fd_2 = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
            while (strncmp(buffer, "stop", 4) != 0)
            {
                // Write data to the file
                ssize_t bytes_written = write(fd_2, buffer, strlen(buffer));
                if (bytes_written == -1)
                {
                    perror("Error writing to file");
                    close(fd);
                    exit(1);
                }
                memset(buffer, 0, MAX_PACKET_LENGTH);
                recv(client_socket, buffer, sizeof(buffer), 0);
            }
            // close the connection with the client by ack
            char buffer_2[BUFFER_LENGTH];
            memset(buffer_2, 0, sizeof(buffer_2));
            strcpy(buffer_2, "...storage server judged the data to be written asynchronously ,so closing the client connection with storage server...");
            send(client_socket, buffer_2, strlen(buffer_2), 0);
            close(client_socket);
            off_t startPos = lseek(fd_2, 0, SEEK_SET);
            if (startPos == -1)
            {
                perror("Failed to reset to start");
                close(fd);
                exit(1);
            }
            ssize_t bytes_read, bytes_written;
            memset(buffer, 0, sizeof(buffer));

            // Read from source and write to destination
            while ((bytes_read = read(fd_2, buffer, sizeof(buffer))) > 0)
            {

                bytes_written = write(fd, buffer, bytes_read);

                if (bytes_written < 0)
                {
                    perror("Error writing to destination file");
                    close(fd);
                    close(fd_2);
                    exit(1);
                }
                // data is written to storage immediately
                if (fsync(fd) < 0)
                {
                    perror("fsync");
                }
            }

            if (bytes_read < 0)
            {
                perror("Error reading from source file");
            }
            // remember to close the file descriptor
            close(fd);
            close(fd_2);
            // memset(buffer, 0, MAX_PACKET_LENGTH);
            // strcpy(buffer, "data is successfully written asynchronusly as judged by the storage server");
            // send(naming_socket, buffer, strlen(buffer), 0);
        }
    }

    writer_active = 0;

    // Signal waiting readers and writers
    pthread_cond_broadcast(&rw_cond);
    pthread_mutex_unlock(&rw_mutex);
}

void get_permissions(mode_t mode, char *perm_str)
{
    // File type
    perm_str[0] = S_ISDIR(mode) ? 'd' : S_ISLNK(mode) ? 'l'
                                                      : // Symbolic link
                                        S_ISREG(mode) ? '-'
                                                      : // Regular file
                                        S_ISCHR(mode) ? 'c'
                                                      : // Character device
                                        S_ISBLK(mode) ? 'b'
                                                      : // Block device
                                        S_ISFIFO(mode) ? 'p'
                                                       : // FIFO/pipe
                                        S_ISSOCK(mode) ? 's'
                                                       : '?'; // Socket or unknown

    // User permissions
    perm_str[1] = (mode & S_IRUSR) ? 'r' : '-';
    perm_str[2] = (mode & S_IWUSR) ? 'w' : '-';
    perm_str[3] = (mode & S_IXUSR) ? 'x' : '-';
    // Group permissions
    perm_str[4] = (mode & S_IRGRP) ? 'r' : '-';
    perm_str[5] = (mode & S_IWGRP) ? 'w' : '-';
    perm_str[6] = (mode & S_IXGRP) ? 'x' : '-';

    // Other permissions
    perm_str[7] = (mode & S_IROTH) ? 'r' : '-';
    perm_str[8] = (mode & S_IWOTH) ? 'w' : '-';
    perm_str[9] = (mode & S_IXOTH) ? 'x' : '-';

    // Null-terminate the string
    perm_str[10] = '\0';
}

void send_perm(const char *file_path, int client_socket)
{
    struct stat file_stat;
    char file_details[1024];
    char permissions[11];
    char file_type[20];

    // Get file metadata
    if (stat(file_path, &file_stat) == -1)
    {
        perror("Error retrieving file metadata");
        const char *error_msg = "ERROR: Could not retrieve file metadata.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        int ack = 106;
        send(client_socket, &ack, sizeof(int), 0);
        return;
    }

    if (S_ISREG(file_stat.st_mode))
        strcpy(file_type, "Regular File");
    else if (S_ISDIR(file_stat.st_mode))
        strcpy(file_type, "Directory");
    else if (S_ISCHR(file_stat.st_mode))
        strcpy(file_type, "Character Device");
    else if (S_ISBLK(file_stat.st_mode))
        strcpy(file_type, "Block Device");
    else if (S_ISFIFO(file_stat.st_mode))
        strcpy(file_type, "FIFO/Pipe");
    else if (S_ISLNK(file_stat.st_mode))
        strcpy(file_type, "Symbolic Link");
    else if (S_ISSOCK(file_stat.st_mode))
        strcpy(file_type, "Socket");
    else
        strcpy(file_type, "Unknown");

    get_permissions(file_stat.st_mode, permissions);

    // Format file details
    snprintf(file_details, sizeof(file_details),
             "File_path: %s\n"
             "Type: %s\n"
             "Permissions: %s\n"
             "Size: %ld bytes\n"
             "Last Modified: %s\n",
             file_path,
             file_type,
             permissions,
             file_stat.st_size,
             ctime(&file_stat.st_mtime)); // Format modification time

    // Send the file details to the client
    if (send(client_socket, file_details, strlen(file_details), 0) == -1)
    {
        perror("Error sending file details to client");
    }
    else
    {
        printf("Sent file details to client:\n%s\n", file_details);
        int ack = 0;
        send(client_socket, &ack, sizeof(int), 0);
    }
}

void create_file(const char *file_path, char *filename, int nm_socket)
{
    struct stat file_stat;
    if (stat(file_path, &file_stat) == -1)
    {
        perror("Error retrieving file metadata");
        int ack = 110;
        send(nm_socket, &ack, sizeof(ack), 0);
        return;
    }
    if (S_ISDIR(file_stat.st_mode))
    {
        char final_path[BUFFER_SIZE] = "";
        strcpy(final_path, file_path);
        strcat(final_path, "/");
        strcat(final_path, filename);
        int len = strlen(filename);
        if (filename[len - 1] == '/')
        {
            if (stat(final_path, &file_stat) == 0 && S_ISDIR(file_stat.st_mode))
            {
                int ack = 111;
                send(nm_socket, &ack, sizeof(ack), 0);
                printf("Directory already exists: %s\n", final_path);
                return;
            }

            int status = mkdir(final_path, S_IRWXU);
            if (status != 0)
            {
                int ack = 101;
                send(nm_socket, &ack, sizeof(ack), 0);
                perror("Error creating directory");
                return;
            }
            printf("Directory created: %s\n", final_path);
            int success = 0;
            send(nm_socket, &success, sizeof(success), 0);
        }
        else
        {
            if (access(final_path, F_OK) == 0)
            {
                printf("File already exists: %s\n", final_path);
                int ack = 111;
                send(nm_socket, &ack, sizeof(ack), 0);
                return;
            }

            FILE *file = fopen(final_path, "w+");
            if (file == NULL)
            {
                int ack = 101;
                send(nm_socket, &ack, sizeof(ack), 0);
                perror("Error opening file");
                return;
            }
            fclose(file);
            printf("File created: %s\n", final_path);
            int success = 0;
            send(nm_socket, &success, sizeof(success), 0);
        }
    }
    else
    {
        int success = 101;
        send(nm_socket, &success, sizeof(success), 0);
        return;
    }
}

int delete_path(const char *path, int nm_socket)
{
    struct stat statbuf;

    if (stat(path, &statbuf) == -1)
    {
        printf("error at 1: %s\n", path);
        perror("Error: stat failed");
        return -1;
    }

    if (S_ISREG(statbuf.st_mode))
    {
        if (remove(path) == 0)
        {
            printf("Deleted file: %s\n", path);
            return 0;
        }
        else
        {
            perror("Error deleting file");
            printf("error at: %s\n", path);
            return -1;
        }
    }
    else if (S_ISDIR(statbuf.st_mode))
    {
        DIR *dir = opendir(path);
        if (!dir)
        {
            printf("error at: %s\n", path);
            perror("Error opening directory");
            return -1;
        }

        struct dirent *entry;
        char full_path[1024];

        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

            if (delete_path(full_path, nm_socket) != 0)
            {
                closedir(dir);
                return -1;
            }
        }

        closedir(dir);

        if (rmdir(path) == 0)
        {
            printf("Deleted directory: %s\n", path);
        }
        else
        {
            perror("Error deleting directory");
            printf("error at: %s\n", path);
            return -1;
        }
    }
    else
    {
        printf("Error: Path is neither a file nor a directory\n");
        return -1;
    }

    return 0;
}

void *process_client(void *args)
{
    int client_socket = *(int *)args;
    client_req_t query;
    recv(client_socket, &query, sizeof(query), 0);
    printf("received query: %s\n", query.oper_name);
    if (strcasecmp(query.oper_name, "READ") == 0)
    {
        printf("wtf\n");
        read_or_stream_file(query.srcpath, client_socket, 0);
    }
    else if (strcasecmp(query.oper_name, "WRITE") == 0)
    {
        write_fn(query.srcpath, client_socket, query.sync);
        printf("ajdbweiu\n"); // include nmsocket
        // add acknowledgements
    }
    else if (strcasecmp(query.oper_name, "GETSP") == 0)
    {
        send_perm(query.srcpath, client_socket);
        // send ack
    }
    else if (strcasecmp(query.oper_name, "STREAM") == 0)
    {
        read_or_stream_file(query.srcpath, client_socket, 1);
    }
    close(client_socket);
    pthread_exit(NULL);
}

void *process_nm(void *args)
{
    int nm_socket = *(int *)args;
    client_req_t query;
    recv(nm_socket, &query, sizeof(query), 0);
    printf("received query: %s\n", query.oper_name);
    if (strcasecmp(query.oper_name, "CREATE") == 0)
    {
        printf("received query: %s %s %s\n", query.oper_name, query.srcpath, query.filename);
        create_file(query.srcpath, query.filename, nm_socket);
    }
    else if (strcasecmp(query.oper_name, "DELETE") == 0)
    {
        printf("received query: %s %s\n", query.oper_name, query.srcpath);
        // delete_path(query.srcpath, nm_socket);
        if (delete_path(query.srcpath, nm_socket) == 0)
        {
            printf("Successfully deleted: %s\n", query.srcpath);
            int success = 0;
            send(nm_socket, &success, sizeof(success), 0);
        }
        else
        {
            printf("Failed to delete: %s\n", query.srcpath);
            int ack = 102;
            send(nm_socket, &ack, sizeof(ack), 0);
        }
    }
    close(nm_socket);
    pthread_exit(NULL);
}

void *client_connection_handler(void *args)
{
    pthread_t client_threads[50];
    int new_socket1;

    // Accept an incoming connection
    while (1)
    {
        pthread_t client_thread;
        if ((new_socket1 = accept(g_client_socket, (struct sockaddr *)&address_for_client, (socklen_t *)&addrlen)) < 0)
        {
            perror("Accept failed");
            close(g_client_socket);
            exit(EXIT_FAILURE);
        }
        // logMsg("New client detected\n");
        pthread_create(&client_threads[num_clients], NULL, process_client, (void *)&new_socket1);
        num_clients++;
    }

    return NULL;
}

void *nm_connection_handler(void *args)
{
    pthread_t nm_threads[50];
    int new_socket2;

    // Accept an incoming connection
    while (1)
    {
        pthread_t nm_thread;
        if ((new_socket2 = accept(g_nm_socket, (struct sockaddr *)&address_for_nm, (socklen_t *)&addrlen)) < 0)
        {
            perror("Accept failed");
            close(g_nm_socket);
            exit(EXIT_FAILURE);
        }
        printf("nm server connected\n");
        pthread_create(&nm_threads[num_nm], NULL, process_nm, (void *)&new_socket2);
        num_nm++;
    }

    return NULL;
}
// void *start_server(void *th_args)
// {
//     thread_args_t *thread_args = (thread_args_t *)th_args;

//     int server_socket;
//     struct sockaddr_in server_addr;

//     server_socket = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_socket == -1)
//     {
//         perror("Socket creation failed");
//         exit(1);
//     }

//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(thread_args->port);
//     inet_pton(AF_INET, thread_args->server_ip, &server_addr.sin_addr);

//     if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
//     {
//         perror("Bind failed");
//         close(server_socket);
//         exit(1);
//     }

//     if (listen(server_socket, 50) == -1)
//     {
//         perror("Listen failed");
//         close(server_socket);
//         exit(1);
//     }

//     while (1)
//     {
//         // printf("4\n");
//         int socket = 0;
//         struct sockaddr_in addr;
//         socklen_t addr_len = sizeof(addr);
//         socket = accept(server_socket, (struct sockaddr *)&addr, &addr_len);
//         if (socket == -1)
//         {
//             perror("Accept failed");
//             close(server_socket);
//             continue;
//         }
//         if (thread_args->execute_flag == 0)
//         {
//             pthread_t client_thread;
//             if (pthread_create(&client_thread, NULL, process_client, (void *)&socket))
//             {
//                 perror("Error while creating client_thread");
//                 close(socket);
//                 continue;
//             }
//         }
//         else
//         {
//             pthread_t nm_thread;
//             if (pthread_create(&nm_thread, NULL, process_nm, (void *)&socket))
//             {
//                 perror("Error while creating nm_thread");
//                 close(socket);
//                 continue;
//             }
//         }
//     }
// }

int main(int argc, char *argv[])
{
    // validate input, IP, port
    if (argc != 3)
    {
        printf("Usage: %s <IP Address> <port>\n", argv[0]);
        return 1;
    }
    char *ns_ip_address = argv[1];
    int ns_port = atoi(argv[2]);
    if (ns_port < 1 || ns_port > 65535)
    {
        printf("Error: Invalid port number. port must be between 1 and 65535.\n");
        return 1;
    }

    ss_t ss;
    ss.idx = -1;
    // get ss_ip
    char ss_ip[NI_MAXHOST];
    if (get_ss_ip(ss_ip))
    {
        strcpy(ss.ip, ss_ip);
        printf("%s\n", ss.ip);
    }

    // get port for NM
    ss.port_for_nm = get_available_port();
    if (ss.port_for_nm != -1)
    {
        printf("Available port for NM: %d\n", ss.port_for_nm);
    }
    else
    {
        printf("Failed to get an available port for NM.\n");
        return 1;
    }

    // get port for client
    ss.port_for_client = get_available_port();
    if (ss.port_for_client != -1)
    {
        printf("Available port for client: %d\n", ss.port_for_client);
    }
    else
    {
        printf("Failed to get an available port for client.\n");
        return 1;
    }

    // get port for other ss
    ss.port_for_ss = get_available_port();
    if (ss.port_for_ss != -1)
    {
        printf("Available port for client: %d\n", ss.port_for_ss);
    }
    else
    {
        printf("Failed to get an available port for client.\n");
        return 1;
    }

    // get accessible paths from USER
    get_list_of_accesible_paths(&ss);
    printf("%s\n", ss.accessible_paths);

    // initialize the connection
    int nm_socket;
    struct sockaddr_in server_addr;

    nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (nm_socket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(ns_port);
    inet_pton(AF_INET, ns_ip_address, &server_addr.sin_addr);

    if (connect(nm_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(nm_socket);
        exit(EXIT_FAILURE);
    }

    printf("connection succesfull\n");
    // ss.is_online = 1;

    if (send(nm_socket, &ss, sizeof(ss), 0) < 0)
    {
        perror("Send failed");
        close(nm_socket);
        exit(EXIT_FAILURE);
    }
    printf("Struct sent successfully!\n");

    // receiving initialization ack
    char buffer[1024] = {0};
    ssize_t bytes_received = 0;
    memset(buffer, '\0', sizeof(buffer));
    bytes_received = recv(nm_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0)
    {
        perror("recv failed");
        return 0;
    }
    else if (bytes_received == 0)
    {
        printf("Client closed the connection.\n");
    }
    else
    {
        buffer[bytes_received] = '\0';
        printf("Received message:\n%s\n", buffer);
    }
    close(nm_socket);

    // create servers
    // for client connections
    // pthread_t server_thread1;
    // pthread_t server_thread2;

    // thread_args_t *th_args_client = malloc(sizeof(thread_args_t));
    // if (th_args_client == NULL)
    // {
    //     perror("Failed to allocate memory for th_args_client");
    //     exit(EXIT_FAILURE);
    // }

    // strcpy(th_args_client->server_ip, ss.ip);
    // th_args_client->port = ss.port_for_client;
    // th_args_client->execute_flag = 0;
    // printf("2\n");

    // if (pthread_create(&server_thread1, NULL, start_server, (void *)th_args_client) != 0)
    // {
    //     perror("Error while creating server thread on port_for_client");
    //     exit(EXIT_FAILURE);
    // }
    // // printf("3\n");
    // thread_args_t *th_args_nm = malloc(sizeof(thread_args_t));
    // if (th_args_nm == NULL)
    // {
    //     perror("Failed to allocate memory for th_args_client");
    //     exit(EXIT_FAILURE);
    // }
    // strcpy(th_args_nm->server_ip, ss.ip);
    // th_args_nm->port = ss.port_for_nm;
    // th_args_nm->execute_flag = 1;
    // // printf("4.5\n");
    // if (pthread_create(&server_thread2, NULL, start_server, (void *)th_args_nm) != 0)
    // {
    //     perror("Error while creating server thread on port_for_nm");
    //     exit(EXIT_FAILURE);
    // }
    // // printf("5\n");
    // pthread_join(server_thread1, NULL);
    // pthread_join(server_thread2, NULL);

    // for client connections
    if ((g_client_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&address_for_client, 0, addrlen);
    address_for_client.sin_family = AF_INET;
    address_for_client.sin_port = htons(ss.port_for_client);

    if (inet_pton(AF_INET, ss.ip, &address_for_client.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }

    if (bind(g_client_socket, (struct sockaddr *)&address_for_client, addrlen) < 0)
    {
        perror("Bind failed");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(g_client_socket, 50) < 0)
    {
        perror("Listen failed");
        close(g_client_socket);
        exit(EXIT_FAILURE);
    }

    // for storage connections
    if ((g_nm_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    memset(&address_for_nm, 0, addrlen);
    address_for_nm.sin_family = AF_INET;
    address_for_nm.sin_port = htons(ss.port_for_nm);
    if (inet_pton(AF_INET, ss.ip, &address_for_nm.sin_addr) <= 0)
    {
        perror("Invalid address or address not supported");
        close(g_nm_socket);
        exit(EXIT_FAILURE);
    }

    if (bind(g_nm_socket, (struct sockaddr *)&address_for_nm, addrlen) < 0)
    {
        perror("Bind failed");
        close(g_nm_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(g_nm_socket, 50) < 0)
    {
        perror("Listen failed");
        close(g_nm_socket);
        exit(EXIT_FAILURE);
    }

    printf("Server listening for client on port %d\n", ss.port_for_client);
    printf("Server listening for storage on port %d\n", ss.port_for_nm);

    pthread_t client_connections, nm_connections;
    pthread_create(&client_connections, NULL, client_connection_handler, NULL);
    pthread_create(&nm_connections, NULL, nm_connection_handler, NULL);

    pthread_join(client_connections, NULL);
    pthread_join(nm_connections, NULL);

    // Close sockets when done
    close(g_client_socket);
    close(g_nm_socket);
    return 0;
}