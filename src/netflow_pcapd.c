/*
 *  Copyright (c) 2021, Peter Haag
 *  All rights reserved.
 *  
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions are met:
 *  
 *   * Redistributions of source code must retain the above copyright notice, 
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice, 
 *     this list of conditions and the following disclaimer in the documentation 
 *     and/or other materials provided with the distribution.
 *   * Neither the name of the author nor the names of its contributors may be 
 *     used to endorse or promote products derived from this software without 
 *     specific prior written permission.
 *  
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 *  POSSIBILITY OF SUCH DAMAGE.
 *  
 */

#ifdef HAVE_CONFIG_H 
#include "config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

#include "util.h"
#include "nfdump.h"
#include "nffile.h"
#include "nfxV3.h"
#include "nfnet.h"
#include "output_short.h"
#include "bookkeeper.h"
#include "collector.h"
#include "exporter.h"
#include "netflow_pcapd.h"

typedef struct exporter_pcapd_s {
	// struct exporter_s
	struct exporter_pcapd_s *next;

	// exporter information
	exporter_info_record_t info;	// exporter record nffile

	uint64_t	packets;			// number of packets sent by this exporter
	uint64_t	flows;				// number of flow records sent by this exporter
	uint32_t	sequence_failure;	// number of sequence failues
	uint32_t	padding_errors;		// number of sequence failues

	sampler_t *sampler;				// list of samplers associated with this exporter
	// end of struct exporter_s

} exporter_pcapd_t;

/* module limited globals */
static int printRecord;

static inline exporter_pcapd_t *getExporter(FlowSource_t *fs, pcapd_header_t *header);

/* functions */

#include "nffile_inline.c"

int Init_pcapd(int verbose) {
	printRecord = verbose;
	return 1;
} // End of Init_pcapd

static inline exporter_pcapd_t *getExporter(FlowSource_t *fs, pcapd_header_t *header) {
exporter_pcapd_t **e = (exporter_pcapd_t **)&(fs->exporter_data);
uint16_t	version    = ntohs(header->version);
#define IP_STRING_LEN   40
char ipstr[IP_STRING_LEN];

	// search the matching pcapd exporter
	while ( *e ) {
		if ( (*e)->info.version == version && 
			 (*e)->info.ip.V6[0] == fs->ip.V6[0] && (*e)->info.ip.V6[1] == fs->ip.V6[1]) 
			return *e;
		e = &((*e)->next);
	}

	// nothing found
	*e = (exporter_pcapd_t *)malloc(sizeof(exporter_pcapd_t));
	if ( !(*e)) {
		LogError("Process_pcapd: malloc() error in %s line %d: %s\n", __FILE__, __LINE__, strerror (errno));
		return NULL;
	}
	memset((void *)(*e), 0, sizeof(exporter_pcapd_t));
	(*e)->next	 			= NULL;
	(*e)->info.header.type  = ExporterInfoRecordType;
	(*e)->info.header.size  = sizeof(exporter_info_record_t);
	(*e)->info.version 		= version;
	(*e)->info.id			= 0;
	(*e)->info.ip			= fs->ip;
	(*e)->info.sa_family	= fs->sa_family;
	(*e)->info.sysid 		= 0;
	(*e)->packets			= 0;
	(*e)->flows				= 0;
	(*e)->sequence_failure	= 0;

	if ( fs->sa_family == PF_INET6 ) {
		uint64_t _ip[2];
		_ip[0] = htonll(fs->ip.V6[0]);
		_ip[1] = htonll(fs->ip.V6[1]);
		inet_ntop(AF_INET6, &_ip, ipstr, sizeof(ipstr));
		dbg_printf("Process_pacpd: New IPv6 exporter %s - add EXipReceivedV6\n", ipstr);
	} else {
		uint32_t _ip = htonl(fs->ip.V4);
		inet_ntop(AF_INET, &_ip, ipstr, sizeof(ipstr));
		dbg_printf("Process_pacpd: New IPv4 exporter %s - add EXipReceivedV4\n", ipstr);
	}

	FlushInfoExporter(fs, &((*e)->info));

	LogInfo("Process_pcapd: SysID: %u, New exporter: IP: %s\n", (*e)->info.sysid, ipstr);

	return (*e);

} // End of getExporter

static void *GetExtension(recordHeaderV3_t *recordHeader, int extensionID) {

	size_t recSize  = sizeof(recordHeaderV3_t);
	elementHeader_t *elementHeader = (elementHeader_t *)((void *)recordHeader + recSize);
	void *extension = NULL;
	while (extension == NULL && recSize < recordHeader->size) {
		if ( elementHeader->type == extensionID ) {
			extension = (void *)elementHeader + sizeof(elementHeader_t);
		} else {
			// prevent potential endloess loop with buggy record
			if ( elementHeader->length == 0 )
				return NULL;
			recSize += elementHeader->length;
			elementHeader = (elementHeader_t *)recordHeader + recSize;
		}
	}
	return extension;

} // End of GetExtension

