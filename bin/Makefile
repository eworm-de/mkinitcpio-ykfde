# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS		+= -std=gnu11 -O2 -fPIC -Wall -Werror
CFLAGS_YUBIKEY	+= -liniparser -lkeyutils -lykpers-1 -lyubikey
CFLAGS_SYSTEMD	+= $(shell pkg-config --cflags --libs libsystemd 2>/dev/null)
LDFLAGS		+= -Wl,-z,now -Wl,-z,relro -pie

all: worker ykfde ykfde-cpio

worker: worker.c ../config.h
	$(CC) $(CFLAGS) $(CFLAGS_YUBIKEY) $(CFLAGS_SYSTEMD) $(LDFLAGS) -o worker worker.c

ykfde: ykfde.c ../config.h ../version.h
	$(CC) $(CFLAGS) $(CFLAGS_YUBIKEY) $(CFLAGS_SYSTEMD) -lcryptsetup $(LDFLAGS) -o ykfde ykfde.c

ykfde-cpio: ykfde-cpio.c ../config.h ../version.h
	$(CC) $(CFLAGS) -larchive $(LDFLAGS) -o ykfde-cpio ykfde-cpio.c

install: worker ykfde ykfde-cpio
	$(INSTALL) -D -m0755 worker $(DESTDIR)/usr/lib/ykfde/worker
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/bin/ykfde
	$(INSTALL) -D -m0755 ykfde-cpio $(DESTDIR)/usr/bin/ykfde-cpio

clean:
	$(RM) -f worker ykfde ykfde-cpio
