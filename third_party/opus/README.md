# Opus sources

This directory contains unmodified portable C sources and headers from
[libopus 1.6.1](https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz).
The release archive SHA-256 is
`6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1`.
`COPYING` and the `LICENSE*` files retain the upstream copyright and
patent terms. Binary distributions include these notices.

`sources.mk` selects the upstream `OPUS_SOURCES`, `CELT_SOURCES`,
`SILK_SOURCES` and `SILK_SOURCES_FIXED` lists. The headers come from
`include`, `src`, `celt`, `silk` and `silk/fixed`. Architecture assembly,
SIMD implementations, float analysis and neural models are excluded.
The linker discards unused decoder and multichannel functions.

The TikuOS configuration and allocation hooks live in `services/audio`.
They select fixed-point arithmetic and the upstream 120000-byte checked
scratch buffer. Encoder state and scratch come from caller-owned arenas;
heap allocation hooks return null. Shared scratch pointers allow one
foreground encoder. Hardening failures return through the wrapper's
error path. Upstream source comments retain their original style.
