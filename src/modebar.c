/*
 * Copyright (C) 2018  Minnesota Department of Transportation
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "elog.h"
#include "modebar.h"

#define ACCENT_GRAY	0x444444
#define COLOR_MON	0xFFFF88

struct modecell {
	GtkWidget	*box;
	GtkWidget	*key;
	GtkWidget	*lbl;
};

#define MODECELL_MON	0
#define MODECELL_CAM	1
#define MODECELL_ENT	2
#define MODECELL_SEQ	3
#define MODECELL_PRESET	4
#define MODECELL_LAST	5

struct modebar {
	struct lock	*lock;
	pthread_t	tid;
	GtkWidget	*box;
	GtkCssProvider	*css_provider;
	struct modecell cells[MODECELL_LAST];
	char		entry[6];
	char		mon[6];
	char		cam[6];
	char            seq[6];
	char            cam_req[6];
	char            seq_req[6];
	char            preset_req[6];
	bool		prev_req;
	bool		next_req;
};

static GtkWidget *create_label(GtkCssProvider *css_provider, const char *name,
	int n_chars)
{
	GtkWidget *lbl = gtk_label_new("");
	GtkStyleContext *ctx = gtk_widget_get_style_context(lbl);
	gtk_style_context_add_provider(ctx, GTK_STYLE_PROVIDER (css_provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	gtk_widget_set_name(lbl, name);
	gtk_label_set_selectable(GTK_LABEL(lbl), FALSE);
	if (n_chars)
		gtk_label_set_max_width_chars(GTK_LABEL(lbl), n_chars);
	gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
	g_object_set(G_OBJECT(lbl), "single-line-mode", TRUE, NULL);
	return lbl;
}

static void modecell_init(struct modecell *mcell, GtkCssProvider *css_provider,
	const char *name, const char *k)
{
	mcell->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	mcell->key = create_label(css_provider, "key_lbl", 0);
	mcell->lbl = create_label(css_provider, name, 0);
	gtk_label_set_text(GTK_LABEL(mcell->key), k);
	gtk_label_set_xalign(GTK_LABEL(mcell->lbl), 0.0);
	gtk_box_pack_start(GTK_BOX(mcell->box), GTK_WIDGET(mcell->key), FALSE,
		FALSE, 0);
	gtk_box_pack_start(GTK_BOX(mcell->box), GTK_WIDGET(mcell->lbl), TRUE,
		TRUE, 0);
}

static void modebar_add_cell(struct modebar *mbar, int n_cell, const char *name,
	const char *k)
{
	assert(n_cell >= 0 && n_cell < MODECELL_LAST);
	struct modecell *mcell = mbar->cells + n_cell;
	modecell_init(mcell, mbar->css_provider, name, k);
	gtk_box_pack_start(GTK_BOX(mbar->box), GTK_WIDGET(mcell->box), TRUE,
		TRUE, 0);
}

static void modebar_set_text2(struct modebar *mbar, int n_cell, const char *t) {
	assert(n_cell >= 0 && n_cell < MODECELL_LAST);
	struct modecell *mcell = mbar->cells + n_cell;
	gtk_label_set_text(GTK_LABEL(mcell->lbl), t);
}

bool modebar_has_mon(const struct modebar *mbar) {
	return strlen(mbar->mon);
}

static bool modebar_has_cam(const struct modebar *mbar) {
	return strlen(mbar->cam);
}

static void modebar_set_text(struct modebar *mbar) {
	char buf[10];
	bool has_cam = modebar_has_cam(mbar);
	snprintf(buf, sizeof(buf), "Mon %s", mbar->mon);
	modebar_set_text2(mbar, MODECELL_MON, buf);
	snprintf(buf, sizeof(buf), "Cam %s", mbar->cam);
	modebar_set_text2(mbar, MODECELL_CAM, has_cam ? buf : "");
	snprintf(buf, sizeof(buf), "%s_", mbar->entry);
	modebar_set_text2(mbar, MODECELL_ENT, buf);
	snprintf(buf, sizeof(buf), "Seq %s", mbar->seq);
	modebar_set_text2(mbar, MODECELL_SEQ, has_cam ? buf : "");
	modebar_set_text2(mbar, MODECELL_PRESET, has_cam ? "Preset" : "");
}

static char get_key_char(const GdkEventKey *key) {
	switch (key->keyval) {
	case GDK_KEY_0:
	case GDK_KEY_KP_0:
	case GDK_KEY_KP_Insert:
		return '0';
	case GDK_KEY_1:
	case GDK_KEY_KP_1:
	case GDK_KEY_KP_End:
		return '1';
	case GDK_KEY_2:
	case GDK_KEY_KP_2:
	case GDK_KEY_KP_Down:
		return '2';
	case GDK_KEY_3:
	case GDK_KEY_KP_3:
	case GDK_KEY_KP_Page_Down:
		return '3';
	case GDK_KEY_4:
	case GDK_KEY_KP_4:
	case GDK_KEY_KP_Left:
		return '4';
	case GDK_KEY_5:
	case GDK_KEY_KP_5:
	case GDK_KEY_KP_Begin:
		return '5';
	case GDK_KEY_6:
	case GDK_KEY_KP_6:
	case GDK_KEY_KP_Right:
		return '6';
	case GDK_KEY_7:
	case GDK_KEY_KP_7:
	case GDK_KEY_KP_Home:
		return '7';
	case GDK_KEY_8:
	case GDK_KEY_KP_8:
	case GDK_KEY_KP_Up:
		return '8';
	case GDK_KEY_9:
	case GDK_KEY_KP_9:
	case GDK_KEY_KP_Page_Up:
		return '9';
	case GDK_KEY_period:
	case GDK_KEY_KP_Decimal:
	case GDK_KEY_KP_Delete:
		return '.';
	case GDK_KEY_slash:
	case GDK_KEY_KP_Divide:
		return '/';
	case GDK_KEY_asterisk:
	case GDK_KEY_KP_Multiply:
		return '*';
	case GDK_KEY_minus:
	case GDK_KEY_KP_Subtract:
		return '-';
	case GDK_KEY_plus:
	case GDK_KEY_KP_Add:
		return '+';
	case GDK_KEY_Tab:
	case GDK_KEY_KP_Tab:
		return '\t';
	case GDK_KEY_Return:
	case GDK_KEY_KP_Enter:
		return '\n';
	default:
		return 0;
	}
}

static void modebar_clear_entry(struct modebar *mbar) {
	memset(mbar->entry, 0, sizeof(mbar->entry));
}

static void modebar_set_mon(struct modebar *mbar) {
	strncpy(mbar->mon, mbar->entry, sizeof(mbar->mon));
	memset(mbar->cam, 0, sizeof(mbar->cam));
	memset(mbar->seq, 0, sizeof(mbar->seq));
	modebar_clear_entry(mbar);
}

static void modebar_set_cam(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && mbar->tid) {
		strncpy(mbar->cam_req, mbar->entry, sizeof(mbar->cam_req));
		pthread_kill(mbar->tid, SIGUSR1);
	}
	modebar_clear_entry(mbar);
}

static void modebar_set_seq(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && mbar->tid) {
		strncpy(mbar->seq_req, mbar->entry, sizeof(mbar->seq_req));
		pthread_kill(mbar->tid, SIGUSR1);
	}
	modebar_clear_entry(mbar);
}

static void modebar_set_preset(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && modebar_has_cam(mbar) && mbar->tid) {
		strncpy(mbar->preset_req, mbar->entry,sizeof(mbar->preset_req));
		pthread_kill(mbar->tid, SIGUSR1);
	}
	modebar_clear_entry(mbar);
}

static void modebar_press(struct modebar *mbar, GdkEventKey *key) {
	char k = get_key_char(key);
	if (k >= '0' && k <= '9') {
		int len = strlen(mbar->entry);
		if (1 == len && '0' == mbar->entry[0])
			len = 0;
		if (len + 1 < sizeof(mbar->entry)) {
			mbar->entry[len] = k;
			mbar->entry[len + 1] = 0;
		}
	}
	else if ('.' == k)
		modebar_set_mon(mbar);
	else if ('\n' == k)
		modebar_set_cam(mbar);
	else if ('-' == k)
		mbar->prev_req = true;
	else if ('+' == k)
		mbar->next_req = true;
	else if ('*' == k)
		modebar_set_seq(mbar);
	else if ('/' == k)
		modebar_set_preset(mbar);
	modebar_set_text(mbar);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *key, gpointer data) {
	struct modebar *mbar = data;
	modebar_press(mbar, key);
	return FALSE;
}

struct modebar *modebar_create(GtkWidget *window, struct lock *lock) {
	struct modebar *mbar = malloc(sizeof(struct modebar));
	memset(mbar, 0, sizeof(struct modebar));
	mbar->lock = lock;
	mbar->css_provider = gtk_css_provider_new();
	mbar->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
	gtk_box_set_homogeneous(GTK_BOX(mbar->box), TRUE);
	modebar_add_cell(mbar, MODECELL_MON, "mon_lbl", ".");
	modebar_add_cell(mbar, MODECELL_CAM, "bar_lbl", " ");
	modebar_add_cell(mbar, MODECELL_ENT, "ent_lbl", " ");
	modebar_add_cell(mbar, MODECELL_SEQ, "bar_lbl", "*");
	modebar_add_cell(mbar, MODECELL_PRESET, "bar_lbl", "/");
	g_object_set(G_OBJECT(mbar->box), "spacing", 8, NULL);
	g_signal_connect(G_OBJECT(window), "key_press_event",
		G_CALLBACK(key_press), mbar);
	modebar_set_mon(mbar);
	modebar_set_text(mbar);
	return mbar;
}

void modebar_set_tid(struct modebar *mbar, pthread_t tid) {
	mbar->tid = tid;
}

GtkWidget *modebar_get_box(struct modebar *mbar) {
	return mbar->box;
}

static const char MODEBAR_CSS[] =
	"* { "
		"color: white; "
		"font-family: Overpass; "
		"font-size: %upt; "
	"}\n"
	"box { "
		"margin-top: 1px; "
	"}\n"
	"label {"
		"background-color: #%06x; "
		"padding-left: 8px; "
		"padding-right: 8px; "
	"}\n"
	"label#key_lbl {"
		"color: #%06x; "
		"background-color: white; "
	"}\n"
	"label#mon_lbl {"
		"color: #%06x; "
		"font-weight: Bold; "
	"}\n"
	"label#ent_lbl {"
		"background-color: #%06x; "
		"font-weight: Bold; "
	"}\n";

void modebar_set_accent(struct modebar *mbar, int32_t accent, uint32_t font_sz){
	char css[sizeof(MODEBAR_CSS) + 16];
	GError *err = NULL;
	int32_t a0 = (accent > 0) ? accent : ACCENT_GRAY;
	int32_t a1 = (a0 >> 1) & 0x7F7F7F; // divide rgb by 2

	snprintf(css, sizeof(css), MODEBAR_CSS, font_sz, a1, a1, COLOR_MON, a0);
	gtk_css_provider_load_from_data(mbar->css_provider, css, -1, &err);
	if (err != NULL)
		elog_err("CSS error: %s\n", err->message);
}

/* ASCII separators */
static const char RECORD_SEP = '\x1E';
static const char UNIT_SEP = '\x1F';

