import { execFile } from 'node:child_process';
import { existsSync, realpathSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { TextDocument } from 'vscode-languageserver-textdocument';

export function compilerPath(configured) {
  if (configured) return configured;
  if (process.env.RIVELC) return process.env.RIVELC;
  const sibling = join(dirname(realpathSync(process.argv[1])), 'rivelc');
  return existsSync(sibling) ? sibling : 'rivelc';
}
export function compile(text, compiler, signal) {
  if (Buffer.byteLength(text) > 2 * 1024 * 1024) return Promise.reject(new Error('Rivel analysis supports documents up to 2 MiB.'));
  return new Promise((resolve, reject) => {
    const child = execFile(compiler, ['--analyze', '-'], {
      timeout: 10000, maxBuffer: 32 * 1024 * 1024, signal,
    }, (error, stdout, stderr) => {
      if (error) return reject(new Error(`Cannot analyze with ${compiler}: ${stderr.trim() || error.message}`));
      try {
        const data = JSON.parse(stdout);
        if (data.schemaVersion !== 1) throw new Error('Unsupported compiler analysis format; rebuild Rivel.');
        resolve(data);
      } catch (error) { reject(error); }
    });
    child.stdin.on('error', () => {}); // A failed compiler may close stdin before receiving the buffer.
    child.stdin.end(text);
  });
}
export class Analysis {
  constructor(document, data) {
    this.document = document;
    this.text = document.getText();
    Object.assign(this, data);
    this.byId = new Map(this.symbols.map(s => [s.id, s]));
    this.references = [...this.references, ...this.symbols.map(s => ({ range: s.selection, symbol: s.id }))];
    const utf16 = this.utf16 = new Uint32Array(Buffer.byteLength(this.text) + 1);
    const bytes = this.bytes = new Uint32Array(this.text.length + 1);
    let byte = 0, unit = 0;
    for (const char of this.text) {
      const width = Buffer.byteLength(char);
      for (let j = 0; j < width; j++) utf16[byte + j] = unit;
      for (let j = 0; j < char.length; j++) bytes[unit + j] = byte;
      byte += width; unit += char.length;
    }
    utf16[byte] = unit; bytes[unit] = byte;
  }
  offset(position) { return this.bytes[this.document.offsetAt(position)]; }
  position(byte) { return this.document.positionAt(this.utf16[Math.min(byte, this.utf16.length - 1)]); }
  range(span) { return { start: this.position(span[0]), end: this.position(span[1]) }; }
  slice(span) { return this.text.slice(this.utf16[span[0]], this.utf16[span[1]]); }
  referenceAt(byte) {
    return this.references.find(r => r.range[0] <= byte && byte < r.range[1])
      ?? this.references.find(r => r.range[1] === byte);
  }
  refsTo(id) {
    return [...new Map(this.references.filter(r => r.symbol === id).map(r => [r.range.join(':'), r])).values()];
  }
  visible(byte) {
    return this.symbols.filter(s => s.kind !== 8 && s.kind !== 6 && s.scope[0] <= byte && byte <= s.scope[1]);
  }
  members(type) {
    const owner = this.symbols.find(s => s.kind === 23 && s.name === type);
    return owner ? this.symbols.filter(s => s.owner === owner.id) : [];
  }
  expressionBefore(byte) {
    return this.expressions.filter(e => e.range[1] <= byte && !this.slice([e.range[1], byte]).trim())
      .sort((a, b) => b.range[1] - a.range[1] || a.range[0] - b.range[0])[0];
  }
}
export async function analyzeText(uri, version, text, compiler, signal) {
  return new Analysis(TextDocument.create(uri, 'rivel', version, text), await compile(text, compiler, signal));
}
