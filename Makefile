all: uuinput

FUSE_CFLAGS = $(shell pkg-config --cflags fuse)
FUSE_LIBS = $(shell pkg-config --libs fuse)

uuinput: uuinput.c Makefile
	cc -o$@ $< $(FUSE_CFLAGS) $(FUSE_LIBS)

clean:
	rm -f uuinput
