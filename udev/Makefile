# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS	+= -std=c11 -O2 -fPIC -Wall -Werror
LDFLAGS	+= -Wl,-z,now -Wl,-z,relro -pie

all: ykfde

ykfde: ykfde.c ../config.h
	$(CC) $(CFLAGS) -liniparser -lkeyutils -lykpers-1 -lyubikey $(LDFLAGS) -o ykfde ykfde.c

install: ykfde
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/lib/udev/ykfde

clean:
	$(RM) -f ykfde
