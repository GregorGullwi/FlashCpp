# 2026-05-16 Sema First-Class Architecture Plan

## Context

The first-stage refactor makes `SemanticAnalysis` exist before `Parser` construction and makes `Parser` hold a non-null semantic dependency directly. This removes the temporary active-sema registration path, but it intentionally does not finish the larger semantic-state cleanup.

The remaining work is to separate:

- always-valid semantic services that parser/template/constexpr code can call at any time
- post-parse whole-translation-unit normalization work
- queries whose answer may legitimately be "not known yet"

## Goal

Stop modeling semantic availability as nullable object plumbing. Callers should assume semantic infrastructure exists and should instead branch only on whether a specific semantic fact is available.

## Stage 5: Split Always-Valid Services From Post-Parse Normalization

### Progress update

Stage 5 progress so far:

- `SemanticAnalysis` owns an explicit non-virtual `ParserSemanticServices` boundary for parser-safe operations.
- `SemanticAnalysis` now exposes named lifecycle/state queries for parser attachment and post-parse normalization start/completion.
- constexpr evaluation and pending-root normalization call sites have started routing through `ParserSemanticServices` instead of reaching straight into the full post-parse owner API.
- the handoff into `AstToIr` now asserts that post-parse normalization completed before codegen consumes finalized semantic data.
- `TemplateEnvironment::semantic_context` was confirmed unused and removed.
- three helpers in `Parser_Templates_Inst_ClassTemplate.cpp` that forwarded a nullable `Parser*` into `EvaluationContext::sema` now take `Parser&`: the ternary `parser ? &parser->semanticAnalysis() : nullptr` assignment is gone.
- four file-static conversion-operator helpers in `SemanticAnalysis.cpp` (`findStructPointerConversionOperator`, `collectAllStructPointerConversionOperators`, `hasAmbiguousPointerConversionOperators`, `structHasConversionOperatorTo`) changed from `SemanticAnalysis*` to `SemanticAnalysis&`, removing all null guards on the sema parameter at those call sites.
- post-parse whole-translation-unit orchestration now lives in a dedicated internal `PostParseSemanticNormalizer` layer instead of being open-coded directly inside `SemanticAnalysis::run()` / `SemanticAnalysis::normalizePendingSemanticRoots()`.
- `SemanticAnalysis` remains the compilation-lifetime owner/coordinator and delegates whole-TU normalization work into that dedicated layer.
- `Parser::normalizePendingSemanticRootsIfAvailable()` now routes through `semantic_analysis_.parserSemanticServices().normalizePendingSemanticRoots()` instead of bypassing the parser-safe boundary.
- parser-safe resolved direct-call and `operator()` lookups now have explicit query-state APIs that distinguish `NotYetAnalyzed`, `AnalyzedAbsent`, and `Available`.
- sema records when those call-query families were actually examined, so callers no longer have to infer phase from object lifetime or from a missing map entry.
- constexpr direct-call reuse now goes through the explicit direct-call query API, so the parser-safe boundary exercises the new contract directly.
- parser-safe overload-argument-type queries now expose `NotYetAnalyzed`, `AnalyzedAbsent`, and `Available` instead of making callers guess from a missing cache entry.
- sema now records when overload-resolution argument typing was actually attempted for an expression node, even when no type could be produced.
- the eager `getOverloadResolutionArgType(...)` API remains in place for existing callers, but it now has a sibling query API for phase-aware consumers and tests.
- parser-safe expression-type queries now also distinguish `NotYetAnalyzed`, `AnalyzedAbsent`, and `Available` instead of treating a missing semantic slot as the only state signal.
- expression normalization now records that an expression node was sema-visited even when no semantic type slot was produced, so expression-type queries no longer have to guess phase from slot presence alone.
- subscript-resolution queries now use the same explicit `NotYetAnalyzed` / `AnalyzedAbsent` / `Available` contract, so parser-safe `operator[]` lookups no longer have to infer phase from a missing side-table entry.
- after the remaining code audit, the unresolved nullable/empty lookups are now mostly narrower specialized caches (`identifier` / `qualified-identifier` / member-access / unary-dereference / structured-binding) that are primarily codegen/finalized consumers rather than the broad parser-safe query families Stage 5 set out to split and phase-harden.

