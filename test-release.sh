#!/usr/bin/env bash

# Tests building and installing a NanoTracer release.

function clean() {
	rm -f nanotracer.zip
	rm -rf tmp
}

set -e

clean

./make-release.sh

unzip nanotracer.zip -d tmp

./tmp/install.sh

clean