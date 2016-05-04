Full disk encryption with Yubikey (Yubico key) for dracut
=========================================================

This allows to automatically unlock a LUKS encrypted hard disk from `systemd`-
enabled initramfs.

Requirements
------------

To compile and use yubikey full disk encryption you need:

* libyubikey-devel
* ykpers-devel
* iniparser-devel
* libarchive-devel
* cryptsetup-devel
* python-markdown
* systemd-devel
* keyutils-libs-devel

Build and install
-----------------

Building and installing is very easy. Just run:

> make

Some distributions do have different names for `markdown` executable.
For Fedora you have to run:

> make MD=markdown_py

Build command is followed by:

> make install-dracut

This will place files to their desired places in filesystem.

Usage
-----

### config files `/etc/crypttab` and `/etc/ykfde.conf`

Make sure systemd knows about your encrypted device by
adding a line to `/etc/crypttab`. It should read like:

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
your LUKS device. When `ykfde` asks for a passphrase it requires a valid
passphrase from available slot.

Alternatively, adding a key with second factor (`foo` in this example)
is as easy:

> ykfde --new-2nd-factor foo

To update the challenge run:

> ykfde --2nd-factor foo

And changing second factor (from `foo` to `bar` in this example) is
straight forward:

> ykfde --2nd-factor foo --new-2nd-factor bar

The current and new second factor can be read from terminal, increasing
security by not displaying on display and not writing to shell history.
Use switches `--ask-2nd-factor` and `--ask-new-2nd-factor` for that.

Make sure to enable second factor in `/etc/ykfde.conf`.

### cpio archive with challenges

Every time you update a challenge and/or a second factor run:

> ykfde-cpio

This will write a cpio archive `/boot/ykfde-challenges.img` containing
your current challenges. Enable systemd service `ykfde` to do this
automatically on every boot:

> systemctl enable ykfde.service

### `dracut`

Build the initramfs:

> dracut -f

### boot loader

Update you `grub` configuration by running:

> grub2-mkconfig -o /boot/grub/grub.cfg

This will add new boot entry that loads the challenges. With other boot
loaders make sure to load the cpio archive `/boot/ykfde-challenges.img`
as additional initramfs.

Reboot and have fun!
