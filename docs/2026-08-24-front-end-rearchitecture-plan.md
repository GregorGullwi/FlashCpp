# Front-end rearchitecture plan

**Date:** 2026-08-24
**Status:** Revised after independent Claude Opus architecture review

## Decision

FlashCpp will keep the existing repository, backend, constexpr evaluator,
lexer, preprocessor directive handling, support containers, and regression
corpus. The object writers will be kept and extended, but their monolithic
section model will be replaced where multi-translation-unit support requires
it.

The parser-owned semantic and template architecture will be replaced behind
explicit facades. The replacement gets a new semantic AST model and scoped
arena. Stable IDs will not be bolted onto the existing pointer-keyed AST as if
that solved its ownership problems. The old AST will be bridged during
migration and deleted from normalized front-end paths over time.

This is not a greenfield compiler rewrite. It is also not a continuation of
the current parser-to-sema annotation migration.

## Target pipeline

```text
parser-facing token buffer
    -> syntax parser
    -> syntax AST
    -> persistent declarations, entities, and scopes
    -> semantic analysis and template instantiation
    -> normalized semantic AST
    -> typed AST-to-IR lowering
    -> existing machine-code and object-file backend
```

The target ownership model is:

- The lexer and current preprocessing path feed an indexed parser-facing
  `TokenBuffer`. Tokens have stable indices, source locations, and macro
  provenance while retained.
- The parser owns syntax and source fidelity.
- Persistent scopes own lexical context.
- A `DeclarationBuilder` owned by `FrontendContext` creates declarations,
  matches redeclarations, creates or finds entities, and applies merge rules.
- Declaration entities carry canonical identity, redeclaration chains,
  linkage, and definition state.
- Sema owns lookup, expression typing, conversions, overload resolution,
  access control, constant-expression requirements, and diagnostics.
- The template engine operates on AST patterns and semantic identities. It
  does not restore lexer positions or mutate parser state.
- Constexpr consumes sema-owned types and declarations. It requests
  instantiation through the template-engine facade.
- AST-to-IR consumes normalized semantic nodes. It cannot perform lookup,
  overload resolution, access checks, template materialization, or writes into
  sema.
- The backend consumes typed IR and target ABI information.

## Architectural invariants

1. `ScopeId`, `DeclId`, `EntityId`, `ExprId`, `TypeId`, and `TemplateDeclId`
   are strong, stable identities owned by a compilation context.
2. `DeclId` identifies one source declaration. `EntityId` identifies the
   canonical C++ entity across redeclarations and definitions.
3. Canonical types are recursive structural nodes. Identity is independent of
   parse order, spelling, parser stacks, arena addresses, and translation-unit
   layout.
4. Template parameters use depth and index. Names remain only for diagnostics
   and source printing.
5. One semantic fact has one authority. Temporary shadow comparison is
   allowed, but dual writes are not.
6. Syntax nodes contain no mangled names or backend state.
7. Template bodies are AST patterns, not saved lexer positions.
8. Failed tentative parsing and failed substitution commit no declarations,
   entities, types, diagnostics, instantiations, or AST nodes.
9. Lookup returns complete declaration sets and represents ambiguity directly.
10. Overload resolution records complete implicit conversion sequences.
11. ELF mangling conforms to the Itanium C++ ABI. COFF mangling conforms to
    the MSVC ABI. Mangled output is a deterministic pure function of the
    canonical entity, canonical types, and target ABI. A proprietary hash is
    not acceptable.
12. Normalized codegen paths fail with `InternalError` when semantic facts are
    absent. They do not recover by spelling, mangled name, arity, or parser
    queries.
13. No standard-library or vendor spelling may affect general language
    semantics.
14. Object format, calling convention, exception ABI, data model, and target
    architecture are separate target properties.
15. Stable addresses are an arena implementation property, not semantic
    identity. Cross-layer APIs use IDs rather than pointers.
16. `StringHandle` identifies interned spelling only. Its numeric value cannot
    identify declarations, entities, types, templates, specializations, or
    emitted ABI names.

## Memory and allocation policy

Keep the existing allocation-oriented primitives, but narrow their roles.

### `InlineVector`

- Use it for collections with a measured small-size distribution.
- Choose inline capacity from observed spill rates and record-size impact, not
  convenience.
- Avoid large inline capacities in hot AST, declaration, type, and expression
  records. Replicated unused inline storage can cost more than occasional
  allocation.
- Remember that inline storage remains part of the object after a spill. As a
  first-order estimate, compare `sizeof(container metadata) + N * sizeof(T)`
  against the metadata-only heap container, weighted by the measured spill
  rate.
- Attribute spill telemetry to a named container family or call site. Template
  instantiation alone is not enough when the same `InlineVector<T, N>` appears
  in unrelated records.

### `ChunkedVector<T>`

- Use typed `ChunkedVector<T>` arenas for long-lived objects that need stable
  addresses.
- Expose stable IDs across subsystem boundaries. Do not expose the stable
  address as identity.
- Pin each arena for its lifetime. Arena objects must be non-copyable and
  non-movable, or owned behind stable indirection. Existing `ChunkedVector<T>`
  move and copy operations do not preserve element addresses across allocator
  changes and must not be relied upon.
- Treat the `ChunkSize` template parameter as an element count, not bytes.
  Every new arena must pass an explicit measured element count rather than
  using the current size-dependent default.
- Keep hot records compact and move rare payloads into typed cold-storage
  arenas indexed by ID.
- Typed `ChunkedVector<T>` storage owns and destroys its elements. Do not also
  register their destructors.

### `ChunkedAnyVector`

- Treat the existing global `ChunkedAnyVector` as a legacy AST bridge only.
- Do not allocate new semantic declarations, entities, canonical types, or
  normalized expressions in it.
- Do not extend raw-pointer-keyed state around it.
- Enforce the restriction at `ASTNode::emplace_node` with a compile-time legacy
  node allow-list or an equivalent type-level guard.

### `StringHandle`

- Use `StringHandle` for interned source spelling, lookup keys, and diagnostics.
- Resolve spelling to `DeclId`, `EntityId`, `TypeId`, or `TemplateDeclId`
  before semantic identity is required.
- Never hash or serialize the numeric handle into mangling, caches shared
  across translation units in future implementations, or persistent artifacts.

The shipping compiler already violates this rule: `StringHandle` and global
`TypeIndex` slot values contribute to template argument hashes that feed the
proprietary `$hash` symbol form. Architecture boundary 3B owns deletion of that
behavior.

### Lifetime domains

`FrontendContext` owns separate allocation domains:

1. a syntax arena for parsed source structure;
2. a semantic arena for declarations, entities, canonical types, and
   normalized expressions;
3. monotonic scratch storage for tentative parsing and substitution probes;
4. an IR arena whose lifetime ends after object emission.

Typed `ChunkedVector<T>` arenas destroy their elements normally. Monotonic
scratch storage and the legacy `ChunkedAnyVector` bridge need explicit
destructor handling for objects with `std::string`, `std::vector`, or other
owning members, or a representation whose payload lives in a destructible side
arena. Releasing raw arena bytes alone does not release nested heap
allocations.

New cold storage is ID-indexed. Existing pointer-indexed `TypeInfo` cold arenas
remain behind the legacy bridge until architecture boundary 11 converts or
removes them.

Do not optimize the current `TypeInfo` layout further unless the change either
survives the recursive canonical type model or is required to keep the shipping
compiler usable during migration. Do not swap the `gTypeInfo` container as a
standalone optimization.

