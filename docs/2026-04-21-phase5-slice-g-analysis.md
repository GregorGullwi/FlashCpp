# Phase 5 Slice G — Analysis and Status
# Evaluated: 2026-04-21

This document was originally written as a forward-looking recommendation after Slice F landed.
It has been updated in place with the current implementation status of each item.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Status legend

  [DONE]    Fully implemented and verified on the current Windows baseline (2204 pass / 149 expected-fail).
  [PARTIAL] Structurally present but the clean-split described below is not yet realised.
  [TODO]    Not yet started.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Progress since the original write-up

The core architectural recommendation from this document has largely landed already:

 - Slice G-H (`79790edd`) introduced explicit lazy-member ODR-use tracking plus the sema-side remap for intra-instantiation member calls.
 - Slices I+J (`53327120`) deleted the last live `AstToIr::materializeLazyMemberIfNeeded` forwarder and simplified the codegen struct visitor into a pure consumer.
 - Item #8 below was finished afterwards on this branch (`bd49098c`): `registerLateMaterializedTopLevelNode*` now dedups by `raw_pointer()` identity instead of relying on callers not to double-push.
 - The stale post-forwarder comments called out below have also been cleaned up (`da472bdc`).
 - Item #9 below is now finished on this branch as well: `ReachableStructWalker.h` provides the shared reachable-struct traversal used by both `SemanticAnalysis::drainLazyMemberRegistry`
   and codegen's AST walk.
 - Item #6 below is now finished on this branch too: `Log.h` exposes `FLASH_INVARIANT_PROBE` / `FLASH_INVARIANT_PROBE_FORMAT` with off/log/fail modes, and codegen uses a
   `Phase5StructDrain` probe to catch any future regression where a still-lazy member or constructor reaches the struct visitor.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## High-impact architectural changes

### 1. Add an explicit odr_used bit on lazy member registry entries   [DONE]

Every site that currently calls ensureMemberFunctionMaterialized already knows why it's asking: a direct call, a member call, a conversion, a constexpr evaluation, a vtable
seed, an address-of. Turn that into a two-step protocol:

 // Phase 1: record that someone needs the body (cheap, idempotent, no reparse).
 lazy_registry.markOdrUsed(struct_name, member_name, reason);
 
 // Phase 2 (drain): for every entry that is odr_used && !materialized, reparse.

The drain filter becomes odr_used && !materialized instead of the current needsInstantiation (which is really just "was-cloned && !materialized"). This is principled and makes
three things possible:

 - Drain can safely walk every instantiated struct — it only touches members explicitly marked ODR-used. The SFINAE-probed pair<const int, int>::swap is never marked ODR-used,
   so the drain skips it automatically.
 - The codegen forwarder becomes deletable: sites that today call materializeLazyMemberIfNeeded as a first-materializer move their markOdrUsed(...) calls into sema annotation
   passes (Slices B/C already did this for some paths); the final drain then materializes them.
 - Diagnostics improve: the reason is preserved, so "why did this member fail to instantiate?" has an answer ("ODR-used from call at foo.cpp:42").

Current state (DONE): LazyMemberInstantiationRegistry now carries a separate odr_used_ set (TemplateRegistry_Lazy.h). markOdrUsed / markOdrUsedAny / isOdrUsed / isOdrUsedAny
/ snapshotOdrUsedLazyEntries are all implemented. Multiple sema sites in SemanticAnalysis.cpp call markOdrUsed. drainLazyMemberRegistry runs a two-pass fixpoint: an AST-walk
pass (mirrors the old struct-visitor scope) followed by an ODR-use snapshot pass that picks up residuals not reachable from parser_.get_nodes(). The odr_used_ set persists
across markInstantiated so later passes can still answer "was this member ever ODR-used?".

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 2. Split class-template instantiation into shape and body phases with distinct failure modes   [TODO]

Today, try_instantiate_class_template and instantiateLazyMemberFunction mix two fundamentally different responsibilities:

 - Shape substitution — applying the template argument map to member declarations to produce the new struct's layout, signatures, nested types. Must succeed whenever the class
   is well-formed; failure is a hard error.
 - Body substitution — applying the map to each member's body tokens and re-parsing. Can fail legitimately (the body might reference an overload that doesn't exist for this
   specialization). Failure should be ODR-use-triggered: SFINAE-only probes swallow it, real ODR uses promote it to a compile error.

The current data model makes "shape done, body pending" a state expressible only via "member FunctionDeclarationNode has no definition yet". That's a one-bit state. What we
actually need is a proper variant:

 // Conceptual — on FunctionDeclarationNode
 std::variant<
     NotMaterialized{ SubstitutionContext },   // have tokens + arg map, never tried
     Materialized{ Body },                     // fully parsed, ready for IR
     FailedSubstitution{ Diagnostic }          // known ill-formed; SFINAE uses, ODR-use errors
 > body_state_;

