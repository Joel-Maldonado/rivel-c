# Rivel editor support

Rivel ships a reusable TextMate grammar and an installable VS Code extension.
Syntax colors follow the editor's theme.

## Formats and compatibility

There is no single grammar format accepted by every editor.

| Format | Purpose | Included here |
| --- | --- | --- |
| TextMate grammar | Regex-based syntax scopes for coloring | JSON and XML grammars |
| VSIX | VS Code extension package carrying the grammar and editing settings | Built by `npm run package` |
| Tree-sitter | An incremental parser with highlighting queries, used by another group of editors | Not implemented |
| Language Server Protocol (LSP) | Editor/compiler integration such as diagnostics and completion | Not implemented |

[VS Code uses TextMate grammars](https://code.visualstudio.com/api/language-extensions/syntax-highlight-guide)
for syntax highlighting. [Tree-sitter](https://tree-sitter.github.io/tree-sitter/3-syntax-highlighting.html)
uses a separate parser and query format. A TextMate grammar alone does not
add native Tree-sitter support to Neovim, Helix, or Zed.

## VS Code and compatible editors

Use **Extensions: Install from VSIX...** to install `rivel-0.1.0.vsix`.
The file is included in the `rivel-editor-support` artifact produced by CI,
or can be built locally:

```sh
# From the repository root; Node.js 22 or newer is needed only for development.
cd editors/vscode
npm ci --ignore-scripts
npm test
npm run package
code --install-extension ../../dist/rivel-0.1.0.vsix
```

Other editors with VS Code-compatible VSIX support can use their own
**Install from VSIX** command. See the [extension README](vscode/README.md)
for snippets, features, and limitations. No Marketplace publication is
required to install the local package.

## Sublime Text

[Sublime Text accepts TextMate `.tmLanguage` files](https://www.sublimetext.com/docs/syntax.html).
Open **Preferences > Browse Packages...** and copy
[`Rivel.tmLanguage`](textmate/Rivel.tmbundle/Syntaxes/Rivel.tmLanguage)
into the `User` folder. Open a `.rivel` file or select **Rivel** from the
syntax menu. The VS Code snippets and editing settings are separate and
are not installed by copying the grammar.

## TextMate

Copy [`Rivel.tmbundle`](textmate/Rivel.tmbundle) into
`~/Library/Application Support/TextMate/Bundles/`, then reopen TextMate.
The bundle associates `.rivel` files with the `source.rivel` grammar.

## Maintaining the grammar

Edit [`rivel.tmLanguage.json`](vscode/syntaxes/rivel.tmLanguage.json), the
canonical source. Then run:

```sh
cd editors/vscode
npm run export
npm test
npm run check:exports
```

The exporter generates the XML TextMate bundle from the same rules. Tests
parse the XML with the TextMate engine, check equivalence with the JSON, and
verify scopes against Rivel's compiler keywords, builtins, and source corpus.
The CI editor-support job tests the grammar, packages the VSIX, and uploads
the installable artifacts.

Highlighting is lexical: it cannot distinguish every user-defined type or
method by meaning. A compiler-backed LSP would be the next step for
diagnostics and symbol-aware tooling.
