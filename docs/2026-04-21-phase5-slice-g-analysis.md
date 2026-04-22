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
 - Item #4 below is now finished on this branch (2026-04-21 follow-up): `drainLazyMemberRegistry` pass 1 now skips `isInstantiatedNode(i)` roots;
   instantiated-struct members materialise only via the ODR-use snapshot pass 2. Closing the six coverage gaps (binary operator overloads, CV-aware conversion operators, struct→enum UDC,
   `markOdrUsedAllInClass`, receiver-aware `tryMaterializeLazyCallTarget`, range-for begin/end, `member_context_stack_` fallback for `this->member()`) was what unlocked it.

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

### 2. Split class-template instantiation into shape and body phases with distinct failure modes   [PARTIAL]

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

Current state (PARTIAL, 2026-04-22): the data-model prerequisite is in place.
`FunctionDeclarationNode` now carries a three-state `BodyStateTag`
(`NotMaterialized` / `Materialized` / `FailedSubstitution`) encoded as a tag +
optional-body + optional-reason (equivalent to the conceptual variant, but
without forcing every AST walker through `std::visit`; `get_definition()` keeps
its existing ABI so Category A / C consumers are unchanged). Five queries and
one failure mutator are exposed on the node: `is_materialized()`,
`failed_substitution()`, `needs_body_materialization()` (used by instantiation
drivers — returns false when the node already failed substitution, so a cached
failure is never re-probed), `has_any_body_source()`, `substitution_failure_reason()`,
and `mark_failed_substitution(reason)`. Category B instantiation-driver sites
on `FunctionDeclarationNode` now route through the new helpers.

Follow-up slice (2026-04-22): the `any_deleted_at_best` tie-break heuristic in
`try_instantiate_template` has now been narrowed to its legitimate
"all-best-specificity-candidates-are-`= delete`" meaning. The unlock was
preserving `cv_qualifier` through template type bindings:
`registerTemplateTypeBinding` (both the user-defined and primitive paths) now
attaches a `TypeSpecifierNode` carrying the qualifier, and
`parse_type_specifier` composes the alias-chain `cv_qualifier` with any
explicit cv from the token stream. With that in place, `is_const<First>` where
`First=const int` correctly matches the `is_const<const T>` specialization, so
`enable_if<!is_const<F>::value && !is_const<S>::value, void>::type` collapses to
substitution failure through the existing reparse-failure path rather than
silently succeeding. The `test_namespaced_pair_swap_sfinae_ret0.cpp` regression
that previously blocked this narrowing no longer occurs, and the related
KNOWN_ISSUES entry has been closed. Per-overload failure memoization via
`mark_failed_substitution` at every reparse site is still a desirable follow-up
(it would avoid redoing the reparse on repeat probes) but is no longer on the
critical path for correctness.

Item 2d follow-up (2026-04-22): the remaining gaps in per-overload
failure memoization inside `try_instantiate_single_template` have been
closed at the registry level. Two SFINAE-failure sites past the
key-computation point were previously returning `std::nullopt` without
calling `gTemplateRegistry.markFailedInstantiation(key, overload_id)`:
the `requires`-clause failure path and the per-parameter concept-
constraint failure path. Both failures are deterministic in
`(template_name, template_args, overload_id)` (pure predicates over
the deduced arguments), so memoizing them is safe and matches the
existing return-type / dependent-alias reparse-failure memos. Every
post-key failure return in `try_instantiate_single_template` now
registers the failure with the registry; repeat SFINAE probes for the
same overload short-circuit at the `isFailedInstantiation` fast-path
instead of re-evaluating the constraint. The cycle-detection bail-out
in the trailing-return reparse is intentionally left unmemoized — it
is transient, not a real substitution failure. Full suite still passes
at 2174 pass / 149 expected-fail.

Item 2c cleanup (2026-04-22, immediately after the narrowing): empirical
verification with `Templates:debug` logs confirmed that the reparse-failure
path in `try_instantiate_single_template` now rejects the non-deleted
`swap(pair<First, Second>&, pair<First, Second>&)` overload on its own
(`SFINAE: Return type parsing failed: SFINAE substitution failure:
enable_if$…::type`), leaving only the `= delete` sentinel in the
`sfinae_candidates` vector. Running the full suite with the explicit
"all-deleted ⇒ SFINAE failure" block neutralised — while still returning
`nullopt` when no non-deleted candidate exists at best specificity — passed
all 2174 tests. The tie-break block was therefore simplified: the
`all_best_deleted` / `saw_best` tracking pair was removed, and the single
remaining check is `if (!best_non_deleted) return nullopt;`. Semantically
this is C++17 [temp.deduct.call] behaviour: selecting a deleted function in
the immediate context of a decltype/SFINAE probe is itself a substitution
failure, so what used to read like a tie-break heuristic is now just
standard overload resolution expressed in one line.

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

