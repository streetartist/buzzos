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

    /* Try DHCP; fall back to static 10.0.2.15 if it fails */
    net_ip = 0;
    if (net_dhcp() < 0) {
        net_ip = 0x0F02000A; /* 10.0.2.15 */
        dbg("dhcp failed, using static IP\n");
    }
    serial_puts("[net] IP=");
    serial_puthex(net_ip);
    serial_puts("\n");
}

/* --- helpers --- */
static uint16_t bswap16(uint16_t v) { return (v << 8) | (v >> 8); }
static uint32_t bswap32(uint32_t v) {
    return ((uint32_t)bswap16((uint16_t)(v >> 16)))
         | ((uint32_t)bswap16((uint16_t)v) << 16);
}

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
    /* Route through gateway (10.0.2.2) for non-local destinations */
    uint32_t arp_ip = dst_ip;
    if ((dst_ip & 0x00FFFFFF) != (net_ip & 0x00FFFFFF))
        arp_ip = 0x0202000A; /* QEMU user-mode gateway */
    if (arp_resolve(arp_ip, dst_mac) < 0) return -1;

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

/* ================================================================
 *  Raw send / recv wrappers
 * ================================================================ */
int net_send(const void *data, size_t len) {
    return nd->send(nd, data, len);
}

size_t net_recv(void *buf, size_t max) {
    return nd->recv(nd, buf, max);
}

/* ================================================================
 *  Transport-layer checksum (pseudo-header + segment)
 * ================================================================ */
static uint16_t trans_checksum(uint32_t src_ip, uint32_t dst_ip,
                                uint8_t proto, const void *seg, size_t len) {
    uint32_t sum = 0;
    const uint8_t *b;
    b = (const uint8_t *)&src_ip;
    sum += ((uint16_t)b[0] << 8) | b[1];
    sum += ((uint16_t)b[2] << 8) | b[3];
    b = (const uint8_t *)&dst_ip;
    sum += ((uint16_t)b[0] << 8) | b[1];
    sum += ((uint16_t)b[2] << 8) | b[3];
    sum += proto;
    /* Pseudo-header segment length: added as its numeric value (network-order
     * bytes 00 LL read big-endian == len), NOT byte-swapped. */
    sum += (uint16_t)len;
    b = (const uint8_t *)seg;
    for (size_t i = 0; i + 1 < len; i += 2)
        sum += ((uint16_t)b[i] << 8) | b[i + 1];
    if (len & 1) sum += (uint16_t)b[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return bswap16((uint16_t)~sum);
}

/* ================================================================
 *  UDP
 * ================================================================ */
static int udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                    const void *data, size_t len) {
    uint8_t pkt[1514];
    struct udp_hdr *udp = (struct udp_hdr *)pkt;
    udp->src_port = bswap16(src_port);
    udp->dst_port = bswap16(dst_port);
    udp->length   = bswap16((uint16_t)(sizeof(*udp) + len));
    udp->checksum = 0;
    memcpy(pkt + sizeof(*udp), data, len);
    return ip_send(dst_ip, 17, pkt, sizeof(*udp) + len);
}

/* ================================================================
 *  DNS — simple A-record resolver
 * ================================================================ */

static size_t dns_encode_name(uint8_t *out, const char *name) {
    uint8_t *start = out;
    while (*name) {
        const char *dot = name;
        while (*dot && *dot != '.') dot++;
        size_t seg = (size_t)(dot - name);
        if (seg > 63) seg = 63;
        *out++ = (uint8_t)seg;
        for (size_t i = 0; i < seg; i++) *out++ = (uint8_t)name[i];
        name = dot;
        if (*name == '.') name++;
    }
    *out++ = 0;
    return (size_t)(out - start);
}

static const uint8_t *dns_skip_name(const uint8_t *p) {
    for (;;) {
        if ((*p & 0xC0) == 0xC0) return p + 2;
        if (*p == 0) return p + 1;
        p += 1 + *p;
    }
}

