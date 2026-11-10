#!/bin/sh
# fetch_vendor.sh: download the third-party tools the flash kit bundles into vendor/ and check them
# (see vendor/VENDOR.md). Needs curl, tar with xz, and md5sum/sha256sum (or md5/shasum on macOS).
set -e
cd "$(dirname "$0")/vendor"
DFU=dfu-util-0.11-binaries
ZADIG=zadig-2.9.exe
curl -fL -o "$DFU.tar.xz" "https://dfu-util.sourceforge.net/releases/$DFU.tar.xz"
MD5=$( (md5sum "$DFU.tar.xz" 2>/dev/null || md5 -r "$DFU.tar.xz") | cut -d' ' -f1)
[ "$MD5" = "fac2c6583808e68a9a27d40425e18d63" ] || { echo "dfu-util archive: md5 mismatch ($MD5)"; exit 1; }
tar -xJf "$DFU.tar.xz"
mkdir -p windows macos-x86_64
cp "$DFU/win64/dfu-util-static.exe" windows/
cp "$DFU/darwin-x86_64/dfu-util" "$DFU/darwin-x86_64/libusb-1.0.0.dylib" macos-x86_64/
cp "$DFU/README" dfu-util-README.txt
rm -rf "$DFU" "$DFU.tar.xz"
curl -fL -o "windows/$ZADIG" "https://github.com/pbatard/libwdi/releases/download/v1.5.1/$ZADIG"
SHA=$( (sha256sum "windows/$ZADIG" 2>/dev/null || shasum -a 256 "windows/$ZADIG") | cut -d' ' -f1)
[ "$SHA" = "4ecaa95df3da3621486a043aef8b3050b8bafe7c901402871e816229ef82039b" ] || { echo "Zadig: sha256 mismatch ($SHA)"; exit 1; }
echo "vendor/: dfu-util 0.11 and Zadig 2.9 in place"
