SRC_NAME = chinadns-ng
THISDIR = $(shell pwd)

all:


clean:
	if [ -f $(SRC_NAME)/Makefile ] ; then \
		$(MAKE) -C $(SRC_NAME) clean ; \
	fi ; \
	rm -f config_done

romfs:
	$(ROMFSINST) -p +x $(THISDIR)/chinadns-ng /usr/bin/chinadns-ng
	$(ROMFSINST) -p +x $(THISDIR)/ddnsto /usr/bin/ddnsto