int net_dns_resolve(const char *hostname, uint32_t *ip_out) {
    uint8_t qbuf[512];
    memset(qbuf, 0, sizeof(qbuf));
    qbuf[0] = 0x12; qbuf[1] = 0x34;
    qbuf[2] = 0x01; qbuf[3] = 0x00;
    qbuf[4] = 0x00; qbuf[5] = 0x01;
    size_t nlen = dns_encode_name(qbuf + 12, hostname);
    uint8_t *q = qbuf + 12 + nlen;
    q[0] = 0x00; q[1] = 0x01;
    q[2] = 0x00; q[3] = 0x01;
    size_t qlen = (size_t)(q + 4 - qbuf);

    dbg("dns: sending query\n");
    if (udp_send(0x0302000A, 12345, 53, qbuf, qlen) < 0) {
        dbg("dns: udp send failed\n");
        return -1;
    }

    for (int tries = 0; tries < 200; tries++) {
        uint8_t rbuf[1514];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n == 0) { for (volatile int j = 0; j < 50000; j++) {} continue; }
        if (n < sizeof(struct eth_frame) + sizeof(struct ip_hdr) + sizeof(struct udp_hdr))
            continue;
        struct eth_frame *re = (struct eth_frame *)rbuf;
        if (bswap16(re->ethertype) != 0x0800) continue;
        struct ip_hdr *rip = (struct ip_hdr *)re->payload;
        if (rip->protocol != 17) continue;
        struct udp_hdr *rudp = (struct udp_hdr *)(rip + 1);
        if (bswap16(rudp->src_port) != 53) continue;
        if (bswap16(rudp->dst_port) != 12345) continue;

        const uint8_t *dns = (const uint8_t *)(rudp + 1);
        size_t dlen = bswap16(rudp->length) - sizeof(*rudp);
        if (dlen < 12) continue;
        if (dns[0] != 0x12 || dns[1] != 0x34) continue;
        uint16_t ancount = ((uint16_t)dns[6] << 8) | dns[7];
        if (ancount == 0) { dbg("dns: no answers\n"); return -1; }

        const uint8_t *p = dns + 12;
        p = dns_skip_name(p);
        p += 4;

        for (uint16_t i = 0; i < ancount; i++) {
            p = dns_skip_name(p);
            uint16_t rtype = ((uint16_t)p[0] << 8) | p[1];
            uint16_t rdlen = ((uint16_t)p[8] << 8) | p[9];
            if (rtype == 1 && rdlen == 4) {
                *ip_out = ((uint32_t)p[10]) | ((uint32_t)p[11] << 8)
                        | ((uint32_t)p[12] << 16) | ((uint32_t)p[13] << 24);
                serial_puts("[dns] resolved ");
                serial_puts(hostname);
                serial_puts(" -> ");
                serial_puthex(*ip_out);
                serial_puts("\n");
                return 0;
            }
            p += 10 + rdlen;
        }
    }
    dbg("dns: timeout\n");
    return -1;
}

/* ================================================================
 *  Minimal TCP client
 * ================================================================ */

static struct {
    uint32_t dst_ip;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t seq;
    uint32_t ack;
    int      state;
} tcp;

