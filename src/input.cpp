#include "xlang/input.h"

#include "xlang/error.h"

#include <algorithm>

namespace xlang {

namespace {

std::string extensionLower(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace

InputKind detectInputKind(const std::filesystem::path& path) {
    const std::string ext = extensionLower(path);
    if (ext == ".xlang") {
        return InputKind::Xlang;
    }
    if (ext == ".o") {
        return InputKind::Object;
    }
    if (ext == ".ll") {
        return InputKind::LlvmIr;
    }
    throw XlangError("unsupported input extension `" + ext +
                     "` (expected .xlang, .o, or .ll)");
}

BuildKind defaultBuildKind(const InputKind input_kind) {
    if (input_kind == InputKind::Object || input_kind == InputKind::LlvmIr) {
        return BuildKind::Exe;
    }
    return BuildKind::Exe;
}

std::filesystem::path defaultOutputPath(const std::filesystem::path& input,
                                        const InputKind input_kind, const BuildKind build_kind) {
    const std::filesystem::path parent =
        input.has_parent_path() ? input.parent_path() : std::filesystem::path(".");
    const std::string stem = input.stem().string();

    if (build_kind == BuildKind::Lib) {
        if (input_kind == InputKind::Object) {
            return parent / input.filename();
        }
        if (input_kind == InputKind::LlvmIr) {
            return parent / (stem + ".o");
        }
        return parent / (stem + ".o");
    }

    if (input_kind == InputKind::Object) {
        return parent / stem;
    }
    if (input_kind == InputKind::LlvmIr) {
        return parent / stem;
    }
    return parent / stem;
}

ResolvedBuildInputs resolveBuildInputs(const std::vector<std::filesystem::path>& inputs) {
    if (inputs.empty()) {
        throw XlangError("build requires at least one input file");
    }

    std::vector<std::filesystem::path> xlang_inputs;
    std::vector<std::filesystem::path> object_inputs;
    std::vector<std::filesystem::path> llvm_inputs;

    for (const std::filesystem::path& input : inputs) {
        switch (detectInputKind(input)) {
            case InputKind::Xlang:
                xlang_inputs.push_back(input);
                break;
            case InputKind::Object:
                object_inputs.push_back(input);
                break;
            case InputKind::LlvmIr:
                llvm_inputs.push_back(input);
                break;
        }
    }

    if (xlang_inputs.size() > 1) {
        throw XlangError("build accepts only one .xlang source per invocation");
    }
    if (llvm_inputs.size() > 1) {
        throw XlangError("build accepts only one .ll source per invocation");
    }
    if (!xlang_inputs.empty() && !llvm_inputs.empty()) {
        throw XlangError("cannot mix .xlang and .ll sources in one build");
    }

    ResolvedBuildInputs resolved;

    if (!xlang_inputs.empty()) {
        resolved.primary = xlang_inputs.front();
        resolved.primary_kind = InputKind::Xlang;
        resolved.link_objects = object_inputs;
        return resolved;
    }

    if (!llvm_inputs.empty()) {
        resolved.primary = llvm_inputs.front();
        resolved.primary_kind = InputKind::LlvmIr;
        resolved.link_objects = object_inputs;
        return resolved;
    }

    if (object_inputs.empty()) {
        throw XlangError("no valid build inputs");
    }

    resolved.primary = object_inputs.front();
    resolved.primary_kind = InputKind::Object;
    if (object_inputs.size() > 1) {
        resolved.link_objects.assign(object_inputs.begin() + 1, object_inputs.end());
    }
    return resolved;
}

}  // namespace xlang
