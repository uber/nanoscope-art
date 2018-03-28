#!/usr/bin/env bash

# Flashes Nanoscope ROM onto adb-connected device.

WORK_DIR=$(dirname "$0")
OVERLAY_DIR=$HOME/.nanoscope/overlay
OVERLAY_URL_HASH=$(echo $NANOSCOPE_OVERLAY | md5)
OVERLAY_ZIP_DIR=$OVERLAY_DIR/$OVERLAY_URL_HASH

function wait_for_device() {
	set -e
	adb wait-for-device
}

function wait_for_boot() {
	set -e
	wait_for_device
	while [[ -z "$(adb shell getprop sys.boot_completed)" ]]; do sleep 1; done
}

function wait_for_bootloader() {
	set -e
	# wait-for-bootloader doesn't seem to work
	while [ -z "$(fastboot devices)" ]; do sleep 1; done
}

function is_locked() {
	fastboot getvar unlocked 2>&1 | grep "unlocked: no" &> /dev/null
	echo $?
}

function is_unlocked() {
	fastboot getvar unlocked 2>&1 | grep "unlocked: yes" &> /dev/null
	echo $?
}

function ensure_bootloader_unlocked() {
	set -e
	ensure_on_bootloader
	if [ "$(is_locked)" == "0" ]; then
		fastboot flashing unlock
	fi
	while [ "$(is_unlocked)" == "1" ]; do
		sleep 1
	done
}

function ensure_root() {
	set -e
	wait_for_device
	adb root
}

function ensure_on_bootloader() {
	set -e
	if [ -z "$(fastboot devices)" ]; then
		wait_for_device
		adb reboot-bootloader
		wait_for_bootloader
	fi
}

function flash() {
	set -e
	ensure_bootloader_unlocked
	ensure_on_bootloader
	ANDROID_PRODUCT_OUT=$WORK_DIR fastboot flashall -w
}

function ensure_overlay_downloaded() {
	set -e
	if [ ! -d "$OVERLAY_ZIP_DIR" ]; then
		mkdir -p $OVERLAY_DIR
		curl -L $NANOSCOPE_OVERLAY > overlay.zip
		unzip overlay.zip -d $OVERLAY_ZIP_DIR
		rm overlay.zip
	fi
}

function sideload_overlay() {
	set -e
	wait_for_boot
	ensure_root
	adb disable-verity
	adb reboot sideload
	adb kill-server
	sleep 2
	adb wait-for-sideload
	adb sideload "$OVERLAY_ZIP_DIR/sideload.zip"
	sleep 5
	adb reboot
	wait_for_boot
	adb root
	adb shell pm grant com.google.android.gms android.permission.ACCESS_FINE_LOCATION
}

set -o xtrace
set -e

# If set, NANOSCOPE_OVERLAY points to a zip URL that contains *.img override files and
# a sideload.zip file to be sideloaded over the base ROM. All files are optional.
if [ -n "$NANOSCOPE_OVERLAY" ]; then
	ensure_overlay_downloaded
	cp $OVERLAY_ZIP_DIR/*.img $WORK_DIR || :
fi

flash

if [ -f "$OVERLAY_ZIP_DIR/sideload.zip" ]; then
	sideload_overlay
fi
