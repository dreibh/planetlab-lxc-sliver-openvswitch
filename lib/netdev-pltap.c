/*
 * Copyright (c) 2012 Giuseppe Lettieri
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

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
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
#include "tunalloc.h"

VLOG_DEFINE_THIS_MODULE(netdev_pltap);

struct netdev_pltap {
    struct netdev up;
    char *real_name;
    struct netdev_stats stats;
    enum netdev_flags new_flags;
    enum netdev_flags flags;
    int fd;
    struct sockaddr_in local_addr;
    int local_netmask;
    bool valid_local_ip;
    bool valid_local_netmask;
    bool sync_flags_needed;
    struct list sync_list;
    unsigned int change_seq;
};

static const struct netdev_rx_class netdev_rx_pltap_class;

static struct list sync_list;

struct netdev_rx_pltap {
    struct netdev_rx up;    
    int fd;
};

static int af_inet_sock = -1;

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static struct shash pltap_netdevs = SHASH_INITIALIZER(&pltap_netdevs);

static int netdev_pltap_create(const struct netdev_class *, const char *,
                               struct netdev **);

static void netdev_pltap_update_seq(struct netdev_pltap *);
static int get_flags(struct netdev_pltap *dev, enum netdev_flags *flags);

static bool
netdev_pltap_finalized(struct netdev_pltap *dev)
{
    return dev->valid_local_ip && dev->valid_local_netmask;
}

static bool
is_netdev_pltap_class(const struct netdev_class *class)
{
    return class->create == netdev_pltap_create;
}

static struct netdev_pltap *
netdev_pltap_cast(const struct netdev *netdev)
{
    ovs_assert(is_netdev_pltap_class(netdev_get_class(netdev)));
    return CONTAINER_OF(netdev, struct netdev_pltap, up);
}

static struct netdev_rx_pltap*
netdev_rx_pltap_cast(const struct netdev_rx *rx)
{
    netdev_rx_assert_class(rx, &netdev_rx_pltap_class);
    return CONTAINER_OF(rx, struct netdev_rx_pltap, up);
}

static void sync_needed(struct netdev_pltap *dev)
{
    if (dev->sync_flags_needed)
        return;

    dev->sync_flags_needed = true;
    list_insert(&sync_list, &dev->sync_list);
	
}

static void sync_done(struct netdev_pltap *dev)
{
    if (!dev->sync_flags_needed)
        return;

    (void) list_remove(&dev->sync_list);
    dev->sync_flags_needed = false;
}

static int
netdev_pltap_create(const struct netdev_class *class OVS_UNUSED, const char *name,
                    struct netdev **netdevp)
{
    struct netdev_pltap *netdev;
    int error;

    netdev = xzalloc(sizeof *netdev);

    netdev->real_name = xzalloc(IFNAMSIZ + 1);
    memset(&netdev->local_addr, 0, sizeof(netdev->local_addr));
    netdev->valid_local_ip = false;
    netdev->valid_local_netmask = false;
    netdev->flags = 0;
    netdev->sync_flags_needed = false;
    list_init(&netdev->sync_list);


    /* Open tap device. */
    netdev->fd = tun_alloc(IFF_TAP, netdev->real_name);
    if (netdev->fd < 0) {
        error = errno;
        VLOG_WARN("tun_alloc(IFF_TAP, %s) failed: %s", name, ovs_strerror(error));
        goto cleanup;
    }
    VLOG_DBG("real_name = %s", netdev->real_name);

    /* Make non-blocking. */
    error = set_nonblocking(netdev->fd);
    if (error) {
        goto cleanup;
    }

    netdev_init(&netdev->up, name, &netdev_pltap_class);
    shash_add(&pltap_netdevs, name, netdev);
    *netdevp = &netdev->up;
    return 0;

cleanup:
    free(netdev);
    return error;
}

static void
netdev_pltap_destroy(struct netdev *netdev_)
{
    struct netdev_pltap *netdev = netdev_pltap_cast(netdev_);

    if (netdev->fd != -1)
    	close(netdev->fd);

    sync_done(netdev);

    shash_find_and_delete(&pltap_netdevs,
                          netdev_get_name(netdev_));
    free(netdev);
}

static int netdev_pltap_up(struct netdev_pltap *dev);

