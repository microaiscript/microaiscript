# Architecture

MicroAiScript has two execution paths that share the front end and the value
model:

```
                    +--> interpreter.c (tree-walking) ----------> result
source --> scanner --> parser --> AST --+
                    +--> aic-cross/compiler.c --> .aic --> vm.c --> result
```

The tree-walking interpreter is the reference. The bytecode path is experimental
(see [`aic-cross/README.md`](../aic-cross/README.md)).

## Source map

| File | Responsibility |
|------|----------------|
| `scanner.c`, `token.c` | Tokenizer. Template strings are scanned recursively; newlines are tokens. |
| `parser.c` | Recursive-descent parser producing the AST in `node.h`. The first syntax error is recorded in `parser->error_message` with line and column. |
| `node.c` | AST node allocation and destruction. |
| `interpreter.c` | `eval_node` evaluates the AST; module loading (`import`, `dlopen`); `as_interpreter_*` functions; standard-library registration. |
| `scope.c` | Lexical scopes: variables, mutability, namespace scopes. Reference counted so closures keep their scope alive. |
| `value.c` | Reference-counted `as_value_t`, arrays, insertion-ordered hash-table objects, equality, string conversion. |
| `primitive_props.c` | Built-in properties of strings, arrays and numbers (`"abc".len`, `[1].map(...)`, `(5).to_str()`). |
| `std_*.c` | Standard library, one file per namespace: `Core`, `Math` (+ `std_rng.c` for `Math:gen_rng`), `Str`, `Num`, `Arr`, `Obj`, `Date`, `Json`, `Uri`, `Error`, `Async`, `Util`. |
| `traceback.c`, `error.c` | Python-style error reports and the runtime-error entry point used by native code. |
| `aiscript.c` | The public embedding API (`include/aiscript.h`). |
| `bytecode.c`, `bytecode_io.c` | Bytecode structures, disassembler, `.aic` reader/writer. |
| `vm.c` | Stack VM for `.aic` programs. |
| `tools/ais-vm.c` | Command-line runner for `.aic` files. |
| `main.c` | The `aiscript` command: file, `-e`, REPL, signal handling. |
| `icon/` | Windows resource files. |

`aic-cross/compiler.c` (AST to bytecode) and `optimizer.c` live outside `src/`
because only the cross-compiler and the conformance runner use them.

## Values

`as_value_t` is a tagged union (`null`, `bool`, `num`, `str`, `arr`, `obj`, `fn`,
`error`) with a reference count. Numbers are `double`. Objects keep insertion
order. User functions hold their parameters, body and a retained closure scope;
native functions hold a C function pointer and a `void*` user pointer.

## Control flow and errors

The interpreter has no exceptions. `eval_node` returns a value, and these flags on
`as_interpreter_t` steer evaluation:

- `aborted`: set by `return` and by runtime errors. A function call clears it
  when it was a `return`, but keeps unwinding when `error_message` is set.
- `break_flag`/`continue_flag` with optional labels and a `break_value` for
  `break #label expr`.

A runtime error is raised either by returning an AiScript `error` value (for
example after a `NameError`) or by `as_raise_runtime_error()`, which records the
message on the interpreter, calls the embedder's `err` callback and sets
`aborted`. `as_eval` returns `NULL` in the second case.

Syntax errors are never raised during evaluation: `as_eval`/`as_parse` return
them through `error_out`.

## Interpreter state

`as_interpreter_t` owns the global scope, the module cache, the I/O callbacks and
the current error. The currently running interpreter is also stored in a
process-wide variable (`g_current_interp`) so that native functions can find it;
this is why an interpreter must not be used from several threads at once.

## Standard library registration

`as_interpreter_register_std` registers every function by its full name
(`"Math:sin"`, `"Arr:create"`) in the global scope. Each namespace can be removed
at build time with `-DNO_<NAMESPACE>`.

## Bytecode

The VM is a stack machine with a 256-slot value stack and 64 call frames. A
program is a code section plus constant pool, symbol table and optional debug
info. Standard-library calls and host functions are resolved by symbol name when
the program is loaded (`as_vm_bind_host_ffi`). The disassembler in `bytecode.c`
prints a program in readable form (`aic-cross -d`).

## Known structural limitations

- `eval_node` is one large function, and `primitive_props.c` resolves properties
  with chains of `strcmp`. Both work but are hard to extend.
- Many diagnostics in the standard library are written directly to stderr rather
  than through the interpreter's error callback.
- The interpreter is not re-entrant across threads (see above).
- The `CONST_FUNC_CODE` constant kind is not implemented in the VM's constant
  loader (it prints an error and yields `null`). Programs compiled by `aic-cross`
  that define functions still run, as the conformance suite shows.
