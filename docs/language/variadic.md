---
title: Variadic functions
---

A function whose last parameter is a **pack** takes any number of arguments. The pack is written with two dots before its name.

```xlang
from "std" import println

fn countSum(..args) -> i64 {
    local total: i64 = 0
    for a in args {
        total += a as i64
    }
    return total
}

@entry fn main() -> i32 {
    println("{}", countSum(1, 2, 3))
    return 0
}
```

The pack is fixed at compile time: each call instantiates the function for the argument types of that call, and the instance has one parameter per argument. HIR names such an instance by its argument types, for example `countSum<i32, i32, i32>`.

## Inside the body

- `args.len` is the argument count.
- `for a in args { ... }` is unrolled once per argument, with `a` typed as that argument. `break` and `continue` work in it.
- `..args` in a call forwards the arguments to another pack function.

## Rules

- A pack is not a value. Using it bare (`E0735`), indexing it (`E0738`), or spreading it outside a call (`E0737`) is an error.
- A function has at most one pack, and it must be the last parameter (`E0734`).
- A pack cannot be `export(c)`, `declare`, `virtual`, `override`, generic, or a class or interface method (`E0736`).
- A call considers a pack function only when no fixed-arity overload of that name has as many parameters as the call has arguments. Otherwise the fixed-arity overload wins.

## Where it is used

The formatting functions are written with a pack, which is why they take any number of arguments:

```xlang
print(fmt: str, ..args)
println(fmt: str, ..args)
sprintf(fmt: str, ..args) -> String
```

`print` adds no newline and `println` adds one `\n`. {% raw %}`{}` takes the next argument, `{{` and `}}` print braces{% endraw %}, and a count mismatch panics. See [Strings]({{ site.baseurl }}/std/strings.html) for the formatting rules.

`syscall(nr, args...)` is the one builtin with a variable argument count; it is not a pack function, and its maximum argument count is checked against the target at compile time.
