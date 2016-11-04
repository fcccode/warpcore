#pragma once

#include <stdint.h>

#define ICMP_TYPE_ECHOREPLY 0
#define ICMP_TYPE_UNREACH 3
#define ICMP_TYPE_ECHO 8

#define ICMP_UNREACH_PROTOCOL 2 // bad protocol
#define ICMP_UNREACH_PORT 3     // bad port

struct icmp_hdr {
    uint8_t type;   // type of message
    uint8_t code;   // type sub code
    uint16_t cksum; // ones complement checksum of struct
} __aligned(4);


struct warpcore;

// see icmp.c for documentation of functions
extern void
icmp_tx_unreach(struct warpcore * w, const uint8_t code, void * const buf);

extern void icmp_rx(struct warpcore * w, void * const buf);