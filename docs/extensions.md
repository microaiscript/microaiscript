# Extensions beyond the AiScript specification

MicroAiScript implements the AiScript language as specified upstream, and adds the
features below. Programs that rely on them will not run on other AiScript
implementations. Everything in this document is specific to this project.

## Module system

AiScript itself has no module system. Here it is provided by `import` and `from`
statements, which are only allowed at the top level of a file.

```aiscript
import "math_utils.ais"                     // namespace named after the file: math_utils:square(2)
import "math_utils.ais" as M                // explicit namespace: M:square(2)
from "math_utils.ais" import square, cube   // names bound directly
import "native/hello.so"                    // native module
```

Selective imports are covered in [SELECTIVE_IMPORTS.md](SELECTIVE_IMPORTS.md).

- **Lookup.** A module name is searched in each directory of `Core:modpath` in
  order, as `<dir>/<name>/__init__.ais` and then `<dir>/<name>.ais`. The default
  path is `.`, `/usr/local/lib/aiscript`, `/usr/lib/aiscript` and
  `~/.local/lib/aiscript` (Windows has its own defaults). `Core:modpath` is an
  ordinary array that scripts may modify.
- **Caching.** A module is loaded once per interpreter. `Core:reload_module(name)`
  forces a reload and `Core:unload_module(name)` drops it.
- **Metadata.** A module may declare `### { ... }` metadata, readable with
  `Core:metadata(name)`. See [MODULE_METADATA.md](MODULE_METADATA.md).

## Native modules

A shared library (`.so`, `.dylib`, `.dll`) can be imported. It must export

```c
AS_EXPORT void as_modinit_<name>(as_interpreter_t* interp, as_scope_t* scope);
AS_EXPORT as_module_metadata_t* as_modmeta_<name>(void);   /* optional */
```

where `<name>` is the file name without directory or extension. The init function
registers functions with `as_scope_add()` and `as_value_fn_native()`. The
`AS_EXPORT` macro and metadata struct come from [`aiscript.h`](../include/aiscript.h).
Build the executable with `-rdynamic` (the default Makefile does) or link modules
against `libaiscript`.

Examples:

| Source | Purpose |
|--------|---------|
| `examples/native/hello.c`, `math_ext.c`, `metadata_test.c` | Minimal modules |
| `examples/native/ui_gtk.c` | GTK 3 UI binding, see [GTK_UI_MODULE.md](GTK_UI_MODULE.md) |
| `modules/http-client/` | HTTP client on libcurl |
| `modules/http-server/` | HTTP server on libmicrohttpd2 (fetch it with `fetch-mhd2.sh`) |

Native modules run arbitrary code with the privileges of the process.

## Error reporting

Runtime errors from the interpreter are printed as Python-style tracebacks with
file, line and "did you mean" suggestions for misspelled names, and the process
exits with status 1. Build with `-DNO_PYTHONIC_ERRORS` to get a single
`Runtime error: ...` line instead. Fatal signals (for example a segmentation
fault) are caught and reported as `Fatal AiScript error`; set
`AISCRIPT_COREDUMP=1` to get a core dump anyway.

## Embedding API

The C API in [`aiscript.h`](../include/aiscript.h) is not part of the language;
see [embedding.md](embedding.md).

## Bytecode and the VM

`aic-cross` compiles a program to `.aic` bytecode and `ais-vm` runs it; see
[`aic-cross/README.md`](../aic-cross/README.md). The VM is experimental and
differs from the interpreter in some corners. It binds extra host functions
`peek(addr)` and `poke(addr, value)` for raw memory access on microcontrollers.
Never run untrusted bytecode with those available.

## Packaging

`utilities/ais-pack` bundles a script, its modules and native libraries into an
`.aiz` archive ([format](aiz-format.md)) and can build a standalone executable
around `libaiscript`. See [`utilities/ais-pack/README.md`](../utilities/ais-pack/README.md).

## Standard library differences

- `Core:modpath`, `Core:metadata`, `Core:unload_module` and `Core:reload_module`
  are additions.
- `Core:v` is the specification version with a `-c` suffix (`1.3.0-c`).
- Printing an object shows its contents (`{ a: 1 }`); an array is shown as
  `[1, 2]`, where upstream prints `[ 1, 2 ]`.
- `docs/reference/misskey-extended-api.md` documents the Misskey host API (the
  `Mk:` namespace, `USER_ID`, ...). It is upstream documentation kept for
  reference; the core interpreter does not provide it, and only the GTK example
  module offers a partial `Ui:`/`Mk:` surface.
