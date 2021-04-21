#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include "constants.h"

#define BUFSIZE 256
static char buf[BUFSIZE];
static int counter = 0;
static char *commands[] = {"0 %d %d %s", "1 %d %d %s", "2 %d %d", "3 %d %d"};

void vprintf_error_and_exit(char *form, ...)//prints like printf and exits
{
    va_list vl;
    va_start(vl, form);
    vfprintf(stderr, form, vl);
    va_end(vl);
    exit(1);
}

void print_error_and_exit(const char *str)//prints string and exits
{
    perror(str);
    exit(1);
}

void print_server_error_exit()
{
    fprintf(stderr, "Unexpected response from server\n");
    exit(1);
}

char str_match(char *one, char *two)//this function returns true if the strings are exactly the same, false otherwise
{
    return strlen(one) == strlen(two) && !strcmp(one, two);
}

int get_connection(char *hostname, int i) // this function gets a connection to a hostname
{
    memset(buf, 0, BUFSIZE);
    int socketfd;

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0)
        print_error_and_exit("Error creating socket");

    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(SERVERPORTS[i]);
    int addrlen = sizeof(serveraddr);

    struct addrinfo addri, *res;
    memset(&addri, 0, sizeof(addri));

    addri.ai_family = AF_INET;
    addri.ai_socktype = SOCK_STREAM;
    addri.ai_flags = AI_ADDRCONFIG;

    int retval = getaddrinfo(hostname, NULL, &addri, &res);
    if(retval < 0)
    	print_error_and_exit("Error resolving name");

    serveraddr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

    if(connect(socketfd, (struct sockaddr *)&serveraddr, addrlen) < 0)
    	vprintf_error_and_exit("Error connecting to host %s\n", hostname);

    return socketfd;
}

static int serverconns[3];
static int clientno;

