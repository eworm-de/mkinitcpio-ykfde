# commands
CC	:= gcc
INSTALL	:= install
RM	:= rm
# flags
CFLAGS	+= -std=c11 -O2 -fpic -pie -Wall -Werror

all: ykfde

ykfde: ykfde.c
	$(CC) $(CFLAGS) -lykpers-1 -lyubikey -liniparser -lcryptsetup $(LDFLAGS) -o ykfde ykfde.c

install: ykfde
	$(INSTALL) -D -m0755 ykfde $(DESTDIR)/usr/bin/ykfde

clean:
	$(RM) -f ykfde
