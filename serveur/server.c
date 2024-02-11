#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

#define SERVER_PORT 69
#define MAX_PACKET_SIZE 516
#define TIMEOUT_SECONDS 5

#define RRQ_OPCODE 1
#define WRQ_OPCODE 2
#define DATA_OPCODE 3
#define ACK_OPCODE 4
#define ERROR_OPCODE 5


int main()
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket < 0)
    {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error binding server socket");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1)
    {
        char request_packet[MAX_PACKET_SIZE];
        ssize_t bytes_received = recvfrom(server_socket, request_packet, MAX_PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received < 0)
        {
            perror("Error receiving request packet");
            continue;
        }

        unsigned short opcode;
        memcpy(&opcode, request_packet, sizeof(opcode));
        opcode = ntohs(opcode);

        handle_request(server_socket, client_addr, request_packet + 2, opcode);
    }

    close(server_socket);
    return 0;
}