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
#include <string.h>
#include "elog.h"
#include "lock.h"

void lock_init(struct lock *l) {
	int rc = pthread_mutex_init(&l->mutex, NULL);
	if (rc)
		elog_err("lock_init: %s\n", strerror(rc));
}

void lock_destroy(struct lock *l) {
	int rc = pthread_mutex_destroy(&l->mutex);
	if (rc)
		elog_err("lock_destroy: %s\n", strerror(rc));
}

void lock_acquire(struct lock *l) {
	int rc = pthread_mutex_lock(&l->mutex);
	if (rc)
		elog_err("config_lock: %s\n", strerror(rc));
}

void lock_release(struct lock *l) {
	int rc = pthread_mutex_unlock(&l->mutex);
	if (rc)
		elog_err("config_unlock: %s\n", strerror(rc));
}
