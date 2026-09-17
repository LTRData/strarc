#!/usr/bin/env python3
"""Native Windows backup/restore smoke test against a locally built strarc.exe."""
import argparse
import hashlib
import os
import struct
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("exe", type=Path)
args = parser.parse_args()
exe = args.exe.resolve(strict=True)


def run(arguments, data=None, failed=False):
    result = subprocess.run([str(exe), *arguments], input=data,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if bool(result.returncode) != failed:
        raise AssertionError((arguments, result.returncode, result.stderr))
    if failed:
        assert b"strarc done" not in result.stderr, result.stderr
        assert b"with errors" in result.stderr, result.stderr
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).digest()


HEADER = struct.Struct("<IIqI")
MAGIC = 0xBAC00001
ABORT = HEADER.pack(0, 0xBAC00002, 4, 0) + struct.pack("<I", 23)  # ERROR_CRC


def stream(stream_id, payload, name="", attributes=0):
    encoded = name.encode("utf-16le")
    return HEADER.pack(stream_id, attributes, len(payload), len(encoded)) + encoded + payload


def native_entries(archive):
    """Parse declared lengths, never scan for signatures in file payload."""
    result = []
    offset = 0
    while offset < len(archive):
        sid, attributes, size, name_size = HEADER.unpack_from(archive, offset)
        end = offset + HEADER.size + name_size + size
        assert end <= len(archive)
        if sid == 0 and attributes == MAGIC:
            result.append([archive[offset:end], []])
        else:
            assert result
            result[-1][1].append(archive[offset:end])
        offset = end
    return result


def entry_prefix(name, metadata, directory=False):
    metadata = bytearray(metadata[:52])
    struct.pack_into("<I", metadata, 0, 16 if directory else 128)
    encoded = name.encode("utf-16le")
    return HEADER.pack(0, MAGIC, 52, len(encoded)) + encoded + metadata


with tempfile.TemporaryDirectory(prefix="strarc-smoke-") as directory:
    root = Path(directory)
    source = root / "source"
    source.mkdir()
    (source / "large.bin").write_bytes(bytes(range(256)) * 1025)
    (source / "small.txt").write_bytes(b"second file\r\n")
    ads = Path(str(source / "large.bin") + ":metadata")
    ads.write_bytes(b"alternate stream content\r\n")
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
        assert digest(ads) == digest(Path(str(target / "large.bin") + ":metadata")), name
        if name.startswith("normal"):
            assert b"Error in archive" not in result.stderr, result.stderr
            assert b"Invalid header" not in result.stderr, result.stderr
        print(f"PASS: {name}")

    first, second = native_entries(normal)
    sid, attributes, size, name_size = HEADER.unpack_from(first[0])
    metadata = first[0][HEADER.size + name_size:][:52]
    first_data = next(part for part in first[1] if HEADER.unpack_from(part)[0] == 1)
    _, _, data_size, data_name = HEADER.unpack_from(first_data)
    payload_start = HEADER.size + data_name
    good = min(117, data_size)
    padded = first_data[:payload_start + good] + bytes(data_size - good)
    later = second[0] + b"".join(second[1])
    failed_cases = {
        "padded-data": first[0] + padded + ABORT,
        "padded-security": first[0] + stream(3, bytes(200000)) + ABORT,
        "padded-alternate": first[0] + stream(1, b"main") +
            stream(4, bytes(200000), ":failed:$DATA") + ABORT,
        "before-stream": first[0] + ABORT,
    }
    for name, failed_entry in failed_cases.items():
        for pipe in (False, True):
            target = root / (name + ("-pipe" if pipe else "-file"))
            target.mkdir()
            content = failed_entry + later
            command = ["-x", "-v", "-b:65536", f"-d:{target}"]
            if pipe:
                result = run(command, content, failed=True)
            else:
                archive.write_bytes(content)
                result = run([*command, str(archive)], failed=True)
            assert not (target / "large.bin").exists(), result.stderr
            assert digest(target / "small.txt") == digest(source / "small.txt")
            print(f"PASS: {target.name}")

    # Terminal record at EOF and test-only interpretation both return nonzero.
    target = root / "failed-last"
    target.mkdir()
    run(["-x", f"-d:{target}"], failed_cases["padded-data"], failed=True)
    assert not (target / "large.bin").exists()
    run(["-t"], failed_cases["padded-data"] + later, failed=True)
    print("PASS: terminal failure at EOF and test mode")

    for mode, switches in (("existing-skip", []), ("excluded", ["-e:large.bin"]),
                           ("overwrite", ["-o"])):
        target = root / mode
        target.mkdir()
        existing = target / "large.bin"
        existing.write_bytes(b"existing destination must survive when skipped")
        run(["-x", *switches, f"-d:{target}"],
            failed_cases["padded-data"] + later, failed=True)
        if mode == "overwrite":
            assert not existing.exists()
        else:
            assert existing.read_bytes() == b"existing destination must survive when skipped"
        assert digest(target / "small.txt") == digest(source / "small.txt")
        print(f"PASS: {mode}")

    child = entry_prefix("recovered\\child.txt", metadata) + stream(1, b"recovered child")
    failed_directory = entry_prefix("recovered", metadata, directory=True) + ABORT
    target = root / "failed-directory"
    target.mkdir()
    run(["-x", f"-d:{target}"], child + failed_directory + later, failed=True)
    assert (target / "recovered" / "child.txt").read_bytes() == b"recovered child"
    assert digest(target / "small.txt") == digest(source / "small.txt")
    print("PASS: failed directory retains recovered children")

    # Successful hard links still use the legacy BACKUP_LINK representation.
    os.link(source / "small.txt", source / "linked.txt")
    archive = root / "links.sa"
    run(["-c", "-s:s", f"-d:{source}", str(archive), "small.txt", "linked.txt"])
    target = root / "links"
    target.mkdir()
    run(["-x", "-s:s", f"-d:{target}", str(archive)])
    assert os.path.samefile(target / "small.txt", target / "linked.txt")
    assert digest(target / "small.txt") == digest(source / "small.txt")
    print("PASS: native hard links")
