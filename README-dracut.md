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

Make sure systemd knows about your encrypted device by
adding a line to `/etc/crypttab`. It should read like:

> `mapping-name` /dev/`LUKS-device` -

Normally, there is already an entry for your device.

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

Build the dracut:

> dracut -f

Now you have two choices. If you want, that the challenges are updated every boot, go on. else stop here.

### change challenges on boot

To change the challenges every boot it takes too long to generate whole new initramfs. So we load an additional initram with the bootloader.

Build the cpio archive with the challenges:

> ykfde-cpio

A kernel postinstall script adds a entry to the grub2 every time a new kernel is installed. However, if you build your grub config manually, it gets lost and you must setup the grub conf by yourself or reinstall the kernel-modules.

### enable service

Enable `systemd` service `ykfde-cpio.service`. it generate every boot a new challenge and updates the initram `ykfde-challenges.img` and the LUKS passphrase.

*Be carefully:* Do not enable if you haven't setup the bootloader with the ykfde-challenges.img. If you do, you have to rebuild with dracut manually every time the service is executed.



Reboot and have fun!
