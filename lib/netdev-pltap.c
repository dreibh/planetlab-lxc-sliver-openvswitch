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

struct netdev_dev_pltap {
    struct netdev_dev netdev_dev;
    char *real_name;
    char *error;
    struct netdev_stats stats;
    enum netdev_flags flags;
    int fd;
    struct sockaddr_in local_addr;
    int local_netmask;
    bool valid_local_ip;
    bool valid_local_netmask;
    bool finalized;
    unsigned int change_seq;
};

struct netdev_pltap {
    struct netdev netdev;
} ;

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(5, 20);

static struct shash pltap_netdev_devs = SHASH_INITIALIZER(&pltap_netdev_devs);

static int netdev_pltap_create(const struct netdev_class *, const char *,
                               struct netdev_dev **);

static struct shash pltap_creating = SHASH_INITIALIZER(&pltap_creating);

static void netdev_pltap_update_seq(struct netdev_dev_pltap *);

static bool
is_pltap_class(const struct netdev_class *class)
{
    return class->create == netdev_pltap_create;
}

static struct netdev_dev_pltap *
netdev_dev_pltap_cast(const struct netdev_dev *netdev_dev)
{
    assert(is_pltap_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev_dev, struct netdev_dev_pltap, netdev_dev);
}

static struct netdev_pltap *
netdev_pltap_cast(const struct netdev *netdev)
{
    struct netdev_dev *netdev_dev = netdev_get_dev(netdev);
    assert(is_pltap_class(netdev_dev_get_class(netdev_dev)));
    return CONTAINER_OF(netdev, struct netdev_pltap, netdev);
}

static int
netdev_pltap_create(const struct netdev_class *class OVS_UNUSED, const char *name,
                    struct netdev_dev **netdev_devp)
{
    struct netdev_dev_pltap *netdev_dev;
    int error;

    netdev_dev = xzalloc(sizeof *netdev_dev);

    netdev_dev->real_name = xzalloc(IFNAMSIZ + 1);
    netdev_dev->error = NULL;
    memset(&netdev_dev->local_addr, 0, sizeof(netdev_dev->local_addr));
    netdev_dev->valid_local_ip = false;
    netdev_dev->valid_local_netmask = false;
    netdev_dev->finalized = false;


    /* Open tap device. */
    netdev_dev->fd = tun_alloc(IFF_TAP, netdev_dev->real_name);
    if (netdev_dev->fd < 0) {
        error = errno;
        VLOG_WARN("tun_alloc(IFF_TAP, %s) failed: %s", name, strerror(error));
        goto cleanup;
    }
    VLOG_DBG("real_name = %s", netdev_dev->real_name);

    /* Make non-blocking. */
    error = set_nonblocking(netdev_dev->fd);
    if (error) {
        goto cleanup;
    }

    netdev_dev_init(&netdev_dev->netdev_dev, name, &netdev_pltap_class);
    shash_add(&pltap_netdev_devs, name, netdev_dev);
    *netdev_devp = &netdev_dev->netdev_dev;
    return 0;

cleanup:
    free(netdev_dev);
    return error;
}

static void
netdev_pltap_destroy(struct netdev_dev *netdev_dev_)
{
    struct netdev_dev_pltap *netdev_dev = netdev_dev_pltap_cast(netdev_dev_);

    if (netdev_dev->fd != -1)
    	close(netdev_dev->fd);

    shash_find_and_delete(&pltap_netdev_devs,
                          netdev_dev_get_name(netdev_dev_));
    free(netdev_dev);
}

static int
netdev_pltap_open(struct netdev_dev *netdev_dev_, struct netdev **netdevp)
{
    struct netdev_pltap *netdev;

    netdev = xmalloc(sizeof *netdev);
    netdev_init(&netdev->netdev, netdev_dev_);

    *netdevp = &netdev->netdev;
    return 0;
}

static void
netdev_pltap_close(struct netdev *netdev_)
{
    struct netdev_pltap *netdev = netdev_pltap_cast(netdev_);
    free(netdev);
}

