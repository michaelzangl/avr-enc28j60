/*
 * ipconfig.c
 *
 * IP configuration file.
 *
 * Can store the IP address of the device.
 *
 *  Created on: 09.04.2012
 *      Author: michael
 */

#include "tcpip.h"
#include <avr/eeprom.h>
#include <string.h>

IpAddress ipAddressEEMEM EEMEM = { 192, 168, 1, 180 };

IpAddress ipAddressCache = { 0, 0, 0, 0 };

static void loadCache() {
	if (ipAddressCache.addr1 == 0) {
		eeprom_read_block(&ipAddressCache, &ipAddressEEMEM, sizeof(IpAddress));
	}
}

/**
 * Gets my IP address.
 */
IpAddress *getMyIp() {
	loadCache();
	return &ipAddressCache;
}

uint8_t isMyIp(IpAddress *address) {
	loadCache();
	return address->addr1 == ipAddressCache.addr1
			&& address->addr2 == ipAddressCache.addr2
			&& address->addr3 == ipAddressCache.addr3
			&& address->addr4 == ipAddressCache.addr4;
}

void setMyIp(IpAddress *address) {
	eeprom_write_block(address, &ipAddressEEMEM, sizeof(IpAddress));

	memcpy(&ipAddressCache, address, sizeof(address));
}

void setToMyIp(IpAddress *address) {
	loadCache();
	address->addr1 = ipAddressCache.addr1;
	address->addr2 = ipAddressCache.addr2;
	address->addr3 = ipAddressCache.addr3;
	address->addr4 = ipAddressCache.addr4;
}
