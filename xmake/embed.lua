-- Generate embedded_runtime.cpp / embedded_libs.cpp from runtime/ and libs/

function _symbolize(rel)
    return rel:gsub("%.", "_"):gsub("/", "_"):gsub("\\", "_")
end

function _sorted_xlang_files(dir)
    local files = os.files(path.join(dir, "**.xlang"))
    table.sort(files)
    return files
end

function generate_runtime(runtime_dir, output_file)
    local files = _sorted_xlang_files(runtime_dir)
    local body = {}
    local array_init = {}

    for _, file in ipairs(files) do
        local rel = path.relative(file, runtime_dir):gsub("\\", "/")
        local symbol = _symbolize(rel)
        local content = io.readfile(file)
        table.insert(body, string.format(
            "constexpr const char kEmbeddedRuntime_%s[] = R\"XLANG_RT(%s)XLANG_RT\";\n",
            symbol, content))
        table.insert(array_init, string.format(
            "    {\"%s\", kEmbeddedRuntime_%s},", rel, symbol))
    end

    local main_content = io.readfile(path.join(runtime_dir, "runtime.xlang"))
    local out = string.format([[
#include "xlang/embedded_runtime.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace xlang {
namespace {

struct EmbeddedRuntimeFile {
    const char* name;
    const char* source;
};

%s

const EmbeddedRuntimeFile kEmbeddedRuntimeFiles[] = {
%s
};

std::size_t embeddedRuntimeFileCount() {
    return sizeof(kEmbeddedRuntimeFiles) / sizeof(kEmbeddedRuntimeFiles[0]);
}

}  // namespace

const char kEmbeddedRuntimeSource[] = R"XLANG_RT(%s)XLANG_RT";

std::filesystem::path materializeEmbeddedRuntime(const std::filesystem::path& work_dir) {
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    for (std::size_t i = 0; i < embeddedRuntimeFileCount(); ++i) {
        const EmbeddedRuntimeFile& file = kEmbeddedRuntimeFiles[i];
        const std::filesystem::path path = work_dir / file.name;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("failed to write embedded runtime file: " + path.string());
        }
        out << file.source;
    }
    return work_dir;
}

}  // namespace xlang
]], table.concat(body, "\n"), table.concat(array_init, "\n"), main_content)

    os.mkdir(path.directory(output_file))
    io.writefile(output_file, out)
end

function generate_libs(libs_dir, output_file)
    local files = _sorted_xlang_files(libs_dir)
    local body = {}
    local array_init = {}

    for _, file in ipairs(files) do
        local rel = path.relative(file, libs_dir):gsub("\\", "/")
        local symbol = _symbolize(rel)
        local content = io.readfile(file)
        table.insert(body, string.format(
            "constexpr const char kEmbeddedLibs_%s[] = R\"XLANG_LIB(%s)XLANG_LIB\";\n",
            symbol, content))
        table.insert(array_init, string.format(
            "    {\"%s\", kEmbeddedLibs_%s},", rel, symbol))
    end

    local out = string.format([[
#include "xlang/embedded_libs.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace xlang {
namespace {

struct EmbeddedLibsFile {
    const char* name;
    const char* source;
};

%s

const EmbeddedLibsFile kEmbeddedLibsFiles[] = {
%s
};

std::size_t embeddedLibsFileCount() {
    return sizeof(kEmbeddedLibsFiles) / sizeof(kEmbeddedLibsFiles[0]);
}

}  // namespace

std::filesystem::path materializeEmbeddedLibs(const std::filesystem::path& work_dir) {
    std::error_code ec;
    std::filesystem::create_directories(work_dir, ec);
    for (std::size_t i = 0; i < embeddedLibsFileCount(); ++i) {
        const EmbeddedLibsFile& file = kEmbeddedLibsFiles[i];
        const std::filesystem::path path = work_dir / file.name;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("failed to write embedded libs file: " + path.string());
        }
        out << file.source;
    }
    return work_dir;
}

}  // namespace xlang
]], table.concat(body, "\n"), table.concat(array_init, "\n"))

    os.mkdir(path.directory(output_file))
    io.writefile(output_file, out)
end
