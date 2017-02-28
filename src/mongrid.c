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
#include <gst/video/video.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>

static const int BOX_HEIGHT = 54;

struct moncell {
	char		name[8];
	char		mid[8];
	double		acc_red;
	double		acc_green;
	double		acc_blue;
	GtkWidget	*widget;
	guintptr	handle;
	int		width;
	int		height;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*src;
	GstElement	*depay;
	GstElement	*decoder;
	GstElement	*scaler;
	GstElement	*filter;
	GstElement	*videobox;
	GstElement	*convert0;
	GstElement	*draw_overlay;
	GstElement	*mon_overlay;
	GstElement	*txt_overlay;
	GstElement	*convert1;
	GstElement	*sink;
};

static const uint32_t GST_VIDEO_TEST_SRC_BLACK = 2;

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

static GstElement *make_src_rtsp(struct moncell *mc, const char *loc) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", loc, NULL);
	g_object_set(G_OBJECT(src), "latency", 2, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	g_signal_connect(src, "pad-added", G_CALLBACK(source_pad_added_cb), mc);
	return src;
}

static GstElement *make_scaler(void) {
	return gst_element_factory_make("videoscale", NULL);
}

static GstElement *make_filter(struct moncell *mc) {
	GstElement *flt = gst_element_factory_make("capsfilter", NULL);
	GstCaps *caps = gst_caps_new_simple("video/x-raw",
		"width", G_TYPE_INT, mc->width - 2,
		"height", G_TYPE_INT, mc->height - BOX_HEIGHT - 2, NULL);
	g_object_set(G_OBJECT(flt), "caps", caps, NULL);
	gst_caps_unref(caps);
	return flt;
}

static GstElement *make_videobox(void) {
	GstElement *vbx = gst_element_factory_make("videobox", NULL);
	g_object_set(G_OBJECT(vbx), "top", -1, NULL);
	g_object_set(G_OBJECT(vbx), "bottom", -BOX_HEIGHT - 1, NULL);
	g_object_set(G_OBJECT(vbx), "left", -1, NULL);
	g_object_set(G_OBJECT(vbx), "right", -1, NULL);
	return vbx;
}

static GstElement *make_convert(void) {
	return gst_element_factory_make("videoconvert", NULL);
}

static void prepare_draw_cb(GstElement *ovl, GstCaps *caps, gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	GstVideoInfo info;

	if (gst_video_info_from_caps(&info, caps) &&
	   (mc->width != info.width || mc->height != info.height))
	{
		fprintf(stderr, "xvimage: %dx%d\n", mc->width, mc->height);
		fprintf(stderr, "draw: %dx%d\n", info.width, info.height);
	}
}

static void do_draw_cb(GstElement *ovl, cairo_t *cr, guint64 timestamp,
	guint64 duration, gpointer data)
{
	struct moncell *mc = (struct moncell *) data;

	if (mc->width <= 0 || mc->height < BOX_HEIGHT)
		return;

	int top = mc->height - BOX_HEIGHT + 2;
	int bottom = mc->height - 2;
	int left = 2;
	int right = mc->width - 2;

	cairo_move_to(cr, left, top);
	cairo_line_to(cr, right, top);
	cairo_line_to(cr, right, bottom);
	cairo_line_to(cr, left, bottom);
	cairo_set_source_rgba(cr, mc->acc_red, mc->acc_green, mc->acc_blue, 1);
	cairo_fill(cr);
}

static GstElement *make_draw(struct moncell *mc) {
	GstElement *ovl = gst_element_factory_make("cairooverlay", NULL);
	g_signal_connect(ovl, "caps-changed", G_CALLBACK(prepare_draw_cb), mc);
	g_signal_connect(ovl, "draw", G_CALLBACK(do_draw_cb), mc);
	return ovl;
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
	g_object_set(G_OBJECT(ovl), "font-desc", "Cantarell, 22", NULL);
	g_object_set(G_OBJECT(ovl), "shaded-background", FALSE, NULL);
	g_object_set(G_OBJECT(ovl), "shading-value", 255, NULL);
	g_object_set(G_OBJECT(ovl), "color", 0xFFFFFFE0, NULL);
	g_object_set(G_OBJECT(ovl), "halignment", a, NULL);
	g_object_set(G_OBJECT(ovl), "valignment", va, NULL);
	g_object_set(G_OBJECT(ovl), "wrap-mode", -1, NULL); // no wrapping
	g_object_set(G_OBJECT(ovl), "xpad", 8, NULL);
	g_object_set(G_OBJECT(ovl), "ypad", 0, NULL);
	return ovl;
}

