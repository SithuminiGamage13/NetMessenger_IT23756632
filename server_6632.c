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
#define MAX_ROOMS 20
#define BUFFER_SIZE 1024
#define USERNAME_SIZE 50
#define ROOM_NAME_SIZE 50
#define NID_TAG "NID:7566"

typedef struct
{
    int socket_fd;
    int active;
    int registered;
    char username[USERNAME_SIZE];
} Client;

typedef struct
{
    int active;
    char name[ROOM_NAME_SIZE];
    int members[MAX_CLIENTS];
} Room;

static Client clients[MAX_CLIENTS];
static Room rooms[MAX_ROOMS];

static pthread_mutex_t state_mutex =
    PTHREAD_MUTEX_INITIALIZER;


/* -------------------------------------------------- */
/* SEND COMPLETE TEXT MESSAGE                         */
/* -------------------------------------------------- */

static int send_all(int fd, const char *message)
{
    size_t length = strlen(message);
    size_t sent = 0;

    while (sent < length)
    {
        ssize_t n =
            send(fd,
                 message + sent,
                 length - sent,
                 0);

        if (n <= 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}


/* -------------------------------------------------- */
/* USER FUNCTIONS                                     */
/* -------------------------------------------------- */

static int username_exists(const char *username)
{
    int exists = 0;

    pthread_mutex_lock(&state_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(clients[i].username,
                   username) == 0)
        {
            exists = 1;
            break;
        }
    }

    pthread_mutex_unlock(&state_mutex);

    return exists;
}


static void build_user_list(char *output,
                            size_t output_size)
{
    output[0] = '\0';

    pthread_mutex_lock(&state_mutex);

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
                        output_size -
                        strlen(output) - 1);
            }

            strncat(output,
                    clients[i].username,
                    output_size -
                    strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(&state_mutex);
}


/* -------------------------------------------------- */
/* BROADCAST                                          */
/* -------------------------------------------------- */

static void broadcast_message(int sender_index,
                              const char *message)
{
    char outgoing[BUFFER_SIZE + 128];

    snprintf(outgoing,
             sizeof(outgoing),
             "MSG BCAST %s %s\n",
             clients[sender_index].username,
             message);

    pthread_mutex_lock(&state_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i != sender_index &&
            clients[i].active &&
            clients[i].registered)
        {
            send_all(clients[i].socket_fd,
                     outgoing);
        }
    }

    pthread_mutex_unlock(&state_mutex);
}


/* -------------------------------------------------- */
/* PRIVATE MESSAGE                                    */
/* -------------------------------------------------- */

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

    pthread_mutex_lock(&state_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(clients[i].username,
                   target) == 0)
        {
            send_all(clients[i].socket_fd,
                     outgoing);

            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&state_mutex);

    return found;
}


/* -------------------------------------------------- */
/* ROOM FUNCTIONS                                     */
/* -------------------------------------------------- */

static int find_room(const char *room_name)
{
    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active &&
            strcmp(rooms[i].name,
                   room_name) == 0)
        {
            return i;
        }
    }

    return -1;
}


static int join_room(int client_index,
                     const char *room_name)
{
    int room_index;

    pthread_mutex_lock(&state_mutex);

    room_index = find_room(room_name);

    if (room_index == -1)
    {
        for (int i = 0; i < MAX_ROOMS; i++)
        {
            if (!rooms[i].active)
            {
                room_index = i;

                rooms[i].active = 1;

                strncpy(rooms[i].name,
                        room_name,
                        ROOM_NAME_SIZE - 1);

                rooms[i].name[
                    ROOM_NAME_SIZE - 1] = '\0';

                memset(rooms[i].members,
                       0,
                       sizeof(rooms[i].members));

                break;
            }
        }
    }

    if (room_index != -1)
    {
        rooms[room_index]
            .members[client_index] = 1;
    }

    pthread_mutex_unlock(&state_mutex);

    return room_index;
}


