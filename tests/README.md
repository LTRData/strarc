# Archive reliability regression checks

## Portable fault injection and scanning tests

On Linux with Python 3 and GCC (including AddressSanitizer/UndefinedBehaviorSanitizer):

```sh
UBSAN_OPTIONS=halt_on_error=1 python3 tests/run-reliability.py
```

For containers where LeakSanitizer cannot inspect `/proc`, add
`ASAN_OPTIONS=detect_leaks=0`. AddressSanitizer remains enabled. Set `CXX` to select
another GCC-compatible compiler.

The runner extracts the **actual production method bodies** into a temporary
translation unit, compiles them against deterministic in-memory Win32 doubles,
and executes assertions. It includes `BackupFile`, `BackupDirectory`,
`ReadFileStreamsToArchive`, all filename-list entry paths, the backup/restore
dispatch and reporting tail of `Main`, `RestoreDirectoryTree`, the stream restore
methods, link tracker, archive readers and `BackupCopyFile`.
The runner adapts only MSVC's `sizeof TYPE` syntax and `%I64u` format spelling.
The fixture supplies the `header` property as a macro. No generated files are
written into the repository.

Coverage:

- Continue after a source error in recursive traversal, explicit filename lists,
  ANSI stdin and Unicode stdin; report nonzero with successful/failed counts,
  with verbose mode on and off. Open-error skipping is preserved.
- Inject a failed read at every byte of a multi-stream source, including header,
  name, payload and between-stream boundaries. Compare the resulting bytes to
  an independent framing model. Discard bytes returned by a failed call.
- Treat premature successful zero-byte reads as failures inside streams/prefixes.
  Retain byte-for-byte successful output across physical read-buffer boundaries.
  Assert all non-abort `BackupRead` requests exceed `sizeof(WIN32_STREAM_ID)`.
- Extract failed-entry archives using the production stream parser, `BackupWrite`
  loop, alternate/sparse-stream loop, discard helper and command-line summaries.
  Fail destination writes in the first and later buffers; reject padded metadata;
  verify draining, next-file recovery and one failure count per entry.
- Handle markers at EOF, ignore marker-like payload bytes, reject malformed or
  truncated markers, preserve skipped/test-mode destinations and directories,
  and report deletion failure while continuing.
- Register only successful hard-link sources (using the real `LinkTrackerItem`
  implementation), and preserve the archive attribute on failed source files.
- Exercise padding beyond 4 GiB with a counting output sink, without allocating
  or writing that much data. Stop on cancellation during padding or failed
  output during header/padding/terminal-record writes.
- Exercise the actual copy helper with synchronous in-memory pipe/thread
  substitutes; source/destination failures must return false and only the
  duplicate target handle is closed by the helper.
- Retain the original buffered resynchronization tests: 8 MiB damaged region,
  all 19 header splits at 64/128 KiB boundaries, short-reading pipes, invalid
  candidates, EOF, already-buffered headers and 100 small cached entries.

This is a control-flow and byte-preservation harness, **not a Windows/NTFS
integration test**. It substitutes source/destination handles, base-file opening,
selection, final metadata, and native API calls. It runs the actual stream restore
methods, but does not prove native ACL, sparse-file or reparse-point behavior.
Hard-link output is checked at the archive level; hard-link extraction needs the
native smoke test. Alignment sanitization is excluded because existing code
stores metadata through potentially unaligned pointers; other address and
undefined-behavior checks remain enabled.

## Native Windows smoke test

Build `strarc.exe` using the repository's existing Windows build/dependencies,
then run on a disposable NTFS test location:

```powershell
python tests/windows-smoke.py C:\path\to\strarc.exe
```

This creates an ordinary archive with an alternate data stream and restores
through files and pipes, including all 19 recovery-boundary splits and an 8 MiB
invalid prefix. It compares restored data/alternate-stream hashes. It then
constructs failed-entry derivatives from native-produced file headers and checks
zero-padded data, rejected security metadata, alternate-stream failures, a terminal
marker at EOF, test mode, exclusions, existing-file skipping, overwrite deletion,
retained directories and hard links. All paths are disposable temporary paths.
The derivatives are extraction fixtures, not evidence of native `BackupRead`
source failures; deterministic writer fault injection is in the portable harness.

The native script must be run with a newly built **0.3.0m** executable on NTFS.
It has not been run in the Linux development environment. Use Procmon filtered
to `input.sa` to inspect recovery read sizes. No real disk damage is induced.

## Scope of recovery

Failed source entries are padded and marked, allowing later files to be restored.
Cancellation or failed archive output still stops backup. Resynchronization of
older damaged archives only searches bytes the restore parser has reached; it
cannot recover later headers already consumed as payload of an incomplete
stream. See [format details and compatibility](../docs/archive-format.md).