int main(int argc, char *argv[])
{
    if(argc != 5)
    {
    	fprintf(stderr, "Please enter a client id and three hostnames as arguments\n");
		exit(1);//exit if the correct arguments were not supplied
    }

    clientno = atoi(argv[1]);//first argument is the client number
    srand(clientno);//seed rng with client number

    int i;
    for(i = 0; i < 3; i++)
    {
        serverconns[i] = get_connection(argv[i + 2], i);//get connection to each server
        if(serverconns[i] < 0)//error check
            vprintf_error_and_exit("Error getting connection for host %s\n", argv[i + 2]);
    }

    memset(buf, 0, BUFSIZE);

	//declare / initialize variables to be used in the loop

    int done = 0;
    int opt = ENQUIRE;
    int server = rand() % 3;

    struct fileinfo *files = NULL;
    int nofiles;
    int file;
    int prevserver;

    while(!done)
    {
    	memset(buf, 0, BUFSIZE);
		prevserver = server;
		server = rand() % 3;//get random server for read / enquire requests

		switch(opt)
		{
			case READ:
				;
				file = rand() % nofiles;//select random file

				sprintf(buf, commands[READ], clientno, counter, files[file].filename);
				if(send(serverconns[server], buf, BUFSIZE, 0) < 0)//send read request
					vprintf_error_and_exit("Failed to send read request to server %d\n", server);
				printf("sent read request to server %d\n", server);
				memset(buf, 0, BUFSIZE);
				if(recv(serverconns[server], buf, BUFSIZE, 0) < 0)//receive response from server
					vprintf_error_and_exit("Failed to receive reply to read request from server %d\n", server);
				
				buf[1] = '\0';
				int command = atoi(buf);//get response
				if(command == DENY || command == CANCEL)
				{
					printf("read request denied\n");
					break;//end read operation, access denied
				}
				else
				{
					printf("read file: %s, string read: %s\n", files[file].filename, buf + 2);
					counter++;//print read string, increment local clock
				}
				break;

			case WRITE:
				;
				file = rand() % nofiles;//select random file

				for(i = 0; i < 3; i++)
				{
					sprintf(buf, commands[WRITE], clientno, counter, files[file].filename);
					if(send(serverconns[i], buf, BUFSIZE, 0) < 0)//send write request to each server
						vprintf_error_and_exit("Failed to send write request");

					printf("sent write request to server %d\n", i);

					memset(buf, 0, BUFSIZE);
					if(recv(serverconns[i], buf, BUFSIZE, 0) < 0)//receive response from each server
						vprintf_error_and_exit("Failed to receive write request reply from server %d\n", i);
					
					if(atoi(buf) == DENY)//if request was denied, cancel previous requests that were granted
					{
						printf("write to server %d denied\n", i);
						for(i = i - 1; i >= 0; i--)
						{
							memset(buf, 0, BUFSIZE);
							sprintf(buf, "%d", CANCEL);
							if(send(serverconns[i], buf, BUFSIZE, 0) < 0)//send cancel message
								vprintf_error_and_exit("Failed to send cancel request to server %d\n", i);
							printf("sent write cancellation to server %d\n", i);
							if(recv(serverconns[i], buf, BUFSIZE, 0) < 0)//receive cancel confirmation
								vprintf_error_and_exit("Failed to receive cancel confirmation from server %d\n", i);
						}
						break;//break from this for loop
					}
				}
				if(atoi(buf) == DENY || atoi(buf) == CANCEL)
					break;//break from this switch case

				for(i = 0; i < 3; i++)//if all requests were granted, send write requests to all servers
				{
					memset(buf, 0, BUFSIZE);
					sprintf(buf, commands[WRITE], clientno, counter, files[file].filename);
					if(send(serverconns[i], buf, BUFSIZE, 0) < 0)//send write request
						vprintf_error_and_exit("Failed to send write request to server %d\n", i);
					memset(buf, 0, BUFSIZE);
					if(recv(serverconns[i], buf, BUFSIZE, 0) < 0)//receive write confirmation
						vprintf_error_and_exit("Failed to receive write confirmation from server %d\n", i);
					if(*buf == '1')//print message if file could not be opened / written to
						printf("Received write error from server %d\n", i);
					
				}
				counter++;//increment local clock

				break;
		
			case ENQUIRE:
				sprintf(buf, commands[ENQUIRE], clientno, counter);
				if(send(serverconns[server], buf, BUFSIZE, 0) < 0)//send enquire request
					print_error_and_exit("Error sending enquire request");
				printf("sent enquire request to server %d\n", server);
				memset(buf, 0, BUFSIZE);
				if(recv(serverconns[server], buf, BUFSIZE, 0) < 0)//receive enquire response
					print_error_and_exit("Error receiving enquire request");

				char *token = strtok(buf, "\n");//get first token delimited by '\n'
				if(!files)//if the file structure has not been initialized
				{
					while(token)//while token not null
					{
						files = realloc(files, ++nofiles * sizeof(struct fileinfo));//allocate memory for each file in the response
						files[nofiles - 1].filename = token;//set filename to each token delimited by '\n' for now
						token = strtok(NULL, "\n");//get next token
					}
				}

				else//if the file structure has been previously initialized
				{
					for(i = 0; i < nofiles; i++)
					{
						free(files[i].filename);//free the filename pointer
						files[i].filename = token;//set filename to each token delimited for '\n' for now
						token = strtok(NULL, "\n");
					}
				}

				for(i = 0; i < nofiles; i++)//for each file
				{
					char *temp = strtok(files[i].filename, " ");//now set file structure data by tokenizing filename
					files[i].filename = strcpy(malloc(strlen(temp) + 1), temp);//get heap string for file name
					files[i].operation = *strtok(NULL, " ");
					files[i].clientid = atoi(strtok(NULL, " "));
					printf("%s, %d, %c\n", files[i].filename, files[i].clientid, files[i].operation);//print out file information
				}

				break;
		}

		opt = rand() % 3;//set new command randomly
		if(counter == 100)//if counter is 100, end the loop
			done = 1;
    }
	
    for(i = 0; i < 3; i++)//disconnect from each server
    {
    	memset(buf, 0, BUFSIZE);
		sprintf(buf, commands[DISCONNECT], clientno, counter);
		if(send(serverconns[i], buf, BUFSIZE, 0) < 0)
	    		print_error_and_exit("Error sending disconnect message");
		printf("Disconnect sent to server %d\n", i);
		if(recv(serverconns[i], buf, BUFSIZE, 0) < 0)
			print_error_and_exit("Error receiving disconnect message");
		printf("Disconnect received from server %d\n", i);
		close(serverconns[i]);
    }

    for(i = 0; i < nofiles; i++)
    	free(files[i].filename);//free heap strings from file data structure
    free(files);//free file data structure

    return 0;
}
