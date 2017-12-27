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

#include <stdio.h>
#include <curl/curl.h>
#include <gst/sdp/sdp.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include "elog.h"
#include "nstr.h"
#include "config.h"
#include "stream.h"

#define ONE_SEC_US	(1000000)
#define TEN_SEC_US	(10000000)
#define ONE_SEC_NS	(1000000000)

#define STREAM_NUM_VIDEO	(0)

static const uint32_t DEFAULT_LATENCY = 50;

static int stream_elem_next(const struct stream *st) {
	int i = 0;
	while (st->elem[i]) {
		i++;
		if (i >= MAX_ELEMS) {
			elog_err("Too many elements\n");
			return 0;
		}
	}
	return i;
}

static void pad_added_cb(GstElement *src, GstPad *pad, gpointer data) {
	GstElement *sink = (GstElement *) data;
	GstPad *p = gst_element_get_static_pad(sink, "sink");
	if (p) {
		GstPadLinkReturn ret = gst_pad_link(pad, p);
		if (ret != GST_PAD_LINK_OK) {
			elog_err("Pad link error: %s\n",
				gst_pad_link_get_name(ret));
		}
		gst_object_unref(p);
	} else
		elog_err("Sink pad not found\n");
}

static void stream_link(struct stream *st, int i) {
	GstElement *sink = st->elem[i - 1];
	GstElement *src = st->elem[i];
	if (!gst_element_link(src, sink)) {
		g_signal_connect(src, "pad-added", G_CALLBACK(pad_added_cb),
		                 sink);
	}
}

static void stream_add(struct stream *st, GstElement *elem) {
	if (gst_bin_add(GST_BIN(st->pipeline), elem)) {
		int i = stream_elem_next(st);
		st->elem[i] = elem;
		if (i > 0)
			stream_link(st, i);
	} else
		elog_err("Element not added to pipeline\n");
}

static GstElement* stream_create_xvimagesink(struct stream *st) {
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, st->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", st->aspect, NULL);
	return sink;
}

static void stream_add_sink(struct stream *st) {
	GstElement *sink = (st->handle)
	      ? stream_create_xvimagesink(st)
	      : gst_element_factory_make("fakesink", NULL);
	stream_add(st, sink);
}

static void stream_add_text(struct stream *st) {
	char font[32];
	snprintf(font, sizeof(font), "Overpass, Bold %d", st->font_sz);
	GstElement *txt = gst_element_factory_make("textoverlay", NULL);
	g_object_set(G_OBJECT(txt), "text", st->description, NULL);
	g_object_set(G_OBJECT(txt), "font-desc", font, NULL);
	g_object_set(G_OBJECT(txt), "shaded-background", FALSE, NULL);
	g_object_set(G_OBJECT(txt), "color", 0xFFFFFFE0, NULL);
	g_object_set(G_OBJECT(txt), "halignment", 0, NULL); // left
	g_object_set(G_OBJECT(txt), "valignment", 2, NULL); // top
	g_object_set(G_OBJECT(txt), "wrap-mode", -1, NULL); // no wrapping
	g_object_set(G_OBJECT(txt), "xpad", 48, NULL);
	g_object_set(G_OBJECT(txt), "ypad", 36, NULL);
	stream_add(st, txt);
}

static void stream_add_videobox(struct stream *st) {
	GstElement *vbx = gst_element_factory_make("videobox", "vbox");
	g_object_set(G_OBJECT(vbx), "top", -1, NULL);
	g_object_set(G_OBJECT(vbx), "bottom", -1, NULL);
	g_object_set(G_OBJECT(vbx), "left", -1, NULL);
	g_object_set(G_OBJECT(vbx), "right", -1, NULL);
	stream_add(st, vbx);
}

static void stream_add_queue(struct stream *st) {
	GstElement *que = gst_element_factory_make("queue", NULL);
	g_object_set(G_OBJECT(que), "max-size-time", 650000000, NULL);
	stream_add(st, que);
}

