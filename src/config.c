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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "nstr.h"

static const char *PATH = "/var/lib/monstream/%s";

nstr_t config_load(const char *name, nstr_t str) {
	char path[64];
	int fd;

	if (snprintf(path, sizeof(path), PATH, name) < 0) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		goto err;
	}
	fd = open(path, O_RDONLY | O_NOFOLLOW, 0);
	if (fd >= 0) {
		ssize_t n_bytes = read(fd, str.buf, str.buf_len);
		if (n_bytes < 0) {
			fprintf(stderr, "Read %s: %s\n", path, strerror(errno));
			goto err;
		}
		str.len = n_bytes;
		close(fd);
		return str;
	} else {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		goto err;
	}
err:
	str.len = 0;
	return str;
}

ssize_t config_store(const char *name, nstr_t cmd) {
	char path[64];
	int fd;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	if (snprintf(path, sizeof(path), PATH, name) < 0) {
		fprintf(stderr, "Error: %s\n", strerror(errno));
		return -1;
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd >= 0) {
		ssize_t n_bytes = write(fd, cmd.buf, cmd.len);
		if (n_bytes < 0) {
			fprintf(stderr, "Write %s: %s\n", path,strerror(errno));
			return -1;
		}
		close(fd);
		return n_bytes;
	} else {
		fprintf(stderr, "Open %s: %s\n", path, strerror(errno));
		return -1;
	}
}
