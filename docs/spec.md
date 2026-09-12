# Rivel Language Specification

This document is the contract for the language. The compiler implements
exactly this; anything not described here is not part of Rivel. It supersedes
`grammar.md`, which described the previous newline-separated syntax.

Rivel is a small, statically typed, garbage-collected language that compiles
to native code. It borrows Python's vocabulary and its attitude that the
programmer should never think about memory, and C's punctuation: braces,
semicolons, and symbolic operators.

```rivel
struct Point {
    x: float;
    y: float;

    func length(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}

func main() -> int {
    origin := Point(x: 0.0, y: 0.0);
    total := 0;
    for i in 0..10 {
        if i % 2 == 0 && i != 4 {
            total += i;
        } else if i == 7 {
            continue;
        }
    }
    println(f"total = {total}, length = {origin.length()}");
    return 0;
}
```

## 1. Lexical structure

### 1.1 Source text

Source files are UTF-8. Whitespace (space, tab, carriage return, newline) is
insignificant except as a token separator. Newlines never terminate a
statement; semicolons do.

### 1.2 Comments

```rivel
// line comment
/* block comment, /* which may nest */ like this */
```

### 1.3 Identifiers and keywords

```
IDENT ::= [A-Za-z_][A-Za-z0-9_]*
```

Keywords cannot be used as identifiers:

```
func  struct  self  if  else  while  for  in  break  continue  return
true  false  null
```

Reserved for future use, also rejected as identifiers:

```
enum  match  import  as  const  pub  type  is
```

Type names `int`, `float`, `bool`, `str`, `list`, and `void` are ordinary
identifiers bound in the global scope. They can be shadowed by nothing; a
declaration with one of these names is an error.

### 1.4 Literals

Integer literals are decimal, hexadecimal (`0x`), octal (`0o`), or binary
(`0b`), with optional `_` separators. They have type `int`. A literal that does
not fit in a signed 64-bit integer is an error.

```rivel
42    1_000_000    0xFF    0o755    0b1010_0101
```

Float literals have a digit on both sides of the decimal point, an exponent,
or both. They have type `float`.

```rivel
1.0    0.5    2.5e-3    6.02e23    1e9
```

`1..10` lexes as three tokens: `1`, `..`, `10`. `1.` and `.5` are not literals.

Boolean literals are `true` and `false`. The null literal is `null`.

String literals are double-quoted, single-line, and support these escapes:

```
\\   \"   \n   \r   \t   \0   \xHH   \u{H...}
```

`\xHH` inserts one byte. `\u{...}` inserts the UTF-8 encoding of a code point.
Any other escape is an error. A plain string literal never interpolates; braces
inside it are ordinary characters.

Formatted string literals start with `f"`. Inside them, `{expr}` inserts the
value of any expression whose type can be converted with `str()`. `{{` and `}}`
produce literal braces. The expression may contain nested string literals.

```rivel
f"{name} scored {score * 2} points"
f"json: {{\"ok\": {ok}}}"
```

### 1.5 Operators and punctuation

```
+   -   *   /   %   &   |   ^   ~   <<   >>
==  !=  <   <=  >   >=  &&  ||  !
=   +=  -=  *=  /=  %=  :=  ??
:   ;   ,   .   ..  ..=  ->  ?
(   )   {   }   [   ]
```

## 2. Types

### 2.1 Built-in types

| Type    | Values                                  | Semantics |
|---------|-----------------------------------------|-----------|
| `int`   | signed 64-bit integers                  | value     |
| `float` | IEEE 754 binary64                       | value     |
| `bool`  | `true`, `false`                         | value     |
| `str`   | immutable sequence of UTF-8 bytes       | value     |
| `list[T]` | growable sequence of `T`              | reference |
| `T?`    | either a `T` or `null`                  | as `T`    |
| `void`  | no value; only as a function's result   |           |

`str` has value semantics: two strings are equal when their bytes are equal,
and a string can never be mutated. `len(s)` is the byte length and `s[i]` is
the one-byte string at byte offset `i`. Rivel does not interpret code points;
this is the same choice Go makes, and it keeps indexing O(1).

### 2.2 Struct types

```rivel
struct Token {
    kind: int;
    text: str;
    line: int;
    next: Token?;

    func is_eof(self) -> bool {
        return self.kind == 0;
    }
}
```

