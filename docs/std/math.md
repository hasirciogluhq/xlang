# Mathematics

The mathematics modules are in `std/math`. There is no C runtime: every function is written in xlang, and each file header states its method and range. Accuracy is checked against `bc -l` (arbitrary precision) on sample points; where a claim is weaker than full double precision, the header says so.

## Integers (`integer.xl`)

| Function                         | Notes                                                                        |
| -------------------------------- | ---------------------------------------------------------------------------- |
| `absInt`, `signInt`, `clampInt`  | signed 64-bit values                                                         |
| `gcd`, `lcm`                     | Euclid; `lcm` panics on overflow                                             |
| `factorial`                      | up to 20; panics above                                                       |
| `binomial`                       | exact; panics when the result does not fit                                   |
| `mulMod`, `modPow`, `modInverse` | modulus below 2^63; `modInverse` returns 0 when no inverse exists            |
| `isPrime`, `nextPrime`           | Miller-Rabin with the 12 bases 2 to 37, deterministic for every 64-bit value |
| `legendre`, `jacobi`             | Euler's criterion; Jacobi by reciprocity (Cohen, algorithm 1.4.10)           |
| `crt`                            | Chinese remainder theorem for two coprime moduli, returns `CrtResult`        |

## Big integers

### `BigInt` (`big.xl`)

`BigInt` is an owning, unsigned arbitrary-precision integer: a class with a `Vec<ui32>` of little-endian 32-bit limbs and a method for every operation. A method writes its result into a destination, so no operation allocates a return value and the result of `a + b` can go into `c`, into `a` or into `b`:

```xlang
from "std/math" import BigInt

local a: BigInt = BigInt(1)
local b: BigInt = BigInt(18446744073709551615)
local sum: BigInt = BigInt()
a.add(&b, &sum)                       // sum = a + b, a and b unchanged
print(sum.toDecimal().asStr())        // 18446744073709551616
```

`BigInt()` is 0 and `BigInt(v)` is a `ui64` (the low limb is `v & 0xFFFFFFFF`, the high limb `v >> 32`). The representation is canonical: no zero limbs above the most significant one, so `cmp` is a length comparison followed by one limb comparison, and equal numbers have equal limb vectors.

| Group         | Methods                                                                                                                                               |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Shape         | `count()` limbs, `bitLength()` bits, `isZero`, `isEven`, `isOdd`, `isOne`, `low64()` (the value modulo 2^64)                                          |
| Comparison    | `cmp(&o)` gives -1, 0 or 1; `equals(&o)`                                                                                                              |
| Arithmetic    | `add(&o, &out)`, `sub(&o, &out)`, `mul(&o, &out)`, `divMod(&o, &q, &r)` (false for a zero divisor), `mod(&o, &out)`, `pow(e: ui64, &out)`             |
| Number theory | `gcd(&o, &out)`, `lcm(&o, &out)`, `modPow(&e, &m, &out)` (false for an even or zero modulus), `modInverse(&m, &out)` (false when `gcd(self, m) != 1`) |
| Bits          | `shiftLeft(n: usize, &out)`, `shiftRight(n: usize, &out)` (rounds down)                                                                               |
| Text          | `fromDecimal(s, &out)`, `fromHex(s, &out)` (optional `0x`; both false on bad text), `toDecimal()`, `toHex()`, `toString()` (all return `String`)      |
| Copy          | `copy(&out)`                                                                                                                                          |

The bound is `BIG_MAX = 130` limbs, that is 4160 bits: it is the size of the fixed arrays that `bignum` and `bigdiv` use, so every `BigInt` fits one of their operand buffers. An operation whose result would need a 131st limb panics with `BigInt: result exceeds 4160 bits` (and `sub` panics when the right operand is larger, because the type is unsigned) instead of wrapping. A caller that must not stop checks `bitLength()` before the operation.

`modPow` needs an odd modulus: Montgomery reduction is defined modulo an odd number. There are no operator overloads; the operation is the method name.

### Limb array functions (`bigint.xl`, `bigdiv.xl`, `bignum.xl`)

Below `BigInt` are the same algorithms on raw limb arrays, for the fixed-size operands of the crypto modules (`std/crypto/sig/ecdsa`, RSA). Limbs are little-endian `ui32`. `bigintAdd`, `bigintSub`, `bigintMul`, `bigintDivMod` (Knuth Algorithm D), `bigintPow`, `bigintGcd`, `bigintLcm`, `bigintModPow` (Montgomery, odd modulus), `bigintModInverse`, and `bigintIsProbablePrime` (Miller-Rabin with random bases, FIPS 186-5 B.3.1). These take `*ui32` and an explicit limb count and write into caller arrays; `BigInt` never changes them.

