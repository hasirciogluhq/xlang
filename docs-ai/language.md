# language

Source files use the extension xl and declarations end at a line break without semicolons.
Types are i8 to i64, ui8 to ui64, isize, usize, f32, f64, bool, void, pointers, function pointers and fixed arrays.
There is no string type, no character literal, no generics, no tuples, no overloading and no variadic functions.
A string literal has type pointer to ui8 and is NUL-terminated.
Functions are written fn name(a: T) -> R, locals with local name: T = value, and globals with global name: T = constant.
Implicit conversions are same-signedness integer widening only, and every other conversion needs as.
Struct holds data, class has methods with single inheritance and virtual and override, and interface is nominal and needs an impl block.
Imports use from "m" import x or import x from "m" or import * from "m", and export from re-exports with optional except.
The global namespace holds only the closed builtins and register names, and everything else comes from the stdlib by import.
Conditional compilation uses hash if directives, macros are called with a bang, and diagnostics have E and W codes.
