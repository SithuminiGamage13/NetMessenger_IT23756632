#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 12632
#define BACKLOG 5
#define BUFFER_SIZE 1024
#define NID_TAG "NID:7566"

int main(void)
{
    int server_fd;
    int client_fd;
    int opt = 1;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];
    char username[50];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("NetMessenger server started.\n");
    printf("Registration Number: IT23756632\n");
    printf("Listening on port %d\n", PORT);

    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
        perror("accept");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Client connected from %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received =
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received <= 0)
    {
        printf("Client disconnected before registration.\n");
        close(client_fd);
        close(server_fd);
        return EXIT_SUCCESS;
    }

    buffer[bytes_received] = '\0';

    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("Received: %s\n", buffer);

    if (sscanf(buffer, "REGISTER %49s", username) == 1)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "OK REGISTERED %s %s\n",
                 username,
                 NID_TAG);

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Registered user: %s\n", username);
    }
    else
    {
        const char *response =
            "ERR 005 INVALID_COMMAND NID:7566\n";

        send(client_fd,
             response,
             strlen(response),
             0);
    }

    close(client_fd);
    close(server_fd);

    return EXIT_SUCCESS;
}
