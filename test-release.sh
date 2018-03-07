#!/usr/bin/env bash

# Tests building and installing a Nanoscope release.

function clean() {
	rm -f nanoscope-rom-test.zip
	rm -rf tmp
}

set -e

clean

./make-release.sh test

unzip nanoscope-rom-test.zip -d tmp

./tmp/install.sh

clean
