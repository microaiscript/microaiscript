# Contributing

Thank you for helping with MicroAiScript. The repository is
<https://github.com/microaiscript/microaiscript>.

## Getting started

```sh
git clone https://github.com/microaiscript/microaiscript
cd microaiscript
make && make test test_scripts test_conformance
```

Read [docs/architecture.md](docs/architecture.md) for the code layout and
[docs/testing.md](docs/testing.md) for the test suites.

## Guidelines

- **The specification is upstream's.** Language behaviour follows
  [aiscript-dev/aiscript](https://github.com/aiscript-dev/aiscript); when in
  doubt, read its tests in `test/` and match them. Anything that goes beyond it
  belongs in [docs/extensions.md](docs/extensions.md).
- **Warnings are errors in spirit.** The build must stay clean under
  `-Wall -Wextra`.
- **Style.** C11 (`-std=gnu11`), four-space indentation, `as_` prefix for public
  symbols, a short comment above each function that says what and why. Match the
  surrounding code.
- **Memory.** Values are reference counted. Every `as_value_*` constructor result
  and every `as_strdup` needs a matching release. See
  [docs/embedding.md](docs/embedding.md#values-and-ownership).
- **Tests.** Add a unit test (`test/`), a conformance case, or a regression script
  with every behaviour change or fix, and run all four test targets before
  sending a change.
- **Docs.** Update the relevant file in `docs/` and `CHANGELOG.md` for any
  user-visible change.
- **Public API.** Declare it in `include/aiscript.h` with `AISCRIPT_API` and a
  documentation comment. Changing a signature needs a CHANGELOG entry.

## Working on spec updates

Clone the upstream repositories next to the sources (they are git-ignored):

```sh
git clone https://github.com/aiscript-dev/aiscript aiscript
git clone https://github.com/poppingmoon/aiscript-rs aiscript-rs
```

`aiscript/CHANGELOG.md` and `aiscript/unreleased/` list what changed;
`aiscript-rs` is a second implementation to cross-check behaviour, and its tests
are the source of the conformance cases (see
[docs/testing.md](docs/testing.md#regenerating-the-fixtures)).

## Reporting bugs

Include the AiScript program, the command you ran, the output and `aiscript -v`.
For crashes, run with `AISCRIPT_COREDUMP=1` and attach a backtrace if you can.

## License

By contributing you agree that your work is released under the MIT license.
