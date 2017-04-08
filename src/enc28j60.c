/*
 * enc28j60.c
 *
 *  Created on: 29.10.2011
 *      Author: michael
 */

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "enc28j60.h"
#include "config.h"

//#define DEBUG_ENC

#ifdef DEBUG_ENC
#include "usart.h"
#define debugString sendString
#define debugHex sendAsHex
#else
#define debugString(str)
#define debugHex(n)
#endif

#define RECEIVE_START 0x00
#define RECEIVE_END 0x0800
#define ENC_SEND_START 0x0801
#define ENC_SEND_END 0x0b00
#define MAX_FRAMELENGTH 1518

#define SPI_SS_PIN 4
#define SPI_MOSI_PIN 5
#define SPI_MISO_PIN 6
#define SPI_SCK_PIN 7
#define SPI_PORT PORTB
#define SPI_DDR DDRB

#define ENC_COMMAND_READ 0x00
#define ENC_COMMAND_WRITE 0x40
#define ENC_COMMAND_SETBITS 0x80
#define ENC_COMMAND_CLEARBITS 0xa0
#define ENC_COMMAND_WBM 0x7A
#define ENC_COMMAND_RBM 0x3A
#define ENC_COMMAND_RESET 0xff

/**
 * ENC constants use:
 * First 2 Bits for bank
 * 1 Bit 0
 * Last 5 Bits for address
 */
#define ENC_ERDPTL 0x00
#define ENC_ERDPTH 0x01
#define ENC_EWRPTL 0x02
#define ENC_EWRPTH 0x03
#define ENC_ETXSTL 0x04
#define ENC_ETXSTH 0x05
#define ENC_ETXNDL 0x06
#define ENC_ETXNDH 0x07
#define ENC_ERXSTL 0x08
#define ENC_ERXSTH 0x09
#define ENC_ERXNDL 0x0a
#define ENC_ERXNDH 0x0b
#define ENC_ERXRDPTL 0x0c
#define ENC_ERXRDPTH 0x0d
#define ENC_ERXWRPTL 0x0e
#define ENC_ERXWRPTH 0x0f
#define ENC_ESTAT 0x1d
#define ENC_CLKRDY 0

#define ENC_MACON1 (0x80 | 0x00)
#define ENC_TXPAUS 3
#define ENC_RXPAUS 2
#define ENC_PASSALL 1
#define ENC_MARXEN 0
#define ENC_MACON3 (0x80 | 0x02)
#define ENC_PADCFG2 7
#define ENC_PADCFG1 6
#define ENC_PADCFG0 5
#define ENC_TXCRCEN 4
#define ENC_PHDREN 3
#define ENC_HFRMEN 2
#define ENC_FRMLNEN 1
#define ENC_FULDPX 0
#define ENC_MACON4 (0x80 | 0x03)
#define ENC_MAMXFLL (0x80 | 0x0a)
#define ENC_MAMXFLH (0x80 | 0x0b)
#define ENC_MABBIPG (0x80 | 0x04)
#define ENC_MAIPGL (0x80 | 0x06)
#define ENC_MAIPGH (0x80 | 0x07)

#define ENC_MAADR5 (0xc0 | 0x00)
#define ENC_MAADR6 (0xc0 | 0x01)
#define ENC_MAADR3 (0xc0 | 0x02)
#define ENC_MAADR4 (0xc0 | 0x03)
#define ENC_MAADR1 (0xc0 | 0x04)
#define ENC_MAADR2 (0xc0 | 0x05)

#define ENC_MICMD (0x80 | 0x12)
#define ENC_MIIRD 0
#define ENC_MIREGADR (0x80 | 0x14)
#define ENC_MIWRL (0x80 | 0x16)
#define ENC_MIWRH (0x80 | 0x17)
#define ENC_MIRDL (0x80 | 0x18)
#define ENC_MIRDH (0x80 | 0x19)
#define ENC_MISTAT (0xc0 | 0x0a)
#define ENC_BUSY 0

/*  - - - - PHY registers - - - - */
#define ENC_PHCON1 0x00
#define ENC_PDPXMD 8

// on any bank!
#define ENC_EIE 0x1b
#define ENC_INTIE 7
#define ENC_PKTIE 6
#define ENC_TXIE 3

#define ENC_EIR 0x1c
#define ENC_PKTIF 6
#define ENC_TXIF 3

#define ENC_ESTAT 0x1d

