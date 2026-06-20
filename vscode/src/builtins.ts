import * as vscode from "vscode";

export interface CatalogEntry {
  label: string;
  detail: string;
  documentation: string;
  insertText?: string;
  kind?: vscode.CompletionItemKind;
  /** Struct name when this is a method on a receiver type */
  receiver?: string;
}

export const KEYWORDS: CatalogEntry[] = [
  { label: "fn", detail: "keyword", documentation: "Define a function." },
  { label: "local", detail: "keyword", documentation: "Declare a local variable." },
  { label: "return", detail: "keyword", documentation: "Return from the current function." },
  { label: "import", detail: "keyword", documentation: "Import a module or symbols from a module." },
  { label: "from", detail: "keyword", documentation: "Used with `import` for module paths and selective imports." },
  { label: "as", detail: "keyword", documentation: "Bind an import alias (`import * as m from mod`)." },
  { label: "export", detail: "keyword", documentation: "Export a symbol for other modules." },
  { label: "struct", detail: "keyword", documentation: "Define a struct type." },
  { label: "interface", detail: "keyword", documentation: "Define an interface type." },
  { label: "declare", detail: "keyword", documentation: "Forward-declare syscall or external symbols." },
  { label: "syscall", detail: "keyword", documentation: "Mark a declare block as an OS bridge syscall." },
  { label: "external", detail: "keyword", documentation: "Link against a symbol defined in another object file." },
  { label: "new", detail: "keyword", documentation: "Allocate a struct or array value." },
  { label: "delete", detail: "keyword", documentation: "Free heap memory." },
  { label: "if", detail: "keyword", documentation: "Conditional branch." },
  { label: "else", detail: "keyword", documentation: "Alternative branch for `if`." },
  { label: "while", detail: "keyword", documentation: "Loop while condition is truthy." },
  { label: "array", detail: "keyword", documentation: "Used in `new array T` and `array T` field types." },
  { label: "go", detail: "keyword", documentation: "Spawn a goroutine-like task (`go worker()`)." },
  { label: "true", detail: "literal", documentation: "Boolean true (`1` at runtime)." },
  { label: "false", detail: "literal", documentation: "Boolean false (`0` at runtime)." },
  { label: "null", detail: "literal", documentation: "Null pointer literal." },
];

export const TYPES: CatalogEntry[] = [
  { label: "void", detail: "type", documentation: "No value." },
  { label: "int32", detail: "type", documentation: "32-bit signed integer." },
  { label: "int64", detail: "type", documentation: "64-bit signed integer." },
  { label: "bigint", detail: "type", documentation: "128-bit integer." },
  { label: "float", detail: "type", documentation: "32-bit float." },
  { label: "double", detail: "type", documentation: "64-bit float." },
  { label: "bool", detail: "type", documentation: "Boolean stored as `i8`." },
  { label: "char", detail: "type", documentation: "Single byte character." },
  { label: "string", detail: "type", documentation: "UTF-8 string (`i8*`)." },
];

export const RUNTIME_BUILTINS: CatalogEntry[] = [
  {
    label: "print",
    detail: "print(...) → int32",
    documentation: "Variadic formatted output (printf-style). Single-arg calls append a newline.",
    insertText: 'print("$0")',
  },
  {
    label: "fetch",
    detail: "fetch(url: string) → FetchResponse",
    documentation: "HTTP/HTTPS GET client from `runtime/net.xlang`. Returns status code and body.",
    insertText: 'fetch("$0")',
  },
  {
    label: "spawn",
    detail: "spawn(fn)",
    documentation: "Enqueue a goroutine-like task on the runtime scheduler.",
    insertText: "spawn(${1:worker}(${2:args}))",
  },
  { label: "wait_all", detail: "wait_all() → int32", documentation: "Block until all spawned tasks finish.", insertText: "wait_all()" },
  { label: "cpu", detail: "cpu() → int32", documentation: "Logical CPU count.", insertText: "cpu()" },
  { label: "add_worker", detail: "add_worker() → int32", documentation: "Add a scheduler worker thread.", insertText: "add_worker()" },
];

