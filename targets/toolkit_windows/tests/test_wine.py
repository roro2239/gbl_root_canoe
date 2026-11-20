#!/usr/bin/env python3
"""Exercise the shipped batch file and Windows binaries using real ABL fixtures."""

import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
TARGET = ROOT / "targets/toolkit_windows"
WARNING = "Warning: Failed to patch ABL GBL"


def run(args, *, cwd, env=None, expected=0, timeout=60):
    result = subprocess.run(
        [str(arg) for arg in args], cwd=cwd, env=env,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
        encoding="utf-8", errors="replace",
    )
    if expected is not None and result.returncode != expected:
        raise AssertionError(
            f"{args!r}: exit {result.returncode}, expected {expected}\n{result.stdout}"
        )
    return result


def normal_fixture(patcher, source, patched, fake_log, out):
    original = source.read_bytes()
    normal_path = out / "normal.efi"
    result = run([patcher, source, normal_path, "normal"], cwd=out)
    normal = normal_path.read_bytes()
    assert len(normal) == len(original)

    # 独立核对真实模式保留状态字符串、状态读取、计算和写回指令。
    for pattern, length in (
        (r"Anchor offset : 0x([0-9A-F]+)", 32),
        (r"Found source LDRB at 0x([0-9A-F]+)", 4),
        (r"0x([0-9A-F]+): STRB .*SINK", 4),
        (r"Found ADRL triple at 0x([0-9A-F]+)", 8),
    ):
        match = re.search(pattern, fake_log)
        assert match, fake_log
        offset = int(match[1], 16)
        assert normal[offset:offset + length] == original[offset:offset + length]
        assert patched[offset:offset + length] != original[offset:offset + length]

    expected = bytearray(original)
    gbl = original.find("efisp".encode("utf-16le"))
    if gbl >= 0:
        expected[gbl:gbl + 10] = "nulls".encode("utf-16le")
    warning = re.search(r"Patched CBZ at 0x([0-9A-F]+) to use WZR", result.stdout)
    if warning:
        offset = int(warning[1], 16)
        instruction = struct.unpack_from("<I", original, offset)[0]
        struct.pack_into("<I", expected, offset, (instruction & ~31) | 31)
    fastboot = re.search(r"\[fastboot:applied\].*?0x([0-9A-F]+)", result.stdout)
    if fastboot:
        offset = int(fastboot[1], 16)
        struct.pack_into("<I", expected, offset, 0x14000007)
        assert normal[offset:offset + 4] == patched[offset:offset + 4]
    assert normal == expected, "真实状态模式改动了状态透传以外的指令"

    explicit_fake = out / "explicit-fake.efi"
    run([patcher, source, explicit_fake, "fake_locked"], cwd=out)
    assert explicit_fake.read_bytes() == patched, "旧 CLI 默认行为发生变化"
    failed = out / "failed.efi"
    failed.write_bytes(b"previous-output")
    run([patcher, explicit_fake, failed, "normal"], cwd=out, expected=1)
    assert failed.read_bytes() == b"previous-output"
    for options in (["invalid"], ["normal", "fake_locked"], ["--defer-fastboot", "--defer-fastboot"]):
        run([patcher, source, failed, *options], cwd=out, expected=1)
        assert failed.read_bytes() == b"previous-output"

    deferred_outputs = {}
    for options, content in (
        (["normal", "--defer-fastboot"], normal),
        (["--defer-fastboot", "normal"], normal),
        (["--defer-fastboot"], patched),
    ):
        deferred = out / "deferred.efi"
        run([patcher, source, deferred, *options], cwd=out)
        expected_deferred = bytearray(content)
        if fastboot:
            offset = int(fastboot[1], 16)
            expected_deferred[offset:offset + 4] = original[offset:offset + 4]
        assert deferred.read_bytes() == expected_deferred
        deferred_outputs["normal" if "normal" in options else "fake_locked"] = deferred.read_bytes()
    assert source.read_bytes() == original
    # 工具包保留原始 FB 分支，交给 BDS 的启动开关处理。
    return deferred_outputs["fake_locked"], deferred_outputs["normal"]


