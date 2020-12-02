#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>
#include "protocol.h"
#include <linux/sched.h>

#define BUFFERSIZE 75
#define BACKLOG 10 /* how many pending connections queue will hold */
#define NUMHANDLERTHREADS 5
#define STRINGSIZE 10000

int numRequests = 0;
int cleanUp = 0;

pthread_t pThreads[NUMHANDLERTHREADS]; // thread structures
pthread_mutex_t memMutex[NUMHANDLERTHREADS];//memory list mutex
pthread_mutex_t requestMutex;/* global mutex for our program. */
pthread_cond_t gotRequest;/* global condition variable for our program. */

struct sysinfo mySystem; //struct for data storage

/* format of a single request. */
struct request
{
	int number;	  									/* number of the request for debgging                 */
	int clientfd; // the client who requested it, can be used for later
	struct requestPacket *rPtr;
	struct request *next; 				/* pointer to next request, NULL if none. */
};

struct managerInfo //struct for a manager looking after an process executing
{
	int parentThread;
	int log;
	FILE* p;
	int timeout;
	pid_t childPid;
	int *doneFlag;
	int threadID;
	char *fileName[BUFFERSIZE];
	char *args[BUFFERSIZE];
};

//struct for storing memory info
typedef struct memInfo memInfo_t;
struct memInfo
{
	char timestamp[26];
	pid_t pid;
	long memusage;
	struct memInfo *next;
	char *fileName[BUFFERSIZE];
	char *args[BUFFERSIZE];
};

struct memInfo *memListsHeads[NUMHANDLERTHREADS];
struct request *requests = NULL;	/* head of linked list of requests. */
struct request *lastRequest = NULL; /* pointer to last request.         */

int requestCounter = 0;
int threadKillFlag = 0;

char timeBuffer[26];
void currentTime();
void currentTimeToPointer();
void logToFile(char **cmd);

//memkill request
void memKill(float percent, int socketIdentifier)
{
	sysinfo(&mySystem);
	unsigned long int x = mySystem.totalram;
	unsigned long int s = mySystem.totalswap;
	const double megabyte = 1024 * 1024;
	float memThreshhold = (mySystem.totalram / 100) * percent;

	//lets look through the proccess to find their usage
	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		if (memListsHeads[i] != NULL)
		{
			if (memListsHeads[i]->memusage > memThreshhold)
			{
				int pid = memListsHeads[i]->pid;
				kill(memListsHeads[i]->pid, 9);
			}
		}
	}

	close(socketIdentifier);
}

//mem request with no PID
void memNoPID(int socketIdentifier)
{
	int runningProcesses = 0;
	char responseBuffer[NUMHANDLERTHREADS][BUFFERSIZE];
	char currentTime[26];
	currentTimeToPointer(&currentTime);

	//work out how many response we need
	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		if (memListsHeads[i] != NULL)
		{
			//printf("%d %ld %s %s\n", memListsHeads[i]->pid, memListsHeads[i]->memusage, memListsHeads[i]->fileName, memListsHeads[i]->args);
			sprintf((char *)responseBuffer[runningProcesses], "%d %ld %s %s", memListsHeads[i]->pid, memListsHeads[i]->memusage, memListsHeads[i]->fileName, memListsHeads[i]->args);
			runningProcesses++;
		}
	}
	//send that number to the client
	send(socketIdentifier, &runningProcesses, sizeof(int), 0);

	//send each one of those
	for (int i = 0; i < runningProcesses; i++)
	{
		send(socketIdentifier, responseBuffer[i], sizeof(char) * BUFFERSIZE, 0);
	}
}

//add mem node
void addMemNode(int threadID, long usage, pid_t pid, char *fileName[BUFFERSIZE], char *args[BUFFERSIZE])
{
	if (usage == -1)
	{
		return;
	}

	memInfo_t *new = (memInfo_t *)malloc(sizeof(memInfo_t));

	if (memListsHeads[threadID] == NULL) //if the head is currently nothing
	{
		new->next = NULL;
	}
	else
	{
		new->next = memListsHeads[threadID]; //if the head is an element
	}
	//add the data
	new->pid = pid;
	new->memusage = usage;
	//generate time stamp
	char time[26];
	currentTimeToPointer(&time);
	strcpy(new->timestamp, time);
	strcpy((char *)new->fileName, (char *)fileName);
	strcpy((char *)new->args, (char *)args);

	//make new the new head
	memListsHeads[threadID] = new;
}

