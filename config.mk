# includes and libs
INCS = -I. -I/usr/include
LIBS = -L/usr/lib -lc `pkg-config --libs fuse` `pkg-config --libs libconfig` `pkg-config --libs libbsd`

# flags
CPPFLAGS = `pkg-config --cflags fuse`
CFLAGS =  -g -std=gnu99 -Wall -O0  ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc
