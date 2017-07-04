# commands
INSTALL := install
MD	:= markdown
RM	:= rm
CP	:= cp
SED	:= sed
# this is just a fallback in case you do not use git but downloaded
# a release tarball...
VERSION := 0.7.1

.DELETE_ON_ERROR:

all: bin/worker bin/ykfde bin/ykfde-cpio README.html README-mkinitcpio.html README-dracut.html

bin/worker: bin/worker.c config.h
	$(MAKE) -C bin worker

bin/ykfde: bin/ykfde.c config.h version.h
	$(MAKE) -C bin ykfde

bin/ykfde-cpio: bin/ykfde-cpio.c config.h version.h
	$(MAKE) -C bin ykfde-cpio

config.h: config.def.h
	$(CP) config.def.h config.h

version.h: $(wildcard .git/HEAD .git/index .git/refs/tags/*) Makefile
	echo "#ifndef VERSION" > $@
	echo "#define VERSION \"$(shell git describe --tags --long 2>/dev/null || echo ${VERSION})\"" >> $@
	echo "#endif" >> $@

%.html: %.md
	$(MD) $< > $@
	$(SED) -i 's/\(README[-[:alnum:]]*\).md/\1.html/g' $@

install: install-mkinitcpio

install-bin: bin/worker bin/ykfde bin/ykfde-cpio
	$(MAKE) -C bin install
	$(INSTALL) -D -m0644 conf/ykfde.conf $(DESTDIR)/etc/ykfde.conf
	$(INSTALL) -d -m0700 $(DESTDIR)/etc/ykfde.d/
	$(INSTALL) -D -m0644 conf/gitignore $(DESTDIR)/etc/ykfde.d/.gitignore
	$(INSTALL) -D -m0755 grub/09_linux $(DESTDIR)/etc/grub.d/09_linux
	$(INSTALL) -D -m0644 systemd/ykfde.service $(DESTDIR)/usr/lib/systemd/system/ykfde.service
	$(INSTALL) -D -m0644 systemd/ykfde-2f.service $(DESTDIR)/usr/lib/systemd/system/ykfde-2f.service
	$(INSTALL) -D -m0644 systemd/ykfde-worker.service $(DESTDIR)/usr/lib/systemd/system/ykfde-worker.service

install-doc: README.html README-mkinitcpio.html README-dracut.html
	$(INSTALL) -D -m0644 README.md $(DESTDIR)/usr/share/doc/ykfde/README.md
	$(INSTALL) -D -m0644 README.html $(DESTDIR)/usr/share/doc/ykfde/README.html
	$(INSTALL) -D -m0644 README-mkinitcpio.md $(DESTDIR)/usr/share/doc/ykfde/README-mkinitcpio.md
	$(INSTALL) -D -m0644 README-mkinitcpio.html $(DESTDIR)/usr/share/doc/ykfde/README-mkinitcpio.html
	$(INSTALL) -D -m0644 README-dracut.md $(DESTDIR)/usr/share/doc/ykfde/README-dracut.md
	$(INSTALL) -D -m0644 README-dracut.html $(DESTDIR)/usr/share/doc/ykfde/README-dracut.html

install-mkinitcpio: install-bin install-doc
	$(INSTALL) -D -m0644 mkinitcpio/ykfde $(DESTDIR)/usr/lib/initcpio/install/ykfde
	$(INSTALL) -D -m0644 udev/20-ykfde.rules $(DESTDIR)/usr/lib/initcpio/udev/20-ykfde.rules

install-dracut: install-bin install-doc
	$(INSTALL) -D -m0755 dracut/module-setup.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/module-setup.sh
	$(INSTALL) -D -m0755 dracut/parse-mod.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/parse-mod.sh
	$(INSTALL) -D -m0755 dracut/ykfde.sh $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/ykfde.sh
	$(INSTALL) -D -m0644 udev/20-ykfde.rules $(DESTDIR)/usr/lib/dracut/modules.d/90ykfde/20-ykfde.rules

clean:
	$(MAKE) -C bin clean
	$(RM) -f README.html README-mkinitcpio.html README-dracut.html version.h

distclean: clean
	$(RM) -f config.h

release:
	git archive --format=tar.xz --prefix=mkinitcpio-ykfde-$(VERSION)/ $(VERSION) > mkinitcpio-ykfde-$(VERSION).tar.xz
	gpg -ab mkinitcpio-ykfde-$(VERSION).tar.xz
	git archive --format=tar.gz --prefix=mkinitcpio-ykfde-$(VERSION)/ $(VERSION) > mkinitcpio-ykfde-$(VERSION).tar.gz
	gpg -ab mkinitcpio-ykfde-$(VERSION).tar.gz
