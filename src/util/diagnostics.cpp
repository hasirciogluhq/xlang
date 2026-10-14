#include "xlang/util/diagnostics.h"

#include "xlang/util/error.h"

#include <algorithm>
#include <format>
#include <map>
#include <ostream>
#include <tuple>

namespace xlang {

namespace {

constexpr std::size_t kTabWidth = 4;
constexpr std::size_t kMaxLineWidth = 110;
constexpr std::size_t kLeadWidth = 40;

[[nodiscard]] const std::vector<DiagnosticInfo>& registry() {
    static const std::vector<DiagnosticInfo> table = {
        {"E0001", Severity::Error, "lexical error",
         "The source contains a character, literal or escape the lexer cannot read (unknown "
         "character, unterminated string, malformed number, misplaced `#` directive). Fix the "
         "token at the marked position."},
        {"E0002", Severity::Error, "syntax error",
         "The tokens do not form a valid construct at the marked position. The detail line names "
         "what the parser expected. Statements and items end at a line break; an expression may "
         "continue on the next line only inside parentheses/brackets or after a binary operator."},
        {"E0003", Severity::Error, "preprocessor error",
         "A `#error` directive was reached, a conditional-compilation directive is malformed "
         "(missing condition, `#endif`, stray `#else`), or a condition uses an undefined name or "
         "mixes kinds. Use `defined(NAME)` to test for optional definitions."},
        {"E0004", Severity::Error, "macro expansion error",
         "A macro call has the wrong number or kind of arguments, a macro is defined twice, or "
         "expansion is deeper than 64 levels. The labels show the expansion chain."},
        {"W0201", Severity::Warning, "layout of a declared type is unknown, C layout assumed",
         "A `declare`d struct/class/global has no layout record in the linked objects and no entry "
         "in `xl.layout`, so the C layout is assumed. Add `@repr(c)` if the other side is C/C++, "
         "build the other side with xl so a layout record is emitted, or silence it with "
         "`@allow(W0201)` / `--allow=W0201`."},
        {"W0202", Severity::Warning, "declared layout contradicts the recorded layout",
         "The layout written in the `declare` differs from the recorded layout of the same type."},
        {"W0203", Severity::Warning, "layout record uses a different layout scheme",
         "The layout record was produced by another default layout scheme version (for example "
         "v2 vs v3)."},
        {"W0301", Severity::Warning, "declared symbol not resolved in this compilation",
         "The symbol is `declare`d but not defined in this compilation; it will be looked up at "
         "link time."},
        {"E0302", Severity::Error, "type not expressible in C used in `export(c)`",
         "`export(c)` signatures accept only C-compatible types: integers, `f32/f64`, `bool`, "
         "`*T`, `@repr(c)` structs, function pointers and integer-based enums."},
        {"W0401", Severity::Warning, "same entity imported/exported through two paths",
         "Not an error (it is one entity), but two paths may silently conflict later if one of "
         "them defines its own symbol. Import through a single path or use `import * as ns`."},
        {"E0401", Severity::Error, "different entities bound to the same name",
         "Two imports or exports bind the same name to different entities. Rename one with `as`, "
         "use `except`, or import into a namespace with `import * as ns`."},
        {"E0501", Severity::Error, "no registered allocator for `new`",
         "`new T { }` uses the global registered with `register(allocator)`, and none was found. "
         "Write `new(a) T { }` or register an allocator."},
        {"E0502", Severity::Error, "duplicate `register`",
         "A role (or a global name) was registered twice. At most one registration per role is "
         "allowed in the whole program; turn off the stdlib feature that registers it if needed."},
        {"E0503", Severity::Error, "`register` role contract not met",
         "The registered symbol does not fit its role (for example an `allocator` global whose "
         "type is not `Allocator` and has no `impl Allocator for T`, or a `panic` with a wrong "
         "signature)."},
        {"E0504", Severity::Error, "interface used without `impl`",
         "Interfaces are nominal: a type is usable as an interface only after "
         "`impl Interface for Type`, even when its method names match."},
        {"W0402", Severity::Warning, "local name shadows a registered global",
         "A local definition or import hides a `register`ed global name. The role binding is "
         "unaffected because it is bound by registration, not by name."},
        {"E0601", Severity::Error, "implicit signedness-changing conversion",
         "An implicit conversion changes signedness (`ui <-> i`, `ui -> f`; floats count as "
         "signed), or a binary operation mixes signedness. Use `as`, or write the expression "
         "inside an `unsafe { }` block."},
        {"E0701", Severity::Error, "constant array index out of bounds",
         "A constant index is outside `[0, N)`. Checked regardless of `--safety` and cannot be "
         "silenced."},
        {"E0702", Severity::Error, "static assertion failed",
         "A `staticAssert` condition evaluated to false at compile time (or was not a bool "
         "constant)."},
        {"E0703", Severity::Error, "type has no known size",
         "`sizeOf`/layout needs a sized type: `void`, opaque `declare` types, types from modules "
         "that are not loaded, and types that contain themselves by value have none."},
        {"E0704", Severity::Error, "cyclic type alias",
         "A `type` alias refers to itself, directly or through other aliases."},
        {"E0705", Severity::Error, "invalid enum or layout attribute",
         "An enum has a non-constant underlying type, a duplicate member, a missing value in a "
         "non-integer enum or a value of the wrong kind; or `@align`/`@layout` has a bad argument."},
        {"E0706", Severity::Error, "unknown type or name",
         "The name is not declared in this module (or is not of the expected kind)."},
        {"E0707", Severity::Error, "not a compile-time constant",
         "A constant expression (array length, enum value, `staticAssert`, `sizeOf` argument) "
         "uses something that cannot be evaluated at compile time."},
        {"E0708", Severity::Error, "type mismatch",
         "A value does not have the type its context needs and no implicit conversion exists "
         "(`as` is required for narrowing, float to integer, pointer casts and so on)."},
        {"E0709", Severity::Error, "invalid operation",
         "An operator, field access, index, call or assignment does not apply to the operand: "
         "unknown field or method, dereferencing a non-pointer, assigning to a non-place, "
         "`&` of a temporary, arithmetic on pointers, and similar."},
        {"E0710", Severity::Error, "wrong number of arguments",
         "A call has a different number of arguments than the callee declares, or a `syscall` "
         "has more arguments than the target allows."},
        {"E0711", Severity::Error, "module or imported name not found",
         "An `import`/`export ... from` names a module that cannot be located, a path that is both "
         "`x.xl` and `x/index.xl`, or a name the module does not export."},
        {"E0712", Severity::Error, "invalid `virtual`/`override` use",
         "A method marked `override` replaces nothing, replaces a method with a different "
         "signature, re-declares an already virtual method without `override`, or hides a "
         "virtual base method with a plain method."},
        {"E0713", Severity::Error, "`impl` does not match its interface",
         "An `impl Interface for Type` lacks a method of the interface, defines a method the "
         "interface does not have, or a method has a different signature."},
        {"E0714", Severity::Error, "code generation failed",
         "The back end could not lower the program: an unresolved external reference, a syscall "
         "form the target does not support, or a generated module that failed LLVM verification "
         "(an internal error)."},
        {"W0601", Severity::Warning, "implicit signed integer to float conversion",
         "A signed integer is implicitly converted to `f32`/`f64` and precision may be lost. "
         "Write `as f64`, or silence with `@allow(W0601)` / `--allow=W0601`."},
    };
    return table;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(c));
            } else {
                out += c;
            }
        }
    }
    return out;
}

