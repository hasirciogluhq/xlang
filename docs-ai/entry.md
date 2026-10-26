# entry

The process starts at the OS entry symbol, which calls the registered start function and then main, and exits with main's int32 result.
The registered start function is declared with register(start) and takes no parameters and returns no value.
The stdlib defines start as an empty function that will hold runtime and boot initialization.
start always runs before main, and main runs only after start has returned.
main is the user program's entry and may be defined or overridden freely.
A void main makes the process exit with status 0.
Without the stdlib no start is registered, so only main runs.
Without a main no entry stub is emitted.
