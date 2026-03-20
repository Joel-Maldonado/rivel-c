# Rivel v1 Grammar

This document describes the implemented Rivel v1 core, not the larger draft
surface syntax.

## Top level

```txt
Program       ::= Separator* TopLevelDecl (Separator+ TopLevelDecl)* Separator* EOF
TopLevelDecl  ::= GlobalConstDecl | FunctionDecl
GlobalConstDecl ::= "const" IDENT TypeAnn? "=" Expr
FunctionDecl  ::= "fn" IDENT "(" ParamList? ")" "->" Type Block
ParamList     ::= Param ("," Param)*
Param         ::= IDENT ":" Type
TypeAnn       ::= ":" Type
Type          ::= "Int" | "Bool"
```

## Blocks and statements

```txt
Block         ::= "{" Separator* Stmt* "}"
Stmt          ::= BindingStmt
                | AssignStmt
                | ReturnStmt
                | CallStmt
                | IfStmt
                | WhileStmt
BindingStmt   ::= ("const" | "mut") IDENT TypeAnn? "=" Expr Separator?
AssignStmt    ::= IDENT "=" Expr Separator?
ReturnStmt    ::= "return" Expr Separator?
CallStmt      ::= IDENT "(" ArgList? ")" Separator?
IfStmt        ::= "if" Expr Block ("elif" Expr Block)* ("else" Block)?
WhileStmt     ::= "while" Expr Block
Separator     ::= NEWLINE | ";"
```

## Expressions

```txt
Expr          ::= OrExpr
OrExpr        ::= AndExpr ("or" AndExpr)*
AndExpr       ::= EqualityExpr ("and" EqualityExpr)*
EqualityExpr  ::= ComparisonExpr (("==" | "!=") ComparisonExpr)*
ComparisonExpr ::= AddExpr (("<" | "<=" | ">" | ">=") AddExpr)*
AddExpr       ::= MulExpr (("+" | "-") MulExpr)*
MulExpr       ::= UnaryExpr (("*" | "/" | "%") UnaryExpr)*
UnaryExpr     ::= ("not" | "-") UnaryExpr | CallExpr
CallExpr      ::= Primary ("(" ArgList? ")")*
ArgList       ::= Expr ("," Expr)*
Primary       ::= INT_LIT | BOOL_LIT | IDENT | "(" Expr ")"
```

## Semantics

- `Int` is signed 64-bit.
- `Bool` is strict; conditions must be `Bool`.
- Assignments require a `mut` binding.
- Top-level `const` initializers must be constant expressions.
- Functions may call later functions and may recurse.
- `print(expr)` is a builtin call statement that accepts exactly one `Int` or `Bool` and writes to `stdout` with a trailing newline.
- `print` is a reserved top-level name.