## Stack and recursion policy

Unbounded language recursion must not map directly to native C++ call depth.

- Keep recursive descent where grammar composition is shallow and bounded.
  Use loops or explicit parse-frame stacks for parser nesting controlled by
  source input.
- Process template instantiation through an explicit worklist of small
  `InstantiationFrame` records stored in a typed arena.
- Store frame parents, declarations, arguments, environments, points of
  instantiation, and diagnostics by ID rather than copying AST or container
  values into recursive calls.
- Use iterative traversal for substitution, constraints, lookup, and
  expression operations whose depth is controlled by source code.
- Do not place large `InlineVector` instances, fixed arrays, AST values, or
  template environments in recursive-path local variables.
- Do not rely on tail-call optimization.
- Measure compiler frames with the platform's stack-usage reports and maintain
  a baseline-driven frame-size warning threshold.
- Run deep template, constraint, and constexpr probes under the normal
  operating-system stack limit. Do not use the current 64 MiB stack increase
  to validate the new path.

The target is support for at least 1024 levels of logical template
instantiation with nearly constant native stack usage.

## Parser token and checkpoint policy

The replacement parser interface uses an indexed token buffer, not a
destructive token queue. Consuming a token advances a cursor. Local lookahead
and tentative parsing copy the cursor rather than removing and reinserting
tokens.

- Use stable `TokenIndex` values instead of raw lexer positions.
- Keep the buffer append-only while a parser checkpoint may refer to it.
  Chunked storage may release consumed prefixes after no cursor, checkpoint,
  syntax node, or diagnostic refers to those tokens.
- A parser checkpoint contains the token cursor, scratch-arena mark,
  diagnostic mark, and transactional declaration or registry marks. Restoring
  only the cursor is not rollback.
- Limit checkpoints to local grammar ambiguity. Template instantiation,
  delayed member handling, and semantic retries operate on AST patterns and
  semantic IDs, never by restoring an old token cursor.
- Parse source-controlled nesting with loops or small arena-owned parse frames
  where practical. Retain ordinary recursive descent for grammar structure
  whose maximum native depth is bounded.
- Migrated template syntax must support at least 1024 levels of nesting under
  the normal OS stack limit without native stack growth. A configurable logical
  complexity budget may reject pathological input, but native recursion depth
  is not a user-visible language limit.

This policy does not require replacing the current preprocessing subsystem.
The token buffer is the parser-facing boundary and can initially adapt the
existing lexer and preprocessing path.

## Concurrency readiness

Concurrent front-end execution is deferred. Do not select a lane API, job
system, dependency scheduler, or worker topology during the core migration.
Preserve the properties that would be expensive to retrofit:

- no observable output depends on numeric ID values, string-intern order,
  discovery order, worker count, or unordered-container iteration;
- syntax, semantic, scratch, and IR allocation ownership can be separated
  without raw pointers crossing ownership boundaries;
- semantic results can be frozen and consumed without mutation;
- diagnostics and emitted fragments have deterministic source or symbol keys;
- semantic recursion, constexpr, deduplication, and emission state is owned by
  explicit compilation objects rather than hidden process-global or
  `thread_local` state;
- future parallel and single-worker execution must use the same semantic path
  and produce byte-identical diagnostics and objects.

Before semantic-AST lowering, close the emission set. This includes implicit
special members, vtables, VTTs, thunks, RTTI, static-initialization functions,
string literals, canonical types, and mangled names needed by lowering.
Discovering semantic work during lowering is an internal error.

Do not restructure the parser around speculative concurrency. The declaration
outline remains a real source-ordered declaration parse, not a brace scanner.
If profiling later justifies intra-translation-unit parallelism, evaluate
frozen per-function lowering and emission before parser, template, constraint,
or constexpr parallelism. Measure end-to-end phase time, work-size
distribution, serial tails, peak memory, and interaction with external build
parallelism before choosing an executor.

Before selecting a production query, coroutine, interner, or parallel parsing
model, execute
`docs/2026-08-24-parallel-front-end-architecture-experiment.md`. Its correctness
and performance gates decide whether parallel sema or parsing enters this plan.
The experiment is test-only and does not lift the concurrency deferral.
Its vertical slice cannot begin before architecture boundaries 1 and 3A.

### Initial parallel-experiment evidence

The test-only boundary-2 and boundary-3 comparisons now live under
`tests/parallel_frontend_experiment/`. On an AMD Ryzen 7 3700X using MSVC
19.44 and clang 20.1, 32 seeded query schedules produced identical terminal
and diagnostic hashes at one through eight workers. The same workloads passed
clang AddressSanitizer. ThreadSanitizer remains required on a supported host.

The fixed-seed 1,000-function `parallel_frontend_large` shipping baseline had
an 8,905 ms median with a 55 ms interquartile range. Coarse exclusive phases
were 46.1% parsing and 9.4% post-parse semantic analysis. Instrumented template
instantiation was 215 ms, or 5.0% of parsing; the current build did not expose
parser body-time or semantic critical-path span. These numbers therefore do
not adjudicate boundary 1's 40% and theoretical-speedup stop gate.

On the synthetic wide-query crossover, four-worker coroutine execution became
worthwhile only between work multipliers 128 and 512. At multiplier 512 its
one-worker overhead against direct execution was 3.1% with clang and 4.1% with
MSVC, while four workers were 3.25 and 3.19 times faster than direct execution.
The explicit worklist scheduler did not scale on this ready-queue design.
This retains the coroutine prototype as a corpus-validation candidate but does
not select it for production: the real query-size distribution is not measured.

For 250,000 skewed canonical-type requests, the 32-shard table was 1.61 times
faster than the one-mutex reference at four workers with clang and 1.52 times
faster with MSVC. It regressed one-worker throughput by 43% and 59%, and its
measured lock acquisition cost remained above the 10% gate. The read-through
cache did not scale. Retain the single-mutex design until a corpus-derived type
request trace demonstrates a candidate that passes both gates.

These are provisional synthetic results. Concurrent front-end execution stays
deferred, losing prototypes remain comparison fixtures, and the architecture
outcome is not recorded until boundary-1 trace/span data and the gated vertical
slice are available.

## Delivery rules

- Keep `main` buildable and warning-clean after every change.
- Use small migration PRs with one ownership change and a deletion target.
- Add a reduced non-`std` regression before fixing a language defect exposed by
  a standard header.
- Mutation-validate architectural tests. Each new test must fail on the current
  compiler or on a deliberately broken implementation before it counts as
  coverage.
- Every compatibility path needs a counter, a named removal architecture
  boundary, and a hard failure for already-migrated constructs.
- No construct may remain routable to both old and new implementations across
  the architecture boundary that declares the new path authoritative.
- Do not add a parallel type, argument, lookup, environment, or declaration
  representation without an adapter and named deletion architecture boundary.
- Move semantic operations behind facades, establish the new authority, then
  delete the old implementation. Do not copy an operation into another layer.
- Do not combine this work with an SSA or machine-code backend rewrite.
- Each pull request boundary normally lands through one short-lived branch and
  one pull request. Do not keep a long-running migration branch.

## Agent execution contract

This document is an architecture roadmap, not a sufficient implementation
prompt for an agent.

An architecture boundary describes an observable ownership state and normally
spans several pull requests. A pull request boundary is one landable change
with one approved execution brief.

