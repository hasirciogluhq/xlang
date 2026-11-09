---
title: Strings
---

The language has no owned string type. It has `str`, a read-only view, and the standard library adds `String`, which owns its bytes. `std/str` holds both: `std/str/str` and `std/str/compare` work on views, `std/str/string` owns text, and `std/str/fmt` does the formatting.

## `str` — the view

A `str` is a pointer and a length, and it never owns its bytes. Its fields are `ptr` (`*ui8`) and `len` (`usize`), both read-only. A string literal takes its type from the place it is written in:

| Context | Type | Bytes live in | Mutable |
|---|---|---|---|
| expected `*ui8` (or an enum over `*ui8`) | `*ui8` | `.rodata`, NUL-terminated | no |
| expected `[ui8; N]` | `[ui8; N]` copy, zero-padded | the array | yes |
| anything else, including no expected type | `str` | `.rodata`, with a trailing NUL not counted in `len` | no |

`local buf: [ui8; 8] = "Jones"` copies the bytes into the array; a literal longer than `N` is `E0717`. A `str` literal's `len` is the number of decoded bytes, so `"Jo\0nes".len` is 6.

`std/str/str` gives the view helpers:

```xlang
eq(a, b) -> bool                  // byte equality; `==` is not defined on str
fromC(p: *ui8) -> str             // view of a NUL-terminated C string
fromParts(p: *ui8, len: usize) -> str
view(s: str, first: usize, n: usize) -> str   // no copy; panics outside the string
find(s: str, needle: str) -> i64  // byte offset, or -1
findLast(s: str, needle: str) -> i64  // byte offset of the last occurrence, or -1
contains, startsWith, endsWith
toU64(s: str, out: *ui64) -> bool
toI64(s: str, out: *i64) -> bool
```

`view` returns a view without copying, and it is valid only while the bytes it views live. `toU64` and `toI64` parse decimal only, return false on overflow or junk, and leave `out` unchanged then.

`std/str/compare` gives `compare(a, b) -> i32` (-1, 0, 1), `equalIgnoreCase`, `containsAny`, `isSpace`, `findByte(s, b) -> i64` and `findLastByte(s, b) -> i64` (the one-byte searches, so a delimiter scan names the character instead of building a one-byte `str`), and `globMatch(pattern, text)` (`*` and `?`, no escapes).

Splitting and fixed-size copies (`std/str/string`) return owned values:

```xl
split(s: str, sep: str) -> Vec<String>          // keeps empty pieces; panics on an empty separator
splitAsciiWhitespace(s: str) -> Vec<String>     // a run of whitespace is one separator, no empty pieces
trim, trimStart, trimEnd, replace, replaceAll, repeat
copyTruncated(s: str, dst: *ui8, cap: usize) -> usize
```

`splitAsciiWhitespace` is the tokenizer for a field list (`"  a b  "` gives `[a, b]`). `copyTruncated` copies at most `cap` bytes and stops before the code point that would be split, so the result is a shorter valid string, never half a character; it writes no terminator and returns the length. Use it where a buffer has a fixed size (an OS field, a fixed-width table).

A `str` converts to `*ui8` only through `.ptr`; `as` cannot convert it. `s[i]` reads one byte and, under `--safety=checked`, panics when `i >= len`. Writing through a `str` (`s[i] = x`, `s.len = n`) is `E0716`.

`view(s, lo, hi)` and the slice operator `s[lo..hi]` do the same job: both return a `str` that starts at byte `lo` with length `hi - lo`, without copying. Prefer the operator in new code. `s[a..]` runs to the end and `s[..b]` starts at 0; both bounds are checked under `--safety=checked`, and an array of bytes can be sliced the same way. A byte literal `b'x'` is a `ui8` constant, so `s[i] == b'x'` needs no `as`.

## `String` — owned text

`String` is an owned, growable byte string that allocates through `currentAllocator` and frees itself when dropped. It needs drop, so it is move-only.

```xlang
from "std" import String, println

@entry fn main() -> i32 {
    local s: String = String("hello")
    s.append(" world")
    println("{}", s.asStr())
    return 0
}
```

| Group | Members |
|---|---|
| Constructors | `init()` (empty), `init(str)`, `init(usize, ui8)` (n copies of a byte) |
| Reading | `length`, `asStr`, `cstr`, `at`, `slice`, `find`, `startsWith`, `endsWith` |
| Changing | `push`, `append(str)`, `append(*String)`, `clone`, `clear`, `reserve` |
| Comparing and case | `equals`, `toLowerAscii`, `toUpperAscii` (both return new `String`s) |

Empty strings do not allocate, and there is no small-string optimization. Appending a view of a `String`'s own buffer is not supported.

The text-returning free functions build a new `String` from a view: `slice(s, first, n)`, `trim`, `trimStart`, `trimEnd`, `split(s, sep) -> Vec<String>` (keeps empty pieces; panics on an empty separator), `replace` (first occurrence), `replaceAll` and `repeat`. Functions that return text return an owned `String`; a `str` view does not keep its bytes alive, so the owner of the bytes must outlive it.

## Printing and formatting

`print` takes `str`, `*ui8`, `i64`, `ui64`, `f64`, `bool` or `*String`, and adds no newline. `println` adds one newline after its text; `printEndl` writes only a newline. The print family writes to its file descriptor without allocating. `fprint` and `fprintln` take the fd first.

With a format string, any number of arguments may follow it (the functions are variadic):

```xlang
println("{} and {}", 1, "two")
```

{% raw %}`{}` takes the next argument and `{{`/`}}` print braces.{% endraw %} A format whose placeholder count differs from its argument count panics before anything is written. `f64` prints with six decimals.

| Function | Result |
|---|---|
| `sprintf(fmt, ..args)` | a new `String` |
| `format(fmt, ..args)` | a new `String` |
| `fmtAppend(out: *String, fmt, ..args)` | appends to an existing `String` |
| `snprintf(buf, cap, fmt, ..args) -> usize` | writes at most `cap - 1` bytes plus a NUL into `buf` and returns the full length |
| `toString(v)` | a new `String` from one `i64`, `ui64`, `f64`, `bool`, `str` or `*ui8` |
