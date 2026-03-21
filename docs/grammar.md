# Rivel v1 Language Reference

This document describes the Rivel syntax and semantics implemented in this
repository today. It is a reference for the current compiler, not a draft of a
larger future language.

## Lexical Rules

### Whitespace

Spaces, tabs, and carriage returns are ignored outside tokens.

### Comments

Rivel v1 supports line comments that start with `#` and continue to the end of
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
const  mut  fn  return  if  elif  else  while
and  or  not
Int  Bool
true  false
```

The lexer also rejects several legacy or unsupported words early, including
`let`, `exit`, `import`, `from`, and `for`.

### Literals

Rivel v1 supports:

- decimal integer literals, stored as signed 64-bit `Int`
- boolean literals: `true`, `false`

String literals, character literals, and list literals are not part of v1.

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
Type            ::= "Int" | "Bool"
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

Rivel v1 has exactly two types:

- `Int`
- `Bool`

Type annotations may appear on:

- top-level `const`
- local `const`
- local `mut`

Type annotations do not appear on expressions. Local bindings may omit an
annotation and inherit the initializer's type.

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
BindingStmt ::= ("const" | "mut") IDENT TypeAnn? "=" Expr Separator?
AssignStmt  ::= IDENT "=" Expr Separator?
ReturnStmt  ::= "return" Expr Separator?
CallStmt    ::= IDENT "(" ArgList? ")" Separator?
IfStmt      ::= "if" Expr Block ("elif" Expr Block)* ("else" Block)?
WhileStmt   ::= "while" Expr Block
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
Primary         ::= INT_LIT | BOOL_LIT | IDENT | "(" Expr ")"
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

### Builtin `print`

`print` is a builtin call statement with this effective shape:

```txt
print "(" Expr ")"
```

Rules:

- it must be used as a statement, not as an expression
- it accepts exactly one argument
- the argument must be `Int` or `Bool`
- it writes the value followed by a newline
- `print` is a reserved top-level name

Example:

```rivel
fn main() -> Int {
    print(true)
    print(false)
    return 0
}
```

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

- arithmetic operators require `Int` operands and produce `Int`
- comparison operators require `Int` operands and produce `Bool`
- equality operators require matching operand types and produce `Bool`
- `and` and `or` require `Bool` operands and produce `Bool`
- unary `-` requires `Int`
- `not` requires `Bool`

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

- integer and boolean literals
- references to other top-level constants
- unary `-` and `not`
- supported arithmetic, comparison, equality, and logical operators
- grouped expressions

Rejected in top-level constant initializers:

- references to locals
- function calls
- cyclic constant definitions

Example:

```rivel
const BASE: Int = 40
const READY: Bool = BASE == 40 and not false
```

## Runtime Behavior

The generated program includes runtime helpers for:

- printing `Int`
- printing `Bool`
- checking division by zero for `/` and `%`

Division or modulo by zero prints `division by zero` to `stderr` and exits with
status `1`.

## Not in Rivel v1

The current implementation explicitly rejects these surface forms:

- legacy `let`
- legacy `exit`
- `import` and `from`
- `for ... in ...`
- string literals
- character literals
- list syntax
- member access with `.`
- top-level `mut`
- `//` and `/* ... */` comments

If you are documenting or testing Rivel, prefer examples that stay inside the
implemented v1 core above.
