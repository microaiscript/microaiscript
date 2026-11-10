# Testing

All targets run from the repository root.

| Command | What it runs |
|---------|--------------|
| `make test` | C unit tests (`test/test_*.c`) linked against the interpreter. Exits non-zero if any assertion fails. |
| `make test_scripts` | Every `test/regression/*.ais` with `bin/aiscript`; a script passes when it exits with status 0. |
| `make test_conformance` | The AiScript conformance cases in `test/conformance/*.json`, each run through the interpreter and through the compiler plus VM. |
| `make test_basic_vm` | Compiles `test/basic/*.ais` with `aic-cross` and runs them in `ais-vm`; checks the exit status. |

## Unit tests

Tests use the small framework in `include/test.h`:

```c
TEST(my_case) {
    as_value_t* result = execute_script("<: 1 + 2");
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((int)VALUE_NUM(result), 3);
    as_value_free(result);
}
```

Register the test in the file's `run_*_tests()` function with `RUN_TEST(my_case)`,
and call that function from `test/test_main.c` when adding a new file. An
assertion failure marks the test as failed and makes the binary exit with
status 1.

`execute_script()` ignores syntax errors. To test errors, call `as_eval()`
directly, as `test/test_errors.c` and `test/test_spec_1_3.c` do.

## Regression scripts

`test/regression/*.ais` are plain scripts run for their exit status. Helper
modules (`math_utils*.ais`) are skipped by the runner. A script that reaches a
runtime error or a syntax error exits with status 1 and fails.

## Conformance suite

The cases are JSON files grouped by area (`syntax`, `literals`, `std`,
`primitive_props`, `jump_statements`, `types`, `interpreter`, `index`,
`newline`). Each case has a `program`, and either an `expect` value or an
`expect_error` of `syntax` or `runtime`. Flags such as `skip_vm`,
`xfail_vm` and `xfail_interpreter` record known differences, and `tags` mark
areas.

Output of `make test_conformance`:

```
passed:  713
failed:  0
xfailed: 0
xpassed: 0
skipped: 22
lenient: 110 (negative cases accepted; ...)
```

- `skipped` counts cases that cannot run in this implementation (extraction
  errors in the source data) or are marked `skip_interpreter`.
- `lenient` counts cases that expect an error but run successfully. They do not
  fail the default run. Set `AS_CONFORMANCE_STRICT=1` to make each of them a
  failure and see the list. [conformance.md](conformance.md) explains the gap.
- Set `AS_CONFORMANCE_VERBOSE=1` to print every case name, which helps locate a
  crash or hang.

### Regenerating the fixtures

The cases were extracted from the Rust implementation
[aiscript-rs](https://github.com/poppingmoon/aiscript-rs) by
`test/conformance/extract_from_rs.py`. To regenerate or refresh them, clone the
upstream projects next to the sources (both directories are git-ignored):

```sh
git clone https://github.com/poppingmoon/aiscript-rs aiscript-rs
git clone https://github.com/aiscript-dev/aiscript aiscript    # reference for spec changes
python3 test/conformance/extract_from_rs.py
```

The script takes no arguments and rewrites the JSON files in `test/conformance/`.
It does not extract the `identifiers` tests (macro-generated matrices). Review the
diff before committing, since new cases may need `xfail_*` or `skip_*` flags.

## Adding a regression for a bug

Prefer a conformance case if the behaviour is part of the language, a unit test
for embedding or error-reporting behaviour, and a `test/regression` script for
module and namespace behaviour that needs files.