Remaining Stage 5 work:

- Stage 5 core work is complete for the parser-safe services and resolved-call/query families called out above; any further query-state tightening is now narrower follow-on work on specialized finalized/codegen-side caches and fits better under Stage 6 API-contract/finalized-query hardening

### 5.1 Define the internal split inside `SemanticAnalysis`

Create explicit internal layers:

- `SemanticAnalysis` as the compilation-lifetime owner/coordinator
- `ParserSemanticServices` for operations valid during parsing and instantiation
- `PostParseSemanticNormalizer` for the current `run()` whole-TU sweep

The exact class names can change, but the boundary should be explicit in code.

### 5.2 Move parser-phase operations into the always-valid layer

Candidate operations:

- `normalizePendingSemanticRoots()`
- `ensureMemberFunctionMaterialized(...)`
- direct/operator/subscript resolved-call lookup helpers
- parser/constexpr-facing expression-type queries that are already safe pre-`run()`
- lazy-member ODR-use marking

These should be callable without implying that whole-TU normalization has already completed.

### 5.3 Make state transitions explicit

Add small, named state queries instead of relying on object lifetime:

- parser attached
- post-parse normalization started
- post-parse normalization completed

This should replace implicit assumptions such as "if sema exists, final semantic answers should exist too."

### 5.4 Isolate post-parse-only data flows

Audit side tables and categorize them:

- built incrementally during parsing
- built during pending-root normalization
- built only by the whole-TU sweep

Any code reading a table should be able to tell whether an empty lookup means:

- not analyzed yet
- analyzed and absent
- deliberately unsupported in that phase

## Stage 6: Make Callers Depend On Semantic Facts, Not Semantic Presence

### Progress update

Stage 6 progress so far:

