#!/usr/bin/env bash

# Flashes NanoTracer ROM onto adb-connected device.

WORK_DIR=$(dirname "$0")
CONFIG_DIR=$HOME/.nanotracer
GAPPS_ARCH=arm64
GAPPS_DATE=20180303
GAPPS_API_VERSION=7.1
GAPPS_VARIANT=pico
GAPPS_VERSION="open_gapps-$GAPPS_ARCH-$GAPPS_API_VERSION-$GAPPS_VARIANT-$GAPPS_DATE"
GAPPS_URL="https://github.com/opengapps/$GAPPS_ARCH/releases/download/$GAPPS_DATE/$GAPPS_VERSION.zip"
GAPPS_FILE=$CONFIG_DIR/$GAPPS_VERSION.zip

function ensure_config_dir() {
	mkdir -p $CONFIG_DIR
}

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
	ANDROID_PRODUCT_OUT=$WORK_DIR fastboot flashall
}

function disable_verity() {
	set -e
	ensure_root
	adb disable-verity
}

function ensure_gapps_downloaded() {
	set -e
	ensure_config_dir
	if [ ! -f $GAPPS_FILE ]; then
		wget -O $GAPPS_FILE $GAPPS_URL
	fi
}

function install_gapps() {
	set -e
	disable_verity
	if [ -z "$(adb shell pm path com.google.android.gms)" ]; then
		ensure_root
		adb reboot sideload
		adb kill-server
		adb wait-for-sideload
		adb sideload $GAPPS_FILE
		sleep 5
		adb reboot
		wait_for_boot
		adb root
		adb shell pm grant com.google.android.gms android.permission.ACCESS_FINE_LOCATION
	fi
}

set -o xtrace
set -e

ensure_gapps_downloaded &
DOWNLOAD_PID=$!

flash
wait_for_boot
wait $DOWNLOAD_PID
install_gapps