Before implementation, each pull request boundary requires an approved
execution brief containing:

- one bounded objective;
- exact files and symbols expected to change;
- the current control and data flow;
- the target API and ownership change;
- invariants that must hold after the PR;
- behavior explicitly out of scope;
- regressions that fail before the change;
- validation commands;
- compatibility code and counters affected;
- code that must be deleted before the PR is complete;
- known risks and conditions that require stopping for redesign;
- expected allocation-domain, record-size, inline-capacity, and lifetime
  effects when the change introduces or modifies stored data.

An agent cannot declare completion while a listed regression still fails, a
named deletion target remains reachable, a required counter moved in the wrong
direction, or a machine-checkable completion rule in the brief fails.

A weaker agent may implement a reviewed execution brief when the change is
mechanical or locally bounded. A strong compiler model should write or review
briefs that alter identity, ownership, lookup, types, templates, diagnostics,
or parser control flow.

Agents must not invent fallback paths, broaden scope, or keep both old and new
authorities when the brief requires deletion.

A reverted architectural attempt must add a written finding describing the
failed invariant and root cause before another implementation retries the same
approach.

### Documentation policy

Do not create worklog, progress, completion-summary, investigation-summary, or
branch-history documents for completed pull requests. The one exception is the
migration progress ledger defined below.

Use:

- commits for implementation history;
- regression tests for preserved behavior;
- issues for unresolved defects;
- execution briefs for the next pull request boundary;
- `docs/MIGRATION_PROGRESS.md` for cross-run progress tracking.

#### Migration progress snapshot

`docs/MIGRATION_PROGRESS.md` is the single living-state record of the
front-end migration so a fresh coordination run can restore position without
conversation history. Contract:

- It is a snapshot, not a history: each landed migration pull request
  overwrites the file in place. Earlier states are recoverable from git.
- The "remaining work" section fully replaces its previous content on every
  update; resolved blockers and findings are deleted, never archived here.
- Fixed sections and cells only. Content that does not fit belongs in an
  issue, not in prose.
- Updated inside the same pull request that lands the migration work, so
  reviewers see the self-reported estimates.
- Every other progress or summary form remains forbidden.

Update this plan or another architecture document only when completed work:

- changes a public or cross-layer contract;
- changes an architectural invariant or boundary;
- exposes an unresolved blocker that affects the next pull request boundary;
- changes the next execution brief or stop criteria.

Agent reports remain in the task conversation unless one of those conditions
requires a concise repository update. Do not copy agent summaries into the
repository.

A reverted attempt needs only the smallest written finding required to prevent
the next implementation from repeating the same failed assumption. It is not a
general worklog.

## Normative references

Clause tags such as `[class.member.lookup]` refer to the C++20 standard,
ISO/IEC 14882:2020 (final draft N4861):

- stable per-section HTML: `https://timsong-cpp.github.io/cppwp/n4861/<tag>`;
- PDF: `https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4861`;
- normative LaTeX source: `https://github.com/cplusplus/draft`;
- cppreference indexes these sections but is not a correctness authority.

Each architecture boundary below carries an "Implements:" list naming the
clauses its work must satisfy. Execution follows boundary order, not clause
order; the tags are correctness targets for tests and execution briefs.
Mangling is the exception: it is defined by ABI documents, not ISO clauses.

## Architecture boundary 0: diagnosability and measurement

Architectural work cannot be judged with the current diagnostic and runner
contracts.

### Diagnostic engine

Introduce:

- stable diagnostic IDs;
- severity;
- `SourceLocation` and `SourceRange`;
- structured arguments;
- attached notes;
- template-instantiation context;
- diagnostic accumulation and recovery.

`CompileError` may temporarily carry a structured diagnostic while retaining
`what()`. New semantic code must emit diagnostics through the engine.

### Crash and invariant handling

- Install an alternate signal stack with `sigaltstack` and `SA_ONSTACK`.
- Add an effective crash-handler reentry guard.
- Remove the `InternalError`-to-warning downgrade.
- Stop swallowing `std::bad_any_cast` during per-root IR processing.
- Investigate the deterministic standard-header crash under ASAN.

### Runner hardening

- Encode the exact expected diagnostic ID multiset in every new negative-test
  filename: `_e1001.cpp`, `_e1003_e1051.cpp`, and so on. Diagnostic location,
  severity, name, and note role are not part of this runner contract.
- Reserve `_fail.cpp` for the frozen legacy negative-test inventory. Reject any
  new `_fail.cpp` name, and migrate the inventory to `_e<number>` filenames in
  bounded diagnostic-owner batches.
- Require a clean source-rejection exit status for negative tests. A separate
  immutable seven-name inventory temporarily permits internal status 2 only
  for a still-present original legacy `_fail.cpp` that produced no object.
  Driver failures, timeouts, crashes, missing results, unlisted legacy tests,
  and every filename-encoded test remain strict. Delete the exception at
  boundary 2F.
- Compare diagnostic IDs as a multiset. Repeated `_e<number>` segments require
  the same number of emitted occurrences.
- Add multi-translation-unit compile and link cases.
- Add a PIE link variant.
- Fail on discovered but skipped test files.
- Reject encoded return values outside 0 through 255 on Linux.
- Run selected standard-header probes as tracked expected failures.
- Add a clang differential mode for reduced implementation-independent tests.

### Architectural regression corpus

Add reduced tests for:

- integral promotions in `auto`, overload resolution, `sizeof`, constexpr, and
  template deduction;
- same-named templates in different namespaces;
- template specializations crossing translation-unit boundaries;
- inline functions, vtables, RTTI, templates, and implicit special members
  shared by two translation units;
- dependent ADL at the point of instantiation;
- ambiguous member lookup through multiple bases;
- pointer-to-data-member owner and pointee identity;
- nested template access to outer template parameters;
- positive and negative generic detection;
- ambiguous and invalid partial specializations;
- compound requirements and constraint subsumption;
- legal and illegal redeclaration merging;
- tentative parsing and substitution rollback.

Existing failures may enter a named expected-failure manifest. A test cannot be
weakened into a parse-only or unconditional-return check.

### Migration counters

Do not manually add counters to every legacy call site. Track shared choke
points first:

- count parser expression-type queries in the shared query entry point;
- count symbol lookup, access, and overload requests at the service boundary;
- route parser and IR template-materialization requests through
  `TemplateEngine` and count them there;
- count token replay in the lexer-position restore functions;
- count unresolved semantic queries at the AST-to-IR entry boundary;
- count old and new template routing inside the facade;
- count diagnostics emitted outside `DiagnosticEngine`;
- report current and peak bytes by allocation domain;
- report object counts and bytes by major syntax and semantic record type;
- report discarded scratch bytes;
- report `InlineVector` spill counts for selected hot record families;
- report string-table entry count and spelling bytes.

This telemetry is new instrumentation. The existing `AllocationTracker` is
compile-time gated and aggregates by compilation phase, so it cannot provide
allocation-domain, record-family, spill-family, or scratch-discard data. Follow
the existing always-available intern-statistics pattern and expose the detailed
report through the migration/performance stats mode used by CI.

Some legacy behavior, such as `'$'` string surgery or direct parser calls, has
no shared entry point yet. For those cases:

1. create a static inventory with `rg`;
2. route one caller family through a facade;
3. make the old entry point private or change its signature so remaining
   callers become compile errors;
4. add the runtime counter at the new choke point;
5. delete the static inventory when direct calls are no longer possible.

