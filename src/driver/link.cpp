// Plan 5.8 / 5.9: linker driver and in-process archiver.

#include "xlang/codegen/link.h"

#include "xlang/util/error.h"

#include <llvm/Object/ArchiveWriter.h>
#include <llvm/Support/Error.h>

#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sys/wait.h>

namespace xlang::codegen {

namespace {

[[nodiscard]] std::filesystem::path findTool(const std::vector<std::string>& names) {
    std::vector<std::filesystem::path> dirs;
    if (const char* path = std::getenv("PATH")) {
        std::string p = path;
        std::size_t start = 0;
        while (start <= p.size()) {
            const std::size_t end = p.find(':', start);
            dirs.emplace_back(p.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }
    for (const char* extra : {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin", "/opt/homebrew/bin",
                              "/usr/lib/llvm/bin", "/usr/bin"}) {
        dirs.emplace_back(extra);
    }
    std::error_code ec;
    for (const std::string& name : names) {
        for (const std::filesystem::path& d : dirs) {
            const std::filesystem::path candidate = d / name;
            if (std::filesystem::is_regular_file(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

[[nodiscard]] std::string quote(const std::string& s) {
    std::string out = "'";
    for (const char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    return out + "'";
}

}  // namespace

void link(const LinkRequest& r) {
    const TargetInfo& t = r.config->target;
    std::vector<std::string> args;
    std::vector<std::filesystem::path> temporaries;  ///< generated import libraries, removed after linking
    std::filesystem::path tool;
    const bool shared = r.kind == LinkKind::Shared;
    if (t.os == "macos") {
        tool = findTool({"ld64.lld"});
        if (tool.empty()) {
            throw XlangError("no Mach-O linker found (`ld64.lld`); install LLVM's lld, or stop at `--emit=obj`");
        }
        const std::string arch = t.arch == "aarch64" ? "arm64" : "x86_64";
        args = {"-arch", arch, "-platform_version", "macos", "11.0", "11.0", "-o", r.output.string()};
        if (shared) {
            args.push_back("-dylib");
        } else {
            args.push_back("-e");
            args.push_back("_" + (r.entry.empty() ? std::string("start") : r.entry));
            args.push_back("-no_uuid");
            args.push_back("-dead_strip");
        }
    } else if (t.os == "linux") {
        tool = findTool({"ld.lld", "ld"});
        if (tool.empty()) {
            throw XlangError("no ELF linker found (`ld.lld` or `ld`); install one, or stop at `--emit=obj`");
        }
        args = {"-o", r.output.string(), "-nostdlib", "-static"};
        if (shared) {
            args = {"-o", r.output.string(), "-nostdlib", "-shared"};
        } else {
            args.push_back("-e");
            args.push_back(r.entry.empty() ? "_start" : r.entry);
            args.push_back("--gc-sections");
        }
    } else if (t.os == "windows") {
        tool = findTool({"lld-link"});
        if (tool.empty()) {
            throw XlangError("no COFF linker found (`lld-link`); install LLVM's lld, or stop at `--emit=obj`");
        }
        args = {"/nodefaultlib", "/out:" + r.output.string(), "/subsystem:console"};
        // Native OS libraries (crypt32, ws2_32, ...) imported with `@library`: one import library per
        // DLL, generated from the symbols the program declares (no Windows SDK needed).
        std::map<std::string, std::set<std::string>> byLibrary;
        for (const auto& [lib, sym] : r.imports) {
            byLibrary[lib].insert(sym);
        }
        if (!shared) {
            byLibrary["ntdll"].insert("NtTerminateProcess");  // the entry stub's exit
        }
        if (!byLibrary.empty()) {
            const std::filesystem::path dlltool = findTool({"llvm-dlltool"});
            if (dlltool.empty()) {
                throw XlangError("no import-library tool found (`llvm-dlltool`); install LLVM, or stop at `--emit=obj`");
            }
            const std::filesystem::path dir = r.output.parent_path().empty() ? std::filesystem::path(".")
                                                                           : r.output.parent_path();
            const std::string machine = t.arch == "aarch64" ? "arm64" : "i386:x86-64";
            for (const auto& [lib, syms] : byLibrary) {
                std::string dll = lib;
                if (dll.size() < 4 || dll.substr(dll.size() - 4) != ".dll") {
                    dll += ".dll";
                }
                const std::filesystem::path def = dir / (lib + ".xl-import.def");
                const std::filesystem::path implib = dir / (lib + ".xl-import.lib");
                std::string text = "LIBRARY " + dll + "\nEXPORTS\n";
                for (const std::string& s : syms) {
                    text += "    " + s + "\n";
                }
                {
                    std::ofstream f(def, std::ios::binary);
                    f << text;
                }
                const std::string cmd = quote(dlltool.string()) + " -m " + quote(machine) + " -d " +
                                        quote(def.string()) + " -l " + quote(implib.string()) + " 2>&1";
                const int rc = std::system(cmd.c_str());
                std::filesystem::remove(def);
                if (rc != 0) {
                    throw XlangError("cannot create the import library for `" + dll + "`");
                }
                args.push_back(implib.string());
                temporaries.push_back(implib);
            }
        }
        if (shared) {
            args.push_back("/dll");
        } else {
            args.push_back("/entry:" + (r.entry.empty() ? std::string("mainCRTStartup") : r.entry));
        }
    } else {
        throw XlangError("cannot link for target OS `" + t.os + "`");
    }
    for (const std::filesystem::path& o : r.objects) {
        args.push_back(o.string());
    }
    std::string command = quote(tool.string());
    for (const std::string& a : args) {
        command += " " + quote(a);
    }
    command += " 2>&1";
    const int status = std::system(command.c_str());
    for (const std::filesystem::path& tmp : temporaries) {
        std::filesystem::remove(tmp);
    }
    if (status != 0) {
        throw XlangError(std::format("linker failed: {}", command));
    }
}

void archive(const std::vector<std::filesystem::path>& objects, const std::filesystem::path& output,
             const BuildConfig& config) {
    std::vector<llvm::NewArchiveMember> members;
    for (const std::filesystem::path& o : objects) {
        auto m = llvm::NewArchiveMember::getFile(o.string(), true);
        if (!m) {
            throw XlangError("cannot read `" + o.string() + "`: " + llvm::toString(m.takeError()));
        }
        members.push_back(std::move(*m));
    }
    const auto kind = config.target.os == "macos" ? llvm::object::Archive::K_DARWIN : llvm::object::Archive::K_GNU;
    if (llvm::Error e = llvm::writeArchive(output.string(), members, llvm::SymtabWritingMode::NormalSymtab, kind, true,
                                           false)) {
        throw XlangError("cannot write archive: " + llvm::toString(std::move(e)));
    }
}

}  // namespace xlang::codegen
