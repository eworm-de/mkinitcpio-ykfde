# commands
INSTALL := install
MD	:= markdown
RM	:= rm
CP	:= cp
SED	:= sed
# this is just a fallback in case you do not use git but downloaded
# a release tarball...
VERSION := 0.5.0

all: bin/ykfde bin/ykfde-cpio udev/ykfde README.html README-mkinitcpio.html README-dracut.html

bin/ykfde: bin/ykfde.c config.h
	$(MAKE) -C bin ykfde

bin/ykfde-cpio: bin/ykfde-cpio.c config.h
	$(MAKE) -C bin ykfde-cpio

udev/ykfde: udev/ykfde.c config.h
	$(MAKE) -C udev ykfde

config.h: config.def.h
	$(CP) config.def.h config.h

README.html: README.md
	$(MD) README.md > README.html
	$(SED) -i 's/\(README[-[:alnum:]]*\).md/\1.html/g' README.html

README-mkinitcpio.html: README-mkinitcpio.md
	$(MD) README-mkinitcpio.md > README-mkinitcpio.html
	$(SED) -i 's/\(README[-[:alnum:]]*\).md/\1.html/g' README-mkinitcpio.html

README-dracut.html: README-dracut.md
	$(MD) README-dracut.md > README-dracut.html
	$(SED) -i 's/\(README[-[:alnum:]]*\).md/\1.html/g' README-dracut.html

install: install-mkinitcpio

install-bin: bin/ykfde udev/ykfde
	$(MAKE) -C bin install
	$(MAKE) -C udev install
	$(INSTALL) -D -m0644 conf/ykfde.conf $(DESTDIR)/etc/ykfde.conf
	$(INSTALL) -D -m0644 systemd/ykfde-cpio.service $(DESTDIR)/usr/lib/systemd/system/ykfde-cpio.service
	$(INSTALL) -d -m0700 $(DESTDIR)/etc/ykfde.d/

install-doc: README.html README-mkinitcpio.html README-dracut.html
	$(INSTALL) -D -m0644 README.md $(DESTDIR)/usr/share/doc/ykfde/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)/usr/share/doc/ykfde/README.html
	$(INSTALL) -D -m0644 README-mkinitcpio.md $(DESTDIR)/usr/share/doc/ykfde/README-mkinitcpio.md
	$(INSTALL) -D -m0644 README-mkinitcpio.html $(DESTDIR)/usr/share/doc/ykfde/README-mkinitcpio.html
	$(INSTALL) -D -m0644 README-dracut.md $(DESTDIR)/usr/share/doc/ykfde/README-dracut.md
	$(INSTALL) -D -m0644 README-dracut.html $(DESTDIR)/usr/share/doc/ykfde/README-dracut.html

install-mkinitcpio: install-bin install-doc
	$(INSTALL) -D -m0644 mkinitcpio/ykfde $(DESTDIR)/usr/lib/initcpio/install/ykfde
	$(INSTALL) -D -m0644 mkinitcpio/ykfde-cpio $(DESTDIR)/usr/lib/initcpio/install/ykfde-cpio
	$(INSTALL) -D -m0644 udev/20-ykfde.rules $(DESTDIR)/usr/lib/initcpio/udev/20-ykfde.rules

install-dracut: install-bin install-doc
	$(INSTALL) -D -m0755 dracut/module-setup.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/module-setup.sh
	$(INSTALL) -D -m0755 dracut/parse-mod.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/parse-mod.sh
	$(INSTALL) -D -m0755 dracut/ykfde.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/ykfde.sh
	$(INSTALL) -D -m0644 udev/20-ykfde.rules $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/20-ykfde.rules

clean:
	$(MAKE) -C bin clean
	$(MAKE) -C udev clean
	$(RM) -f README.html README-mkinitcpio.html README-dracut.html

distclean: clean
	$(RM) -f config.h

release:
	git archive --format=tar.xz --prefix=mkinitcpio-ykfde-$(VERSION)/ $(VERSION) > mkinitcpio-ykfde-$(VERSION).tar.xz
	gpg -ab mkinitcpio-ykfde-$(VERSION).tar.xz
	git archive --format=tar.gz --prefix=mkinitcpio-ykfde-$(VERSION)/ $(VERSION) > mkinitcpio-ykfde-$(VERSION).tar.gz
	gpg -ab mkinitcpio-ykfde-$(VERSION).tar.gz
