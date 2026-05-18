# Fontleroy — Cross-compile Makefile for tg5040
# Run inside Docker toolchain container

CROSS   = aarch64-nextui-linux-gnu-
CC      = $(CROSS)gcc
SYSROOT = /opt/aarch64-nextui-linux-gnu/aarch64-nextui-linux-gnu/libc

CFLAGS  = -O2 -Wall -Wextra -Wno-unused-parameter \
          -DPLATFORM_TG5040 \
          -I../../third_party/apostrophe/include \
          -I../../third_party/pakkit/include \
          --sysroot=$(SYSROOT)

LDFLAGS = --sysroot=$(SYSROOT) \
          -lSDL2 -lSDL2_ttf -lSDL2_image -lpthread -lm

SRC     = ../../src/main.c
OUT     = pak/bin/fontleroy

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(OUT)
