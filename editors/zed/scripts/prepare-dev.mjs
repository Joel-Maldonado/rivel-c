import { cp, mkdir, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { homedir } from 'node:os';
import { join } from 'node:path';
const extension = new URL('../', import.meta.url);
const output = pathToFileURL(join(homedir(), '.local/share/rivel/zed-extension') + '/');
await mkdir(output, { recursive: true });
for (const entry of ['Cargo.toml', 'Cargo.lock', 'extension.toml', 'src', 'languages']) {
  await cp(new URL(entry, extension), new URL(entry, output), { recursive: true });
}
// Zed invokes cargo from its GUI environment. Pin rustc in this disposable
// development copy so a Homebrew compiler cannot bypass rustup's WASI targets.
const rustc = execFileSync('rustup', ['which', 'rustc'], { encoding: 'utf8' }).trim();
await mkdir(new URL('.cargo/', output), { recursive: true });
await writeFile(new URL('.cargo/config.toml', output), `[build]\nrustc = ${JSON.stringify(rustc)}\n`);
console.log(`In Zed, run "zed: install dev extension" and select:\n${fileURLToPath(output)}`);