static void stream_add_jitter(struct stream *st) {
	GstElement *jtr = gst_element_factory_make("rtpjitterbuffer", NULL);
	g_object_set(G_OBJECT(jtr), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(jtr), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(jtr), "max-dropout-time", 1500, NULL);
	stream_add(st, jtr);
	st->jitter = jtr;
}

static GstCaps *create_caps_mpeg2(void) {
	return gst_caps_new_simple("application/x-rtp",
	                           "clock-rate", G_TYPE_INT, 90000,
	                           "encoding-name", G_TYPE_STRING, "MP2T",
	                           NULL);
}

static GstCaps *create_caps_generic(const struct stream *st) {
	return gst_caps_new_simple("application/x-rtp",
	                "clock-rate", G_TYPE_INT, 90000,
	                "sprop-parameter-sets", G_TYPE_STRING, st->sprops,
	                NULL);
}

static GstCaps *stream_create_caps(const struct stream *st) {
	return (strcmp("MPEG2", st->encoding) == 0)
	      ?	create_caps_mpeg2()
	      : create_caps_generic(st);
}

static void stream_add_filter(struct stream *st) {
	GstElement *fltr = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = stream_create_caps(st);
	g_object_set(G_OBJECT(fltr), "caps", caps, NULL);
	gst_caps_unref(caps);
	stream_add(st, fltr);
}

static void stream_add_src_udp(struct stream *st) {
	GstElement *src = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(src), "uri", st->location, NULL);
	// Post GstUDPSrcTimeout messages after 2 seconds (ns)
	g_object_set(G_OBJECT(src), "timeout", 2 * ONE_SEC_NS, NULL);
	stream_add(st, src);
}

static const char *stream_location_http(const struct stream *st) {
	if ((strcmp("PNG", st->encoding) == 0) ||
	    (strcmp("MJPEG", st->encoding) == 0))
	{
		return st->location;
	} else {
		/* Use IP address in TEST-NET-1 range to ensure the stream
		 * will timeout quickly */
		return "http://192.0.2.1/";
	}
}

static void stream_add_src_http(struct stream *st) {
	GstElement *src = gst_element_factory_make("souphttpsrc", NULL);
	g_object_set(G_OBJECT(src), "location", stream_location_http(st), NULL);
	g_object_set(G_OBJECT(src), "timeout", 2, NULL);
	g_object_set(G_OBJECT(src), "retries", 0, NULL);
	stream_add(st, src);
}

static gboolean select_stream_cb(GstElement *src, guint num, GstCaps *caps,
	gpointer user_data)
{
	switch (num) {
	case STREAM_NUM_VIDEO:
		return TRUE;
	default:
		return FALSE;
	}
}

static void stream_add_src_rtsp(struct stream *st) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	g_object_set(G_OBJECT(src), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(src), "timeout", ONE_SEC_US, NULL);
	g_object_set(G_OBJECT(src), "tcp-timeout", TEN_SEC_US, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "select-stream", G_CALLBACK(select_stream_cb),st);
	stream_add(st, src);
}

static bool stream_has_description(const struct stream *st) {
	return st->description[0] != '\0';
}

static bool stream_is_udp(const struct stream *st) {
	return strncmp("udp://", st->location, 6) == 0;
}

static bool stream_is_http(const struct stream *st) {
	return strncmp("http://", st->location, 7) == 0;
}

static bool stream_is_rtsp(const struct stream *st) {
	return strncmp("rtsp://", st->location, 7) == 0;
}

static bool stream_is_sdp(const struct stream *st) {
	return stream_is_http(st)
	    && (strstr(st->location, ".sdp") != NULL);
}

static void stream_add_mpeg4(struct stream *st) {
	GstElement *dec = gst_element_factory_make("avdec_mpeg4", NULL);
	g_object_set(G_OBJECT(dec), "output-corrupt", FALSE, NULL);
	stream_add(st, dec);
	stream_add(st, gst_element_factory_make("rtpmp4vdepay", NULL));
}

static void stream_add_h264(struct stream *st) {
	GstElement *dec = gst_element_factory_make("avdec_h264", NULL);
	g_object_set(G_OBJECT(dec), "output-corrupt", FALSE, NULL);
	stream_add(st, dec);
	stream_add(st, gst_element_factory_make("rtph264depay", NULL));
}

