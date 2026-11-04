# Encodings

The encoding modules are in `std/encoding`. Each encoder writes into a caller buffer and returns the length; each decoder returns the byte count, or `-1` for input that is not valid.

| Encoding | Functions | Specification |
|---|---|---|
| Hexadecimal | `hexEncode`, `hexDecode`, `base16Encode`, `base16Decode` | RFC 4648, section 8 |
| Base64 | `base64Encode`, `base64Decode` | RFC 4648, section 4 |
| Base64url | `base64UrlEncode`, `base64UrlDecode` | RFC 4648, section 5 |
| Base32 | `base32Encode`, `base32Decode` | RFC 4648, section 6 (padded, canonical decoding) |
| Base32hex | `base32HexEncode`, `base32HexDecode` | RFC 4648, section 7 |
| Base85 | `base85Encode`, `base85Decode` | RFC 1924 (no padding; the IPv6 alphabet) |
| ascii85 | `ascii85Encode`, `ascii85Decode` | Adobe PostScript Language Reference, third edition, section 3.3.2 |
| PEM | `pemNext` (read one block), `pemEncode(label, llen, der, n)` (write one block, 64-character lines) | RFC 7468 |
| Base58 (Bitcoin alphabet) | `base58Encode`, `base58Decode` | Bitcoin Base58Check alphabet; leading zero bytes map to `1` |
| CBOR (encode only) | `cborHead`, `cborUint`, `cborInt`, `cborString`, `cborArrayHead`, `cborMapHead`, `cborBool`, `cborNull` | RFC 8949 (shortest-form arguments, section 4.2.1) |
| Protocol Buffers wire format | `pbVarint`, `pbReadVarint`, `pbZigzag`, `pbUnzigzag`, `pbTag`, `pbFixed32`, `pbFixed64` | https://protobuf.dev encoding guide (varints, zigzag, wire types) |
| JSON (top-level members) | `jsonHas`, `jsonInt`, `jsonBool`, `jsonString` | RFC 8259 |

Base32 decoding is canonical: the padding must match the length, and the unused bits of the last character must be zero.
Base85 (RFC 1924) and ascii85 (Adobe) are different formats. They share only the 4-byte grouping.

## UTF-8 and CSV

| Module | Functions | Specification |
|---|---|---|
| `std/encoding/utf8` | `utf8Decode`, `utf8Valid`, `utf8Encode`, `utf8EncodedLen`, `utf8SeqLen` | RFC 3629 (no overlong forms, no surrogates, at most U+10FFFF) |
| `std/encoding/csv` | `csvRecord(text, n, pos)` returns the fields of one record as `Vec<String>` | RFC 4180 (quoted fields, doubled quotes, CRLF or LF) |

A CSV record that spans lines needs the caller to join the lines when a quote is still open.

## JSON (full grammar)

`std/encoding/jsonfull` validates and navigates JSON without building a tree (RFC 8259):

| Function | Purpose |
|---|---|
| `jsonValid(text, n)` | the whole input is exactly one JSON value |
| `jsonSkipRaw(text, n, pos)` | the raw text of the value at `pos`, and the position after it |
| `jsonPointerGet(text, n, ptr, plen)` | the raw value at a JSON Pointer (RFC 6901); an empty view when absent |
| `jsonStringDecode(raw)` | decodes the escapes of a string body, with `\uXXXX` and surrogate pairs to UTF-8 |
| `JsonWriter` | writes objects, arrays, keys, strings, integers, booleans and null, with escaping |

Nesting deeper than 512 levels is rejected. The validator checks the escapes; control characters inside strings are rejected.

## Base58, CBOR and Protocol Buffers

`base58Encode` takes at most 512 input bytes; `base58Decode` accepts an output of up to 800 bytes. The CBOR functions write one item each: the caller writes the items of an array or map after its header. The Protocol Buffers functions are the wire primitives (varint, zigzag, tag, fixed-width values); there is no message schema or code generator.

## Internationalized names and more formats

| Module | Functions | Specification |
|---|---|---|
| `std/encoding/punycode` | `punyEncode`, `punyDecode` | RFC 3492, section 6 (code points in, ASCII out) |
| `std/encoding/idna` | `idnaLabelToAscii` | RFC 3490, section 5 (the `xn--` form of one label) |
| `std/encoding/cborread` | `cborReadHead`, `cborSkip` | RFC 8949, section 3 and section 5 (structure check and skipping; nesting limit 64) |
| `std/encoding/msgpack` | `msgpackUint`, `msgpackInt`, `msgpackStr`, `msgpackHead`, `msgpackBool`, `msgpackNil`, `msgpackFloat64`, `msgpackReadUint`, `msgpackSkip` | MessagePack format specification (msgpack.org; no RFC) |

`idnaLabelToAscii` takes a label already in the form the caller wants: nameprep (RFC 3491: case folding and normalization) is not run.
The CBOR reader checks the structure and skips items; it does not build values. Indefinite-length strings and containers are accepted.

## TOML (`std/encoding/toml`)

| Function | Specification |
|---|---|
| `tomlValid(text, n)` | TOML 1.0, https://toml.io/en/v1.0.0: the whole document is checked against the grammar |
| `tomlGet(text, n, key)` returns the raw value text | a dotted key such as `"owner.name"`, followed through table headers; an empty view when the key is absent |

The module builds no tree. The lookup returns the value as written (a view into the document), so a caller reads strings and numbers with its own code. Checked against the example document of the specification and against invalid inputs (leading zeros, misplaced underscores, a surrogate escape, an unterminated string, a date that does not exist).
