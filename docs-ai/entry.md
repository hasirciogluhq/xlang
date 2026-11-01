# entry

The process starts at an entry symbol that the compiler generates, which runs every init function and then main, and exits with main's int32 result.
An init function is marked with the attribute init, takes no parameters, returns no value and has a body.
Init functions run in group order, with the stdlib group first and the user group second.
Within a group a module's imports initialize before the module, and modules run in source order.
Calling an init function is an error, and an init attribute on a method, a generic item, or a function with parameters or a return type is an error.
A user registration of the panic, alloc, free or unwind role replaces the stdlib one, and two registrations of one role in the same group are an error.
A program replaces where memory comes from by assigning the stdlib global currentAllocator, for example inside an init function, or by registering its own alloc and free.
A panic ends the process with status 134 unless an enclosing try statement catches it.
A caught panic runs the defer statements and drops of every frame between the panic and the try, then continues in the catch block.
The unwinder is stdlib code in the runtime area registered with the unwind role, and the compiler only emits landing pads, try snapshots and the pad table.
Unwinding walks frame pointers, so frames of foreign code cannot be unwound and every stack entry point zeroes the frame pointer.
main is the user program's entry and may be defined freely, and a void main makes the process exit with status 0.
Without a main no entry stub is emitted.
Building a file without a main as an executable is a diagnostic reported before code generation, and not a linker failure.
Entry stubs exist for linux and macos on x86_64 and aarch64 and for windows on x86_64 only.
