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
#include "nstr.h"
#include "stream.h"
#include "lock.h"

struct moncell {
	struct stream	stream;          /* must be first, due to casting */
	char		mid[8];          /* monitor ID */
	char		accent[8];       /* accent color for title */
	char		cam_id[20];      /* camera ID */
	char		description[64]; /* location description */
	uint32_t	font_sz;
	GtkCssProvider	*css_provider;
	GtkWidget	*box;
	GtkWidget	*video;
	GtkWidget	*title;
	GtkWidget	*mon_lbl;
	GtkWidget	*stat_lbl;
	GtkWidget	*cam_lbl;
	GtkWidget	*desc_lbl;
	gboolean	started;
	gboolean        failed;
};

struct mongrid {
	struct lock	lock;
	GtkWidget	*window;
	GtkGrid		*grid;
	uint32_t	rows;
	uint32_t	cols;
	struct moncell	*cells;
};

static struct mongrid grid;

static bool is_moncell_valid(struct moncell *mc) {
	int n_cells = grid.rows * grid.cols;
	return mc >= grid.cells && mc < (grid.cells + n_cells);
}

#define ACCENT_GRAY	"444444"

static void moncell_set_cam_id(struct moncell *mc, const char *cam_id) {
	strncpy(mc->cam_id, cam_id, sizeof(mc->cam_id));
}

static void moncell_set_description(struct moncell *mc, const char *desc) {
	strncpy(mc->description, desc, sizeof(mc->description));
}

static const char CSS_FORMAT[] =
	"* { "
		"color: #FFFFFF; "
		"font-family: Cantarell; "
		"font-size: %upt; "
	"}\n"
	"box.title { "
		"margin-top: 1px; "
		"background-color: #%s; "
	"}\n"
	"label {"
		"padding-left: 8px; "
		"padding-right: 8px; "
		"border-right: solid 1px white; "
	"}\n"
	"label#mon_lbl {"
		"color: #FFFF88; "
		"background-color: #%s; "
		"font-weight: Bold; "
		"border-left: solid 1px white; "
	"}\n"
	"label#stat_lbl {"
		"color: #882222; "
		"background-color: #808080; "
	"}\n"
	"label#cam_lbl {"
		"font-weight: Bold; "
	"}\n";

static void moncell_set_accent(struct moncell *mc) {
	char css[sizeof(CSS_FORMAT) + 16];
	GError *err = NULL;
	const char *a0 = (strlen(mc->accent) > 0) ? mc->accent : ACCENT_GRAY;
	const char *a1 = (mc->started) ? a0 : ACCENT_GRAY;

	snprintf(css, sizeof(css), CSS_FORMAT, mc->font_sz, a1, a0);
	gtk_css_provider_load_from_data(mc->css_provider, css, -1, &err);
	if (err != NULL)
		elog_err("CSS error: %s\n", err->message);
}

static void moncell_update_title(struct moncell *mc) {
	gtk_label_set_text(GTK_LABEL(mc->mon_lbl), mc->mid);
	gtk_label_set_text(GTK_LABEL(mc->cam_lbl), mc->cam_id);
	gtk_label_set_text(GTK_LABEL(mc->desc_lbl), mc->description);
}

static void moncell_restart_stream(struct moncell *mc) {
	if (!mc->started) {
		stream_start(&mc->stream);
		mc->started = TRUE;
	}
}

static void moncell_update_accent_title(struct moncell *mc) {
	mc->failed = !mc->started;
	moncell_set_accent(mc);
	moncell_update_title(mc);
}

static gboolean do_update_title(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	lock_acquire(&grid.lock, __func__);
	/* moncell may have been freed while timer ran */
	if (is_moncell_valid(mc))
		moncell_update_accent_title(mc);
	lock_release(&grid.lock, __func__);
	return FALSE;
}

static gboolean do_stop_stream(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	lock_acquire(&grid.lock, __func__);
	/* moncell may have been freed while timer ran */
	if (is_moncell_valid(mc))
		stream_stop(&mc->stream);
	lock_release(&grid.lock, __func__);
	return FALSE;
}

static gboolean do_restart(gpointer data) {
	struct moncell *mc = (struct moncell *) data;
	lock_acquire(&grid.lock, __func__);
	/* moncell may have been freed while timer ran */
	if (is_moncell_valid(mc))
		moncell_restart_stream(mc);
	lock_release(&grid.lock, __func__);
	return FALSE;
}

