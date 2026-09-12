# Rivel language server

`make lsp` at the repository root builds a bundled executable in `bin/rivel-lsp`.
It requires Node.js 22+ and `rivelc` from the same checkout. Run it as
`rivel-lsp --stdio`. It speaks standard JSON-RPC/LSP over stdin/stdout.

The C compiler's `rivelc --analyze -` mode reads the unsaved buffer from stdin
and exports versioned JSON with diagnostics, byte ranges, inferred types,
declarations, resolved references, calls, and tokens. Analysis performs no code
generation or program execution. The server converts byte ranges to UTF-16
positions using the exact document version, and advertises incremental sync.

The server checks open documents after a 150 ms editing pause, cancels obsolete
compiler processes, and rejects requests against stale snapshots. Each analysis
has a ten-second timeout, a 2 MiB source limit, and a 32 MiB JSON output limit.
Compiler failures become diagnostics; they do not terminate the server.

Rename uses resolved symbol identities, validates the new identifier, and checks
the rewritten program before returning versioned edits. It requires an error-free
buffer. It never edits strings, comments, unrelated fields, or sibling bindings.
The entry point `main`, `self`, builtins, and reserved names cannot be renamed.

Rivel has no imports or multi-file compilation yet. Definition, references, and
rename therefore operate within one source file. Workspace symbol search lists
top-level declarations and methods from open buffers and up to 200 `.rivel` files
on disk, skipping generated/vendor/hidden directories and symlinks.

Capabilities: diagnostics, completion (including struct/string/list members),
hover, definition, references, document highlights, prepare rename and rename,
signature help, document/workspace symbols, folding, inferred type inlay hints,
and semantic tokens. Formatting and code actions are not advertised.
Completion in malformed expressions uses the parser's recovered tree; some
severely incomplete expressions may have no type until enough syntax is present.

Set `RIVELC` or the `initializationOptions.compilerPath` LSP option to select a
compiler explicitly. Otherwise the server checks beside its real executable
path, then PATH. No configuration file in the opened project is executed.

`make test-lsp` runs protocol integration tests against the real compiler,
including Unicode/CRLF positions, unsaved edits, rename conflicts, narrowing,
independent scopes/documents, error recovery, and the complete language corpus.
The tests use Node's built-in test runner and no mocked compiler.