static void stream_add_png(struct stream *st) {
	stream_add(st, gst_element_factory_make("imagefreeze", NULL));
	stream_add(st, gst_element_factory_make("videoconvert", NULL));
	stream_add(st, gst_element_factory_make("pngdec", NULL));
}

static void stream_add_later_elements(struct stream *st) {
	stream_add_sink(st);
	// NOTE: MJPEG and textoverlay don't play well together,
	//       due to timestamp issues.
	if (stream_has_description(st) && strcmp("MJPEG", st->encoding) != 0) {
		stream_add_text(st);
	}
	stream_add_videobox(st);
	if (stream_is_sdp(st)) {
		stream_add_png(st);
	} else if (strcmp("H264", st->encoding) == 0) {
		stream_add_h264(st);
	} else if (strcmp("MPEG4", st->encoding) == 0) {
		stream_add_mpeg4(st);
	} else if (strcmp("PNG", st->encoding) == 0) {
		stream_add_png(st);
	} else if (strcmp("MJPEG", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("jpegdec", NULL));
	} else if (strcmp("MPEG2", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("mpeg2dec", NULL));
		stream_add(st, gst_element_factory_make("tsdemux", NULL));
		stream_add(st, gst_element_factory_make("rtpmp2tdepay", NULL));
		stream_add_queue(st);
	} else
		elog_err("Invalid encoding: %s\n", st->encoding);
}

static void stream_add_udp_pipe(struct stream *st) {
	stream_add_jitter(st);
	stream_add_filter(st);
	stream_add_src_udp(st);
}

static void stream_start_pipeline(struct stream *st) {
	stream_add_later_elements(st);
	if (stream_is_udp(st))
		stream_add_udp_pipe(st);
	else if (stream_is_http(st))
		stream_add_src_http(st);
	else if (stream_is_rtsp(st))
		stream_add_src_rtsp(st);
	else {
		elog_err("Invalid location: %s\n", st->location);
		return;
	}
	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
}

static void stream_remove_all(struct stream *st) {
	GstBin *bin = GST_BIN(st->pipeline);
	for (int i = 0; i < MAX_ELEMS; i++) {
		if (st->elem[i])
			gst_bin_remove(bin, st->elem[i]);
	}
	memset(st->elem, 0, sizeof(st->elem));
	st->jitter = NULL;
}

static void stream_stop_pipeline(struct stream *st) {
	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	stream_remove_all(st);
}

static void stream_do_stop(struct stream *st) {
	if (st->do_stop)
		st->do_stop(st);
}

static void stream_ack_started(struct stream *st) {
	lock_acquire(st->lock, __func__);
	if (st->ack_started)
		st->ack_started(st);
	lock_release(st->lock, __func__);
}

static void stream_msg_eos(struct stream *st) {
	lock_acquire(st->lock, __func__);
	elog_err("End of stream: %s\n", st->location);
	stream_do_stop(st);
	lock_release(st->lock, __func__);
}

static GstElement *stream_find_videobox(struct stream *st) {
	GstBin *bin = GST_BIN(st->pipeline);
	return gst_bin_get_by_name(bin, "vbox");
}

static gint videobox_span(gint total, uint32_t gap, int idx, int span) {
	// Gap is hundredths of percent of total
	int g = (gap > 0) ? (total * gap / (10000 / 2)) : 0;
	return (idx > 0 && idx < 4 && span >= 2 && span <= 4)
	     ? (idx * total / span) + g
	     : -1;
}

static gint stream_videobox_top(struct stream *st, gint height) {
	return videobox_span(height, st->vgap,
		st->crop[2] - 'A',
		(st->crop[3] - 'A') + 1);
}

static gint stream_videobox_bottom(struct stream *st, gint height) {
	return videobox_span(height, st->vgap,
		st->crop[3] - st->crop[2],
		(st->crop[3] - 'A') + 1);
}

