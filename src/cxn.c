/*
 * Copyright (C) 2017-2018  Minnesota Department of Transportation
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

#include <assert.h>
#include <errno.h>
#include <netdb.h>		/* for socket stuff */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "elog.h"
#include "lock.h"
#include "cxn.h"

/* Connection struct */
struct cxn {
	struct lock             lock;
	int			fd;
	struct sockaddr_storage addr;
	socklen_t               len;
};

struct cxn *cxn_create(void) {
	struct cxn *cxn = malloc(sizeof(struct cxn));
	memset(cxn, 0, sizeof(struct cxn));
	lock_init(&cxn->lock);
	return cxn;
}

static int cxn_get_fd(struct cxn *cxn) {
	int fd;
 	lock_acquire(&cxn->lock, __func__);
	fd = cxn->fd;
	lock_release(&cxn->lock, __func__);
	return fd;
}

bool cxn_established(struct cxn *cxn) {
	bool exists;

 	lock_acquire(&cxn->lock, __func__);
	exists = cxn->len;
	lock_release(&cxn->lock, __func__);

	return exists;
}

static socklen_t cxn_get_addr(struct cxn *cxn, struct sockaddr_storage *addr) {
	socklen_t len;
 	lock_acquire(&cxn->lock, __func__);
	*addr = cxn->addr;
	len = cxn->len;
	lock_release(&cxn->lock, __func__);
	return len;
}

static void cxn_set_addr(struct cxn *cxn, struct sockaddr_storage *addr,
	socklen_t len)
{
 	lock_acquire(&cxn->lock, __func__);
	cxn->addr = *addr;
	cxn->len = len;
	lock_release(&cxn->lock, __func__);
}

static void cxn_set_fd(struct cxn *cxn, int fd) {
 	lock_acquire(&cxn->lock, __func__);
	cxn->fd = fd;
	lock_release(&cxn->lock, __func__);
}

static void cxn_log(struct cxn *cxn, const char *msg) {
	if (cxn_established(cxn)) {
		struct sockaddr_storage addr;
		socklen_t len;
		char host[NI_MAXHOST];
		char service[NI_MAXSERV];
		int s;

		len = cxn_get_addr(cxn, &addr);
		s = getnameinfo((struct sockaddr *) &addr, len, host,
			NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV);
		if (0 == s)
			elog_err("cxn: %s:%s %s\n", host, service, msg);
		else
			elog_err("getnameinfo: %s\n", gai_strerror(s));
	} else
		elog_err("No connection address; %s\n", msg);
}

static int cxn_bind_try(const char *service) {
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
		elog_err("getaddrinfo: %s\n", gai_strerror(rc));
		goto fail;
	}
	for (ai = rai; ai; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype,
			ai->ai_protocol);
		if (fd >= 0) {
			if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
				freeaddrinfo(rai);
				return fd;
			} else {
				elog_err("bind: %s\n", strerror(errno));
				close(fd);
			}
		} else {
			elog_err("socket: %s\n", strerror(errno));
		}
	}
	freeaddrinfo(rai);
fail:
	elog_err("Could not bind to port: %s\n", service);
	return -1;
}

void cxn_bind(struct cxn *cxn, const char *service) {
	int fd;
	while (true) {
		fd = cxn_bind_try(service);
		if (fd >= 0)
			break;
		sleep(1);
	};
	cxn_set_fd(cxn, fd);
}

void cxn_send(struct cxn *cxn, nstr_t str) {
	struct sockaddr_storage addr;
	socklen_t               len;
	int                     fd;
	ssize_t                 n;

	assert(cxn_established(cxn));
	fd = cxn_get_fd(cxn);
	len = cxn_get_addr(cxn, &addr);
	n = sendto(fd, str.buf, str.len, 0, (struct sockaddr *) &addr, len);
	if (n < 0) {
		elog_err("Send socket: %s\n", strerror(errno));
		cxn_log(cxn, "send error");
	}
}

static void cxn_connect(struct cxn *cxn, int fd, struct sockaddr_storage *addr,
	socklen_t len)
{
	if (connect(fd, (const struct sockaddr *) addr, len) == 0) {
		cxn_set_addr(cxn, addr, len);
		cxn_log(cxn, "connected");
	} else
		elog_err("connect: %s\n", strerror(errno));
}

static void cxn_disconnect(struct cxn *cxn, int fd) {
	struct sockaddr_storage addr;

	memset(&addr, 0, sizeof(struct sockaddr_storage));
	addr.ss_family = AF_UNSPEC;
	if (connect(fd, (const struct sockaddr *) &addr,
		sizeof(struct sockaddr_storage)) == 0)
	{
		cxn_log(cxn, "disconnected");
		cxn_set_addr(cxn, &addr, 0);
	} else
		elog_err("disconnect: %s\n", strerror(errno));
}

nstr_t cxn_recv(struct cxn *cxn, nstr_t str) {
	int                     fd;
	struct sockaddr_storage addr;
	socklen_t               len;
	ssize_t                 n;

	fd = cxn_get_fd(cxn);
	len = sizeof(struct sockaddr_storage);
	n = recvfrom(fd, str.buf, str.buf_len, 0, (struct sockaddr *) &addr,
		&len);
	if (n >= 0) {
		str.len = n;
		if (!cxn_established(cxn))
			cxn_connect(cxn, fd, &addr, len);
	} else {
		if (ECONNREFUSED == errno)
			cxn_disconnect(cxn, fd);
		else {
			elog_err("Read socket: %s\n", strerror(errno));
			cxn_log(cxn, "recv error");
		}
		str.len = 0;
	}
	return str;
}

void cxn_destroy(struct cxn *cxn) {
	int fd = cxn_get_fd(cxn);
	if (fd && (close(fd) < 0))
		elog_err("Close: %s\n", strerror(errno));
	lock_destroy(&cxn->lock);
}
