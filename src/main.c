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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include "config.h"
#include "nstr.h"

#define VERSION "1.13"
#define BANNER "monstream: v" VERSION "  Copyright (C) 2017-2023  MnDOT\n"

static char *SINK_VAAPI = "sink\x1FVAAPI\x1E";
static char *SINK_XVIMAGE = "sink\x1FXVIMAGE\x1E";

void run_player(bool gui, bool stats, const char *port);

int main(int argc, char* argv[]) {
	int i;
	bool gui = true;
	bool stats = false;
	const char *port = "7001";
	char buf[64];
	nstr_t str;

	printf(BANNER);
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-gui") == 0)
			gui = false;
		else if (strcmp(argv[i], "--sink") == 0) {
			i++;
			str = nstr_init(buf, sizeof(buf));
			if (strcmp(argv[i], "VAAPI") == 0)
				nstr_cat_z(&str, SINK_VAAPI);
			else if (strcmp(argv[i], "XVIMAGE") == 0)
				nstr_cat_z(&str, SINK_XVIMAGE);
			else {
				fprintf(stderr, "Invalid sink: %s\n", argv[i]);
				goto out;
			}
			config_store("sink", str);
		} else if (strcmp(argv[i], "--port") == 0) {
			i++;
			port = argv[i];
		} else if (strcmp(argv[i], "--stats") == 0)
			stats = true;
		else if (strcmp(argv[i], "--test") == 0) {
			config_test();
			goto out;
		} else if (strcmp(argv[i], "--version") == 0)
			goto out;
		else {
			if (strcmp(argv[i], "--help") != 0) {
				fprintf(stderr, "Invalid option: %s\n",
					argv[i]);
			}
			goto help;
		}
	}
	curl_global_init(CURL_GLOBAL_ALL);
	run_player(gui, stats, port);
	curl_global_cleanup();
out:
	return 0;
help:
	printf("Usage: %s [option]\n", argv[0]);
	printf("  --version       Display version and exit\n");
	printf("  --no-gui        Run headless (still connect to streams)\n");
	printf("  --stats         Display statistics on stream errors\n");
	printf("  --port [p]      Listen on given UDP port (default 7001)\n");
	printf("  --sink VAAPI    Configure VA-API video acceleration\n");
	printf("  --sink XVIMAGE  Configure xvimage sink (no acceleration)\n");
	return 1;
}
