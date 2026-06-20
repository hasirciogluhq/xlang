import * as vscode from "vscode";
import {
  ATOMIC_BOOL_METHODS,
  ATOMIC_INT_METHODS,
  COMPILER_BUILTINS,
  EXPECT_CHAIN,
  KEYWORDS,
  LOCK_METHODS,
  NET_SYSCALLS,
  PROCESS_SYSCALLS,
  REQUEST_METHODS,
  ROUTER_METHODS,
  RUNTIME_BUILTINS,
  RWLOCK_METHODS,
  SERVER_METHODS,
  TEST_BUILTINS,
  TYPES,
  catalogToHover,
  type CatalogEntry,
} from "./builtins";
import { getLanguageIndex } from "./languageIndex";
import { symbolToHover, type SymbolDoc } from "./symbols";

const WORD = /[\w.]+/;

export function registerHover(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerHoverProvider("xlang", {
      provideHover(document, position) {
        const index = getLanguageIndex();
        const line = document.lineAt(position.line).text;

        const member = findMemberExpression(line, position.character, position.line);
        if (member) {
          const sym = index.findSymbol(document, member.member, member.object);
          if (sym) {
            return new vscode.Hover(symbolToHover(sym), member.range);
          }
        }

        const word = document.getWordRangeAtPosition(position, WORD);
        if (!word) {
          return null;
        }
        const name = document.getText(word);

        const catalog = findCatalogEntry(name);
        if (catalog) {
          return new vscode.Hover(catalogToHover(catalog), word);
        }

        const sym = index.findSymbol(document, name);
        if (sym) {
          return new vscode.Hover(symbolToHover(sym), word);
        }

        return null;
      },
    })
  );
}

export function registerDefinitions(context: vscode.ExtensionContext): void {
  context.subscriptions.push(
    vscode.languages.registerDefinitionProvider("xlang", {
      provideDefinition(document, position) {
        const index = getLanguageIndex();
        const member = findMemberExpression(
          document.lineAt(position.line).text,
          position.character,
          position.line
        );
        if (member) {
          const sym = index.findSymbol(document, member.member, member.object);
          if (sym?.filePath) {
            return new vscode.Location(
              vscode.Uri.file(sym.filePath),
              new vscode.Position(sym.line, 0)
            );
          }
        }

        const word = document.getWordRangeAtPosition(position, WORD);
        if (!word) {
          return null;
        }
        const sym = index.findSymbol(document, document.getText(word));
        if (sym?.filePath && sym.line > 0) {
          return new vscode.Location(vscode.Uri.file(sym.filePath), new vscode.Position(sym.line, 0));
        }
        return null;
      },
    })
  );
}

function findMemberExpression(
  line: string,
  character: number,
  lineNumber: number
): { object: string; member: string; range: vscode.Range } | null {
  const prefix = line.slice(0, character);
  const match = /([\w]+)\.([\w]*)$/.exec(prefix);
  if (!match || !match[2]) {
    return null;
  }
  const object = match[1];
  const member = match[2];
  const start = character - member.length;
  return {
    object,
    member,
    range: new vscode.Range(lineNumber, start, lineNumber, character),
  };
}

function findCatalogEntry(name: string): CatalogEntry | undefined {
  const pools = [
    ...KEYWORDS,
    ...TYPES,
    ...RUNTIME_BUILTINS,
    ...COMPILER_BUILTINS,
    ...TEST_BUILTINS,
    ...EXPECT_CHAIN,
    ...PROCESS_SYSCALLS,
    ...NET_SYSCALLS,
    ...ROUTER_METHODS,
    ...REQUEST_METHODS,
    ...SERVER_METHODS,
    ...LOCK_METHODS,
    ...RWLOCK_METHODS,
    ...ATOMIC_INT_METHODS,
    ...ATOMIC_BOOL_METHODS,
  ];
  return pools.find((e) => e.label === name);
}
