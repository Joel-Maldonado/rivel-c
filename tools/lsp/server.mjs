import { createConnection, TextDocuments, TextDocumentSyncKind, ResponseError, LSPErrorCodes } from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { readFile, readdir } from 'node:fs/promises';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { join } from 'node:path';
import { Analysis, analyzeText, compilerPath, compile } from './analysis.mjs';
import { builtins, keywords, types, reserved, builtinMembers, signature } from './builtins.mjs';

if (process.argv.includes('--version')) { console.log('rivel-lsp 0.2.0'); process.exit(0); }
if (process.argv.includes('--help')) { console.log('Usage: rivel-lsp [--stdio]\nSet RIVELC to select the compiler.'); process.exit(0); }

const connection = createConnection(process.stdin, process.stdout);
const documents = new TextDocuments(TextDocument);
const states = new Map();
let compiler;
let roots = [];
const semanticTypes = ['type', 'struct', 'parameter', 'variable', 'property', 'function', 'method'];

connection.onInitialize(params => {
  compiler = compilerPath(params.initializationOptions?.compilerPath);
  roots = (params.workspaceFolders ?? (params.rootUri ? [{ uri: params.rootUri }] : []))
    .filter(r => r.uri.startsWith('file:')).map(r => fileURLToPath(r.uri));
  return {
    serverInfo: { name: 'rivel-lsp', version: '0.2.0' },
    capabilities: {
      positionEncoding: 'utf-16', textDocumentSync: TextDocumentSyncKind.Incremental,
      hoverProvider: true, definitionProvider: true, referencesProvider: true,
      documentHighlightProvider: true, renameProvider: { prepareProvider: true },
      completionProvider: { triggerCharacters: ['.'] },
      signatureHelpProvider: { triggerCharacters: ['(', ','], retriggerCharacters: [','] },
      documentSymbolProvider: true, workspaceSymbolProvider: true, foldingRangeProvider: true,
      inlayHintProvider: true,
      semanticTokensProvider: { legend: { tokenTypes: semanticTypes, tokenModifiers: ['declaration', 'readonly'] }, full: true },
      workspace: { workspaceFolders: { supported: true, changeNotifications: true } },
    },
  };
});
connection.onInitialized(() => {
  connection.workspace.onDidChangeWorkspaceFolders(event => {
    const removed = new Set(event.removed.map(f => f.uri));
    roots = roots.filter(root => !removed.has(pathToFileURL(root).href));
    roots.push(...event.added.filter(f => f.uri.startsWith('file:')).map(f => fileURLToPath(f.uri)));
  });
});
connection.onDidChangeConfiguration(event => {
  compiler = compilerPath(event.settings?.rivel?.compilerPath);
  for (const document of documents.all()) schedule(document);
});
function cancelled(token) {
  if (token?.isCancellationRequested) throw new ResponseError(LSPErrorCodes.RequestCancelled, 'Request cancelled.');
}
function start(state) {
  clearTimeout(state.timer);
  if (state.promise) return state.promise;
  const snapshot = TextDocument.create(state.uri, 'rivel', state.version, state.text);
  state.promise = compile(state.text, compiler, state.controller.signal).then(data => {
    const analysis = new Analysis(snapshot, data);
    if (states.get(state.uri) === state) {
      connection.sendDiagnostics({ uri: state.uri, version: state.version,
        diagnostics: analysis.diagnostics.map(d => ({ range: analysis.range(d.range), severity: d.severity, message: d.message, source: 'rivel' })) });
    }
    return analysis;
  }).catch(error => {
    if (!state.controller.signal.aborted && states.get(state.uri) === state) {
      connection.console.error(error.message);
      connection.sendDiagnostics({ uri: state.uri, version: state.version, diagnostics: [{
        range: { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } },
        severity: 1, source: 'rivel-lsp', message: error.message,
      }] });
    }
    return null;
  });
  return state.promise;
}
function schedule(document) {
  const previous = states.get(document.uri);
  if (previous) { clearTimeout(previous.timer); previous.controller.abort(); }
  const state = { uri: document.uri, version: document.version, text: document.getText(), controller: new AbortController() };
  states.set(document.uri, state);
  state.timer = setTimeout(() => start(state), 150);
}
documents.onDidChangeContent(({ document }) => schedule(document));
documents.onDidClose(({ document }) => {
  const state = states.get(document.uri);
  if (state) { clearTimeout(state.timer); state.controller.abort(); }
  states.delete(document.uri);
  connection.sendDiagnostics({ uri: document.uri, diagnostics: [] });
});
connection.onShutdown(() => {
  for (const state of states.values()) { clearTimeout(state.timer); state.controller.abort(); }
});
async function current(params, token) {
  cancelled(token);
  const state = states.get(params.textDocument.uri);
  if (!state) return null;
  const result = await start(state);
  cancelled(token);
  if (states.get(state.uri) !== state) throw new ResponseError(LSPErrorCodes.ContentModified, 'Document changed during analysis.');
  return result;
}
function selected(a, position) {
  const ref = a.referenceAt(a.offset(position));
  return { ref, symbol: ref && a.byId.get(ref.symbol) };
}
function docs(symbol) {
  return { kind: 'markdown', value: `\`\`\`rivel\n${signature(symbol)}\n\`\`\`${symbol.documentation ? `\n\n${symbol.documentation}` : ''}` };
}
connection.onHover(async (p, token) => {
  const a = await current(p, token); if (!a) return null;
  const { ref, symbol } = selected(a, p.position);
  if (symbol) {
    const expression = a.expressions.find(e => e.range[0] === ref.range[0] && e.range[1] === ref.range[1]);
    const display = symbol.kind === 13 && expression && expression.type !== '?' ? { ...symbol, type: expression.type } : symbol;
    return { contents: docs(display), range: a.range(ref.range) };
  }
  const offset = a.offset(p.position);
  const word = a.tokens.find(t => t.range[0] <= offset && offset < t.range[1]);
  const dot = word && a.tokens.filter(t => t.range[1] <= word.range[0]).at(-1);
  const base = dot?.text === '.' ? a.expressionBefore(dot.range[0]) : null;
  const builtin = (base ? builtinMembers(base.type) : builtins).find(b => b.name === word?.text);
  return builtin ? { contents: docs(builtin), range: a.range(word.range) } : null;
});
connection.onDefinition(async (p, token) => {
  const a = await current(p, token); if (!a) return null;
  const { symbol } = selected(a, p.position);
  return symbol ? { uri: p.textDocument.uri, range: a.range(symbol.selection) } : null;
});
connection.onReferences(async (p, token) => {
  const a = await current(p, token); if (!a) return [];
  const { symbol } = selected(a, p.position); if (!symbol) return [];
  return a.refsTo(symbol.id).filter(r => p.context.includeDeclaration || r.range[0] !== symbol.selection[0])
    .map(r => ({ uri: p.textDocument.uri, range: a.range(r.range) }));
});
connection.onDocumentHighlight(async (p, token) => {
  const a = await current(p, token); if (!a) return [];
  const { symbol } = selected(a, p.position);
  return symbol ? a.refsTo(symbol.id).map(r => ({ range: a.range(r.range), kind: 1 })) : [];
});
function renamable(a, position) {
  const result = selected(a, position);
  if (!result.symbol || result.symbol.name === 'self' || (result.symbol.kind === 12 && result.symbol.name === 'main')) return null;
  return result;
}
connection.onPrepareRename(async (p, token) => {
  const a = await current(p, token); if (!a) return null;
  const result = renamable(a, p.position);
  return result ? { range: a.range(result.ref.range), placeholder: result.symbol.name } : null;
});
connection.onRenameRequest(async (p, token) => {
  const a = await current(p, token); if (!a) return null;
  const result = renamable(a, p.position);
  if (!result) throw new ResponseError(-32602, 'This name cannot be renamed.');
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(p.newName) || reserved.has(p.newName)) throw new ResponseError(-32602, 'Choose a non-reserved Rivel identifier.');
  if (a.diagnostics.some(d => d.severity === 1)) throw new ResponseError(-32602, 'Fix compiler errors before renaming so all references can be resolved.');
  const references = a.refsTo(result.symbol.id);
  let rewritten = a.text;
  for (const ref of [...references].sort((x, y) => y.range[0] - x.range[0])) {
    rewritten = rewritten.slice(0, a.utf16[ref.range[0]]) + p.newName + rewritten.slice(a.utf16[ref.range[1]]);
  }
  const checked = await compile(rewritten, compiler);
  cancelled(token);
  if (states.get(p.textDocument.uri)?.version !== a.document.version) throw new ResponseError(LSPErrorCodes.ContentModified, 'Document changed during rename.');
  const error = checked.diagnostics.find(d => d.severity === 1);
  if (error) throw new ResponseError(-32602, `Rename would break the program: ${error.message}`);
  return { documentChanges: [{ textDocument: { uri: p.textDocument.uri, version: a.document.version },
    edits: references.map(r => ({ range: a.range(r.range), newText: p.newName })) }] };
});
function completion(symbol, range) {
  const kind = ({ 6: 2, 8: 5, 12: 3, 13: 6, 23: 22 })[symbol.kind] ?? 14;
  return { label: symbol.name, kind, detail: signature(symbol), documentation: docs(symbol),
    textEdit: { range, newText: symbol.name } };
}
connection.onCompletion(async (p, token) => {
  let a = await current(p, token); if (!a) return [];
  const unit = a.document.offsetAt(p.position);
  const byte = a.offset(p.position);
  if (a.tokens.some(t => [4, 6].includes(t.kind) && t.range[0] < byte && byte < t.range[1])) return [];
  const prefix = a.text.slice(0, unit).match(/[A-Za-z_][A-Za-z0-9_]*$/)?.[0] ?? '';
  const before = unit - prefix.length;
  const range = { start: a.document.positionAt(before), end: p.position };
  const dot = a.text.slice(0, before).match(/\.\s*$/);
  if (dot) {
    const dotUnit = before - dot[0].length;
    const dotByte = a.bytes[dotUnit];
    let expression = a.expressionBefore(dotByte);
    if (!expression || expression.type === '?') {
      // A bare dot or half-written member prevents a complete parse. Remove that
      // suffix for this completion only; the user's diagnostics stay untouched.
      const suffixEnd = unit + (a.text.slice(unit).match(/^[A-Za-z0-9_]*/)?.[0].length ?? 0);
      let repaired = a.text.slice(0, dotUnit) + ' '.repeat(suffixEnd - dotUnit) + a.text.slice(suffixEnd);
      if (/^\s*(?:}|$)/.test(repaired.slice(suffixEnd))) repaired = repaired.slice(0, suffixEnd) + ';' + repaired.slice(suffixEnd);
      a = await analyzeText(a.document.uri, a.document.version, repaired, compiler);
      cancelled(token);
      expression = a.expressionBefore(dotByte);
    }
    if (!expression) return [];
    return [...a.members(expression.type), ...builtinMembers(expression.type)]
      .filter(s => s.name.startsWith(prefix)).map(s => completion(s, range));
  }
  const visible = a.visible(a.offset(p.position));
  const items = [...visible, ...builtins].filter(s => s.name.startsWith(prefix)).map(s => completion(s, range));
  for (const name of [...keywords, ...types]) {
    if (name.startsWith(prefix) && !items.some(i => i.label === name)) items.push({ label: name, kind: types.includes(name) ? 25 : 14, textEdit: { range, newText: name } });
  }
  return [...new Map(items.map(i => [i.label, i])).values()];
});
connection.onSignatureHelp(async (p, token) => {
  const a = await current(p, token); if (!a) return null;
  const byte = a.offset(p.position);
  // Compiler tokens exclude comments and preserve strings as single tokens.
  const stack = [];
  for (const t of a.tokens.filter(t => t.range[0] < byte)) {
    if (['(', '[', '{'].includes(t.text)) stack.push({ token: t, commas: 0 });
    else if ([')', ']', '}'].includes(t.text)) stack.pop();
    else if (t.text === ',' && stack.length) stack.at(-1).commas++;
  }
  const open = [...stack].reverse().find(s => s.token.text === '('); if (!open) return null;
  const calleeToken = a.tokens.filter(t => t.range[1] <= open.token.range[0]).at(-1);
  if (!calleeToken) return null;
  const ref = a.referenceAt(calleeToken.range[0]);
  let symbol = ref && a.byId.get(ref.symbol);
  symbol ??= a.visible(byte).find(s => s.name === calleeToken.text && (s.kind === 12 || s.kind === 23));
  symbol ??= builtins.find(s => s.name === calleeToken.text);
  if (!symbol) {
    const dot = a.tokens.filter(t => t.range[1] <= calleeToken.range[0]).at(-1);
    if (dot?.text === '.') {
      const base = a.expressionBefore(dot.range[0]);
      if (base) symbol = [...a.members(base.type), ...builtinMembers(base.type)].find(s => s.name === calleeToken.text);
    }
  }
  if (!symbol) return null;
  if (symbol.kind === 23) symbol = { ...symbol, kind: 12, parameters: a.members(symbol.name).filter(s => s.kind === 8) };
  const parameters = symbol.parameters.filter(s => s.name !== 'self').map(s => ({ label: `${s.name}: ${s.type}` }));
  return { signatures: [{ label: signature(symbol), documentation: symbol.documentation, parameters }], activeSignature: 0,
    activeParameter: Math.min(open.commas, Math.max(0, parameters.length - 1)) };
});
connection.onDocumentSymbol(async (p, token) => {
  const a = await current(p, token); if (!a) return [];
  const convert = s => ({ name: s.name, detail: signature(s), kind: s.kind, range: a.range(s.range), selectionRange: a.range(s.selection),
    children: a.symbols.filter(child => child.owner === s.id).map(convert) });
  return a.symbols.filter(s => s.owner === -1).map(convert);
});
connection.onFoldingRanges(async (p, token) => {
  const a = await current(p, token); if (!a) return [];
  return a.folds.map(span => { const range = a.range(span); return { startLine: range.start.line, endLine: Math.max(range.start.line, range.end.line - 1), kind: 'region' }; })
    .filter(r => r.endLine > r.startLine);
});
connection.languages.inlayHint.on(async (p, token) => {
  const a = await current(p, token); if (!a) return [];
  const lo = a.offset(p.range.start), hi = a.offset(p.range.end);
  return a.symbols.filter(s => s.inferred && s.type !== '?' && s.type !== '<error>' && s.selection[1] >= lo && s.selection[1] <= hi)
    .map(s => ({ position: a.position(s.selection[1]), label: `: ${s.type}`, kind: 1, paddingLeft: false }));
});
connection.languages.semanticTokens.on(async (p, token) => {
  const a = await current(p, token); if (!a) return { data: [] };
  const tokens = [];
  const seen = new Set();
  for (const ref of a.references) {
    const symbol = a.byId.get(ref.symbol); if (!symbol || seen.has(ref.range.join(':'))) continue;
    seen.add(ref.range.join(':'));
    const range = a.range(ref.range);
    if (range.start.line !== range.end.line) continue;
    const parent = a.byId.get(symbol.owner);
    const parameter = parent?.parameters.some(p => p.name === symbol.name);
    const type = ({ 23: 1, 8: 4, 12: 5, 6: 6 })[symbol.kind] ?? (parameter ? 2 : 3);
    tokens.push([range.start.line, range.start.character, range.end.character - range.start.character, type,
      (ref.range[0] === symbol.selection[0] ? 1 : 0) | (symbol.immutable ? 2 : 0)]);
  }
  tokens.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  let line = 0, character = 0;
  const data = tokens.flatMap(([l, c, ...rest]) => { const token = [l - line, l === line ? c - character : c, ...rest]; line = l; character = c; return token; });
  return { data };
});
const ignored = new Set(['node_modules', 'build', 'bin', 'lib', 'target', 'third_party', 'legacy']);
async function* sourceFiles(directory) {
  let entries;
  try { entries = await readdir(directory, { withFileTypes: true }); } catch { return; }
  for (const entry of entries) {
    if (entry.name.startsWith('.') || ignored.has(entry.name)) continue;
    const path = join(directory, entry.name);
    if (entry.isDirectory()) yield* sourceFiles(path);
    else if (entry.isFile() && path.endsWith('.rivel')) yield path;
  }
}
connection.onWorkspaceSymbol(async (p, token) => {
  const result = [], seen = new Set();
  const add = a => {
    for (const s of a.symbols.filter(s => (s.owner === -1 || s.kind === 6) && s.name.toLowerCase().includes(p.query.toLowerCase()))) {
      result.push({ name: s.name, kind: s.kind, location: { uri: a.document.uri, range: a.range(s.selection) } });
    }
  };
  for (const state of states.values()) { cancelled(token); const a = await start(state); if (a) add(a); seen.add(state.uri); }
  let count = 0;
  for (const root of roots) for await (const path of sourceFiles(root)) {
    cancelled(token);
    const uri = pathToFileURL(path).href;
    if (seen.has(uri)) continue;
    seen.add(uri);
    if (++count > 200) { connection.console.info('Workspace symbol scan limited to 200 files.'); return result; }
    try { add(await analyzeText(uri, 0, await readFile(path, 'utf8'), compiler)); }
    catch (error) { connection.console.error(`Cannot index ${path}: ${error.message}`); }
  }
  return result;
});
documents.listen(connection);
connection.listen();
