/*
 * tcpip.c
 *
 *  Created on: 29.10.2011
 *      Author: michael
 */

#include "tcpip.h"
#include "enc28j60.h"
#include "config.h"
#include "ipconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG_TCP

#ifdef DEBUG_TCP
#include "usart.h"
#define debugString sendString
#define debugHex sendAsHex
#else
#define debugString(str)
#define debugHex(n)
#endif

#define WINDOW_SIZE 5792

TCPApp *apps[TCP_MAX_APPS];
TCPChannel *channels[TCP_MAX_CHANNELS];

IPHeader incommingIpHeader;
EthernetHeader incommingEthHeader;
TCPHeader incommingTcpHeader;

uint16_t tcpipHeaderStartPointer;
uint16_t tcpHeaderStartPosition;
uint16_t tcpipStartPosition;
uint32_t ipHeaderCecksum; //ip header, recomputed, without length!
uint16_t tcpHeaderPreChecksum;
#define TCP_LENGTH_OFFSET 2
#define TCP_CHECKSUM_OFFSET 10

static uint8_t isBroadcast(MacAddress* address) {
	for (int i = 0; i < 6; i++) {
		if (address->bytes[i] != 0xff) {
			return 0;
		}
	}
	return 1;
}

static uint8_t isMyMac(MacAddress* address) {
	return address->mac1 == MY_MAC_1 && address->mac2 == MY_MAC_2
			&& address->mac3 == MY_MAC_2 && address->mac4 == MY_MAC_4
			&& address->mac5 == MY_MAC_5 && address->mac6 == MY_MAC_6;
}


static void setToMyMac(MacAddress *address) {
	address->mac1 = MY_MAC_1;
	address->mac2 = MY_MAC_2;
	address->mac3 = MY_MAC_3;
	address->mac4 = MY_MAC_4;
	address->mac5 = MY_MAC_5;
	address->mac6 = MY_MAC_6;
}


/* ======================== TCP =========================== */
void initTcpIp() {
	for (int i = 0; i < TCP_MAX_APPS; i++) {
		apps[i] = 0;
	}
	for (int i = 0; i < TCP_MAX_CHANNELS; i++) {
		channels[i] = 0;
	}
}

static TCPApp *findAppWithPort(uint16_t port) {
	for (int i = 0; i < TCP_MAX_APPS; i++) {
		if (apps[i] != 0 && apps[i]->port == port) {
			return apps[i];
		}
	}
	return 0;
}

uint8_t addTcpApp(TCPApp *app) {
	for (int i = 0; i < TCP_MAX_APPS; i++) {
		if (apps[i] == 0) {
			apps[i] = app;
			return 1;
		}
	}
	return 0;
}

static void freeChannel(TCPChannel *channel) {
	for (int i = 0; i < TCP_MAX_CHANNELS; i++) {
		if (channels[i] == channel) {
			channels[i] = 0;
		}
	}
}

static uint8_t getFreeChannelPos() {
	for (int i = 0; i < TCP_MAX_CHANNELS; i++) {
		if (channels[i] == 0) {
			return i;
		}
	}
	return TCP_MAX_CHANNELS;
}

void finTcpSession(TCPChannel *channel) {
	TCPApp *app = channel->app;
	app->disconnect(channel);

	sendTcpResponseHeader(channel, (1 << TCP_FLAG_FIN));
	sendTcpResponse(channel);
	freeChannel(channel);
}

static void writeSequenceNumber(SequenceNumber *to, uint32_t from) {
	to->part1 = (uint8_t) (from >> 24);
	to->part2 = (uint8_t) (from >> 16);
	to->part3 = (uint8_t) (from >> 8);
	to->part4 = (uint8_t) (from >> 0);
}

static uint32_t decodeSeqNumber(SequenceNumber *number) {
	return ((uint32_t) number->part1 << 24) | ((uint32_t) number->part2 << 16)
			| ((uint32_t) number->part3 << 8) | (uint32_t) number->part4;
}

