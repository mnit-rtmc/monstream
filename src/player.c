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
#define _MULTI_THREADED
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>		/* strerror */
#include <unistd.h>		/* sleep */
#include "elog.h"
#include "nstr.h"
#include "config.h"
#include "mongrid.h"
#include "cxn.h"

static struct cxn *cxn;

/* ASCII separators */
static const char RECORD_SEP = '\x1E';
static const char UNIT_SEP = '\x1F';

#define DEFAULT_LATENCY	(50)
#define DEFAULT_FONT_SZ (32)

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
	nstr_t p7 = nstr_split(&str, UNIT_SEP); // crop code
	nstr_t p8 = nstr_split(&str, UNIT_SEP); // horizontal gap
	nstr_t p9 = nstr_split(&str, UNIT_SEP); // vertical gap
	assert(nstr_cmp_z(p1, "monitor"));
	int mon = nstr_parse_u32(p2);
	if (mon >= 0) {
		char mid[8];
		int32_t accent;
		char fname[16];
		int aspect;
		uint32_t font_sz;
		char crop[6];
		uint32_t hgap;
		uint32_t vgap;
		nstr_wrap(mid, sizeof(mid), p3);
		accent = nstr_parse_hex(p4);
		aspect = nstr_parse_u32(p5);
		font_sz = parse_font_sz(p6);
		nstr_wrap(crop, sizeof(crop), p7);
		hgap = nstr_parse_u32(p8);
		vgap = nstr_parse_u32(p9);
		elog_cmd(cmd);
		mongrid_set_mon(mon, mid, accent, aspect, font_sz, crop, hgap,
			vgap);
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

static void read_commands(void) {
	char buf[1024];

	process_commands(cxn_recv(cxn, nstr_make(buf, sizeof(buf), 0)));
}

static void *command_thread(void *arg) {
	const char *port = arg;
	cxn_bind(cxn, port);
	while (true) {
		read_commands();
	}
	return NULL;
}

static void *status_thread(void *data) {
	char buf[256];
	while (true) {
		if (cxn_exists(cxn)) {
			nstr_t str = nstr_make(buf, sizeof(buf), 0);
			str = mongrid_status(str);
			cxn_send(cxn, str);
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

static bool create_thread(void *(func)(void *), void *arg) {
	pthread_t thread;
	int rc = pthread_create(&thread, NULL, func, arg);
	if (rc)
		elog_err("pthread_create: %d\n", strerror(rc));
	return !rc;
}

void run_player(bool gui, bool stats, const char *port) {
	config_init();
	mongrid_create(gui, stats);
	cxn = cxn_create();
	if (!create_thread(command_thread, (void *) port))
		goto fail;
	if (!create_thread(status_thread, NULL))
		goto fail;
	while (true) {
		uint32_t mon = load_config();
		if (mongrid_init(mon))
			break;
		load_commands(mon);
		mongrid_run();
		mongrid_reset();
	}
fail:
	cxn_destroy(cxn);
	mongrid_destroy();
	config_destroy();
}