#### 2026-04-22 audit: KNOWN_ISSUES#2 is blocked on this item

An attempt at landing the KNOWN_ISSUES#2 fix (specificity-sorted iteration in the
non-SFINAE path of `try_instantiate_template`) was prototyped end-to-end. It
immediately fixes the minimal `f(T) vs f(T*)` partial-ordering repro recorded in
KNOWN_ISSUES.md, but regresses exactly one full-suite test —
`test_namespaced_pair_swap_sfinae_ret0.cpp`. The regression is not a flaw in the
specificity-sort itself; it is a direct manifestation of this item's underlying
problem:

 - The test's `decltype(swap(declval<pair&>(), declval<pair&>()))` default-template-
   argument expression is, conceptually, a SFINAE probe. Under proper partial
   ordering the `swap(pair<F,S>&, pair<F,S>&) = delete` overload wins and the
   `= delete` sentinel becomes a substitution failure per [temp.deduct.call].
 - FlashCpp currently evaluates a sub-step of that expression in *non-SFINAE
   context* before the SFINAE wrap kicks in, because `in_sfinae_context_` is a
   single parser-global bit that cannot represent "the caller is in SFINAE, but
   this sub-evaluation currently isn't".
 - With first-match overload selection, that leaked non-SFINAE call harmlessly
   picks the bodyless `swap(Type&, Type&)` forward declaration with
   `Type = pair<const int, int>`, so no pair::swap body is ever materialised and
   the subsequent SFINAE probe still correctly concludes
   `is_swappable = false_type`.
 - With specificity-sorted selection, the same leaked non-SFINAE call correctly
   picks the pair-specialised `swap(pair<F,S>&, pair<F,S>&)` overload, whose body
   (`left.swap(right)`) forces `pair<const int, int>::swap` to materialise.
   That body contains the ill-formed inner `swap(const int&, const int&)` call,
   and codegen trips on the dependent type when lowering it to IR.

The fundamental fix for both items is the same one item #3 prescribes: an
`InstantiationContext` value threaded through every substitution / instantiation
call, so that the `decltype`-default-argument sub-evaluation carries
`InstantiationMode::SfinaeProbe` rather than inheriting the caller's raw
`in_sfinae_context_` bit. With that in place, the pair-specialised overload
still wins per partial ordering, but it wins *as a SFINAE probe*, so its body is
never eagerly materialised and the existing SFINAE failure path handles the
`= delete` sentinel correctly.

**Dependency:** KNOWN_ISSUES.md entry "Non-SFINAE function-template overload
selection uses 'first match' instead of most-specific" is now explicitly
blocked on this item. See the KNOWN_ISSUES entry for the minimal repro and the
detailed trace of the non-SFINAE leak.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

### 4. Give late-materialized instantiations their own first-class list, separate from ast_nodes_   [DONE]

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

#### 2026-04-21 audit attempt (this session)

