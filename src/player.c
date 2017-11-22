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

#include <assert.h>
#include <errno.h>
#include <netdb.h>		/* for socket stuff */
#define _MULTI_THREADED
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>		/* for memset */
#include <unistd.h>
#include "elog.h"
#include "nstr.h"
#include "config.h"
#include "lock.h"

/* Host name of peer from which commands are accepted */
const char *PEER = "tms-iris.dot.state.mn.us";

/* ASCII separators */
static const char RECORD_SEP = '\x1E';
static const char UNIT_SEP = '\x1F';

void mongrid_create(bool gui, bool stats);
int32_t mongrid_init(uint32_t num);
void mongrid_run(void);
void mongrid_restart(void);
void mongrid_clear(void);
void mongrid_destroy(void);
void mongrid_set_mon(uint32_t idx, const char *mid, const char *accent,
	bool aspect, uint32_t font_sz);
void mongrid_play_stream(uint32_t idx, const char *cam_id, const char *loc,
	const char *desc, const char *encoding, uint32_t latency);
nstr_t mongrid_status(nstr_t str);

#define DEFAULT_LATENCY	(50)
#define DEFAULT_FONT_SZ (32)

static int open_bind(const char *service) {
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

static void connect_peer(int fd, const char *peer) {
	struct addrinfo hints;
	struct addrinfo *ai;
	struct addrinfo *rai = NULL;
	int rc;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	rc = getaddrinfo(peer, NULL, &hints, &rai);
	if (rc) {
		elog_err("getaddrinfo: %s\n", gai_strerror(rc));
		return;
	}
	for (ai = rai; ai; ai = ai->ai_next) {
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
			freeaddrinfo(rai);
			return;
		}
	}
	elog_err("Could not connect to peer: %s\n", peer);
	freeaddrinfo(rai);
	return;
}

static uint32_t parse_latency(nstr_t lat) {
	int l = nstr_parse_u32(lat);
	return (l > 0) ? l : DEFAULT_LATENCY;
}

