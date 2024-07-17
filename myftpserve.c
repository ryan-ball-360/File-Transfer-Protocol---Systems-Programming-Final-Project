#include "myftp.h"

#define BACKLOG 4


void handleClient(int clientfd);
int processCommand(int clientfd, char *command, int dataSocketFd);

int handleDataConnection(int controlSocketFd)
{   //socket creation based on socket routines from lecture slides
    int listeningSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocketFd < 0)
    {
        perror("socket error\n");
        write(controlSocketFd, "EFailed to create data socket\n", 30);
        close(listeningSocketFd);
        return -1;
    }
    //socket binding based on socket routines from lecture slides
    struct sockaddr_in dataAddr;
    memset(&dataAddr, 0, sizeof(dataAddr));
    dataAddr.sin_family = AF_INET;
    dataAddr.sin_port = htons(0);
    dataAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listeningSocketFd, (struct sockaddr *)&dataAddr, sizeof(dataAddr)) < 0)
    {
        perror("bind error\n");
        close(listeningSocketFd);
        return -1;
    }

    socklen_t len = sizeof(dataAddr);
    if (getsockname(listeningSocketFd, (struct sockaddr *)&dataAddr, &len) < 0)
    {
        perror("getsockname error\n");
        close(listeningSocketFd);
        return -1;
    }
    //listen based on socket routines from lecture slides
    if (listen(listeningSocketFd, BACKLOG) < 0)
    {
        perror("listen error\n");
        close(listeningSocketFd);
        return -1;
    }

    int port = ntohs(dataAddr.sin_port);
    char acknowledgementMessage[BUFFER_SIZE];
    snprintf(acknowledgementMessage, sizeof(acknowledgementMessage), "A%d\n", port);
    if (write(controlSocketFd, acknowledgementMessage, strlen(acknowledgementMessage)) < 0)
    {
        perror("write error\n");
        close(listeningSocketFd);
        return -1;
    }
    // accept data connection based on socket routines from lecture slides, with some modifications
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int dataSocketFd = accept(listeningSocketFd, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (dataSocketFd < 0)
    {
        perror("accept error\n");
        write(controlSocketFd, "EFailed to accept data connection\n", 34);
        close(listeningSocketFd);
        return -1;
    }

    close(listeningSocketFd);
    return dataSocketFd; 
}



void handleClient(int clientfd)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    int dataSocketFd = -1;
    char commandBuffer[BUFFER_SIZE];
    int commandLength = 0;

    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);

        bytesRead = read(clientfd, buffer, BUFFER_SIZE - 1 - commandLength);
        if (bytesRead <= 0) 
        {
            if (bytesRead == 0) 
            {
                printf("Client disconnected\n");
            } else 
            {
                perror("Read error");
            }
            break;
        }

        memcpy(commandBuffer + commandLength, buffer, bytesRead);
        commandLength += bytesRead;
        if (commandBuffer[commandLength - 1] != '\n')
        {
            continue;
        }

        commandBuffer[commandLength] = '\0';
        printf("Received command: %s", commandBuffer);

        dataSocketFd = processCommand(clientfd, commandBuffer, dataSocketFd);

        if (strncmp(commandBuffer, "Q", 1) == 0)
        {
            printf("Quit command received. Closing connection.\n");
            break;
        }

        commandLength = 0;
    }

    close(clientfd);
}

void handleChangeDirectory(int controlSocketFd, const char *pathname)
{
    if (chdir(pathname) == 0)
    {
        const char *acknowledgeResponse = "A\n";
        if (write(controlSocketFd, acknowledgeResponse, strlen(acknowledgeResponse)) < 0)
        {
            perror("write ack");
        }
    }
    else
    {
        char errorMsg[BUFFER_SIZE];
        snprintf(errorMsg, sizeof(errorMsg), "EFailed to change directory: %s\n", strerror(errno));
        if (write(controlSocketFd, errorMsg, strlen(errorMsg)) < 0)
        {
            perror("write error");
        }
    }
}

void handleListDirectory(int controlSocketFd, int dataSocketFd)
{
    if (dataSocketFd < 0)
    {
        const char *errorMsg = "ENo data connection established\n";
        write(controlSocketFd, errorMsg, strlen(errorMsg));
        printf("Error: No data connection established\n");
        return;
    }
    const char *acknowledgeResponse = "A\n";
    write(controlSocketFd, acknowledgeResponse, strlen(acknowledgeResponse));

    
    pid_t pid;
    if(!(pid = fork()))
    {
        close(STDOUT_FILENO);
        dup(dataSocketFd); 
        if (execlp("ls", "ls", "-l", NULL) == -1)
        {
            perror("execlp");
            write(controlSocketFd, "Els -l command failed\n", 22);
            exit(1);
        }
    }
    printf("ls -l output sent to client\n");
    waitpid(pid, NULL, 0);
    close(dataSocketFd);
}




