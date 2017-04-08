/*
 * enc28j60.h
 *
 *  Created on: 29.10.2011
 *      Author: michael
 */

#ifndef ENC28J60_H_
#define ENC28J60_H_

#include "tcpip.h"
#include <stdint.h>
#include <avr/pgmspace.h>

void initEnc(void);

/**
 * Function to call when a package is received
 */
#define ENC_RECEIVE_PACKAGE ethernetPackageReceived

void pollEnc(void);

/**
 * Opens a enc package for sending and sets up the write pointer.
 */
void encStartPackage();

void encRestartPackage();
/**
 * writes a single 8 bit value to the enc and progresses the write pointer.
 */
void encWriteChar(uint8_t value);
/**
 * writes a char sequence to the enc
 */
void encWriteSequence(void *data, uint8_t length);
/**
 * Sends the opened package.
 */
void encSend();

/**
 * Reads a char.
 */
uint8_t encReadChar();
uint8_t encReadUntil(uint8_t *buffer, uint8_t maxn, char character);
uint8_t encSkipUntil(char character);
uint8_t encReadSequence(uint8_t *buffer, uint8_t length);
uint8_t encSkip(uint8_t n);

uint16_t encGetWriteMark();
void encSetWritePointer(uint16_t mark);
void encSetWritePointerOffseted(uint16_t mark, uint16_t offset);

uint16_t encGetSendLength();

void encComputeTcpChecksum(uint16_t pseudoHeaderChecksum,
		uint16_t tcpheaderStart);
uint16_t encGetRemaining();
void encDecreaseRemainingTo(uint16_t remaining);

void encWriteInt(uint16_t number);
void encWriteInt32(uint32_t number);
void encWriteStringParameters_P(PGM_P message, uint16_t parameters[], uint8_t parametercount);
int16_t encReadInt(char *skipped);
uint8_t encReadUntilSpace(uint8_t *buffer, uint8_t maxn);
void encCopyIncommingOutgoing(char until);

#endif /* ENC28J60_H_ */