//cleans the linked list used by a function when its done
void purgeList(int threadID)
{
	memInfo_t *current = memListsHeads[threadID];
	memListsHeads[threadID] = NULL;
	while (current != NULL)
	{
		memInfo_t *next = current->next;
		free(current);
		current = next;
	}
}

void memPID(int pid, int clientSocket, int threadID)
{
	//lock
	pthread_mutex_lock(&memMutex[threadID]);

	bool foundPid = false;
	struct memInfo *target = NULL;
	char responseBuffer[BUFFERSIZE]={0};

	char myBuff[10000]={0};

	//lets search through our linked lists for the pid
	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		if (memListsHeads[i] != NULL)
		{
			if (memListsHeads[i]->pid == pid)
			{
				//we have our target list
				foundPid = true;
				target = memListsHeads[i];
			}
		}
	}

	//if not found say its over
	if (!foundPid)
	{
		//say there is nothing
		strcpy(responseBuffer, "NOTHING");
		send(clientSocket, responseBuffer, sizeof(responseBuffer), 0);
	}
	else
	{
		while (target != NULL)
		{
			//printf("\n%s - %ld \n", target->timestamp, target->memusage);
			sprintf(responseBuffer, "%s - %ld \n", target->timestamp, target->memusage);
			//printf("%s",responseBuffer);
			//fflush(stdout);
			//send(clientSocket, &responseBuffer, sizeof(responseBuffer), 0);
			strcat(myBuff, responseBuffer);
			//printf("%s\n", responseBuffer);
			target = target->next;
		}
		//tell the controller we are done
		//printf("%s\n",myBuff);
		//printf("done\n");
		//printf("%s\n",myBuff);
		//strcpy(responseBuffer, "DONE");
		send(clientSocket, myBuff, sizeof(myBuff), 0);
	}
	//free lock
	pthread_mutex_unlock(&memMutex[threadID]);
}

//memory usage calculator
long processMemUse(int pidint)
{
	// creating the string to be used as a path to the maps file of a given process
	char path[20], pid[20], tail[20];
	strcpy(path, "//proc/");
	//converts the passed int to char string
	sprintf(pid, "%d", pidint);
	strcpy(tail, "/maps");
	// stich above strings together to "path" string
	strcat(pid, tail);
	strcat(path, pid);

	// open the file in read mode
	FILE *fp;
	fp = fopen(path, "r");
	if (fp == NULL)
	{
		return -1;
	}

	//read file line by line, isolate the lines where inode is 0, compute total current memory usage
	char *line = NULL;
	char *save = NULL;
	ssize_t read;
	size_t offset = 0;

	//this will be the total
	long bytesUsed = 0;
	char *marker = " 0 ";
	char *markerTwo = " 0\n";

	while ((read = getline(&line, &offset, fp)) != -1)
	{
		if ((strstr(line, marker) != NULL) || (strstr(line, markerTwo) != NULL))
		{
			char *line2 = line;
			strtok(line2, " ");
			//extract the hexadecimal memory offsets
			char first[30], second[30];
			int i = 0;
			while (line2[i] != '-')
			{
				first[i] = line2[i];
				i++;
			}
			first[i] = '\0';
			i++;

			int b = 0;
			while (line2[i] != ' ')
			{
				second[b] = line2[i];
				i++;
				b++;
			}
			//perform arithmetic and add result to total
			long total = ((strtol(second, NULL, 16)) - (strtol(first, NULL, 16)));
			bytesUsed = bytesUsed + total;
		}
	}

	free(line);
	fclose(fp);
	return bytesUsed;
}