static int
netdev_pltap_create_finalize(struct netdev_dev_pltap *dev)
{
    int ifd = -1, ofd = -1, maxfd;
    size_t bytes_to_write, bytes_to_read = 1024,
           bytes_written = 0, bytes_read = 0;
    int error = 0;
    char *msg = NULL, *reply = NULL;

    if (dev->finalized)
        return 0;
    if (!dev->valid_local_ip || !dev->valid_local_netmask)
        return 0;
    
    ofd = open("/vsys/vif_up.out", O_RDONLY | O_NONBLOCK);
    if (ofd < 0) {
        VLOG_ERR("Cannot open vif_up.out: %s", strerror(errno));
	error = errno;
	goto cleanup;
    }
    ifd = open("/vsys/vif_up.in", O_WRONLY | O_NONBLOCK);
    if (ifd < 0) {
        VLOG_ERR("Cannot open vif_up.in: %s", strerror(errno));
	error = errno;
	goto cleanup;
    }
    maxfd = (ifd < ofd) ? ofd : ifd;

    msg = xasprintf("%s\n"IP_FMT"\n%d\n",
       dev->real_name,
       IP_ARGS(&dev->local_addr.sin_addr),
       dev->local_netmask);
    reply = (char*)xmalloc(bytes_to_read);
    if (!msg || !reply) {
        VLOG_ERR("Out of memory");
	error = ENOMEM;
	goto cleanup;
    }
    bytes_to_write = strlen(msg);
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
	    VLOG_ERR("selec error: %s", strerror(errno));
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
	            VLOG_ERR("write on vif_up.in: %s", strerror(errno));
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
	            VLOG_ERR("read on vif_up.out: %s", strerror(errno));
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
        VLOG_ERR("vif_up returned: %s", reply);
	dev->error = reply;
	reply = NULL;
	error = EAGAIN;
	goto cleanup;
    }
    dev->finalized = true;
    free(dev->error);
    dev->error = NULL;
    netdev_pltap_update_seq(dev);

cleanup:
    free(msg);
    free(reply);
    close(ifd);
    close(ofd);
    return error;
}

static int
netdev_pltap_get_config(struct netdev_dev *dev_, struct smap *args)
{
    struct netdev_dev_pltap *netdev_dev = netdev_dev_pltap_cast(dev_);

    if (netdev_dev->valid_local_ip)
    	smap_add_format(args, "local_ip", IP_FMT,
            IP_ARGS(&netdev_dev->local_addr.sin_addr));
    if (netdev_dev->valid_local_netmask)
        smap_add_format(args, "local_netmask", "%"PRIu32,
            ntohs(netdev_dev->local_netmask));
    return netdev_pltap_create_finalize(netdev_dev);
}

static int
netdev_pltap_set_config(struct netdev_dev *dev_, const struct smap *args)
{
    struct netdev_dev_pltap *netdev_dev = netdev_dev_pltap_cast(dev_);
    struct shash_node *node;

    VLOG_DBG("pltap_set_config(%s)", netdev_dev_get_name(dev_));
    SMAP_FOR_EACH(node, args) {
        VLOG_DBG("arg: %s->%s", node->name, (char*)node->data);
    	if (!strcmp(node->name, "local_ip")) {
	    struct in_addr addr;
	    if (lookup_ip(node->data, &addr)) {
		VLOG_WARN("%s: bad 'local_ip'", node->name);
	    } else {
		netdev_dev->local_addr.sin_addr = addr;
		netdev_dev->valid_local_ip = true;
	    }
	} else if (!strcmp(node->name, "local_netmask")) {
	    netdev_dev->local_netmask = atoi(node->data);
	    // XXX check valididy
	    netdev_dev->valid_local_netmask = true;
	} else {
	    VLOG_WARN("%s: unknown argument '%s'", 
	    	netdev_dev_get_name(dev_), node->name);
	}
    }
    return netdev_pltap_create_finalize(netdev_dev);        
}

static int
netdev_pltap_listen(struct netdev *netdev_ OVS_UNUSED)
{
    return 0;
}

static int
netdev_pltap_recv(struct netdev *netdev_, void *buffer, size_t size)
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev_));
    struct tun_pi pi;
    struct iovec iov[2] = {
        { .iov_base = &pi, .iov_len = sizeof(pi) },
	{ .iov_base = buffer, .iov_len = size }
    };
    if (!dev->finalized)
        return -EAGAIN;
    for (;;) {
        ssize_t retval;
        retval = readv(dev->fd, iov, 2);
        if (retval >= 0) {
            if (retval <= size) {
	    	return retval;
	    } else {
	    	return -EMSGSIZE;
	    }
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error receiveing Ethernet packet on %s: %s",
                    netdev_get_name(netdev_), strerror(errno));
            }
            return -errno;
        }
    }
}

static void
netdev_pltap_recv_wait(struct netdev *netdev_)
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev_));
    if (dev->finalized && dev->fd >= 0) {
        poll_fd_wait(dev->fd, POLLIN);
    }
}

static int
netdev_pltap_send(struct netdev *netdev_, const void *buffer, size_t size)
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev_));
    struct tun_pi pi = { 0, htons(ETH_P_IP) };
    struct iovec iov[2] = {
        { .iov_base = &pi, .iov_len = sizeof(pi) },
	{ .iov_base = buffer, .iov_len = size }
    };
    if (dev->fd < 0 || !dev->finalized)
        return EAGAIN;
    for (;;) {
        ssize_t retval;
        retval = writev(dev->fd, iov, 2);
        if (retval >= 0) {
	    if (retval != size + sizeof(pi)) {
	        VLOG_WARN_RL(&rl, "sent partial Ethernet packet (%zd bytes of %zu) on %s",
		             retval, size + sizeof(pi), netdev_get_name(netdev_));
	    }
            return 0;
        } else if (errno != EINTR) {
            if (errno != EAGAIN) {
                VLOG_WARN_RL(&rl, "error sending Ethernet packet on %s: %s",
                    netdev_get_name(netdev_), strerror(errno));
            }
            return errno;
        }
    }
}

