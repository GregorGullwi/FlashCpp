# 2026-04-04 codegen lookup investigation

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
