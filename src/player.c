/*
 * Copyright (C) 2017  Minnesota Department of Transportation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <errno.h>
#include <gtk/gtk.h>
#include <netdb.h>		/* for socket stuff */
#define _MULTI_THREADED
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>		/* for memset */

void mongrid_set_id(uint32_t idx, const char *mid);
int32_t mongrid_play_stream(uint32_t idx, const char *loc, const char *desc,
	const char *stype);

ssize_t config_load(const char *name, char *buf, size_t n);
ssize_t config_store(const char *name, const char *buf, size_t n);

int open_bind(const char *service) {
	struct addrinfo hints;
	struct addrinfo *ai;
	struct addrinfo *rai = NULL;
	int rc;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	rc = getaddrinfo(NULL, service, &hints, &rai);
	if (rc) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
		return -1;
	}

	for (ai = rai; ai; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype,
			ai->ai_protocol);
		if (fd >= 0) {
			if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
				freeaddrinfo(rai);
				return fd;
			} else
				close(fd);
		}
	}
	freeaddrinfo(rai);
	return -1;
}

static const char *param_end(const char *buf, const char *end) {
	const char *b = buf;
	while (b < end) {
		if ('\x1F' == *b || '\x1E' == *b)
			return b;
		++b;
	}
	return end;
}

static const char *param_next(const char *buf, const char *end) {
	const char *pe = param_end(buf, end);
	return (pe < end) ? pe + 1 : end;
}

static bool param_check(const char *val, const char *buf, const char *pe) {
	return memcmp(val, buf, pe - buf) == 0;
}

static char *nstr_cpy(char *dst, const char *dend, const char *src,
	const char *send)
{
	const char *se = param_end(src, send);
	size_t sn = se - src;
	size_t dn = dend - dst;
	size_t n = (sn < dn) ? sn : dn;
	memcpy(dst, src, n);
	return dst + n;
}

static char *nstr_cat(char *dst, const char *dend, const char *src) {
	const char *s = src;
	char *d = dst;
	while (d < dend) {
		if (0 == *s)
			break;
		*d = *s;
		d++;
		s++;
	}
	return d;
}

static void nstr_end(char *dst, const char *end) {
	if (dst >= end) {
		size_t n = 1 + (dst - end);
		dst -= n;
	}
	*dst = '\0';
}

static int parse_uint(const char *p, const char *end) {
	const char *pe = param_end(p, end);
	char buf[32];
	size_t n = pe - p;
	if (n < 32) {
		memcpy(buf, p, n);
		buf[n] = '\0';
		return atoi(buf);
	} else
		return -1;
}

static void process_play(const char *buf, const char *end) {
	const char *p2 = param_next(buf, end);	// mon index
	const char *p3 = param_next(p2, end);	// camera ID
	const char *p4 = param_next(p3, end);	// stream URI
	const char *p5 = param_next(p4, end);	// stream type
	const char *p6 = param_next(p5, end);	// title
	int mon = parse_uint(p2, end);
	char desc[128];
	char uri[128];
	char stype[16];
	const char *dend = desc + 128;
	const char *uend = uri + 128;
	const char *tend = stype + 16;
	char *u, *s;
	char *d = nstr_cpy(desc, dend, p3, end);
	char fname[16];
	if (p6 < end) {
		d = nstr_cat(d, dend, " --- ");
		d = nstr_cpy(d, dend, p6, end);
	}
	nstr_end(d, dend);
	u = nstr_cpy(uri, uend, p4, end);
	nstr_end(u, uend);
	s = nstr_cpy(stype, tend, p5, end);
	nstr_end(s, tend);
	mongrid_play_stream(mon, uri, desc, stype);
	sprintf(fname, "play.%d", mon);
	config_store(fname, buf, (end - buf));
}

static void process_stop(const char *buf, const char *end) {
	// FIXME
	printf("stop!\n");
}

static void process_monitor(const char *buf, const char *end) {
	const char *p2 = param_next(buf, end);	// mon index
	const char *p3 = param_next(p2, end);	// monitor ID
	int mon = parse_uint(p2, end);
	char mid[8];
	const char *mend = mid + 8;
	char fname[16];
	char *m = nstr_cpy(mid, mend, p3, end);
	nstr_end(m, mend);
	mongrid_set_id(mon, mid);
	sprintf(fname, "monitor.%d", mon);
	config_store(fname, buf, (end - buf));
}

static void process_config(const char *buf, const char *end) {
	config_store("config", buf, (end - buf));
	exit(0);
}

static void process_command(const char *buf, const char *end) {
	const char *pe = param_end(buf, end);
	if (param_check("play", buf, pe))
		process_play(buf, end);
	else if (param_check("stop", buf, pe))
		process_stop(buf, end);
	else if (param_check("monitor", buf, pe))
		process_monitor(buf, end);
	else if (param_check("config", buf, pe))
		process_config(buf, end);
	else
		fprintf(stderr, "Invalid command: %s\n", buf);
}

static const char *command_end(const char *buf, const char *end) {
	const char *b = buf;
	while (b < end) {
		if ('\x1E' == *b)
			return b;
		++b;
	}
	return end;
}

static void process_commands(const char *buf, size_t n) {
	const char *end = buf + n;
	const char *c = buf;
	while (c < end) {
		const char *ce = command_end(c, end);
		process_command(c, ce);
		c = param_next(ce, end);
	}
}

static void load_command(const char *fname) {
	char buf[128];
	ssize_t n = config_load(fname, buf, 128);
	if (n < 0)
		return;
	const char *end = buf + n;
	process_command(buf, end);
}

static void load_commands(uint32_t mon) {
	int i;
	for (i = 0; i < mon; i++) {
		char fname[16];
		sprintf(fname, "monitor.%d", i);
		load_command(fname);
		sprintf(fname, "play.%d", i);
		load_command(fname);
	}
}

void *command_thread(void *data) {
	uint32_t *mon = data;
	char buf[1024];
	int fd;

	load_commands(*mon);
	fd = open_bind("7001");
	if (fd < 0)
		goto out;
	while (true) {
		ssize_t n = read(fd, buf, 1023);
		if (n < 0) {
			fprintf(stderr, "Read socket: %s\n", strerror(errno));
			break;
		}
		buf[n] = '\0';
		process_commands(buf, n);
	}
	close(fd);
out:
	return NULL;
}

uint32_t load_config(void) {
	char buf[128];
	ssize_t n = config_load("config", buf, 128);
	if (n < 0)
		return 1;
	const char *end = buf + n;
	const char *pe = param_end(buf, end);
	if (param_check("config", buf, pe)) {
		const char *p2 = param_next(pe, end);
		int m = parse_uint(p2, end);
		return (m > 0) ? m : 1;
	} else {
		fprintf(stderr, "Invalid command: %s\n", buf);
		return 1;
	}
}
