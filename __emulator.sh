#!/usr/bin/env bash

WORK_DIR=$(dirname "$0")

AVDMANAGER="$ANDROID_HOME/tools/bin/avdmanager"
SDKMANAGER="$ANDROID_HOME/tools/bin/sdkmanager"
EMULATOR="$ANDROID_HOME/tools/emulator"

NANOSCOPE_IMG_DIR=$ANDROID_HOME/system-images/nanoscope
BASE_IMG_DIR=$ANDROID_HOME/system-images/android-25/default/x86

if [ ! -d "$NANOSCOPE_IMG_DIR" ]; then
	rm -rf $NANOSCOPE_IMG_DIR

	$SDKMANAGER emulator
	$SDKMANAGER "system-images;android-25;default;x86"

	cp -r $BASE_IMG_DIR $NANOSCOPE_IMG_DIR
	cp "$WORK_DIR"/system.img $NANOSCOPE_IMG_DIR
	cp "$WORK_DIR"/ramdisk.img $NANOSCOPE_IMG_DIR
	sed -i '' 's/GLESDynamicVersion.*/GLESDynamicVersion\ =\ off/g' $NANOSCOPE_IMG_DIR/advancedFeatures.ini

	$AVDMANAGER create avd -f -n nanoscope -k "system-images;android-25;default;x86" --device "pixel_xl"
	sed -i '' 's/image\.sysdir\.1.*/image.sysdir.1=system-images\/nanoscope/g' ~/.android/avd/nanoscope.avd/config.ini
fi

$EMULATOR -verbose -avd nanoscope
