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
#include <gtk/gtk.h>
#include <netdb.h>		/* for socket stuff */
#define _MULTI_THREADED
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>		/* for memset */
#include "nstr.h"

void mongrid_set_id(uint32_t idx, const char *mid, uint32_t accent);
int32_t mongrid_play_stream(uint32_t idx, const char *loc, const char *desc,
	const char *stype);

nstr_t config_load(const char *name, nstr_t str);
ssize_t config_store(const char *name, nstr_t cmd);

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

static void process_play(nstr_t cmd) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, '\x1F');	// "play"
	nstr_t p2 = nstr_split(&str, '\x1F');	// mon index
	nstr_t p3 = nstr_split(&str, '\x1F');	// camera ID
	nstr_t p4 = nstr_split(&str, '\x1F');	// stream URI
	nstr_t p5 = nstr_split(&str, '\x1F');	// stream type
	nstr_t p6 = nstr_split(&str, '\x1F');	// title
	assert(nstr_cmp_z(p1, "play"));
	int mon = nstr_parse_u32(p2);
	if (mon >= 0) {
		char desc[128];
		char uri[128];
		char stype[16];
		char fname[16];
		nstr_t d = nstr_make_cpy(desc, sizeof(desc), 0, p3);
		if (nstr_len(p6)) {
			nstr_cat_z(&d, " --- ");
			nstr_cat(&d, p6);
		}
		nstr_z(d);
		nstr_wrap(uri, sizeof(uri), p4);
		nstr_wrap(stype, sizeof(stype), p5);
		mongrid_play_stream(mon, uri, desc, stype);
		sprintf(fname, "play.%d", mon);
		config_store(fname, cmd);
	} else
		fprintf(stderr, "Invalid monitor: %s\n", nstr_z(cmd));
}

static void process_monitor(nstr_t cmd) {
	nstr_t str = nstr_dup(cmd);
	nstr_t p1 = nstr_split(&str, '\x1F');	// "monitor"
	nstr_t p2 = nstr_split(&str, '\x1F');	// mon index
	nstr_t p3 = nstr_split(&str, '\x1F');	// monitor ID
	nstr_t p4 = nstr_split(&str, '\x1F');	// accent color
	assert(nstr_cmp_z(p1, "monitor"));
	int mon = nstr_parse_u32(p2);
	if (mon >= 0) {
		char mid[8];
		char fname[16];
		nstr_wrap(mid, sizeof(mid), p3);
		int32_t acc = nstr_parse_hex(p4);
		mongrid_set_id(mon, mid, acc);
		sprintf(fname, "monitor.%d", mon);
		config_store(fname, cmd);
	} else
		fprintf(stderr, "Invalid monitor: %s\n", nstr_z(cmd));
}

static void process_config(nstr_t cmd) {
	config_store("config", cmd);
	exit(0);
}

static void process_command(nstr_t cmd) {
	nstr_t p1 = nstr_chop(cmd, '\x1F');
	if (nstr_cmp_z(p1, "play"))
		process_play(cmd);
	else if (nstr_cmp_z(p1, "monitor"))
		process_monitor(cmd);
	else if (nstr_cmp_z(p1, "config"))
		process_config(cmd);
	else
		fprintf(stderr, "Invalid command: %s\n", nstr_z(cmd));
}

static void process_commands(nstr_t str) {
	while (nstr_len(str)) {
		process_command(nstr_split(&str, '\x1E'));
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
		process_commands(nstr_make(buf, 1024, n));
	}
	close(fd);
out:
	return NULL;
}

uint32_t load_config(void) {
	char buf[128];
	nstr_t str = config_load("config", nstr_make(buf, sizeof(buf), 0));
	if (nstr_len(str)) {
		nstr_t cmd = nstr_chop(str, '\x1E');
		nstr_t p1 = nstr_split(&cmd, '\x1F');
		if (nstr_cmp_z(p1, "config")) {
			nstr_t p2 = nstr_split(&cmd, '\x1F');
			int m = nstr_parse_u32(p2);
			if (m > 0)
				return m;
		} else
			fprintf(stderr, "Invalid command: %s\n", nstr_z(str));
	}
	return 1;
}
