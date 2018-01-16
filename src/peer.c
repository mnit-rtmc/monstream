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
#include "nstr.h"
#include "lock.h"

struct peer {
	struct lock             lock;
	int			fd;
	struct sockaddr_storage addr;
	socklen_t               len;
};

/* Peer host socket address */
static struct peer peer_h;

bool peer_exists(void) {
	bool exists;

 	lock_acquire(&peer_h.lock, __func__);
	exists = peer_h.len;
	lock_release(&peer_h.lock, __func__);

	return exists;
}

static socklen_t peer_get_addr(struct sockaddr_storage *addr) {
	socklen_t len;
 	lock_acquire(&peer_h.lock, __func__);
	*addr = peer_h.addr;
	len = peer_h.len;
	lock_release(&peer_h.lock, __func__);
	return len;
}

static void peer_set_addr(struct sockaddr_storage *addr, socklen_t len) {
 	lock_acquire(&peer_h.lock, __func__);
	peer_h.addr = *addr;
	peer_h.len = len;
	lock_release(&peer_h.lock, __func__);
}

static int peer_get_fd(void) {
	int fd;
 	lock_acquire(&peer_h.lock, __func__);
	fd = peer_h.fd;
	lock_release(&peer_h.lock, __func__);
	return fd;
}

static void peer_set_fd(int fd) {
 	lock_acquire(&peer_h.lock, __func__);
	peer_h.fd = fd;
	lock_release(&peer_h.lock, __func__);
}

static void peer_log(const char *msg) {
	if (peer_exists()) {
		struct sockaddr_storage addr;
		socklen_t len;
		char host[NI_MAXHOST];
		char service[NI_MAXSERV];
		int s;

		len = peer_get_addr(&addr);
		s = getnameinfo((struct sockaddr *) &addr, len, host,
			NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV);
		if (0 == s)
			elog_err("Peer %s:%s %s\n", host, service, msg);
		else
			elog_err("getnameinfo: %s\n", gai_strerror(s));
	} else
		elog_err("No peer address; %s\n", msg);
}

static int peer_bind_try(const char *service) {
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

void peer_bind(const char *service) {
	int fd;
	while (true) {
		fd = peer_bind_try(service);
		if (fd >= 0)
			break;
		sleep(1);
	};
	peer_set_fd(fd);
}

void peer_send(nstr_t str) {
	struct sockaddr_storage addr;
	socklen_t               len;
	int                     fd;
	ssize_t                 n;

	assert(peer_exists());
	fd = peer_get_fd();
	len = peer_get_addr(&addr);
	n = sendto(fd, str.buf, str.len, 0, (struct sockaddr *) &addr, len);
	if (n < 0) {
		elog_err("Send socket: %s\n", strerror(errno));
		peer_log("send error");
	}
}

static void peer_connect(int fd, struct sockaddr_storage *addr, socklen_t len) {
	if (connect(fd, (const struct sockaddr *) addr, len) == 0) {
		peer_set_addr(addr, len);
		peer_log("connected");
	} else
		elog_err("connect: %s\n", strerror(errno));
}

nstr_t peer_recv(nstr_t str) {
	int                     fd;
	struct sockaddr_storage addr;
	socklen_t               len;
	ssize_t                 n;

	fd = peer_get_fd();
	len = sizeof(struct sockaddr_storage);
	n = recvfrom(fd, str.buf, str.buf_len, 0, (struct sockaddr *) &addr,
		&len);
	if (n >= 0) {
		str.len = n;
		if (!peer_exists())
			peer_connect(fd, &addr, len);
	} else {
		elog_err("Read socket: %s\n", strerror(errno));
		peer_log("recv error");
		str.len = 0;
	}
	return str;
}

void peer_init(void) {
	memset(&peer_h, 0, sizeof(struct peer));
	lock_init(&peer_h.lock);
}

void peer_destroy(void) {
	int fd = peer_get_fd();
	if (fd && (close(fd) < 0))
		elog_err("Close: %s\n", strerror(errno));
	lock_destroy(&peer_h.lock);
}
