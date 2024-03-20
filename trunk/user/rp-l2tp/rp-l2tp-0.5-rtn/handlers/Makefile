# Generated automatically from Makefile.in by configure.
# $Id: Makefile.in 4267 2012-05-24 17:37:42Z themiron.ru $
# Makefile for LNS handlers
# LIC: GPL

prefix=/usr
exec_prefix=${prefix}
mandir=${prefix}/man
docdir=/usr/doc/l2tp-$(VERSION)
install=/usr/bin/install -c
install_dir=/usr/bin/install -c -d
sbindir=${exec_prefix}/sbin

HANDLERS=sync-pppd.so cmd.so

OBJS=pty.o sync-pppd.o dstring.o
SRCS=$(OBJS:.o=.c)
CFLAGS=-O2 -fomit-frame-pointer -pipe -Wall -Wno-trigraphs -Wno-pointer-sign -Wno-strict-aliasing -I/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/mipsel-linux-uclibc/sysroot/usr/include -DPPPOL2TP_V1 -ffunction-sections -fdata-sections -I.. -I../libevent -I. -Wall

all: $(HANDLERS) l2tp-control

l2tp-control: cmd-control.o
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc -o l2tp-control cmd-control.o

cmd-control.o: cmd-control.c
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -c -o $@ $^

%.o: %.c
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc $(CFLAGS) -fPIC -c -o $@ $<

sync-pppd.so: pty.o sync-pppd.o
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc -shared -o $@ $^

cmd.so: cmd.o dstring.o
	/opt/rt-n56u/trunk/../toolchain-mipsel/toolchain-3.4.x/bin/mipsel-linux-uclibc-gcc -mips32r2 -march=mips32r2 -mtune=1004kc -shared -o $@ $^

clean:
	rm -f *.so *.o *~
	rm -f l2tp-control

install: all
	-mkdir -p $(RPM_INSTALL_ROOT)$(sbindir)
	-mkdir -p $(RPM_INSTALL_ROOT)$(prefix)/lib/l2tp/plugins
	$(install) -m 755 -s l2tp-control $(RPM_INSTALL_ROOT)$(sbindir)
	$(install) -m 755 $(HANDLERS) $(RPM_INSTALL_ROOT)$(prefix)/lib/l2tp/plugins

distclean: clean
	rm -f Makefile
