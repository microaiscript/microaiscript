"""CLI: pack, unpack, build-exe."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tomllib
from pathlib import Path

from . import aiz_format
from . import minify


def _repo_root_from_package() -> Path:
    # utilities/ais-pack/ais_pack/__main__.py -> repo root is parents[2]
    return Path(__file__).resolve().parent.parent.parent.parent


def load_manifest(path: Path) -> dict:
    data = path.read_bytes()
    if path.suffix.lower() == ".toml":
        return tomllib.loads(data.decode("utf-8"))
    raise ValueError(f"unsupported manifest type: {path}")


def cmd_pack(args: argparse.Namespace) -> int:
    mf_path = Path(args.manifest).resolve()
    root = Path(args.root) if args.root else mf_path.parent
    cfg = load_manifest(mf_path)
    entry = cfg["entry"]
    files = list(cfg["files"])
    do_minify = cfg.get("minify", True) and not args.no_minify

    path_to_data: dict[str, bytes] = {}
    for rel in files:
        p = root / rel
        if not p.is_file():
            print(f"ais-pack: missing file: {p}", file=sys.stderr)
            return 1
        raw = p.read_bytes()
        if do_minify and rel.endswith(".ais"):
            try:
                raw = minify.minify_ais(raw.decode("utf-8")).encode("utf-8")
            except (UnicodeDecodeError, ValueError) as e:
                print(f"ais-pack: minify failed for {rel}: {e}", file=sys.stderr)
                return 1
        # normalize path separators to POSIX for archive
        key = rel.replace("\\", "/")
        path_to_data[key] = raw

    order = [f.replace("\\", "/") for f in files]
    blob = aiz_format.pack_files(entry.replace("\\", "/"), path_to_data, order)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    print(f"wrote {out} ({len(blob)} bytes)")
    return 0


def cmd_unpack(args: argparse.Namespace) -> int:
    blob = Path(args.input).read_bytes()
    manifest, files = aiz_format.unpack_bytes(blob)
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for rel, data in files.items():
        dest = out_dir / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    print(f"extracted {len(files)} files to {out_dir}")
    print(f"entry: {manifest['entry']}")
    return 0


def emit_bundle_c(blob: bytes, out_path: Path) -> None:
    lines = ["#include <stddef.h>", "const unsigned char ais_pack_bundle[] = {"]
    for i in range(0, len(blob), 16):
        chunk = blob[i : i + 16]
        hexes = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f"  {hexes},")
    lines.append("};")
    lines.append(f"const size_t ais_pack_bundle_len = {len(blob)};")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def cmd_build_exe(args: argparse.Namespace) -> int:
    repo = Path(args.repo_root).resolve()
    mf_path = Path(args.manifest).resolve()
    root = Path(args.root) if args.root else mf_path.parent
    cfg = load_manifest(mf_path)
    entry = cfg["entry"]
    files = list(cfg["files"])
    do_minify = cfg.get("minify", True) and not args.no_minify

    path_to_data: dict[str, bytes] = {}
    for rel in files:
        p = root / rel
        if not p.is_file():
            print(f"ais-pack: missing file: {p}", file=sys.stderr)
            return 1
        raw = p.read_bytes()
        if do_minify and rel.endswith(".ais"):
            try:
                raw = minify.minify_ais(raw.decode("utf-8")).encode("utf-8")
            except (UnicodeDecodeError, ValueError) as e:
                print(f"ais-pack: minify failed for {rel}: {e}", file=sys.stderr)
                return 1
        path_to_data[rel.replace("\\", "/")] = raw
    order = [f.replace("\\", "/") for f in files]
    blob = aiz_format.pack_files(entry.replace("\\", "/"), path_to_data, order)

    build_dir = Path(args.build_dir) if args.build_dir else repo / "build" / "ais-pack"
    build_dir.mkdir(parents=True, exist_ok=True)
    bundle_c = build_dir / "ais_pack_bundle_data.c"
    emit_bundle_c(blob, bundle_c)

    liba = repo / "bin" / "libaiscript.a"
    cjson_c = repo / "lib" / "cJSON.c"
    inc_ais = repo / "include"
    inc_lib = repo / "lib"
    launcher = repo / "utilities" / "ais-pack" / "template" / "launcher.c"
    if not liba.is_file():
        print(f"ais-pack: build libaiscript first: {liba} missing (run: make lib)", file=sys.stderr)
        return 1
    if not launcher.is_file():
        print(f"ais-pack: missing {launcher}", file=sys.stderr)
        return 1

    out_exe = Path(args.output).resolve()
    out_exe.parent.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("CC", "cc")
    cmd = [
        cc,
        "-O2",
        "-std=gnu11",
        "-Wall",
        "-Wextra",
        f"-I{inc_ais}",
        f"-I{inc_lib}",
        str(launcher),
        str(cjson_c),
        str(bundle_c),
        str(liba),
        "-o",
        str(out_exe),
        "-lm",
        "-ldl",
        "-lpthread",
        "-lz",
        "-rdynamic",
    ]
    print(" ".join(cmd))
    r = subprocess.run(cmd, cwd=str(repo))
    if r.returncode != 0:
        return r.returncode
    print(f"built {out_exe}")
    return 0


def main() -> int:
    p = argparse.ArgumentParser(prog="ais-pack", description="MicroAiScript .aiz packer")
    sub = p.add_subparsers(dest="cmd", required=True)

    pp = sub.add_parser("pack", help="create a .aiz from ais-pack.toml")
    pp.add_argument("-m", "--manifest", required=True, help="path to ais-pack.toml")
    pp.add_argument("-o", "--output", required=True, help="output .aiz path")
    pp.add_argument("--root", help="project root (default: manifest directory)")
    pp.add_argument("--no-minify", action="store_true", help="do not minify .ais sources")
    pp.set_defaults(func=cmd_pack)

    pu = sub.add_parser("unpack", help="extract a .aiz to a directory")
    pu.add_argument("-i", "--input", required=True, help=".aiz file")
    pu.add_argument("-d", "--output-dir", required=True, help="destination directory")
    pu.set_defaults(func=cmd_unpack)

    pb = sub.add_parser("build-exe", help="pack and link the C launcher (static libaiscript)")
    pb.add_argument("-m", "--manifest", required=True, help="path to ais-pack.toml")
    pb.add_argument("-o", "--output", required=True, help="output executable path")
    pb.add_argument("--root", help="project root for source files (default: manifest directory)")
    pb.add_argument(
        "--repo-root",
        dest="repo_root",
        default=str(_repo_root_from_package()),
        help="AisScript-C repository root (default: auto-detected)",
    )
    pb.add_argument("--build-dir", help="where to write generated C (default: build/ais-pack)")
    pb.add_argument("--no-minify", action="store_true")
    pb.set_defaults(func=cmd_build_exe)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
