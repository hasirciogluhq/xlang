# Compression

The compression modules are in `std/compress`: decoders in `inflate.xl` and encoders in `deflate.xl`.

| Function | Specification |
|---|---|
| `inflate(src, n, out, cap)` | DEFLATE, RFC 1951: stored, fixed-Huffman and dynamic-Huffman blocks |
| `gunzip(src, n, out, cap)` | gzip, RFC 1952: header flags, CRC-32 and ISIZE checked |
| `zlibDecompress(src, n, out, cap)` | zlib, RFC 1950: header check, Adler-32 checked; a preset dictionary is refused |

The output goes into a caller buffer of `cap` bytes. A stream that would produce more than `cap` bytes, or that is invalid, returns -1.
The decoders were checked against files made with the system `gzip -n`: a fixed-Huffman block (short text), a dynamic-Huffman block (about 9 KB of numbers, gzip -9), and a stored block (3000 random bytes, gzip -1). Each gzip result is also checked by its CRC-32 and length, and a zlib wrapper built from the same DEFLATE body is checked by an independently computed Adler-32.

## Compression (`std/compress/deflate`)

| Function | Specification |
|---|---|
| `deflateFixed(src, n, out, cap)` | RFC 1951: one fixed-Huffman block with greedy LZ77 matches (32 KiB window) |
| `deflateStored(src, n, out, cap)` | RFC 1951 section 3.2.4: stored blocks, no compression |
| `zlibCompress(src, n, out, cap)` | RFC 1950: header 0x78 0x9c, DEFLATE, Adler-32 |
| `gzipCompress(src, n, out, cap)` | RFC 1952: header with mtime 0 and no name, DEFLATE, CRC-32 and ISIZE |

Each function returns the byte count, or -1 when the output does not fit in `cap`. The output was checked with `gzip -t` and a gzip round trip, with perl's Compress::Zlib (zlib), and with IO::Uncompress::RawInflate (raw fixed and stored streams); empty input is a valid gzip member.
The writer does not produce dynamic-Huffman blocks, so the compressed size is larger than zlib's at its default level. The decoders in this module read every block type.
