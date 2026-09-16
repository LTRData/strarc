#!/usr/bin/env python3
"""Native Windows backup/restore smoke test against a locally built strarc.exe."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("exe", type=Path)
args = parser.parse_args()
exe = args.exe.resolve(strict=True)


def run(arguments, data=None):
    result = subprocess.run([str(exe), *arguments], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise AssertionError((arguments, result.returncode, result.stderr))
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


with tempfile.TemporaryDirectory(prefix="strarc-smoke-") as directory:
    root = Path(directory)
    source = root / "source"
    source.mkdir()
    (source / "large.bin").write_bytes(bytes(range(256)) * 1025)
    (source / "small.txt").write_bytes(b"second file\r\n")
    archive = root / "normal.sa"
    result = run(["-c", "-v", "-s:s", f"-d:{source}", str(archive),
                  "large.bin", "small.txt"])
    assert b"strarc done" in result.stderr
    normal = archive.read_bytes()
    assert normal[:8] == bytes.fromhex("000000000100c0ba")
    cases = [("normal", normal, False), ("normal-pipe", normal, True)]
    # The first recovery block ends at absolute offset 65537: the original
    # 20 bytes are read, byte zero is discarded, then the block is filled.
    for split in range(1, 20):
        damaged = b"\xCC" * (65537 - split) + normal
        cases.append((f"split-{split}", damaged, False))
        cases.append((f"pipe-split-{split}", damaged, True))
    cases.append(("large-gap", b"\xCC" * (8 * 1024 * 1024 + 13) + normal, False))
    for name, content, pipe in cases:
        target = root / name
        target.mkdir()
        command = ["-x", "-v", "-s:s", "-b:65536", f"-d:{target}"]
        if pipe:
            result = run(command, content)
        else:
            damaged_path = root / "input.sa"
            damaged_path.write_bytes(content)
            result = run([*command, str(damaged_path)])
        for original in source.iterdir():
            assert digest(original) == digest(target / original.name), name
        if name.startswith("normal"):
            assert b"Error in archive" not in result.stderr, result.stderr
            assert b"Invalid header" not in result.stderr, result.stderr
        print(f"PASS: {name}")
