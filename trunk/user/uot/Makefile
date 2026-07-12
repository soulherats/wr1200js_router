SRC_NAME = uot
THISDIR = $(shell pwd)
USERDIR = $(ROOTDIR)/user
SHDIR = $(ROOTDIR)/user/shared

# 新增了 -Os 参数，让编译器进行极限体积压缩
CFLAGS  += -ffunction-sections -fdata-sections -fvisibility=hidden -std=gnu99 -Os
CFLAGS  += -Wall -I. -I$(SHDIR) -I$(SHDIR)/include

LDFLAGS += -Wl,--gc-sections
LDFLAGS += -pthread

EXEC = uot
OBJS = uot.c

all:
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJS) $(LDFLAGS)
	$(STRIP) $(EXEC)

clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) clean ; \
	fi ; \
	rm -f config_done $(EXEC) *.o

romfs:
	$(ROMFSINST) -p +x $(THISDIR)/$(EXEC) /usr/bin/uote
