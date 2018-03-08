#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

static int sock;
static struct sockaddr_in addr[3];

static int lte_dsock_init(int port, int chan)
{
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		fprintf(stderr, "Socket failed\n");
		return -1;
	}

	addr[chan].sin_family = AF_INET;
	addr[chan].sin_port = htons(port);

	if (inet_aton("localhost", (struct in_addr *) &addr[chan].sin_addr) < 0) {
		fprintf(stderr, "Address failed\n");
		return -1;
	}

	return 0;
}

int lte_dsock_send(float *data, int len, int chan)
{
	int rc;

	uint8_t *hack = (uint8_t *) data;

	*hack = chan;

	rc = sendto(sock, data, 2 * len * sizeof(float), 0,
		    (const struct sockaddr *) &addr[0], sizeof(addr));
	if (rc < 0) {
		fprintf(stderr, "Send error\n");
		return -1;
	}

	return 0;
}

int lte_dsock_spectro(float *data, int len, int chan)
{
	int rc, _chan;

	if (!chan)
		_chan = 1;
	else
		_chan = 2;

	uint8_t *hack = (uint8_t *) data;

	*hack = 0;

	rc = sendto(sock, data, 2 * len * sizeof(float), 0,
		    (const struct sockaddr *) &addr[_chan], sizeof(addr));
	if (rc < 0) {
		fprintf(stderr, "Send error\n");
		return -1;
	}

	return 0;
}

static void __attribute__((constructor)) init_sockets()
{
	lte_dsock_init(8888, 0);
	lte_dsock_init(9999, 1);
	lte_dsock_init(7777, 2);
}
