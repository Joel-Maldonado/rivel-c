import assert from 'node:assert/strict';
import { readFile, readdir } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import textmate from 'vscode-textmate';
import oniguruma from 'vscode-oniguruma';

const require = createRequire(import.meta.url);
const repositoryRoot = new URL('../../../', import.meta.url);
const grammarPath = new URL('../syntaxes/rivel.tmLanguage.json', import.meta.url);
const grammarSource = await readFile(grammarPath, 'utf8');
const wasm = await readFile(require.resolve('vscode-oniguruma/release/onig.wasm'));
await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));

const registry = new textmate.Registry({
  onigLib: Promise.resolve({
    createOnigScanner: (patterns) => new oniguruma.OnigScanner(patterns),
    createOnigString: (value) => new oniguruma.OnigString(value),
  }),
  loadGrammar: async (scopeName) => scopeName === 'source.rivel'
    ? textmate.parseRawGrammar(grammarSource, fileURLToPath(grammarPath))
    : null,
});
const grammar = await registry.loadGrammar('source.rivel');
assert.ok(grammar, 'The Rivel grammar must load in the real TextMate engine');

function tokenize(source, filename = 'snippet') {
  let state = textmate.INITIAL;
  return source.split(/\r?\n/).map((text, index) => {
    const result = grammar.tokenizeLine(text, state, 1000);
    assert.equal(result.stoppedEarly, false, `${filename}:${index + 1} exceeded the tokenization limit`);
    state = result.ruleStack;
    return { text, tokens: result.tokens };
  });
}

function tokensFor(line, fragment) {
  const start = line.text.indexOf(fragment);
  assert.notEqual(start, -1, `Missing test fragment ${JSON.stringify(fragment)} in ${line.text}`);
  const end = start + fragment.length;
  const tokens = line.tokens.filter((token) => token.startIndex < end && token.endIndex > start);
  assert.ok(tokens.length > 0, `No tokens cover ${JSON.stringify(fragment)}`);
  return tokens;
}

function hasScope(token, prefix) {
  return token.scopes.some((scope) => scope === prefix || scope.startsWith(`${prefix}.`));
}

function assertScope(line, fragment, prefix) {
  for (const token of tokensFor(line, fragment)) {
    assert.ok(hasScope(token, prefix),
      `${JSON.stringify(fragment)} needs ${prefix}; got ${token.scopes.join(', ')}`);
  }
}

function assertNoScope(line, fragment, prefix) {
  for (const token of tokensFor(line, fragment)) {
    assert.ok(!hasScope(token, prefix),
      `${JSON.stringify(fragment)} must not have ${prefix}; got ${token.scopes.join(', ')}`);
  }
}

test('the portable TextMate export preserves the canonical grammar', async () => {
  const exportPath = new URL('../../textmate/Rivel.tmbundle/Syntaxes/Rivel.tmLanguage', import.meta.url);
  const exported = textmate.parseRawGrammar(await readFile(exportPath, 'utf8'), fileURLToPath(exportPath));
  const { uuid, ...exportedGrammar } = exported;
  assert.match(uuid, /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i,
    'The TextMate bundle needs a valid grammar UUID');
  assert.deepEqual(exportedGrammar, JSON.parse(grammarSource),
    'The XML export must preserve every canonical scope and matching rule');
});

test('recognizes function and struct declarations, fields, and methods', () => {
  const lines = tokenize([
    'struct Point {',
    '    x: float;',
    '    func length(self) -> float { return sqrt(self.x); }',
    '}',
    'func main() -> int { return 0; }',
  ].join('\n'));
  assertScope(lines[0], 'struct', 'storage.type.struct');
  assertScope(lines[0], 'Point', 'entity.name.type.struct');
  assertScope(lines[1], 'float', 'storage.type.builtin');
  assertScope(lines[2], 'func', 'storage.type.function');
  assertScope(lines[2], 'length', 'entity.name.function');
  assertScope(lines[2], 'self', 'variable.language.self');
  assertScope(lines[2], 'sqrt', 'support.function.builtin');
  assertScope(lines[4], 'main', 'entity.name.function');
});

