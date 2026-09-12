import { runTests } from '@vscode/test-electron';
import { mkdir, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
const root = fileURLToPath(new URL('../../../', import.meta.url));
const profile = root + 'build/vscode-lsp-qa';
await mkdir(profile + '/user/User', { recursive: true });
const file = profile + '/smoke.rivel';
await writeFile(file, 'struct Point { x: int; }\nfunc main() { p := Point(x: 1); println(p.x); }\n');
await writeFile(profile + '/smoke.rv', 'func main() { message := "Hello"; println(message); }\n');
await writeFile(profile + '/user/User/settings.json', JSON.stringify({
  'rivel.serverPath': root + 'bin/rivel-lsp', 'rivel.compilerPath': root + 'bin/rivelc',
  'security.workspace.trust.enabled': false, 'window.confirmSaveUntitledWorkspace': false,
}));
await runTests({
  vscodeExecutablePath: process.env.VSCODE_EXECUTABLE,
  extensionDevelopmentPath: root + 'editors/vscode',
  extensionTestsPath: root + 'editors/vscode/test/host.cjs',
  extensionTestsEnv: { RIVEL_TEST_FILE: file },
  launchArgs: ['--user-data-dir', profile + '/user', '--extensions-dir', profile + '/extensions', '--skip-welcome', '--skip-release-notes', '--disable-updates', '--disable-workspace-trust', file],
});
