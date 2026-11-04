# runtime

A user registration of the panic, alloc, free or unwind role replaces the stdlib one, and two registrations of one role in the same group are an error.
A program replaces where memory comes from by assigning the stdlib global currentAllocator, for example inside an init function, or by registering its own alloc and free.
A panic ends the process with status 134 unless an enclosing try statement catches it.
A caught panic runs the defer statements and drops of every frame between the panic and the try, then continues in the catch block.
The unwinder is stdlib code in the runtime area registered with the unwind role, and the compiler only emits landing pads, try snapshots and the pad table.
Unwinding walks frame pointers, so frames of foreign code cannot be unwound and every stack entry point zeroes the frame pointer.
