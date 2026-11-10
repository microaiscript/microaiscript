# Selective Imports in AiScript C

## Overview

AiScript C supports Python-style selective imports, allowing you to import specific functions from modules without using namespace prefixes.

## Syntax

### Basic Selective Import
```aiscript
from "module_path" import function1, function2, function3
```

### Traditional Import (Still Supported)
```aiscript
// Full import with automatic namespace
import "module_path"

// Full import with custom namespace
import "module_path" as CustomName
```

## Examples

### Example 1: Selective Import from .ais Module

**math_utils.ais:**
```aiscript
let add = @(a, b) {
    a + b
}

let subtract = @(a, b) {
    a - b
}

let multiply = @(a, b) {
    a * b
}
```

**Using selective import:**
```aiscript
from "math_utils.ais" import add, multiply

// Call directly without namespace prefix
print(add(5, 3))       // 8
print(multiply(4, 7))  // 28

// subtract is NOT imported, so this would error:
// print(subtract(10, 3))  // Error!
```

### Example 2: Selective Import from Native Module

```aiscript
from "native/hello.so" import say_hello, greet

say_hello()           // No namespace prefix needed!
print(greet("Alice")) // Direct call!
```

### Example 3: Mixed Import Styles

```aiscript
// Selective import for frequently used functions
from "utils.ais" import helper, validator

// Full import for less frequently used functions
import "advanced_utils.ais" as AdvUtils

helper()                    // Direct call
validator(data)             // Direct call
AdvUtils:complex_function() // Namespaced call
```

## Comparison with Traditional Imports

### Traditional Full Import
```aiscript
import "math_utils.ais"

math_utils:add(5, 3)
math_utils:multiply(4, 7)
math_utils:subtract(10, 3)
```

**Pros:**
- All module functions available
- Clear namespace prevents naming conflicts
- Easy to see where functions come from

**Cons:**
- Verbose (need namespace prefix every time)
- More typing for frequently used functions

### Selective Import
```aiscript
from "math_utils.ais" import add, multiply

add(5, 3)
multiply(4, 7)
```

**Pros:**
- Clean, concise syntax
- No namespace prefix needed
- Perfect for frequently used functions

**Cons:**
- Must explicitly list imports
- Potential for naming conflicts with other imports

## Use Cases

### 1. Frequently Used Utilities
```aiscript
from "string_utils.ais" import trim, split, join

let parts = split(trim(input), ",")
let result = join(parts, ";")
```

### 2. Native Module Convenience
```aiscript
from "native/crypto.so" import hash, encrypt, decrypt

let hashed = hash(password)
let encrypted = encrypt(data, key)
```

### 3. Clean Test Files
```aiscript
from "test_framework.ais" import assert_eq, assert_true, test

test("addition works", @{
    assert_eq(add(2, 3), 5)
    assert_true(add(0, 0) == 0)
})
```

## Module Search Paths

Selective imports use the same `Core:modpath` search mechanism:

```aiscript
// Check current search paths
print(Core:modpath)  // [".", "/usr/local/lib/aiscript", ...]

// Add custom search path
Core:modpath.push("./my_modules")

// Now can import from ./my_modules/
from "my_module.ais" import my_function
```

## Module Caching

Modules are cached regardless of import style:

```aiscript
// First import loads and caches the module
from "utils.ais" import helper

// Second import uses cached module (very fast!)
from "utils.ais" import another_helper
```

## Implementation Details

### Parser
- New `TOK_FROM` keyword
- `parse_import()` handles both `import` and `from` syntax
- Stores list of function names in `as_node_import_t.imports[]`

### Interpreter
- If selective import: register only specified functions in global scope
- If full import: register all functions with namespace prefix
- Module caching works identically for both styles

### Memory Management
- Imported functions are reference-counted
- Module scope is released after registration
- No memory leaks

## Error Handling

### Function Not Found
```aiscript
from "module.ais" import nonexistent
// Warning: 'nonexistent' not found in module 'module'
```

### Module Not Found
```aiscript
from "nonexistent.ais" import func
// Error: Module 'nonexistent.ais' not found
```

### Syntax Errors
```aiscript
from "module.ais" import  // Missing function list
// Parse error: Expected identifier in import list

from "module.ais"  // Missing 'import' keyword
// Parse error: Expected 'import' after module path
```

## Best Practices

### 1. Import What You Need
```aiscript
// ✅ Good: Only import what you use
from "utils.ais" import trim, split

// ❌ Avoid: Importing everything defeats the purpose
from "utils.ais" import func1, func2, func3, func4, func5, ...
```

### 2. Use Full Imports for Clarity
```aiscript
// ✅ Good: Use full imports when you need many functions
import "complex_library.ais" as Lib

Lib:function1()
Lib:function2()
Lib:function3()
```

### 3. Combine Strategies
```aiscript
// ✅ Good: Mix selective and full imports
from "utils.ais" import commonly_used
import "advanced.ais" as Adv

commonly_used()      // Clean
Adv:rare_function()  // Clear origin
```

### 4. Avoid Naming Conflicts
```aiscript
// ❌ Avoid: Naming conflicts
from "module1.ais" import process
from "module2.ais" import process  // Overwrites!

// ✅ Good: Use full imports or aliases
import "module1.ais" as M1
import "module2.ais" as M2

M1:process()
M2:process()
```

## Native Module API

Native modules work identically with selective imports:

**hello.c:**
```c
#include <aiscript.h>

AS_EXPORT void as_modinit_hello(as_interpreter_t* interp, as_scope_t* module_scope) {
    // Register functions in module scope
    as_value_t* fn = as_value_fn_native(say_hello, NULL);
    as_scope_add(module_scope, "say_hello", fn, false);
    as_value_release(fn);
}
```

**usage.ais:**
```aiscript
from "hello.so" import say_hello

say_hello()  // Works perfectly!
```

## Performance

- Module loading: Same cost as full imports
- Function calls: **Faster** than full imports (no namespace lookup)
- Memory usage: Slightly lower (only imported functions referenced)

## Compatibility

- ✅ Compatible with existing `import` syntax
- ✅ Works with both .ais and native modules
- ✅ Works with module caching and hot reload
- ✅ Cross-platform (Windows, Linux, macOS)

## Summary

Selective imports provide a cleaner, more convenient way to use module functions while maintaining full compatibility with traditional imports. Use them when:

- You only need a few functions from a module
- You want cleaner, more readable code
- You're importing utilities you use frequently

For comprehensive module usage, traditional full imports remain the better choice.

