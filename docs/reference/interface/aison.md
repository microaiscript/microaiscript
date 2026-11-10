# JavaScript Implementation: AiSON

**AiSON** (AiScript Object Notation) is a data interchange syntax based on AiScript's [metadata syntax](../syntax.md#metadata-syntax).

Only pure [literals](../literals.md) are allowed as elements. Including functions or any other type of dynamic expression will result in a syntax error.

It's designed to be a more delightful to write compared to JSON. For example, object keys don't require `"` (double quotes), and trailing commas `,` are permitted. This makes writing simple data structures easier.

More strictly:

  * Only **one top-level object** is permitted.
  * **Dynamic expressions**, such as functions or dynamic bindings for object values, are not allowed.
  * **Namespaces and metadata** are not supported.

```aiscript
{
    name: "example"
    version: 42
    keywords: ["foo", "bar", "baz"]
    // A trailing comma is allowed on the last item
}
```

## JavaScript API

The `@syuilo/aiscript` package includes a built-in function for parsing AiSON.

You can use `AiSON.parse` to convert an AiSON string directly into a JavaScript value. Syntax errors will throw an `errors.AiScriptSyntaxError`.

```ts
import { AiSON } from '@syuilo/aiscript';

const data = AiSON.parse('{ key: "value" }');
```

You can use `AiSON.stringify` to convert a JavaScript value into an AiSON string. The first argument is the value to convert, and the **third** argument specifies the indentation width. The second argument is unused and exists for compatibility with `JSON.stringify` (replacer functions are not supported).

```ts
import { AiSON } from '@syuilo/aiscript';

const data = { key: "value" };

// Generate AiSON string without indentation
const str1 = AiSON.stringify(data);

// Generate AiSON string with an indentation width of 2
const str2 = AiSON.stringify(data, null, 2);

// Generate AiSON string indented with tabs
const str3 = AiSON.stringify(data, null, '\t');
```
