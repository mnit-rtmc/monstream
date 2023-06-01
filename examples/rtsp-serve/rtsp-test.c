// RTSP video test server
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

static gboolean session_cleanup(GstRTSPServer *server) {
    GstRTSPSessionPool *pool = gst_rtsp_server_get_session_pool(server);
    gst_rtsp_session_pool_cleanup(pool);
    g_object_unref(pool);
    return TRUE;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory,
      "( "
      "videotestsrc ! "
      "video/x-raw,width=352,height=288,framerate=15/1 ! "
      "x264enc ! "
      "rtph264pay name=pay0 pt=96"
      " )"
    );
    GstRTSPServer *server = gst_rtsp_server_new();
    GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);
    gst_rtsp_mount_points_add_factory(mounts, "/stream", factory);
    g_object_unref(mounts);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    if (gst_rtsp_server_attach(server, NULL) > 0) {
        g_timeout_add_seconds(2, (GSourceFunc) session_cleanup, server);
        g_print("listening at rtsp://127.0.0.1:8554/stream\n");
        g_main_loop_run(loop);
        return 0;
    } else {
        g_print("RTSP server attach failed\n");
        return -1;
    }
}
