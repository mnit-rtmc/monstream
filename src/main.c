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
#include <stdbool.h>

#define VERSION "0.16"
#define BANNER "monstream: v" VERSION "  Copyright (C)  MnDOT\n"

bool run_player(void);

int main(void) {
	printf(BANNER);
	gst_init(NULL, NULL);
	gtk_init(NULL, NULL);
	while (run_player()) { }
	return 1;
}
