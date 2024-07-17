#include "myftp.h"

void exitOperation(int controlSocketfd);
void cdOperation(const char *pathname);
void rcdOperation(int controlSocketfd, const char *pathname);
void lsOperation();
void rlsOperation(int controlSocketfd, const char *serverIP);
void getOperation(int controlSocketfd, const char *pathname, const char *serverIP);
void showOperation(int controlSocketfd, const char *pathname, const char *serverIP);
void putOperation(int controlSocketfd, const char *pathname, const char *serverIP);



const char *getFilenameFromPath(const char *path) 
{
    const char *filename = path;
    for (const char *p = path; *p; p++) 
    {
        if (*p == '/') 
        {
            filename = p + 1;
        }
    }
    return filename;
}

int setupDataConnection(int controlSocketfd, const char *serverIP)
{
    const char *dCommand = "D\n";
    if (write(controlSocketfd, dCommand, strlen(dCommand)) < 0) 
    {
        perror("setupDataConnection: Write error on control socket");
        return -1;
    }

    char buffer[BUFFER_SIZE];
    int dataPort = -1;

    while (1) 
    {
        memset(buffer, 0, BUFFER_SIZE);
        if (read(controlSocketfd, buffer, BUFFER_SIZE - 1) <= 0) 
        {
            perror("setupDataConnection: Read error or server closed the connection");
            return -1;
        }

        if (buffer[0] == 'A') {
            if (sscanf(buffer, "A%d", &dataPort) == 1) 
            {
                break;
            }
        }
    }

    // Socket creation based on lecture slides socket routines
    int dataSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (dataSocketFd < 0) 
    {
        perror("setupDataConnection: Failed to create data socket");
        return -1;
    }
    // Partly based on lecture slides
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(dataPort); 
    inet_pton(AF_INET, serverIP, &serverAddr.sin_addr);

    // Connection based on lecture slides
    if (connect(dataSocketFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) 
    {
        perror("setupDataConnection: Failed to connect to data port");
        close(dataSocketFd);
        return -1;
    }

    return dataSocketFd;
}

void processCommand(char *command, int controlSocketfd, const char *serverIP) 
{
    char *args[10];
    int argc = 0;

    char *token = strtok(command, " \t\n");
    while (token != NULL && argc < 10) 
    {
        args[argc++] = token;
        token = strtok(NULL, " \t\n");
    }

    if (argc == 0) 
    {
        return;
    }

    char *cmd = args[0];

    if (strcmp(cmd, "exit") == 0) 
    {
        exitOperation(controlSocketfd);
    } 
    else if (strcmp(cmd, "cd") == 0 && argc > 1) 
    {
        cdOperation(args[1]);
    } 
    else if (strcmp(cmd, "rcd") == 0 && argc > 1) 
    {
        rcdOperation(controlSocketfd, args[1]);
    } 
    else if (strcmp(cmd, "ls") == 0) 
    {
        lsOperation();
    } 
    else if (strcmp(cmd, "rls") == 0) 
    {
        rlsOperation(controlSocketfd, serverIP);
    } 
    else if (strcmp(cmd, "get") == 0 && argc > 1) 
    {
        getOperation(controlSocketfd, args[1], serverIP);
    } 
    else if (strcmp(cmd, "show") == 0 && argc > 1) 
    {
        showOperation(controlSocketfd, args[1], serverIP);
    } 
    else if (strcmp(cmd, "put") == 0 && argc > 1) 
    {
        putOperation(controlSocketfd, args[1], serverIP);
    } 
    else 
    {
        printf("Unknown command or insufficient arguments: %s\n", cmd);
    }
}

void exitOperation(int controlSocketfd)
{
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    const char *exitSig  = "Q\n";
    if (write(controlSocketfd, exitSig, strlen(exitSig)) == -1)
    {
        perror("failed to send exit signal");
    }
    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
    } 
    else if (buffer[0] == 'A') 
    {
        printf("Server closed the connection\n");
    } 
    close(controlSocketfd);
    exit(0);
}

void cdOperation(const char *pathname)
{
    if (chdir(pathname) != 0) 
    {
        perror("cd (change directory) failed");
    }
    else
    {
        printf("changed directory to %s\n", pathname);
    }
}

void rcdOperation(int controlSocketfd, const char *pathname) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    snprintf(buffer, sizeof(buffer), "C%s\n", pathname);
    
    if (write(controlSocketfd, buffer, strlen(buffer)) == -1) 
    {
        perror("sending rcd command failed");
        return;
    }

    if (read(controlSocketfd, buffer, sizeof(buffer)) <= 0) 
    {
        perror("reading rcd response failed");
        return;
    }

    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
    } 
    else if (buffer[0] == 'A') 
    {
        printf("Directory changed successfully\n");
    } 
    else 
    {
        fprintf(stderr, "Unexpected response: %s\n", buffer);
    }
}

