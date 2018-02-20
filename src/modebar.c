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
#include <errno.h>
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

enum btn_req {
	REQ_NONE,
	REQ_PREV,
	REQ_NEXT,
	REQ_IRIS_STOP,
	REQ_IRIS_OPEN,
	REQ_IRIS_CLOSE,
	REQ_FOCUS_STOP,
	REQ_FOCUS_NEAR,
	REQ_FOCUS_FAR,
	REQ_WIPER,
	REQ_OPEN,
	REQ_ENTER,
	REQ_CANCEL,
};

struct modebar {
	struct lock	*lock;
	pthread_t	tid; // status thread ID
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
	enum btn_req    btn_req;
	bool		ptz;
	int16_t		pan;
	int16_t		tilt;
	int16_t		zoom;
	int32_t		accent;
	uint32_t	font_sz;
	bool		online;
	bool		visible;
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
	case GDK_KEY_BackSpace:
		return 8; // ^h
	default:
		return 0;
	}
}

static bool modebar_is_visible(const struct modebar *mbar) {
	return mbar->visible;
}

void modebar_hide(struct modebar *mbar) {
	mbar->visible = false;
	gtk_widget_hide(mbar->box);
}

static void modebar_show(struct modebar *mbar) {
	mbar->visible = true;
	gtk_widget_show_all(mbar->box);
}

static void modebar_backspace(struct modebar *mbar) {
	int l = strlen(mbar->entry);
	if (l > 0)
		mbar->entry[l - 1] = 0;
	modebar_show(mbar);
}

static void modebar_clear_entry(struct modebar *mbar) {
	memset(mbar->entry, 0, sizeof(mbar->entry));
	modebar_show(mbar);
}

static void modebar_set_mon(struct modebar *mbar) {
	strncpy(mbar->mon, mbar->entry, sizeof(mbar->mon));
	memset(mbar->cam, 0, sizeof(mbar->cam));
	memset(mbar->seq, 0, sizeof(mbar->seq));
	modebar_clear_entry(mbar);
}

static void modebar_wake_status(struct modebar *mbar) {
	int rc = pthread_kill(mbar->tid, SIGUSR1);
	if (rc)
		elog_err("pthread_kill: %s\n", strerror(rc));
}

static void modebar_set_cam(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && mbar->tid) {
		strncpy(mbar->cam_req, mbar->entry, sizeof(mbar->cam_req));
		modebar_wake_status(mbar);
	}
	modebar_clear_entry(mbar);
}

static void modebar_set_req(struct modebar *mbar, enum btn_req req) {
	mbar->btn_req = req;
	modebar_wake_status(mbar);
}

static void modebar_set_seq(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && mbar->tid) {
		const char *e = (strlen(mbar->entry)) ? mbar->entry : "pause";
		strncpy(mbar->seq_req, e, sizeof(mbar->seq_req));
		modebar_wake_status(mbar);
	}
	modebar_clear_entry(mbar);
}