int net_tcp_connect(uint32_t ip, uint16_t port) {
    tcp.dst_ip   = ip;
    tcp.dst_port = port;
    tcp.src_port = 40960;
    tcp.seq      = 0x12345678;
    tcp.ack      = 0;
    tcp.state    = 0;

    /* Send SYN */
    {
        uint8_t pkt[sizeof(struct tcp_hdr)];
        struct tcp_hdr *th = (struct tcp_hdr *)pkt;
        memset(th, 0, sizeof(*th));
        th->src_port = bswap16(tcp.src_port);
        th->dst_port = bswap16(tcp.dst_port);
        th->seq      = bswap32(tcp.seq);
        th->data_off = (uint8_t)(sizeof(*th) / 4) << 4;
        th->flags    = TCP_SYN;
        th->window   = bswap16(1460);
        th->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, th, sizeof(*th));
        if (ip_send(tcp.dst_ip, 6, th, sizeof(*th)) < 0) {
            dbg("tcp: syn send failed\n"); return -1;
        }
    }
    dbg("tcp: syn sent\n");

    /* Wait for SYN-ACK */
    for (int tries = 0; tries < 200; tries++) {
        uint8_t rbuf[1514];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n == 0) { for (volatile int j = 0; j < 50000; j++) {} continue; }
        if (n < sizeof(struct eth_frame) + sizeof(struct ip_hdr) + sizeof(struct tcp_hdr))
            continue;
        struct eth_frame *re = (struct eth_frame *)rbuf;
        if (bswap16(re->ethertype) != 0x0800) continue;
        struct ip_hdr *rip = (struct ip_hdr *)re->payload;
        if (rip->protocol != 6) continue;
        struct tcp_hdr *rth = (struct tcp_hdr *)(rip + 1);
        if (bswap16(rth->src_port) != tcp.dst_port) continue;
        if (bswap16(rth->dst_port) != tcp.src_port) continue;
        if (!(rth->flags & TCP_SYN) || !(rth->flags & TCP_ACK)) continue;

        tcp.ack = bswap32(rth->seq) + 1;
        tcp.seq++;

        /* Send ACK */
        {
            uint8_t ak[sizeof(struct tcp_hdr)];
            struct tcp_hdr *ah = (struct tcp_hdr *)ak;
            memset(ah, 0, sizeof(*ah));
            ah->src_port = bswap16(tcp.src_port);
            ah->dst_port = bswap16(tcp.dst_port);
            ah->seq      = bswap32(tcp.seq);
            ah->ack      = bswap32(tcp.ack);
            ah->data_off = (uint8_t)(sizeof(*ah) / 4) << 4;
            ah->flags    = TCP_ACK;
            ah->window   = bswap16(1460);
            ah->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, ah, sizeof(*ah));
            ip_send(tcp.dst_ip, 6, ah, sizeof(*ah));
        }
        dbg("tcp: connected\n");
        tcp.state = 2;
        return 0;
    }
    dbg("tcp: connect timeout\n");
    return -1;
}

int net_tcp_send(const void *data, size_t len) {
    if (tcp.state != 2) return -1;

    uint8_t pkt[sizeof(struct tcp_hdr) + len];
    struct tcp_hdr *th = (struct tcp_hdr *)pkt;
    memset(th, 0, sizeof(*th));
    th->src_port = bswap16(tcp.src_port);
    th->dst_port = bswap16(tcp.dst_port);
    th->seq      = bswap32(tcp.seq);
    th->ack      = bswap32(tcp.ack);
    th->data_off = (uint8_t)(sizeof(*th) / 4) << 4;
    th->flags    = TCP_PSH | TCP_ACK;
    th->window   = bswap16(1460);
    memcpy(pkt + sizeof(*th), data, len);
    th->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, pkt, sizeof(pkt));

    int ret = ip_send(tcp.dst_ip, 6, pkt, sizeof(pkt));
    if (ret == 0) tcp.seq += (uint32_t)len;
    return ret;
}

