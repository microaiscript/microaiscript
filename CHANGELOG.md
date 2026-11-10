# Changelog

All notable changes are documented here. The version number follows the AiScript
language specification that is implemented, with a `-c` suffix for this C
implementation.

## 1.3.0-c

### Language (AiScript 1.3.0)

- Object shorthand: `{ a }` is `{ a: a }`, in object literals and in object
  destructuring (`let { value } = obj`). Only plain identifiers qualify;
  `{ "a" }` and `{ exists }` are syntax errors.
- Duplicate keys in object literals and destructuring patterns are a syntax error.
- `Math:gen_rng`: a `null` (or other non-number, non-string) seed is rejected,
  `options` must be an object with a string `algorithm`, and ChaCha20 now
  derives numeric seeds from all 8 bytes of the number. Pass
  `chacha20_number_seed_legacy_behavior: true` for the previous behaviour.
  Sequences produced from the same numeric seed differ from 1.2.x.
- New `help` constant.
- Exponent notation (`1e3`) is no longer scanned as a number; it was never part of
  the specification.
- Attribute values are evaluated, including for namespace members.

### Fixes

- Syntax errors are reported by `as_eval`/`as_parse` and make the CLI exit with
  status 1. Previously most were printed to stderr and execution continued.
- Runtime errors raised by native functions make the CLI exit with status 1 and
  print a message; they are no longer swallowed when raised inside a function.
- Reading an unknown property of a string, array or number (for example
  `"".constructor`) is a runtime error instead of a crash.
- `x / 0` is `Infinity`/`-Infinity` and `0 / 0` is `NaN`, in the interpreter and the VM.
- Numbers print as `NaN`, `Infinity` and `-Infinity`, and with the shortest digits
  that round-trip (`0.1 + 0.2` prints `0.30000000000000004`).
- Objects print their contents (`{ a: 1 }`) instead of `obj<...>`.
- An unlabeled `eval { ... }` at the start of a statement can be an operand
  (`eval { return 1 } && false`), and `;` after a top-level namespace is accepted.
- The legacy ChaCha20 seed conversion no longer relies on undefined behaviour for
  out-of-range numbers.
- `libaiscript` no longer requires GNU Readline to link.
- The unit-test framework now counts failed assertions as failures.

### API

- `as_parse()` takes a `size_t* count_out` argument so callers can learn the node
  count (**breaking**: it was unusable before).
- The `err` callback of `as_interp_opts_t` is now invoked for runtime errors.
- `as_eval` clears the error left by a previous call.
- All public functions are marked `AISCRIPT_API`; `as_num_to_str()` is new.
- `AISCRIPT_VERSION` is `"1.3.0-c"`.

### Project

- Removed generated binaries, scratch tests and development notes from the
  repository; build output is ignored.
- libmicrohttpd2 is no longer vendored: `modules/http-server/fetch-mhd2.sh`
  downloads a pinned commit and applies `patches/`.
- The vendored `lib/cJSON.*` (needed by the conformance runner) is tracked.
- Added documentation: README, `docs/embedding.md`, `docs/extensions.md`,
  `docs/architecture.md`, `docs/testing.md`, `docs/conformance.md`.
- Build is warning-free with `-Wall -Wextra`; dead code was removed.
- New `make test_scripts` target; removed the broken `test_bytecode` target.

## 1.2.1-c

Initial tracked release: AiScript 1.2.1 interpreter, standard library, REPL,
native and `.ais` module loading, bytecode compiler and VM, ais-pack, and the
embedding API.
