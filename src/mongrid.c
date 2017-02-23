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
#include <stdlib.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>

struct moncell {
	char		name[8];
	char		mid[8];
	GtkWidget	*widget;
	guintptr	handle;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*src;
	GstElement	*depay;
	GstElement	*decoder;
	GstElement	*videobox;
	GstElement	*mon_overlay;
	GstElement	*txt_overlay;
	GstElement	*sink;
};

static const uint32_t GST_VIDEO_TEST_SRC_BALL = 18;

static GstElement *make_src_blank(void) {
	GstElement *src = gst_element_factory_make("videotestsrc", NULL);
	g_object_set(G_OBJECT(src), "pattern", GST_VIDEO_TEST_SRC_BALL, NULL);
	g_object_set(G_OBJECT(src), "foreground-color", 0x408020, NULL);
	return src;
}

static GstElement *make_src_rtsp(const char *loc) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", loc, NULL);
	g_object_set(G_OBJECT(src), "latency", 2, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	return src;
}

static GstElement *make_videobox(void) {
	GstElement *vbx = gst_element_factory_make("videobox", NULL);
	g_object_set(G_OBJECT(vbx), "bottom", -28, NULL);
	return vbx;
}

enum align {
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT,
};
enum valign {
	VALIGN_BASELINE,
	VALIGN_BOTTOM,
	VALIGN_TOP,
};

static GstElement *make_txt_overlay(const char *desc, enum align a,
	enum valign va)
{
	GstElement *ovl = gst_element_factory_make("textoverlay", NULL);
	g_object_set(G_OBJECT(ovl), "text", desc, NULL);
	g_object_set(G_OBJECT(ovl), "font-desc", "Cantarell, 14", NULL);
	g_object_set(G_OBJECT(ovl), "shaded-background", FALSE, NULL);
	g_object_set(G_OBJECT(ovl), "shading-value", 255, NULL);
	g_object_set(G_OBJECT(ovl), "color", 0xFFFFFFE0, NULL);
	g_object_set(G_OBJECT(ovl), "halignment", a, NULL);
	g_object_set(G_OBJECT(ovl), "valignment", va, NULL);
	g_object_set(G_OBJECT(ovl), "wrap-mode", -1, NULL); // no wrapping
	g_object_set(G_OBJECT(ovl), "xpad", 0, NULL);
	g_object_set(G_OBJECT(ovl), "ypad", 0, NULL);
	return ovl;
}

static GstElement *make_sink(struct moncell *mc) {
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, mc->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", FALSE, NULL);
	return sink;
}

static void moncell_stop_pipeline(struct moncell *mc) {
	gst_element_set_state(mc->pipeline, GST_STATE_NULL);
	if (mc->src)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->src);
	if (mc->depay)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->depay);
	if (mc->decoder)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->decoder);
	if (mc->videobox)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->videobox);
	if (mc->mon_overlay)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->mon_overlay);
	if (mc->txt_overlay)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->txt_overlay);
	if (mc->sink)
		gst_bin_remove(GST_BIN(mc->pipeline), mc->sink);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->videobox = NULL;
	mc->mon_overlay = NULL;
	mc->txt_overlay = NULL;
	mc->sink = NULL;
}

static void moncell_start_blank(struct moncell *mc) {
	mc->src = make_src_blank();
	mc->videobox = make_videobox();
	mc->mon_overlay = make_txt_overlay(mc->mid, ALIGN_LEFT, VALIGN_BOTTOM);
	mc->sink = make_sink(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->videobox,
		mc->mon_overlay, mc->sink, NULL);

	gst_element_link(mc->src, mc->videobox);
	gst_element_link(mc->videobox, mc->mon_overlay);
	gst_element_link(mc->mon_overlay, mc->sink);

	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void on_source_pad_added(GstElement *src, GstPad *pad, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	GstPad *spad = gst_element_get_static_pad(mc->depay, "sink");
	gst_pad_link(pad, spad);
	gst_object_unref(spad);
}

static void moncell_start_pipeline(struct moncell *mc, const char *loc,
	const char *desc, const char *stype)
{
	mc->src = make_src_rtsp(loc);
	if (memcmp(stype, "H264", 4) == 0) {
		mc->depay = gst_element_factory_make("rtph264depay", NULL);
		mc->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else {
		mc->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		mc->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
	mc->videobox = make_videobox();
	mc->mon_overlay = make_txt_overlay(mc->mid, ALIGN_LEFT, VALIGN_BOTTOM);
	mc->txt_overlay = make_txt_overlay(desc, ALIGN_RIGHT, VALIGN_BOTTOM);
	mc->sink = make_sink(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->depay, mc->decoder,
		mc->videobox, mc->mon_overlay, mc->txt_overlay, mc->sink, NULL);
	g_signal_connect(mc->src, "pad-added", G_CALLBACK(on_source_pad_added),
		mc);

	gst_element_link(mc->src, mc->depay);
	gst_element_link(mc->depay, mc->decoder);
	gst_element_link(mc->decoder, mc->videobox);
	gst_element_link(mc->videobox, mc->mon_overlay);
	gst_element_link(mc->mon_overlay, mc->txt_overlay);
	gst_element_link(mc->txt_overlay, mc->sink);

	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		fprintf(stderr, "End of stream\n");
		moncell_stop_pipeline(mc);
		moncell_start_blank(mc);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		fprintf(stderr, "GST Error: %s\n", error->message);
		g_error_free(error);
		moncell_stop_pipeline(mc);
		moncell_start_blank(mc);
		break;
	}
	default:
		break;
	}
	return TRUE;
}

static void moncell_init(struct moncell *mc, uint32_t idx) {
	snprintf(mc->name, 8, "m%d", idx);
	memset(mc->mid, 0, 8);
	mc->widget = gtk_drawing_area_new();
	mc->handle = 0;
	mc->pipeline = gst_pipeline_new(mc->name);
	mc->bus = gst_pipeline_get_bus(GST_PIPELINE(mc->pipeline));
	gst_bus_add_watch(mc->bus, bus_call, mc);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->videobox = NULL;
	mc->mon_overlay = NULL;
	mc->txt_overlay = NULL;
	mc->sink = NULL;
}

static void moncell_set_handle(struct moncell *mc) {
	mc->handle = GDK_WINDOW_XID(gtk_widget_get_window(mc->widget));
}

static int32_t moncell_play_stream(struct moncell *mc, const char *loc,
	const char *desc, const char *stype)
{
	moncell_stop_pipeline(mc);
	moncell_start_pipeline(mc, loc, desc, stype);
	return 1;
}

static void moncell_set_id(struct moncell *mc, const char *mid) {
	strncpy(mc->mid, mid, 8);
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
	gst_init(NULL, NULL);
	gtk_init(NULL, NULL);
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
			gtk_grid_attach(gtk_grid, mc->widget, c, r, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(window), (GtkWidget *) gtk_grid);
	gtk_window_set_title((GtkWindow *) window, "MonStream");
	gtk_window_fullscreen((GtkWindow *) window);
	gtk_widget_show_all(window);
	gtk_widget_realize(window);
	mongrid_set_handles();
	return 0;
}

void mongrid_set_id(uint32_t idx, const char *mid) {
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_set_id(mc, mid);
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
