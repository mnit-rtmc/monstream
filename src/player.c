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
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>		/* strerror */
#include <sys/stat.h>
#include <time.h>		/* nanosleep */
#include <unistd.h>
#include "elog.h"
#include "nstr.h"
#include "config.h"
#include "mongrid.h"
#include "cxn.h"

struct player {
	struct cxn *cxn;
	const char *port;
	pthread_t  cmd_tid;
	pthread_t  stat_tid;
	pthread_t  joy_tid;
	bool       configuring; // does this need a mutex?
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

static void player_display(struct player *plyr, nstr_t cmd) {
	nstr_t str     = nstr_dup(cmd);
	nstr_t display = nstr_split(&str, UNIT_SEP);    // "display"
	nstr_t mid     = nstr_split(&str, UNIT_SEP);    // monitor ID
	nstr_t cam     = nstr_split(&str, UNIT_SEP);    // camera ID
	nstr_t seq     = nstr_split(&str, UNIT_SEP);    // sequence ID
	assert(nstr_cmp_z(display, "display"));
	elog_cmd(cmd);
	mongrid_display(mid, cam, seq);
}

static void player_play(struct player *plyr, nstr_t cmd, bool store) {
	nstr_t str      = nstr_dup(cmd);
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
		elog_cmd(cmd);
		if (!plyr->configuring) {
			uint32_t latency = parse_latency(lat);
			mongrid_play_stream(mon, cam_id, loc, desc, encoding,
				latency);
		}
		if (store) {
			char fname[16];
			sprintf(fname, "play.%d", mon);
			config_store(fname, cmd);
		}
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static void player_monitor(struct player *plyr, nstr_t cmd, bool store) {
	nstr_t str     = nstr_dup(cmd);
	nstr_t monitor = nstr_split(&str, UNIT_SEP);    // "monitor"
	nstr_t mdx     = nstr_split(&str, UNIT_SEP);    // mon index
	nstr_t mid     = nstr_split(&str, UNIT_SEP);    // monitor ID
	nstr_t acc     = nstr_split(&str, UNIT_SEP);    // accent color
	nstr_t asp     = nstr_split(&str, UNIT_SEP);    // force-aspect-ratio
	nstr_t sz      = nstr_split(&str, UNIT_SEP);    // font size
	nstr_t crop    = nstr_split(&str, UNIT_SEP);    // crop code
	nstr_t hg      = nstr_split(&str, UNIT_SEP);    // horizontal gap
	nstr_t vg      = nstr_split(&str, UNIT_SEP);    // vertical gap
	nstr_t extra   = nstr_split(&str, UNIT_SEP);    // extra mon
	assert(nstr_cmp_z(monitor, "monitor"));
	int mon = nstr_parse_u32(mdx);
	if (mon >= 0) {
		elog_cmd(cmd);
		if (!plyr->configuring) {
			int32_t accent = nstr_parse_hex(acc);
			int aspect = nstr_parse_u32(asp);
			uint32_t font_sz = parse_font_sz(sz);
			uint32_t hgap = nstr_parse_u32(hg);
			uint32_t vgap = nstr_parse_u32(vg);
			mongrid_set_mon(mon, mid, accent, aspect, font_sz, crop,
				hgap, vgap, extra);
		}
		if (store) {
			char fname[16];
			sprintf(fname, "monitor.%d", mon);
			config_store(fname, cmd);
		}
	} else
		elog_err("Invalid monitor: %s\n", nstr_z(cmd));
}

static void player_config(struct player *plyr, nstr_t cmd) {
	nstr_t str     = nstr_dup(cmd);
	nstr_t config  = nstr_split(&str, UNIT_SEP);	// "config"
	nstr_t mdx     = nstr_split(&str, UNIT_SEP);    // mon index
	assert(nstr_cmp_z(config, "config"));
	int mon = nstr_parse_u32(mdx);
	if (mon >= 0) {
		elog_cmd(cmd);
		if (mon > 0) {
			config_store("config", cmd);
			plyr->configuring = false;
			mongrid_restart();
		} else
			plyr->configuring = true;
	} else
		elog_err("Invalid config: %s\n", nstr_z(cmd));
}

static void player_proc_cmd(struct player *plyr, nstr_t cmd, bool store) {
	nstr_t p1 = nstr_chop(cmd, UNIT_SEP);
	if (nstr_cmp_z(p1, "display"))
		player_display(plyr, cmd);
	else if (nstr_cmp_z(p1, "play"))
		player_play(plyr, cmd, store);
	else if (nstr_cmp_z(p1, "monitor"))
		player_monitor(plyr, cmd, store);
	else if (nstr_cmp_z(p1, "config"))
		player_config(plyr, cmd);
	else
		elog_err("Invalid command: %s\n", nstr_z(cmd));
}

static void player_proc_cmds(struct player *plyr, nstr_t str, bool store) {
	while (nstr_len(str)) {
		player_proc_cmd(plyr, nstr_split(&str, RECORD_SEP), store);
	}
}

static void player_read_cmds(struct player *plyr) {
	char buf[1024];
	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	player_proc_cmds(plyr, cxn_recv(plyr->cxn, str), true);
}

static void *cmd_thread(void *arg) {
	struct player *plyr = arg;

	cxn_bind(plyr->cxn, plyr->port);
	while (true) {
		player_read_cmds(plyr);
	}
	return NULL;
}

static void player_send_status(struct player *plyr) {
	char buf[256];

	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	str = mongrid_status(str);
	cxn_send(plyr->cxn, str);
}

static void sleep_for(time_t sec, long nano) {
	struct timespec ts;
	ts.tv_sec = sec;
	ts.tv_nsec = nano;
	nanosleep(&ts, NULL);
}

static void *status_thread(void *arg) {
	struct player *plyr = arg;

	while (true) {
		bool online = cxn_established(plyr->cxn);
		mongrid_set_online(online);
		if (online) {
			player_send_status(plyr);
			if (mongrid_mon_selected())
				sleep_for(0, 333333333);
			else
				sleep_for(1, 0);
		} else
			sleep_for(2, 0);
	}
	return NULL;
}

const char *JOY_PATH = "/dev/input/js0";

static void process_joystick(void) {
	int fd = open(JOY_PATH, O_RDONLY | O_NOFOLLOW, 0);
	if (fd > 0) {
		while (mongrid_joy_event(fd)) {
			// keep going
		}
		if (close(fd) < 0)
			elog_err("Close %s: %s\n", JOY_PATH, strerror(errno));
	} else if (errno != ENOENT)
		elog_err("Open %s: %s\n", JOY_PATH, strerror(errno));
}

static void *joy_thread(void *arg) {
	while (true) {
		process_joystick();
		sleep_for(1, 0);
	}
	return NULL;
}

static pthread_t player_create_thread(struct player *plyr,
	void *(func)(void *))
{
	pthread_t tid;
	int rc = pthread_create(&tid, NULL, func, plyr);
	if (rc) {
		elog_err("pthread_create: %s\n", strerror(rc));
		return 0;
	} else
		return tid;
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

static void player_load_cmd(struct player *plyr, const char *fname) {
	char buf[128];
	nstr_t str = nstr_make(buf, sizeof(buf), 0);
	player_proc_cmds(plyr, config_load(fname, str), false);
}

static void player_load_cmds(struct player *plyr, uint32_t mon) {
	int i;
	for (i = 0; i < mon; i++) {
		char fname[16];
		sprintf(fname, "monitor.%d", i);
		player_load_cmd(plyr, fname);
		sprintf(fname, "play.%d", i);
		player_load_cmd(plyr, fname);
	}
}

static void player_sig_handler(int n_sig) {
	// just ignore
}

static void player_install_handler(struct player *plyr) {
	struct sigaction sa;
	sa.sa_handler = player_sig_handler;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR1, &sa, NULL);
}

void run_player(bool gui, bool stats, const char *port) {
	struct player plyr;

	memset(&plyr, 0, sizeof(struct player));
	plyr.port = port;
	config_init();
	plyr.cxn = cxn_create();
	mongrid_create(gui, stats);
	plyr.cmd_tid = player_create_thread(&plyr, cmd_thread);
	plyr.stat_tid = player_create_thread(&plyr, status_thread);
	plyr.joy_tid = player_create_thread(&plyr, joy_thread);
	plyr.configuring = false;
	player_install_handler(&plyr);
	while (plyr.cmd_tid && plyr.stat_tid && plyr.joy_tid) {
		uint32_t mon = load_config();
		if (mongrid_init(mon, plyr.stat_tid))
			break;
		player_load_cmds(&plyr, mon);
		mongrid_run();
		mongrid_reset();
	}
	mongrid_destroy();
	cxn_destroy(plyr.cxn);
	config_destroy();
}
