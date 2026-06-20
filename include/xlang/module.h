#pragma once

#include "xlang/ast.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace xlang {

class ModuleLoader {
public:
    explicit ModuleLoader(std::filesystem::path entry);

    [[nodiscard]] Program load();

private:
    [[nodiscard]] Program loadFile(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path resolveModule(const std::filesystem::path& from,
                                                      const std::string& name) const;
    void mergeAll(Program& into, const Program& from);
    void mergePrefixed(Program& into, const Program& from, const std::string& alias);
    void mergeSelected(Program& into, const Program& from, const ImportDecl& import);

    std::filesystem::path entry_;
    std::unordered_map<std::string, Program> cache_;
    std::unordered_map<std::string, bool> loading_;
};

Program loadProgram(const std::filesystem::path& entry);

}  // namespace xlang
