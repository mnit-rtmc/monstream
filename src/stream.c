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

#include <assert.h>
#include <stdio.h>
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

static GstElement *make_element(const char *factory_name, const char *name) {
	GstElementFactory *factory = gst_element_factory_find(factory_name);
	if (!factory) {
		elog_err("Factory %s not found\n", factory_name);
		return NULL;
	}
	GstElement *elem = gst_element_factory_create(factory, name);
	gst_object_unref(factory);
	if (!elem)
		elog_err("Failed to create %s element\n", factory_name);
	return elem;
}

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
	if (elem != NULL && gst_bin_add(GST_BIN(st->pipeline), elem)) {
		int i = stream_elem_next(st);
		st->elem[i] = elem;
		if (i > 0)
			stream_link(st, i);
	} else
		elog_err("Element not added to pipeline\n");
}

static GstElement *stream_create_real_sink(struct stream *st) {
	GstElement *sink = make_element("xvimagesink", NULL);
	if (sink != NULL) {
		// Playback sometimes stutters without "sync" enabled
		g_object_set(G_OBJECT(sink), "sync", TRUE, NULL);
		GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
		gst_video_overlay_set_window_handle(overlay, st->handle);
		g_object_set(G_OBJECT(sink), "force-aspect-ratio", st->aspect,
			NULL);
		st->sink = sink;
	} else {
		elog_err("Sink element not created!\n");
	}
	return sink;
}

static void stream_add_sink(struct stream *st) {
	GstElement *sink = (st->handle)
	      ? stream_create_real_sink(st)
	      : make_element("fakesink", NULL);
	stream_add(st, sink);
}

static void stream_add_text(struct stream *st) {
	char font[32];
	snprintf(font, sizeof(font), "Overpass, Bold %d", st->font_sz);
	GstElement *txt = make_element("textoverlay", NULL);
	g_object_set(G_OBJECT(txt), "text", st->description, NULL);
	g_object_set(G_OBJECT(txt), "font-desc", font, NULL);
	g_object_set(G_OBJECT(txt), "shaded-background", FALSE, NULL);
	g_object_set(G_OBJECT(txt), "color", 0xFFFFFFE0, NULL);
	g_object_set(G_OBJECT(txt), "halignment", 2, NULL); // right
	g_object_set(G_OBJECT(txt), "valignment", 2, NULL); // top
	g_object_set(G_OBJECT(txt), "wrap-mode", -1, NULL); // no wrapping
	g_object_set(G_OBJECT(txt), "xpad", 48, NULL);
	g_object_set(G_OBJECT(txt), "ypad", 36, NULL);
	g_object_set(G_OBJECT(txt), "scale-mode", 2, NULL); // display
	stream_add(st, txt);
}

static void stream_add_queue(struct stream *st) {
	GstElement *que = make_element("queue", NULL);
	g_object_set(G_OBJECT(que), "max-size-time", 650000000, NULL);
	stream_add(st, que);
}

static void stream_add_jitter(struct stream *st) {
	GstElement *jtr = make_element("rtpjitterbuffer", NULL);
	g_object_set(G_OBJECT(jtr), "latency", st->latency, NULL);
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
	GstElement *fltr = make_element("capsfilter", NULL);
	GstCaps *caps = stream_create_caps(st);
	g_object_set(G_OBJECT(fltr), "caps", caps, NULL);
	gst_caps_unref(caps);
	stream_add(st, fltr);
}

static void stream_add_src_udp(struct stream *st) {
	GstElement *src = make_element("udpsrc", NULL);
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
		elog_err("Unsupported encoding for HTTP: %s\n", st->encoding);
		/* Use IP address in TEST-NET-1 range to ensure the stream
		 * will timeout quickly */
		return "http://192.0.2.1/";
	}
}

