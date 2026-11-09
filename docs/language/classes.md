---
title: Classes
---

A `class` holds fields and methods, may derive from one base class, and carries a vptr. Everything here is also covered in the [language reference]({{ site.baseurl }}/language/); this page collects the rules that belong to classes alone.

## Declaration

```xlang
class Animal {
    legs: i32

    fn init(self, legs: i32) {
        self.legs = legs
    }

    virtual fn sound(self) -> i32 { return 1 }

    fn drop(self) { }
}

class Dog : Animal {
    tail: i32

    fn init(self, tail: i32) {
        Animal.init(self, 4)          // the base `init` with no arguments runs first
        self.tail = tail
    }

    override fn sound(self) -> i32 { return 2 }
}
```

- A `class` has single inheritance (`class Dog : Animal`) and a vptr; `virtual` methods are replaced with `override`.
- Field names follow `camelCase`, type names `PascalCase`.
- A struct holds data only and has no methods; a class owns both. See [interface and enum]({{ site.baseurl }}/language/#structs-classes-interfaces-and-enums) in the reference.

## Constructors and destructors

- A class may declare `fn init(self, params...)`. Constructors take their parameters after `self`, may be overloaded, and return nothing (`E0722` otherwise).
- A class may declare at most one `fn drop(self)`. A destructor takes only `self`, returns nothing, and is neither `virtual` nor `override` (`E0722` otherwise).
- `T(args)` builds a new object: it is zeroed, its vptr is set, then `init` runs on it.
- A class **with** an `init` cannot be built with a field literal; `T { ... }` is then `E0723`. A class **without** one keeps the field literal.
- A derived `init` runs the base `init` that takes no arguments first. When the base has constructors but none without arguments, the derived `init` is `E0722`.
- Inside an `init`, `self.f = v` on a droppable field is a raw store: the field holds zeroed memory there, so the old value is not dropped.

## Drop and moves

A class *needs drop* when it has a `drop` method, or a field or base class that needs drop. Such values are move-only.

- A local or by-value parameter that needs drop is dropped when its block ends or the function returns, last-in first-out, together with `defer`.
- The drop runs the user `drop` body, then the drop of each field that needs drop in reverse declaration order, then the base class drop.
- Moving a droppable local by value (a `local` initializer, the right side of an assignment, a by-value argument, a `return` value) is a move, and its drop is cancelled. A temporary passes its ownership on.
- Moving a field, an element or `*p` by value is `E0720`; take its address instead.
- A class that needs drop cannot be a struct field or a global (`E0721`); use a class field or a `*T` made with `new`. An array whose element type needs drop is also `E0721`, because arrays do not drop their elements. A droppable element belongs in a `Vec<T>`, which drops its elements when it is dropped.
- Drops are statically dispatched: deleting a derived object through a base pointer runs only the base drop.
- `__builtin_read(p)` and `__builtin_write(p, v)` are the raw move and store through a pointer, with no move and no drop. They are what a container needs for its slots, and they are unchecked, like `*p`.

## Allocation

The compiler knows no allocator type and no allocator method names, only the registered `alloc` and `free` functions.

- `new T(args)` gets memory from the registered `alloc` and constructs in it.
- `new(p) T(args)` constructs in the memory `p` points at and allocates nothing (`E0709` when `p` is not a pointer).
- `delete p` runs the drop of the pointee when it needs drop, then releases the memory through the registered `free`.
- There is no explicit-allocator form of `delete`. To release memory from another allocator, call `__builtin_drop` and that allocator's `free` yourself.
- Without a registered `alloc`, `new T` is `E0501`; without a registered `free`, `delete p` is `E0501`. The stdlib registers both by default; see [Runtime]({{ site.baseurl }}/compiler/runtime.html).

## Generics

A class may take type parameters (`class Box<T> { ... }`). Each use with concrete type arguments creates a separate class with its own layout and vptr. A generic method cannot be `virtual` or `override` (`E0730`) and cannot be overloaded (`E0405`).
