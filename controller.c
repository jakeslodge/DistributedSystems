#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "protocol.h"

#define BUFFERSIZE 75
#define LOCALIP "127.0.0.1"
#define STRINGSIZE 10000

struct request
{
};

void printError()
{
    fprintf(stderr,"Usage: controller <address> <port> {[-o out_file] [-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
    exit(1);
}

void verifyFlags(requestPacket myRequest,int l,int o,int t)
{
    if(myRequest.logFlag && myRequest.outFlag)
            {
                if(o > l)
                {
                    printError();
                }
            }
            if(myRequest.logFlag && myRequest.sigtermFlag)
            {
                if(l > t)
                {
                    printError();
                }
            }
            if(myRequest.outFlag && myRequest.sigtermFlag)
            {
                if(o > t)
                {
                    printError();
                }
            }

    
}

void printRequest(requestPacket *r)
{
    if (r->memKillFlag)
    {
        printf("memkill flag: %d , value: %f\n", r->memKillFlag, r->memKillPercent);
    }
    if (r->memFlag)
    {
        printf("mem flag: %d, pid (optional): %d \n", r->memFlag, ntohl(r->memRequestPID));
    }
    if (r->logFlag)
    {
        printf("logfile: %s\n", r->logFile);
    }
    if (r->outFlag)
    {
        printf("outfile: %s\n", r->outFile);
    }
    if (r->sigtermFlag)
    {
        printf("custom timeout: %d \n", r->timeout);
    }
    printf("file: %s\n", r->file);
}

void initalizeRequest(requestPacket *r)
{
    r->logFlag = false;
    r->outFlag = false;
    r->sigtermFlag = false;
    r->memFlag = false;
    r->memRequestPID = htonl(-1);
    r->memKillFlag = false;
}

/*
Main entry point for the program
usage: ./controller <address> <port> {additional args}
*/

int main(int argc, char **argv)
{
    int sockfd;
    //hostent is used to store host information
    struct hostent *he;
    //sockaddr specifies transport information
    struct sockaddr_in overseerAddr;

    if(argc==1)
    {
        printError();
    }

    //check for --help flag
    if (argc == 2)
    {
        if (0 == (strcmp(argv[1], "--help")))
        {
            printf("Usage: controller <address> <port> {[-o out_file][-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
            exit(1);
        }
    }

    if (0 == (strcmp(argv[1], "--help")))
    {
        printf("Usage: controller <address> <port> {[-o out_file] [-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
        exit(1);
    } 
   
    //check that they have given an <address> and <port>
    if (argc < 3)
    {
        fprintf(stderr,"Usage: controller <address> <port> {[-o out_file] [-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
        exit(1);
    }

    //check if the port given is an interger
    char *ptr;
    long ret;
    ret = strtol(argv[2], &ptr, 10);
    if(ret == 0)
    {
        fprintf(stderr,"Usage: controller <address> <port> {[-o out_file] [-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
        exit(1);
    }

    //create a request packet to be sent and start filling it
    requestPacket myRequest;
    initalizeRequest(&myRequest);

    //work through argc and figure out what type of request will be made
    for (int i = 1; i < (argc); i++)
    {

        //check for memkill
        if (strcmp("memkill", argv[i]) == 0)
        {
            //if we have memkill then the argument length must be 4
            if (argc == 5)
            {
                myRequest.memKillPercent = atof(argv[4]);
                myRequest.memKillFlag = true;
            }
            else
            {
                fprintf(stderr, "usage: overseerAddress port_number memkill <percent>\n");
                exit(1);
            }
        }
        //check for mem <pid>
        if (strcmp("mem", argv[i]) == 0)
        {
            //if we have 5 then we have a pid
            if (argc == 5)
            {
                myRequest.memFlag = true;
                myRequest.memRequestPID = htonl(atoi(argv[4]));
            }
            else
            {
                myRequest.memFlag = true;
            }
        }
    }

    
    int logLocation = 0;
    int oLocation = 0;
    int tLocation = 0;

    /* We know at this point that we have an address and port, so for every flag we find 
    the <file> will be offset by 2 */
    if ((!myRequest.memFlag) && (!myRequest.memKillFlag))
    {
        int fileLocation = 3;
        for (int i = 0; i < argc; i++)
        {
            if (0 == strcmp(argv[i], "-log"))
            {
                fileLocation = fileLocation + 2;
                myRequest.logFlag = true;
                logLocation = i;
                strcpy(myRequest.logFile, argv[i + 1]);
            }
            else if (0 == strcmp(argv[i], "-o"))
            {
                fileLocation = fileLocation + 2;
                myRequest.outFlag = true;
                oLocation = i;
                strcpy(myRequest.outFile, argv[i + 1]);
            }
            else if (0 == strcmp(argv[i], "-t"))
            {
                fileLocation = fileLocation + 2;
                myRequest.sigtermFlag = true;
                tLocation = i;
                myRequest.timeout = htonl(atoi(argv[i + 1]));
            }
            //check that flags are provided when flags are used
        }
        verifyFlags(myRequest,logLocation,oLocation,tLocation);

            

        //check that the a filename is given with a flag
        if (fileLocation >= argc)
        {
            fprintf(stderr,"Usage: controller <address> <port> {[-o out_file] [-log log_file][-t seconds] <file> [arg...] | mem [pid] | memkill <percent>}\n");
            exit(1);
        }


        //the file to run should be at fileLocation, do a check to see if they have added a file
        if (argc < 4)
        {
            fprintf(stderr, "usage: overseerAddress port_number {flags} <file> {args}\n");
            exit(1);
        }
        else
        {
            //all is good lets grab that filename
            strcpy(myRequest.file, argv[fileLocation]);
        }

        //set my the argument lengths in the struct
        myRequest.nParams = htonl(argc - fileLocation - 1);

        int l = 0;
        for (int z = fileLocation + 1; z < argc; z++) //put the rest of the arguments in the array
        {
            strcpy(myRequest.params[l], argv[z]);
            l++;
        }
    }


    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket\n");
        exit(1);
    }

    overseerAddr.sin_family = AF_INET;            /* host byte order */
    overseerAddr.sin_port = htons(atoi(argv[2])); /* short, network byte order */
    bzero(&(overseerAddr.sin_zero), 8); /* zero the rest of the struct */

    if(strcmp(argv[1],"localhost")==0)
    {
        inet_pton(AF_INET, LOCALIP, &overseerAddr.sin_addr);
    }
    else
    {
        inet_pton(AF_INET, argv[1], &overseerAddr.sin_addr);
    }
    
    //Lets attempt to connect to the overseer, if not exit and report the error
    if (connect(sockfd, (struct sockaddr *)&overseerAddr,
                sizeof(struct sockaddr)) == -1)
    {
        fprintf(stderr, "Could not connect to overseer at %s %d \n", argv[1], atoi(argv[2]));
        exit(1);
    }

    //otherwise send it
    //printf("attempting to send my struct\n");
    //send(sockfd,&myRequest,sizeof(requestPacket),0);
    send(sockfd,&myRequest,sizeof(requestPacket),0);


    //if we are sending a normal request we can terminate

    //else we must wait for a response
    if (myRequest.memFlag && myRequest.memRequestPID == -1)
    {
        //printf("waiting for response\n");
        //we are just wanting to recive an int
        int responseCount = 0;
        recv(sockfd, &responseCount, sizeof(int), 0);
        //printf("there are %d responses\n", responseCount);

        for (int i = 0; i < responseCount; i++)
        {
            char buff[BUFFERSIZE];
            recv(sockfd, buff, sizeof(char) * BUFFERSIZE, 0);
            printf("%s\n", buff);
        }
    }
    else if (myRequest.memFlag && myRequest.memRequestPID != -1)
    {
        char myBuff[10000]={0};
        //char* myBuff = calloc(1,sizeof(char)*STRINGSIZE);
        
        bool finished = false;

        // while (!finished)
        // {
        //     recv(sockfd, &myBuff, sizeof(char)*75 , 0);
        //     if (strcmp("NOTHING", myBuff) == 0)
        //     {
        //         //nothing found terminate
        //         finished = true;
        //     }
        //     else if (strcmp("DONE", myBuff) == 0)
        //     {
        //         finished = true;
        //     }
        //     else
        //     {
        //         //print that info
        //         printf("%s\n", myBuff);
        //     }
        // }


        recv(sockfd, &myBuff, sizeof(myBuff) , 0);
        printf("%s \n",myBuff);
    }
    
    //shutdown(sockfd,2);
    close(sockfd);//close the socket

    return 0;
}
