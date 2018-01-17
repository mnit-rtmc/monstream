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

struct player {
	struct cxn *cxn;
	const char *port;
};

/* ASCII separators */
static const char RECORD_SEP = '\x1E';
static const char UNIT_SEP = '\x1F';

/* Default values */
static const uint32_t DEFAULT_LATENCY = 50;
static const uint32_t DEFAULT_FONT_SZ = 32;

static uint32_t parse_latency(nstr_t lat) {
	int l = nstr_parse_u32(lat);
	return (l > 0) ? l : DEFAULT_LATENCY;
}

static uint32_t parse_font_sz(nstr_t fsz) {
	int s = nstr_parse_u32(fsz);
	return (s > 0) ? s : DEFAULT_FONT_SZ;
}

static void proc_play(nstr_t cmd, bool store) {
	nstr_t str = nstr_dup(cmd);
	nstr_t play     = nstr_split(&str, UNIT_SEP);   // "play"
	nstr_t mdx      = nstr_split(&str, UNIT_SEP);   // mon index
	nstr_t cam_id   = nstr_split(&str, UNIT_SEP);   // camera ID
	nstr_t loc      = nstr_split(&str, UNIT_SEP);   // stream URI
	nstr_t encoding = nstr_split(&str, UNIT_SEP);   // encoding
	nstr_t desc     = nstr_split(&str, UNIT_SEP);   // title
	nstr_t lat      = nstr_split(&str, UNIT_SEP);   // latency
	assert(nstr_cmp_z(play, "play"));
	int mon = nstr_parse_u32(mdx);
	if (mon >= 0) {
		uint32_t latency = parse_latency(lat);
		elog_cmd(cmd);
		mongrid_play_stream(mon, cam_id, loc, desc, encoding, latency);
		if (store) {
			char fname[16];
			sprintf(fname, "play.%d", mon);
			config_store(fname, cmd);
		}
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static void proc_monitor(nstr_t cmd, bool store) {
	nstr_t str = nstr_dup(cmd);
	nstr_t monitor = nstr_split(&str, UNIT_SEP);    // "monitor"
	nstr_t mdx     = nstr_split(&str, UNIT_SEP);    // mon index
	nstr_t mid     = nstr_split(&str, UNIT_SEP);    // monitor ID
	nstr_t acc     = nstr_split(&str, UNIT_SEP);    // accent color
	nstr_t asp     = nstr_split(&str, UNIT_SEP);    // force-aspect-ratio
	nstr_t sz      = nstr_split(&str, UNIT_SEP);    // font size
	nstr_t crop    = nstr_split(&str, UNIT_SEP);    // crop code
	nstr_t hg      = nstr_split(&str, UNIT_SEP);    // horizontal gap
	nstr_t vg      = nstr_split(&str, UNIT_SEP);    // vertical gap
	assert(nstr_cmp_z(monitor, "monitor"));
	int mon = nstr_parse_u32(mdx);
	if (mon >= 0) {
		int32_t accent = nstr_parse_hex(acc);
		int aspect = nstr_parse_u32(asp);
		uint32_t font_sz = parse_font_sz(sz);
		uint32_t hgap = nstr_parse_u32(hg);
		uint32_t vgap = nstr_parse_u32(vg);
		elog_cmd(cmd);
		mongrid_set_mon(mon, mid, accent, aspect, font_sz, crop, hgap,
			vgap);
		if (store) {
			char fname[16];
			sprintf(fname, "monitor.%d", mon);
			config_store(fname, cmd);
		}
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static void proc_config(nstr_t cmd) {
	elog_cmd(cmd);
	config_store("config", cmd);
	mongrid_restart();
}

static void proc_cmd(nstr_t cmd, bool store) {
	nstr_t p1 = nstr_chop(cmd, UNIT_SEP);
	if (nstr_cmp_z(p1, "play"))
		proc_play(cmd, store);
	else if (nstr_cmp_z(p1, "monitor"))
		proc_monitor(cmd, store);
	else if (nstr_cmp_z(p1, "config"))
		proc_config(cmd);
	else
		elog_err("Invalid command: %s\n", nstr_z(cmd));
}

static void proc_cmds(nstr_t str, bool store) {
	while (nstr_len(str)) {
		proc_cmd(nstr_split(&str, RECORD_SEP), store);
	}
}

static void player_read_cmds(struct player *player) {
	char buf[1024];
	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	proc_cmds(cxn_recv(player->cxn, str), true);
}

static void *cmd_thread(void *arg) {
	struct player *player = arg;

	cxn_bind(player->cxn, player->port);
	while (true) {
		player_read_cmds(player);
	}
	return NULL;
}

static void player_send_status(struct player *player) {
	char buf[256];

	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	str = mongrid_status(str);
	cxn_send(player->cxn, str);
}

static void *status_thread(void *arg) {
	struct player *player = arg;

	while (true) {
		if (cxn_established(player->cxn))
			player_send_status(player);
		sleep(1);
	}
	return NULL;
}

static bool player_create_thread(struct player *player, void *(func)(void *)) {
	pthread_t thread;
	int rc = pthread_create(&thread, NULL, func, player);
	if (rc)
		elog_err("pthread_create: %d\n", strerror(rc));
	return !rc;
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

static void load_cmd(const char *fname) {
	char buf[128];
	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	proc_cmds(config_load(fname, str), false);
}

static void load_cmds(uint32_t mon) {
	int i;
	for (i = 0; i < mon; i++) {
		char fname[16];
		sprintf(fname, "monitor.%d", i);
		load_cmd(fname);
		sprintf(fname, "play.%d", i);
		load_cmd(fname);
	}
}

void run_player(bool gui, bool stats, const char *port) {
	struct player player;

	memset(&player, 0, sizeof(struct player));
	player.port = port;
	config_init();
	player.cxn = cxn_create();
	mongrid_create(gui, stats);
	if (!player_create_thread(&player, cmd_thread))
		goto fail;
	if (!player_create_thread(&player, status_thread))
		goto fail;
	while (true) {
		uint32_t mon = load_config();
		if (mongrid_init(mon))
			break;
		load_cmds(mon);
		mongrid_run();
		mongrid_reset();
	}
fail:
	mongrid_destroy();
	cxn_destroy(player.cxn);
	config_destroy();
}
