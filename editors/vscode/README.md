# Rivel for VS Code

Syntax highlighting, snippets, and compiler-backed language features for Rivel.
Colors follow your editor theme, including nested comments and formatted strings.

## Install

1. Install Node.js 22+ and run `make lsp` in the Rivel repository root.
2. Get `rivel-0.2.0.vsix` from the editor-support CI artifact, or build it below.
3. Run **Extensions: Install from VSIX...** and select the package.
4. Set `rivel.serverPath` to the absolute path of the built `bin/rivel-lsp`,
   or add the checkout's `bin` directory to PATH.
5. Open a `.rivel` file. Its language mode should be **Rivel**.

`rivel.compilerPath` optionally selects a different compiler. Otherwise the
server finds `rivelc` beside itself, through `RIVELC`, or on PATH.
Use **Rivel: Restart Language Server** after rebuilding the server.

## Features

- Diagnostics for unsaved buffers, typed hover, and name/member completion.
- Definition, references, checked rename, and symbol highlighting.
- Function signature help, document/workspace symbols, and folding.
- Inferred type inlay hints and semantic highlighting.
- TextMate highlighting, bracket pairing, comments, and indentation.
- Snippets: `main`, `func`, `struct`, `method`, `for`, `fori`, `foreach`,
  `while`, `if`, `parseint`, and `print`.

The server runs only in trusted workspaces. Highlighting and snippets work
without the server, including in untrusted workspaces. Each Rivel file is an
independent program: navigation and rename are within a file; workspace symbol
search spans files. Rename requires an error-free buffer and checks the result
for conflicts. Formatting and code actions are not provided.

## Build and test

From this directory:

```sh
npm ci --ignore-scripts
npm test
npm run check:exports
npm run package
```

The package is written to `../../dist/rivel-0.2.0.vsix` and can also be installed
with `code --install-extension ../../dist/rivel-0.2.0.vsix`.

Run `node scripts/test-host.mjs` for real editor-host integration tests after
building the server and `npm run build`. Tests use an isolated profile under
`build/vscode-lsp-qa`; `VSCODE_EXECUTABLE` selects an installed VS Code executable.

The portable grammar is `syntaxes/rivel.tmLanguage.json`, scope `source.rivel`.
Run `npm run export` after editing it to refresh the XML TextMate grammar.
See the [editor guide](https://github.com/Joel-Maldonado/rivel-c/blob/main/editors/README.md)
for Zed and other clients.