## Floating point (`float.xl`, `trig.xl`, `math.xl`)

| Group                     | Functions                                                                                      | Method                                                 |
| ------------------------- | ---------------------------------------------------------------------------------------------- | ------------------------------------------------------ |
| Rounding                  | `floor`, `ceil`, `trunc`, `round` (away from zero), `remainder` (IEEE), `fmod` (exact), `modf` | integer conversion within 2^52; exact subtraction      |
| Exponential and logarithm | `exp`, `exp2`, `log`, `ln`, `log2`, `log10`, `pow`, `cbrt`, `hypot`                            | range reduction and Taylor or atanh series             |
| Bits and scaling          | `frexp`, `ldexp`, `copysign`, `nextafter`, `f64Bits`, `f64FromBits`                            | reinterpretation of the bit pattern                    |
| Trigonometry              | `sin`, `cos`, `tan`, `atan`, `atan2`, `asin`, `acos`                                           | reduction by pi/2 (two parts), Taylor on [-pi/4, pi/4] |
| Hyperbolic                | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`                                              | exponentials, series near zero                         |
| Square root               | `sqrt` (in `math.xl`)                                                                          | Newton iteration                                       |

Sample values agree with `bc -l` to 1e-9. Full double accuracy is not verified, and the trigonometric reduction loses accuracy for arguments much larger than 1e6.

## Vectors, rectangles and matrices

Geometry is split by type, one file per value class, so a caller that needs only 2D points does not read the matrix code:

| File            | Contents                                                                                                                                                                                     |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `scalar.xl`     | `MATH_PI`, `MATH_PI_2`, `MATH_TAU`, `lerp`, `smoothstep`, `mapRange`, `clamp`, `degToRad`, `radToDeg`                                                                                        |
| `vec2.xl`       | `Vec2`                                                                                                                                                                                       |
| `vec3.xl`       | `Vec3`                                                                                                                                                                                       |
| `vec4.xl`       | `Vec4`                                                                                                                                                                                       |
| `rect.xl`       | `Rect`                                                                                                                                                                                       |
| `matrix.xl`     | `Mat`, `matIdentity`, `matCopy`, `matAdd`, `matSub`, `matTranspose`, `matDet`, `matInverse` and the internal LU                                                                              |
| `matrix2.xl`    | `Mat2` and `mat2Identity`, `mat2Rotation`, `mat2Scale`, `mat2FromColumns`, `mat2Lerp`                                                                                                        |
| `matrix3.xl`    | `Mat3` and `mat3Identity`, `mat3RotationX/Y/Z`, `mat3RotationAxis`, `mat3Scale`, `mat3Translation`, `mat3FromColumns`, `mat3Lerp`                                            |
| `matrix4.xl`    | `Mat4` and `mat4Identity`, `mat4Translation`, `mat4Scale`, `mat4RotationX/Y/Z`, `mat4RotationAxis`, `mat4FromQuat`, `mat4Perspective`, `mat4Ortho`, `mat4LookAt`, `mat4FromMat3`, `mat4Lerp` |
| `quaternion.xl` | `Quat` and `quatIdentity`, `quatFromAxisAngle`, `quatFromEuler`, `quatToEuler`, `quatFromMat3`, `quatLookRotation`                                                                           |
| `transform.xl`  | `Transform` and `transformIdentity`                                                                                                                                                          |

`Vec2`, `Vec3`, `Vec4` and `Rect` are classes with `f64` fields, an `init` constructor (`Vec2(1.0, 2.0)`, `Vec3(1.0, 2.0, 3.0)`, `Vec4(1.0, 2.0, 3.0, 1.0)`, `Rect(x, y, w, h)`) and methods. They hold no pointer and use no heap, so they are copied by value, held in arrays and fields, and returned from functions without an allocation. Because they have an `init`, the field literal `Vec3 { x: .. }` does not exist: a value is built with the constructor. A method takes the other operand by pointer (`a.add(&b)`) and returns a new value, which must be stored in a variable before another method is called on it (the receiver has to be addressable):

```xlang
local a: Vec3 = Vec3(1.0, 2.0, 3.0)
local b: Vec3 = Vec3(4.0, 5.0, 6.0)
local c: Vec3 = a.cross(&b)
local n: Vec3 = c.normalized()        // a unit vector; c || 7.348
```

### `Vec2` and `Vec3`

| Method                                       | `Vec2`                                              | `Vec3`                     | Result                                           |
| -------------------------------------------- | --------------------------------------------------- | -------------------------- | ------------------------------------------------ |
| `add`, `sub`                                 | yes                                                 | yes                        | vector                                           |
| `mul`                                        | yes                                                 | yes                        | componentwise (Hadamard) product                 |
| `scale(k)`, `negate()`                       | yes                                                 | yes                        | vector                                           |
| `dot`                                        | yes                                                 | yes                        | `f64`                                            |
| `cross`                                      | yes (`f64`: the z of the 3D cross, the signed area) | yes (`Vec3`, right-handed) |                                                  |
| `length`, `lengthSquared`                    | yes                                                 | yes                        | `f64`                                            |
| `normalized()`                               | yes                                                 | yes                        | unit vector; the zero vector for the zero vector |
| `distance`, `distanceSquared`                | yes                                                 | yes                        | `f64`                                            |
| `lerp(&o, t)`                                | yes                                                 | yes                        | vector, `t` not clamped                          |
| `min`, `max`                                 | yes                                                 | yes                        | componentwise                                    |
| `equals`, `near(&o, eps)`, `nearDefault(&o)` | yes                                                 | yes                        | `bool`; `nearDefault` uses 1e-9                  |
| `clamped(lo, hi)`                            | yes                                                 | no                         | vector                                           |
| `transform(&Mat)`                            | 2x2 matrix                                          | 3x3 matrix                 | vector                                           |
| `transformAffine(&Mat)`                      | 3x3 matrix `(x, y, 1)`                              | no                         | vector                                           |

`normalized` returns the zero vector for the zero vector rather than a NaN, so a caller never needs the boolean that an out-parameter form would return; `length()` on the result tells the caller which case it got.

### `Vec4`

A homogeneous vector `(x, y, z, w)`: `w = 1` for a point under an affine matrix, `w = 0` for a direction. `dot`, `length`, `lengthSquared`, `normalized`, `scale`, `add`, `sub`, `negate`, `lerp`, `distance`, `equals`, `near`, `nearDefault`, and `xyz()` (the first three components as a `Vec3`). There is no cross product in 4D.

### `Rect`

An axis-aligned rectangle stored as origin plus extent (`x`, `y`, `w`, `h`), with `y` growing downward as in screen coordinates; every method compares coordinates directly, so it works the same in a y-down frame.

| Method                                               | Result                                                                                     |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| `area`, `perimeter`                                  | `f64`; 0 for an empty rectangle                                                            |
| `isEmpty`                                            | `bool`: true when `w` or `h` is not positive                                               |
| `left`, `top`, `right` (`x + w`), `bottom` (`y + h`) | `f64`; the right and bottom edges are exclusive                                            |
| `origin()`, `center()`                               | `Vec2`                                                                                     |
| `contains(&p)`, `containsRect(&o)`                   | `bool`; the border counts as inside                                                        |
| `intersects(&o)`                                     | `bool`; touching edges count                                                               |
| `union(&o)`, `intersect(&o)`                         | `Rect`; an empty operand is ignored by `union` and gives an empty rectangle in `intersect` |
| `translate(dx, dy)`, `inflate(d)`, `deflate(d)`      | `Rect`; `deflate` clamps the extent to zero                                                |
| `distanceTo(&p)`                                     | `f64`; 0 inside                                                                            |
| `equals`, `near(&o, eps)`                            | `bool`                                                                                     |

### `Mat` and the matrix functions (`matrix.xl`)

`Mat` is the one math type that owns heap storage: a row-major `rows x cols` matrix of `f64` on the registered allocator, entry `(i, j)` at `data[i * cols + j]`. `Mat(0, 0)` is the empty matrix. A `Mat` owns its storage and is move-only; a function that writes into a caller's matrix takes `*Mat`, and `resize` / `mulTo` reuse the storage the caller already has.

| Member                                       | Notes                                            |
| -------------------------------------------- | ------------------------------------------------ |
| `at(i, j)`, `set(i, j, v)`, `slot(i, j)`     | entry access; `slot` is unchecked                |
| `rows`, `cols`, `count()`, `isEmpty()`       | shape                                            |
| `resize(rows, cols)`                         | reshapes and zeroes, reusing storage             |
| `transposed()`, `mul(&b)`, `mulTo(&b, &out)` | new matrix; `mulTo` writes into `out`            |
| `det()`                                      | determinant of a square matrix, through `matDet` |
| `equals(&o)`, `near(&o, eps)`                | exact and 1e-9 comparisons                       |

`matDet(&a)` is the determinant and `matInverse(&a, &out)` writes the inverse (false when `a` is singular); both factor a copy of `a` with partial-pivoting LU (Golub and Van Loan, Matrix Computations, algorithm 3.4.1), so neither changes its input and both cost O(n^3) with two extra `n x n` buffers. `matIdentity(rows, cols)`, `matAdd(&a, &b, &out)`, `matSub(&a, &b, &out)` and `matTranspose(&a, &out)` are the elementwise operations; `matCopy(&dst, &src)` copies entries.

The determinant is zero only for an exactly singular matrix; a nearly singular one gives large finite values, and the caller estimates the conditioning itself (no `rcond` is computed). The work buffers come from the registered allocator, so the matrix code is the only part of the geometry library that can fail on out-of-memory.

### Fixed-size matrices, quaternions and transforms

The types above are for an order the program does not know until run time. For the orders that cover almost every 2D and 3D program there are fixed-size classes instead: `Mat2` (2x2), `Mat3` (3x3), `Mat4` (4x4), `Quat` and `Transform`. They hold `f64` fields in a fixed array (`m: [f64; 4]`, `[f64; 9]`, `[f64; 16]`), use no heap and no pointer, and cost exactly the arithmetic they perform, which is what a per-frame call in a renderer or a physics step needs. Row-major throughout: entry `(i, j)` of an `N x N` matrix is `m[i * N + j]`, with the matrix acting on a column vector.

There are no static methods in the language, so a named constructor is a free function: `mat3Identity()`, `mat4Perspective(..)`, `quatFromAxisAngle(..)`, `transformIdentity()`. A method that returns a value cannot be chained directly (`a.mul(&b).transposed()` needs an addressable receiver), so every product also has an out-parameter form and the result goes into a local first:

```xlang
local r: Mat4 = mat4RotationZ(MATH_PI / 2.0)
local t: Mat4 = mat4Translation(1.0, 2.0, 3.0)
local m: Mat4 = t.mul(&r)             // translate * rotate
local out: Vec4 = Vec4(0.0, 0.0, 0.0, 0.0)
m.mulVec(&Vec4(1.0, 0.0, 0.0, 1.0), &out)
```

| Type        | Methods                                                                                                                                                                                                         |
| ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Mat2`      | `add`, `sub`, `scale`, `mul(&b)` / `mulTo(&b, &out)`, `mulVec(&v, &out)`, `transposed()`, `det()`, `inverse(&out)` (false when singular), `trace()`, `isIdentity()`, `near(&o, eps)`, `nearDefault(&o)`         |
| `Mat3`      | the same, with `mulVec(&Vec3, &Vec3)`                                                                                                                                                                           |
| `Mat4`      | the same, with `mulVec(&Vec4, &Vec4)`; `inverse` is the general 4x4 cofactor inverse                                                                                                                            |
| `Quat`      | `add`, `sub`, `scale`, `mul(&b)` (Hamilton product) / `mulTo`, `conjugate()`, `inverse()`, `normalized()`, `length()`, `dot`, `rotateVec(&v, &out)`, `slerp(&b, t)`, `toMat3()`, `near`                         |
| `Transform` | `toMat4()`, `applyPoint(&p, &out)` (with the translation), `applyVector(&v, &out)` (without), `inverse(&out)` (false for a zero scale), `compose(&parent, &out)`, `translate(dx, dy, dz)`, `rotate(&q)`, `near` |

