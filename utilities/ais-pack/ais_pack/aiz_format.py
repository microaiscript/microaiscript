"""Read/write .aiz bundles — see docs/aiz-format.md."""

from __future__ import annotations

import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from typing import Dict, List, Tuple

MAGIC = b"AIZ\x01"
FORMAT_VERSION = 1
MANIFEST_VERSION = 1


@dataclass
class AizManifest:
    entry: str
    files: List[dict]  # {"path": str, "size": int, "sha256": str (optional)}

    def to_json_bytes(self) -> bytes:
        obj = {
            "aiz_manifest_version": MANIFEST_VERSION,
            "entry": self.entry,
            "files": self.files,
        }
        return json.dumps(obj, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def pack_files(
    entry: str,
    path_to_data: Dict[str, bytes],
    file_order: List[str],
) -> bytes:
    """Build a .aiz blob: *file_order* determines concatenation order; keys are POSIX paths."""
    raw_parts: List[bytes] = []
    files_meta: List[dict] = []
    for rel in file_order:
        if rel not in path_to_data:
            raise KeyError(f"missing file data for {rel!r}")
        data = path_to_data[rel]
        h = hashlib.sha256(data).hexdigest()
        files_meta.append({"path": rel, "size": len(data), "sha256": h})
        raw_parts.append(data)
    raw = b"".join(raw_parts)
    manifest = AizManifest(entry=entry, files=files_meta)
    mj = manifest.to_json_bytes()
    payload = zlib.compress(raw, level=9)
    header = MAGIC + struct.pack("<II", FORMAT_VERSION, len(mj)) + mj
    return header + payload


def unpack_bytes(blob: bytes) -> Tuple[dict, Dict[str, bytes]]:
    """Return (manifest_dict, path -> bytes)."""
    if len(blob) < 12:
        raise ValueError(".aiz too small")
    if blob[:4] != MAGIC:
        raise ValueError("bad .aiz magic")
    ver, mj_len = struct.unpack("<II", blob[4:12])
    if ver != FORMAT_VERSION:
        raise ValueError(f"unsupported format version {ver}")
    if len(blob) < 12 + mj_len:
        raise ValueError("truncated manifest")
    mj = blob[12 : 12 + mj_len].decode("utf-8")
    manifest = json.loads(mj)
    if manifest.get("aiz_manifest_version") != MANIFEST_VERSION:
        raise ValueError("unsupported manifest version")
    zdata = blob[12 + mj_len :]
    raw = zlib.decompress(zdata)
    files = manifest["files"]
    out: Dict[str, bytes] = {}
    off = 0
    for ent in files:
        path = ent["path"]
        size = int(ent["size"])
        chunk = raw[off : off + size]
        if len(chunk) != size:
            raise ValueError(f"short read for {path}")
        if "sha256" in ent:
            if hashlib.sha256(chunk).hexdigest() != ent["sha256"]:
                raise ValueError(f"sha256 mismatch for {path}")
        out[path] = chunk
        off += size
    if off != len(raw):
        raise ValueError("payload size does not match file table")
    return manifest, out