A prototype that simply skipped `isInstantiatedNode(i)` roots in
`drainLazyMemberRegistry` pass 1 was built and tested end-to-end against the
full suite. It surfaced five previously-hidden ODR-use coverage gaps:

 1. Binary operator member-overloads (`operator==`, `operator+`, ...) not
    marked ODR-used from the `normalizeExpression` BinaryOperatorNode branch.
    → Fixed in this session via a new `markResolvedOperatorOverloadOdrUsed`
    helper that pulls `{parent_struct_name, member_name, is_const}` out of
    `resolved_member_operator_overload()` and calls `markOdrUsed`.
 2. Conversion-operator overload resolution skipped CV-aware pairing with
    codegen's `findConversionOperator`. `structHasConversionOperatorTo` marked
    only the first match; codegen later picked a different CV qualifier and
    hit a lazy stub.
    → Fixed by iterating all `mf.conversion_target_type`-matching overloads
    and marking each (both const and non-const) ODR-used.
 3. `tryAnnotateConversion` bailed out early on `to_desc.category() == Enum`
    before reaching the UserDefined-rank path, so struct→enum UDCs bypassed
    `structHasConversionOperatorTo` entirely and relied on pass 1 draining
    the instantiated root wholesale.
    → Fixed by narrowing the early-return to `from != Struct`.
 4. `computeInstantiatedLookupName` canonicalises enum template arguments as
    `TypeCategory::UserDefined` rather than `Enum`, so conversion-operator
    stubs with enum template args register under an un-canonicalised name
    (e.g. `operator value_type`). Sema can't round-trip the lookup.
    → Worked around via a conservative
    `LazyMemberInstantiationRegistry::markOdrUsedAllInClass(StringHandle)`
    helper, invoked as a fallback after `structHasConversionOperatorTo`
    finishes; it only fires for instantiated classes sema is already
    annotating, so SFINAE-probed instantiations remain untouched.
 5. **Blocker** — using-decl pack expansion that imports static members from
    a concrete base instantiation. For
    `struct Combined : Bases... { using Bases::call...; }` with
    `Combined<Fun<1>>`, sema resolves `c.call()` to the *pattern's*
    `Fun::call`. The resolved FunctionDeclarationNode reports
    `parent_struct_name().empty() == true` and `is_member_function() == false`
    (verified via ad-hoc logging at `tryMaterializeLazyCallTarget`). No
    existing ODR-use site knows which instantiated owner
    (`Fun<1>` / `Fun$hash`) to mark, so skipping instantiated roots leaves
    `Fun$hash::call` unmaterialized and a link error reaches the test.
 6. **Blocker** — late-materialised out-of-line member bodies (the
    `test_late_member_body_class_template_*_ret42.cpp` and
    `test_late_member_signature_qualified_dependent_type_ret42.cpp` cluster).
    Once cross-instantiation substitution has run for
    `Holder$hash::run`, the body references a sibling
    `Box$hash::get` that still carries no ODR-use mark. The reference site
    that chooses `Box$hash::get` is currently inside the instantiation-time
    substitution walk (not a sema annotation pass), so the ODR-use signal
    never reaches the drain.

With fixes (1)–(4) in place and the pass-1 skip prototype active, gaps (5)
and (6) each block exactly one (or a small cluster of) tests. Because
item #4's whole purpose is to flip consumption rules for every
instantiated root, partial coverage is unsafe: either every instantiated
root honours the ODR-use protocol or none of them can be skipped.

Decision: revert the pass-1 skip to preserve the 2204 / 149 baseline and
land fixes (1)–(4) + `markOdrUsedAllInClass` as independent improvements.
They are all correct-on-their-own-merit ODR-use closures and will be
required again when the final split lands. The pass-1 loop now carries a
comment pointing at blockers (5) and (6) so the next attempt has a
concrete starting list.

#### 2026-04-22 follow-up: receiver-aware ODR-use

Added `tryMaterializeLazyCallTarget(func_decl, call_info)` in
`SemanticAnalysis.cpp` (overload of the existing pattern-parent helper).
When the resolver returns a member-function decl with either (a) an empty
`parent_struct_name` (using-decl pack / free-function-view resolution) or
(b) a parent that names a template pattern, the new overload derives the
concrete instantiated owner from the call's receiver type, walks the
receiver's inheritance chain, and marks the matching lazy member
ODR-used. It also defends against the resolver having lost const-
qualification by trying both `is_const` variants. Wired through
`tryAnnotateCallArgConversionsImpl`. Declaration in `SemanticAnalysis.h`.

Gained coverage (re-enabling the pass-1 skip prototype with this helper):

 - `test_late_member_body_class_template_paths_ret42.cpp`
 - `test_late_member_body_class_template_functional_style_ret42.cpp`
 - `test_late_member_signature_qualified_dependent_type_ret42.cpp`
 - `test_using_decl_pack_expansion_ret1.cpp`

All four tests now link cleanly under the pass-1 skip prototype.

Still missing with the skip enabled (observed link errors, net −3 vs.
baseline):

 - `test_constexpr_range_begin_end_ret0.cpp` — range-based-for over an
   instantiated struct. `normalizeRangedForLoopDecl` in
   `SemanticAnalysis.cpp` resolves `begin`/`end` member functions but
   never calls `markOdrUsed` on the concrete instantiation. Fix: after
   `set_resolved_member_begin_function` / `_end_function`, mark both
   members ODR-used on `range_type_info->name()` when it is a template
   instantiation.
 - `test_deferred_base_placeholder_codegen_ret0.cpp` — `optional<T>::has_value`
   body calls `_M_payload._M_is_engaged()`. The late-materialised
   member body's `this->member` / `sub_obj.member` calls do not pass
   through `tryAnnotateCallArgConversionsImpl` for the inner receiver,
   so the receiver-aware helper is never invoked.
 - `test_dependent_base_this_lookup_ret0.cpp` — `Derived<T>::f` calls
   `this->get()` on `Base<T>`. Same gap as above — the call annotation
   pass simply does not run on the late-materialised body under the
   skip prototype. Need to investigate where / whether sema is re-
   running on the cloned body.

