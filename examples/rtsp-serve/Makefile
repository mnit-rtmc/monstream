CC = gcc
DEPS = gstreamer-1.0 gstreamer-video-1.0 gstreamer-plugins-base-1.0 gstreamer-rtsp-server-1.0
CFLAGS = -std=gnu11 -O2 -Wall -Werror -flto `pkg-config --cflags $(DEPS)`
LIBS = `pkg-config --libs $(DEPS)`
TARGET = rtsp-serve

all:  $(TARGET)

$(TARGET): rtsp-test.c
	$(CC) -o $(TARGET) $(CFLAGS) $< $(LIBS)

clean:
	rm -rf $(TARGET)
