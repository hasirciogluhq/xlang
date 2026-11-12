#include "xlang/pkg/toml.h"

#include <cctype>
#include <set>

namespace xlang::pkg {

const TomlValue* TomlValue::get(const std::string& key) const {
    const auto it = table.find(key);
    return it == table.end() ? nullptr : &it->second;
}

const char* TomlValue::kindName(Kind k) {
    switch (k) {
        case Kind::String: return "a string";
        case Kind::Integer: return "an integer";
        case Kind::Bool: return "a boolean";
        case Kind::Array: return "an array";
        case Kind::Table: return "a table";
    }
    return "a value";
}

std::string tomlQuote(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    TomlValue run() {
        TomlValue root;
        TomlValue* current = &root;
        std::set<std::string> defined;  // explicitly written `[table]` headers
        for (;;) {
            skipBlank();
            if (eof()) {
                break;
            }
            if (peek() == '[') {
                current = header(root, defined);
            } else {
                assignment(*current);
            }
        }
        return root;
    }

private:
    std::string_view s_;
    std::size_t i_{0};
    int line_{1};

    [[nodiscard]] bool eof() const { return i_ >= s_.size(); }
    [[nodiscard]] char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    [[noreturn]] void fail(const std::string& what) const { throw TomlError(line_, what); }

    void skipSpaces() {
        while (!eof() && (peek() == ' ' || peek() == '\t')) {
            ++i_;
        }
    }
    void skipComment() {
        if (peek() == '#') {
            while (!eof() && peek() != '\n') {
                ++i_;
            }
        }
    }
    /// Whitespace, comments and newlines.
    void skipBlank() {
        for (;;) {
            skipSpaces();
            skipComment();
            if (peek() == '\r') {
                ++i_;
            } else if (peek() == '\n') {
                ++i_;
                ++line_;
            } else {
                return;
            }
        }
    }
    void endOfLine() {
        skipSpaces();
        skipComment();
        if (peek() == '\r') {
            ++i_;
        }
        if (eof()) {
            return;
        }
        if (peek() != '\n') {
            fail("expected the end of the line");
        }
        ++i_;
        ++line_;
    }

    std::string bareOrQuotedKey() {
        skipSpaces();
        if (peek() == '"' || peek() == '\'') {
            return stringValue();
        }
        std::string key;
        while (!eof() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '-')) {
            key += peek();
            ++i_;
        }
        if (key.empty()) {
            fail("expected a key");
        }
        return key;
    }
    std::vector<std::string> keyPath() {
        std::vector<std::string> path{bareOrQuotedKey()};
        for (;;) {
            skipSpaces();
            if (peek() != '.') {
                return path;
            }
            ++i_;
            path.push_back(bareOrQuotedKey());
        }
    }

    /// Descends through `path`, creating tables; arrays of tables continue in their last element.
    TomlValue& descend(TomlValue& from, const std::vector<std::string>& path, std::size_t count) {
        TomlValue* t = &from;
        for (std::size_t k = 0; k < count; ++k) {
            TomlValue& next = t->table[path[k]];
            if (next.line == 0) {
                next.kind = TomlValue::Kind::Table;
                next.line = line_;
            }
            if (next.kind == TomlValue::Kind::Array) {
                if (next.array.empty() || !next.array.back().isTable()) {
                    fail("`" + path[k] + "` is not a table");
                }
                t = &next.array.back();
            } else if (next.kind == TomlValue::Kind::Table) {
                t = &next;
            } else {
                fail("`" + path[k] + "` is not a table");
            }
        }
        return *t;
    }

    TomlValue* header(TomlValue& root, std::set<std::string>& defined) {
        ++i_;  // [
        const bool array = peek() == '[';
        if (array) {
            ++i_;
        }
        const std::vector<std::string> path = keyPath();
        skipSpaces();
        if (peek() != ']') {
            fail("expected `]`");
        }
        ++i_;
        if (array) {
            if (peek() != ']') {
                fail("expected `]]`");
            }
            ++i_;
        }
        const int header_line = line_;
        endOfLine();
        TomlValue& parent = descend(root, path, path.size() - 1);
        const std::string& last = path.back();
        TomlValue* result = nullptr;
        if (array) {
            TomlValue& slot = parent.table[last];
            if (slot.line == 0) {
                slot.kind = TomlValue::Kind::Array;
                slot.line = header_line;
            }
            if (slot.kind != TomlValue::Kind::Array) {
                fail("`" + last + "` is not an array of tables");
            }
            slot.array.emplace_back();
            slot.array.back().kind = TomlValue::Kind::Table;
            slot.array.back().line = header_line;
            result = &slot.array.back();
        } else {
            std::string id;
            for (const std::string& p : path) {
                id += p + '\x1f';
            }
            if (!defined.insert(id).second) {
                fail("table `" + last + "` is defined twice");
            }
            TomlValue& slot = parent.table[last];
            if (slot.line == 0) {
                slot.kind = TomlValue::Kind::Table;
                slot.line = header_line;
            }
            if (slot.kind != TomlValue::Kind::Table) {
                fail("`" + last + "` is not a table");
            }
            result = &slot;
        }
        return result;
    }

