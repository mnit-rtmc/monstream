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

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>
#include "elog.h"

struct moncell {
	pthread_mutex_t mutex;
	char		name[8];
	char		mid[8];
	char		accent[8];
	gboolean	aspect;
	uint32_t	font_sz;
	GtkCssProvider	*css_provider;
	GtkWidget	*box;
	GtkWidget	*video;
	GtkWidget	*title;
	GtkWidget	*mon_lbl;
	GtkWidget	*cam_lbl;
	guintptr	handle;
	char		location[128];
	char		description[64];
	char		stype[8];
	uint32_t	latency;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*src;
	GstElement	*filter;
	GstElement	*jitter;
	GstElement	*depay;
	GstElement	*decoder;
	GstElement	*convert;
	GstElement	*freezer;
	GstElement	*videobox;
	GstElement	*sink;
	gboolean	started;
};

#define ACCENT_GRAY	"444444"
#define DEFAULT_LATENCY	(50)

static const uint32_t GST_VIDEO_TEST_SRC_BLACK = 2;

static void moncell_set_location(struct moncell *mc, const char *loc) {
	strncpy(mc->location, loc, sizeof(mc->location));
}

static void moncell_set_description(struct moncell *mc, const char *desc) {
	strncpy(mc->description, desc, sizeof(mc->description));
}

static void moncell_set_stype(struct moncell *mc, const char *stype) {
	strncpy(mc->stype, stype, sizeof(mc->stype));
}

static void moncell_set_latency(struct moncell *mc, uint32_t latency) {
	mc->latency = latency;
}

static GstElement *make_src_blank(void) {
	GstElement *src = gst_element_factory_make("videotestsrc", NULL);
	g_object_set(G_OBJECT(src), "pattern", GST_VIDEO_TEST_SRC_BLACK, NULL);
	return src;
}

static void source_pad_added_cb(GstElement *src, GstPad *pad, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	GstPad *spad = gst_element_get_static_pad(mc->depay, "sink");
	gst_pad_link(pad, spad);
	gst_object_unref(spad);
}

static GstElement *make_src_rtsp(struct moncell *mc) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", mc->location, NULL);
	g_object_set(G_OBJECT(src), "latency", mc->latency, NULL);
	g_object_set(G_OBJECT(src), "timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "tcp-timeout", 1000000, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "pad-added", G_CALLBACK(source_pad_added_cb), mc);
	return src;
}

static GstElement *make_src_udp(struct moncell *mc) {
	GstElement *src = gst_element_factory_make("udpsrc", NULL);
	g_object_set(G_OBJECT(src), "uri", mc->location, NULL);
	// Post GstUDPSrcTimeout messages after 1 second (ns)
	g_object_set(G_OBJECT(src), "timeout", 1000000000, NULL);
	return src;
}

static GstElement *make_filter(struct moncell *mc) {
	GstElement *filter = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = gst_caps_new_empty_simple("application/x-rtp");
	g_object_set(G_OBJECT(filter), "caps", caps, NULL);
	gst_caps_unref(caps);
	return filter;
}

static GstElement *make_jitter(struct moncell *mc) {
	GstElement *jitter = gst_element_factory_make("rtpjitterbuffer", NULL);
	g_object_set(G_OBJECT(jitter), "latency", mc->latency, NULL);
	g_object_set(G_OBJECT(jitter), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(jitter), "max-dropout-time", 1500, NULL);
	return jitter;
}

