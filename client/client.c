#include "client.h"

int CheckIfValidString(char *String)
{
    if (strncasecmp(String, "write", 5) == 0 || strncasecmp(String, "delete", 6) == 0 || strncasecmp(String, "GetSP", 5) == 0 || strncasecmp(String, "read", 4) == 0 || strncasecmp(String, "exit", 4) == 0 || strncasecmp(String, "Stream", 6) == 0 || strncasecmp(String, "create", 6) == 0 || strncasecmp(String, "copy", 4) == 0 || strncasecmp(String, "List", 4) == 0)
    {
        return 1;
    }
    return 0;
}

void printError(int errorCode)
{
    switch (errorCode)
    {
    case ECS:
        printf("Error Connecting to Server(Error Code : %d)\n", ECS);
        break;
    case FCF:
        printf("Failed to Create File(Error Code : %d)\n", FCF);
        break;
    case FDF:
        printf("Failed to Delete File(Error Code : %d)\n", FDF);
        break;
    case FRF:
        printf("Failed to Read File(Error Code : %d)\n", FRF);
        break;
    case FWF:
        printf("Failed to Write File(Error Code : %d)\n", FWF);
        break;
    case II:
        printf("Invalid Input(Error Code : %d)\n", II);
        break;
    case CFF:
        printf("Failed to Copy File(Error Code : %d)\n", CFF);
        break;
    case FNF:
        printf("File Not Found(Error Code : %d)\n", FNF);
        break;
    case RFM:
        printf("Error Opening File Stat(Error Code : %d)\n", RFM);
        break;
    case FAE:
        printf("File Already Exists(Error Code : %d)\n", FAE);
        break;
    case RFD:
        printf("Read requested from directory(Error Code : %d)\n", RFD);
        break;
    case PERM:
        printf("Permission denied(Error Code : %d)\n", PERM);
        break;
    case OK:
        printf("Succesfully Completed\n");
        break;
    default:
        printf("Unknown Error Code: %d\n", errorCode);
        break;
    }
}

void *receiveOnce(void *socket_fd)
{
    int sockfd = *(int *)socket_fd;
    char buffer[1024];
    int bytesReceived;
    bytesReceived = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0)
    {
        buffer[bytesReceived] = '\0';
        printf("%s\n", buffer);
    }
    else if (bytesReceived == 0)
    {
        printf("Connection closed by server.\n");
    }
    else
    {
        perror("recv failed");
    }
    pthread_exit(NULL);
}

void addLeadingSlash(char **path)
{
    if ((*path)[0] == '/')
    {
        return;
    }
    size_t len = strlen(*path) + 2; // +1 for '/' and +1 for '\0'
    char *newPath = malloc(len);
    if (!newPath)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    snprintf(newPath, len, "/%s", *path);
    free(*path);
    *path = newPath;
}

int ReceiveAck(int ClientSocket)
{
    int ack = 0;
    recv(ClientSocket, &ack, sizeof(ack), 0);
    return ack;
}

