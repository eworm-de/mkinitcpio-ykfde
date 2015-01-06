#!/bin/bash

check() {
	return 0
}

# called by dracut
depends() {
	return 0
}

install() {
	inst_rules "$moddir/20-ykfde.rules"
	inst_hook cmdline 30 "$moddir/parse-mod.sh"
	inst_simple "$moddir/ykfde.sh" /sbin/ykfde.sh
	inst_simple /usr/lib/udev/ykfde
	inst_simple /etc/ykfde.conf
	inst_dir /etc/ykfde.d/*

	dracut_need_initqueue
}