This gives us:

 - A uniform query for "is this materialized?" at every call site, killing the scattered get_definition().has_value() checks.
 - A place to cache SFINAE failures so a repeat probe doesn't redo the reparse.
 - A principled way to distinguish "library didn't ask" from "library asked and it failed".

Current state (TODO): FunctionDeclarationNode still uses the one-bit get_definition().has_value() test everywhere. No variant state has been introduced. The existing
"SFINAE enable_if<false> not causing substitution failure in return type" KNOWN_ISSUES entry is a direct consequence of this gap.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 3. Introduce an InstantiationContext value that propagates through instantiation machinery   [TODO]

Right now instantiation knows whether it's in SFINAE via a parser-global in_sfinae_context_ flag. That's a single bit of implicit state. It couldn't catch the pair<const int,
int> case because the struct was instantiated before the SFINAE probe started.

Replace the flag with a value threaded through every call to try_instantiate_class_template, instantiateLazyMemberFunction, substituteType, etc.:

 struct InstantiationContext {
     InstantiationMode mode;       // HardError | SfinaeProbe | ShapeOnly
     SourceLocation origin_site;   // where the instantiation was requested
     const InstantiationContext* parent;  // chain for diagnostic backtraces
 };

Benefits:

 - Every instantiated struct can record the contexts it was ever instantiated under (a set of {SFINAE, HardError, ShapeOnly}). The drain's "is this struct safe to drain all
   members of?" question becomes an explicit data query, not an implicit side effect of AST reachability.
 - Diagnostics get proper instantiation backtraces for free (every chained parent contributes a frame).
 - Specialization for shape-only vs. body requests becomes a type-system question, not a manual flag check.

Current state (TODO): Parser.h still has in_sfinae_context_ as a plain bool. No InstantiationContext struct exists. The "non-SFINAE function-template overload selection uses
first match instead of most-specific" KNOWN_ISSUES entry would also benefit from the richer context chain.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 4. Give late-materialized instantiations their own first-class list, separate from ast_nodes_   [PARTIAL]

The ast_nodes_ vector is currently doing double duty:

 - Source-order list of user-written top-level declarations (its original job).
 - Splice target for late-materialized class-template instantiations (via registerLateMaterializedTopLevelNode).

The latter caused real bugs this session — duplicate push -> LNK2005 — and makes the "what should codegen emit fully vs. on-demand" question unanswerable from the data structure
alone.

Clean split:

 class Parser {
     std::vector<ASTNode> user_source_nodes_;        // immutable after initial parse
     std::vector<ASTNode> instantiated_struct_nodes_;// append-only side list; dedup'd
 };

Codegen then runs two passes with different filters:

 - user_source_nodes_: emit every member (user asked for it by writing it).
 - instantiated_struct_nodes_: emit only odr_used members.

This is exactly the distinction Slice F was groping for; making it explicit in the data model kills an entire class of "accidental proxy" bugs. It also makes
registerLateMaterializedTopLevelNode unnecessary as an ad-hoc hook.

Current state (PARTIAL): A parallel ast_node_is_instantiated_ vector (std::vector<uint8_t>) exists in Parser.h, maintained in lockstep with ast_nodes_ by
appendUserNode / registerLateMaterializedTopLevelNode* / eraseTopLevelNodeAt. The queries isInstantiatedNode(i), userNodeCount(), and instantiatedNodeCount() work. Dedup is
now handled directly by registerLateMaterializedTopLevelNode* via instantiated_node_keys_ (an unordered_set<const void*> keyed by ASTNode::raw_pointer()), so duplicate
late-materialized roots are rejected even before pending-semantic-root enqueue. However, ast_nodes_ is still a single vector; codegen and drainLazyMemberRegistry both iterate
get_nodes() uniformly without a real split into user_source_nodes_ vs. instantiated_struct_nodes_. The tracking infrastructure is there; the consumption/data-model split is not.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 5. Push ODR-use marking fully into sema; reduce codegen to a pure consumer   [DONE]

Slices A-E moved the materialization earlier (from codegen to sema annotation). The next step is to move the ODR-use signaling earlier too. Concretely:

 - tryAnnotateConversion, tryAnnotateCallArgConversionsImpl, tryAnnotateMemberAccessImpl, and the constexpr evaluator should all call markOdrUsed(...) at the point they
   identify a target.
 - The struct-visitor in IrGenerator_Visitors_Decl.cpp stops being a materializer; it only iterates what sema has already prepared.
 - generateDeferredMemberFunctions stops having any materialization logic.
 - AstToIr::materializeLazyMemberIfNeeded disappears.

