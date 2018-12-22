SRCS=
TARGET=dospack
INCLUDES=-I.
DISPLAY_HEAD ?= gl
CFLAGS=-O2 -Wall -g -Werror
DP_PLATFORM ?= desktop

ifeq ($(DISPLAY_HEAD),gl)
CFLAGS += -DDP_VIDEO_ENGINE=dp_video_gl_init -DDP_PLATFORM_DESKTOP
LDFLAGS += -lSDL -lGL -lm
endif

ifeq ($(DISPLAY_HEAD),null)
CFLAGS += -DDP_VIDEO_ENGINE=dp_video_null_init -DDP_PLATFORM_DESKTOP
LDFLAGS += -lrt
endif

ifeq ($(DP_32BIT),y)
LDFLAGS += -m32
CFLAGS += -m32
endif

all : $(TARGET)

SUBDIR=core/
include ${SUBDIR}makeinclude
SUBDIR=ui/
include ${SUBDIR}makeinclude
SUBDIR=desktop/
include ${SUBDIR}makeinclude
SUBDIR=games/dave/
include ${SUBDIR}makeinclude

include rules.make
