CC=gcc
CFLAGS=-O2

ImageWriter: ImageWriter.c
	$(CC) -o ImageWriter $(CFLAGS) ImageWriter.c

.PHONY: clean

clean:
	rm -f ImageWriter

all: ImageWriter

default: all

.PHONY: all clean