Decision (unchanged): keep the pass-1 skip disabled; keep the receiver-
aware helper, it is a correct and independent ODR-use closure. Commit
the improvement and leave item #4 at PARTIAL. The next session should
target the three failures above — each has a concrete fix pointer in
the list above.

#### 2026-04-21 follow-up: item #4 LANDED

All three blockers above closed; pass-1 skip of `isInstantiatedNode(i)`
in `drainLazyMemberRegistry` is now ENABLED and the full suite stays
green at 2204 pass / 149 expected-fail.

Fixes landed:

 1. **Range-for begin/end ODR-use** (`SemanticAnalysis.cpp`
    `normalizeRangedForLoopDecl`, after `set_resolved_member_begin_function`).
    When `range_type_info->isTemplateInstantiation()`, mark both
    `begin` and `end` lazy members ODR-used on the concrete owner,
    trying the selected const variant first and falling back to the
    opposite to defend against resolver-const-qualification drift.
    Mirrors the receiver-aware helper's const-variant strategy.
 2. **`member_context_stack_` fallback in receiver-aware helper**
    (`SemanticAnalysis.cpp` `tryMaterializeLazyCallTarget(func_decl,
    call_info)`). When `inferExpressionType(call_info.receiver)`
    returns an invalid id — which is what happens for `this->member()`
    inside a late-materialised dependent body where the `this`
    expression is not yet type-resolved — fall back to
    `tryGetTypeInfo(member_context_stack_.back())`. That stack already
    carries the innermost struct whose body we are annotating, which
    is the correct receiver type for implicit- and explicit-`this`
    calls. Covers both `test_dependent_base_this_lookup_ret0.cpp`
    (Derived<T>::f calling Base<T>::get) and
    `test_deferred_base_placeholder_codegen_ret0.cpp`
    (optional<T>::has_value calling optional_payload<T>::_M_is_engaged
    via a sub-object receiver that also resolves through the fallback).

The drain-pass-1 `if (parser_.isInstantiatedNode(i)) continue;` is now
permanent. Codegen and drainLazyMemberRegistry now have different
consumption rules for user-written roots vs. instantiated roots:

 - User-written roots (`!isInstantiatedNode(i)`): drained wholesale in
   pass 1 — every member is materialised because the user wrote them.
 - Instantiated roots: drained only through pass 2
   (`snapshotOdrUsedLazyEntries`) — only members that sema-side sites
   have explicitly marked ODR-used materialise. SFINAE probes never
   reach the markers, so their candidate instantiations stay dormant.

Lessons (updated):

 - The six ODR-use gaps that the prototype surfaced were all real
   bugs, not artefacts of the skip. Closing them individually was the
   correct path.
 - `member_context_stack_` is an under-used oracle. It already holds
   the currently-normalised struct context and is trivial to consult
   as a fallback when expression-typing fails. Any future ODR-use
   site that needs "which struct am I inside right now?" should use
   it directly.
 - `isTemplateInstantiation()` is the right guard for distinguishing
   an instantiated owner from a user-written struct. Gating
   `markOdrUsed` on that bit keeps SFINAE-safe.

Lessons:

 - Wholesale-draining instantiated roots was masking at least six distinct
   ODR-use gaps. An audit-first approach (prototype the skip, observe what
   breaks, close the gap) is the correct methodology; blanket skipping is
   not.
 - Sema annotation sites must be the single source of truth for ODR-use.
   Anything that resolves a call, conversion, or member access against a
   concrete instantiation must translate the resolved target into a
   `(struct, member, is_const)` triple and call `markOdrUsed`. Helpers
   that can't recover the struct/member (see blockers 5 and 6) indicate a
   sema-side resolution bug, not a drain-side bug.
 - `computeInstantiatedLookupName` has a latent enum-template-argument
   canonicalisation bug. The `markOdrUsedAllInClass` helper is a pragmatic
   gate, but the lookup name should be fixed at the registration site in
   `Parser_Templates_Inst_ClassTemplate.cpp:201`.
 - CV-aware conversion-operator selection is duplicated between sema's
   `structHasConversionOperatorTo` and codegen's `findConversionOperator`.
   The two must stay aligned (or share a helper) for ODR-use marking to
   match codegen's actual selection.

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

### 10. Promote needsInstantiation-style queries into typed predicates   [DONE]

Free-standing needsInstantiation, needsInstantiationAny, getLazyMemberInfo, getLazyMemberInfoAny are strictly-similar string-handle-keyed queries. They'd read better as methods
on a typed LazyMemberKey value that captures struct+member+const-ness. The const-ness std::optional<bool> juggling at every call site is a minor papercut that adds up.