static uint16_t getTcpPreChecksum(IPHeader *ipHeader) {
	uint32_t checksum = 0;
	checksum += (uint16_t) (ipHeader->source.addr1 << 8)
			| ipHeader->source.addr2;
	checksum += (uint16_t) (ipHeader->source.addr3 << 8)
			| ipHeader->source.addr4;
	checksum += (uint16_t) (ipHeader->destination.addr1 << 8)
			| ipHeader->destination.addr2;
	checksum += (uint16_t) (ipHeader->destination.addr3 << 8)
			| ipHeader->destination.addr4;
	checksum += PROTOCOL_TCP;

	uint16_t realChecksum = (uint16_t) (checksum >> 16) + (uint16_t) checksum;
	return realChecksum;
}

static uint32_t precomputeIpHeaderChecksum(IPHeader *header) {
	header->checksumh = 0;
	header->checksuml = 0;
	header->lengthh = 0;
	header->lengthl = 0;

	uint32_t checksum = 0;
	uint8_t* checksummableHeader = (uint8_t*) header;
	for (int i = 0; i < 20; i += 2) {
		uint8_t high = checksummableHeader[i];
		uint8_t low = checksummableHeader[i + 1];
		checksum += (((uint16_t) high << 8) | (uint16_t) low);
	}
	return checksum;
}

/**
 * generates and writes the ethernet header to the enc.
 */
static void writeEthernetheader(MacAddress *destination, uint16_t type) {
	EthernetHeader header;
	memcpy(&(header.destination), destination, sizeof(MacAddress));
	setToMyMac(&header.source);
	header.typeh = (uint8_t) (type >> 8);
	header.typel = (uint8_t) type;
	encWriteSequence(&header, sizeof(EthernetHeader));
}
static void writeHeaders(TCPChannel *channel, uint8_t flags) {
	TCPApp *app = channel->app;
	//ip
	writeEthernetheader(&channel->mac, 0x0800);
	static IPHeader ipHeader;
	ipHeader.headerlength = (4 << 4) | 5;
	ipHeader.ds_field = 0;
	ipHeader.identificationh = 0; // unsupported
	ipHeader.identificationl = 0;
	ipHeader.fragmentoffset1 = 0;
	ipHeader.fragmentoffset2 = 0;
	ipHeader.ttl = 64;
	ipHeader.protocol = PROTOCOL_TCP;
	ipHeader.checksumh = 0;
	ipHeader.checksuml = 0;
	setToMyIp(&ipHeader.source);
	memcpy(&ipHeader.destination, &channel->ip, sizeof(IpAddress));

	tcpipStartPosition = encGetSendLength();
	tcpipHeaderStartPointer = encGetWriteMark();
	encWriteSequence(&ipHeader, sizeof(IPHeader));

	ipHeaderCecksum = precomputeIpHeaderChecksum(&ipHeader);
	tcpHeaderPreChecksum = getTcpPreChecksum(&ipHeader);

	tcpHeaderStartPosition = encGetWriteMark();

	//tcp
	static TCPHeader tcpHeader;
	tcpHeader.source.porth = (uint8_t) (app->port >> 8);
	tcpHeader.source.portl = (uint8_t) app->port;
	tcpHeader.destination.porth = (uint8_t) (channel->port >> 8);
	tcpHeader.destination.portl = (uint8_t) channel->port;
	tcpHeader.flagsh = 5 << 4;
	tcpHeader.flagsl = flags;
	writeSequenceNumber(&tcpHeader.seqenceNumber, channel->seqnumber);
	if (flags & (1 << TCP_FLAG_ACK)) {
		writeSequenceNumber(&tcpHeader.ackNumber, channel->acknumber);
	} else {
		writeSequenceNumber(&tcpHeader.ackNumber, 0);
	}
	tcpHeader.widowsizeh = (uint8_t) (WINDOW_SIZE >> 8);
	tcpHeader.widowsizel = (uint8_t) WINDOW_SIZE;
	tcpHeader.urgent1 = 0;
	tcpHeader.urgent2 = 0;
	encWriteSequence(&tcpHeader, sizeof(TCPHeader));
	debugString("TCP header sent\n");
}

