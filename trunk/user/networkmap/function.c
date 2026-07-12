#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#include <netutils.h>
#include <include/ibox.h>

#include "networkmap.h"

extern unsigned int scan_now;

#define MDNS_PORT 5353
#define MDNS_GROUP "224.0.0.251"

// DNS 头部结构体 (12 字节)
struct DNS_HEADER {
	uint16_t id;
	uint16_t flags;
	uint16_t q_count;
	uint16_t ans_count;
	uint16_t auth_count;
	uint16_t add_count;
};

// 将普通的字符串转换为 DNS 报文中的 Label 格式
static void 
ChangetoDnsNameFormat(unsigned char* dns, unsigned char* host) {
	int lock = 0, i;
	strcat((char*)host, ".");
	for (i = 0; i < strlen((char*)host); i++) {
		if (host[i] == '.') {
			*dns++ = i - lock;
			for (; lock < i; lock++) {
				*dns++ = host[lock];
			}
			lock++;
		}
	}
	*dns++ = '\0';
}

// 深度解析 DNS Name（支持处理 DNS 压缩指针 0xC0xx）
static unsigned char* 
ParseDnsName(unsigned char* reader, unsigned char* buffer, char* out_name, int* count) {
	unsigned char* out_ptr = (unsigned char*)out_name;
	unsigned int jumped = 0, offset;
	int i;

	*count = 0;
	while (*reader != 0) {
		if (*reader >= 192) { // 发现 DNS 压缩指针 (0xC0)
			offset = ((*reader) & 0x3F) << 8 | *(reader + 1);
			reader = buffer + offset;
			jumped = 1;
		} else {
			unsigned int len = *reader;
			reader++;
			if (!jumped) (*count)++;

			for (i = 0; i < len; i++) {
				*out_ptr++ = *reader++;
				if (!jumped) (*count)++;
			}
			*out_ptr++ = '.';
		}
	}

	if (jumped) {
		(*count) += 2;
	} else {
		(*count) += 1;
	}

	if (out_ptr > (unsigned char*)out_name) {
		*(out_ptr - 1) = '\0';

		size_t name_len = strlen(out_name);
		if (name_len >= 6) {
			char *suffix = out_name + name_len - 6;
			if (strcasecmp(suffix, ".local") == 0) {
				*suffix = '\0'; // 匹配成功，直接在 ".local" 的 '.' 位置截断字符串
			}
		}
	}

	return reader;
}