export const COMPILER_BUILTINS: CatalogEntry[] = [
  { label: "invoke0", detail: "invoke0(fn: int64) → int32", documentation: "Call a function pointer with no arguments.", insertText: "invoke0(${1:entry})" },
  { label: "str_len", detail: "str_len(s: string) → int32", documentation: "Byte length of a string.", insertText: "str_len(${1:s})" },
  { label: "str_eq", detail: "str_eq(a, b) → int32", documentation: "Returns `1` when strings are equal.", insertText: "str_eq(${1:a}, ${2:b})" },
  { label: "str_byte", detail: "str_byte(s, i) → int32", documentation: "Byte at index (0–255).", insertText: "str_byte(${1:s}, ${2:i})" },
  { label: "str_find", detail: "str_find(hay, needle) → int32", documentation: "Index of substring or `-1`.", insertText: 'str_find(${1:haystack}, "${2:needle}")' },
  { label: "str_sub", detail: "str_sub(s, start, len) → string", documentation: "Copy substring.", insertText: "str_sub(${1:s}, ${2:start}, ${3:len})" },
  { label: "str_from_int", detail: "str_from_int(n) → string", documentation: "Format int32 as decimal string.", insertText: "str_from_int(${1:n})" },
  { label: "array_len", detail: "array_len(arr) → int64", documentation: "Number of elements in an array.", insertText: "array_len(${1:arr})" },
  { label: "array_push", detail: "array_push(arr, val) → int32", documentation: "Append to array.", insertText: "array_push(${1:arr}, ${2:val})" },
  { label: "array_get", detail: "array_get(arr, i) → T", documentation: "Read array element by index.", insertText: "array_get(${1:arr}, ${2:i})" },
  { label: "array_pop", detail: "array_pop(arr) → T", documentation: "Pop from array end.", insertText: "array_pop(${1:arr})" },
  { label: "array_pop_front", detail: "array_pop_front(arr) → T", documentation: "Pop from array front.", insertText: "array_pop_front(${1:arr})" },
];

export const TEST_BUILTINS: CatalogEntry[] = [
  {
    label: "expect",
    detail: "expect(actual) → Expect",
    documentation: "Vitest-style assertion. Chain `.toEqual`, `.toBeTrue`, `.not()`, etc.",
    insertText: "expect(${1:actual}).toEqual(${2:expected})",
  },
  { label: "expectFn", detail: "expectFn(fn) → ExpectFn", documentation: "Assert on function pointers (`.toThrow`, `.toError`).", insertText: "expectFn(${1:handler}).toThrow()" },
  { label: "fail", detail: "fail(msg) → int32", documentation: "Fail the current test.", insertText: 'fail("${1:message}")' },
  { label: "panic", detail: "panic(msg)", documentation: "Panic with stack trace.", insertText: 'panic("${1:message}")' },
  { label: "recover", detail: "recover() → int32", documentation: "Recover from panic inside defer-like scopes.", insertText: "recover()" },
];

export const EXPECT_CHAIN: CatalogEntry[] = [
  { label: "toEqual", detail: "toEqual(expected)", documentation: "Assert deep equality.", insertText: "toEqual(${1:expected})" },
  { label: "toBeTrue", detail: "toBeTrue()", documentation: "Assert value is truthy.", insertText: "toBeTrue()" },
  { label: "toBeFalse", detail: "toBeFalse()", documentation: "Assert value is falsy.", insertText: "toBeFalse()" },
  { label: "toThrow", detail: "toThrow()", documentation: "Assert function panics.", insertText: "toThrow()" },
  { label: "toError", detail: "toError()", documentation: "Assert function returns error.", insertText: "toError()" },
  { label: "not", detail: "not()", documentation: "Negate the next assertion.", insertText: "not()" },
];

export const JSON_METHODS: CatalogEntry[] = [
  { label: "Int", detail: "Int(key) → int32", documentation: "Read integer field from Json object.", insertText: 'Int("${1:key}")', receiver: "Json" },
  { label: "String", detail: "String(key) → string", documentation: "Read string field.", insertText: 'String("${1:key}")', receiver: "Json" },
  { label: "Bool", detail: "Bool(key) → int32", documentation: "Read bool field.", insertText: 'Bool("${1:key}")', receiver: "Json" },
  { label: "Has", detail: "Has(key) → int32", documentation: "Check if key exists.", insertText: 'Has("${1:key}")', receiver: "Json" },
  { label: "Get", detail: "Get(key) → JsonValue", documentation: "Get nested value.", insertText: 'Get("${1:key}")', receiver: "Json" },
  { label: "AsInt", detail: "AsInt() → int32", documentation: "Coerce JsonValue to int32.", insertText: "AsInt()", receiver: "JsonValue" },
  { label: "AsString", detail: "AsString() → string", documentation: "Coerce JsonValue to string.", insertText: "AsString()", receiver: "JsonValue" },
  { label: "AsBool", detail: "AsBool() → int32", documentation: "Coerce JsonValue to bool.", insertText: "AsBool()", receiver: "JsonValue" },
  { label: "IsNull", detail: "IsNull() → int32", documentation: "True when JsonValue is null.", insertText: "IsNull()", receiver: "JsonValue" },
];

