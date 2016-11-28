#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __linux__
#include <netinet/ether.h>
#else
#include <net/ethernet.h>
#endif

// #include "arp.h"
#include "backend.h"
#include "plat.h"
#include "util.h"
#include "version.h"


/// A global list of netmap engines that have been initialized for different
/// interfaces.
///
static SLIST_HEAD(engines, warpcore) wc = SLIST_HEAD_INITIALIZER(wc);


/// Allocate a w_iov chain for @p payload bytes, for eventual use with w_tx().
/// Must be later freed with w_free().
///
/// @param      w     Warpcore engine.
/// @param[in]  len   Amount of payload bytes in the returned chain.
///
/// @return     Chain of w_iov structs.
///
struct w_iov * __attribute__((nonnull))
w_alloc(struct warpcore * const w, const uint32_t len)
{
    assert(len, "len is zero");
    struct w_iov * v = 0;
    int32_t l = (int32_t)len;
    uint32_t n = 0;
    STAILQ_HEAD(vh, w_iov) chain = STAILQ_HEAD_INITIALIZER(chain);
    while (l > 0) {
        v = STAILQ_FIRST(&w->iov);
        assert(v != 0, "out of spare bufs after grabbing %d", n);
        STAILQ_REMOVE_HEAD(&w->iov, next);
        // warn(debug, "allocating spare buf %u to app", v->idx);
        v->buf = IDX2BUF(w, v->idx) + sizeof(struct w_hdr);
        v->len = w->mtu - sizeof(struct w_hdr);
        l -= v->len;
        n++;
        STAILQ_INSERT_TAIL(&chain, v, next);
    }

    // adjust length of last iov so chain is the exact length requested
    v->len += l; // l is negative

    warn(info, "allocating iov (len %d in %d bufs) for user tx", len, n);
    return STAILQ_FIRST(&chain);
}


/// Return a w_iov chain obtained via w_alloc() or w_rx() back to warpcore. The
/// application must not use @p v after this call.
///
/// Do not make this __attribute__((nonnull)), so the caller doesn't have to
/// check v.
///
/// @param      w     Warpcore engine.
/// @param      v     w_iov to return.
///
void w_free(struct warpcore * const w, struct w_iov * v)
{
    while (v) {
        // move v to the available iov list
        // warn(debug, "returning buf %u to spare list", v->idx);
        struct w_iov * const n = STAILQ_NEXT(v, next);
        STAILQ_INSERT_HEAD(&w->iov, v, next);
        v = n;
    };
}


/// Return the total payload length of w_iov chain @p v.
///
/// Do not make this __attribute__((nonnull)), so the caller doesn't have to
/// check v.
///
/// @param[in]  v     A w_iov chain.
///
/// @return     Sum of the payload lengths of the w_iov structs in @p v.
///
uint32_t w_iov_len(const struct w_iov * const v)
{
    uint32_t l = 0;
    for (const struct w_iov * i = v; i; i = STAILQ_NEXT(i, next))
        l += i->len;
    return l;
}


/// Connect a bound socket to a remote IP address and port. Depending on the
/// backend, this function may block until a MAC address has been resolved with
/// ARP.
///
/// Calling w_connect() will make subsequent w_tx() operations on the w_sock
/// enqueue payload data towards that destination. Unlike with the Socket API,
/// w_connect() can be called several times, which will re-bind a connected
/// w_sock and allows a server application to send data to multiple peers over a
/// w_sock.
///
/// @param      s      w_sock to connect.
/// @param[in]  dip    Destination IPv4 address to bind to.
/// @param[in]  dport  Destination UDP port to bind to.
///
void __attribute__((nonnull))
w_connect(struct w_sock * const s, const uint32_t dip, const uint16_t dport)
{
    s->hdr.ip.dst = dip;
    s->hdr.udp.dport = dport;
    backend_connect(s);

    warn(notice, "IP proto %d socket connected to %s port %d", s->hdr.ip.p,
         inet_ntoa(*(const struct in_addr * const) & dip), ntohs(dport));
}


