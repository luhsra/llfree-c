# LLFree: Lock- and Log-free Allocator

Dieses Ropo enthält die ReImplementierung des LLFree allokators in C.

**Related Projects**
- LLFree:     https://github.com/luhsra/llfree-rs
- Benchmarks: https://scm.sra.uni-hannover.de/research/llc-bench

## Usage

```sh
# Release build
# In folder llc
make all

# Running unit-tests
# In folder llc
make test

# Running LLFree tests with LLC
cargo test -- --test-threads 1

# Running a benchmark
cargo perf <benchmark> -- <args>
# For example print help for the `bench` benchmark
cargo perf bench -- -h
```