A struct declares fields and methods. Field names and method names share one
namespace per struct and must be unique. A struct is a **reference type**: a
value of type `Token` refers to an object on the garbage-collected heap, and
assigning it copies the reference, not the object.

```rivel
a := Point(x: 1.0, y: 2.0);
b := a;
b.x = 5.0;
// a.x is now 5.0
```

A struct value is never null. A field or variable that may be absent uses the
optional type `Token?`. Recursive structures are expressed through optionals.

Construction names every field, in any order, with no defaults:

```rivel
t := Token(kind: 1, text: "func", line: 3, next: null);
```

Methods take an explicit first parameter `self`, which has the struct's type
and is never optional. A method is called with dot syntax: `t.is_eof()`.
There is no inheritance and no interfaces.

### 2.3 Optionals

`T?` holds either a `T` or `null`. A `T` converts implicitly to `T?`; `null`
converts to any optional type. Nothing converts implicitly out of an optional.

Reading a field, calling a method, indexing, or using an arithmetic operator
on an optional is a compile-time error. To use the value, narrow it:

```rivel
func last(node: Node?) -> int {
    cur := node;
    while cur != null {
        if cur.next == null {
            return cur.value;     // cur.next is Node? here, cur is Node
        }
        cur = cur.next;
    }
    return -1;
}
```

Narrowing rules:

- Inside the body of `if c` or `while c`, every variable `v` for which `c` has
  the form `v != null`, or `v != null && ...`, has type `T` instead of `T?`.
- Inside an `else` branch of `if v == null`, and inside `if v == null || ...`'s
  else, `v` has type `T`.
- After `if v == null { ... }` whose body always leaves the function or loop
  (ends in `return`, `break`, `continue`, or `panic`), `v` has type `T` for the
  rest of the enclosing block.
- Assigning to `v` inside a narrowed region ends the narrowing from that point.

Narrowing applies to local variables and parameters only, never to fields or
elements, because those can change through another reference.

The `??` operator provides a default: `x ?? d` has type `T` when `x` is `T?`
and `d` is `T`.

Comparisons `x == null` and `x != null` are allowed for any optional. Two
optionals of the same type compare equal when both are null or both hold equal
values.

### 2.4 Lists

`list[T]` is a growable, mutable, reference-semantics sequence.

```rivel
xs: list[int] = [];
ys := [1, 2, 3];
ys.append(4);
ys[0] = 10;
n := len(ys);
tail := ys[1..3];       // new list [2, 3]
for y in ys { println(y); }
```

An empty list literal `[]` takes its type from context (an annotation,
a parameter type, or a field type). Without context it is an error.

Methods on `list[T]`:

| Call                  | Result    | Meaning |
|-----------------------|-----------|---------|
| `xs.append(v)`        | `void`    | add to the end |
| `xs.pop()`            | `T`       | remove and return the last element; panics if empty |
| `xs.insert(i, v)`     | `void`    | insert at `i`, shifting the rest |
| `xs.remove_at(i)`     | `T`       | remove and return the element at `i` |
| `xs.clear()`          | `void`    | remove everything |
| `xs.contains(v)`      | `bool`    | element-wise `==` search |
| `xs.index_of(v)`      | `int`     | first index of `v`, or `-1` |

Indexing with an out-of-range index panics. Slicing `xs[a..b]` requires
`0 <= a <= b <= len(xs)` and produces a new list.

### 2.5 Type conversions

The only implicit conversions are `int` to `float`, `T` to `T?`, and `null`
to `T?`. The `int` to `float` promotion happens in arithmetic, comparison,
assignment, argument passing, returns, and list literals with mixed elements.

Explicit conversions are ordinary calls:

| Call         | Result   | Meaning |
|--------------|----------|---------|
| `int(f)`     | `int`    | truncates toward zero; panics if out of range |
| `int(s)`     | `int?`   | parses a decimal integer; `null` on failure |
| `float(i)`   | `float`  | exact for values up to 2^53 |
| `float(s)`   | `float?` | parses a decimal float; `null` on failure |
| `str(v)`     | `str`    | for `int`, `float`, `bool`, `str` |
| `ord(s)`     | `int`    | first byte of a one-byte string |
| `chr(i)`     | `str`    | one-byte string for `0 <= i < 256` |

`str(f)` prints the shortest decimal that round-trips, and always includes a
decimal point or exponent: `str(3.0)` is `"3.0"`, `str(0.1)` is `"0.1"`.