void __attribute__((nonnull)) w_disconnect(struct w_sock * const s)
{
    s->hdr.ip.dst = 0;
    s->hdr.udp.dport = 0;

    warn(notice, "IP proto %d socket disconnected", s->hdr.ip.p);
}


/// Bind a w_sock to the given local UDP port number.
///
/// @param      w     The w_sock to bind.
/// @param[in]  port  The local port number to bind to, in network byte order.
///
/// @return     Pointer to a bound w_sock.
///
struct w_sock * __attribute__((nonnull))
w_bind(struct warpcore * const w, const uint16_t port)
{
    struct w_sock * s = w->udp[port];
    if (s) {
        warn(warn, "UDP source port %d already in bound", ntohs(port));
        return s;
    }

    assert((w->udp[port] = s = calloc(1, sizeof(struct w_sock))) != 0,
           "cannot allocate w_sock");

    // initialize the non-zero fields of outgoing template header
    s->hdr.eth.type = ETH_TYPE_IP;
    memcpy(&s->hdr.eth.src, w->mac, ETH_ADDR_LEN);
    // s->hdr.eth.dst is set on w_connect()

    s->hdr.ip.vhl = (4 << 4) + 5;
    s->hdr.ip.ttl = 1; // XXX TODO: pick something sensible
    s->hdr.ip.off |= htons(IP_DF);
    s->hdr.ip.p = IP_P_UDP;
    s->hdr.ip.src = w->ip;
    s->hdr.udp.sport = port;
    // s->hdr.ip.dst is set on w_connect()

    s->w = w;
    STAILQ_INIT(&s->iv);
    SLIST_INSERT_HEAD(&w->sock, s, next);

    backend_bind(s);

    warn(notice, "IP proto %d socket bound to port %d", s->hdr.ip.p,
         ntohs(port));

    return s;
}


/// Close a warpcore socket. This dequeues all data from w_sock::iv and
/// w_sock::ov, i.e., data will *not* be placed in rings and sent.
///
/// @param      s     w_sock to close.
///
void __attribute__((nonnull)) w_close(struct w_sock * const s)
{
    // make iovs of the socket available again
    while (!STAILQ_EMPTY(&s->iv)) {
        struct w_iov * const v = STAILQ_FIRST(&s->iv);
        warn(debug, "free iv buf %u", v->idx);
        STAILQ_REMOVE_HEAD(&s->iv, next);
        STAILQ_INSERT_HEAD(&s->w->iov, v, next);
    }

    // remove the socket from list of sockets
    SLIST_REMOVE(&s->w->sock, s, w_sock, next);

    // free the socket
    s->w->udp[s->hdr.udp.sport] = 0;
    free(s);
}


/// Shut a warpcore engine down cleanly. In addition to calling into the
/// backend-specific cleanup function, it frees up the extra buffers and other
/// memory structures.
///
/// @param      w     Warpcore engine.
///
void __attribute__((nonnull)) w_cleanup(struct warpcore * const w)
{
    warn(notice, "warpcore shutting down");
    backend_cleanup(w);
    free(w->bufs);
    free(w->udp);
    SLIST_REMOVE(&wc, w, warpcore, next);
    free(w);
}