With ODR-use explicit (change #1), sema drain is the single materialization site. Codegen becomes 100% a consumer.

Current state (DONE): The struct-visitor in IrGenerator_Visitors_Decl.cpp no longer contains any materializeLazyMemberIfNeeded fallback — its comments explicitly state "No
defensive materialization fallback needed here" (lines ~1253-1264). generateDeferredMemberFunctions in IrGenerator_Visitors_TypeInit.cpp still exists and is called from
FlashCppMain.cpp, but its comments confirm the old materialization fallback is dead ("the function-shaped materializeLazyMemberIfNeeded fallback that used to live here is now
dead"). AstToIr::materializeLazyMemberIfNeeded no longer exists as a declared or defined function. Multiple SemanticAnalysis.cpp sema annotation sites call markOdrUsed, and
the stale bridge-era comments in SemanticAnalysis.cpp / SemanticAnalysis.h have been cleaned up. Current baseline remains green at 2204 pass / 149 expected-fail.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Medium-impact changes

### 6. Reusable invariant-probe infrastructure   [DONE]

The audit guard in materializeLazyMemberIfNeeded (writing to audit_phase5f.log, later turned into a hard-fail guard) was the single most effective tool in this whole slice. It
told us empirically which tests still violated the invariant we were trying to establish.

That pattern should be a first-class macro:

 FLASH_INVARIANT_PROBE(Phase5F, "first-materialization at codegen",
     StructName=struct_name, MemberName=member_name);

With a few modes:

 - off: compiled out in release.
 - log: appends to a per-probe log file.
 - fail: throws InternalError on violation (for CI-enforced tightening).

Every slice that tightens an invariant would benefit. Right now each slice reinvents the instrumentation, and we remove it before commit — meaning we have zero regression
coverage that the invariant holds going forward.

Current state (DONE): `src/Log.h` now provides `FLASH_INVARIANT_PROBE(name, ...)` and `FLASH_INVARIANT_PROBE_FORMAT(name, fmt, ...)` backed by a compile-time
`FLASHCPP_INVARIANT_PROBE_MODE` switch:

 - `0` = off (compiled out)
 - `1` = log (append to `audit_<probe>.log` and emit a warning)
 - `2` = fail (throw `InternalError`)

The first concrete use is `Phase5StructDrain` in `IrGenerator_Visitors_Decl.cpp`, which trips if codegen ever reaches a still-lazy member function or constructor that should
have been drained by sema already. Current baseline remains green at 2204 pass / 149 expected-fail.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 7. Stable ASTNodeId instead of raw_pointer() identity   [TODO]

I needed node identity this session (dedup in registerLateMaterializedTopLevelNode). raw_pointer() works but it's an implementation detail of ChunkedAnyVector and it's not a
typed concept. A real ASTNodeId (monotonically assigned at node creation, stored inside the node) would:

 - Let us use hash-based dedup structures cleanly.
 - Survive hypothetical future refactors of the underlying storage.
 - Allow persistent caches (e.g., "already-materialized" sets) that are not tied to raw addresses.

Current state (TODO): Dedup in Parser.h still uses const void* raw pointers (both pending_semantic_root_keys_ and instantiated_node_keys_). No typed ASTNodeId concept exists.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 8. Make registerLateMaterializedTopLevelNode dedup-by-default   [DONE]

Even without #7, the helper should always dedup. Multiple instantiation paths (partial-spec success, primary-template success, base-class helper recursion) can all reach the
same freshly-created struct. Silently double-pushing causes LNK2005s that are painful to diagnose. Dedup should be the contract, not the caller's responsibility.

Current state (DONE): registerLateMaterializedTopLevelNode and registerLateMaterializedTopLevelNodeFront now both dedup directly through Parser.h's
instantiated_node_keys_ set before mutating ast_nodes_. Duplicate calls still forward to enqueuePendingSemanticRoot(node) (itself dedup'd), so the same node is neither
double-pushed into the top-level list nor double-enqueued for sema normalization. The contract is the helper's, not the caller's.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 9. Centralize the "what is a top-level struct for codegen purposes?" walker   [DONE]

Today both drainLazyMemberRegistry and the codegen struct-visitor implement their own "recurse namespaces + nested classes + structs" traversal. They have to match exactly or
else sema drains something codegen won't emit (under-draining: harmless) or codegen emits something sema didn't drain (over-emitting: LNK errors).

A shared forEachReachableStructDecl(parser, callback) utility would eliminate the drift risk. This is a very small refactor that prevents a whole class of "sema and codegen
disagree about what's reachable" bugs.

Current state (DONE): `src/ReachableStructWalker.h` now owns the shared recursion over namespaces + structs + nested classes. `SemanticAnalysis::drainLazyMemberRegistry`
uses `FlashCpp::forEachReachableStructDecl(top_node, drainOneStruct)`, and codegen's top-level `AstToIr::visit` uses `FlashCpp::walkReachableStructDecls(...)` with namespace
enter/exit hooks plus struct enter/exit hooks (`beginStructDeclarationCodegen` / `endStructDeclarationCodegen`). This preserves codegen's context-sensitive behavior while removing the
separate hand-rolled walkers. The refactor was validated against the full Windows baseline: 2204 pass / 149 expected-fail.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Lower-impact, still useful

### 10. Promote needsInstantiation-style queries into typed predicates   [TODO]

Free-standing needsInstantiation, needsInstantiationAny, getLazyMemberInfo, getLazyMemberInfoAny are strictly-similar string-handle-keyed queries. They'd read better as methods
on a typed LazyMemberKey value that captures struct+member+const-ness. The const-ness std::optional<bool> juggling at every call site is a minor papercut that adds up.

Current state (TODO): All four free-standing overloads plus the new markOdrUsed / isOdrUsed family still use the raw StringHandle pair + bool pattern. The makeKey static
helper in LazyMemberInstantiationRegistry is an internal step toward a typed key, but it's private and not exposed to callers.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 11. Explicit lifecycle hooks for instantiations   [TODO]

normalizePendingSemanticRoots has become the "hey sema, look at what just appeared" generic hook. It's called from parser, sema, codegen, and constexpr evaluator at slightly
different moments. A more structured lifecycle:

 instantiationDiscovered(struct) -> shapeMaterialized(struct) -> memberOdrUsed(struct, member) -> memberMaterialized(struct, member)

with explicit listeners would make it obvious which phase each consumer cares about. Today each consumer has to infer it from empirical debugging.

Current state (TODO): normalizePendingSemanticRootsIfAvailable / normalizePendingSemanticRoots remain the single generic hook. No structured lifecycle or listener pattern
exists.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Summary table

| # | Item                                       | Status   |
|---|--------------------------------------------|----------|
| 1 | Explicit ODR-use bit on lazy registry      | DONE     |
| 2 | Shape / body phase split + variant state   | TODO     |
| 3 | InstantiationContext threading             | TODO     |
| 4 | Separate instantiated-struct list          | PARTIAL  |
| 5 | Sema owns ODR-use; codegen pure consumer   | DONE     |
| 6 | FLASH_INVARIANT_PROBE infrastructure       | DONE     |
| 7 | Stable ASTNodeId                           | TODO     |
| 8 | registerLateMaterializedTopLevelNode dedup | DONE     |
| 9 | Shared forEachReachableStructDecl walker   | DONE     |
|10 | Typed LazyMemberKey predicates             | TODO     |
|11 | Explicit instantiation lifecycle hooks     | TODO     |

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## What this buys us, in order

Items #1, #5, #8, and #9 are done. Together they deliver the core Slice G promise: sema marks every needed member, drains it, and codegen is a pure consumer. The
materializeLazyMemberIfNeeded forwarder no longer exists as live code.

Item #4 is partially done — the parallel tracking vector is there, the clean consumption split is not. Completing it (making codegen apply different emission rules for user
nodes vs. instantiated nodes) would make "what should be emitted for this instantiation?" an answerable data-structure question rather than an inference from AST reachability.

Items #2 and #3 remain the principled long-term fix for the SFINAE corner cases still listed in KNOWN_ISSUES. The workarounds in place (= delete tie-breaking heuristic,
hasLaterUsableTemplateDefinitionWithMatchingShape check) paper over real gaps in the data model. Phase 6 work on deduction-rule cleanup will benefit from both of these.

Item #6 (invariant probes) is now in place: future architectural tightening can keep its guardrails checked in instead of re-inventing one-off audit files for each slice.

Items #10 and #11 are refactoring quality-of-life items that reduce future maintenance risk with low implementation cost.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Recommended next steps

1. Complete item #4 (separate instantiated-struct list, consumption side): make codegen's top-level node iterator apply different emission rules for instantiated nodes
   (emit only odr_used members) vs. user-written nodes (emit every member). This is the last structural piece the original Slice G recommendation called for.

2. Item #10 (typed LazyMemberKey predicates) is now the best small cleanup. The registry API still exposes a pile of near-duplicate `StringHandle + bool` queries, and item #6
   means future refactors can guard the migration with checked-in probes instead of temporary audits.

3. Items #2 and #3 are Phase 6 prerequisites for principled SFINAE handling. They are medium-sized architectural changes. The existing KNOWN_ISSUES workarounds are stable
   enough to defer until Phase 6 begins.

4. Item #7 (stable ASTNodeId) is still optional for correctness, but it becomes much more attractive if item #4 proceeds. Once there are multiple long-lived dedup/tracking sets,
   raw_pointer()-identity becomes more of a liability.
