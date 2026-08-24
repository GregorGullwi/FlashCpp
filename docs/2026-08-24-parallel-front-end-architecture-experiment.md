# Parallel front-end architecture experiment

**Date:** 2026-08-24
**Status:** Next execution brief
**Authority:** Subordinate to
`docs/2026-08-24-front-end-rearchitecture-plan.md`

## Purpose

Determine whether parallel parsing, semantic queries, coroutine suspension, and
concurrent canonical type registration improve FlashCpp enough to justify their
complexity.

This experiment does not select the production scheduler or make experimental
sema authoritative. It produces evidence for an update to the main
rearchitecture plan.

## Questions

1. How much time does the current compiler spend in body parsing, semantic
   analysis, type registration, lookup, template work, constexpr evaluation,
   lowering, and code generation?
2. Does a memoized semantic query model remain competitive with direct
   single-threaded calls?
3. Do explicit worklist frames or C++20 stackless coroutines provide the better
   implementation of suspended semantic queries?
4. Can canonical type registration scale through sharding without making
   single-worker compilation slower or making type identity depend on
   scheduling?
5. Does parallel function-body parsing repay the sequential declaration
   outline pass and additional retained syntax memory?
6. Which workloads contain enough independent semantic work to produce useful
   end-to-end speedup?

## Constraints

- Keep the production compiler path unchanged while the experiment runs.
- Use the same query and semantic code in one-worker and multi-worker modes.
- Do not introduce stackful fibers. They preserve native stacks, increase
  suspended-task memory, and work against the stack-usage goals in the main
  plan.
- Do not use numeric `TypeId`, `DeclId`, worker index, discovery order, or hash
  iteration order in observable output.
- Do not hold locks, parser transactions, or worker-local scratch references
  across query suspension.
- Keep syntax and semantic results immutable after publication.
- Use structured failures for substitution and semantic probes. Failed work
  cannot publish declarations, types, diagnostics, or instantiations.
- Add no third-party benchmark or scheduling dependency.
- Record decisions by updating the main plan and implementation commits. Do not
  create a worklog or results-summary document.

Experiment boundary 1 may run at any time. Experiment boundaries 2 and 3 may
run during the core migration as test-only code. Experiment boundary 4 may not
begin before architecture boundaries 1 and 3A provide the parser-facing token
buffer, front-end context, arenas, stable identities, persistent scopes, and
canonical types. The vertical slice reuses those components rather than
creating parallel replacements.

This experiment does not lift the main plan's deferral of concurrent front-end
execution. That deferral ends only when the main plan records an outcome from
this experiment.

## Experiment boundary 1: establish the baseline

Add runtime-gated instrumentation to the shipping compiler.

Measure:

- wall and CPU time for preprocessing, declaration parsing, body parsing,
  semantic analysis, lookup, type registration, templates, constexpr,
  lowering, code generation, and object emission;
- exclusive and inclusive phase time and allocation bytes through a
  re-entrancy-aware scoped phase stack;
- function-body and template-work distributions, including median, high
  percentiles, and the largest item;
- type-registration requests, hits, misses, and repeated structural shapes;
- lookup and semantic dependency depth where current choke points permit it;
- semantic dependency-graph work and critical-path span per corpus input, with
  the derived maximum theoretical speedup at 2, 4, and 8 workers;
- allocation bytes and peak live bytes by lifetime domain;
- native stack-frame reports for parser, sema, template, and constexpr paths;
- peak resident memory.

Use aggregated counters and histograms by default. Detailed traces must be
explicitly enabled and bounded.

Inclusive-only phase numbers are not admissible for the stop decision. Extend
the existing `AllocationPhase` mechanism instead of introducing a second phase
system.

The fixed corpus must contain:

- a small compiler smoke input;
- the migration regression corpus;
- a generated translation unit with many independent functions;
- a generated deep chain of dependent `auto` return types;
- a template-heavy translation unit;
- selected standard-header probes;
- the fixed-seed generated target `parallel_frontend_large`, containing a
  corpus-derived mixture of independent bodies, source-order dependencies,
  templates, constexpr work, and canonical type requests;
- one large real translation unit if one is available in the repository.

Validate instrumentation overhead before accepting a baseline. Compare an
instrumented build with instrumentation disabled against an uninstrumented
build on the fixed corpus. If disabled-mode overhead exceeds 1%, reduce probe
granularity before measuring.

Stop this experiment unless `parallel_frontend_large` shows both:

- body parsing, sema, type registration, templates, and constexpr account for
  at least 40% of exclusive wall time;
- observed work and critical-path span imply at least a 2.0 times theoretical
  four-worker speedup for that combined phase set.

Record both numbers in the main plan even when the experiment stops. Keep
useful general telemetry, but do not add a scheduler to optimize work without a
measured parallelism bound.

## Experiment boundary 2: compare query execution models

Build a test-only semantic query engine with one common workload and result
model.

Compare:

1. direct single-threaded memoized calls;
2. explicit arena-owned query frames on a worklist;
3. C++20 `co_await` with coroutine frames allocated from a task arena.