/// Initialize a warpcore engine on the given interface. Ethernet and IPv4
/// source addresses and related information, such as the netmask, are taken
/// from the active OS configuration of the interface. A default router,
/// however, needs to be specified with @p rip, if communication over a WAN is
/// desired.
///
/// Since warpcore relies on random() to generate random values, the caller
/// should also set an initial seed with srandom() or srandomdev(). Warpcore
/// does not do this, to allow the application control over the seed.
///
/// @param[in]  ifname  The OS name of the interface (e.g., "eth0").
/// @param[in]  rip     The default router to be used for non-local
///                     destinations. Can be zero.
///
/// @return     Initialized warpcore engine.
///
struct warpcore * __attribute__((nonnull))
w_init(const char * const ifname, const uint32_t rip)
{
    struct warpcore * w;
    bool link_up = false;

    // SLIST_FOREACH (w, &wc, next)
    //     assert(strcmp(ifname, w->req.nr_name),
    //            "can only have one warpcore engine active on %s", ifname);

    // allocate engine struct
    assert((w = calloc(1, sizeof(struct warpcore))) != 0,
           "cannot allocate struct warpcore");

    // we mostly loop here because the link may be down
    // mpbs can be zero on generic platforms
    while (link_up == false || IS_ZERO(w->mac) || w->mtu == 0 || w->ip == 0 ||
           w->mask == 0) {

        // get interface information
        struct ifaddrs * ifap;
        assert(getifaddrs(&ifap) != -1, "%s: cannot get interface information",
               ifname);

        bool found = false;
        for (const struct ifaddrs * i = ifap; i->ifa_next; i = i->ifa_next) {
            if (strcmp(i->ifa_name, ifname) != 0)
                continue;
            else
                found = true;

            switch (i->ifa_addr->sa_family) {
            case AF_LINK:
                plat_get_mac(w->mac, i);
                w->mtu = plat_get_mtu(i);
#ifndef NDEBUG
                uint32_t mbps = plat_get_mbps(i);
#endif
                link_up = plat_get_link(i);
                warn(notice, "%s addr %s, MTU %d, speed %uG, link %s",
                     i->ifa_name,
                     ether_ntoa((const struct ether_addr * const)w->mac),
                     w->mtu, mbps / 1000, link_up ? "up" : "down");
                break;
            case AF_INET:
                // get IP addr and netmask
                if (!w->ip)
                    w->ip = ((struct sockaddr_in *)(void *)i->ifa_addr)
                                ->sin_addr.s_addr;
                if (!w->mask)
                    w->mask = ((struct sockaddr_in *)(void *)i->ifa_netmask)
                                  ->sin_addr.s_addr;
                break;
            default:
                warn(notice, "ignoring unknown addr family %d on %s",
                     i->ifa_addr->sa_family, i->ifa_name);
                break;
            }
        }
        freeifaddrs(ifap);
        assert(found, "unknown interface %s", ifname);

        // sleep for a bit, so we don't burn the CPU when link is down
        usleep(10000);
    }
    assert(w->ip != 0 && w->mask != 0 && w->mtu != 0 && !IS_ZERO(w->mac),
           "%s: cannot obtain needed interface information", ifname);

    // set the IP address of our default router
    w->rip = rip;

#ifndef NDEBUG
    char ip_str[INET_ADDRSTRLEN];
    char mask_str[INET_ADDRSTRLEN];
    char rip_str[INET_ADDRSTRLEN];
    warn(notice, "%s has IP addr %s/%s%s%s", ifname,
         inet_ntop(AF_INET, &w->ip, ip_str, INET_ADDRSTRLEN),
         inet_ntop(AF_INET, &w->mask, mask_str, INET_ADDRSTRLEN),
         rip ? ", router " : "",
         rip ? inet_ntop(AF_INET, &w->rip, rip_str, INET_ADDRSTRLEN) : "");
#endif

    // initialize lists of sockets and iovs
    SLIST_INIT(&w->sock);
    STAILQ_INIT(&w->iov);

    // allocate socket pointers
    assert((w->udp = calloc(UINT16_MAX, sizeof(struct w_sock *))) != 0,
           "cannot allocate UDP sockets");

    backend_init(w, ifname);

    // store the initialized engine in our global list
    SLIST_INSERT_HEAD(&wc, w, next);

    warn(info, "%s %s with %s backend on %s ready", warpcore_name,
         warpcore_version, w->backend, ifname);
    return w;
}
