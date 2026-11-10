#!/bin/sh
# build.sh: spec.py -> knxprod.h + ETS XML -> signed build/garantia-knx.knxprod (OpenKNXproducer + local ETS6).
# OpenKNXproducer 4.3.12 (Windows, signs with the local ETS6). OKP=... points at it.
set -e
cd "$(dirname "$0")"
OKP="${OKP:-D:/elektronika/knx-rgbic/firmware/tools/OpenKNXproducer/tools/Windows/OpenKNXproducer-x64.exe}"
if [ ! -f "$OKP" ]; then
    echo "OpenKNXproducer not found at $OKP"
    echo "Get it from https://github.com/OpenKNX/OpenKNXproducer (4.3.12 was used; it needs ETS6 installed to sign)"
    echo "and run: OKP=/path/to/OpenKNXproducer-x64.exe sh build.sh"
    exit 1
fi
python gen.py
python gen_xml.py
mkdir -p build
"$OKP" create -h build/knxprod-openknx.h -o build/garantia-knx.knxprod garantia-knx.xml