The compiler and linker should help find callers. Prefer access control,
signature changes, deleted overloads, and dependency removal over maintaining a
hand-written list of source locations.

Inline data-shape conventions need a different removal mechanism. The `'$'`
family has no shared API to make private. Architecture boundary 3B removes the
hash from registered type names, after which all `find('$')` recovery becomes
dead code. Its guard is a static inventory plus a test that no registered type
name or emitted symbol contains the proprietary template hash form. Do not
create a facade around the string surgery.

Counters are directional evidence, not proof that every hidden path was found.
Each counter needs:

- a fixed corpus and recorded baseline;
- a named architecture boundary where it must reach zero;
- a compile-time or static guard preventing new direct callers;
- removal when the old path becomes unreachable.

Exit criteria:

- structured diagnostics can be asserted by tests;
- internal invariant failures cannot be reported as success;
- every known architectural defect has a mutation-validated regression or a
  tracked expected failure;
- choke-point counters and the remaining static inventories are visible in CI
  on a fixed corpus;
- diagnostics emitted outside the engine have a baseline and a named removal
  target in architecture boundary 11;
- memory telemetry has a reproducible baseline for a small input, the fixed
  migration corpus, and selected standard-header probes.

## Architecture boundary 1: front-end context, arenas, identities, and entities

Introduce a per-translation-unit `FrontendContext`.

It owns:

- diagnostics;
- target information;
- syntax, semantic, scratch, and IR allocation domains;
- scopes;
- declarations and entities;
- canonical types;
- template registries and the template-engine facade;
- migration statistics.

Existing globals may initially forward to the active context. New code cannot
add process-global compiler state.

### Scoped arena

Add typed arena models with:

- stable handles;
- pinned, non-copyable and non-movable typed `ChunkedVector<T>` storage for
  committed semantic object families, or equivalent stable indirection;
- explicit element-count chunk sizes for every new typed arena;
- transactional registry checkpoints;
- monotonic scratch allocation for tentative parsing and substitution probes;
- destructor registration only for scratch or legacy untyped allocations with
  owning members;
- commit by publishing stable IDs and registry entries, without copying or
  relocating cyclic AST graphs.

Failed probes may leave unreachable arena bytes until the translation unit
ends. They must leave no observable declaration, entity, type, diagnostic, or
instantiation registration. Track discarded scratch bytes and enforce a
reasonable implementation limit so repeated failure cannot exhaust memory
silently.

The existing `ChunkedAnyVector` remains available to the old AST during the
bridge. New semantic nodes cannot use raw addresses as identity.

Keep hot declaration, entity, type, and expression records compact. Optional
template, constexpr, diagnostic, and source-detail payloads belong in typed
cold-storage arenas when measurements show that inline storage is mostly
unused.

### Declaration and entity model

Add `DeclarationBuilder`, owned by `FrontendContext` and invoked by the syntax
parser. It creates source declarations, resolves the canonical entity, and
applies redeclaration merge rules. Sema consults the result rather than creating
parallel entities.

Deliver:

- `DeclId` for each source declaration;
- `EntityId` for the canonical C++ entity;
- redeclaration chains;
- declaration versus definition state;
- linkage and visibility on the entity;
- merge rules for default arguments, exception specifications, `inline`,
  `constexpr`, attributes, friend declarations, and template declarations.

Implements: [basic.def], [basic.link], [dcl.fct.default], [except.spec],
[dcl.inline], [dcl.constexpr], [dcl.attr], [class.friend],
[namespace.memdef].

### Persistent scopes

Deliver:

- stable `ScopeId`;
- parent links;
- declaration lists;
- namespace and class ownership;
- source-order positions for point-sensitive lookup;
- `ScopeId` recorded at declaration and expression lookup sites.

Implements: [basic.scope], [basic.lookup.unqual], [basic.lookup.qual].

### Template facade shell

Route public template instantiation requests through one `TemplateEngine`
facade without changing behavior. Add old-engine routing counters.

Exit criteria:

- leaving parser scope does not destroy lookup information;
- a scratch transaction can allocate declarations and types, fail, and leave
  every committed registry unchanged;
- discarded scratch bytes are measured and bounded;
- IDs cannot be constructed from pointers;
- no new semantic table is keyed by `const void*`;
- no new semantic object is allocated in `ChunkedAnyVector`;
- the legacy allocation choke point rejects non-legacy semantic node types at
  compile time;
- arena bytes, record counts, string-table bytes, and selected `InlineVector`
  spill counts are reported through `FrontendContext`;
- declaration merging passes its initial regression set;
- every template instantiation entry point passes through the facade.

## Architecture boundary 2: multi-translation-unit object model

This is a prerequisite, not optional parallel hardening.

Deliver:

- per-symbol or otherwise groupable code and data sections;
- COMDAT groups or correct weak emission for vague-linkage entities;
- correct symbol sizes;
- deduplicated defined and undefined symbol-table entries;
- valid `.eh_frame` and relocation-section flags;
- correct `.data.rel.ro` placement and relocation kinds for RTTI and vtables;
- PIE-safe output.

Implements: [basic.def.odr], [basic.link], [dcl.inline], [temp.spec],
[temp.inst]. Section layout, COMDAT groups, `.eh_frame`, and relocation
kinds follow the Itanium psABI and Microsoft x64 ABI, not ISO clauses.

Object-file policy for explicit instantiations (2026-09-02): ISO
[temp.explicit] still allows only one explicit instantiation definition in
the program, but neither MSVC nor Clang encodes that as a unique strong
symbol. Measured on
`tests/multi_tu/explicit_instantiation_comdat_ret54` (`template int
bump<int>(int);`, `template class StrongBox<int>;`):

- MSVC `cl` and `clang-cl` emit `??$bump@H@@YAHH@Z`,
  `?set@?$StrongBox@H@@QEAAXH@Z`, and `?get@?$StrongBox@H@@QEBAHXZ` as
  External `SELECT_ANY` (pick any). Unique non-templates (`firstBump`,
  `firstBox`) stay ordinary External `.text`. Two TUs that both contain
  the explicit instantiations link; the linker merges COMDATs rather than
  LNK2005.
- `clang++` Itanium ELF emits the same specializations as `STB_WEAK` in
  per-symbol `.text.*` sections (`llvm-nm` `W`). `firstBump` / `firstBox`
  are `STB_GLOBAL`. Implicit `bump<short>` in the other TU is also weak.

FlashCpp matches that mergeable policy on COFF and ELF: explicit
instantiation definitions of function and class-template members use the
same vague-linkage emission as implicit instantiations (COFF `SELECT_ANY`,
ELF `STB_WEAK`). Do not demote them to unique strong EXTERNAL / `STB_GLOBAL`
to “enforce” [temp.explicit] in the object file. Unique out-of-line
non-template members remain strong. Mangled-name spelling
(`??$bump@H@@YAHH@Z` vs hashed `bump$…` / `StrongBox$…`) is architecture
boundary 3B, not this linkage choice.

Function COMDATs are emitted natively into `.text$N` / `.xdata$N` / `.pdata$N`
during codegen. Unique functions remain in unified `.text` / `.xdata` / `.pdata`.
COFFI cannot reorder symbols, so the section-definition STATIC+aux5 is created
immediately before the EXTERNAL leader. Gate 0 criterion 8 is closed for COFF
function COMDATs.

Exit criteria:

- two translation units sharing inline functions, template instantiations,
  vtables, RTTI, and implicit special members link and run;
- symbol tables contain one coherent entry per symbol;
- the linker produces no `.eh_frame` or text-relocation warnings.

## Architecture boundary 3A: canonical types

Replace flat type descriptions with recursive canonical nodes for:

- builtins and qualifiers;
- pointers and references;
- arrays;
- function and member-function types;
- pointer-to-data-member and pointer-to-member-function types;
- records and enums;
- template parameters;
- template specializations;
- dependent types.

Implements: [basic.types], [basic.fundamental], [basic.compound],
[dcl.type], [dcl.ptr], [dcl.ref], [dcl.array], [dcl.fct], [dcl.mptr],
[class], [dcl.enum], [temp.param], [temp.spec], [temp.dep.type].

Delete the parallel pointer-level and array-dimension representation as
semantic currency. Parser declarator structures may remain syntax-only until
canonicalization.

Store canonical type nodes in a typed arena owned by `FrontendContext`. `TypeId`
is the external identity. Interned spellings may assist lookup and diagnostics,
but `StringHandle` does not participate in canonical type equality.

Exit criteria:

- canonicalization does not inspect parser or member-context stacks;
- parse-order changes do not alter type identity;
- pointer-to-member overloads distinguish owner and pointee types;
- interleaved pointer, array, function, and member-pointer declarators have one
  structural representation;
- flat pointer-level and array-dimension fields are absent from migrated
  semantic paths;
- canonical type identity remains unchanged when string-table insertion order
  changes.

### Parallel-experiment handoff at boundary 3A

An agent completing this boundary must read both the initial evidence under
Concurrency readiness and
`docs/2026-08-24-parallel-front-end-architecture-experiment.md`. Start the
canonical type table behind a single-mutex implementation boundary; the tested
sharded and read-through-cache variants did not pass the one-worker and lock
cost gates. Capture a corpus-derived structural type-request trace while the
new canonicalizer is brought up, then rerun experiment boundary 3 before
changing that implementation.

Do not turn the explicit recursion worklists into the tested parallel
ready-queue scheduler. Keep direct semantic execution as the production
baseline and retain the coroutine prototype only for comparison against real
query-size and dependency-span telemetry. After boundary 3A, begin the
experiment's vertical slice only when architecture boundary 1 and all of its
completion criteria are satisfied, and when the experiment's listed
token-buffer, ownership, identity, arena, and persistent-scope prerequisites
are actually present. The runnable prototypes and commands are
indexed by `tests/parallel_frontend_experiment/README.md`.

## Architecture boundary 3B: ABI-conforming mangling

Implement:

- Itanium `<template-args>` and substitution compression on ELF;
- MSVC template-name encoding and back references on COFF;
- constructor and destructor variants required by each ABI;
- mangling from `EntityId`, canonical types, and target ABI only.

Correctness sources here are the Itanium C++ ABI mangling specification and
Microsoft x64 name-mangling documentation; ISO clauses do not define mangled
names.

Exit criteria:

- parse-order changes do not alter mangled output;
- mangled names match clang or MSVC byte-for-byte for a fixed ABI corpus;
- template specializations have compatible identities across translation
  units;
- no registered type name or emitted symbol uses the proprietary `$hash`
  template form;
- `find('$')` recovery sites are deleted rather than wrapped in a facade.

## Architecture boundary 4: authoritative expression sema

Introduce a semantic expression node or result containing:

- `TypeId`;
- value category;
- selected declaration or overload set;
- implicit conversion sequence;
- constant-expression requirements;
- semantic rewrite or lowering records.

Start with:

1. literals and identifiers;
2. builtin unary and binary operators;
3. assignment and initialization;
4. conditional expressions;
5. calls.

Implements: [conv.prom], [expr.arith.conv], [conv.qual], [dcl.init.ref],
[dcl.init.list], [expr.cond], [expr.ass], [expr.call], [over.best.ics],
[expr.const].

Use shadow comparison before switching each expression family.

Exit criteria:

- one implementation owns integral promotions, usual arithmetic conversions,
  qualification conversions, reference binding, and list-initialization
  narrowing;
- parser, constexpr, templates, and AST-to-IR read the same expression type;
- duplicate arithmetic-conversion implementations are deleted;
- scalar AST-to-IR performs no type reconstruction;
- promotion, narrowing, constexpr-width, `auto`, and deduction regressions use
  the new path.

## Architecture boundary 5: lookup, overload resolution, and access control

Introduce one `NameLookup` service over persistent scopes and declaration
entities.

Deliver:

- unqualified, qualified, member, ADL, and using-directive lookup;
- source-order and point-of-instantiation inputs;
- declaration-set merging and explicit ambiguity;
- class-member lookup according to `[class.member.lookup]`;
- access checking integrated with lookup results;
- one overload engine with complete implicit conversion sequences,
  derived-to-base distance, template ordering hooks, and candidate diagnostics;
- declaration-keyed constexpr bindings.

Implements: [basic.scope], [basic.lookup.unqual], [basic.lookup.qual],
[basic.lookup.udir], [basic.lookup.classref], [basic.lookup.argdep],
[class.member.lookup], [class.access], [class.access.base],
[over.match.funcs], [over.match.viable], [over.match.best],
[over.match.oper], [over.best.ics], [over.ics.rank],
[temp.deduct.partial].

Migration order:

1. class-member lookup and access;
2. sema expression lookup;
3. parser binding through the shared service;
4. constexpr;
5. AST-to-IR, followed by deletion of its lookup code.

Exit criteria:

- ambiguous-base member lookup is diagnosed;
- promotion ranks above conversion;
- parser and sema cannot produce different answers for one `ScopeId`;
- AST-to-IR has no `gSymbolTable`, lazy-member lookup, parser lookup, or
  access-check calls;
- mangled-name and arity-only call selection are deleted.

This boundary owns removal of lookup and access logic from AST-to-IR. It does
not own template-materialization calls, which move behind `TemplateEngine` in
architecture boundary 6.

## Architecture boundary 6: template identity, arguments, and environments

Move the facade to semantic identity.

Deliver:

- registries keyed by `TemplateDeclId`;
- parameters keyed by depth and index;
- one ordered `TemplateArgument` representation for types, values, templates,
  and packs;
- one immutable substitution environment;
- declaration-keyed instantiation and specialization caches;
- identity-correct class, function, alias, and variable template registries;
- adapters from old representations with named deletion architecture
  boundaries.

Implements: [temp.param], [temp.arg], [temp.names], [temp.res],
[temp.dep], [temp.spec], [temp.alias], [temp.variadic], [temp.fct],
[temp.over.link].

Exit criteria:

- same-named templates in different namespaces never collide or overwrite;
- nested templates retain outer bindings by identity;
- alias and variable templates no longer use single name-keyed slots;
- every old argument, environment, and key representation has a removal
  architecture boundary;
- parser and AST-to-IR template-materialization calls route through the facade;
- direct calls to parser instantiation entry points are impossible outside the
  old-engine implementation hidden behind the facade.

## Architecture boundary 7: probe transactions, SFINAE, and constraint satisfaction

This architecture boundary precedes AST-based partial-specialization and
function-template instantiation.

Deliver:

- substitution failure as a typed result rather than an exception controlled
  by global parser mode;
- isolated probe transactions over scratch declarations, types, diagnostics,
  and instantiation records;
