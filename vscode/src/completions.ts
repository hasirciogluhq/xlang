import * as vscode from "vscode";

type Builtin = { label: string; detail: string; insertText?: string };

const RUNTIME_BUILTINS: Builtin[] = [
  { label: "print", detail: "Variadic printf-style output", insertText: 'print("$0")' },
  { label: "fetch", detail: "HTTP/HTTPS GET → FetchResponse", insertText: 'fetch("$0")' },
  { label: "spawn", detail: "Enqueue goroutine-like task", insertText: "spawn(${1:worker}(${2:args}))" },
  { label: "wait_all", detail: "Wait for all spawned tasks", insertText: "wait_all()" },
  { label: "cpu", detail: "CPU core count", insertText: "cpu()" },
  { label: "add_worker", detail: "Add scheduler worker thread", insertText: "add_worker()" },
];

const JSON_MODULE: Builtin[] = [
  { label: "parse", detail: "json.parse(text) → Json object", insertText: "parse(${1:text})" },
  { label: "Get", detail: "json.Get(j, key) → JsonValue", insertText: 'Get(${1:data}, "${2:key}")' },
  { label: "Int", detail: "json.Int(j, key) → int32", insertText: 'Int(${1:data}, "${2:key}")' },
  { label: "String", detail: "json.String(j, key) → string", insertText: 'String(${1:data}, "${2:key}")' },
  { label: "Bool", detail: "json.Bool(j, key) → int32", insertText: 'Bool(${1:data}, "${2:key}")' },
  { label: "Has", detail: "json.Has(j, key) → int32", insertText: 'Has(${1:data}, "${2:key}")' },
  { label: "Null", detail: "json.Null(j, key) → int32", insertText: 'Null(${1:data}, "${2:key}")' },
  { label: "AsInt", detail: "json.AsInt(v) → int32", insertText: "AsInt(${1:value})" },
  { label: "AsString", detail: "json.AsString(v) → string", insertText: "AsString(${1:value})" },
  { label: "AsBool", detail: "json.AsBool(v) → int32", insertText: "AsBool(${1:value})" },
  { label: "IsNull", detail: "json.IsNull(v) → int32", insertText: "IsNull(${1:value})" },
];

const JSON_METHODS: Builtin[] = [
  { label: "Int", detail: "data.Int(key) → int32", insertText: 'Int("${1:key}")' },
  { label: "String", detail: "data.String(key) → string", insertText: 'String("${1:key}")' },
  { label: "Bool", detail: "data.Bool(key) → int32", insertText: 'Bool("${1:key}")' },
  { label: "Has", detail: "data.Has(key) → int32", insertText: 'Has("${1:key}")' },
  { label: "Null", detail: "data.Null(key) → int32", insertText: 'Null("${1:key}")' },
  { label: "Get", detail: "data.Get(key) → JsonValue", insertText: 'Get("${1:key}")' },
  { label: "AsInt", detail: "value.AsInt() → int32", insertText: "AsInt()" },
  { label: "AsString", detail: "value.AsString() → string", insertText: "AsString()" },
  { label: "AsBool", detail: "value.AsBool() → int32", insertText: "AsBool()" },
  { label: "IsNull", detail: "value.IsNull() → int32", insertText: "IsNull()" },
];

