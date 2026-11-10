# AiScript Syntax

## Statements and Expressions
In AiScript, syntax elements are categorized into "statements" and "expressions," excluding comments.  
Statements can only be written at the beginning of a line or within syntax elements that accept expressions (such as `if` or function literals). They are not expected to return a value and always return null.  
Expressions, on the other hand, can be written in almost all syntax elements that require some value and often return a meaningful value.  

## Comments
Lines starting with `//` or sections enclosed by `/*` `*/` are comments and do not affect the program's behavior.

```aiscript
// Single-line comment
/*
   Multi-line comment
*/
```

## Version Notation
By writing the following notation on the first line of the program, you can specify the intended version of AiScript.  
This version may be read by the host program.  
```aiscript
/// @ 0.16.0
```

## Statements

### Variable and Function Declarations
Use `let` for immutable variables (constants), `var` for mutable variables, and `@` for functions.  
#### About Reserved Words
There are some keywords that cannot be used as names when declaring variables or functions.  
For details, please refer to [keywords.md](./keywords.md).  
#### Variables
```aiscript
// Immutable (cannot be reassigned)
let answer = 42
// Mutable (can be reassigned)
var answer2 = 57
```
```aiscript
// Omission of initial value is not allowed
var answer // Syntax Error
// Reserved words like match cannot be used as variable names
let match = 12 // Syntax Error
// Redeclaration of variables with the same name is prohibited
var a = 1
var a = 2 // Runtime Error
let a = 3 // Runtime Error
```
#### Functions
Function declarations work the same as initializing an immutable variable with a function.  
```aiscript
// The last expression is implicitly returned
@add(x, y) {
	x + y
}
<: add(1, 2) // 3
// Initializing a constant with a literal function works the same
let add2 = @(x, y) {
	x + y
}
// You can also explicitly write return
@add3(x, y) {
	return x + y
}
// Arguments can be written on multiple lines
@add4(
	x,
	y
) {
	x + y
}
// Optional arguments
@func1(a, b?) {
	<: a
	<: b // If omitted, it becomes null
}
func1('hoge') // 'hoge' null
// Arguments with default values (can be used in combination with optional arguments)
@func2(a, b?, c = 'piyo', d?) {
	<: a
	<: b
	<: c
	<: d
}
func2('hoge', 'fuga') // 'hoge' 'fuga' 'piyo' null
// Variables can be used for default values (the value is fixed at the time of declaration)
var v = 'hoge'
@func3(a = v) {
	<: a
}
v = 'fuga'
func3() // 'hoge'
// One-liner
@func4(a,b?,c=1){<:a;<:b;<:c}
```
```aiscript
// Reserved words like match cannot be used as function names
@match(x, y) { // Syntax Error
  x == y
}
// A colon cannot be added after the last argument
@add(x, y,) { // Syntax Error
	x + y
}
// Redeclaration is not allowed, same as variables
var func = null
@func() { // Runtime Error
  'hoge'
}
// Optional argument syntax and default value syntax cannot be used together
@func(a? = 1) {} // Syntax Error
```

### Assignment
Change the value of a declared variable.  
```aiscript
var a = 0
a = 1
<: a // 1
```
```aiscript
// Variables declared with let cannot be assigned
let a = 0
a = 1 // Runtime Error
```

#### Destructuring Assignment
```aiscript
// Array destructuring assignment
var a = ''
var b = ''
[a, b] = ['hoge', 'fuga']
<: a // 'hoge'
<: b // 'fuga'
// Object destructuring assignment
{ name: a, nature: b } = { name: 'Ai-chan', nature: 'kawaii' }
<: a // 'Ai-chan'
<: b // 'kawaii'
// Combination
let ai_kun = {
  name: 'Ai-kun',
  nature: ['kakkoii', 'kawaii', 'ponkotsu'],
}
{ name: a, nature: [b] } = ai_kun
<: a // 'Ai-kun'
<: b // 'kakkoii'
```
```aiscript
// Destructuring assignment can also be used in declarations
let [hoge, fuga] = ['hoge', 'fuga']

each let { value: a }, [{ value: 1 }, { value: 2 }] {
	<: a // 1, 2
}

// Can also be used in arguments
@func([a, b] = [0, 0]) {
	[a, b]
}
func([1, 2]) // [1, 2]
func([1]) // [1, null], not [1, 0]
func() // [0, 0]

// Declarations that include redeclaration are not allowed
var a = 0
let [a, b] = [1, 'piyo'] // Runtime Error

// Cannot be used in namespace declarations
:: Ai {
	// Runtime Error
	let [chan, kun] = ['kawaii', 'kakkoii']
}
```
```aiscript
// Error if the assigned value is not a type that can be destructured
[a, b] = 1 // Runtime Error
{ zero: a, one: b } = ['hoge', 'fuga'] // Runtime Error
```