static void process_play(nstr_t cmd) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, UNIT_SEP);	// "play"
	nstr_t p2 = nstr_split(&str, UNIT_SEP);	// mon index
	nstr_t p3 = nstr_split(&str, UNIT_SEP);	// camera ID
	nstr_t p4 = nstr_split(&str, UNIT_SEP);	// stream URI
	nstr_t p5 = nstr_split(&str, UNIT_SEP);	// encoding
	nstr_t p6 = nstr_split(&str, UNIT_SEP);	// title
	nstr_t p7 = nstr_split(&str, UNIT_SEP);	// latency
	assert(nstr_cmp_z(p1, "play"));
	int mon = nstr_parse_u32(p2);
	if (mon >= 0) {
		char cam_id[20];
		char desc[128];
		char uri[128];
		char encoding[16];
		char fname[16];

		nstr_wrap(cam_id, sizeof(cam_id), p3);
		nstr_wrap(uri, sizeof(uri), p4);
		nstr_wrap(encoding, sizeof(encoding), p5);
		nstr_wrap(desc, sizeof(desc), p6);
		elog_cmd(cmd);
		mongrid_play_stream(mon, cam_id, uri, desc, encoding,
			parse_latency(p7));
		sprintf(fname, "play.%d", mon);
		config_store(fname, cmd);
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static uint32_t parse_font_sz(nstr_t fsz) {
	int s = nstr_parse_u32(fsz);
	return (s > 0) ? s : DEFAULT_FONT_SZ;
}

static void process_monitor(nstr_t cmd) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, UNIT_SEP);	// "monitor"
	nstr_t p2 = nstr_split(&str, UNIT_SEP);	// mon index
	nstr_t p3 = nstr_split(&str, UNIT_SEP);	// monitor ID
	nstr_t p4 = nstr_split(&str, UNIT_SEP);	// accent color
	nstr_t p5 = nstr_split(&str, UNIT_SEP); // force-aspect-ratio
	nstr_t p6 = nstr_split(&str, UNIT_SEP);	// font size
	assert(nstr_cmp_z(p1, "monitor"));
	int mon = nstr_parse_u32(p2);
	if (mon >= 0) {
		char mid[8];
		char accent[8];
		char fname[16];
		int aspect;
		nstr_wrap(mid, sizeof(mid), p3);
		nstr_wrap(accent, sizeof(accent), p4);
		aspect = nstr_parse_u32(p5);
		elog_cmd(cmd);
		mongrid_set_mon(mon, mid, accent, aspect, parse_font_sz(p6));
		sprintf(fname, "monitor.%d", mon);
		config_store(fname, cmd);
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static void process_config(nstr_t cmd) {
	elog_cmd(cmd);
	config_store("config", cmd);
	mongrid_restart();
}

static void process_command(nstr_t cmd) {
	nstr_t p1 = nstr_chop(cmd, UNIT_SEP);
	if (nstr_cmp_z(p1, "play"))
		process_play(cmd);
	else if (nstr_cmp_z(p1, "monitor"))
		process_monitor(cmd);
	else if (nstr_cmp_z(p1, "config"))
		process_config(cmd);
	else
		elog_err("Invalid command: %s\n", nstr_z(cmd));
}

static void process_commands(nstr_t str) {
	while (nstr_len(str)) {
		process_command(nstr_split(&str, RECORD_SEP));
	}
}

static void load_command(const char *fname) {
	char buf[128];
	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	process_commands(config_load(fname, str));
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

struct peer {
	struct lock             lock;
	int			fd;
	struct sockaddr_storage addr;
	socklen_t               len;
};

/* Peer host socket address */
static struct peer peer_h;

static bool has_peer(void) {
	bool has_peer;

 	lock_acquire(&peer_h.lock, __func__);
	has_peer = peer_h.len;
	lock_release(&peer_h.lock, __func__);

	return has_peer;
}

static void log_peer(void) {
	if (has_peer()) {
		struct sockaddr_storage addr;
		socklen_t len;
		char host[NI_MAXHOST];
		char service[NI_MAXSERV];
		int s;

	 	lock_acquire(&peer_h.lock, __func__);
		addr = peer_h.addr;
		len = peer_h.len;
		lock_release(&peer_h.lock, __func__);

		s = getnameinfo((struct sockaddr *) &addr, len, host,
			NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV);
		if (0 == s)
			elog_err("Peer %s:%s\n", host, service);
		else
			elog_err("getnameinfo: %s\n", gai_strerror(s));
	} else
		elog_err("No peer address\n");
}

static void read_commands(int fd) {
	char                    buf[1024];
	struct sockaddr_storage addr;
	socklen_t               len;

	len = sizeof(struct sockaddr_storage);
	ssize_t n = recvfrom(fd, buf, sizeof(buf), 0,
		(struct sockaddr *) &addr, &len);
 	lock_acquire(&peer_h.lock, __func__);
	peer_h.addr = addr;
	peer_h.len = len;
	lock_release(&peer_h.lock, __func__);
	if (n >= 0)
		process_commands(nstr_make(buf, sizeof(buf), n));
	else {
		elog_err("Read socket: %s\n", strerror(errno));
		log_peer();
	}
}

static void *command_thread(void *data) {
	int fd;

	fd = open_bind("7001");
	if (fd > 0) {
	 	lock_acquire(&peer_h.lock, __func__);
		peer_h.fd = fd;
		lock_release(&peer_h.lock, __func__);
		connect_peer(fd, PEER);
		while (true) {
			read_commands(fd);
		}
		close(fd);
	}
	return NULL;
}

static void send_status(nstr_t str) {
	struct sockaddr_storage addr;
	socklen_t               len;
	int			fd;

 	lock_acquire(&peer_h.lock, __func__);
	addr = peer_h.addr;
	len = peer_h.len;
	fd = peer_h.fd;
	lock_release(&peer_h.lock, __func__);

	ssize_t n = sendto(fd, str.buf, str.len, 0,
		(struct sockaddr *) &addr, len);
	if (n < 0) {
		elog_err("Send socket: %s\n", strerror(errno));
		log_peer();
	}
}

static void *status_thread(void *data) {
	char buf[256];
	while (true) {
		if (has_peer()) {
			nstr_t str = nstr_make(buf, sizeof(buf), 0);
			str = mongrid_status(str);
			send_status(str);
		}
		sleep(1);
	}
	return NULL;
}

static uint32_t load_config(void) {
	char buf[128];
	nstr_t str = config_load("config", nstr_make(buf, sizeof(buf), 0));
	if (nstr_len(str)) {
		nstr_t cmd = nstr_chop(str, RECORD_SEP);
		nstr_t p1 = nstr_split(&cmd, UNIT_SEP);
		if (nstr_cmp_z(p1, "config")) {
			nstr_t p2 = nstr_split(&cmd, UNIT_SEP);
			int m = nstr_parse_u32(p2);
			if (m > 0)
				return m;
		} else
			elog_err("Invalid command: %s\n", nstr_z(str));
	}
	return 1;
}

static bool create_thread(void *(func)(void *)) {
	pthread_t thread;
	int rc = pthread_create(&thread, NULL, func, NULL);
	if (rc)
		elog_err("pthread_create: %d\n", strerror(rc));
	return !rc;
}

void run_player(bool gui, bool stats) {
	mongrid_create(gui, stats);
	config_init();
	lock_init(&peer_h.lock);
	peer_h.len = 0;
	if (!create_thread(command_thread))
		goto fail;
	if (!create_thread(status_thread))
		goto fail;
	while (true) {
		uint32_t mon = load_config();
		if (mongrid_init(mon))
			break;
		load_commands(mon);
		mongrid_run();
		mongrid_clear();
	}
fail:
	lock_destroy(&peer_h.lock);
	config_destroy();
	mongrid_destroy();
}