/**
 * Writes the header to enc
 */
void sendTcpResponseHeader(TCPChannel *channel, uint8_t flags) {
	encStartPackage();
	writeHeaders(channel, flags);
}

/**
 * Final send method, after sendTcpResponseHeader
 */
void sendTcpResponse(TCPChannel *channel) {
	uint16_t length = encGetSendLength() - tcpipStartPosition;

	uint16_t endPointer = encGetWriteMark();
	encSetWritePointerOffseted(tcpipHeaderStartPointer, TCP_LENGTH_OFFSET);
	encWriteChar((uint8_t) (length >> 8));
	encWriteChar((uint8_t) length);

	uint32_t withLength = ipHeaderCecksum + length;
	uint16_t checksum = ((uint16_t) withLength + (uint16_t) (withLength >> 16))
			^ 0xffff;
	encSetWritePointerOffseted(tcpipHeaderStartPointer, TCP_CHECKSUM_OFFSET);
	encWriteChar((uint8_t) (checksum >> 8));
	encWriteChar((uint8_t) checksum);

	encSetWritePointer(endPointer);

	encComputeTcpChecksum(tcpHeaderPreChecksum, tcpHeaderStartPosition);

	encSend();
	channel->seqnumber += length - sizeof(IPHeader) - sizeof(TCPHeader);
	debugString("TCP response completed\n");
}

/**
 * Resends the last package to an other session,
 * assuming the package was send directly before this one.
 */
void resendTcpResponse(TCPChannel *channel, uint8_t flags) {
	uint16_t endPointer = encGetWriteMark();
	encRestartPackage();
	writeHeaders(channel, flags);
	encSetWritePointer(endPointer);
	sendTcpResponse(channel);
}

static void tcpSendSynAck(TCPChannel *channel) {
	sendTcpResponseHeader(channel, (1 << TCP_FLAG_SYN) | (1 << TCP_FLAG_ACK));
	sendTcpResponse(channel);
}
static uint8_t ipEquals(IpAddress *ip1, IpAddress *ip2) {
	return ip1->addr4 == ip2->addr4 && ip1->addr3 == ip2->addr3
			&& ip1->addr2 == ip2->addr2 && ip1->addr1 == ip2->addr1;
}

static uint8_t portEquals(TCPPort *p1, uint16_t port) {
	return p1->portl == (uint8_t) port && p1->porth == (uint8_t) (port >> 8);
}
static TCPChannel* getChannelFor(TCPApp *app, IpAddress *sourceip,
		TCPHeader * header) {
	for (int i = 0; i < TCP_MAX_CHANNELS; i++) {
		if (channels[i] != 0 && portEquals(&header->destination, app->port)
				&& portEquals(&header->source, channels[i]->port)
				&& ipEquals(sourceip, &channels[i]->ip)) {
			return channels[i];
		}
	}
	return 0;
}

TCPChannel temporaryCahnnel;

