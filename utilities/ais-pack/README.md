# ais-pack

Python 3 tool to bundle AiScript sources (optionally minified) and native `.so` files into a **`.aiz`** zlib archive, and to link a small **C launcher** that embeds **`libaiscript.a`** and runs the bundle from a temp directory.

## Requirements

- **Python 3.11+** (stdlib `tomllib` for `ais-pack.toml`)
- **`make lib`** at the repository root (`bin/libaiscript.a`)
- **`zlib`** development library for the C launcher (`-lz`; typical on Linux)

## Usage

From the repo root (or set `PYTHONPATH=utilities/ais-pack`):

```bash
./utilities/ais-pack/ais-pack pack -m path/to/ais-pack.toml -o app.aiz
./utilities/ais-pack/ais-pack unpack -i app.aiz -d out/
./utilities/ais-pack/ais-pack build-exe -m path/to/ais-pack.toml -o myapp
```

`build-exe` compiles `template/launcher.c` with `lib/cJSON.c`, generated bundle data, and `bin/libaiscript.a`. Override the compiler with `CC=gcc`.

## Format

See [docs/aiz-format.md](../../docs/aiz-format.md).

## Example

See [examples/ais-pack-demo](../../examples/ais-pack-demo).
