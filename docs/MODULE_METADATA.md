# Module Metadata in AiScript C

## Overview

AiScript C supports metadata for both native modules (`.so`/`.dll`/`.dylib`) and AiScript modules (`.ais`). Metadata provides information about a module such as its name, version, author, description, license, and more.

## For Native Modules

Native modules can optionally export metadata by implementing the `as_modmeta_<name>` function.

### Example

```c
#include <aiscript.h>

/* Module functions */
static as_value_t* my_function(as_value_t** args, size_t arg_count, void* userdata) {
    // ... function implementation
}

/* Module initialization */
AS_EXPORT void as_modinit_mymodule(as_interpreter_t* interp, as_scope_t* module_scope) {
    as_value_t* fn = as_value_fn_native(my_function, NULL);
    as_scope_add(module_scope, "my_function", fn, false);
    as_value_release(fn);
}

/* Module metadata (optional) */
static as_module_metadata_t module_meta = {
    .name = "mymodule",
    .version = "1.0.0",
    .author = "Your Name",
    .description = "Description of your module",
    .license = "MIT",
    .url = "https://github.com/you/mymodule"
};

AS_EXPORT as_module_metadata_t* as_modmeta_mymodule(void) {
    return &module_meta;
}
```

### Metadata Fields

All fields are optional (can be `NULL`):

| Field | Type | Description |
|-------|------|-------------|
| `name` | `const char*` | Module name |
| `version` | `const char*` | Version string (e.g., "1.0.0") |
| `author` | `const char*` | Author name or organization |
| `description` | `const char*` | Brief description of the module |
| `license` | `const char*` | License type (e.g., "MIT", "GPL") |
| `url` | `const char*` | Homepage or repository URL |

## For .ais Modules

AiScript modules can include metadata using the `###` syntax at the beginning of the file:

```aiscript
### {
    name: "mymodule"
    version: "1.0.0"
    author: "Your Name"
    description: "Description of your module"
    tags: ["tag1", "tag2"]
    keywords: ["keyword1", "keyword2"]
}

// Module code follows
let my_function = @(x) {
    x + 1
}
```

### Metadata Format

- Must appear at the beginning of the file
- Uses object literal syntax `### { ... }`
- Only literals are allowed (strings, numbers, booleans, arrays, null)
- Any field names are allowed (not limited to predefined fields)
- Multiple metadata blocks can be used (they will all be merged into the metadata object)

## Reading Metadata at Runtime

### `Core:metadata(module_name)`

Returns the metadata object for a loaded module, or `null` if the module has no metadata or is not loaded.

```aiscript
import "mymodule"

let meta = Core:metadata("mymodule")

if (meta != null) {
    print("Module:", meta.name)
    print("Version:", meta.version)
    print("Author:", meta.author)
} else {
    print("No metadata available")
}
```

### Accessing Metadata Fields

The metadata is returned as an AiScript object, so you can access fields using dot notation:

```aiscript
let meta = Core:metadata("mymodule")

// Access predefined fields
print(meta.name)
print(meta.version)
print(meta.author)
print(meta.description)
print(meta.license)
print(meta.url)

// For .ais modules, access custom fields
print(meta.tags)       // e.g., ["tag1", "tag2"]
print(meta.keywords)   // Custom field
```

## Use Cases

### 1. Version Checking

```aiscript
import "mymodule"

let meta = Core:metadata("mymodule")
if (meta != null) {
    let required_version = "1.0.0"
    if (meta.version != required_version) {
        print(`Warning: Expected version {required_version}, got {meta.version}`)
    }
}
```

### 2. Module Discovery

```aiscript
// Load multiple modules
import "module1"
import "module2"
import "module3"

// List all module information
let modules = ["module1", "module2", "module3"]
each (let name, modules) {
    let meta = Core:metadata(name)
    if (meta != null) {
        print(`{name} v{meta.version} by {meta.author}`)
    }
}
```

### 3. License Compliance

```aiscript
import "third_party_module"

let meta = Core:metadata("third_party_module")
if (meta != null && meta.license != null) {
    print(`This software uses {meta.name} licensed under {meta.license}`)
}
```

### 4. Documentation Generation

```aiscript
// Automatically generate module documentation
import "mymodule" as MyMod

let meta = Core:metadata("mymodule")
if (meta != null) {
    print("# " + meta.name)
    print("")
    print(meta.description)
    print("")
    print("**Version:** " + meta.version)
    print("**Author:** " + meta.author)
    print("**License:** " + meta.license)
}
```

## Implementation Details

### Caching

Metadata is loaded when a module is first imported and cached in memory. Subsequent calls to `Core:metadata()` for the same module are fast (no disk I/O or parsing).

### Memory Management

- Metadata objects are reference-counted
- When a module is unloaded (`Core:unload_module`), its metadata is also freed
- The returned metadata object from `Core:metadata()` has its reference count incremented

### Native Module Loading

1. Module is loaded with `dlopen`/`LoadLibrary`
2. Init function `as_modinit_<name>` is called
3. Metadata function `as_modmeta_<name>` is looked up (optional)
4. If found, metadata is converted to AiScript object and cached
5. Module handle is stored for later unloading

### .ais Module Loading

1. File is read and parsed
2. Metadata nodes (`###`) are extracted from AST
3. Metadata nodes are converted to AiScript object
4. Object is cached with module
5. Module code is executed

## External API (C)

For programs embedding AiScript C, metadata can also be read using the C API:

```c
/* Get metadata from parsed nodes (before execution) */
size_t as_get_metadata(as_node_t** nodes, size_t node_count, as_node_t** meta_out);

/* Access module cache (after import) */
as_module_cache_entry_t* cache = interp->module_cache;
while (cache) {
    if (cache->metadata) {
        // Access metadata as as_value_t* object
        as_value_t* meta = cache->metadata;
    }
    cache = cache->next;
}
```

See `examples/metadata_reader.c` for a complete example of reading metadata using the C API.

## Examples

Complete working examples are provided in:

- **Native module with metadata:** `examples/native/metadata_test.c`
- **AiScript module with metadata:** `examples/math_utils_with_meta.ais`
- **Runtime metadata access:** `examples/test_module_metadata.ais`
- **External C API:** `examples/metadata_reader.c`

## Compatibility

- ✅ Works with both native and .ais modules
- ✅ Compatible with all import styles (`import`, `from X import Y`)
- ✅ Optional - modules work fine without metadata
- ✅ Cross-platform (Windows, Linux, macOS)