static void moncell_stop_stream(struct moncell *mc, guint delay) {
	mc->started = FALSE;
	if (grid.window)
		g_timeout_add(0, do_update_title, mc);
	g_timeout_add(0, do_stop_stream, mc);
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

static void moncell_init_gtk(struct moncell *mc) {
	mc->css_provider = gtk_css_provider_new();
	mc->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	mc->video = gtk_drawing_area_new();
	mc->title = create_title(mc);
	mc->mon_lbl = create_label(mc, 6);
	gtk_widget_set_name(mc->mon_lbl, "mon_lbl");
	mc->stat_lbl = create_label(mc, 0);
	gtk_widget_set_name(mc->stat_lbl, "stat_lbl");
	mc->cam_lbl = create_label(mc, 0);
	gtk_widget_set_name(mc->cam_lbl, "cam_lbl");
	mc->desc_lbl = create_label(mc, 0);
	moncell_set_accent(mc);
	moncell_update_title(mc);
	gtk_box_pack_start(GTK_BOX(mc->title), mc->mon_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mc->title), mc->stat_lbl, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(mc->title), mc->desc_lbl, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(mc->title), mc->cam_lbl, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mc->box), mc->video, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(mc->box), mc->title, FALSE, FALSE, 0);
}

static void moncell_init(struct moncell *mc, uint32_t idx) {
	memset(mc, 0, sizeof(struct moncell));
	stream_init(&mc->stream, idx, &grid.lock);
	mc->stream.do_stop = moncell_stop;
	if (grid.window)
		mc->stream.ack_started = moncell_ack_started;
	mc->font_sz = 32;
	if (grid.window)
		moncell_init_gtk(mc);
	mc->started = FALSE;
	mc->failed = TRUE;
}

static void moncell_destroy(struct moncell *mc) {
	stream_destroy(&mc->stream);
	if (grid.window) {
		gtk_widget_destroy(mc->mon_lbl);
		gtk_widget_destroy(mc->stat_lbl);
		gtk_widget_destroy(mc->cam_lbl);
		gtk_widget_destroy(mc->desc_lbl);
		gtk_widget_destroy(mc->video);
		gtk_widget_destroy(mc->title);
		gtk_widget_destroy(mc->box);
	}
}

static void moncell_set_handle(struct moncell *mc) {
	guintptr handle = GDK_WINDOW_XID(gtk_widget_get_window(mc->video));
	stream_set_handle(&mc->stream, handle);
}

static void moncell_play_stream(struct moncell *mc, const char *cam_id,
	const char *loc, const char *desc, const char *encoding,
	uint32_t latency, const char *sprops)
{
	stream_set_id(&mc->stream, cam_id);
	stream_set_location(&mc->stream, loc);
	stream_set_encoding(&mc->stream, encoding);
	stream_set_latency(&mc->stream, latency);
	stream_set_sprops(&mc->stream, sprops);
	moncell_set_cam_id(mc, cam_id);
	moncell_set_description(mc, desc);
	/* Stopping the stream will trigger a restart */
	moncell_stop_stream(mc, 20);
}

static void moncell_set_mon(struct moncell *mc, const char *mid,
	const char *accent, gboolean aspect, uint32_t font_sz)
{
	strncpy(mc->mid, mid, sizeof(mc->mid));
	strncpy(mc->accent, accent, sizeof(mc->accent));
	stream_set_aspect(&mc->stream, aspect);
	mc->font_sz = font_sz;
	if (grid.window)
		g_timeout_add(0, do_update_title, mc);
}

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

static void hide_cursor(GtkWidget *window) {
	GdkDisplay *display;
	GdkCursor *cursor;

	display = gtk_widget_get_display(window);
	cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
	gdk_window_set_cursor(gtk_widget_get_window(window), cursor);
}

static void moncell_update_stats(struct moncell *mc, guint64 lost) {
	char buf[16];
	snprintf(buf, sizeof(buf), "%" G_GUINT64_FORMAT, lost);
	gtk_label_set_text(GTK_LABEL(mc->stat_lbl), buf);
}

static gboolean do_stats(gpointer data) {
	lock_acquire(&grid.lock, __func__);
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			guint64 lost = stream_stats(&mc->stream);
			if (grid.window)
				moncell_update_stats(mc, lost);
		}
	}
	lock_release(&grid.lock, __func__);
	return TRUE;
}

