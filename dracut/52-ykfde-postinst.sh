#!/bin/bash
# -*- mode: shell-script; indent-tabs-mode: nil; sh-basic-offset: 4; -*-
# ex: ts=8 sw=4 sts=4 et filetype=sh

export LANG=C

KERNEL_VERSION="$1"
KERNEL_IMAGE="$2"

GRUB2_CFG="/etc/grub2-efi.cfg /etc/grub2.cfg"
CUSTOM_INITRD="ykfde-challenges.img"

for CFG in ${GRUB2_CFG}
do
   if [ -f ${CFG} ]; then
	sed -i --follow-symlinks "s:/initramfs-${KERNEL_VERSION}.img$:/initramfs-${KERNEL_VERSION}.img /${CUSTOM_INITRD}:g" ${CFG}
   fi
done


exit 0