### for
Loops a given number of times.  
```aiscript
let repeat = 5
for repeat print('Wan') // WanWanWanWanWan
// Multiple statements can be written using {}
for 2 + 3 {
	<: 'Nyan'
} // NyanNyanNyanNyanNyan
// Can also be enclosed in ()
for ({ a: 3 }.a) {
  <: 'Piyo'
} // PiyoPiyoPiyo
```
```aiscript
// Space required just before {
for 5{ // Syntax Error
	<: 'Mogu'
}
```
#### for-let
Declare an iterator variable and reference it within the loop.  
```aiscript
for let i, 5 {
	<: i
} // 0 1 2 3 4
// Initial value can also be set
for let i = 3, 5 {
	<: i
} // 3 4 5 6 7
```
```aiscript
// Iterator variable must be declared with let
for var i, 5 {
	<: i
} // Syntax Error
```

### each
Loops through each element of an array.  
```aiscript
let arr = ['foo', 'bar', 'baz']
each let v, arr {
	<: v
} // foo bar baz
```
```aiscript
// Space required just before {
each let v, arr{ // Syntax Error
	<: v
}
```

### while
Loops as long as the condition is true.  
If the condition is false from the beginning, the loop is not executed.
```aiscript
var count = 0
while count < 42 {
	count += 1
}
<: count // 42
// If the condition is false from the beginning
while false {
  <: 'hoge'
} // no output
```

### do-while
Loops as long as the condition is true.  
Even if the condition is false from the beginning, the loop is executed once.
```aiscript
var count = 0
do {
	count += 1
} while count < 42
<: count // 42
// If the condition is false from the beginning
do {
  <: 'hoge'
} while false // hoge
```

### loop
Loops indefinitely until `break` is called.  
```aiscript
var i = 5
loop {
	<: i
	i -= 1
	if i == 0 break
} // 5 4 3 2 1
```

## Global-only Statements
Special statements that are not allowed to be written inside other syntax elements.  
These syntax elements are hoisted at the start of execution, so they are read first regardless of where they are written in the program.  

### Metadata Syntax
A feature that allows embedding metadata in an AiScript file using a notation similar to object literals.  
Metadata may be read by the host program.  
Only pure [literals](./literals.md) excluding functions are allowed as elements, and including any other expressions will result in a syntax error.  
```aiscript
### {
	name: "example"
	version: 42
	keywords: ["foo", "bar", "baz"]
}
```

### Namespace
A feature that allows adding a common prefix to multiple constants and functions.  
Mutable variables are not allowed.  
This is an underdeveloped feature and may undergo significant changes in the future.  
```aiscript
:: Ai {
	let chan = 'kawaii'
	@kun() {
		<: chan
	}
}
<: Ai:chan // kawaii
Ai:kun() // kawaii
```

## Expressions

### Literals
Syntax that allows writing values directly in the script.  
For details, see → [literals.md](./literals.md)  

