# commands
INSTALL := install

all: udev/ykfde

udev/ykfde: udev/ykfde.c
	$(MAKE) -C udev

install: udev/ykfde
	$(MAKE) -C udev install
	$(INSTALL) -D -m0644 conf/ykfde.conf $(DESTDIR)/etc/ykfde.conf
	$(INSTALL) -D -m0755 bin/ykfde $(DESTDIR)/usr/bin/ykfde
	$(INSTALL) -D -m0644 install/ykfde $(DESTDIR)/usr/lib/initcpio/install/ykfde

clean:
	$(MAKE) -C udev clean
