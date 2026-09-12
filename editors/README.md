# Rivel editor support

Rivel provides one language server for LSP clients, a Tree-sitter grammar for
Zed and other structural editors, and a TextMate grammar for VS Code, Sublime
Text, and TextMate.

## Build the language server

Install Node.js 22+ and the C build tools described in the root README, then:

```sh
make lsp
```

This creates `bin/rivel-lsp` alongside `bin/rivelc`. Add the checkout's `bin`
directory to PATH, or configure absolute executable paths in your editor.
The server uses stdio and never compiles or runs your program during editing.

Supported features:

- Compiler diagnostics on unsaved buffers, including errors and warnings.
- Typed hover, name completion, and struct/string/list member completion.
- Go to definition, references, symbol highlighting, and checked rename.
- Function signatures, document outline, workspace symbol search, and folding.
- Inferred type inlay hints and semantic highlighting where enabled by the editor.

Each `.rivel` file is an independent program today. Navigation and rename stay
within that file; workspace search lists symbols across files. Rename requires
an error-free buffer and checks the proposed result for name conflicts.
Formatting and code actions are not implemented. See [server details](../tools/lsp/README.md).

## Zed

[Installation guide](zed/README.md). Set `lsp.rivel.binary.path` to the built
`bin/rivel-lsp` if it is not on PATH, then use **zed: install dev extension** and
select `editors/zed`. Zed builds the adapter and grammar. This is a local
installation, not a registry listing.

## VS Code and compatible editors

```sh
cd editors/vscode
npm ci --ignore-scripts
npm run package
```

Use **Extensions: Install from VSIX** to install `dist/rivel-0.2.0.vsix` from the
repository root. Set `rivel.serverPath` to the absolute path of `bin/rivel-lsp`
if it is not on PATH. `rivel.compilerPath` optionally overrides `rivelc`.
The extension starts the server for `.rivel` files in trusted workspaces and
supports unsaved untitled Rivel documents. Use **Rivel: Restart Language Server**
after changing settings. Syntax highlighting and snippets remain available
without a server, including in untrusted workspaces.

For editor-host tests, run `node scripts/test-host.mjs`. Set `VSCODE_EXECUTABLE`
to use an existing VS Code binary instead of downloading a test copy.
The test uses an isolated profile under `build/vscode-lsp-qa`.

## Neovim 0.11+

```lua
vim.filetype.add({ extension = { rivel = 'rivel' } })
vim.lsp.config('rivel', {
  cmd = { '/absolute/path/to/rivel-c/bin/rivel-lsp', '--stdio' },
  filetypes = { 'rivel' },
  root_markers = { '.git' },
})
vim.lsp.enable('rivel')
```

The server provides semantic tokens. For Tree-sitter highlighting, register
`editors/tree-sitter-rivel` with your chosen parser manager and install its
`queries/highlights.scm` as `queries/rivel/highlights.scm`.

## Emacs with Eglot

```elisp
(define-derived-mode rivel-mode prog-mode "Rivel"
  (setq-local comment-start "// ")
  (setq-local comment-end ""))
(add-to-list 'auto-mode-alist '("\\.rivel\\'" . rivel-mode))
(with-eval-after-load 'eglot
  (add-to-list 'eglot-server-programs
               '(rivel-mode . ("/absolute/path/to/rivel-c/bin/rivel-lsp" "--stdio"))))
(add-hook 'rivel-mode-hook #'eglot-ensure)
```

Other LSP clients use the same executable, `--stdio`, and language ID `rivel`.
The Neovim and Emacs snippets are configuration examples; editor-host tests cover VS Code, with live verification in Zed.

## Portable TextMate highlighting

The canonical grammar is `vscode/syntaxes/rivel.tmLanguage.json`, scope
`source.rivel`. Its generated XML export is
`textmate/Rivel.tmbundle/Syntaxes/Rivel.tmLanguage`.

For TextMate, open `textmate/Rivel.tmbundle`. For Sublime Text, copy the XML
`.tmLanguage` into a `Rivel` folder under **Preferences: Browse Packages**.
These grammars highlight current Rivel syntax, including nested block comments
and formatted-string expressions. LSP features require a separate client setup.

Run `npm test` and `npm run check:exports` in `vscode` to verify highlighting.
Run `npm run export` after changing the canonical grammar. The generated files
are checked in so editors can use them without Node or a build step.
