# strarc — Stream Archiver

A Windows command-line backup and restore tool that streams files and their metadata through the Windows backup APIs. It uses its own archive format, with file headers followed by backup streams; it does not create tar or ZIP files.

The source identifies the current version as **0.3.0l**. The detailed [strarc manual](strarc.txt) describes **0.3.0g** from March 2015 and remains useful for options, backup strategies and historical limitations. Use `strarc -?` for the current command-line reference.

## Capabilities

- Archive and restore file data, alternate data streams, security information, timestamps and attributes, subject to filesystem support and available privileges.
- Track hard links, preserve reparse points with `-j`, and handle sparse files and short 8.3 names.
- Stream archives through standard input/output or external compression programs.
- Select files with explicit lists or path filters, and use archive-attribute-based full, differential or incremental backups.
- Save snapshots of loaded registry hives with `-r`.
- Use the C++ `StrArc` class from [strarc.hpp](strarc.hpp); the NMAKE build produces both `strarc.exe` and `strarc.lib`.

The executable attempts to enable the caller's backup and restore privileges. It does not grant missing privileges; file access and metadata restoration can still fail. Use an appropriately privileged account when backing up or restoring protected files and security information.

## Quick start

These examples use Windows Command Prompt (`cmd.exe`). Create `D:\Backups` first and keep archive files outside the source tree.

Create an archive of `C:\Data`, storing junctions and other reparse points themselves:

```bat
strarc -c -j "-d:C:\Data" D:\Backups\data.sa
```

List the archive and then extract into a new destination directory:

```bat
strarc -t D:\Backups\data.sa
strarc -x -l "-d:D:\RestoredData" D:\Backups\data.sa
```

Preview which files would be backed up, without creating an archive:

```bat
strarc -c -n -l "-d:C:\Data"
```

Copy a tree through a pipe into a separate destination:

```bat
strarc -c -j "-d:C:\Data" | strarc -x -l "-d:D:\DataCopy"
```

Put switches before the archive name. Without an archive filename, creation writes to stdout and extraction/listing reads stdin. To supply backup file lists while using stdout, use `-` as the archive-name placeholder. Relative archive names are resolved before the working directory specified by `-d` is opened.

Creating a named archive overwrites it unless `-a` is used. Extraction skips existing files by default; `-o` enables replacement, with conditional variants documented in the manual. Restore to a fresh directory first and review diagnostics before replacing existing data.

## Common options

| Option | Purpose |
| --- | --- |
| `-c`, `-x`, `-t` | Create, extract, or read/list an archive without extracting its files. Select one mode. |
| `-d:DIR` | Select the source or destination directory. Extraction can create the destination. |
| `-j` | Back up reparse points instead of following their targets. |
| `-l` | List filenames during backup or restore. Cannot accompany archive output to stdout, `-t` or `-v`. |
| `-v` | Send detailed diagnostics to stderr. |
| `-e:LIST`, `-i:LIST` | Exclude/include comma-separated relative-path substrings; exclusions take precedence. |
| `-f`, `-F` | Read backup filenames from stdin as ANSI or Unicode text, respectively. |
| `-s:s` | Skip security information during backup or restore. See the manual for other metadata-skip flags. |
| `-m:f`, `-m:d`, `-m:i` | Full, differential or incremental selection using archive attributes. Full and incremental modes clear those attributes on successfully backed-up files. |
| `-z:CMD` | Run an external filter for archive I/O, such as a compression/decompression utility. |
| `-b:SIZE` | Set the stream buffer size. The current source default is **128 KiB**, overridable at compile time. |

Without `-m`, the default copy-backup method includes files without clearing their archive attributes. Archive compression is supplied by an external program, not built into the format.

## Backup and restore limits

The manual's Windows NT/2000/XP/2003 wording and version-specific bug notes are historical, not a current compatibility matrix. In particular, its 512 KiB buffer default differs from the current source.

EFS encryption state is not preserved: the archive is not encrypted by strarc, and extraction does not re-encrypt the files. Compression state of individual alternate data streams is also not preserved. Short-name and security restoration depend on the target filesystem and privileges.

The `-r` feature snapshots registry hives; it does not provide a VSS snapshot or application-consistent capture of an entire live system. Its temporary `.$sards` files can remain if the run is interrupted or their directories are excluded. See the manual's registry-backup section before using it.

`-t` reads the archive and reports structural/read errors, but the format has no cryptographic integrity check. Review stderr and perform a trial restore for important backups; per-file errors can be reported while the command continues, so a zero exit code alone does not prove a complete backup or restore.

## Building

The repository has a Visual Studio solution and a separate NMAKE build. The checked-in Visual C++ project selects:

| Platform | Debug toolset | Release toolset |
| --- | --- | --- |
| Win32 | v120 | v90 |
| x64 | v120 | v90 |
| ARM | v140 | v140 |

Both build routes require shared LTR Data headers and libraries outside this repository. Headers such as `winstrct.h`, `ntfileio.hpp` and `spsleep.h` are in [LTRData/include](https://github.com/LTRData/include). The project imports `..\winstrct.props` and, for some configurations, absolute paths to the maintainer's WDK 7 and signing property sheets. The local `strarc.props` also contains a fixed WDK library path. Arrange or adapt these dependencies before building.

The root [Makefile](Makefile) uses `cl`, `link`, `lib` and `rc`, writes outputs under the CPU directory, and includes ARM/ARM64 branches as well as x86/x64 handling. It uses `_BUILDARCH` when set, otherwise `CPU` or an `i386` default, and explicitly requires `..\lib\minwcrt.lib`. Its install targets contain maintainer-specific drive paths. These are legacy build configurations, not a self-contained modern SDK build.

## License and history

[MIT License](LICENSE), by Olof Lagerkvist. The standalone license preserves the notice from [strarc.txt](strarc.txt), which remains unchanged.

Some code originated in Olof Lagerkvist's commercial `ntarc` tool from 1998–2000 and was subsequently released as part of strarc, as described in the original manual.