- normalized atomic constraints with parameter mappings;
- constraint satisfaction;
- simple, type, compound, and nested requirements;
- `noexcept` and return-type requirements as parts of compound requirements;
- instantiation-context diagnostics.

Implements: [temp.deduct], [temp.constr], [temp.constr.normal],
[temp.constr.atomic], [temp.constr.op], [expr.prim.req],
[expr.prim.req.simple], [expr.prim.req.type], [expr.prim.req.compound],
[expr.prim.req.nested], [temp.point].

Exit criteria:

- failed probes leave no committed state;
- generic detection works without recognizing `void_t` or library spellings;
- compound requirements reject missing or incorrectly typed expressions;
- hardcoded standard-library detection and trait names are absent from general
  compiler logic.

## Architecture boundary 8A: AST-based template instantiation

Implement:

```text
template AST pattern + substitution environment + semantic context
    -> instantiated semantic declarations and expressions
```

The new path cannot access the lexer or parser cursor.

Migration order:

1. class templates with data members and aliases;
2. class-template member functions;
3. partial specializations;
4. function templates;
5. variable and alias templates;
6. deduction guides and explicit instantiations;
7. out-of-line and lazy members.

Deliver:

- structural substitution over the new semantic AST;
- shared P-against-A deduction;
- proper non-deduced contexts;
- partial-specialization matching and ordering;
- an iterative point-of-instantiation work queue;
- small arena-owned `InstantiationFrame` records linked by ID for logical depth
  and diagnostics;
- conforming implementation limits without a sticky translation-unit cap.

Implements: [temp.spec], [temp.inst], [temp.explicit],
[temp.spec.partial], [temp.spec.partial.match],
[temp.spec.partial.order], [temp.deduct], [temp.deduct.call],
[temp.deduct.type], [temp.deduct.guide], [temp.deduct.partial],
[temp.point], [temp.variadic].

Exit criteria:

- migrated constructs never restore saved lexer positions;
- migrated probes commit no state on failure;
- namespace, outer-binding, specialization, ADL, detection, and cross-TU
  regressions pass;
- at least 1024 levels of logical instantiation run under the normal OS stack
  limit without native stack usage growing with logical depth;
- replay counters reach zero for migrated constructs;
- each migrated old path is deleted.

## Architecture boundary 8B: constraint subsumption and constrained ordering

This boundary starts only after architecture boundary 8A has migrated function
templates and partial ordering to the new template engine.

Deliver:

- subsumption over normalized atomic constraints and parameter mappings;
- constrained function-template ordering layered on the new function-template
  partial-ordering implementation;
- diagnostics for incomparable and unsatisfied constraints.

Implements: [temp.constr.order], [temp.constr.atomic],
[over.match.best].

Exit criteria:

- subsumption selects the more constrained overload;
- constrained ordering never calls the parser-owned deduction engine;
- the syntactic legacy constraint-ordering implementation is deleted.

## Architecture boundary 9: semantic AST-to-IR lowering

AST-to-IR is an active migration layer, not a passive consumer.

Architecture boundaries 5 and 6 are preconditions. Lookup, access checks, and
template-materialization calls must already have moved out of AST-to-IR before
this boundary replaces its lowering contract.

Deliver:

- a typed lowering interface from normalized semantic nodes;
- lowering records for range-for, implicit construction, conversions, calls,
  access paths, temporaries, and cleanup;
- no semantic lookup, access checking, overload resolution, template
  materialization, or parser callbacks;
- no writes from AST-to-IR into semantic state;
- adapters to the existing flat IR while the backend remains unchanged.

Implements: [stmt.ranged], [class.temporary], [class.copy.elision],
[class.conv], [dcl.init], [over.match.list], [over.match.copy],
[over.match.ref], [expr.ref], [except.ctor].

Exit criteria:

- all semantic questions needed by lowering are answered before entry;
- the emission set is closed before entry, and lowering cannot intern a new
  string, create a canonical type, request an instantiation, or discover a new
  emitted entity;
- missing facts produce `InternalError`;
- codegen-synthesized name-to-type maps are deleted;
- lowering consumes only normalized semantic nodes and typed lowering records;
- the old AST-to-IR semantic compatibility interface is deleted.

## Architecture boundary 10: parser ownership replacement

Architecture boundaries 7, 8A, 8B, and 9 are preconditions. Boundary 10 does
not begin while sema, templates, constexpr, or lowering still require parser
state, saved-token replay, or parser callbacks.

Boundary 10 transforms the current parser behind one translation-unit parse
entry point. It does not introduce two complete parsers that can both accept
the same construct. A replacement parser component may be introduced for a
bounded syntax family behind that entry point, but the family dispatcher must
select exactly one implementation. Shadow parsing is forbidden because token,
diagnostic, scratch, and declaration side effects make it a second authority.

Before each boundary-10 pull request, its execution brief must contain a
routing table with one row per affected syntax family:

```text
family | current parser entry | target syntax entry | output representation
       | semantic calls removed | compatibility adapter and counter
       | compile-time routing guard | old code deleted by this PR
```

An unmigrated family may still produce a legacy AST bridge node. A migrated
family produces syntax-owned structure and publishes declarations only through
`DeclarationBuilder`; it cannot allocate a parallel semantic object in
`ChunkedAnyVector` or fall back to the legacy family parser after failure. Each
source declaration maps to one `DeclId` and one canonical `EntityId` result,
never one legacy registration plus one new registration. Remaining bridge
nodes and adapters have counters and named deletion targets in boundary 10F or
11.

### Architecture boundary 10A: single entry point and indexed token input

Introduce the indexed parser-facing `TokenBuffer`, stable `TokenIndex`, and
cheap cursor copies for lookahead. The existing lexer and preprocessing path
feed the buffer through an adapter; this boundary does not replace
preprocessing or change accepted language behavior.

The current parser consumes tokens through the single parser-facing cursor.
Remove destructive token-queue operations and direct lexer-position ownership
from the migrated input path. Syntax nodes and diagnostics retain token indices
or source ranges, never token addresses.

Record each balanced block (`{...}`, `(...)`, `[...]`) in the `TokenBuffer` as
a `{TokenIndex begin, TokenIndex end}` range over the post-preprocessing token
stream. Tokens produced by macro expansion are included. Block ranges provide
O(1) skip-to-end for body handling, error recovery, and checkpointing; replace
repeated balanced-bracket scans as their caller families migrate.

### Architecture boundary 10B: transactional parser state

Move tentative parsing onto scoped transactions. A parser checkpoint contains
the token cursor, scratch-arena mark, diagnostic mark, and transactional marks
for declarations and registries. Commit publishes the chosen parse once;
rollback restores every component and leaves no observable declaration,
entity, type, diagnostic, or instantiation.

Tentative parsing exists only where the grammar is ambiguous.
[dcl.ambig.res], [stmt.ambig], and [temp.names] bound the allowed rollback
sites. Delayed semantic work and template instantiation cannot create or
restore a parser checkpoint.

### Architecture boundary 10C: syntax-only declarations

Make declaration outlining part of the real source-ordered declaration parser,
not a token-level body-range scanner. Migrate declaration families in an order
listed by the execution brief. Each migrated family creates source-faithful
syntax and invokes `DeclarationBuilder` for declaration and entity publication.

Delete parser-owned entity creation, redeclaration merging, linkage decisions,
and scope reconstruction for each family when it switches. The compatibility
adapter may translate an unmigrated legacy syntax node into the declaration
API, but it cannot create a second declaration identity or write a second
semantic result.

