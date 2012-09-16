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

struct netdev_dev_tunnel {
    struct netdev_dev netdev_dev;
    uint8_t hwaddr[ETH_ADDR_LEN];
    struct netdev_stats stats;
    enum netdev_flags flags;
    int sockfd;
    struct sockaddr_in local_addr;
    struct sockaddr_in remote_addr;
    bool valid_remote_ip;
    bool valid_remote_port;
    bool connected;
    unsigned int change_seq;
};

struct netdev_tunnel {
    struct netdev netdev;
} ;

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static struct shash tunnel_netdev_devs = SHASH_INITIALIZER(&tunnel_netdev_devs);

static int netdev_tunnel_create(const struct netdev_class *, const char *,
                               struct netdev_dev **);
static void netdev_tunnel_update_seq(struct netdev_dev_tunnel *);

static bool
is_tunnel_class(const struct netdev_class *class)
{
    return class->create == netdev_tunnel_create;
}

static struct netdev_dev_tunnel *
netdev_dev_tunnel_cast(const struct netdev_dev *netdev_dev)
{
    assert(is_tunnel_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev_dev, struct netdev_dev_tunnel, netdev_dev);
}

static struct netdev_tunnel *
netdev_tunnel_cast(const struct netdev *netdev)
{
    struct netdev_dev *netdev_dev = netdev_get_dev(netdev);
    assert(is_tunnel_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev, struct netdev_tunnel, netdev);
}

static int
netdev_tunnel_create(const struct netdev_class *class, const char *name,
                    struct netdev_dev **netdev_devp)
{
    static unsigned int n = 0xaa550000;
    struct netdev_dev_tunnel *netdev_dev;
    int error;

    netdev_dev = xzalloc(sizeof *netdev_dev);
    netdev_dev_init(&netdev_dev->netdev_dev, name, class);
    netdev_dev->hwaddr[0] = 0x55;
    netdev_dev->hwaddr[1] = 0xaa;
    netdev_dev->hwaddr[2] = n >> 24;
    netdev_dev->hwaddr[3] = n >> 16;
    netdev_dev->hwaddr[4] = n >> 8;
    netdev_dev->hwaddr[5] = n;
    netdev_dev->flags = 0;
    netdev_dev->change_seq = 1;
    memset(&netdev_dev->remote_addr, 0, sizeof(netdev_dev->remote_addr));
    netdev_dev->valid_remote_ip = false;
    netdev_dev->valid_remote_port = false;
    netdev_dev->connected = false;


    netdev_dev->sockfd = inet_open_passive(SOCK_DGRAM, "", 0, &netdev_dev->local_addr, 0);
    if (netdev_dev->sockfd < 0) {
    	error = netdev_dev->sockfd;
        goto error;
    }


    shash_add(&tunnel_netdev_devs, name, netdev_dev);

    n++;

    *netdev_devp = &netdev_dev->netdev_dev;

    VLOG_DBG("tunnel_create: name=%s, fd=%d, port=%d", name, netdev_dev->sockfd, netdev_dev->local_addr.sin_port);

    return 0;

error:
    free(netdev_dev);
    return error;
}

static void
netdev_tunnel_destroy(struct netdev_dev *netdev_dev_)
{
    struct netdev_dev_tunnel *netdev_dev = netdev_dev_tunnel_cast(netdev_dev_);

    if (netdev_dev->sockfd != -1)
    	close(netdev_dev->sockfd);

    shash_find_and_delete(&tunnel_netdev_devs,
                          netdev_dev_get_name(netdev_dev_));
    free(netdev_dev);
}

static int
netdev_tunnel_open(struct netdev_dev *netdev_dev_, struct netdev **netdevp)
{
    struct netdev_tunnel *netdev;

    netdev = xmalloc(sizeof *netdev);
    netdev_init(&netdev->netdev, netdev_dev_);

    *netdevp = &netdev->netdev;
    return 0;
}

static void
netdev_tunnel_close(struct netdev *netdev_)
{
    struct netdev_tunnel *netdev = netdev_tunnel_cast(netdev_);
    free(netdev);
}

static int
netdev_tunnel_get_config(struct netdev_dev *dev_, struct smap *args)
{
    struct netdev_dev_tunnel *netdev_dev = netdev_dev_tunnel_cast(dev_);

    if (netdev_dev->valid_remote_ip)
    	smap_add_format(args, "remote_ip", IP_FMT,
		IP_ARGS(&netdev_dev->remote_addr.sin_addr));
    if (netdev_dev->valid_remote_port)
        smap_add_format(args, "remote_port", "%"PRIu16,
		ntohs(netdev_dev->remote_addr.sin_port));
    return 0;
}