int main(int argc, char *argv[])
{

    char Naming_ipAddress[16];
    int portNumber;
    int NamingServerSocket;
    while (1)
    {
        memset(Naming_ipAddress, 0, sizeof(Naming_ipAddress));
        if (argc < 1)
        {
            strcpy(Naming_ipAddress, "127.0.0.1");
        }
        else
        {
            strcpy(Naming_ipAddress, argv[1]);
        }

        if ((NamingServerSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            perror("Error Creating Socket");
            exit(1);
        }
        struct sockaddr_in NamingServer;
        NamingServer.sin_addr.s_addr = inet_addr(Naming_ipAddress);
        NamingServer.sin_family = AF_INET;
        NamingServer.sin_port = htons(8080);
        if (inet_aton(Naming_ipAddress, &NamingServer.sin_addr) == 0)
        {
            perror("invalid IP Address!");
            exit(1);
        }
        if (connect(NamingServerSocket, (struct sockaddr *)&NamingServer, sizeof(NamingServer)) < 0)
        {
            printf("Error connecting to Server (Error Code:%d)\n", ECS);
            exit(1);
        }
        char buffer[1024];
        struct timeval timeout;
        timeout.tv_sec = 5; // i kept 5 seconds as timeout
        timeout.tv_usec = 0;
        printf("Input : ");
        char input[1024];
        fgets(input, sizeof(input), stdin);
        input[strlen(input) - 1] = '\0';
        if (CheckIfValidString(input) == 0)
        {
            printf("Invalid Input (ERROR Code : %d)\n", II);
        }
        else
        {
            if (strcasecmp(input, "exit") == 0)
            {
                break;
            }
            int NumofCommands = 0;
            char *tokenize;
            char *command[100];
            char *save_token;
            tokenize = strtok_r(input, " \t\n", &save_token);
            while (tokenize != NULL)
            {
                command[NumofCommands] = tokenize;
                NumofCommands = NumofCommands + 1;
                tokenize = strtok_r(NULL, " \t\n", &save_token);
            }
            command[NumofCommands] = NULL;
            client_req_t Packet;
            if (NumofCommands > 1)
            {
                addLeadingSlash(&command[1]);
                strcpy(Packet.srcpath, command[1]);
            }
            strcpy(Packet.oper_name, command[0]);
            strcpy(Packet.destpath, "");
            strcpy(Packet.filename, "");
            Packet.sync = 0; // default asynocrosous
            if (strcasecmp(command[0], "delete") == 0)
            {
                send(NamingServerSocket, &Packet, sizeof(Packet), 0);
                if (setsockopt(NamingServerSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
                {
                    perror("Naming Server timeout message");
                    close(NamingServerSocket);
                    exit(EXIT_FAILURE);
                }
                recv(NamingServerSocket, buffer, sizeof(buffer), 0);
                int acknowledgement = ReceiveAck(NamingServerSocket);
                if (acknowledgement != OK)
                {
                    printError(acknowledgement);
                    close(NamingServerSocket);
                }
                else
                {
                    printf("Operation Successful\n");
                    send(NamingServerSocket, "closing", sizeof("closing"), 0);
                    close(NamingServerSocket);
                }
            }
            else if (strcasecmp(command[0], "create") == 0)
            {
                if (strcmp(command[3], "-f") == 0)
                {
                }
                else if (strcmp(command[3], "-d") == 0)
                {
                    strcat(command[2], "/"); // assume d is directory
                }
                else
                {
                }
                strcpy(Packet.filename, command[2]);
                send(NamingServerSocket, &Packet, sizeof(Packet), 0);
                if (setsockopt(NamingServerSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
                {
                    perror("Naming Server timeout message");
                    close(NamingServerSocket);
                    exit(EXIT_FAILURE);
                }
                recv(NamingServerSocket, buffer, sizeof(buffer), 0);
                int acknowledgement = ReceiveAck(NamingServerSocket);
                if (acknowledgement != OK)
                {
                    printError(acknowledgement);
                    close(NamingServerSocket);
                }
                else
                {
                    printf("Operation Successful\n");
                    send(NamingServerSocket, "closing", sizeof("closing"), 0);
                    close(NamingServerSocket);
                }
            }
            else if (strcasecmp(command[0], "copy") == 0)
            {
                addLeadingSlash(&command[2]);
                strcpy(Packet.destpath, command[2]);
                send(NamingServerSocket, &Packet, sizeof(Packet), 0);
                if (setsockopt(NamingServerSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
                {
                    perror("Naming Server timeout message");
                    close(NamingServerSocket);
                    exit(EXIT_FAILURE);
                }
                recv(NamingServerSocket, buffer, sizeof(buffer), 0);
                int acknowledgement = ReceiveAck(NamingServerSocket);
                if (acknowledgement != OK)
                {
                    printError(acknowledgement);
                    close(NamingServerSocket);
                }
                else
                {
                    printf("Operation Successful\n");
                    send(NamingServerSocket, "closing", sizeof("closing"), 0);
                    close(NamingServerSocket);
                }
            }
            else if (strcasecmp(command[0], "List") == 0)
            {
                printf("%s\n", Packet.oper_name);
                send(NamingServerSocket, &Packet, sizeof(Packet), 0);
                if (setsockopt(NamingServerSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
                {
                    perror("Naming Server timeout message");
                    close(NamingServerSocket);
                    exit(EXIT_FAILURE);
                }
                recv(NamingServerSocket, buffer, sizeof(buffer), 0);
                char buf[1024];
                while (1)
                {
                    memset(buf, 0, sizeof(buf));
                    recv(NamingServerSocket, buf, sizeof(buf), 0);
                    int n = strlen(buf);
                    printf("%d\n", n);
                    if (buf[n - 4] == 'S' && buf[n - 3] == 'T' && buf[n - 2] == 'O' && buf[n - 1] == 'P')
                    {
                        buf[n - 4] = '\0';
                        printf("%s\n", buf);
                        break;
                    }
                    printf("%s\n", buf);
                }
                send(NamingServerSocket, "closing", sizeof("closing"), 0);
                close(NamingServerSocket);
            }
            else
            {
                send(NamingServerSocket, &Packet, sizeof(Packet), 0);
                if (setsockopt(NamingServerSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) < 0)
                {
                    perror("Naming Server timeout message");
                    close(NamingServerSocket);
                    exit(EXIT_FAILURE);
                }
                recv(NamingServerSocket, buffer, sizeof(buffer), 0);
                client_response_t ss;
                int ClientSocket;
                memset(&ss, 0, sizeof(ss));
                int errorcode;
                recv(NamingServerSocket, &errorcode, sizeof(int), 0); // recive error codes
                if (errorcode != OK)
                {
                    printError(errorcode);
                    close(NamingServerSocket);
                }
                else
                {
                    recv(NamingServerSocket, &ss, sizeof(ss), 0);
                    printf("ip: %s, %d\n", ss.ip, ss.port);
                    if ((ClientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                    {
                        perror("Error Creating Socket");
                        exit(1);
                    }
                    struct sockaddr_in nmAddr;
                    nmAddr.sin_family = AF_INET;
                    nmAddr.sin_port = htons(ss.port);
                    nmAddr.sin_addr.s_addr = inet_addr(ss.ip);
                    if (inet_aton(ss.ip, &nmAddr.sin_addr) == 0)
                    {
                        perror("invalid IP Address!");
                        exit(1);
                    }
                    if (connect(ClientSocket, (struct sockaddr *)&nmAddr, sizeof(nmAddr)) < 0)
                    {
                        printf("Error connecting to Server (Error Code:%d)\n", ECS);
                        exit(1);
                    }
                    printf("oper2:%s path2:%s\n", Packet.oper_name, Packet.srcpath);
                    if (strcasecmp(command[0], "read") == 0)
                    {
                        int readcorrectly = 0;
                        char buf[1024];
                        char data[1024];
                        send(ClientSocket, &Packet, sizeof(Packet), 0);
                        printf("oper:%s path:%s\n", Packet.oper_name, Packet.srcpath);

                        while (1)
                        {
                            memset(buf, 0, sizeof(buf));
                            int received = recv(ClientSocket, buf, sizeof(buf) - 1, 0); // Reserve space for null
                            if (received > 0)
                            {
                                buf[received] = '\0'; // Null-terminate the string
                            }
                            int n = strlen(buf);

                            if (buf[n - 4] == 'S' && buf[n - 3] == 'T' && buf[n - 2] == 'O' && buf[n - 1] == 'P')
                            {
                                buf[n - 4] = '\0';
                                printf("%s\n", buf);
                                break;
                            }
                            printf("%s\n", buf);
                        }
                        int acknowledgement = ReceiveAck(ClientSocket);
                        printError(acknowledgement);
                        close(ClientSocket);
                        send(NamingServerSocket, "closing", sizeof("closing"), 0);
                        close(NamingServerSocket);
                    }
                    if (strcasecmp(command[0], "write") == 0)
                    {
                        if (NumofCommands > 2)
                        {
                            if (strcasecmp(command[2], "--SYNC") == 0)
                            {
                                Packet.sync = 1;
                            }
                        }

                        send(ClientSocket, &Packet, sizeof(Packet), 0);
                        printf("input: \n");
                        char data[BUFFER_LENGTH];
                        if (Packet.sync == 1)
                        {
                            char ch = 'a';
                            while (ch != '\n')
                            {

                                int index = 0;

                                while (ch != '\n' && index < MAX_PACKET_LENGTH)
                                {

                                    scanf("%c", &ch);

                                    data[index] = ch;
                                    index++;
                                }
                                if (index < MAX_PACKET_LENGTH)
                                {
                                    data[index - 1] = '\0';
                                }
                                else
                                {
                                    data[index] = '\0';
                                }
                                send(ClientSocket, data, strlen(data), 0);
                            }
                            strcpy(data, "stop");
                            send(ClientSocket, data, strlen(data), 0);
                            char buff[BUFFER_LENGTH];
                            recv(ClientSocket, buff, strlen(buff), 0);
                            printf("%s\n", buff);
                            close(ClientSocket);
                            send(NamingServerSocket, "closing", sizeof("closing"), 0);
                            close(NamingServerSocket);
                        }
                        else
                        {
                            char ch = 'a';
                            while (ch != '\n')
                            {
                                int index = 0;
                                while (ch != '\n' && index < MAX_PACKET_LENGTH)
                                {
                                    scanf("%c", &ch);
                                    data[index] = ch;
                                    index++;
                                }
                                // data[index] = '\0';
                                if (index < MAX_PACKET_LENGTH)
                                {
                                    data[index - 1] = '\0';
                                }
                                else
                                {
                                    data[index] = '\0';
                                }
                                send(ClientSocket, data, strlen(data), 0);
                            }
                            strcpy(data, "stop");
                            send(ClientSocket, data, strlen(data), 0);

                            char buff[BUFFER_LENGTH];
                            recv(ClientSocket, buff, strlen(buff), 0);
                            printf("%s\n", buff);
                            if (strncmp("...", buff, 3) != 0)
                            {
                                memset(buff, 0, sizeof(buff));
                                recv(ClientSocket, buff, strlen(buff), 0);
                                printf("%s\n", buff);
                                close(ClientSocket);
                                send(NamingServerSocket, "closing", sizeof("closing"), 0);
                                close(NamingServerSocket);
                            }
                            else
                            {
                                close(ClientSocket);
                                // pthread_t recvThread;
                                // if (pthread_create(&recvThread, NULL, receiveOnce, (void *)&NamingServerSocket) != 0)
                                // {
                                //     perror("Thread creation failed");
                                //     send(NamingServerSocket, "closing", sizeof("closing"), 0);
                                //     close(NamingServerSocket);
                                //     exit(EXIT_FAILURE);
                                // }
                                // pthread_join(recvThread, NULL);
                                // printf("Receive thread completed. Closing connection.\n");
                                send(NamingServerSocket, "closing", sizeof("closing"), 0);
                                close(NamingServerSocket);
                            }
                        }
                    }
                    if (strcasecmp(command[0], "Stream") == 0)
                    {
                        printf("operaa:%s\n", Packet.oper_name);
                        send(ClientSocket, &Packet, sizeof(Packet), 0);
                        char data[1024];
                        ssize_t bytes_received; // To store the number of bytes received
                        // Create a pipe to stream audio data to mpv
                        int pipefd[2];
                        if (pipe(pipefd) == -1)
                        {
                            perror("Failed to create pipe");
                            close(ClientSocket);
                            return 0;
                        }

                        // Fork to create a child process for mpv
                        pid_t pid = fork();
                        if (pid < 0)
                        {
                            perror("Fork failed");
                            close(pipefd[0]);
                            close(pipefd[1]);
                            close(ClientSocket);
                            return 0;
                        }

                        if (pid == 0)
                        {
                            // Child process: Redirect pipe's read end to stdin and run mpv
                            close(pipefd[1]);              // Close write end
                            dup2(pipefd[0], STDIN_FILENO); // Redirect pipe read end to stdin
                            close(pipefd[0]);
                            // Execute mpv with audio input from stdin
                            execlp("mpv", "mpv", "--no-terminal", "--", "-", NULL);
                            perror("Failed to execute mpv");
                            exit(1);
                        }

                        // Parent process: Send received audio data to the pipe's write end
                        close(pipefd[0]); // Close read end
                        while ((bytes_received = recv(ClientSocket, data, sizeof(data), 0)) > 0)
                        {
                            if (strncasecmp(data, "STOP", 4) == 0)
                            {
                                break;
                            }
                            if (write(pipefd[1], data, bytes_received) != bytes_received)
                            {
                                perror("Failed to write to pipe");
                                close(pipefd[1]);
                                close(ClientSocket);
                                return 0;
                            }
                        }

                        if (bytes_received < 0)
                        {
                            perror("Error receiving data");
                        }
                        // Close the write end of the pipe and the socket
                        close(pipefd[1]);
                        int acknowledgement = ReceiveAck(ClientSocket);
                        printError(acknowledgement);
                        close(ClientSocket);
                        wait(NULL);
                        send(NamingServerSocket, "closing", sizeof("closing"), 0);
                        close(NamingServerSocket);
                    }
                    if (strcasecmp(command[0], "getSP") == 0)
                    {
                        char data[1024];
                        send(ClientSocket, &Packet, sizeof(Packet), 0);
                        ssize_t bytes_received = recv(ClientSocket, &data, sizeof(data), 0);
                        if (bytes_received <= 0)
                        {
                            perror("Failed to receive file information");
                            close(ClientSocket);
                            return 0;
                        }
                        printf("File details: ");
                        printf("%s\n", data);
                        int acknowledgement = ReceiveAck(ClientSocket);
                        printError(acknowledgement);
                        close(ClientSocket);
                        send(NamingServerSocket, "closing", sizeof("closing"), 0);
                        close(NamingServerSocket);
                    }
                }
            }
        }
    }
}