[[nodiscard]] std::size_t digits(std::size_t value) {
    std::size_t n = 1;
    while (value >= 10) {
        value /= 10;
        ++n;
    }
    return n;
}

/// One display column per cell: a code point, or one space of an expanded tab.
struct DisplayLine {
    std::vector<std::string> cells;
    std::vector<std::size_t> byte_to_cell;  ///< size = bytes + 1

    [[nodiscard]] std::size_t cellOf(std::size_t byte_index) const {
        return byte_to_cell[std::min(byte_index, byte_to_cell.size() - 1)];
    }
};

[[nodiscard]] DisplayLine expand(std::string_view line) {
    DisplayLine out;
    out.byte_to_cell.assign(line.size() + 1, 0);
    for (std::size_t i = 0; i < line.size();) {
        const auto lead = static_cast<unsigned char>(line[i]);
        std::size_t len = 1;
        if (lead >= 0xF0) {
            len = 4;
        } else if (lead >= 0xE0) {
            len = 3;
        } else if (lead >= 0xC0) {
            len = 2;
        }
        len = std::min(len, line.size() - i);
        for (std::size_t k = 0; k < len; ++k) {
            out.byte_to_cell[i + k] = out.cells.size();
        }
        if (line[i] == '\t') {
            for (std::size_t k = 0; k < kTabWidth; ++k) {
                out.cells.emplace_back(" ");
            }
        } else {
            out.cells.emplace_back(line.substr(i, len));
        }
        i += len;
    }
    out.byte_to_cell[line.size()] = out.cells.size();
    return out;
}

