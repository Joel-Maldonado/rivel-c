# Rivel Language Reference

The current state of Rivel. Of course, there are lots of basic features not implemented yet. Expect lots of changes.

## Lexical Rules

### Whitespace

Spaces, tabs, and carriage returns are ignored outside tokens.

### Comments

Right now, Rivel supports line comments that start with `#` and continue to the end of
the line.

```rivel
# this is a comment
const X = 1
```

`//` and `/* ... */` comments are rejected.

### Statement and Declaration Separators

Newlines and semicolons both act as separators.

```rivel
const A = 1
const B = 2
```

```rivel
const A = 1; const B = 2
```

Repeated separators are fine. Inside parentheses, newlines do not end the
current statement, which allows multi-line grouped expressions and argument
lists. A newline that appears before the opening `(` still acts as a separator.

```rivel
fn main() -> Int {
    const total = add(
        20,
        22
    )
    return total
}
```

### Identifiers

Identifiers follow this shape:

```txt
IDENT ::= [A-Za-z_][A-Za-z0-9_]*
```

Keywords cannot be used where an identifier is expected.

### Keywords

The current reserved keywords and built-in literals are:

```txt
const  mut  fn  return  if  elif  else  while  for  in
and  or  not
Int  Double  Bool  String
true  false
```

### Literals

Rivel supports:

- decimal integer literals, stored as signed 64-bit `Int`
- decimal double literals with a required decimal point, such as `1.0`, `0.5`, and `10.`
- boolean literals: `true`, `false`
- string literals in double quotes

Strings are immutable UTF-8 byte sequences. Supported escapes are:

- `\\`
- `\"`
- `\n`
- `\r`
- `\t`

Character literals and list literals are not part of v1.

## Top-Level Structure

Only two declaration forms exist at the top level: `const` and `fn`.

```txt
Program         ::= Separator* TopLevelDecl (Separator+ TopLevelDecl)* Separator* EOF
TopLevelDecl    ::= GlobalConstDecl | FunctionDecl
GlobalConstDecl ::= "const" IDENT TypeAnn? "=" Expr
FunctionDecl    ::= "fn" IDENT "(" ParamList? ")" "->" Type Block
ParamList       ::= Param ("," Param)*
Param           ::= IDENT ":" Type
TypeAnn         ::= ":" Type
Type            ::= "Int" | "Double" | "Bool" | "String"
Separator       ::= NEWLINE | ";"
```

Notes:

- top-level `mut` is rejected
- top-level declarations must be separated by at least one newline or `;`
- function parameter types and function return types are always required
- top-level constant annotations are optional

Example:

```rivel
const BASE: Int = 40
const READY = BASE == 40 and not false

fn main() -> Int {
    if READY {
        return BASE + 2
    } else {
        return 0
    }
}
```

## Types

Right now, Rivel has exactly four types:

- `Int`
- `Double`
- `Bool`
- `String`

Type annotations may appear on:

- top-level `const`
- local `const`
- local `mut`

Type annotations do not appear on expressions. Local bindings may omit an
annotation and inherit the initializer's type.

`String` values are immutable UTF-8 byte sequences. `len` and `substr` count
bytes, not Unicode scalar values.

## Blocks and Statements

Blocks are brace-delimited and contain zero or more statements.

```txt
Block       ::= "{" Separator* Stmt* "}"
Stmt        ::= BindingStmt
              | AssignStmt
              | ReturnStmt
              | CallStmt
              | IfStmt
              | WhileStmt
              | ForRangeStmt
BindingStmt ::= ("const" | "mut") IDENT TypeAnn? "=" Expr Separator?
AssignStmt  ::= IDENT "=" Expr Separator?
ReturnStmt  ::= "return" Expr Separator?
CallStmt    ::= IDENT "(" ArgList? ")" Separator?
IfStmt      ::= "if" Expr Block ("elif" Expr Block)* ("else" Block)?
WhileStmt   ::= "while" Expr Block
ForRangeStmt ::= "for" IDENT "in" Expr (".." | "..=") Expr Block
```

Notes:

- `return` always requires a value
- assignment targets must be simple names
- only named function calls can be used as statements
- empty blocks are allowed

### Local Bindings

```rivel
fn main() -> Int {
    const x = 10
    mut y: Int = 32
    y = y
    return x + y
}
```

Rules:

- `const` creates an immutable local binding
- `mut` creates a mutable local binding
- assignment to a `const` binding is rejected
- same-scope redefinition is rejected
- nested scopes may shadow outer bindings

### `if`, `elif`, and `else`

```rivel
fn main() -> Int {
    const x = 2

    if x == 0 {
        return 0
    } elif x == 1 {
        return 1
    } elif x == 2 {
        return 42
    } else {
        return 3
    }
}
```

Conditions must be `Bool`.

The current parser rejects a condition that starts with outer parentheses:

```rivel
if (x == 1) { ... }   # rejected
while (ready) { ... } # rejected
```

Grouped subexpressions are still allowed when the condition does not begin with
`(`:

```rivel
if x == (20 + 22) {
    return 1
}
```

### `while`

```rivel
fn main() -> Int {
    mut i = 0
    mut total: Int = 0

    while i < 7 {
        total = total + i
        i = i + 1
    }

    return total
}
```

`while` conditions must also be `Bool`.

### `for` ranges

```rivel
fn main() -> Int {
    mut total = 0

    for i in 1..=6 {
        total = total + i
    }

    return total
}
```

Rules:

- `for` ranges iterate over `Int` values only
- `..` uses an exclusive end bound
- `..=` uses an inclusive end bound
- the loop variable is a fresh immutable `Int` binding
- range bounds are evaluated once before the loop starts
- descending ranges are empty

