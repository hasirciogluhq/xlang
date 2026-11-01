# language

Source files use the extension xl, declarations end at a line break without semicolons, and conditional compilation uses hash if directives.
Types are i8 to i64, ui8 to ui64, isize, usize, f32, f64, bool, void, str, pointers, function pointers and fixed arrays.
There is no character literal and no tuples, a function's last parameter may be a variadic pack, and the owned string type String is a stdlib type.
Functions and non-virtual methods overload by parameter types, a call picks the lowest argument cost, and fn name = target makes an alias for the target's overloads.
Functions, structs, classes, interfaces, aliases and class methods take unconstrained type parameters, and each concrete use is a separate instance checked on its own, with a note for each instantiation that requested an error inside it, up to eight levels.
A string literal is a pointer to ui8 when one is expected, a zero-padded ui8 array copy when that is expected, and otherwise a read-only str with fields ptr and len.
Functions are written fn name(a: T) -> R, locals with local name: T = value, globals with global name: T = constant, and read-only constants with const name = value whose initializers must be compile-time constants.
A try block followed by a catch block with an optional name binds the panic message as a ui8 pointer, and a panic raised anywhere inside the try body runs the pending defer statements and drops of the frames in between and continues in the catch block.
Implicit conversions are same-signedness integer widening only, and every other conversion needs as.
Struct holds data, class has methods, single inheritance and optional init constructors and drop destructors, and interface is nominal and needs an impl block.
Imports use from "m" import x, modules may import each other, export * from "m" re-exports, and a path that starts with a dependency's repository path comes from that installed package; the global namespace holds only the closed builtins and the registered panic, alloc, free and unwind names, which a user registration of the same role replaces.
