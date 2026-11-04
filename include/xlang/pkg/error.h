#pragma once

#include "xlang/util/error.h"

#include <string>
#include <utility>

namespace xlang::pkg {

/// A package-system failure with a diagnostic code (`E0740`..`E0746`) and a fix hint. The message
/// of the exception is `CODE: text`.
class PackageError : public XlangError {
public:
    PackageError(std::string code, const std::string& text, std::string fix = {})
        : XlangError(code + ": " + text), code_(std::move(code)), text_(text), fix_(std::move(fix)) {}

    [[nodiscard]] const std::string& code() const { return code_; }
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] const std::string& fix() const { return fix_; }

private:
    std::string code_;
    std::string text_;
    std::string fix_;
};

}  // namespace xlang::pkg
