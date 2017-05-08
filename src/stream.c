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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include "elog.h"
#include "stream.h"

static const uint32_t DEFAULT_LATENCY = 50;
static const uint32_t GST_VIDEO_TEST_SRC_BLACK = 2;

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
	GstPadLinkReturn ret = gst_pad_link(pad, p);
	if (ret != GST_PAD_LINK_OK)
		elog_err("Pad link error: %s\n", gst_pad_link_get_name(ret));
	gst_object_unref(p);
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

static void stream_add_sink(struct stream *st) {
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, st->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", st->aspect, NULL);
	stream_add(st, sink);
}

static void stream_add_videobox(struct stream *st) {
	GstElement *vbx = gst_element_factory_make("videobox", NULL);
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
}

static GstCaps *create_caps_mpeg2(void) {
	return gst_caps_new_simple("application/x-rtp",
	                           "clock-rate", G_TYPE_INT, 90000,
	                           "encoding-name", G_TYPE_STRING, "MP2T",
	                           NULL);
}

static GstCaps *create_caps_generic(void) {
	return gst_caps_new_simple("application/x-rtp",
	                           "clock-rate", G_TYPE_INT, 90000,
	                           NULL);
}

static GstCaps *stream_create_caps(struct stream *st) {
	return (strcmp("MPEG2", st->encoding) == 0)
	      ?	create_caps_mpeg2()
	      : create_caps_generic();
}

static void stream_add_filter(struct stream *st) {
	GstElement *fltr = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = stream_create_caps(st);
	g_object_set(G_OBJECT(fltr), "caps", caps, NULL);
	gst_caps_unref(caps);
	stream_add(st, fltr);
}

static void stream_add_sdp_demux(struct stream *st) {
	GstElement *sdp = gst_element_factory_make("sdpdemux", NULL);
	g_object_set(G_OBJECT(sdp), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(sdp), "timeout", 1000000, NULL);
	stream_add(st, sdp);
}

static void stream_add_src_blank(struct stream *st) {
	GstElement *src = gst_element_factory_make("videotestsrc", NULL);
	g_object_set(G_OBJECT(src), "pattern", GST_VIDEO_TEST_SRC_BLACK, NULL);
	stream_add(st, src);
}

static void stream_add_src_udp(struct stream *st) {
	GstElement *src = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(src), "uri", st->location, NULL);
	// Post GstUDPSrcTimeout messages after 1 second (ns)
	g_object_set(G_OBJECT(src), "timeout", 1000000000, NULL);
	stream_add(st, src);
}

static void stream_add_src_http(struct stream *st) {
	GstElement *src = gst_element_factory_make("souphttpsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	stream_add(st, src);
}

static void stream_add_src_rtsp(struct stream *st) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	g_object_set(G_OBJECT(src), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(src), "timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "tcp-timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	stream_add(st, src);
}

void stream_start_blank(struct stream *st) {
	stream_add_sink(st);
	stream_add_src_blank(st);

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
}

static void stream_add_later_elements(struct stream *st) {
	stream_add_sink(st);
	stream_add_videobox(st);
	if (strcmp("MPEG2", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("mpeg2dec", NULL));
		stream_add(st, gst_element_factory_make("tsdemux", NULL));
		stream_add(st, gst_element_factory_make("rtpmp2tdepay", NULL));
		stream_add_queue(st);
	} else if (strcmp("MPEG4", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("avdec_mpeg4", NULL));
		stream_add(st, gst_element_factory_make("rtpmp4vdepay", NULL));
	} else if (strcmp("H264", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("avdec_h264", NULL));
		stream_add(st, gst_element_factory_make("rtph264depay", NULL));
	} else if (strcmp("PNG", st->encoding) == 0) {
		stream_add(st, gst_element_factory_make("imagefreeze", NULL));
		stream_add(st, gst_element_factory_make("videoconvert", NULL));
		stream_add(st, gst_element_factory_make("pngdec", NULL));
	} else
		elog_err("Invalid encoding: %s\n", st->encoding);
}

static void stream_add_udp_pipe(struct stream *st) {
	stream_add_jitter(st);
	stream_add_filter(st);
	stream_add_src_udp(st);
}

static void stream_add_http_pipe(struct stream *st) {
	if (strcmp("PNG", st->encoding) != 0)
		stream_add_sdp_demux(st);
	stream_add_src_http(st);
}

static void stream_add_rtsp_pipe(struct stream *st) {
	stream_add_src_rtsp(st);
}

void stream_start_pipeline(struct stream *st) {
	stream_add_later_elements(st);
	if (strncmp("udp", st->location, 3) == 0)
		stream_add_udp_pipe(st);
	else if (strncmp("http", st->location, 4) == 0)
		stream_add_http_pipe(st);
	else if (strncmp("rtsp", st->location, 4) == 0)
		stream_add_rtsp_pipe(st);
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
}

void stream_stop_pipeline(struct stream *st) {
	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	stream_remove_all(st);
}

static void stream_stop(struct stream *st) {
	if (st->stop)
		st->stop(st);
}

static void stream_ack_started(struct stream *st) {
	if (st->ack_started)
		st->ack_started(st);
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
	struct stream *st = (struct stream *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		elog_err("End of stream: %s\n", st->location);
		stream_stop(st);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		elog_err("Error: %s  %s\n", error->message, st->location);
		g_error_free(error);
		stream_stop(st);
		break;
	}
	case GST_MESSAGE_WARNING: {
		gchar *debug;
		GError *error;
		gst_message_parse_warning(msg, &error, &debug);
		g_free(debug);
		elog_err("Warning: %s  %s\n", error->message, st->location);
		g_error_free(error);
		break;
	}
	case GST_MESSAGE_ELEMENT:
		if (gst_message_has_name(msg, "GstUDPSrcTimeout")) {
			elog_err("udpsrc timeout -- stopping stream\n");
			stream_stop(st);
		}
		break;
	case GST_MESSAGE_ASYNC_DONE:
		stream_ack_started(st);
		break;
	default:
		break;
	}
	return TRUE;
}

void stream_init(struct stream *st, uint32_t idx) {
	char name[8];

	snprintf(name, 8, "m%d", idx);
	memset(st->location, 0, sizeof(st->location));
	memset(st->encoding, 0, sizeof(st->encoding));
	st->latency = DEFAULT_LATENCY;
	st->handle = 0;
	st->aspect = FALSE;
	st->pipeline = gst_pipeline_new(name);
	st->bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	gst_bus_add_watch(st->bus, bus_cb, st);
	memset(st->elem, 0, sizeof(st->elem));
	st->stop = NULL;
	st->ack_started = NULL;
}

void stream_destroy(struct stream *st) {
	stream_stop_pipeline(st);
	gst_bus_remove_watch(st->bus);
	g_object_unref(st->pipeline);
	st->pipeline = NULL;
}

void stream_set_location(struct stream *st, const char *loc) {
	strncpy(st->location, loc, sizeof(st->location));
}

void stream_set_encoding(struct stream *st, const char *encoding) {
	strncpy(st->encoding, encoding, sizeof(st->encoding));
}

void stream_set_latency(struct stream *st, uint32_t latency) {
	st->latency = latency;
}

void stream_set_handle(struct stream *st, guintptr handle) {
	st->handle = handle;
}

void stream_set_aspect(struct stream *st, gboolean aspect) {
	st->aspect = aspect;
}
