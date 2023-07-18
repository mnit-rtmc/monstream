#include <gst/gst.h>
#include <gtk/gtk.h>

struct App {
  GtkWidget *window;
  GstElement *pipeline;
};

static void error_cb(GstBus *bus, GstMessage *msg, gpointer user_data) {
  gchar *debug = NULL;
  GError *err = NULL;

  gst_message_parse_error(msg, &err, &debug);

  g_print("Error: %s\n", err->message);
  g_error_free(err);

  if (debug) {
    g_print("Debug details: %s\n", debug);
    g_free(debug);
  }
}

static GtkWidget *app_sink_widget(struct App *app) {
  GstElement *sink = gst_bin_get_by_name(GST_BIN(app->pipeline), "vsink");
  g_assert(sink);

  GtkWidget *widget;
  g_object_get(sink, "widget", &widget, NULL);
  g_object_unref(sink);
  return widget;
}

static void build_window(struct App *app) {
  app->window = gtk_window_new(0);
  g_object_ref(app->window);
  g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_object_set(G_OBJECT(box), "height-request", 400, NULL);
  g_object_set(G_OBJECT(box), "width-request", 400, NULL);
  gtk_container_add(GTK_CONTAINER(app->window), box);
  GtkWidget *widget = app_sink_widget(app);
  gtk_box_pack_start(GTK_BOX(box), widget, TRUE, TRUE, 0);
  gtk_box_reorder_child(GTK_BOX(box), widget, 0);
  g_object_unref(widget);
  gtk_widget_show_all(app->window);
}

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);
  gst_init(&argc, &argv);

  struct App *app = g_slice_new0(struct App);

  if (argc > 1) {
    app->pipeline = gst_parse_launch(
      "playbin video-sink=\"gtksink name=vsink\"", NULL);
    g_object_set(app->pipeline, "uri", argv[1], NULL);
  } else {
    app->pipeline = gst_parse_launch(
      "videotestsrc pattern=18 background-color=0xFF0088AA ! "
      "videoconvert ! "
      "gtksink name=vsink",
      NULL);
  }

  build_window(app);

  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(app->pipeline));
  gst_bus_add_signal_watch(bus);
  g_signal_connect(bus, "message::error", G_CALLBACK(error_cb), app);
  gst_object_unref(bus);

  gst_element_set_state(app->pipeline, GST_STATE_PLAYING);

  gtk_main();

  gst_element_set_state(app->pipeline, GST_STATE_NULL);
  gst_object_unref(app->pipeline);
  g_object_unref(app->window);
  g_slice_free(struct App, app);

  return 0;
}
