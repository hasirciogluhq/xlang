# variadic

A function whose last parameter is a pack takes any number of arguments, and the pack is written with two dots before its name.
Each call instantiates the function for the argument types of that call, so the argument count and the argument types are fixed at compile time.
Inside the body the pack's len is the argument count, and a for loop over the pack runs once per argument with that argument's own type.
Break and continue work inside a pack loop, and spreading the pack in a call forwards its arguments to another pack function.
A pack cannot be used as a value, indexed, or spread outside a call, and a function has at most one pack, which must be its last parameter.
A pack function cannot be exported with C linkage, declared external, virtual, override, generic, or a class or interface method.
A call considers a pack function only when no fixed-arity overload of that name has the same number of parameters as the call has arguments.
The formatting functions of std fmt and std io are written with a pack, so print, println, sprintf and the other format functions take any number of arguments.
