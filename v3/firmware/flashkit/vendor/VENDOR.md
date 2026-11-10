# Third-party tools in the flash kit

Downloaded 2026-11-09. They aren't run on the build PC; they're only copied into the kit.

| File | From | Check |
| --- | --- | --- |
| `windows/dfu-util-static.exe` | dfu-util 0.11, official binaries: `https://dfu-util.sourceforge.net/releases/dfu-util-0.11-binaries.tar.xz`, folder `win64/` (statically linked with libusb, so it needs no DLL) | The archive's md5 `fac2c6583808e68a9a27d40425e18d63` equals the published `dfu-util-0.11-binaries.tar.xz.md5`. sha256 of the exe: `662cc5c1…b9f87d` |
| `windows/zadig-2.9.exe` | Zadig 2.9 (libwdi v1.5.1, 2024-06-13): `https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe` | Authenticode signature valid, signer "Akeo Consulting". sha256 `4ecaa95d…82039b` |
| `macos-x86_64/dfu-util`, `macos-x86_64/libusb-1.0.0.dylib` | the same dfu-util 0.11 archive, folder `darwin-x86_64/` | Same archive md5. The binary is x86_64 (Mach-O `0x1000007`) and links libusb at `/usr/local/opt/libusb/lib/libusb-1.0.0.dylib` (an Intel Homebrew path). On an Apple Silicon Mac it is only a fallback: it runs under Rosetta 2, with `DYLD_FALLBACK_LIBRARY_PATH` pointing at the bundled dylib. Not tested; Homebrew's native arm64 `dfu-util` is the primary tool |

**Licences** (redistributed unmodified, for the owner's own use):
- dfu-util: GPL-2.0-or-later, source at `https://dfu-util.sourceforge.net`;
- libusb: LGPL-2.1, `https://libusb.info`;
- Zadig/libwdi: GPL-3.0, `https://github.com/pbatard/libwdi`.

`dfu-util-README.txt` is the binary archive's README.
