#pragma once

#include "xlang/util/error.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace xlang::pkg {

/// The TOML subset used by `xlang.toml` and `xlang.lock`: tables, arrays of tables, dotted and
/// quoted keys, basic and literal strings, integers, booleans, arrays and inline tables.
/// Floats, dates and multi-line strings are rejected.
struct TomlValue {
    enum class Kind { String, Integer, Bool, Array, Table };
    Kind kind{Kind::Table};
    std::string string;
    std::int64_t integer{0};
    bool boolean{false};
    std::vector<TomlValue> array;
    std::map<std::string, TomlValue> table;
    int line{0};

    [[nodiscard]] bool isTable() const { return kind == Kind::Table; }
    [[nodiscard]] bool isString() const { return kind == Kind::String; }
    [[nodiscard]] bool isArray() const { return kind == Kind::Array; }
    /// Member of a table, or null.
    [[nodiscard]] const TomlValue* get(const std::string& key) const;
    [[nodiscard]] static const char* kindName(Kind k);
};

class TomlError : public XlangError {
public:
    TomlError(int line, const std::string& text)
        : XlangError("line " + std::to_string(line) + ": " + text), line_(line) {}
    [[nodiscard]] int line() const { return line_; }

private:
    int line_;
};

/// Parses a whole document; the result is the root table. Throws `TomlError`.
[[nodiscard]] TomlValue parseToml(std::string_view text);

/// `"..."` with the escapes TOML requires.
[[nodiscard]] std::string tomlQuote(std::string_view s);

}  // namespace xlang::pkg
