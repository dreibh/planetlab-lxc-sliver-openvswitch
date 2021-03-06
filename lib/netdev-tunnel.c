/*
 * Copyright (c) 2010, 2011, 2012 Nicira Networks.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "flow.h"
#include "list.h"
#include "dpif-netdev.h"
#include "netdev-provider.h"
#include "odp-util.h"
#include "ofp-print.h"
#include "ofpbuf.h"
#include "packets.h"
#include "poll-loop.h"
#include "shash.h"
#include "sset.h"
#include "unixctl.h"
#include "socket-util.h"
#include "vlog.h"

VLOG_DEFINE_THIS_MODULE(netdev_tunnel);

struct netdev_tunnel {
    struct netdev up;

    /* Protects all members below. */
    struct ovs_mutex mutex;

    uint8_t hwaddr[ETH_ADDR_LEN];
    struct netdev_stats stats;
    enum netdev_flags flags;
    int sockfd;
    struct sockaddr_storage local_addr;
    struct sockaddr_storage remote_addr;
    bool valid_remote_ip;
    bool valid_remote_port;
    bool connected;
    unsigned int change_seq;
};

struct netdev_rxq_tunnel {
    struct netdev_rxq up;
    int fd;
};

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static struct ovs_mutex tunnel_netdevs_mutex = OVS_MUTEX_INITIALIZER;
static struct shash tunnel_netdevs OVS_GUARDED_BY(tunnel_netdevs_mutex)
    = SHASH_INITIALIZER(&tunnel_netdevs);

static int netdev_tunnel_construct(struct netdev *netdevp_);
static void netdev_tunnel_update_seq(struct netdev_tunnel *);

static bool
is_netdev_tunnel_class(const struct netdev_class *class)
{
    return class->construct == netdev_tunnel_construct;
}

static struct netdev_tunnel *
netdev_tunnel_cast(const struct netdev *netdev)
{
    ovs_assert(is_netdev_tunnel_class(netdev_get_class(netdev)));
    return CONTAINER_OF(netdev, struct netdev_tunnel, up);
}

static struct netdev_rxq_tunnel *
netdev_rxq_tunnel_cast(const struct netdev_rxq *rx)
{
    ovs_assert(is_netdev_tunnel_class(netdev_get_class(rx->netdev)));
    return CONTAINER_OF(rx, struct netdev_rxq_tunnel, up);
}

static struct netdev *
netdev_tunnel_alloc(void)
{
    struct netdev_tunnel *netdev = xzalloc(sizeof *netdev);
    return &netdev->up;
}

static int
netdev_tunnel_construct(struct netdev *netdev_)
{
    static atomic_uint next_n = ATOMIC_VAR_INIT(0);
    struct netdev_tunnel *netdev = netdev_tunnel_cast(netdev_);
    unsigned int n;

    atomic_add(&next_n, 1, &n);

    ovs_mutex_init(&netdev->mutex);
    netdev->hwaddr[0] = 0xfe;
    netdev->hwaddr[1] = 0xff;
    netdev->hwaddr[2] = 0xff;
    netdev->hwaddr[3] = n >> 16;
    netdev->hwaddr[4] = n >> 8;
    netdev->hwaddr[5] = n;
    netdev->flags = 0;
    netdev->change_seq = 1;
    memset(&netdev->remote_addr, 0, sizeof(netdev->remote_addr));
    netdev->valid_remote_ip = false;
    netdev->valid_remote_port = false;
    netdev->connected = false;


    netdev->sockfd = inet_open_passive(SOCK_DGRAM, "", 0,
        &netdev->local_addr, 0);
    if (netdev->sockfd < 0) {
    	return netdev->sockfd;
    }


    shash_add(&tunnel_netdevs, netdev_get_name(netdev_), netdev);

    n++;

    VLOG_DBG("tunnel_create: name=%s, fd=%d, port=%d",
        netdev_get_name(netdev_), netdev->sockfd, ss_get_port(&netdev->local_addr));

    return 0;

}

static void
netdev_tunnel_destruct(struct netdev *netdev_)
{
    struct netdev_tunnel *netdev = netdev_tunnel_cast(netdev_);

    ovs_mutex_lock(&tunnel_netdevs_mutex);

    if (netdev->sockfd != -1)
    	close(netdev->sockfd);

    shash_find_and_delete(&tunnel_netdevs,
                          netdev_get_name(netdev_));

    ovs_mutex_destroy(&netdev->mutex);
    ovs_mutex_unlock(&tunnel_netdevs_mutex);
}

