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

#include <gst/gst.h>
#include <gtk/gtk.h>
#define _MULTI_THREADED
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "elog.h"

#define VERSION "0.16"
#define BANNER "monstream: v" VERSION "  Copyright (C)  MnDOT\n"

uint32_t load_config(void);
void *command_thread(void *data);
int32_t mongrid_init(uint32_t num);
void mongrid_destroy(void);

static bool do_main(void) {
	pthread_t thread;
	uint32_t mon;
	int rc;

	mon = load_config();
	if (mongrid_init(mon))
		return false;
	rc = pthread_create(&thread, NULL, command_thread, &mon);
	if (rc) {
		elog_err("pthread_create: %d\n", rc);
		goto fail;
	}
	gtk_main();
	mongrid_destroy();
	return true;
fail:
	mongrid_destroy();
	return false;
}

int main(void) {
	printf(BANNER);
	gst_init(NULL, NULL);
	gtk_init(NULL, NULL);
	while (do_main()) { }
	return 1;
}
