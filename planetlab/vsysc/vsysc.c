#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define VSYS_PATH "/vsys"

#define MAXPATH 1024
#define BUFSIZE 4096

#define IN 0
#define OUT 1

char ctrl[2][MAXPATH]; /* paths of vsys.in & vsys.out */

static void mkpath(int dir, const char* vsys)
{
	static const char *suffix[] = { "in", "out" };
	int n;

	if ( (n = snprintf(ctrl[dir], MAXPATH, "%s/%s.%s", VSYS_PATH, vsys, suffix[dir])) < 0) {
		perror(vsys);
		exit(EXIT_FAILURE);
	} else if (n >= MAXPATH) {
		fprintf(stderr, "argument too long\n");
		exit(EXIT_FAILURE);
	}
}

static int open_ctrl(int dir)
{
	int fd;
	
	if ( (fd = open(ctrl[dir], (dir == IN ? O_WRONLY : O_RDONLY) | O_NONBLOCK)) < 0) {
		perror(ctrl[dir]);
		exit(EXIT_FAILURE);
	}
	return fd;
}


static void set_nonblocking(int fd)
{
	int val;

	if ( (val = fcntl(fd, F_GETFL, 0)) < 0) {
		perror("fcntl F_GETFL");
		exit(EXIT_FAILURE);
	}
	if (fcntl(fd, F_SETFL, val | O_NONBLOCK) < 0) {
		perror("fcntl F_SETFL");
		exit(EXIT_FAILURE);
	}
}

#if 0
static void print_set(const char* name, int max, const fd_set* set)
{
	int i, n = 0;
	fprintf(stderr, "%s: {", name);
	for (i = 0; i < max; i++) {
		if (FD_ISSET(i, set)) {
			if (n++) fprintf(stderr, ", ");
			fprintf(stderr, "%d", i);
		}
	}
	fprintf(stderr, "}\n");
}
#endif

struct channel {
	const char *name;
	int active;
	int writing;
	char buf[BUFSIZE];
	char *rp, *wp;
	int rfd, wfd;
};

static int active_channels = 0;

static void channel_init(struct channel *c, const char* name, int rfd, int wfd)
{
	c->name = name;
	c->rp = c->buf;
	c->wp = c->buf;
	c->rfd = rfd;
	c->wfd = wfd;
	c->active = 1;
	active_channels++;
}

static void channel_fdset(struct channel *c, fd_set* readset, fd_set* writeset)
{
	if (!c->active)
		return;
	if (c->writing) {
		FD_SET(c->wfd, writeset);
	} else {
		FD_SET(c->rfd, readset);
	} 
}

static void channel_run(struct channel *c, const fd_set* readset, const fd_set* writeset)
{
	int n;

	if (!c->active)
		return;
	if (c->writing) {
		if (FD_ISSET(c->wfd, writeset)) {
			if ( (n = write(c->wfd, c->wp, c->rp - c->wp)) < 0) {
				perror(c->name);
				exit(EXIT_FAILURE);
			}
			c->wp += n;
			if (c->wp == c->rp) {
				c->wp = c->rp = c->buf;
				c->writing = 0;
			} 
		}
	} else {
		if (FD_ISSET(c->rfd, readset)) {
			if ( (n = read(c->rfd, c->rp, BUFSIZE)) < 0) {
				perror(c->name);
				exit(EXIT_FAILURE);
			}
			if (n) {
				c->wp = c->rp;
				c->rp += n;
				c->writing = 1;
			} else {
				close(c->wfd);
				c->active = 0;
				active_channels--;
			}
		}
	}
}

static struct channel channels[2];


int main(int argc, char *argv[])
{
	int fd[2]; /* fds of vsys.in & vsys.out */
	int maxfd;

	fd_set readset, writeset;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <vsys>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	mkpath(IN,  argv[1]);
	mkpath(OUT, argv[1]);

	maxfd = (STDOUT_FILENO > STDIN_FILENO ? STDOUT_FILENO : STDIN_FILENO);

	fd[OUT] = open_ctrl(OUT);
	if (fd[OUT] > maxfd)
		maxfd = fd[OUT];
	fd[IN]  = open_ctrl(IN);
	if (fd[IN] > maxfd)
		maxfd = fd[IN];

	set_nonblocking(STDIN_FILENO);
	set_nonblocking(STDOUT_FILENO);

	channel_init(&channels[IN], "IN", STDIN_FILENO, fd[IN]);
	channel_init(&channels[OUT], "OUT", fd[OUT], STDOUT_FILENO);

	while (active_channels) {
		FD_ZERO(&readset);
		FD_ZERO(&writeset);
		channel_fdset(&channels[IN], &readset, &writeset);
		channel_fdset(&channels[OUT], &readset, &writeset);
		if (select(maxfd + 1, &readset, &writeset, NULL, NULL) < 0) {
			perror("select");
			exit(EXIT_FAILURE);
		}
		channel_run(&channels[IN], &readset, &writeset);
		channel_run(&channels[OUT], &readset, &writeset);
	}
	return EXIT_SUCCESS;
}

