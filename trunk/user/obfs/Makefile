SRC_NAME=simple-obfs

CFLAGS += -ffunction-sections -fdata-sections -fvisibility=hidden
LDFLAGS += -Wl,--gc-sections

all: config_test
	$(MAKE) -j$(HOST_NCPU) -C $(SRC_NAME)

config_test:
	( if [ -f ./config_done ]; then \
		echo "the same configuration"; \
	else \
		make configure && touch config_done; \
	fi )

configure:
	( cd $(SRC_NAME) && \
	./autogen.sh ; \
	knfsd_cv_bsd_signals=no \
	PKG_CONFIG_PATH=$(STAGEDIR)/lib/pkgconfig \
	LIBS="-L$(STAGEDIR)/lib" \
	./configure \
		--prefix=/ \
		--disable-ssp \
		--disable-documentation \
		--disable-assert \
		--host=$(HOST_TARGET) \
		--build=$(HOST_BUILD) ; \
	)

clean:
	if [ -f $(SRC_NAME1)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) distclean ; \
	fi; \
	rm -f config_done

romfs:
	$(ROMFSINST) $(SRC_NAME)/src/obfs-local /usr/bin/obfs-local