int net_tcp_recv(void *buf, size_t max) {
    if (tcp.state != 2) return -1;

    for (int tries = 0; tries < 500; tries++) {
        uint8_t rbuf[1514];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n == 0) { for (volatile int j = 0; j < 50000; j++) {} continue; }
        if (n < sizeof(struct eth_frame) + sizeof(struct ip_hdr) + sizeof(struct tcp_hdr))
            continue;
        struct eth_frame *re = (struct eth_frame *)rbuf;
        if (bswap16(re->ethertype) != 0x0800) continue;
        struct ip_hdr *rip = (struct ip_hdr *)re->payload;
        if (rip->protocol != 6) continue;
        struct tcp_hdr *rth = (struct tcp_hdr *)(rip + 1);
        if (bswap16(rth->src_port) != tcp.dst_port) continue;
        if (bswap16(rth->dst_port) != tcp.src_port) continue;

        if (rth->flags & TCP_RST) { dbg("tcp: rst\n"); tcp.state = 0; return -1; }
        if (rth->flags & TCP_FIN) {
            dbg("tcp: fin received\n");
            uint32_t peer_seq = bswap32(rth->seq);
            tcp.ack = peer_seq + 1;
            /* ACK the FIN */
            {
                uint8_t ak[sizeof(struct tcp_hdr)];
                struct tcp_hdr *ah = (struct tcp_hdr *)ak;
                memset(ah, 0, sizeof(*ah));
                ah->src_port = bswap16(tcp.src_port);
                ah->dst_port = bswap16(tcp.dst_port);
                ah->seq      = bswap32(tcp.seq);
                ah->ack      = bswap32(tcp.ack);
                ah->data_off = (uint8_t)(sizeof(*ah) / 4) << 4;
                ah->flags    = TCP_ACK;
                ah->window   = bswap16(1460);
                ah->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, ah, sizeof(*ah));
                ip_send(tcp.dst_ip, 6, ah, sizeof(*ah));
            }
            /* Send our FIN-ACK */
            {
                uint8_t fn[sizeof(struct tcp_hdr)];
                struct tcp_hdr *fh = (struct tcp_hdr *)fn;
                memset(fh, 0, sizeof(*fh));
                fh->src_port = bswap16(tcp.src_port);
                fh->dst_port = bswap16(tcp.dst_port);
                fh->seq      = bswap32(tcp.seq);
                fh->ack      = bswap32(tcp.ack);
                fh->data_off = (uint8_t)(sizeof(*fh) / 4) << 4;
                fh->flags    = TCP_FIN | TCP_ACK;
                fh->window   = bswap16(1460);
                fh->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, fh, sizeof(*fh));
                ip_send(tcp.dst_ip, 6, fh, sizeof(*fh));
            }
            tcp.state = 0;
            return 0;
        }

        /* Data packet */
        uint8_t doff  = (rth->data_off >> 4) * 4;
        if (doff < sizeof(struct tcp_hdr)) continue;
        const uint8_t *payload = ((const uint8_t *)rth) + doff;
        size_t plen = n - sizeof(struct eth_frame) - sizeof(struct ip_hdr) - doff;
        if (plen == 0) continue;
        if (plen > max) plen = max;
        memcpy(buf, payload, plen);

        uint32_t peer_seq = bswap32(rth->seq);
        tcp.ack = peer_seq + (uint32_t)plen;

        /* Send ACK */
        {
            uint8_t ak[sizeof(struct tcp_hdr)];
            struct tcp_hdr *ah = (struct tcp_hdr *)ak;
            memset(ah, 0, sizeof(*ah));
            ah->src_port = bswap16(tcp.src_port);
            ah->dst_port = bswap16(tcp.dst_port);
            ah->seq      = bswap32(tcp.seq);
            ah->ack      = bswap32(tcp.ack);
            ah->data_off = (uint8_t)(sizeof(*ah) / 4) << 4;
            ah->flags    = TCP_ACK;
            ah->window   = bswap16(1460);
            ah->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, ah, sizeof(*ah));
            ip_send(tcp.dst_ip, 6, ah, sizeof(*ah));
        }
        serial_puts("[tcp] rx data len=");
        serial_puthex((uint32_t)plen);
        serial_puts("\n");
        return (int)plen;
    }
    dbg("tcp: recv timeout\n");
    return -1;
}

void net_tcp_close(void) {
    if (tcp.state != 2) return;
    uint8_t fn[sizeof(struct tcp_hdr)];
    struct tcp_hdr *fh = (struct tcp_hdr *)fn;
    memset(fh, 0, sizeof(*fh));
    fh->src_port = bswap16(tcp.src_port);
    fh->dst_port = bswap16(tcp.dst_port);
    fh->seq      = bswap32(tcp.seq);
    fh->ack      = bswap32(tcp.ack);
    fh->data_off = (uint8_t)(sizeof(*fh) / 4) << 4;
    fh->flags    = TCP_FIN | TCP_ACK;
    fh->window   = bswap16(1460);
    fh->checksum = trans_checksum(net_ip, tcp.dst_ip, 6, fh, sizeof(*fh));
    ip_send(tcp.dst_ip, 6, fh, sizeof(*fh));
    tcp.state = 0;
    dbg("tcp: closed\n");
}

