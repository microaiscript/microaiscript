# Embedding MicroAiScript

`libaiscript` lets a C or C++ program run AiScript, expose host functions to
scripts, and call script functions from the host. Everything is declared in
[`include/aiscript.h`](../include/aiscript.h), which is the only header an
embedder includes.

## Building and linking

```sh
make lib                 # bin/libaiscript.so and bin/libaiscript.a
sudo make install-lib    # installs under PREFIX (default /usr/local) with an aiscript.pc file
```

The library does not depend on GNU Readline. With the files installed:

```sh
cc app.c $(pkg-config --cflags --libs aiscript) -o app
```

Without installing, point the compiler at the build tree:

```sh
cc -Iinclude app.c bin/libaiscript.a -lm -ldl -lpthread -o app
```

On Windows the library is built as `libaiscript.dll`; symbols marked
`AISCRIPT_API` are exported.

## Running a script

```c
#include <aiscript.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    as_interpreter_t* interp = as_interpreter_new();

    char* error = NULL;
    as_value_t* result = as_eval(interp, "let x = 40\n<: x + 2", &error);

    if (error) {                          /* syntax error */
        fprintf(stderr, "%s\n", error);
        free(error);
    } else if (!result) {                 /* runtime error */
        fprintf(stderr, "%s\n", as_interpreter_get_error(interp));
    } else {
        printf("%g\n", as_value_get_num(result));   /* 42 */
    }

    as_value_free(result);                /* NULL is allowed */
    as_interpreter_free(interp);
    return 0;
}
```

An interpreter keeps its global scope between `as_eval` calls, so a program can
be fed in pieces and functions defined earlier can be called later.

### Error reporting

| Situation | `as_eval` returns | Where the message is |
|-----------|-------------------|----------------------|
| Syntax error | `NULL` | `*error_out` (release with `free()`) |
| Runtime error | `NULL` | `as_interpreter_get_error()`; also delivered to the `err` callback if one is installed |
| Success | the value of the last statement | none |

Each `as_eval` clears the error of the previous call. Call
`as_interpreter_clear_error()` to reset the state yourself, for example before
`as_interpreter_call()`.

To parse once and run many times, use `as_parse()` / `as_interpreter_exec()` and
free the nodes with `as_nodes_free()`:

```c
size_t count;
as_node_t** nodes = as_parse(source, &count, &error);
as_value_t* result = as_interpreter_exec(interp, nodes, count);
as_nodes_free(nodes, count);
```

## Values and ownership

Values are reference counted (`as_value_t`, [`types.h`](../include/types.h)).

- Constructors (`as_value_num`, `as_value_str`, `as_value_arr`, `as_value_obj`,
  `as_value_null`, ...) return a value with one reference owned by the caller.
- Drop a reference with `as_value_free()` (alias of `as_value_release()`); add one
  with `as_value_retain()`.
- Containers retain what is put into them: after `as_arr_push(arr, v)` or
  `as_obj_set(obj, "k", v)` the caller still owns its own reference to `v` and
  must release it.
- `as_set_global()` also takes its own reference.
- `as_get_global()` returns a borrowed pointer. Retain it if you keep it past the
  next call into the interpreter.

Read primitives with `as_value_get_type()`, `as_value_get_num()`,
`as_value_get_str()` and `as_value_get_bool()`. They return neutral defaults for
mismatched types, so check the type first when it matters. Arrays and objects are
accessed through `as_arr_get`, `as_arr_push`, `as_obj_get`, `as_obj_set`,
`as_obj_has` and `as_obj_delete`.

## Exposing host functions

A native function has the signature `as_value_t* fn(as_value_t** args, size_t arg_count, void* userdata)`.
It must return a new value, never `NULL`; return `as_value_null()` for no result.
Arguments are borrowed.

```c
static as_value_t* host_add(as_value_t** args, size_t argc, void* userdata) {
    (void)userdata;
    if (argc < 2 || as_value_get_type(args[0]) != AS_VALUE_NUM ||
                    as_value_get_type(args[1]) != AS_VALUE_NUM) {
        as_raise_runtime_error("host_add: expected two numbers");   /* error.h */
        return as_value_null();
    }
    return as_value_num(as_value_get_num(args[0]) + as_value_get_num(args[1]));
}

as_register_fn(interp, "host_add", host_add, NULL);
as_eval(interp, "<: host_add(2, 3)", &error);       /* 5 */
```

`as_raise_runtime_error()` (declared in `error.h`) aborts the running script with a
runtime error; the message becomes available through `as_interpreter_get_error()`.
The `as_check_num/str/bool/arr/obj/fn` helpers verify an argument's type and set
a descriptive error when it does not match.

## Calling script functions

```c
as_eval(interp, "@greet(name) { `Hello, {name}!` }", &error);

as_value_t* arg = as_value_str("world");
as_value_t* args[] = { arg };
as_value_t* out = as_interpreter_call(interp, "greet", args, 1);
if (out) printf("%s\n", as_value_get_str(out));   /* Hello, world! */
as_value_free(out);
as_value_free(arg);
```

`as_interpreter_call()` looks the function up by name in the global scope and
returns `NULL` with an error set if it is missing, not a function, or fails.

## Custom I/O

`as_interpreter_new_ex()` takes callbacks that replace stdout, stderr and stdin:

```c
static void on_output(as_value_t* v, void* user) {
    if (as_value_get_type(v) == AS_VALUE_STR) {
        /* append as_value_get_str(v) to a buffer owned by `user` ... */
    } else {
        char* text = as_value_to_string(v);   /* e.g. 42, [1, 2], { a: 1 }; release with free() */
        /* append text ... */
        free(text);
    }
}

static void on_error(const char* message, void* user) { /* log it */ }

as_interp_opts_t opts = {0};
opts.out = on_output;      /* every <: and print() */
opts.err = on_error;       /* runtime errors, instead of stderr */
opts.userdata = my_context;
as_interpreter_t* interp = as_interpreter_new_ex(&opts);
```

`as_value_to_string()` shows strings in quotes, which is why the handler above
treats them separately. `opts.in` answers `readline()`: return a `malloc`ed string (or `NULL`).

Errors raised through the traceback reporter and `as_raise_runtime_error()` reach
the `err` callback. A few diagnostics inside the standard library are still
written straight to stderr.

## Limits to know about

- An interpreter is not thread-safe, and the library tracks the running
  interpreter in a process-wide variable, so run each interpreter on one thread
  at a time. `Async:timeout` and `Async:interval` use background threads that
  queue callbacks, which are dispatched between top-level statements of
  `as_eval`.
- Scripts can read and write files and load native libraries through `import`.
  There is no sandbox. See [extensions.md](extensions.md) before running
  untrusted code.
- `as_num_to_str()` formats numbers the way scripts print them.

## Native modules

A shared library loaded by `import "name.so"` exports
`AS_EXPORT void as_modinit_name(as_interpreter_t*, as_scope_t*)` and registers
its functions with `as_scope_add()`. See [MODULE_METADATA.md](MODULE_METADATA.md)
and `examples/native/`.
