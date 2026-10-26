# classes

A class has fields, methods, single inheritance and a vptr, and a virtual method is replaced in a derived class with override.
A class may declare init constructors that take parameters after self and can be overloaded, and at most one drop destructor that takes only self, and neither returns a value.
T(args) builds a new object by zeroing it, setting its vptr and running init on it.
A class with an init cannot be built with a field literal, and a derived init first runs the base init that takes no arguments.
A class needs drop when it has a drop method or a field or base class that needs drop, and values that need drop are move-only.
A droppable local or by-value parameter is dropped when its block ends, and the drop runs the user drop body, then the field drops in reverse declaration order, then the base drop.
Moving a droppable local by value cancels its drop, and moving a field, an element or a dereferenced pointer by value is an error.
A class that needs drop cannot be a struct field or a global, and an array whose elements need drop is rejected as a local, parameter, field or global.
new T allocates through the registered allocator, and delete runs the drop of the pointee before it frees the memory.
Drops are statically dispatched, so deleting a derived object through a base pointer runs only the base drop.