static gint stream_videobox_left(struct stream *st, gint width) {
	return videobox_span(width, st->hgap,
		st->crop[0] - 'A',
		(st->crop[1] - 'A') + 1);
}

static gint stream_videobox_right(struct stream *st, gint width) {
	return videobox_span(width, st->hgap,
		st->crop[1] - st->crop[0],
		(st->crop[1] - 'A') + 1);
}

static void stream_config_videobox(struct stream *st, gint width, gint height) {
	GstElement *vbx = stream_find_videobox(st);
	if (vbx) {
		gint top = stream_videobox_top(st, height);
		gint bottom = stream_videobox_bottom(st, height);
		gint left = stream_videobox_left(st, width);
		gint right = stream_videobox_right(st, width);
		g_object_set(G_OBJECT(vbx), "top", top, NULL);
		g_object_set(G_OBJECT(vbx), "bottom", bottom, NULL);
		g_object_set(G_OBJECT(vbx), "left", left, NULL);
		g_object_set(G_OBJECT(vbx), "right", right, NULL);
		gst_object_unref(vbx);
	}
}

static void stream_config_size_caps(struct stream *st, GstCaps *caps) {
	for (int i = 0; i < gst_caps_get_size(caps); i++) {
		GstStructure *s = gst_caps_get_structure(caps, i);
		gint height = 0;
		gint width = 0;
		gst_structure_get_int(s, "width", &width);
		gst_structure_get_int(s, "height", &height);
		if (width > 0 && height > 0)
			stream_config_videobox(st, width, height);
	}
	gst_caps_unref(caps);
}

static void stream_config_size_pad(struct stream *st, GstPad *pad) {
	GstCaps *caps = gst_pad_get_current_caps(pad);
	if (caps)
		stream_config_size_caps(st, caps);
	else
		elog_err("Could not get vbox src pad current caps\n");
	gst_object_unref(pad);
}

static void stream_config_size(struct stream *st, GstElement *vbx) {
	GstPad *pad = gst_element_get_static_pad(vbx, "src");
	if (pad)
		stream_config_size_pad(st, pad);
	else
		elog_err("Could not find vbox src pad\n");
	gst_object_unref(vbx);
}

static void stream_msg_playing(struct stream *st) {
	GstElement *vbx = stream_find_videobox(st);
	if (vbx)
		stream_config_size(st, vbx);
}

static void stream_msg_state(struct stream *st, GstMessage *msg) {
	GstState old, state, pending;
	gst_message_parse_state_changed(msg, &old, &state, &pending);
	if (GST_STATE_PLAYING == state) {
		lock_acquire(st->lock, __func__);
		stream_msg_playing(st);
		lock_release(st->lock, __func__);
	}
}

static void stream_msg_error(struct stream *st, GstMessage *msg) {
	GError *error;
	gchar *debug;

	gst_message_parse_error(msg, &error, &debug);
	g_free(debug);
	lock_acquire(st->lock, __func__);
	elog_err("Error: %s  %s\n", error->message, st->location);
	stream_do_stop(st);
	lock_release(st->lock, __func__);
	g_error_free(error);
}

static void stream_msg_warning(struct stream *st, GstMessage *msg) {
	GError *warning;
	gchar *debug;

	gst_message_parse_warning(msg, &warning, &debug);
	g_free(debug);
	lock_acquire(st->lock, __func__);
	elog_err("Warning: %s  %s\n", warning->message, st->location);
	stream_do_stop(st);
	lock_release(st->lock, __func__);
	g_error_free(warning);
}

static void stream_msg_element(struct stream *st, GstMessage *msg) {
	if (gst_message_has_name(msg, "GstUDPSrcTimeout")) {
		elog_err("udpsrc timeout -- stopping stream\n");
		lock_acquire(st->lock, __func__);
		stream_do_stop(st);
		lock_release(st->lock, __func__);
	}
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
	struct stream *st = (struct stream *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		stream_msg_eos(st);
		break;
	case GST_MESSAGE_STATE_CHANGED:
		if (GST_MESSAGE_SRC(msg) == GST_OBJECT(st->pipeline))
			stream_msg_state(st, msg);
		break;
	case GST_MESSAGE_ERROR:
		stream_msg_error(st, msg);
		break;
	case GST_MESSAGE_WARNING:
		stream_msg_warning(st, msg);
		break;
	case GST_MESSAGE_ELEMENT:
		stream_msg_element(st, msg);
		break;
	case GST_MESSAGE_ASYNC_DONE:
		stream_ack_started(st);
		break;
	default:
		break;
	}
	return TRUE;
}

