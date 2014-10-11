include config.mk

SRC = mammutfs.c
OBJ = ${SRC:.c=.o}

all: options mammutfs

options:
	@echo mammutfs build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

mammutfs: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f mammutfs ${OBJ}

.PHONY: all options clean
