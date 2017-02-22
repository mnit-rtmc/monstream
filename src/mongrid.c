#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gtk/gtk.h>

struct monsink {
	char		name[8];
	GtkWidget	*widget;
	guintptr	handle;
	GstElement	*pipeline;
	GstBus		*bus;
	GstElement	*src;
	GstElement	*depay;
	GstElement	*decoder;
	GstElement	*videobox;
	GstElement	*txt_overlay;
	GstElement	*sink;
};

static GstElement *make_videobox(void) {
	GstElement *vbx = gst_element_factory_make("videobox", NULL);
	g_object_set(G_OBJECT(vbx), "left", -142, NULL);
	return vbx;
}

static GstElement *make_txt_overlay(const char *desc) {
	GstElement *ovl = gst_element_factory_make("textoverlay", NULL);
	g_object_set(G_OBJECT(ovl), "text", desc, NULL);
	g_object_set(G_OBJECT(ovl), "font-desc", "Cantarell, 14", NULL);
	g_object_set(G_OBJECT(ovl), "shaded-background", TRUE, NULL);
	g_object_set(G_OBJECT(ovl), "shading-value", 192, NULL);
	g_object_set(G_OBJECT(ovl), "color", 0xFFFFFFA0, NULL);
	g_object_set(G_OBJECT(ovl), "halignment", 2, NULL); // right
	g_object_set(G_OBJECT(ovl), "valignment", 2, NULL); // top
	g_object_set(G_OBJECT(ovl), "wrap-mode", -1, NULL); // no wrapping
	g_object_set(G_OBJECT(ovl), "deltax", 16, NULL);
	g_object_set(G_OBJECT(ovl), "deltay", -16, NULL);
	return ovl;
}

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
	struct monsink *ms = (struct monsink *) data;
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		printf("End of stream\n");
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *error;
		gst_message_parse_error(msg, &error, &debug);
		g_free(debug);
		printf("Error: %s\n", error->message);
		g_error_free(error);
		break;
	}
	default:
		break;
	}
	return TRUE;
}

static void monsink_init(struct monsink *ms, uint32_t row, uint32_t col) {
	snprintf(ms->name, 8, "m%d,%d", row, col);
	ms->widget = gtk_drawing_area_new();
	ms->handle = 0;
	ms->pipeline = gst_pipeline_new(ms->name);
	ms->bus = gst_pipeline_get_bus(GST_PIPELINE(ms->pipeline));
	gst_bus_add_watch(ms->bus, bus_call, ms);
	ms->src = NULL;
	ms->depay = NULL;
	ms->decoder = NULL;
	ms->videobox = NULL;
	ms->txt_overlay = NULL;
	ms->sink = NULL;
}

static void on_source_pad_added(GstElement *src, GstPad *pad, gpointer data) {
	struct monsink *ms = (struct monsink *) data;
	GstPad *spad = gst_element_get_static_pad(ms->depay, "sink");
printf("source_pad_added\n");
	gst_pad_link(pad, spad);
	gst_object_unref(spad);
}

static GstElement *make_src(const char *loc) {
	GstElement *src = gst_element_factory_make("rtspsrc", NULL);
	g_object_set(G_OBJECT(src), "location", loc, NULL);
	g_object_set(G_OBJECT(src), "latency", 2, NULL);
	g_object_set(G_OBJECT(src), "drop-on-latency", TRUE, NULL);
	g_object_set(G_OBJECT(src), "do-retransmission", FALSE, NULL);
	return src;
}

static void monsink_start_pipeline(struct monsink *ms, const char *loc,
	const char *desc, const char *stype)
{
	ms->src = make_src(loc);
	if (memcmp(stype, "H264", 4) == 0) {
		ms->depay = gst_element_factory_make("rtph264depay", NULL);
		ms->decoder = gst_element_factory_make("avdec_h264", NULL);
	} else {
		ms->depay = gst_element_factory_make("rtpmp4vdepay", NULL);
		ms->decoder = gst_element_factory_make("avdec_mpeg4", NULL);
	}
//	ms->videobox = make_videobox();
	ms->txt_overlay = make_txt_overlay(desc);
	ms->sink = gst_element_factory_make("xvimagesink", NULL);
	GstVideoOverlay *overlay = GST_VIDEO_OVERLAY(ms->sink);
	gst_video_overlay_set_window_handle(overlay, ms->handle);

	gst_bin_add_many(GST_BIN(ms->pipeline), ms->src, ms->depay, ms->decoder,
		ms->txt_overlay, ms->sink, NULL);
	g_signal_connect(ms->src, "pad-added", G_CALLBACK(on_source_pad_added),
		ms);

	gst_element_link(ms->src, ms->depay);
	gst_element_link(ms->depay, ms->decoder);
	gst_element_link(ms->decoder, ms->txt_overlay);
//	gst_element_link(ms->decoder, ms->videobox);
//	gst_element_link(ms->videobox, ms->txt_overlay);
	gst_element_link(ms->txt_overlay, ms->sink);

	gst_element_set_state(ms->pipeline, GST_STATE_PLAYING);
}

static void monsink_stop_pipeline(struct monsink *ms) {
	gst_element_set_state(ms->pipeline, GST_STATE_NULL);
	gst_bin_remove(GST_BIN(ms->pipeline), ms->src);
	gst_bin_remove(GST_BIN(ms->pipeline), ms->depay);
	gst_bin_remove(GST_BIN(ms->pipeline), ms->decoder);
//	gst_bin_remove(GST_BIN(ms->pipeline), ms->videobox);
	gst_bin_remove(GST_BIN(ms->pipeline), ms->txt_overlay);
	gst_bin_remove(GST_BIN(ms->pipeline), ms->sink);
}

static void monsink_set_handle(struct monsink *ms) {
	ms->handle = GDK_WINDOW_XID(gtk_widget_get_window(ms->widget));
}

static int32_t monsink_play_stream(struct monsink *ms, const char *loc,
	const char *desc, const char *stype)
{
	if (ms->src)
		monsink_stop_pipeline(ms);
	monsink_start_pipeline(ms, loc, desc, stype);
}

struct mongrid {
	uint32_t	rows;
	uint32_t	cols;
	GtkWidget	*window;
	struct monsink	*sinks;
};

static struct mongrid GRID;

static void mongrid_set_handles(void) {
	for (uint32_t r = 0; r < GRID.rows; r++) {
		for (uint32_t c = 0; c < GRID.cols; c++) {
			uint32_t i = r * GRID.cols + c;
			struct monsink *ms = GRID.sinks + i;
			monsink_set_handle(ms);
		}
	}
}

int32_t mongrid_init(uint32_t rows, uint32_t cols) {
	GtkWidget *window;
	GtkGrid *grid;
	if (rows > 4 || cols > 4) {
		GRID.rows = 0;
		GRID.cols = 0;
		fprintf(stderr, "ERROR: Grid too large: %dx%d\n", rows, cols);
		return 1;
	}
	GRID.rows = rows;
	GRID.cols = cols;
	GRID.sinks = calloc(rows * cols, sizeof(struct monsink));
	gst_init(NULL, NULL);
	gtk_init(NULL, NULL);
	window = gtk_window_new(0);
	GRID.window = window;
	grid = (GtkGrid *) gtk_grid_new();
	gtk_grid_set_column_spacing(grid, 4);
	gtk_grid_set_row_spacing(grid, 4);
	gtk_grid_set_column_homogeneous(grid, TRUE);
	gtk_grid_set_row_homogeneous(grid, TRUE);
	for (uint32_t r = 0; r < rows; r++) {
		for (uint32_t c = 0; c < cols; c++) {
			uint32_t i = r * cols + c;
			struct monsink *ms = GRID.sinks + i;
			monsink_init(ms, r, c);
			gtk_grid_attach(grid, ms->widget, c, r, 1, 1);
		}
	}
	gtk_container_add(GTK_CONTAINER(window), (GtkWidget *) grid);
	gtk_window_set_title((GtkWindow *) window, "MonStream");
	gtk_window_fullscreen((GtkWindow *) window);
	gtk_widget_show_all(window);
	gtk_widget_realize(window);
	mongrid_set_handles();
	return 0;
}

int32_t mongrid_play_stream(uint32_t row, uint32_t col, const char *loc,
	const char *desc, const char *stype)
{
	if (row < GRID.rows && col < GRID.cols) {
		uint32_t i = row * GRID.cols + col;
		struct monsink *ms = GRID.sinks + i;
		return monsink_play_stream(ms, loc, desc, stype);
	} else
		return 1;
}
