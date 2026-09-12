import { mkdir, readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const extensionRoot = new URL('../', import.meta.url);
const manifest = JSON.parse(await readFile(new URL('package.json', extensionRoot), 'utf8'));
const outputDirectory = new URL('../../dist/', extensionRoot);
await mkdir(outputDirectory, { recursive: true });
const output = fileURLToPath(new URL(`rivel-${manifest.version}.vsix`, outputDirectory));
const require = createRequire(import.meta.url);
const cli = require.resolve('@vscode/vsce/vsce');
const result = spawnSync(process.execPath, [cli, 'package', '--no-dependencies', '--out', output], {
  cwd: fileURLToPath(extensionRoot),
  stdio: 'inherit',
});
if (result.error) throw result.error;
process.exitCode = result.status ?? 1;
