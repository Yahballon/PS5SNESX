# Third-party code vendored for the PS5 port

The PS5 payload SDK has no zlib or zstd, and PCSX2 takes both from its dependency build rather than
`3rdparty/`. The port needs them for CHD images (vk-285-108): `3rdparty/libchdr` decompresses hunks with
zlib (the `zlib` and `cdzl` codecs), zstd (`zstd`, `cdzs`), LZMA (`lzma`, `cdlz`; from `3rdparty/lzma`),
Huffman and FLAC (in libchdr). CSO and ZSO images (vk-285-113) need zlib's inflate for a CSO's blocks and
LZ4 for a ZSO's. Only the decompression sources are here (LZ4's single `lz4.c` holds both directions), byte-identical
to the releases:

| Folder | Release | Tarball SHA-256 | License |
|---|---|---|---|
| `zlib/` | zlib 1.3.1 (`zlib-1.3.1.tar.gz`, zlib.net) | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | zlib (`zlib/LICENSE`) |
| `zstd/` | zstd 1.5.6 (`zstd-1.5.6.tar.gz`, github.com/facebook/zstd releases) | `8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1` | BSD (`zstd/LICENSE`), used under BSD; also GPLv2 (`zstd/COPYING`) |
| `lz4/` | LZ4 1.10.0 (`lz4-1.10.0.tar.gz`, github.com/lz4/lz4 releases) | `537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b` | BSD 2-Clause (`lz4/LICENSE`, the library's) |

- **zlib:** inflate only: `adler32.c crc32.c inffast.c inflate.c inftrees.c zutil.c` and their headers
  (`gzguts.h` because `zutil.c` includes it). No deflate, no gz* file functions.
- **zstd:** `lib/zstd.h`, `lib/zstd_errors.h`, `lib/common/` without `pool.*` and `threading.*`, and
  `lib/decompress/` without `huf_decompress_amd64.S` (built with `ZSTD_DISABLE_ASM`). No compression,
  dictionary builder or legacy formats.

- **lz4:** `lib/lz4.c`, `lib/lz4.h` and `lib/LICENSE` of the release (the library is BSD 2-Clause; the release's
  programs, GPLv2, are not here).

`ps5/coreorbis/Makefile.vk` builds them (`CHD_CSRCS`, `LZ4_CSRCS`).
