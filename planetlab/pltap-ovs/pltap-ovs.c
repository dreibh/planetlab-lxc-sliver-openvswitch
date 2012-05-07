#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "tunalloc.h"

#define OVS_SOCK "/var/tun/pl-ovs.control"

char *appname;

#define ERROR(msg)								\
        do {									\
                fprintf(stderr, "%s: %s: %s", appname, msg, strerror(errno));	\
                exit(1);							\
        } while (0)


int send_vif_fd(int sock_fd, int vif_fd, char *vif_name)
{
        int retval;
        struct msghdr msg;
        struct cmsghdr *p_cmsg;
        struct iovec vec;
        size_t cmsgbuf[CMSG_SPACE(sizeof(vif_fd)) / sizeof(size_t)];
        int *p_fds;


        msg.msg_control = cmsgbuf;
        msg.msg_controllen = sizeof(cmsgbuf);
        p_cmsg = CMSG_FIRSTHDR(&msg);
        p_cmsg->cmsg_level = SOL_SOCKET;
        p_cmsg->cmsg_type = SCM_RIGHTS;
        p_cmsg->cmsg_len = CMSG_LEN(sizeof(vif_fd));
        p_fds = (int *) CMSG_DATA(p_cmsg);
        *p_fds = vif_fd;
        msg.msg_controllen = p_cmsg->cmsg_len;
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &vec;
        msg.msg_iovlen = 1;
        msg.msg_flags = 0;

        /* Send the interface name as the iov */
        vec.iov_base = vif_name;
        vec.iov_len = strlen(vif_name)+1;

        while ((retval = sendmsg(sock_fd, &msg, 0)) == -1 && errno == EINTR);
        if (retval == -1) {
                ERROR("sending file descriptor");
        }
        return 0;
}

void send_fd(int p, int fd, char* vif_name)
{
        int control_fd;
        int accept_fd;
        struct sockaddr_un addr, accept_addr;
        socklen_t addr_len = sizeof(accept_addr);
        int i;

        control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (control_fd == -1 && errno != ENOENT) {
                ERROR("Could not create UNIX socket");
        }

        memset(&addr, 0, sizeof(struct sockaddr_un));
        /* Clear structure */
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, OVS_SOCK,
                        sizeof(addr.sun_path) - 1);

        if (unlink(OVS_SOCK) == -1 && errno != ENOENT) {
                ERROR("Could not unlink " OVS_SOCK " control socket");
        }

        if (bind(control_fd, (struct sockaddr *) &addr,
                                sizeof(struct sockaddr_un)) == -1) {
                ERROR("Could not bind to " OVS_SOCK " control socket");
        }

        if (listen(control_fd, 5) == -1) {
                ERROR("listen on " OVS_SOCK " failed");
        }
        if (write(p, "1", 1) != 1) {
                ERROR("writing on the synch pipe");
        }
        if ((accept_fd = accept(control_fd, (struct sockaddr*) &accept_addr,
                                                &addr_len)) == -1) {
                ERROR("accept on " OVS_SOCK " failed");
        }
        send_vif_fd(accept_fd, fd, vif_name);
}

int main(int argc, char* argv[])
{
        char if_name[IFNAMSIZ];
        int p[2]; // synchronization pipe
        char dummy;

        if (pipe(p) < 0) {
                ERROR("pipe");
        }

        int tun_fd = tun_alloc(IFF_TAP, if_name);

        appname = argv[0];

        switch(fork()) {
        case -1:
                ERROR("fork");
                exit(1);
        case 0:
                close(1);
                open("/dev/null", O_WRONLY);
                close(p[0]);
                send_fd(p[1], tun_fd, if_name);
                exit(0);
        default:
                close(p[1]);
                if (read(p[0], &dummy, 1) != 1) {
                        ERROR("reading from the synch pipe");
                }
                printf("%s\n", if_name);
        }
        return 0;
}
