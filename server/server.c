#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket() and bind() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <errno.h>      /* for errno */
#include <semaphore.h>

#define ECHOMAX 255     /* Longest string to echo */
#define BUFF_SIZE 10

struct packet {
    char data[100];
    int seq;
    int corrupt;
    int wsize;
};

//make port 4950
void receiveIt();
void *sendIt(char *received);
void printIt(char *msg, int num);
void *ack_function(void *arg);
void *send_function(void *arg);
void *application();
void createPacket(struct packet * a, char * msg, int seq);

//global flags
int wsize = 3; //our window size which gets sent to client.
int sent = 0;
int rcvd_num = 0;

struct packet* buffer[BUFF_SIZE];
int curr_pos = 0;

int sock;                        /* Socket */
struct sockaddr_in echoClntAddr; /* Requestor address */
char echoBuffer[ECHOMAX];        /* Buffer for echo string */
int recvMsgSize;                 /* Size of received message */
unsigned int cliAddrLen;         /* Length of incoming message */

void DieWithError(char *errorMessage);  /* External error handling function */

int main(int argc, char *argv[])
{
    struct sockaddr_in echoServAddr; /* Local address */
    unsigned short echoServPort;     /* Server port */
    pthread_t a_thread, b_thread, c_thread;
    void *thread_result;
    int res;
    int firstTime = 1;

    if (argc != 2)         /* Test for correct number of parameters */
    {
        fprintf(stderr,"Usage:  %s <UDP SERVER PORT>\n", argv[0]);
        exit(1);
    }

    echoServPort = atoi(argv[1]);  /* First arg:  local port */

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        DieWithError("socket() failed");
        
    struct timeval tv;
    
    tv.tv_sec = 3;  /* 30 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    /*
     You can use the setsockopt function to set a timeout on receive operations:
     
     
     SO_RCVTIMEO
     
     Sets the timeout value that specifies the maximum amount of time an input 
     function waits until it completes. It accepts a timeval structure with 
     the number of seconds and microseconds specifying the limit on how long 
     to wait for an input operation to complete. If a receive operation has 
     blocked for this much time without receiving additional data, it shall 
     return with a partial count or errno set to [EAGAIN] or [EWOULDBLOCK] if 
     no data is received. The default for this option is zero, which indicates 
     that a receive operation shall not time out. This option takes a 
     timeval structure. Note that not all implementations allow this option to be set (mac does).
     */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    
    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort);      /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");
    
    /* Set the size of the in-out parameter */
    cliAddrLen = sizeof(echoClntAddr);
    
    //this is the handshake
    char *msg = "Reciever: I got your first message!\n\n";
    int size = strlen(msg);
    int ss;
    
    //get first message
    while (firstTime == 1){
        
        if ((recvMsgSize = recvfrom(sock, echoBuffer, ECHOMAX, 0,
                (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0) //blocks for x seconds
        {
            if (errno == EWOULDBLOCK)  
                printf("still waiting\n");
        }   
        else
        {       
            printf("Made first contact with requester\n\n");
            printIt(echoBuffer,recvMsgSize);
            firstTime = 0;
            struct packet * special_packet = malloc(sizeof(struct packet));
            createPacket(special_packet, msg, -1);
            special_packet->wsize = wsize;
            //now tell it to start sending.
            if ((ss=sendto(sock, (void *) special_packet, sizeof(struct packet), 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr))) < 0){
                DieWithError("sendto() failed");
            }
            printf("%s", "Sent second special segment containing window size information.\n\n");
        }
    }
    
    printf("Decide on window size, etc.\n");
    printf("Now create a two threads to handle sending packets and receive acks from requestor\n");
    
    res = pthread_create(&a_thread, NULL, ack_function, NULL);  //thread for ack reads
    
    if (res != 0) {
        perror("Thread creation failed");
        (EXIT_FAILURE);
    }
    
    res = pthread_create(&b_thread, NULL, sendIt, NULL);  //thread for apacket sends
    if (res != 0) {
        perror("Thread creation failed");
        (EXIT_FAILURE);
    }
    
    res = pthread_create(&c_thread, NULL, application, NULL);
    if (res != 0) {
        perror("Thread creation failed");
        (EXIT_FAILURE);
    }
    
    res = pthread_join(a_thread, &thread_result);
    if (res != 0) {
        perror("Thread join failed");
        exit(EXIT_FAILURE);
    }
    printf("Thread done\n");
    
}

void *ack_function(void *arg) {
    receiveIt();
}

void printIt(char *msg, int num){
    char *p;
    int i=0;
    p = msg;
    while (i < num){
        printf("%c",*(p+i));
        i++;
    }
}

void createPacket(struct packet * a, char * msg, int seq)
{
    int i, len;
    len = strlen(msg);
    for(i = 0; i < len; i ++){
        a->data[i] = msg[i];
    }
    a->data[len] = '\0';
    a->seq = seq;
    
}

void *sendIt(char *received){
    int count = 1; //start at 1 because it's always 1 ahead of received SEQ #.
    char *msg = "ACK\n";;
    int size = strlen(msg);
    int ss;
    
    while(1) {
        while(rcvd_num - sent > 0){ //we received new packets if > 0
            struct packet * newpacket = malloc(sizeof(struct packet));
            createPacket(newpacket, msg, count); //ack for every single packet received. How do we send an ack for every n packet, up to wsize.
                
            if ((ss = sendto(sock, (void *) newpacket, sizeof(struct packet), 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr))) == -1){
                DieWithError("sendto() failed");
            }
                
            printf("ACK with SEQ %d sent\n", count); //this should be in sync with our seq numbers
            count = ((count + 1) % (wsize * 2)); //kind of like a for loop.
            sent++;
        }
    }
}