const HTTP_BUILTINS: Builtin[] = [
  { label: "NewRouter", detail: "Create HTTP router", insertText: "NewRouter()" },
  { label: "Get", detail: "Register GET route", insertText: 'Get("${1:/path}", ${2:handler})' },
  { label: "Post", detail: "Register POST route", insertText: 'Post("${1:/path}", ${2:handler})' },
  { label: "Put", detail: "Register PUT route", insertText: 'Put("${1:/path}", ${2:handler})' },
  { label: "Delete", detail: "Register DELETE route", insertText: 'Delete("${1:/path}", ${2:handler})' },
  { label: "Group", detail: "Route group with prefix", insertText: 'Group("${1:/prefix}")' },
  { label: "Mount", detail: "Mount sub-router", insertText: 'Mount("${1:/prefix}", ${2:child})' },
  { label: "Match", detail: "Match method + path → handler ptr", insertText: 'Match("${1:GET}", "${2:/path}")' },
  { label: "URLParam", detail: "Route param from last match", insertText: 'URLParam("${1:name}")' },
  { label: "RespondText", detail: "Set text/plain response", insertText: 'RespondText(${1:200}, "${2:ok}")' },
  { label: "RespondJson", detail: "Set application/json response", insertText: 'RespondJson(${1:200}, "${2:{}}")' },
  { label: "RespondHtml", detail: "Set text/html response", insertText: 'RespondHtml(${1:200}, "${2:<html></html>}")' },
  { label: "ListenAndServe", detail: "Start server; on_listen(server) when ready", insertText: 'ListenAndServe("${1:127.0.0.1}", ${2:8080}, ${3:r}, ${4:on_listen})' },
  { label: "ServerProtocol", detail: "http — active during listen callback", insertText: "ServerProtocol()" },
  { label: "ServerHostname", detail: "Listen hostname during callback", insertText: "ServerHostname()" },
  { label: "ServerPort", detail: "Listen port during callback", insertText: "ServerPort()" },
  { label: "ServeOnce", detail: "Handle single HTTP request", insertText: 'ServeOnce("${1:127.0.0.1}", ${2:8080}, ${3:r})' },
];

const COMPILER_BUILTINS: Builtin[] = [
  { label: "invoke0", detail: "Call int64 function pointer (no args)", insertText: "invoke0(${1:entry})" },
  { label: "str_len", detail: "String byte length", insertText: "str_len(${1:s})" },
  { label: "str_eq", detail: "1 if strings equal", insertText: "str_eq(${1:a}, ${2:b})" },
  { label: "str_byte", detail: "Byte at index (0–255)", insertText: "str_byte(${1:s}, ${2:i})" },
  { label: "str_find", detail: "Substring index or -1", insertText: 'str_find(${1:haystack}, "${2:needle}")' },
  { label: "str_sub", detail: "Substring copy", insertText: "str_sub(${1:s}, ${2:start}, ${3:len})" },
  { label: "str_from_int", detail: "int32 → string", insertText: "str_from_int(${1:n})" },
  { label: "array_len", detail: "Array element count", insertText: "array_len(${1:arr})" },
  { label: "array_push", detail: "Append to array", insertText: "array_push(${1:arr}, ${2:val})" },
  { label: "array_get", detail: "Array element at index", insertText: "array_get(${1:arr}, ${2:i})" },
  { label: "array_pop", detail: "Pop from array end", insertText: "array_pop(${1:arr})" },
  { label: "array_pop_front", detail: "Pop from array front", insertText: "array_pop_front(${1:arr})" },
];

const EXPECT_CHAIN: Builtin[] = [
  { label: "toEqual", detail: "Assert equality", insertText: "toEqual(${1:expected})" },
  { label: "toBeTrue", detail: "Assert truthy", insertText: "toBeTrue()" },
  { label: "toBeFalse", detail: "Assert falsy", insertText: "toBeFalse()" },
  { label: "toThrow", detail: "Assert panic", insertText: "toThrow()" },
  { label: "toError", detail: "Assert error return", insertText: "toError()" },
  { label: "not", detail: "Negate assertion", insertText: "not()" },
];

const TEST_BUILTINS: Builtin[] = [
  { label: "expect", detail: "Vitest-style assertion (chain .toEqual / .toBeTrue / .not())", insertText: "expect(${1:actual}).toEqual(${2:expected})" },
  { label: "expectFn", detail: "Expect for function pointer (.toThrow / .toError)", insertText: "expectFn(${1:handler}).toThrow()" },
  { label: "fail", detail: "Fail test with message", insertText: 'fail("${1:message}")' },
  { label: "panic", detail: "Panic with stack trace (Go-style)", insertText: 'panic("${1:message}")' },
  { label: "recover", detail: "Recover from panic (Go-style)", insertText: "recover()" },
];

