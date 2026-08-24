# Parallel front-end experiment benchmarks

This directory contains test-only benchmark programs and a small common
runner. Nothing here is part of a FlashCpp shipping target or project file.
The runner has no third-party dependencies and is intended to be linked into
the query, scheduler, interner, and vertical-slice experiments.

## Runner contract

Include `benchmark_harness.h` and call:

```cpp
using namespace flash::parallel_frontend_benchmark;

int main(int argc, const char* const* argv) {
    return runBenchmark(argc, argv, [](BenchmarkContext& context) {
        // Build/reset the workload from context.seed. Do not retain pointers
        // to context and do not print to stdout from the callback.
        const std::uint64_t hash = run_workload(context);
        return BenchmarkResult{hash, operation_count, allocated_bytes};
    });
}
```

`result_hash` is checked across all measured iterations. It must represent the
observable semantic result and must not depend on addresses, worker IDs, or
unordered iteration order. `operations` and `bytes` are optional counters for
the workload-specific report. `BenchmarkContext::nextRandom()` is a deterministic
SplitMix64 stream derived from `--seed` and the iteration number.

The command-line runner accepts `--benchmark`, `--variant`, `--input`,
`--workers`, `--seed`, `--warmup`, `--iterations`, `--output`, and repeatable
`--config KEY=VALUE`. Measured iterations default to 10 and values below 10
are rejected. `--output -` writes to stdout; another path receives JSONL.

Each output consists of one `metadata` record, warmup and measured `sample`
records, and one `summary` record. The metadata records operating system,
architecture, CPU model, logical CPU count, compiler family/version, and all
benchmark settings. Samples include wall time, process CPU time when available,
peak resident set size when available, the deterministic seed, and counters.
Summaries include median, p95, IQR, and MAD for wall and CPU time.

## Local smoke build

The smoke driver exercises the runner without touching the compiler projects.
For example, from a Visual Studio or LLVM developer shell:

```text
clang++ -std=c++20 -O2 -Wall -Wextra -Werror \
  benchmark_smoke.cpp benchmark_harness.cpp -o benchmark_smoke
benchmark_smoke --benchmark smoke --variant direct --iterations 10 --seed 0x1234
```

On Windows, `clang-cl /std:c++20 /O2 /W4 /EHsc benchmark_smoke.cpp
benchmark_harness.cpp /Fe:benchmark_smoke.exe` is equivalent. The generated
JSONL is a build artifact and should not be committed.

## Build all comparisons

From the repository root, build warning-clean optimized executables with both
supported Windows toolchains:

```powershell
pwsh tests/parallel_frontend_experiment/build_benchmarks.ps1 -Toolchain all
```

The script writes to the system temporary directory unless `-OutputDirectory`
is supplied. See `query_README.md` and `interner_benchmark.md` for workload and
variant options.

## Shipping baseline

Generate and time the fixed-seed `parallel_frontend_large` translation unit
without retaining its source or object artifact:

```powershell
pwsh tests/parallel_frontend_experiment/run_shipping_baseline.ps1 `
  -Iterations 10 -WarmupIterations 2 -FunctionCount 1000
```

Use `-OutputPath` to retain the JSONL measurements as an untracked build
artifact. The driver refuses fewer than ten measured iterations.
