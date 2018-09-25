#!/bin/bash

# Install script from BeagleLogic is adapted by Kevin Verniers to only install the necessary features.

# Install script for BeagleLogic from git repository
#
# This script is meant to be executed in either a chroot environment (used while
# building the BeagleLogic system image) or after doing a git clone of the
# BeagleLogic repository.
#
# This script only supports the BeagleBone Debian image which have all the
# required dependencies

DIR=$(cd `dirname $0` && pwd -P)
log="beaglelogic:"

# The kernel looks for beaglelogic-pru0-fw and beaglelogic-pru1-fw in /lib/firmware
# The firmware is installed as 'beaglelogic-pru0-fw' and 'beaglelogic-pru1-logic'
# beaglelogic-pru1-fw is further symlinked to beaglelogic-pru1-logic
# This arrangement enables running PRUDAQ by simply updating the symlink and
# reloading the beaglelogic kernel module
install_pru_firmware () {
	# Required for compiling the firmware, see firmware/Makefile
	if [ ! -d /usr/share/ti/cgt-pru/bin ] ; then
		ln -s /usr/bin/ /usr/share/ti/cgt-pru/bin
	fi

	echo "${log} Building and installing PRU firmware"
	export PRU_CGT=/usr/share/ti/cgt-pru
	cd "${DIR}/firmware"
	make
	make install

	if [ ! "x${RUNNING_AS_CHROOT}" = "xyes" ] ; then
		update-initramfs -u -k `uname -r`
	fi
}

create_beaglelogic_group() {
	echo "${log} Creating beaglelogic group and adding $DEFAULT_USER to it"

	groupadd -f beaglelogic
	usermod -aG beaglelogic ${DEFAULT_USER}
}


# Udev rules required to allow the default user to modify BeagleLogic
# sysfs attributes without requiring root permissions
install_udev_rules() {
	echo "${log} Installing udev rules"
	cp -v "${DIR}/scripts/90-beaglelogic.rules" "/etc/udev/rules.d/"
}

install_systemd_service() {
	echo "${log} Installing systemd startup service"
	cp -v "${DIR}/scripts/beaglelogic-startup.service" "/lib/systemd/system/beaglelogic-startup.service"
	chown root:root "/lib/systemd/system/beaglelogic-startup.service"
	sed -i -e "s:DIR:${DIR}:" "/lib/systemd/system/beaglelogic-startup.service"
	systemctl enable beaglelogic-startup.service || true
}

update_uboot_uenv_txt() {
	if [ ! "x${RUNNING_AS_CHROOT}" = "xyes" ] ; then
		echo "${log} Updating uEnv.txt"
		sed -i -e "s:#disable_uboot_overlay_video:disable_uboot_overlay_video:" "/boot/uEnv.txt"
		sed -i -e "s:uboot_overlay_pru:#uboot_overlay_pru:" "/boot/uEnv.txt"
		echo '#Load BeagleLogic Cape' >> "/boot/uEnv.txt"
		echo 'uboot_overlay_pru=/lib/firmware/beaglelogic-00A0.dtbo' >> "/boot/uEnv.txt"
	fi
}

display_success_message() {
	if [ ! "x${RUNNING_AS_CHROOT}" = "xyes" ] ; then
		echo "${log} Successfully Installed. Please reboot"
	else
		echo "${log} Installation Completed, uEnv.txt must be updated"
	fi
}

# Added by Kevin to set correct kernel module at boot
set_kernel_module_at_boot() {
	# Required for compiling the firmware, see firmware/Makefile
	echo "${log} Setting correct kernel module to load at boot"
	cp -v "${DIR}/kernel/beaglelogic.ko" "/lib/modules/$(uname -r)/kernel/drivers/misc/"
}


if [ "x$1" = "x--upgrade" ] ; then
	UPGRADING="yes"
else
	UPGRADING="no"
fi

if [ "x$1" = "x--update-uenv-txt" ] ; then
	update_uboot_uenv_txt
	exit 0
fi

if [ "x$1" = "x--chroot" ] ; then
	DEFAULT_USER=$2
	RUNNING_AS_CHROOT="yes"
else
	RUNNING_AS_CHROOT="no"
	if [ ! "x$SUDO_USER" = "x" ] ; then
		DEFAULT_USER=$SUDO_USER
	else
		DEFAULT_USER="debian"
	fi
fi

install_pru_firmware
if [ "x${UPGRADING}" = "xno" ] ; then
	create_beaglelogic_group
fi
install_udev_rules
install_systemd_service
if [ "x${UPGRADING}" = "xno" ] ; then
	update_uboot_uenv_txt
fi
set_kernel_module_at_boot
display_success_message
