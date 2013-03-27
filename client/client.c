#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <semaphore.h>

#define BUFF_SIZE 10
#define TRUE 1
#define FALSE 0

struct packet {
    char data[100];
    int seq;
    int corrupt;
    int wsize;
};

//set network globals
int sockfd;
struct addrinfo hints, *servinfo, *p;

//set GBN globals
struct packet* buffer[BUFF_SIZE]; //buffer for this simulated router
int wsize, left, right; //used to determine can_send and LEFT/RIGHT boundaries for tracking current window.
int can_send; //incremented by receiver. Opens up room for application.
int num_in_buffer = 0; //incremented by application to signal sending.
int sent_unacked; //limits # packets sent by sender until can_send increases (from receiver -> application).
int outstanding = 0;

int just_sent = FALSE;

int resend = 0; //flag for resending packets in current window.

void *sendPacket();
void *receivePacket();
void *application();
void initializeHandshake();

int main(int argc, char *argv[])
{
    int rv;
    int numbytes;
    pthread_t thread1, thread2, thread3;
    int  iret1, iret2, iret3;
    
    if (argc != 3) {
        fprintf(stderr,"usage: client hostname port\n");
        exit(1);
    }
    
    //set timer info
    struct timeval tv;
    tv.tv_sec = 10;  /* 30 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }
    
    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("talker: socket");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to bind socket\n");
        return 2;
    }
    
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    //inititalize the handshake.
    initializeHandshake();
    
    iret1 = pthread_create( &thread1, NULL, sendPacket, NULL);
    iret2 = pthread_create( &thread2, NULL, receivePacket, NULL);
    iret3 = pthread_create( &thread3, NULL, application, NULL);
    
    freeaddrinfo(servinfo);
    pthread_join( thread1, NULL);
    pthread_join( thread2, NULL); 
    pthread_join( thread3, NULL);

    printf("Thread 1 returns: %d\n", iret1);
    printf("Thread 2 returns: %d\n", iret2);
    printf("Thread 3 returns: %d\n", iret3);
    
    close(sockfd);
    return 0;
}

int randomNumber(int upperBound){  //this is the simple rn generator
    int j;
    j=1+(rand() % upperBound);
    return j;
}

void initializeHandshake(){
    int numbytes;
    int firstTime = 1;
    char *msg = "Lets do this!!!\n";    
    int struct_size;
    struct_size = sizeof(struct packet);
    char buf[struct_size];      
        
    if ((numbytes = sendto(sockfd, msg, strlen(msg), 0,
        p->ai_addr, p->ai_addrlen)) == -1) {
            perror("talker: sendto");
            exit(1);
    }
    
    printf("%s", "Sent first contact.\n");
        
    while (firstTime == 1){ 
        
        if((numbytes = recv(sockfd, buf, sizeof(struct packet), 0)) < 0) {
            if (errno == EWOULDBLOCK)  {//timeout
                printf("Waiting for handshake...!\n\n");
            }
        }
        else
        {       
            printf("Got first special ack from our server buddy\n");
            printf("msg: %s with window size of %d\n", ((struct packet*)buf)->data,  ((struct packet*)buf)->wsize);
            
            //now set the wsize
            wsize = ((struct packet*)buf)->wsize;
            can_send = wsize;
            //set left and right bounds of window
            left = 0;
            right = (wsize - 1) % BUFF_SIZE;
            
            firstTime = 0;
        }
    }

}

//calculates how mnay sspaces free up
int slideDistance(int seq)
{
    int num = 0;
    num = buffer[left]->seq;
    if(seq < num)
        return ((wsize * 2) - num) + seq;
    else
        return (seq - buffer[left]->seq);
}

//this is cumulative. So if I get a seq # of 5, I should move my can_sendability accordingly.
void *receivePacket()
{
    int size_of_packet;
    size_of_packet = sizeof(struct packet);
    char rcvBuffer[size_of_packet];
    //this will signal the sender to send more
    int numbytes;
    int seq_num;
    
    printf("Waiting to receive acks\n\n");
    while(1) {
        while(just_sent == FALSE); //hang until something is sent.
    
        if((numbytes = recv(sockfd, rcvBuffer, sizeof(struct packet), 0)) < 0) { 
            if (errno == EWOULDBLOCK) {//timeout
                printf("Still waiting for ack...!\n\n");
                resend = TRUE;
                //set a flag that will trigger resend
            }
        }else{
            seq_num = ((struct packet*)rcvBuffer)->seq;
            int slide = slideDistance(seq_num); //how many more can we send?
            printf("ACK %d. Slide by %d\n", seq_num, slide);
            
            outstanding--; //number sent but unacked
            can_send += slide; //transport can take n more. Notifies application.
            
            //move window
            left = (left + slide) % BUFF_SIZE; 
            right = (right + slide) % BUFF_SIZE;
            printf("[Left %d, Right %d]\n", left, right);
        }

    }
}

void resendPackets(int left, int right)
{
    int rand;
    int numbytes;
    int i;
    rand = randomNumber(100);
    
    for(i = left; i != (right + 1) % BUFF_SIZE; i = (i + 1) % BUFF_SIZE){
        if(rand > 10){
            rand = randomNumber(100);
            if(rand < 5) {
                printf("Setting corrupt bit\n");
                buffer[i]->corrupt = 1;
            } else {
                printf("Normal packet\n");
                buffer[i]->corrupt = 0;
            }
            if ((numbytes = sendto(sockfd, (void *) buffer[i], sizeof(struct packet), 0,
                    p->ai_addr, p->ai_addrlen)) == -1) {
                    perror("talker: sendto");
                    exit(1);
            }
        }
                
        printf("resent: %s with seq #%d\n\n", buffer[i]->data, buffer[i]->seq);         
    }
    
    resend = FALSE; //set flag to false again
}

//do nothing for now.
void *sendPacket()
{
    printf("Sender started.\n\n"); 
    int i;
    int numbytes;
    int curr_pos = 0;
    
    //simulating corruption
    int rand;
    
    while(1){
        //flag set by receiver to resend packets currently in window.
        if(resend){
            resendPackets(left, right);
        }
        
        //sends anytime something gets pushed onto our buffer
        if((num_in_buffer - sent_unacked) > 0){
            
            /*
            * rand < 10, no send.
            * rand >= 10, gen another rand. If < 5, set corrupt to 1. Else, 0.
            */
            rand = randomNumber(100);
            printf("RAND # %d\n", rand);
            /*if(rand < 10) 
                printf("Do not send packet\n");
            else{
                rand = randomNumber(100);
                if(rand < 5) printf("Setting corrupt bit\n");
                else printf("normal packet\n");
            }*/
            
            sleep(1); //delay
            if(buffer[curr_pos]->seq < 0){ //Wait for done packet. if we hit done, halt and wait.
                if(outstanding == 0){ //dont send until we have none outstanding.
                    //finish this.
                    if(rand > 10){
                        rand = randomNumber(100);
                        if(rand < 5) {
                            printf("Setting corrupt bit\n");
                            buffer[curr_pos]->corrupt = 1;
                        } else {
                            printf("Normal packet\n");
                            buffer[curr_pos]->corrupt = 0;
                        }
                        
                        if ((numbytes = sendto(sockfd, (void *) buffer[curr_pos], sizeof(struct packet), 0,
                            p->ai_addr, p->ai_addrlen)) == -1) {
                            perror("talker: sendto");
                            exit(1);
                        }
                        printf("Just sent our death pill with left = %d curr post = %d. Peace.\n\n", left, curr_pos);
                        exit(1); //kill program
                    }else{
                        printf("Skipping packet\n");
                    }
                }
            }else{
                if(rand > 10){
                    rand = randomNumber(100);
                    if(rand < 5){ 
                        printf("Setting corrupt bit\n");
                        buffer[curr_pos]->corrupt = 1;
                    } else { 
                        printf("Normal packet\n");
                        buffer[curr_pos]->corrupt = 0;
                    }
                    if ((numbytes = sendto(sockfd, (void *) buffer[curr_pos], sizeof(struct packet), 0,
                        p->ai_addr, p->ai_addrlen)) == -1) {
                        perror("talker: sendto");
                        exit(1);
                    }
                } else{
                    printf("Skipping packet\n");
                }
                
                printf("SENT: %s with seq # %d\n\n", buffer[curr_pos]->data, buffer[curr_pos]->seq);
                just_sent = TRUE; //this flag will allow receiver to start timer again.
                
                curr_pos = ((curr_pos + 1) % BUFF_SIZE);
                sent_unacked++; //increment sent but unacked
                outstanding++;
                //printf("Number unacked %d\n", outstanding);   
            }
        }
        
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

//deal with its communication with sender later.
void *application()
{
    FILE *ptr_file;
    char buf[1000];
    ptr_file = fopen("reluctance.txt","r");
    int seq = 0;
    int curr_index = 0;
    int sent = 0;
    
    if (!ptr_file){
        perror("opening file failed");
        exit(1);
    }
    
    printf("Application: I'm reading to start sending.\n\n");
    while (fgets(buf, 1000, ptr_file) != NULL){ //read til new line
        struct packet * new_packet = malloc(sizeof(struct packet));
        createPacket(new_packet, buf, seq);
        buffer[curr_index] = new_packet;
        
        sent++;
        num_in_buffer++; //number in the queue. This tells the sender to start sending.
        curr_index = (curr_index + 1) % BUFF_SIZE;
        seq = ((seq + 1) % (wsize * 2)); //update seq #
        
        while(sent >= can_send); //hang until receiver tells us that we can go
    }
    
    //send DONE packet
    char * g = "Client: DONE!\n";
    struct packet * newpacket = malloc(sizeof(struct packet));
    createPacket(newpacket, g , -1);
    buffer[curr_index] = newpacket;
    sent++;
    num_in_buffer++;
}


