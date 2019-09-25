#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <sys/mman.h>
#include <fcntl.h>

#define PAYLOAD 1400

char SENDFILE[50];

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int filesize = 0;
int errnum= -1;
char *filedata;

struct sockaddr_in server;
struct stat filedatabuf;

int numpackets=0;

struct timespec start, stop;
double totaltime;

typedef struct udp_datagram {
    uint32_t seq_num;
    uint32_t ts;
    uint32_t data_size;
    char buf[PAYLOAD];
    int retransmit;
} udp_datagram;

pthread_t filethread[1];
int sockfd;

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int createSocket(char *hostname, char *portstr) {
  int port;
  struct hostent *serv_host;
  port = atoi(portstr);
  int newsockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (newsockfd < 0)
      error("Error opening socket");

  long int buffersize = 25000000;

  if(setsockopt(newsockfd, SOL_SOCKET, SO_SNDBUF, &buffersize, sizeof(buffersize)) == -1) {
      printf("Error setting setsocket buffer options");
  }

  if(setsockopt(newsockfd, SOL_SOCKET, SO_RCVBUF, &buffersize, sizeof(buffersize)) == -1) {
      printf("Error setting setsocket buffer options");
  }

  serv_host = gethostbyname(hostname);
  if (serv_host == NULL) {
      fprintf(stderr,"Error, no such host\n");
      exit(0);
  }

  server.sin_family = AF_INET;
  bcopy((char *)serv_host->h_addr, (char *)&server.sin_addr.s_addr,serv_host->h_length);
  server.sin_port = htons(port);
  return newsockfd;
}

char *fileopen(char *str){
    int fp;
    char *filedata;

    fp = open(str, O_RDONLY);
    if(fp < 0 ){
        error("Error in opening the file\n");
    }

    if(fstat(fp,&filedatabuf) < 0){
        error("Error while getting file status\n");
    }

    filedata = mmap((caddr_t)0, filedatabuf.st_size , PROT_READ , MAP_SHARED , fp , 0 ) ;

    if(filedata == MAP_FAILED){
        error("Error in mmap\n");
    }

    return filedata;
}

void *threadroutine(void* arg){
    int numpackets, startbyte, lastseq, size_p, n=0;
    udp_datagram sendpacket;
    int packetmiss, errnum;
    socklen_t servlen;
    servlen = sizeof(server);

    while (1) {

        n = recvfrom(sockfd,&packetmiss,sizeof(int),0,(struct sockaddr *)&server,&servlen);
        if(n<0){
            error("Error while receiving message from server\n");
        }
        if( packetmiss < 0){
            if(clock_gettime(CLOCK_REALTIME , &stop) == -1){
                error("Error during getting clock time");
            }
            printf("Server received the whole file!\n");
            pthread_exit(0);
        }
        numpackets =  filesize / PAYLOAD;
        startbyte = filesize % PAYLOAD;

        size_p = PAYLOAD;
        if(startbyte != 0 && numpackets ==  packetmiss)
        {
        	size_p = startbyte;
        }
        pthread_mutex_lock(&lock);

        memcpy(sendpacket.buf , &filedata[packetmiss*PAYLOAD] , size_p );
        pthread_mutex_unlock(&lock);

        sendpacket.seq_num = packetmiss;
        sendpacket.data_size = size_p;
        sendpacket.retransmit = 1;

        n = sendto(sockfd,&sendpacket,sizeof(udp_datagram), 0,(struct sockaddr *) &server,servlen);
        if (n < 0)
            error("Error while sending packets");
    }
}


int main(int argc, char *argv[])
{
    float transaction_time = 0.0;
  	unsigned char buffer[PAYLOAD];
  	unsigned char* chptr = NULL;
  	struct stat filenamebuf;
    int seq = 0 , datasize =0, n;
    socklen_t fromlen;

    FILE* fp = NULL;

    if (argc != 4) {
        fprintf(stderr,"usage %s hostname port SENDFILE\n", argv[0]);
        exit(0);
    }

    sockfd = createSocket(argv[1],argv[2]);

    chptr = memset(buffer, '\0', PAYLOAD);
    if(chptr == NULL) {
      error("Error in memset");
    }

    strcpy(SENDFILE, argv[3]);

    fp = fopen(SENDFILE, "r");
    if(fp == NULL){
      error("Error during opening file");
    }

  	if(stat(SENDFILE,&filenamebuf)==0) {
          filesize=filenamebuf.st_size;
          printf("Size of file being sent (in bytes): %d\n", filesize);
  	}

    n = sendto(sockfd,&filesize,sizeof(filesize), 0,(struct sockaddr *) &server, sizeof(server));
    if (n < 0)
        error("Error while Sending");

    pthread_mutex_lock(&lock);

    filedata = fileopen(SENDFILE);
    datasize = filedatabuf.st_size;

    pthread_mutex_unlock(&lock);

    if((errnum = pthread_create(&filethread[0], 0, threadroutine, (void*)0 ))){
        fprintf(stderr, "pthread_create[0] %s\n",strerror(errnum));
        pthread_exit(0);
    }

    if(clock_gettime(CLOCK_REALTIME , &start) == -1){
        error("Error during getting clock time");
    }

    while (datasize > 0) {
        int chunk, share;
        udp_datagram sendpacket;
        memset(&sendpacket , 0 , sizeof(udp_datagram));

        share = datasize;
        chunk = PAYLOAD;

        if(share - chunk < 0){
            chunk = share;
        } else {
            share = share - chunk;
        }

        pthread_mutex_lock(&lock);
        memcpy(sendpacket.buf, &filedata[seq*PAYLOAD], chunk);
        pthread_mutex_unlock(&lock);

        sendpacket.seq_num = seq;
        sendpacket.data_size = chunk;
        sendpacket.retransmit = 0;
        usleep(100);
        sendto(sockfd , &sendpacket , sizeof(sendpacket), 0, (struct sockaddr *) &server,sizeof(server));
        seq++;
        datasize -= chunk;
    }

    pthread_join(filethread[0], 0);
    transaction_time = (stop.tv_sec - start.tv_sec) + (double)(stop.tv_nsec - start.tv_nsec)/pow(10,9);
    printf("Time taken to send file: %f\n", transaction_time);
    munmap(filedata, filedatabuf.st_size);
    close(sockfd);
    return 0;
}