test('covers every current compiler keyword and reserved word', async () => {
  const lexer = await readFile(new URL('src/lex/lexer.c', repositoryRoot), 'utf8');
  const keywordTable = lexer.match(/KEYWORDS\[\]\s*=\s*\{([\s\S]*?)\n\};/)[1];
  const keywords = [...keywordTable.matchAll(/\{"([a-z_]+)",\s*TOK_KW_\w+\}/g)]
    .map((match) => match[1]);
  assert.ok(keywords.length >= 14, 'Expected the compiler keyword table');
  const specialScopes = {
    func: 'storage.type.function',
    struct: 'storage.type.struct',
    self: 'variable.language.self',
    true: 'constant.language.boolean',
    false: 'constant.language.boolean',
    null: 'constant.language.null',
  };
  for (const word of keywords) {
    const [line] = tokenize(word);
    assertScope(line, word, specialScopes[word] ?? 'keyword.control');
  }

  const reservedTable = lexer.match(/RESERVED\[\]\s*=\s*\{([^}]+)\}/)[1];
  const reserved = [...reservedTable.matchAll(/"([a-z_]+)"/g)].map((match) => match[1]);
  assert.ok(reserved.length >= 8, 'Expected the compiler reserved-word table');
  for (const word of reserved) {
    const [line] = tokenize(word);
    for (const token of tokensFor(line, word)) {
      assert.ok(hasScope(token, 'keyword') || hasScope(token, 'invalid'),
        `Reserved word ${word} must be distinguishable from an identifier`);
    }
  }
});

test('covers all compiler built-in functions and types', async () => {
  const builtins = await readFile(new URL('src/sema/builtins.c', repositoryRoot), 'utf8');
  const functionTable = builtins.match(/BUILTIN_FUNCS\[\]\s*=\s*\{([\s\S]*?)\n\};/)[1];
  const names = [...functionTable.matchAll(/\{"([a-z_]+)",\s*BI_\w+\}/g)]
    .map((match) => match[1]);
  assert.ok(names.length >= 20, 'Expected the compiler builtin function table');
  for (const name of names) {
    const [line] = tokenize(`${name}();`);
    assertScope(line, name, 'support.function.builtin');
  }
  for (const name of ['int', 'float', 'bool', 'str', 'list', 'void']) {
    const [line] = tokenize(`value: ${name};`);
    assertScope(line, name, 'storage.type.builtin');
  }
  for (const name of ['str', 'int', 'float']) {
    const [line] = tokenize(`${name}(value);`);
    for (const token of tokensFor(line, name)) {
      assert.ok(hasScope(token, 'storage.type.builtin') || hasScope(token, 'support.function.builtin'));
    }
  }
  for (const table of ['STR_METHODS', 'LIST_METHODS']) {
    const methods = builtins.match(new RegExp(`${table}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`))[1];
    for (const match of methods.matchAll(/\{"([a-z_]+)",\s*BI_\w+\}/g)) {
      const [line] = tokenize(`value.${match[1]}();`);
      assertScope(line, match[1], 'support.function.builtin');
      if (!names.includes(match[1])) {
        const [freeCall] = tokenize(`${match[1]}();`);
        assertNoScope(freeCall, match[1], 'support.function.builtin');
      }
    }
  }
});

test('recognizes nested list types, optionals, and custom parameter types', () => {
  const lines = tokenize([
    'struct Node { next: Link?; }',
    'func visit(node: Node, values: list[list[int]]) -> Node? {',
    '    return node;',
    '}',
  ].join('\n'));
  assertScope(lines[0], 'Link', 'entity.name.type');
  assertScope(lines[0], '?', 'keyword.operator.optional');
  assertScope(lines[1], 'Node', 'entity.name.type');
  assertScope(lines[1], 'int', 'storage.type.builtin');
  for (const token of lines[1].tokens) {
    const text = lines[1].text.slice(token.startIndex, token.endIndex);
    if (text === 'list') {
      assert.ok(hasScope(token, 'storage.type.builtin'));
    }
  }
  assertScope(lines[2], 'return', 'keyword.control');
});

test('does not recognize keywords or types inside longer identifiers', () => {
  const names = ['structure', 'functional', 'if_ready', 'true_value', 'nullish', 'integer', 'println_count'];
  for (const name of names) {
    const [line] = tokenize(`${name} := 0;`);
    for (const prefix of ['keyword', 'storage.type', 'constant.language', 'support.function.builtin']) {
      assertNoScope(line, name, prefix);
    }
  }
});