const KEYWORDS = [
  "fn", "local", "return", "import", "from", "as", "export", "external",
  "syscall", "declare", "struct", "interface", "new", "delete", "if", "else", "while", "array", "go",
  "true", "false", "null",
];

const TYPES = [
  "void", "int", "int32", "int64", "bigint",
  "float", "float32", "double", "float64",
  "bool", "char", "string",
];

const MODULES = [
  { label: "http", detail: "HTTP package — router + server (libs/http/)" },
  { label: "http/router", detail: "Chi-like HTTP router (libs/http/router.xlang)" },
  { label: "http/server", detail: "HTTP server + ListenAndServe (libs/http/server.xlang)" },
  { label: "json", detail: "json.parse + field accessors (libs/json.xlang)" },
  { label: "process", detail: "fork/exec/pipe/fd/env syscalls (libs/process.xlang)" },
  { label: "test", detail: "expect / test_run_* helpers (libs/test.xlang)" },
];

const PROCESS_SYSCALLS: Builtin[] = [
  { label: "env_get", detail: "Read environment variable", insertText: 'env_get("${1:KEY}")' },
  { label: "env_set", detail: "Set environment variable", insertText: 'env_set("${1:KEY}", "${2:value}")' },
  { label: "cwd", detail: "Current working directory", insertText: "cwd()" },
  { label: "chdir", detail: "Change working directory", insertText: 'chdir("${1:/path}")' },
  { label: "run_capture", detail: "Run executable, capture stdout/stderr", insertText: 'run_capture(${1:path}, "${2:args}")' },
  { label: "capture_stdout", detail: "Stdout from last run_capture", insertText: "capture_stdout()" },
  { label: "proc_fork", detail: "Fork process (returns pid)", insertText: "proc_fork()" },
  { label: "proc_exec", detail: "Exec with newline-separated args", insertText: 'proc_exec(${1:path}, "${2:args}")' },
  { label: "proc_wait", detail: "Wait for child pid", insertText: "proc_wait(${1:pid})" },
  { label: "proc_exit", detail: "Exit current process", insertText: "proc_exit(${1:code})" },
  { label: "proc_kill", detail: "Send signal to process", insertText: "proc_kill(${1:pid}, ${2:signal})" },
  { label: "pipe_create", detail: "Create pipe (read<<32|write)", insertText: "pipe_create()" },
  { label: "file_read", detail: "Read entire file as string", insertText: 'file_read("${1:path}")' },
  { label: "pipe_write_fd", detail: "Write end of pipe handle", insertText: "pipe_write_fd(${1:handle})" },
  { label: "fd_close", detail: "Close file descriptor", insertText: "fd_close(${1:fd})" },
  { label: "fd_read", detail: "Read from fd into string", insertText: "fd_read(${1:fd}, ${2:max})" },
  { label: "fd_write", detail: "Write string to fd", insertText: 'fd_write(${1:fd}, "${2:data}")' },
  { label: "fd_dup2", detail: "Duplicate fd to target", insertText: "fd_dup2(${1:old_fd}, ${2:new_fd})" },
];

const NET_SYSCALLS: Builtin[] = [
  { label: "net_tcp_connect", detail: "TCP connect", insertText: "net_tcp_connect(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tls_connect", detail: "TLS connect", insertText: "net_tls_connect(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tcp_listen", detail: "TCP listen", insertText: "net_tcp_listen(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tcp_accept", detail: "TCP accept", insertText: "net_tcp_accept(${1:listen_fd}: int64): int64" },
  { label: "net_send", detail: "Send on TCP fd", insertText: "net_send(${1:fd}: int64, ${2:data}: string): int32" },
  { label: "net_tls_send", detail: "Send on TLS fd", insertText: "net_tls_send(${1:fd}: int64, ${2:data}: string): int32" },
  { label: "net_recv", detail: "Recv from TCP fd", insertText: "net_recv(${1:fd}: int64, ${2:max}: int32): string" },
  { label: "net_tls_recv", detail: "Recv from TLS fd", insertText: "net_tls_recv(${1:fd}: int64, ${2:max}: int32): string" },
  { label: "net_close", detail: "Close TCP fd", insertText: "net_close(${1:fd}: int64): int32" },
  { label: "net_tls_close", detail: "Close TLS fd", insertText: "net_tls_close(${1:fd}: int64): int32" },
];

