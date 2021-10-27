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

#include "config.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>

#include "util.h"
#include "nfdump.h"
#include "nfxV3.h"
#include "output_util.h"
#include "output_short.h"

#define IP_STRING_LEN (INET6_ADDRSTRLEN)

static void stringEXgenericFlow(FILE *stream, record_map_t *r) {
EXgenericFlow_t *genericFlow = (EXgenericFlow_t *)r->offsetMap[EXgenericFlowID];

	if ( !genericFlow ) return;

	char datestr1[64], datestr2[64], datestr3[64];
	struct tm *ts;
	time_t when = genericFlow->msecFirst / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr1, "<unknown>", 63);
	} else {
		ts = localtime(&when);
		strftime(datestr1, 63, "%Y-%m-%d %H:%M:%S", ts);
	}

	when = genericFlow->msecLast / 1000LL;
	if ( when == 0 ) {
		strncpy(datestr2, "<unknown>", 63);
	} else {
		ts = localtime(&when);
		strftime(datestr2, 63, "%Y-%m-%d %H:%M:%S", ts);
	}

	if ( genericFlow->msecReceived ) {
		when = genericFlow->msecReceived / 1000LL;
		ts = localtime(&when);
		strftime(datestr3, 63, "%Y-%m-%d %H:%M:%S", ts);
	} else {
		datestr3[0] = '0';
		datestr3[1] = '\0';
	}

	fprintf(stream, 
"  first        =     %13llu [%s.%03llu]\n"
"  last         =     %13llu [%s.%03llu]\n"
"  received at  =     %13llu [%s.%03llu]\n"
"  proto        =               %3u %s\n"
"  tcp flags    =              0x%.2x %s\n"
, (long long unsigned)genericFlow->msecFirst, datestr1, genericFlow->msecFirst % 1000LL
, (long long unsigned)genericFlow->msecLast, datestr2, genericFlow->msecLast % 1000LL
, (long long unsigned)genericFlow->msecReceived, datestr3, (long long unsigned)genericFlow->msecReceived % 1000L
, genericFlow->proto, ProtoString(genericFlow->proto, 0)
, genericFlow->proto == IPPROTO_TCP ? genericFlow->tcpFlags : 0, FlagsString(genericFlow->proto == IPPROTO_TCP ? genericFlow->tcpFlags :0));

	if ( genericFlow->proto == IPPROTO_ICMP || genericFlow->proto == IPPROTO_ICMPV6 ) { // ICMP
		fprintf(stream,
"  ICMP         =              %2u.%-2u type.code\n"
	, genericFlow->icmpType, genericFlow->icmpCode);
	} else {
		fprintf(stream,
"  src port     =             %5u\n"
"  dst port     =             %5u\n"
"  src tos      =               %3u\n"
	, genericFlow->srcPort, genericFlow->dstPort, genericFlow->srcTos);
	}

	fprintf(stream,
"  in packets   =        %10llu\n"
"  in bytes     =        %10llu\n"
	, (unsigned long long)genericFlow->inPackets, (unsigned long long)genericFlow->inBytes);

} // End of EXgenericFlowID

static void stringEXtunIPv4(FILE *stream, record_map_t *r) {
EXtunIPv4_t *tunIPv4 = (EXtunIPv4_t *)r->offsetMap[EXtunIPv4ID];

	if ( !tunIPv4 ) return;

	char as[IP_STRING_LEN], ds[IP_STRING_LEN];
	char sloc[128], dloc[128];
	sloc[0] = '\0';
	dloc[0] = '\0';
	uint32_t src = htonl(tunIPv4->tunSrcAddr);
	uint32_t dst = htonl(tunIPv4->tunDstAddr);
	inet_ntop(AF_INET, &src, as, sizeof(as));
	inet_ntop(AF_INET, &dst, ds, sizeof(ds));

	// LookupLocation(tunIPv4->tun_src_ip.V6, sloc, 128);
	// LookupLocation(tunIPv4->tun_dst_ip.V6, dloc, 128);
	fprintf(stream,
"  tun proto    =               %3u %s\n"
"  tun src addr =  %16s%s%s\n"
"  tun dst addr =  %16s%s%s\n"
	, tunIPv4->tunProto, ProtoString(tunIPv4->tunProto, 0)
	, as, strlen(sloc) ? ": " : "", sloc
	, ds, strlen(dloc) ? ": " : "", dloc);

} // End of stringEXtunIPv4

