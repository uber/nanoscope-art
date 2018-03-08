#!/usr/bin/env bash

# Flashes Nanoscope ROM onto adb-connected device.

WORK_DIR=$(dirname "$0")
CONFIG_DIR=$HOME/.nanoscope

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

set -o xtrace
set -e

flash