/* ================================================================
 *  HTTP GET
 * ================================================================ */
int net_wget(const char *host, void (*putc)(char)) {
    uint32_t ip;
    if (net_dns_resolve(host, &ip) < 0) {
        dbg("wget: dns failed\n");
        return -1;
    }
    if (net_tcp_connect(ip, 80) < 0) {
        dbg("wget: tcp connect failed\n");
        return -1;
    }

    char req[256];
    int rlen = 0;
    {
        const char *fmt = "GET / HTTP/1.0\r\nHost: ";
        while (*fmt) req[rlen++] = *fmt++;
        const char *h = host;
        while (*h) req[rlen++] = *h++;
        const char *tail = "\r\nUser-Agent: BuzzOS/1.0\r\nConnection: close\r\n\r\n";
        const char *t = tail;
        while (*t) req[rlen++] = *t++;
    }

    dbg("wget: sending request\n");
    if (net_tcp_send(req, (size_t)rlen) < 0) {
        dbg("wget: send failed\n");
        net_tcp_close();
        return -1;
    }

    dbg("wget: receiving...\n");
    char rbuf[2048];
    for (;;) {
        int n = net_tcp_recv(rbuf, sizeof(rbuf));
        if (n < 0) { dbg("wget: recv error\n"); break; }
        if (n == 0) break;
        for (int i = 0; i < n; i++) putc(rbuf[i]);
    }
    net_tcp_close();
    return 0;
}

/* ================================================================
 *  DHCP client (RFC 2131 — minimal 4-step handshake)
 * ================================================================ */

/* DHCP message types */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

/* BOOTP/DHCP fixed header (simplified — we only use what we need). */
struct dhcp_pkt {
    uint8_t  op;          /* 1=request, 2=reply */
    uint8_t  htype;       /* 1=ethernet */
    uint8_t  hlen;        /* 6 */
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;      /* "your" IP (offered to client) */
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t cookie;      /* magic cookie 0x63825363 */
    uint8_t  options[64]; /* DHCP options */
} __attribute__((packed));

#define DHCP_MAGIC 0x63538263  /* 99.130.83.99 in LE */

/* Build and send a raw DHCP frame (broadcast, no IP stack needed). */
static int dhcp_send(uint8_t msg_type, uint32_t xid, uint32_t req_ip) {
    uint8_t frame[sizeof(struct eth_frame) + sizeof(struct ip_hdr)
                  + sizeof(struct udp_hdr) + sizeof(struct dhcp_pkt)];
    memset(frame, 0, sizeof(frame));

    struct eth_frame *eth = (struct eth_frame *)frame;
    struct ip_hdr    *ip  = (struct ip_hdr *)eth->payload;
    struct udp_hdr   *udp = (struct udp_hdr *)(ip + 1);
    struct dhcp_pkt  *dhcp = (struct dhcp_pkt *)(udp + 1);

    /* Ethernet: broadcast */
    memset(eth->dst, 0xFF, 6);
    memcpy(eth->src, net_mac, 6);
    eth->ethertype = bswap16(0x0800);

    /* IP: 0.0.0.0 → 255.255.255.255 */
    size_t udp_total = sizeof(*udp) + sizeof(*dhcp);
    ip->ver_ihl   = 0x45;
    ip->total_len = bswap16((uint16_t)(sizeof(*ip) + udp_total));
    ip->ttl       = 64;
    ip->protocol  = 17;
    ip->src_ip    = 0;
    ip->dst_ip    = 0xFFFFFFFF;
    ip->checksum  = ip_checksum(ip, sizeof(*ip));

    /* UDP: 68 → 67 */
    udp->src_port = bswap16(68);
    udp->dst_port = bswap16(67);
    udp->length   = bswap16((uint16_t)udp_total);
    udp->checksum = 0;  /* optional for IPv4 UDP */

    /* DHCP */
    dhcp->op    = 1;
    dhcp->htype = 1;
    dhcp->hlen  = 6;
    dhcp->xid   = xid;
    dhcp->flags = bswap16(0x8000); /* broadcast flag */
    memcpy(dhcp->chaddr, net_mac, 6);
    dhcp->cookie = DHCP_MAGIC;

    /* Options */
    uint8_t *opt = dhcp->options;
    *opt++ = 53; *opt++ = 1; *opt++ = msg_type;  /* DHCP message type */
    if (msg_type == DHCP_REQUEST && req_ip) {
        *opt++ = 50; *opt++ = 4;                  /* requested IP */
        memcpy(opt, &req_ip, 4); opt += 4;
    }
    *opt++ = 55; *opt++ = 3; *opt++ = 1; *opt++ = 3; *opt++ = 6; /* param req */
    *opt++ = 255;                                 /* end */

    return nd->send(nd, frame, sizeof(frame));
}

