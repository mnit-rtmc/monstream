/*
 * Copyright (C) 2018  Minnesota Department of Transportation
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

#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <gst/sdp/sdp.h>
#include "elog.h"
#include "config.h"
#include "sdp.h"

/*
 * This sdp module is required because the sdpdemux element has multiple bugs.
 * Also, the sdp files are cached in the /var/lib/monstream/cache directory.
 * The cache file names are a hash of the sdp URL.
 *
 * Most importantly, caching sdp files allows the stream to be started quickly.
 * It also prevents problems with Axis encoders, when many monstream clients try
 * to fetch the sdp file at once (unintentional denial-of-service attack).
 *
 * After an sdp stream is started, the sdp file is fetched again, to check for
 * changes since the cached version was stored.
 */

static const int64_t TIMEOUT_SEC = 5L;

static bool sdp_data_check(nstr_t str) {
	return nstr_starts_with(str, "http://")
	    && nstr_contains(str, ".sdp");
}

void sdp_data_init(struct sdp_data *sdp, nstr_t loc) {
	sdp->is_sdp = sdp_data_check(loc);
	sdp->loc_hash = nstr_hash_fnv(loc);
	sdp->cache = nstr_init(sdp->cache_buf, sizeof(sdp->cache_buf));
	sdp->fetch = nstr_init(sdp->fetch_buf, sizeof(sdp->fetch_buf));
	sdp->loc = nstr_make_cpy(sdp->loc_buf, sizeof(sdp->loc_buf), loc);
	sdp->udp = nstr_init(sdp->udp_buf, sizeof(sdp->udp_buf));
	sdp->sprops = nstr_init(sdp->sprop_buf, sizeof(sdp->sprop_buf));
}

static bool sdp_data_parse(struct sdp_data *sdp, nstr_t str) {
	nstr_t udp = nstr_init(sdp->udp_buf, sizeof(sdp->udp_buf));
	nstr_t sprops = nstr_init(sdp->sprop_buf, sizeof(sdp->sprop_buf));
	GstSDPMessage msg;
	memset(&msg, 0, sizeof(msg));
	if (gst_sdp_message_init(&msg) != GST_SDP_OK) {
		elog_err("gst_sdp_message_init error\n");
		return false;
	}
	if (gst_sdp_message_parse_buffer((const guint8 *) str.buf,
		str.len, &msg) != GST_SDP_OK)
	{
		elog_err("gst_sdp_message_parse_buffer error\n");
		goto err;
	}
	guint n_medias = gst_sdp_message_medias_len(&msg);
	for (guint i = 0; i < n_medias; i++) {
		char uri[64];
		const GstSDPMedia *media = gst_sdp_message_get_media(&msg, i);
		const GstSDPConnection *conn;
		const GstCaps *caps;
		const GstStructure *gstr;
		const char *sp;

		if (strncmp("video", gst_sdp_media_get_media(media), 5) != 0)
			continue;
		conn = gst_sdp_media_get_connection(media, 0);
		if (!gst_sdp_address_is_multicast(conn->nettype, conn->addrtype,
		                                  conn->address))
			continue;
		caps = gst_sdp_media_get_caps_from_media(media, (gint) 96);
		gstr = gst_caps_get_structure(caps, 0);
		if (!gst_structure_has_field_typed(gstr, "sprop-parameter-sets",
		                                   G_TYPE_STRING))
			continue;

		snprintf(uri, sizeof(uri), "udp://%s:%d", conn->address,
			gst_sdp_media_get_port(media));
		nstr_cat_z(&udp, uri);
		sp = gst_structure_get_string(gstr, "sprop-parameter-sets");
		nstr_cat_z(&sprops, sp);
		goto out;
	}
	elog_err("sdp_data_parse failed: no valid media\n");
err:
	gst_sdp_message_uninit(&msg);
	return false;
out:
	/* Redirect location with value in SDP */
	sdp->udp = udp;
	sdp->sprops = sprops;
	gst_sdp_message_uninit(&msg);
	return true;
}

static bool sdp_data_from_cache(struct sdp_data *sdp) {
	sdp->cache = config_load_cache(sdp->loc_hash, sdp->cache);
	return (nstr_len(sdp->cache) > 0) && sdp_data_parse(sdp, sdp->cache);
}

bool sdp_data_cache(struct sdp_data *sdp) {
	bool s = (sdp->is_sdp) && sdp_data_from_cache(sdp);
	if (s)
		elog_err("SDP cache: %s\n", nstr_z(sdp->udp));
	return s;
}

static size_t sdp_write(void *contents, size_t size, size_t nmemb, void *uptr) {
	nstr_t *str = (nstr_t *) uptr;
	size_t sz = size * nmemb;
	nstr_t src = nstr_init_n(contents, sz, sz);
	nstr_cat(str, src);
	return nstr_len(*str);
}

static void sdp_data_get_http(struct sdp_data *sdp) {
	CURLcode rc;

	CURL *ch = curl_easy_init();
	curl_easy_setopt(ch, CURLOPT_URL, sdp->loc_buf);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, TIMEOUT_SEC);
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, TIMEOUT_SEC);
	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, sdp_write);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, &sdp->fetch);
	curl_easy_setopt(ch, CURLOPT_HTTPAUTH, 0);
	rc = curl_easy_perform(ch);
	if (rc != CURLE_OK) {
		elog_err("curl error: %s\n", curl_easy_strerror(rc));
		sdp->fetch = nstr_init(sdp->fetch_buf, sizeof(sdp->fetch_buf));
	}
	curl_easy_cleanup(ch);
}

static bool sdp_data_into_cache(struct sdp_data *sdp) {
	sdp_data_get_http(sdp);
	if (nstr_len(sdp->fetch) > 0 && sdp_data_parse(sdp, sdp->fetch)) {
		if (!nstr_equals(sdp->cache, sdp->fetch)) {
			config_store_cache(sdp->loc_hash, sdp->fetch);
			return true;
		}
	}
	return false;
}

bool sdp_data_fetch(struct sdp_data *sdp) {
	bool s = (sdp->is_sdp) && sdp_data_into_cache(sdp);
	if (s)
		elog_err("SDP fetch: %s\n", nstr_z(sdp->udp));
	return s;
}