void Process_pcapd(void *in_buff, ssize_t in_buff_cnt, FlowSource_t *fs) {

		// map pacpd data structure to input buffer
		pcapd_header_t *pcapd_header = (pcapd_header_t *)in_buff;

		exporter_pcapd_t *exporter = getExporter(fs, pcapd_header);
		if ( !exporter ) {
			LogError("Process_pcapd: NULL Exporter: Skip pcapd record processing");
			return;
		}
		exporter->packets++;

		// reserve space in output stream for EXipReceivedVx
		uint32_t receivedSize = 0;
		if ( fs->sa_family == PF_INET6 )
			receivedSize = EXipReceivedV6Size;
		else
			receivedSize = EXipReceivedV4Size;

		// this many data to process
		ssize_t size_left = in_buff_cnt;

		// time received for this packet
		uint64_t msecReceived = ((uint64_t)fs->received.tv_sec * 1000LL) + 
							     (uint64_t)((uint64_t)fs->received.tv_usec / 1000LL);

	  	uint16_t count = ntohl(pcapd_header->numRecord);
		uint32_t numRecords = 0;

		if ( (sizeof(pcapd_header_t) + sizeof(recordHeaderV3_t)) > size_left ) {
			LogError("Process_pcapd: Not enough data.");
			dbg_printf("Process_pcapd: Not enough data.");
			return;
		}

		int buffAvail = 0;
		// 1st record
		recordHeaderV3_t *recordHeaderV3 = in_buff + sizeof(pcapd_header_t);
		size_left -= sizeof(pcapd_header_t);
		do {

			// output buffer size check
			if ( recordHeaderV3->size > buffAvail ) {
				buffAvail = CheckBufferSpace(fs->nffile, recordHeaderV3->size + receivedSize);
				if ( buffAvail == 0 ) {
					LogError("Process_pcapd: output buffer size error.");
					dbg_printf("Process_pcapd: output buffer size error.");
					return;
				}
			}

			if ( recordHeaderV3->size > size_left ) {
				LogError("Process_pcapd: record size error.");
				dbg_printf("Process_pcapd: record size error.");
				return;
			}

			// copy record
			memcpy(fs->nffile->buff_ptr, (void *)recordHeaderV3, recordHeaderV3->size);

			// add router IP at the end of copied record
			recordHeaderV3_t *copiedV3 =  fs->nffile->buff_ptr;
			// add router IP

			if ( fs->sa_family == PF_INET6 ) {
	   			PushExtension(copiedV3, EXipReceivedV6, ipReceivedV6);
				ipReceivedV6->ip[0] = fs->ip.V6[0];
				ipReceivedV6->ip[1] = fs->ip.V6[1];
				dbg_printf("Add IPv6 route IP extension\n");
			} else {
	   			PushExtension(copiedV3, EXipReceivedV4, ipReceivedV4);
				ipReceivedV4->ip = fs->ip.V4;
				dbg_printf("Add IPv4 route IP extension\n");
			}

			dbg_printf("Record: %u elements, size: %u\n\n", copiedV3->numElements, copiedV3->size);


			EXgenericFlow_t *genericFlow = GetExtension(recordHeaderV3, EXgenericFlowID);
			if ( genericFlow ) {
				genericFlow->msecReceived = msecReceived;

				// Update stats
				switch (genericFlow->proto) {
					case IPPROTO_ICMP:
						fs->nffile->stat_record->numflows_icmp++;
						fs->nffile->stat_record->numpackets_icmp += genericFlow->inPackets;
						fs->nffile->stat_record->numbytes_icmp   += genericFlow->inBytes;
						// fix odd CISCO behaviour for ICMP port/type in src port
						if ( genericFlow->srcPort != 0 ) {
							uint8_t *s1 = (uint8_t *)&(genericFlow->srcPort);
							uint8_t *s2 = (uint8_t *)&(genericFlow->dstPort);
							s2[0] = s1[1];
							s2[1] = s1[0];
							genericFlow->srcPort = 0;
						}
						break;
					case IPPROTO_TCP:
						fs->nffile->stat_record->numflows_tcp++;
						fs->nffile->stat_record->numpackets_tcp += genericFlow->inPackets;
						fs->nffile->stat_record->numbytes_tcp   += genericFlow->inBytes;
						break;
					case IPPROTO_UDP:
						fs->nffile->stat_record->numflows_udp++;
						fs->nffile->stat_record->numpackets_udp += genericFlow->inPackets;
						fs->nffile->stat_record->numbytes_udp   += genericFlow->inBytes;
						break;
					default:
						fs->nffile->stat_record->numflows_other++;
						fs->nffile->stat_record->numpackets_other += genericFlow->inPackets;
						fs->nffile->stat_record->numbytes_other   += genericFlow->inBytes;
				}
				fs->nffile->stat_record->numflows++;
				fs->nffile->stat_record->numpackets	+= genericFlow->inPackets;
				fs->nffile->stat_record->numbytes	+= genericFlow->inBytes;
			}

			numRecords++;
			exporter->flows++;

			if ( printRecord ) {
			 	flow_record_short(stdout, copiedV3);
			}

			// update size_left
			size_left -= recordHeaderV3->size;

			// advance output
			fs->nffile->buff_ptr += copiedV3->size;

			// update record block
			fs->nffile->block_header->size += copiedV3->size;
			fs->nffile->block_header->NumRecords++;

			// advance input buffer to next flow record
			recordHeaderV3 = (recordHeaderV3_t *)((void *)recordHeaderV3 + recordHeaderV3->size);

		} while (size_left > sizeof(recordHeaderV3_t)); 

		if ( size_left )
			LogInfo("Process_pcapd(): bytes left in buffer: %zu", size_left);

		if ( numRecords != count ) 
			LogInfo("Process_pcapd(): expected %u records, processd: %u", count, numRecords);

	return;

} /* End of Process_pcapd */