static GstElement *make_sink(struct moncell *mc) {
	guint64 width, height;
	GstElement *sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(sink);
	gst_video_overlay_set_window_handle(overlay, mc->handle);
	g_object_set(G_OBJECT(sink), "force-aspect-ratio", FALSE, NULL);
	g_object_get(G_OBJECT(sink), "window-width", &width,
	                             "window-height", &height, NULL);
	mc->width = width;
	mc->height = height;
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
	moncell_remove_element(mc, mc->scaler);
	moncell_remove_element(mc, mc->filter);
	moncell_remove_element(mc, mc->videobox);
	moncell_remove_element(mc, mc->convert0);
	moncell_remove_element(mc, mc->draw_overlay);
	moncell_remove_element(mc, mc->mon_overlay);
	moncell_remove_element(mc, mc->txt_overlay);
	moncell_remove_element(mc, mc->convert1);
	moncell_remove_element(mc, mc->sink);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->scaler = NULL;
	mc->filter = NULL;
	mc->videobox = NULL;
	mc->convert0 = NULL;
	mc->draw_overlay = NULL;
	mc->mon_overlay = NULL;
	mc->txt_overlay = NULL;
	mc->convert1 = NULL;
	mc->sink = NULL;
	mc->width = 0;
	mc->height = 0;
}

static void moncell_start_blank(struct moncell *mc) {
	mc->src = make_src_blank();
	mc->scaler = make_scaler();
	mc->videobox = make_videobox();
	mc->convert0 = make_convert();
	mc->draw_overlay = make_draw(mc);
	mc->mon_overlay = make_txt_overlay(mc->mid, ALIGN_LEFT, VALIGN_BOTTOM);
	mc->convert1 = make_convert();
	mc->sink = make_sink(mc);
	// NOTE: after sink to get width/height
	mc->filter = make_filter(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->scaler, mc->filter,
		mc->videobox, mc->convert0, mc->draw_overlay, mc->mon_overlay,
		mc->convert1, mc->sink, NULL);
	if (!gst_element_link_many(mc->src, mc->scaler, mc->filter,
	                           mc->videobox, mc->convert0, mc->draw_overlay,
	                           mc->mon_overlay, mc->convert1, mc->sink,
	                           NULL))
		fprintf(stderr, "Unable to link elements\n");
	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static void moncell_start_pipeline(struct moncell *mc, const char *loc,
	const char *desc, const char *stype)
{
	mc->src = make_src_rtsp(mc, loc);
	if (memcmp(stype, "H264", 4) == 0) {
		mc->depay = gst_element_factory_make("rtph264depay", NULL);
		mc->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else {
		mc->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		mc->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
	mc->scaler = make_scaler();
	mc->videobox = make_videobox();
	mc->convert0 = make_convert();
	mc->draw_overlay = make_draw(mc);
	mc->mon_overlay = make_txt_overlay(mc->mid, ALIGN_LEFT, VALIGN_BOTTOM);
	mc->txt_overlay = make_txt_overlay(desc, ALIGN_RIGHT, VALIGN_BOTTOM);
	mc->convert1 = make_convert();
	mc->sink = make_sink(mc);
	// NOTE: after sink to get width/height
	mc->filter = make_filter(mc);

	gst_bin_add_many(GST_BIN(mc->pipeline), mc->src, mc->depay, mc->decoder,
		mc->scaler, mc->filter, mc->videobox, mc->convert0,
		mc->draw_overlay, mc->mon_overlay, mc->txt_overlay,
		mc->convert1, mc->sink, NULL);
	if (!gst_element_link_many(mc->depay, mc->decoder, mc->scaler,
	                           mc->filter, mc->videobox, mc->convert0,
	                           mc->draw_overlay, mc->mon_overlay,
	                           mc->txt_overlay, mc->convert1, mc->sink,
	                           NULL))
		fprintf(stderr, "Unable to link elements\n");
	gst_element_set_state(mc->pipeline, GST_STATE_PLAYING);
}

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data) {
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
	mc->acc_red = 0.0;
	mc->acc_green = 0.2;
	mc->acc_blue = 0.0;
	mc->widget = gtk_drawing_area_new();
	mc->handle = 0;
	mc->width = 0;
	mc->height = 0;
	mc->pipeline = gst_pipeline_new(mc->name);
	mc->bus = gst_pipeline_get_bus(GST_PIPELINE(mc->pipeline));
	gst_bus_add_watch(mc->bus, bus_cb, mc);
	mc->src = NULL;
	mc->depay = NULL;
	mc->decoder = NULL;
	mc->scaler = NULL;
	mc->filter = NULL;
	mc->videobox = NULL;
	mc->convert0 = NULL;
	mc->draw_overlay = NULL;
	mc->mon_overlay = NULL;
	mc->txt_overlay = NULL;
	mc->convert1 = NULL;
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

static void moncell_set_id(struct moncell *mc, const char *mid,
	uint32_t accent)
{
	strncpy(mc->mid, mid, 8);
	mc->acc_red   = ((accent >> 16) & 0xFF) / 256.0;
	mc->acc_green = ((accent >>  8) & 0xFF) / 256.0;
	mc->acc_blue  = ((accent >>  0) & 0xFF) / 256.0;
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
	display = gtk_widget_get_display(window);
	cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
	gdk_window_set_cursor(gtk_widget_get_window(window), cursor);
	mongrid_set_handles();
	return 0;
}

void mongrid_set_id(uint32_t idx, const char *mid, uint32_t accent) {
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
