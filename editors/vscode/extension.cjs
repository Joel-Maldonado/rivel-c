const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
let client;
async function start(context) {
  if (!vscode.workspace.isTrusted || client) return;
  const config = vscode.workspace.getConfiguration('rivel');
  const command = config.get('serverPath') || 'rivel-lsp';
  client = new LanguageClient('rivel', 'Rivel', { command, args: ['--stdio'] }, {
    documentSelector: [{ scheme: 'file', language: 'rivel' }, { scheme: 'untitled', language: 'rivel' }],
    initializationOptions: { compilerPath: config.get('compilerPath') || undefined },
    outputChannel: vscode.window.createOutputChannel('Rivel Language Server', { log: true }),
  });
  await client.start();
}
exports.activate = async context => {
  context.subscriptions.push(vscode.workspace.onDidGrantWorkspaceTrust(() => start(context)));
  context.subscriptions.push(vscode.commands.registerCommand('rivel.restartServer', async () => {
    if (client) await client.stop();
    client = undefined;
    await start(context);
  }));
  context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(async event => {
    if (event.affectsConfiguration('rivel')) {
      if (client) await client.stop();
      client = undefined;
      await start(context);
    }
  }));
  await start(context);
};
exports.deactivate = () => client?.stop();