#define ENC_ECON2 0x1e
#define ENC_PKTDEC 6
#define ENC_AUTOINC 7

#define ENC_ECON1 0x1f
#define ENC_TXRTS 3
#define ENC_RXEN 2

//enable rx;
uint8_t enc_rxen_bit;

static void spiInit() {
	SPI_DDR = (1 << SPI_MOSI_PIN) | (1 << SPI_SCK_PIN) | (1 << SPI_SS_PIN);
	SPCR = (1 << SPE) | (1 << MSTR);
	SPSR = (1 << SPI2X);
	SPI_PORT |= (1 << SPI_SS_PIN);
}

static void waitSpiFinished() {
	while (!(SPSR & (1 << SPIF))) {
	}
}
static void startSpiFrame() {
	SPI_PORT &= ~(1 << SPI_SS_PIN);
}
static void sendOnSpi(uint8_t value) {
	SPDR = value;
	waitSpiFinished();
}
/**
 * Receives a byte without sending something.
 */
static uint8_t receiveOnSpi() {
	SPDR = 0;
	waitSpiFinished();
	uint8_t data = SPDR;
	return data;
}
static void endSpiFrame() {
	SPI_PORT |= (1 << SPI_SS_PIN);
}

/* ===================== unbanked enc commands ====================== */

static void sendEncReset() {
	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RESET);
	endSpiFrame();
}

/**
 * Writes the value in a register on the current bank.
 * registerAddress must be smaller/equal to 0x1f
 */
static void writeEncRegisterUnbanked(uint8_t registerAddress, uint8_t value) {
	uint8_t command = ENC_COMMAND_WRITE | registerAddress;
	startSpiFrame();
	sendOnSpi(command);
	sendOnSpi(value);
	endSpiFrame();
}
/**
 * Sets the bits in a register on the current bank.
 * registerAddress must be smaller/equal to 0x1f
 */
static void setBitsInEncRegisterUnbanked(uint8_t registerAddress, uint8_t value) {
	uint8_t command = ENC_COMMAND_SETBITS | registerAddress;
	startSpiFrame();
	sendOnSpi(command);
	sendOnSpi(value);
	endSpiFrame();
}

/**
 * Clears the bits in a register on the current bank.
 * registerAddress must be smaller/equal to 0x1f
 */
static void clearBitsInEncRegisterUnbanked(uint8_t registerAddress,
		uint8_t value) {
	uint8_t command = ENC_COMMAND_CLEARBITS | registerAddress;
	startSpiFrame();
	sendOnSpi(command);
	sendOnSpi(value);
	endSpiFrame();
}

static uint8_t readEncRegisterUnbanked(uint8_t registerAddress) {
	uint8_t command = ENC_COMMAND_READ | registerAddress;
	startSpiFrame();
	sendOnSpi(command);
	uint8_t value = receiveOnSpi();
	endSpiFrame();
	return value;
}

/* ===================== banked enc commands ====================== */
static void setEncBank(uint8_t address) {
	static uint8_t oldBankmask = 0x01; // invalid

	uint8_t bankmasked = address & 0xc0;
	if (bankmasked != oldBankmask) {
		oldBankmask = bankmasked;
		uint8_t value = bankmasked >> 6;
		clearBitsInEncRegisterUnbanked(ENC_ECON1, 0x03);
		if (value != 0x00) {
			setBitsInEncRegisterUnbanked(ENC_ECON1, value);
		}
	}
}

static void writeEncRegister(uint8_t registerAddress, uint8_t value) {
	setEncBank(registerAddress);
	writeEncRegisterUnbanked(0x1f & registerAddress, value);
}
static void setBitsInEncRegister(uint8_t registerAddress, uint8_t value) {
	setEncBank(registerAddress);
	setBitsInEncRegisterUnbanked(0x1f & registerAddress, value);
}

/*
static void clearBitsInEncRegister(uint8_t registerAddress, uint8_t value) {
	setEncBank(registerAddress);
	clearBitsInEncRegisterUnbanked(0x1f & registerAddress, value);
}*/

/**
 * TODO: dummy byte in mac/mii register
 */
static uint8_t readEncRegister(uint8_t registerAddress) {
	setEncBank(registerAddress);
	return readEncRegisterUnbanked(0x1f & registerAddress);
}

static void waitEncPhyNotBusy() {
	uint8_t stat;
	do {
		stat = readEncRegister(ENC_MISTAT);
	} while (stat & (1 << ENC_BUSY));
}