test('recognizes numeric bases, separators, and floating-point exponents', () => {
  const integers = ['0', '42', '1_000_000', '0xFF', '0xdead_beef', '0o755', '0b1010_0101'];
  const floats = ['1.0', '0.5', '2.5e-3', '6.02e23', '1e9', '2E+10', '1_000.25'];
  for (const literal of [...integers, ...floats]) {
    const [line] = tokenize(`value := ${literal};`);
    assertScope(line, literal, 'constant.numeric');
    const tokens = tokensFor(line, literal);
    assert.equal(tokens.length, 1, `${literal} should remain one numeric token`);
    assert.equal(line.text.slice(tokens[0].startIndex, tokens[0].endIndex), literal);
  }
});

test('keeps range operators separate from decimal points', () => {
  for (const operator of ['..', '..=']) {
    const [line] = tokenize(`for i in 1${operator}10 { println(i); }`);
    assertScope(line, '1', 'constant.numeric');
    assertScope(line, '10', 'constant.numeric');
    assertNoScope(line, operator, 'constant.numeric');
    assertScope(line, operator, 'keyword.operator');
    const numeric = line.tokens.filter((token) => hasScope(token, 'constant.numeric'));
    assert.deepEqual(numeric.map((token) => line.text.slice(token.startIndex, token.endIndex)), ['1', '10']);
  }
  const [line] = tokenize('value := 1.; other := .5;');
  for (const token of line.tokens.filter((item) => hasScope(item, 'constant.numeric'))) {
    assert.ok(!line.text.slice(token.startIndex, token.endIndex).includes('.'),
      'The compiler requires digits on both sides of a decimal point');
  }
});

test('recognizes all supported string escapes without opening interpolation', () => {
  const escapes = [String.raw`\\`, String.raw`\"`, String.raw`\n`, String.raw`\r`,
    String.raw`\t`, String.raw`\0`, String.raw`\x41`, String.raw`\u{1F600}`];
  for (const escape of escapes) {
    const [line] = tokenize(`value := "before ${escape} after"; return 0;`);
    assertScope(line, escape, 'constant.character.escape');
    assertScope(line, 'after', 'string.quoted.double');
    assertNoScope(line, 'after', 'meta.embedded.expression');
    assertScope(line, 'return', 'keyword.control');
    assertNoScope(line, 'return', 'string');
  }
});

test('keeps comments and braces inside plain strings as text', () => {
  const [line] = tokenize('println("// comment /* nested */ {value}"); return 0;');
  for (const fragment of ['// comment', '/* nested */', '{value}']) {
    assertScope(line, fragment, 'string.quoted.double');
    assertNoScope(line, fragment, 'comment');
    assertNoScope(line, fragment, 'meta.embedded.expression');
  }
  assertScope(line, 'return', 'keyword.control');
  assertNoScope(line, 'return', 'string');
});

test('supports nested block comments across lines and then resumes code', () => {
  const lines = tokenize([
    '/* outer comment',
    '   /* inner comment',
    '      still inner */ still outer',
    '   */ func main() -> int { return 0; } // trailing comment',
  ].join('\n'));
  assertScope(lines[0], 'outer comment', 'comment.block');
  assertScope(lines[1], 'inner comment', 'comment.block');
  assertScope(lines[2], 'still inner', 'comment.block');
  assertScope(lines[2], 'still outer', 'comment.block');
  assertScope(lines[3], 'func', 'storage.type.function');
  assertNoScope(lines[3], 'func', 'comment');
  assertScope(lines[3], 'trailing comment', 'comment.line');
});

test('highlights formatted-string expressions and returns to literal text', () => {
  const [line] = tokenize('println(f"score {len(items) + 2} points"); return 0;');
  assertScope(line, 'score', 'string.quoted.double.interpolated');
  assertNoScope(line, 'score', 'meta.embedded.expression');
  assertScope(line, 'len', 'support.function.builtin');
  assertScope(line, 'len', 'meta.embedded.expression');
  assertScope(line, '2', 'constant.numeric');
  assertScope(line, '2', 'meta.embedded.expression');
  assertScope(line, 'points', 'string.quoted.double.interpolated');
  assertNoScope(line, 'points', 'meta.embedded.expression');
  assertScope(line, 'return', 'keyword.control');
  assertNoScope(line, 'return', 'string');
});

