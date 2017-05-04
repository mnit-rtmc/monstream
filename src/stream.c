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

void stream_set_handle(struct stream *st, guintptr handle) {
	st->handle = handle;
}

void stream_set_aspect(struct stream *st, gboolean aspect) {
	st->aspect = aspect;
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

static GstElement *make_src_blank(void) {
	GstElement *src = gst_element_factory_make("videotestsrc", NULL);
	g_object_set(G_OBJECT(src), "pattern", GST_VIDEO_TEST_SRC_BLACK, NULL);
	return src;
}

static void source_pad_added_cb(GstElement *src, GstPad *pad, gpointer data) {
	struct stream *st = (struct stream *) data;
	GstPad *spad = gst_element_get_static_pad(st->depay, "sink");
	gst_pad_link(pad, spad);
	gst_object_unref(spad);
}

static GstElement *make_src_rtsp(struct stream *st) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	g_object_set(G_OBJECT(src), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(src), "timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "tcp-timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "pad-added", G_CALLBACK(source_pad_added_cb), st);
	return src;
}

static GstElement *make_sdp_demux(struct stream *st) {
	GstElement *sdp = gst_element_factory_make("sdpdemux", NULL);
	g_object_set(G_OBJECT(sdp), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(sdp), "timeout", 1000000, NULL);
	return sdp;
}

static GstElement *make_src_udp(struct stream *st) {
	GstElement *src = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(src), "uri", st->location, NULL);
	// Post GstUDPSrcTimeout messages after 1 second (ns)
	g_object_set(G_OBJECT(src), "timeout", 1000000000, NULL);
	return src;
}

static GstElement *make_filter(struct stream *st) {
	GstElement *filter = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = gst_caps_new_simple("application/x-rtp",
	                                    "clock-rate", G_TYPE_INT, 90000,
	                                    NULL);
	g_object_set(G_OBJECT(filter), "caps", caps, NULL);
	gst_caps_unref(caps);
	return filter;
}

static GstElement *make_jitter(struct stream *st) {
	GstElement *jitter = gst_element_factory_make("rtpjitterbuffer", NULL);
	g_object_set(G_OBJECT(jitter), "latency", st->latency, NULL);
	g_object_set(G_OBJECT(jitter), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(jitter), "max-dropout-time", 1500, NULL);
	return jitter;
}

static GstElement *make_src_http(struct stream *st) {
	GstElement *src = gst_element_factory_make("souphttpsrc", NULL);
	g_object_set(G_OBJECT(src), "location", st->location, NULL);
	return src;
}

static GstElement *make_videobox(void) {
	GstElement *vbx = gst_element_factory_make("videobox", NULL);
	g_object_set(G_OBJECT(vbx), "top", -1, NULL);
	g_object_set(G_OBJECT(vbx), "bottom", -1, NULL);
	g_object_set(G_OBJECT(vbx), "left", -1, NULL);
	g_object_set(G_OBJECT(vbx), "right", -1, NULL);
	return vbx;
}

static GstElement *make_sink(struct stream *st) {
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, st->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", st->aspect, NULL);
	return sink;
}

static void stream_remove_element(struct stream *st, GstElement *elem) {
	if (elem)
		gst_bin_remove(GST_BIN(st->pipeline), elem);
}

void stream_stop_pipeline(struct stream *st) {
	gst_element_set_state(st->pipeline, GST_STATE_NULL);
	stream_remove_element(st, st->src);
	stream_remove_element(st, st->filter);
	stream_remove_element(st, st->jitter);
	stream_remove_element(st, st->demux);
	stream_remove_element(st, st->depay);
	stream_remove_element(st, st->decoder);
	stream_remove_element(st, st->convert);
	stream_remove_element(st, st->freezer);
	stream_remove_element(st, st->videobox);
	stream_remove_element(st, st->sink);
	st->src = NULL;
	st->filter = NULL;
	st->jitter = NULL;
	st->demux = NULL;
	st->depay = NULL;
	st->decoder = NULL;
	st->convert = NULL;
	st->freezer = NULL;
	st->videobox = NULL;
	st->sink = NULL;
}

static void stream_stop(struct stream *st) {
	if (st->stop)
		st->stop(st);
}

static void stream_ack_started(struct stream *st) {
	if (st->ack_started)
		st->ack_started(st);
}

