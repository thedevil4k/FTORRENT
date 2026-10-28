# Archive test fixtures

These files are small, read-only inputs for `tests/archive_inspector.cpp`. They
are never executed or extracted by the application or the tests.

## RAR5 fixtures

| file | source |
| :--- | :--- |
| `test_read_format_rar5_stored.rar` | <https://github.com/libarchive/libarchive/blob/803c67fbacb0e46d95c2fa991f5380786f30604e/libarchive/test/test_read_format_rar5_stored.rar.uu> |
| `test_read_format_rar5_compressed.rar` | <https://github.com/libarchive/libarchive/blob/803c67fbacb0e46d95c2fa991f5380786f30604e/libarchive/test/test_read_format_rar5_compressed.rar.uu> |
| `test_read_format_rar5_encrypted.rar` | <https://github.com/libarchive/libarchive/blob/803c67fbacb0e46d95c2fa991f5380786f30604e/libarchive/test/test_read_format_rar5_encrypted.rar.uu> |

The `.uu` files were decoded to their original bytes. They come from the
libarchive test suite, which is distributed under the BSD 2-Clause License:

```text
Copyright (c) 2003-2026, The libarchive project
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

The RAR5 fixtures contain only plain text and binary test payloads. None of
them contains an `.exe`.

## `setup.zip`

`setup.zip` was generated for this repository. It contains synthetic bytes
only; there is no real executable or malware inside. Its purpose is to exercise
the listing rules for hostile names:

- `setup.exe` — a plain executable entry.
- `docs/readme.txt` — a non-executable entry that must not be listed.
- `invoice.pdf.exe` — a double extension.
- `../escape.exe` — a traversal-style name that must be reported verbatim,
  never used as a filesystem path.

The SHA-256 of the `setup.exe` payload is:

```text
be1307d1e38db0d22f03095558630f1c748ca3451b48d0f252dce4ac2226b97d
```
