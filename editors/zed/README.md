# Rivel for Zed

Rivel highlighting, outline, bracket matching, indentation and language server
integration. The parser supports nested comments and nested formatted strings.

## Install from this checkout

1. Install Node.js 22 or newer, then run `make lsp` in the repository root.
   This builds `bin/rivelc` and the bundled `bin/rivel-lsp` executable.
2. Add the checkout's `bin` directory to your PATH, or set an absolute path in
   Zed's settings:

   ```json
   {
     "lsp": {
       "rivel": {
         "binary": { "path": "/absolute/path/to/rivel-c/bin/rivel-lsp" }
       }
     }
   }
   ```

3. Install Rust using rustup. Open Zed's command palette, choose
   **zed: install dev extension**, and select `editors/zed` in this checkout.
   Zed builds the Rust adapter and downloads the WASI SDK to compile the grammar.
4. Open a `.rivel` file. The language selector should say **Rivel**. Hover a
   variable, use Go to Definition, or type `value.` to request member completion.

If your compiler is installed separately, configure
`lsp.rivel.initialization_options.compilerPath` with its absolute path.
The LSP normally finds `rivelc` beside its executable, through `RIVELC`, or on PATH.
Node must also be available in Zed's environment.

For semantic highlighting and inferred type hints, these optional Zed settings
can be added under `languages.Rivel`:

```json
{ "semantic_tokens": "combined", "inlay_hints": { "enabled": true } }
```

Use **editor: restart language server** after changing the compiler or server.
The extension is installed locally; it has not been published to Zed's registry.
See [the editor guide](../README.md) for capabilities, limits, and other editors.

## Develop

The canonical grammar is in `../tree-sitter-rivel`. Its generated C parser is
committed so Zed does not need the Tree-sitter CLI at installation time. The
extension manifest pins a repository revision containing that parser.
After changing the grammar, run its generation/tests and update the revision
in `extension.toml`. Copy `queries/highlights.scm` to `languages/rivel/highlights.scm`.

Build the adapter independently with:

```sh
rustup target add wasm32-wasip1
cargo build --locked --target wasm32-wasip1
```

If Homebrew's Rust precedes rustup on PATH, use
`RUSTC="$(rustup which rustc)" cargo build --locked --target wasm32-wasip1`.
For a development copy that pins rustc without changing your PATH, run
`node scripts/prepare-dev.mjs` and install the printed `~/.local/share/rivel/zed-extension`
directory. This copy survives `make clean`. Rerun the
script then rebuild the development extension after updating its source.
Newer Zed versions may build with `wasm32-wasip2`; install that rustup target
as well if requested by your editor.
