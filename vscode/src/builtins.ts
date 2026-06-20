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
  { label: "invoke1", detail: "invoke1(fn: int64, ctx) → int32", documentation: "Call a function pointer with one argument (struct pointer, i32, …). Used by HTTP handlers and listen callbacks.", insertText: "invoke1(${1:entry}, ${2:ctx})" },
  { label: "invoke", detail: "invoke(fn: int64, arg: int32) → int32", documentation: "Call a function pointer with one i32 argument.", insertText: "invoke(${1:entry}, ${2:arg})" },
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
  { label: "Group", detail: "Group(prefix) → Router", documentation: "Route group — shares parent routes with prefix.", insertText: 'Group("${1:/prefix}")', receiver: "Router" },
  { label: "Mount", detail: "Mount(prefix, child)", documentation: "Mount a sub-router.", insertText: 'Mount("${1:/prefix}", ${2:child})', receiver: "Router" },
  { label: "Match", detail: "Match(method, path) → MatchResult", documentation: "Match route without dispatching.", insertText: 'Match("${1:GET}", "${2:/path}")', receiver: "Router" },
  { label: "DispatchRequest", detail: "DispatchRequest(method, path) → Request", documentation: "Match route, invoke handler with Request, return filled response.", insertText: 'DispatchRequest("${1:GET}", "${2:/path}")', receiver: "Router" },
  { label: "ListenAndServe", detail: "ListenAndServe(host, port, on_listen)", documentation: "TCP server — callback receives ServerInfo.", insertText: 'ListenAndServe("${1:127.0.0.1}", ${2:8080}, ${3:on_listen})', receiver: "Router" },
  { label: "ServeOnce", detail: "ServeOnce(host, port, on_listen)", documentation: "Accept one connection then exit.", insertText: 'ServeOnce("${1:127.0.0.1}", ${2:8080}, ${3:on_listen})', receiver: "Router" },
];

export const HTTP_TYPES: CatalogEntry[] = [
  { label: "Router", detail: "type", documentation: "HTTP router instance — routes, dispatch, ListenAndServe.", kind: vscode.CompletionItemKind.Struct },
  { label: "Request", detail: "type", documentation: "Per-request context passed to handlers: Param, RespondText/Json/Html.", kind: vscode.CompletionItemKind.Struct },
  { label: "ServerInfo", detail: "type", documentation: "Listen callback context: Protocol, Hostname, Port.", kind: vscode.CompletionItemKind.Struct },
  { label: "MatchResult", detail: "type", documentation: "Route match result from Router.Match.", kind: vscode.CompletionItemKind.Struct },
];

export const REQUEST_METHODS: CatalogEntry[] = [
  { label: "Param", detail: "Param(name) → string", documentation: "URL path parameter for this request.", insertText: 'Param("${1:id}")', receiver: "Request" },
  { label: "RespondText", detail: "RespondText(status, body)", documentation: "Plain-text HTTP response.", insertText: 'RespondText(${1:200}, "${2:ok}")', receiver: "Request" },
  { label: "RespondJson", detail: "RespondJson(status, body)", documentation: "JSON HTTP response.", insertText: 'RespondJson(${1:200}, "${2:{}}")', receiver: "Request" },
  { label: "RespondHtml", detail: "RespondHtml(status, body)", documentation: "HTML HTTP response.", insertText: 'RespondHtml(${1:200}, "${2:<html></html>}")', receiver: "Request" },
];

export const SERVER_METHODS: CatalogEntry[] = [
  { label: "Protocol", detail: "Protocol() → string", documentation: "Listen callback — http/https.", insertText: "Protocol()", receiver: "ServerInfo" },
  { label: "Hostname", detail: "Hostname() → string", documentation: "Listen callback — bound host.", insertText: "Hostname()", receiver: "ServerInfo" },
  { label: "Port", detail: "Port() → int32", documentation: "Listen callback — bound port.", insertText: "Port()", receiver: "ServerInfo" },
];

export const SYNC_FACTORIES: CatalogEntry[] = [
  { label: "NewLock", detail: "NewLock() → Lock", documentation: "Create embeddable mutex.", insertText: "NewLock()" },
  { label: "NewRWLock", detail: "NewRWLock() → RWLock", documentation: "Create reader-writer lock.", insertText: "NewRWLock()" },
  { label: "NewAtomicInt", detail: "NewAtomicInt(n) → AtomicInt", documentation: "Heap atomic int64 cell.", insertText: "NewAtomicInt(${1:0})" },
  { label: "NewAtomicBool", detail: "NewAtomicBool(v) → AtomicBool", documentation: "Heap atomic bool cell.", insertText: "NewAtomicBool(${1:0})" },
];

export const LOCK_METHODS: CatalogEntry[] = [
  { label: "Lock", detail: "Lock() → int32", documentation: "Acquire mutex (blocks).", insertText: "Lock()", receiver: "Lock" },
  { label: "Unlock", detail: "Unlock() → int32", documentation: "Release mutex; reset timeout.", insertText: "Unlock()", receiver: "Lock" },
  { label: "SetTimeout", detail: "SetTimeout(ms) → int32", documentation: "Auto-release after ms while held.", insertText: "SetTimeout(${1:ms})", receiver: "Lock" },
  { label: "TryLock", detail: "TryLock() → int32", documentation: "Non-blocking acquire.", insertText: "TryLock()", receiver: "Lock" },
  { label: "IsHeld", detail: "IsHeld() → int32", documentation: "Returns 1 when held.", insertText: "IsHeld()", receiver: "Lock" },
];

export const RWLOCK_METHODS: CatalogEntry[] = [
  { label: "ReadLock", detail: "ReadLock() → int32", documentation: "Acquire shared read lock.", insertText: "ReadLock()", receiver: "RWLock" },
  { label: "ReadUnlock", detail: "ReadUnlock() → int32", documentation: "Release read lock.", insertText: "ReadUnlock()", receiver: "RWLock" },
  { label: "WriteLock", detail: "WriteLock() → int32", documentation: "Acquire exclusive write lock.", insertText: "WriteLock()", receiver: "RWLock" },
  { label: "WriteUnlock", detail: "WriteUnlock() → int32", documentation: "Release write lock.", insertText: "WriteUnlock()", receiver: "RWLock" },
  { label: "SetWriteTimeout", detail: "SetWriteTimeout(ms) → int32", documentation: "Auto-release write lock after ms while held.", insertText: "SetWriteTimeout(${1:ms})", receiver: "RWLock" },
  { label: "WriteIsHeld", detail: "WriteIsHeld() → int32", documentation: "Returns 1 when write lock held.", insertText: "WriteIsHeld()", receiver: "RWLock" },
];

export const ATOMIC_INT_METHODS: CatalogEntry[] = [
  { label: "Load", detail: "Load() → int64", documentation: "Atomic load.", insertText: "Load()", receiver: "AtomicInt" },
  { label: "Store", detail: "Store(val) → int32", documentation: "Atomic store.", insertText: "Store(${1:val})", receiver: "AtomicInt" },
  { label: "FetchAdd", detail: "FetchAdd(delta) → int64", documentation: "Atomic fetch-and-add.", insertText: "FetchAdd(${1:delta})", receiver: "AtomicInt" },
  { label: "CompareExchange", detail: "CompareExchange(exp, des) → int32", documentation: "Atomic compare-exchange.", insertText: "CompareExchange(${1:expected}, ${2:desired})", receiver: "AtomicInt" },
];

export const ATOMIC_BOOL_METHODS: CatalogEntry[] = [
  { label: "Load", detail: "Load() → int32", documentation: "Atomic bool load (0/1).", insertText: "Load()", receiver: "AtomicBool" },
  { label: "Store", detail: "Store(val) → int32", documentation: "Atomic bool store.", insertText: "Store(${1:val})", receiver: "AtomicBool" },
];

export const KNOWN_MODULES: CatalogEntry[] = [
  { label: "http", detail: "module", documentation: "HTTP router + TCP server (`libs/http/router.xlang`)." },
  { label: "http/router", detail: "module", documentation: "Router, Request, ListenAndServe — instance methods only." },
  { label: "json", detail: "module", documentation: "JSON parse + typed field accessors (`data.Int(key)`)." },
  { label: "test", detail: "module", documentation: "Vitest-style `expect` API and test runner hooks." },
  { label: "process", detail: "module", documentation: "Process, pipe, fd, env syscalls." },
  { label: "sync", detail: "module (runtime)", documentation: "Lock, RWLock, AtomicInt — method-style API (`l.Lock()`, `a.FetchAdd()`)." },
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