/***** mDNS Name Query function *****/
static int
mdns_query(struct in_addr *dst_ip, NET_CLIENT *pnet_client)
{
	int sock_mdns, res_len, reuse, q_count, ans_count, consumed;
	struct sockaddr_in own_saddr, dst_saddr, from_addr;
	socklen_t from_len;
	struct timeval timeout;
	struct ifreq ifr;
	unsigned char buf[1024];
	unsigned char qname[256];
	unsigned char *ip = (unsigned char *)&dst_ip->s_addr;
	int ip_parts[4];

	// 1. 转换 IP 格式并构建逆向解析域名
	snprintf((char*)qname, sizeof(qname), "%d.%d.%d.%d.in-addr.arpa", 
			ip[3], ip[2], ip[1], ip[0]);

	// 2. 创建 UDP 套接字
	sock_mdns = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_mdns < 0) {
		NMP_DEBUG_M("mDNS: socket error.\n");
		return -1;
	}

	reuse = 1;
	setsockopt(sock_mdns, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
	setsockopt(sock_mdns, SOL_SOCKET, SO_REUSEPORT, (char*)&reuse, sizeof(reuse));
#endif

	// 强制将套接字绑定到 Padavan 的 LAN 桥接网卡 IFNAME_BR (br0)
	memset(&ifr, 0, sizeof(ifr));
	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", IFNAME_BR);
	if (setsockopt(sock_mdns, SOL_SOCKET, SO_BINDTODEVICE, (char *)&ifr, sizeof(ifr)) < 0) {
		NMP_DEBUG_M("mDNS: bind to device %s failed\n", IFNAME_BR);
		close(sock_mdns);
		return -1;
	}

	// 3. 绑定本地 5353 端口
	memset(&own_saddr, 0, sizeof(own_saddr));
	own_saddr.sin_family = AF_INET;
	own_saddr.sin_port = htons(MDNS_PORT);
	own_saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock_mdns, (struct sockaddr *)&own_saddr, sizeof(own_saddr)) < 0) {
		NMP_DEBUG_M("mDNS: bind 5353 error.\n");
		close(sock_mdns);
		return -1;
	}

	// 加入组播组
	struct ip_mreqn mreqn;
	memset(&mreqn, 0, sizeof(mreqn));
	mreqn.imr_multiaddr.s_addr = inet_addr(MDNS_GROUP);
	mreqn.imr_address.s_addr   = htonl(INADDR_ANY);
	mreqn.imr_ifindex          = if_nametoindex(IFNAME_BR); // 显式指定 br0 的接口索引

	if (setsockopt(sock_mdns, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn, sizeof(mreqn)) < 0) {
		NMP_DEBUG_M("mDNS: join multicast group error.\n");
		close(sock_mdns);
		return -1;
	}

	// 设置 800ms 的接收超时
	timeout.tv_sec = 0;
	timeout.tv_usec = 800000;
	setsockopt(sock_mdns, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	// 4. 构建 mDNS 查询报文
	memset(&dst_saddr, 0, sizeof(dst_saddr));
	dst_saddr.sin_family = AF_INET;
	dst_saddr.sin_port = htons(MDNS_PORT);
	dst_saddr.sin_addr.s_addr = inet_addr(MDNS_GROUP);

	memset(buf, 0, sizeof(buf));
	struct DNS_HEADER *dns = (struct DNS_HEADER *)buf;
	dns->id = htons(0x0000);
	dns->flags = htons(0x0000);
	dns->q_count = htons(1);

	unsigned char *qdata = buf + sizeof(struct DNS_HEADER);
	ChangetoDnsNameFormat(qdata, qname);
	qdata = qdata + strlen((const char*)qdata) + 1;

	uint16_t qtype = htons(0x000c);  // PTR 记录
	uint16_t qclass = htons(0x0001); // IN Class
	memcpy(qdata, &qtype, 2);  qdata += 2;
	memcpy(qdata, &qclass, 2); qdata += 2;

	// 5. 发送查询
	if (sendto(sock_mdns, buf, (qdata - buf), 0, (struct sockaddr*)&dst_saddr, sizeof(dst_saddr)) == -1) {
		NMP_DEBUG_M("mDNS: send error!\n");
		close(sock_mdns);
		return -1;
	}

	while (1) {
		from_len = sizeof(from_addr);
		res_len = recvfrom(sock_mdns, buf, sizeof(buf), 0, (struct sockaddr*)&from_addr, &from_len);
		if (res_len < 0) {
			break; // 超时退出
		}

		struct DNS_HEADER *res_dns = (struct DNS_HEADER *)buf;

		ans_count = ntohs(res_dns->ans_count);
		q_count = ntohs(res_dns->q_count);

		if (ans_count == 0) continue;

		unsigned char* reader = buf + sizeof(struct DNS_HEADER);
		char dummy_name[256];

		for (int i = 0; i < q_count; i++) {
			ParseDnsName(reader, buf, dummy_name, &consumed);
			reader += (consumed + 4);
		}

		for (int i = 0; i < ans_count; i++) {
			ParseDnsName(reader, buf, dummy_name, &consumed);
			reader += consumed;

			uint16_t type = ntohs(*(uint16_t*)reader);   reader += 2;
			uint16_t class = ntohs(*(uint16_t*)reader);  reader += 2;
			reader += 4;
			uint16_t rdlength = ntohs(*(uint16_t*)reader); reader += 2;

			if (type == 12) {
				char hostname[256] = {0};
				ParseDnsName(reader, buf, hostname, &consumed);

				if (hostname[0]) {
					NMP_DEBUG_M("mDNS Name: %s\n", hostname);
					if (!pnet_client->device_name[0] && is_valid_hostname(hostname)) {
						memcpy(pnet_client->device_name, hostname, 18);
						pnet_client->device_name[18] = 0;
					}
					close(sock_mdns);
					return 0;
				}
			}
			reader += rdlength;
		}
	}

	close(sock_mdns);
	NMP_DEBUG_M("mDNS timeout...\n");
	return -1;
}

