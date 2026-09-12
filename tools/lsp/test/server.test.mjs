import assert from 'node:assert/strict';
import { test } from 'node:test';
import { readFile } from 'node:fs/promises';
import { readdirSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { Client, position, root } from './client.mjs';
const source = `struct Point {
    x: int;
    func value(self) -> int { return self.x; }
}
func add(a: int, b: int) -> int { return a + b; }
func main() {
    p := Point(x: 3);
    message := "😀 café"; println(message); println(p.x);
    total := add(p.value(), 2);
    println(f"{total}");
    { sibling := 1; println(sibling); }
    { sibling := 2; println(sibling); }
}
`;
async function withClient(t, text = source) {
  const client = new Client(); t.after(() => client.close());
  const init = await client.initialize(); const uri = client.open(text);
  return { client, uri, init, at: (needle, occurrence) => ({ textDocument: { uri }, position: position(text, needle, occurrence) }) };
}
test('protocol initialization, fragmented framing, real diagnostics and shutdown', async t => {
  const { client, uri, init } = await withClient(t);
  assert.equal(init.capabilities.positionEncoding, 'utf-16');
  assert.equal(init.capabilities.renameProvider.prepareProvider, true);
  assert.deepEqual(await client.diagnostics(uri, 1), []);
  await assert.rejects(client.request('unknown/method', {}), { code: -32601 });
});
test('hover and definition use resolved fields and UTF-16 positions', async t => {
  const { client, at } = await withClient(t);
  const p = at('p.x'); p.position.character += 2;
  const hover = await client.request('textDocument/hover', p);
  assert.match(hover.contents.value, /x: int/);
  const definition = await client.request('textDocument/definition', p);
  assert.deepEqual(definition.range.start, position(source, 'x: int'));
  assert.deepEqual(hover.range.start, p.position);
});
test('references and rename distinguish sibling scopes, strings and comments', async t => {
  const { client, at } = await withClient(t);
  const refs = await client.request('textDocument/references', { ...at('sibling'), context: { includeDeclaration: true } });
  assert.equal(refs.length, 2);
  const edit = await client.request('textDocument/rename', { ...at('sibling'), newName: 'counter' });
  assert.equal(edit.documentChanges[0].edits.length, 2);
  assert.equal(edit.documentChanges[0].textDocument.version, 1);
  await assert.rejects(client.request('textDocument/rename', { ...at('sibling'), newName: 'p' }), /already defined/);
  await assert.rejects(client.request('textDocument/rename', { ...at('sibling'), newName: 'while' }), /non-reserved/);
});
test('rename includes struct annotations, constructors, field labels and members', async t => {
  const { client, at } = await withClient(t);
  const fields = await client.request('textDocument/rename', { ...at('x: int'), newName: 'coordinate' });
  assert.equal(fields.documentChanges[0].edits.length, 4);
  const structs = await client.request('textDocument/references', { ...at('Point'), context: { includeDeclaration: true } });
  assert.equal(structs.length, 2);
});
test('completion respects scope and resolves struct, string and list members', async t => {
  const { client, at } = await withClient(t);
  const items = await client.request('textDocument/completion', at('println(f'));
  assert.ok(items.some(i => i.label === 'total'));
  assert.ok(!items.some(i => i.label === 'sibling'));
  const members = await client.request('textDocument/completion', { ...at('p.x'), position: { ...at('p.x').position, character: at('p.x').position.character + 2 } });
  assert.deepEqual(members.map(i => i.label).sort(), ['value', 'x']);
  for (const [text, expected] of [['func main() { s := "hello"; s. }', 'trim'], ['func main() { xs := [1, 2]; xs. }', 'append']]) {
    const uri = client.open(text, 'file:///tmp/completion-' + expected + '.rivel');
    const items = await client.request('textDocument/completion', { textDocument: { uri }, position: position(text, ' }') });
    assert.ok(items.some(i => i.label === expected), JSON.stringify(items));
  }
});
test('signature help, outline, folding, semantic tokens and inferred type hints', async t => {
  const { client, uri, at } = await withClient(t);
  const help = await client.request('textDocument/signatureHelp', at('2);'));
  assert.match(help.signatures[0].label, /add\(a: int, b: int\)/);
  assert.equal(help.activeParameter, 1);
  const symbols = await client.request('textDocument/documentSymbol', { textDocument: { uri } });
  assert.ok(symbols.find(s => s.name === 'Point').children.some(s => s.name === 'value'));
  assert.ok((await client.request('textDocument/foldingRange', { textDocument: { uri } })).length > 0);
  const semantic = await client.request('textDocument/semanticTokens/full', { textDocument: { uri } });
  assert.ok(semantic.data.length > 20); assert.equal(semantic.data.length % 5, 0);
  const hints = await client.request('textDocument/inlayHint', { textDocument: { uri }, range: { start: { line: 0, character: 0 }, end: { line: 99, character: 0 } } });
  assert.ok(hints.some(h => h.label === ': Point'));
});
test('unsaved incremental edits publish current diagnostics and clear on close', async t => {
  const { client, uri, at } = await withClient(t);
  await client.diagnostics(uri, 1);
  const value = position(source, 'add(p.value(), 2)');
  client.notify('textDocument/didChange', { textDocument: { uri, version: 2 }, contentChanges: [{ range: { start: value, end: { ...value, character: value.character + 17 } }, text: 'missing' }] });
  const diagnostics = await client.diagnostics(uri, 2);
  assert.ok(diagnostics.some(d => /unknown|undefined/.test(d.message)), JSON.stringify(diagnostics));
  client.notify('textDocument/didChange', { textDocument: { uri, version: 3 }, contentChanges: [{ text: source }] });
  assert.deepEqual(await client.diagnostics(uri, 3), []);
  client.notify('textDocument/didClose', { textDocument: { uri } });
  await new Promise(r => setTimeout(r, 30));
  assert.deepEqual(client.notifications.filter(n => n.method === 'textDocument/publishDiagnostics').at(-1).params.diagnostics, []);
});
test('compiler handles the whole valid and invalid corpus and a larger program', async () => {
  const paths = ['tests/cases', 'examples'].flatMap(dir => readdirSync(root + dir, { recursive: true }).filter(p => p.endsWith('.rivel')).map(p => dir + '/' + p));
  for (const path of paths) {
    const result = JSON.parse(execFileSync(process.env.RIVEL_TEST_COMPILER || root + 'bin/rivelc', ['--analyze', path], { cwd: root, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }));
    assert.equal(result.schemaVersion, 1, path);
  }
  const game = await readFile(root + 'examples/tic_tac_toe/main.rivel', 'utf8');
  assert.ok(game.length > 10000);
});
test('incomplete parameter lists and bare list annotations do not crash analysis', async t => {
  const { client } = await withClient(t);
  for (const [index, text] of ['func search(board: int, player:', 'func main() { xs: list = []; }', 'struct S { func method(self, value:) {} }'].entries()) {
    const uri = client.open(text, `file:///tmp/incomplete-${index}.rivel`);
    assert.ok((await client.diagnostics(uri, 1)).some(d => d.severity === 1));
  }
});
test('type annotation references, narrowing, CRLF and Unicode are preserved', async t => {
  const text = 'struct Node { value: int; }\r\nfunc main() {\r\n n: Node? = null;\r\n if n != null { println("😀"); println(n.value); }\r\n}\r\n';
  const { client, at } = await withClient(t, text);
  const refs = await client.request('textDocument/references', { ...at('Node'), context: { includeDeclaration: true } });
  assert.equal(refs.length, 2);
  const hover = await client.request('textDocument/hover', at('n.value'));
  assert.match(hover.contents.value, /n: Node\n/);
});
test('workspace symbols use unsaved files, and unrelated files keep separate definitions', async t => {
  const { client, uri, at } = await withClient(t);
  client.open('func add() {} func main() { add(); }', 'file:///tmp/other.rivel');
  const symbols = await client.request('workspace/symbol', { query: 'add' });
  assert.equal(symbols.length, 2);
  const def = await client.request('textDocument/definition', at('add(p'));
  assert.equal(def.uri, uri);
  assert.deepEqual(def.range.start, position(source, 'add(a'));
});
test('rapid edits never publish results from superseded documents', async t => {
  const { client, uri } = await withClient(t);
  await client.diagnostics(uri, 1);
  for (let version = 2; version <= 30; version++) client.notify('textDocument/didChange', {
    textDocument: { uri, version }, contentChanges: [{ text: version === 30 ? 'func main() {}' : 'func main() { missing(); }' }],
  });
  assert.deepEqual(await client.diagnostics(uri, 30), []);
  await new Promise(r => setTimeout(r, 100));
  const last = client.notifications.filter(n => n.method === 'textDocument/publishDiagnostics' && n.params.uri === uri).at(-1);
  assert.equal(last.params.version, 30);
});
test('missing compiler is reported as a diagnostic and the server stays alive', async t => {
  const client = new Client(); t.after(() => client.close());
  await client.initialize({ compilerPath: '/does-not-exist/rivelc' });
  const uri = client.open('func main() {}');
  assert.match((await client.diagnostics(uri, 1))[0].message, /Cannot analyze/);
  assert.deepEqual(await client.request('textDocument/documentSymbol', { textDocument: { uri } }), []);
});
test('completion signatures agree with compiler builtin arity and parameter types', async () => {
  const { builtins, builtinMembers } = await import('../builtins.mjs');
  const valueFor = type => {
    if (type.startsWith('list[str]')) return '["a"]';
    if (type.startsWith('list[')) return '[1]';
    if (type.startsWith('str')) return '"a"';
    if (type.startsWith('float')) return '1.0';
    if (type.startsWith('bool')) return 'true';
    return '1';
  };
  const groups = [[builtins, '', ''], [builtinMembers('str'), 's := "text";', 's.'], [builtinMembers('list[int]'), 'xs := [1];', 'xs.']];
  for (const [symbols, setup, receiver] of groups) for (const symbol of symbols) {
    const args = symbol.parameters.map(p => valueFor(p.type)).join(', ');
    const input = `func main() { ${setup} ${receiver}${symbol.name}(${args}); }`;
    const result = JSON.parse(execFileSync(process.env.RIVEL_TEST_COMPILER || root + 'bin/rivelc', ['--analyze', '-'], { input, encoding: 'utf8' }));
    assert.deepEqual(result.diagnostics.filter(d => d.severity === 1), [], input);
  }
});