The `for` header introduces a scope around the body block. That means the
bounds are resolved before the loop variable exists, and the body may shadow
the loop variable with a nested binding:

```rivel
fn main() -> Int {
    mut total = 0

    for i in 0..3 {
        const i = i + 10
        total = total + i
    }

    return total
}
```

## Expressions

The parser implements expressions in this precedence order, from lowest to
highest:

1. `or`
2. `and`
3. `==`, `!=`
4. `<`, `<=`, `>`, `>=`
5. `+`, `-`
6. `*`, `/`, `%`
7. unary `not`, unary `-`
8. named function calls and primary expressions

All binary operators are left-associative.

### Grammar

```txt
Expr            ::= OrExpr
OrExpr          ::= AndExpr ("or" AndExpr)*
AndExpr         ::= EqualityExpr ("and" EqualityExpr)*
EqualityExpr    ::= ComparisonExpr (("==" | "!=") ComparisonExpr)*
ComparisonExpr  ::= AddExpr (("<" | "<=" | ">" | ">=") AddExpr)*
AddExpr         ::= MulExpr (("+" | "-") MulExpr)*
MulExpr         ::= UnaryExpr (("*" | "/" | "%") UnaryExpr)*
UnaryExpr       ::= ("not" | "-") UnaryExpr | CallExpr
CallExpr        ::= IDENT "(" ArgList? ")" | Primary
ArgList         ::= Expr ("," Expr)*
Primary         ::= INT_LIT | DOUBLE_LIT | BOOL_LIT | STRING_LIT | IDENT | "(" Expr ")"
```

### Calls

Calls may appear in expressions or as standalone statements.

```rivel
fn main() -> Int {
    return add_one(41)
}

fn add_one(x: Int) -> Int {
    return x + 1
}
```

The current language only allows calling a named function directly. You cannot
call the result of another expression.

### Builtins

Builtin names are reserved top-level identifiers:

- `print`
- `len`
- `substr`
- `contains`
- `starts_with`
- `ends_with`

### Builtin `print`

`print` is a builtin call statement with this effective shape:

```txt
print "(" Expr ")"
```

Rules:

- it must be used as a statement, not as an expression
- it accepts exactly one argument
- the argument must be `Int`, `Double`, `Bool`, or `String`
- it writes the value followed by a newline
- `print` is a reserved top-level name

Example:

```rivel
fn main() -> Int {
    print(true)
    print(false)
    print("hello")
    return 0
}
```

### String Builtins

These builtins may be used in expression position or as standalone call
statements:

```txt
len(s: String) -> Int
substr(s: String, start: Int, len: Int) -> String
contains(s: String, needle: String) -> Bool
starts_with(s: String, prefix: String) -> Bool
ends_with(s: String, suffix: String) -> Bool
```

Rules:

- `len` returns the string's byte length
- `substr` uses byte offsets and lengths
- negative or out-of-range `substr` bounds are rejected in constant expressions
- runtime `substr` bounds errors print `substring out of range` to `stderr` and exit with status `1`

## Static Semantics

### Entry Point

A program must define:

```rivel
fn main() -> Int { ... }
```

Constraints:

- `main` must exist
- `main` must take no parameters
- `main` must return `Int`

At runtime, the returned `Int` becomes the process exit code.

### Operators and Type Rules

- `+` accepts numeric operands and produces `Int` for `Int + Int`, otherwise `Double`; it also accepts `String + String` and produces `String`
- `-` and `*` accept numeric operands; `Int op Int` produces `Int`, otherwise they produce `Double`
- `/` accepts numeric operands; `Int / Int` produces `Int`, otherwise it produces `Double`
- `%` requires `Int` operands and produces `Int`
- comparison operators accept numeric operands and produce `Bool`
- equality operators accept matching operand types, and also allow mixed `Int`/`Double` comparisons, producing `Bool`
- `and` and `or` require `Bool` operands and produce `Bool`
- unary `-` accepts `Int` or `Double`
- `not` requires `Bool`
- `Int` may widen implicitly to `Double` in numeric expressions, assignments, annotated bindings, function arguments, and returns

Conditions in `if` and `while` must be `Bool`.

### Scope and Names

- locals are resolved lexically
- nested scopes may shadow outer names
- references to unknown names are rejected
- functions may call functions declared later in the file
- recursion is allowed

### Return Checking

Every function must return on all control-flow paths. An `if` chain only counts
as guaranteed return when every branch, including `else`, returns.

### Top-Level Constants

Top-level `const` initializers must be constant expressions.

Allowed ingredients:

- integer, double, boolean, and string literals
- references to other top-level constants
- unary `-` and `not`
- supported arithmetic, comparison, equality, and logical operators
- pure string builtins: `len`, `substr`, `contains`, `starts_with`, `ends_with`
- grouped expressions

Rejected in top-level constant initializers:

- references to locals
- non-builtin function calls
- builtin `print`
- cyclic constant definitions

Example:

```rivel
const BASE: Int = 40
const READY: Bool = BASE == 40 and not false
```

## Runtime Behavior

The generated program includes runtime helpers for:

- printing `Int`
- printing `Double`
- printing `Bool`
- printing `String`
- checking division by zero for `/` and `%`
- checking substring bounds for `substr`

Division or modulo by zero prints `division by zero` to `stderr` and exits with
status `1`.

Floating-point division follows the generated C program's `double` behavior, so
it may produce `inf` or `nan`.

Substring bounds failures print `substring out of range` to `stderr` and exit
with status `1`.

## Not in Rivel v1

The current implementation explicitly rejects these surface forms:

- `import` and `from`
- character literals
- list syntax
- member access with `.`
- string indexing
- top-level `mut`
- generic iterables or standalone range values
- stepped or reverse `for` loops
