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

struct moncell {
	pthread_mutex_t mutex;
	char		name[8];
	char		mid[8];
	char		accent[8];
	GtkWidget	*box;
	GtkWidget	*video;
	GtkWidget	*title;
	GtkWidget	*mon_lbl;
	GtkWidget	*cam_lbl;
	guintptr	handle;
	char		location[128];
	char		description[64];
	char		stype[8];
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*src;
	GstElement	*depay;
	GstElement	*decoder;
	GstElement	*convert;
	GstElement	*freezer;
	GstElement	*videobox;
	GstElement	*sink;
	gboolean	stopped;
};

#define ACCENT_GRAY	"#444444"

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
	g_object_set(G_OBJECT(src), "latency", 50, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "pad-added", G_CALLBACK(source_pad_added_cb), mc);
	return src;
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
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", FALSE, NULL);
	return sink;
}

static void moncell_remove_element(struct moncell *mc, GstElement *elem) {
	if (elem)
		gst_bin_remove(GST_BIN(mc->pipeline), elem);
}

static void moncell_stop_pipeline(struct moncell *mc) {
	gst_element_set_state(mc->pipeline, GST_STATE_NULL);
	moncell_remove_element(mc, mc->src);
	moncell_remove_element(mc, mc->depay);
	moncell_remove_element(mc, mc->decoder);
	moncell_remove_element(mc, mc->convert);
	moncell_remove_element(mc, mc->freezer);
	moncell_remove_element(mc, mc->videobox);
	moncell_remove_element(mc, mc->sink);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->convert = NULL;
	mc->freezer = NULL;
	mc->videobox = NULL;
	mc->sink = NULL;
}

static void moncell_set_accent(struct moncell *mc, const char *accent) {
	GdkRGBA rgba;
	if (gdk_rgba_parse(&rgba, accent)) {
		gtk_widget_override_background_color(mc->title,
			GTK_STATE_FLAG_NORMAL, &rgba);
	}
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
		fprintf(stderr, "Unable to link elements\n");
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
		fprintf(stderr, "Unable to link elements\n");

	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_start_pipeline(struct moncell *mc) {
	if (strcmp("PNG", mc->stype) == 0) {
		moncell_start_png(mc);
		return;
	}
	mc->src = make_src_rtsp(mc);
	if (strcmp("H264", mc->stype) == 0) {
		mc->depay = gst_element_factory_make("rtph264depay", NULL);
		mc->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else {
		mc->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		mc->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
	mc->videobox = make_videobox();
	mc->sink = make_sink(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->depay, mc->decoder,
		mc->videobox, mc->sink, NULL);
	if (!gst_element_link_many(mc->depay, mc->decoder, mc->videobox,
	                           mc->sink, NULL))
		fprintf(stderr, "Unable to link elements\n");

	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_lock(struct moncell *mc) {
	int rc = pthread_mutex_lock(&mc->mutex);
	if (rc)
		fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(rc));
}

static void moncell_unlock(struct moncell *mc) {
	int rc = pthread_mutex_unlock(&mc->mutex);
	if (rc)
		fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(rc));
}

static void moncell_stop_stream(struct moncell *mc) {
	moncell_lock(mc);
	moncell_set_accent(mc, ACCENT_GRAY);
	moncell_stop_pipeline(mc);
	moncell_start_blank(mc);
	mc->stopped = TRUE;
	moncell_unlock(mc);
}

static void moncell_restart_stream(struct moncell *mc) {
	moncell_lock(mc);
	moncell_stop_pipeline(mc);
	moncell_start_pipeline(mc);
	mc->stopped = FALSE;
	moncell_unlock(mc);
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		fprintf(stderr, "End of stream: %s\n", mc->location);
		moncell_stop_stream(mc);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		fprintf(stderr, "Error: %s  %s\n", error->message,
			mc->location);
		g_error_free(error);
		moncell_stop_stream(mc);
		break;
	}
	case GST_MESSAGE_ASYNC_DONE: {
		if (mc->stopped)
			moncell_restart_stream(mc);
		else
			moncell_set_accent(mc, mc->accent);
		break;
	}
	default:
		break;
	}
	return TRUE;
}

static GtkWidget *create_title(void) {
	return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
}

static GtkWidget *create_label(int n_chars) {
	GdkRGBA rgba;
	GtkWidget *lbl = gtk_label_new("");
	gtk_label_set_selectable(GTK_LABEL(lbl), FALSE);
	if (n_chars)
		gtk_label_set_max_width_chars(GTK_LABEL(lbl), n_chars);
	g_object_set(G_OBJECT(lbl), "single-line-mode", TRUE, NULL);
	gtk_widget_override_font(lbl, pango_font_description_from_string(
		"Cantarell Bold 32"));
	gdk_rgba_parse(&rgba, "#FFFFFF");
	gtk_widget_override_color(lbl, GTK_STATE_FLAG_NORMAL, &rgba);
	return lbl;
}

static void moncell_init(struct moncell *mc, uint32_t idx) {
	int rc = pthread_mutex_init(&mc->mutex, NULL);
	if (rc)
		fprintf(stderr, "pthread_mutex_init: %s\n", strerror(rc));
	snprintf(mc->name, 8, "m%d", idx);
	memset(mc->mid, 0, sizeof(mc->mid));
	memset(mc->accent, 0, sizeof(mc->accent));
	memset(mc->location, 0, sizeof(mc->location));
	memset(mc->description, 0, sizeof(mc->description));
	memset(mc->stype, 0, sizeof(mc->stype));
	mc->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	mc->video = gtk_drawing_area_new();
	mc->title = create_title();
	mc->mon_lbl = create_label(6);
	mc->cam_lbl = create_label(0);
	mc->handle = 0;
	mc->pipeline = gst_pipeline_new(mc->name);
	mc->bus = gst_pipeline_get_bus(GST_PIPELINE(mc->pipeline));
	gst_bus_add_watch(mc->bus, bus_cb, mc);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->convert = NULL;
	mc->freezer = NULL;
	mc->videobox = NULL;
	mc->sink = NULL;
	mc->stopped = TRUE;
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
		fprintf(stderr, "pthread_mutex_destroy: %s\n", strerror(rc));
}

static void moncell_set_handle(struct moncell *mc) {
	mc->handle = GDK_WINDOW_XID(gtk_widget_get_window(mc->video));
}

static int32_t moncell_play_stream(struct moncell *mc, const char *loc,
	const char *desc, const char *stype)
{
	moncell_lock(mc);
	moncell_set_location(mc, loc);
	moncell_set_description(mc, desc);
	moncell_set_stype(mc, stype);
	moncell_update_title(mc);
	moncell_unlock(mc);
	/* Stopping the stream will trigger a restart */
	moncell_stop_stream(mc);
	return 1;
}

static void moncell_set_id(struct moncell *mc, const char *mid,
	const char *accent)
{
	strncpy(mc->mid, mid, sizeof(mc->mid));
	mc->accent[0] = '#';
	strncpy(mc->accent + 1, accent, sizeof(mc->accent) - 1);
	moncell_set_accent(mc, mc->accent);
	moncell_update_title(mc);
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
		fprintf(stderr, "ERROR: Grid too large: %d\n", num);
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

void mongrid_set_id(uint32_t idx, const char *mid, const char *accent) {
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_set_id(mc, mid, accent);
	}
}

int32_t mongrid_play_stream(uint32_t idx, const char *loc, const char *desc,
	const char *stype)
{
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_play_stream(mc, loc, desc, stype);
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