void tcpHeaderReceived() {
	debugString("TCP: Received tcp header\n");

	uint8_t readBytes = encReadSequence((uint8_t*) &incommingTcpHeader,
			sizeof(TCPHeader));

	uint8_t headerlength = (incommingTcpHeader.flagsh >> 4) * 4;
	if (headerlength > readBytes) {
		uint8_t toSkip = headerlength - readBytes;
		encSkip(toSkip);
	}

	uint16_t port = ((uint16_t) incommingTcpHeader.destination.porth << 8)
			| incommingTcpHeader.destination.portl;
	TCPApp *app = findAppWithPort(port);

	if (app == 0) {
		debugString("TCP: No app found for port.\n");
		//we only accept packages on registered ports.
		return;
	}

	if (incommingTcpHeader.flagsl & (1 << TCP_FLAG_SYN)) {
		if (!(incommingTcpHeader.flagsl & (1 << TCP_FLAG_ACK))) {

			//new connection is to be established. Only incoming supported yet.
			debugString("================= Incomming syn reqest on port "); debugHex(incommingTcpHeader.destination.porth); debugHex(incommingTcpHeader.destination.portl); debugString("\n");

			uint8_t freeChannelPos = getFreeChannelPos();
			if (app != 0 && freeChannelPos < TCP_MAX_CHANNELS) {
				TCPChannel *channel;
				debugString("Connecting application.\n");
				channel = app->connect();
				if (channel != 0) {
					debugString("Application accepted connection.\n");
					channels[freeChannelPos] = channel;
					channel->acknumber = decodeSeqNumber(
							&incommingTcpHeader.seqenceNumber) + 1;
					channel->seqnumber = 0x100; //TODO: initial seq number?
					memcpy(&channel->mac, &incommingEthHeader.source,
							sizeof(MacAddress));
					memcpy(&channel->ip, &incommingIpHeader.source,
							sizeof(IpAddress));
					channel->port = ((uint16_t) incommingTcpHeader.source.porth
							<< 8) | incommingTcpHeader.source.portl;
					channel->app = app;
					channel->timeRemaining = TCP_TIMEOUT;
					tcpSendSynAck(channel);
				}
			}
		}
	} else if (incommingTcpHeader.flagsl & (1 << TCP_FLAG_RST)) {
		debugString("TCP: Resetting.\n");
		TCPChannel *channel = getChannelFor(app, &incommingIpHeader.source,
				&incommingTcpHeader);
		if (channel != 0) {
			app->disconnect(channel);
		}
		freeChannel(channel);
	} else if (incommingTcpHeader.flagsl & (1 << TCP_FLAG_FIN)) {
		debugString("TCP: Closing connection.\n");
		TCPChannel *channel = getChannelFor(app, &incommingIpHeader.source,
				&incommingTcpHeader);
		if (channel != 0) {
			app->disconnect(channel);
		} else {
			channel = &temporaryCahnnel;
			channel->mac = incommingEthHeader.source;
			channel->ip = incommingIpHeader.source;
			channel->port = (incommingTcpHeader.source.porth << 8)
					+ incommingTcpHeader.source.portl;
			channel->app = app;
		}
		channel->acknumber = decodeSeqNumber(&incommingTcpHeader.seqenceNumber)
				+ 1;
		channel->seqnumber = decodeSeqNumber(&incommingTcpHeader.ackNumber);
		channel->timeRemaining = TCP_TIMEOUT;

		sendTcpResponseHeader(channel,
				(1 << TCP_FLAG_ACK) | (1 << TCP_FLAG_FIN));
		sendTcpResponse(channel);
		freeChannel(channel);
	} else {
		debugString("TCP: passing normal package to app.\n");
		TCPChannel *channel = getChannelFor(app, &incommingIpHeader.source,
				&incommingTcpHeader);
		uint16_t dataLength = encGetRemaining();
		if (channel != 0) {
			channel->acknumber = decodeSeqNumber(
					&incommingTcpHeader.seqenceNumber) + dataLength;
			channel->seqnumber = decodeSeqNumber(&incommingTcpHeader.ackNumber);
			channel->timeRemaining = TCP_TIMEOUT;

			if (dataLength > 0) {
				sendSimpleAck(channel);
			}

			app->receivePackage(channel);
		}
	}

}

