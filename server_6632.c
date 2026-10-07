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

static Client clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

static int send_all(int fd, const char *message)
{
    size_t len = strlen(message);
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = send(fd, message + sent, len - sent, 0);

        if (n <= 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

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

static void build_user_list(char *output, size_t size)
{
    output[0] = '\0';

    pthread_mutex_lock(&clients_mutex);

    int first = 1;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active && clients[i].registered)
        {
            if (!first)
                strncat(output, ",", size - strlen(output) - 1);

            strncat(output,
                    clients[i].username,
                    size - strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

static void remove_client(int index)
{
    pthread_mutex_lock(&clients_mutex);

    clients[index].socket_fd = -1;
    clients[index].active = 0;
    clients[index].registered = 0;
    clients[index].username[0] = '\0';

    pthread_mutex_unlock(&clients_mutex);
}

static void broadcast_message(int sender_index, const char *message)
{
    char outgoing[BUFFER_SIZE + 128];

    snprintf(outgoing,
             sizeof(outgoing),
             "MSG BCAST %s %s\n",
             clients[sender_index].username,
             message);

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i != sender_index &&
            clients[i].active &&
            clients[i].registered)
        {
            send_all(clients[i].socket_fd, outgoing);
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}

static int private_message(int sender_index,
                           const char *target,
                           const char *message)
{
    int found = 0;

    char outgoing[BUFFER_SIZE + 128];

    snprintf(outgoing,
             sizeof(outgoing),
             "MSG PRIV %s %s\n",
             clients[sender_index].username,
             message);

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(clients[i].username, target) == 0)
        {
            send_all(clients[i].socket_fd, outgoing);
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    return found;
}

static void *handle_client(void *arg)
{
    int index = *((int *)arg);
    free(arg);

    int fd = clients[index].socket_fd;

    char buffer[BUFFER_SIZE];
    char username[USERNAME_SIZE];

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t n = recv(fd,
                         buffer,
                         sizeof(buffer) - 1,
                         0);

        if (n <= 0)
        {
            if (clients[index].registered)
            {
                printf("User %s disconnected unexpectedly.\n",
                       clients[index].username);
            }

            break;
        }

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Client %d received: %s\n",
               index,
               buffer);

        if (!clients[index].registered)
        {
            memset(username, 0, sizeof(username));

            if (sscanf(buffer,
                       "REGISTER %49s",
                       username) == 1)
            {
                if (username_exists(username))
                {
                    send_all(fd,
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

                char response[128];

                snprintf(response,
                         sizeof(response),
                         "OK REGISTERED %s %s\n",
                         username,
                         NID_TAG);

                send_all(fd, response);

                printf("Registered user: %s\n", username);
            }
            else
            {
                send_all(fd,
                         "ERR 005 REGISTER_REQUIRED NID:7566\n");
            }

            continue;
        }

        if (strcmp(buffer, "LIST") == 0)
        {
            char list[BUFFER_SIZE];
            char response[BUFFER_SIZE + 64];

            build_user_list(list, sizeof(list));

            snprintf(response,
                     sizeof(response),
                     "OK USERS %s %s\n",
                     list,
                     NID_TAG);

            send_all(fd, response);
        }
        else if (strncmp(buffer, "BCAST ", 6) == 0)
        {
            const char *message = buffer + 6;

            if (*message == '\0')
            {
                send_all(fd,
                         "ERR 005 INVALID_COMMAND NID:7566\n");
            }
            else
            {
                broadcast_message(index, message);

                send_all(fd,
                         "OK SENT NID:7566\n");
            }
        }
        else if (strncmp(buffer, "PMSG ", 5) == 0)
        {
            char target[USERNAME_SIZE];
            char message[BUFFER_SIZE];

            memset(target, 0, sizeof(target));
            memset(message, 0, sizeof(message));

            if (sscanf(buffer,
                       "PMSG %49s %1023[^\n]",
                       target,
                       message) == 2)
            {
                if (private_message(index,
                                    target,
                                    message))
                {
                    send_all(fd,
                             "OK SENT NID:7566\n");
                }
                else
                {
                    send_all(fd,
                             "ERR 002 USER_NOT_FOUND NID:7566\n");
                }
            }
            else
            {
                send_all(fd,
                         "ERR 005 INVALID_COMMAND NID:7566\n");
            }
        }
        else if (strcmp(buffer, "QUIT") == 0)
        {
            send_all(fd,
                     "OK BYE NID:7566\n");

            printf("User %s disconnected gracefully.\n",
                   clients[index].username);

            break;
        }
        else
        {
            send_all(fd,
                     "ERR 005 INVALID_COMMAND NID:7566\n");
        }
    }

    close(fd);
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
        clients[i].socket_fd = -1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

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
    printf("Maximum clients: %d\n", MAX_CLIENTS);

    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

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

                clients[i].socket_fd = client_fd;
                clients[i].active = 1;
                clients[i].registered = 0;
                clients[i].username[0] = '\0';

                break;
            }
        }

        pthread_mutex_unlock(&clients_mutex);

        if (index == -1)
        {
            send_all(client_fd,
                     "ERR 006 SERVER_FULL NID:7566\n");

            close(client_fd);
            continue;
        }

        printf("Client connected from %s:%d (slot %d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               index);

        int *thread_index = malloc(sizeof(int));

        if (thread_index == NULL)
        {
            close(client_fd);
            remove_client(index);
            continue;
        }

        *thread_index = index;

        pthread_t tid;

        if (pthread_create(&tid,
                           NULL,
                           handle_client,
                           thread_index) != 0)
        {
            free(thread_index);
            close(client_fd);
            remove_client(index);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);

    return EXIT_SUCCESS;
} 