static int
netdev_tunnel_connect(struct netdev_dev_tunnel *dev)
{
    if (dev->sockfd < 0)
        return EBADF;
    if (!dev->valid_remote_ip || !dev->valid_remote_port)
        return 0;
    dev->remote_addr.sin_family = AF_INET;
    if (connect(dev->sockfd, (struct sockaddr*) &dev->remote_addr, sizeof(dev->remote_addr)) < 0) {
        return errno;
    }
    dev->connected = true;
    netdev_tunnel_update_seq(dev);
    VLOG_DBG("%s: connected to (%s, %d)", netdev_dev_get_name(&dev->netdev_dev),
        inet_ntoa(dev->remote_addr.sin_addr), ntohs(dev->remote_addr.sin_port));
    return 0;
}

static int
netdev_tunnel_set_config(struct netdev_dev *dev_, const struct smap *args)
{
    struct netdev_dev_tunnel *netdev_dev = netdev_dev_tunnel_cast(dev_);
    struct shash_node *node;

    VLOG_DBG("tunnel_set_config(%s)", netdev_dev_get_name(dev_));
    SMAP_FOR_EACH(node, args) {
        VLOG_DBG("arg: %s->%s", node->name, (char*)node->data);
    	if (!strcmp(node->name, "remote_ip")) {
	    struct in_addr addr;
	    if (lookup_ip(node->data, &addr)) {
		VLOG_WARN("%s: bad 'remote_ip'", node->name);
	    } else {
		netdev_dev->remote_addr.sin_addr = addr;
		netdev_dev->valid_remote_ip = true;
	    }
	} else if (!strcmp(node->name, "remote_port")) {
	    netdev_dev->remote_addr.sin_port = htons(atoi(node->data));
	    netdev_dev->valid_remote_port = true;
	} else {
	    VLOG_WARN("%s: unknown argument '%s'", 
	    	netdev_dev_get_name(dev_), node->name);
	}
    }
    return netdev_tunnel_connect(netdev_dev);        
}

static int
netdev_tunnel_listen(struct netdev *netdev_ OVS_UNUSED)
{
    return 0;
}

static int
netdev_tunnel_recv(struct netdev *netdev_, void *buffer, size_t size)
{
    struct netdev_dev_tunnel *dev = 
    	netdev_dev_tunnel_cast(netdev_get_dev(netdev_));
    if (!dev->connected)
        return -EAGAIN;
    for (;;) {
        ssize_t retval;
        retval = recv(dev->sockfd, buffer, size, MSG_TRUNC);
	VLOG_DBG("%s: recv(%"PRIxPTR", %"PRIu64", MSG_TRUNC) = %"PRId64,
		 netdev_get_name(netdev_), (uintptr_t)buffer, size, retval);
        if (retval >= 0) {
	    dev->stats.rx_packets++;
	    dev->stats.rx_bytes += retval;
            if (retval <= size) {
	    	return retval;
	    } else {
	    	dev->stats.rx_errors++;
		dev->stats.rx_length_errors++;
	    	return -EMSGSIZE;
	    }
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error receiveing Ethernet packet on %s: %s",
                    netdev_get_name(netdev_), strerror(errno));
	        dev->stats.rx_errors++;
            }
            return -errno;
        }
    }
}

static void
netdev_tunnel_recv_wait(struct netdev *netdev_)
{
    struct netdev_dev_tunnel *dev = 
    	netdev_dev_tunnel_cast(netdev_get_dev(netdev_));
    if (dev->sockfd >= 0) {
        poll_fd_wait(dev->sockfd, POLLIN);
    }
}

static int
netdev_tunnel_send(struct netdev *netdev_, const void *buffer, size_t size)
{
    struct netdev_dev_tunnel *dev = 
    	netdev_dev_tunnel_cast(netdev_get_dev(netdev_));
    if (!dev->connected)
        return EAGAIN;
    for (;;) {
        ssize_t retval;
        retval = send(dev->sockfd, buffer, size, 0);
    	VLOG_DBG("%s: send(%"PRIxPTR", %"PRIu64") = %"PRId64,
	         netdev_get_name(netdev_), (uintptr_t)buffer, size, retval);
        if (retval >= 0) {
	    dev->stats.tx_packets++;
	    dev->stats.tx_bytes++;
	    if (retval != size) {
	        VLOG_WARN_RL(&rl, "sent partial Ethernet packet (%"PRId64" bytes of "
		             "%"PRIu64") on %s", retval, size, netdev_get_name(netdev_));
		dev->stats.tx_errors++;
	    }
            return 0;
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error sending Ethernet packet on %s: %s",
                    netdev_get_name(netdev_), strerror(errno));
		dev->stats.tx_errors++;
            }
            return errno;
        }
    }
}

static void
netdev_tunnel_send_wait(struct netdev *netdev_)
{
    struct netdev_dev_tunnel *dev = 
    	netdev_dev_tunnel_cast(netdev_get_dev(netdev_));
    if (dev->sockfd >= 0) {
        poll_fd_wait(dev->sockfd, POLLOUT);
    }
}