Implementation 1 is the performance reference for query-framework overhead,
not a candidate suspension mechanism. Gates that require suspension, such as
constant native stack at 1,024 dependency levels and waiter cancellation, do
not disqualify it. Its chain result includes native stack usage. Retaining
direct execution means using the main plan's iterative worklists for
source-controlled depth, not retaining unbounded native recursion.

Each query cell has one owner and one immutable terminal result:

```text
Empty
    -> Computing(owner, dependencies, waiters)
    -> Ready(result)
    -> Failed(structured failure)
```

Workers never block waiting for a query owned by another worker. They suspend
the current query record and process unrelated ready work. Completion queues
waiters for later resumption; it never resumes them recursively while holding a
query lock.

Run these workloads through every implementation:

| Workload | Required behavior |
| --- | --- |
| Wide | Analyze at least 10,000 independent function roots. |
| Chain | Resolve at least 1,024 dependent deduced-return queries without native stack growth. |
| Diamond | Deduplicate a shared dependency reached through many paths. |
| Fan-out | Serve many simultaneous requests for one result with one computation. |
| Legal cycle | Represent recursive nominal record types without requesting premature completion. |
| Invalid cycle | Diagnose deduced-return, constexpr, and exact-instantiation cycles without deadlock. |
| Heavy tail | Keep workers useful when one root costs much more than the others. |
| Scheduler failure storm | Release repeated failed query work without leaked waiters, frames, or unbounded memory. |
| Cancellation | Wake or cancel every waiter deterministically after a terminal failure. |
| Source-ordered prefix | Resolve names through growing source-order declaration sets, redeclaration merging, and ADL at points of instantiation. |

Boundary 1 determines root count, dependency depth, fan-out, and per-query work
for the p50 and p95 corpus shapes. Run those measured shapes in addition to the
stress magnitudes above. Stress workloads prove correctness and scaling shape;
they are not eligible for the performance gates. Report the per-query work-size
crossover at which each suspension model overtakes direct calls.

Collect:

- one-worker overhead against direct calls;
- wall and CPU time at 1, 2, 4, and 8 workers where hardware permits;
- query count, cache hits, suspensions, resumptions, and worker migrations;
- ready-queue and query-cell contention;
- coroutine or explicit-frame allocation bytes;
- peak resident memory;
- native stack usage as logical dependency depth grows;
- deterministic result and diagnostic hashes.

The worklist and coroutine variants share the query table, hashing, task arena,
ready queue, scheduler, and non-type-erased dispatch strategy. Only the
suspension mechanism differs. Report frame bytes per query kind.

Time the query comparison under both MSVC and clang. Judge a candidate by its
worse toolchain result; a model that wins on only one supported toolchain is
not selected.

## Experiment boundary 3: compare canonical type interners

Use one immutable structural `TypeKey` and compare:

1. one mutex around the canonical type table;
2. hash-sharded tables with a lock per shard;
3. a shared table fronted by a small per-worker read-through cache that takes
   no lock on a cache hit.

Nominal record identity is `EntityId`. Record completion and layout are
separate semantic queries. Structural type interning covers pointers,
references, arrays, functions, member pointers, qualifiers, template
parameters, and specializations.

Typed arena storage must lease chunks to workers so common allocation does not
take the interner lock. A record becomes visible only after complete
construction and release publication.

Measure:

- requests per second under the recorded boundary-1 request trace, preserving
  key shapes, multiplicities, and hot-key skew;
- hit ratio and duplicate construction;
- time waiting for shards and arena chunks;
- one-worker overhead;
- scaling by worker count;
- bytes per canonical type;
- bytes lost to losing construction races;
- peak resident memory;
- equality and output stability under shuffled schedules.

Do not pursue a lock-free table unless sharded locking remains a measured
bottleneck. Correct publication and collision comparison matter more than
removing a short uncontended lock.

## Experiment boundary 4: build a vertical front-end slice

Build a non-authoritative slice using the winning query and interner candidates.
It must use real parser-facing tokens and front-end ownership rules rather than
only a synthetic dependency graph.

The slice covers:

- sequential declaration outlining with stable scopes and source positions;
- on-demand function-body parsing from immutable token ranges;
- builtin, pointer, function, and nominal record types;
- function calls;
- `auto` and `decltype(auto)` return deduction;
- lambdas returning native and record values;
- overload sets and ADL, including source-order-sensitive calls and dependent
  calls resolved at a point of instantiation;
- delayed complete-class constructs, including member-function bodies, default
  arguments, default member initializers, and a function try block;
- one function template with deduction and substitution failure;
- one class template specialization;
- legal incomplete-record cycles;
- invalid deduced-return cycles;
- concurrent requests for one function, type, or specialization.

The declaration outline is a real parser operation. It must understand
declarators, attributes, default arguments, trailing return types, constraints,
function try blocks, and complete-class contexts. It cannot guess body ranges
with a brace scan.

