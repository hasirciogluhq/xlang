#pragma once

#include "xlang/ast.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xlang {

class ModuleLoader {
public:
    explicit ModuleLoader(std::filesystem::path entry,
                           std::vector<std::filesystem::path> search_paths = {});

    [[nodiscard]] Program load();

private:
    [[nodiscard]] Program loadFile(const std::filesystem::path& path);
    [[nodiscard]] Program loadPackage(const std::filesystem::path& dir);
    [[nodiscard]] std::filesystem::path resolveModule(const std::filesystem::path& from,
                                                      const std::string& name) const;
    [[nodiscard]] std::optional<std::string> findSubmodulePath(const std::string& package,
                                                               const std::string& name) const;
    void mergeAll(Program& into, const Program& from);
    void mergePrefixed(Program& into, const Program& from, const std::string& alias);
    void mergeSelected(Program& into, const Program& from, const ImportDecl& import);
    void mergeFromClauses(Program& into, ImportDecl& import,
                          const std::filesystem::path& from_dir);
    void collectPreludeStructs(const ImportDecl& import, const std::filesystem::path& from_dir,
                               std::vector<StructDecl>& out,
                               std::unordered_set<std::string>& seen);

    std::filesystem::path entry_;
    std::vector<std::filesystem::path> search_paths_;
    std::unordered_map<std::string, Program> cache_;
    std::unordered_map<std::string, bool> loading_;
};

Program loadProgram(const std::filesystem::path& entry,
                    const std::vector<std::filesystem::path>& search_paths = {});
[[nodiscard]] Program cloneProgram(const Program& program);

}  // namespace xlang
