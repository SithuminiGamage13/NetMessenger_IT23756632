#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#define PORT 12632
#define BACKLOG 10
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define USERNAME_SIZE 50
#define NID_TAG "NID:7566"

typedef struct
{
    int socket_fd;
    int active;
    int registered;
    char username[USERNAME_SIZE];
} Client;

Client clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Send the complete response even if send() writes only part of it */
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


/* Check whether a username is already being used */
static int username_exists(const char *username)
{
    int exists = 0;

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(clients[i].username, username) == 0)
        {
            exists = 1;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    return exists;
}


/* Build the comma-separated LIST result */
static void build_user_list(char *output, size_t output_size)
{
    output[0] = '\0';

    pthread_mutex_lock(&clients_mutex);

    int first = 1;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active &&
            clients[i].registered)
        {
            if (!first)
            {
                strncat(output,
                        ",",
                        output_size - strlen(output) - 1);
            }

            strncat(output,
                    clients[i].username,
                    output_size - strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}


/* Remove a client from the global client table */
static void remove_client(int index)
{
    pthread_mutex_lock(&clients_mutex);

    clients[index].socket_fd = -1;
    clients[index].active = 0;
    clients[index].registered = 0;
    clients[index].username[0] = '\0';

    pthread_mutex_unlock(&clients_mutex);
}


/* Each connected client runs inside its own thread */
static void *handle_client(void *arg)
{
    int index = *((int *)arg);
    free(arg);

    int client_fd = clients[index].socket_fd;

    char buffer[BUFFER_SIZE];
    char username[USERNAME_SIZE];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received =
            recv(client_fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (bytes_received <= 0)
        {
            pthread_mutex_lock(&clients_mutex);

            if (clients[index].registered)
            {
                printf("User %s disconnected unexpectedly.\n",
                       clients[index].username);
            }
            else
            {
                printf("Unregistered client disconnected.\n");
            }

            pthread_mutex_unlock(&clients_mutex);

            break;
        }

        buffer[bytes_received] = '\0';

        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Client %d received: %s\n",
               index,
               buffer);

        /*
         * A new client must REGISTER before using
         * any other command.
         */
        if (!clients[index].registered)
        {
            memset(username, 0, sizeof(username));

            if (sscanf(buffer,
                       "REGISTER %49s",
                       username) == 1)
            {
                if (username_exists(username))
                {
                    send_all(
                        client_fd,
                        "ERR 001 USERNAME_TAKEN NID:7566\n");

                    continue;
                }

                pthread_mutex_lock(&clients_mutex);

                clients[index].registered = 1;

                strncpy(clients[index].username,
                        username,
                        USERNAME_SIZE - 1);

                clients[index].username[USERNAME_SIZE - 1] = '\0';

                pthread_mutex_unlock(&clients_mutex);

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "OK REGISTERED %s %s\n",
                         username,
                         NID_TAG);

                send_all(client_fd, response);

                printf("Registered user: %s\n",
                       username);
            }
            else
            {
                send_all(
                    client_fd,
                    "ERR 005 REGISTER_REQUIRED NID:7566\n");
            }

            continue;
        }


        /* LIST command */
        if (strcmp(buffer, "LIST") == 0)
        {
            char user_list[BUFFER_SIZE];
            char response[BUFFER_SIZE + 64];

            build_user_list(user_list,
                            sizeof(user_list));

            snprintf(response,
                     sizeof(response),
                     "OK USERS %s %s\n",
                     user_list,
                     NID_TAG);

            send_all(client_fd, response);
        }


        /* QUIT command */
        else if (strcmp(buffer, "QUIT") == 0)
        {
            send_all(
                client_fd,
                "OK BYE NID:7566\n");

            pthread_mutex_lock(&clients_mutex);

            printf("User %s disconnected gracefully.\n",
                   clients[index].username);

            pthread_mutex_unlock(&clients_mutex);

            break;
        }


        /* Anything else is currently invalid */
        else
        {
            send_all(
                client_fd,
                "ERR 005 INVALID_COMMAND NID:7566\n");
        }
    }

    close(client_fd);

    remove_client(index);

    return NULL;
}


int main(void)
{
    int server_fd;
    int opt = 1;

    struct sockaddr_in server_addr;

    memset(clients, 0, sizeof(clients));

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket_fd = -1;
    }


    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }


    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        return EXIT_FAILURE;
    }


    memset(&server_addr,
           0,
           sizeof(server_addr));

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


    if (listen(server_fd,
               BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }


    printf("NetMessenger multi-client server started.\n");
    printf("Registration Number: IT23756632\n");
    printf("Listening on port %d\n", PORT);
    printf("Maximum clients: %d\n", MAX_CLIENTS);


    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_fd =
            accept(server_fd,
                   (struct sockaddr *)&client_addr,
                   &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }


        int index = -1;

        pthread_mutex_lock(&clients_mutex);

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!clients[i].active)
            {
                index = i;

                clients[i].socket_fd =
                    client_fd;

                clients[i].active = 1;
                clients[i].registered = 0;
                clients[i].username[0] = '\0';

                break;
            }
        }

        pthread_mutex_unlock(&clients_mutex);


        if (index == -1)
        {
            send_all(
                client_fd,
                "ERR 006 SERVER_FULL NID:7566\n");

            close(client_fd);

            continue;
        }


        printf("Client connected from %s:%d "
               "(slot %d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               index);


        int *thread_index =
            malloc(sizeof(int));

        if (thread_index == NULL)
        {
            perror("malloc");

            close(client_fd);
            remove_client(index);

            continue;
        }

        *thread_index = index;


        pthread_t thread_id;

        if (pthread_create(&thread_id,
                           NULL,
                           handle_client,
                           thread_index) != 0)
        {
            perror("pthread_create");

            free(thread_index);
            close(client_fd);
            remove_client(index);

            continue;
        }


        pthread_detach(thread_id);
    }


    close(server_fd);

    return EXIT_SUCCESS;
}
