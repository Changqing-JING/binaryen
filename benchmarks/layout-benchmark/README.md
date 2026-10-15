# Function Placement Benchmark

## Baseline and Solution

The Rust workload combines Markdown parsing, regex, and compression. The baseline
keeps the original function order after optimization. The solution estimates
execution frequencies from Wasm IR and clusters functions using HFSort, without
PGO.

Run from this directory:

```bash
../../build/bin/wasm-opt -O3 -g large_bench_orig.wasm -o baseline.wasm
../../build/bin/wasm-opt -g baseline.wasm \
  --reorder-functions-by-call-graph=hfsort-estimated -o solution.wasm
```

## Results

### Native AOT

https://github.com/wasm-ecosystem/wasm-compiler

Native AOT execution under Cachegrind, with a 32 KiB, 8-way I1 cache and 64-byte
lines. Compilation is excluded; measurement covers module start and `_start`.
Three trials per layout produced identical counts and returned zero.

| Metric                 |    Baseline |    Solution |
| ---------------------- | ----------: | ----------: |
| Instruction references | 234,142,715 | 234,142,715 |
| I1 misses              |     895,595 |     880,339 |
| LLi misses             |      16,442 |      16,422 |
| D1 misses              |     691,107 |     690,802 |
| LLd misses             |      56,735 |      56,737 |

**1.70% fewer I1 misses than baseline.** Both native binaries are 4,120,772 bytes;
canonicalized Wasm is byte-identical, confirming only function order changed.
This does not establish a runtime speedup.

### wasm-interp

WABT `wasm-interp` 1.0.41, running `_start` once per process:

```bash
valgrind --tool=cachegrind --cache-sim=yes \
  --I1=32768,8,64 --D1=32768,8,64 --LL=8388608,16,64 \
  wasm-interp solution.wasm --run-export=_start
```

Repeat with `baseline.wasm`. These counts include loading, validation, and
interpretation; they are not directly comparable to the native execution-only
counts. I1 measures interpreter code, not native code generated from Wasm.

| Metric                 |       Baseline |       Solution |
| ---------------------- | -------------: | -------------: |
| Instruction references | 22,758,297,578 | 22,758,307,735 |
| I1 misses              |        719,073 |        721,800 |
| LLi misses             |          6,028 |          6,023 |
| D1 misses              |      3,195,574 |      3,120,025 |
| LLd misses             |        380,471 |        375,964 |

Three trials produced identical counts and returned zero: **2.36% fewer D1
misses**, 1.18% fewer LLd misses, and 0.38% more I1 misses.

## Key LLVM Concepts

- [BranchProbabilityInfo](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/BranchProbabilityInfo.cpp): loop, branch-hint, comparison, and cold-path weights.
- [BlockFrequencyInfoImpl](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/BlockFrequencyInfoImpl.cpp): bottom-up loop packaging and frequency scaling from exit probability.
- [SyntheticCountsUtils (LLVM 14)](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/SyntheticCountsUtils.cpp): caller-first SCC propagation with one simultaneous internal update to bound recursion.
- [LLD CallGraphSort](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/lld/ELF/CallGraphSort.cpp): density-based call-chain clustering with merge limits.

This is an IR-level adaptation: sizes use expression counts rather than native
bytes, and indirect-call frequencies are not modeled.