def native_fixtures(work):
    extractor = work / "extractfv"
    patcher = work / "patch_abl"
    run(["gcc", "-O2", ROOT / "submodules/ablfvextractor/extractfv.c",
         "-llzma", "-o", extractor], cwd=ROOT)
    run(["gcc", "-O2", "-I", ROOT / "submodules/patcher/include",
         *sorted((ROOT / "submodules/patcher/src").rglob("*.c")),
         "-o", patcher], cwd=ROOT)
    cases = []
    for source in sorted((ROOT / "ablrepo").glob("*/abl.img")):
        out = work / source.parent.name
        out.mkdir()
        run([extractor, source, "-o", out], cwd=work)
        original = (out / "LinuxLoader.efi").read_bytes()
        result = run([patcher, out / "LinuxLoader.efi", out / "boot.efi"], cwd=work)
        patched = (out / "boot.efi").read_bytes()
        assert original != patched, f"{source}: patcher made no changes"
        assert len(original) == len(patched), f"{source}: patcher changed image size"
        if source.parent.name == "myron":
            assert hashlib.md5(original).hexdigest() == "993388c4312750c9bfd0df8aae8f1e11"
        patched, normal = normal_fixture(patcher, out / "LinuxLoader.efi", patched, result.stdout, out)
        cases.append((source.parent.name, source.read_bytes(), original, patched, normal, WARNING in result.stdout))
        print(f"PASS native: {source.parent.name}", flush=True)
    assert cases, "No checked-in ABL fixtures found"

    # Remove only the UTF-16 GBL marker from a real extracted loader to cover
    # the warning branch without relying on a particular vendor ABL release.
    original = cases[0][2]
    marker = "efisp".encode("utf-16le")
    assert marker in original, "GBL fixture marker is missing"
    no_gbl = original.replace(marker, "nulls".encode("utf-16le"))
    source = work / "no_gbl.efi"
    source.write_bytes(no_gbl)
    result = run([patcher, source, work / "no_gbl_patched.efi"], cwd=work)
    assert WARNING in result.stdout, "Missing GBL warning in native reference"
    patched = (work / "no_gbl_patched.efi").read_bytes()
    patched, normal = normal_fixture(patcher, source, patched, result.stdout, work)
    cases.append(("no-gbl", no_gbl, no_gbl, patched, normal, True))
    print("PASS native: no-gbl warning fixture", flush=True)
    return cases


