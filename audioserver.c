#define _XOPEN_SOURCE 500
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include "library.h"
#include "audio.h"
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <time.h>

#define SERVER_PORT 1234
#define MESSAGE_BUFFER 1024
#define SLEEP 1500
#define SEM "/mysem"
#define SAMPLE_RATE 46000
#define SAMPLE_SIZE 16
#define CHANNELS 2
#define CLIENTS 10
#define MISSING_PACKAGES 200
#define TIMEOUT 40000

struct shared_memory *shared_mem_ptr;
sem_t *sem;


struct client_details{

    int fd[2];
    struct sockaddr_in cln_addr;

};

//shared memory 
struct shared_memory{

    struct client_details client[CLIENTS];
};


//create a timestamp
void timestamp(char stime[])
{
    time_t ltime;
    struct tm *result;
   
    ltime = time(NULL);
    if((result=localtime(&ltime)) == NULL){
        perror("Error occured while getting localtime\n");
    }
    sprintf(stime, "%d%d%d%d%d%d:",result->tm_mday, result->tm_mon, result->tm_year, result->tm_hour, result->tm_min, result->tm_sec);
 
}

//add a timestamp to a specific type of message
void create_message(char UDP[]){

    char time[14];
    timestamp(time);
    strncat(UDP, time, 13);
}