/***** ASUS routers detect function *****/
static int
asus_dd_query(struct in_addr *dst_ip, NET_CLIENT *pnet_client)
{
	// （保持你原有的代码逻辑不变...）
	char buffer[512];
	struct sockaddr_in own_saddr, dst_saddr;
	struct timeval timeout;
	int sock_dd, recvlen, retry, dd_result, flag_one;
	IBOX_COMM_PKT_HDR_EX dd_hdr;

	sock_dd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_dd < 0)
	{
		NMP_DEBUG("DD: socket error.\n");
		return -1;
	}

	flag_one = 1;
	setsockopt(sock_dd, SOL_SOCKET, SO_REUSEADDR, (char *)&flag_one, sizeof(flag_one));

	memset(&own_saddr, 0, sizeof(own_saddr));
	own_saddr.sin_family = AF_INET;
	own_saddr.sin_port = htons(IBOX_CLI_PORT);
	own_saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock_dd, (struct sockaddr *)&own_saddr, sizeof(own_saddr)) < 0) {
		close(sock_dd);
		NMP_DEBUG_M("DD: bind error!\n");
		return -1;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = 500000;
	setsockopt(sock_dd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(sock_dd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	memset(&dst_saddr, 0, sizeof(dst_saddr));
	memcpy(&dst_saddr.sin_addr, dst_ip, sizeof(struct in_addr));
	dst_saddr.sin_family = AF_INET;
	dst_saddr.sin_port = htons(IBOX_SRV_PORT);

	memset(&dd_hdr, 0, sizeof(dd_hdr));
	dd_hdr.ServiceID = NET_SERVICE_ID_IBOX_INFO;
	dd_hdr.PacketType = NET_PACKET_TYPE_CMD;
	dd_hdr.OpCode = NET_CMD_ID_GETINFO;

	dd_result = 0;

	for (retry = 0; retry < 2; retry++) {
		if (sendto(sock_dd, (char *)&dd_hdr, sizeof(dd_hdr), 0, (struct sockaddr*)&dst_saddr, sizeof(dst_saddr)) == -1) {
			NMP_DEBUG_M("DD: send error!\n");
			break;
		}
		
		bzero(buffer, sizeof(buffer));
		recvlen = recv(sock_dd, buffer, sizeof(buffer), 0);
		if (recvlen >= (int)(sizeof(IBOX_COMM_PKT_RES)+sizeof(PKT_GET_INFO))) {
			IBOX_COMM_PKT_RES_EX *dd_res = (IBOX_COMM_PKT_RES_EX *)buffer;
			
			if (dd_res->ServiceID == NET_SERVICE_ID_IBOX_INFO &&
				dd_res->PacketType == NET_PACKET_TYPE_RES &&
				dd_res->OpCode == NET_CMD_ID_GETINFO) {
				PKT_GET_INFO *dd_info = (PKT_GET_INFO *)(buffer+sizeof(IBOX_COMM_PKT_RES));
				
				dd_result = 1;
				pnet_client->type = 3; 
				
				NMP_DEBUG_M("DD: productID=%s\n", dd_info->ProductID);
				if (!pnet_client->device_name[0] && is_valid_hostname(dd_info->ProductID)) {
					memcpy(pnet_client->device_name, dd_info->ProductID, 18);
					pnet_client->device_name[18] = 0;
				}
				break;
			}
		}
		usleep(100000);
	}

	close(sock_dd);

	if (!dd_result) {
		NMP_DEBUG_M("DD timeout...\n");
		return -1;
	}
	return 0;
}

/***** HTTP Server detect function *****/
static int
http_query(struct in_addr *dst_ip, NET_CLIENT *pnet_client)
{
	// （保持你原有的代码逻辑不变...）
	int sock_http, recvlen;
	struct sockaddr_in dst_saddr;
	struct timeval timeout = {1, 0};
	char buffer[1024] = {0};

	sock_http = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_http < 0)
	{
		NMP_DEBUG("HTTP: socket error.\n");
		return -1;
	}

	memset(&dst_saddr, 0, sizeof(dst_saddr));
	memcpy(&dst_saddr.sin_addr, dst_ip, sizeof(struct in_addr));
	dst_saddr.sin_family = AF_INET;
	dst_saddr.sin_port = htons(HTTP_PORT);

	setsockopt(sock_http, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(sock_http, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	if (connect(sock_http, (struct sockaddr*)&dst_saddr, sizeof(dst_saddr))== -1)
	{
		close(sock_http);
		NMP_DEBUG_M("HTTP: connect error!\n");
		return -1;
	}

	snprintf(buffer, sizeof(buffer), "GET / HTTP/1.1\r\nHost: %s\r\n\r\n", inet_ntoa(dst_saddr.sin_addr));
	if (send(sock_http, buffer, strlen(buffer), 0) == -1)
	{
		close(sock_http);
		NMP_DEBUG_M("HTTP: send error!\n");
		return -1;
	}

	bzero(buffer, sizeof(buffer));
	recvlen = recv(sock_http, buffer, sizeof(buffer)-1, 0);
	if (recvlen > 12)
	{
		NMP_DEBUG_M("Check http response: %s\n", buffer);
		if (!memcmp(buffer, "HTTP/1.", 7) &&
		   (!memcmp(buffer+9, "2", 1) || !memcmp(buffer+9, "3", 1) || !memcmp(buffer+9, "401", 3)) )
		{
			NMP_DEBUG("Found HTTP!\n");
			pnet_client->http = 1;
		}
	}

	close(sock_http);
	return 0;
}

/***** NBNS Name Query function *****/
static int
nbns_query(struct in_addr *src_ip, struct in_addr *dst_ip, NET_CLIENT *pnet_client)
{
	// （保持你原有的代码逻辑不变...）
	struct sockaddr_in own_saddr, dst_saddr, other_addr2;
	int sock_nbns, recvlen, retry, nbns_result, flag_one;
	socklen_t other_addr_len2;
	struct timeval timeout;
	char recvbuf[512], device_name[18];
	char sendbuf[50] = {0x87, 0x96, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
			    0x00, 0x00, 0x00, 0x00, 0x20, 0x43, 0x4b, 0x41,
			    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
			    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
			    0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
			    0x41, 0x41, 0x41, 0x41, 0x41, 0x00, 0x00, 0x21,
			    0x00, 0x01};

	sock_nbns = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_nbns < 0)
	{
		NMP_DEBUG_M("NBNS: socket error.\n");
		return -1;
	}

	flag_one = 1;
	setsockopt(sock_nbns, SOL_SOCKET, SO_REUSEADDR, (char*)&flag_one, sizeof(flag_one));

	memset(&own_saddr, 0, sizeof(own_saddr));
	memcpy(&own_saddr.sin_addr, src_ip, sizeof(struct in_addr));
	own_saddr.sin_family = AF_INET;
	own_saddr.sin_port = htons(NBNS_PORT);

	if (bind(sock_nbns, (struct sockaddr *)&own_saddr, sizeof(own_saddr)) < 0)
	{
		NMP_DEBUG_M("NBNS: bind error.\n");
		close(sock_nbns);
		return -1;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = 500000;
	setsockopt(sock_nbns, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	setsockopt(sock_nbns, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	memset(&dst_saddr, 0, sizeof(dst_saddr));
	memcpy(&dst_saddr.sin_addr, dst_ip, sizeof(struct in_addr));
	dst_saddr.sin_family = AF_INET;
	dst_saddr.sin_port = htons(NBNS_PORT);

	nbns_result = 0;
	bzero(device_name, sizeof(device_name));

	for (retry = 0; retry < 3; retry++)
	{
		if (sendto(sock_nbns, sendbuf, sizeof(sendbuf), 0, (struct sockaddr*)&dst_saddr, sizeof(dst_saddr)) == -1) {
			NMP_DEBUG_M("NBNS: send error!\n");
			break;
		}
		
		other_addr_len2 = sizeof(other_addr2);
		memset(&other_addr2, 0, sizeof(other_addr2));
		bzero(recvbuf, sizeof(recvbuf));
		recvlen = recvfrom(sock_nbns, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&other_addr2, &other_addr_len2);
		if (recvlen > 74)
		{
			NBNS_RESPONSE *nbns_rsp = (NBNS_RESPONSE *)recvbuf;
			if (((nbns_rsp->flags[0]>>4) == 8) && (nbns_rsp->number_of_names > 0) &&
			     (other_addr2.sin_addr.s_addr == dst_saddr.sin_addr.s_addr))
			{
				nbns_result = 1;
				if ( !(nbns_rsp->name_flags1[0] & 0x80) )
					memcpy(device_name, nbns_rsp->device_name1, 16);
				else if ( nbns_rsp->number_of_names > 1 && !(nbns_rsp->name_flags2[0] & 0x80) )
					memcpy(device_name, nbns_rsp->device_name2, 16);
				else if ( nbns_rsp->number_of_names > 2 && !(nbns_rsp->name_flags3[0] & 0x80) )
					memcpy(device_name, nbns_rsp->device_name3, 16);
				else if ( nbns_rsp->number_of_names > 3 && !(nbns_rsp->name_flags4[0] & 0x80) )
					memcpy(device_name, nbns_rsp->device_name4, 16);
				
				break;
			}
		}
		usleep(100000);
	}

	close(sock_nbns);

	if (!nbns_result) {
		NMP_DEBUG_M("NBNS timeout...\n");
		return -1;
	}

	if (pnet_client->type == 6)
		pnet_client->type = 1; 

	if (device_name[0]) {
		device_name[17] = 0;
		NMP_DEBUG("NBNS Name: %s\n", device_name);
		if (!pnet_client->device_name[0] && is_valid_hostname(device_name))
			memcpy(pnet_client->device_name, device_name, 18);
	} else {
		NMP_DEBUG("NBNS: NO hostname!\n");
	}

	return 0;
}

void
find_all_app(struct in_addr *src_ip, struct in_addr *dst_ip, NET_CLIENT *pnet_client)
{
	/* 1. check ASUS AP */
	asus_dd_query(dst_ip, pnet_client);

	if (scan_now == 0)
		return;

	/* 🌟2. 优先进行 mDNS 组播查询（针对现代苹果、智能家居及现代 Linux 设备） */
	if (!pnet_client->device_name[0])
		mdns_query(dst_ip, pnet_client);

	if (scan_now == 0)
		return;

	/* 3. check SMB server (NBNS 查询) */
	if (pnet_client->type == 6 || !pnet_client->device_name[0])
		nbns_query(src_ip, dst_ip, pnet_client);

	if (scan_now == 0)
		return;

	/* 4. check http server */
	http_query(dst_ip, pnet_client);
}
