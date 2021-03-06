#ifndef MODEBAR_H
#define MODEBAR_H

#include <gtk/gtk.h>
#include <linux/joystick.h>
#include "nstr.h"
#include "lock.h"

struct modebar *modebar_create(GtkWidget *window, struct lock *lock);
GtkWidget *modebar_get_box(struct modebar *mbar);
bool modebar_is_visible(const struct modebar *mbar);
void modebar_hide(struct modebar *mbar);
void modebar_set_accent(struct modebar *mbar, int32_t accent, uint32_t font_sz);
bool modebar_has_mon(const struct modebar *mbar);
nstr_t modebar_status(struct modebar *mbar, nstr_t str);
void modebar_display(struct modebar *mbar, nstr_t mon, nstr_t cam, nstr_t seq);
void modebar_set_tid(struct modebar *mbar, pthread_t tid);
void modebar_joy_event(struct modebar *mbar, struct js_event *ev);
void modebar_set_online(struct modebar *mbar, bool online);

#endif