### Architecture boundary 10D: syntax-only expressions and statements

Migrate expression and statement families behind the same single parser entry
and family dispatcher. The parser records syntax, token ranges, and grammar
structure only. It does not write expression types, selected declarations,
conversion sequences, overload results, access decisions, constexpr results,
template substitutions, lowering records, or backend names.

Delete the corresponding parser-owned semantic operation when each family
switches. A migrated family must consume the authoritative results from sema
and cannot retry through the legacy expression or statement parser.

### Architecture boundary 10E: bounded parser control flow

Convert source-controlled parser recursion to loops or small arena-owned parse
frames. Use block ranges for direct jumps instead of rescanning balanced input.
Keep ordinary recursive descent only for grammar composition with a bounded
native depth.

Measure changed recursive-path frames where the toolchain supports stack-usage
output. Deep declaration, expression, statement, template-syntax, and balanced
block probes must run under the normal operating-system stack limit. A
configurable logical complexity limit may diagnose pathological input; raising
the process stack is not validation.

### Architecture boundary 10F: delete the parser service locator

Close the parser-facing interface to syntax production and declaration
publication. Remove parser-to-sema cycles, parser callbacks from constexpr,
parser-owned overload resolution, constexpr decisions, access checks,
template substitution, and instantiation. Delete `Parser_Templates_Inst_*`,
saved-token replay helpers, and parser includes used only to request semantic
services.

Drive the token-restore, parser semantic-call, parser callback, legacy family
routing, and legacy parser-allocation counters to zero on the fixed migration
corpus. Make former service entry points private or delete their signatures so
new direct callers fail to compile. Delete boundary-10 compatibility adapters
when their final family switches; only bridge storage explicitly assigned to
boundary 11 may remain.

Combined boundary-10 exit criteria:

- rollback never retains an instantiation or registry mutation;
- local parser rollback restores every transaction component, not only the
  token cursor;
- no delayed semantic operation or template instantiation restores a saved
  token cursor;
- deep parser-nesting probes fail with a diagnostic at the implementation
  limit or complete without native stack exhaustion;
- `SemanticAnalysis` no longer stores a parser pointer;
- constexpr cannot call parser methods;
- `Parser_Templates_Inst_*` and replay helper files are deleted;
- the single parser entry point exposes a bounded syntax-facing interface
  instead of acting as a compiler service locator, and no syntax family is
  routable to both legacy and migrated implementations.

## Architecture boundary 11: delete transitional storage

Begin only after architecture boundary 10F has closed the parser-facing
interface and deleted the parser service locator.

Delete:

- raw-pointer semantic side tables;
- duplicate resolved fields on syntax nodes;
- parser return-type hints and mangled-name recovery;
- old and new shadow-comparison code;
- bridge nodes that have no remaining users;
- raw `std::cerr` diagnostics and bare `CompileError(std::string)` construction
  outside approved terminal adapters;
- compatibility counters after they reach zero.

Exit criteria:

- every semantic fact has one storage location and one writer;
- cloning and substitution cannot orphan semantic state;
- syntax nodes contain no backend names or codegen lifecycle state;
- the outside-`DiagnosticEngine` counter is zero;
- the final pipeline matches this document.

## Deferred work

Do not combine these with the core migration without a concrete dependency:

- token-based preprocessing;
- a target triple and cross-compilation;
- typed IR payloads and an IR verifier;
- CFG, SSA, optimization, and global register allocation;
- enabling concurrent front-end execution. Concurrency-safe ownership,
  deterministic publication, and equivalent single-worker behavior remain
  requirements of the core migration.

## Decision gates

### Gate 0: diagnosable multi-TU baseline

Closed 2026-09-04: the multi-TU corpus links and runs without linker warnings
on Windows and ELF (PIE and no-PIE), including `run_elf_eh_frame_tests.sh`.
Architecture boundary 3A may begin.

### Gate 1: identity foundation

Do not begin architecture boundary 4 until boundaries 3A and 3B produce
canonical identities and ABI-conforming mangling without parse-order,
parser-stack, raw-pointer, or proprietary-hash inputs.

### Gate 2: semantic authority

Do not begin broad template migration in architecture boundary 6 until boundary
5 proves one expression type, one lookup answer, and complete conversion
ranking for the scalar and member-lookup corpus.

### Gate 3: template replacement viability

Do not begin architecture boundary 10A until architecture boundaries 7, 8A,
8B, and 9 pass:

- cross-TU specialization identity;
- namespace-separated same-name templates;
- nested outer bindings;
- dependent ADL;
- partial-specialization ambiguity;
- generic detection without library-name recognition;
- compound requirements and subsumption.

If these require token replay or parser-state recovery, revise the model rather
than adding compatibility code.

### Gate 4: old-engine deletion

The migration is not complete while token replay, parser-owned instantiation,
AST-to-IR lookup, or pointer-keyed semantic state remains reachable for
normalized code.

## Initial pull request boundaries

Pull request boundary 1, boundaries 2A through 2F, and boundary 3 advance
architecture boundary 0. Pull request boundary 4 closes its tracking work and
introduces the template facade needed by architecture boundary 1. Pull request
boundaries 5 and 6 continue architecture boundary 1.

### Pull request boundary 1: diagnostics and crash diagnosability

- Add the diagnostic engine core.
- Convert three to five diagnostics that already have correct source locations.
- Record the remaining raw diagnostic sites and begin the outside-engine
  counter. Do not move member lookup diagnostics into sema in this PR.
- Install the alternate signal stack and reentry guard.
- Remove `InternalError` and `bad_any_cast` downgrades.

### Pull request boundary 2A: filename diagnostic contract and expected-failure manifest

- Replace inline negative-test assertions with the `_e<number>` filename
  grammar and exact diagnostic-ID multiset matching.
- Give clean source rejection and internal compiler or driver failure distinct
  exit statuses, and make the runners reject every non-source failure.
- Freeze the existing `_fail.cpp` inventory so no new legacy entry can be
  added. Rename the already structured declarator-family tests first.
- Run the complete frozen inventory under the new process-status contract.
  Freeze the seven legacy tests that currently reach internal-failure status 2
  in a second immutable inventory. Only a still-present original `_fail.cpp`
  representation in that inventory may use the temporary exception, and only
  when no object was produced. Encoded successors, unlisted legacy tests,
  crashes, timeouts, driver failures, worker failures, and missing results
  remain hard failures. Report the active count against a baseline of 7 with
  direction down and removal boundary 2F.
- Add the named expected-failure manifest and enforce stale-entry detection.
  This manifest is only for positive tests blocked by known compiler defects;
  it cannot contain negative-test filenames.

### Pull request boundaries 2B through 2F: legacy negative-test conversion

Convert the frozen `_fail.cpp` inventory by diagnostic owner. Each pull request
must name its exact test slice and compiler emission sites before editing,
assign distinct stable `DiagnosticId` values at shared semantic choke points,
rename the converted tests to `_e<number>` form, and mutation-validate the
filename expectation.

- 2B: lexer, parser, declarator, and source-structure diagnostics;
- 2C: constexpr, initialization, and constant-expression diagnostics;
- 2D: conversions, overload resolution, operators, and access diagnostics;
- 2E: templates, lookup, deduction, constraints, and substitution diagnostics;
- 2F: remaining semantic and lowering diagnostics, followed by deletion of
  `_fail.cpp` classification, the frozen inventories, and the seven-test
  internal-failure compatibility.

