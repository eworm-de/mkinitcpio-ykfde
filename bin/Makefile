# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS	+= -std=gnu11 -O2 -fPIC -Wall -Werror
LDFLAGS	+= -Wl,-z,now -Wl,-z,relro -pie

all: ykfde ykfde-cpio

ykfde: ykfde.c ../config.h ../version.h
	$(CC) $(CFLAGS) -lcryptsetup -liniparser -lkeyutils -lykpers-1 -lyubikey $(LDFLAGS) -o ykfde ykfde.c

ykfde-cpio: ykfde-cpio.c ../config.h
	$(CC) $(CFLAGS) -larchive $(LDFLAGS) -o ykfde-cpio ykfde-cpio.c

install: ykfde ykfde-cpio
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/bin/ykfde
	$(INSTALL) -D -m0755 ykfde-cpio $(DESTDIR)/usr/bin/ykfde-cpio

clean:
	$(RM) -f ykfde ykfde-cpio