static void writeEncPhyRegister(uint8_t address, uint16_t value) {
	writeEncRegister(ENC_MIREGADR, address);
	writeEncRegister(ENC_MIWRL, (uint8_t) (value));
	writeEncRegister(ENC_MIWRH, (uint8_t) (value >> 8));
	waitEncPhyNotBusy();
}

/*
static uint16_t readEncPhyRegister(uint8_t address, uint16_t value) {
	writeEncRegister(ENC_MIREGADR, address);
	writeEncRegister(ENC_MICMD, (1 << ENC_MIIRD));
	waitEncPhyNotBusy();
	writeEncRegister(ENC_MICMD, (1 << ENC_MIIRD));
	uint8_t low = readEncRegister(ENC_MIRDL);
	uint8_t high = readEncRegister(ENC_MIRDL);
	return ((uint16_t) high << 8) | low;
}*/

void setupReceiveBuffer() {
	writeEncRegister(ENC_ERXSTL, (uint8_t) RECEIVE_START);
	writeEncRegister(ENC_ERXSTH, (uint8_t) (RECEIVE_START >> 8));
	writeEncRegister(ENC_ERXNDL, (uint8_t) RECEIVE_END);
	writeEncRegister(ENC_ERXNDH, (uint8_t) (RECEIVE_END >> 8));

	writeEncRegister(ENC_ERXRDPTL, (uint8_t) RECEIVE_START);
	writeEncRegister(ENC_ERXRDPTH, (uint8_t) (RECEIVE_START >> 8));
	writeEncRegister(ENC_ERDPTL, (uint8_t) RECEIVE_START);
	writeEncRegister(ENC_ERDPTH, (uint8_t) (RECEIVE_START >> 8));
}

void waitForOsc() {
	while (1) {
		uint8_t estat = readEncRegister(ENC_ESTAT);
		if (estat & (1 << ENC_CLKRDY)) {
			break;
		}
	}
}

void setupMac() {
	writeEncRegister(ENC_MACON1,
			(1 << ENC_TXPAUS) | (1 << ENC_RXPAUS) | (1 << ENC_MARXEN));

	writeEncRegister(ENC_MACON3,
			(1 << ENC_PADCFG0) | (1 << ENC_TXCRCEN) | (1 << ENC_FULDPX));
	writeEncRegister(ENC_MAMXFLL, (uint8_t) MAX_FRAMELENGTH);
	writeEncRegister(ENC_MAMXFLH, (uint8_t) (MAX_FRAMELENGTH >> 8));

	writeEncRegister(ENC_MABBIPG, 0x15);
	writeEncRegister(ENC_MAIPGL, 0x12);

	writeEncRegister(ENC_MAADR1, MY_MAC_1);
	writeEncRegister(ENC_MAADR2, MY_MAC_2);
	writeEncRegister(ENC_MAADR3, MY_MAC_3);
	writeEncRegister(ENC_MAADR4, MY_MAC_4);
	writeEncRegister(ENC_MAADR5, MY_MAC_5);
	writeEncRegister(ENC_MAADR6, MY_MAC_6);

	writeEncPhyRegister(ENC_PHCON1, (1 << ENC_PDPXMD));
}
/**
 * Does the whole init sequence.
 */
void initEnc(void) {
	spiInit();
	sendEncReset();
	setupReceiveBuffer();
	waitForOsc();
	setupMac();
	writeEncRegister(ENC_ECON2, (1 << ENC_AUTOINC));
	setBitsInEncRegister(ENC_ECON1, 1 << ENC_RXEN);

}

typedef struct {
	uint8_t nextaddrl;
	uint8_t nextaddrh;
	uint8_t lengthl;
	uint8_t lengthh;
	uint8_t status2;
	uint8_t status3;
} ReceivedPackageHeader;

uint16_t receivedPackageLength;
//how many bytes have not already been read.
uint16_t receivedPackageRemaining;

/**
 * Reads length bytes to the buffer, no matter what happens.
 */
void encReadSequenceUnsafe(uint8_t *buffer, uint8_t length) {
	uint8_t i = 0;

	debugString("SPI: Reading: ");debugHex(length);debugString(" bytes:");

	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);
	for (i = 0; i < length; i++) {
		buffer[i] = receiveOnSpi();
		debugString(" ");debugHex(buffer[i]);
	}debugString("\n");
	endSpiFrame();
}