void stream_init(struct stream *st, uint32_t idx, struct lock *lock) {
	char name[8];

	st->lock = lock;
	snprintf(name, sizeof(name), "m%d", idx);
	memset(st->crop, 0, sizeof(st->crop));
	memset(st->cam_id, 0, sizeof(st->cam_id));
	memset(st->location, 0, sizeof(st->location));
	memset(st->encoding, 0, sizeof(st->encoding));
	memset(st->sprops, 0, sizeof(st->sprops));
	memset(st->description, 0, sizeof(st->description));
	st->loc_hash = 0;
	st->latency = DEFAULT_LATENCY;
	st->font_sz = 22;
	st->hgap = 0;
	st->vgap = 0;
	st->handle = 0;
	st->aspect = FALSE;
	st->pipeline = gst_pipeline_new(name);
	st->bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	gst_bus_add_watch(st->bus, bus_cb, st);
	memset(st->elem, 0, sizeof(st->elem));
	st->jitter = NULL;
	st->lost = 0;
	st->late = 0;
	st->n_starts = 0;
	st->do_stop = NULL;
	st->ack_started = NULL;
}

void stream_destroy(struct stream *st) {
	stream_stop_pipeline(st);
	gst_bus_remove_watch(st->bus);
	g_object_unref(st->pipeline);
	st->pipeline = NULL;
	st->lock = NULL;
}

void stream_set_handle(struct stream *st, guintptr handle) {
	st->handle = handle;
}

void stream_set_aspect(struct stream *st, bool aspect) {
	st->aspect = aspect;
}

static uint64_t fnv_hash(const char *str, uint32_t len) {
	const void *key = str;
	const uint8_t *p = key;
	uint64_t h = 14695981039346656037UL;
	int i;
	for (i = 0; i < len; i++) {
		h = (h * 1099511628211UL) ^ p[i];
	}
	return h;
}

void stream_set_params(struct stream *st, const char *cam_id, const char *loc,
	const char *desc, const char *encoding, uint32_t latency)
{
	strncpy(st->cam_id, cam_id, sizeof(st->cam_id));
	strncpy(st->location, loc, sizeof(st->location));
	strncpy(st->description, desc, sizeof(st->description));
	strncpy(st->encoding, encoding, sizeof(st->encoding));
	memset(st->sprops, 0, sizeof(st->sprops));
	st->latency = latency;
	st->loc_hash = fnv_hash(loc, strlen(loc));
	st->n_starts = 0;
}

void stream_set_font_size(struct stream *st, uint32_t sz) {
	st->font_sz = sz;
}

void stream_set_crop(struct stream *st, const char *crop, uint32_t hgap,
	uint32_t vgap)
{
	strncpy(st->crop, crop, sizeof(st->crop));
	st->hgap = hgap;
	st->vgap = vgap;
}

static bool stream_update_stats(struct stream *st) {
	if (st->jitter) {
		GstStructure *s;
		g_object_get(st->jitter, "stats", &s, NULL);
		if (s) {
			guint64 lost, late;
			gboolean r =
			        gst_structure_get_uint64(s, "num-lost", &lost)
			     && gst_structure_get_uint64(s, "num-late", &late);
			gst_structure_free(s);
			if (r) {
				st->lost = lost;
				st->late = late;
				return true;
			}
		}
	}
	return false;
}

bool stream_stats(struct stream *st) {
	guint64 lost = st->lost;
	guint64 late = st->late;
	bool update = stream_update_stats(st);
	if (update && (lost != st->lost || late != st->late)) {
		elog_err("stats %s: %" G_GUINT64_FORMAT " lost, %"
			G_GUINT64_FORMAT " late pkts\n", st->cam_id, st->lost,
			st->late);
		return true;
	} else
		return false;
}