static int leave_room(int client_index,
                      const char *room_name)
{
    int result = -1;

    pthread_mutex_lock(&state_mutex);

    int room_index = find_room(room_name);

    if (room_index != -1)
    {
        if (rooms[room_index]
                .members[client_index])
        {
            rooms[room_index]
                .members[client_index] = 0;

            result = 1;
        }
        else
        {
            result = 0;
        }
    }

    pthread_mutex_unlock(&state_mutex);

    return result;
}


static void build_room_list(char *output,
                            size_t output_size)
{
    output[0] = '\0';

    pthread_mutex_lock(&state_mutex);

    int first = 1;

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active)
        {
            if (!first)
            {
                strncat(output,
                        ",",
                        output_size -
                        strlen(output) - 1);
            }

            strncat(output,
                    rooms[i].name,
                    output_size -
                    strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(&state_mutex);
}


static int room_message(int sender_index,
                        const char *room_name,
                        const char *message)
{
    int room_index;
    int sender_is_member = 0;

    char outgoing[BUFFER_SIZE + 160];

    pthread_mutex_lock(&state_mutex);

    room_index = find_room(room_name);

    if (room_index == -1)
    {
        pthread_mutex_unlock(&state_mutex);
        return -1;
    }

    sender_is_member =
        rooms[room_index]
            .members[sender_index];

    if (!sender_is_member)
    {
        pthread_mutex_unlock(&state_mutex);
        return 0;
    }

    snprintf(outgoing,
             sizeof(outgoing),
             "MSG ROOM %s %s %s\n",
             room_name,
             clients[sender_index].username,
             message);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (rooms[room_index].members[i] &&
            clients[i].active &&
            clients[i].registered)
        {
            send_all(clients[i].socket_fd,
                     outgoing);
        }
    }

    pthread_mutex_unlock(&state_mutex);

    return 1;
}


/* -------------------------------------------------- */
/* REMOVE CLIENT                                      */
/* -------------------------------------------------- */

static void remove_client(int index)
{
    pthread_mutex_lock(&state_mutex);

    for (int i = 0; i < MAX_ROOMS; i++)
    {
        if (rooms[i].active)
        {
            rooms[i].members[index] = 0;
        }
    }

    clients[index].socket_fd = -1;
    clients[index].active = 0;
    clients[index].registered = 0;
    clients[index].username[0] = '\0';

    pthread_mutex_unlock(&state_mutex);
}