static void stream_add_src_http(struct stream *st) {
	GstElement *src = make_element("souphttpsrc", NULL);
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
	GstElement *src = make_element("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	g_object_set(G_OBJECT(src), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(src), "timeout", ONE_SEC_US, NULL);
	g_object_set(G_OBJECT(src), "tcp-timeout", TEN_SEC_US, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "select-stream", G_CALLBACK(select_stream_cb),st);
	stream_add(st, src);
}

/** Set crop parameters */
void stream_set_crop(struct stream *st, nstr_t crop, uint32_t hgap,
	uint32_t vgap)
{
	nstr_to_cstr(st->crop, sizeof(st->crop), crop);
	st->hgap = hgap;
	st->vgap = vgap;
}

/** Check if a crop is valid.
 * @param num Window fraction numerator, 0 to 7.
 * @param den Window fraction denominator, 1 to 8. */
static bool crop_valid(int num, int den) {
	return num >= 0 && num < den && den <= 8;
}

/** Check if crop is configured */
static bool stream_has_crop(const struct stream *st) {
	return crop_valid(st->crop[0] - 'A', st->crop[1] - 'A' + 1)
	    || crop_valid(st->crop[2] - 'A', st->crop[3] - 'A' + 1);
}

/** Calculate edge crop gap, in pixels.
 *
 * @param pix Pixel width/height of cropped view.
 * @param gap Gap, in hundredths of percent of total (0.00 to 100.00) */
static int crop_gap(gint pix, uint32_t gap) {
	return (gap > 0 && gap <= 10000) ? (pix * gap / (10000 * 2)) : 0;
}

/** Get number of pixels to crop from edge.
 *
 * @param total Width/height of window.
 * @param num Window fraction numerator, 0 to 7.
 * @param den Window fraction denominator, 1 to 8.
 * @param gap Gap, in hundredths of percent of window (0.00 to 100.00). */
static gint crop_pix(gint total, int num, int den, uint32_t gap) {
	return (total > 0 && crop_valid(num, den))
	      ? (total * num / den) + crop_gap(total / den, gap)
	      : 0;
}

/** Get number of pixels to crop from top edge */
static gint stream_crop_top(const struct stream *st, gint height) {
	int num = st->crop[2] - 'A';
	int den = st->crop[3] - 'A' + 1;
	return crop_pix(height, num, den, st->vgap);
}

/** Get number of pixels to crop from bottom edge */
static gint stream_crop_bottom(const struct stream *st, gint height) {
	int num = st->crop[3] - st->crop[2];
	int den = st->crop[3] - 'A' + 1;
	return crop_pix(height, num, den, st->vgap);
}

/** Get number of pixels to crop from left edge */
static gint stream_crop_left(const struct stream *st, gint width) {
	int num = st->crop[0] - 'A';
	int den = st->crop[1] - 'A' + 1;
	return crop_pix(width, num, den, st->hgap);
}

/** Get number of pixels to crop from right edge */
static gint stream_crop_right(const struct stream *st, gint width) {
	int num = st->crop[1] - st->crop[0];
	int den = st->crop[1] - 'A' + 1;
	return crop_pix(width, num, den, st->hgap);
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

static bool stream_is_location_ok(const struct stream *st) {
	return stream_is_udp(st) || stream_is_http(st) || stream_is_rtsp(st);
}

static void stream_add_mpeg4(struct stream *st) {
	GstElement *dec = make_element("avdec_mpeg4", NULL);
	g_object_set(G_OBJECT(dec), "output-corrupt", FALSE, NULL);
	stream_add(st, dec);
	stream_add(st, make_element("rtpmp4vdepay", NULL));
}

static GstElement *stream_create_h264dec(const struct stream *st) {
	return (strcmp("VAAPI", st->sink_name) == 0)
	      ? make_element("vaapih264dec", NULL)
	      : make_element("openh264dec", NULL);
}

static bool stream_is_encoding_ok(const struct stream *st) {
	return (strcmp("H264", st->encoding) == 0) ||
	       (strcmp("MPEG4", st->encoding) == 0) ||
	       (strcmp("PNG", st->encoding) == 0) ||
	       (strcmp("MJPEG", st->encoding) == 0) ||
	       (strcmp("MPEG2", st->encoding) == 0);
}

static void stream_add_h264(struct stream *st) {
	stream_add(st, stream_create_h264dec(st));
	stream_add(st, make_element("h264parse", NULL));
	stream_add(st, make_element("rtph264depay", NULL));
}

static void stream_add_png(struct stream *st) {
	stream_add(st, make_element("imagefreeze", NULL));
	stream_add(st, make_element("videoconvert", NULL));
	stream_add(st, make_element("pngdec", NULL));
}

static void stream_add_later_elements(struct stream *st) {
	assert(stream_is_encoding_ok(st));
	stream_add_sink(st);
	// NOTE: MJPEG and textoverlay don't play well together,
	//       due to timestamp issues.
	if (stream_has_description(st) && strcmp("MJPEG", st->encoding) != 0)
		stream_add_text(st);
	if (stream_has_crop(st))
		stream_add(st, make_element("videobox", "vbox"));
	if (strcmp("H264", st->encoding) == 0) {
		stream_add_h264(st);
	} else if (strcmp("MPEG4", st->encoding) == 0) {
		stream_add_mpeg4(st);
	} else if (strcmp("PNG", st->encoding) == 0) {
		stream_add_png(st);
	} else if (strcmp("MJPEG", st->encoding) == 0) {
		stream_add(st, make_element("jpegdec", NULL));
	} else {
		stream_add(st, make_element("mpeg2dec", NULL));
		stream_add(st, make_element("tsdemux", NULL));
		stream_add(st, make_element("rtpmp2tdepay", NULL));
		stream_add_queue(st);
	}
}

static void stream_add_udp_pipe(struct stream *st) {
	stream_add_jitter(st);
	stream_add_filter(st);
	stream_add_src_udp(st);
}

static void stream_start_pipeline(struct stream *st) {
	assert(stream_is_location_ok(st));
	stream_add_later_elements(st);
	if (stream_is_udp(st))
		stream_add_udp_pipe(st);
	else if (stream_is_http(st))
		stream_add_src_http(st);
	else
		stream_add_src_rtsp(st);
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
	st->sink = NULL;
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

static void stream_config_videobox(struct stream *st, gint width, gint height) {
	GstElement *vbx = stream_find_videobox(st);
	if (vbx) {
		gpointer gobj = G_OBJECT(vbx);
		g_object_set(gobj, "top", stream_crop_top(st, height), NULL);
		g_object_set(gobj, "bottom", stream_crop_bottom(st, height),
			NULL);
		g_object_set(gobj, "left", stream_crop_left(st, width), NULL);
		g_object_set(gobj, "right", stream_crop_right(st, width), NULL);
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

void stream_init(struct stream *st, uint32_t idx, struct lock *lock,
	nstr_t sink_name)
{
	char name[8];

	st->lock = lock;
	snprintf(name, sizeof(name), "m%d", idx);
	memset(st->sink_name, 0, sizeof(st->sink_name));
	nstr_to_cstr(st->sink_name, sizeof(st->sink_name), sink_name);
	memset(st->crop, 0, sizeof(st->crop));
	memset(st->cam_id, 0, sizeof(st->cam_id));
	memset(st->location, 0, sizeof(st->location));
	memset(st->encoding, 0, sizeof(st->encoding));
	memset(st->sprops, 0, sizeof(st->sprops));
	memset(st->description, 0, sizeof(st->description));
	st->latency = DEFAULT_LATENCY;
	st->font_sz = 22;
	st->hgap = 0;
	st->vgap = 0;
	st->handle = 0;
	st->aspect = FALSE;
	st->pipeline = gst_pipeline_new(name);
	GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	st->watch = gst_bus_add_watch(bus, bus_cb, st);
	gst_object_unref(bus);
	memset(st->elem, 0, sizeof(st->elem));
	st->jitter = NULL;
	st->sink = NULL;
	st->last_pts = 0;
	st->pushed = 0;
	st->lost = 0;
	st->late = 0;
	st->do_stop = NULL;
	st->ack_started = NULL;
}

void stream_destroy(struct stream *st) {
	stream_stop_pipeline(st);
	gst_object_unref(st->pipeline);
	g_source_remove(st->watch);
	st->pipeline = NULL;
	st->lock = NULL;
}

void stream_set_handle(struct stream *st, guintptr handle) {
	st->handle = handle;
}

void stream_set_aspect(struct stream *st, bool aspect) {
	st->aspect = aspect;
}

void stream_set_params(struct stream *st, nstr_t cam_id, nstr_t loc,
	nstr_t desc, nstr_t encoding, uint32_t latency, nstr_t sprops)
{
	nstr_to_cstr(st->cam_id, sizeof(st->cam_id), cam_id);
	nstr_to_cstr(st->location, sizeof(st->location), loc);
	nstr_to_cstr(st->description, sizeof(st->description), desc);
	nstr_to_cstr(st->encoding, sizeof(st->encoding), encoding);
	st->latency = latency;
	nstr_to_cstr(st->sprops, sizeof(st->sprops), sprops);
}

void stream_set_font_size(struct stream *st, uint32_t sz) {
	st->font_sz = sz;
}

/* Check sink to make sure that last-sample is updating.
 * If not, post an EOS message on the bus. */
static void stream_check_sink(struct stream *st) {
	GstSample *s;
	g_object_get(st->sink, "last-sample", &s, NULL);
	if (s) {
		GstBuffer *b = gst_sample_get_buffer(s);
		GstClockTime t = GST_BUFFER_PTS(b);
		gst_sample_unref(s);
		if (t == st->last_pts) {
			elog_err("PTS stuck at %lu; posting EOS\n", t);
			GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(
			                                   st->pipeline));
			gst_bus_post(bus, gst_message_new_eos(
			             GST_OBJECT_CAST(st->sink)));
			gst_object_unref(bus);
		}
		st->last_pts = t;
	}
}

void stream_check_eos(struct stream *st) {
	if (st->sink)
		stream_check_sink(st);
}

static bool stream_jitter_stats(struct stream *st) {
	GstStructure *s;
	g_object_get(st->jitter, "stats", &s, NULL);
	if (s) {
		guint64 pushed, lost, late;
		gboolean r =
			gst_structure_get_uint64(s, "num-pushed", &pushed)
		     && gst_structure_get_uint64(s, "num-lost", &lost)
		     && gst_structure_get_uint64(s, "num-late", &late);
		gst_structure_free(s);
		if (r) {
			st->pushed = pushed;
			st->lost = lost;
			st->late = late;
			return true;
		}
	}
	return false;
}

static bool stream_update_stats(struct stream *st) {
	return (st->jitter) && stream_jitter_stats(st);
}

static guint64 pkt_count(guint64 t0, guint64 t1) {
	return (t0 < t1) ? t1 - t0 : 0;
}

bool stream_stats(struct stream *st) {
	guint64 pushed = st->pushed;
	guint64 lost = st->lost;
	guint64 late = st->late;
	bool update = stream_update_stats(st);
	if (update) {
		elog_err("stats %s: "
			 "%" G_GUINT64_FORMAT " pushed, "
			 "%" G_GUINT64_FORMAT " lost, "
			 "%" G_GUINT64_FORMAT " late pkts\n", st->cam_id,
		         pkt_count(pushed, st->pushed),
		         pkt_count(lost, st->lost),
		         pkt_count(late, st->late));
		return true;
	} else
		return false;
}

static void stream_reset_counters(struct stream *st) {
	st->pushed = 0;
	st->lost = 0;
	st->late = 0;
}

static void stream_start_pipe(struct stream *st) {
	stream_reset_counters(st);
	stream_start_pipeline(st);
}

bool stream_start(struct stream *st) {
	/* Make sure pipeline is not running */
	stream_stop_pipeline(st);
	if (!stream_is_location_ok(st)) {
		elog_err("Invalid location: %s\n", st->location);
		return false;
	}
	if (!stream_is_encoding_ok(st)) {
		elog_err("Invalid encoding: %s\n", st->encoding);
		return false;
	}
	stream_start_pipe(st);
	return true;
}

void stream_stop(struct stream *st) {
	stream_stop_pipeline(st);
}