static void
netdev_pltap_send_wait(struct netdev *netdev_)
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev_));
    if (dev->finalized && dev->fd >= 0) {
        poll_fd_wait(dev->fd, POLLOUT);
    }
}

static int
netdev_pltap_drain(struct netdev *netdev_)
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev_));
    char buffer[128];
    int error;

    if (dev->fd < 0 || !dev->finalized)
    	return 0;
    for (;;) {
    	error = recv(dev->fd, buffer, 128, MSG_TRUNC);
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
get_etheraddr(const char *netdev_name, uint8_t ea[ETH_ADDR_LEN])
{
    struct ifreq ifr;
    int hwaddr_family;
    int af_inet_sock;

    /* Create AF_INET socket. */
    af_inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (af_inet_sock < 0) {
        VLOG_ERR("failed to create inet socket: %s", strerror(errno));
    }

    memset(&ifr, 0, sizeof ifr);
    ovs_strzcpy(ifr.ifr_name, netdev_name, sizeof ifr.ifr_name);
    if (ioctl(af_inet_sock, SIOCGIFHWADDR, &ifr) < 0) {
        /* ENODEV probably means that a vif disappeared asynchronously and
         * hasn't been removed from the database yet, so reduce the log level
         * to INFO for that case. */
        VLOG(errno == ENODEV ? VLL_INFO : VLL_ERR,
             "ioctl(SIOCGIFHWADDR) on %s device failed: %s",
             netdev_name, strerror(errno));
	close(af_inet_sock);
        return errno;
    }
    hwaddr_family = ifr.ifr_hwaddr.sa_family;
    if (hwaddr_family != AF_UNSPEC && hwaddr_family != ARPHRD_ETHER) {
        VLOG_WARN("%s device has unknown hardware address family %d",
                  netdev_name, hwaddr_family);
    }
    memcpy(ea, ifr.ifr_hwaddr.sa_data, ETH_ADDR_LEN);
    close(af_inet_sock);
    return 0;
}

static int
netdev_pltap_get_etheraddr(const struct netdev *netdev,
                           uint8_t mac[ETH_ADDR_LEN])
{
    struct netdev_dev_pltap *dev = 
    	netdev_dev_pltap_cast(netdev_get_dev(netdev));
    if (dev->fd < 0 || !dev->finalized)
        return EAGAIN;
    return get_etheraddr(dev->real_name, mac);
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
netdev_pltap_update_flags(struct netdev *netdev,
                          enum netdev_flags off, enum netdev_flags on,
                          enum netdev_flags *old_flagsp)
{
    struct netdev_dev_pltap *dev =
        netdev_dev_pltap_cast(netdev_get_dev(netdev));

    if ((off | on) & ~(NETDEV_UP | NETDEV_PROMISC)) {
        return EINVAL;
    }

    // XXX should we actually do something with these flags?
    *old_flagsp = dev->flags;
    dev->flags |= on;
    dev->flags &= ~off;
    if (*old_flagsp != dev->flags) {
        netdev_pltap_update_seq(dev);
    }
    return 0;
}

static unsigned int
netdev_pltap_change_seq(const struct netdev *netdev)
{
    return netdev_dev_pltap_cast(netdev_get_dev(netdev))->change_seq;
}

/* Helper functions. */

static void
netdev_pltap_update_seq(struct netdev_dev_pltap *dev)
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
    struct netdev_dev_pltap *pltap_dev;

    pltap_dev = shash_find_data(&pltap_netdev_devs, argv[1]);
    if (!pltap_dev) {
        unixctl_command_reply_error(conn, "no such pltap netdev");
        return;
    }
    if (pltap_dev->error) {
    	unixctl_command_reply_error(conn, pltap_dev->error);
	return;
    }

    unixctl_command_reply(conn, pltap_dev->real_name);
}

static int
netdev_pltap_init(void)
{
    unixctl_command_register("netdev-pltap/get-tapname", "port",
                             1, 1, netdev_pltap_get_real_name, NULL);
    return 0;
}

const struct netdev_class netdev_pltap_class = {
    "pltap",
    netdev_pltap_init,
    NULL,  
    NULL,            

    netdev_pltap_create,
    netdev_pltap_destroy,
    netdev_pltap_get_config,
    netdev_pltap_set_config, 

    netdev_pltap_open,
    netdev_pltap_close,

    netdev_pltap_listen,
    netdev_pltap_recv,
    netdev_pltap_recv_wait,
    netdev_pltap_drain,

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
