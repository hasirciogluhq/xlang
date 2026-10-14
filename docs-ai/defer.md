# defer

defer followed by a statement runs that statement when the enclosing block is left, in reverse order of registration.
The scope is the block, not the function, so a defer in a loop body runs at the end of every iteration.
It runs on fall off, return, break, continue and labeled jumps, and does not run on panic, trap or exit.
A return value is computed first and the deferred statements run after it, so they cannot change what is returned.
Registration is lexical: a defer is seen only by exits that appear after it in the source.
Names bind where the defer is written and values are read when it runs.
return, break, continue, local and defer cannot be deferred, which is error E0709.
The compiler copies the statement to each exit, so there is no runtime stack, no allocation and no RAII.
