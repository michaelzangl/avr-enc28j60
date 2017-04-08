/*
 * tcpip.h
 *
 *  Created on: 29.10.2011
 *      Author: michael
 */

#ifndef TCPIP_H_
#define TCPIP_H_

#include <stdint.h>

#define PROTOCOL_TCP 0x06
#define TCP_MAX_CHANNELS 10
#define TCP_MAX_APPS 5
//counter value to set after every reception.
#define TCP_TIMEOUT 100
// Counter value at which a keep-alive is sent. low = later.
#define TCP_WARNING 20

typedef union {
	struct {
		uint8_t mac1, mac2, mac3, mac4, mac5, mac6;
	};
	uint8_t bytes[6];
} MacAddress;

typedef struct {
	MacAddress destination;
	MacAddress source;
	uint8_t typeh;
	uint8_t typel;
} EthernetHeader;

typedef struct {
	uint8_t addr1;
	uint8_t addr2;
	uint8_t addr3;
	uint8_t addr4;
} IpAddress;

typedef struct {
	uint8_t headerlength; // + versionid
	uint8_t ds_field;
	uint8_t lengthh;
	uint8_t lengthl;
	uint8_t identificationh;
	uint8_t identificationl;
	uint8_t fragmentoffset1; // includes flags
	uint8_t fragmentoffset2;
	uint8_t ttl;
	uint8_t protocol;
	uint8_t checksumh;
	uint8_t checksuml;
	IpAddress source;
	IpAddress destination;
} IPHeader;

typedef struct {
	uint8_t porth;
	uint8_t portl;
} TCPPort;

typedef struct {
	uint8_t part1;
	uint8_t part2;
	uint8_t part3;
	uint8_t part4;
} SequenceNumber;

#define TCP_FLAG_SYN 1
#define TCP_FLAG_FIN 0
#define TCP_FLAG_PSH 3
#define TCP_FLAG_ACK 4
#define TCP_FLAG_RST 2

typedef struct {
	TCPPort source;
	TCPPort destination;
	SequenceNumber seqenceNumber;
	SequenceNumber ackNumber;
	uint8_t flagsh;
	uint8_t flagsl;
	uint8_t widowsizeh;
	uint8_t widowsizel;

	uint8_t checksumh;
	uint8_t checksuml;
	uint8_t urgent1;
	uint8_t urgent2;
} TCPHeader;

typedef struct {
	uint8_t hardwaretypeh;
	uint8_t hardwaretypel;
	uint8_t protocolh;
	uint8_t protocoll;
	uint8_t hardwaresize;
	uint8_t protocolsize;
	uint8_t opcodeh;
	uint8_t opcodel;
	MacAddress senderMac;
	IpAddress senderIp;
	MacAddress targetMac;
	IpAddress targetIp;

} ArpPackage;

typedef struct TCPApp TCPApp;

typedef struct {
	TCPApp *app;
	// timeout counter.
	uint16_t timeRemaining;
	uint32_t seqnumber; // the next sequence number to send.
	uint32_t acknumber; // The next ack number to send. This number is one more than the seq number of the last received package. If the package is a sync ack, it is just the seq of the last package.
	IpAddress ip;
	MacAddress mac;
	uint16_t port;
} TCPChannel;

struct TCPApp {
	uint16_t port;
	/**
	 * connects on a given channel. Channel is in 0..(TCP_MAX_CHANNEL - 1)
	 * Returns 0 if the connection should be aborted,
	 * a pointer to an emtpty tcp channel if it is accepted.
	 *
	 * The returned channel does not have to have the port, ip and sequence
	 * number fields set, they are added automatically.
	 */
	TCPChannel* (*connect)();
	/**
	 * Receives a package.
	 */
	void (*receivePackage)(TCPChannel *channel);
	/**
	 * Called when a given channel is forced to disconnect.
	 */
	void (*disconnect)(TCPChannel *channel);
};

void ethernetPackageReceived();
uint8_t addTcpApp(TCPApp *app);
void initTcpIp();

void sendSimpleAck(TCPChannel *channel);
void sendTcpResponseHeader(TCPChannel *channel, uint8_t flags);
void sendTcpResponse();
void resendTcpResponse(TCPChannel *channel, uint8_t flags);

void finTcpSession(TCPChannel *channel);
void tcpTimeoutDowncount();
void tcpTimeoutPoll();

#endif /* TCPIP_H_ */