- `AstToIr` now requires `SemanticAnalysis&` at construction time instead of receiving semantic data through a later setter call.
- the `setSemanticData(...)` plumbing is gone, so codegen cannot accidentally start in a semantic-null state anymore.
- IR generation helpers now treat semantic analysis as mandatory infrastructure and only branch on whether a particular semantic fact was available.
- `Parser*` → `Parser&` in `makeStaticMemberInitializerEvaluationContext` and its two callers; the `parser ? &parser->semanticAnalysis() : nullptr` ternary assignment into `EvaluationContext::sema` is eliminated.
- `SemanticAnalysis*` → `SemanticAnalysis&` in four file-static conversion-operator helpers in `SemanticAnalysis.cpp`; null guards removed.
- dependent-unqualified constexpr call reuse now requires a sema-backed `EvaluationContext` when the context is parser-owned, instead of silently skipping sema reuse on a missing pointer.
- lazy constexpr member materialization now requires sema for parser-owned contexts and no longer falls back to direct parser-side instantiate/normalize bookkeeping.
- parser-owned `EvaluationContext::normalizePendingSemanticRoots()` now throws on missing sema instead of quietly becoming a no-op.
- parser-owned dependent-unqualified constexpr reuse now always performs sema query through the parser-owned sema requirement before POI fallback, instead of guarding that sema query behind `if (sema != nullptr)`.
- parser-owned lazy constexpr member materialization no longer uses mixed `parser || sema` gating; since every `EvaluationContext` construction site always sets both `parser` and `sema` together, the materialization guard now uses a single `if (sema != nullptr)` check that covers both parser-owned and standalone sema-only callers.
- `EvaluationContext::normalizePendingSemanticRoots()` now uses a single `if (sema != nullptr)` check; the redundant parser-first branch is removed since sema is always set whenever parser is set.
- `EvaluationContext::normalizePendingSemanticRoots()` now hard-fails when `parser` is set but `sema` is missing, while still allowing standalone non-parser evaluator contexts without sema to return early.
- constexpr member-function materialization lookup/replay now uses a shared parser-owned sema requirement helper: parser-owned contexts throw on missing sema instead of silently skipping sema materialization.
- parser-owned constexpr sema invariants now route through a single `EvaluationContext::requireParserOwnedSema(...)` helper, and dependent-unqualified call reuse plus member-materialization lookup/replay now consume that shared contract instead of file-local nullable helper variants.
- `Evaluator::evaluate(...)` now enforces the parser-owned sema invariant at the constexpr entrypoint: parser-owned contexts (`parser != nullptr`) hard-fail immediately when `sema` is missing instead of allowing deeper nullable behavior.
- parser-owned constexpr/template/substitution `EvaluationContext` setup now goes through explicit parser-owned construction instead of open-coding paired `parser`/`sema` field assignments at each parser construction site.
- that parser-side construction-site audit now covers the parser-owned constexpr/template/substitution call families called out in Stage 6.1, reducing drift while `EvaluationContext::sema` remains nullable for standalone/non-parser contexts.
- `EvaluationContext::sema` itself is still nullable overall because standalone non-parser evaluator callers remain, but the parser/template/member-function paths are now closer to the intended Stage 6 invariant.
- the `struct-without-conversion-operator` codegen fallback has been removed: the conditional `if (sema_normalized_current_function_)` guard is now an unconditional `InternalError` since all 2393 tests pass without the fallback path; lambda/requires cases confirmed to not exercise struct-to-non-struct return lowering at all.
- `AstToIr::parser_` changed from `Parser*` to `Parser&`: AstToIr always takes a `Parser&` at construction, so the field was a pointer that was never null; converting it to a reference removes 13 defensive `if (parser_)` / `if (!parser_)` null guards in IrGenerator files and changes 18 `parser_->method()` calls to `parser_.method()`.
- `AstToIr::makeEvalContext(const SymbolTable&)` added: centralises `EvaluationContext` creation for codegen; the 17+ manual `ctx.parser = &parser_; ctx.sema = &sema_` assignment pairs scattered across IrGenerator files are replaced with a single `makeEvalContext(...)` call, so every codegen-created context is guaranteed to receive parser and sema.
- additional codegen constexpr-evaluation sites in `IrGenerator_MemberAccess.cpp`, `IrGenerator_NewDeleteCast.cpp`, and `IrGenerator_Expr_Conversions.cpp` now build contexts via `makeEvalContext(...)` instead of direct `EvaluationContext(symbol_table)` construction, so these paths no longer drop parser/sema wiring.
- remaining direct codegen constexpr-evaluation sites in `IrGenerator_Expr_Primitives.cpp`, `IrGenerator_Visitors_Decl.cpp`, and `IrGenerator_Stmt_Decl.cpp` now also route through `makeEvalContext(...)`; this shrinks the remaining direct-construction tail to helper-style sites (for example multidimensional member-array dimension evaluation in `IrGenerator_MemberAccess.cpp`) that are not currently wired through `AstToIr::makeEvalContext`.
- parser-owned lazy static-member materialization in constexpr/codegen no longer calls `Parser::instantiateLazyStaticMember(...)` directly: `ParserSemanticServices::tryInstantiateLazyStaticMember(...)` now owns that parser-attached entrypoint, and the affected call families in `ConstExprEvaluator_Core.cpp`, `ConstExprEvaluator_Members.cpp`, and `IrGenerator_Visitors_TypeInit.cpp` route through sema services instead of parser plumbing.
- codegen overload-argument typing in `AstToIr::buildCodegenOverloadResolutionArgType(...)` now consumes `ParserSemanticServices::getOverloadResolutionArgTypeQuery(...)` and treats `NotYetAnalyzed` as an invariant violation in sema-normalized bodies instead of silently collapsing all missing answers into parser fallback behavior.
- binary-operator overload-argument typing in `IrGenerator_Expr_Operators.cpp` now also consumes the explicit overload-arg query API before parser fallback, so this codegen family no longer depends on nullable sema lookup plumbing.
- `AstToIr::generateMemberFunctionCallIr(...)` now resolves callable `operator()` receivers through parser-semantic query-state APIs (`getResolvedOpCallQuery` + `getExpressionTypeQuery`), keeping legacy parser fallback for non-normalized/`AnalyzedAbsent` cases while enforcing `NotYetAnalyzed` as an invariant violation in sema-normalized bodies.
- `AstToIr::generateFunctionCallIr(...)` now routes sema-owned direct-call target consumption through `ParserSemanticServices::getResolvedDirectCallQuery(...)` and treats `NotYetAnalyzed` as an invariant violation in sema-normalized bodies (unless sema explicitly recorded the call as unresolved), while preserving legacy parser lookup recovery for non-normalized paths.
- `AstToIr::getCallExpressionReturnType(...)` now consumes `ParserSemanticServices::getExpressionTypeQuery(...)`; sema-normalized bodies hard-fail on `NotYetAnalyzed`, while parser fallback is retained for non-normalized flow and placeholder/invalid type recovery.
- inline_always single-argument lowering in `AstToIr::generateFunctionCallIr(...)` now queries argument types through `ParserSemanticServices::getExpressionTypeQuery(...)`, hard-fails on `NotYetAnalyzed` in sema-normalized bodies, and keeps parser fallback for unavailable/placeholder/invalid-type recovery.
- `AstToIr::generateMemberFunctionCallIr(...)` now routes member-call receiver struct-type lookup in `resolveStructTypeFromReceiverNode(...)` through `ParserSemanticServices::getExpressionTypeQuery(...)`, treating `NotYetAnalyzed` as an invariant violation in sema-normalized bodies while preserving identifier/static-cast and parser fallback recovery for non-normalized flow.
- `AstToIr::generateArraySubscriptIr(...)` now consumes `ParserSemanticServices::getResolvedOpSubscriptQuery(...)` before deciding whether `operator[]` dispatch should fall back to builtin array indexing, and sema-normalized bodies now treat `NotYetAnalyzed` subscript-resolution state as an invariant violation instead of quietly skipping to legacy fallback logic.
- the function-pointer `operator()` branch in `AstToIr::generateFunctionCallIr(...)` now also consumes `ParserSemanticServices::getResolvedOpCallQuery(...)`, so sema-normalized direct-call lowering hard-fails on `NotYetAnalyzed` instead of silently treating an unfinished callable lookup as ordinary fallback flow.
- ternary common-type fallback in `IrGenerator_Expr_Operators.cpp` now consumes `ParserSemanticServices::getExpressionTypeQuery(...)` for both branch expressions, making sema-normalized ternary lowering fail explicitly on `NotYetAnalyzed` branch-type queries instead of reading nullable sema slots.
- `typeid(expr)` lowering in `IrGenerator_NewDeleteCast.cpp` now consumes `ParserSemanticServices::getExpressionTypeQuery(...)` for the operand's static type/reference query, so sema-normalized bodies hard-fail on `NotYetAnalyzed` instead of silently collapsing incomplete semantic typing into the legacy runtime/type-index fallback path.
- generic-lambda argument typing in `IrGenerator_Call_Indirect.cpp` now consumes `ParserSemanticServices::getExpressionTypeQuery(...)`, so sema-normalized member-call lowering fails explicitly on `NotYetAnalyzed` argument-type queries before falling back to the existing symbol/closure/literal deduction helpers.
- builtin `++`/`--` fallback typing in `IrGenerator_Expr_Conversions.cpp` now also consumes `ParserSemanticServices::getExpressionTypeQuery(...)`, but only hard-fails on `NotYetAnalyzed` where the recovery path actually depends on sema-owned expression typing; identifier/member/dereference pointer-shape recovery still keeps the older parser/declaration-based fallback so Duff's-device-style code continues to lower correctly.
- parser-owned normalization callers now invoke `Parser::normalizePendingSemanticRoots()` directly; the old `normalizePendingSemanticRootsIfAvailable()` name is gone because parser-attached semantic normalization is no longer an optional capability in these paths.
- parser-owned constexpr member-function materialization lookup/replay in `ConstExprEvaluator_Members.cpp` now calls `requireParserOwnedSema(...)` directly instead of routing parser-backed contexts through a nullable helper first; standalone sema-only evaluator contexts still keep the explicit nullable fallback.
- sema-owned structured-binding tuple-size constexpr evaluation now uses explicit parser-owned construction whenever a parser is attached instead of open-coding paired `parser`/`sema` assignment; standalone sema-only evaluation still sets only `sema` explicitly.
- the constexpr evaluation entrypoint in `ConstExprEvaluator_Core.cpp` now enforces parser-owned sema attachment via `requireParserOwnedSema(\"evaluate\")` instead of duplicating the parser+sema invariant check inline.
- parser-required constexpr POI/variable-template paths now bind a local `Parser&` immediately after their null boundary checks, and `ConstExprEvaluator_Members.cpp` no longer carries a dead `context.parser == nullptr` guard inside parser-only template-owner materialization or duplicated parser-presence conditions for dependent expression substitution.
- dependent template-argument owner rebinding in `ConstExprEvaluator_Members.cpp` now explicitly skips parser-backed positional template-parameter lookup when no parser is attached, and the parser-only owner/nested-alias materialization branches bind local `Parser&` references instead of repeatedly dereferencing nullable `context.parser` inside already-guarded scopes.
- array-bound evaluation in `applyDeclarationArrayBoundsToTypeSpec(...)` is now sema-backed in both semantic-normalization and parser argument-type callers: sema-owned sites pass `SemanticAnalysis`, parser-owned sites pass `Parser`, and neither path builds a bare constexpr `EvaluationContext` anymore. substituted alias array-dimension evaluation in `Parser_Templates_Inst_ClassTemplate.cpp` now also carries parser-owned sema before evaluating the substituted bound expressions.
- member-access codegen now routes the multidimensional array-dimension fallback helper through `AstToIr::makeEvalContext(...)` instead of constructing a bare constexpr context locally, so this remaining codegen-owned dimension-evaluation path now receives parser + sema by construction too.
- `EvaluationContext` now has explicit sema-backed modes for both parser-owned and standalone evaluation via dedicated constructors, `requireSema(...)` / `requireParserAttachedSema(...)` centralize the remaining invariants, `requireParserOwnedSema(...)` now also verifies that `parser` and `sema` come from the same owner, `AstToIr::tryEvaluateAsConstExpr(...)` now uses `makeEvalContext(...)`, and `ConstExpr::Evaluator::evaluate(...)` hard-fails immediately on any bare sema-less context that still slips through.
- `EvaluationContext::normalizePendingSemanticRoots()` now requires parser-attached semantic analysis instead of silently no-oping on a null sema, parser-attached member-function materialization now requires parser-attached sema explicitly, and the old nullable `resolveMaterializationSema(...)` seam is gone.
- constexpr `sizeof(array[index])` now falls back through `global_symbols` before expression-type decay, so inferred global arrays keep the correct element size during codegen folding, and `sizeof(S{0})` on a struct prvalue now stays locked in as a passing regression test instead of an expected failure; the regression now also covers mixed-layout and templated prvalue cases.
- `EvaluationContext` now also has explicit parser-owned and sema-only constructors, and both the shared helpers and the remaining parser-owned call sites now use those constructors directly instead of constructing a raw context and attaching semantic ownership afterward. The final array-bounds parser helper now also takes `Parser&` instead of a dishonest `const Parser&`, because its constexpr evaluation can instantiate templates and normalize pending semantic roots.
- dependent-unqualified call POI resolution in both `ConstExprEvaluator_Core.cpp` and `ConstExprEvaluator_Members.cpp` now requires parser-owned sema via `requireParserOwnedSema(...)` directly instead of first probing `context.parser` with ad-hoc null checks.
- Stage 6 Slice A complete: dependent-unqualified constexpr POI call resolution in `ConstExprEvaluator_Core.cpp` and `ConstExprEvaluator_Members.cpp` now handles `getResolvedDirectCallQuery(...)` states explicitly (`Available` consumes sema result, `AnalyzedAbsent` allows POI fallback, `NotYetAnalyzed` hard-fails once parser-attached post-parse normalization has started while keeping pre-normalization fallback behavior).
- Stage 6 Slice B complete: member-call receiver typing in `IrGenerator_Call_Indirect.cpp` now gates parser fallback behind explicit semantic query states in both `generateMemberFunctionCallIr(...)` and `resolveStructTypeFromReceiverNode(...)`; sema-normalized bodies hard-fail on `NotYetAnalyzed`, while non-normalized paths keep recovery fallback for `AnalyzedAbsent` and explicitly unusable available results.
- Stage 6 Slice C complete: non-identifier range-expression typing in `AstToIr::visitRangedForStatementNode(...)` now queries `ParserSemanticServices::getExpressionTypeQuery(...)` first, treats `NotYetAnalyzed` as an invariant violation in sema-normalized bodies, and restricts parser fallback to explicit recovery states (`AnalyzedAbsent`, unusable `Available`, and non-normalized `NotYetAnalyzed` recovery).
- Stage 6 Slice D complete: `ExpressionSubstitutor::substituteFunctionCallImpl(...)` now keeps explicit-template call substitution in template-aware unresolved form when variable-template/dependent-template instantiation cannot be completed, instead of silently dropping into generic callable fallback flow.
- parser-gated constexpr template instantiation in `ConstExprEvaluator_Core.cpp` now routes parser access through `requireParserOwnedSema(...)` directly; the old parser-null guard/error branch in `tryEvaluateAsVariableTemplate(...)` was removed.
- template-function constexpr fallback instantiation in `ConstExprEvaluator_Core.cpp` now also requires parser-owned sema unconditionally before parser use, removing the remaining `if (context.parser)` gate in that parser-required instantiation path.

