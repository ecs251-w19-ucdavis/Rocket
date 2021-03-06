#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "rocket_core.h"
#include "page.h"
#include "com.h"

#define BASE_BUFFER_SIZE 1024

int num_connected_clients = 0;
socket_t master_socket = -1;
socket_t sig_socket = -1;

int address_size = -1;

ClientInfo *clientInfos = NULL;
PageOwnership *pageOwnerships = NULL;

pthread_t *threads = NULL;
pthread_mutex_t server_socket_lock;

void parse_buf(char *buf, int buf_length, int *client_num, int *operation, int *page_num)
{
    char *ptr = strtok(buf, ",");
    *client_num = (int)strtol(ptr, NULL, 10);
    ptr = strtok(NULL, ",");
    *operation = (int)strtol(ptr, NULL, 10);
    ptr = strtok(NULL, ",");
    *page_num = (int)strtol(ptr, NULL, 10);
}

void *independent_listener_server(void *param)
{
    int acc_sock = *((int *)param);
    printf("Listening to accepted socket: %d\n", acc_sock);
    char buf[BASE_BUFFER_SIZE];
    sleep(5);

    while (1)
    {

        //RECEIVING PAGE NUMBER FROM CLIENT 1
        int val = recv_msg(acc_sock, buf, BASE_BUFFER_SIZE);
        if (val == -1)
        {
            continue;
        }
        buf[val] = '\0';

        pthread_mutex_lock(&server_socket_lock);

        int client_number = -1, operation = -1, page_number = -1;
        char copy_buf[BASE_BUFFER_SIZE];
        strcpy(copy_buf, buf);
        parse_buf(copy_buf, strlen(copy_buf), &client_number, &operation, &page_number);

        if (operation == WRITING)
        {

            int target_client_sock = pageOwnerships[page_number].clientExclusiveWriter.client_socket;

            if (target_client_sock == -1)
            {
                int index = pageOwnerships[page_number].clientReaders.num_readers - 1;
                target_client_sock = pageOwnerships[page_number].clientReaders.readers[index].client_socket;
            }

            //SENDING PAGE REQUEST TO CLIENT 2
            if (send_msg(target_client_sock, &page_number, sizeof(int)) <= 0)
            {
                printf("Could not send message for page request!\n");
                exit(1);
            }
            printf("[INFO] Page request sent to client\n");
            char data[PAGE_SIZE];

            //RECEIVING PAGE FROM CLIENT 2
            for (int total = PAGE_SIZE, index = 0; total != 0;)
            {
                int val = recv_msg(target_client_sock, &data[index], total);
                total = total - val;
                index = index + val;
            }
            printf("[INFO] Page requested successfully from client\n");
            //UPDATE PAGE OWNERSHIPS!!! WE ARE ONLY CHANGING SOCKET. NOTHING ELSE.

            pageOwnerships[page_number].clientExclusiveWriter.client_socket = clientInfos[client_number].client_socket;
            pageOwnerships[page_number].clientReaders.num_readers = 0;

            printf("[INFO] Updated page ownership\n");
            //SENDING PAGE FROM MASTER TO CLIENT 1
            printf("[INFO] Attempting to send page from master to client...\n");

            send_msg(acc_sock, &data, PAGE_SIZE);

            pthread_mutex_unlock(&server_socket_lock);
        }

        else if (operation == READING)
        {
            int target_client_sock = pageOwnerships[page_number].clientExclusiveWriter.client_socket;

            // if there is no exclusive writer
            if (target_client_sock == -1)
            {
                int index = pageOwnerships[page_number].clientReaders.num_readers - 1;
                target_client_sock = pageOwnerships[page_number].clientReaders.readers[index].client_socket;
            }

            //SENDING PAGE REQUEST TO CLIENT 2
            if (send_msg(target_client_sock, &page_number, sizeof(int)) <= 0)
            {
                printf("Could not send message for page request!\n");
                exit(1);
            }
            printf("[INFO] Page request sent to client\n");
            char data[PAGE_SIZE];

            //RECEIVING PAGE FROM CLIENT 2
            for (int total = PAGE_SIZE, index = 0; total != 0;)
            {
                int val = recv_msg(target_client_sock, &data[index], total);
                total = total - val;
                index = index + val;
            }
            printf("[INFO] Page requested successfully from client\n");

            //UPDATE PAGE OWNERSHIPS!!!
            int index = pageOwnerships[page_number].clientReaders.num_readers;
            pageOwnerships[page_number].clientReaders.readers[index].client_socket = clientInfos[client_number].client_socket;
            pageOwnerships[page_number].clientReaders.num_readers++;

            pageOwnerships[page_number].clientExclusiveWriter.client_socket = -1;
            pageOwnerships[page_number].clientExclusiveWriter.sig_socket = -1;

            printf("[INFO] Updated page ownership\n");
            //SENDING PAGE FROM MASTER TO CLIENT 1
            printf("[INFO] Attempting to send page from master to client...\n");

            if (send_msg(acc_sock, &data, PAGE_SIZE) <= 0)
            {
                printf("Failed to send page data\n");
            }

            pthread_mutex_unlock(&server_socket_lock);
        }
    }
    return NULL;
}

