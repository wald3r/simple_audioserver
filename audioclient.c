#define _GNU_SOURCE       
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <netdb.h>
#include <string.h>
#include <time.h>
#include "library.h"
#include "audio.h"

#define SERVER_PORT 1234
#define MESSAGE_BUFFER 1024
#define SAMPLE_RATE 48000
#define SAMPLE_SIZE 16
#define CHANNELS 2
#define TIMEOUT 5
#define SLEEP 3000

void timestamp(char stime[])
{
    time_t ltime;
    struct tm *result;
   
    ltime = time(NULL);
    result=localtime(&ltime);
    sprintf(stime, "%d%d%d%d%d%d:",result->tm_mday, result->tm_mon, result->tm_year, result->tm_hour, result->tm_min, result->tm_sec);
 
}


void create_message(char UDP[]){

    char time[14];
    timestamp(time);
    strncat(UDP, time, 13);

}



int stream_data(int client_socket_fd, int audio_fd, struct sockaddr_in srv_addr, socklen_t flen){

    int nmbrs_fd, ret_val;
    fd_set read_set;
    int package_counter = 0;
    int send_ack = 0;
    char rcv_message[MESSAGE_BUFFER];
    struct timeval timeout;


    for(;;){

        //create set of descriptors
        FD_ZERO(&read_set);
        FD_SET(client_socket_fd, &read_set); 

	    //set timeout
        timeout.tv_sec = TIMEOUT;

        //wait for incoming message
        nmbrs_fd = select(client_socket_fd+1, &read_set, NULL, NULL, &timeout);
        if(nmbrs_fd < 0){
            perror("Error occured while selecting.\n");
            return -1;
        }
        else if(FD_ISSET(client_socket_fd, &read_set)){        
            
            ret_val = recvfrom(client_socket_fd, &rcv_message, MESSAGE_BUFFER, 0, (struct sockaddr*) &srv_addr, &flen);
            if(ret_val == -1){
                perror("Error occured while receiving a message. \n");
                return -1;
            }  

            if(rcv_message[2] == '2' && rcv_message[0] == 'U'){
                ret_val = printf("Finished Streaming!\n");
                if(ret_val < 0){
                    perror("Couldn't print: Finished streaming!\n");
                    return -1;
                }     
                break;  
            }

            char UDP3[64] = "UP3";
            create_message(UDP3);
            ret_val = sendto(client_socket_fd, &UDP3, MESSAGE_BUFFER, 0, (struct sockaddr*) &srv_addr, flen);
            if(ret_val == -1){
                perror("Error occured while sending a message. \n");
                return -1;
            }

            send_ack++;
            write(audio_fd, rcv_message, MESSAGE_BUFFER);
            package_counter++;
        }
        else{
            ret_val = printf("Timeout! Connection lost!\n");
            if(ret_val < 0){
                perror("Couldn't print: Timeout! Connection lost!\n");   
                return -1;
            }
            break;
        }
	}

    ret_val = printf("RCV_PCK: %d, PCK_SEND: %d\n", package_counter, send_ack);
    if(ret_val < 0){
        perror("print error\n");
    }
    return 1;
}

int main(int argc, char **argv){

    struct hostent *hostnm;
    struct in_addr *ipaddr;
    struct sockaddr_in srv_addr;
    char rcv_message[MESSAGE_BUFFER];
    char UDP1[64] = "UP1";
    socklen_t flen;
    int ret_val, client_socket_fd, audio_fd, sample_rate, channels, sample_size;

    if(argc != 3){
        ret_val = printf("Usage: ./audioclient <input_host_name_with_no_space_inside_it> <input_file_name_with_no_space_inside_it>/n/n");
        if(ret_val < 0){
            perror("Couldn't print the message: Usage....\n");
            exit(EXIT_FAILURE);
        }
		return EXIT_FAILURE;
    }

    //get ipv4 address
    hostnm = gethostbyname(argv[1]);
    if(hostnm == NULL){
        perror("Error getting hostname!\n");
        exit(EXIT_FAILURE);
    }
    
    //server information
    ipaddr = (struct in_addr*) hostnm->h_addr_list[0];
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(SERVER_PORT);
    srv_addr.sin_addr.s_addr = inet_addr(inet_ntoa(*ipaddr));

    flen = sizeof(struct sockaddr_in);

    client_socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(client_socket_fd == -1){
        perror("Error bind client_socket\n");
        exit(EXIT_FAILURE);
    }

    create_message(UDP1);
    strncat(UDP1, argv[2], 48);

    ret_val = sendto(client_socket_fd, &UDP1, MESSAGE_BUFFER, 0, (struct sockaddr*) &srv_addr, flen);
    if(ret_val == -1){
        perror("Error occured while sending a message. \n");
        exit(EXIT_FAILURE);
    }

    ret_val = recvfrom(client_socket_fd, &rcv_message, MESSAGE_BUFFER, 0, (struct sockaddr*) &srv_addr, &flen);
    if(ret_val == -1){
        perror("Error occured while receiving a message. \n");
        exit(EXIT_FAILURE);
    } 

    if(rcv_message[2] == '4'){
        ret_val = printf("Message got ignored!\n");
        if(ret_val < 0){
            perror("Couldn't print: Message got ignored!\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);

    } 
    else if(rcv_message[2] == '3'){
        ret_val = printf("Song available! Start streaming!\n");
        if(ret_val < 0){
            perror("Couldn't print: Song available!\n");
            exit(EXIT_FAILURE);
        }
    }
    
	sample_size = SAMPLE_SIZE;    
    sample_rate = SAMPLE_RATE;
	channels = CHANNELS;
	
	
	// open output
	audio_fd = aud_writeinit(sample_rate, sample_size, channels);
	if (audio_fd < 0){
		perror("error: unable to open audio output.\n");
		exit(EXIT_FAILURE);
	}
    
    ret_val = stream_data(client_socket_fd, audio_fd, srv_addr, flen);
    if(ret_val == -1){
        perror("Error occoured during streaming process\n");
        exit(EXIT_FAILURE);
    }    

    ret_val = close(audio_fd);
    if(ret_val == -1){
        perror("Error occured while closing audio_fd.\n");
        exit(EXIT_FAILURE);
    }    
    
    ret_val = close(client_socket_fd);
    if(ret_val == -1){
        perror("Error occured while closing the socket.\n");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