//manager function
void *manager(void *arg)
{
	//we have been parsed the info
	struct managerInfo info = *(struct managerInfo *)arg;

	int clock = 1;
	char mTimeBuffer[26];

	int killTime = __INT_MAX__;

	int done;
	int *dptr = info.doneFlag;
	done = *dptr;

	bool killMsg = false;
	bool termMsg = false;

	pid_t child = info.childPid;

	while (!done) //pointer inside the fork that changes when the program is done
	{
		long usage = processMemUse(info.childPid);
		//append to the linked list

		//lets take the mutex
		pthread_mutex_lock(&memMutex[info.parentThread]);
		addMemNode(info.parentThread, usage, info.childPid, info.fileName, info.args);
		pthread_mutex_unlock(&memMutex[info.parentThread]);

		//end mutex

		//check if the child needs to be killed
		if (clock >= info.timeout)
		{
			//first attempt
			if (!termMsg)
			{
				killTime = clock + 5;
				//child needs to be killed
				currentTimeToPointer(&mTimeBuffer);
				
				//start
				if (info.log)
				{
					fprintf(info.p,"%s - sent SIGTERM to %d\n", mTimeBuffer, info.childPid); //string swapped to argString
					fflush(info.p);
				}
				else
				{
					printf("%s - sent SIGTERM to %d\n", mTimeBuffer, info.childPid);
				}
				//end
				kill(info.childPid, SIGTERM);
				termMsg = true;
			}
		}

		if (clock >= killTime || cleanUp)
		{
			//child needs to be killed forcefully
			kill(info.childPid, SIGKILL);
			currentTimeToPointer(&mTimeBuffer);
			if (!killMsg)
			{
				
				//start
				if (info.log)
				{
					fprintf(info.p,"%s - sent SIGKILL to %d\n", mTimeBuffer, info.childPid); //string swapped to argString
					fflush(info.p);
				}
				else
				{
					printf("%s - sent SIGKILL to %d\n", mTimeBuffer, info.childPid);
				}
				//end
				killMsg = true;
			}
			if (killMsg)
			{
				kill(child, SIGKILL);
			}
		}
		//update done flag value
		clock++;
		sleep(1);
		done = *dptr;
	}
}

