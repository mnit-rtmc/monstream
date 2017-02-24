/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003,2004> David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcairooverlay.h"

#include <string.h>
#include <math.h>

/*
 * This hacked up version of gstcairo is used to statically link, because the
 * Fedora gstreamer1-plugins-good package is broken.
 */

static gboolean plugin_init (GstPlugin *plugin) {
	GST_DEBUG_CATEGORY_STATIC (cairo_debug);
	GST_DEBUG_CATEGORY_INIT (cairo_debug, "cairo", 0, "Cairo elements");
	gst_element_register (plugin, "cairooverlay", GST_RANK_NONE,
		GST_TYPE_CAIRO_OVERLAY);
	return TRUE;
}

void cairoplugin_register_static(void) {
	gst_plugin_register_static(
		GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		"cairo",
		"Cairo-based elements",
		plugin_init,
		"version X",
		"LGPL",
		"cairo",
		"cairooverlay",
		"origin");
}
