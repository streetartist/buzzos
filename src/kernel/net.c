#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "netdev.h"
#include "serial.h"

static void *memset(void *d, int c, size_t n) { for (size_t i=0;i<n;i++) ((uint8_t*)d)[i]=(uint8_t)c; return d; }
static void *memcpy(void *d, const void *s, size_t n) { for (size_t i=0;i<n;i++) ((uint8_t*)d)[i]=((const uint8_t*)s)[i]; return d; }

uint8_t  net_mac[6];
uint32_t net_ip   = 0x0202000A; /* 10.0.2.2 host → guest is 10.0.2.15 */

static struct netdev *nd;

static void dbg(const char *s) { serial_puts("[net] "); serial_puts(s); }

void net_init(void) {
    ne2000_init_device();
    nd = netdev_get();
    for (int i = 0; i < 6; i++) net_mac[i] = nd->mac[i];
    /* Guest IP: 10.0.2.15 */
    net_ip = 0x0F02000A;
    serial_puts("[net] IP=");
    serial_puthex(net_ip);
    serial_puts("\n");
}

/* --- helpers --- */
static uint16_t bswap16(uint16_t v) { return (v << 8) | (v >> 8); }

static uint16_t ip_checksum(const void *data, size_t len) {
    uint32_t sum = 0;
    const uint8_t *b = (const uint8_t *)data;
    /* sum 16-bit words in network byte order */
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += ((uint16_t)b[i] << 8) | b[i + 1];
    if (len & 1) sum += (uint16_t)b[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    /* Words were summed big-endian, so ~sum is the checksum value in host
     * order. The field is network byte order, so swap before storing (the
     * direct assignment on little-endian x86 would otherwise reverse it). */
    return bswap16((uint16_t)~sum);
}

/* --- ARP --- */
static int arp_resolve(uint32_t ip, uint8_t *mac_out) {
    uint8_t pkt[42];
    struct eth_frame *eth = (struct eth_frame *)pkt;
    struct arp_pkt   *arp = (struct arp_pkt *)eth->payload;

    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, net_mac, 6);
    eth->ethertype = bswap16(0x0806);

    arp->htype = bswap16(1);
    arp->ptype = bswap16(0x0800);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = bswap16(1); /* request */
    memcpy(arp->sha, net_mac, 6);
    arp->spa = net_ip;
    memset(arp->tha, 0, 6);
    arp->tpa = ip;

    nd->send(nd, pkt, sizeof(pkt));
    dbg("arp sent, waiting reply\n");
        uint8_t rbuf[64];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n >= 42) {
            struct eth_frame *re = (struct eth_frame *)rbuf;
            if (bswap16(re->ethertype) == 0x0806) {
                struct arp_pkt *ra = (struct arp_pkt *)re->payload;
                if (bswap16(ra->oper) == 2 && ra->spa == ip) {
                    memcpy(mac_out, ra->sha, 6);
                    return 0;
                }
            }
        }
    dbg("arp resolve timeout\n");
    return -1;
}

/* --- IP send --- */
static int ip_send(uint32_t dst_ip, uint8_t proto, const void *data, size_t len) {
    uint8_t dst_mac[6];
    if (arp_resolve(dst_ip, dst_mac) < 0) return -1;

    uint8_t buf[1514];
    struct eth_frame *eth = (struct eth_frame *)buf;
    struct ip_hdr    *ip  = (struct ip_hdr *)eth->payload;

    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, net_mac, 6);
    eth->ethertype = bswap16(0x0800);

    memset(ip, 0, sizeof(*ip));
    ip->ver_ihl   = 0x45;
    ip->total_len = bswap16((uint16_t)(sizeof(*ip) + len));
    ip->id        = bswap16(1);
    ip->ttl       = 64;
    ip->protocol  = proto;
    ip->src_ip    = net_ip;
    ip->dst_ip    = dst_ip;
    ip->checksum  = ip_checksum(ip, sizeof(*ip));

    memcpy(ip + 1, data, len);
    return nd->send(nd, buf, sizeof(struct eth_frame) + sizeof(*ip) + len);
}
int net_ping(uint32_t ip) {
    struct icmp_echo echo;
    memset(&echo, 0, sizeof(echo));
    echo.type = 8; /* echo request */
    echo.code = 0;
    echo.id   = bswap16(0x42);
    echo.seq  = bswap16(1);
    echo.checksum = 0;
    echo.checksum = ip_checksum(&echo, sizeof(echo));
    int ret = ip_send(ip, 1, &echo, sizeof(echo));
    if (ret < 0) { dbg("ip_send failed\n"); return -1; }
    dbg("ping sent, waiting reply\n");
    for (int tries = 0; tries < 50; tries++) {
        uint8_t rbuf[1514];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n == 0) { for (volatile int j = 0; j < 50000; j++) {} continue; }
        dbg("ping recv loop got packet\n");
        if (n < sizeof(struct eth_frame) + sizeof(struct ip_hdr)) { dbg("too short\n"); continue; }
        struct eth_frame *re = (struct eth_frame *)rbuf;
        uint16_t etype = bswap16(re->ethertype);
        if (etype != 0x0800) {
            serial_puts("[net] ping loop rx non-IP ethertype=");
            serial_puthex(etype);
            serial_puts("\n");
            continue;
        }
        struct ip_hdr *rip = (struct ip_hdr *)re->payload;
        if (rip->protocol != 1) {
            serial_puts("[net] ping loop rx non-ICMP proto=");
            serial_puthex(rip->protocol);
            serial_puts("\n");
            continue;
        }
        if (rip->src_ip != ip) {
            serial_puts("[net] ping loop src_ip mismatch\n");
            continue;
        }
        struct icmp_echo *recho = (struct icmp_echo *)(rip + 1);
        if (recho->type == 0 && bswap16(recho->id) == 0x42) {
            serial_puts("[net] ping reply from ");
            serial_puthex(ip);
            serial_puts("\n");
            return 0;
        }
        serial_puts("[net] ping loop ICMP type=");
        serial_puthex(recho->type);
        serial_puts("\n");
    }
    return -1;
}

void net_status(void) {
    serial_puts("[net] MAC=");
    for (int i=0;i<6;i++){serial_puthex(net_mac[i]);serial_putc(':');}
    serial_puts(" IP="); serial_puthex(net_ip); serial_puts("\n");
}
