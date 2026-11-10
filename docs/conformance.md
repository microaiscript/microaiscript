# Conformance status

Target: **AiScript 1.3.0**. Upstream has not tagged 1.3.0; "1.3.0" here means the
upstream `master` branch (version `1.3.0` in `package.json`, changelog fragments in
`unreleased/`) as of January 2026. `Core:v` reports `1.3.0-c`.

## What is implemented

Changes since AiScript 1.0, and where they stand:

| Version | Change | Status |
|---------|--------|--------|
| 1.1.0 | Reserved words allowed as object-literal keys | Done |
| 1.2.0 | `Obj:from_kvs` | Done |
| 1.2.0 | Reserved words and string literals after `.` (`x.default`, `x."a b"`) | Done |
| 1.2.1 | Attribute values are evaluated, including those of namespace members | Done. Scripts cannot read attributes; this only surfaces errors in them. |
| 1.3.0 | Object shorthand `{ a }` in literals and destructuring | Done. Keywords and strings as shorthand are syntax errors. |
| 1.3.0 | Duplicate object keys are a syntax error | Done, for literals and destructuring patterns |
| 1.3.0 | `Math:gen_rng`: `null` seed rejected | Done |
| 1.3.0 | `Math:gen_rng`: ChaCha20 numeric seeds use all 64 bits; `chacha20_number_seed_legacy_behavior` restores the old behaviour | Done |
| 1.3.0 | `help` constant points at the new guide URL | Done (the constant did not exist before) |
| n/a | Exponent notation (`1e3`) is **not** part of the language | Removed from the scanner |

AiSON (`AiSON.stringify`) and `isNull`/`assertNull` are JavaScript host APIs and
have no counterpart here.

## Known gaps

The conformance suite counts programs that must be rejected but are accepted
("lenient" cases, 110 of 733). Valid programs behave as specified; the gaps are in
**rejecting invalid ones**. Run `AS_CONFORMANCE_STRICT=1 bin/test_conformance` for
the full list.

- **Jump-statement placement (79 cases).** `break`, `continue` and `return` are
  not validated: upstream rejects them where they cannot be reached or have
  no target (for example `break` outside a loop, `return` in a function's default
  argument expression without an enclosing function, jumps inside conditions of
  `for`/`each`/`match`).
- **Statement separation and layout (14 cases).** Two statements on one line
  without a separator, `match` cases without separators, unterminated multi-line
  comments, some line-break rules around infix operators, a `match` scope rule
  and destructuring declarations in namespaces are accepted.
- **Type annotations (6 cases).** Duplicate type parameters, empty `<>`, a type
  parameter given type arguments, and invalid inner types are not checked.
- **Runtime errors (11 cases).** Out-of-range and non-integer array index on
  assignment or read, a missing function argument, reading a property that
  JavaScript would inherit, `reduce` on an empty array without an initial
  value, and an invalid template expression evaluate instead of raising an error.
- **Identifier rules.** `\uXXXX` escapes and non-ASCII characters in identifiers
  are not rejected, and reserved future words (`async`, `class`, ...) are
  accepted as variable names. Error columns are counted in bytes, where upstream
  counts UTF-16 code units.

## Intentional differences

See [extensions.md](extensions.md) for additions. Output formatting also differs
slightly: `<: [1, 2]` prints `[1, 2]`, and values passed to `<:` are written with
this implementation's formatter rather than upstream's `reprValue`.

## Verifying a change against the spec

1. Find the relevant upstream test in `aiscript/test/` and port it to
   `test/test_*.c` or the conformance JSON.
2. Run `make test test_conformance`. Add `AS_CONFORMANCE_STRICT=1` when the change
   is about rejecting invalid programs.
