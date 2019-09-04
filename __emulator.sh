#!/usr/bin/env bash

WORK_DIR="$( cd "$(dirname "$0")" ; pwd -P )"

AVDMANAGER="$ANDROID_HOME/tools/bin/avdmanager"
SDKMANAGER="$ANDROID_HOME/tools/bin/sdkmanager"
EMULATOR="$ANDROID_HOME/tools/emulator"

NANOSCOPE_IMG_DIR=$ANDROID_HOME/system-images/nanoscope
BASE_IMG_DIR=$ANDROID_HOME/system-images/android-25/default/x86

needs_update() {
	if [ ! -d "$NANOSCOPE_IMG_DIR" ]; then return 0; fi
        if [ ! -f "$NANOSCOPE_IMG_DIR/SOURCE" ]; then return 0; fi
        if [ "$(cat $NANOSCOPE_IMG_DIR/SOURCE)" = "$WORK_DIR" ]; then
		return 1
	else
		return 0
        fi
}

if needs_update; then
	rm -rf $NANOSCOPE_IMG_DIR

	$SDKMANAGER emulator
	$SDKMANAGER "system-images;android-25;default;x86"

	cp -r $BASE_IMG_DIR $NANOSCOPE_IMG_DIR
	cp "$WORK_DIR"/system.img $NANOSCOPE_IMG_DIR
	cp "$WORK_DIR"/ramdisk.img $NANOSCOPE_IMG_DIR
        printf $WORK_DIR > $NANOSCOPE_IMG_DIR/SOURCE
	sed -i '' 's/GLESDynamicVersion.*/GLESDynamicVersion\ =\ off/g' $NANOSCOPE_IMG_DIR/advancedFeatures.ini

	$AVDMANAGER create avd -f -n nanoscope -k "system-images;android-25;default;x86" --device "pixel_xl"

	NANOSCOPE_AVD_DIR=$HOME/.android/avd/nanoscope.avd
	echo hw.keyboard=yes >> $NANOSCOPE_AVD_DIR/config.ini
	sed -i '' 's/image\.sysdir\.1.*/image.sysdir.1=system-images\/nanoscope/g' $NANOSCOPE_AVD_DIR/config.ini
	sed -i '' 's/path="system-images;android-25;default;x86"/path="system-images;nanoscope"/g' $NANOSCOPE_IMG_DIR/package.xml
fi

$EMULATOR -verbose -avd nanoscope
