#ifndef BUZZOS_NET_H
#define BUZZOS_NET_H

#include <stddef.h>
#include <stdint.h>
#define NET_IO_BASE 0x300
#define NET_IRQ     10

/* Ethernet frame layout */
struct eth_frame {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
    uint8_t  payload[];
} __attribute__((packed));

/* ARP packet */
struct arp_pkt {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

/* IP header */
struct ip_hdr {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

/* ICMP echo */
struct icmp_echo {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
    uint8_t  data[];
} __attribute__((packed));

/* Initialise the NE2000, set up MAC and IP. */
void net_init(void);

/* Send an Ethernet frame. Returns 0 on success. */
int  net_send(const void *data, size_t len);

/* Poll for a received frame. Returns length or 0 if none. */
size_t net_recv(void *buf, size_t max);

/* Send an ICMP echo request and wait for reply. Returns 0 on success. */
int  net_ping(uint32_t ip);

/* Print network status. */
void net_status(void);

/* Our MAC and IP */
extern uint8_t  net_mac[6];
extern uint32_t net_ip;

#endif
