# AiScript reference

This directory is the language and standard-library reference for the AiScript
version that MicroAiScript implements (**1.3.0**).

The pages are adapted from the upstream AiScript documentation, which is MIT
licensed (Copyright (c) 2021-2024 syuilo, <https://github.com/aiscript-dev/aiscript>),
and updated for 1.3.0. Where this implementation differs from the specification,
see [../extensions.md](../extensions.md) and [../conformance.md](../conformance.md).

| Page | Contents |
|------|----------|
| [syntax.md](syntax.md) | Statements, expressions, operators, labels |
| [literals.md](literals.md) | Literals, including object shorthand (1.3.0) |
| [keywords.md](keywords.md) | Reserved words |
| [builtin-props.md](builtin-props.md) | Properties of strings, arrays and numbers |
| [std.md](std.md), [std-math.md](std-math.md) | Standard library |
| [interface/](interface/) | AiSON and the host interface (JavaScript hosts only) |
| [misskey-extended-api.md](misskey-extended-api.md) | Misskey host API, kept for reference; not provided by the core interpreter |