void handleRequest(struct request *aRequest, int threadID)
{
	//lets unpack the request and put it in the file
	requestPacket *packet = (requestPacket *)aRequest->rPtr;

	//check what type of request it is
	if (packet->memFlag)
	{
		int qpid = ntohl(packet->memRequestPID);
		//default request
		if (qpid == -1)
		{
			//regular mem
			memNoPID(aRequest->clientfd);
		}
		else
		{
			memPID(qpid, aRequest->clientfd, threadID);
		}
	}
	else if (packet->memKillFlag)
	{
		memKill(packet->memKillPercent, aRequest->clientfd);
	}
	else
	{
		FILE *p=fopen(packet->logFile,"w");
		
		
		

		//personal time buffer
		char pTimeBuffer[26];

		//argString for printing
		char argString[FILENAME_MAX] = "";

		for (int i = 0; i < packet->nParams; i++)
		{
			strcat(argString, packet->params[i]);
			strcat(argString, " ");
		}

		//check for custom timeout
		int timeout = 10;
		if (packet->sigtermFlag)
		{
			timeout = packet->timeout;
		}



		//fflush(NULL);

		//get the file path
		char cmd[BUFFERSIZE];
		strcpy(cmd, packet->file);

		//extract executable name
		char execname[BUFFERSIZE];
		strcpy(execname, packet->file);
		int ch = '/';
		char *ptr;
		ptr = strrchr(execname, ch);
		ptr++;

		char *arguments[ARGS_MAX];
		for (int i = 0; i < ARGS_MAX; i++)
		{
			arguments[i] = NULL;
		}
		arguments[0] = ptr;
		for (int i = 0; i < packet->nParams; i++)
		{
			char *temp = (char *)&packet->params[i];
			arguments[i + 1] = temp;
		}

		bool failed = false;

		//unshare(CLONE_FILES);
		pid_t pid = fork();
		int status;

		if (pid == 0) // child process will run the process
		{
			int saved_stdout;
			saved_stdout = dup(1);

			int outfd;
			if (packet->outFlag)
			{
			if ((outfd = open(packet->outFile, O_CREAT | O_TRUNC | O_WRONLY, 0644)) < 0)
			{
				perror("creating failed"); /* open failed */
			}
			dup2(outfd,STDOUT_FILENO);
			dup2(outfd,STDERR_FILENO);
			}
			

			
			execv(cmd, arguments);
			char cTimeBuffer[26];
			currentTimeToPointer(&cTimeBuffer);

			dup2(saved_stdout, 1);
			close(saved_stdout);
			if (packet->logFlag)
			{
				fprintf(p,"%s - could not execute %s %s\n", cTimeBuffer, packet->file, argString); //string swapped to argString
			}
			else
			{
				printf("%s - could not execute %s %s\n", cTimeBuffer, packet->file, argString);
			}
			
			
			

			exit(-1); //exit
		}
		else
		{
			currentTimeToPointer(&pTimeBuffer);
			//start
			if (packet->logFlag)
			{
				fprintf(p,"%s - attempting to execute %s %s\n", pTimeBuffer, packet->file, argString); //string swapped to argString
				fflush(p);
			}
			else
			{
				printf("%s - attempting to execute %s %s\n", pTimeBuffer, packet->file, argString); //string swapped to argString
				
			}
			//check if it is running
			bool worked = true;
			sleep(1);
			pid_t returnPID = waitpid(pid, &status, WNOHANG); /* WNOHANG def'd in wait.h */

			if (returnPID == 0)
			{
				//running
				currentTimeToPointer(&pTimeBuffer);
				if (packet->logFlag)
			{
				fprintf(p,"%s - %s %s has been execute with pid %d \n", pTimeBuffer, packet->file, argString, pid); //string swapped to argString
				fflush(p);
			}
			else
			{
				printf("%s - %s %s has been execute with pid %d \n", pTimeBuffer, packet->file, argString, pid);
			}
		
			//end
				
			}
			else
			{
				//dead
				purgeList(threadID);
				//free(string);
				return;
			}
			//create manager thread
			int done = 0;
			int *dptr = &done;
			struct managerInfo info;
			if (packet->logFlag)
			{
				info.log = 1;
			}
			else
			{
				info.log = 0;
			}
			info.p = p;
			info.parentThread = threadID;
			info.childPid = pid;
			info.timeout = timeout;
			info.doneFlag = dptr;
			info.threadID = threadID;
			strcpy((char *)info.fileName, execname);
			strcpy((char *)info.args, argString);

			pthread_t handle;
			pthread_create(&handle, NULL, manager, &info);

			//catch the child weather that be normal exit or signal
			waitpid(pid, &status, 0);
			if (WIFSIGNALED(status) && worked)
			{
				done = 1;
				int returned = WEXITSTATUS(status);
				currentTimeToPointer(&pTimeBuffer);
				if (packet->logFlag)
			{
				fprintf(p,"%s %d has terminated with status code %d\n", pTimeBuffer, pid, returned); //string swapped to argString
				fflush(p);
			}
			else
			{
				printf("%s %d has terminated with status code %d\n", pTimeBuffer, pid, returned);
			}
			}
			else if (WIFEXITED(status) && worked)
			{
				done = 1;
				int returned = WEXITSTATUS(status);
				currentTimeToPointer(&pTimeBuffer);
				if (packet->logFlag)
			{
				fprintf(p,"%s %d has terminated with status code %d\n", pTimeBuffer, pid, returned); //string swapped to argString
				fflush(p);
			}
			else
			{
				printf("%s %d has terminated with status code %d\n", pTimeBuffer, pid, returned);
			}
			}
	

			//join the thread back
			pthread_join(handle, NULL);
			//purge the memlist used by that thread
			purgeList(threadID);
		}
	}
}

