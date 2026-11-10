# `.aiz` bundle format (version 1)

MicroAiScript **ais-pack** produces a single-file archive of AiScript sources (typically minified) and native shared objects. The layout is fixed and little-endian.

## File layout

| Offset | Size | Description |
|--------|------|-------------|
| 0 | 4 | Magic bytes: `0x41 0x49 0x5A 0x01` (`AIZ` + version byte `1`) |
| 4 | 4 | **Format version** (`uint32_t`, LE). Currently `1`. |
| 8 | 4 | **Manifest length** (`uint32_t`, LE): byte length of the UTF-8 JSON manifest (uncompressed). |
| 12 | `manifest_len` | **Manifest** (UTF-8 JSON text, no NUL terminator stored). |
| `12 + manifest_len` | remainder | **Payload**: zlib (**RFC 1950**) compressed blob. |

The **uncompressed** payload is the concatenation of every file’s raw bytes, in the same order as the `files` array in the manifest.

## Manifest JSON

Top-level object:

| Field | Type | Meaning |
|-------|------|---------|
| `aiz_manifest_version` | number | Must be `1`. |
| `entry` | string | POSIX-style path relative to the extract root (e.g. `main.ais`). Must match one of `files[].path`. |
| `files` | array | Ordered list of file entries (see below). |

Each element of `files`:

| Field | Type | Meaning |
|-------|------|---------|
| `path` | string | Relative path using `/` (no leading slash). |
| `size` | number | Uncompressed size in bytes within the payload concatenation. |
| `sha256` | string (optional) | Lowercase hex SHA-256 of the file bytes (for verification). |

## Compression

The payload is exactly `zlib.compress(raw_payload)` in Python 3 (`zlib` module, default wbits), i.e. zlib wrapper with Adler-32. Decompress with zlib (e.g. C `uncompress()` from `-lz`, or Python `zlib.decompress`).

## Interoperability

Readers should reject unknown **format version** values in the header (bytes 4–7). The first byte of the magic (after `AIZ`) may increment for breaking changes; **manifest** versioning is separate (`aiz_manifest_version`).