## 3. Declarations

A program is a sequence of top-level declarations in any order. Functions,
structs, and globals can reference each other regardless of order; functions
may be mutually recursive.

### 3.1 Functions

```rivel
func add(a: int, b: int) -> int {
    return a + b;
}

func greet(name: str) {        // returns void
    println(f"hello, {name}");
}
```

Parameter types and the return type are always written. A function without
`->` returns `void` and may `return;`. Every path through a function with a
non-void return type must end in `return` or a call to `panic`.

There are no default arguments, overloading, or variadic functions.

### 3.2 Structs

See section 2.2. Structs are declared only at top level.

### 3.3 Globals

```rivel
MAX_TOKENS := 4096;
VERSION: str = "0.2";
```

A top-level declaration with `:=` or `: T =` creates a global variable. Its
initializer must be a constant expression (section 5.9). Globals are mutable
from any function, are initialized in declaration order before `main` runs,
and may reference globals declared earlier in the file.

### 3.4 Entry point

`func main() -> int` or `func main()` must exist. The returned `int`, masked
to 0..255, becomes the process exit status; a `void` main exits with 0.

## 4. Statements

```
Block     ::= "{" Stmt* "}"
Stmt      ::= VarDecl | Assign | ExprStmt | If | While | For
            | "break" ";" | "continue" ";" | Return | Block
VarDecl   ::= IDENT (":" Type "=" | ":=") Expr ";"
Assign    ::= Target ("=" | "+=" | "-=" | "*=" | "/=" | "%=") Expr ";"
ExprStmt  ::= Expr ";"
If        ::= "if" Expr Block ("else" "if" Expr Block)* ("else" Block)?
While     ::= "while" Expr Block
For       ::= "for" IDENT "in" Expr Block
Return    ::= "return" Expr? ";"
```

### 4.1 Variables

`x := e;` declares `x` with the type of `e`. `x: T = e;` declares `x` with type
`T` and requires `e` to convert to `T`. Every variable has an initializer.

Variables are block-scoped. Declaring a name that is already visible, whether
from an enclosing block, a parameter, or a global, is an error. Two sibling
blocks may each declare the same name.

### 4.2 Assignment

The target of an assignment is a variable, a field (`p.x`, `p.inner.x`), or an
element (`xs[i]`). Compound assignment `t op= e` is `t = t op e` with `t`
evaluated once.

### 4.3 Expression statements

Only a call may be used as a statement. Its result, if any, is discarded.

### 4.4 Conditionals and loops

Conditions have type `bool`; there is no truthiness. `else if` chains are
sugar for nested `if`.

`while` and `for` bodies may use `break` and `continue`. `for x in e` iterates:

- a range `a..b` (exclusive) or `a..=b` (inclusive) of `int`, bound once
  before the loop, empty when descending; `x` has type `int`;
- a `list[T]`, over its elements by index at loop start; `x` has type `T`;
- a `str`, over its bytes; `x` has type `str` of length 1.

The loop variable is immutable and scoped to the body.

## 5. Expressions

### 5.1 Precedence

From lowest to highest. Binary operators are left-associative except `??`,
which is right-associative, and comparisons, which do not chain.

| Level | Operators                     | Notes |
|-------|-------------------------------|-------|
| 1     | `??`                          | right-assoc |
| 2     | `\|\|`                        | short-circuit |
| 3     | `&&`                          | short-circuit |
| 4     | `==` `!=` `<` `<=` `>` `>=`   | non-associative: `a < b < c` is an error |
| 5     | `+` `-` `\|` `^`              | |
| 6     | `*` `/` `%` `<<` `>>` `&`     | |
| 7     | unary `-` `!` `~`             | |
| 8     | postfix: call, `.field`, `.method()`, `[index]`, `[a..b]` | |

This is Go's precedence table. Unlike C, bitwise operators bind tighter than
comparisons, so `x & 1 == 0` means `(x & 1) == 0`.

### 5.2 Evaluation order

Operands and arguments are evaluated strictly left to right. `&&` and `||`
evaluate their right operand only when needed. A call's callee and arguments
are evaluated before the call, in order.

### 5.3 Arithmetic

`+ - * / %` accept `int` and `float`; mixed operands promote to `float`.

On `int`:

- `+`, `-`, `*`, and unary `-` **panic on overflow**. Rivel does not wrap
  silently. The runtime provides `wrapping_add`, `wrapping_sub`, and
  `wrapping_mul` for hashing and similar code.
- `/` is **floor division** and `%` is **floor modulo**, so the result of `%`
  has the sign of the divisor: `-7 / 2 == -4` and `-7 % 2 == 1`. Both panic
  when the divisor is zero. This is Python's rule, chosen because it makes
  `i % n` a valid index for any `i`.

On `float`, all operators follow IEEE 754; there are no panics, and division
by zero produces an infinity or NaN.

`+` on two `str` values concatenates. No other operator applies to `str`.

Bitwise `& | ^ ~ << >>` apply to `int` only. Shifts by a negative amount or by
64 or more panic. `>>` is arithmetic.

### 5.4 Comparison

`==` and `!=` apply to any two values of the same type, or to `int` and
`float`, or to `T?` and `T`, or to an optional and `null`. Equality is always
structural:

- `int`, `float`, `bool`: by value; `NaN != NaN`
- `str`: by bytes
- `list[T]`: same length and element-wise `==`
- struct: field-wise `==`, recursively; comparing a self-referential structure
  that forms a cycle does not terminate
- optionals: both null, or both present and equal

There is no identity comparison.

`< <= > >=` apply to `int` and `float` (mixed promotes) and to `str`
(byte-wise lexicographic).

### 5.5 Logical

`&&`, `||`, `!` apply to `bool` only.

### 5.6 Calls

```rivel
f(a, b)               // function
p.method(a)           // method; p is passed as self
Point(x: 1.0, y: 2.0) // struct construction; every field named exactly once
```

Argument count and types must match the declaration. Labeled arguments are
only valid in struct construction.

### 5.7 Indexing and slicing

`xs[i]` on `list[T]` yields `T`; on `str` yields a one-byte `str`. The index
must be an `int` with `0 <= i < len`. Negative indexes are not supported.
`xs[a..b]` yields a new list or a new string with elements `a` through `b - 1`.

### 5.8 Formatted strings

`f"..."` evaluates its `{expr}` parts left to right, converts each with `str`,
and concatenates. Only types accepted by `str()` may appear.

### 5.9 Constant expressions

A constant expression is built only from literals, other globals, unary and
binary operators, and `str()`, `len()` on constant strings. Constant
expressions are evaluated at compile time with the same semantics as at run
time, including overflow and division checks, which become compile errors.

## 6. Built-in functions

Built-ins live in the global scope and cannot be redeclared.

| Signature                          | Meaning |
|------------------------------------|---------|
| `print(v)`                         | write `str(v)` to stdout |
| `println(v)`                       | write `str(v)` and a newline to stdout |
| `eprintln(v)`                      | write `str(v)` and a newline to stderr |
| `len(x) -> int`                    | length of a `str` or `list[T]` |
| `str(v) -> str`, `int(v)`, `float(v)`, `ord(s)`, `chr(i)` | see 2.5 |
| `panic(msg: str)`                  | print `panic: msg` with the source location and exit with status 101 |
| `assert(cond: bool)`               | panic when false |
| `exit(code: int)`                  | terminate immediately |
| `args() -> list[str]`              | command-line arguments, excluding the program name |
| `read_file(path: str) -> str?`     | whole file contents, or `null` |
| `write_file(path: str, data: str) -> bool` | write, `true` on success |
| `read_line() -> str?`              | one line from stdin without the newline, `null` at end |
| `sqrt(f)`, `abs(i)`, `min(a, b)`, `max(a, b)` | numeric helpers; `abs`, `min`, `max` work on `int` or `float` |
| `wrapping_add(a, b)`, `wrapping_sub`, `wrapping_mul` | two's-complement `int` arithmetic |

Methods on `str`:

| Call                        | Result      |
|-----------------------------|-------------|
| `s.contains(t)`             | `bool`      |
| `s.starts_with(t)`          | `bool`      |
| `s.ends_with(t)`            | `bool`      |
| `s.find(t)`                 | `int`, byte index or `-1` |
| `s.split(sep)`              | `list[str]` |
| `s.join(parts: list[str])`  | `str`       |
| `s.trim()`                  | `str`       |
| `s.upper()`, `s.lower()`    | `str`, ASCII only |
| `s.replace(old, new)`       | `str`       |
| `s.repeat(n)`               | `str`       |

