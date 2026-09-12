import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';

const grammarPath = new URL('../syntaxes/rivel.tmLanguage.json', import.meta.url);
const bundlePath = new URL('../../textmate/Rivel.tmbundle/', import.meta.url);
const grammar = JSON.parse(await readFile(grammarPath, 'utf8'));

function escapeXml(text) {
  return text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;');
}

function plist(value, depth = 0) {
  const indent = '  '.repeat(depth);
  if (typeof value === 'string') return `${indent}<string>${escapeXml(value)}</string>`;
  if (typeof value === 'boolean') return `${indent}<${value}/>`;
  if (typeof value === 'number') return `${indent}<integer>${value}</integer>`;
  if (Array.isArray(value)) {
    return `${indent}<array>\n${value.map(item => plist(item, depth + 1)).join('\n')}\n${indent}</array>`;
  }
  if (value && typeof value === 'object') {
    const entries = Object.entries(value).map(([key, item]) =>
      `${indent}  <key>${escapeXml(key)}</key>\n${plist(item, depth + 1)}`);
    return `${indent}<dict>\n${entries.join('\n')}\n${indent}</dict>`;
  }
  throw new Error(`Unsupported plist value: ${value}`);
}

function document(value) {
  return '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n' +
    `<plist version="1.0">\n${plist(value)}\n</plist>\n`;
}

const outputs = [
  ['Syntaxes/Rivel.tmLanguage', { ...grammar, uuid: '20B58C48-56B4-4142-BBC6-693E7B9D5839' }],
  ['info.plist', { name: 'Rivel', uuid: '6EF5C6AF-A48E-4A33-9FC8-D50B38B14D05' }],
];

for (const [name, value] of outputs) {
  const path = new URL(name, bundlePath);
  const output = document(value);
  if (process.argv.includes('--check')) {
    if (await readFile(path, 'utf8') !== output) {
      throw new Error(`${fileURLToPath(path)} is out of date; run npm run export.`);
    }
  } else {
    await mkdir(new URL('.', path), { recursive: true });
    await writeFile(path, output);
  }
}
console.log(process.argv.includes('--check') ? 'TextMate export is current.' : 'TextMate bundle exported.');
