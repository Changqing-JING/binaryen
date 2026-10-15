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

## Key LLVM Concepts

- [BranchProbabilityInfo](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/BranchProbabilityInfo.cpp): loop, branch-hint, comparison, and cold-path weights.
- [BlockFrequencyInfoImpl](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/BlockFrequencyInfoImpl.cpp): bottom-up loop packaging and frequency scaling from exit probability.
- [SyntheticCountsUtils (LLVM 14)](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/llvm/lib/Analysis/SyntheticCountsUtils.cpp): caller-first SCC propagation with one simultaneous internal update to bound recursion.
- [LLD CallGraphSort](https://github.com/llvm/llvm-project/blob/llvmorg-14.0.6/lld/ELF/CallGraphSort.cpp): density-based call-chain clustering with merge limits.

This is an IR-level adaptation: sizes use expression counts rather than native
bytes, and indirect-call frequencies are not modeled.