/* ============================= IP =========================== */
void ipPackageReceived() {
	debugString("IP: Received ip header\n");
	uint8_t readBytes = encReadSequence((uint8_t*) &incommingIpHeader,
			sizeof(incommingIpHeader));

	uint16_t packagelength = (incommingIpHeader.lengthh << 8)
			| incommingIpHeader.lengthl;
	encDecreaseRemainingTo(packagelength - sizeof(incommingIpHeader));

	uint8_t headerlen = (incommingIpHeader.headerlength & 0x0f) * 4;
	if (headerlen > readBytes) {
		uint8_t toSkip = headerlen - readBytes;
		encSkip(toSkip);
	}

	if (isMyIp(&incommingIpHeader.destination)) {
		if (incommingIpHeader.protocol == PROTOCOL_TCP) {
			tcpHeaderReceived();
		} else {
			debugString("IP: Wrong protocol\n");
		}
	} else {
		debugString("IP: The package is NOT addressed at me: "); debugHex(incommingIpHeader.destination.addr1); debugString("."); debugHex(incommingIpHeader.destination.addr2); debugString("."); debugHex(incommingIpHeader.destination.addr3); debugString("."); debugHex(incommingIpHeader.destination.addr4); debugString("\n");
	}
}

/* ============================= ARP =========================== */
void arpPackageReceived() {
	debugString("ARP: Got arp package\n");
	if (isBroadcast(&(incommingEthHeader.destination))
			|| isMyMac(&(incommingEthHeader.destination))) {
		//received broadcast arp package.
		ArpPackage arpPackage;
		encReadSequence((uint8_t*) &arpPackage, sizeof(ArpPackage));

		if (arpPackage.protocolh == 0x08 && arpPackage.protocoll == 0x00
				&& arpPackage.hardwaresize == 6 && arpPackage.protocolsize == 4
				&& arpPackage.opcodeh == 0 && arpPackage.opcodel == 1
				&& isMyIp(&arpPackage.targetIp)) {
			//arp request
			debugString("Got arp request addressed at me\n");

			//use old package as response.
			arpPackage.opcodel = 2;
			memcpy(&arpPackage.targetMac, &arpPackage.senderMac,
					sizeof(MacAddress));
			memcpy(&arpPackage.targetIp, &arpPackage.senderIp,
					sizeof(IpAddress));
			setToMyMac(&arpPackage.senderMac);
			setToMyIp(&arpPackage.senderIp);

			encStartPackage();
			writeEthernetheader(&arpPackage.targetMac, 0x0806);
			encWriteSequence(&arpPackage, sizeof(ArpPackage));
			encSend();
		}
	}
}

/**
 * receives a new ethernet package.
 */
void ethernetPackageReceived() {
	encReadSequence((uint8_t*) &incommingEthHeader, sizeof(incommingEthHeader));

	debugString("ETH: Received network package with type "); debugHex(incommingEthHeader.typeh); debugHex(incommingEthHeader.typel); debugString("\n");

	if (incommingEthHeader.typeh == 0x08 && incommingEthHeader.typel == 0x00) {
		ipPackageReceived();
	} else if (incommingEthHeader.typeh == 0x08
			&& incommingEthHeader.typel == 0x06) {
		arpPackageReceived();
	} debugString("ETH: Network package handled\n");
}

/**
 * Convenience function.
 */
void sendSimpleAck(TCPChannel *channel) {
	sendTcpResponseHeader(channel, (1 << TCP_FLAG_ACK));
	sendTcpResponse(channel);
}

static void sendKeepAlive(TCPChannel *channel) {
	sendTcpResponseHeader(channel, 0);
	sendTcpResponse(channel);
}

uint8_t tcpTimeoutDowncountFlag;

void tcpTimeoutDowncount() {
	tcpTimeoutDowncountFlag = 1;
}

void tcpTimeoutPoll() {

	if (tcpTimeoutDowncountFlag) {
		uint8_t i;
		for (i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
			if (channels[i] == 0) {
				continue;
			} else if (channels[i]->timeRemaining) {
				channels[i]->timeRemaining--;
				if (channels[i]->timeRemaining == TCP_WARNING) {
					sendKeepAlive(channels[i]);
				}
			} else {
				//kill it
				finTcpSession(channels[i]);
			}
		}
		tcpTimeoutDowncountFlag = 0;
	}
}