test('handles strings and nested formatted strings inside interpolation', () => {
  const [plain] = tokenize('println(f"result {len("hello") + 1} done");');
  assertScope(plain, 'hello', 'string.quoted.double');
  assertScope(plain, 'hello', 'meta.embedded.expression');
  assertScope(plain, '1', 'constant.numeric');
  assertScope(plain, '1', 'meta.embedded.expression');
  assertNoScope(plain, 'done', 'meta.embedded.expression');

  const [nested] = tokenize('println(f"outer {f"inner {value + 3}"} done"); return 0;');
  assertScope(nested, 'inner', 'string.quoted.double.interpolated');
  assertScope(nested, 'value', 'meta.embedded.expression');
  assertScope(nested, '3', 'constant.numeric');
  assertNoScope(nested, 'done', 'meta.embedded.expression');
  assertNoScope(nested, 'return', 'string');
});

test('balances nested braces before ending an interpolation', () => {
  const [line] = tokenize('value := f"result { { 1 } + 2 } done"; return 0;');
  assertScope(line, '1', 'meta.embedded.expression');
  assertScope(line, '2', 'meta.embedded.expression');
  assertScope(line, '2', 'constant.numeric');
  assertNoScope(line, 'done', 'meta.embedded.expression');
  assertScope(line, 'done', 'string.quoted.double.interpolated');
  assertNoScope(line, 'return', 'string');
});

test('treats doubled formatted-string braces as literal text', () => {
  const [line] = tokenize('println(f"{{name}} = {score}, {{}} done");');
  for (const fragment of ['{{name}}', '{{}}']) {
    assertScope(line, fragment, 'string.quoted.double.interpolated');
    assertNoScope(line, fragment, 'meta.embedded.expression');
  }
  assertScope(line, 'score', 'meta.embedded.expression');
  assertNoScope(line, 'done', 'meta.embedded.expression');
});

test('recovers from incomplete single-line strings on the next line', () => {
  const incomplete = ['value := "unfinished', 'value := "unfinished\\',
    'value := f"unfinished', 'value := f"unfinished\\'];
  for (const source of incomplete) {
    const lines = tokenize(`${source}\nprintln("next line"); return 0;`);
    assertScope(lines[1], 'println', 'support.function.builtin');
    assertNoScope(lines[1], 'println', 'string');
    assertScope(lines[1], 'next line', 'string.quoted.double');
    assertScope(lines[1], 'return', 'keyword.control');
    assertNoScope(lines[1], 'return', 'string');
  }
});

test('closed strings with invalid escapes do not consume following code', () => {
  const [line] = tokenize(String.raw`value := "bad \q escape"; return 0;`);
  assertScope(line, 'escape', 'string.quoted.double');
  assertScope(line, 'return', 'keyword.control');
  assertNoScope(line, 'return', 'string');
});

async function rivelFiles(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const path = new URL(entry.name + (entry.isDirectory() ? '/' : ''), directory);
    if (entry.isDirectory()) {
      files.push(...await rivelFiles(path));
    } else if (entry.isFile() && entry.name.endsWith('.rivel')) {
      files.push(path);
    }
  }
  return files;
}

test('tokenizes every example and compiler case without timing out', async (context) => {
  const files = [
    ...await rivelFiles(new URL('examples/', repositoryRoot)),
    ...await rivelFiles(new URL('tests/cases/', repositoryRoot)),
  ];
  assert.ok(files.length >= 100, 'Expected the complete Rivel example and language corpus');
  let lineCount = 0;
  for (const file of files) {
    const lines = tokenize(await readFile(file, 'utf8'), fileURLToPath(file));
    lineCount += lines.length;
    for (const line of lines) {
      for (const token of line.tokens) {
        assert.equal(token.scopes[0], 'source.rivel', `${file.pathname} lost its language scope`);
        assert.ok(token.endIndex > token.startIndex, `${file.pathname} produced an empty token`);
      }
    }
  }
  context.diagnostic(`Tokenized ${files.length} Rivel files (${lineCount} lines).`);
});
