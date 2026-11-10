#!/bin/sh
# get-knx-thelsing.sh: fetch thelsing/knx at the commit this firmware is built with and apply the local fixes
# (patches/knx-thelsing.patch, from the knx-rgbic project; see README.md "Local fixes"). The PlatformIO project
# links it as ../knx-thelsing. Needs git. Safe to run again.
set -e
cd "$(dirname "$0")"
REV=980c047ad7fc5e27bf2fae95e48acde5d5e0b4fd
if [ ! -d knx-thelsing/.git ]; then
    git clone https://github.com/thelsing/knx.git knx-thelsing
fi
cd knx-thelsing
if git apply --reverse --check ../patches/knx-thelsing.patch 2>/dev/null; then
    echo "knx-thelsing: patch already applied"
    exit 0
fi
git -c advice.detachedHead=false checkout -q "$REV"
git apply --whitespace=nowarn ../patches/knx-thelsing.patch
echo "knx-thelsing: $REV with patches/knx-thelsing.patch applied"
