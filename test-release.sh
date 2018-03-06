#!/usr/bin/env bash

# Tests building and installing a NanoTracer release.

function clean() {
	rm -f nanotracer-test.zip
	rm -rf tmp
}

set -e

clean

./make-release.sh test

unzip nanotracer-test.zip -d tmp

./tmp/install.sh

clean