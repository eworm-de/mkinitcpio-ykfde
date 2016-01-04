/*
 * (C) 2014-2016 by Christian Hesse <mail@eworm.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* path to the configuration file */
#define	CONFIGFILE	"/etc/ykfde.conf"

/* path to challenge files
 * make sure this is an absolute path with trailing slash */
#define CHALLENGEDIR	"/etc/ykfde.d/"

/* config file device name */
#define CONFDEVNAME	"device name"
/* config file Yubikey slot */
#define CONFYKSLOT	"yk slot"
/* config file LUKS slot */
#define CONFLUKSSLOT	"luks slot"

/* path to cpio archive (initramfs image) */
#define CPIOFILE	"/boot/ykfde-challenges.img"
/* path to temporary cpio archive (initramfs image) */
#define CPIOTMPFILE	CPIOFILE "-XXXXXX"

#endif /* _CONFIG_H */

// vim: set syntax=c:
