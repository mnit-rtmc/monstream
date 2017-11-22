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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#define VERSION "0.28"
#define BANNER "monstream: v" VERSION "  Copyright (C) 2017  MnDOT\n"

void run_player(bool gui, bool stats);

int main(int argc, char* argv[]) {
	int i;
	bool gui = true;
	bool stats = false;

	printf(BANNER);
	curl_global_init(CURL_GLOBAL_ALL);
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-gui") == 0)
			gui = false;
		else if (strcmp(argv[i], "--stats") == 0)
			stats = true;
		else
			printf("Invalid option: %s\n", argv[i]);
	}
	run_player(gui, stats);
	curl_global_cleanup();
	return 1;
}
