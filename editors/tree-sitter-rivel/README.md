# Tree-sitter Rivel

A reusable Tree-sitter grammar for the current language in `docs/spec.md`.
Generated parser ABI: 14. The grammar handles structs and methods, inference,
optionals, lists, range expressions, nested comments, and nested f-strings.
Semantic validity is checked by the compiler/LSP, not the highlighting parser.

```sh
npm ci
npm run generate
npm test
```

`tree-sitter-cli` downloads its version-pinned native binary during installation.
If your npm policy blocks dependency install scripts, review its `install.js` and
run `node install.js` from `node_modules/tree-sitter-cli` before generating.
A C compiler is required for parser tests. Generated files under `src/` are
committed; the external comment scanner is handwritten.

`queries/highlights.scm` uses conventional capture names. Zed's extension includes
outline, indentation, bracket and text-object queries. Other Tree-sitter editors
may adapt those queries to their own capture names.