export const ROUTER_METHODS: CatalogEntry[] = [
  { label: "Get", detail: "Get(path, handler)", documentation: "Register GET route.", insertText: 'Get("${1:/path}", ${2:handler})', receiver: "Router" },
  { label: "Post", detail: "Post(path, handler)", documentation: "Register POST route.", insertText: 'Post("${1:/path}", ${2:handler})', receiver: "Router" },
  { label: "Put", detail: "Put(path, handler)", documentation: "Register PUT route.", insertText: 'Put("${1:/path}", ${2:handler})', receiver: "Router" },
  { label: "Delete", detail: "Delete(path, handler)", documentation: "Register DELETE route.", insertText: 'Delete("${1:/path}", ${2:handler})', receiver: "Router" },
  { label: "Group", detail: "Group(prefix) → Router", documentation: "Create a route group with path prefix.", insertText: 'Group("${1:/prefix}")', receiver: "Router" },
  { label: "Mount", detail: "Mount(prefix, child)", documentation: "Mount a sub-router.", insertText: 'Mount("${1:/prefix}", ${2:child})', receiver: "Router" },
];

export const KNOWN_MODULES: CatalogEntry[] = [
  { label: "http", detail: "module", documentation: "HTTP package — `libs/http/` (router + server)." },
  { label: "http/router", detail: "module", documentation: "Chi-like router — routes, params, dispatch." },
  { label: "http/server", detail: "module", documentation: "TCP server — ListenAndServe, ServerInfo callback." },
  { label: "json", detail: "module", documentation: "JSON parse + typed field accessors." },
  { label: "test", detail: "module", documentation: "Vitest-style `expect` API and test runner hooks." },
  { label: "process", detail: "module", documentation: "Process, pipe, fd, env syscalls." },
  { label: "scheduler", detail: "module (runtime)", documentation: "spawn / wait_all worker pool." },
  { label: "net", detail: "module (runtime)", documentation: "fetch HTTP client." },
  { label: "errors", detail: "module (runtime)", documentation: "panic / recover helpers." },
];

export const PROCESS_SYSCALLS: CatalogEntry[] = [
  { label: "file_read", detail: "file_read(path) → string", documentation: "Read an entire file into a string.", insertText: 'file_read("${1:path}")' },
  { label: "env_get", detail: "env_get(key) → string", documentation: "Read environment variable.", insertText: 'env_get("${1:KEY}")' },
  { label: "cwd", detail: "cwd() → string", documentation: "Current working directory.", insertText: "cwd()" },
  { label: "run_capture", detail: "run_capture(path, args) → int32", documentation: "Run subprocess and capture stdout.", insertText: 'run_capture(${1:path}, "${2:args}")' },
];

export const NET_SYSCALLS: CatalogEntry[] = [
  { label: "net_tcp_listen", detail: "net_tcp_listen(host, port) → int64", documentation: "Open TCP listen socket." },
  { label: "net_tcp_accept", detail: "net_tcp_accept(fd) → int64", documentation: "Accept incoming connection." },
  { label: "net_send", detail: "net_send(fd, data) → int32", documentation: "Send bytes on TCP fd." },
  { label: "net_recv", detail: "net_recv(fd, max) → string", documentation: "Receive bytes from TCP fd." },
];

export function catalogToCompletion(entry: CatalogEntry, sortPrefix: string): vscode.CompletionItem {
  const kind = entry.kind ?? vscode.CompletionItemKind.Function;
  const item = new vscode.CompletionItem(entry.label, kind);
  item.detail = entry.detail;
  item.documentation = new vscode.MarkdownString(entry.documentation);
  item.sortText = `${sortPrefix}_${entry.label}`;
  if (entry.insertText) {
    item.insertText = new vscode.SnippetString(entry.insertText);
  }
  return item;
}

export function catalogToHover(entry: CatalogEntry): vscode.MarkdownString {
  const md = new vscode.MarkdownString();
  md.appendMarkdown(`**${entry.label}** — ${entry.detail}\n\n`);
  md.appendMarkdown(entry.documentation);
  return md;
}