### Operators
Represent major operations.  
#### Unary Operators
Used as a prefix to an expression. There are three types: logical negation (`!`), positive sign (`+`), and negative sign (`-`).
#### Binary Operators
Used between two expressions. There are arithmetic operations and their derivatives (`+`,`-`,`*`,`^`,`/`,`%`), comparison operations (`==`,`!=`,`<`,`<=`,`>`,`>=`), and logical operations (`&&`,`||`).
#### Operator Precedence
For example, in `1 + 2 * 3`, `2 * 3` is calculated first, followed by `1 +`. This is because `*` has a higher precedence than `+`. See the table below for a list of precedences.  
This precedence can be changed by enclosing in `(` `)`, like `(1 + 2) * 3`.  
#### Syntactic Sugar of Binary Operators
Binary operators are replaced with corresponding built-in functions during parsing.  
(The unary operator `!` also has a corresponding function `Core:not`, but it is not replaced.)  
See the table below for which functions they are replaced with.  
### List of Operators
The higher the precedence, the higher it is in the table. (Some have the same precedence)  
<table>
	<tbody>
		<tr><th>Operator</th><th>Corresponding Function</th><th>Meaning</th></tr>
		<tr><td><code>^</code></td><td><code>Core:pow</code></td><td>Exponentiation</td></tr>
		<tr><td><code>+ (unary)</code></td><td>None</td><td>Positive</td></tr>
		<tr><td><code>- (unary)</code></td><td>None</td><td>Negative</td></tr>
		<tr><td><code>! (unary)</code></td><td>None</td><td>Negation</td></tr>
		<tr><td><code>*</code></td><td><code>Core:mul</code></td><td>Multiplication</td></tr>
		<tr><td><code>/</code></td><td><code>Core:div</code></td><td>Division</td></tr>
		<tr><td><code>%</code></td><td><code>Core:mod</code></td><td>Modulus</td></tr>
		<tr><td><code>+</code></td><td><code>Core:add</code></td><td>Addition</td></tr>
		<tr><td><code>-</code></td><td><code>Core:sub</code></td><td>Subtraction</td></tr>
		<tr><td><code>></code></td><td><code>Core:gt</code></td><td>Greater than</td></tr>
		<tr><td><code>>=</code></td><td><code>Core:gteq</code></td><td>Greater than or equal to</td></tr>
		<tr><td><code><</code></td><td><code>Core:lt</code></td><td>Less than</td></tr>
		<tr><td><code><=</code></td><td><code>Core:lteq</code></td><td>Less than or equal to</td></tr>
		<tr><td><code>==</code></td><td><code>Core:eq</code></td><td>Equal to</td></tr>
		<tr><td><code>!=</code></td><td><code>Core:neq</code></td><td>Not equal to</td></tr>
		<tr><td><code>&&</code></td><td><code>Core:and</code></td><td>And</td></tr>
		<tr><td><code>||</code></td><td><code>Core:or</code></td><td>Or</td></tr>
	</tbody>
</table>

### if
Performs conditional branching based on whether the expression following the keyword `if` evaluates to true or false.  
It can be treated as an expression and returns the value of the last statement.
There must be one or more spaces or tabs immediately after `if`. (Even if there is a newline)  
If the conditional expression evaluates to a value that is not of type `bool`, an error occurs.  
```aiscript
// Single-line
if answer == 42 print("correct answer")
// Multiple lines
if answer == 42 {
	<: "correct answer"
}
// The conditional expression can also be enclosed in ()
if ({ a: true }.a) print('ok')
// Can be used as an expression
<: `{if answer == 42 "collect answer"}`
// else, elif, else if can also be used
let result = if answer == "bebeyo" {
	"correct answer"
} elif answer == "ai" {
	"kawaii"
} else if answer == "hoge" {
	"fuga"
} else {
	"wrong answer"
}
// If there is no else, null is returned if the conditional expression is false
<: if false 1 // null
```
```aiscript
// Spaces before and after the conditional expression are required (even if enclosed in parentheses)
if(true) return 1 // Syntax Error
if (true)return 1 // Syntax Error
// Spaces just before elif, else are required
if (false) {
}elif (true) { // Syntax Error
}else {} // Syntax Error
```

### eval
Also known as block expression.
Evaluates the statements within `{ }` in sequence and returns the value of the last statement.
```aiscript
let foo = eval {
	let x = 1
	let y = 2
	x + y
}
<: foo // 3
```

### match
```aiscript
let x = 1
let y = match x {
	case 1 => "yes"
	case 0 => "no"
	default => "other"
}
<: y // "yes"

// One-liner
<:match x{case 1=>"yes",case 0=>"no",default=>"other"} // "yes"
```

### exists
Returns true if the given variable or function name exists, otherwise returns false.
```aiscript
// Variable bar does not exist, so false
var foo = exists bar
// Variable foo exists, so true
var bar = exists foo
```

## Labeled Statements and Expressions
The following statements and expressions can be labeled.
- [`for`](#for)
- [`each`](#each)
- [`while`](#while)
- [`do-while`](#do-while)
- [`loop`](#loop)
- [`if`](#if)
- [`eval`](#eval)
- [`match`](#match)

You can exit outer blocks using labeled `break` and `continue` statements in nested blocks.
```aiscript playground
#outer: for let x, 3 {
	for let y, 2 {
		if (x == 1 && y == 1) {
			continue #outer
		}
		<: [x, y]
	}
}
```

A `break` statement associated with `eval`, `if`, or `match` can include a value.
```aiscript playground
<: #label1: eval {
	break #label1
	1
} // => null

<: #label2: eval {
	break #label2 2
	3
} // => 2
```
