# JavaScript Implementation: Core Runtime

:::tip
For basic usage, please see the [Guide: "Embedding into Your Application"](/en/guides/implementation).
:::

## Parser

### `Parser.parse(script: string)`

Converts a string of AiScript code into an Abstract Syntax Tree (AST).

```ts
import { Parser } from '@syuilo/aiscript';

const program = "<: \"Hello, World!\"";

let parser: Parser;

async function run() {
    parser = new Parser();
    const ast = parser.parse(program);
}

run();
```

### `Parser.addPlugin(type: PluginType, plugin: ParserPlugin)`

Adds a plugin to the current instance that receives the AST to perform validation (`validate`) or modification (`transform`).

-----

## Interpreter

AiScript Values must be converted to JavaScript primitive types as needed.

### Constructor

`Interpreter(consts: Record<string, Value>, opts: ...)`

#### `consts`

Specifies properties to be injected at runtime. Provide an object where the keys are property names and the values are the AiScript values to inject.

#### `opts`

Specifies execution options. All are optional.

- `in(q: string): Promise<string>`: This function is called to accept input. `q` is the value to display in the prompt, and the return value is a Promise that resolves with the string of user input.
- `out(value: Value): void`: This function is called to produce output. `value` is the AiScript Value to be output.
- `err(e: AiScriptError): void`: This function is called when a problem occurs during AiScript execution. `e` is an `AiScriptError` that extends JavaScript's `Error` class.
- `log(type: string, params: LogObject): void`: This function is called when there is a log from the runtime.
- `maxStep: number`: To limit the execution depth, enter the maximum number of steps here.
- `abortOnError: boolean`: Whether to stop all other executions handled by the interpreter (including future ones) when an error occurs during execution.
- `irqRate: number`: Allows you to specify the Interrupt Request (IRQ) rate.
- `irqSleep() => Promise<void>`: Allows you to define the IRQ wait.

### `Interpreter.exec(script?: Ast.Node[])`

Asynchronously executes the AiScript AST specified in `script`. **This is the recommended method for normal use.**

### `Interpreter.execSync(script?: Ast.Node[])`

Synchronously executes the AiScript AST specified in `script`. Use with caution.

:::danger
- When running synchronously, it is **strongly recommended** to adjust limits such as the maximum number of steps. Unlike the asynchronous `exec`, `execSync` can have a severe impact on the host JavaScript environment if a malicious script (e.g., an infinite loop) is executed with poor configuration.
- When running synchronously, functions injected via `consts` that return a Promise (in other words, async functions) cannot be executed and will result in an error.
:::

### `Interpreter.execFn(fn: VFn, args: Value[])`

Asynchronously executes the AiScript function passed in `fn`. The `args` array contains the arguments for the function. To execute synchronously, use `Interpreter.execFnSync`.

### `Interpreter.execFnSimple(fn: VFn, args: Value[])`

Asynchronously executes the AiScript function passed in `fn`. Unlike `execFn`, which calls the interpreter's callback (`opts.err`) if an error occurs within the function, `execFnSimple` will always throw an `Error`.

### `Interpreter.collectMetadata(script?: Ast.Node[])`

Retrieves the content of the [metadata syntax](../syntax.md#metadata-syntax) as a JavaScript value.