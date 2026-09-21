"""Linux/WSL 主机回归：临时编译真实 C 实现，验证存储与仓库 ABL 样本。"""
import pathlib
import re
import struct
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[3]
edk = repo / "submodules/uefi/edk2"
tests = pathlib.Path(__file__).resolve().parent
patcher_root = repo / "submodules/patcher"


def run(args, success=True):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    assert (result.returncode == 0) == success, result.stdout + result.stderr
    return result.stdout


with tempfile.TemporaryDirectory(prefix="boot-warning-tests-") as directory:
    work = pathlib.Path(directory)
    test = work / "test_boot_warning"
    run(["clang", "-g", "-O1", "-fshort-wchar", "-DMDEPKG_NDEBUG", "-DUSING_LTO",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-Wall", "-Wextra", "-Werror",
         "-I", edk / "MdePkg/Include", "-I", edk / "MdePkg/Include/X64",
         tests / "test_boot_warning.c", tests / "boot_warning_host.c", "-o", test])
    print(run([test]).strip())
    load_test = work / "test_boot_warning_load"
    run(["clang", "-g", "-O1", "-fshort-wchar", "-DMDEPKG_NDEBUG", "-DUSING_LTO",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
         "-I", edk / "MdePkg/Include", "-I", edk / "MdePkg/Include/X64",
         tests / "test_boot_warning_load.c", tests / "boot_warning_host.c", "-o", load_test])
    print(run([load_test]).strip())
    patcher = work / "patch_abl"
    extractor = work / "extractfv"
    run(["clang", "-O2", "-I", patcher_root / "include",
         *sorted((patcher_root / "src").rglob("*.c")), "-o", patcher])
    run(["clang", "-O2", repo / "submodules/ablfvextractor/extractfv.c", "-llzma", "-o", extractor])
    matched = 0
    for sample in sorted((repo / "ablrepo").glob("*/abl.img")):
        dest = work / sample.parent.name
        dest.mkdir()
        run([extractor, "-o", dest, sample])
        source = dest / "LinuxLoader.efi"
        for mode in ("normal", "fake_locked"):
            hidden, shown = dest / f"{mode}.efi", dest / f"{mode}_shown.efi"
            log = run([patcher, source, hidden, mode])
            match = re.search(r"Patched CBZ at 0x([0-9A-F]+) to use WZR", log)
            before = hidden.read_bytes()
            run([test, hidden, shown], success=bool(match))
            assert hidden.read_bytes() == before, "磁盘输入被改写"
            if not match:
                assert not shown.exists(), "不支持的输入不应产生输出"
                continue
            matched += 1
            offset = int(match[1], 16)
            instruction = struct.unpack_from("<I", before, offset)[0]
            expected = bytearray(before)
            struct.pack_into("<I", expected, offset, instruction | 0x01000000)
            assert shown.read_bytes() == expected, "出现提示分支之外的修改"
            failed = dest / f"{mode}_invalid.efi"
            run([test, shown, failed], success=False)
            assert not failed.exists()
        print(sample.parent.name, "双模式提示恢复或明确拒绝通过")
    assert matched > 0, "没有实际覆盖支持的 ABL 样本"
    print(f"共验证 {matched} 个支持提示恢复的双模式产物")
