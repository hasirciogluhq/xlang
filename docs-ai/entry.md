# entry

A program has exactly one function marked with the entry attribute: an executable must have one, a static library or object file must not, and a shared library may have one.
The entry function takes no parameters, or an argument count of type i32 followed by an argument vector of type pointer to pointer to ui8, and returns i32 or nothing.
The entry function is identified only by its attribute, and it may be overloaded like any other function.
The compiler generates the process start-up, which runs every init function, then the entry function, then every fini function in reverse order, and exits with the status the entry function returns.
The init and fini attributes mark module-level boot and teardown functions, which take no parameters, return nothing and have a body, and all three attributes are optional.
Init functions run in group order with the stdlib group first, and within a group a module's imports initialize before the module and modules run in source order.
Calling an init, fini or entry function, or using its name as a value, is an error, and an init or fini attribute on a method, a generic item or a function with parameters or a return type is an error.
A shared library runs its init functions and its entry function when it is loaded and its fini functions when it is unloaded, while a static library or an object file leaves both to the host.
The attributes entry, init and fini are the only names the compiler reads for start-up and teardown.
Windows executables take no entry arguments and Windows shared libraries run no hooks yet.
