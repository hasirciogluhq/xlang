# archive-tool

Compresses and decompresses files with DEFLATE in its gzip (RFC 1952) and zlib (RFC 1950) wrappers.

```
xlang build -o archive-tool examples/archive-tool/main.xl
./archive-tool gzip notes.txt notes.txt.gz
./archive-tool gunzip notes.txt.gz notes.out
./archive-tool zlib notes.txt notes.zz
./archive-tool unzlib notes.zz notes.out
```

The writer produces fixed-Huffman blocks, so its output is valid but larger than zlib's default. The decoders read every
block type and check the CRC-32 or Adler-32 of the wrapper.