/* Wait for a DHCP reply of the given type. Returns the offered IP. */
static uint32_t dhcp_recv(uint8_t expect_type, uint32_t xid) {
    for (int tries = 0; tries < 300; tries++) {
        uint8_t rbuf[1514];
        size_t n = nd->recv(nd, rbuf, sizeof(rbuf));
        if (n == 0) { for (volatile int j = 0; j < 50000; j++) {} continue; }
        if (n < sizeof(struct eth_frame) + sizeof(struct ip_hdr)
               + sizeof(struct udp_hdr) + 240)
            continue;
        struct eth_frame *re = (struct eth_frame *)rbuf;
        if (bswap16(re->ethertype) != 0x0800) continue;
        struct ip_hdr *rip = (struct ip_hdr *)re->payload;
        if (rip->protocol != 17) continue;
        struct udp_hdr *rudp = (struct udp_hdr *)(rip + 1);
        if (bswap16(rudp->src_port) != 67) continue;
        if (bswap16(rudp->dst_port) != 68) continue;

        struct dhcp_pkt *dhcp = (struct dhcp_pkt *)(rudp + 1);
        if (dhcp->op != 2) continue;
        if (dhcp->xid != xid) continue;
        if (dhcp->cookie != DHCP_MAGIC) continue;

        /* Find message type in options */
        uint8_t *opt = dhcp->options;
        uint8_t *end = opt + sizeof(dhcp->options);
        uint8_t found_type = 0;
        while (opt < end && *opt != 255) {
            uint8_t code = *opt++;
            if (code == 0) continue;  /* padding */
            uint8_t olen = *opt++;
            if (code == 53 && olen >= 1) found_type = *opt;
            opt += olen;
        }
        if (found_type != expect_type) continue;

        return dhcp->yiaddr;
    }
    return 0;
}

int net_dhcp(void) {
    uint32_t xid = 0xBEEF0001;

    dbg("dhcp: DISCOVER\n");
    if (dhcp_send(DHCP_DISCOVER, xid, 0) < 0) {
        dbg("dhcp: send failed\n"); return -1;
    }

    uint32_t offered = dhcp_recv(DHCP_OFFER, xid);
    if (!offered) { dbg("dhcp: no OFFER\n"); return -1; }
    serial_puts("[dhcp] offered IP=");
    serial_puthex(offered);
    serial_puts("\n");

    dbg("dhcp: REQUEST\n");
    if (dhcp_send(DHCP_REQUEST, xid, offered) < 0) {
        dbg("dhcp: send failed\n"); return -1;
    }

    uint32_t acked = dhcp_recv(DHCP_ACK, xid);
    if (!acked) { dbg("dhcp: no ACK\n"); return -1; }

    net_ip = acked;
    serial_puts("[dhcp] IP assigned: ");
    serial_puthex(net_ip);
    serial_puts("\n");
    return 0;
}
