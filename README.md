# MicroAiScript

MicroAiScript is an implementation of [AiScript](https://github.com/aiscript-dev/aiscript)
in C. It provides a command-line interpreter, a REPL, an embeddable library
(`libaiscript`) and an experimental bytecode compiler and VM. Its only hard
dependencies are a C11 compiler, libc, libm and pthreads, so it runs where a
JavaScript engine is too large or unavailable.

- **Language version:** AiScript 1.3.0 (`Core:v` reports `1.3.0-c`). The 1.3.0
  specification is still a development preview upstream; see
  [CHANGELOG.md](CHANGELOG.md) and [docs/conformance.md](docs/conformance.md) for what is and is not
  covered.
- **Extensions beyond the specification:** native module loading, `.ais` imports
  with a search path, module metadata, a bytecode format, and a packer. They are
  documented in [docs/extensions.md](docs/extensions.md).
- **License:** MIT. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for bundled and optional components.

## Build

Requirements: a C11 compiler (GCC or Clang), `make`, and optionally
[GNU Readline](https://tiswww.case.edu/php/chet/readline/rltop.html) for line
editing in the REPL (detected through `pkg-config`, or by header; the build
works without it).

```sh
make              # builds bin/aiscript
make lib          # builds bin/libaiscript.so and bin/libaiscript.a
make vm           # builds bin/ais-vm, the bytecode runner
sudo make install # installs bin/aiscript under /usr/local (override with PREFIX=)
sudo make install-lib   # installs the libraries, headers and aiscript.pc
```

Windows is supported with MinGW (`make` detects `Windows_NT`); the Makefile links
Readline statically in that case.

Standard-library namespaces can be compiled out to shrink the binary by adding
`-DNO_DATE`, `-DNO_JSON`, `-DNO_URI` and similar flags to `CFLAGS` (see the
`NO_*` macros in `src/std_*.c`).
`-DNO_PYTHONIC_ERRORS` switches the Python-style tracebacks off in favour of
one-line error messages.

## Usage

```sh
bin/aiscript                       # interactive REPL, ".exit" or Ctrl-D to quit
bin/aiscript examples/hello.ais    # run a file
bin/aiscript -e '<: Core:v'        # run a string
bin/aiscript --help
```

`<:` prints a value. The process exits with status 1 on a syntax error or an
uncaught runtime error.

```aiscript
@fib(n) {
    if (n < 2) n else fib(n - 1) + fib(n - 2)
}

let results = Arr:create(10).map(@(_, i) { fib(i) })
<: results

// 1.3.0: object shorthand
let name = "AiScript"
let version = Core:v
<: { name, version }
```

More programs are in [examples/](examples/).

## Embedding

Link against `libaiscript` and include `aiscript.h`:

```c
#include <aiscript.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    as_interpreter_t* interp = as_interpreter_new();
    char* error = NULL;

    as_value_t* result = as_eval(interp, "<: 1 + 2", &error);
    if (error) {
        fprintf(stderr, "%s\n", error);
        free(error);
    } else if (!result) {
        fprintf(stderr, "%s\n", as_interpreter_get_error(interp));
    }

    as_value_free(result);
    as_interpreter_free(interp);
    return 0;
}
```

```sh
cc demo.c $(pkg-config --cflags --libs aiscript) -o demo
```

The full guide (I/O callbacks, native functions, calling script functions,
ownership rules) is in [docs/embedding.md](docs/embedding.md).

## Documentation

| Document | Contents |
|----------|----------|
| [docs/reference/](docs/reference/) | AiScript language and standard-library reference |
| [docs/embedding.md](docs/embedding.md) | Embedding API guide |
| [docs/extensions.md](docs/extensions.md) | Features that go beyond the AiScript specification |
| [docs/architecture.md](docs/architecture.md) | How the implementation is organised |
| [docs/testing.md](docs/testing.md) | Test suites and how to run them |
| [docs/conformance.md](docs/conformance.md) | Spec conformance status and known gaps |
| [docs/MODULE_METADATA.md](docs/MODULE_METADATA.md) | Metadata for native modules |
| [docs/SELECTIVE_IMPORTS.md](docs/SELECTIVE_IMPORTS.md) | `from ... import ...` |
| [docs/GTK_UI_MODULE.md](docs/GTK_UI_MODULE.md) | GTK UI example module |
| [docs/aiz-format.md](docs/aiz-format.md) | `.aiz` bundle format used by ais-pack |
| [aic-cross/README.md](aic-cross/README.md) | Bytecode compiler |
| [utilities/ais-pack/README.md](utilities/ais-pack/README.md) | Bundling tool |

## Repository layout

```
src/            interpreter, parser, standard library, VM (src/tools/ais-vm.c is the VM runner)
include/        public and internal headers; aiscript.h is the embedding API
aic-cross/      .ais to .aic bytecode compiler
modules/        optional native modules (HTTP client with libcurl, HTTP server with libmicrohttpd2)
utilities/      ais-pack, a bundler and standalone-executable builder
examples/       example programs and native-module sources
test/           unit tests, script regression tests and the conformance suite
lib/            vendored cJSON (used only by the conformance runner)
docs/           documentation
```

## Testing

```sh
make test               # C unit tests
make test_scripts       # script regression tests (test/regression)
make test_conformance   # AiScript conformance cases, run through the interpreter and the VM
make test_basic_vm      # bytecode smoke tests
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
