#include <stdio.h>

#include "../../src/com.h"

int main(int argc, char* argv[])
{
    /* first create the socket */
    int server_socket = create_socket();

    /* struct containing details about port and IP address */
    struct sockaddr_in addr = create_socket_addr(9002, INADDR_ANY);

    /* bind the socket to the struct containing info
       about the port and IP address */
    bind_socket(server_socket, &addr);

    /* listen for a maximum of 3 clients on the specified socket,
       (clients are backlogged to a queue of size 3) */
    listen_for_connections(server_socket, 3);

    int client_socket;
    int val = 0;

    /* 'accept_connection' function will write to these if specified */
    sockaddr_in_t client_addr;
    int addr_length;

    /* server accepts client that is in front of the internal queue */
    while((client_socket = accept_connection(server_socket, &client_addr, &addr_length)) != -1)
    {
        unsigned short int sin_family = client_addr.sin_family;
        uint16_t port = client_addr.sin_port;
        uint32_t s_addr = client_addr.sin_addr.s_addr;

        /* Printing out information about the client */
        printf("sin_family: %d\n", sin_family);
        printf("port: %d\n", port);
        printf("s_addr: %d\n", s_addr);
        printf("addr_length: %d\n\n", addr_length);

        send_msg(client_socket, &val, sizeof(val));
        val++;
    }

    /* free all resources associated with the socket */
    close_socket(server_socket);

    return 0;
}