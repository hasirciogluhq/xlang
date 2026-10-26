# language

Source files use the extension xl, declarations end at a line break without semicolons, and conditional compilation uses hash if directives.
Types are i8 to i64, ui8 to ui64, isize, usize, f32, f64, bool, void, str, pointers, function pointers and fixed arrays.
There is no character literal, no tuples and no variadic functions, and the owned string type String is a stdlib type.
Functions and non-virtual methods overload by parameter types, a call picks the lowest argument cost, and fn name = target makes an alias for the target's overloads.
Functions, structs and classes take unconstrained type parameters, and each concrete use is a separate instance checked on its own, with a note for each instantiation that requested an error inside it, up to eight levels.
A string literal is a pointer to ui8 when one is expected, a zero-padded ui8 array copy when that is expected, and otherwise a read-only str with fields ptr and len.
Functions are written fn name(a: T) -> R, locals with local name: T = value, globals with global name: T = constant, and read-only constants with const name = value whose initializers must be compile-time constants.
Implicit conversions are same-signedness integer widening only, and every other conversion needs as.
Struct holds data, class has methods, single inheritance and optional init constructors and drop destructors, and interface is nominal and needs an impl block.
Imports use from "m" import x and export from re-exports, and the global namespace holds only the closed builtins and the registered panic and allocator names, which a user registration of the same role replaces.