Body parsing owns its cursor, checkpoints, diagnostics, syntax allocation, and
local declarations. A parser may request declaration classification needed by
the grammar, but it cannot wait for record layout, constexpr evaluation, or
other full semantic results.

The substitution-failure case must roll back parser scratch allocation,
diagnostics, and declaration registration together. The synthetic scheduler
failure workload does not count as evidence for semantic transaction rollback.

Limit the number of live body pipelines. Parallel parsing cannot retain every
body AST merely to keep workers busy.

Run the slice with one and multiple workers. Compare language results against
reduced expected tests and a conforming reference compiler. The shipping
FlashCpp result is evidence, not ground truth.

## Benchmark method

- Use optimized builds with the same compiler flags for every variant.
- Run no unrelated build or test jobs during timing.
- Record CPU model, available cores, memory, operating system, compiler, and
  build configuration with the machine-readable benchmark output.
- Warm each workload before measurement.
- Run at least ten measured iterations and report median and high-percentile
  time rather than the best run.
- Report interquartile range or median absolute deviation with every median. A
  difference smaller than baseline run-to-run spread is "no measured
  difference" and cannot satisfy an improvement gate.
- Pin workers to homogeneous physical cores where the operating system permits
  it. Otherwise record core topology and repeat decision measurements on a
  homogeneous machine. Record the CPU frequency governor and turbo state.
- Measure one worker first. A parallel implementation that regresses ordinary
  compilation is not free.
- Run multi-worker variants with a fixed worker budget. Do not derive it from
  raw hardware thread count when an external build system owns concurrency.
- Verify output hashes and diagnostic ordering on every measured iteration.
- Provide seeded schedule perturbation for ready-queue order, worker start
  order, and optional delays at suspension and publication. Multi-worker
  correctness workloads run with at least 32 seeds.
- Run multi-worker correctness workloads under clang ThreadSanitizer and
  AddressSanitizer. Any report is a hard failure.
- Report allocator live bytes and resident memory separately. Record allocator
  arena configuration, including `MALLOC_ARENA_MAX` where applicable.

## Decision gates

The correctness gates are absolute:

- all variants produce the same semantic results;
- diagnostics and serialized output are identical across worker counts and
  shuffled schedules;
- cycle and cancellation tests terminate without deadlock;
- failed probes commit no state;
- 1,024 logical dependency levels run under the normal OS stack limit with
  nearly constant native stack usage;
- MSVC and clang builds remain warning-clean.

Commit threshold values before collecting the comparison results. Initial
thresholds are:

- no more than 5% regression in measured semantic-phase time for the chosen
  query model against equivalent direct calls in one-worker mode on
  corpus-derived workloads;
- at least 1.5 times measured semantic-phase speedup with four workers on a
  corpus-derived workload whose baseline proves sufficient independent work;
- at least 10% projected end-to-end improvement on
  `parallel_frontend_large`;
- no more than 3% projected end-to-end regression on fixed-corpus inputs
  without exploitable parallelism;
- no more than 1.5 times the one-worker peak resident memory at four workers for
  the vertical slice;
- no shared interner or ready-queue lock accounting for more than 10% of worker
  time.

Compute projected end-to-end effects by applying the measured phase speedup to
the boundary-1 exclusive phase shares and report the shares used for every
input. Quote an end-to-end result as measured only when a binary runs the full
shipping pipeline. Measure lock cost as accumulated acquisition wait divided
by total worker wall time.

If both worklist and coroutine models pass, prefer the smaller and easier model
unless the other wins by a repeatable margin in measured semantic-phase time
or memory under both toolchains.

Parallel body parsing passes only if the complete outline-plus-body pipeline
beats sequential parsing and stays within the memory gate. Faster isolated
body parsing is insufficient.

If every candidate for a dimension fails a committed gate, retain the
single-threaded or single-lock design for that dimension without further
selection.

## Outcomes

At the end of the experiment, update the main rearchitecture plan with one of
these decisions:

1. retain direct single-threaded semantic execution;
2. adopt memoized queries with explicit worklist frames;
3. adopt memoized queries with C++20 coroutine suspension;
4. adopt concurrent canonical type interning independently of parallel sema;
5. permit parallel body parsing for a measured class of translation units.

Prototype code lives under `tests/parallel_frontend_experiment/`. It is
excluded from `FlashCpp.vcxproj`, `FlashCppMSVC.vcxproj`, and shipping Makefile
targets, and it links no shipping entry point.

The pull request that records the outcome in the main plan deletes losing
prototypes and temporary detailed tracing. The experiment is incomplete while
a losing prototype remains buildable.

Boundary-1 instrumentation extends the architecture-boundary-0 telemetry from
the main plan rather than creating a second telemetry system. Retain it only
when disabled-mode overhead stays within the validated budget and it protects
a stated invariant. Otherwise delete it with the prototypes.

Machine-readable benchmark output is a build artifact and is not committed.
The main rearchitecture plan is the only narrative record of the decision.
Preserve benchmark workloads and correctness regressions that continue to
protect the selected architecture.
