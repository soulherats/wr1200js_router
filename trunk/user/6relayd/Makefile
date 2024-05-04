SRC_NAME=6relayd
URL_PATH=https://download.strongswan.org/$(SRC_NAME).tar.bz2

CFLAGS +=
LDFLAGS +=

THISDIR = $(shell pwd)

all: config_test
	$(MAKE) -C $(SRC_NAME)

config_test:
	( if [ -f $(THISDIR)/config_done ]; then \
		echo "the same configuration"; \
	else \
		make configure && touch config_done; \
	fi )

configure:
	( cd $(SRC_NAME) ; \
	PKG_CONFIG_PATH=$(STAGEDIR)/lib/pkgconfig \
	./configure \
	)

clean:
	rm -f config_done
	rm -rf $(THISDIR)/install
	rm -rf $(THISDIR)/$(SRC_NAME)

romfs:
	$(STRIP) $(THISDIR)/$(SRC_NAME)/bin/6relayd
	$(ROMFSINST) $(THISDIR)/$(SRC_NAME)/bin/6relayd /usr/sbin/6relayd
