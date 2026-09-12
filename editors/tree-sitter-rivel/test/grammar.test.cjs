const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync, spawnSync } = require('node:child_process');
const grammar = path.resolve(__dirname, '..');
const root = path.resolve(grammar, '../..');
const cli = path.join(grammar, 'node_modules/tree-sitter-cli/tree-sitter');
const config = path.join(root, 'build/tree-sitter-config.json');
fs.mkdirSync(path.dirname(config), { recursive: true });
fs.writeFileSync(config, JSON.stringify({ 'parser-directories': [path.dirname(grammar)] }));
function run(args) { return execFileSync(cli, args, { cwd: grammar, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }); }
test('parse all runnable examples and language tests', () => {
  const files = execFileSync('rg', ['--files', 'examples', 'tests/cases/run', 'tests/cases/panic', 'tests/cases/warn'], { cwd: root, encoding: 'utf8' }).trim().split('\n').filter(p => p.endsWith('.rivel'));
  assert.ok(files.length >= 40);
  run(['parse', '--config-path', config, '--quiet', ...files.map(p => path.join(root, p))]);
});
test('nested comments and interpolation preserve their syntax structure', () => {
  const fixture = path.join(root, 'build/tree-sitter-fixture.rivel');
  fs.writeFileSync(fixture, '/* outer /* inner */ still comment */\nfunc main() { println(f"outer {f"inner {1 + 2}"} {{ok}}"); }');
  const tree = run(['parse', '--config-path', config, fixture]);
  assert.ok(!tree.includes('ERROR'), tree);
  assert.equal((tree.match(/block_comment/g) ?? []).length, 1);
  assert.equal((tree.match(/formatted_string \[/g) ?? []).length, 2);
  assert.ok(tree.includes('binary_expression'));
});
test('incomplete source reports a recoverable syntax error', () => {
  const fixture = path.join(root, 'build/tree-sitter-incomplete.rivel');
  fs.writeFileSync(fixture, 'func main() { value := Point(x: 1); value. }');
  const result = spawnSync(cli, ['parse', '--config-path', config, fixture], { cwd: grammar, encoding: 'utf8' });
  assert.equal(result.status, 1);
  assert.match(result.stdout, /ERROR|MISSING/);
});
test('all Zed queries compile against the generated parser', () => {
  const queries = path.join(root, 'editors/zed/languages/rivel');
  for (const name of fs.readdirSync(queries).filter(n => n.endsWith('.scm'))) {
    run(['query', '--config-path', config, path.join(queries, name), path.join(root, 'examples/structs.rivel')]);
  }
  assert.equal(fs.readFileSync(path.join(queries, 'highlights.scm'), 'utf8'), fs.readFileSync(path.join(grammar, 'queries/highlights.scm'), 'utf8'));
});
