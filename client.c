#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <memory.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdarg.h>
#include "gbnpacket.c"

#define TIMEOUT 3
#define RETRIES 1000
#define PORTNUM 6969
#define WINDOWGBN 5
#define WINDOWSAW 1

// globals
int sequence_number = 1;
int send_flag = 1;
int retries = 0;

//prototypes
struct gbnpacket next_packet(FILE *file); 
void shift_by_one(struct gbnpacket *window, size_t window_size, FILE *file);
void catch_alarm(int sig);

// Usage: 
int main(int argc, char *argv[]) {
	signal(SIGALRM, catch_alarm);
	//ensure we have the right number of args
	if (argc != 4) {
		printf("Usage: %s <server name> <xfer file> <gbn|saw> \n",
			argv[0]);
		exit(1);
	}

	FILE *file;
	//Attempt to open file, and ensure it's valid
	file = fopen(argv[2], "rb");
	if (file == NULL) {
		printf("file %s not valid!\n",
			argv[2]);
		exit(1);
	}

	int window_size;
	//Determine whether we're using GBN or SAW and swet window_size
	if (strcmp(argv[3],"gbn") != 0 && strcmp(argv[3],"saw") != 0) {
		printf("Error: neither GBN nor SAW specified\n");
		exit(1);
	}
	if (strcmp(argv[3], "gbn") == 0) {
		window_size = WINDOWGBN;
	} 
	if (strcmp(argv[3], "saw") == 0) {
		window_size = WINDOWSAW;
	}

	struct hostent *hp;
	//The name of the server we're sending to is given as argv[1], it can 
	//be an ip address, or the colloquial name of the server (ie blanca or shavano)
	if ((hp = gethostbyname(argv[1])) == 0 ) {
		printf("Invalid or unknown host\n");
		exit(1);
	}

	//Create our socket
	int sk;
	if ((sk = socket( PF_INET, SOCK_DGRAM, 0)) < 0) {
		printf("Problem creating socket");
		exit(1);
	}

	struct sockaddr_in server;
	server.sin_family = AF_INET;
	
	memcpy( &server.sin_addr.s_addr, hp->h_addr, hp->h_length );
	
	server.sin_port = htons(PORTNUM);

	struct gbnpacket window[window_size];
	
	int i;
	
	//Fill the window
	for(i=0; i<window_size; i++) {
		window[i] = next_packet(file);
	}

	while( (window[0].type != 0) ) {
	
		if( send_flag > 0 ) {
			send_flag = 0;

			//send all packets in the window
			for(i=0; i<window_size; i++) {
				int sent_len = sendto(
					sk,
					&window[i],
					sizeof(window[0]),
					0,
					(struct sockaddr *) &server,
					sizeof(server));
				if(sent_len != sizeof(window[0])) {
					perror("sent different number of bytes than expected!!!i\n");
					exit(1);
				}
			}
		}
		// Wait for responses
		struct sockaddr_in from_server;
		int from_server_len = sizeof(from_server);
		struct gbnpacket recd;
		alarm(TIMEOUT);
		int response_len;
		while( 
			(response_len = recvfrom(
				sk,
				&recd,
				sizeof(recd),
				0,
				(struct sockaddr *) &from_server,
				&from_server_len)) < 0) {
			if(errno == EINTR) {
				if (retries < RETRIES) {
					printf("Timed out, %d more retries...\n", retries);
					break;
				}else {
					printf("Timed out, exiting :(\n");
					exit(1);	
				}
			}else {
				printf("recvfrom failed :(");
				exit(1);
			}
		}
		
		//got response, cancel timeout
		if(response_len) {
			alarm(0);//cancel alarm
	
			// if we get back the first packet we sent, shift the window by 1 and send the next guy
			if (recd.sequence_number == window[0].sequence_number) {
				shift_by_one(window, window_size, file);
				int sent_len = sendto(
					sk,
					&window[window_size-1],
					sizeof(window[0]),
					0,
					(struct sockaddr *) &server,
					sizeof(server));
				if(sent_len != sizeof(window[0])) {
					perror("sent different number of bytes than expected!!!i\n");
					exit(1);
				}
			}else {
				send_flag = 1;
			}
		}
	}
	fclose(file);	

	return 0;
}

void catch_alarm(int sig) {
	retries++;
	send_flag=1;
}

/*
 * This is the function that is called when creating our window and when we've
 * recieved a good ACK back. This function gets rid of window[0], and shifts 
 * all other entries by one to the left. Then the last element in window is
 * filled with the new packet.
 */
void shift_by_one(struct gbnpacket *window, size_t window_size, FILE *file) {
	if(window_size <= 1) { /* implies SAW */
		window[0] = next_packet(file);
	}
	else { /* window_size > 1 implies GBN */
		int i;
		for( i=0; i < window_size-1; i++) {
			window[i] = window[i+1];
		}
		window[window_size-1] = next_packet(file);
	}
}


struct gbnpacket next_packet(FILE *file) {
	struct gbnpacket packet;
	packet.type = 1;
	packet.sequence_number = sequence_number;
	sequence_number++;
	//Now read the data into the packet
	size_t size = fread( packet.data, sizeof(packet.data[0]), PACKET_SIZE, file);
	packet.length = size;

	if (size <= 0) {
		packet.type = 0;
		memset( packet.data, 0, PACKET_SIZE );
	}

	return packet;
}


