static int
netdev_pltap_rx_open(struct netdev *netdev_, struct netdev_rx **rxp)
{
    struct netdev_pltap *netdev =
        netdev_pltap_cast(netdev_);
    struct netdev_rx_pltap *rx;
    int err;

    rx = xmalloc(sizeof *rx);
    netdev_rx_init(&rx->up, netdev_, &netdev_rx_pltap_class);
    rx->fd = netdev->fd;
    *rxp = &rx->up;
    if (!netdev_pltap_finalized(netdev))
        return 0;
    err = netdev_pltap_up(netdev);
    if (err) {
        free(rx);
        *rxp = NULL;
        return err;
    }
    return 0;
}

static void
netdev_rx_pltap_destroy(struct netdev_rx *rx_)
{
    struct netdev_rx_pltap *rx = netdev_rx_pltap_cast(rx_);
    free(rx);
}

static int vsys_transaction(const char *script,
	const char **preply, char *format, ...)
{
    char *msg = NULL, *reply = NULL;
    const size_t reply_size = 1024;
    int ifd = -1, ofd = -1, maxfd;
    size_t bytes_to_write, bytes_to_read,
           bytes_written = 0, bytes_read = 0;
    int error = 0;
    char *ofname = NULL, *ifname = NULL;
    va_list args;

    va_start(args, format);
    msg = xvasprintf(format, args);
    va_end(args);
    reply = (char*)xmalloc(reply_size);
    if (!msg || !reply) {
    	VLOG_ERR("Out of memory");
	error = ENOMEM;
	goto cleanup;
    }

    ofname = xasprintf("/vsys/%s.out", script);
    ifname = xasprintf("/vsys/%s.in", script);
    if (!ofname || !ifname) {
    	VLOG_ERR("Out of memory");
	error = ENOMEM;
	goto cleanup;
    }

    ofd = open(ofname, O_RDONLY | O_NONBLOCK);
    if (ofd < 0) {
        VLOG_ERR("Cannot open %s: %s", ofname, ovs_strerror(errno));
	error = errno;
	goto cleanup;
    }
    ifd = open(ifname, O_WRONLY | O_NONBLOCK);
    if (ifd < 0) {
        VLOG_ERR("Cannot open %s: %s", ifname, ovs_strerror(errno));
	error = errno;
	goto cleanup;
    }
    maxfd = (ifd < ofd) ? ofd : ifd;

    bytes_to_write = strlen(msg);
    bytes_to_read = reply_size;
    while (bytes_to_write || bytes_to_read) {
        fd_set readset, writeset, errorset;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_ZERO(&errorset);
	if (bytes_to_write) {
	    FD_SET(ifd, &writeset);
	    FD_SET(ifd, &errorset);
	}
	FD_SET(ofd, &readset);
	FD_SET(ofd, &errorset);
	if (select(maxfd + 1, &readset, &writeset, &errorset, NULL) < 0) {
	    if (errno == EINTR)
	        continue;
	    VLOG_ERR("selec error: %s", ovs_strerror(errno));
	    error = errno;
	    goto cleanup;
	}
	if (FD_ISSET(ifd, &errorset) || FD_ISSET(ofd, &errorset)) {
	    VLOG_ERR("error condition on ifd or ofd");
	    goto cleanup;
	}
	if (FD_ISSET(ifd, &writeset)) {
	    ssize_t n = write(ifd, msg + bytes_written, bytes_to_write);    
	    if (n < 0) {
	    	if (errno != EAGAIN && errno != EINTR) {
	            VLOG_ERR("write on %s: %s", ifname, ovs_strerror(errno));
		    error = errno;
		    goto cleanup;
		}
            } else {
	        bytes_written += n;
		bytes_to_write -= n;
		if (bytes_to_write == 0)
		    close(ifd);
	    }
	}
	if (FD_ISSET(ofd, &readset)) {
	    ssize_t n = read(ofd, reply + bytes_read, bytes_to_read);    
	    if (n < 0) {
	    	if (errno != EAGAIN && errno != EINTR) {
	            VLOG_ERR("read on %s: %s", ofname, ovs_strerror(errno));
		    error = errno;
		    goto cleanup;
		}
            } else if (n == 0) {
	        bytes_to_read = 0;
            } else {
	        bytes_read += n;
		bytes_to_read -= n;
	    }
	}
    }
    if (bytes_read) {
    	reply[bytes_read] = '\0';
	if (preply) {
		*preply = reply;
		reply = NULL; /* prevent freeing the reply msg */
	} else {
		VLOG_ERR("%s returned: %s", script, reply);
	}
	error = EAGAIN;
	goto cleanup;
    }

cleanup:
    free(msg);
    free(reply);
    free(ofname);
    free(ifname);
    close(ifd);
    close(ofd);
    return error;
}

