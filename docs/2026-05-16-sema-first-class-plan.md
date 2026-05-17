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
- parser-owned constexpr/template/substitution `EvaluationContext` setup now goes through a shared `attachParserOwnedSema(Parser&)` helper instead of open-coding paired `parser`/`sema` field assignments at each parser construction site.
- that parser-side construction-site audit now covers the parser-owned constexpr/template/substitution call families called out in Stage 6.1, reducing drift while `EvaluationContext::sema` remains nullable for standalone/non-parser contexts.
- `EvaluationContext::sema` itself is still nullable overall because standalone non-parser evaluator callers remain, but the parser/template/member-function paths are now closer to the intended Stage 6 invariant.
- the `struct-without-conversion-operator` codegen fallback has been removed: the conditional `if (sema_normalized_current_function_)` guard is now an unconditional `InternalError` since all 2393 tests pass without the fallback path; lambda/requires cases confirmed to not exercise struct-to-non-struct return lowering at all.
- `AstToIr::parser_` changed from `Parser*` to `Parser&`: AstToIr always takes a `Parser&` at construction, so the field was a pointer that was never null; converting it to a reference removes 13 defensive `if (parser_)` / `if (!parser_)` null guards in IrGenerator files and changes 18 `parser_->method()` calls to `parser_.method()`.
- `AstToIr::makeEvalContext(const SymbolTable&)` added: centralises `EvaluationContext` creation for codegen; the 17+ manual `ctx.parser = &parser_; ctx.sema = &sema_` assignment pairs scattered across IrGenerator files are replaced with a single `makeEvalContext(...)` call, so every codegen-created context is guaranteed to receive parser and sema.

Remaining Stage 6 work:

- `EvaluationContext::sema` is still a nullable pointer for standalone evaluator call sites (array-dimension evaluation, simple constant folding in MemberAccess/NewDelete); making it non-nullable requires either injecting sema at those sites or introducing an explicit "no-sema" opt-in type
- continue replacing parser/codegen fallback ambiguity with explicit "fact unavailable yet" contracts
- keep tightening finalized-query misuse into hard invariants once the remaining fallback families are retired
- struct-without-conversion-operator codegen fallback removed; sema return-conversion is now fully enforced for struct returns ✅
- `parser_` in `AstToIr` is now `Parser&` — no more defensive null guards in codegen ✅
- codegen-side `EvaluationContext` construction centralised via `makeEvalContext`; all codegen contexts carry parser + sema ✅

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
