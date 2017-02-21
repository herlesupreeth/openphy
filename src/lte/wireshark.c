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
#include <string.h>

static int sock;
static struct sockaddr_in addr[2];

#define MAC_LTE_START_STRING_LEN	7
#define MAC_MAX_LEN			512

/* Radio type */
#define FDD_RADIO 1
#define TDD_RADIO 2

/* Direction */
#define DIRECTION_UPLINK   0
#define DIRECTION_DOWNLINK 1

/* RNTI type */
#define NO_RNTI  0
#define P_RNTI   1
#define RA_RNTI  2
#define C_RNTI   3
#define SI_RNTI  4
#define SPS_RNTI 5
#define M_RNTI   6

#define MAC_LTE_START_STRING		"mac-lte"
#define MAC_LTE_RNTI_TAG		0x02
#define MAC_LTE_UEID_TAG		0x03
#define MAC_LTE_SUBFRAME_TAG		0x04
#define MAC_LTE_PREDEFINED_DATA_TAG	0x05
#define MAC_LTE_RETX_TAG		0x06
#define MAC_LTE_CRC_STATUS_TAG		0x07
#define MAC_LTE_EXT_BSR_SIZES_TAG	0x08
#define MAC_LTE_PAYLOAD_TAG		0x01

struct mac_frame {
	char start[MAC_LTE_START_STRING_LEN];
	uint8_t radio_type;
	uint8_t dir;
	uint8_t rnti_type;
	uint8_t rnti_tag;
	uint16_t rnti;
	uint8_t payload_tag;
	char payload[MAC_MAX_LEN];
} __attribute__((packed));

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

int lte_wireshark_send(uint8_t *data, int len,
		       uint16_t subframe, int si_rnti, uint16_t rnti)
{
	int rc;

	if ((len < 0) || (len > MAC_MAX_LEN))
		return 0;

	struct mac_frame hdr;
	const char *start = MAC_LTE_START_STRING;

	memcpy(hdr.start, start, strlen(start));
	hdr.radio_type = FDD_RADIO;
	hdr.dir = DIRECTION_DOWNLINK;

	if (si_rnti)
		hdr.rnti_type = SI_RNTI;
	else
		hdr.rnti_type = RA_RNTI;

	hdr.rnti_tag = MAC_LTE_RNTI_TAG;
	hdr.rnti = htons(rnti);
	hdr.payload_tag = MAC_LTE_PAYLOAD_TAG;
	memcpy(hdr.payload, data, len);

	rc = sendto(sock, &hdr, sizeof(struct mac_frame) - (MAC_MAX_LEN - len), 0,
		    (const struct sockaddr *) &addr[0], sizeof(addr));
	if (rc < 0) {
		fprintf(stderr, "Send error\n");
		return -1;
	}

	return 0;
}

static void __attribute__((constructor)) init_sockets()
{
	lte_dsock_init(6666, 0);
}
