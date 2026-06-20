import * as path from "node:path";
import * as vscode from "vscode";
import { XlangDiagnostics, runXlank } from "./diagnostics";
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

  context.subscriptions.push(
    vscode.commands.registerCommand("xlang.formatDocument", async () => {
      const editor = vscode.window.activeTextEditor;
      if (!editor || editor.document.languageId !== "xlang") {
        return;
      }
      await vscode.commands.executeCommand("editor.action.formatDocument");
    }),
    vscode.commands.registerCommand("xlang.runFile", () => runCurrentFile()),
    vscode.commands.registerCommand("xlang.buildFile", () => buildCurrentFile())
  );
}

export function deactivate(): void {
  diagnostics?.dispose();
  diagnostics = undefined;
}

async function runCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xlang") {
    vscode.window.showWarningMessage("Open a .xlang file first.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  const filePath = editor.document.uri.fsPath;
  const cwd = path.dirname(filePath);
  const terminal = vscode.window.createTerminal({
    name: "xlang run",
    cwd,
  });

  terminal.show();
  const xlankPath = (await import("./diagnostics")).resolveXlankPath();
  const binary = (await xlankPath) ?? "xlank";
  terminal.sendText(`${quote(binary)} run ${quote(filePath)}`);
}

async function buildCurrentFile(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== "xlang") {
    vscode.window.showWarningMessage("Open a .xlang file first.");
    return;
  }

  if (editor.document.isDirty) {
    await editor.document.save();
  }

  const filePath = editor.document.uri.fsPath;
  const cwd = path.dirname(filePath);

  try {
    const { stderr } = await runXlank(["build", filePath], cwd);
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

function quote(value: string): string {
  if (/[\s"'\\]/.test(value)) {
    return `"${value.replace(/"/g, '\\"')}"`;
  }
  return value;
}