static nstr_t modebar_switch(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "switch%c%s%c%s%c", UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->cam_req, RECORD_SEP);
	nstr_cat_z(&str, buf);
	memset(mbar->cam_req, 0, sizeof(mbar->cam_req));
	return str;
}

static nstr_t modebar_prev(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "previous%c%s%c", UNIT_SEP, mbar->mon,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	mbar->prev_req = false;
	return str;
}

static nstr_t modebar_next(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "next%c%s%c", UNIT_SEP, mbar->mon,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	mbar->next_req = false;
	return str;
}

static nstr_t modebar_sequence(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "sequence%c%s%c%s%c", UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->seq_req, RECORD_SEP);
	nstr_cat_z(&str, buf);
	memset(mbar->seq_req, 0, sizeof(mbar->seq_req));
	return str;
}

static nstr_t modebar_preset(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "preset%c%s%c%s%c%s%c%s%c",
		UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->cam,
		UNIT_SEP, "recall",
		UNIT_SEP, mbar->preset_req,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	memset(mbar->preset_req, 0, sizeof(mbar->preset_req));
	return str;
}

static nstr_t modebar_query(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "query%c%s%c", UNIT_SEP, mbar->mon,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	return str;
}

nstr_t modebar_status(struct modebar *mbar, nstr_t str) {
	if (modebar_has_mon(mbar)) {
		if (strlen(mbar->cam_req))
			return modebar_switch(mbar, str);
		else if (mbar->prev_req)
			return modebar_prev(mbar, str);
		else if (mbar->next_req)
			return modebar_next(mbar, str);
		else if (strlen(mbar->seq_req))
			return modebar_sequence(mbar, str);
		else if (strlen(mbar->preset_req))
			return modebar_preset(mbar, str);
		else
			return modebar_query(mbar, str);
	} else
		return str;
}

static gboolean do_modebar_set_text(gpointer data) {
	struct modebar *mbar = data;
	lock_acquire(mbar->lock, __func__);
	modebar_set_text(mbar);
	lock_release(mbar->lock, __func__);
	return FALSE;
}

void modebar_display(struct modebar *mbar, nstr_t mon, nstr_t cam, nstr_t seq) {
	if (nstr_cmp_z(mon, mbar->mon)) {
		nstr_wrap(mbar->cam, sizeof(mbar->cam), cam);
		nstr_wrap(mbar->seq, sizeof(mbar->seq), seq);
	} else {
		// Invalid monitor number
		memset(mbar->mon, 0, sizeof(mbar->mon));
		memset(mbar->cam, 0, sizeof(mbar->cam));
		memset(mbar->seq, 0, sizeof(mbar->seq));
	}
	g_timeout_add(0, do_modebar_set_text, mbar);
}
