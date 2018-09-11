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

#include <errno.h>
#include <netdb.h>		/* for socket stuff */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
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

static void cxn_set_timeout(struct cxn *cxn, int fd) {
	struct timeval tv;
	tv.tv_sec = 35;
	tv.tv_usec = 0;
	// Set receive timeout to 35 seconds -- first poll should be received
	// within 30 seconds.  Normally, cxn_send will disconnect on error,
	// but this will allow cxn_recv to disconnect after the timeout.
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
		elog_err("setsockopt: %s\n", strerror(errno));
}

void cxn_bind(struct cxn *cxn, const char *service) {
	int fd;
	while (true) {
		fd = cxn_bind_try(service);
		if (fd >= 0)
			break;
		sleep(1);
	};
	cxn_set_timeout(cxn, fd);
	cxn_set_fd(cxn, fd);
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

bool cxn_send(struct cxn *cxn, nstr_t str) {
	struct sockaddr_storage addr;
	socklen_t               len;
	int                     fd;

	fd = cxn_get_fd(cxn);
	len = cxn_get_addr(cxn, &addr);
	if (len > 0) {
		ssize_t n = sendto(fd, str.buf, str.len, 0,
			(struct sockaddr *) &addr, len);
		if (n >= 0)
			return true;
		elog_err("sendto: %s\n", strerror(errno));
		cxn_log(cxn, "send error");
		cxn_disconnect(cxn, fd);
	}
	return false;
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
		int e = errno;
		elog_err("recvfrom: %s\n", strerror(e));
		cxn_log(cxn, "recv error");
		str.len = 0;
		if (e != 0 && e != EINTR)
			cxn_disconnect(cxn, fd);
	}
	return str;
}

void cxn_destroy(struct cxn *cxn) {
	int fd = cxn_get_fd(cxn);
	if (fd && (close(fd) < 0))
		elog_err("close: %s\n", strerror(errno));
	lock_destroy(&cxn->lock);
}