//will append the request to the linked list and wake a thread
void addRequest(struct requestPacket *rPacket, int socketIdentifier)
{
	//create new request
	struct request *nRequest = (struct request *)calloc(1, sizeof(struct request));
	if (!nRequest)
	{ /* malloc failed */
		fprintf(stderr, "add_request: out of memory\n");
		exit(1);
	}
	//lets add information
	nRequest->rPtr = rPacket;
	nRequest->clientfd = socketIdentifier;
	nRequest->number = requestCounter;
	requestCounter++;
	nRequest->next = NULL;

	//lock the linked list mutex
	pthread_mutex_lock(&requestMutex);

	if (numRequests == 0)
	{
		//special case list is empty
		requests = nRequest;
		lastRequest = nRequest;
	}
	else
	{
		//append this to the end
		lastRequest->next = nRequest;
		lastRequest = nRequest;
	}
	//increase the number of requests
	numRequests++;

	//unlock the mutex
	pthread_mutex_unlock(&requestMutex);
	//signal condition variable
	pthread_cond_signal(&gotRequest);
}

//pulls the request at the head of the list and returns it to the caller
struct request *getRequest()
{
	struct request *aRequest; //pointer to the request

	if (numRequests > 0)
	{
		aRequest = requests;
		requests = aRequest->next;
		if (requests == NULL)
		{ /* this was the last request on the list */
			lastRequest = NULL;
		}
		/* decrease the total number of pending requests */
		numRequests--;
	}
	else
	{ /* requests list is empty */
		aRequest = NULL;
	}

	/* return the request to the caller. */
	return aRequest;
}

/*
	handle request loops will remove a request from the linked list
	and pass it to a thread to be used
*/
void *handleRequestsLoop(void *data)
{
	struct request *aRequest;	   /* pointer to a request.               */
	int threadID = *((int *)data); /* thread identifying number           */

	while (!cleanUp)
	{
		if (numRequests > 0)
		{ /* a request is pending */
			aRequest = getRequest();
			if (aRequest)
			{ /* got a request - handle it and free it */
				/* unlock mutex - so other threads would be able to handle */
				/* other reqeusts waiting in the queue paralelly.          */
				pthread_mutex_unlock(&requestMutex);
				handleRequest(aRequest, threadID);
				close(aRequest->clientfd); //close the socket
				free(aRequest->rPtr);	   //Free the child
				free(aRequest);			   //free this request from the linked list

				//maybe have to lock the list again
				pthread_mutex_lock(&requestMutex);
			}
		}
		else
		{
			/* wait for a request to arrive. note the mutex will be */
			/* unlocked here, thus allowing other threads access to */
			/* requests list.                                       */

			//printf("thread sleeping %d \n", threadID);
			pthread_cond_wait(&gotRequest, &requestMutex);
			//printf("thread was triggered! %d \n", threadID);
			/* and after we return from pthread_cond_wait, the mutex  */
			/* is locked again, so we don't need to lock it ourselves */
		}
	}
	printf("thread %d cleaning up... BYE!\n", threadID);
}

// will just accept all connections and report them
void startListening(int argc, char *argv[])
{
	int sockfd, newfd;
	struct sockaddr_in myAddr;	  /* my address information */
	struct sockaddr_in theirAddr; /* connector's address information */
	socklen_t sinSize;

	/* Get port number for server to listen on */
	if (argc != 2)
	{
		fprintf(stderr, "usage: client port_number\n");
		exit(1);
	}
	/* generate the socket */
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(1);
	}

	// may need later to reuse socket, works fine without for now

	int opt_enable = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable));
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt_enable, sizeof(opt_enable));

	/* clean myAddr struct */
	memset(&myAddr, 0, sizeof(myAddr));

	/* generate the end point */
	myAddr.sin_family = AF_INET;			 /* host byte order */
	myAddr.sin_port = htons(atoi(argv[1]));	 /* short, network byte order */
	myAddr.sin_addr.s_addr = INADDR_ANY;	 /* auto-fill with my IP */
	/* bzero(&(myAddr.sin_zero), 8);   ZJL*/ /* zero the rest of the struct */

	/* bind the socket to the end point */
	if (bind(sockfd, (struct sockaddr *)&myAddr, sizeof(struct sockaddr)) == -1)
	{
		perror("bind");
		exit(1);
	}

	/* start listnening */
	if (listen(sockfd, BACKLOG) == -1)
	{
		perror("listen");
		exit(1);
	}

	//printf("server starts listnening ...\n");

	/* repeat: accept, send, close the connection */
	/* for every accepted connection, use a sepetate process or thread to serve it */
	while (1)
	{ /* main accept() loop */
		sinSize = sizeof(struct sockaddr_in);
		if ((newfd = accept(sockfd, (struct sockaddr *)&theirAddr,
							&sinSize)) == -1)
		{
			perror("accept");
			continue;
		}

		currentTime();
		printf("%s - connection recieved from %s\n",
			   timeBuffer, inet_ntoa(theirAddr.sin_addr));

		requestPacket *rp = calloc(1, sizeof(requestPacket));
		recv(newfd, rp, sizeof(requestPacket), MSG_WAITALL);

		//fix our packet make it host address not network
		rp->nParams = ntohl(rp->nParams);
		rp->timeout = ntohl(rp->timeout);

		//pass this to the linked list for later use
		addRequest((struct requestPacket *)rp, newfd);
	}
}