static void stringEXtunIPv6(FILE *stream, record_map_t *r) {
EXtunIPv6_t *tunIPv6 = (EXtunIPv6_t *)r->offsetMap[EXtunIPv6ID];

	if ( !tunIPv6 ) return;

	char as[IP_STRING_LEN], ds[IP_STRING_LEN];
	uint64_t src[2], dst[2];
	char sloc[128], dloc[128];
	sloc[0] = '\0';
	dloc[0] = '\0';

	src[0] = htonll(tunIPv6->tunSrcAddr[0]);
	src[1] = htonll(tunIPv6->tunSrcAddr[1]);
	dst[0] = htonll(tunIPv6->tunDstAddr[0]);
	dst[1] = htonll(tunIPv6->tunDstAddr[1]);
	inet_ntop(AF_INET6, &src, as, sizeof(as));
	inet_ntop(AF_INET6, &dst, ds, sizeof(ds));

	// LookupLocation(r->tun_src_ip.V6, sloc, 128);
	// LookupLocation(r->tun_dst_ip.V6, dloc, 128);
	fprintf(stream,
"  tun proto    =               %3u %s\n"
"  tun src addr =  %16s%s%s\n"
"  tun dst addr =  %16s%s%s\n"
	, tunIPv6->tunProto, ProtoString(tunIPv6->tunProto, 0)
	, as, strlen(sloc) ? ": " : "", sloc
	, ds, strlen(dloc) ? ": " : "", dloc);

} // End of stringEXtunIPv6

static void stringsEXipv4Flow(FILE *stream, record_map_t *r) {
EXipv4Flow_t *ipv4Flow = (EXipv4Flow_t *)r->offsetMap[EXipv4FlowID];

	if ( !ipv4Flow ) return;

	if ( r->offsetMap[EXtunIPv4ID] ) stringEXtunIPv4(stream, r);
	else if ( r->offsetMap[EXtunIPv6ID] ) stringEXtunIPv6(stream, r);

	char as[IP_STRING_LEN], ds[IP_STRING_LEN];
	uint32_t src = htonl(ipv4Flow->srcAddr);
	uint32_t dst = htonl(ipv4Flow->dstAddr);
	inet_ntop(AF_INET, &src, as, sizeof(as));
	inet_ntop(AF_INET, &dst, ds, sizeof(ds));

	fprintf(stream,
"  src addr     =  %16s\n"
"  dst addr     =  %16s\n"
	, as, ds);

} // End of stringsEXipv4Flow

static void stringsEXipv6Flow(FILE *stream, record_map_t *r) {
EXipv6Flow_t *ipv6Flow = (EXipv6Flow_t *)r->offsetMap[EXipv6FlowID];

	if ( !ipv6Flow ) return;

	if ( r->offsetMap[EXtunIPv4ID] ) stringEXtunIPv4(stream, r);
	else if ( r->offsetMap[EXtunIPv6ID] ) stringEXtunIPv6(stream, r);

	char as[IP_STRING_LEN], ds[IP_STRING_LEN];
	uint64_t src[2], dst[2];
	src[0] = htonll(ipv6Flow->srcAddr[0]);
	src[1] = htonll(ipv6Flow->srcAddr[1]);
	dst[0] = htonll(ipv6Flow->dstAddr[0]);
	dst[1] = htonll(ipv6Flow->dstAddr[1]);
	inet_ntop(AF_INET6, &src, as, sizeof(as));
	inet_ntop(AF_INET6, &dst, ds, sizeof(ds));

	fprintf(stream,
"  src addr     =  %16s\n"
"  dst addr     =  %16s\n"
	, as, ds);

} // End of stringsEXipv6Flow

static void stringsEXflowMisc(FILE *stream, record_map_t *r) {
EXflowMisc_t *flowMisc = (EXflowMisc_t *)r->offsetMap[EXflowMiscID];

	if ( !flowMisc ) return;

	char snet[IP_STRING_LEN] = {0};
	char dnet[IP_STRING_LEN] = {0};
	if ( r->offsetMap[EXipv6FlowID] ) {
		// IPv6
		EXipv6Flow_t *ipv6Flow = (EXipv6Flow_t *)r->offsetMap[EXipv6FlowID];
		inet6_ntop_mask(ipv6Flow->srcAddr, flowMisc->srcMask, snet, sizeof(snet));
 		inet6_ntop_mask(ipv6Flow->dstAddr, flowMisc->dstMask, dnet, sizeof(dnet));
	}
	if ( r->offsetMap[EXipv4FlowID] ) {
		// IPv4
		EXipv4Flow_t *ipv4Flow = (EXipv4Flow_t *)r->offsetMap[EXipv4FlowID];
 		inet_ntop_mask(ipv4Flow->srcAddr, flowMisc->srcMask, snet, sizeof(snet));
 		inet_ntop_mask(ipv4Flow->dstAddr, flowMisc->dstMask, dnet, sizeof(dnet));
	}

	EXgenericFlow_t *genericFlow = (EXgenericFlow_t *)r->offsetMap[EXgenericFlowID];
	uint32_t fwdStatus = 0;
	uint32_t tos	   = 0;
	if ( genericFlow ) {
		fwdStatus = genericFlow->fwdStatus;
		tos = genericFlow->srcTos;
	}

	fprintf(stream,
"  input        =          %8u\n"
"  output       =          %8u\n"
"  src mask     =             %5u %s/%u\n"
"  dst mask     =             %5u %s/%u\n"
"  fwd status   =               %3u\n"
"  dst tos      =               %3u\n"
"  direction    =               %3u\n"
"  biFlow Dir   =              0x%.2x %s\n"
"  end reason   =              0x%.2x %s\n"
	, flowMisc->input, flowMisc->output, 
	  flowMisc->srcMask, snet, flowMisc->srcMask, flowMisc->dstMask, dnet, flowMisc->dstMask, 
	  fwdStatus, tos, flowMisc->dir, flowMisc->biFlowDir, biFlowString(flowMisc->biFlowDir),
	  flowMisc->flowEndReason, FlowEndString(flowMisc->flowEndReason));

} // End of stringsEXflowMisc