Current state (DONE): `TemplateRegistry_Lazy.h` now exposes a typed `LazyMemberKey` value with exact/any-const helpers, and the registry implements typed overloads for
`needsInstantiation`, `getLazyMemberInfo`, `markInstantiated`, `markOdrUsed`, and `isOdrUsed`. The old raw `StringHandle + bool` call shape still exists only as thin wrappers
around the typed API so call sites can migrate incrementally.

Important lesson from the 2026-04-21 implementation: the mechanical API refactor was easy; the hidden risk was `StringBuilder` lifetime discipline inside the "any-const"
lookup path. A representative regression (`flashcpp_crash_20260421_174831.log`) showed `StringBuilder::~StringBuilder` asserting from
`LazyMemberInstantiationRegistry::needsInstantiation` during `Parser::parse_member_postfix`, i.e. a typed-key wrapper reused the old `preview()`-based two-variant lookup
without preserving the required `commit()/reset()` discipline. The shipped fix was to eliminate that pattern in the typed any-const helpers and build committed non-const /
const keys directly.

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
| 2 | Shape / body phase split + variant state   | PARTIAL  |
| 3 | InstantiationContext threading             | TODO     |
| 4 | Separate instantiated-struct list          | DONE     |
| 5 | Sema owns ODR-use; codegen pure consumer   | DONE     |
| 6 | FLASH_INVARIANT_PROBE infrastructure       | DONE     |
| 7 | Stable ASTNodeId                           | TODO     |
| 8 | registerLateMaterializedTopLevelNode dedup | DONE     |
| 9 | Shared forEachReachableStructDecl walker   | DONE     |
|10 | Typed LazyMemberKey predicates             | DONE     |
|11 | Explicit instantiation lifecycle hooks     | TODO     |

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## What this buys us, in order

Items #1, #5, #8, and #9 are done. Together they deliver the core Slice G promise: sema marks every needed member, drains it, and codegen is a pure consumer. The
materializeLazyMemberIfNeeded forwarder no longer exists as live code.

Item #4 is now done (2026-04-21 follow-up): the drain-pass-1 loop skips
instantiated roots, and pass-2 ODR-use snapshot drives their member
materialisation. Codegen can now treat user-written and instantiated
roots under fundamentally different rules — user-written roots emit
everything they declared, instantiated roots emit only what sema
marked ODR-used. This closes the "what should be emitted for this
instantiation?" question at the data-structure level.

Items #2 and #3 remain the principled long-term fix for the SFINAE corner cases still listed in KNOWN_ISSUES. Item #2's data-model prerequisite (three-state
`BodyStateTag` on `FunctionDeclarationNode` plus `needs_body_materialization` / `mark_failed_substitution` API) is in place as of 2026-04-21; the remaining work is wiring every
reparse-failure site in `try_instantiate_single_template` to the new mutator so the `= delete` tie-break heuristic can be narrowed. The workarounds in place (= delete tie-breaking heuristic,
hasLaterUsableTemplateDefinitionWithMatchingShape check) paper over real gaps in the data model. Phase 6 work on deduction-rule cleanup will benefit from both of these.

Item #6 (invariant probes) is now in place: future architectural tightening can keep its guardrails checked in instead of re-inventing one-off audit files for each slice.

Item #11 remains a refactoring quality-of-life item that could reduce future maintenance risk, but item #10 is now done and already removed a fair amount of duplicated
const-ness plumbing from the lazy-member registry API.

-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

## Recommended next steps

1. Items #2 and #3 are now the highest-value remaining architectural follow-ups. They are the Phase 6 prerequisites for principled SFINAE handling, and the existing
   KNOWN_ISSUES workarounds are stable enough that this can be tackled deliberately instead of as an emergency cleanup. The 2026-04-22 audit under item #3
   confirms empirically that KNOWN_ISSUES#2 (non-SFINAE partial ordering) is blocked on item #3 (`InstantiationContext` threading): the specificity-sorted
   non-SFINAE selection itself is a one-function change, but it cannot land alone without regressing the pair/swap SFINAE cluster.

2. Item #4 is still the main unfinished Slice G structural split, but it should only be retried behind a focused ODR-use coverage audit. The earlier regressions showed that the
   data model is close, not that blind top-level filtering is safe.

3. Item #7 (stable ASTNodeId) is still optional for correctness, but it becomes more attractive if the project grows more long-lived dedup/tracking sets beyond the current
   raw_pointer()-keyed helpers.

4. Item #11 (explicit instantiation lifecycle hooks) remains a smaller cleanup that can wait until there is a clearer consumer for those phases.
