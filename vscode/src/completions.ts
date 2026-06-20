import * as vscode from "vscode";
import {
  COMPILER_BUILTINS,
  EXPECT_CHAIN,
  HTTP_TYPES,
  KEYWORDS,
  NET_SYSCALLS,
  PROCESS_SYSCALLS,
  RUNTIME_BUILTINS,
  SYNC_FACTORIES,
  TEST_BUILTINS,
  TYPES,
  catalogToCompletion,
} from "./builtins";
import { getLanguageIndex } from "./languageIndex";
import { symbolToCompletion } from "./symbols";

const TRIGGER_CHARS = [".", "(", " ", ..."abcdefghijklmnopqrstuvwxyz"];

export function registerCompletions(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      "xlang",
      {
        provideCompletionItems(document, position) {
          if (isInsideString(document, position)) {
            return [];
          }

          const index = getLanguageIndex();
          const line = document.lineAt(position.line).text;
          const linePrefix = line.slice(0, position.character);
          const items: vscode.CompletionItem[] = [];

          const memberMatch = /([\w]+)\.([\w]*)$/.exec(linePrefix);
          if (memberMatch) {
            const objectName = memberMatch[1];
            const members = index.resolveMemberAccess(document, objectName);
            for (const sym of members) {
              items.push(symbolToCompletion(sym, "0"));
            }
            if (items.length > 0) {
              return items;
            }
          }

          if (isImportLine(linePrefix)) {
            for (const mod of index.listWorkspaceModules(document.uri.fsPath)) {
              const item = new vscode.CompletionItem(mod.path, vscode.CompletionItemKind.Module);
              item.detail = "module";
              item.documentation = new vscode.MarkdownString(mod.documentation);
              item.sortText = `0_${mod.path}`;
              items.push(item);
            }
            for (const snippet of importSnippets()) {
              items.push(snippet);
            }
            return items;
          }

          for (const kw of KEYWORDS) {
            items.push(catalogToCompletion(kw, "1"));
          }
          for (const ty of TYPES) {
            items.push(catalogToCompletion({ ...ty, kind: vscode.CompletionItemKind.TypeParameter }, "2"));
          }

          for (const entry of [
            ...RUNTIME_BUILTINS,
            ...COMPILER_BUILTINS,
            ...TEST_BUILTINS,
            ...PROCESS_SYSCALLS,
          ]) {
            items.push(catalogToCompletion(entry, "3"));
          }

          for (const sym of index.localSymbols(document)) {
            if (sym.kind === "function" || sym.kind === "struct" || sym.exported) {
              items.push(symbolToCompletion(sym, "4"));
            }
          }

          for (const binding of index.resolveImportBindings(document)) {
            const item = new vscode.CompletionItem(binding.alias, vscode.CompletionItemKind.Module);
            item.detail = binding.modulePath;
            item.documentation = new vscode.MarkdownString(`Import alias → \`${binding.modulePath}\``);
            item.sortText = `5_${binding.alias}`;
            items.push(item);

            if (binding.modulePath === "sync" || binding.alias === "sync") {
              for (const entry of SYNC_FACTORIES) {
                items.push(catalogToCompletion(entry, "5"));
              }
            }
            if (binding.modulePath === "http" || binding.modulePath.startsWith("http/")) {
              for (const entry of HTTP_TYPES) {
                items.push(catalogToCompletion(entry, "5"));
              }
            }
          }

          if (/expect\s*\([^)]*\)\s*\.\s*[\w]*$/.test(linePrefix) || /\.(to\w*)?$/.test(linePrefix)) {
            for (const chain of EXPECT_CHAIN) {
              items.push(catalogToCompletion(chain, "6"));
            }
          }

          if (/^\s*declare\s+syscall\s+\w*\s*$/.test(linePrefix.trimEnd())) {
            for (const sc of [...NET_SYSCALLS, ...PROCESS_SYSCALLS]) {
              items.push(catalogToCompletion(sc, "7"));
            }
          }

          if (/^\s*go\s*$/.test(linePrefix.trimEnd())) {
            items.push(makeSnippet("go ${1:worker}(${2:args})", "go worker(args)"));
          }

          return items;
        },
      },
      ...TRIGGER_CHARS
    )
  );
}

function isImportLine(linePrefix: string): boolean {
  return /^\s*import\s/.test(linePrefix) || /^\s*from\s+[\w./]+\s+import\s/.test(linePrefix);
}

function importSnippets(): vscode.CompletionItem[] {
  return [
    makeSnippet('import * as ${1:alias} from ${2:module}', "import * as alias from module"),
    makeSnippet('import ${1:Name} from ${2:module}', "import Name from module"),
    makeSnippet('import * as ${1:a}, ${2:Name} from ${3:module}', "import * as a, Name from module"),
    makeSnippet('import ${1:alias} from ${2:module/path}', "import alias from module/path"),
    makeSnippet("from ${1:module} import ${2:symbol}", "from module import symbol"),
  ];
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
