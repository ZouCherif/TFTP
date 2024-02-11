#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>



int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s put filename 127.0.0.1 69\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *operation = argv[1];
    const char *filename = argv[2];
    const char *server_ip = argv[3];
    const int server_port = atoi(argv[4]);

    int server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    server_addr.sin_port = htons(server_port);

    if (strcmp(operation, "put") == 0){
        handle_wrq(server_socket, server_addr, filename);
    }
    else if (strcmp(operation, "get") == 0){
        handle_rrq(server_socket, server_addr, filename);
    }
    else
    {
        fprintf(stderr, "Unsupported operation\n");
        exit(EXIT_FAILURE);
    }

    close(server_socket);
    return 0;
}