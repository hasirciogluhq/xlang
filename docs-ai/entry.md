# entry

The process starts at an entry symbol that the compiler generates, which runs every init function and then main, and exits with main's int32 result.
An init function is marked with the attribute init, takes no parameters, returns no value and has a body.
Init functions run in group order, with the stdlib group first and the user group second.
Within a group a module's imports initialize before the module, and modules run in source order.
Calling an init function is an error, and an init attribute on a method, a generic item, or a function with parameters or a return type is an error.
A user registration of the panic or allocator role replaces the stdlib one, and two registrations of one role in the same group are an error.
Inside an init function a program may replace a registered global with the builtin __builtin_setRegistered, and that builtin is an error anywhere else.
main is the user program's entry and may be defined freely, and a void main makes the process exit with status 0.
Without a main no entry stub is emitted.
Entry stubs exist for linux and macos on x86_64 and aarch64 and for windows on x86_64 only.
