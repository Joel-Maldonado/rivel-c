# Rivel for VS Code

Syntax highlighting, snippets, bracket matching, comment toggling, and
indentation support for `.rivel` files. Colors come from your editor theme.
The extension is declarative and needs no compiler, language server, or
runtime process to provide highlighting.

## Install

1. Get `rivel-0.1.0.vsix` from the editor-support CI artifact, or build it below.
2. In VS Code, run **Extensions: Install from VSIX...** from the Command Palette.
3. Select the VSIX, then open any `.rivel` file. The language mode should be **Rivel**.

The same package can be installed in VS Code-compatible editors that support
local VSIX extensions. If a file was manually assigned another language,
click the language mode in the status bar and choose Rivel.

## Features

- Functions, structs, parameters, builtin types and functions, and method calls
- Strings and escapes, including expressions inside nested f-strings
- Nested block comments and line comments
- Integers in decimal, hex, octal, and binary; floats and range operators
- Optional types, lists, `self`, booleans, and `null`
- Snippets: `main`, `func`, `struct`, `method`, `for`, `fori`, `foreach`,
  `while`, `if`, `parseint`, and `print`
- Bracket pairing, comment commands, indentation, and `// #region` folding

This is lexical highlighting. It does not provide compiler diagnostics,
symbol-aware completion, rename, go-to-definition, or formatting. Calls and
user-defined types are recognized from surrounding syntax, not resolved
against a symbol table. The `struct` snippet reflects Rivel's actual
class-style construct; the language has no `class` keyword or inheritance.

## Build and test

Use Node.js 22 or newer. From this directory:

```sh
npm ci --ignore-scripts
npm test
npm run check:exports
npm run package
```

The package is written to `../../dist/rivel-0.1.0.vsix`. From this directory,
install it with:

```sh
code --install-extension ../../dist/rivel-0.1.0.vsix
```

The tests use `vscode-textmate` and the Oniguruma engine to check token scopes,
tricky syntax, and every current example and compiler test case. The source
grammar is `syntaxes/rivel.tmLanguage.json`, with scope `source.rivel`.
Run `npm run export` after editing it to refresh the compatible XML grammar.

For other editors, see the [editor support guide](https://github.com/Joel-Maldonado/rivel-c/blob/main/editors/README.md).
