#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define FILEPATH "textref.txt"
#define MAXBUFSIZE 1400
#define MAXSEND 20
#define false 0
#define true 1

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

char *Sequence_check;
int search_pointer;
char *data;
struct stat stat_buffer;

struct timespec start_time , stop_time;
double transaction_time;

typedef struct datagram {
    uint32_t seqNum;                /* store sequence # */
    uint32_t ts;                    /* time stamp */
    uint32_t dataSize;              /* datasize , normally it will be 1460*/
    char buf[MAXBUFSIZE];            /* Data buffer */
    int retransmit;
}datagram;

pthread_t thrd[1];
int datarecv = 0;
int filesize;
int sockfd;
struct sockaddr_in serv_addr;

int Packets_Arrived = 0 , sequences_total = 0;

int CheckMissingSequences(){
	int i;
	if( Sequence_check == NULL )
		return -1;

	for ( i = search_pointer; i <= sequences_total ; i++)
	{
		if (Sequence_check[i] == 0 )
		{
			if (i != sequences_total)
				search_pointer= i + 1;
			else
			        search_pointer = 0;
				
            		return i;
		}
	}

	search_pointer = 0;
	return -1;
    
}

void error(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(0);
}

void* thread_routine(void* arg){
    int n = 0;

    int retransmission_index;
    fprintf(stdout, "Checking the sequences as we receive the file..........\n");
    while(1)
    {
        if(Packets_Arrived == sequences_total+1 ) {
            fprintf(stdout, "There is no missing data\n");
            pthread_exit(0);
        }

        usleep(100);

        pthread_mutex_lock(&lock);
        retransmission_index = CheckMissingSequences();
        pthread_mutex_unlock(&lock);

        if(retransmission_index >= 0 && retransmission_index <= sequences_total){

            n = sendto(sockfd, &retransmission_index ,sizeof(int),0,(struct sockaddr *)&serv_addr,sizeof(serv_addr));
            
            if (n < 0)
                error("Error in sending missing acknowledgements\n");
        }
        
    }
}

int update_recv_seq(char **arrayNode , int seqNum){
    
	if( seqNum >= 0 && seqNum <=sequences_total){
       		 if( Sequence_check[seqNum] == 0 ){
			Sequence_check[seqNum] = 1;
			return 1;
		} 
	}
    return 0;
}

void Initialize_Server(int portno){

    bzero((char *) &serv_addr, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
}

void Make_ChangesTo_Socket(long int sndsize){

    if(setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char *)&sndsize, (long int)sizeof(sndsize)) == -1)
    {
        fprintf(stdout, "error with setsocket: Didnt make changes to sending socket");
    }
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char *)&sndsize, (long int)sizeof(sndsize)) == -1)
    {
        fprintf(stdout, "error with setsocket: Didnt make changes to Receiving socket");
        
    }
}
int Calculate_Sequences(int Receive_from_length){

    int n = recvfrom(sockfd,&filesize,sizeof(filesize),0,(struct sockaddr *)&serv_addr,&Receive_from_length);
    
    sequences_total = ceil(filesize / MAXBUFSIZE);
    fprintf(stdout, "Total Sequences :: %d\n\n",sequences_total);
	
    return n;
}


int main(int argc, char *argv[])
{
    
    socklen_t Receive_from_length;
    FILE *fp;
    datagram recvDG;


    int n, portno, i;
    int errno = 0 ;
    int ack = -1;
    int datasize = 0;



    
    if (argc != 2) error("Usage: [Server Executable] [Port Number]");

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    portno = atoi(argv[1]);
    Initialize_Server(portno);

    
    long int sndsize = 25000000;
    Make_ChangesTo_Socket(sndsize);

    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    
    Receive_from_length = sizeof(struct sockaddr_in);

    n = Calculate_Sequences(Receive_from_length);

    fp = fopen(FILEPATH , "w+");
    
    if((errno = pthread_create(&thrd[0], 0, thread_routine , (void*)0 ))){
        fprintf(stderr, "pthread_create[0] %s\n",strerror(errno));
        pthread_exit(0);
    }
    
    Sequence_check = calloc(sequences_total , sizeof(char));

    
    if(clock_gettime(CLOCK_REALTIME , &start_time) == -1){
        error("clock get time");
    }
    
    while (1)
    {
        
        n = recvfrom(sockfd,&recvDG,sizeof(datagram),0,(struct sockaddr *)&serv_addr,&Receive_from_length);		
        if (n < 0)
            error("recvfrom: Not receiving properly");
        if( n == sizeof(int))
            continue;

        pthread_mutex_lock(&lock);

        if(update_recv_seq( &Sequence_check , recvDG.seqNum)){
			
            fseek( fp , MAXBUFSIZE*recvDG.seqNum , SEEK_SET  );
            fwrite(&recvDG.buf , recvDG.dataSize , 1 , fp);
            fflush(fp);
            Packets_Arrived++;
        }

        pthread_mutex_unlock(&lock);

        if(Packets_Arrived == sequences_total+1 ){
        fprintf(stdout, "The whole 1GB file has been received with reliability!\n");
	 
        for(i=0 ; i<MAXSEND ; i++){
            //printf("sending ack\n");
            n = sendto(sockfd, &ack, sizeof(int), 0,(struct sockaddr *) &serv_addr,sizeof(serv_addr));
            if (n < 0)
                error("sendto");
       }
        
        break;
        }
    }

	pthread_join(thrd[0], 0);

	fclose(fp);


	if(clock_gettime(CLOCK_REALTIME, &stop_time) == -1){
   	 error("clock get time stop_time");
	}
	transaction_time = (stop_time.tv_sec - start_time.tv_sec) + (double)(stop_time.tv_nsec - start_time.tv_nsec)/pow(10,9);

	printf("Time for transfering the whole file: %f sec\n\n",transaction_time);

	munmap(data, stat_buffer.st_size);

	close(sockfd);
	return 0;
}
