#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 12632
#define BUFFER_SIZE 1024

static int send_all(int fd, const char *message)
{
    size_t length = strlen(message);
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t n = send(fd,
                         message + sent,
                         length - sent,
                         0);

        if (n <= 0)
        {
            return -1;
        }

        sent += (size_t)n;
    }

    return 0;
}

int main(void)
{
    int sockfd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connecting to NetMessenger server...\n");

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connected successfully to %s:%d\n",
           SERVER_IP,
           PORT);

    while (1)
    {
        printf("> ");

        if (fgets(buffer, sizeof(buffer), stdin) == NULL)
        {
            break;
        }

        if (send_all(sockfd, buffer) < 0)
        {
            printf("Failed to send command.\n");
            break;
        }

        memset(response, 0, sizeof(response));

        ssize_t bytes_received =
            recv(sockfd,
                 response,
                 sizeof(response) - 1,
                 0);

        if (bytes_received <= 0)
        {
            printf("Server disconnected.\n");
            break;
        }

        response[bytes_received] = '\0';

        printf("Server: %s", response);

        if (strncmp(buffer, "QUIT", 4) == 0)
        {
            break;
        }
    }

    close(sockfd);

    return EXIT_SUCCESS;
}
