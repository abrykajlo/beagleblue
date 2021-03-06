#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/rfcomm.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include "beagleblue.h"
//this is hard coded on both ends
#define ANDROID_PORT 1
#define GLASS_PORT 2

static char android_send_buffer[BUFFER_SIZE];
static char android_recv_buffer[BUFFER_SIZE];
static char glass_send_buffer[BUFFER_SIZE];
static char glass_recv_buffer[BUFFER_SIZE];


static bool android_is_sending = false;
static bool glass_is_sending = false;

static bool beagleblue_is_done;
static bool beagleblue_is_connected = false;

static pthread_t android_recv_thread_id;
static pthread_t android_send_thread_id;
static pthread_t glass_recv_thread_id;
static pthread_t glass_send_thread_id;

static pthread_mutex_t android_send_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t glass_send_mutex = PTHREAD_MUTEX_INITIALIZER;

static int android_sock = -1, android_client = -1;
static int glass_sock = -1, glass_client = -1;

//consider converting to macro
static void set_bluetooth_mode(uint32_t mode)
{
	int sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	int dev_id = hci_get_route(NULL);

	struct hci_dev_req dr;

	dr.dev_id  = dev_id;
	dr.dev_opt = mode;
	if (ioctl(sock, HCISETSCAN, (unsigned long) &dr) < 0) {
		fprintf(stderr, "Can't set scan mode on hci%d: %s (%d)\n", dev_id, strerror(errno), errno);
    }

    close(sock);
}

//if sockets are initialized close them before this
static void beagleblue_connect(int *sock, int *client, uint8_t channel)
{
	struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
	socklen_t opt = sizeof(rem_addr);
	char buf[20];

	//initialize socket
	*sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

	//bind socket to local bluetooth device
	loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = channel;
    bind(*sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr)); 

    // put socket into listening mode
    listen(*sock, 1);


    // accept one connection
    *client = accept(*sock, (struct sockaddr *)&rem_addr, &opt);

    ba2str( &rem_addr.rc_bdaddr, buf );
    fprintf(stdout, "accepted connection from %s", buf);
    fflush(stdout);
}

static void *android_recv_thread(void *callback)
{
	void (*on_receive)(char *) = callback;
	fd_set fds;
	//init up here
	while(!beagleblue_is_done) {
		memset(android_recv_buffer, 0, BUFFER_SIZE); //clear the buffer

		struct timeval timeout;
		timeout.tv_sec = TIMEOUT_SEC;
		timeout.tv_usec = TIMEOUT_USEC;

		FD_ZERO(&fds);
		FD_SET(android_client, &fds);
		select(FD_SETSIZE, &fds, NULL, NULL, &timeout);

		if (FD_ISSET(android_client, &fds)) {
			recv(android_client, android_recv_buffer, 16, 0);
			on_receive(android_recv_buffer);
		}
	}
}

static void *android_send_thread()
{
	while(!beagleblue_is_done) {
		if (android_is_sending) {
			send(android_client, android_send_buffer, strlen(android_send_buffer), 0);
			android_is_sending = false;
			pthread_mutex_unlock(&android_send_mutex);
		}
	}
}

static void *glass_recv_thread(void *callback)
{
	void (*on_receive)(char *) = callback;
	fd_set fds;
	//init up here
	while(!beagleblue_is_done) {
		memset(glass_recv_buffer, 0, BUFFER_SIZE); //clear the buffer

		struct timeval timeout;
		timeout.tv_sec = TIMEOUT_SEC;
		timeout.tv_usec = TIMEOUT_USEC;

		FD_ZERO(&fds);
		FD_SET(glass_client, &fds);
		select(FD_SETSIZE, &fds, NULL, NULL, &timeout);

		if (FD_ISSET(glass_client, &fds)) {
			recv(glass_client, glass_recv_buffer, BUFFER_SIZE, MSG_DONTWAIT);
			on_receive(glass_recv_buffer);
		}
	}
}

static void *glass_send_thread()
{
	while(!beagleblue_is_done) {
		if (glass_is_sending) {
			send(glass_client, glass_send_buffer, strlen(glass_send_buffer), 0);
			android_is_sending = false;	
			pthread_mutex_unlock(&glass_send_mutex);
		}
	}
}

void beagleblue_init(void (*on_receive)(char *))
{
	beagleblue_is_done = false;
	set_bluetooth_mode(SCAN_INQUIRY | SCAN_PAGE);
	printf("Bluetooth Discoverable\n");
	beagleblue_connect(&android_sock, &android_client, ANDROID_PORT);
	//beagleblue_connect(&glass_sock, &glass_client, GLASS_PORT);
	set_bluetooth_mode(SCAN_DISABLED);
	pthread_create(&android_send_thread_id, NULL, &android_send_thread, NULL);
	pthread_create(&android_recv_thread_id, NULL, &android_recv_thread, on_receive);
	//pthread_create(&glass_send_thread_id, NULL, &glass_send_thread, NULL);
	//pthread_create(&glass_recv_thread_id, NULL, &glass_recv_thread, on_receive);
	return;
}

void beagleblue_exit()
{
	beagleblue_is_done = true;
	return;
}

int beagleblue_glass_send(char *buf)
{
	//gets unlocked inside the glass send thread
	pthread_mutex_lock(&glass_send_mutex);
		memset(glass_send_buffer, 0, BUFFER_SIZE);
		strncpy(glass_send_buffer, buf, BUFFER_SIZE);
		glass_is_sending = true;
		//needs to be thread safe

	return 0;
}

int beagleblue_android_send(char *buf)
{
	//gets unlock inside android send thread
	pthread_mutex_lock(&android_send_mutex);
	memset(android_send_buffer, 0, BUFFER_SIZE);
		strncpy(android_send_buffer, buf, BUFFER_SIZE);
		android_is_sending = true; //modify to be thread safe
	return 0;
}

void beagleblue_join()
{
	pthread_join(glass_recv_thread_id, NULL);
	pthread_join(glass_send_thread_id, NULL);
	pthread_join(android_recv_thread_id, NULL);
	pthread_join(android_send_thread_id, NULL);
}