#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 12632

int main(void)
{
	int sockfd;

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

	printf("Connecting to NetMessenger server...\n");

	if (connect(sockfd,
		(struct sockaddr *)&server_addr,
		sizeof(server_addr)) < 0)
	{
		perror("connect");
		close(sockfd);
		return EXIT_FAILURE;
	}

	printf("Connected successfully to %s%d\n",
		SERVER_IP,
		PORT);

	close(sockfd);

	return EXIT_SUCCESS;
}
