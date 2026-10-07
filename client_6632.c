#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define PORT 12632
#define BUFFER_SIZE 1024
#define MAX_FILE_SIZE (5 * 1024 * 1024)

static int sockfd;


/* --------------------------------------------------------- */
/* TCP HELPERS                                               */
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
            buffer[used++] = c;
    }

    buffer[used] = '\0';

    return (ssize_t)used;
}


static int recv_exact(int fd,
                      void *buffer,
                      size_t length)
{
    size_t total = 0;

    char *ptr = buffer;

    while (total < length)
    {
        ssize_t n =
            recv(fd,
                 ptr + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total += (size_t)n;
    }

    return 0;
}


static int send_all_raw(int fd,
                        const void *data,
                        size_t length)
{
    size_t total = 0;

    const char *ptr = data;

    while (total < length)
    {
        ssize_t n =
            send(fd,
                 ptr + total,
                 length - total,
                 0);

        if (n <= 0)
            return -1;

        total += (size_t)n;
    }

    return 0;
}


/* --------------------------------------------------------- */
/* LOCAL FILE HELPERS                                        */
/* --------------------------------------------------------- */

static int create_directory(const char *path)
{
    if (mkdir(path, 0755) == 0)
        return 0;

    if (errno == EEXIST)
        return 0;

    return -1;
}


static int get_file_size(const char *filename,
                         size_t *size)
{
    FILE *fp =
        fopen(filename, "rb");

    if (fp == NULL)
        return -1;

    if (fseek(fp,
              0,
              SEEK_END) != 0)
    {
        fclose(fp);
        return -1;
    }

    long result =
        ftell(fp);

    fclose(fp);

    if (result < 0)
        return -1;

    *size =
        (size_t)result;

    return 0;
}


static int send_file_bytes(int fd,
                           const char *filename,
                           size_t filesize)
{
    FILE *fp =
        fopen(filename, "rb");

    if (fp == NULL)
        return -1;

    char buffer[4096];

    size_t remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk =
            remaining > sizeof(buffer)
                ? sizeof(buffer)
                : remaining;

        size_t nread =
            fread(buffer,
                  1,
                  chunk,
                  fp);

        if (nread == 0)
        {
            fclose(fp);
            return -1;
        }

        if (send_all_raw(
                fd,
                buffer,
                nread) < 0)
        {
            fclose(fp);
            return -1;
        }

        remaining -= nread;
    }

    fclose(fp);

    return 0;
}


/* --------------------------------------------------------- */
/* RECEIVE FORWARDED FILE                                    */
/* --------------------------------------------------------- */

static int receive_file(const char *sender,
                        const char *filename,
                        size_t filesize)
{
    if (create_directory(
            "./received_files") < 0)
    {
        return -1;
    }

    char path[512];

    snprintf(path,
             sizeof(path),
             "./received_files/%s_%s",
             sender,
             filename);

    FILE *fp =
        fopen(path, "wb");

    if (fp == NULL)
        return -1;

    char buffer[4096];

    size_t remaining =
        filesize;

    while (remaining > 0)
    {
        size_t chunk =
            remaining > sizeof(buffer)
                ? sizeof(buffer)
                : remaining;

        if (recv_exact(
                sockfd,
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

    printf(
        "\nFile received from %s: %s (%zu bytes)\n",
        sender,
        path,
        filesize);

    return 0;
}


/* --------------------------------------------------------- */
/* RECEIVER THREAD                                           */
/* --------------------------------------------------------- */

static void *receiver_thread(void *arg)
{
    (void)arg;

    char line[BUFFER_SIZE + 256];

    while (1)
    {
        ssize_t n =
            recv_line(
                sockfd,
                line,
                sizeof(line));

        if (n <= 0)
            break;

        if (strncmp(
                line,
                "MSG FILE ",
                9) == 0)
        {
            char sender[50];
            char filename[256];

            unsigned long size_ul;

            if (sscanf(
                    line,
                    "MSG FILE %49s %255s %lu",
                    sender,
                    filename,
                    &size_ul) == 3)
            {
                if (receive_file(
                        sender,
                        filename,
                        (size_t)size_ul) < 0)
                {
                    printf(
                        "\nFailed to receive file.\n");
                }
            }
        }
        else
        {
            printf("\n%s\n",
                   line);
        }

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}


/* --------------------------------------------------------- */
/* MAIN                                                      */
/* --------------------------------------------------------- */

int main(void)
{
    struct sockaddr_in server_addr;

    sockfd =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(
            sockfd,
            (struct sockaddr *)
                &server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf(
        "Connected to NetMessenger server %s:%d\n",
        SERVER_IP,
        PORT);

    pthread_t receiver;

    if (pthread_create(
            &receiver,
            NULL,
            receiver_thread,
            NULL) != 0)
    {
        perror("pthread_create");

        close(sockfd);

        return EXIT_FAILURE;
    }

    char input[BUFFER_SIZE + 256];

    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(
                input,
                sizeof(input),
                stdin) == NULL)
        {
            break;
        }

        if (strncmp(
                input,
                "SENDFILE ",
                9) == 0)
        {
            char target[50];
            char filename[256];

            unsigned long typed_size;

            if (sscanf(
                    input,
                    "SENDFILE %49s %255s %lu",
                    target,
                    filename,
                    &typed_size) != 3)
            {
                printf(
                    "Usage: SENDFILE <target> "
                    "<filename> <filesize>\n");

                continue;
            }

            size_t actual_size;

            if (get_file_size(
                    filename,
                    &actual_size) < 0)
            {
                printf(
                    "Cannot open file: %s\n",
                    filename);

                continue;
            }

            if (actual_size >
                MAX_FILE_SIZE)
            {
                printf(
                    "File is larger than 5 MB.\n");

                continue;
            }

            if (actual_size !=
                (size_t)typed_size)
            {
                printf(
                    "Incorrect filesize. "
                    "Actual size is %zu bytes.\n",
                    actual_size);

                continue;
            }

            if (send_all_raw(
                    sockfd,
                    input,
                    strlen(input)) < 0)
            {
                break;
            }

            if (send_file_bytes(
                    sockfd,
                    filename,
                    actual_size) < 0)
            {
                printf(
                    "Failed to send file.\n");

                break;
            }

            continue;
        }

        if (send_all_raw(
                sockfd,
                input,
                strlen(input)) < 0)
        {
            break;
        }

        if (strncmp(
                input,
                "QUIT",
                4) == 0)
        {
            sleep(1);
            break;
        }
    }

    shutdown(sockfd,
             SHUT_WR);

    pthread_join(receiver,
                 NULL);

    close(sockfd);

    return EXIT_SUCCESS;
}