void handleGetFile(int controlSocketFd, int dataSocketFd, const char *pathname) 
{
    if (dataSocketFd < 0) 
    {
        const char *errorMsg = "ENo data connection established\n";
        write(controlSocketFd, errorMsg, strlen(errorMsg));
        return;
    }

    int filefd = open(pathname, O_RDONLY);
    if (filefd < 0) 
    {
        char errorMsg[BUFFER_SIZE];

        if (errno == ENOENT) 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EFile does not exist%s\n", strerror(errno));
        } 
        else if (errno == EACCES) 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EPermission denied: %s\n", strerror(errno));
        } 
        else if (errno == EISDIR) 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EPath is a directory: %s\n", strerror(errno));
        } 
        else 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EFailed to open file: %s\n", strerror(errno));
        }

        write(controlSocketFd, errorMsg, strlen(errorMsg));
        close(dataSocketFd);
        return;
    }

    const char *acknowledgementMsg = "A\n";
    write(controlSocketFd, acknowledgementMsg, strlen(acknowledgementMsg));

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = read(filefd, buffer, sizeof(buffer))) > 0) {
        if (write(dataSocketFd, buffer, bytesRead) < 0) {
            perror("Write to data socket");
            break;
        }
    }

    close(filefd);
    close(dataSocketFd);
}



void handlePutFile(int controlSocketFd, int dataSocketFd, const char *pathname)
{
    if (dataSocketFd < 0)
    {
        const char *errorMsg = "ENo data connection established\n";
        write(controlSocketFd, errorMsg, strlen(errorMsg));
        return;
    }
    int filefd = open(pathname, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (filefd < 0) 
    {
        char errorMsg[BUFFER_SIZE];

        if (errno == EEXIST) 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EFile already exists: %s\n", strerror(errno));
        } 
        else 
        {
            snprintf(errorMsg, sizeof(errorMsg), "EFailed to open file: %s\n", strerror(errno));
        }

        write(controlSocketFd, errorMsg, strlen(errorMsg));
        close(dataSocketFd);
        return;
    }
    const char *acknowledgementMsg = "A\n";
    write(controlSocketFd, acknowledgementMsg, strlen(acknowledgementMsg));

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = read(dataSocketFd, buffer, sizeof(buffer))) > 0)
    {
        if (write(filefd, buffer, bytesRead) < 0)
        {
            perror("write to file");
            break;
        }
    }
    close(filefd);
    close(dataSocketFd);
}

int processCommand(int clientfd, char *command, int dataSocketFd)
{
    if (command[0] == '\0')
    {
        return dataSocketFd;
    }

    char commandSpecifier = command[0];
    char *parameter = command + 1; 

    char *newline = strchr(parameter, '\n');
    if (newline)
    {
        *newline = '\0';
    }

    switch (commandSpecifier)
    {
        case 'D':
            if (dataSocketFd != -1)
            {
                close(dataSocketFd);
            }
            return handleDataConnection(clientfd);
        case 'C':
            if (dataSocketFd != -1)
            {
                close(dataSocketFd);
            }
            handleChangeDirectory(clientfd, parameter);
            break;
        case 'L':
            handleListDirectory(clientfd, dataSocketFd);
            break;
        case 'G':
            handleGetFile(clientfd, dataSocketFd, parameter);
            break;
        case 'P':
            handlePutFile(clientfd, dataSocketFd, parameter);
            break;
        case 'Q':
            if (write(clientfd, "A\n", 2) < 0) 
            {
                perror("write acknowledgement");
            }
            break;
        default:
            char errorMsg[BUFFER_SIZE];
            snprintf(errorMsg, sizeof(errorMsg), "EUnknown command\n");
            if (write(clientfd, errorMsg, strlen(errorMsg)) < 0)
            {
                perror("write error");
            }
    }
    return dataSocketFd;
}

int main(int argc, char *argv[])
{
    // Server socket setup is mostly the same as my assignment8 runServer function, with a few modifications throughout to fit this assignment7 requirements
    int serverSocketfd;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t addressLength = sizeof(clientAddress);

    serverSocketfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketfd == -1)
    {
        perror("socket");
        exit(1);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(12800);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSocketfd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
    {
        perror("bind");
        close(serverSocketfd);
        exit(1);
    }

    if (listen(serverSocketfd, BACKLOG) == -1)
    {
        perror("listen");
        close(serverSocketfd);
        exit(1);
    }

    printf("Server: waiting for connections on port %s...\n", PORT);

    while (1)
    {
        addressLength = sizeof(clientAddress);
        int clientfd = accept(serverSocketfd, (struct sockaddr *)&clientAddress, &addressLength);
        if (clientfd == -1)
        {
            perror("accept");
            continue;
        }
        printf("Server: got connection from %s\n", inet_ntoa(clientAddress.sin_addr));

        pid_t pid = fork();
        if (pid == 0) 
        {
            close(serverSocketfd);
            handleClient(clientfd);
            exit(0);
        } 
        else 
        {
            close(clientfd);

            while (waitpid(-1, NULL, WNOHANG) > 0) 
            {
            }
        }
    }
    close(serverSocketfd);
    return 0;
}
