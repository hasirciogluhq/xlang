import * as vscode from "vscode";

const RUNTIME_BUILTINS: Array<{ label: string; detail: string; insertText?: string }> = [
  { label: "print", detail: "Formatted stdout (printf-style)", insertText: 'print("$0")' },
  { label: "fetch", detail: "HTTP GET → FetchResponse { status, ok, body }", insertText: 'fetch("$0")' },
  { label: "json_get_int", detail: "Parse JSON field as int32", insertText: 'json_get_int(${1:body}, "${2:key}")' },
  { label: "json_get_string", detail: "Parse JSON field as string", insertText: 'json_get_string(${1:body}, "${2:key}")' },
  { label: "json_get_bool", detail: "Parse JSON field as bool", insertText: 'json_get_bool(${1:body}, "${2:key}")' },
  { label: "spawn", detail: "Spawn goroutine-like task", insertText: "spawn(${1:worker}(${2:args}))" },
  { label: "wait_all", detail: "Wait for all spawned tasks", insertText: "wait_all()" },
  { label: "cpu", detail: "CPU count", insertText: "cpu()" },
  { label: "add_worker", detail: "Add scheduler worker", insertText: "add_worker()" },
];

const TEST_BUILTINS: Array<{ label: string; detail: string; insertText?: string }> = [
  { label: "describe", detail: "Test suite name", insertText: 'describe("${1:suite}")' },
  { label: "it", detail: "Run test function", insertText: 'it("${1:name}", ${2:test_fn})' },
  { label: "expect_eq", detail: "Assert int32 equality", insertText: "expect_eq(${1:actual}, ${2:expected})" },
  { label: "expect_ne", detail: "Assert int32 inequality", insertText: "expect_ne(${1:actual}, ${2:expected})" },
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

export function registerCompletions(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider("xlang", {
      provideCompletionItems(document, position) {
        const linePrefix = document.lineAt(position).text.slice(0, position.character);
        const inString = isInsideString(document, position);
        if (inString) {
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

        for (const builtin of [...RUNTIME_BUILTINS, ...TEST_BUILTINS]) {
          const item = new vscode.CompletionItem(builtin.label, vscode.CompletionItemKind.Function);
          item.detail = builtin.detail;
          if (builtin.insertText) {
            item.insertText = new vscode.SnippetString(builtin.insertText);
          }
          item.sortText = `2_${builtin.label}`;
          items.push(item);
        }

        if (/^\s*declare\s+(syscall|external)\s*$/.test(linePrefix.trimEnd())) {
          items.push(makeSnippet("declare syscall ${1:name}(${2:args})", "declare syscall"));
          items.push(makeSnippet("declare external fn ${1:name}(${2:args})", "declare external fn"));
        }

        return items;
      },
    })
  );
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