void mongrid_create(bool gui, bool stats) {
	GtkWidget *window;

	gst_init(NULL, NULL);
	lock_init(&grid.lock);
	if (gui) {
		gtk_init(NULL, NULL);
		window = gtk_window_new(0);
		grid.window = window;
		gtk_window_set_title((GtkWindow *) window, "MonStream");
		gtk_window_fullscreen((GtkWindow *) window);
		gtk_widget_realize(window);
		hide_cursor(window);
	} else
		grid.window = NULL;
	if (stats)
		g_timeout_add(1000, do_stats, NULL);
}

static void mongrid_init_gtk(void) {
	grid.grid = (GtkGrid *) gtk_grid_new();
	gtk_grid_set_column_spacing(grid.grid, 4);
	gtk_grid_set_row_spacing(grid.grid, 4);
	gtk_grid_set_column_homogeneous(grid.grid, TRUE);
	gtk_grid_set_row_homogeneous(grid.grid, TRUE);
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			gtk_grid_attach(grid.grid, mc->box, c, r, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(grid.window), (GtkWidget *) grid.grid);
	gtk_widget_show_all(grid.window);
	gtk_widget_realize(grid.window);
	mongrid_set_handles();
}

int32_t mongrid_init(uint32_t num) {
	lock_acquire(&grid.lock, __func__);
	if (num > 16) {
		grid.rows = 0;
		grid.cols = 0;
		elog_err("Grid too large: %d\n", num);
		goto err;
	}
	grid.rows = get_rows(num);
	grid.cols = get_cols(num);
	num = grid.rows * grid.cols;
	grid.cells = calloc(num, sizeof(struct moncell));
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			moncell_init(mc, i);
		}
	}
	if (grid.window)
		mongrid_init_gtk();
	lock_release(&grid.lock, __func__);
	return 0;
err:
	lock_release(&grid.lock, __func__);
	return 1;
}

void mongrid_clear(void) {
	lock_acquire(&grid.lock, __func__);
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			moncell_destroy(mc);
		}
	}
	free(grid.cells);
	grid.cells = NULL;
	grid.rows = 0;
	grid.cols = 0;
	if (grid.window) {
		gtk_container_remove(GTK_CONTAINER(grid.window),
			(GtkWidget *) grid.grid);
	}
	lock_release(&grid.lock, __func__);
}

void mongrid_set_mon(uint32_t idx, const char *mid, const char *accent,
	gboolean aspect, uint32_t font_sz)
{
	lock_acquire(&grid.lock, __func__);
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		moncell_set_mon(mc, mid, accent, aspect, font_sz);
	}
	lock_release(&grid.lock, __func__);
}

void mongrid_play_stream(uint32_t idx, const char *cam_id, const char *loc,
	const char *desc, const char *encoding, uint32_t latency,
	const char *sprops)
{
	lock_acquire(&grid.lock, __func__);
	if (idx < grid.rows * grid.cols) {
		struct moncell *mc = grid.cells + idx;
		moncell_play_stream(mc, cam_id, loc, desc, encoding, latency,
			sprops);
	}
	lock_release(&grid.lock, __func__);
}

void mongrid_destroy(void) {
	mongrid_clear();
	if (grid.window)
		gtk_widget_destroy(grid.window);
	grid.window = NULL;
	lock_destroy(&grid.lock);
}

/* ASCII separators */
static const char RECORD_SEP = '\x1E';
static const char UNIT_SEP = '\x1F';

static nstr_t moncell_status(struct moncell *mc, nstr_t str, uint32_t idx) {
	char buf[64];
	char failed[8] = "";

	if (mc->failed)
		strcpy(failed, "failed");
	snprintf(buf, sizeof(buf), "status%c%d%c%s%c%s%c", UNIT_SEP,
		idx, UNIT_SEP,
		mc->cam_id, UNIT_SEP,
		failed, RECORD_SEP);
	nstr_cat_z(&str, buf);
	return str;
}

nstr_t mongrid_status(nstr_t str) {
	lock_acquire(&grid.lock, __func__);
	for (uint32_t r = 0; r < grid.rows; r++) {
		for (uint32_t c = 0; c < grid.cols; c++) {
			uint32_t i = r * grid.cols + c;
			struct moncell *mc = grid.cells + i;
			str = moncell_status(mc, str, i);
		}
	}
	lock_release(&grid.lock, __func__);
	return str;
}
