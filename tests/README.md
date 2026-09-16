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
`ReadFileStreamsToArchive`, all filename-list entry paths, the backup dispatch
and reporting tail of `Main`, `RestoreDirectoryTree`, and the archive readers.
The runner adapts only MSVC's `sizeof TYPE` syntax and `%I64u` format spelling.
The fixture supplies the `header` property as a macro. No generated files are
written into the repository.

Coverage:

- Fail `BackupRead` after its stream header and 117 payload bytes have been
  written. Verify nonzero command-line result, an unconditional aborted message,
  no success message, context cleanup, handle closure, and no subsequent file or
  enclosing directory header appended. Exercise directory recursion, explicit
  filenames, ANSI stdin and Unicode stdin, with verbose mode both on and off.
- Verify missing/unopenable files are still skipped, successful files continue,
  and a failed session cannot append another file through a later API call.
- Round-trip ordinary file data through real backup/header/traversal code and
  a minimal data-stream restore sink. Check normal EOF produces no corruption
  diagnostic.
- Recover across an 8 MiB damaged region. Count raw `ReadFile` requests and assert
  that scanning uses block requests, with no one-byte requests for that case.
- Find every possible split of a 20-byte header across first and later scan
  blocks, at 64 KiB and 128 KiB buffer sizes, also with short-reading,
  non-seekable pipe doubles. No seek operation is available in the fixture.
- Reject wrong stream IDs, magic values, metadata sizes (including nonzero high
  DWORD), and zero, odd or oversized filename lengths. Cover truncated EOF,
  recovery from an already-read invalid header, and preservation of 100 small
  files in the read-ahead suffix.

This is a control-flow and byte-preservation harness, **not a Windows/NTFS
integration test**. It substitutes filesystem enumeration, source reads and
restore writes, and does not validate ACLs, alternate streams, sparse files,
reparse points or native ABI/build compatibility. Alignment sanitization is
excluded because existing backup code writes metadata via potentially unaligned
struct pointers; other undefined-behavior checks remain enabled. The new scanner
copies candidate headers into aligned locals.

## Native Windows smoke test

Build `strarc.exe` using the repository's existing Windows build/dependencies,
then run on a disposable NTFS test location:

```powershell
python tests/windows-smoke.py C:\path\to\strarc.exe
```

This creates an ordinary two-file archive, restores it from a file and a pipe,
then prefixes damage so each of the 19 possible header splits crosses the first
scan block boundary. It compares SHA-256 hashes of both restored files in every
case and also exercises an 8 MiB damaged prefix. All files are under a temporary
directory, removed on completion. Security streams are omitted for this smoke
test; it does not require modifying real source files or inducing disk errors.
Use Procmon filtered to the `input.sa` archive to inspect recovery read sizes.
The portable harness above covers deterministic mid-stream source failures.

## Scope of recovery

Resynchronization only searches bytes the restore parser has reached. If an old
archive contains a short stream followed by later files, the recorded stream
length can still consume those files as payload before recovery starts. Faster
scanning cannot guarantee recovery of those files. Creation now stops at the
first source-stream failure, leaves the partial file in place, and returns a
failure. Rollback/truncation and continued backup on seekable archives are
intentionally deferred; the format is unchanged.
