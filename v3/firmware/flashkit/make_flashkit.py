"""make_flashkit.py: assemble the portable flash kit from the current build.

    python make_flashkit.py        (after building firmware/u3 and firmware/knxprod)

Writes dist/garantia-knx-flashkit-<fw version>/ and the same as a .zip. The zip keeps the Unix execute bits
of the macOS scripts and the bundled macOS dfu-util, so they run straight after unzipping on a Mac. Windows
.cmd files get CRLF line endings (batch labels and goto can misbehave with LF only).
Sources: kit/ (scripts, README), vendor/ (dfu-util, Zadig; see vendor/VENDOR.md), ../u3/build/ (images),
../knxprod/build/ (the signed .knxprod).
"""
import datetime
import hashlib
import pathlib
import shutil
import struct
import zipfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
FW = HERE.parent
BUILD = FW / "u3" / "build"
IMAGES = ["garantia-knx-u3-app.bin", "garantia-knx-u3-app.hex", "garantia-knx-u3-full.bin", "garantia-knx-u3-full.hex"]
KNXPROD = FW / "knxprod" / "build" / "garantia-knx.knxprod"
EXEC = {"macos/_flash.sh", "macos/flash-update.command", "macos/flash-blank-chip.command",
        "macos/list-devices.command", "macos/tools/darwin-x86_64/dfu-util"}


def sha256(p: pathlib.Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def app_info(app: bytes):
    magic, ver = struct.unpack_from("<IH", app, 0x100)
    size, crc = struct.unpack_from("<II", app, 0x108)
    assert magic == 0x50413355, "no U3 application descriptor"
    assert size == len(app), "application image isn't patched (size)"
    assert zlib.crc32(app[:0x10C] + b"\0" * 4 + app[0x110:]) & 0xFFFFFFFF == crc, "application CRC mismatch"
    return ver, size, crc


def knxprod_version() -> str:
    with zipfile.ZipFile(KNXPROD) as z:
        names = [n for n in z.namelist() if "_A-" in n and n.endswith(".xml")]
    # M-00FA_A-4741-02-xxxx.xml: application number and version
    parts = pathlib.PurePosixPath(names[0]).stem.split("_A-")[1].split("-")
    return f"application 0x{parts[0]} version {int(parts[1], 16)}"


def main():
    app = (BUILD / "garantia-knx-u3-app.bin").read_bytes()
    full = (BUILD / "garantia-knx-u3-full.bin").read_bytes()
    ver, size, crc = app_info(app)
    assert full[0x1000:] == app, "full image doesn't contain the current application"
    assert struct.unpack_from("<I", full, 0x100)[0] == 0x4D423355, "full image has no boot manager"
    boot_ver = struct.unpack_from("<H", full, 0x104)[0]
    name = f"garantia-knx-flashkit-{ver >> 8}.{ver & 0xFF}"
    out = HERE / "dist" / name
    if out.exists():
        shutil.rmtree(out)
    (out / "firmware").mkdir(parents=True)
    (out / "ets").mkdir()
    (out / "logs").mkdir()
    (out / "windows" / "tools").mkdir(parents=True)
    (out / "macos" / "tools" / "darwin-x86_64").mkdir(parents=True)

    # Scripts and README
    for src in (HERE / "kit").rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(HERE / "kit")
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        data = src.read_bytes()
        if src.suffix == ".cmd":
            data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        elif src.suffix in (".sh", ".command"):
            data = data.replace(b"\r\n", b"\n")
        dst.write_bytes(data)
    (out / "logs" / "README.txt").write_text("The scripts save their dfu-util output here.\n", encoding="utf-8")

    # Tools
    shutil.copy2(HERE / "vendor" / "windows" / "dfu-util-static.exe", out / "windows" / "tools")
    shutil.copy2(HERE / "vendor" / "windows" / "zadig-2.9.exe", out / "windows" / "tools")
    for f in ("dfu-util", "libusb-1.0.0.dylib"):
        shutil.copy2(HERE / "vendor" / "macos-x86_64" / f, out / "macos" / "tools" / "darwin-x86_64")
    shutil.copy2(HERE / "vendor" / "VENDOR.md", out / "THIRD-PARTY.md")

    # Firmware and checksums
    sums = []
    for f in IMAGES:
        shutil.copy2(BUILD / f, out / "firmware" / f)
        sums.append(f"{sha256(out / 'firmware' / f)}  {f}")
    (out / "firmware" / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="ascii", newline="\n")
    shutil.copy2(KNXPROD, out / "ets" / KNXPROD.name)

    today = datetime.date.today().isoformat()
    manifest = f"""garantia-knx flash kit {ver >> 8}.{ver & 0xFF}, assembled {today}

Firmware (U3, STM32G0B1KCU6)
  application  {ver >> 8}.{ver & 0xFF}, {size} bytes, CRC-32 {crc:08X}, at 0x08001000
  boot manager version {boot_ver} (only in garantia-knx-u3-full.*)
  firmware/garantia-knx-u3-app.bin/.hex    every update (flash-update)
  firmware/garantia-knx-u3-full.bin/.hex   blank chips only (flash-blank-chip)

ETS product
  ets/{KNXPROD.name}: {knxprod_version()}
  After an update that changes this version, download the application from ETS again.

Tools
  windows/tools/dfu-util-static.exe   dfu-util 0.11 (official win64 static build)
  windows/tools/zadig-2.9.exe         Zadig 2.9 (signed, Akeo Consulting), for the one-time WinUSB driver
  macos: Homebrew's dfu-util (brew install dfu-util); fallback macos/tools/darwin-x86_64 (dfu-util 0.11, Rosetta 2)
  Provenance and licences: THIRD-PARTY.md

SHA256
""" + "\n".join("  " + s for s in sums) + "\n"
    (out / "MANIFEST.txt").write_text(manifest, encoding="utf-8", newline="\n")

    # Zip with Unix modes
    zpath = HERE / "dist" / f"{name}.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for p in sorted(out.rglob("*")):
            rel = p.relative_to(out).as_posix()
            arc = f"{name}/{rel}" + ("/" if p.is_dir() else "")
            zi = zipfile.ZipInfo(arc, date_time=datetime.datetime.now().timetuple()[:6])
            zi.create_system = 3   # Unix, so the modes below count
            if p.is_dir():
                zi.external_attr = (0o40755 << 16) | 0x10
                z.writestr(zi, b"")
            else:
                zi.compress_type = zipfile.ZIP_DEFLATED
                zi.external_attr = ((0o100755 if rel in EXEC else 0o100644) << 16)
                z.writestr(zi, p.read_bytes())
    print(f"{out}\n{zpath} ({zpath.stat().st_size // 1024} KB)")
    print(manifest)


if __name__ == "__main__":
    main()
