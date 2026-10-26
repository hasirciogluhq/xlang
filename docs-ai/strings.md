# strings

A string literal takes its type from the place it is written in, as a pointer to ui8, as a zero-padded ui8 array copy, or otherwise as a read-only str.
A str is a view of a pointer and a length, and it never owns its bytes.
The std str module compares and converts str values with strEq, strFromC and strFromParts.
String is an owned, growable byte string in the stdlib that allocates through the registered allocator and frees itself when dropped.
String has the methods length, asStr, cstr, at, push, append, equals, clone, clear and reserve.
Empty strings do not allocate, and appending a view of a String's own buffer is not supported.
print takes str, *ui8, i64, ui64, f64, bool or *String, and adds no newline.
print with a format string replaces each placeholder with one argument, up to six arguments, and double braces print a brace.
A format whose placeholder count differs from its argument count panics before that call prints anything.