Remaining Stage 6 work:

- `EvaluationContext` still has a raw symbol-table constructor for any future genuinely mode-delayed construction, but the last live parser-owned construct-then-attach path is now gone
- continue replacing parser/codegen fallback ambiguity with explicit "fact unavailable yet" contracts
- keep tightening finalized-query misuse into hard invariants once the remaining fallback families are retired
- struct-without-conversion-operator codegen fallback removed; sema return-conversion is now fully enforced for struct returns ✅
- `parser_` in `AstToIr` is now `Parser&` — no more defensive null guards in codegen ✅
- codegen-side `EvaluationContext` construction is now further centralised via `makeEvalContext`, and the converted call sites carry parser + sema by construction

### 6.1 Remove remaining nullable semantic branches in parser-owned contexts

Target:

- constexpr evaluation contexts
- template instantiation helpers
- expression substitution
- parser-created semantic contexts

Replace `if (sema)` checks with direct semantic calls and allow the called API to return "fact unavailable" where appropriate.

### 6.2 Tighten API contracts

Split APIs into clear categories:

- total operations: always callable, always meaningful
- partial queries: always callable, may return empty
- finalized queries: only legal after post-parse normalization

For finalized queries, prefer hard assertions or `InternalError` on misuse instead of quiet fallback logic.

