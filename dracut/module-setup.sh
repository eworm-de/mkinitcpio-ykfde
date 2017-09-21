#!/bin/bash

check() {
	return 0
}

# called by dracut
depends() {
	return 0
}

install() {
	# install basic files to initramfs
	inst_rules "$moddir/20-ykfde.rules"
	inst_hook cmdline 30 "$moddir/parse-mod.sh"
	inst_simple "$moddir/ykfde.sh" /sbin/ykfde.sh
	inst_simple /usr/lib/ykfde/worker
	inst_simple /etc/ykfde.conf
	inst_simple /usr/lib/systemd/system/ykfde-worker.service
	ln_r $systemdsystemunitdir/ykfde-worker.service $systemdsystemunitdir/sysinit.target.wants/ykfde-worker.service

	# this is required for second factor
	if egrep -qi 'second factor = (yes|true|1)' /etc/ykfde.conf; then
		inst_simple /usr/lib/systemd/system/cryptsetup-pre.target
		inst_simple /usr/lib/systemd/system/ykfde-2f.service
		ln_r $systemdsystemunitdir/ykfde-2f.service $systemdsystemunitdir/sysinit.target.wants/ykfde-2f.service
		inst_simple /usr/bin/systemd-ask-password

	fi

	dracut_need_initqueue
}