static void receivePackage() {
	static ReceivedPackageHeader networkheader;
	encReadSequenceUnsafe((uint8_t*) &networkheader,
			sizeof(ReceivedPackageHeader));

	receivedPackageLength = networkheader.lengthl
			| (networkheader.lengthh << 8);
	receivedPackageRemaining = receivedPackageLength;

	//call the handler
	ENC_RECEIVE_PACKAGE();

	writeEncRegister(ENC_ERDPTL, networkheader.nextaddrl);
	writeEncRegister(ENC_ERDPTH, networkheader.nextaddrh);
	writeEncRegister(ENC_ERXRDPTL, networkheader.nextaddrl);
	writeEncRegister(ENC_ERXRDPTH, networkheader.nextaddrh);

	//decrement receive pointer (may clear interrupt flag)
	setBitsInEncRegisterUnbanked(ENC_ECON2, 1 << ENC_PKTDEC);
}

void pollEnc() {
	uint8_t eirvalue = readEncRegisterUnbanked(ENC_EIR);
	if (eirvalue & (1 << ENC_PKTIF)) {
		//packet received
		receivePackage();
	}
}

static uint8_t makeReceiveLengthSafe(uint8_t length) {
	if (length <= receivedPackageRemaining) {
		return length;
	} else {
		return receivedPackageRemaining;
	}
}

uint8_t encReadSequence(uint8_t *buffer, uint8_t length) {
	uint8_t reallength = makeReceiveLengthSafe(length);
	encReadSequenceUnsafe(buffer, reallength);
	receivedPackageRemaining -= reallength;
	return reallength;
}

uint8_t encSkip(uint8_t n) {
	uint8_t reallength = makeReceiveLengthSafe(n);
	uint8_t i = 0;
	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);
	for (i = 0; i < reallength; i++) {
		receiveOnSpi();
	}
	endSpiFrame();
	receivedPackageRemaining -= reallength;
	return reallength;
}

/**
 * Reads a char.
 */
uint8_t encReadChar() {
	if (receivedPackageRemaining > 0) {
		receivedPackageRemaining--;
		startSpiFrame();
		sendOnSpi(ENC_COMMAND_RBM);
		uint8_t value = receiveOnSpi();
		endSpiFrame();
		return value;
	} else {
		return 0;
	}
}

/**
 * Reads until the given char, or until maxn bytes have been read,
 * or until the end of the package is reached.
 * Returns the number of bytes read, excluding the end char (which may also have been read)
 */
uint8_t encReadUntil(uint8_t *buffer, uint8_t maxn, char character) {
	uint8_t maxread;
	if (maxn > receivedPackageRemaining) {
		maxread = receivedPackageRemaining;
	} else {
		maxread = maxn;
	}

	uint8_t i = 0;

	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);
	for (i = 0; i < maxread; i++) {
		uint8_t read = receiveOnSpi();
		receivedPackageRemaining--;
		buffer[i] = read;

		if (read == (uint8_t) character) {
			endSpiFrame();
			return i;
		}
	}
	endSpiFrame();
	return maxread;
}

uint8_t encReadUntilSpace(uint8_t *buffer, uint8_t maxn) {
	uint8_t i;

	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);

	for (i = 0; i < maxn && receivedPackageRemaining > 0; i++) {
		uint8_t read = receiveOnSpi();
		receivedPackageRemaining--;
		buffer[i] = read;
		if (read == ' ' || read == '\n' || read == '\r') {
			endSpiFrame();
			return i;
		}
	}

	endSpiFrame();
	return i - 1;
}

uint8_t encSkipUntil(char character) {
	uint8_t skipped = 0;
	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);
	while (receivedPackageRemaining > 0) {
		char received = receiveOnSpi();
		receivedPackageRemaining--;
		if (skipped < 255) {
			skipped++;
		}
		if (character == received) {
			break;
		}
	}
	endSpiFrame();
	return skipped;
}

/**
 * Reads a int from the stream. Skips the car after the int!
 * Returns 0 if the stram has no more bytes, skipped is then set to \0.
 * Return 0 if no int was read.
 */
