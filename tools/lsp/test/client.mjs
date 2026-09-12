import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { setTimeout as delay } from 'node:timers/promises';
import { fileURLToPath } from 'node:url';
export const root = fileURLToPath(new URL('../../../', import.meta.url));
export class Client {
  constructor() {
    this.process = spawn(process.execPath, [root + 'bin/rivel-lsp', '--stdio'], { stdio: ['pipe', 'pipe', 'pipe'] });
    this.pending = new Map(); this.notifications = []; this.id = 0; this.buffer = Buffer.alloc(0); this.stderr = '';
    this.process.stderr.on('data', chunk => this.stderr += chunk);
    this.process.stdout.on('data', chunk => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      while (true) {
        const end = this.buffer.indexOf('\r\n\r\n'); if (end < 0) break;
        const length = Number(this.buffer.subarray(0, end).toString().match(/Content-Length: (\d+)/i)?.[1]);
        if (!Number.isInteger(length)) throw new Error('Invalid LSP frame');
        if (this.buffer.length < end + 4 + length) break;
        const message = JSON.parse(this.buffer.subarray(end + 4, end + 4 + length));
        this.buffer = this.buffer.subarray(end + 4 + length);
        if ('id' in message) {
          const pending = this.pending.get(message.id);
          if (pending) { this.pending.delete(message.id); clearTimeout(pending.timer); message.error ? pending.reject(Object.assign(new Error(message.error.message), message.error)) : pending.resolve(message.result); }
        } else this.notifications.push(message);
      }
    });
  }
  send(message, fragmented = false) {
    const json = JSON.stringify({ jsonrpc: '2.0', ...message });
    const bytes = Buffer.from(`Content-Length: ${Buffer.byteLength(json)}\r\n\r\n${json}`);
    if (fragmented) for (let i = 0; i < bytes.length; i += 7) this.process.stdin.write(bytes.subarray(i, i + 7));
    else this.process.stdin.write(bytes);
  }
  request(method, params, fragmented) {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`Timed out: ${method}; ${this.stderr}`)); }, 15000);
      this.pending.set(id, { resolve, reject, timer }); this.send({ id, method, params }, fragmented);
    });
  }
  notify(method, params) { this.send({ method, params }); }
  async initialize(options = {}) {
    const result = await this.request('initialize', { processId: process.pid, capabilities: {}, initializationOptions: options }, true);
    this.notify('initialized', {}); return result;
  }
  open(text, uri = 'file:///tmp/rivel-language-server-test.rivel', version = 1) {
    this.notify('textDocument/didOpen', { textDocument: { uri, languageId: 'rivel', version, text } }); return uri;
  }
  async diagnostics(uri, version) {
    for (let i = 0; i < 400; i++) {
      const found = this.notifications.findLast(n => n.method === 'textDocument/publishDiagnostics' && n.params.uri === uri && (version === undefined || n.params.version === version));
      if (found) return found.params.diagnostics;
      await delay(20);
    }
    throw new Error(`No diagnostics for ${uri} version ${version}; ${this.stderr}`);
  }
  async close() {
    try { await this.request('shutdown', null); this.notify('exit'); await once(this.process, 'exit'); }
    finally { this.process.kill(); }
  }
}
export function position(text, needle, occurrence = 0) {
  let offset = -1;
  for (let i = 0; i <= occurrence; i++) offset = text.indexOf(needle, offset + 1);
  if (offset < 0) throw new Error(`Missing ${needle}`);
  const lines = text.slice(0, offset).split('\n'); return { line: lines.length - 1, character: lines.at(-1).length };
}
