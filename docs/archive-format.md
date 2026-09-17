# Archive framing and failed entries (0.4.0)

strarc archives are sequential Windows backup streams with custom file-entry
headers. Multi-byte values are little-endian. `WIN32_STREAM_ID` contributes
exactly **20 bytes** before its variable-length name; do not serialize the C/C++
structure's trailing member or alignment padding.

## Existing entry layout

Each entry begins with a file header:

| Offset | Width | Field | Required value |
| --- | --- | --- | --- |
| 0 | 4 | Stream ID | `BACKUP_INVALID` (0) |
| 4 | 4 | Attributes | `STRARC_MAGIC` (`0xBAC00001`) |
| 8 | 8 | Payload length | 52 or 78 |
| 16 | 4 | Name length | Nonzero, even, at most 65534 |
| 20 | Name length | Name | Relative UTF-16LE path, no terminating NUL |
| After name | Payload length | Metadata | 52-byte `BY_HANDLE_FILE_INFORMATION`, optionally followed by a 26-byte short-name field |

It is followed by zero or more Windows backup streams. Each has the same
20-byte fixed header, then its declared name bytes and payload bytes. A new
custom file header or EOF ends a successful entry. Ordinary successful output
retains this layout; there is no new mandatory success record or archive preamble.

## New terminal failed-entry record

After a recoverable source-stream failure, the writer emits this record at a
stream boundary:

| Offset | Width | Field | Required value |
| --- | --- | --- | --- |
| 0 | 4 | Stream ID | `BACKUP_INVALID` (0) |
| 4 | 4 | Attributes | `STRARC_FAILED_FILE` (`0xBAC00002`) |
| 8 | 8 | Payload length | 4 |
| 16 | 4 | Name length | 0 |
| 20 | 4 | Payload | Original Win32 read error code |

For example, an `ERROR_CRC` (23) record is:

```text
00 00 00 00 02 00 C0 BA 04 00 00 00 00 00 00 00 00 00 00 00 17 00 00 00
```

The record means **the current entry failed during backup**. It is not a
pathname-based delete request. It terminates that entry; only a new file header
or EOF may follow. It is recognized only at a parsed stream boundary, never by
searching inside payload bytes. It is consumed by strarc, never by `BackupWrite`.
Malformed control fields or a truncated error payload are archive errors.

### Writing

[`BackupRead`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-backupread)
continues to use the configured large buffer, as required by the API. A separate reusable
prefix buffer holds at most 20 + 65534 bytes while a stream header/name is being
assembled. The writer validates nonnegative bounded lengths and even, bounded
names before publishing the prefix. The name must also fit the configured
archive buffer, as required by extraction.

On a source error:

1. Ignore all output from the unsuccessful `BackupRead` call, even if it reports
   bytes. Preserve only data from successful calls already written to the archive.
2. Log the filename and error to stderr and release the backup context.
3. If a stream prefix has already been published, fill its remaining payload
   with zeros, using 64-bit remaining-length accounting and bounded writes.
4. Discard any unpublished partial stream prefix and emit the terminal record.
5. Continue with the next source entry. Do not clear the failed file's archive
   attribute or register it as a hard-link source.

A successful zero-byte read is normal EOF only at a stream boundary. Inside a
header, name or payload it is recorded as `ERROR_HANDLE_EOF`. Invalid source
stream lengths/names are recorded as `ERROR_INVALID_DATA` before that prefix is
published. A failure after complete streams but before confirmed EOF still marks
the whole entry failed.

The writer does not retry later ranges of the failed file. The feature preserves
later entries, not a partially readable file. Padding can consume substantial
output space/time. Cancellation or output failure during header, data, padding
or record writes leaves the session stopped; it must not append another entry
behind an incomplete one. No rollback, seek or whole-file staging is performed.

### Extraction and reporting

The extractor reports the recorded source error and discards the regular-file
handle opened for this entry. It does not open a pathname to perform deletion.
Entries skipped because of selection, existing-file rules or test mode are
parsed and reported without deleting an existing destination. Directory entries
are retained because they may contain recovered children; their metadata may
be incomplete. Deletion failures are logged, and a partial regular file may
remain if deletion is denied.

A rejected metadata/data write drains the remaining advertised bytes before
parsing the next stream. The alternate/sparse-stream sub-parser also recognizes
the record and hands it to the enclosing file parser. These paths work through
nonseekable input and recovery read-ahead.

Backup and extraction/testing return nonzero and print an unconditional
`completed with errors` summary when failed entries were encountered. Successful
and failed entry counts are separate; directory entries are included. An entry
whose destination write failed before its terminal record is counted once.
Skipped/tested failure records are still failures of the archive and are reported.
Ordinary pre-header open failures retain the legacy skip-and-log behavior and
are not part of the new failed-stream count.

The internal pipe-based copy helper also treats recorded source/destination
failures as failed copies. It restores through a duplicate of the target handle
so cleanup does not close the caller-owned handle.

The C++ API exposes `GetFailedFileCount()`. `HasBackupFailed()` reports either
recorded failures or an unfinished entry. A `BackupFile()` call returns false
for an entry that was marked failed, but subsequent calls may continue once its
failure record has been written. The internal structural-failure latch remains
set after cancellation/output failure and prevents further appends.

Extraction with overwrite enabled is not transactional. Removing an entry after
it failed cannot restore a destination that was already overwritten, and directory
metadata already applied is not rolled back. Restore into a fresh directory when
preserving existing destinations matters.

## Compatibility and limits

Use an extractor/tester at least as new as the writer; failed-entry records require
**0.4.0 or later**. The archive has no global version negotiation. Older extractors
may ignore/reject the record or retain zero-padded data, so their behavior is not
supported for archives containing these records. New extractors retain support
for older archives.

No checksums, authenticated failure markers or global completeness manifest are
added. If the archive is truncated exactly before a terminal record, an extractor
cannot distinguish that from the existing success-at-EOF layout. Review the
backup log and status; this is not an integrity or transactional-restore format.

Buffered resynchronization remains available for damaged older archives, but
cannot recover headers already consumed as payload of an earlier short stream.
