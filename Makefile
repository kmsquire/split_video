# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=    libavdevice                        \
                libavformat                        \
                libavfilter                        \
                libavcodec                         \
                libswresample                      \
                libswscale                         \
                libavutil                          \

CFLAGS += -Wall -g -O3
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS)) $(CFLAGS)
CFLAGS := -I/usr/local/opt/gettext/include $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) $(LDLIBS)
LDLIBS := -L/usr/local/opt/gettext/lib $(LDLIBS)

# the following examples make explicit use of the math library
split_video:  LDLIBS += -lm

.phony: all clean-test clean

all: split_video split_video.o

clean:
	$(RM) split_video split_video.o
