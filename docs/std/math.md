# Mathematics

The mathematics modules are in `std/math`. There is no C runtime: every function is written in xlang, and each file header states its method and range. Accuracy is checked against `bc -l` (arbitrary precision) on sample points; where a claim is weaker than full double precision, the header says so.

## Integers (`integer.xl`)

| Function | Notes |
|---|---|
| `absInt`, `signInt`, `clampInt` | signed 64-bit values |
| `gcd`, `lcm` | Euclid; `lcm` panics on overflow |
| `factorial` | up to 20; panics above |
| `binomial` | exact; panics when the result does not fit |
| `mulMod`, `modPow`, `modInverse` | modulus below 2^63; `modInverse` returns 0 when no inverse exists |
| `isPrime`, `nextPrime` | Miller-Rabin with the 12 bases 2 to 37, deterministic for every 64-bit value |
| `legendre`, `jacobi` | Euler's criterion; Jacobi by reciprocity (Cohen, algorithm 1.4.10) |
| `crt` | Chinese remainder theorem for two coprime moduli, returns `CrtResult` |

## Big integers (`bigint.xl`, `bigdiv.xl`, `bignum.xl`)

Limbs are little-endian `ui32`. `bigintAdd`, `bigintSub`, `bigintMul`, `bigintDivMod` (Knuth Algorithm D), `bigintPow`, `bigintGcd`, `bigintLcm`, `bigintModPow` (Montgomery, odd modulus), `bigintModInverse`, and `bigintIsProbablePrime` (Miller-Rabin with random bases, FIPS 186-5 B.3.1).

## Floating point (`float.xl`, `trig.xl`, `math.xl`)

| Group | Functions | Method |
|---|---|---|
| Rounding | `floor`, `ceil`, `trunc`, `round` (away from zero), `remainder` (IEEE), `fmod` (exact), `modf` | integer conversion within 2^52; exact subtraction |
| Exponential and logarithm | `exp`, `exp2`, `log`, `ln`, `log2`, `log10`, `pow`, `cbrt`, `hypot` | range reduction and Taylor or atanh series |
| Bits and scaling | `frexp`, `ldexp`, `copysign`, `nextafter`, `f64Bits`, `f64FromBits` | reinterpretation of the bit pattern |
| Trigonometry | `sin`, `cos`, `tan`, `atan`, `atan2`, `asin`, `acos` | reduction by pi/2 (two parts), Taylor on [-pi/4, pi/4] |
| Hyperbolic | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh` | exponentials, series near zero |
| Square root | `sqrt` (in `math.xl`) | Newton iteration |

Sample values agree with `bc -l` to 1e-9. Full double accuracy is not verified, and the trigonometric reduction loses accuracy for arguments much larger than 1e6.

## Vectors and matrices (`vector.xl`)

`dot`, `cross`, `norm`, `normalize`, `lerp`, `smoothstep`, `mapRange`, `degToRad`, `radToDeg`, and row-major matrices: `matAdd`, `matSub`, `matMul`, `matTranspose`, `matDet` (pivoted LU), `matInverse` (Gauss-Jordan). The matrix functions allocate their scratch space from the registered allocator.