static int
netdev_pltap_up(struct netdev_pltap *dev)
{
    if (!netdev_pltap_finalized(dev)) {
        return 0;
    }
    
    return vsys_transaction("vif_up", NULL, "%s\n"IP_FMT"\n%d\n",
	       dev->real_name,
	       IP_ARGS(dev->local_addr.sin_addr.s_addr),
	       dev->local_netmask);
}

static int
netdev_pltap_down(struct netdev_pltap *dev)
{
    if (!netdev_pltap_finalized(dev)) {
        return 0;
    }
    
    return vsys_transaction("vif_down", NULL, "%s\n", dev->real_name);
}

static int
netdev_pltap_promisc(struct netdev_pltap *dev, bool promisc)
{
    if (!netdev_pltap_finalized(dev)) {
        return 0;
    }

    return vsys_transaction("promisc", NULL, "%s\n%s",
	       dev->real_name,
	       (promisc ? "" : "-\n"));
}

static void
netdev_pltap_sync_flags(struct netdev_pltap *dev)
{

    if (dev->fd < 0 || !netdev_pltap_finalized(dev)) {
    	sync_done(dev);
    	return;
    }
    
    VLOG_DBG("sync_flags(%s): current: %s %s  target: %s %s",
        dev->real_name,
    	(dev->flags & NETDEV_UP ? "UP" : "-"),
    	(dev->flags & NETDEV_PROMISC ? "PROMISC" : "-"),
    	(dev->new_flags & NETDEV_UP ? "UP" : "-"),
    	(dev->new_flags & NETDEV_PROMISC ? "PROMISC" : "-"));

    if ((dev->new_flags & NETDEV_UP) && !(dev->flags & NETDEV_UP)) {
        (void) netdev_pltap_up(dev);
    } else if (!(dev->new_flags & NETDEV_UP) && (dev->flags & NETDEV_UP)) {
        (void) netdev_pltap_down(dev);
    }

    if ((dev->new_flags & NETDEV_PROMISC) ^ (dev->flags & NETDEV_PROMISC)) {
        (void) netdev_pltap_promisc(dev, dev->new_flags & NETDEV_PROMISC);
    }

    netdev_pltap_update_seq(dev);
    sync_done(dev);
}


static int
netdev_pltap_get_config(const struct netdev *dev_, struct smap *args)
{
    struct netdev_pltap *netdev = netdev_pltap_cast(dev_);

    if (netdev->valid_local_ip)
    	smap_add_format(args, "local_ip", IP_FMT,
            IP_ARGS(netdev->local_addr.sin_addr.s_addr));
    if (netdev->valid_local_netmask)
        smap_add_format(args, "local_netmask", "%"PRIu32,
            ntohs(netdev->local_netmask));
    return 0;
}

static int
netdev_pltap_set_config(struct netdev *dev_, const struct smap *args)
{
    struct netdev_pltap *netdev = netdev_pltap_cast(dev_);
    struct shash_node *node;

    VLOG_DBG("pltap_set_config(%s)", netdev_get_name(dev_));
    SMAP_FOR_EACH(node, args) {
        VLOG_DBG("arg: %s->%s", node->name, (char*)node->data);
    	if (!strcmp(node->name, "local_ip")) {
	    struct in_addr addr;
	    if (lookup_ip(node->data, &addr)) {
		VLOG_WARN("%s: bad 'local_ip'", node->name);
	    } else {
		netdev->local_addr.sin_addr = addr;
		netdev->valid_local_ip = true;
	    }
	} else if (!strcmp(node->name, "local_netmask")) {
	    netdev->local_netmask = atoi(node->data);
	    // XXX check valididy
	    netdev->valid_local_netmask = true;
	} else {
	    VLOG_WARN("%s: unknown argument '%s'", 
	    	netdev_get_name(dev_), node->name);
	}
    }
    if (netdev_pltap_finalized(netdev)) {
        netdev->new_flags |= NETDEV_UP;
        sync_needed(netdev);
    }
    return 0;
}

