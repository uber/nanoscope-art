#!/usr/bin/env bash

WORK_DIR=$(dirname "$0")
export DYLD_LIBRARY_PATH="$ANDROID_HOME/emulator/lib64/qt/lib:$ANDROID_HOME/emulator/lib64:$DYLD_LIBRARY_PATH"

KERNEL_FILE=$ANDROID_HOME/system-images/android-25/google_apis/x86/kernel-qemu

if [ ! -f "$KERNEL_FILE" ]; then
	$ANDROID_HOME/tools/bin/sdkmanager "system-images;android-25;google_apis;x86"
fi

$ANDROID_HOME/emulator/emulator64-x86 -verbose \
-sysdir $WORK_DIR \
-system $WORK_DIR/system.img \
-ramdisk $WORK_DIR/ramdisk.img \
-data $WORK_DIR/userdata.img \
-kernel $KERNEL_FILE \
-memory 1024 \
-partition-size 2056 \
-gpu host \
-skindir $ANDROID_HOME/skins \
-skin pixel