/* -------------------------------------------------- */
/* CLIENT THREAD                                      */
/* -------------------------------------------------- */

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

        ssize_t n =
            recv(fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (n <= 0)
        {
            if (clients[index].registered)
            {
                printf(
                    "User %s disconnected unexpectedly.\n",
                    clients[index].username);
            }

            break;
        }

        buffer[n] = '\0';

        buffer[strcspn(buffer,
                       "\r\n")] = '\0';

        printf("Client %d received: %s\n",
               index,
               buffer);


        /* ------------------------------------------ */
        /* REGISTER                                   */
        /* ------------------------------------------ */

        if (!clients[index].registered)
        {
            memset(username,
                   0,
                   sizeof(username));

            if (sscanf(buffer,
                       "REGISTER %49s",
                       username) == 1)
            {
                if (username_exists(username))
                {
                    send_all(
                        fd,
                        "ERR 001 USERNAME_TAKEN "
                        "NID:7566\n");

                    continue;
                }

                pthread_mutex_lock(
                    &state_mutex);

                clients[index].registered = 1;

                strncpy(
                    clients[index].username,
                    username,
                    USERNAME_SIZE - 1);

                clients[index]
                    .username[
                        USERNAME_SIZE - 1] = '\0';

                pthread_mutex_unlock(
                    &state_mutex);

                char response[128];

                snprintf(
                    response,
                    sizeof(response),
                    "OK REGISTERED %s %s\n",
                    username,
                    NID_TAG);

                send_all(fd, response);

                printf(
                    "Registered user: %s\n",
                    username);
            }
            else
            {
                send_all(
                    fd,
                    "ERR 005 REGISTER_REQUIRED "
                    "NID:7566\n");
            }

            continue;
        }


        /* ------------------------------------------ */
        /* LIST                                       */
        /* ------------------------------------------ */

        if (strcmp(buffer,
                   "LIST") == 0)
        {
            char list[BUFFER_SIZE];

            char response[
                BUFFER_SIZE + 64];

            build_user_list(
                list,
                sizeof(list));

            snprintf(
                response,
                sizeof(response),
                "OK USERS %s %s\n",
                list,
                NID_TAG);

            send_all(fd, response);
        }


        /* ------------------------------------------ */
        /* BCAST                                      */
        /* ------------------------------------------ */

        else if (strncmp(buffer,
                         "BCAST ",
                         6) == 0)
        {
            const char *message =
                buffer + 6;

            if (*message == '\0')
            {
                send_all(
                    fd,
                    "ERR 005 INVALID_COMMAND "
                    "NID:7566\n");
            }
            else
            {
                broadcast_message(
                    index,
                    message);

                send_all(
                    fd,
                    "OK SENT NID:7566\n");
            }
        }


        /* ------------------------------------------ */
        /* PMSG                                       */
        /* ------------------------------------------ */

        else if (strncmp(buffer,
                         "PMSG ",
                         5) == 0)
        {
            char target[USERNAME_SIZE];
            char message[BUFFER_SIZE];

            memset(target,
                   0,
                   sizeof(target));

            memset(message,
                   0,
                   sizeof(message));

            if (sscanf(
                    buffer,
                    "PMSG %49s %1023[^\n]",
                    target,
                    message) == 2)
            {
                if (private_message(
                        index,
                        target,
                        message))
                {
                    send_all(
                        fd,
                        "OK SENT NID:7566\n");
                }
                else
                {
                    send_all(
                        fd,
                        "ERR 002 USER_NOT_FOUND "
                        "NID:7566\n");
                }
            }
            else
            {
                send_all(
                    fd,
                    "ERR 005 INVALID_COMMAND "
                    "NID:7566\n");
            }
        }


        /* ------------------------------------------ */
        /* JOIN                                       */
        /* ------------------------------------------ */

        else if (strncmp(buffer,
                         "JOIN ",
                         5) == 0)
        {
            char room_name[ROOM_NAME_SIZE];

            if (sscanf(buffer,
                       "JOIN %49s",
                       room_name) == 1)
            {
                if (join_room(
                        index,
                        room_name) != -1)
                {
                    char response[128];

                    snprintf(
                        response,
                        sizeof(response),
                        "OK JOINED %s %s\n",
                        room_name,
                        NID_TAG);

                    send_all(fd, response);
                }
                else
                {
                    send_all(
                        fd,
                        "ERR 008 ROOM_LIMIT_REACHED "
                        "NID:7566\n");
                }
            }
            else
            {
                send_all(
                    fd,
                    "ERR 005 INVALID_COMMAND "
                    "NID:7566\n");
            }
        }


        /* ------------------------------------------ */
        /* LEAVE                                      */
        /* ------------------------------------------ */

        else if (strncmp(buffer,
                         "LEAVE ",
                         6) == 0)
        {
            char room_name[ROOM_NAME_SIZE];

            if (sscanf(buffer,
                       "LEAVE %49s",
                       room_name) == 1)
            {
                int result =
                    leave_room(
                        index,
                        room_name);

                if (result == 1)
                {
                    char response[128];

                    snprintf(
                        response,
                        sizeof(response),
                        "OK LEFT %s %s\n",
                        room_name,
                        NID_TAG);

                    send_all(fd,
                             response);
                }
                else if (result == -1)
                {
                    send_all(
                        fd,
                        "ERR 003 ROOM_NOT_FOUND "
                        "NID:7566\n");
                }
                else
                {
                    send_all(
                        fd,
                        "ERR 007 NOT_IN_ROOM "
                        "NID:7566\n");
                }
            }
            else
            {
                send_all(
                    fd,
                    "ERR 005 INVALID_COMMAND "
                    "NID:7566\n");
            }
        }


        /* ------------------------------------------ */
        /* ROOMS                                      */
        /* ------------------------------------------ */

        else if (strcmp(buffer,
                        "ROOMS") == 0)
        {
            char room_list[BUFFER_SIZE];

            char response[
                BUFFER_SIZE + 64];

            build_room_list(
                room_list,
                sizeof(room_list));

            snprintf(
                response,
                sizeof(response),
                "OK ROOMS %s %s\n",
                room_list,
                NID_TAG);

            send_all(fd, response);
        }


        /* ------------------------------------------ */
        /* RMSG                                       */
        /* ------------------------------------------ */

        else if (strncmp(buffer,
                         "RMSG ",
                         5) == 0)
        {
            char room_name[ROOM_NAME_SIZE];
            char message[BUFFER_SIZE];

            memset(room_name,
                   0,
                   sizeof(room_name));

            memset(message,
                   0,
                   sizeof(message));

            if (sscanf(
                    buffer,
                    "RMSG %49s %1023[^\n]",
                    room_name,
                    message) == 2)
            {
                int result =
                    room_message(
                        index,
                        room_name,
                        message);

                if (result == 1)
                {
                    send_all(
                        fd,
                        "OK SENT NID:7566\n");
                }
                else if (result == -1)
                {
                    send_all(
                        fd,
                        "ERR 003 ROOM_NOT_FOUND "
                        "NID:7566\n");
                }
                else
                {
                    send_all(
                        fd,
                        "ERR 007 NOT_IN_ROOM "
                        "NID:7566\n");
                }
            }
            else
            {
                send_all(
                    fd,
                    "ERR 005 INVALID_COMMAND "
                    "NID:7566\n");
            }
        }


        /* ------------------------------------------ */
        /* QUIT                                       */
        /* ------------------------------------------ */

        else if (strcmp(buffer,
                        "QUIT") == 0)
        {
            send_all(
                fd,
                "OK BYE NID:7566\n");

            printf(
                "User %s disconnected gracefully.\n",
                clients[index].username);

            break;
        }


        /* ------------------------------------------ */
        /* INVALID                                    */
        /* ------------------------------------------ */

        else
        {
            send_all(
                fd,
                "ERR 005 INVALID_COMMAND "
                "NID:7566\n");
        }
    }

    close(fd);

    remove_client(index);

    return NULL;
}


