#/bin/sh

libtool --mode=link gcc player.c mongrid.c `pkg-config gstreamer-1.0 --cflags --libs` `pkg-config gtk+-3.0 --cflags --libs` `pkg-config gstreamer-video-1.0 --cflags --libs` -o monstream