static void
netdev_tunnel_dealloc(struct netdev *netdev_)
{
    struct netdev_tunnel *netdev = netdev_tunnel_cast(netdev_);
    free(netdev);
}

static int
netdev_tunnel_get_config(const struct netdev *dev_, struct smap *args)
{
    struct netdev_tunnel *netdev = netdev_tunnel_cast(dev_);

    ovs_mutex_lock(&netdev->mutex);
    if (netdev->valid_remote_ip) {
        const struct sockaddr_in *sin =
            ALIGNED_CAST(const struct sockaddr_in *, &netdev->remote_addr);
        smap_add_format(args, "remote_ip", IP_FMT,
                IP_ARGS(sin->sin_addr.s_addr));
    }
    if (netdev->valid_remote_port)
        smap_add_format(args, "remote_port", "%"PRIu16,
                ss_get_port(&netdev->remote_addr));
    ovs_mutex_unlock(&netdev->mutex);
    return 0;
}

static int
netdev_tunnel_connect(struct netdev_tunnel *dev)
    OVS_REQUIRES(dev->mutex)
{
    char buf[1024];
    struct sockaddr_in *sin =
        ALIGNED_CAST(struct sockaddr_in *, &dev->remote_addr);
    if (dev->sockfd < 0)
        return EBADF;
    if (!dev->valid_remote_ip || !dev->valid_remote_port)
        return 0;
    if (connect(dev->sockfd, (struct sockaddr*) sin, sizeof(*sin)) < 0) {
        VLOG_DBG("%s: connect returned %s", netdev_get_name(&dev->up),
                ovs_strerror(errno));
        return errno;
    }
    dev->connected = true;
    netdev_tunnel_update_seq(dev);
    VLOG_DBG("%s: connected to (%s, %d)", netdev_get_name(&dev->up),
            inet_ntop(AF_INET, &sin->sin_addr.s_addr, buf, 1024),
            ss_get_port(&dev->remote_addr));
    return 0;
}

static int
netdev_tunnel_set_config(struct netdev *dev_, const struct smap *args)
{
    struct netdev_tunnel *netdev = netdev_tunnel_cast(dev_);
    struct shash_node *node;
    int error;
    struct sockaddr_in *sin =
        ALIGNED_CAST(struct sockaddr_in *, &netdev->remote_addr);

    ovs_mutex_lock(&netdev->mutex);
    VLOG_DBG("tunnel_set_config(%s)", netdev_get_name(dev_));
    SMAP_FOR_EACH(node, args) {
        VLOG_DBG("arg: %s->%s", node->name, (char*)node->data);
        if (!strcmp(node->name, "remote_ip")) {
            struct in_addr addr;
            if (lookup_ip(node->data, &addr)) {
                VLOG_WARN("%s: bad 'remote_ip'", node->name);
            } else {
                sin->sin_family = AF_INET;
                sin->sin_addr = addr;
                netdev->valid_remote_ip = true;
            }
        } else if (!strcmp(node->name, "remote_port")) {
            sin->sin_port = htons(atoi(node->data));
            netdev->valid_remote_port = true;
        } else {
            VLOG_WARN("%s: unknown argument '%s'", 
                    netdev_get_name(dev_), node->name);
        }
    }
    error = netdev_tunnel_connect(netdev);        
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}

static struct netdev_rxq *
netdev_tunnel_rxq_alloc(void)
{
    struct netdev_rxq_tunnel *rx = xzalloc(sizeof *rx);
    return &rx->up;
}

static int
netdev_tunnel_rxq_construct(struct netdev_rxq *rx_)
{   
    struct netdev_rxq_tunnel *rx = netdev_rxq_tunnel_cast(rx_);
    struct netdev *netdev_ = rx->up.netdev;
    struct netdev_tunnel *netdev = netdev_tunnel_cast(netdev_);

    ovs_mutex_lock(&netdev->mutex);
    rx->fd = netdev->sockfd;
    ovs_mutex_unlock(&netdev->mutex);
    return 0;
}

