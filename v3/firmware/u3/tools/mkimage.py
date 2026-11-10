"""U3 image tools: patch the application descriptor, check the boot manager, write the DFU images.

The application descriptor (src/shared/u3_shared.h) sits at offset 0x100 of the application image:
  +0x00 magic "U3AP", +0x04 fwVersion, +0x06 reserved, +0x08 imageSize, +0x0C crc32
imageSize and crc32 are filled in here; the boot manager checks both before it starts the application.

Once both images are built, build/ gets:
  garantia-knx-u3-app.hex / .bin    application alone, for 0x0800 1000: the image for every UPDATE. It never
                                    touches the boot manager, so an interrupted update stays recoverable with
                                    SW1 (review G-1). The .hex carries its address, so it can't land elsewhere.
  garantia-knx-u3-full.bin / .hex   boot manager (padded to 4 KB) + application, for 0x0800 0000: only for a
                                    BLANK chip (or a new boot manager), on a stable bench supply. A power cut
                                    while page 0 is written leaves a chip that only SWD can recover.
"""
import pathlib
import struct
import zlib

DESC = 0x100
APP_MAGIC = 0x50413355
BOOT_MAGIC = 0x4D423355
APP_MAX = 0x1F000
BOOT_MAX = 0x1000
FLASH_BASE = 0x08000000
APP_BASE = 0x08001000


def _pad8(b: bytes) -> bytes:
    return b + b"\xff" * ((-len(b)) % 8)


def patch_app(path) -> dict:
    p = pathlib.Path(path)
    img = bytearray(_pad8(p.read_bytes()))
    magic, ver = struct.unpack_from("<IH", img, DESC)
    if magic != APP_MAGIC:
        raise SystemExit(f"{p}: no application descriptor at 0x{DESC:X}")
    if len(img) > APP_MAX:
        raise SystemExit(f"{p}: image is {len(img)} bytes, more than the {APP_MAX} available in bank 1")
    struct.pack_into("<I", img, DESC + 8, len(img))
    struct.pack_into("<I", img, DESC + 12, 0)
    crc = zlib.crc32(bytes(img)) & 0xFFFFFFFF
    struct.pack_into("<I", img, DESC + 12, crc)
    p.write_bytes(bytes(img))
    print(f"U3 app: version {ver >> 8}.{ver & 0xFF}, {len(img)} bytes ({100 * len(img) / APP_MAX:.1f} % of bank 1), crc32 {crc:08X}")
    return {"version": ver, "size": len(img), "crc": crc}


def check_boot(path) -> dict:
    p = pathlib.Path(path)
    img = _pad8(p.read_bytes())
    magic, ver = struct.unpack_from("<IH", img, DESC)
    if magic != BOOT_MAGIC:
        raise SystemExit(f"{p}: no boot descriptor at 0x{DESC:X}")
    if len(img) > BOOT_MAX:
        raise SystemExit(f"{p}: boot manager is {len(img)} bytes, more than {BOOT_MAX}")
    p.write_bytes(img)
    crc = zlib.crc32(img) & 0xFFFFFFFF
    print(f"U3 boot manager: version {ver}, {len(img)} bytes, crc32 {crc:08X}")
    return {"version": ver, "size": len(img), "crc": crc}


def _ihex(data: bytes, base: int) -> str:
    """Intel HEX with extended linear address records, 16 bytes per line."""
    def rec(addr16, typ, payload):
        b = bytes([len(payload), addr16 >> 8, addr16 & 0xFF, typ]) + payload
        return ":" + b.hex().upper() + f"{(-sum(b)) & 0xFF:02X}"
    out, upper = [], None
    for off in range(0, len(data), 16):
        a = base + off
        if a >> 16 != upper:
            upper = a >> 16
            out.append(rec(0, 4, struct.pack(">H", upper)))
        out.append(rec(a & 0xFFFF, 0, data[off:off + 16]))
    out.append(rec(0, 1, b""))
    return "\n".join(out) + "\n"


def write_images(project_dir) -> None:
    proj = pathlib.Path(project_dir)
    app = proj / ".pio" / "build" / "u3_app" / "firmware.bin"
    boot = proj / ".pio" / "build" / "u3_boot" / "firmware.bin"
    if not (app.exists() and boot.exists()):
        print("U3 images: waiting for both u3_app and u3_boot builds")
        return
    a, b = app.read_bytes(), boot.read_bytes()
    av, asize, acrc = struct.unpack_from("<H2xII", a, DESC + 4)
    if asize != len(a) or (zlib.crc32(a[:DESC + 12] + b"\0\0\0\0" + a[DESC + 16:]) & 0xFFFFFFFF) != acrc:
        raise SystemExit("U3 images: application image isn't patched; rebuild u3_app")
    if struct.unpack_from("<I", b, DESC)[0] != BOOT_MAGIC:
        raise SystemExit("U3 images: boot manager image has no descriptor; rebuild u3_boot")
    full = b + b"\xff" * (APP_BASE - FLASH_BASE - len(b)) + a
    out = proj / "build"
    out.mkdir(exist_ok=True)
    (out / "garantia-knx-u3-full.bin").write_bytes(full)
    (out / "garantia-knx-u3-full.hex").write_text(_ihex(full, FLASH_BASE), encoding="ascii", newline="\n")
    (out / "garantia-knx-u3-app.bin").write_bytes(a)
    (out / "garantia-knx-u3-app.hex").write_text(_ihex(a, APP_BASE), encoding="ascii", newline="\n")
    print(f"U3 images: build/garantia-knx-u3-app.hex/.bin (updates, from 0x{APP_BASE:08X}), "
          f"build/garantia-knx-u3-full.bin/.hex (blank chip, {len(full)} bytes from 0x{FLASH_BASE:08X}); "
          f"app {av >> 8}.{av & 0xFF} crc32 {acrc:08X}")
