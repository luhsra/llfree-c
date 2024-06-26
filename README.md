# LLFree: Lock- and Log-free Allocator

This repo contains the reimplementation of LLFree in C.

The two main design goals of this page frame allocator are multicore scalability and crash consistency.

**Related Projects**
- Rust-based LLFree: https://github.com/luhsra/llfree-rs
- Benchmarks: https://github.com/luhsra/llfree-bench
- Modified Linux: https://github.com/luhsra/llfree-linux
- Benchmark Module: https://github.com/luhsra/linux-alloc-bench


## Usage

The API is defined in the [llfree.h](src/llfree.h) header.
If you want to integrate LLFree into your project, you have to create your own [llfree_platform.h](std/llfree_platform.h) and [llfree_types.h](std/llfree_types.h) files.

> See https://github.com/luhsra/llfree-linux for more information

Debug build as static library
```sh
make
```

Release build as static library
```sh
make DEBUG=0
```

Running unit-tests
```sh
make test
# or tests that contain "bitfield" in their name
make test T=bitfield
```

## Architecture

<div style="text-align:center">

![LLFree Architecture](fig/llfree-arch.svg)

</div>

In general, LLFree is separated into a lower allocator, responsible for allocating pages of 4K up to 4M, and an upper allocator, designed to prevent memory sharing and fragmentation.

The persistent lower allocator can be found in [lower.c](src/lower.c).
Internally, this allocator has 512-bit-large bit fields at the lowest level.
They store which 4K pages are allocated.
The second level consists of 2M entries, one for each bit field. These entries contain a counter of free pages in the related bit field and a flag if the whole subtree is allocated as a 2M huge page.
These 2M entries are further grouped into trees with 8 entries (this can be defined at compile-time).

The [llfree.c](src/llfree.c) file contains the upper allocator, which depends on the lower allocator for the actual allocations and only manages the higher-level trees.
Its purpose is to improve performance by preventing memory sharing and fragmentation.
To do so, it reserves chunks of the memory for cores.
It is completely volatile and has to be rebuilt on boot.

## Publication

**LLFree: Scalable and Optionally-Persistent Page-Frame Allocation**<br>
Lars Wrenger, Florian Rommel, Alexander Halbuer, Christian Dietrich, Daniel Lohmann<br>
In: 2023 USENIX Annual Technical Conference (USENIX '23); USENIX Association