void lsOperation() {
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) 
    {
        perror("pipe");
        return;
    }

    pid = fork();
    if (pid == -1) 
    {
        perror("fork");
        return;
    }

    if (pid == 0) 
    {
     
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execlp("ls", "ls", "-l", NULL);
        perror("execlp ls");
        exit(EXIT_FAILURE);
    } 

    close(pipefd[1]); 

    pid_t more_pid = fork();
    if (more_pid == 0) 
    {
        dup2(pipefd[0], STDIN_FILENO);
        execlp("more", "more", "-20", NULL);
        perror("execlp more");
        exit(EXIT_FAILURE);
    } 
    else 
    {
        close(pipefd[0]);
        int status;
        waitpid(more_pid, &status, 0);
    }

    waitpid(pid, NULL, 0);
}

void rlsOperation(int controlSocketfd, const char *serverIP) 
{
    int dataSocketfd = setupDataConnection(controlSocketfd, serverIP);

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "L\n");
    if (write(controlSocketfd, buffer, strlen(buffer)) < 0) 
    {
        perror("Failed to send 'G' command");
        close(dataSocketfd);
        return;
    }

    if (read(controlSocketfd, buffer, BUFFER_SIZE) <= 0) 
    {
        perror("Failed to read server response for 'L' command");
        close(dataSocketfd);
        return;
    }

    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
        close(dataSocketfd);
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) 
    {
        perror("pipe");
        close(dataSocketfd);
        return;
    }

    pid_t pid = fork();
    if (pid == -1) 
    {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        close(dataSocketfd);
        return;
    }

    if (pid == 0) 
    {
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);

        execlp("more", "more", "-20", NULL);
        perror("execlp");
        exit(EXIT_FAILURE);
    }

    close(pipefd[0]); 

    char dataBuffer[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = read(dataSocketfd, dataBuffer, sizeof(dataBuffer) - 1)) > 0) 
    {
        dataBuffer[bytesRead] = '\0';
        if (write(pipefd[1], dataBuffer, bytesRead) < 0) 
        {
            perror("write to pipe");
            break;
        }
    }

    close(pipefd[1]);
    close(dataSocketfd);

    waitpid(pid, NULL, 0);
}

void getOperation(int controlSocketfd, const char *pathname, const char *serverIP) 
{
    int dataSocketfd = setupDataConnection(controlSocketfd, serverIP);
    if (dataSocketfd < 0) 
    {
        return;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "G%s\n", pathname);
    if (write(controlSocketfd, buffer, strlen(buffer)) < 0) 
    {
        perror("Failed to send 'G' command");
        close(dataSocketfd);
        return;
    }

    if (read(controlSocketfd, buffer, BUFFER_SIZE) <= 0) 
    {
        perror("Failed to read server response for 'G' command");
        close(dataSocketfd);
        return;
    }

    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
        close(dataSocketfd);
        return;
    }

    const char *filename = getFilenameFromPath(pathname);
    int filefd = open(filename, O_WRONLY | O_CREAT | O_EXCL | O_TRUNC, 0666);
    if (filefd < 0) 
    {
        perror("Error opening/creating file for writing");
        close(dataSocketfd);
        return;
    }

    ssize_t bytesRead;
    while ((bytesRead = read(dataSocketfd, buffer, sizeof(buffer))) > 0) 
    {
        if (write(filefd, buffer, bytesRead) < 0) 
        {
            perror("Failed to write to file");
            break;
        }
    }

    close(filefd);

    close(dataSocketfd);
}