#### Diagnostic-contract durability and legacy-investment stop rule

The durable product of these pull requests is the diagnostic contract and its
regression, not the location of the temporary emission call. A
`DiagnosticId` names the rejected C++ rule and remains stable when authority
moves from parser, template replay, constexpr, or AST-to-IR code into the new
semantic services. The later migration moves the emission call; it does not
renumber the diagnostic or replace the regression merely because its original
owner was deleted.

A conversion may attach a stable ID at an existing bounded rejection choke
point when doing so adds no new semantic decision, recovery behavior, replay,
lookup, substitution state, or fallback. Its execution brief must name the
architecture boundary that takes ownership of the emission and the old call
that boundary deletes.

A conversion must stop rather than add or improve any of the following solely
to empty the frozen inventory:

- saved-token replay or parser-mode-controlled substitution;
- spelling, mangled-name, arity, or raw-pointer identity recovery;
- parser-owned lookup, overload resolution, access checking, deduction,
  constraint handling, or template instantiation;
- AST-to-IR lookup or semantic reconstruction;
- lowering-time recovery for a semantic fact that the normalized lowering
  contract requires on entry.

When the stop rule applies, the approved execution brief must choose one of
these outcomes explicitly:

1. delete the test when it covers an incomplete unsupported implementation or
   duplicates another regression, as permitted below;
2. land a separately reviewed vertical slice through the new authoritative
   component when that component's architectural prerequisites are present;
3. amend this roadmap before implementation to defer the named test slice to
   its owning architecture boundary and move every affected inventory,
   compatibility-count, and cleanup target with it.

Do not use option 3 implicitly. Until such an amendment lands, boundary 2F
still owns deletion of `_fail.cpp` classification, both frozen inventories,
and the seven-test internal-failure compatibility.

For boundary 2E, parser-level template syntax diagnostics may be converted at
their existing bounded parser owner. Deduction, substitution, constraint, and
instantiation diagnostics may receive stable IDs at an existing shared
rejection choke point, but the conversion cannot restructure the old engine;
architecture boundaries 6 through 8 own their semantic implementation.

For boundary 2F, distinguish a source-language rejection from a missing
lowering fact. A valid source rejection temporarily discovered in AST-to-IR
may receive its stable ID there only when the conversion introduces no new
semantic query or recovery and names architecture boundary 4 or 5 as the
relocation owner. A missing fact required by normalized lowering is an
`InternalError`, not a new source diagnostic; architecture boundary 9 owns
enforcement of that invariant.

Do not introduce a generic legacy diagnostic ID, derive IDs from message text,
or assign IDs per test. A conversion batch must stop and split again if its
rejection paths do not share a bounded diagnostic owner.

A frozen test that fails only because an incomplete parser or sema
implementation does not yet support the construct may be deleted instead of
implementing the missing parser error.

When two identical tests are found that exercise the same code paths, delete
one of them. When two nearly identical positive (successful) tests are found,
merge them if both are small. Leave medium or large test files alone and do
not extend them.

### Pull request boundary 3: first architectural regression slices

- Add promotion, namespace-template-identity, and ambiguous-member-lookup
  probes as tracked expected failures.
- Correct namespace-collision tests so colliding templates have observably
  different structure.
- Mutation-validate every new probe.

This boundary requires a strong compiler review even when a weaker agent writes
the test files.

### Pull request boundary 4: migration counters and template facade

- Route instantiation entry points through `TemplateEngine`.
- Add counters for replay, AST-to-IR lookup, codegen-to-parser calls,
  post-parse parser typing, and centralized string-based identity recovery.
- Add a static inventory for inline `'$'` parsing rather than wrapping it.
- Prevent counter increases on a fixed CI corpus.

This is an architectural PR and requires a strong implementation agent.

### Pull request boundary 5: `FrontendContext`, IDs, and scoped arena

- Add the context and strong ID types.
- Define syntax, semantic, scratch, and IR allocation domains.
- Add monotonic scratch allocation, registry rollback, commit, destructor, and
  discarded-byte accounting tests.
- Add initial arena, string-table, and selected `InlineVector` telemetry.
- Forward selected globals through the active context without behavior change.

This is an architectural PR and requires a strong implementation agent.

### Pull request boundary 6: persistent scopes and first recovery deletion

- Make scope exit move a cursor instead of destroying the scope.
- Record `ScopeId` on declarations and lookup sites.
- Delete one spelling-based namespace recovery path and require the recorded
  scope.

## Stop criteria

Stop the current sequence if any of these occurs:

1. After the arena and identity work, the first migrated expression families
   cannot retain stable semantic identity across cloning and substitution
   without dual pointer and ID ownership.
2. ABI-conforming mangling still requires parser stacks, registration order,
   raw addresses, or content hashes.
3. Two migrated template attempts reintroduce saved-token replay for the same
   missing semantic input or ambient parser-state dependency.
4. A migration counter fails to reach zero across two consecutive architecture
   boundaries because the new path cannot answer a question the old path
   answers.
5. A construct remains supported by both engines after the architecture
   boundary that declares the new engine authoritative.
6. After architecture boundary 10F, `SemanticAnalysis::parser_` or
   `ConstExpr::EvaluationContext::parser` still exists, or a non-parser,
   non-bridge translation unit includes `Parser.h` to request a semantic
   service.
7. Two consecutive architecture boundaries each need more than five unplanned
   compatibility paths. A compatibility path is code guarded by a migration
   counter or compatibility marker that was absent from the approved execution
   brief.
8. The object-writer section model cannot support COMDAT and vague linkage
   without replacing the object-writer API. Stop before architecture boundary
   3A and write a separate object-writer replacement plan rather than bypassing
   Gate 0.
9. The first two probe-transaction slices require relocating cyclic AST graphs
   or rewriting inbound pointers instead of committing stable IDs and registry
   entries.

Criteria 1 through 7 and 9 justify replacing the strangler with a clean new
front end inside FlashCpp. Criterion 8 pauses the front-end sequence for a
separate object-writer replacement plan. Neither outcome means discarding the
backend, constexpr evaluator, object writers, or tests.

## Bug-fixing policy during migration

Fix current bugs immediately when they:

- prevent diagnosis or testing;
- corrupt syntax needed by the new front end;
- block a migration facade;
- affect reusable backend or object-writer code;
- can be fixed by landing the new authoritative component.

Do not extend old replay, name-recovery, pointer-keyed sema, codegen lookup, or
standard-library special cases. Use those defects as vertical migration slices:
add the regression, implement the replacement component, route the construct,
then delete the old path.

## Relationship to existing documents

This plan supersedes architectural directions that assume parser-owned
instantiation, pointer-keyed semantic storage, or saved-token replay will be
the final design.

The following documents remain useful as historical evidence, but are
superseded as implementation plans:

- `docs/2026-08-16-exhaustive-expression-rewrite-and-sema-boundary-plan.md`;
- `docs/2026-05-12-template-argument-architecture-audit.md`;
- `docs/2026-04-08-template-instantiation-materialization-plan.md`;
- `docs/2026-04-04-codegen-name-lookup-investigation.md`;
- archived parser, sema, constexpr, and implicit-cast plan pointers.

`docs/SEMANTIC_ANALYSIS_STATUS.md` continues to describe the shipping compiler
until architecture boundaries are reached. Existing plans remain useful as
implementation history and defect references.
