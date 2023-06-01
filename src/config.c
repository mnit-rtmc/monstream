/*
 * Copyright (C) 2017-2023  Minnesota Department of Transportation
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "elog.h"
#include "nstr.h"
#include "lock.h"

#define PATH_LEN	(128)
static const char *PATH = "/var/lib/monstream/%s";
static const char *CACHE = "cache/%016lx";

/* Lock to protect config files */
static struct lock _lock;

void config_init(void) {
	char path[PATH_LEN];
	int rc;
	mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	lock_init(&_lock);

	if (snprintf(path, sizeof(path), PATH, "cache") < 0) {
		elog_err("Error: %s\n", strerror(errno));
		return;
	}
	rc = mkdir(path, mode);
	if (rc < 0 && errno != EEXIST) {
		elog_err("mkdir %s: %s\n", path, strerror(errno));
	}
}

void config_destroy(void) {
	lock_destroy(&_lock);
}

nstr_t config_load(const char *name, nstr_t str) {
	char path[PATH_LEN];
	int fd;

	lock_acquire(&_lock, __func__);
	if (snprintf(path, sizeof(path), PATH, name) < 0) {
		elog_err("Error: %s\n", strerror(errno));
		goto err;
	}
	fd = open(path, O_RDONLY | O_NOFOLLOW, 0);
	if (fd >= 0) {
		ssize_t n_bytes = read(fd, str.buf, str.buf_len);
		if (n_bytes < 0) {
			elog_err("Read %s: %s\n", path, strerror(errno));
			goto err;
		}
		str.len = n_bytes;
		close(fd);
		goto out;
	} else {
		elog_err("Open %s: %s\n", path, strerror(errno));
		goto err;
	}
out:
	lock_release(&_lock, __func__);
	return str;
err:
	lock_release(&_lock, __func__);
	str.len = 0;
	return str;
}

nstr_t config_load_cache(uint64_t hash, nstr_t str) {
	char path[PATH_LEN];

	if (snprintf(path, sizeof(path), CACHE, hash) < 0) {
		elog_err("Error: %s\n", strerror(errno));
		goto err;
	}
	return config_load(path, str);
err:
	str.len = 0;
	return str;
}

ssize_t config_store(const char *name, nstr_t str) {
	char path[PATH_LEN];
	int fd;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	lock_acquire(&_lock, __func__);
	if (snprintf(path, sizeof(path), PATH, name) < 0) {
		elog_err("Error: %s\n", strerror(errno));
		goto err;
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd >= 0) {
		ssize_t n_bytes = write(fd, str.buf, str.len);
		if (n_bytes < 0) {
			elog_err("Write %s: %s\n", path,strerror(errno));
			goto err;
		}
		close(fd);
		lock_release(&_lock, __func__);
		return n_bytes;
	} else {
		elog_err("Open %s: %s\n", path, strerror(errno));
		goto err;
	}
err:
	lock_release(&_lock, __func__);
	return -1;
}

ssize_t config_store_cache(uint64_t hash, nstr_t str) {
	char path[PATH_LEN];

	if (snprintf(path, sizeof(path), CACHE, hash) < 0) {
		elog_err("Error: %s\n", strerror(errno));
		goto err;
	}
	return config_store(path, str);
err:
	return -1;
}

/* Test config */
static char *CONFIG = "config" "\x1F"
                      "1" "\x1E";

/* Test monitor */
static char *MONITOR = "monitor" "\x1F"
                       "0" "\x1F"
                       "TEST" "\x1F"
                       "FF44FF" "\x1F"
                       "1" "\x1F"
                       "20" "\x1F"
                       "AAAA" "\x1F"
                       "0" "\x1F"
                       "0" "\x1F"
                       "\x1F" "\x1E";

/* Test play */
static char *PLAY = "play" "\x1F"
                    "0" "\x1F"
                    "1" "\x1F"
                    "rtsp://127.0.0.1:8554/stream" "\x1F"
                    "H264" "\x1F"
                    "Test Video" "\x1F"
                    "500" "\x1E";

void config_test() {
	int n = strlen(CONFIG);
	nstr_t config = nstr_init_n(CONFIG, n, n);
	config_store("config", config);
	n = strlen(MONITOR);
	nstr_t monitor = nstr_init_n(MONITOR, n, n);
	config_store("monitor.0", monitor);
	n = strlen(PLAY);
	nstr_t play = nstr_init_n(PLAY, n, n);
	config_store("play.0", play);
}
