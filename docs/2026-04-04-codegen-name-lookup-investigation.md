# 2026-04-04 codegen lookup investigation

## Concrete implementation plan

This top section is the execution plan.

The rest of this document is the investigation backing the plan. Each task below points to the detailed findings so an agent can:

- start with a concrete change target,
- know which files to inspect first,
- and query the investigation sections for the reasoning and source locations behind the task.

### Phase 1: ordinary call resolution becomes sema-owned

**Goal**

Stop `IrGenerator_Call_Direct.cpp` from rescanning symbol tables and member hierarchies to rediscover direct-call targets.

**Primary files**

- `src/SemanticAnalysis.h`
- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Call_Direct.cpp`
- `src/AstNodeTypes_DeclNodes.h`

**Concrete work**

1. Add a sema-owned side table for resolved ordinary calls, similar to the existing operator-call and subscript tables.
2. Store the final `FunctionDeclarationNode*` for ordinary direct calls during semantic normalization.
3. Change direct-call lowering to consume that resolved target first.
4. For sema-normalized bodies, turn the current name-based recovery chain into `InternalError` instead of more lookup.
5. Keep only the minimum fallback needed for bodies sema never normalized yet, and document that boundary in code.

**Done when**

- direct-call lowering no longer needs `lookup_all(...)`, `gSymbolTable` rescans, mangled-name retry lookup, or member/base-class search for sema-normalized bodies,
- the selected callee comes from sema-owned resolved data,
- missing resolved data in a normalized body fails loudly.

**Read/query first**

- [Evidence 1](#1-direct-call-lowering-still-re-resolves-callees-by-name)
- [What already exists on the semantic-analysis side](#what-already-exists-on-the-semantic-analysis-side)
- [Why this is fragile](#why-this-is-fragile)

### Phase 2: move expression typing out of codegen

**Goal**

Remove `buildCodegenOverloadResolutionArgType(...)` as a codegen-owned type reconstruction path.

**Primary files**

- `src/SemanticAnalysis.h`
- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Stmt_Decl.cpp`
- `src/IrGenerator_MemberAccess.cpp`

**Concrete work**

1. Extend semantic normalization so it records the normalized type/category for the expression shapes currently reconstructed in codegen.
2. Add a sema accessor for that expression-type data.
3. Replace `buildCodegenOverloadResolutionArgType(...)` call sites with sema-owned type queries.
4. Remove codegen-side recursive type guessing for identifiers, member accesses, casts, and nested calls.
5. Once sema value-category coverage is correct for the broader expression surface, delete the temporary expression-shape whitelist in `buildCodegenOverloadResolutionArgType(...)` and let the sema query run first for any `ExpressionNode`.
6. **Known risk — enum constant value category**: `inferExpressionValueCategory` must classify enum constants (enumerators) as `PRValue`, not `LValue`. If the `IdentifierBinding::EnumConstant` check is unreliable or missing, sema will emit an lvalue-reference qualifier for enumerator arguments, causing constructor overload resolution to prefer deleted `const T&` overloads over `T&&` overloads. Regression test: `tests/test_ctor_enum_prvalue_ret0.cpp`. See also `docs/KNOWN_ISSUES.md`.

**Done when**

- constructor/copy/move-sensitive codegen decisions no longer depend on ad-hoc declaration lookups or recursive AST-shape type recovery,
- `buildCodegenOverloadResolutionArgType(...)` is either deleted or reduced to a temporary shim used only for non-normalized bodies,
- widening sema-first queries no longer depends on a hand-maintained whitelist of expression shapes.

**Read/query first**

- [Evidence 2](#2-codegen-still-performs-its-own-expression-type-reconstruction)
- [Evidence 3](#3-member-access-lowering-still-recovers-member-information-by-name)
- [What already exists on the semantic-analysis side](#what-already-exists-on-the-semantic-analysis-side)

### Phase 3: constructor selection becomes sema-owned

**Goal**

Stop constructor choice from depending on codegen-side argument-type rebuilding.

**Primary files**

- `src/SemanticAnalysis.h`
- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Stmt_Decl.cpp`
- related constructor-resolution helpers shared with constexpr/codegen paths

**Concrete work**

1. Reuse the expression-type data from Phase 2 for constructor-call normalization.
2. Record the selected constructor declaration in sema.
3. Make constructor-lowering sites consume the resolved constructor directly.
4. Remove arity/name-based constructor matching from lowering paths.

**Done when**

- constructor codegen does not re-run overload selection,
- copy-vs-move-sensitive initialization reads sema-owned results instead of reconstructed argument types.

**Read/query first**

- [Evidence 2](#2-codegen-still-performs-its-own-expression-type-reconstruction)
- [Recommended direction / Priority 3](#priority-3-pre-resolve-constructors-in-sema)
- `docs/2026-03-12_IMPLICIT_CAST_SEMA_PLAN.md` Phase 18 notes about constructor matching cleanup

### Phase 4: remove codegen-to-sema callbacks

**Goal**

Make lowering consume precomputed semantic results only; do not ask `SemanticAnalysis` to resolve new language facts during codegen.

**Primary files**

- `src/SemanticAnalysis.h`
- `src/SemanticAnalysis.cpp`
- `src/IrGenerator_Stmt_Control.cpp`

**Concrete work**

1. Ensure range-for normalization always records the dereference target when the body is sema-normalized.
2. Replace the codegen callback to `resolveRangedForIteratorDereference(...)` with a strict read of precomputed data.
3. For normalized bodies, missing dereference resolution becomes `InternalError`.

**Done when**

- range-for lowering no longer calls back into sema for normalized bodies,
- the sema/codegen phase boundary is one-way: sema annotates, codegen consumes.

**Read/query first**

- [Evidence 5](#5-range-for-lowering-still-calls-back-into-semantic-analysis)
- [Recommended direction / Priority 4](#priority-4-remove-codegen-to-sema-callbacks)

### Phase 5: finish call-node consolidation in service of sema-owned lowering

**Goal**

Use the existing call-node consolidation effort to remove duplicate resolution paths permanently instead of patching them one at a time.

**Primary files**

- `docs/2026-04-02-call-node-consolidation-plan.md`
- `src/AstNodeTypes_DeclNodes.h`
- parser/template-substitution/call-lowering files touched by the consolidation plan

**Concrete work**

1. Continue moving call metadata onto the shared call abstraction.
2. Collapse separate semantic-resolution paths for free/member/static-member calls onto one sema-owned pipeline.
3. Collapse separate codegen lowering paths once the shared call abstraction is authoritative.
4. Delete temporary member-to-free fallback conversions once all downstream users read the unified representation directly.

**Done when**

- one normalized call representation feeds semantic resolution and lowering,
- ordinary-call caching from Phase 1 works through the shared call abstraction rather than one legacy node only.

**Read/query first**

- [Recommended direction / Priority 5](#priority-5-finish-call-node-consolidation-with-sema-owned-resolution)
- `docs/2026-04-02-call-node-consolidation-plan.md`

### Why this order

- Phase 1 first, because direct-call fallback recovery is the largest single name-lookup hotspot and it establishes the ordinary-call caching pattern.
- Phase 2 next, because constructor cleanup depends on sema-owned expression typing instead of codegen-side reconstruction.
- Phase 3 after that, because constructor resolution becomes straightforward once Phase 2 provides sema-owned argument types.
- Phase 4 can then remove the remaining sema callback in lowering with the same “normalized body must already be resolved” rule.
- Phase 5 last, because call-node consolidation is the broad cleanup that should consume the sema-owned resolution model from the earlier phases rather than invent a parallel one.

### Guardrails for every phase

- For sema-normalized bodies, missing semantic resolution should become `InternalError`, not another codegen lookup pass.
- Prefer adding or extending sema side tables over encoding more fallback logic in `IrGenerator_*`.
- Keep layout/runtime queries in codegen, but move language lookup and overload/type decisions into sema.
- Re-run targeted tests around the touched lowering path first, then run the normal repository validation commands before finishing larger slices.

## Question

Does the codegen / IR generation layer still do too much lookup and semantic recovery by name instead of consuming semantic-analysis results?

## Short answer

Yes.

The current pipeline is already moving toward a sema-first design, but codegen still contains several places where it:

- rescans symbol tables by name to re-find call targets,
- reconstructs expression types for overload decisions,
- walks struct hierarchies to recover members,
- and in one case calls back into `SemanticAnalysis` during lowering.

The repo already treats this as architectural debt. `REVIEW.md` explicitly asks reviewers to look for code generation doing lookups or fallbacks due to missing semantic-analysis logic, and earlier plans already describe semantic normalization as the intended direction.

## Evidence

### 1. Direct-call lowering still re-resolves callees by name

`src/IrGenerator_Call_Direct.cpp:620-830` contains a long recovery chain after a `FunctionCallNode` already points at a selected declaration:

- local/global `lookup_all(...)` rescans by unqualified name,
- direct rescans against `gSymbolTable`,
- fallback mangled-name lookup through `lookupSymbol(...)`,
- recursive member-function search through the current struct and its base classes,
- extra defensive pointer scans to recover a matching overload.

This is the clearest example of duplicate work. The node already carries declaration and mangled-name data (`src/AstNodeTypes_DeclNodes.h:2170-2214`), but codegen still has to recover the final callable through multiple name-based fallback paths.

### 2. Codegen still performs its own expression-type reconstruction

`src/IrGenerator_Stmt_Decl.cpp:68-153` implements `buildCodegenOverloadResolutionArgType(...)`.

That helper infers argument types inside codegen from:

- identifiers via declaration lookup,
- member access via `resolveMemberAccessType(...)`,
- cast nodes,
- constructor calls,
- initializer-list construction,
- free-function calls,
- member-function calls.

This is semantic work. It exists mainly so codegen can choose constructors and preserve copy/move behavior later in the same file (`src/IrGenerator_Stmt_Decl.cpp:155-190` and nearby constructor-overload sites).

### 3. Member access lowering still recovers member information by name

`src/IrGenerator_MemberAccess.cpp:3495-3540` resolves member access types by:

1. asking `buildCodegenOverloadResolutionArgType(...)` for the base type,
2. loading `StructTypeInfo`,
3. linearly scanning `struct_info->members` for the requested member name.

This is smaller in scope than the call-resolution case, but it is still another place where codegen reconstructs semantic facts from names and AST shape instead of consuming a pre-resolved result.

### 4. Some expression lowering still does ad-hoc identifier lookups

`src/IrGenerator_Expr_Conversions.cpp:1118-1260` contains several `lookupSymbol(...)` calls to rediscover declaration/type information while lowering:

- member increment/decrement handling,
- lambda-to-function-pointer decay through identifiers,
- fallback type recovery for closure objects.

Some of these are practical implementation shortcuts, but they still show codegen depending on fresh lookups instead of normalized semantic data.

### 5. Range-for lowering still calls back into semantic analysis

`src/IrGenerator_Stmt_Control.cpp:1034-1039` does this:

- use `node.resolved_dereference_function()` if available,
- otherwise call `sema_->resolveRangedForIteratorDereference(...)`.

That callback is especially important because it crosses the phase boundary in the wrong direction: codegen is not just reading sema annotations, it is asking sema to do more resolution work during lowering.

## What already exists on the semantic-analysis side

`src/SemanticAnalysis.h:62-115` and `src/SemanticAnalysis.h:284-320` show that the compiler already has the right kind of infrastructure:

- generic expression semantic slots,
- compound-assignment back-conversion slots,
- resolved `operator()` side tables,
- resolved `operator[]` side tables,
- function-call and member-call reference-binding tables,
- tracking for bodies that sema has normalized.

So the main problem is not missing storage. The bigger issue is that only some semantic facts are cached today, while ordinary call resolution, constructor resolution, member resolution, and general expression typing still leak into codegen.

## What looks legitimate vs suspicious

### Legitimate / acceptable in the current architecture

- Local scope lookup for already-lowered variables through `AstToIr::lookupSymbol(...)` in `src/IrGenerator_Helpers.cpp:423-439`.
- Runtime-oriented struct/member access once sema has already decided what entity is being used.
- Lazy type-info/member-info access when codegen needs layout or offsets, not language lookup.

### Suspicious / duplicate work

- Re-resolving direct calls from symbol tables by name in `IrGenerator_Call_Direct.cpp`.
- Reconstructing expression types in `buildCodegenOverloadResolutionArgType(...)`.
- Choosing constructors in codegen by rebuilding overload argument types.
- Recovering member access targets from names in `resolveMemberAccessType(...)`.
- Calling back into `SemanticAnalysis` during range-for lowering.

## Why this is fragile

These fallbacks make the compiler less robust because codegen must guess the same answer that sema was supposed to decide earlier.

That creates several failure modes:

- parser/sema/codegen can disagree on which declaration is “the” selected one,
- template instantiations can need pointer-equality or mangled-name recovery hacks,
- delayed parsing and base-class walking can hide bugs instead of surfacing them early,
- codegen becomes dependent on symbol-table shape instead of semantically normalized AST state.

The long fallback chains in `IrGenerator_Call_Direct.cpp` are the strongest sign of this: they are defensive code added because the semantic result is not trusted enough to be consumed directly.

## Recommended direction

### Priority 1: cache resolved ordinary calls, not just operator calls

Extend the existing sema side-table approach so ordinary `FunctionCallNode` / unified call nodes carry or reference the final `FunctionDeclarationNode*`.

That would let `IrGenerator_Call_Direct.cpp` stop rescanning symbol tables and stop walking base classes just to rediscover the same callable.

### Priority 2: move general expression typing fully into sema

`buildCodegenOverloadResolutionArgType(...)` should disappear over time.

Codegen should read normalized expression type/category information instead of reconstructing it from identifiers, member accesses, casts, and nested call nodes.

### Priority 3: pre-resolve constructors in sema

Constructor selection currently depends on codegen-side type rebuilding.

Once expression types and ordinary call targets are cached in sema, constructor resolution should move with them.

### Priority 4: remove codegen-to-sema callbacks

The range-for dereference fallback should become a hard expectation on precomputed semantic data.

If sema did not resolve the iterator dereference for a normalized body, codegen should treat that as an internal compiler bug rather than re-running resolution.

### Priority 5: finish call-node consolidation with sema-owned resolution

`docs/2026-04-02-call-node-consolidation-plan.md` already points in the same direction: unify call representation, merge lookup/conversion logic onto one semantic abstraction, and remove member-to-free fallback conversions in codegen.

That work is a good vehicle for removing duplicate lookup logic instead of patching each `IrGenerator_*` site independently.

## Bottom line

The suspicion is correct: codegen still does too many lookups by name and too much semantic recovery.

The good news is that the repository already has:

- explicit architectural guidance against this,
- a semantic-analysis pass in the pipeline,
- side-table infrastructure that can hold resolved facts,
- and an active call-node consolidation effort that aligns with the cleanup.

So the next robust step is not another round of smarter codegen fallbacks. It is to keep pushing semantic resolution earlier and make codegen trust pre-resolved semantic data, with `InternalError` for normalized bodies when that data is missing.