//just waiting for the messages to come in
void receiveIt()
{
    int expect = 0; //seq #'s start at 0
    int seq_recvd;
    int corrupt;
    int ha;
    ha = sizeof(struct packet);
    char wtf[ha];
    printf("Waiting to receive\n");
    for (;;) /* Run forever */
    {
        /* Block until receive message from a client or time out*/
        if ((recvMsgSize = recvfrom(sock, wtf, sizeof(struct packet), 0,
            (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0)
        {
            /* error: recvfrom() would have timed out */
            if (errno == EWOULDBLOCK)  
                printf("nothing on the line. Waiting for %d\n", expect);
        }   
        else
         {  
            printf("Received: %s with seq # %d\n\n", ((struct packet*)wtf)->data, ((struct packet*)wtf)->seq );
            seq_recvd = ((struct packet*)wtf)->seq;
            corrupt = ((struct packet*)wtf)->corrupt;
            
            if(corrupt > 0) printf("Ew. SEQ %d is a corrupt packet. Throw away.\n", seq_recvd);
            
            // printf("Expected = %d and received = %d", expect, seq_recvd);
            if((expect == seq_recvd || seq_recvd < 0) && corrupt == 0){ //must not be corrupt. Or else discarded.
                //got the DONE packet.
                if(seq_recvd < 0){ 
                    exit(1); 
                }
                 //write it to our file (application)
                expect = ((expect + 1) % (wsize * 2)); 
                rcvd_num++; //signals send + application
                buffer[curr_pos] = ((struct packet*)wtf); //push to buffer
                curr_pos = (curr_pos + 1) % BUFF_SIZE; //update pos
             }
         }
    }
}

//our app which will read
void *application()
{
    int i;
    int j = 0;
    int index = 0;
    FILE *file; 
    file = fopen("file.txt","a+");
    
    while(1){
        while(index < rcvd_num){
            j = index % BUFF_SIZE;
            //printf("writing seq %d to file with %s\n", j, buffer[j]->data);
            fwrite(buffer[j]->data, strlen(buffer[j]->data), 1, file);
            fflush(file); //flush immediately
            index++;
        }
    }
    
    fclose(file);
}

