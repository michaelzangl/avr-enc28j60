/*
 * ipconfig.h
 *
 *  Created on: 09.04.2012
 *      Author: michael
 */

#ifndef IPCONFIG_H_
#define IPCONFIG_H_

void getMyIp(IpAddress *address);
void setMyIp(IpAddress *address);
void setToMyIp(IpAddress *address);
uint8_t isMyIp(IpAddress *address);


#endif /* IPCONFIG_H_ */