## 7. Run-time behavior

### 7.1 Panics

These conditions stop the program with a message on stderr, the source
location that triggered it, and exit status 101:

- integer overflow in `+`, `-`, `*`, unary `-`, `abs`
- integer division or modulo by zero
- shift amount out of range
- index or slice out of range
- `pop()` or `remove_at()` on an empty list
- `int(f)` where `f` is NaN, infinite, or out of range
- `chr(i)` out of range, `ord(s)` on a string whose length is not 1
- failed `assert`
- explicit `panic`

Panics cannot be caught.

### 7.2 Memory

All heap values (strings, lists, struct objects, boxed optionals) are managed
by a tracing garbage collector. Programs never free memory. Object lifetime is
unobservable: there are no destructors and no finalizers.

### 7.3 Output

`print` and `println` write bytes to stdout, buffered; the buffer is flushed on
exit, on panic, and before reading stdin.

## 8. Grammar

```
Program     ::= TopDecl* EOF
TopDecl     ::= FuncDecl | StructDecl | GlobalDecl
FuncDecl    ::= "func" IDENT "(" Params? ")" ("->" Type)? Block
Params      ::= Param ("," Param)* ","?
Param       ::= IDENT ":" Type
StructDecl  ::= "struct" IDENT "{" StructItem* "}"
StructItem  ::= IDENT ":" Type ";"
              | "func" IDENT "(" "self" ("," Params)? ")" ("->" Type)? Block
GlobalDecl  ::= IDENT (":" Type "=" | ":=") Expr ";"

Type        ::= BaseType "?"?
BaseType    ::= "int" | "float" | "bool" | "str" | "void"
              | "list" "[" Type "]"
              | IDENT

Block       ::= "{" Stmt* "}"
Stmt        ::= IDENT (":" Type "=" | ":=") Expr ";"
              | Expr (("=" | "+=" | "-=" | "*=" | "/=" | "%=") Expr)? ";"
              | "if" Expr Block ("else" "if" Expr Block)* ("else" Block)?
              | "while" Expr Block
              | "for" IDENT "in" ForIter Block
              | "break" ";" | "continue" ";"
              | "return" Expr? ";"
              | Block
ForIter     ::= Expr ((".." | "..=") Expr)?

Expr        ::= OrExpr ("??" Expr)?
OrExpr      ::= AndExpr ("||" AndExpr)*
AndExpr     ::= CmpExpr ("&&" CmpExpr)*
CmpExpr     ::= AddExpr (("==" | "!=" | "<" | "<=" | ">" | ">=") AddExpr)?
AddExpr     ::= MulExpr (("+" | "-" | "|" | "^") MulExpr)*
MulExpr     ::= UnaryExpr (("*" | "/" | "%" | "<<" | ">>" | "&") UnaryExpr)*
UnaryExpr   ::= ("-" | "!" | "~") UnaryExpr | PostfixExpr
PostfixExpr ::= Primary
              ( "(" Args? ")"
              | "." IDENT
              | "[" Expr (".." Expr)? "]" )*
Args        ::= Arg ("," Arg)* ","?
Arg         ::= (IDENT ":")? Expr
Primary     ::= INT | FLOAT | STRING | FSTRING | "true" | "false" | "null"
              | IDENT | "(" Expr ")" | "[" Args? "]"
```

A postfix `(` after a bare identifier that names a struct is construction;
after anything else it is a call. The parser produces one call node with
optional labels and the checker decides.

## 9. Differences from the previous Rivel

For programs written against `grammar.md`:

- statements end with `;`; newlines are whitespace
- `fn` is `func`; `const`/`mut` are `x := e;` and `x: T = e;`
- `elif` is `else if`; `and`/`or`/`not` are `&&`/`||`/`!`
- `Int`/`Double`/`Bool`/`String` are `int`/`float`/`bool`/`str`
- `"${x}"` is `f"{x}"`; plain strings do not interpolate
- `Person { name: "x" }` is `Person(name: "x")`
- structs are reference types; nested field assignment works
- `/` and `%` on `int` floor instead of truncating
- integer overflow panics instead of wrapping
- `substr`, `contains`, `starts_with`, `ends_with` are methods on `str`
- `let`, `exit`, `import`, `from` are no longer rejected as identifiers
  (`import` is reserved)