    void assignment(TomlValue& table) {
        const std::vector<std::string> path = keyPath();
        skipSpaces();
        if (peek() != '=') {
            fail("expected `=` after the key");
        }
        ++i_;
        skipSpaces();
        TomlValue v = value();
        TomlValue& parent = descend(table, path, path.size() - 1);
        if (parent.kind != TomlValue::Kind::Table) {
            fail("`" + path.back() + "` is not inside a table");
        }
        if (parent.table.contains(path.back())) {
            fail("key `" + path.back() + "` is defined twice");
        }
        parent.table.emplace(path.back(), std::move(v));
        endOfLine();
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string stringValue() {
        const char quote = peek();
        ++i_;
        if (s_.substr(i_, 2) == std::string(2, quote)) {
            fail("multi-line strings are not supported");
        }
        std::string out;
        for (;;) {
            if (eof() || peek() == '\n') {
                fail("unterminated string");
            }
            const char c = peek();
            ++i_;
            if (c == quote) {
                return out;
            }
            if (c != '\\' || quote == '\'') {
                out += c;
                continue;
            }
            const char e = peek();
            ++i_;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = peek();
                        ++i_;
                        if (!std::isxdigit(static_cast<unsigned char>(h))) {
                            fail("bad \\u escape");
                        }
                        cp = cp * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : std::tolower(h) - 'a' + 10);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail(std::string("unknown escape `\\") + e + "`");
            }
        }
    }

    TomlValue value() {
        TomlValue v;
        v.line = line_;
        const char c = peek();
        if (c == '"' || c == '\'') {
            v.kind = TomlValue::Kind::String;
            v.string = stringValue();
        } else if (c == '[') {
            ++i_;
            v.kind = TomlValue::Kind::Array;
            for (;;) {
                skipBlank();
                if (peek() == ']') {
                    ++i_;
                    break;
                }
                v.array.push_back(value());
                skipBlank();
                if (peek() == ',') {
                    ++i_;
                } else if (peek() != ']') {
                    fail("expected `,` or `]` in the array");
                }
            }
        } else if (c == '{') {
            ++i_;
            v.kind = TomlValue::Kind::Table;
            for (;;) {
                skipSpaces();
                if (peek() == '}') {
                    ++i_;
                    break;
                }
                const std::vector<std::string> path = keyPath();
                skipSpaces();
                if (peek() != '=') {
                    fail("expected `=` after the key");
                }
                ++i_;
                skipSpaces();
                TomlValue item = value();
                TomlValue& parent = descend(v, path, path.size() - 1);
                if (parent.table.contains(path.back())) {
                    fail("key `" + path.back() + "` is defined twice");
                }
                parent.table.emplace(path.back(), std::move(item));
                skipSpaces();
                if (peek() == ',') {
                    ++i_;
                } else if (peek() != '}') {
                    fail("expected `,` or `}` in the inline table");
                }
            }
        } else if (s_.substr(i_, 4) == "true") {
            i_ += 4;
            v.kind = TomlValue::Kind::Bool;
            v.boolean = true;
        } else if (s_.substr(i_, 5) == "false") {
            i_ += 5;
            v.kind = TomlValue::Kind::Bool;
        } else if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+') {
            std::string digits;
            while (!eof() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '-' || peek() == '+' || peek() == '.' || peek() == ':')) {
                if (peek() != '_') {
                    digits += peek();
                }
                ++i_;
            }
            std::size_t used = 0;
            try {
                v.integer = std::stoll(digits, &used, 10);
            } catch (...) {
                used = 0;
            }
            if (used != digits.size()) {
                fail("unsupported value `" + digits + "` (only integers are supported as numbers)");
            }
            v.kind = TomlValue::Kind::Integer;
        } else {
            fail("expected a value");
        }
        return v;
    }
};

}  // namespace

TomlValue parseToml(std::string_view text) {
    return Parser(text).run();
}

}  // namespace xlang::pkg