int stream_data(char *datafile, int socket_server_fd, socklen_t flen, struct client_details client_ptr){

    struct sockaddr_in cln_tmp = client_ptr.cln_addr;
    struct timeval timeout;
    int data_fd, bytesread, ret_val, nmbrs_fd;
    char buffer[MESSAGE_BUFFER], rcv_message[MESSAGE_BUFFER];
    int sample_rate = SAMPLE_RATE;
    int sample_size = SAMPLE_SIZE;
    int channels = CHANNELS;
    int ack_counter = 1;
    fd_set read_set;
    int package_counter = 0;
    char UDP2[64] = "UP2";
    char UDP3[64] = "UP3";
    char UDP4[64] = "UP4";


    data_fd = aud_readinit(datafile, &sample_rate, &sample_size, &channels);
	if (data_fd < 0){
        printf("failed to open datafile %s, skipping request!\n", datafile);
        create_message(UDP4);
        ret_val = sendto(socket_server_fd, &UDP4, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
        if(ret_val == -1){
            perror("Error occured while sending a message. \n");
            return -1;
        }   
        return -1;
    }
    else{
        create_message(UDP3);
        ret_val = sendto(socket_server_fd, &UDP3, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
        if(ret_val == -1){
            perror("Error occured while sending a message. \n");
            return -1;
        }   
    }
    
    bytesread = read(data_fd, buffer, MESSAGE_BUFFER);
    ret_val = sendto(socket_server_fd, &buffer, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
    if(ret_val == -1){
        perror("Error occured while sending a message. \n");
        return -1;
    }
    
    package_counter++;
    ret_val = usleep(SLEEP);
    if(ret_val < 0){
        perror("Error occured while sleeping\n");
        return -1;
    }

	while (bytesread > 0){
     
            bytesread = read(data_fd, buffer, MESSAGE_BUFFER);
            package_counter++;

            ret_val = sendto(socket_server_fd, &buffer, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
            if(ret_val == -1){
                perror("Error occured while sending a message. \n");
                return -1;
            }

            //create set of descriptors
            FD_ZERO(&read_set);
            FD_SET(client_ptr.fd[0], &read_set); 

	        //set timeout
            timeout.tv_usec = TIMEOUT;

            //wait for incoming message
            nmbrs_fd = select(client_ptr.fd[0]+1, &read_set, NULL, NULL, &timeout);
            if(nmbrs_fd < 0){
                perror("Error occured while selecting.\n");
                return -1;
            }

            else if(FD_ISSET(client_ptr.fd[0], &read_set)){ 

                
                memset(rcv_message, 0, MESSAGE_BUFFER);
                if((read(client_ptr.fd[0], rcv_message, MESSAGE_BUFFER)) == -1){
                   perror("Error occured while reading from the pipe\n");
                }
                
                //verify ack message
                if(rcv_message[0] == 'U' && rcv_message[2] == '3'){
                    ack_counter++;
                }
                //stop streaming when to many packages are missing
                if(ack_counter+MISSING_PACKAGES < package_counter){
                    return -1;
                } 
          
                ret_val = usleep(SLEEP);
                if(ret_val < 0){
                    perror("Error occured while sleeping\n");
                    return -1;
                }
            }  
            else{
                //stop streaming when to many packages are missing
                if(ack_counter+MISSING_PACKAGES < package_counter){
                    perror("Lost connection!\n");
                    return -1;
                } 
            }
    }

    if((close(client_ptr.fd[0])) == -1){
        perror("Error occured while reading from the pipe\n");
    }

    ret_val = printf("Song: %s, PCK_SEND: %d, RCV_ACK: %d\n", datafile, package_counter, ack_counter);
    if(ret_val < 0){
        perror("error printing\n");
    }

    //send streaming finished message
    create_message(UDP2);
    strncat(UDP2, datafile, 47);
    
    ret_val = sendto(socket_server_fd, &UDP2, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
    if(ret_val == -1){
        perror("Error occured while sending a message. \n");
        return -1;
    }   

    ret_val = close(data_fd);
    if(ret_val < 0){
        perror("Error occured while closen data_fd\n");
        return -1;
    }
    
    return 1;

}



//Does the client already has a connection?
int check_clients(struct sockaddr_in cln_tmp){

    for(int i = 0; i < CLIENTS; i++){
        if(shared_mem_ptr->client[i].cln_addr.sin_addr.s_addr == cln_tmp.sin_addr.s_addr){
            return i;
        }
    }
  
    return -1;
}  

//add new client
int add_client(struct sockaddr_in cln_tmp){

    for(int i = 0; i < CLIENTS; i++){
        if(shared_mem_ptr->client[i].cln_addr.sin_addr.s_addr == 0){
             shared_mem_ptr->client[i].cln_addr = cln_tmp;
             if(pipe(shared_mem_ptr->client[i].fd) < 0){
                 perror("pipe error\n");
             }
             return i;
        }
     }

    return -1;
}

void init_struct(){

    for(int i = 0; i < CLIENTS; i++){
        shared_mem_ptr->client[i].cln_addr.sin_addr.s_addr = 0;
    }
}


//delete a client
void delete_client(int client){

    sem_wait(sem);
    shared_mem_ptr->client[client].cln_addr.sin_addr.s_addr = 0;
    sem_post(sem);
}


int main(void){

    struct sockaddr_in srv_addr, cln_tmp;
    int socket_server_fd, ret_val, client, check_client, fd_posix, add_new_client;    
    char rcv_message[MESSAGE_BUFFER];
    socklen_t flen;
    pid_t pid;

    //create semaphore
    sem = sem_open(SEM, O_CREAT, 0777, 1);
    if(sem == SEM_FAILED){
        perror("error opening semaphore\n");
        exit(EXIT_FAILURE);
    }   


    flen = sizeof(struct sockaddr_in);    

    //unlink memory area when it already is linked
	shm_unlink("/posix_shared");

	//create memory area
	fd_posix = shm_open("/posix_shared", O_CREAT | O_RDWR | 0666, 0);
	if(fd_posix < -1){
		perror("fd_posix\n");
		exit(EXIT_FAILURE);
	}

	//set size
	if(ftruncate(fd_posix, sizeof(struct shared_memory)) == -1){
		perror("truncate\n");
		exit(EXIT_FAILURE);
	}
	
	//map pointer to shared memory area;
	if((shared_mem_ptr = mmap(NULL, sizeof(struct shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED, fd_posix, 0)) == MAP_FAILED){
		perror("mapping\n");
		exit(EXIT_FAILURE);
	}

    //Server information
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(SERVER_PORT);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    init_struct();

    socket_server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(socket_server_fd == -1){
        perror("Error creating socket_server.\n");
        exit(EXIT_FAILURE);
    }
    
    ret_val = bind(socket_server_fd, (struct sockaddr *) &srv_addr, sizeof(struct sockaddr_in));
    if(ret_val == -1){
        perror("Error binding socket. \n");
        exit(EXIT_FAILURE);
    }    

    ret_val = printf("Server is up and running!\n");
    if(ret_val < 0){
        perror("Couldn't print: Server is up and running!\n");
        exit(EXIT_FAILURE);
    }

    for(;;){

        //rcv messages from all the clients
        ret_val = recvfrom(socket_server_fd, &rcv_message, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, &flen);
        if(ret_val == -1){
            perror("Error occured while receiving a message. \n");
        }
        sem_wait(sem);
        //check in shared memory if it is a new connection or not
        check_client = check_clients(cln_tmp);
        if(check_client == -1){
            add_new_client = add_client(cln_tmp);
        }
        sem_post(sem);

        //create new child process and start streaming
        if(check_client == -1 && rcv_message[0] == 'U' && rcv_message[2] == '1' && add_new_client != -1){
            
            pid = fork();
            if(pid < 0){
                perror("Error creating child process!\n");
            }
            if(pid == 0){

                sem_post(sem);
                client = check_clients(cln_tmp);
                sem_wait(sem);
                
                const char delimiter[] = ":";
                char *token;
                char *running = strdupa(rcv_message);
                token = strsep(&running, delimiter);
                token = strsep(&running, delimiter);

                ret_val = stream_data(token, socket_server_fd, flen, shared_mem_ptr->client[client]);
                if(ret_val < 0){
                    sleep(2);
                    delete_client(client);
                    perror("Error occured while streaming!\n");
                    exit(EXIT_FAILURE);
                }
                else{
                    sleep(2);
                    delete_client(client);
                    exit(EXIT_SUCCESS);
                }
        
            }
        }
        //send message to child processes via a pipe
        else if(rcv_message[0] == 'U' && rcv_message[2] == '3'){
                if((write(shared_mem_ptr->client[check_client].fd[1], rcv_message, MESSAGE_BUFFER)) == -1){
                     perror("Error occured while writing in the pipe\n");
                
            }
        }
        //ignore package if necessary
        else{
            char UDP4[64] = "UP4";
            create_message(UDP4);
            ret_val = sendto(socket_server_fd, &UDP4, MESSAGE_BUFFER, 0, (struct sockaddr*) &cln_tmp, flen);
            if(ret_val == -1){
                perror("Error occured while sending a message. \n");
                return -1;
            }   
            ret_val = printf("Package ignored: %s\n", rcv_message);
            if(ret_val < 0){
                perror("Couldn't print: Package ignored.. \n");
            }
        }
    }

    ret_val = close(socket_server_fd);
    if(ret_val == -1){
        perror("Error occured during socket_server closing. \n");
        exit(EXIT_FAILURE);
    }

    ret_val = sem_close(sem);
    if(ret_val == -1){
        perror("Error occured during sem closing. \n");
        exit(EXIT_FAILURE);
    }

    ret_val = close(fd_posix);
    if(ret_val == -1){
        perror("Error occured during shared memory closing. \n");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
