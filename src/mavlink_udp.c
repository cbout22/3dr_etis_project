/* These headers are for QNX, but should all be standard on unix/linux */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <pthread.h>
#if (defined __QNX__) | (defined __QNXNTO__)
/* QNX specific headers */
#include <unix.h>
#else
/* Linux / MacOS POSIX timer headers */
#include <sys/time.h>
#include <time.h>
#include <arpa/inet.h>
#include <stdbool.h> /* required for the definition of bool in C99 */
#endif

/* This assumes you have the mavlink headers on your include path
or in the same folder as this source file */
#include <mavlink.h>
#include "mavlink_perso_lib.h"

#define BUFFER_LENGTH 2041 // minimum buffer size that can be used with qnx (I don't know why)


void init_mavlink_udp_connect(int* sock, struct sockaddr_in* locAddr, int local_port, struct sockaddr_in* targetAddr, char* target_ip);
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
void* threadReciving (void* arg);
void* threadSending (void* arg);


//Struct of the vehicle
Vehicle vehicle;

//Connect
int sock;
struct sockaddr_in targetAddr;
struct sockaddr_in locAddr;


/**
* Main
*
*
*/
int main(int argc, char* argv[])
{

	char help[] = "--help";


	char target_ip[100];
	int local_port=14550;
	

	//struct sockaddr_in fromAddr;
	uint8_t buf[BUFFER_LENGTH];
	int bytes_sent;
	mavlink_message_t msg;
	uint16_t len;
	int i = 0;

	// Check if --help flag was used
	if ((argc == 2) && (strcmp(argv[1], help) == 0))
	{
		printf("\n");
		printf("\tUsage:\n\n");
		printf("\t");
		printf("%s", argv[0]);
		printf(" <ip address of QGroundControl> [<Listening port>]\n");
		printf("\tDefault for localhost: udp-server 10.1.1.1, it refer to 3DR controller\n\n");
		exit(EXIT_FAILURE);
	}


	// Change the target ip if parameter was given
	strcpy(target_ip, "10.1.1.1");
	if (argc == 2)
	{
		strcpy(target_ip, argv[1]);
	}
	else if (argc == 3)
	{
		strcpy(target_ip, argv[1]);
		local_port = atoi(argv[2]);
	}
	

	//Connection
	init_mavlink_udp_connect(&sock, &locAddr, local_port, &targetAddr, target_ip);


	//Initialization order
	// Sending an heartbeat : mean we are the ground control (cf. param : 255)
	mavlink_msg_heartbeat_pack(255,0,&msg,MAV_TYPE_GCS,MAV_AUTOPILOT_INVALID,MAV_MODE_MANUAL_ARMED,0x0,MAV_STATE_ACTIVE);
	len = mavlink_msg_to_send_buffer(buf, &msg);
	bytes_sent = sendto(sock, buf, len, 0, (struct sockaddr*)&targetAddr, sizeof(struct sockaddr_in));
	if (bytes_sent==-1) {
		perror("Sending Heartbeat");
		exit(EXIT_FAILURE);
	}
	memset(buf, 0, BUFFER_LENGTH);

	// Request param list : expected a MAVLINK_MSG_ID_PARAM_VALUE
	mavlink_msg_param_request_list_pack(255,0,&msg,1,0);
	len = mavlink_msg_to_send_buffer(buf, &msg);
	bytes_sent = sendto(sock, buf, len, 0, (struct sockaddr*)&targetAddr, sizeof(struct sockaddr_in));
	if (bytes_sent==-1) {
		perror("Sending param list");
		exit(EXIT_FAILURE);
	}
	memset(buf, 0, BUFFER_LENGTH);

	// Request protocol version : expected COMMAND_ACK
	mavlink_msg_command_long_pack(255,0,&msg,1,0,MAV_CMD_REQUEST_PROTOCOL_VERSION,4,1,0,0,0,0,0,0);
	len = mavlink_msg_to_send_buffer(buf, &msg);
	bytes_sent = sendto(sock, buf, len, 0, (struct sockaddr*)&targetAddr, sizeof(struct sockaddr_in));
	if (bytes_sent==-1) {
		perror("Sending data stream");
		exit(EXIT_FAILURE);
	}
	memset(buf, 0, BUFFER_LENGTH);
	//End initialization order

	
	pthread_t myThreadReciving;
	pthread_t myThreadSending;

	//Create threads
	pthread_create (&myThreadReciving, NULL, threadReciving, (void*)NULL);
	pthread_create (&myThreadSending, NULL, threadSending, (void*)NULL);

	//Wait threads
	pthread_join (myThreadReciving, NULL);
	pthread_join (myThreadSending, NULL);
	
	close(sock);
	exit(EXIT_SUCCESS);
}


