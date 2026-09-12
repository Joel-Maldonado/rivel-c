const assert = require('node:assert/strict');
const vscode = require('vscode');
exports.run = async () => {
  const extension = vscode.extensions.getExtension('Joel-Maldonado.rivel');
  assert.ok(extension, 'Rivel extension loaded');
  await extension.activate();
  const document = await vscode.workspace.openTextDocument(vscode.Uri.file(process.env.RIVEL_TEST_FILE));
  assert.equal(document.languageId, 'rivel');
  await vscode.window.showTextDocument(document);
  const text = document.getText();
  const pos = document.positionAt(text.indexOf('p.x') + 2);
  let hover;
  for (let i = 0; i < 100; i++) {
    hover = await vscode.commands.executeCommand('vscode.executeHoverProvider', document.uri, pos);
    if (hover?.length) break;
    await new Promise(r => setTimeout(r, 100));
  }
  assert.ok(hover?.some(h => h.contents.some(c => c.value.includes('x: int'))), 'Compiler-backed hover');
  const definitions = await vscode.commands.executeCommand('vscode.executeDefinitionProvider', document.uri, pos);
  assert.equal(definitions[0].range.start.line, 0);
  const completions = await vscode.commands.executeCommand('vscode.executeCompletionItemProvider', document.uri, pos);
  assert.ok(completions.items.some(i => i.label === 'x'));
  const rename = await vscode.commands.executeCommand('vscode.executeDocumentRenameProvider', document.uri, pos, 'coordinate');
  assert.equal(rename.get(document.uri).length, 3);
  const edit = new vscode.WorkspaceEdit();
  edit.replace(document.uri, new vscode.Range(pos, pos.translate(0, 1)), 'missing');
  await vscode.workspace.applyEdit(edit);
  let diagnostics;
  for (let i = 0; i < 100; i++) {
    diagnostics = vscode.languages.getDiagnostics(document.uri);
    if (diagnostics.some(d => /missing/.test(d.message))) break;
    await new Promise(r => setTimeout(r, 100));
  }
  assert.ok(diagnostics.some(d => /missing/.test(d.message)), 'Live diagnostics on unsaved edit');
  await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
  const shortDocument = await vscode.workspace.openTextDocument(vscode.Uri.file(process.env.RIVEL_TEST_FILE.replace(/\.rivel$/, '.rv')));
  assert.equal(shortDocument.languageId, 'rivel', '.rv files are detected as Rivel');
  await vscode.window.showTextDocument(shortDocument);
  const shortPosition = shortDocument.positionAt(shortDocument.getText().lastIndexOf('message'));
  const shortHover = await vscode.commands.executeCommand('vscode.executeHoverProvider', shortDocument.uri, shortPosition);
  assert.ok(shortHover?.some(h => h.contents.some(c => c.value.includes('message: str'))), '.rv files receive compiler-backed hover');
  await vscode.commands.executeCommand('workbench.action.revertAndCloseActiveEditor');
  console.log('VS Code host passed: .rivel/.rv detection, hover, definition, completion, rename, unsaved diagnostics.');
};
