all: yoursh
CC=gcc
CFLAGS=-O2 -Wall -Wno-trigraphs 
LDFLAGS=
LIBS=-lutil
LIBTERMCAP=-lncurses
USE_GLIBC=1

CFLAGS += '-DISSUE_FILE="/etc/issue.net"' -DPARANOID_TTYS \
	   -DNO_REVOKE \
	   -DLOGIN_WRAPPER=\"/bin/sh\" \
	   -D_GNU_SOURCE

OBJS = telnetd.o state.o termstat.o systerm.o utility.o \
	global.o


yoursh: $(OBJS)
	$(CC) -g $(LDFLAGS) $^ $(LIBS) -o $@

$(OBJS): defs.h ext.h telnetd.h

clean:
	rm -f *.o yoursh