static void stringsEXcntFlow(FILE *stream, record_map_t *r) {
EXcntFlow_t *cntFlow = (EXcntFlow_t *)r->offsetMap[EXcntFlowID];

	if ( !cntFlow ) return;

	fprintf(stream,
"  out packets  =        %10llu\n"
"  out bytes    =        %10llu\n"
"  aggr flows   =        %10llu\n"
	, (long long unsigned)cntFlow->outPackets, (long long unsigned)cntFlow->outBytes,
	  (long long unsigned)cntFlow->flows);

} // End of stringEXcntFlow

static void stringsEXvLan(FILE *stream, record_map_t *r) {
EXvLan_t *vLan = (EXvLan_t *)r->offsetMap[EXvLanID];

	if ( !vLan ) return;

	fprintf(stream,
"  src vlan     =             %5u\n"
"  dst vlan     =             %5u\n"
, vLan->srcVlan, vLan->dstVlan);

} // End of stringsEXvLan

static void stringsEXasRouting(FILE *stream, record_map_t *r) {
EXasRouting_t *asRouting = (EXasRouting_t *)r->offsetMap[EXasRoutingID];

	if ( !asRouting ) return;

	fprintf(stream,
"  src as       =             %5u\n"
"  dst as       =             %5u\n"
, asRouting->srcAS, asRouting->dstAS);

} // End of stringsEXasRouting

static void stringsEXbgpNextHopV4(FILE *stream, record_map_t *r) {
EXbgpNextHopV4_t *bgpNextHopV4 = (EXbgpNextHopV4_t *)r->offsetMap[EXbgpNextHopV4ID];

	if ( !bgpNextHopV4 ) return;

	char ip[IP_STRING_LEN];
	ip[0] = 0;
	uint32_t i = htonl(bgpNextHopV4->ip);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  bgp next hop =  %16s\n"
, ip);

} // End of stringsEXbgpNextHopV4

static void stringsEXbgpNextHopV6(FILE *stream, record_map_t *r) {
EXbgpNextHopV6_t *bgpNextHopV6 = (EXbgpNextHopV6_t *)r->offsetMap[EXbgpNextHopV6ID];

	if ( !bgpNextHopV6 ) return;

	char ip[IP_STRING_LEN];
	uint64_t i[2];
	i[0] = htonll(bgpNextHopV6->ip[0]);
	i[1] = htonll(bgpNextHopV6->ip[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  bgp next hop =  %16s\n"
, ip);

} // End of stringsEXbgpNextHopV6

static void stringsEXipNextHopV4(FILE *stream, record_map_t *r) {
EXipNextHopV4_t *ipNextHopV4 = (EXipNextHopV4_t *)r->offsetMap[EXipNextHopV4ID];

	if ( !ipNextHopV4 ) return;

	char ip[IP_STRING_LEN];
	ip[0] = 0;
	uint32_t i = htonl(ipNextHopV4->ip);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip next hop  =  %16s\n"
, ip);

} // End of stringsEXipNextHopV4

static void stringsEXipNextHopV6(FILE *stream, record_map_t *r) {
EXipNextHopV6_t *ipNextHopV6 = (EXipNextHopV6_t *)r->offsetMap[EXipNextHopV6ID];

	if ( !ipNextHopV6 ) return;

	char ip[IP_STRING_LEN];
	uint64_t i[2];
	i[0] = htonll(ipNextHopV6->ip[0]);
	i[1] = htonll(ipNextHopV6->ip[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip next hop  =  %16s\n"
, ip);

} // End of stringsEXipNextHopV6


static void stringsEXipReceivedV4(FILE *stream, record_map_t *r) {
EXipReceivedV4_t *ipReceivedV4 = (EXipReceivedV4_t *)r->offsetMap[EXipReceivedV4ID];

	if ( !ipReceivedV4 ) return;

	char ip[IP_STRING_LEN];
	ip[0] = 0;
	uint32_t i = htonl(ipReceivedV4->ip);
	inet_ntop(AF_INET, &i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip exporter  =  %16s\n"
, ip);

} // End of stringsEXipReceivedV4


static void stringsEXipReceivedV6(FILE *stream, record_map_t *r) {
EXipReceivedV6_t *ipReceivedV6 = (EXipReceivedV6_t *)r->offsetMap[EXipReceivedV6ID];

	if ( !ipReceivedV6 ) return;

	char ip[IP_STRING_LEN];
	uint64_t i[2];
	i[0] = htonll(ipReceivedV6->ip[0]);
	i[1] = htonll(ipReceivedV6->ip[1]);
	inet_ntop(AF_INET6, i, ip, sizeof(ip));
	ip[IP_STRING_LEN-1] = 0;

	fprintf(stream,
"  ip exporter  =  %16s\n"
, ip);

} // End of stringsEXipReceivedV6

typedef void (*funcPrintRecord_t)(FILE *, record_map_t *r);
static funcPrintRecord_t funcPrintRecord[MAXELEMENTS] = {
	NULL,
	stringEXgenericFlow,
	stringsEXipv4Flow,
	stringsEXipv6Flow,
	stringsEXflowMisc,
	stringsEXcntFlow,
	stringsEXvLan,
	stringsEXasRouting,
	stringsEXbgpNextHopV4,
	stringsEXbgpNextHopV6,
	stringsEXipNextHopV4,
	stringsEXipNextHopV6,
	stringsEXipReceivedV4,
	stringsEXipReceivedV6
};

void flow_record_short(FILE *stream, recordHeaderV3_t *recordHeaderV3) {
record_map_t record_map = {0};

	record_map.recordHeader = recordHeaderV3;
	void *p   = (void *)recordHeaderV3;
	// void *eor = p + recordHeaderV3->size;

	elementHeader_t *elementHeader = (elementHeader_t *)(p + sizeof(recordHeaderV3_t));
	for (int i=0; i<recordHeaderV3->numElements; i++ ) {
		uint32_t type = elementHeader->type;
		record_map.offsetMap[type] = (void *)(void *)elementHeader + sizeof(elementHeader_t);
		elementHeader = (elementHeader_t *)((void *)elementHeader + elementHeader->length);
	}

	char elementString[MAXELEMENTS * 5];
	elementString[0] = '\0';
	for (int i=0; i<MAXELEMENTS; i++) {
		if ( record_map.offsetMap[i] ) 
			snprintf(elementString + strlen(elementString), sizeof(elementString) - strlen(elementString), "%u ", i);
	}

	char *type;
	char version[8];
	if ( TestFlag(recordHeaderV3->flags, V3_FLAG_EVENT)) {
		type = "EVENT";
		version[0] = '\0';
	} else {
		if ( recordHeaderV3->nfversion != 0 ) {
			snprintf(version, 8, " v%u", recordHeaderV3->nfversion & 0x0F);
			if ( recordHeaderV3->nfversion & 0x80 ) {
				type = "SFLOW";
			} else if ( recordHeaderV3->nfversion & 0x40 ) {
				type = "PCAP";
			} else {
				type = "NETFLOW";
			}
		} else {
			// compat with previous versions
			type = "FLOW";
			version[0] = '\0';
		}
	}

	fprintf(stream, "\n"
"Flow Record: \n"
"  Flags        =              0x%.2x %s%s%s, %s\n"
"  Elements     =             %5u: %s\n"
"  size         =             %5u\n"
"  engine type  =             %5u\n"
"  engine ID    =             %5u\n"
"  export sysid =             %5u\n"
,	recordHeaderV3->flags, type, version,
	TestFlag(recordHeaderV3->flags, V3_FLAG_ANON) ? " Anonymized" : "", 
	TestFlag(recordHeaderV3->flags, V3_FLAG_SAMPLED) ? "Sampled" : "Unsampled", 
	recordHeaderV3->numElements, elementString, recordHeaderV3->size, recordHeaderV3->engineType, recordHeaderV3->engineID, recordHeaderV3->exporterID);

	for (int i=0; i<MAXELEMENTS; i++) {
		if ( record_map.offsetMap[i] && funcPrintRecord[i]) 
			funcPrintRecord[i](stream, &record_map);
	}

} // flow_record_short