int16_t encReadInt(char *skipped) {
	uint8_t current;
	int16_t number = 0;
	uint8_t isNegative = 0;
	*skipped = 0;

	if (receivedPackageRemaining > 0) {
		startSpiFrame();
		sendOnSpi(ENC_COMMAND_RBM);
		while (receivedPackageRemaining > 0) {
			current = receiveOnSpi();
			receivedPackageRemaining--;

			if (current == '-') {
				isNegative = 1; // should only work on first char.
			} else if (current >= '0' && current <= '9') {
				number = number * 10 + (current - '0');
			} else {
				*skipped = current;
				break;
			}
		}
		endSpiFrame();

		if (isNegative) {
			number *= -1;
		}
		return number;
	} else {
		return 0;
	}
}

uint16_t encGetRemaining() {
	return receivedPackageRemaining;
}
void encDecreaseRemainingTo(uint16_t remaining) {
	if (remaining < receivedPackageRemaining) {
		receivedPackageRemaining = remaining;
	}
}

uint16_t encSendStart = ENC_SEND_START + 1;
uint16_t encSendLength = 0xffff;

void encStartPackage() {
	uint16_t statusbyte = encSendStart - 1;
	writeEncRegister(ENC_ETXSTL, (uint8_t) statusbyte);
	writeEncRegister(ENC_ETXSTH, (uint8_t) (statusbyte >> 8));
	writeEncRegister(ENC_EWRPTL, (uint8_t) statusbyte);
	writeEncRegister(ENC_EWRPTH, (uint8_t) (statusbyte >> 8));

	//write package control bit
	encSendLength = 0;
	encWriteChar(0x00);
	encSendLength = 0;
}

/**
 * Same as encStartPackage, but it is guaranteed that the old data is not deleted.
 */
void encRestartPackage() {
	encStartPackage();
}

void encSend() {
	if (encSendLength != 0xffff) {
		uint16_t endOfPackage = encSendLength + encSendStart - 1;
		writeEncRegister(ENC_ETXNDL, (uint8_t) endOfPackage);
		writeEncRegister(ENC_ETXNDH, (uint8_t) (endOfPackage >> 8));

		debugString("ENC: sending from ");debugHex(readEncRegister(ENC_ETXSTH));debugHex(readEncRegister(ENC_ETXSTL));debugString(" to ");debugHex(readEncRegister(ENC_ETXNDH));debugHex(readEncRegister(ENC_ETXNDL));debugString("\n");

		clearBitsInEncRegisterUnbanked(ENC_EIR, (1 << ENC_TXIF));
		// interrupt...
		//setBitsInEncRegisterUnbanked(ENC_EIE, (1<<ENC_TXIE) | (1<<ENC_INTIE));
		setBitsInEncRegisterUnbanked(ENC_ECON1, 1 << ENC_TXRTS);

		while (1) {
			uint8_t value = readEncRegisterUnbanked(ENC_ECON1);
			if (!(value & (1 << ENC_TXRTS))) {
				break;
			}
		}debugString("ENC: send finished\n");
	} else {
		debugString("ENC: called encSend() while no package is opened.\n");
	}
	encSendLength = 0xffff;
}

void encWriteChar(uint8_t value) {
	if (encSendLength != 0xffff) {
		debugString("SPI: sending ");debugHex(value);debugString("\n");

		startSpiFrame();
		sendOnSpi(ENC_COMMAND_WBM);
		sendOnSpi(value);
		endSpiFrame();

		encSendLength++;
	} else {
		debugString("ENC: called encWriteChar() while no package is opened.\n");
	}
}

void encWriteSequence(void *datastart, uint8_t length) {
	if (encSendLength != 0xffff) {
		debugString("SPI: sending ");debugHex(length);debugString(" bytes:");

		uint8_t *data = (uint8_t*) datastart;
		startSpiFrame();
		sendOnSpi(ENC_COMMAND_WBM);
		uint8_t i;
		for (i = 0; i < length; i++) {
			debugString(" ");debugHex(data[i]);
			sendOnSpi(data[i]);
		}
		endSpiFrame();
		debugString("\n");
		encSendLength += length;
	} else {
		debugString(
				"ENC: called encWriteSequence() while no package is opened.\n");
	}
}

void encWriteStringParameters_P(PGM_P message, uint16_t parameters[],
		uint8_t parametercount) {
	if (encSendLength != 0xffff) {
		uint8_t currentParamIndex = 0;
		PGM_P pgmpos = message;
		char current;

		startSpiFrame();
		sendOnSpi(ENC_COMMAND_WBM);
		while ((current = pgm_read_byte(pgmpos)) != 0) {
			if (current == '%' && currentParamIndex < parametercount) {
				endSpiFrame();

				encWriteInt(parameters[currentParamIndex]);
				currentParamIndex++;

				startSpiFrame();
				sendOnSpi(ENC_COMMAND_WBM);
			} else {
				sendOnSpi(current);
				encSendLength++;
			}
			pgmpos++;
		}
		endSpiFrame();
	} else {
		debugString(
				"ENC: called encWriteStringParameters_P() while no package is opened.\n");
	}
}