### 6.3 Remove parser/codegen fallback ambiguity gradually

For each family of logic, move from:

- sema if available, otherwise parser/codegen fallback

to:

- semantic service always available
- semantic query returns a precise answer or explicit "not available yet"

Priority families:

1. constexpr direct-call reuse and lazy-member materialization
2. overload-resolution argument typing
3. expression-type queries used by codegen fallback paths
4. remaining codegen `if (sema_)` branches that should become invariants

### 6.4 End state for codegen

Codegen should treat semantic analysis as mandatory input:

- pass `SemanticAnalysis&` into `AstToIr` constructor
- remove `setSemanticData(...)`
- delete optional semantic branches in IR generation

At that point, codegen nullability should be considered a bug.

## Suggested Execution Order

1. Inventory semantic APIs by phase: parser-safe, pending-root-safe, post-parse-only.
2. Extract parser-safe operations behind a dedicated internal interface/class.
3. Convert constexpr/template helpers to the parser-safe interface and remove nullable branches there.
4. Convert `AstToIr` construction to require semantic analysis directly. ✅
5. Remove legacy parser/codegen fallback paths once invariants are enforced.

## Risks To Watch

- re-entrant normalization loops between parser lazy materialization and semantic normalization
- callers that currently use empty semantic answers to mean both "not yet analyzed" and "no semantic result"
- codegen branches that still silently recover through parser state when semantic ownership should be mandatory
- test helpers that accidentally run post-parse normalization multiple times on the same semantic object without resetting expectations

## Success Criteria

- no parser-owned context needs to ask whether semantic infrastructure exists
- post-parse normalization is a named phase, not the condition for semantic existence
- semantic queries communicate availability through their result contract, not through nullable object plumbing
- codegen requires semantic analysis rather than optionally consuming it
