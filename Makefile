# Makefile - build for X68000 (elf2x68k toolchain + libsocket)
#
# Requirements:
#   - m68k-xelf-gcc in PATH
#   - libsocket available in the toolchain (link with -lsocket)
#
# Build:
#   make -f Makefile.x68k
#
# Output:
#   omnimsg.x

TARGET = omnimsg
CC = m68k-xelf-gcc

CFLAGS = -O2 -std=c99 -Wall -Wextra
SRCS = src/omnimsg.c
LIBS = -lsocket

all: $(TARGET).x

$(TARGET).x: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET).x