/* -------------------------------------------------- */
/* MAIN                                               */
/* -------------------------------------------------- */

int main(void)
{
    int server_fd;
    int opt = 1;

    struct sockaddr_in server_addr;

    memset(clients,
           0,
           sizeof(clients));

    memset(rooms,
           0,
           sizeof(rooms));

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        clients[i].socket_fd = -1;
    }


    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }


    if (setsockopt(
            server_fd,
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

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);


    if (bind(
            server_fd,
            (struct sockaddr *)
                &server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }


    if (listen(
            server_fd,
            BACKLOG) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }


    printf(
        "NetMessenger server started.\n");

    printf(
        "Registration Number: IT23756632\n");

    printf(
        "Listening on port %d\n",
        PORT);

    printf(
        "Maximum clients: %d\n",
        MAX_CLIENTS);


    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_fd =
            accept(
                server_fd,
                (struct sockaddr *)
                    &client_addr,
                &client_len);

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }


        int index = -1;

        pthread_mutex_lock(
            &state_mutex);

        for (int i = 0;
             i < MAX_CLIENTS;
             i++)
        {
            if (!clients[i].active)
            {
                index = i;

                clients[i].socket_fd =
                    client_fd;

                clients[i].active = 1;

                clients[i].registered = 0;

                clients[i]
                    .username[0] = '\0';

                break;
            }
        }

        pthread_mutex_unlock(
            &state_mutex);


        if (index == -1)
        {
            send_all(
                client_fd,
                "ERR 006 SERVER_FULL "
                "NID:7566\n");

            close(client_fd);

            continue;
        }


        printf(
            "Client connected from %s:%d "
            "(slot %d)\n",
            inet_ntoa(
                client_addr.sin_addr),
            ntohs(
                client_addr.sin_port),
            index);


        int *thread_index =
            malloc(sizeof(int));

        if (thread_index == NULL)
        {
            close(client_fd);

            remove_client(index);

            continue;
        }


        *thread_index = index;

        pthread_t thread_id;

        if (pthread_create(
                &thread_id,
                NULL,
                handle_client,
                thread_index) != 0)
        {
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