static size_t sdp_write(void *contents, size_t size, size_t nmemb, void *uptr) {
	size_t sz = size * nmemb;
	nstr_t *str = (nstr_t *) uptr;
	nstr_t src = nstr_make(contents, sz, sz);
	nstr_cat(str, src);
	return nstr_len(*str);
}

static int64_t stream_timeout(const struct stream *st) {
	return (st->n_starts) <= 1 ? 1L : 5L;
}

static nstr_t stream_get_sdp_http(struct stream *st, nstr_t str) {
	CURLcode rc;

	int64_t timeout = stream_timeout(st);
	CURL *ch = curl_easy_init();
	curl_easy_setopt(ch, CURLOPT_URL, st->location);
	curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, sdp_write);
	curl_easy_setopt(ch, CURLOPT_WRITEDATA, &str);
	curl_easy_setopt(ch, CURLOPT_HTTPAUTH, 0);
	rc = curl_easy_perform(ch);
	if (rc != CURLE_OK) {
		elog_err("curl error: %s\n", curl_easy_strerror(rc));
		str = nstr_make(str.buf, 0, 0);
	}
	curl_easy_cleanup(ch);
	return str;
}

static nstr_t stream_get_sdp(struct stream *st, nstr_t str) {
	if (st->n_starts == 0) {
		str = config_load_cache(st->loc_hash, str);
	}
	if (nstr_len(str) == 0) {
		str = stream_get_sdp_http(st, str);
		if (nstr_len(str) > 0)
			config_store_cache(st->loc_hash, str);
	}
	st->n_starts++;
	return str;
}

static bool stream_sdp_parse(struct stream *st, nstr_t sdp) {
	char sp_buf[64];
	char udp_buf[64];
	nstr_t udp_uri = nstr_make(udp_buf, sizeof(udp_buf), 0);
	nstr_t sprops = nstr_make(sp_buf, sizeof(sp_buf), 0);
	GstSDPMessage msg;
	memset(&msg, 0, sizeof(msg));
	if (gst_sdp_message_init(&msg) != GST_SDP_OK) {
		elog_err("gst_sdp_message_init error\n");
		return false;
	}
	if (gst_sdp_message_parse_buffer((const guint8 *) sdp.buf, sdp.len,
		&msg) != GST_SDP_OK)
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
		if (!gst_structure_has_field_typed(gstr,
			"sprop-parameter-sets", G_TYPE_STRING))
			continue;

		snprintf(uri, sizeof(uri), "udp://%s:%d", conn->address,
			gst_sdp_media_get_port(media));
		nstr_cat_z(&udp_uri, uri);
		sp = gst_structure_get_string(gstr, "sprop-parameter-sets");
		nstr_cat_z(&sprops, sp);
		goto out;
	}
	elog_err("streap_sdp_parse failed: no valid media\n");
err:
	gst_sdp_message_uninit(&msg);
	return false;
out:
	strncpy(st->location, nstr_z(udp_uri), sizeof(st->location));
	strncpy(st->sprops, nstr_z(sprops), sizeof(st->sprops));
	elog_err("SDP redirect to %s\n", st->location);
	gst_sdp_message_uninit(&msg);
	return true;
}

static void stream_start_pipe(struct stream *st) {
	/* NOTE: sdpdemux element has multiple bugs -- we need to handle sdp
	 *       download ourselves, using curl. */
	if (stream_is_sdp(st)) {
		char buf[1024];
		nstr_t sdp = stream_get_sdp(st, nstr_make(buf, sizeof(buf), 0));
		if (nstr_len(sdp) > 0)
			stream_sdp_parse(st, sdp);
	}
	stream_start_pipeline(st);
}

void stream_start(struct stream *st) {
	/* Make sure pipeline is not running */
	stream_stop_pipeline(st);
	if (st->location[0])
		stream_start_pipe(st);
}

void stream_stop(struct stream *st) {
	stream_stop_pipeline(st);
}
