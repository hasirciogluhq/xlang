# Compiler-level gaps (self-hosting)

Type: **Missing** = absent. **Wrong** = present but incorrect or unsafe. **Unverified** = not confirmed in docs or source.

| # | Topic | Type | Now | Should be |
|---|-------|------|-----|-----------|
| 1 | Sum type / payload enum | Missing | Enums are integer constants only (`enum Name: T`). AST nodes need class + `kind` field + manual casts. | Tagged union such as `enum Expr { Num(i64), Bin(*Expr, Op, *Expr) }`. `switch` binds the payload and checks exhaustiveness; reading the wrong variant is a compile or checked-mode error. |
| 2 | Slice (`[]T`) | Missing | Only `*T` (unchecked) and `[T; N]` (fixed size, checked). | `(ptr, len)` fat pointer with bounds checks under `--safety=checked`. `[T; N]` and `Vec` convert to it implicitly. |
| 3 | Tuple / multiple return | Missing | None (not defined by the plan). | Either tuples with destructuring, or deliberately none and use generic `Result`/`Option`. User decision. |
| 4 | `for x in coll` | Missing | `for` works only on `lo..hi` and variadic packs. | Desugar to the `Iter<T>` interface so any collection can be iterated. |
| 5 | Closure | Missing | Only `fn(A) -> R` pointers, no capture. | Capturing lambda, or deliberately none. Not required for bootstrap. |
| 6 | Generic constraint | Missing | None. A body is checked only for the types it is instantiated with. | Concept-like constraints; errors reported at the declaration. Can wait. |
| 7 | Generic method scope | Missing | Class only. In `impl`/`interface`: `E0733`. Cannot be `virtual`/`override` (`E0730`) or overloaded (`E0405`). | Interface may stay without generic methods (vtable). `impl` support is reasonable. |
| 8 | Operator overload (`[]`, `==`, `<`) | Unverified | Not found in `docs/LANGUAGE.md`. | Needed so `Vec` indexing and `Map<K, V>` key comparison are user-definable. Check first. |
| 9 | Drop dispatch | **Wrong** | Drops are static: `delete` of a derived object through a base pointer runs only the base drop. | Virtual drop dispatch through the vtable, or a base without virtual drop is an error. |
| 10 | Base constructor | **Wrong** | A derived `init` calls the base's argument-less `init` automatically; no explicit call with arguments. | Explicit base call (`super.init(...)`-like); the implicit call only for the default case. |
| 11 | Array of droppable elements | Missing | `E0721` at locals, parameters, fields and globals. | Array drops its elements in order. |
| 12 | Droppable type as global / struct field | Missing | A class needing drop cannot be a global or struct field (`E0721`). | Allow globals (no drop at exit); define an ownership rule for struct fields. |
| 13 | Global initializer | Design OK, edges missing | Compile-time constants only; dynamic setup via `@init`. | Do **not** add dynamic initializers (init-order problems). Widen the set of accepted constant expressions (consteval calls). |
| 14 | `const` cannot read other consts | **Wrong/weak** | `const B = A * 2` is `E0715`. `const` is not usable as array length, `staticAssert` operand or `define` argument. | `const`s may read each other, count as fully constant, and work as array lengths. Clarify or merge the `define`/`const` split. |
| 15 | Constant folding | Missing | HIR does not fold; `N * 2` is constant in global initializers but folded later. | Fold in HIR; `const`, array length and switch labels share one folder. |
| 16 | Consteval | Missing | None. | `consteval fn` for compile-time tables (keyword table, hashes). Optional. |
| 17 | `register` roles | Design limit | Only `panic`, `alloc`, `free`. | New runtime need means new role. Unwind/try may need one (see 18). |
| 18 | Error propagation | Missing/partial | Working tree has `std/runtime/unwind.xl` and `test/e2e/try.xl`; state not inspected. | One chosen mechanism (`Result` + `try`, or panic recover). |
| 19 | Windows aarch64 | Missing | Entry stub exists for x86_64 Windows only. | aarch64 stub, or an explicit "unsupported" diagnostic. |
| 20 | LLVM dependency | Structural | Codegen links LLVM statically (needs libc++/libc). | Own backend, or record the conflict with the no-libc rule in `KNOWN-LIMITATIONS.md`. |

## Priority for bootstrap

1. First: 9, 10 (silent wrong behavior), then 1, 2, 14, 15, 4, 18.
2. Then: 8, 11, 12.
3. The rest does not block bootstrap.
