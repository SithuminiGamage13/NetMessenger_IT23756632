#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define PORT 12632
#define BUFFER_SIZE 1024

static int sockfd;

static int send_all(int fd, const char *message)
{
    size_t len = strlen(message);
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = send(fd,
                         message + sent,
                         len - sent,
                         0);

        if (n <= 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

static void *receiver_thread(void *arg)
{
    (void)arg;

    char buffer[BUFFER_SIZE];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t n = recv(sockfd,
                         buffer,
                         sizeof(buffer) - 1,
                         0);

        if (n <= 0)
            break;

        buffer[n] = '\0';

        printf("\n%s", buffer);
        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

int main(void)
{
    struct sockaddr_in server_addr;

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

    if (connect(sockfd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connected to NetMessenger server %s:%d\n",
           SERVER_IP,
           PORT);

    pthread_t receiver;

    if (pthread_create(&receiver,
                       NULL,
                       receiver_thread,
                       NULL) != 0)
    {
        perror("pthread_create");
        close(sockfd);
        return EXIT_FAILURE;
    }

    char buffer[BUFFER_SIZE];

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(buffer,
                  sizeof(buffer),
                  stdin) == NULL)
        {
            break;
        }

        if (send_all(sockfd, buffer) < 0)
            break;

        if (strncmp(buffer, "QUIT", 4) == 0)
            break;
    }

    pthread_join(receiver, NULL);

    close(sockfd);

    return EXIT_SUCCESS;
}
