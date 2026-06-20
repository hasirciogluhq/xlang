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

const JSON_BUILTINS: Builtin[] = [
  { label: "json_get_int", detail: "JSON object field as int32", insertText: 'json_get_int(${1:body}, "${2:key}")' },
  { label: "json_get_string", detail: "JSON object field as string", insertText: 'json_get_string(${1:body}, "${2:key}")' },
  { label: "json_get_bool", detail: "JSON object field as bool", insertText: "json_get_bool(${1:body}, \"${2:key}\")" },
  { label: "json_has_key", detail: "1 if JSON key exists", insertText: 'json_has_key(${1:body}, "${2:key}")' },
  { label: "json_is_null", detail: "1 if JSON field is null", insertText: 'json_is_null(${1:body}, "${2:key}")' },
  { label: "json_get_field", detail: "Raw JsonResult { ok, value, next }", insertText: 'json_get_field(${1:body}, "${2:key}")' },
  { label: "json_unescape", detail: "Unescape JSON string contents", insertText: "json_unescape(${1:raw})" },
];

const HTTP_BUILTINS: Builtin[] = [
  { label: "NewRouter", detail: "Create HTTP router", insertText: "NewRouter()" },
  { label: "Get", detail: "Register GET route", insertText: 'Get(${1:r}, "${2:/path}", ${3:handler})' },
  { label: "Post", detail: "Register POST route", insertText: 'Post(${1:r}, "${2:/path}", ${3:handler})' },
  { label: "Put", detail: "Register PUT route", insertText: 'Put(${1:r}, "${2:/path}", ${3:handler})' },
  { label: "Delete", detail: "Register DELETE route", insertText: 'Delete(${1:r}, "${2:/path}", ${3:handler})' },
  { label: "Group", detail: "Route group with prefix", insertText: 'Group(${1:r}, "${2:/prefix}")' },
  { label: "Mount", detail: "Mount sub-router", insertText: 'Mount(${1:r}, "${2:/prefix}", ${3:child})' },
  { label: "Match", detail: "Match method + path → handler ptr", insertText: 'Match(${1:r}, "${2:GET}", "${3:/path}")' },
  { label: "URLParam", detail: "Route param from last match", insertText: 'URLParam("${1:name}")' },
  { label: "RespondText", detail: "Set text/plain response", insertText: 'RespondText(${1:200}, "${2:ok}")' },
  { label: "RespondJson", detail: "Set application/json response", insertText: 'RespondJson(${1:200}, "${2:{}}")' },
  { label: "ListenAndServe", detail: "Start HTTP server loop", insertText: 'ListenAndServe("${1:127.0.0.1}", ${2:8080}, ${3:r})' },
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

const TEST_BUILTINS: Builtin[] = [
  { label: "describe", detail: "Test suite name", insertText: 'describe("${1:suite}")' },
  { label: "it", detail: "Run test function", insertText: 'it("${1:name}", ${2:test_fn})' },
  { label: "expect_eq", detail: "Assert int32 equality", insertText: "expect_eq(${1:actual}, ${2:expected})" },
  { label: "expect_ne", detail: "Assert int32/int64 inequality", insertText: "expect_ne(${1:actual}, ${2:expected})" },
  { label: "expect_str_eq", detail: "Assert string equality", insertText: 'expect_str_eq(${1:actual}, "${2:expected}")' },
  { label: "expect_true", detail: "Assert truthy value", insertText: "expect_true(${1:value})" },
  { label: "expect_false", detail: "Assert falsy value", insertText: "expect_false(${1:value})" },
  { label: "test_summary", detail: "Print summary, return failure count", insertText: "test_summary()" },
];

const KEYWORDS = [
  "fn", "local", "return", "import", "from", "as", "export", "external",
  "syscall", "declare", "struct", "new", "delete", "if", "else", "while", "array",
  "true", "false", "null",
];

const TYPES = [
  "void", "int", "int32", "int64", "bigint",
  "float", "float32", "double", "float64",
  "bool", "char", "string",
];

const MODULES = [
  { label: "http", detail: "HTTP router + server (runtime/http.xlang)" },
  { label: "http/router", detail: "Chi-like HTTP router" },
  { label: "http/server", detail: "HTTP server + ListenAndServe" },
  { label: "net", detail: "fetch HTTP/HTTPS client" },
  { label: "json", detail: "JSON parse helpers" },
  { label: "scheduler", detail: "spawn / wait_all worker pool" },
  { label: "test", detail: "describe / it / expect_* runner" },
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
          const item = new vscode.CompletionItem(kw, vscode.CompletionItemKind.Keyword);
          item.sortText = `0_${kw}`;
          items.push(item);
        }

        for (const ty of TYPES) {
          const item = new vscode.CompletionItem(ty, vscode.CompletionItemKind.TypeParameter);
          item.sortText = `1_${ty}`;
          items.push(item);
        }

        const allBuiltins = [
          ...RUNTIME_BUILTINS,
          ...JSON_BUILTINS,
          ...HTTP_BUILTINS,
          ...COMPILER_BUILTINS,
          ...TEST_BUILTINS,
        ];
        for (const builtin of allBuiltins) {
          items.push(makeBuiltin(builtin, "2"));
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
          for (const sc of NET_SYSCALLS) {
            const item = new vscode.CompletionItem(sc.label, vscode.CompletionItemKind.Function);
            item.detail = sc.detail;
            item.insertText = new vscode.SnippetString(sc.insertText ?? sc.label);
            item.sortText = `4_${sc.label}`;
            items.push(item);
          }
        }

        return items;
      },
    }, ...ALL_TRIGGER_CHARS)
  );
}

const NET_SYSCALLS: Builtin[] = [
  { label: "net_tcp_connect", detail: "TCP connect", insertText: "net_tcp_connect(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tls_connect", detail: "TLS connect", insertText: "net_tls_connect(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tcp_listen", detail: "TCP listen", insertText: "net_tcp_listen(${1:host}: string, ${2:port}: int32): int64" },
  { label: "net_tcp_accept", detail: "TCP accept", insertText: "net_tcp_accept(${1:listen_fd}: int64): int64" },
  { label: "net_send", detail: "Send on TCP fd", insertText: "net_send(${1:fd}: int64, ${2:data}: string): int32" },
  { label: "net_recv", detail: "Recv from TCP fd", insertText: "net_recv(${1:fd}: int64, ${2:max}: int32): string" },
  { label: "net_close", detail: "Close TCP fd", insertText: "net_close(${1:fd}: int64): int32" },
];

const ALL_TRIGGER_CHARS = [
  ".", "(", " ", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m",
  "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z",
];

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
