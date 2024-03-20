# Generated automatically from Makefile.in by configure.
# Generated automatically from Makefile.in by configure.
# $Id: Makefile.in 3323 2011-09-21 18:45:48Z lly.dev $
#
# Makefile for event-handling library
#
# Copyright 2002 Roaring Penguin Software Inc.
#
# This software may be distributed according to the terms of the GNU
# General Public License, version 2 or (at your option) any later version.
# LIC: GPL

OBJS=event.o event_tcp.o hash.o event_sig.o
SRCS=$(OBJS:.o=.c)
HDRS=event.h event_tcp.h eventpriv.h hash.h
CFLAGS=-O2 -fomit-frame-pointer -pipe -Wall -Wno-trigraphs -Wno-pointer-sign -Wno-strict-aliasing -I/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/mipsel-linux-uclibc/sysroot/usr/include -DPPPOL2TP_V1 -ffunction-sections -fdata-sections -I..

all: libevent.a

libevent.a: $(OBJS)
	rm -f libevent.a
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-ar -cq libevent.a $(OBJS)
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-ranlib libevent.a

event.o: event.c $(HDRS)
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -c -o event.o event.c

hash.o: hash.c $(HDRS)
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -c -o hash.o hash.c

event_sig.o: event_sig.c $(HDRS)
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -c -o event_sig.o event_sig.c

event_tcp.o: event_tcp.c $(HDRS)
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -c -o event_tcp.o event_tcp.c

clean: FORCE
	rm -f *.a *.o *~

distclean: clean
	rm -f Makefile

FORCE:

.phony: FORCE