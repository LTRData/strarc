#!/usr/bin/env python3
"""Compile actual strarc methods against deterministic in-memory Win32 doubles."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def method(path, signature):
    source = (ROOT / path).read_text(encoding="utf-8-sig")
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


methods = []
for result, signature in [
    ("bool", "IsValidFileHeader("), ("bool", "IsNewFileHeader("),
    ("DWORD", "ReadArchive("), ("DWORD", "ReadStreamHeader("),
    ("bool", "ReadNextFileHeader("), ("void", "WriteArchive("),
    ("void", "BackupFiles("), ("bool", "BackupCurrentDirectory("),
    ("bool", "BackupFile(LPCWSTR"),
]:
    methods.append(result + " " + method("strarc.hpp", signature))
for path, result, signature in [
    ("backup.cpp", "bool", "StrArc::ReadFileStreamsToArchive("),
    ("backup.cpp", "bool", "StrArc::BackupFile("),
    ("backup.cpp", "void", "StrArc::BackupDirectory("),
    ("strarc.cpp", "void", "StrArc::BackupFilenamesFromStreamW("),
    ("strarc.cpp", "void", "StrArc::BackupFilenamesFromStreamA("),
    ("restore.cpp", "bool", "StrArc::RestoreDirectoryTree("),
]:
    methods.append(result + " " + method(path, signature).replace("StrArc::", ""))
# Use the unmodified backup dispatch/reporting tail of Main; setup is provided
# by the fixture. This includes every command-line backup dispatch choice.
main = method("parsecmd.cpp", "StrArc::Main(")
main = main[main.index("    if (dwBufferSize < HEADER_SIZE)"):]
methods.append("int Main(int argc, LPWSTR *argv) {\n" + main)
fixture = (ROOT / "tests/reliability.cpp.in").read_text()
source = fixture.replace("// INSERT_PRODUCTION_METHODS", "\n\n".join(methods))
# Only adapt MSVCRT's integer format spelling for the host C runtime.
source = source.replace("%I64u", "%llu")
# MSVC permits sizeof TYPE without parentheses; GCC requires them.
source = source.replace("sizeof BY_HANDLE_FILE_INFORMATION", "sizeof(BY_HANDLE_FILE_INFORMATION)")
with tempfile.TemporaryDirectory(prefix="strarc-reliability-") as directory:
    cpp = Path(directory) / "reliability.cpp"
    exe = Path(directory) / "reliability"
    cpp.write_text(source)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-fshort-wchar",
                    "-g", "-O1", "-fsanitize=address,undefined",
                    "-fno-sanitize=alignment", "-fno-omit-frame-pointer",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
