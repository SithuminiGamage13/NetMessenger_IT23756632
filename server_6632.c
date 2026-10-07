#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define PORT 12632
#define BACKLOG 10
#define MAX_CLIENTS 10
#define MAX_ROOMS 20
#define BUFFER_SIZE 1024
#define USERNAME_SIZE 50
#define ROOM_NAME_SIZE 50
#define MAX_FILE_SIZE (5 * 1024 * 1024)

#define RATE_LIMIT_COMMANDS 8
#define RATE_LIMIT_WINDOW 5

#define NID_TAG "NID:7566"
#define STORAGE_ROOT "./storage/IT23756632"
#define LOG_FILE "netmsg_IT23756632.log"

typedef struct
{
    int socket_fd;
    int active;
    int registered;

    char username[USERNAME_SIZE];

    time_t rate_window_start;
    int command_count;

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

static pthread_mutex_t log_mutex =
    PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t send_mutex[MAX_CLIENTS];


/* --------------------------------------------------------- */
/* LOGGING                                                   */
/* --------------------------------------------------------- */

static void log_event(const char *event)
{
    pthread_mutex_lock(&log_mutex);

    FILE *fp =
        fopen(LOG_FILE, "a");

    if (fp != NULL)
    {
        time_t now =
            time(NULL);

        struct tm *tm_info =
            localtime(&now);

        char timestamp[32];

        strftime(
            timestamp,
            sizeof(timestamp),
            "%Y-%m-%d %H:%M:%S",
            tm_info);

        fprintf(
            fp,
            "[%s] %s\n",
            timestamp,
            event);

        fclose(fp);
    }

    pthread_mutex_unlock(&log_mutex);
}


/* --------------------------------------------------------- */
/* TCP FRAMING                                               */
/* --------------------------------------------------------- */

static ssize_t recv_line(int fd,
                         char *buffer,
                         size_t size)
{
    size_t used = 0;

    while (used < size - 1)
    {
        char c;

        ssize_t n =
            recv(fd,
                 &c,
                 1,
                 0);

        if (n == 0)
            return 0;

        if (n < 0)
            return -1;

        if (c == '\n')
            break;

        if (c != '\r')
        {
            buffer[used++] = c;
        }
    }

    buffer[used] = '\0';

    return (ssize_t)used;
}


static int recv_exact(int fd,
                      void *buffer,
                      size_t length)
{
    size_t total = 0;

    char *ptr =
        buffer;

    while (total < length)
    {
        ssize_t n =
            recv(fd,
                 ptr + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total +=
            (size_t)n;
    }

    return 0;
}


/* --------------------------------------------------------- */
/* SENDING                                                   */
/* --------------------------------------------------------- */

static int send_all_raw(int fd,
                        const void *data,
                        size_t length)
{
    const char *ptr =
        data;

    size_t total = 0;

    while (total < length)
    {
        ssize_t n =
            send(fd,
                 ptr + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total +=
            (size_t)n;
    }

    return 0;
}


static int send_text_index(int index,
                           const char *message)
{
    int result;

    pthread_mutex_lock(
        &send_mutex[index]);

    result =
        send_all_raw(
            clients[index].socket_fd,
            message,
            strlen(message));

    pthread_mutex_unlock(
        &send_mutex[index]);

    return result;
}


/* --------------------------------------------------------- */
/* RATE LIMITING                                             */
/* --------------------------------------------------------- */

static int check_rate_limit(int index)
{
    time_t now =
        time(NULL);

    int allowed = 1;

    pthread_mutex_lock(
        &state_mutex);

    if (clients[index].rate_window_start == 0 ||
        difftime(
            now,
            clients[index].rate_window_start)
            >= RATE_LIMIT_WINDOW)
    {
        clients[index].rate_window_start =
            now;

        clients[index].command_count =
            1;
    }
    else
    {
        clients[index].command_count++;

        if (clients[index].command_count >
            RATE_LIMIT_COMMANDS)
        {
            allowed = 0;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    return allowed;
}


/* --------------------------------------------------------- */
/* USER FUNCTIONS                                            */
/* --------------------------------------------------------- */

static int username_exists(const char *username)
{
    int exists = 0;

    pthread_mutex_lock(
        &state_mutex);

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(
                clients[i].username,
                username) == 0)
        {
            exists = 1;
            break;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    return exists;
}


static int find_user_index(const char *username)
{
    int result = -1;

    pthread_mutex_lock(
        &state_mutex);

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (clients[i].active &&
            clients[i].registered &&
            strcmp(
                clients[i].username,
                username) == 0)
        {
            result = i;
            break;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    return result;
}


static void build_user_list(char *output,
                            size_t size)
{
    output[0] = '\0';

    pthread_mutex_lock(
        &state_mutex);

    int first = 1;

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (clients[i].active &&
            clients[i].registered)
        {
            if (!first)
            {
                strncat(
                    output,
                    ",",
                    size -
                    strlen(output) - 1);
            }

            strncat(
                output,
                clients[i].username,
                size -
                strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);
}


/* --------------------------------------------------------- */
/* PRESENCE                                                  */
/* --------------------------------------------------------- */

static void presence_message(int source_index,
                             const char *username,
                             const char *action)
{
    int targets[MAX_CLIENTS];
    int count = 0;

    pthread_mutex_lock(
        &state_mutex);

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (i != source_index &&
            clients[i].active &&
            clients[i].registered)
        {
            targets[count++] = i;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    char message[160];

    snprintf(
        message,
        sizeof(message),
        "MSG BCAST SERVER %s %s\n",
        username,
        action);

    for (int i = 0;
         i < count;
         i++)
    {
        send_text_index(
            targets[i],
            message);
    }
}


/* --------------------------------------------------------- */
/* BROADCAST                                                 */
/* --------------------------------------------------------- */

static void broadcast_message(int sender_index,
                              const char *message)
{
    int targets[MAX_CLIENTS];
    int count = 0;

    char sender[USERNAME_SIZE];

    pthread_mutex_lock(
        &state_mutex);

    strncpy(
        sender,
        clients[sender_index].username,
        sizeof(sender) - 1);

    sender[
        sizeof(sender) - 1] = '\0';

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (i != sender_index &&
            clients[i].active &&
            clients[i].registered)
        {
            targets[count++] = i;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    char outgoing[
        BUFFER_SIZE + 128];

    snprintf(
        outgoing,
        sizeof(outgoing),
        "MSG BCAST %s %s\n",
        sender,
        message);

    for (int i = 0;
         i < count;
         i++)
    {
        send_text_index(
            targets[i],
            outgoing);
    }
}


/* --------------------------------------------------------- */
/* PRIVATE MESSAGE                                           */
/* --------------------------------------------------------- */

static int private_message(int sender_index,
                           const char *target,
                           const char *message)
{
    int target_index =
        find_user_index(target);

    if (target_index < 0)
        return 0;

    char outgoing[
        BUFFER_SIZE + 128];

    snprintf(
        outgoing,
        sizeof(outgoing),
        "MSG PRIV %s %s\n",
        clients[sender_index].username,
        message);

    send_text_index(
        target_index,
        outgoing);

    return 1;
}


/* --------------------------------------------------------- */
/* ROOM FUNCTIONS                                            */
/* --------------------------------------------------------- */

static int find_room_unlocked(const char *room_name)
{
    for (int i = 0;
         i < MAX_ROOMS;
         i++)
    {
        if (rooms[i].active &&
            strcmp(
                rooms[i].name,
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

    pthread_mutex_lock(
        &state_mutex);

    room_index =
        find_room_unlocked(
            room_name);

    if (room_index == -1)
    {
        for (int i = 0;
             i < MAX_ROOMS;
             i++)
        {
            if (!rooms[i].active)
            {
                room_index = i;

                rooms[i].active = 1;

                strncpy(
                    rooms[i].name,
                    room_name,
                    ROOM_NAME_SIZE - 1);

                rooms[i].name[
                    ROOM_NAME_SIZE - 1] =
                    '\0';

                memset(
                    rooms[i].members,
                    0,
                    sizeof(
                        rooms[i].members));

                break;
            }
        }
    }

    if (room_index != -1)
    {
        rooms[room_index]
            .members[client_index] = 1;
    }

    pthread_mutex_unlock(
        &state_mutex);

    return room_index;
}


static int leave_room(int client_index,
                      const char *room_name)
{
    int result = -1;

    pthread_mutex_lock(
        &state_mutex);

    int room_index =
        find_room_unlocked(
            room_name);

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

    pthread_mutex_unlock(
        &state_mutex);

    return result;
}


static void build_room_list(char *output,
                            size_t size)
{
    output[0] = '\0';

    pthread_mutex_lock(
        &state_mutex);

    int first = 1;

    for (int i = 0;
         i < MAX_ROOMS;
         i++)
    {
        if (rooms[i].active)
        {
            if (!first)
            {
                strncat(
                    output,
                    ",",
                    size -
                    strlen(output) - 1);
            }

            strncat(
                output,
                rooms[i].name,
                size -
                strlen(output) - 1);

            first = 0;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);
}


static int room_message(int sender_index,
                        const char *room_name,
                        const char *message)
{
    int targets[MAX_CLIENTS];
    int count = 0;

    pthread_mutex_lock(
        &state_mutex);

    int room_index =
        find_room_unlocked(
            room_name);

    if (room_index == -1)
    {
        pthread_mutex_unlock(
            &state_mutex);

        return -1;
    }

    if (!rooms[room_index]
             .members[sender_index])
    {
        pthread_mutex_unlock(
            &state_mutex);

        return 0;
    }

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (rooms[room_index]
                .members[i] &&
            clients[i].active &&
            clients[i].registered)
        {
            targets[count++] = i;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    char outgoing[
        BUFFER_SIZE + 160];

    snprintf(
        outgoing,
        sizeof(outgoing),
        "MSG ROOM %s %s %s\n",
        room_name,
        clients[sender_index].username,
        message);

    for (int i = 0;
         i < count;
         i++)
    {
        send_text_index(
            targets[i],
            outgoing);
    }

    return 1;
}


/* --------------------------------------------------------- */
/* STORAGE                                                   */
/* --------------------------------------------------------- */

static int create_directory(const char *path)
{
    if (mkdir(path, 0755) == 0)
        return 0;

    if (errno == EEXIST)
        return 0;

    return -1;
}


static int valid_filename(const char *filename)
{
    if (strstr(
            filename,
            "..") != NULL)
    {
        return 0;
    }

    if (strchr(
            filename,
            '/') != NULL)
    {
        return 0;
    }

    if (strchr(
            filename,
            '\\') != NULL)
    {
        return 0;
    }

    return 1;
}


static int store_received_file(
    int fd,
    const char *username,
    const char *filename,
    size_t filesize,
    char *saved_path,
    size_t saved_path_size)
{
    if (create_directory(
            "./storage") < 0)
    {
        return -1;
    }

    if (create_directory(
            STORAGE_ROOT) < 0)
    {
        return -1;
    }

    char user_directory[256];

    snprintf(
        user_directory,
        sizeof(user_directory),
        "%s/%s",
        STORAGE_ROOT,
        username);

    if (create_directory(
            user_directory) < 0)
    {
        return -1;
    }

    snprintf(
        saved_path,
        saved_path_size,
        "%s/%s",
        user_directory,
        filename);

    FILE *fp =
        fopen(saved_path,
              "wb");

    if (fp == NULL)
        return -1;

    char buffer[4096];

    size_t remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk =
            remaining >
            sizeof(buffer)
                ? sizeof(buffer)
                : remaining;

        if (recv_exact(
                fd,
                buffer,
                chunk) < 0)
        {
            fclose(fp);
            return -1;
        }

        if (fwrite(
                buffer,
                1,
                chunk,
                fp) != chunk)
        {
            fclose(fp);
            return -1;
        }

        remaining -= chunk;
    }

    fclose(fp);

    return 0;
}


/* --------------------------------------------------------- */
/* FILE FORWARDING                                           */
/* --------------------------------------------------------- */

static int send_file_to_client(
    int client_index,
    const char *sender,
    const char *filename,
    const char *path,
    size_t filesize)
{
    FILE *fp =
        fopen(path,
              "rb");

    if (fp == NULL)
        return -1;

    char header[512];

    snprintf(
        header,
        sizeof(header),
        "MSG FILE %s %s %zu\n",
        sender,
        filename,
        filesize);

    pthread_mutex_lock(
        &send_mutex[
            client_index]);

    if (send_all_raw(
            clients[
                client_index]
                .socket_fd,
            header,
            strlen(header)) < 0)
    {
        pthread_mutex_unlock(
            &send_mutex[
                client_index]);

        fclose(fp);

        return -1;
    }

    char buffer[4096];

    size_t remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk =
            remaining >
            sizeof(buffer)
                ? sizeof(buffer)
                : remaining;

        size_t nread =
            fread(
                buffer,
                1,
                chunk,
                fp);

        if (nread == 0)
        {
            pthread_mutex_unlock(
                &send_mutex[
                    client_index]);

            fclose(fp);

            return -1;
        }

        if (send_all_raw(
                clients[
                    client_index]
                    .socket_fd,
                buffer,
                nread) < 0)
        {
            pthread_mutex_unlock(
                &send_mutex[
                    client_index]);

            fclose(fp);

            return -1;
        }

        remaining -= nread;
    }

    pthread_mutex_unlock(
        &send_mutex[
            client_index]);

    fclose(fp);

    return 0;
}


static int send_file_target(
    int sender_index,
    const char *target,
    const char *filename,
    const char *path,
    size_t filesize)
{
    int user_index =
        find_user_index(
            target);

    if (user_index >= 0)
    {
        send_file_to_client(
            user_index,
            clients[
                sender_index]
                .username,
            filename,
            path,
            filesize);

        return 1;
    }

    int targets[MAX_CLIENTS];
    int count = 0;

    pthread_mutex_lock(
        &state_mutex);

    int room_index =
        find_room_unlocked(
            target);

    if (room_index == -1)
    {
        pthread_mutex_unlock(
            &state_mutex);

        return -1;
    }

    if (!rooms[
             room_index]
             .members[
                 sender_index])
    {
        pthread_mutex_unlock(
            &state_mutex);

        return 0;
    }

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        if (i != sender_index &&
            rooms[
                room_index]
                .members[i] &&
            clients[i].active &&
            clients[i].registered)
        {
            targets[count++] =
                i;
        }
    }

    pthread_mutex_unlock(
        &state_mutex);

    for (int i = 0;
         i < count;
         i++)
    {
        send_file_to_client(
            targets[i],
            clients[
                sender_index]
                .username,
            filename,
            path,
            filesize);
    }

    return 1;
}


/* --------------------------------------------------------- */
/* REMOVE CLIENT                                             */
/* --------------------------------------------------------- */

static void remove_client(int index)
{
    pthread_mutex_lock(
        &state_mutex);

    for (int i = 0;
         i < MAX_ROOMS;
         i++)
    {
        if (rooms[i].active)
        {
            rooms[i]
                .members[index] = 0;
        }
    }

    clients[index]
        .socket_fd = -1;

    clients[index]
        .active = 0;

    clients[index]
        .registered = 0;

    clients[index]
        .username[0] = '\0';

    clients[index]
        .rate_window_start = 0;

    clients[index]
        .command_count = 0;

    pthread_mutex_unlock(
        &state_mutex);
}


/* --------------------------------------------------------- */
/* CLIENT THREAD                                             */
/* --------------------------------------------------------- */

static void *handle_client(void *arg)
{
    int index =
        *((int *)arg);

    free(arg);

    int fd =
        clients[index]
            .socket_fd;

    char buffer[
        BUFFER_SIZE];

    while (1)
    {
        ssize_t n =
            recv_line(
                fd,
                buffer,
                sizeof(buffer));

        if (n <= 0)
        {
            if (clients[index]
                    .registered)
            {
                char old_username[
                    USERNAME_SIZE];

                strncpy(
                    old_username,
                    clients[index]
                        .username,
                    sizeof(
                        old_username) - 1);

                old_username[
                    sizeof(
                        old_username) - 1] =
                    '\0';

                printf(
                    "User %s disconnected unexpectedly.\n",
                    old_username);

                char log_message[128];

                snprintf(
                    log_message,
                    sizeof(
                        log_message),
                    "Unexpected disconnect: %s",
                    old_username);

                log_event(
                    log_message);

                presence_message(
                    index,
                    old_username,
                    "left");
            }

            break;
        }

        printf(
            "Client %d received: %s\n",
            index,
            buffer);


        /*
         * SENDFILE is excluded from rate limiting
         * because raw bytes immediately follow its
         * command line. Rejecting the command before
         * consuming those bytes would break framing.
         */
        if (strncmp(
                buffer,
                "SENDFILE ",
                9) != 0)
        {
            if (!check_rate_limit(
                    index))
            {
                send_text_index(
                    index,
                    "ERR 010 RATE_LIMITED NID:7566\n");

                char log_message[128];

                snprintf(
                    log_message,
                    sizeof(
                        log_message),
                    "Rate limit triggered for slot %d",
                    index);

                log_event(
                    log_message);

                continue;
            }
        }


        /* REGISTER */

        if (!clients[index]
                 .registered)
        {
            char username[
                USERNAME_SIZE];

            if (sscanf(
                    buffer,
                    "REGISTER %49s",
                    username) == 1)
            {
                if (username_exists(
                        username))
                {
                    send_text_index(
                        index,
                        "ERR 001 USERNAME_TAKEN NID:7566\n");

                    continue;
                }

                pthread_mutex_lock(
                    &state_mutex);

                clients[index]
                    .registered = 1;

                strncpy(
                    clients[index]
                        .username,
                    username,
                    USERNAME_SIZE - 1);

                clients[index]
                    .username[
                        USERNAME_SIZE - 1] =
                    '\0';

                pthread_mutex_unlock(
                    &state_mutex);

                char response[128];

                snprintf(
                    response,
                    sizeof(response),
                    "OK REGISTERED %s %s\n",
                    username,
                    NID_TAG);

                send_text_index(
                    index,
                    response);

                char log_message[128];

                snprintf(
                    log_message,
                    sizeof(
                        log_message),
                    "Registered: %s",
                    username);

                log_event(
                    log_message);

                presence_message(
                    index,
                    username,
                    "joined");
            }
            else
            {
                send_text_index(
                    index,
                    "ERR 005 REGISTER_REQUIRED NID:7566\n");
            }

            continue;
        }


        /* LIST */

        if (strcmp(
                buffer,
                "LIST") == 0)
        {
            char list[
                BUFFER_SIZE];

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

            send_text_index(
                index,
                response);
        }


        /* BCAST */

        else if (strncmp(
                     buffer,
                     "BCAST ",
                     6) == 0)
        {
            const char *message =
                buffer + 6;

            if (*message == '\0')
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");
            }
            else
            {
                broadcast_message(
                    index,
                    message);

                send_text_index(
                    index,
                    "OK SENT NID:7566\n");

                char log_message[256];

                snprintf(
                    log_message,
                    sizeof(
                        log_message),
                    "BCAST from %s",
                    clients[index]
                        .username);

                log_event(
                    log_message);
            }
        }


        /* PMSG */

        else if (strncmp(
                     buffer,
                     "PMSG ",
                     5) == 0)
        {
            char target[
                USERNAME_SIZE];

            char message[
                BUFFER_SIZE];

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
                    send_text_index(
                        index,
                        "OK SENT NID:7566\n");
                }
                else
                {
                    send_text_index(
                        index,
                        "ERR 002 USER_NOT_FOUND NID:7566\n");
                }
            }
            else
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");
            }
        }


        /* JOIN */

        else if (strncmp(
                     buffer,
                     "JOIN ",
                     5) == 0)
        {
            char room_name[
                ROOM_NAME_SIZE];

            if (sscanf(
                    buffer,
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

                    send_text_index(
                        index,
                        response);
                }
                else
                {
                    send_text_index(
                        index,
                        "ERR 008 ROOM_LIMIT_REACHED NID:7566\n");
                }
            }
            else
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");
            }
        }


        /* LEAVE */

        else if (strncmp(
                     buffer,
                     "LEAVE ",
                     6) == 0)
        {
            char room_name[
                ROOM_NAME_SIZE];

            if (sscanf(
                    buffer,
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

                    send_text_index(
                        index,
                        response);
                }
                else if (result == -1)
                {
                    send_text_index(
                        index,
                        "ERR 003 ROOM_NOT_FOUND NID:7566\n");
                }
                else
                {
                    send_text_index(
                        index,
                        "ERR 007 NOT_IN_ROOM NID:7566\n");
                }
            }
            else
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");
            }
        }


        /* ROOMS */

        else if (strcmp(
                     buffer,
                     "ROOMS") == 0)
        {
            char list[
                BUFFER_SIZE];

            char response[
                BUFFER_SIZE + 64];

            build_room_list(
                list,
                sizeof(list));

            snprintf(
                response,
                sizeof(response),
                "OK ROOMS %s %s\n",
                list,
                NID_TAG);

            send_text_index(
                index,
                response);
        }


        /* RMSG */

        else if (strncmp(
                     buffer,
                     "RMSG ",
                     5) == 0)
        {
            char room_name[
                ROOM_NAME_SIZE];

            char message[
                BUFFER_SIZE];

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
                    send_text_index(
                        index,
                        "OK SENT NID:7566\n");
                }
                else if (result == -1)
                {
                    send_text_index(
                        index,
                        "ERR 003 ROOM_NOT_FOUND NID:7566\n");
                }
                else
                {
                    send_text_index(
                        index,
                        "ERR 007 NOT_IN_ROOM NID:7566\n");
                }
            }
            else
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");
            }
        }


        /* SENDFILE */

        else if (strncmp(
                     buffer,
                     "SENDFILE ",
                     9) == 0)
        {
            char target[
                USERNAME_SIZE];

            char filename[256];

            unsigned long
                filesize_ul;

            if (sscanf(
                    buffer,
                    "SENDFILE %49s %255s %lu",
                    target,
                    filename,
                    &filesize_ul) != 3)
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_COMMAND NID:7566\n");

                continue;
            }

            if (!valid_filename(
                    filename))
            {
                send_text_index(
                    index,
                    "ERR 005 INVALID_FILENAME NID:7566\n");

                continue;
            }

            if (filesize_ul >
                MAX_FILE_SIZE)
            {
                send_text_index(
                    index,
                    "ERR 004 FILE_TOO_LARGE NID:7566\n");

                break;
            }

            char path[512];

            if (store_received_file(
                    fd,
                    clients[index]
                        .username,
                    filename,
                    (size_t)
                        filesize_ul,
                    path,
                    sizeof(path)) < 0)
            {
                send_text_index(
                    index,
                    "ERR 009 FILE_TRANSFER_FAILED NID:7566\n");

                break;
            }

            int result =
                send_file_target(
                    index,
                    target,
                    filename,
                    path,
                    (size_t)
                        filesize_ul);

            if (result == -1)
            {
                send_text_index(
                    index,
                    "ERR 002 USER_NOT_FOUND NID:7566\n");
            }
            else if (result == 0)
            {
                send_text_index(
                    index,
                    "ERR 007 NOT_IN_ROOM NID:7566\n");
            }
            else
            {
                char response[512];

                snprintf(
                    response,
                    sizeof(response),
                    "OK FILE_RECEIVED %s %s\n",
                    filename,
                    NID_TAG);

                send_text_index(
                    index,
                    response);

                char log_message[512];

                snprintf(
                    log_message,
                    sizeof(
                        log_message),
                    "FILE from %s: %s (%lu bytes)",
                    clients[index]
                        .username,
                    filename,
                    filesize_ul);

                log_event(
                    log_message);

                printf(
                    "Stored file: %s\n",
                    path);
            }
        }


        /* QUIT */

        else if (strcmp(
                     buffer,
                     "QUIT") == 0)
        {
            char old_username[
                USERNAME_SIZE];

            strncpy(
                old_username,
                clients[index]
                    .username,
                sizeof(
                    old_username) - 1);

            old_username[
                sizeof(
                    old_username) - 1] =
                '\0';

            send_text_index(
                index,
                "OK BYE NID:7566\n");

            char log_message[128];

            snprintf(
                log_message,
                sizeof(
                    log_message),
                "Graceful disconnect: %s",
                old_username);

            log_event(
                log_message);

            presence_message(
                index,
                old_username,
                "left");

            break;
        }


        /* INVALID */

        else
        {
            send_text_index(
                index,
                "ERR 005 INVALID_COMMAND NID:7566\n");
        }
    }

    close(fd);

    remove_client(
        index);

    return NULL;
}


/* --------------------------------------------------------- */
/* MAIN                                                      */
/* --------------------------------------------------------- */

int main(void)
{
    int server_fd;
    int opt = 1;

    struct sockaddr_in
        server_addr;

    memset(
        clients,
        0,
        sizeof(clients));

    memset(
        rooms,
        0,
        sizeof(rooms));

    for (int i = 0;
         i < MAX_CLIENTS;
         i++)
    {
        clients[i]
            .socket_fd = -1;

        clients[i]
            .rate_window_start = 0;

        clients[i]
            .command_count = 0;

        pthread_mutex_init(
            &send_mutex[i],
            NULL);
    }

    server_fd =
        socket(
            AF_INET,
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
        perror(
            "setsockopt");

        close(
            server_fd);

        return EXIT_FAILURE;
    }

    memset(
        &server_addr,
        0,
        sizeof(
            server_addr));

    server_addr
        .sin_family =
        AF_INET;

    server_addr
        .sin_addr
        .s_addr =
        INADDR_ANY;

    server_addr
        .sin_port =
        htons(PORT);

    if (bind(
            server_fd,
            (struct sockaddr *)
                &server_addr,
            sizeof(
                server_addr)) < 0)
    {
        perror("bind");

        close(
            server_fd);

        return EXIT_FAILURE;
    }

    if (listen(
            server_fd,
            BACKLOG) < 0)
    {
        perror("listen");

        close(
            server_fd);

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
        "Rate limit: %d commands per %d seconds\n",
        RATE_LIMIT_COMMANDS,
        RATE_LIMIT_WINDOW);

    log_event(
        "Server started");


    while (1)
    {
        struct sockaddr_in
            client_addr;

        socklen_t client_len =
            sizeof(
                client_addr);

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
            if (!clients[i]
                     .active)
            {
                index = i;

                clients[i]
                    .socket_fd =
                    client_fd;

                clients[i]
                    .active = 1;

                clients[i]
                    .registered = 0;

                clients[i]
                    .username[0] =
                    '\0';

                clients[i]
                    .rate_window_start =
                    0;

                clients[i]
                    .command_count =
                    0;

                break;
            }
        }

        pthread_mutex_unlock(
            &state_mutex);

        if (index == -1)
        {
            const char *message =
                "ERR 006 SERVER_FULL NID:7566\n";

            send_all_raw(
                client_fd,
                message,
                strlen(message));

            close(
                client_fd);

            continue;
        }

        printf(
            "Client connected from %s:%d (slot %d)\n",
            inet_ntoa(
                client_addr
                    .sin_addr),
            ntohs(
                client_addr
                    .sin_port),
            index);

        int *thread_index =
            malloc(
                sizeof(int));

        if (thread_index ==
            NULL)
        {
            close(
                client_fd);

            remove_client(
                index);

            continue;
        }

        *thread_index =
            index;

        pthread_t thread_id;

        if (pthread_create(
                &thread_id,
                NULL,
                handle_client,
                thread_index) != 0)
        {
            free(
                thread_index);

            close(
                client_fd);

            remove_client(
                index);

            continue;
        }

        pthread_detach(
            thread_id);
    }

    close(server_fd);

    return EXIT_SUCCESS;
}