static void modebar_set_preset(struct modebar *mbar) {
	if (modebar_has_mon(mbar) && modebar_has_cam(mbar) && mbar->tid) {
		strncpy(mbar->preset_req, mbar->entry,sizeof(mbar->preset_req));
		modebar_wake_status(mbar);
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
		modebar_show(mbar);
	}
	else if ('.' == k)
		modebar_set_mon(mbar);
	else if ('\n' == k)
		modebar_set_cam(mbar);
	else if ('-' == k) {
		modebar_set_req(mbar, REQ_PREV);
		modebar_show(mbar);
	} else if ('+' == k) {
		modebar_set_req(mbar, REQ_NEXT);
		modebar_show(mbar);
	} else if ('*' == k)
		modebar_set_seq(mbar);
	else if ('/' == k)
		modebar_set_preset(mbar);
	else if (8 == k)
		modebar_backspace(mbar);
	else if ('\t' == k) {
		modebar_hide(mbar);
		return;
	}
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
	mbar->accent = 0;
	mbar->font_sz = 32;
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
	modebar_hide(mbar);
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

static void modebar_update_accent(struct modebar *mbar) {
	char css[sizeof(MODEBAR_CSS) + 16];
	GError *err = NULL;
	int32_t a0 = (mbar->online) ? mbar->accent : ACCENT_GRAY;
	int32_t a1 = (a0 >> 1) & 0x7F7F7F; // divide rgb by 2

	snprintf(css, sizeof(css), MODEBAR_CSS, mbar->font_sz, a1, a1,
		COLOR_MON, a0);
	gtk_css_provider_load_from_data(mbar->css_provider, css, -1, &err);
	if (err != NULL)
		elog_err("CSS error: %s\n", err->message);
}

void modebar_set_accent(struct modebar *mbar, int32_t accent, uint32_t font_sz){
	mbar->accent = accent;
	mbar->font_sz = font_sz;
	modebar_update_accent(mbar);
}

#define PTZ_THRESH 8192

static void modebar_set_pan(struct modebar *mbar, int16_t pan) {
	int p = mbar->pan;
	mbar->pan = pan;
	if (pan) {
		mbar->ptz = true;
		if (abs(p - (int) pan) > PTZ_THRESH)
			modebar_wake_status(mbar);
	}
}

static void modebar_set_tilt(struct modebar *mbar, int16_t tilt) {
	int t = mbar->tilt;
	mbar->tilt = tilt;
	if (tilt) {
		mbar->ptz = true;
		if (abs(t - (int) tilt) > PTZ_THRESH)
			modebar_wake_status(mbar);
	}
}

static void modebar_set_zoom(struct modebar *mbar, int16_t zoom) {
	int z = mbar->zoom;
	mbar->zoom = zoom;
	if (zoom) {
		mbar->ptz = true;
		if ((z > 0 && zoom <= 0) || (z < 0 && zoom >= 0))
			modebar_wake_status(mbar);
	}
}

static void modebar_joy_axis(struct modebar *mbar, struct js_event *ev) {
	if (0 == ev->number)
		modebar_set_pan(mbar, ev->value);
	else if (1 == ev->number)
		modebar_set_tilt(mbar, -ev->value);
	else if (2 == ev->number)
		modebar_set_zoom(mbar, ev->value);
}

static void modebar_joy_button_press(struct modebar *mbar, int number) {
	// FIXME: CH Products ID: 068e:00ca
	switch (number) {
	case 0:
		modebar_set_req(mbar, REQ_IRIS_OPEN);
		break;
	case 1:
		modebar_set_req(mbar, REQ_IRIS_CLOSE);
		break;
	case 2:
		modebar_set_req(mbar, REQ_FOCUS_NEAR);
		break;
	case 3:
		modebar_set_req(mbar, REQ_FOCUS_FAR);
		break;
	case 4:
		modebar_set_req(mbar, REQ_WIPER);
		break;
	case 5:
		modebar_set_req(mbar, REQ_OPEN);
		break;
	case 6:
		modebar_set_req(mbar, REQ_ENTER);
		break;
	case 7:
		modebar_set_req(mbar, REQ_CANCEL);
		break;
	case 10:
		modebar_set_req(mbar, REQ_PREV);
		break;
	case 11:
		modebar_set_req(mbar, REQ_NEXT);
		break;
	default:
		break;
	}
}

static void modebar_joy_button_release(struct modebar *mbar, int number) {
	// FIXME: CH Products ID: 068e:00ca
	switch (number) {
	case 0:
	case 1:
		modebar_set_req(mbar, REQ_IRIS_STOP);
		break;
	case 2:
	case 3:
		modebar_set_req(mbar, REQ_FOCUS_STOP);
		break;
	default:
		break;
	}
}

static void modebar_joy_button(struct modebar *mbar, struct js_event *ev) {
	if (ev->value)
		modebar_joy_button_press(mbar, ev->number);
	else
		modebar_joy_button_release(mbar, ev->number);
}

static gboolean do_modebar_show(gpointer data) {
	struct modebar *mbar = (struct modebar *) data;
	modebar_show(mbar);
	return FALSE;
}

void modebar_joy_event(struct modebar *mbar, struct js_event *ev) {
	g_timeout_add(0, do_modebar_show, mbar);
	if (ev->type & JS_EVENT_INIT)
		return;
	if (ev->type & JS_EVENT_AXIS)
		modebar_joy_axis(mbar, ev);
	if (ev->type & JS_EVENT_BUTTON)
		modebar_joy_button(mbar, ev);
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
	mbar->btn_req = REQ_NONE;
	return str;
}

static nstr_t modebar_next(struct modebar *mbar, nstr_t str) {
	char buf[64];
	snprintf(buf, sizeof(buf), "next%c%s%c", UNIT_SEP, mbar->mon,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	mbar->btn_req = REQ_NONE;
	return str;
}

static nstr_t modebar_lens(struct modebar *mbar, nstr_t str, const char *cmd) {
	char buf[64];
	snprintf(buf, sizeof(buf), "lens%c%s%c%s%c%s%c",
		UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->cam,
		UNIT_SEP, cmd,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	mbar->btn_req = REQ_NONE;
	return str;
}

static nstr_t modebar_menu(struct modebar *mbar, nstr_t str, const char *cmd) {
	char buf[64];
	snprintf(buf, sizeof(buf), "menu%c%s%c%s%c%s%c",
		UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->cam,
		UNIT_SEP, cmd,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	mbar->btn_req = REQ_NONE;
	return str;
}

static nstr_t modebar_button(struct modebar *mbar, nstr_t str) {
	switch (mbar->btn_req) {
	case REQ_PREV:
		return modebar_prev(mbar, str);
	case REQ_NEXT:
		return modebar_next(mbar, str);
	case REQ_IRIS_STOP:
		return modebar_lens(mbar, str, "iris_stop");
	case REQ_IRIS_OPEN:
		return modebar_lens(mbar, str, "iris_open");
	case REQ_IRIS_CLOSE:
		return modebar_lens(mbar, str, "iris_close");
	case REQ_FOCUS_STOP:
		return modebar_lens(mbar, str, "focus_stop");
	case REQ_FOCUS_NEAR:
		return modebar_lens(mbar, str, "focus_near");
	case REQ_FOCUS_FAR:
		return modebar_lens(mbar, str, "focus_far");
	case REQ_WIPER:
		return modebar_lens(mbar, str, "wiper");
	case REQ_OPEN:
		return modebar_menu(mbar, str, "open");
	case REQ_ENTER:
		return modebar_menu(mbar, str, "enter");
	case REQ_CANCEL:
		return modebar_menu(mbar, str, "cancel");
	default:
		return str;
	}
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

static nstr_t modebar_ptz(struct modebar *mbar, nstr_t str) {
	float pan = mbar->pan / 32767.0f;
	float tilt = mbar->tilt / 32767.0f;
	float zoom = mbar->zoom / 32767.0f;
	char buf[64];
	snprintf(buf, sizeof(buf), "ptz%c%s%c%s%c%6.4f%c%6.4f%c%6.4f%c",
		UNIT_SEP, mbar->mon,
		UNIT_SEP, mbar->cam,
		UNIT_SEP, pan,
		UNIT_SEP, tilt,
		UNIT_SEP, zoom,
		RECORD_SEP);
	nstr_cat_z(&str, buf);
	if ((0 == mbar->pan) && (0 == mbar->tilt) && (0 == mbar->zoom))
		mbar->ptz = false;
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
	if (modebar_is_visible(mbar) && modebar_has_mon(mbar)) {
		if (strlen(mbar->cam_req))
			return modebar_switch(mbar, str);
		else if (mbar->btn_req != REQ_NONE)
			return modebar_button(mbar, str);
		else if (strlen(mbar->seq_req))
			return modebar_sequence(mbar, str);
		else if (strlen(mbar->preset_req))
			return modebar_preset(mbar, str);
		else if (mbar->ptz)
			return modebar_ptz(mbar, str);
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
	nstr_wrap(mbar->mon, sizeof(mbar->mon), mon);
	nstr_wrap(mbar->cam, sizeof(mbar->cam), cam);
	nstr_wrap(mbar->seq, sizeof(mbar->seq), seq);
	g_timeout_add(0, do_modebar_set_text, mbar);
}

static gboolean do_modebar_update_accent(gpointer data) {
	struct modebar *mbar = data;
	lock_acquire(mbar->lock, __func__);
	modebar_update_accent(mbar);
	lock_release(mbar->lock, __func__);
	return FALSE;
}

void modebar_set_online(struct modebar *mbar, bool online) {
	mbar->online = online;
	g_timeout_add(0, do_modebar_update_accent, mbar);
}