static int
netdev_rx_pltap_recv(struct netdev_rx *rx_, void *buffer, size_t size)
{
    struct netdev_rx_pltap *rx = netdev_rx_pltap_cast(rx_);
    struct tun_pi pi;
    struct iovec iov[2] = {
        { .iov_base = &pi, .iov_len = sizeof(pi) },
	{ .iov_base = buffer, .iov_len = size }
    };
    for (;;) {
        ssize_t retval;
        retval = readv(rx->fd, iov, 2);
        if (retval >= 0) {
            if (retval <= size) {
	    	return retval;
	    } else {
	    	return -EMSGSIZE;
	    }
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error receiveing Ethernet packet on %s: %s",
                    netdev_rx_get_name(rx_), ovs_strerror(errno));
            }
            return -errno;
        }
    }
}

static void
netdev_rx_pltap_wait(struct netdev_rx *rx_)
{
    struct netdev_rx_pltap *rx = netdev_rx_pltap_cast(rx_);
    struct netdev_pltap *netdev =
        netdev_pltap_cast(rx->up.netdev);
    if (rx->fd >= 0 && netdev_pltap_finalized(netdev)) {
        poll_fd_wait(rx->fd, POLLIN);
    }
}

static int
netdev_pltap_send(struct netdev *netdev_, const void *buffer, size_t size)
{
    struct netdev_pltap *dev = 
    	netdev_pltap_cast(netdev_);
    struct tun_pi pi = { 0, 0x86 };
    struct iovec iov[2] = {
        { .iov_base = &pi, .iov_len = sizeof(pi) },
	{ .iov_base = (char*) buffer, .iov_len = size }
    };
    if (dev->fd < 0)
        return EAGAIN;
    for (;;) {
        ssize_t retval;
        retval = writev(dev->fd, iov, 2);
        if (retval >= 0) {
	    if (retval != size + 4) {
	        VLOG_WARN_RL(&rl, "sent partial Ethernet packet (%zd bytes of %zu) on %s",
		             retval, size + 4, netdev_get_name(netdev_));
	    }
            return 0;
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error sending Ethernet packet on %s: %s",
                    netdev_get_name(netdev_), ovs_strerror(errno));
            }
            return errno;
        }
    }
}

static void
netdev_pltap_send_wait(struct netdev *netdev_)
{
    struct netdev_pltap *dev = 
    	netdev_pltap_cast(netdev_);
    if (dev->fd >= 0 && netdev_pltap_finalized(dev)) {
        poll_fd_wait(dev->fd, POLLOUT);
    }
}

static int
netdev_rx_pltap_drain(struct netdev_rx *rx_)
{
    struct netdev_rx_pltap *rx = netdev_rx_pltap_cast(rx_);
    char buffer[128];
    int error;

    if (rx->fd < 0)
    	return EAGAIN;
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
netdev_pltap_set_etheraddr(struct netdev *netdevi OVS_UNUSED,
                           const uint8_t mac[ETH_ADDR_LEN] OVS_UNUSED)
{
    return ENOTSUP;
}


// XXX from netdev-linux.c
static int
get_etheraddr(struct netdev_pltap *dev, uint8_t ea[ETH_ADDR_LEN])
{
    struct ifreq ifr;
    int hwaddr_family;

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, dev->real_name, sizeof ifr.ifr_name);
    if (ioctl(af_inet_sock, SIOCGIFHWADDR, &ifr) < 0) {
        /* ENODEV probably means that a vif disappeared asynchronously and
         * hasn't been removed from the database yet, so reduce the log level
         * to INFO for that case. */
        VLOG(errno == ENODEV ? VLL_INFO : VLL_ERR,
             "ioctl(SIOCGIFHWADDR) on %s device failed: %s",
             dev->real_name, ovs_strerror(errno));
        return errno;
    }
    hwaddr_family = ifr.ifr_hwaddr.sa_family;
    if (hwaddr_family != AF_UNSPEC && hwaddr_family != ARPHRD_ETHER) {
        VLOG_WARN("%s device has unknown hardware address family %d",
                  dev->real_name, hwaddr_family);
    }
    memcpy(ea, ifr.ifr_hwaddr.sa_data, ETH_ADDR_LEN);
    return 0;
}

static int
get_flags(struct netdev_pltap *dev, enum netdev_flags *flags)
{
    struct ifreq ifr;

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, dev->real_name, sizeof ifr.ifr_name);
    if (ioctl(af_inet_sock, SIOCGIFFLAGS, &ifr) < 0)
    	return errno;
    *flags = 0;
    if (ifr.ifr_flags & IFF_UP)
    	*flags |= NETDEV_UP;
    if (ifr.ifr_flags & IFF_PROMISC)
        *flags |= NETDEV_PROMISC;
    return 0;
}

