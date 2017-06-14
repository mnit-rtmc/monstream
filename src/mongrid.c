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
#include "elog.h"
#include "stream.h"

struct moncell {
	struct stream	stream;	/* must be first member, due to casting */
	char		mid[8];
	char		accent[8];
	char		description[64];
	uint32_t	font_sz;
	GtkCssProvider	*css_provider;
	GtkWidget	*box;
	GtkWidget	*video;
	GtkWidget	*title;
	GtkWidget	*mon_lbl;
	GtkWidget	*cam_lbl;
	gboolean	started;
};

#define ACCENT_GRAY	"444444"

static void moncell_set_description(struct moncell *mc, const char *desc) {
	strncpy(mc->description, desc, sizeof(mc->description));
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

static void moncell_restart_stream(struct moncell *mc) {
	stream_lock(&mc->stream);
	if (!mc->started) {
		stream_start(&mc->stream);
		mc->started = TRUE;
	}
	stream_unlock(&mc->stream);
}

static gboolean do_update_title(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	stream_lock(&mc->stream);
	if (mc->started)
		moncell_set_accent(mc, mc->accent);
	else
		moncell_set_accent(mc, ACCENT_GRAY);
	moncell_update_title(mc);
	stream_unlock(&mc->stream);
	return FALSE;
}

static gboolean do_restart(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	moncell_restart_stream(mc);
	return FALSE;
}

static void moncell_stop_stream(struct moncell *mc, guint delay) {
	g_timeout_add(0, do_update_title, mc);
	stream_stop(&mc->stream);
	mc->started = FALSE;
	/* delay is needed to allow gtk+ to update accent color */
	g_timeout_add(delay, do_restart, mc);
}

static void moncell_stop(struct stream *st) {
	/* Cast requires stream is first member of struct */
	struct moncell *mc = (struct moncell *) st;
	moncell_stop_stream(mc, 1000);
}

static void moncell_ack_started(struct stream *st) {
	/* Cast requires stream is first member of struct */
	struct moncell *mc = (struct moncell *) st;
	g_timeout_add(0, do_update_title, mc);
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
	stream_init(&mc->stream, idx);
	mc->stream.do_stop = moncell_stop;
	mc->stream.ack_started = moncell_ack_started;
	memset(mc->mid, 0, sizeof(mc->mid));
	memset(mc->accent, 0, sizeof(mc->accent));
	memset(mc->description, 0, sizeof(mc->description));
	mc->font_sz = 32;
	mc->css_provider = gtk_css_provider_new();
	mc->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	mc->video = gtk_drawing_area_new();
	mc->title = create_title(mc);
	mc->mon_lbl = create_label(mc, 6);
	mc->cam_lbl = create_label(mc, 0);
	mc->started = FALSE;

	moncell_set_accent(mc, ACCENT_GRAY);
	moncell_update_title(mc);
	gtk_box_pack_start(GTK_BOX(mc->title), mc->mon_lbl, FALSE, FALSE, 8);
	gtk_box_pack_end(GTK_BOX(mc->title), mc->cam_lbl, FALSE, FALSE, 8);
	gtk_box_pack_start(GTK_BOX(mc->box), mc->video, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(mc->box), mc->title, FALSE, FALSE, 1);
}

static void moncell_destroy(struct moncell *mc) {
	stream_destroy(&mc->stream);
}

static void moncell_set_handle(struct moncell *mc) {
	guintptr handle = GDK_WINDOW_XID(gtk_widget_get_window(mc->video));
	stream_set_handle(&mc->stream, handle);
}

static int32_t moncell_play_stream(struct moncell *mc, const char *loc,
	const char *desc, const char *encoding, uint32_t latency)
{
	stream_lock(&mc->stream);
	stream_set_location(&mc->stream, loc);
	stream_set_encoding(&mc->stream, encoding);
	stream_set_latency(&mc->stream, latency);
	moncell_set_description(mc, desc);
	/* Stopping the stream will trigger a restart */
	moncell_stop_stream(mc, 20);
	stream_unlock(&mc->stream);
	return 1;
}

static void moncell_set_mon(struct moncell *mc, const char *mid,
	const char *accent, gboolean aspect, uint32_t font_sz)
{
	stream_lock(&mc->stream);
	strncpy(mc->mid, mid, sizeof(mc->mid));
	strncpy(mc->accent, accent, sizeof(mc->accent));
	stream_set_aspect(&mc->stream, aspect);
	mc->font_sz = font_sz;
	g_timeout_add(0, do_update_title, mc);
	stream_unlock(&mc->stream);
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
	const char *encoding, uint32_t latency)
{
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		return moncell_play_stream(mc, loc, desc, encoding, latency);
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
