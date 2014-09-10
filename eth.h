#ifndef _eth_h_
#define _eth_h_

#define ETH_ADDR_LEN	6		// Ethernet addresses are six bytes long
#define ETH_ADDR_STRLEN	ETH_ADDR_LEN*3	// xx:xx:xx:xx:xx:xx\0

#define ETH_TYPE_IP	0x0800		// IP protocol Ethertype
#define ETH_TYPE_ARP	0x0806		// ARP protocol Ethertype

#define ETH_BCAST	"\xff\xff\xff\xff\xff\xff"
#define IS_ZERO(e)	((e[0]|e[1]|e[2]|e[3]|e[4]|e[5]) == 0)


struct eth_hdr {
	uint8_t		dst[ETH_ADDR_LEN];
	uint8_t		src[ETH_ADDR_LEN];
	uint16_t	type;
} __attribute__ ((__packed__)) __attribute__((__aligned__(4)));


struct warpcore;
struct w_iov;

// see eth.c for documentation of functions
extern void eth_tx_rx_cur(struct warpcore * w, char * const buf,
		          const uint_fast16_t len);

extern bool eth_tx(struct warpcore * w, struct w_iov * const v,
                   const uint_fast16_t len);

extern void eth_rx(struct warpcore * w, char * const buf);

#endif
