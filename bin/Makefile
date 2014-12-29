# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS	+= -std=c11 -O2 -fpic -pie -Wall -Werror

all: ykfde ykfde-cpio

ykfde: ykfde.c ../config.h
	$(CC) $(CFLAGS) -lykpers-1 -lyubikey -liniparser -lcryptsetup $(LDFLAGS) -o ykfde ykfde.c

ykfde-cpio: ykfde-cpio.c ../config.h
	$(CC) $(CFLAGS) -larchive $(LDFLAGS) -o ykfde-cpio ykfde-cpio.c

install: ykfde ykfde-cpio
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/bin/ykfde
	$(INSTALL) -D -m0755 ykfde-cpio $(DESTDIR)/usr/bin/ykfde-cpio

clean:
	$(RM) -f ykfde ykfde-cpio
