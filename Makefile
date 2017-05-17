CC = gcc
DEPS = gtk+-3.0 gstreamer-1.0 gstreamer-video-1.0
CFLAGS = -std=gnu11 -O2 -Wall -Werror -flto `pkg-config --cflags $(DEPS)`
LIBS = `pkg-config --libs $(DEPS)`
TARGET = monstream

all:  $(TARGET)

SRC = src
BUILD = build
MODULES = player mongrid stream config nstr elog
OBJS = $(addprefix $(BUILD)/, $(addsuffix .o,$(MODULES)))

$(BUILD):
	mkdir $(BUILD)

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(TARGET): $(SRC)/main.c $(BUILD) $(OBJS)
	$(CC) -o $(TARGET) $(CFLAGS) $(LIBS) $(OBJS) $<

clean:
	rm -rf $(BUILD) $(TARGET)