The named constructors build the usual placements: `mat2Rotation(rad)`, `mat3RotationX/Y/Z(rad)`, `mat3RotationAxis(&axis, rad)` (Rodrigues), `mat3Translation(tx, ty)` for 2D homogeneous work, `mat4Perspective(fovYRad, aspect, near, far)` and `mat4Ortho(l, r, b, t, n, f)` in the OpenGL clip convention (view space looks down -z), `mat4LookAt(&eye, &center, &up)`, and the `*FromQuat` / `*FromMat3` conversions. Every angle is in radians.

`Quat` is the rotation type: a unit quaternion `w + xi + yj + zk`, with `Quat()` the identity. `quatFromEuler(x, y, z)` uses the intrinsic ZYX (yaw-pitch-roll) order, `R = Rz * Ry * Rx`, matching `glm::eulerAngleZYX` and three.js `Euler` with order `'XYZ'`; `quatToEuler` inverts exactly that order. `slerp` takes the shorter arc (Shoemake, SIGGRAPH 1985).

`Transform` is a placement in the order `M = T * R * S`: a point is scaled, then rotated, then translated, so `applyPoint(p) = R * (S * p) + t` and `applyVector(v) = R * (S * v)`. `inverse` undoes that order exactly (`S^-1 * R^-1 * T^-1`) and refuses a zero scale component; `compose` gives the child placement in the parent frame (`parent * self`).
