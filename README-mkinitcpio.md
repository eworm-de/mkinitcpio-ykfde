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
* keyutils and linux with `CONFIG_KEYS`
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

### config files `/etc/crypttab.initramfs` and `/etc/ykfde.conf`

Make sure systemd knows about your encrypted device by
adding a line to `/etc/crypttab.initramfs`. It should read like:

> `mapping-name` /dev/`LUKS-device` -

Usually there is already an entry for your device.

Update `/etc/ykfde.conf` with correct settings. Add `mapping-name` from
above to `device name` in the `general` section. Then add a new section
with your key's decimal serial number containing the key slot setting.
The minimal file should look like this:

    [general]
    device name = crypt

    [1234567]
    luks slot = 1

*Be warned*: Do not remove or overwrite your interactive key! Keep that
for backup and rescue!

### key setup

`ykfde` will read its information from these files and understands some
additional options. Run `ykfde --help` for details. Then prepare
the key. Plug it in, make sure it is configured for `HMAC-SHA1`.
After that run:

> ykfde

This will store a challenge in `/etc/ykfde.d/` and add a new slot to
your LUKS device. When `ykfde` asks for a password it requires a valid
password from available slot.

Adding a key with second factor is as easy:

> ykfde -s 2nd-factor

And updating key and second factor is straight forward:

> ykfde -s old-2nd-factor -n new-2nd-factor

The second factor can be read from terminal, increasing security by not
displaying on display and not writing to shell history. Use capital
switches (`-S` and `-N`) for that.

Make sure to enable second factor in `/etc/ykfde.conf`.

### cpio archive with challenges

Every time you update a challenge and/or a second factor run:

> ykfde-cpio

This will write a cpio archive `/boot/ykfde-challenges.img` containing
your current challenges. Enable systemd service `ykfde` to do this
automatically on every boot:

> systemctl enable ykfde.service

### mkinitcpio hook `ykfde`

Last add `ykfde` to your hook list in `/etc/mkinitcpio.conf`. You should
already have `systemd` and `sd-encrypt` there as a `systemd`-enabled
initramfs is prerequisite. Now rebuild your initramfs with:

> mkinitcpio -p linux

### boot loader

Update you `grub` configuration by running:

> grub-mkconfig -o /boot/grub/grub.cfg

This will add new boot entry that loads the challenges. With other boot
loaders make sure to load the cpio archive `/boot/ykfde-challenges.img`
as additional initramfs.

Reboot and have fun!