export function registerCompletions(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider("xlang", {
      provideCompletionItems(document, position) {
        const line = document.lineAt(position.line).text;
        const linePrefix = line.slice(0, position.character);
        if (isInsideString(document, position)) {
          return [];
        }

        const items: vscode.CompletionItem[] = [];

        for (const kw of KEYWORDS) {
          items.push(makeItem(kw, vscode.CompletionItemKind.Keyword, kw, `0_${kw}`));
        }

        for (const ty of TYPES) {
          items.push(makeItem(ty, vscode.CompletionItemKind.TypeParameter, ty, `1_${ty}`));
        }

        for (const builtin of [
          ...RUNTIME_BUILTINS,
          ...JSON_MODULE,
          ...HTTP_BUILTINS,
          ...COMPILER_BUILTINS,
          ...TEST_BUILTINS,
        ]) {
          items.push(makeBuiltin(builtin, "2"));
        }

        if (/\.[\w]*$/.test(linePrefix)) {
          for (const method of [...JSON_METHODS, ...EXPECT_CHAIN]) {
            items.push(makeBuiltin(method, "2m"));
          }
        }

        if (/^\s*import\s+\w*\s*$/.test(linePrefix) || /^\s*import\s$/.test(linePrefix)) {
          for (const mod of MODULES) {
            const item = new vscode.CompletionItem(mod.label, vscode.CompletionItemKind.Module);
            item.detail = mod.detail;
            item.sortText = `3_${mod.label}`;
            items.push(item);
          }
        }

        if (/^\s*declare\s+(syscall|external)\s*$/.test(linePrefix.trimEnd())) {
          items.push(makeSnippet("declare syscall ${1:name}(${2:args})", "declare syscall"));
          items.push(makeSnippet("declare external fn ${1:name}(${2:args})", "declare external fn"));
        }

        if (/^\s*declare\s+syscall\s+\w*\s*$/.test(linePrefix.trimEnd())) {
          for (const sc of [...NET_SYSCALLS, ...PROCESS_SYSCALLS]) {
            items.push(makeBuiltin(sc, "4"));
          }
        }

        if (/^\s*go\s*$/.test(linePrefix.trimEnd())) {
          items.push(makeSnippet("go ${1:worker}(${2:args})", "go worker(args)"));
        }

        return items;
      },
    }, ...TRIGGER_CHARS)
  );
}

const TRIGGER_CHARS = [
  ".", "(", " ", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
  "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
];

function makeItem(
  label: string,
  kind: vscode.CompletionItemKind,
  detail: string,
  sortText: string
): vscode.CompletionItem {
  const item = new vscode.CompletionItem(label, kind);
  item.detail = detail;
  item.sortText = sortText;
  return item;
}

function makeBuiltin(builtin: Builtin, sortPrefix: string): vscode.CompletionItem {
  const item = new vscode.CompletionItem(builtin.label, vscode.CompletionItemKind.Function);
  item.detail = builtin.detail;
  if (builtin.insertText) {
    item.insertText = new vscode.SnippetString(builtin.insertText);
  }
  item.sortText = `${sortPrefix}_${builtin.label}`;
  return item;
}

function makeSnippet(insertText: string, label: string): vscode.CompletionItem {
  const item = new vscode.CompletionItem(label, vscode.CompletionItemKind.Snippet);
  item.insertText = new vscode.SnippetString(insertText);
  return item;
}

function isInsideString(document: vscode.TextDocument, position: vscode.Position): boolean {
  const text = document.lineAt(position.line).text.slice(0, position.character);
  let inString = false;
  let escaped = false;

  for (const ch of text) {
    if (inString) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch === "\\") {
        escaped = true;
        continue;
      }
      if (ch === '"') {
        inString = false;
      }
      continue;
    }
    if (ch === '"') {
      inString = true;
    }
  }

  return inString;
}