void stream_start_blank(struct stream *st) {
	st->src = make_src_blank();
	st->sink = make_sink(st);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->sink, NULL);
	if (!gst_element_link_many(st->src, st->sink, NULL))
		elog_err("Unable to link elements\n");
	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
}

static void stream_start_png(struct stream *st) {
	st->src = make_src_http(st);
	st->decoder = gst_element_factory_make("pngdec", NULL);
	st->convert = gst_element_factory_make("videoconvert", NULL);
	st->freezer = gst_element_factory_make("imagefreeze", NULL);
	st->videobox = make_videobox();
	st->sink = make_sink(st);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->decoder,
		st->convert, st->freezer, st->videobox, st->sink, NULL);
	if (!gst_element_link_many(st->src, st->decoder, st->convert,
	                           st->freezer, st->videobox, st->sink, NULL))
		elog_err("Unable to link elements\n");

	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
}

static void stream_make_later_elements(struct stream *st) {
	if (strcmp("H264", st->encoding) == 0) {
		st->depay = gst_element_factory_make("rtph264depay", NULL);
		st->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else if (strcmp("MPEG4", st->encoding) == 0) {
		st->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		st->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
	st->videobox = make_videobox();
	st->sink = make_sink(st);
}

static void stream_make_udp_pipe(struct stream *st) {
	st->src = make_src_udp(st);
	st->filter = make_filter(st);
	st->jitter = make_jitter(st);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->filter, st->jitter,
			 st->depay, st->decoder, st->videobox, st->sink, NULL);
	if (!gst_element_link_many(st->src, st->filter, st->jitter, st->depay,
	                           st->decoder, st->videobox, st->sink, NULL))
		elog_err("Unable to link elements\n");
}

static void demux_pad_added_cb(GstElement *demux, GstPad *pad, gpointer data) {
	struct stream *st = (struct stream *) data;
	GstPad *spad = gst_element_get_static_pad(st->depay, "sink");
	gst_pad_link(pad, spad);
	gst_object_unref(spad);
}

static void stream_make_http_pipe(struct stream *st) {
	st->src = make_src_http(st);
	st->demux = make_sdp_demux(st);
	g_signal_connect(st->demux, "pad-added", G_CALLBACK(demux_pad_added_cb),
		st);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->demux, st->depay,
		st->decoder, st->videobox, st->sink, NULL);
	gst_element_link(st->src, st->demux);
	if (!gst_element_link_many(st->depay, st->decoder, st->videobox,
	                           st->sink, NULL))
		elog_err("Unable to link elements\n");
}

static void stream_make_rtsp_pipe(struct stream *st) {
	st->src = make_src_rtsp(st);

	gst_bin_add_many(GST_BIN(st->pipeline), st->src, st->depay, st->decoder,
		st->videobox, st->sink, NULL);
	if (!gst_element_link_many(st->depay, st->decoder, st->videobox,
	                           st->sink, NULL))
		elog_err("Unable to link elements\n");
}

void stream_start_pipeline(struct stream *st) {
	if (strcmp("PNG", st->encoding) == 0) {
		stream_start_png(st);
		return;
	}
	stream_make_later_elements(st);
	if (strncmp("udp", st->location, 3) == 0)
		stream_make_udp_pipe(st);
	else if (strncmp("http", st->location, 4) == 0)
		stream_make_http_pipe(st);
	else
		stream_make_rtsp_pipe(st);
	gst_element_set_state(st->pipeline, GST_STATE_PLAYING);
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
	st->aspect = FALSE;
	memset(st->location, 0, sizeof(st->location));
	memset(st->encoding, 0, sizeof(st->encoding));
	st->latency = DEFAULT_LATENCY;
	st->handle = 0;
	st->pipeline = gst_pipeline_new(name);
	st->bus = gst_pipeline_get_bus(GST_PIPELINE(st->pipeline));
	gst_bus_add_watch(st->bus, bus_cb, st);
	st->src = NULL;
	st->filter = NULL;
	st->jitter = NULL;
	st->demux = NULL;
	st->depay = NULL;
	st->decoder = NULL;
	st->convert = NULL;
	st->freezer = NULL;
	st->videobox = NULL;
	st->sink = NULL;
	st->stop = NULL;
	st->ack_started = NULL;
}

void stream_destroy(struct stream *st) {
	stream_stop_pipeline(st);
	gst_bus_remove_watch(st->bus);
	g_object_unref(st->pipeline);
	st->pipeline = NULL;
}