struct Mark {
    const Label* label{nullptr};
    std::size_t first_cell{0};  ///< display columns on this line
    std::size_t last_cell{0};   ///< exclusive
};

struct Shown {
    std::string text;
    std::ptrdiff_t shift{0};  ///< display column of a cell in `text` = cell + shift
    std::size_t width{0};
};

[[nodiscard]] Shown show(const DisplayLine& line, std::size_t anchor) {
    Shown out;
    std::size_t begin = 0;
    std::size_t end = line.cells.size();
    bool lead_cut = false;
    bool tail_cut = false;
    if (line.cells.size() > kMaxLineWidth) {
        if (anchor > kLeadWidth) {
            begin = std::min(anchor - kLeadWidth, line.cells.size() - kMaxLineWidth);
        }
        end = begin + kMaxLineWidth;
        lead_cut = begin > 0;
        tail_cut = end < line.cells.size();
    }
    if (lead_cut) {
        out.text += "\xE2\x80\xA6";
    }
    for (std::size_t i = begin; i < end; ++i) {
        out.text += line.cells[i];
    }
    if (tail_cut) {
        out.text += "\xE2\x80\xA6";
    }
    out.shift = static_cast<std::ptrdiff_t>(lead_cut ? 1 : 0) - static_cast<std::ptrdiff_t>(begin);
    out.width = (end - begin) + (lead_cut ? 1 : 0) + (tail_cut ? 1 : 0);
    return out;
}

void appendBlock(std::string& out, std::string_view tag, const std::string& text) {
    if (text.empty()) {
        return;
    }
    out += tag;
    for (const char c : text) {
        out += c;
        if (c == '\n') {
            out += "        ";
        }
    }
    out += '\n';
}

}  // namespace

const DiagnosticInfo* findDiagnosticInfo(std::string_view code) {
    for (const DiagnosticInfo& info : registry()) {
        if (info.code == code) {
            return &info;
        }
    }
    return nullptr;
}

const std::vector<DiagnosticInfo>& allDiagnosticInfos() {
    return registry();
}

DiagnosticEngine::DiagnosticEngine(const SourceManager& sources, std::ostream& out)
    : sources_(sources), out_(out) {}

