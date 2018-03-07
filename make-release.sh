#!/usr/bin/env bash

# Builds a Nanoscope ROM release zip.
# usage: make-release.sh 0.0.1

VERSION=$1

if [ -z "$VERSION" ]; then
	echo "Please specify a release version."
	exit 1
fi

SCRIPT_DIR=$(dirname "$0")
AOSP_ROOT=$SCRIPT_DIR/..
ANDROID_PRODUCT_OUT=$AOSP_ROOT/out/target/product/angler
INSTALL_SCRIPT=./__install.sh
RELEASE_FILENAMES=("android-info.txt" "ramdisk.img" "userdata-qemu.img" "boot.img" "install.sh" "recovery.img" "vendor.img" "cache.img" "ramdisk-recovery.img" "system.img")
RELEASE_FILES=()
for filename in "${RELEASE_FILENAMES[@]}"; do
	RELEASE_FILES+=($ANDROID_PRODUCT_OUT/$filename)
done

cp $INSTALL_SCRIPT $ANDROID_PRODUCT_OUT/install.sh
chmod +x $ANDROID_PRODUCT_OUT/install.sh
zip -j nanoscope-rom-$VERSION.zip ${RELEASE_FILES[@]}
