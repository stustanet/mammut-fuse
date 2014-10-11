# includes and libs
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc `pkg-config --libs fuse`

# flags
CPPFLAGS = `pkg-config --cflags fuse`
CFLAGS = -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = -s ${LIBS}

# compiler and linker
CC = cc
