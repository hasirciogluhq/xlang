# Strings

A string literal takes its type from the place it is written in, as a pointer to ui8, as a zero-padded ui8 array copy, or otherwise as a read-only str.
A str is a view of a pointer and a length, and it never owns its bytes.
The std str module compares and converts str values with eq, fromC and fromParts, and finds and parses them with find, contains, startsWith, endsWith, toI64 and toU64.
Functions that return text return an owned String, so trim, slice, split and replace give new Strings.
The view function gives a str without copying, and it is valid only while the bytes it views live.
The compare module gives three-way order, ASCII case-insensitive equality and shell-style glob matching.
String is an owned, growable byte string in the stdlib that allocates through currentAllocator and frees itself when dropped.
String has the methods length, asStr, cstr, at, slice, find, startsWith, endsWith, push, append, equals, clone, clear, reserve, toLowerAscii and toUpperAscii, and its init takes nothing, a str, or a count and a byte.
Empty strings do not allocate, and appending a view of a String's own buffer is not supported.
print takes str, *ui8, i64, ui64, f64, bool or *String, and adds no newline.
println adds one newline after its text, printEndl writes only a newline, and the print family writes to its file descriptor without allocating.
A format with a placeholder count that differs from its argument count panics before anything is written.
sprintf and format return a new String, fmtAppend appends to an existing String, and toString converts one value.
snprintf writes into a caller buffer, stops before the cap, ends with a NUL, and returns the full length.