void DiagnosticEngine::configure(const DiagnosticOptions& options) {
    options_ = options;
    allow_.clear();
    deny_.clear();
    for (const std::string& code : options.allow) {
        const DiagnosticInfo* info = findDiagnosticInfo(code);
        if (info == nullptr) {
            throw XlangError(std::format("unknown diagnostic code `{}`", code));
        }
        if (info->severity == Severity::Error) {
            throw XlangError(std::format("`{}` is an error and cannot be allowed", code));
        }
        allow_.insert(code);
    }
    for (const std::string& code : options.deny) {
        if (findDiagnosticInfo(code) == nullptr) {
            throw XlangError(std::format("unknown diagnostic code `{}`", code));
        }
        deny_.insert(code);
    }
    if (!options.log_path.empty()) {
        log_.emplace(options.log_path, std::ios::trunc);
        if (!*log_) {
            throw XlangError(std::format("cannot open diagnostic log `{}`", options.log_path));
        }
    }
}

void DiagnosticEngine::addAllowScope(const Span& scope, const std::vector<std::string>& codes) {
    AllowScope entry;
    entry.scope = scope;
    entry.codes.insert(codes.begin(), codes.end());
    scopes_.push_back(std::move(entry));
}

bool DiagnosticEngine::isAllowed(const Diagnostic& diagnostic) const {
    if (diagnostic.severity != Severity::Warning || options_.warn_all) {
        return false;
    }
    if (options_.allow_all || allow_.contains(diagnostic.code)) {
        return true;
    }
    if (diagnostic.labels.empty()) {
        return false;
    }
    const Span& primary = diagnostic.labels.front().span;
    return std::ranges::any_of(scopes_, [&](const AllowScope& scope) {
        return scope.codes.contains(diagnostic.code) && scope.scope.contains(primary);
    });
}

void DiagnosticEngine::report(Diagnostic diagnostic) {
    if (const DiagnosticInfo* info = findDiagnosticInfo(diagnostic.code)) {
        diagnostic.severity = info->severity;
    }
    diagnostic.suppressed = isAllowed(diagnostic);
    if (!diagnostic.suppressed && diagnostic.severity == Severity::Warning &&
        deny_.contains(diagnostic.code)) {
        diagnostic.severity = Severity::Error;
    }

    if (!diagnostic.suppressed) {
        if (diagnostic.severity == Severity::Error) {
            ++errors_;
        } else {
            ++warnings_;
        }
    }
    pending_.push_back(std::move(diagnostic));
}

void DiagnosticEngine::flush() {
    std::vector<Diagnostic> queue = std::move(pending_);
    pending_.clear();
    const auto key = [](const Diagnostic& d) {
        if (d.labels.empty() || !d.labels.front().span.valid()) {
            return std::tuple<FileId, std::uint32_t, std::uint32_t>(kNoFile, 0, 0);
        }
        const Span& s = d.labels.front().span;
        return std::tuple<FileId, std::uint32_t, std::uint32_t>(s.file, s.line, s.column);
    };
    std::ranges::stable_sort(queue, [&](const Diagnostic& a, const Diagnostic& b) {
        return key(a) < key(b);
    });

    for (const Diagnostic& diagnostic : queue) {
        const std::string text = options_.format == DiagFormat::Json ? renderJson(diagnostic)
                                                                     : renderText(diagnostic);
        if (!diagnostic.suppressed) {
            out_ << text << "\n";
        }
        if (!log_) {
            continue;
        }
        // `--diag-log-level`: all/warn keep warnings (suppressed included) and errors.
        if (options_.log_level == DiagLogLevel::Error && diagnostic.severity != Severity::Error) {
            continue;
        }
        *log_ << text << "\n";
    }
    if (log_) {
        log_->flush();
    }
    out_.flush();
}

void DiagnosticEngine::error(std::string code, const Span& span, std::string message,
                             std::string label) {
    Diagnostic d;
    d.code = std::move(code);
    d.severity = Severity::Error;
    d.message = std::move(message);
    if (span.valid()) {
        d.labels.push_back({span, std::move(label)});
    }
    report(std::move(d));
}

