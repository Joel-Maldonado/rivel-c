// Signatures mirror docs/spec.md. T is a list element or a numeric operand type.
const functions = {
  print: ['value: int | float | bool | str', 'void', 'Write a value without a newline.'],
  println: ['value: int | float | bool | str', 'void', 'Write a value followed by a newline.'],
  eprintln: ['value: int | float | bool | str', 'void', 'Write a value to standard error.'],
  len: ['value: str | list[T]', 'int', 'Return the byte length of a string or number of list elements.'],
  str: ['value: int | float | bool | str', 'str', 'Convert a printable value to a string.'],
  int: ['value: float | str', 'int | int?', 'Truncate a float, or parse a string (null on failure).'],
  float: ['value: int | str', 'float | float?', 'Convert an integer, or parse a string (null on failure).'],
  ord: ['value: str', 'int', 'Return the byte value of a one-byte string.'],
  chr: ['value: int', 'str', 'Return a one-byte string for a value in 0..255.'],
  panic: ['message: str', 'void', 'Stop the program with a panic.'],
  assert: ['condition: bool', 'void', 'Panic when the condition is false.'],
  exit: ['code: int', 'void', 'Exit the process.'],
  args: ['', 'list[str]', 'Command-line arguments, excluding the executable name.'],
  read_file: ['path: str', 'str?', 'Read a file; null on failure.'],
  write_file: ['path: str, text: str', 'bool', 'Write a file; return whether it succeeded.'],
  read_line: ['', 'str?', 'Read one line from stdin; null at EOF.'],
  sqrt: ['value: float', 'float', 'Square root.'],
  abs: ['value: int | float', 'T', 'Absolute value, preserving the operand type.'],
  min: ['a: int | float, b: int | float', 'T', 'The smaller of two numeric values.'],
  max: ['a: int | float, b: int | float', 'T', 'The larger of two numeric values.'],
  wrapping_add: ['a: int, b: int', 'int', 'Add with signed 64-bit wrapping.'],
  wrapping_sub: ['a: int, b: int', 'int', 'Subtract with signed 64-bit wrapping.'],
  wrapping_mul: ['a: int, b: int', 'int', 'Multiply with signed 64-bit wrapping.'],
};
const strings = {
  contains: ['part: str', 'bool'], starts_with: ['prefix: str', 'bool'], ends_with: ['suffix: str', 'bool'],
  find: ['part: str', 'int'], split: ['separator: str', 'list[str]'], join: ['parts: list[str]', 'str'],
  trim: ['', 'str'], upper: ['', 'str'], lower: ['', 'str'], replace: ['old: str, replacement: str', 'str'],
  repeat: ['count: int', 'str'],
};
const lists = {
  append: ['value: T', 'void'], pop: ['', 'T'], insert: ['index: int, value: T', 'void'],
  remove_at: ['index: int', 'T'], clear: ['', 'void'], contains: ['value: T', 'bool'], index_of: ['value: T', 'int'],
};
function entries(table, kind, element = 'T') {
  return Object.entries(table).map(([name, [params, type, documentation]]) => ({
    name, kind, type: type.replace(/\bT\b/g, element), documentation,
    parameters: params ? params.replace(/\bT\b/g, element).split(', ').map(p => {
      const colon = p.indexOf(':');
      return { name: p.slice(0, colon), type: p.slice(colon + 2) };
    }) : [],
  }));
}
export const builtins = entries(functions, 12);
export const keywords = ['func', 'struct', 'self', 'if', 'else', 'while', 'for', 'in', 'break', 'continue', 'return', 'true', 'false', 'null'];
export const types = ['int', 'float', 'bool', 'str', 'list', 'void'];
export const reserved = new Set([...keywords, ...types, 'enum', 'match', 'import', 'as', 'const', 'pub', 'type', 'is', ...Object.keys(functions)]);
export function builtinMembers(type) {
  if (type === 'str') return entries(strings, 6);
  if (type.startsWith('list[') && type.endsWith(']')) return entries(lists, 6, type.slice(5, -1));
  return [];
}
export function signature(symbol) {
  if (symbol.kind === 23) return `struct ${symbol.name}`;
  if (symbol.kind === 12 || symbol.kind === 6) {
    const params = symbol.parameters.filter(p => p.name !== 'self').map(p => `${p.name}: ${p.type}`).join(', ');
    return `func ${symbol.name}(${params}) -> ${symbol.type}`;
  }
  return `${symbol.name}: ${symbol.type}`;
}
