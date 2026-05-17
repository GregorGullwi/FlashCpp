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

### Progress update (2026-05-16)

Initial Stage 5 groundwork is now in place:

- `SemanticAnalysis` owns an explicit non-virtual `ParserSemanticServices` boundary for parser-safe operations.
- `SemanticAnalysis` now exposes named lifecycle/state queries for parser attachment and post-parse normalization start/completion.
- constexpr evaluation and pending-root normalization call sites have started routing through `ParserSemanticServices` instead of reaching straight into the full post-parse owner API.
- the handoff into `AstToIr` now asserts that post-parse normalization completed before codegen consumes finalized semantic data.

### Progress update (2026-05-16, continued)

Further null-pointer elimination in internal helpers:

- `TemplateEnvironment::semantic_context` was confirmed unused and removed.
- Three helpers in `Parser_Templates_Inst_ClassTemplate.cpp` that forwarded a nullable `Parser*` into `EvaluationContext::sema` now take `Parser&`: the ternary `parser ? &parser->semanticAnalysis() : nullptr` assignment is gone.
- Four file-static conversion-operator helpers in `SemanticAnalysis.cpp` (`findStructPointerConversionOperator`, `collectAllStructPointerConversionOperators`, `hasAmbiguousPointerConversionOperators`, `structHasConversionOperatorTo`) changed from `SemanticAnalysis*` to `SemanticAnalysis&`, removing all null guards on the sema parameter at those call sites.

### Progress update (2026-05-17)

The Stage 5 structural split is now explicit in code:

- post-parse whole-translation-unit orchestration now lives in a dedicated internal `PostParseSemanticNormalizer` layer instead of being open-coded directly inside `SemanticAnalysis::run()` / `SemanticAnalysis::normalizePendingSemanticRoots()`
- `SemanticAnalysis` remains the compilation-lifetime owner/coordinator and delegates whole-TU normalization work into that dedicated layer
- `Parser::normalizePendingSemanticRootsIfAvailable()` now routes through `semantic_analysis_.parserSemanticServices().normalizePendingSemanticRoots()` instead of bypassing the parser-safe boundary
- the remaining Stage 5 work is now narrower: phase-aware side-table/query contracts and additional parser-safe query migration, rather than the high-level owner/normalizer split itself

### Progress update (2026-05-17, continued)

Resolved-call query state is now phase-aware instead of using bare null as the only contract:

- parser-safe resolved direct-call and `operator()` lookups now have explicit query-state APIs that distinguish `NotYetAnalyzed`, `AnalyzedAbsent`, and `Available`
- sema records when those call-query families were actually examined, so callers no longer have to infer phase from object lifetime or from a missing map entry
- constexpr direct-call reuse now goes through the explicit direct-call query API, so the parser-safe boundary exercises the new contract directly
- the remaining Stage 5 side-table work is still broader than call resolution alone: expression-type, overload-argument-type, member-access, and similar tables still need the same phase-aware audit

Remaining Stage 5 work:

- continue the side-table audit so additional families beyond resolved-call lookups distinguish "not analyzed yet" from "analyzed and absent"

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

### Progress update (2026-05-16)

Initial Stage 6 codegen invariants are now in place:

- `AstToIr` now requires `SemanticAnalysis&` at construction time instead of receiving semantic data through a later setter call.
- the `setSemanticData(...)` plumbing is gone, so codegen cannot accidentally start in a semantic-null state anymore.
- IR generation helpers now treat semantic analysis as mandatory infrastructure and only branch on whether a particular semantic fact was available.

### Progress update (2026-05-16, continued)

Nullable sema elimination continued in template-instantiation and conversion-operator helpers:

- `Parser*` → `Parser&` in `makeStaticMemberInitializerEvaluationContext` and its two callers; the `parser ? &parser->semanticAnalysis() : nullptr` ternary assignment into `EvaluationContext::sema` is eliminated.
- `SemanticAnalysis*` → `SemanticAnalysis&` in four file-static conversion-operator helpers in `SemanticAnalysis.cpp`; null guards removed.

### Progress update (2026-05-17)

Parser-owned constexpr flows now tighten their semantic contract further:

- dependent-unqualified constexpr call reuse now requires a sema-backed `EvaluationContext` when the context is parser-owned, instead of silently skipping sema reuse on a missing pointer
- lazy constexpr member materialization now requires sema for parser-owned contexts and no longer falls back to direct parser-side instantiate/normalize bookkeeping
- parser-owned `EvaluationContext::normalizePendingSemanticRoots()` now throws on missing sema instead of quietly becoming a no-op
- `EvaluationContext::sema` itself is still nullable overall because standalone non-parser evaluator callers remain, but the parser/template/member-function paths are now closer to the intended Stage 6 invariant

Remaining Stage 6 work:

- finish auditing parser-owned constexpr/template/substitution construction sites so `EvaluationContext::sema` can eventually stop being nullable as a field, not just as an enforced parser-owned invariant
- `EvaluationContext::sema` is still a nullable pointer with a single `if (sema != nullptr)` guard in `normalizePendingSemanticRoots()`; making it non-nullable requires auditing all EvaluationContext creation sites that do not set sema
- continue replacing parser/codegen fallback ambiguity with explicit "fact unavailable yet" contracts
- keep tightening finalized-query misuse into hard invariants once the remaining fallback families are retired

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
