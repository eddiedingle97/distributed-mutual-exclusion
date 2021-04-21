#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include "constants.h"

#define BUFSIZE 256
static char buf[BUFSIZE];
static int serverid = -1;

void print_error_and_exit(const char *str)//prints string and exits
{
    perror(str);
    exit(1);
}

char str_match(char *one, char *two)//returns true if the two strings are exactly the same, false otherwise
{
	return strlen(one) == strlen(two) && !strcmp(one, two);
}

static int clientconns[5];
static int counter[5] = {0, 0, 0, 0, 0};//keeps track of client local clocks
static int table[5] = {-1, -1, -1, -1, -1};//translates indexes to clientconns[] to self-reported client numbers
static char *filedir = NULL;//file directory the server will read/write files from
static struct fileinfo *clientprevfile[5] = {NULL, NULL, NULL, NULL, NULL};//keeps track of the last file a client accessed

int main(int argc, char *argv[])
{
    if(argc != 2)
    {
	    puts("Please enter a unique number for the server from 0-2 as an argument");
	    exit(1);//exit if no server number was given
    }

    int serverno = atoi(argv[1]);
    filedir = filedirs[serverno];//set file directory
    struct fileinfo *files = NULL;//initialize file data structure to null
    int nofiles = 0;
    memset(buf, 0, BUFSIZE);
    DIR *dr = opendir(filedir);//open file directory
    struct dirent *de;

    if(dr)//if directory exists
    {
        while((de = readdir(dr)) != NULL)//this loop populates the file data structure
        {
            if(*de->d_name != '.')//if file is not "..", ".", or some hidden file
            {
                nofiles++;//increment total number of files
                files = realloc(files, nofiles * sizeof(struct fileinfo));//allocate memory for file structure
                files[nofiles - 1].filename = strcpy(malloc(strlen(de->d_name) + 1), de->d_name);//populate data
                files[nofiles - 1].operation = '0';
                files[nofiles - 1].clientid = -1;
            }
        }
        closedir(dr);
    }

    memset(buf, 0, BUFSIZE);
    int socketfd = socket(AF_INET, SOCK_STREAM, 0);//create socket and connect to clients
    if(socketfd < 0)
        print_error_and_exit("Error getting socket file descriptor");

    int opt = 1;
    int retval = setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    if(retval < 0)
        print_error_and_exit("Error setting socket options");

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SERVERPORTS[serverno]);
    int addrlen = sizeof(addr);

    retval = bind(socketfd, (struct sockaddr *)&addr, addrlen);
    if(retval)
        print_error_and_exit("Error binding socket");
   
    retval = listen(socketfd, 5);
    if(retval < 0)
        print_error_and_exit("Error listening");

    int i;
    fd_set readfds;//create set for file descriptors (sockets)
    int fdsetsize = 0;
    FD_ZERO(&readfds);
    int nfds = 0;
    for(i = 0; i < 5; i++)//accept 5 connections, one for each client
    {
        clientconns[i] = accept(socketfd, (struct sockaddr *)&addr, (socklen_t *)&addrlen);
        if(clientconns[i] < 0)
            print_error_and_exit("Error accepting connection");

        if(clientconns[i] >= nfds + 1)//this used only for "select" syscall
            nfds = clientconns[i] + 1;

        FD_SET(clientconns[i], &readfds);//add socket to file descriptor set
	    fdsetsize++;//increment set size
    }

    printf("accepted all 5 connections\n");
    
    //declare / initialize variables for main loop

    int done = 0;
    int client = -1;
    char read[BUFSIZE];
    char *countstr = NULL;
    int clientno = -1;
    char *filename = NULL;

    while(!done)
    {
        memset(buf, 0, BUFSIZE);
        memset(read, 0, BUFSIZE);
        if(select(nfds, &readfds, NULL, NULL, NULL) < 1)//wait for messages to come
            print_error_and_exit("Error waiting for file descriptors");

        for(i = 0; i < 5; i++)//check each socket
            if(FD_ISSET(clientconns[i], &readfds))//if there is a message waiting
            {
                recv(clientconns[i], buf, BUFSIZE, MSG_DONTWAIT);//read the message
                break;//then break the loop
            }

        if(i == 5)//if loop went all the way through
            opt = 10;//try again
        else
            client = i;//set client to i value that broke the loop

        if(strlen(buf) > 0)//if buf not empty
        {
            opt = atoi(strtok(buf, " "));//get command
            table[client] = atoi(strtok(NULL, " "));//set socket number-th element of table to self-reported client number
            countstr = strtok(NULL, " ");
            counter[client] = atoi(countstr);//record local clock
        }

        else//if empty message received, usually means connection was broken
        {
            printf("received empty string from client %d, removing client from queue and disconnecting\n", i);
            FD_CLR(clientconns[client], &readfds);
            close(clientconns[client]);
            clientconns[client] = -1;
            opt = 10;
        }


        switch(opt)
        {
            case READ:
                printf("received read request from client %d\n", table[client]);
                filename = strtok(NULL, " ");
                for(i = 0; i < nofiles; i++)//loop through files
                    if(str_match(filename, files[i].filename))//for the file that was requested
                    {
                        struct fileinfo *file = &files[i];
                        if(clientprevfile[client] && file != clientprevfile[client])//set previous file client was using to unused
                        {
                            clientprevfile[client]->operation = '0';
                            clientprevfile[client]->clientid = -1;
                        }

                        if(file->operation == '0' && file->clientid == -1)//if file is unused
                        {
                            file->operation = 'R';
                            file->clientid = client;//give client access to file
                            strcat(read, filedir);
                            strcat(read, "/");
                            strcat(read, filename);
                            FILE *f = fopen(read, "r");//open file
                            memset(read, 0, BUFSIZE);
                            while(fgets(read, BUFSIZE, f));//loop til the last line is reached
                            snprintf(buf, BUFSIZE, "%d %s", ACCEPT, read);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//send accept response with last line appended
                                    print_error_and_exit("failed to send read response");
                            fclose(f);//close file
                            clientprevfile[client] = file;//set clients previous file to this one
                        }

                        else if(file->clientid == client)//if client already has access to this file
                        {
                            strcat(read, filedir);
                            strcat(read, "/");
                            strcat(read, filename);
                            FILE *f = fopen(read, "r");//open file
                            memset(read, 0, BUFSIZE);
                            while(fgets(read, BUFSIZE, f));//loop til the last line is reached
                            snprintf(buf, BUFSIZE, "%d %s", ACCEPT, read);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//send accept response with last line appended
                                    print_error_and_exit("failed to send read response");
                            fclose(f);//close file
                            clientprevfile[client] = file;//set clients previous file to this one
                        } 

                        else if(file->clientid != client && counter[client] < counter[file->clientid])//if another client has access to this file, but this client has higher priority
                        {
                            file->operation = 'R';
                            file->clientid = client;//give client access to this file
                            strcat(read, filedir);
                            strcat(read, "/");
                            strcat(read, filename);
                            FILE *f = fopen(read, "r");//open file
                            memset(read, 0, BUFSIZE);
                            while(fgets(read, BUFSIZE, f));//loop til the last line is reached
                            snprintf(buf, BUFSIZE, "%d %s", ACCEPT, read);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//send accept response with last line appended
                                    print_error_and_exit("failed to send read response");
                            fclose(f);//close file
                            clientprevfile[client] = file;//set clients previous file to this one
                        }

                        else//otherwise deny client access to the file
                        {
                            sprintf(buf, "%d", DENY);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)
                                    print_error_and_exit("failed to send read response");
                            clientprevfile[client] = NULL;//client no longer has a previous file
                        }
                }
                break;

            case WRITE:
                printf("received write request from client %d\n", table[client]);
                filename = strtok(NULL, " ");
                for(i = 0; i < nofiles; i++)
                    if(str_match(filename, files[i].filename))//if the file requested is found
                    {

                        struct fileinfo *file = &files[i];
                        if(clientprevfile[client] && file != clientprevfile[client])//if the client has switched files
                        {
                            clientprevfile[client]->operation = '0';
                            clientprevfile[client]->clientid = -1;
                        }

                        if(file->operation == '0' && file->clientid == -1)//if the file is unused
                        {
                            sprintf(buf, "%d", ACCEPT);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)
                                    print_error_and_exit("failed to send write response");
                            file->operation = 'W';
                            file->clientid = client;//grant access
                        }

                        else if(file->clientid == client)//if the client already has access to the file
                        {
                            sprintf(buf, "%d", ACCEPT);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)
                                    print_error_and_exit("failed to send write response");//grant access
                        }

                        else if(file->clientid != client && counter[client] < counter[file->clientid])//if another client has access but this client has a higher priority
                        {
                            sprintf(buf, "%d", ACCEPT);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)
                                    print_error_and_exit("failed to send write response");
                            file->operation = 'W';
                            file->clientid = client;//grant access
                        }

                        else//otherwise deny access
                        {
                            puts("access denied");
                            sprintf(buf, "%d", DENY);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)
                                    print_error_and_exit("failed to send write response");
                            break;//break the switch case
                        }

                        memset(buf, 0, BUFSIZE);

                        puts("receiving second request");

                        if(recv(clientconns[client], buf, BUFSIZE, 0) < 0)//receive second request
                            print_error_and_exit("Failed to receive second write message\n");

                        puts("received second request");

                        if(atoi(strtok(buf, " ")) == CANCEL)//the client has been denied access from another server
                        {
                            file->operation = '0';
                            file->clientid = -1;//remove access
                            printf("client %d canceled write request\n", table[client]);
                            memset(buf, 0, BUFSIZE);
                            sprintf(buf, "%d", CANCEL);
                            if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//send confirmation that request has been cancelled
                                print_error_and_exit("cancel confirmation failed");
                            clientprevfile[client] = NULL;
                            break;
                        }

                        else
                        {
                            memset(read, 0, BUFSIZE);
                            strcat(read, strtok(NULL, " "));
                            strcat(read, " ");
                            strcat(read, strtok(NULL, " "));
                            strcat(read, "\n");//construct char buffer to write to file

                            filename = strtok(NULL, " ");
                            char openfile[64];
                            puts(filename);
                            memset(openfile, 0, 64);
                            strcat(openfile, filedir);
                            strcat(openfile, "/");
                            strcat(openfile, filename);//construct char buffer to open file

                            FILE *f = fopen(openfile, "a");//open file
                            if(f)//if file could be opened
                            {
                                fputs(read, f);//write to it
                                fclose(f);//close file
                                printf("wrote \"%s\" to file %s\n", read, filename);
                                memset(buf, 0, BUFSIZE);
                                sprintf(buf, "%d", ACCEPT);
                                if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//confirm to client file was written to
                                    print_error_and_exit("write confirmation failed");
                                clientprevfile[client] = file;
                            }
                            
                            else//if file could not be opened
                            {
                                printf("failed to open file %s\n", filename);
                                memset(buf, 0, BUFSIZE);
                                sprintf(buf, "%d", DENY);
                                if(send(clientconns[client], buf, BUFSIZE, 0) < 0)//report to client there was an error
                                    print_error_and_exit("error message failed");
                                clientprevfile[client] = file;
                            }
                    }
                }
                break;

            case ENQUIRE:
                printf("received enquire request from client %d\n", table[client]);
                memset(buf, 0, BUFSIZE);
                for(i = 0; i < nofiles; i++)//loop through file data structure
                {
                    if(files[i].clientid >= 0)//construct char buffer to serialize data structure
                        sprintf(read, "%s %c %d\n", files[i].filename, files[i].operation, table[client]);
                    else
                        sprintf(read, "%s %c %d\n", files[i].filename, files[i].operation, files[i].clientid);
                    strcat(buf, read);
                    memset(read, 0, BUFSIZE);
                }
                send(clientconns[client], buf, BUFSIZE, 0);//send data to client
                break;

        case DISCONNECT:
            printf("client %d has disconnected\n", table[client]);
            sprintf(buf, "%d", ACCEPT);
            send(clientconns[client], buf, BUFSIZE, 0);//confirm with client server is disconnecting
            FD_CLR(clientconns[client], &readfds);//remove socket from file descriptor set
            close(clientconns[client]);//close connection
            clientconns[client] = -1;//set file descriptor to -1
            if(clientprevfile[client])//if client had a previous file, set it to unused
            {
                clientprevfile[client]->operation = '0';
                clientprevfile[client]->clientid = -1;
            }
            counter[client] = INT_MAX;//make client counter largest possible value
            break;
        }

        memset(buf, 0, BUFSIZE);
        FD_ZERO(&readfds);//empty file descriptor set
        fdsetsize = 0;
        for(i = 0; i < 5; i++)
        {
            if(clientconns[i] > 0)//if connection is still active
            {
                FD_SET(clientconns[i], &readfds);//add socket to file descriptor set
                fdsetsize++;
            }
            if(clientconns[i] >= nfds + 1)
                nfds = clientconns[i] + 1;//this is only for the "select" syscall
        }

        if(!fdsetsize)//if our file descriptor set is empty, all clients have disconnected and the server will exit
            done = 1;
    }
    
    for(i = 0; i < nofiles; i++)
        free(files[i].filename);

    free(files);

    return 0;
}
