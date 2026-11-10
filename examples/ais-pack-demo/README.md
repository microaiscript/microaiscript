# ais-pack demo

This directory shows a minimal **main.ais** plus a native **`demo_native.so`** packed into a `.aiz` archive or a single executable that embeds **libaiscript.a** and the bundle.

## Prerequisites

- Built interpreter libraries from the repo root: `make lib` (produces `bin/libaiscript.a`).
- Python 3.11+ (for `tomllib` in `ais-pack`).
- A C toolchain (`cc`, `zlib`, `pthread`) for `build-exe`.

## Build the native module

From this directory:

```bash
make native
```

This creates `lib/demo_native.so`.

## Create a `.aiz` file

```bash
make pack
```

Writes `demo.aiz`. Inspect contents with:

```bash
../../utilities/ais-pack/ais-pack unpack -i demo.aiz -d _unpack
```

## Build a standalone executable (launcher + static libaiscript + bundle)

From this directory:

```bash
make exe
```

Runs `ais-pack build-exe`, linking against `bin/libaiscript.a`, and writes `demo-packed` in this directory. Run it:

```bash
./demo-packed
```

You should see the `print` output and `hi from demo_native` from the native module.

## Manifest

See `ais-pack.toml`: `entry` is the script run after extraction; `files` lists every `.ais` and `.so` path to include (POSIX paths relative to this folder).
