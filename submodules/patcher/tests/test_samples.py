"""从仓库 ABL 样本验证两种模式；所有生成文件仅写入临时目录。"""
import argparse
import pathlib
import re
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("patcher", type=pathlib.Path)
parser.add_argument("extractor", type=pathlib.Path)
args = parser.parse_args()
patcher, extractor = args.patcher.resolve(), args.extractor.resolve()
repo = pathlib.Path(__file__).resolve().parents[3]


def run(argv, success=True):
    result = subprocess.run(list(map(str, argv)), capture_output=True)
    assert (result.returncode == 0) == success, result.stdout.decode(errors="replace")
    return result.stdout.decode(errors="replace")


with tempfile.TemporaryDirectory() as work:
    work = pathlib.Path(work)
    samples = sorted((repo / "ablrepo").glob("*/abl.img"))
    assert samples
    for sample in samples:
        dest = work / sample.parent.name
        dest.mkdir()
        run([extractor, "-o", dest, sample])
        source = dest / "LinuxLoader.efi"
        original = source.read_bytes()
        normal, fake = dest / "normal.efi", dest / "fake.efi"
        normal_log = run([patcher, source, normal, "normal"])
        fake_log = run([patcher, source, fake, "fake_locked"])
        n, f = normal.read_bytes(), fake.read_bytes()
        assert len(n) == len(f) == len(original)
        # 独立检查状态锚点、真实读取及写回，不依赖补丁函数自述成功。
        anchor = int(re.search(r"Anchor offset : 0x([0-9A-F]+)", fake_log)[1], 16)
        src = int(re.search(r"Found source LDRB at 0x([0-9A-F]+)", fake_log)[1], 16)
        sink = int(re.search(r"0x([0-9A-F]+): STRB .*\*\* SINK", fake_log)[1], 16)
        adrl = int(re.search(r"Found ADRL triple at 0x([0-9A-F]+)", fake_log)[1], 16)
        for off, length in [(anchor, 32), (src, 4), (sink, 4), (adrl, 8)]:
            assert n[off:off+length] == original[off:off+length]
            assert f[off:off+length] != original[off:off+length]
        gate = re.search(r"\[fastboot:applied\].*?0x([0-9A-F]+)", normal_log)
        if gate:
            off = int(gate[1], 16)
            assert struct.unpack_from("<I", n, off)[0] == 0x14000007
            assert n[off:off+4] == f[off:off+4]
        else:
            assert "[fastboot:not_found]" in normal_log
        # normal 的完整差异白名单：GBL 字符串、去黄字条件寄存器及 fastboot 分支。
        expected = bytearray(original)
        gbl = original.find("efisp".encode("utf-16le"))
        if gbl >= 0:
            expected[gbl:gbl+10] = "nulls".encode("utf-16le")
        warning = re.search(r"Patched CBZ at 0x([0-9A-F]+) to use WZR", normal_log)
        if warning:
            warn_off = int(warning[1], 16)
            raw = struct.unpack_from("<I", original, warn_off)[0]
            struct.pack_into("<I", expected, warn_off, (raw & ~31) | 31)
        if gate:
            struct.pack_into("<I", expected, off, 0x14000007)
        assert n == expected, "真实状态模式出现白名单以外的修改"
        default = dest / "default.efi"
        run([patcher, source, default])
        assert default.read_bytes() == f
        failed = dest / "failed.efi"
        run([patcher, fake, failed, "normal"], False)
        assert not failed.exists()
        run([patcher, source, failed, "invalid"], False)
        assert not failed.exists()
        # 在正常状态锚点之后制造 fastboot 拒绝，确认事务失败不会覆盖旧文件。
        if gate:
            broken = bytearray(original)
            struct.pack_into("<I", broken, off, 0x350000e0)
            source.write_bytes(broken)
            failed.write_bytes(b"previous-output")
            run([patcher, source, failed, "normal"], False)
            assert failed.read_bytes() == b"previous-output"
            assert source.read_bytes() == broken
        print(sample.parent.name, "双模式、默认兼容与失败路径通过")
