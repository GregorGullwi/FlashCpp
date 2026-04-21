1. Add an explicit odr_used bit on lazy member registry entries

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

2. Split class-template instantiation into shape and body phases with distinct failure modes

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

3. Introduce an InstantiationContext value that propagates through instantiation machinery

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

4. Give late-materialized instantiations their own first-class list, separate from ast_nodes_

The ast_nodes_ vector is currently doing double duty:

 - Source-order list of user-written top-level declarations (its original job).
 - Splice target for late-materialized class-template instantiations (via registerLateMaterializedTopLevelNode).

The latter caused real bugs this session — duplicate push → LNK2005 — and makes the "what should codegen emit fully vs. on-demand" question unanswerable from the data structure
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

5. Push ODR-use marking fully into sema; reduce codegen to a pure consumer

Slices A-E moved the materialization earlier (from codegen to sema annotation). The next step is to move the ODR-use signaling earlier too. Concretely:

 - tryAnnotateConversion, tryAnnotateCallArgConversionsImpl, tryAnnotateMemberAccessImpl, and the constexpr evaluator should all call markOdrUsed(...) at the point they 
identify a target.
 - The struct-visitor in IrGenerator_Visitors_Decl.cpp stops being a materializer; it only iterates what sema has already prepared.
 - generateDeferredMemberFunctions stops having any materialization logic.
 - AstToIr::materializeLazyMemberIfNeeded disappears.

With ODR-use explicit (change #1), sema drain is the single materialization site. Codegen becomes 100% a consumer.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Medium-impact changes

6. Reusable invariant-probe infrastructure

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

7. Stable ASTNodeId instead of raw_pointer() identity

I needed node identity this session (dedup in registerLateMaterializedTopLevelNode). raw_pointer() works but it's an implementation detail of ChunkedAnyVector and it's not a
typed concept. A real ASTNodeId (monotonically assigned at node creation, stored inside the node) would:

 - Let us use hash-based dedup structures cleanly.
 - Survive hypothetical future refactors of the underlying storage.
 - Allow persistent caches (e.g., "already-materialized" sets) that are not tied to raw addresses.

8. Make registerLateMaterializedTopLevelNode dedup-by-default

Even without #7, the helper should always dedup. Multiple instantiation paths (partial-spec success, primary-template success, base-class helper recursion) can all reach the
same freshly-created struct. Silently double-pushing causes LNK2005s that are painful to diagnose. Dedup should be the contract, not the caller's responsibility.

9. Centralize the "what is a top-level struct for codegen purposes?" walker

Today both drainLazyMemberRegistry and the codegen struct-visitor implement their own "recurse namespaces + nested classes + structs" traversal. They have to match exactly or
else sema drains something codegen won't emit (under-draining: harmless) or codegen emits something sema didn't drain (over-emitting: LNK errors).

A shared forEachReachableStructDecl(parser, callback) utility would eliminate the drift risk. This is a very small refactor that prevents a whole class of "sema and codegen
disagree about what's reachable" bugs.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

Lower-impact, still useful

10. Promote needsInstantiation-style queries into typed predicates

Free-standing needsInstantiation, needsInstantiationAny, getLazyMemberInfo, getLazyMemberInfoAny are strictly-similar string-handle-keyed queries. They'd read better as methods
on a typed LazyMemberKey value that captures struct+member+const-ness. The const-ness std::optional<bool> juggling at every call site is a minor papercut that adds up.

11. Explicit lifecycle hooks for instantiations

normalizePendingSemanticRoots has become the "hey sema, look at what just appeared" generic hook. It's called from parser, sema, codegen, and constexpr evaluator at slightly
different moments. A more structured lifecycle —

 instantiationDiscovered(struct) → shapeMaterialized(struct) → memberOdrUsed(struct, member) → memberMaterialized(struct, member)

— with explicit listeners would make it obvious which phase each consumer cares about. Today each consumer has to infer it from empirical debugging.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

What this buys us, in order

If we do #1 (explicit ODR-use) + #4 (separate instantiated-struct list) + #5 (sema owns ODR-use marking), the Slice F forwarder can be deleted outright with no regression risk:
sema marks exactly what codegen will need, drains it, and codegen becomes a straight consumer.

If we also do #2 (shape/body split) + #3 (InstantiationContext), we finally have a principled answer to every future SFINAE corner case, and the constexpr / dependent /
nested-class bug cluster that keeps surfacing in Phase 5 work becomes mostly a mechanical consequence of the new data model.

#6 (invariant probes) is the force-multiplier: it turns every future architectural tightening from "write audit, run tests, remove audit, repeat" into "flip the probe mode,
trust the CI".

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

My honest recommendation

For next concrete work, I would not jump straight into Phase 6's broader explicit-deduction architecture cleanup. I'd do #1 + #4 first, as a "Phase 5 Slice G" sort of
deliverable. They are:

 - Small enough to land cleanly (new bit on lazy entries, new parser side-list, shuffle a few call sites).
 - Sufficient on their own to delete the materializeLazyMemberIfNeeded forwarder and retire its audit-guard pattern.
 - A prerequisite for any future template-related work that wants to reason precisely about "what should be emitted for this instantiation?" — including Phase 6, which is going
 to benefit from the same ODR-use-vs-probe distinction.

Then Phase 6 on top of that foundation becomes less about architecture and more about the deduction-rule cleanup that was the stated goal all along.