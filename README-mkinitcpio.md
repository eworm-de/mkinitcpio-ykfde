Full disk encryption with Yubikey (Yubico key) for mkinitcpio
=============================================================

This allows to automatically unlock a LUKS encrypted hard disk from `systemd`-
enabled initramfs.

Requirements
------------

To compile and use yubikey full disk encryption you need:

* [yubikey-personalization](https://github.com/Yubico/yubikey-personalization)
* [iniparser](http://ndevilla.free.fr/iniparser/)
* [systemd](http://www.freedesktop.org/wiki/Software/systemd/)
* [cryptsetup](http://code.google.com/p/cryptsetup/)
* [mkinitcpio](https://projects.archlinux.org/mkinitcpio.git/)
* [markdown](http://daringfireball.net/projects/markdown/) (HTML documentation)
* [libarchive](http://www.libarchive.org/) (Update challenge on boot)

Additionally it is expected to have `make` and `pkg-config` around to
successfully compile.

Build and install
-----------------

Building and installing is very easy. Just run:

> make

followed by:

> make install-mkinitcpio

This will place files to their desired places in filesystem.

Usage
-----

Make sure systemd knows about your encrypted device by
adding a line to `/etc/crypttab.initramfs`. It should read like:

> `mapping-name` /dev/`LUKS-device` -

Update `/etc/ykfde.conf` with correct settings. Add `mapping-name` from
above to `device name` in the `general` section. Then add a new section
with your key's decimal serial number containing the key slot setting.
The file should look like this:

    [general]
    device name = crypt

    [1234567]
    luks slot = 1

*Be warned*: Do not remove or overwrite your interactive key! Keep that
for backup and rescue!

`ykfde` will read its information from these files. Then prepare
the key. Plug it in, make sure it is configured for `HMAC-SHA1`.
After that run:

> ykfde

This will store a challenge in `/etc/ykfde.d/` and add a new slot to
your LUKS device. When `ykfde` asks for a password it requires a valid
password from available slot.

Now you have two choices. Use *either of both* hooks, depending on whether
you want to update challenge/response on every boot (`ykfde-cpio`) or
not (`ykfde`).

### `ykfde` hook

Last add `ykfde` to your hook list in `/etc/mkinitcpio.conf` and rebuild
your initramfs with:

> mkinitcpio -p linux

Reboot and have fun!

### `ykfde-cpio` hook

Add `ykfde-cpio` to your hook list in `/etc/mkinitcpio.conf` and rebuild
your initramfs with:

> mkinitcpio -p linux

Additionally enable `systemd` service `ykfde-cpio.service` and make your
bootloader load the new `cpio` image `/boot/ykfde-challenges.img` (in
addition to your usual initramfs).

Reboot and have fun!