static void
netdev_tunnel_rxq_destruct(struct netdev_rxq *rx_ OVS_UNUSED)
{
}

static void
netdev_tunnel_rxq_dealloc(struct netdev_rxq *rx_)
{
    struct netdev_rxq_tunnel *rx = netdev_rxq_tunnel_cast(rx_);

    free(rx);
}

static int
netdev_tunnel_rxq_recv(struct netdev_rxq *rx_, struct ofpbuf **packet, int *c)
{
    struct netdev_rxq_tunnel *rx = netdev_rxq_tunnel_cast(rx_);
    struct netdev_tunnel *netdev =
        netdev_tunnel_cast(rx_->netdev);
    struct ofpbuf *buffer = NULL;
    void *data;
    size_t size;
    int error = 0;

    if (!netdev->connected)
        return EAGAIN;
    buffer = ofpbuf_new_with_headroom(VLAN_ETH_HEADER_LEN + ETH_PAYLOAD_MAX,
        DP_NETDEV_HEADROOM);
    data = ofpbuf_data(buffer);
    size = ofpbuf_tailroom(buffer);

    for (;;) {
        ssize_t retval;
        retval = recv(rx->fd, data, size, MSG_TRUNC);
        VLOG_DBG("%s: recv(%"PRIxPTR", %"PRIuSIZE", MSG_TRUNC) = %"PRIdSIZE,
                netdev_rxq_get_name(rx_), (uintptr_t)data, size, retval);
        if (retval >= 0) {
            netdev->stats.rx_packets++;
            netdev->stats.rx_bytes += retval;
            if (retval <= size) {
                ofpbuf_set_size(buffer, ofpbuf_size(buffer) + retval);
                goto out;
            } else {
                netdev->stats.rx_errors++;
                netdev->stats.rx_length_errors++;
                error = EMSGSIZE;
                goto out;
            }
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error receiveing Ethernet packet on %s: %s",
                        netdev_rxq_get_name(rx_), ovs_strerror(errno));
                netdev->stats.rx_errors++;
            }
            error = errno;
            goto out;
        }
    }
out:
    if (error) {
        ofpbuf_delete(buffer);
    } else {
        dp_packet_pad(buffer);
        packet[0] = buffer;
        *c = 1;
    }

    return error;
}

static void
netdev_tunnel_rxq_wait(struct netdev_rxq *rx_)
{
    struct netdev_rxq_tunnel *rx = 
        netdev_rxq_tunnel_cast(rx_);
    if (rx->fd >= 0) {
        poll_fd_wait(rx->fd, POLLIN);
    }
}

static int
netdev_tunnel_send(struct netdev *netdev_, struct ofpbuf *pkt, bool may_steal)
{
    const void *buffer = ofpbuf_data(pkt);
    size_t size = ofpbuf_size(pkt);
    struct netdev_tunnel *dev = 
        netdev_tunnel_cast(netdev_);
    int error = 0;
    if (!dev->connected) {
        error = EAGAIN;
        goto out;
    }
    for (;;) {
        ssize_t retval;
        retval = send(dev->sockfd, buffer, size, 0);
        VLOG_DBG("%s: send(%"PRIxPTR", %"PRIuSIZE") = %"PRIdSIZE,
                netdev_get_name(netdev_), (uintptr_t)buffer, size, retval);
        if (retval >= 0) {
            dev->stats.tx_packets++;
            dev->stats.tx_bytes += retval;
            if (retval != size) {
                VLOG_WARN_RL(&rl, "sent partial Ethernet packet (%"PRIdSIZE" bytes of "
                        "%"PRIuSIZE") on %s", retval, size, netdev_get_name(netdev_));
                dev->stats.tx_errors++;
            }
            goto out;
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error sending Ethernet packet on %s: %s",
                        netdev_get_name(netdev_), ovs_strerror(errno));
                dev->stats.tx_errors++;
            }
            error = errno;
            goto out;
        }
    }
out:
    if (may_steal) {
        ofpbuf_delete(pkt);
    }

    return error;
}

static void
netdev_tunnel_send_wait(struct netdev *netdev_)
{
    struct netdev_tunnel *dev = netdev_tunnel_cast(netdev_);
    if (dev->sockfd >= 0) {
        poll_fd_wait(dev->sockfd, POLLOUT);
    }
}

static int
netdev_tunnel_rxq_drain(struct netdev_rxq *rx_)
{
    struct netdev_tunnel *netdev =
        netdev_tunnel_cast(rx_->netdev);
    struct netdev_rxq_tunnel *rx = 
        netdev_rxq_tunnel_cast(rx_);
    char buffer[128];
    int error;

    if (!netdev->connected)
        return 0;
    for (;;) {
        error = recv(rx->fd, buffer, 128, MSG_TRUNC);
        if (error) {
            if (error == -EAGAIN)
                break;
            else if (error != -EMSGSIZE)
                return error;
        }
    }
    return 0;
}

static int
netdev_tunnel_set_etheraddr(struct netdev *netdev,
                           const uint8_t mac[ETH_ADDR_LEN])
{
    struct netdev_tunnel *dev = netdev_tunnel_cast(netdev);

    ovs_mutex_lock(&dev->mutex);
    if (!eth_addr_equals(dev->hwaddr, mac)) {
        memcpy(dev->hwaddr, mac, ETH_ADDR_LEN);
        netdev_tunnel_update_seq(dev);
    }
    ovs_mutex_unlock(&dev->mutex);

    return 0;
}

static int
netdev_tunnel_get_etheraddr(const struct netdev *netdev,
                           uint8_t mac[ETH_ADDR_LEN])
{
    const struct netdev_tunnel *dev = netdev_tunnel_cast(netdev);

    ovs_mutex_lock(&dev->mutex);
    memcpy(mac, dev->hwaddr, ETH_ADDR_LEN);
    ovs_mutex_unlock(&dev->mutex);
    return 0;
}


static int
netdev_tunnel_get_stats(const struct netdev *netdev, struct netdev_stats *stats)
{
    const struct netdev_tunnel *dev = netdev_tunnel_cast(netdev);

    ovs_mutex_lock(&dev->mutex);
    *stats = dev->stats;
    ovs_mutex_unlock(&dev->mutex);
    return 0;
}

static int
netdev_tunnel_set_stats(struct netdev *netdev, const struct netdev_stats *stats)
{
    struct netdev_tunnel *dev = netdev_tunnel_cast(netdev);

    ovs_mutex_lock(&dev->mutex);
    dev->stats = *stats;
    ovs_mutex_unlock(&dev->mutex);
    return 0;
}

static int
netdev_tunnel_update_flags(struct netdev *dev_,
                          enum netdev_flags off, enum netdev_flags on,
                          enum netdev_flags *old_flagsp)
{
    struct netdev_tunnel *netdev =
        netdev_tunnel_cast(dev_);
    int error = 0;

    ovs_mutex_lock(&netdev->mutex);
    if ((off | on) & ~(NETDEV_UP | NETDEV_PROMISC)) {
        error = EINVAL;
        goto out;
    }

    // XXX should we actually do something with these flags?
    *old_flagsp = netdev->flags;
    netdev->flags |= on;
    netdev->flags &= ~off;
    if (*old_flagsp != netdev->flags) {
        netdev_tunnel_update_seq(netdev);
    }

out:
    ovs_mutex_unlock(&netdev->mutex);
    return error;
}


/* Helper functions. */

static void
netdev_tunnel_update_seq(struct netdev_tunnel *dev)
    OVS_REQUIRES(dev->mutex)
{
    dev->change_seq++;
    if (!dev->change_seq) {
        dev->change_seq++;
    }
}

static void
netdev_tunnel_get_port(struct unixctl_conn *conn,
                     int argc OVS_UNUSED, const char *argv[], void *aux OVS_UNUSED)
{
    struct netdev_tunnel *tunnel_dev;
    char buf[6];

    ovs_mutex_lock(&tunnel_netdevs_mutex);
    tunnel_dev = shash_find_data(&tunnel_netdevs, argv[1]);
    if (!tunnel_dev) {
        unixctl_command_reply_error(conn, "no such tunnel netdev");
        goto out;
    }

    ovs_mutex_lock(&tunnel_dev->mutex);
    sprintf(buf, "%d", ss_get_port(&tunnel_dev->local_addr));
    ovs_mutex_unlock(&tunnel_dev->mutex);

    unixctl_command_reply(conn, buf);
out:
    ovs_mutex_unlock(&tunnel_netdevs_mutex);
}

