# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS	+= -std=c11 -O2 -Wall -Werror

all: ykfde

ykfde: ykfde.c
	$(CC) $(CFLAGS) -lykpers-1 -lyubikey -liniparser $(LDFLAGS) -o ykfde ykfde.c

install: ykfde
	$(INSTALL) -D -m0644 20-ykfde.rules $(DESTDIR)/usr/lib/initcpio/udev/20-ykfde.rules
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/lib/udev/ykfde

clean:
	$(RM) -f ykfde
