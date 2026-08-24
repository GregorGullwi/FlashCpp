# Boundary 2 query benchmark

`query_benchmark.cpp` is a standalone C++20 experiment. It has one immutable
query graph and runs it through direct memoized calls, arena-owned explicit
frames, and C++20 coroutine frames. The graph contains the ten Boundary 2
workloads: wide (10,000 roots), 1,025-deep chain, diamond, fan-out, legal and
invalid cycles, heavy tail, failure storm, cancellation, and source-ordered
prefix.

Build with clang (the same source also builds with MSVC):

```powershell
clang++ -std=c++20 -O2 -Wall -Wextra -Wshadow -Werror `
  tests/parallel_frontend_experiment/query_benchmark.cpp `
  -o tests/parallel_frontend_experiment/query_benchmark.exe
```

Run a quick smoke pass:

```powershell
tests/parallel_frontend_experiment/query_benchmark.exe --quick
```

Useful filters are `--workload=chain`, `--variant=coroutine`,
`--iterations=10`, `--warmups=2`, `--seed=123`, `--work-multiplier=64`, and
`--workers=4`. Vary the work multiplier to find the suspension/scheduling
crossover for the measured corpus query size. Each `RESULT` line is machine-readable;
`SUMMARY` reports median, IQR, p95, result hash, and diagnostic hash. Worklist
and coroutine variants use the common synchronized ready queue for real
multi-worker execution on `wide`, `heavy_tail`, `diamond`, and `fanout`.
The seeded queue perturbation reports migrations and queue contention while
terminal hashes remain source-order deterministic.

Dependency-depth and cycle-sensitive workloads (`chain`, `legal_cycle`,
`invalid_cycle`, `failure_storm`, `cancellation`, and `source_order_prefix`)
use one execution lane even when a larger worker budget is requested. Direct
execution remains the memoized single-threaded reference. `execution_lanes`
in each result row makes these fallbacks explicit.

The result and diagnostic hashes must agree across all three variants for each
workload. Frame bytes and native depth expose the stack/suspension tradeoff;
waiter, queue, cache-hit, failed-query, and cycle counters expose behavior on
the stress workloads.
