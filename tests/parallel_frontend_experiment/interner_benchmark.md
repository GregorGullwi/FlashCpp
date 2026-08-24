# Canonical type interner benchmark

`interner_benchmark.cpp` is a standalone C++20 experiment for architecture
boundary 3. It compares a single mutex table, 32 hash shards, and a shared
table with a 64-entry per-worker read-through cache. The immutable `TypeKey`
contains builtin, pointer, reference, array, function, member-pointer,
qualified, template-parameter, and specialization shapes.

Build with either supported toolchain (from the repository root):

```powershell
& 'C:\Program Files\LLVM\bin\clang++.exe' -std=c++20 -O2 -pthread `
  tests/parallel_frontend_experiment/interner_benchmark.cpp `
  -o tests/parallel_frontend_experiment/interner_benchmark.exe
```

For MSVC, run the same source from a VS x64 developer prompt:

```text
cl /nologo /std:c++20 /O2 /W4 /EHsc /permissive- ^
  tests\parallel_frontend_experiment\interner_benchmark.cpp ^
  /Fe:tests\parallel_frontend_experiment\interner_benchmark_msvc.exe
```

The default run emits JSON sample and summary lines for 10 measured iterations (after two
warmups) at 1, 2, 4, and 8 workers. Use `--keys=N`, `--requests=N`,
`--iterations=N`, `--warmups=N`, `--workers=1,2,4`, and `--seed=N` to select a
decision run. `--publication-delay` adds seeded publication perturbations.
Redirect stdout to retain machine-readable results; metadata is printed to
stderr. Every result includes request rate, hit/cache-hit ratio, duplicate
construction and losing-race bytes, lock/arena wait time, canonical and arena
bytes, peak RSS, and an output stability hash. A result is marked stable only
when canonical equality, insertion counts, and the schedule-independent hash
all agree.
