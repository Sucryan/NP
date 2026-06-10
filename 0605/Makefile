CC ?= cc
CFLAGS += -Wall -Wextra -std=c11
LIBSSH_CFLAGS := $(shell pkg-config --cflags libssh)
LIBSSH_LIBS := $(shell pkg-config --libs libssh)

.PHONY: all clean

all: libssh_check

libssh_check: libssh_check.c
	$(CC) $(CFLAGS) $(LIBSSH_CFLAGS) $< $(LIBSSH_LIBS) -o $@

clean:
	rm -f libssh_check
