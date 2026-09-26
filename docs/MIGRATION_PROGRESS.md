# Front-end migration progress

This is the handoff for agents continuing the migration. The [rearchitecture
plan](2026-08-24-front-end-rearchitecture-plan.md) is authoritative for the
design, boundaries, and exit criteria. This file records current state and
next work; completed implementation history belongs in git.

Last updated: 2026-09-26.

## Current state

Boundary **3A (canonical types)** is active and incomplete. `TypeSpecifierNode`
now carries an outermost-to-innermost `DeclaratorComponent` spine. Named and
abstract pointer/array declarators use an explicit frame stack, preserving
mixed forms such as `int (*(*p)[3])[4]`. Legacy pointer/array fields are
projected only when the shape is exactly representable.

`CanonicalTypeTable` interns and substitutes structural types, including
ordered pointer/array wrappers and one function component. `TypeId` is not yet
the sole semantic authority: `CanonicalTypeDesc` still carries a structural
bridge, and some conversion planning still uses syntax-facing types. Ordered
pointer objects use `runtime_pointer_depth`, but template, trait, constexpr,
and IR consumers still read flat fields. Array and callable outer wrappers
remain guarded where their consumers are not migrated.

Semantic conversion support covers ordered shape identity, pointer
qualification, object-pointer-to-`cv void*`, array/function decay, boolean
conversion, and outermost ordered-reference binding. Function decay compares
the complete callable type. Derived-to-base and further callable-component
conversions remain deferred. `Parser::get_expression_type` still peels an
ordered pointer for call-argument typing; remove that peel after sema owns
argument typing and overload diagnosis.

Ordered static-member declarations retain their canonical `TypeId` through
publication and the covered template-copy/substitution paths, including full
specializations. A valid static-member type with a nested callable alias can
still exceed the importer and receives `UnsupportedStaticMemberType` (1020).
This is an implementation gap, not a C++ restriction. Undeduced `auto` or
`decltype(auto)` without an initializer is separately diagnosed as
`AutoTypeDeductionFailure` (1014).

The explicit declarator frame stack keeps nested declarator depth off the native
call stack. The recorded Clang stack-usage probe measured `parse_declarator` at
5,160 bytes versus 5,000 bytes on `origin/main`; repeat the comparison when
changing recursive parser paths. `TypeSpecifierNode` measured 520 bytes in the
canonical architecture probe.

The explicit-criteria rollup is **9/79 complete**. Passing tests or the breadth
of landed code do not complete boundary 3A. Implementation effort is not yet
estimated reliably.

## Next work

Continue boundary 3A in this order:

1. **Move call-argument typing to sema.** Remove the ordered-pointer peel from
   `Parser::get_expression_type` after sema can type arguments and own overload
   diagnostics. Retain regressions for ordered pointer/array arguments and
   mismatched callable signatures.
2. **Make `TypeId` the conversion currency.** Move remaining conversion rules
   and callers to the structural planner. Remove duplicate flat and syntax-
   facing authorities only as each caller migrates. Preserve full callable
   comparison, nested cv, array decay, and value-category behavior.
3. **Migrate remaining flat consumers.** Prioritize type-trait operands,
   template argument/substitution storage, constexpr type queries, and IR
   layout/subscript paths. Add reduced non-library regressions for language
   rules. Keep unsupported shapes fail-closed until their consumers are
   structural.
4. **Complete importer and declarator coverage.** Add canonical import support
   for valid ordered forms still rejected at a boundary, including nested
   callable aliases used by static members. Preserve member `TypeId`s through
   every AST copy and substitution route.
5. **Close and mutation-validate the 3A exit criteria.** Prove independence
   from parser/context stacks, parse order, and string insertion order; cover
   remaining pointer-to-member, function, dependent, and template families;
   and remove flat pointer-level and array-dimension fields from migrated
   semantic paths.

Before declaring 3A complete, check every exit criterion in the plan and read
its [parallel architecture experiment](2026-08-24-parallel-front-end-architecture-experiment.md).
Boundary 3B (ABI-conforming mangling) and boundary 4 (authoritative expression
sema) follow 3A. Do not route around unfinished canonical-type ownership to
start them.

## Other open boundaries

- **Scratch rollback and publication:** `FrontendScratchTransaction` rolls
  back scratch state, `DeclarationBuilder`, and `TemplateDeclTable`, but not
  `SymbolTable` maps or namespace publication. Production speculative parsing
  is not integrated with the transaction. Close this before putting more
  tentative publication behind it; see [known issues](KNOWN_ISSUES.md).
- **Compiler bugs and bounded unsupported cases:** consult
  [known issues](KNOWN_ISSUES.md) before selecting adjacent work. Add newly
  found bugs there; keep this file focused on migration work still ahead.
- **Negative tests:** encode the exact expected diagnostic ID multiset in the
  filename (for example, `_e1001.cpp` or `_e1003_e1051.cpp`). `_fail.cpp` is
  reserved for the immutable legacy inventory.

## Active findings

- Two `FrontendContext` doctests have the same failures on clean `36d1b33b` and
  this branch (two failures, five assertions; neither test is disabled): the
  persistent-scope publication test expects no active context, and the AST
  family counter misclassifies `TemplateEnvironmentSnapshotNode` and `BlockNode`
  with clang-cl. Owner: unit fixture lifecycle and legacy AST classification.
- `SemanticAnalysis:ResolvedDirectCallQueryTracksAnalysisState` also fails on
  clean `main`; details are in [known issues](KNOWN_ISSUES.md).
- Scratch `allocateObject` can construct an object before destructor-vector
  registration throws `bad_alloc`, leaving its destructor unregistered. Fix
  this before production scratch probes use nontrivial objects.
- The top-level expression fallback can mask declaration-parse errors; see
  [known issues](KNOWN_ISSUES.md).

## Validation handoff

After compiler-source changes, build with `.\build_flashcpp.bat`. Run focused
regressions while iterating, then `pwsh tests/run_all_tests.ps1` when the change
is ready. Never run the full suite concurrently with the build.

Migration counters and static identity inventories are baselined under
`tests/migration_counters/`; run the host-native counter and inventory scripts
after compiler changes. All 64 fixed-corpus entries were within baseline at
this update. Gate 0's Windows and ELF multi-translation-unit checks remain
required compatibility evidence. See the plan for complete boundary-specific
validation.

For recursive-path changes, report the largest changed native stack frame and
whether stack use remains bounded as logical depth grows. Do not raise the
stack limit to make a regression pass.