static void
netdev_tunnel_get_tx_bytes(struct unixctl_conn *conn,
                     int argc OVS_UNUSED, const char *argv[], void *aux OVS_UNUSED)
{
    struct netdev_tunnel *tunnel_dev;
    char buf[128];

    ovs_mutex_lock(&tunnel_netdevs_mutex);
    tunnel_dev = shash_find_data(&tunnel_netdevs, argv[1]);
    if (!tunnel_dev) {
        unixctl_command_reply_error(conn, "no such tunnel netdev");
        goto out;
    }

    ovs_mutex_lock(&tunnel_dev->mutex);
    sprintf(buf, "%"PRIu64, tunnel_dev->stats.tx_bytes);
    ovs_mutex_unlock(&tunnel_dev->mutex);
    unixctl_command_reply(conn, buf);
out:
    ovs_mutex_unlock(&tunnel_netdevs_mutex);
}

static void
netdev_tunnel_get_rx_bytes(struct unixctl_conn *conn,
                     int argc OVS_UNUSED, const char *argv[], void *aux OVS_UNUSED)
{
    struct netdev_tunnel *tunnel_dev;
    char buf[128];

    ovs_mutex_lock(&tunnel_netdevs_mutex);
    tunnel_dev = shash_find_data(&tunnel_netdevs, argv[1]);
    if (!tunnel_dev) {
        unixctl_command_reply_error(conn, "no such tunnel netdev");
        goto out;
    }

    sprintf(buf, "%"PRIu64, tunnel_dev->stats.rx_bytes);
    unixctl_command_reply(conn, buf);
out:
    ovs_mutex_unlock(&tunnel_netdevs_mutex);
}


static int
netdev_tunnel_init(void)
{
    unixctl_command_register("netdev-tunnel/get-port", "NAME",
                             1, 1, netdev_tunnel_get_port, NULL);
    unixctl_command_register("netdev-tunnel/get-tx-bytes", "NAME",
                             1, 1, netdev_tunnel_get_tx_bytes, NULL);
    unixctl_command_register("netdev-tunnel/get-rx-bytes", "NAME",
                             1, 1, netdev_tunnel_get_rx_bytes, NULL);
    return 0;
}

static void
netdev_tunnel_run(void)
{
}

static void
netdev_tunnel_wait(void)
{
}

const struct netdev_class netdev_tunnel_class = {
    "tunnel",
    netdev_tunnel_init,    
    netdev_tunnel_run,      
    netdev_tunnel_wait,   

    netdev_tunnel_alloc,
    netdev_tunnel_construct,
    netdev_tunnel_destruct,
    netdev_tunnel_dealloc,
    netdev_tunnel_get_config,
    netdev_tunnel_set_config, 
    NULL,			            /* get_tunnel_config */

    netdev_tunnel_send, 
    netdev_tunnel_send_wait,  

    netdev_tunnel_set_etheraddr,
    netdev_tunnel_get_etheraddr,
    NULL,			            /* get_mtu */
    NULL,			            /* set_mtu */
    NULL,                       /* get_ifindex */
    NULL,			            /* get_carrier */
    NULL,                       /* get_carrier_resets */
    NULL,                       /* get_miimon */
    netdev_tunnel_get_stats,
    netdev_tunnel_set_stats,

    NULL,                       /* get_features */
    NULL,                       /* set_advertisements */

    NULL,                       /* set_policing */
    NULL,                       /* get_qos_types */
    NULL,                       /* get_qos_capabilities */
    NULL,                       /* get_qos */
    NULL,                       /* set_qos */
    NULL,                       /* get_queue */
    NULL,                       /* set_queue */
    NULL,                       /* delete_queue */
    NULL,                       /* get_queue_stats */
    NULL,                       /* queue_dump_start */
    NULL,                       /* queue_dump_next */
    NULL,                       /* queue_dump_done */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_status */
    NULL,                       /* arp_lookup */

    netdev_tunnel_update_flags,

    netdev_tunnel_rxq_alloc,
    netdev_tunnel_rxq_construct,
    netdev_tunnel_rxq_destruct,
    netdev_tunnel_rxq_dealloc,
    netdev_tunnel_rxq_recv,
    netdev_tunnel_rxq_wait,
    netdev_tunnel_rxq_drain,
};
