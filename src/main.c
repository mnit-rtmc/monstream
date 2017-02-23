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

#include <gtk/gtk.h>
#define _MULTI_THREADED
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t load_config(void);
void *command_thread(void *data);
int32_t mongrid_init(uint32_t num);

int main(void) {
	pthread_t thread;
	uint32_t mon = load_config();
	int rc;

	if (mongrid_init(mon))
		return -1;
	rc = pthread_create(&thread, NULL, command_thread, &mon);
	if (rc) {
		fprintf(stderr, "pthread_create error: %d\n", rc);
		return 1;
	}
	gtk_main();

	return 0;
}