static int
netdev_pltap_get_etheraddr(const struct netdev *netdev,
                           uint8_t mac[ETH_ADDR_LEN])
{
    struct netdev_pltap *dev = 
    	netdev_pltap_cast(netdev);
    if (dev->fd < 0)
        return EAGAIN;
    return get_etheraddr(dev, mac);
}


// XXX can we read stats in planetlab?
static int
netdev_pltap_get_stats(const struct netdev *netdev OVS_UNUSED, struct netdev_stats *stats OVS_UNUSED)
{
    return ENOTSUP;
}

static int
netdev_pltap_set_stats(struct netdev *netdev OVS_UNUSED, const struct netdev_stats *stats OVS_UNUSED)
{
    return ENOTSUP;
}


static int
netdev_pltap_update_flags(struct netdev *dev_,
                          enum netdev_flags off, enum netdev_flags on,
                          enum netdev_flags *old_flagsp)
{
    struct netdev_pltap *netdev =
        netdev_pltap_cast(dev_);
    int error = 0;

    if ((off | on) & ~(NETDEV_UP | NETDEV_PROMISC)) {
        return EINVAL;
    }

    if (netdev_pltap_finalized(netdev)) {
        error = get_flags(netdev, &netdev->flags);
    }
    *old_flagsp = netdev->flags;
    netdev->new_flags |= on;
    netdev->new_flags &= ~off;
    if (netdev->flags != netdev->new_flags) {
	/* we cannot sync here, since we may be in a signal handler */
        sync_needed(netdev);
    }

    return error;
}

static unsigned int
netdev_pltap_change_seq(const struct netdev *netdev)
{
    return netdev_pltap_cast(netdev)->change_seq;
}

/* Helper functions. */

static void
netdev_pltap_update_seq(struct netdev_pltap *dev)
{
    dev->change_seq++;
    if (!dev->change_seq) {
        dev->change_seq++;
    }
}

static void
netdev_pltap_get_real_name(struct unixctl_conn *conn,
                     int argc OVS_UNUSED, const char *argv[], void *aux OVS_UNUSED)
{
    struct netdev_pltap *pltap_dev;

    pltap_dev = shash_find_data(&pltap_netdevs, argv[1]);
    if (!pltap_dev) {
        unixctl_command_reply_error(conn, "no such pltap netdev");
        return;
    }
    if (pltap_dev->fd < 0) {
    	unixctl_command_reply_error(conn, "no real device attached");
	return;
    }

    unixctl_command_reply(conn, pltap_dev->real_name);
}

static int
netdev_pltap_init(void)
{
    list_init(&sync_list);
    af_inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (af_inet_sock < 0) {
        VLOG_ERR("failed to create inet socket: %s", ovs_strerror(errno));
    }
    unixctl_command_register("netdev-pltap/get-tapname", "port",
                             1, 1, netdev_pltap_get_real_name, NULL);
    return 0;
}

static void
netdev_pltap_run(void)
{
    struct netdev_pltap *iter, *next;
    LIST_FOR_EACH_SAFE(iter, next, sync_list, &sync_list) {
        netdev_pltap_sync_flags(iter);
    }
}

static void
netdev_pltap_wait(void)
{
    if (!list_is_empty(&sync_list)) {
        VLOG_DBG("netdev_pltap: scheduling sync");
        poll_immediate_wake();
    }
}

const struct netdev_class netdev_pltap_class = {
    "pltap",
    netdev_pltap_init,
    netdev_pltap_run,  
    netdev_pltap_wait,            

    netdev_pltap_create,
    netdev_pltap_destroy,
    netdev_pltap_get_config,
    netdev_pltap_set_config, 
    NULL,			/* get_tunnel_config */

    netdev_pltap_rx_open,

    netdev_pltap_send, 
    netdev_pltap_send_wait,  

    netdev_pltap_set_etheraddr,
    netdev_pltap_get_etheraddr,
    NULL,			/* get_mtu */
    NULL,			/* set_mtu */
    NULL,                       /* get_ifindex */
    NULL,			/* get_carrier */
    NULL,                       /* get_carrier_resets */
    NULL,                       /* get_miimon */
    netdev_pltap_get_stats,
    netdev_pltap_set_stats,

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

    netdev_pltap_update_flags,

    netdev_pltap_change_seq
};

static const struct netdev_rx_class netdev_rx_pltap_class = {
    netdev_rx_pltap_destroy,
    netdev_rx_pltap_recv,
    netdev_rx_pltap_wait,
    netdev_rx_pltap_drain,

};