// helper function to receive our request struct (requestPackets);
requestPacket *receiveRequest(int socketIdentifier)
{

	requestPacket *result = malloc(sizeof(requestPacket));

	//WORKING ON: receive the incoming request in the form of a struct, and return it
	//(or maybe stick it in a linked list) may need to change 0 to MSG_WAITALL
	if (recv(socketIdentifier, result, sizeof(requestPacket), 0) == -1)
	{
		perror("recv error");
	};
	return result;
}

// helper function that puts the current time into the timeBuffer character buffer
void currentTime()
{
	// could make these global like the buffer
	time_t timer;
	struct tm *tmInfo;

	timer = time(NULL);
	tmInfo = localtime(&timer);
	strftime(timeBuffer, 26, "%Y-%m-%d %H:%M:%S", tmInfo);
}

void currentTimeToPointer(char *timeBuffer)
{
	// could make these global like the buffer
	time_t timer;
	struct tm *tmInfo;

	timer = time(NULL);
	tmInfo = localtime(&timer);
	strftime(timeBuffer, 26, "%Y-%m-%d %H:%M:%S", tmInfo);
}

void logToFile(char **cmd)
{
	int fds[2];
	pipe(fds);

	if (fork())
	{
		// Set up stdout as the write end of the pipe
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);
	}
	else
	{
		// Double fork to not be a direct child
		if (fork())
			exit(0);
		// Set up stdin as the read end of the pipe, and run tee
		dup2(fds[0], 0);
		close(fds[0]);
		close(fds[1]);
		execvp(cmd[0], cmd);
	}
}

void handleSigint(int sig)
{
	printf("attempting to clean up\n");

	//terminate the threads
	cleanUp = 1;

	//rejoin threads
	for (size_t i = 0; i < NUMHANDLERTHREADS; i++)
	{
		pthread_cond_broadcast(&gotRequest);
		pthread_mutex_unlock(&requestMutex);
		sleep(1);
		pthread_detach(pThreads[i]);
	}
	printf("threads cleaned\n");

	//purge the linked lists
	struct request *traverse = requests;
	while (traverse != NULL)
	{
		//free child
		free(traverse->rPtr);
		struct request *temp = traverse;
		traverse = traverse->next;
		free(traverse);
	}
	printf("linked list cleaned\n");
	//clean memory lists
	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		purgeList(i);
	}

	exit(1);
}

int main(int argc, char *argv[])
{
	//signal handler
	signal(SIGINT, handleSigint);

	int thrId[NUMHANDLERTHREADS]; //thread ID's

	pthread_mutex_init(&requestMutex, NULL);

	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		/* code */
		pthread_mutex_init(&memMutex[i], NULL); //move
	}

	pthread_cond_init(&gotRequest, NULL);

	/* create the request-handling threads */
	for (int i = 0; i < NUMHANDLERTHREADS; i++)
	{
		thrId[i] = i;
		pthread_create(&pThreads[i], NULL, handleRequestsLoop, (void *)&thrId[i]);
	}
	sleep(1);

	//initalize mutex and condition variables
	startListening(argc, argv);

	return 0;
}
