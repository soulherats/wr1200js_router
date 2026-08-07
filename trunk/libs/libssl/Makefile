SRC_NAME=openssl-1.1.1k
SRC_URL=https://www.openssl.org/source/$(SRC_NAME).tar.gz

ifeq ($(strip $(CROSS_COMPILE)),)
CROSS_COMPILE := $(if $(CONFIG_CROSS_COMPILER_PATH),$(CONFIG_CROSS_COMPILER_PATH)/,$(abspath $(CURDIR)/../../../toolchain-mipsel/toolchain-3.4.x/bin)/)mipsel-linux-uclibc-
endif
STAGEDIR ?= $(abspath $(CURDIR)/../../stage)

# OpenSSL must use the firmware toolchain selected by config.arch.
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
CXX = $(CROSS_COMPILE)g++

COPTS = $(CPUFLAGS) -O3 $(filter-out -O%, $(CFLAGS))
COPTS += $(if $(CONFIG_IPV6),-DOPENSSL_USE_IPV6=1,)
COPTS += -DOPENSSL_PREFER_CHACHA_OVER_GCM

CFLAGS := $(COPTS)
CXXFLAGS := $(COPTS)

OPENSSL_CIPHERS = no-capieng no-cms no-rfc3779 no-seed \
  no-ec2m no-err no-threads no-idea no-rc5 no-rmd160 \
  no-static-engine no-md2 no-sse2 no-ts no-mdc2 \
  no-cast no-whirlpool no-sctp no-srp no-ssl2 no-ssl3 \
  no-async no-blake2 no-hw-padlock no-aria no-hw \
  no-devcryptoeng no-ssl3-method no-tests no-sm2 \
  no-sm3 no-sm4 enable-afalgeng no-comp

ifneq ($(CONFIG_FIRMWARE_INCLUDE_SSWAN),y)
OPENSSL_CIPHERS += no-rc2
endif

ifneq ($(CONFIG_FIRMWARE_INCLUDE_OPENSSL_EC),y)
OPENSSL_CIPHERS += no-ec
endif

all: download_test extract_test config_test
	$(MAKE) -j$(HOST_NCPU) -C $(SRC_NAME) \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CXX="$(CXX)"

download_test:
	( if [ ! -f $(SRC_NAME).tar.gz ]; then \
		wget -t5 --timeout=20 --no-check-certificate -O $(SRC_NAME).tar.gz $(SRC_URL); \
	fi )

extract_test:
	( if [ ! -d $(SRC_NAME) ]; then \
		tar -xf $(SRC_NAME).tar.gz; \
		patch -d $(SRC_NAME) -p1 < $(SRC_NAME).patch; \
		patch -d $(SRC_NAME) -p1 < $(SRC_NAME)-afalg.patch; \
		patch -d $(SRC_NAME) -p1 < $(SRC_NAME)-afalg-sha.patch; \
	fi )

config_test: extract_test
	( if [ -f ./config_done ] && grep -q "enable-afalgeng" $(SRC_NAME)/Makefile && \
		! grep -q "OPENSSL_NO_AFALGENG" $(SRC_NAME)/configdata.pm && \
		( if [ "$(CONFIG_FIRMWARE_INCLUDE_OPENSSL_EC)" = "y" ]; then \
			! grep -q '"OPENSSL_NO_EC"' $(SRC_NAME)/configdata.pm && \
			! grep -q '^# *define OPENSSL_NO_EC$$' $(SRC_NAME)/include/openssl/opensslconf.h; \
		else \
			grep -q '"OPENSSL_NO_EC"' $(SRC_NAME)/configdata.pm && \
			grep -q '^# *define OPENSSL_NO_EC$$' $(SRC_NAME)/include/openssl/opensslconf.h; \
		fi ); then \
		echo "the same configuration"; \
	else \
		$(MAKE) -C $(SRC_NAME) clean; \
		$(MAKE) configure && touch config_done; \
	fi )

configure:
	( cd $(SRC_NAME) ; \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CXX="$(CXX)" ./Configure \
		linux-mips32 \
		shared \
		--prefix=/ \
		--openssldir=/etc/ssl \
		$(OPENSSL_CIPHERS) ; \
		sed -i 's|^INSTALLTOP=/$$|INSTALLTOP=/|' Makefile ; \
	)

clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) clean ; \
	fi ; \
	rm -rf $(SRC_NAME)
	rm -f config_done

install:
	$(MAKE) -C $(SRC_NAME) install_sw DESTDIR="$(STAGEDIR)" \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CXX="$(CXX)"
	$(MAKE) -C $(SRC_NAME) install_ssldirs DESTDIR="$(STAGEDIR)" \
		CC="$(CC)" AR="$(AR)" RANLIB="$(RANLIB)" CXX="$(CXX)"
	rm -f $(STAGEDIR)/lib/libssl.a
	rm -f $(STAGEDIR)/lib/libcrypto.a

romfs:
	cp -fP $(STAGEDIR)/lib/libssl.so* $(ROMFSDIR)/lib
	cp -fP $(STAGEDIR)/lib/libcrypto.so* $(ROMFSDIR)/lib
	if [ -f "$(STAGEDIR)/lib/engines-1.1/afalg.so" ]; then \
		mkdir -p "$(ROMFSDIR)/lib/engines-1.1"; \
		cp -fP "$(STAGEDIR)/lib/engines-1.1/afalg.so" \
			"$(ROMFSDIR)/lib/engines-1.1/"; \
	fi
	$(ROMFSINST) /etc_ro/openssl.cnf
ifeq ($(CONFIG_FIRMWARE_INCLUDE_OPENSSL_EXE),y)
	$(ROMFSINST) $(SRC_NAME)/apps/openssl /usr/bin/openssl
endif