void DiagnosticEngine::warning(std::string code, const Span& span, std::string message,
                               std::string label) {
    Diagnostic d;
    d.code = std::move(code);
    d.severity = Severity::Warning;
    d.message = std::move(message);
    if (span.valid()) {
        d.labels.push_back({span, std::move(label)});
    }
    report(std::move(d));
}

std::string DiagnosticEngine::renderText(const Diagnostic& diagnostic) const {
    std::string out;
    out += std::format("{}[{}]{}: {}\n", diagnostic.severity == Severity::Error ? "error" : "warning",
                       diagnostic.code, diagnostic.suppressed ? " (suppressed)" : "",
                       diagnostic.message);

    const std::size_t context =
        diagnostic.severity == Severity::Error ? options_.context_errors : options_.context_warnings;

    // Group labels by file, primary file first.
    std::vector<FileId> file_order;
    std::map<FileId, std::vector<const Label*>> by_file;
    for (const Label& label : diagnostic.labels) {
        if (!label.span.valid()) {
            continue;
        }
        if (!by_file.contains(label.span.file)) {
            file_order.push_back(label.span.file);
        }
        by_file[label.span.file].push_back(&label);
    }

    // Gutter width: largest line number that can be displayed.
    std::size_t max_line = 1;
    for (const auto& [file, labels] : by_file) {
        const std::size_t count = sources_.file(file).lineCount();
        for (const Label* label : labels) {
            max_line = std::max<std::size_t>(max_line,
                                             std::min<std::size_t>(count, label->span.end_line + context));
        }
    }
    const std::size_t width = digits(max_line);
    const std::string blank_gutter(width, ' ');

    bool first_group = true;
    for (const FileId file : file_order) {
        const SourceFile& source = sources_.file(file);
        const std::vector<const Label*>& labels = by_file[file];
        const Span& head = labels.front()->span;

        out += std::format("{}{} {}:{}:{}\n", blank_gutter, first_group ? "-->" : ":::",
                           source.path().string(), head.line, head.column);
        first_group = false;
        out += std::format("{} |\n", blank_gutter);

        // Marks per line (start line and, for multi-line spans, end line).
        std::map<std::size_t, std::vector<Mark>> marks;
        std::map<std::size_t, DisplayLine> displays;
        const auto display = [&](std::size_t line) -> const DisplayLine& {
            auto it = displays.find(line);
            if (it == displays.end()) {
                it = displays.emplace(line, expand(source.line(line))).first;
            }
            return it->second;
        };
        for (const Label* label : labels) {
            const Span& s = label->span;
            const DisplayLine& start_line = display(s.line);
            Mark start;
            start.label = label;
            start.first_cell = start_line.cellOf(s.column - 1);
            if (s.end_line == s.line) {
                start.last_cell = std::max(start.first_cell + 1, start_line.cellOf(s.end_column - 1));
            } else {
                start.last_cell = std::max(start.first_cell + 1, start_line.cells.size());
            }
            marks[s.line].push_back(start);
            if (s.end_line != s.line) {
                const DisplayLine& end_line = display(s.end_line);
                Mark end;
                end.label = label;
                end.first_cell = end_line.cellOf(s.end_column > 1 ? s.end_column - 2 : 0);
                end.last_cell = end.first_cell + 1;
                marks[s.end_line].push_back(end);
            }
        }

        // Context ranges, merged when they touch or overlap.
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        for (const auto& [line, line_marks] : marks) {
            (void)line_marks;
            const std::size_t lo = line > context ? line - context : 1;
            const std::size_t hi = std::min<std::size_t>(source.lineCount(), line + context);
            if (!ranges.empty() && lo <= ranges.back().second + 1) {
                ranges.back().second = std::max(ranges.back().second, hi);
            } else {
                ranges.emplace_back(lo, hi);
            }
        }

        // Render every line first to align the `<=======` column across the file block.
        struct Row {
            std::size_t number;
            Shown shown;
            const std::vector<Mark>* line_marks;
        };
        std::vector<std::vector<Row>> blocks;
        std::size_t marker_column = 0;
        for (const auto& [lo, hi] : ranges) {
            std::vector<Row> rows;
            for (std::size_t line = lo; line <= hi; ++line) {
                Row row;
                row.number = line;
                const auto found = marks.find(line);
                row.line_marks = found != marks.end() ? &found->second : nullptr;
                const DisplayLine& dl = display(line);
                row.shown = show(dl, row.line_marks ? row.line_marks->front().first_cell : 0);
                if (row.line_marks != nullptr) {
                    marker_column = std::max(marker_column, row.shown.width);
                }
                rows.push_back(std::move(row));
            }
            blocks.push_back(std::move(rows));
        }

        bool first_block = true;
        for (const std::vector<Row>& rows : blocks) {
            if (!first_block) {
                out += std::format("{}...\n", blank_gutter);
            }
            first_block = false;
            for (const Row& row : rows) {
                std::string line_out =
                    std::format("{:>{}} | {}", row.number, width, row.shown.text);
                if (row.line_marks != nullptr) {
                    line_out.append(marker_column - row.shown.width + 4, ' ');
                    line_out += "<=======";
                    const std::string& text = row.line_marks->front().label->text;
                    if (!text.empty()) {
                        line_out += " " + text;
                    }
                }
                out += line_out;
                out += '\n';
                if (row.line_marks != nullptr && row.line_marks->size() > 1) {
                    for (const Mark& mark : *row.line_marks) {
                        const std::ptrdiff_t start =
                            static_cast<std::ptrdiff_t>(mark.first_cell) + row.shown.shift;
                        std::string carets = std::format("{} | ", blank_gutter);
                        carets.append(static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, start)), ' ');
                        carets.append(std::max<std::size_t>(1, mark.last_cell - mark.first_cell), '^');
                        if (!mark.label->text.empty()) {
                            carets += " " + mark.label->text;
                        }
                        out += carets;
                        out += '\n';
                    }
                }
            }
        }
        out += std::format("{} |\n", blank_gutter);
    }

    appendBlock(out, "detail: ", diagnostic.detail);
    appendBlock(out, "reason: ", diagnostic.reason);
    appendBlock(out, "fix:    ", diagnostic.fix);
    if (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

std::string DiagnosticEngine::renderJson(const Diagnostic& diagnostic) const {
    std::string out = "{";
    out += std::format("\"code\":\"{}\",\"severity\":\"{}\",\"suppressed\":{},\"message\":\"{}\"",
                       jsonEscape(diagnostic.code),
                       diagnostic.severity == Severity::Error ? "error" : "warning",
                       diagnostic.suppressed ? "true" : "false", jsonEscape(diagnostic.message));
    out += ",\"labels\":[";
    bool first = true;
    for (const Label& label : diagnostic.labels) {
        if (!label.span.valid()) {
            continue;
        }
        if (!first) {
            out += ",";
        }
        first = false;
        out += std::format(
            "{{\"file\":\"{}\",\"line\":{},\"column\":{},\"endLine\":{},\"endColumn\":{},"
            "\"text\":\"{}\"}}",
            jsonEscape(sources_.file(label.span.file).path().string()), label.span.line,
            label.span.column, label.span.end_line, label.span.end_column, jsonEscape(label.text));
    }
    out += "]";
    out += std::format(",\"detail\":\"{}\",\"reason\":\"{}\",\"fix\":\"{}\"}}",
                       jsonEscape(diagnostic.detail), jsonEscape(diagnostic.reason),
                       jsonEscape(diagnostic.fix));
    return out;
}

std::string DiagnosticEngine::explain(std::string_view code) {
    const DiagnosticInfo* info = findDiagnosticInfo(code);
    if (info == nullptr) {
        return {};
    }
    return std::format("{} ({}): {}\n\n{}\n", info->code,
                       info->severity == Severity::Error ? "error" : "warning", info->title,
                       info->explanation);
}

}  // namespace xlang