static int
netdev_tunnel_drain(struct netdev *netdev_)
{
    struct netdev_dev_tunnel *dev = 
    	netdev_dev_tunnel_cast(netdev_get_dev(netdev_));
    char buffer[128];
    int error;

    if (!dev->connected)
    	return 0;
    for (;;) {
    	error = recv(dev->sockfd, buffer, 128, MSG_TRUNC);
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
    struct netdev_dev_tunnel *dev =
        netdev_dev_tunnel_cast(netdev_get_dev(netdev));

    if (!eth_addr_equals(dev->hwaddr, mac)) {
        memcpy(dev->hwaddr, mac, ETH_ADDR_LEN);
        netdev_tunnel_update_seq(dev);
    }

    return 0;
}

static int
netdev_tunnel_get_etheraddr(const struct netdev *netdev,
                           uint8_t mac[ETH_ADDR_LEN])
{
    const struct netdev_dev_tunnel *dev =
        netdev_dev_tunnel_cast(netdev_get_dev(netdev));

    memcpy(mac, dev->hwaddr, ETH_ADDR_LEN);
    return 0;
}


static int
netdev_tunnel_get_stats(const struct netdev *netdev, struct netdev_stats *stats)
{
    const struct netdev_dev_tunnel *dev =
        netdev_dev_tunnel_cast(netdev_get_dev(netdev));

    *stats = dev->stats;
    return 0;
}

static int
netdev_tunnel_set_stats(struct netdev *netdev, const struct netdev_stats *stats)
{
    struct netdev_dev_tunnel *dev =
        netdev_dev_tunnel_cast(netdev_get_dev(netdev));

    dev->stats = *stats;
    return 0;
}

static int
netdev_tunnel_update_flags(struct netdev *netdev,
                          enum netdev_flags off, enum netdev_flags on,
                          enum netdev_flags *old_flagsp)
{
    struct netdev_dev_tunnel *dev =
        netdev_dev_tunnel_cast(netdev_get_dev(netdev));

    if ((off | on) & ~(NETDEV_UP | NETDEV_PROMISC)) {
        return EINVAL;
    }

    // XXX should we actually do something with this flags?
    *old_flagsp = dev->flags;
    dev->flags |= on;
    dev->flags &= ~off;
    if (*old_flagsp != dev->flags) {
        netdev_tunnel_update_seq(dev);
    }
    return 0;
}

static unsigned int
netdev_tunnel_change_seq(const struct netdev *netdev)
{
    return netdev_dev_tunnel_cast(netdev_get_dev(netdev))->change_seq;
}

/* Helper functions. */

static void
netdev_tunnel_update_seq(struct netdev_dev_tunnel *dev)
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
    struct netdev_dev_tunnel *tunnel_dev;
    char buf[6];

    tunnel_dev = shash_find_data(&tunnel_netdev_devs, argv[1]);
    if (!tunnel_dev) {
        unixctl_command_reply_error(conn, "no such tunnel netdev");
        return;
    }

    sprintf(buf, "%d", ntohs(tunnel_dev->local_addr.sin_port));
    unixctl_command_reply(conn, buf);
}


static int
netdev_tunnel_init(void)
{
    unixctl_command_register("netdev-tunnel/get-port", "NAME",
                             1, 1, netdev_tunnel_get_port, NULL);
    return 0;
}

const struct netdev_class netdev_tunnel_class = {
    "tunnel",
    netdev_tunnel_init,         /* init */
    NULL,                       /* run */
    NULL,                       /* wait */

    netdev_tunnel_create,
    netdev_tunnel_destroy,
    netdev_tunnel_get_config,
    netdev_tunnel_set_config, 

    netdev_tunnel_open,
    netdev_tunnel_close,

    netdev_tunnel_listen,
    netdev_tunnel_recv,
    netdev_tunnel_recv_wait,
    netdev_tunnel_drain,

    netdev_tunnel_send, 
    netdev_tunnel_send_wait,  

    netdev_tunnel_set_etheraddr,
    netdev_tunnel_get_etheraddr,
    NULL,			/* get_mtu */
    NULL,			/* set_mtu */
    NULL,                       /* get_ifindex */
    NULL,			/* get_carrier */
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
    NULL,                       /* dump_queues */
    NULL,                       /* dump_queue_stats */

    NULL,                       /* get_in4 */
    NULL,                       /* set_in4 */
    NULL,                       /* get_in6 */
    NULL,                       /* add_router */
    NULL,                       /* get_next_hop */
    NULL,                       /* get_drv_info */
    NULL,                       /* arp_lookup */

    netdev_tunnel_update_flags,

    netdev_tunnel_change_seq
};
