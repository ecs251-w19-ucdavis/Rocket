#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#include "page.h"
#include "rocket_client.h"

#define BASE_BUFFER_SIZE 1024

/* The shared memory that each client will have */
unsigned char* shared_mem;
int master_socket; //Initialize this in init Lesley/Shivang
int slave_socket; //Initialize this in init Lesley/Shivang

int client_num   = -1;

/* gets initialized in the init function,
   address_size corresponds to size of the client's portion of
   the shared memory and not the total shared memory
*/
int address_size = 0;  

pthread_mutex_t *lock;

// For now setting it to be the start of user space (0x40000000). This is to be used as input for mmap among other things
void* get_base_address()
{ 
    return (void*)(1<<30);
}


/* retrieves the starting address of the memory region corresponding 
   to the particular client
*/
void* get_respective_client_base_address()
{
    int address_offset = (client_num * address_size);
    return get_base_address() + address_offset;
}


/* Returns 1 if address is out of bounds for the respective client
   and 0 if it is within bounds */
int is_out_of_bounds(char* address)
{
    if(address == NULL)
    {
        printf("Address is NULL\n");
        return 1;
    }

    if(address < get_respective_client_base_address() || address > get_respective_client_base_address() + address_size)
    {
        printf("Address %x is out of bounds for client %d\n", address, client_num);
        return 1;
    }

    printf("Address %x within the bounds for client %d\n", address, client_num);
    return 0;
}



//Defined this way because it is going to be running as a thread independently where void* param will be the accepted socket file descriptor
void* independent_listener (void* param) 
{
  int acc_sock = *((int *)param);
  char buf[BASE_BUFFER_SIZE];

  for(; ;)
    {
      int val = recv_msg(acc_sock, buf, BASE_BUFFER_SIZE);
      buf[val] = '\0';
      char* temp_str;
      int page_number = (int)strtol(buf, &temp_str, 10);
      pthread_mutex_lock(&lock[page_number]);
      void* page_addr = get_base_address() + (page_number*PAGE_SIZE);
      mprotect(page_addr, PAGE_SIZE, PROT_READ);
      if(send_msg(acc_sock, page_addr, PAGE_SIZE) != 0)
	{
	  printf("Could not send page requested!\n");
	  exit(1);
	}
      mprotect(page_addr, PAGE_SIZE, PROT_NONE);
      pthread_mutex_unlock(&lock[page_number]);

    }
  
}

// Programmed this like on the sigaction manpage: http://man7.org/linux/man-pages/man2/sigaction.2.html
// TODO: locking, buffering, socket communication, copying incoming page, setting protection for page using mprotect, etc.
void sigfault_handler(int sig, siginfo_t *info, void *ucontext)
{
    char *curr_addr = info->si_addr;
    char *base_addr;
    void *temp = get_base_address();
    base_addr = (char *)temp;
    int page_number = ((int)(curr_addr - base_addr)) / PAGE_SIZE;
    
    pthread_mutex_lock(&lock[page_number]);
    char buf[BASE_BUFFER_SIZE];
    snprintf(buf, BASE_BUFFER_SIZE, "%d", page_number);
    
    if(send_msg(master_socket, buf, strlen(buf)) != 0)
      {
	printf("Could not send message for page request!\n");
	exit(1);
      }

    char data[PAGE_SIZE];

    for(int total = PAGE_SIZE, index = 0; total != 0; )
      {
	int val = recv_msg(master_socket, &data[index], total);
	total = total - val;
	index = index + val;
      }

    //void* page_addr = get_base_address() + (page_number*PAGE_SIZE);

    mprotect(curr_addr, PAGE_SIZE, PROT_WRITE);
    memcpy(curr_addr, data, PAGE_SIZE);
    pthread_mutex_unlock(&lock[page_number]);
    
    
}


int rocket_client_init(int addr_size)
{
    address_size = addr_size;

    // Using mmap for mapping the addresses-- private copy-on-write mapping
    shared_mem = mmap(get_respective_client_base_address(), addr_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);


    // Setting up the signal catching code
    struct sigaction sa;
    sa.sa_sigaction = sigfault_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    // TODO: Assigning default pages to master and slave nodes to start with (need to demarcate them somehow), socket code goes here for all communication, thread running function that responds to page requests on both master and slave nodes runs here too.


    /*
        Steps:
        1) Retrieve client number from server
        2) Send server an acknowledgement

    */
    // for establishing the communication with the server.
    socket_t master_socket = create_socket();    
    
    // Todo: change master IP address
    char* MASTER_IP = INADDR_ANY;
    sockaddr_in_t addr = create_socket_addr(9002, MASTER_IP);

    /* Attempting to establish a connection on the socket */
    int connection_status = connect_socket(network_socket, &addr);

    if(connection_status == -1)
    {
        printf("Could not connect!\n");
        exit(1);
    }

    // Retrieve client number from server
    recv_msg(network_socket, (char*) &client_num, sizeof(client_number));
    printf("client number: %d\n", client_num);
   
    // Send server an acknowledgement
    int SUCCESS = 1;
    send_msg(network_socket, (char*) &SUCCESS, sizeof(SUCCESS)); 


    //To call indpendent listener function at the end, it will go something like this:
    //pthread_t thread
    //indp_lisn = pthread_create(&thread, NULL, independent_listener, (void*) fd returned from accept syscall);

    
    return 0;
}


int rocket_client_exit()
{
    close_socket(master_socket);
    return 0;
}


/*  Client gets ownership of the page
    Return: 0 for success, -1 for failure */
int own_page()
{
    return 0;
}


/*  Client gives up ownership of the page
    Return: 0 for success, -1 for failure */
int relinquish_page()
{
    return 0;
}


struct Page* rocket_access_page(int page_num)
{
    return NULL;
}


void* rocket_alloc(int num_bytes)
{
    return 0;
}

int rocket_dealloc(void* address)
{
    return 0;
}


char* client_communicate(socket_t network_socket)
{ 
    // Todo: change master IP address
    char* MASTER_IP = INADDR_ANY;
    sockaddr_in_t addr = create_socket_addr(9002, MASTER_IP);

    /* attempting to establish a connection on the socket */
    int connection_status = connect_socket(network_socket, &addr);

    if(connection_status == -1)
    {
        printf("Could not connect!\n");
        exit(1);
    }

    //struct Page* page_request = NULL;
    int client_number = -1;

    /* read sizeof(client_number) number of bytes and put bytes into the variable 'client_number' */
    recv_msg(network_socket, (char*) &client_number, sizeof(client_number));
    
    printf("client number: %d\n", client_number);
    

    return &client_number;  
}
