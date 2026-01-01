/*
 * (C) 2014-2026 by Christian Hesse <mail@eworm.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
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
/* config file second factor */
#define CONF2NDFACTOR	"second factor"

/* path to cpio archive (initramfs image) */
#define CPIOFILE	"/boot/ykfde-challenges.img"
/* path to temporary cpio archive (initramfs image) */
#define CPIOTMPFILE	CPIOFILE "-XXXXXX"

#endif /* _CONFIG_H */

// vim: set syntax=c:
