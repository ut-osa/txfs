/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VBUSCHANNEL_H__
#define __VBUSCHANNEL_H__

/*  The vbus channel is the channel area provided via the BUS_CREATE controlvm
 *  message for each virtual bus.  This channel area is provided to both server
 *  and client ends of the bus.  The channel header area is initialized by
 *  the server, and the remaining information is filled in by the client.
 *  We currently use this for the client to provide various information about
 *  the client devices and client drivers for the server end to see.
 */
#include <linux/uuid.h>
#include "vbusdeviceinfo.h"
#include "channel.h"

/* {193b331b-c58f-11da-95a9-00e08161165f} */
#define ULTRA_VBUS_CHANNEL_PROTOCOL_GUID \
		UUID_LE(0x193b331b, 0xc58f, 0x11da, \
				0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le UltraVbusChannelProtocolGuid =
	ULTRA_VBUS_CHANNEL_PROTOCOL_GUID;

#define ULTRA_VBUS_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment this whenever you insert or delete fields within this channel
* struct.  Also increment whenever you change the meaning of fields within this
* channel struct so as to break pre-existing software.  Note that you can
* usually add fields to the END of the channel struct withOUT needing to
* increment this. */
#define ULTRA_VBUS_CHANNEL_PROTOCOL_VERSIONID 1

#define ULTRA_VBUS_CHANNEL_OK_CLIENT(pChannel, logCtx)       \
	(ULTRA_check_channel_client(pChannel,				\
				    UltraVbusChannelProtocolGuid,	\
				    "vbus",				\
				    sizeof(ULTRA_VBUS_CHANNEL_PROTOCOL), \
				    ULTRA_VBUS_CHANNEL_PROTOCOL_VERSIONID, \
				    ULTRA_VBUS_CHANNEL_PROTOCOL_SIGNATURE, \
				    __FILE__, __LINE__, logCtx))

#define ULTRA_VBUS_CHANNEL_OK_SERVER(actualBytes, logCtx)    \
	(ULTRA_check_channel_server(UltraVbusChannelProtocolGuid,	\
				    "vbus",				\
				    sizeof(ULTRA_VBUS_CHANNEL_PROTOCOL), \
				    actualBytes,			\
				    __FILE__, __LINE__, logCtx))


#pragma pack(push, 1)		/* both GCC and VC now allow this pragma */
typedef struct _ULTRA_VBUS_HEADERINFO {
	u32 structBytes;	/* size of this struct in bytes */
	u32 deviceInfoStructBytes;	/* sizeof(ULTRA_VBUS_DEVICEINFO) */
	u32 devInfoCount;	/* num of items in DevInfo member */
	/* (this is the allocated size) */
	u32 chpInfoByteOffset;	/* byte offset from beginning of this struct */
	/* to the the ChpInfo struct (below) */
	u32 busInfoByteOffset;	/* byte offset from beginning of this struct */
	/* to the the BusInfo struct (below) */
	u32 devInfoByteOffset;	/* byte offset from beginning of this struct */
	/* to the the DevInfo array (below) */
	u8 reserved[104];
} ULTRA_VBUS_HEADERINFO;

typedef struct _ULTRA_VBUS_CHANNEL_PROTOCOL {
	ULTRA_CHANNEL_PROTOCOL ChannelHeader;	/* initialized by server */
	ULTRA_VBUS_HEADERINFO HdrInfo;	/* initialized by server */
	/* the remainder of this channel is filled in by the client */
	ULTRA_VBUS_DEVICEINFO ChpInfo;	/* describes client chipset device and
					 * driver */
	ULTRA_VBUS_DEVICEINFO BusInfo;	/* describes client bus device and
					 * driver */
	ULTRA_VBUS_DEVICEINFO DevInfo[0];	/* describes client device and
						 * driver for */
	/* each device on the bus */
} ULTRA_VBUS_CHANNEL_PROTOCOL;

#define VBUS_CH_SIZE_EXACT(MAXDEVICES) \
	(sizeof(ULTRA_VBUS_CHANNEL_PROTOCOL) + ((MAXDEVICES) * \
						sizeof(ULTRA_VBUS_DEVICEINFO)))
#define VBUS_CH_SIZE(MAXDEVICES) COVER(VBUS_CH_SIZE_EXACT(MAXDEVICES), 4096)

#pragma pack(pop)

#endif
