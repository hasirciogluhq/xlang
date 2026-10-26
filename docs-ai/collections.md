# collections

Vec<T> in the stdlib module vec is a growable array that stores its elements bitwise in slots allocated through the registered allocator.
The Vec owns the elements below its length, and each one is dropped exactly once when it is replaced, removed, truncated or when the Vec is dropped.
get, first and last return a pointer to an element, so reading an element never copies it.
set drops the old element and stores the new one, and push, insert and set take their value by move.
pop, remove and swapRemove return the removed element by value, so the caller owns it and drops it.
Growth allocates max(needed, 2 * capacity, 4) slots and moves the existing elements into them.
vecClone copies a Vec only when its element type needs no drop, because a bitwise copy would drop an element twice.
An index outside the elements panics with the text Vec: index I out of range for length L.
