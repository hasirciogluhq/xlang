import * as path from "node:path";
import * as vscode from "vscode";
import { registerCompletions } from "./completions";
import {
  XlangDiagnostics,
  resolveCompilerPath,
  runCompiler,
} from "./diagnostics";
import { formatXlangDocument } from "./formatter";

let diagnostics: XlangDiagnostics | undefined;

export function activate(context: vscode.ExtensionContext): void {
  const collection = vscode.languages.createDiagnosticCollection("xlang");
  diagnostics = new XlangDiagnostics(collection);
  context.subscriptions.push(collection, { dispose: () => diagnostics?.dispose() });

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => {
      if (doc.languageId === "xlang") {
        void diagnostics?.refresh(doc);
      }
    }),
    vscode.workspace.onDidSaveTextDocument((doc) => {
      if (doc.languageId === "xlang") {
        void diagnostics?.refresh(doc);
      }
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      const config = vscode.workspace.getConfiguration("xlang");
      if (!config.get<boolean>("diagnosticsOnType", false)) {
        return;
      }
      if (event.document.languageId === "xlang") {
        diagnostics?.schedule(event.document);
      }
    }),
    vscode.workspace.onDidCloseTextDocument((doc) => {
      diagnostics?.clear(doc);
    })
  );

  for (const doc of vscode.workspace.textDocuments) {
    if (doc.languageId === "xlang") {
      void diagnostics?.refresh(doc);
    }
  }

  context.subscriptions.push(
    vscode.languages.registerDocumentFormattingEditProvider("xlang", {
      provideDocumentFormattingEdits(document) {
        const tabSize = vscode.workspace
          .getConfiguration("xlang")
          .get<number>("formatter.tabSize", 4);
        return formatXlangDocument(document, tabSize);
      },
    })
  );

  registerCompletions(context);

  context.subscriptions.push(
    vscode.commands.registerCommand("xlang.formatDocument", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xlang") {
        return;
      }
      await vscode.commands.executeCommand("editor.action.formatDocument");
    }),
    vscode.commands.registerCommand("xlang.runFile", () => runCurrentFile()),
    vscode.commands.registerCommand("xlang.buildFile", () => buildCurrentFile()),
    vscode.commands.registerCommand("xlang.testFile", () => testCurrentFile()),
    vscode.commands.registerCommand("xlang.testSuite", () => testSuite())
  );
}

export function deactivate(): void {
  diagnostics?.dispose();
  diagnostics = undefined;
}

async function runCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xlang") {
    vscode.window.showWarningMessage("Önce bir .xlang dosyası aç.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  const filePath = editor.document.uri.fsPath;
  if (isTestFile(filePath)) {
    vscode.window.showWarningMessage("*.test.xlang dosyaları `xlang test` ile çalışır.");
    return;
  }

  const cwd = path.dirname(filePath);
  const terminal = vscode.window.createTerminal({
    name: "xlang run",
    cwd,
  });

  terminal.show();
  const binary = (await resolveCompilerPath()) ?? "xlank";
  terminal.sendText(`${quote(binary)} run ${quote(filePath)}`);
}

async function buildCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xlang") {
    vscode.window.showWarningMessage("Önce bir .xlang dosyası aç.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  const filePath = editor.document.uri.fsPath;
  if (isTestFile(filePath)) {
    vscode.window.showWarningMessage("*.test.xlang build edilemez; `xlang test` kullan.");
    return;
  }

  const cwd = path.dirname(filePath);

  try {
    const { stderr } = await runCompiler(["build", filePath], cwd, filePath);
    if (stderr.trim()) {
      vscode.window.showInformationMessage(stderr.trim());
    } else {
      vscode.window.showInformationMessage(`Built ${path.basename(filePath)}`);
    }
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    vscode.window.showErrorMessage(message);
  }
}

async function testCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xlang") {
    vscode.window.showWarningMessage("Önce bir .xlang dosyası aç.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  const filePath = editor.document.uri.fsPath;
  const cwd = resolveWorkspaceRoot(filePath);
  const terminal = vscode.window.createTerminal({
    name: "xlang test",
    cwd,
  });

  terminal.show();
  const binary = (await resolveCompilerPath()) ?? "xlank";

  if (isTestFile(filePath)) {
    terminal.sendText(`${quote(binary)} test ${quote(filePath)}`);
    return;
  }

  const testRoot = resolveTestRoot(cwd);
  terminal.sendText(`${quote(binary)} test ${quote(testRoot)}`);
}

async function testSuite(): Promise<void> {
  const cwd = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath ?? process.cwd();
  const terminal = vscode.window.createTerminal({
    name: "xlang test",
    cwd,
  });

  terminal.show();
  const binary = (await resolveCompilerPath()) ?? "xlank";
  const testRoot = resolveTestRoot(cwd);
  terminal.sendText(`${quote(binary)} test ${quote(testRoot)}`);
}

function resolveTestRoot(cwd: string): string {
  const config = vscode.workspace.getConfiguration("xlang");
  const configured = config.get<string>("testRoot", "test/xlang").trim();
  if (path.isAbsolute(configured)) {
    return configured;
  }
  return path.join(cwd, configured);
}

function resolveWorkspaceRoot(filePath: string): string {
  const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath));
  return folder?.uri.fsPath ?? path.dirname(filePath);
}

function isTestFile(filePath: string): boolean {
  return filePath.endsWith(".test.xlang");
}

function quote(value: string): string {
  if (/[\s"'\\]/.test(value)) {
    return `"${value.replace(/"/g, '\\"')}"`;
  }
  return value;
}