static GstElement *make_src_http(struct moncell *mc) {
	GstElement *src = gst_element_factory_make("souphttpsrc", NULL);
	g_object_set(G_OBJECT(src), "location", mc->location, NULL);
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

static GstElement *make_sink(struct moncell *mc) {
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, mc->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", mc->aspect, NULL);
	return sink;
}

static void moncell_remove_element(struct moncell *mc, GstElement *elem) {
	if (elem)
		gst_bin_remove(GST_BIN(mc->pipeline), elem);
}

static void moncell_stop_pipeline(struct moncell *mc) {
	gst_element_set_state(mc->pipeline, GST_STATE_NULL);
	moncell_remove_element(mc, mc->src);
	moncell_remove_element(mc, mc->filter);
	moncell_remove_element(mc, mc->jitter);
	moncell_remove_element(mc, mc->depay);
	moncell_remove_element(mc, mc->decoder);
	moncell_remove_element(mc, mc->convert);
	moncell_remove_element(mc, mc->freezer);
	moncell_remove_element(mc, mc->videobox);
	moncell_remove_element(mc, mc->sink);
	mc->src = NULL;
	mc->filter = NULL;
	mc->jitter = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->convert = NULL;
	mc->freezer = NULL;
	mc->videobox = NULL;
	mc->sink = NULL;
}

static const char CSS_FORMAT[] =
	"* { "
		"color: #FFFFFF; "
		"font-family: Cantarell; "
		"font-size: %upt; "
		"font-weight: Bold "
	"}\n"
	"box.title { "
		"background-color: #%s "
	"}";

static void moncell_set_accent(struct moncell *mc, const char *accent) {
	char css[sizeof(CSS_FORMAT) + 8];
	GError *err = NULL;

	snprintf(css, sizeof(css), CSS_FORMAT, mc->font_sz, accent);
	gtk_css_provider_load_from_data(mc->css_provider, css, -1, &err);
	if (err != NULL)
		elog_err("CSS error: %s\n", err->message);
}

static void moncell_update_title(struct moncell *mc) {
	gtk_label_set_text(GTK_LABEL(mc->mon_lbl), mc->mid);
	gtk_label_set_text(GTK_LABEL(mc->cam_lbl), mc->description);
}

static void moncell_start_blank(struct moncell *mc) {
	mc->src = make_src_blank();
	mc->sink = make_sink(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->sink, NULL);
	if (!gst_element_link_many(mc->src, mc->sink, NULL))
		elog_err("Unable to link elements\n");
	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_start_png(struct moncell *mc) {
	mc->src = make_src_http(mc);
	mc->decoder = gst_element_factory_make("pngdec", NULL);
	mc->convert = gst_element_factory_make("videoconvert", NULL);
	mc->freezer = gst_element_factory_make("imagefreeze", NULL);
	mc->videobox = make_videobox();
	mc->sink = make_sink(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->decoder,
		mc->convert, mc->freezer, mc->videobox, mc->sink, NULL);
	if (!gst_element_link_many(mc->src, mc->decoder, mc->convert,
	                           mc->freezer, mc->videobox, mc->sink, NULL))
		elog_err("Unable to link elements\n");

	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_make_later_elements(struct moncell *mc) {
	if (strcmp("H264", mc->stype) == 0) {
		mc->depay = gst_element_factory_make("rtph264depay", NULL);
		mc->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else {
		mc->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		mc->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
	mc->videobox = make_videobox();
	mc->sink = make_sink(mc);
}

static void moncell_make_udp_pipe(struct moncell *mc) {
	mc->src = make_src_udp(mc);
	mc->filter = make_filter(mc);
	mc->jitter = make_jitter(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->filter, mc->jitter,
			 mc->depay, mc->decoder, mc->videobox, mc->sink, NULL);
	if (!gst_element_link_many(mc->src, mc->filter, mc->jitter, mc->depay,
	                           mc->decoder, mc->videobox, mc->sink, NULL))
		elog_err("Unable to link elements\n");
}

static void moncell_make_rtsp_pipe(struct moncell *mc) {
	mc->src = make_src_rtsp(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->depay, mc->decoder,
		mc->videobox, mc->sink, NULL);
	if (!gst_element_link_many(mc->depay, mc->decoder, mc->videobox,
	                           mc->sink, NULL))
		elog_err("Unable to link elements\n");
}

static void moncell_start_pipeline(struct moncell *mc) {
	if (strcmp("PNG", mc->stype) == 0) {
		moncell_start_png(mc);
		return;
	}
	moncell_make_later_elements(mc);
	if (strncmp("udp", mc->location, 3) == 0)
		moncell_make_udp_pipe(mc);
	else
		moncell_make_rtsp_pipe(mc);
	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_lock(struct moncell *mc) {
	int rc = pthread_mutex_lock(&mc->mutex);
	if (rc)
		elog_err("pthread_mutex_lock: %s\n", strerror(rc));
}

static void moncell_unlock(struct moncell *mc) {
	int rc = pthread_mutex_unlock(&mc->mutex);
	if (rc)
		elog_err("pthread_mutex_unlock: %s\n", strerror(rc));
}

static void moncell_restart_stream(struct moncell *mc) {
	moncell_lock(mc);
	if (!mc->started) {
		moncell_stop_pipeline(mc);
		moncell_start_pipeline(mc);
		mc->started = TRUE;
	}
	moncell_unlock(mc);
}

static gboolean do_restart(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	moncell_restart_stream(mc);
	return FALSE;
}

static void moncell_stop_stream(struct moncell *mc, guint delay) {
	moncell_lock(mc);
	moncell_set_accent(mc, ACCENT_GRAY);
	moncell_stop_pipeline(mc);
	moncell_start_blank(mc);
	mc->started = FALSE;
	moncell_unlock(mc);
	/* delay is needed to allow gtk+ to update accent color */
	g_timeout_add(delay, do_restart, mc);
}

static void moncell_ack_started(struct moncell *mc) {
	moncell_lock(mc);
	if (mc->started)
		moncell_set_accent(mc, mc->accent);
	moncell_unlock(mc);
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		elog_err("End of stream: %s\n", mc->location);
		moncell_stop_stream(mc, 500);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		elog_err("Error: %s  %s\n", error->message, mc->location);
		g_error_free(error);
		moncell_stop_stream(mc, 500);
		break;
	}
	case GST_MESSAGE_WARNING: {
		gchar *debug;
		GError *error;
		gst_message_parse_warning(msg, &error, &debug);
		g_free(debug);
		elog_err("Warning: %s  %s\n", error->message, mc->location);
		g_error_free(error);
		break;
	}
	case GST_MESSAGE_ELEMENT:
		if (gst_message_has_name(msg, "GstUDPSrcTimeout")) {
			elog_err("udpsrc timeout -- stopping stream\n");
			moncell_stop_stream(mc, 500);
		}
		break;
	case GST_MESSAGE_ASYNC_DONE:
		moncell_ack_started(mc);
		break;
	default:
		break;
	}
	return TRUE;
}

static GtkWidget *create_title(struct moncell *mc) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	GtkStyleContext *ctx = gtk_widget_get_style_context(box);
	gtk_style_context_add_class(ctx, "title");
	gtk_style_context_add_provider(ctx,
		GTK_STYLE_PROVIDER (mc->css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	return box;
}

static GtkWidget *create_label(struct moncell *mc, int n_chars) {
	GtkWidget *lbl = gtk_label_new("");
	GtkStyleContext *ctx = gtk_widget_get_style_context(lbl);
	gtk_style_context_add_provider(ctx,
		GTK_STYLE_PROVIDER (mc->css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_label_set_selectable(GTK_LABEL(lbl), FALSE);
	if (n_chars)
		gtk_label_set_max_width_chars(GTK_LABEL(lbl), n_chars);
	gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
	g_object_set(G_OBJECT(lbl), "single-line-mode", TRUE, NULL);
	return lbl;
}

static void moncell_init(struct moncell *mc, uint32_t idx) {
	int rc = pthread_mutex_init(&mc->mutex, NULL);
	if (rc)
		elog_err("pthread_mutex_init: %s\n", strerror(rc));
	snprintf(mc->name, 8, "m%d", idx);
	memset(mc->mid, 0, sizeof(mc->mid));
	memset(mc->accent, 0, sizeof(mc->accent));
	mc->aspect = FALSE;
	mc->font_sz = 32;
	memset(mc->location, 0, sizeof(mc->location));
	memset(mc->description, 0, sizeof(mc->description));
	memset(mc->stype, 0, sizeof(mc->stype));
	mc->latency = DEFAULT_LATENCY;
	mc->css_provider = gtk_css_provider_new();
	mc->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	mc->video = gtk_drawing_area_new();
	mc->title = create_title(mc);
	mc->mon_lbl = create_label(mc, 6);
	mc->cam_lbl = create_label(mc, 0);
	mc->handle = 0;
	mc->pipeline = gst_pipeline_new(mc->name);
	mc->bus = gst_pipeline_get_bus(GST_PIPELINE(mc->pipeline));
	gst_bus_add_watch(mc->bus, bus_cb, mc);
	mc->src = NULL;
	mc->filter = NULL;
	mc->jitter = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->convert = NULL;
	mc->freezer = NULL;
	mc->videobox = NULL;
	mc->sink = NULL;
	mc->started = FALSE;
	moncell_set_accent(mc, ACCENT_GRAY);
	moncell_update_title(mc);
	gtk_box_pack_start(GTK_BOX(mc->title), mc->mon_lbl, FALSE, FALSE, 8);
	gtk_box_pack_end(GTK_BOX(mc->title), mc->cam_lbl, FALSE, FALSE, 8);
	gtk_box_pack_start(GTK_BOX(mc->box), mc->video, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(mc->box), mc->title, FALSE, FALSE, 1);
}

static void moncell_destroy(struct moncell *mc) {
	int rc;

	moncell_lock(mc);
	moncell_stop_pipeline(mc);
	gst_bus_remove_watch(mc->bus);
	g_object_unref(mc->pipeline);
	mc->pipeline = NULL;
	moncell_unlock(mc);
	rc = pthread_mutex_destroy(&mc->mutex);
	if (rc)
		elog_err("pthread_mutex_destroy: %s\n", strerror(rc));
}

static void moncell_set_handle(struct moncell *mc) {
	mc->handle = GDK_WINDOW_XID(gtk_widget_get_window(mc->video));
}

static int32_t moncell_play_stream(struct moncell *mc, const char *loc,
	const char *desc, const char *stype, uint32_t latency)
{
	moncell_lock(mc);
	moncell_set_location(mc, loc);
	moncell_set_description(mc, desc);
	moncell_set_stype(mc, stype);
	moncell_set_latency(mc, latency);
	moncell_update_title(mc);
	moncell_unlock(mc);
	/* Stopping the stream will trigger a restart */
	moncell_stop_stream(mc, 20);
	return 1;
}

static void moncell_set_mon(struct moncell *mc, const char *mid,
	const char *accent, gboolean aspect, uint32_t font_sz)
{
	moncell_lock(mc);
	strncpy(mc->mid, mid, sizeof(mc->mid));
	strncpy(mc->accent, accent, sizeof(mc->accent));
	mc->aspect = aspect;
	mc->font_sz = font_sz;
	moncell_set_accent(mc, mc->accent);
	moncell_update_title(mc);
	moncell_unlock(mc);
}

struct mongrid {
	uint32_t	rows;
	uint32_t	cols;
	GtkWidget	*window;
	struct moncell	*cells;
};

static struct mongrid grid;

static void mongrid_set_handles(void) {
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			moncell_set_handle(mc);
		}
	}
}

static uint32_t get_rows(uint32_t num) {
	uint32_t r = 1;
	uint32_t c = 1;
	while (r * c < num) {
		c++;
		if (r * c < num)
			r++;
	}
	return r;
}

static uint32_t get_cols(uint32_t num) {
	uint32_t r = 1;
	uint32_t c = 1;
	while (r * c < num) {
		c++;
		if (r * c < num)
			r++;
	}
	return c;
}

int32_t mongrid_init(uint32_t num) {
	GtkWidget *window;
	GtkGrid *gtk_grid;
	GdkDisplay *display;
	GdkCursor *cursor;
	if (num > 16) {
		grid.rows = 0;
		grid.cols = 0;
		elog_err("Grid too large: %d\n", num);
		return 1;
	}
	grid.rows = get_rows(num);
	grid.cols = get_cols(num);
	num = grid.rows * grid.cols;
	grid.cells = calloc(num, sizeof(struct moncell));
	window = gtk_window_new(0);
	grid.window = window;
	gtk_grid = (GtkGrid *) gtk_grid_new();
	gtk_grid_set_column_spacing(gtk_grid, 4);
	gtk_grid_set_row_spacing(gtk_grid, 4);
	gtk_grid_set_column_homogeneous(gtk_grid, TRUE);
	gtk_grid_set_row_homogeneous(gtk_grid, TRUE);
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			moncell_init(mc, i);
			gtk_grid_attach(gtk_grid, mc->box, c, r, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(window), (GtkWidget *) gtk_grid);
	gtk_window_set_title((GtkWindow *) window, "MonStream");
	gtk_window_fullscreen((GtkWindow *) window);
	gtk_widget_show_all(window);
	gtk_widget_realize(window);
	display = gtk_widget_get_display(window);
	cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
	gdk_window_set_cursor(gtk_widget_get_window(window), cursor);
	mongrid_set_handles();
	return 0;
}

void mongrid_set_mon(uint32_t idx, const char *mid, const char *accent,
	gboolean aspect, uint32_t font_sz)
{
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_set_mon(mc, mid, accent, aspect, font_sz);
	}
}

int32_t mongrid_play_stream(uint32_t idx, const char *loc, const char *desc,
	const char *stype, uint32_t latency)
{
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_play_stream(mc, loc, desc, stype, latency);
	} else
		return 1;
}

void mongrid_destroy(void) {
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			moncell_destroy(mc);
		}
	}
	gtk_widget_destroy(grid.window);
	grid.window = NULL;
	free(grid.cells);
	grid.cells = NULL;
	grid.rows = 0;
	grid.cols = 0;
}
