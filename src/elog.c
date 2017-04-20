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

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include "nstr.h"

static void elog_now(void) {
	char buf[32];

	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	strftime(buf, sizeof(buf), "%a, %d %b %Y %T %z", tm);
	fprintf(stderr, "%s ", buf);
}

void elog_err(const char *format, ...) {
	va_list va;

	elog_now();
	va_start(va, format);
	vfprintf(stderr, format, va);
	va_end(va);
}

void elog_cmd(nstr_t cmd) {
	elog_now();
	fprintf(stderr, "cmd: ");
	fwrite(cmd.buf, cmd.len, 1, stderr);
	fprintf(stderr, "\n");
}
