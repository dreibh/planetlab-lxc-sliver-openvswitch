/*
 * tunproxy.c --- small demo program for tunneling over UDP with tun/tap
 *
 * Copyright (C) 2003  Philippe Biondi <phil@secdev.org>
 * Copyright (C) 2013  Felician Nemeth <nemethf@tmit.bme.hu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * http://www.secdev.org/projects/tuntap_udp/files/tunproxy.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <errno.h>

#define PERROR(x) do { perror(x); exit(1); } while (0)
#define ERROR(x, args ...) do { fprintf(stderr,"ERROR:" x, ## args); exit(1); } while (0)

extern void exit(int);

void usage()
{
	fprintf(stderr, "Usage: tunproxy -t target_ip:port [-p local_port] [-e]\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sin, sout, remote;
	struct ifreq ifr;
	int fd, s, remote_len, remote_port, local_port, l;
	unsigned int soutlen;
	char c, *p, *remote_ip = 0;
	char buf[2000];
	fd_set fdset;

	int TUNMODE = IFF_TUN, DEBUG = 0;

	while ((c = getopt(argc, argv, "t:p:ehd")) != -1) {
		switch (c) {
		case 'h':
			usage();
		case 'd':
			DEBUG++;
			break;
		case 'p':
			local_port = atoi(optarg);
			break;
		case 't':
			p = memchr(optarg,':',16);
			if (!p) ERROR("invalid argument : [%s]\n",optarg);
			*p = 0;
			remote_ip = optarg;
			remote_port = atoi(p+1);
			break;
		case 'e':
			TUNMODE = IFF_TAP;
			break;
		default:
			usage();
		}
	}
	if (remote_ip == 0) usage();

	if ( (fd = open("/dev/net/tun",O_RDWR)) < 0) PERROR("open");

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = TUNMODE | IFF_NO_PI;
	strncpy(ifr.ifr_name, "toto%d", IFNAMSIZ);
	if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) PERROR("ioctl");

	printf("Allocated interface %s. Configure and use it\n", ifr.ifr_name);
	
	s = socket(PF_INET, SOCK_DGRAM, 0);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(local_port);

	if ( bind(s,(struct sockaddr *)&sin, sizeof(sin)) < 0)
	  PERROR("bind");

	remote_len = sizeof(remote);
	memset(&remote, 0, remote_len);
	remote.sin_family = AF_INET;
	remote.sin_port = htons(remote_port);
	remote.sin_addr.s_addr=inet_addr(remote_ip);

	while (1) {
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		FD_SET(s, &fdset);
		if (select(fd+s+1, &fdset,NULL,NULL,NULL) < 0) PERROR("select");
		if (FD_ISSET(fd, &fdset)) {
			if (DEBUG)
			  if (write(1,">", 1) < 0) PERROR("write");
			l = read(fd, buf, sizeof(buf));
			if (l < 0) 
			  PERROR("read");
			if (sendto(s, buf, l, 0, (struct sockaddr *)&remote, remote_len) < 0)
			  PERROR("sendto");
		}
		if (FD_ISSET(s, &fdset)) {
			if (DEBUG) 
			  if (write(1,"<", 1) < 0) PERROR("write");
			soutlen = sizeof(sout);
			l = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&sout, &soutlen);
			if (l == -1) {
			  if (write(1,"(", 1) < 0) PERROR("write");
			  fprintf(stderr, "[%s,%d]", strerror(errno), l);
			  continue;
			}
			if ((sout.sin_addr.s_addr != remote.sin_addr.s_addr) ||
			    (sout.sin_port != remote.sin_port)) {
			  printf("Got packet from  %s:%u instead of %s:%u\n", 
				 inet_ntoa(sout.sin_addr), ntohs(sout.sin_port),
				 inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));
			}
			if (write(fd, buf, l) < 0) PERROR("write");
		}
	}
}