def wine_cases(work, toolkit, cases):
    wine = shutil.which(os.environ.get("WINE", "wine"))
    wineserver = shutil.which(os.environ.get("WINESERVER", "wineserver"))
    if not wine or not wineserver:
        raise RuntimeError("Wine and wineserver are required; install them before running this test")
    for name in ("extractfv.exe", "patch_abl.exe"):
        if not (toolkit / "bin" / name).is_file():
            raise RuntimeError(f"Missing {toolkit / 'bin' / name}; build the Windows tools first")

    prefix = work / "wine-prefix"
    prefix.mkdir()
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG="-all",
               WINEDLLOVERRIDES="winemenubuilder.exe=d")
    try:
        run([wine, "wineboot", "-u"], cwd=work, env=env, timeout=90)
        # The path exercises quoting and inherited delayed expansion.
        kit = work / "Windows toolkit & (regression)!"
        shutil.copytree(toolkit, kit)
        (kit / "images").mkdir(exist_ok=True)
        (kit / "extracted").mkdir(exist_ok=True)

        def batch():
            return run([wine, "cmd", "/d", "/v:on", "/c", "build.bat"],
                       cwd=kit, env=env, expected=None)

        for name, image, original, patched, normal, no_gbl in cases:
            (kit / "images/abl.img").write_bytes(image)
            result = batch()
            assert result.returncode == 0, f"{name}:\n{result.stdout}"
            assert (kit / "ABL_original.efi").read_bytes() == original, name
            assert (kit / "efisp/boot.efi").read_bytes() == patched, name
            assert (kit / "efisp/boot_normal.efi").read_bytes() == normal, name
            assert "Patched. Outputs:" in result.stdout, result.stdout
            # Wine 10 reads batch files using the OEM code page even after
            # chcp 65001. Check English guidance; console rendering is separate.
            assert "---- Manual install flow (English) ----" in result.stdout, result.stdout
            assert "dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M" in result.stdout, result.stdout
            assert ("3. Downgrade the abl partition" in result.stdout) == no_gbl, result.stdout
            assert ("WARNING: No GBL exploit found" in result.stdout) == no_gbl, result.stdout
            print(f"PASS Wine: {name}, byte-identical to native reference", flush=True)

        # A successful previous run must not make an invalid new input succeed.
        (kit / "extracted/LinuxLoader.efi").write_bytes(cases[0][2])
        (kit / "images/abl.img").write_bytes(b"invalid ABL image")
        result = batch()
        assert result.returncode != 0, result.stdout
        assert "extractfv produced no LinuxLoader.efi" in result.stdout, result.stdout
        assert not (kit / "efisp/boot.efi").exists()
        assert not (kit / "efisp/boot_normal.efi").exists()
        assert not (kit / "ABL_original.efi").exists()
        assert not (kit / "patch_log.txt").exists()
        assert "Patched. Outputs:" not in result.stdout, result.stdout
        print("PASS Wine: invalid image rejects stale outputs", flush=True)

        # A valid PE with no boot-state pattern must fail at the patching stage.
        pe = bytearray(512)
        pe[:2] = b"MZ"
        pe[0x3c:0x40] = (0x80).to_bytes(4, "little")
        pe[0x80:0x84] = b"PE\0\0"
        pe[0xd4:0xd8] = len(pe).to_bytes(4, "little")
        (kit / "images/abl.img").write_bytes(pe)
        result = batch()
        assert result.returncode != 0 and "ERROR: patch_abl failed" in result.stdout, result.stdout
        assert not (kit / "efisp/boot.efi").exists()
        assert not (kit / "efisp/boot_normal.efi").exists()
        assert "Patched. Outputs:" not in result.stdout, result.stdout
        print("PASS Wine: unsupported PE fails patching", flush=True)

        (kit / "images/abl.img").unlink()
        result = batch()
        assert result.returncode != 0 and "Place your stock ABL image" in result.stdout, result.stdout
        print("PASS Wine: missing input", flush=True)

        for name in ("extractfv.exe", "patch_abl.exe"):
            (kit / "images/abl.img").write_bytes(cases[0][1])
            executable = kit / "bin" / name
            hidden = executable.with_suffix(".hidden")
            executable.rename(hidden)
            try:
                result = batch()
                assert result.returncode != 0 and f"Missing bin/{name}" in result.stdout, result.stdout
            finally:
                hidden.rename(executable)
            print(f"PASS Wine: missing {name}", flush=True)
    finally:
        # Only stop this test's isolated server before removing its prefix.
        run([wineserver, "-k"], cwd=work, env=env, expected=None)
        run([wineserver, "-w"], cwd=work, env=env, expected=None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--toolkit", type=Path, default=TARGET / "build/toolkit")
    parser.add_argument("--native-only", action="store_true",
                        help="check reference fixtures only; does NOT validate Windows/Wine")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="canoe-windows-test-") as tmp:
        work = Path(tmp)
        cases = native_fixtures(work)
        if args.native_only:
            print("Native fixture checks passed. Windows/Wine validation was NOT run.")
        else:
            wine_cases(work, args.toolkit.resolve(), cases)
            print("Windows toolkit Wine regression checks passed.")


if __name__ == "__main__":
    main()
