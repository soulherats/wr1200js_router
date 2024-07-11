SRC_NAME = chinadns-ng
THISDIR = $(shell pwd)
USERDIR = $(ROOTDIR)/user
SHDIR = $(ROOTDIR)/user/shared
CFLAGS  += -ffunction-sections -fdata-sections -fvisibility=hidden
CFLAGS += -Wall -I. -I$(SHDIR) -I$(SHDIR)/include 
#-I$(ROOTDIR)/libs/libcurl/curl-7.75.0/include/
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -L$(STAGEDIR)/lib -lz -lcurl -lssl -lcrypto
EXEC = test
OBJS = main.c

all:
	$(CC) $(CFLAGS) -o $(EXEC) $(OBJS) $(LDFLAGS)
	$(STRIP) $(EXEC)

clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) clean ; \
	fi ; \
	rm -f config_done

romfs:
	$(ROMFSINST) -p +x $(THISDIR)/chinadns-ng /usr/bin/chinadns-ng
	$(ROMFSINST) -p +x $(THISDIR)/ddnsto /usr/bin/ddnsto