//Receiving message
void* threadReciving (void* arg){
	
	uint8_t buf[BUFFER_LENGTH];
	ssize_t recsize;
	socklen_t fromlen;
	mavlink_channel_t chan = MAVLINK_COMM_0;
	
	while (1){
		memset(buf, 0, BUFFER_LENGTH);
		
		recsize = recvfrom(sock, (void *)buf, BUFFER_LENGTH, 0, (struct sockaddr *)&targetAddr, &fromlen);
		if (recsize > 0)
		{
			// Something received
			mavlink_message_t msg;
			mavlink_status_t status;
			int i;

			pthread_mutex_lock (&mutex);
			for (i = 0; i < recsize; ++i)
			{
				if (mavlink_parse_char(chan, buf[i], &msg, &status))
				{
					// Packet received
					//printf("\nReceived packet: SYS: %d, COMP: %d, LEN: %d, MSG ID: %d\n", msg.sysid, msg.compid, msg.len, msg.msgid);
					//Broadcast message
					if(mavlink_msg_decode_broadcast(msg, &vehicle)==-1){
						//If this is not a broadcast message
						mavlink_msg_decode_answer(msg);
					}
				}
			}
			pthread_mutex_unlock (&mutex);
		}
		
		sleep(1); // Sleep one second
	}
	
	pthread_exit(NULL); /* End of the thread */
}


//Sending message
void* threadSending (void* arg){
	
	uint8_t buf[BUFFER_LENGTH];
	int bytes_sent;
	mavlink_message_t msg;
	uint16_t len;
	
	while(1){
		memset(buf, 0, BUFFER_LENGTH);
		char order;
		do{
			printf("Temporary menu : \n");
			printf("1 : arm motors\n");
			printf("2 : disarm motors\n");
			printf("3 : get vehicle informations\n");
			scanf("%s", &order);
			if(order == '3'){
				pthread_mutex_lock (&mutex);
				mavlink_display_info_vehicle_all(vehicle);
				pthread_mutex_unlock (&mutex);
			}
		}
		while(mavlink_msg_order(order, &msg)==-1);
		len = mavlink_msg_to_send_buffer(buf, &msg);
		bytes_sent = sendto(sock, buf, len, 0, (struct sockaddr*)&targetAddr, sizeof(struct sockaddr_in));
		if (bytes_sent==-1) {
			perror("Sending data stream");
			exit(EXIT_FAILURE);
		}
	
		sleep(1); // Sleep one second
	}
	
	pthread_exit(NULL); /* End of the thread */
}



/**
* To create the connection
*
*
*/
void init_mavlink_udp_connect(int* sock, struct sockaddr_in* locAddr, int local_port, struct sockaddr_in* targetAddr, char* target_ip)
{
	*sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	memset(locAddr, 0, sizeof(*locAddr));
	locAddr->sin_family = AF_INET;
	locAddr->sin_addr.s_addr = INADDR_ANY;
	locAddr->sin_port = htons(local_port);

	if (-1 == bind(*sock,(struct sockaddr *)locAddr, sizeof(struct sockaddr)))
	{
		perror("error bind failed");
		close(*sock);
		exit(EXIT_FAILURE);
	}
	printf("INIT listenning :\nUdpin: 0.0.0.0:%d\n", ntohs(locAddr->sin_port));
	/* Attempt to make it non blocking */
	#if (defined __QNX__) | (defined __QNXNTO__)
	if (fcntl(*sock, F_SETFL, O_NONBLOCK | FASYNC) < 0)
	#else
	if (fcntl(*sock, F_SETFL, O_NONBLOCK | O_ASYNC) < 0)
	#endif

	{
		fprintf(stderr, "error setting nonblocking: %s\n", strerror(errno));
		close(*sock);
		exit(EXIT_FAILURE);
	}

	char buf[256];
	memset(buf,0,256);
	struct sockaddr_in possibleTarget;
  	socklen_t possibleTargetLen = sizeof(possibleTarget);
	while (recvfrom(*sock, buf, sizeof(buf), 0, (struct sockaddr*)(&possibleTarget), &possibleTargetLen)<=0
				|| possibleTarget.sin_addr.s_addr != inet_addr(target_ip)) {
		memset(buf,0,256);
	}

	memset(targetAddr, 0, sizeof(*targetAddr));
	targetAddr->sin_family = AF_INET;
	targetAddr->sin_addr.s_addr = inet_addr(target_ip);
	targetAddr->sin_port = possibleTarget.sin_port;

	printf("INIT target :\nUdpout: %s:%d\n",target_ip,ntohs(targetAddr->sin_port));
	return;
}