void init_server_socket(int num_clients, int port, const char *IPV4_ADDR)
{
    master_socket = create_socket();
    sig_socket = create_socket();

    if (master_socket == -1 || sig_socket == -1)
    {
        printf("Failed to create server socket!\n");
        exit(1);
    }

    sockaddr_in_t addr = create_socket_addr(port, IPV4_ADDR);
    sockaddr_in_t sig_socket_addr = create_socket_addr(port - 5353, IPV4_ADDR);

    if (bind_socket(master_socket, &addr) == -1)
    {
        printf("Failed to bind server socket!\n");
        exit(1);
    }

    if (listen_for_connections(master_socket, num_clients) == -1)
    {
        printf("Failed to setup listening for connections!\n");
        exit(1);
    }

    if (bind_socket(sig_socket, &sig_socket_addr) == -1)
    {
        printf("Failed to bind server socket!\n");
        exit(1);
    }

    if (listen_for_connections(sig_socket, num_clients) == -1)
    {
        printf("Failed to setup listening for connections!\n");
        exit(1);
    }
}

void print_client_info(ClientInfo *clientInfo)
{
    printf("client_socket: %d\n", clientInfo->client_socket);
    printf("sig_socket: %d\n", clientInfo->sig_socket);
}

void setup_client_connections(int num_clients)
{
    clientInfos = (ClientInfo *)malloc(sizeof(ClientInfo) * num_clients);

    const char *SERVER_IP = INADDR_ANY;
    init_server_socket(num_clients, 9002, SERVER_IP);

    int client_num;

    printf("Setting up client sockets...\n");

    for (client_num = 0; client_num < num_clients; client_num++)
    {
        /* connect to a client */
        sockaddr_in_t client_addr;
        int addr_length;
        socket_t client_socket = accept_connection(master_socket, &client_addr, &addr_length);

        if (client_socket == -1)
        {
            printf("Server failed to connect to client %d!\n", client_num);
            exit(1);
        }

        clientInfos[client_num].client_socket = client_socket; // independent listener

        print_client_info(&clientInfos[client_num]);

        printf("Connected to client %d!\n", client_num);

        /* send the client num to the connected client */
        int num_bytes_sent = send_msg(client_socket, (void *)&client_num, sizeof(client_num));

        if (num_bytes_sent <= 0)
        {
            printf("Server failed to send client number %d to the respective client\n", client_num);
            exit(1);
        }

        /* receive an acknowledgement from the client */
        int received = 0;
        recv_msg(client_socket, (void *)&received, sizeof(int));

        if (!received)
        {
            printf("Server failed to receive an acknowledgement from client %d\n", client_num);
            exit(1);
        }

        printf("<client %d> - acknowledged: %d\n\n", client_num, received);

        num_connected_clients++;
    }

    int connect_to_all_clients_success = (num_connected_clients == num_clients) ? 1 : 0;

    if (connect_to_all_clients_success)
    {
        for (client_num = 0; client_num < num_clients; client_num++)
        {
            int num_bytes_sent = send_msg(clientInfos[client_num].client_socket, (void *)&connect_to_all_clients_success, sizeof(connect_to_all_clients_success));

            if (num_bytes_sent <= 0)
            {
                printf("Server failed to send client number %d to the respective client\n", client_num);
                exit(1);
            }
        }

        printf("Server successfully connected to all %d clients!\n", num_clients);
        sleep(5);
    }

    else
    {
        printf("Failed to connect to all %d clients!\n", num_clients);
        exit(1);
    }

    printf("Setting up signal sockets...\n");

    num_connected_clients = 0;

    for (client_num = 0; client_num < num_clients; client_num++)
    {
        /* connect to a client */
        sockaddr_in_t sig_socket_addr;
        int addr_length;
        socket_t sig_sock = accept_connection(sig_socket, &sig_socket_addr, &addr_length);

        if (sig_sock == -1)
        {
            printf("Server failed to connect to client %d!\n", client_num);
            exit(1);
        }

        clientInfos[client_num].sig_socket = sig_sock;

        print_client_info(&clientInfos[client_num]);

        printf("Connected to client %d!\n", client_num);

        /* send the client num to the connected client */
        int num_bytes_sent = send_msg(sig_sock, (void *)&client_num, sizeof(client_num));

        if (num_bytes_sent <= 0)
        {
            printf("Server failed to send client number %d to the respective client\n", client_num);
            exit(1);
        }

        /* receive an acknowledgement from the client */
        int received = 0;
        recv_msg(sig_sock, (void *)&received, sizeof(int));

        if (!received)
        {
            printf("Server failed to receive an acknowledgement from client %d\n", client_num);
            exit(1);
        }

        printf("<client %d> - acknowledged: %d\n\n", client_num, received);

        num_connected_clients++;
    }

    connect_to_all_clients_success = (num_connected_clients == num_clients) ? 1 : 0;

    if (connect_to_all_clients_success)
    {
        for (client_num = 0; client_num < num_clients; client_num++)
        {
            int num_bytes_sent = send_msg(clientInfos[client_num].sig_socket, (void *)&connect_to_all_clients_success, sizeof(connect_to_all_clients_success));

            if (num_bytes_sent <= 0)
            {
                printf("Server failed to send client number %d to the respective client\n", client_num);
                exit(1);
            }
        }

        printf("Server successfully connected to all %d clients!\n", num_clients);
        sleep(5);
    }

    else
    {
        printf("Failed to connect to all %d clients!\n", num_clients);
        exit(1);
    }

    int page_num;
    int num_pages_each_clients = (address_size / PAGE_SIZE) / num_clients;

    for (page_num = 0; page_num < address_size / PAGE_SIZE; page_num++)
    {
        int client_index = page_num / num_pages_each_clients;
        pageOwnerships[page_num].clientExclusiveWriter = clientInfos[client_index];
    }
}

void setup_independent_listeners()
{
    int i;
    threads = (pthread_t *)(malloc(sizeof(pthread_t) * num_connected_clients));
    for (i = 0; i < num_connected_clients; i++)
    {
        pthread_create(&threads[i], NULL, independent_listener_server, (void *)&clientInfos[i].sig_socket);
    }
}

int rocket_server_init(int addr_size, int num_clients)
{
    static int init = 0;

    pthread_mutex_init(&server_socket_lock, NULL);

    address_size = addr_size;
    if (!init)
    {
        init = 1;

        pageOwnerships = create_pageownerships(addr_size / PAGE_SIZE, num_clients);

        setup_client_connections(num_clients);

        setup_independent_listeners();
    }

    return 0;
}

int rocket_server_exit()
{
    close_socket(master_socket);
    close_socket(sig_socket);
    return 0;
}
