#ifndef MODEBAR_H
#define MODEBAR_H

#include <gtk/gtk.h>
#include "nstr.h"
#include "lock.h"

struct modebar *modebar_create(GtkWidget *window, struct lock *lock);
GtkWidget *modebar_get_box(struct modebar *mbar);
void modebar_set_accent(struct modebar *mbar, int32_t accent, uint32_t font_sz);
bool modebar_has_mon(const struct modebar *mbar);
nstr_t modebar_query(struct modebar *mbar, nstr_t str);
void modebar_display(struct modebar *mbar, nstr_t mon, nstr_t cam, nstr_t seq);

#endif