void showOperation(int controlSocketfd, const char *pathname, const char *serverIP) {
    int dataSocketfd = setupDataConnection(controlSocketfd, serverIP);
    if (dataSocketfd < 0) 
    {
        return;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "G%s\n", pathname);
    if (write(controlSocketfd, buffer, strlen(buffer)) < 0) 
    {
        perror("Failed to send 'G' command");
        close(dataSocketfd);
        return;
    }

    if (read(controlSocketfd, buffer, BUFFER_SIZE) <= 0) 
    {
        perror("Failed to read server response for 'G' command");
        close(dataSocketfd);
        return;
    }

    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
        close(dataSocketfd);
        return;
    }

    pid_t pid = fork();
    if (pid == -1) 
    {
        perror("fork");
        close(dataSocketfd);
        return;
    }

    if (pid == 0) 
    { 
        int morePipe[2];
        if (pipe(morePipe) == -1) 
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        pid_t morePid = fork();
        if (morePid == -1) 
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (morePid == 0) 
        {
            close(morePipe[1]);
            dup2(morePipe[0], STDIN_FILENO);
            close(morePipe[0]);

            execlp("more", "more", "-20", NULL);
            perror("execlp more");
            exit(EXIT_FAILURE);
        } 
        else 
        {
            close(morePipe[0]);

            ssize_t bytesRead, totalBytesWritten, bytesWritten;
            while ((bytesRead = read(dataSocketfd, buffer, sizeof(buffer))) > 0) 
            {
                totalBytesWritten = 0;
                while (totalBytesWritten < bytesRead) 
                {
                    bytesWritten = write(morePipe[1], buffer + totalBytesWritten, bytesRead - totalBytesWritten);
                    if (bytesWritten < 0) 
                    {
                        perror("write to more pipe");
                        break;
                    }
                    totalBytesWritten += bytesWritten;
                }
            }

            close(morePipe[1]);
            close(dataSocketfd);

            waitpid(morePid, NULL, 0);
            exit(0);
        }
    } 
    else 
    {
        close(dataSocketfd);
        waitpid(pid, NULL, 0);
    }
}

void putOperation(int controlSocketfd, const char *pathname, const char *serverIP) 
{
    int dataSocketfd = setupDataConnection(controlSocketfd, serverIP);
    if (dataSocketfd < 0) 
    {
        return;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "P%s\n", pathname);
    if (write(controlSocketfd, buffer, strlen(buffer)) < 0) 
    {
        perror("Failed to send 'P' command");
        close(dataSocketfd);
        return;
    }

    struct stat statbuf;
    if (lstat(pathname, &statbuf) != 0) 
    {
        perror("Failed to get file status");
        close(dataSocketfd);
        return;
    }
    if (!S_ISREG(statbuf.st_mode)) 
    {
        fprintf(stderr, "Error: Not a regular file or is a symbolic link\n");
        close(dataSocketfd);
        return;
    }

    if (read(controlSocketfd, buffer, BUFFER_SIZE) <= 0) 
    {
        perror("Failed to read server response for 'P' command");
        close(dataSocketfd);
        return;
    }

    if (buffer[0] == 'E') 
    {
        fprintf(stderr, "Server error: %s\n", buffer + 1);
        close(dataSocketfd);
        return;
    }

    int filefd = open(pathname, O_RDONLY);
    if (filefd < 0) 
    {
        perror("Failed to open file for reading");
        close(dataSocketfd);
        return;
    }

    char fileBuffer[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = read(filefd, fileBuffer, sizeof(fileBuffer))) > 0) 
    {
        if (write(dataSocketfd, fileBuffer, bytesRead) < 0) 
        {
            perror("Failed to write to data socket");
            break;
        }
    }

    close(filefd);
    close(dataSocketfd);
}



int main(int argc, char *argv[]) 
{
    if (argc < 2) 
    {
        fprintf(stderr, "Usage: %s hostname\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Setting up address of the server based on lecture slides from class (with a few modifications)
    char hostName[NI_MAXHOST];
    strncpy(hostName, argv[1], NI_MAXHOST);
    int socketfd; 
    struct addrinfo hints, *actualdata; 

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_family = AF_INET; 

    int err = getaddrinfo(hostName, PORT, &hints, &actualdata);
    if (err != 0) 
    {
        perror(gai_strerror(err));
        exit(EXIT_FAILURE);
    }
    socketfd = socket(actualdata->ai_family, actualdata->ai_socktype, 0);

    if (socketfd < 0)
    {
        perror("failed to create socket");
        exit(EXIT_FAILURE);
    }
    //socket connection based on lecture slides
    if (connect(socketfd, actualdata->ai_addr, actualdata->ai_addrlen) < 0)
    {
        close(socketfd);
        perror("failed to connect to server");
        exit(EXIT_FAILURE);
    }

    char serverIP[INET_ADDRSTRLEN];
    if (actualdata->ai_family == AF_INET) 
    {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)actualdata->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), serverIP, INET_ADDRSTRLEN);
    }
    else
    {
        fprintf(stderr, "IPv4 addresses only.\n");
        exit(EXIT_FAILURE);
    }

    printf("Connected to %s\n", hostName);
   
    char command[BUFFER_SIZE];
    while (1) 
    {
        printf("MYFTP> ");
        if (fgets(command, sizeof(command), stdin) == NULL) 
        {
            break;
        }

        command[strcspn(command, "\n")] = 0;
        processCommand(command, socketfd, serverIP);
        memset(command, 0, BUFFER_SIZE);
    }
    freeaddrinfo(actualdata);
    close(socketfd);

    return 0;
}