void encWriteInt(uint16_t number) {
	uint16_t rest = number;
	uint8_t started = 0;
	for (uint16_t base = 10000; base >= 1; base /= 10) {
		uint16_t current = rest / base;
		if (started || current > 0) {
			started = 1;
			encWriteChar('0' + (char) current);
			rest -= base * current;
		}
	}
	if (!started) {
		encWriteChar('0');
	}
}

void encWriteInt32(uint32_t number) {
	uint8_t buffer[10];
	uint8_t i = 9;
	do {
		buffer[i] = '0' + (uint8_t) (number % 10);
		number /= 10;
		i--;
	} while (number);
	encWriteSequence(&buffer[i+1], 9 - i);
}

/**
 * Gets a marker that allows you to jump back to the current write position
 */
uint16_t encGetWriteMark() {
	return encSendLength;
}

void encSetWritePointer(uint16_t mark) {
	uint16_t position = mark + encSendStart;
	writeEncRegister(ENC_EWRPTL, (uint8_t) position);
	writeEncRegister(ENC_EWRPTH, (uint8_t) (position >> 8));
	encSendLength = mark;
}

void encSetWritePointerOffseted(uint16_t mark, uint16_t offset) {
	uint16_t realOffset = mark + offset;
	encSetWritePointer(realOffset);
}

/**
 * Copies the rest of the incoming data to the outgoing buffer.
 * TODO: current implementation is slow.
 */
void encCopyIncommingOutgoing(char until) {
	while (encGetRemaining() > 0) {
		uint8_t next = encReadChar();
		if (next == until) {
			return;
		}
		encWriteChar(next);
	}
}

uint16_t encGetSendLength() {
	return encSendLength;
}

#define TCP_CHECKSUM_OFFSET 16

/**
 * Computes the tcp checksum. Assumes that there is a tcp package starting at
 * tcpheaderStart and that its checksum is written to 0.
 * @param pseudoHeaderChecksum The checksum of ip sender, ip destination and
 * PROTOCOL_TCP, in ones complement.
 */
void encComputeTcpChecksum(uint16_t pseudoHeaderChecksum,
		uint16_t tcpheaderStart) {
	debugString("Pre-checksum: ");debugHex(pseudoHeaderChecksum >> 8);debugHex(pseudoHeaderChecksum);debugString("\n");

	uint32_t checksum = pseudoHeaderChecksum;
	uint8_t oldReadpointerl = readEncRegister(ENC_ERDPTL);
	uint8_t oldReadpointerh = readEncRegister(ENC_ERDPTH);
	uint16_t readStart = encSendStart + tcpheaderStart;
	writeEncRegister(ENC_ERDPTH, (uint8_t) (readStart >> 8));
	writeEncRegister(ENC_ERDPTL, (uint8_t) readStart);

	uint16_t packageEnd = encSendLength;
	//length
	checksum += packageEnd - tcpheaderStart;

	startSpiFrame();
	sendOnSpi(ENC_COMMAND_RBM);
	for (int i = tcpheaderStart; i < packageEnd - 1; i += 2) {
		uint16_t byte = (uint16_t) receiveOnSpi() << 8;
		byte |= receiveOnSpi();
		checksum += byte;
	}

	if (encSendLength & 0x1) {
		//odd => add padding
		uint16_t byte = (uint16_t) receiveOnSpi() << 8;
		checksum += byte;
	}
	endSpiFrame();

	encSetWritePointerOffseted(tcpheaderStart, TCP_CHECKSUM_OFFSET);

	uint16_t realChecksum = ((uint16_t) (checksum >> 16) + (uint16_t) checksum)
			^ 0xffff;
	encWriteChar((uint8_t) (realChecksum >> 8));
	encWriteChar((uint8_t) realChecksum);

	encSetWritePointer(packageEnd);
	writeEncRegister(ENC_ERDPTL, oldReadpointerl);
	writeEncRegister(ENC_ERDPTH, oldReadpointerh);
}

