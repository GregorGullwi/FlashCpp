# Template Instantiation / Constexpr Architecture Audit

**Date:** 2026-04-06

## Executive summary

I **partly agree** with the premise.

FlashCpp already has a post-instantiation semantic-analysis step for the main eager pipeline:

1. parse source
2. instantiate many templates during parsing
3. run `SemanticAnalysis::run()`
4. lower to IR

That means the compiler does **not** need a brand-new second global sema pass just to cover the normal eager template-instantiation path.

However, the architecture would still benefit a lot from making semantic analysis the owner of **all materialized instantiated AST**, because today there are still important instantiation paths that happen **after** the main sema pass or inside parser/constexpr-owned machinery:

- lazy template class/member instantiation
- deferred template member-body parsing
- some on-demand template instantiation triggered from constexpr evaluation
- constexpr member/function fallback logic that re-derives facts from parser/type state instead of consuming sema-owned annotations

So the right direction is:

- **keep** the existing post-parse sema pass for eagerly materialized instantiated AST
- **extend sema ownership** to late-instantiated AST and constexpr-relevant instantiated bodies
- **reduce parser/constexpr fallback semantics** over time

## Current architecture audit

### 1. The eager top-level pipeline already runs sema after template instantiation

`src/FlashCppMain.cpp` clearly establishes the main pipeline:

- parsing comments explicitly say template instantiation happens during parsing
- `SemanticAnalysis sema(*parser, ...)` runs immediately after parsing
- IR/codegen happens only after `sema.run()`

This is already the architecture you are asking for at a high level.

### 2. Template instantiation is still parser-owned, not sema-owned

The parser is still responsible for materializing instantiated declarations and bodies:

- `Parser::try_instantiate_template(...)`
- `Parser::try_instantiate_template_explicit(...)`
- `Parser::try_instantiate_class_template(...)`
- `Parser::instantiateLazyMemberFunction(...)`
- `Parser::instantiateLazyClassToPhase(...)`
- `Parser::reparse_template_function_body(...)`

The key pattern is:

- parser restores token positions
- parser registers temporary template bindings in parser/type state
- parser reparses or substitutes bodies
- parser appends new AST nodes to `ast_nodes_`

This means instantiation is still tightly coupled to parser state, token replay, symbol-table mutation, and substitution helpers.

### 3. The biggest sema gap is late instantiation after `SemanticAnalysis::run()`

The main architectural problem is **not** “templates instantiate before sema”.
The bigger issue is that some instantiations happen **after sema has already finished**.

### Evidence

- `SemanticAnalysis::run()` walks `parser_.get_nodes()` once.
- `FlashCppMain.cpp` constructs sema, runs it once, then starts IR lowering.
- during IR/codegen, the pipeline still triggers lazy materialization:
  - `AstToIr::generateDeferredMemberFunctions()`
  - `IrGenerator_Call_Direct.cpp`
  - `IrGenerator_Call_Indirect.cpp`
  - `IrGenerator_MemberAccess.cpp`
  - `IrGenerator_Stmt_Decl.cpp`
  - `IrGenerator_Visitors_TypeInit.cpp`
- those paths call back into:
  - `parser_->instantiateLazyMemberFunction(...)`
  - `parser_->instantiateLazyStaticMember(...)`
  - `parser_->instantiateLazyClassToPhase(...)`

So late-instantiated nodes can reach codegen without going through the same top-level sema normalization pass that eager nodes receive.

### 4. Lazy class instantiation phases amplify the split ownership

`TemplateRegistry_Lazy.h` and `Parser_Templates_Lazy.cpp` define a phased model:

- `Minimal`
- `Layout`
- `Full`

That is useful for performance, but it means the compiler can progressively materialize more of a template instantiation in response to later use sites.

In practice this creates a second semantic boundary:

- the type exists early
- some members/bodies/initializers appear later
- codegen and member-lookup paths must tolerate partially materialized class state

This is exactly the kind of architecture that benefits from an **incremental post-instantiation sema step**.

### 5. Existing sema-boundary work already points in this direction

`docs/2026-03-21_PARSER_TEMPLATE_SEMA_BOUNDARY_PLAN.md` documents a lot of successful cleanup:

- parser-only fold/pack nodes are now forbidden on the sema-owned surface
- direct `parser_.get_expression_type(...)` fallbacks were removed from `SemanticAnalysis.cpp`
- outer template bindings are carried into more instantiated AST

That work already proves the codebase is moving toward stronger sema ownership.

The remaining issue is no longer “should sema own post-parse AST?”.
It is “how do we make **late materialized** AST obey the same contract?”.

### 6. Constexpr currently mixes value evaluation with template/materialization responsibilities

The constexpr layer is also a partial architecture smell.

`ConstExprEvaluator.h` and `ConstExprEvaluator_*.cpp` show that the evaluator still owns or reaches into:

- parser-triggered template instantiation through `EvaluationContext::parser`
- variable-template instantiation from constexpr call sites
- template-function on-demand instantiation
- current-struct template-binding recovery
- fallback-to-base-template member-function lookup
- special-case synthesis such as `integral_constant::value`

This means constexpr evaluation is not just evaluating semantically known AST; it is still compensating for missing earlier normalization/materialization.

### 7. Constexpr already has sema-style fallback pressure points

A few examples from the current tree:

- `evaluate_function_call()` can instantiate template functions on demand
- `tryEvaluateAsVariableTemplate()` can instantiate variable templates on demand
- `call_constexpr_member_fn_on_object()` falls back from an instantiation to the base template's `StructTypeInfo`
- `ConstExprEvaluator_Members.cpp` synthesises `integral_constant::value` when lookup data is missing
- the non-standard docs already record runtime-style fallback behavior for some constexpr failures

All of these are signs that constexpr is still carrying semantic/materialization recovery logic that would be cleaner upstream.

## Conclusion

I agree with the **goal**, but I would phrase it more precisely:

> FlashCpp does not primarily need “a sema pass after template instantiation” for the eager path, because it already has one.
> It needs a way to ensure that **every instantiated AST body that becomes real** — especially lazy and constexpr-triggered ones — is brought onto the same sema-owned surface before codegen or constexpr-specific fallback logic relies on it.

So I recommend an **incremental/fixpoint sema-after-materialization architecture**, not a naive “run the whole sema pass twice” design.

## Recommended target architecture

### Target invariant

Any instantiated declaration/body/initializer that is materialized into AST and is intended for:

- codegen
- constexpr evaluation
- overload resolution reuse
- member lookup reuse

must first pass through the same semantic-normalization boundary.

### Desired pipeline shape

```text
Parse / initial eager instantiation
    ↓
Semantic normalization of current AST roots
    ↓
Late instantiation work queue drains:
    - lazy class phase promotion
    - lazy member/static-member instantiation
    - deferred template body materialization
    - constexpr-requested instantiation handoff
    ↓
Semantic normalization of newly materialized roots
    ↓
Repeat until no new semantic roots are produced
    ↓
IR lowering / backend / constexpr consumers read sema-owned facts first
```

The important part is **normalize newly materialized roots**, not “rerun everything blindly”.

## Plan

### Workstream 1 — Inventory all post-sema materialization points

- enumerate every parser entry point that can append new instantiated AST after `SemanticAnalysis::run()`
- classify each site as eager, lazy, deferred-body, or constexpr-triggered
- define which of those sites must become sema-visible before downstream use
- document which ones can remain parser-internal because they never escape into sema/codegen surfaces

### Workstream 2 — Introduce a first-class “new semantic roots” queue

- create a single ownership concept for newly materialized AST nodes that still need semantic normalization
- stop relying on ad-hoc “append to `ast_nodes_` and hope downstream paths cope”
- make eager instantiation and lazy instantiation register new roots through the same mechanism
- ensure nested late instantiations can enqueue more work without losing ordering or cycle protection

### Workstream 3 — Make semantic analysis incremental over newly materialized nodes

- split “walk the whole translation unit” from “normalize one newly materialized root”
- reuse existing `normalizeTopLevelNode(...)`-style logic as the incremental unit
- preserve the current boundary checks and template-binding seeding for those late roots
- guarantee that a root is normalized at most once per concrete materialization

### Workstream 4 — Move lazy template materialization in front of codegen consumption

- stop using codegen as the first consumer that discovers semantically unnormalized instantiated bodies
- when a lazy member/static member/class phase is forced, hand the resulting AST back through the semantic-root queue before continuing
- keep codegen fallback only for truly unsupported or still-deferred cases during migration
- tighten the invariant so IR generation assumes “materialized means sema-normalized”

### Workstream 5 — Reduce constexpr's parser-owned responsibilities

- treat constexpr as a consumer of sema-normalized AST rather than a secondary materialization engine
- progressively remove direct parser-triggered template instantiation from evaluator hot paths
- replace evaluator-side fallback lookup/synthesis with sema-owned resolution data where possible
- keep constexpr-specific logic focused on value computation, not AST recovery

### Workstream 6 — Make sema annotations the preferred constexpr inputs

- audit constexpr call resolution, constructor resolution, member access, and static-member lookup for existing sema annotations/caches they can trust first
- extend sema-owned annotations where constexpr still has to re-resolve facts independently
- eliminate evaluator-side special cases that exist only because earlier phases did not record enough semantic information

### Workstream 7 — Decide the end-state for late instantiation

There are two viable end-states:

### Option A — Full fixpoint before IR

- drive all reachable lazy materialization before codegen starts
- sema completes on the final AST surface
- codegen becomes much simpler

### Option B — Incremental on-demand sema during late materialization

- keep lazy instantiation for performance/architecture reasons
- but every newly materialized node is sema-normalized immediately before downstream use

Given the current architecture, **Option B** looks like the lower-risk migration path.
If that works well, the project can later decide whether a more eager “drain everything before IR” model is worthwhile.

### Workstream 8 — Tighten docs and invariants

- update the parser/template/non-standard docs to distinguish eager vs late instantiation clearly
- document which AST surfaces are parser-owned, sema-owned, and constexpr-owned
- document which fallback paths are temporary migration bridges
- add TODO/known-issues entries for any remaining constexpr synthesis or late-semantic gaps that are intentionally deferred

## Recommended implementation order

1. inventory all late materialization sites
2. add a semantic-root queue abstraction
3. teach lazy member/static-member instantiation to enqueue roots
4. normalize newly materialized roots before codegen continues
5. migrate constexpr to consume the new invariant
6. remove now-redundant fallback logic

## 2026-04-07 progress update

Started implementation of the low-risk **Option B** path:

- added a parser-owned pending semantic-root queue for late materialized AST
- taught late lazy/template materialization paths to register new roots there
- added `SemanticAnalysis::normalizePendingSemanticRoots()` so the main pass can drain late roots incrementally
- hooked codegen and constexpr-triggered lazy/template materialization paths to normalize pending roots before continuing

This does **not** finish the audit plan yet, but it establishes the first concrete sema handoff for:

- lazy member-function instantiation
- lazy static-member instantiation
- constexpr-triggered function-template instantiation
- constexpr-triggered variable-template instantiation

Remaining work is still needed to reduce fallback logic and tighten the invariant across all late-materialization sites.

## Why this should simplify the code

If this plan succeeds:

- parser stops being responsible for more and more downstream semantic recovery
- codegen stops discovering fresh template bodies first
- constexpr stops doing as much ad-hoc lookup/materialization repair
- sema becomes the single place where newly-real AST becomes trustworthy

That should reduce duplicated overload/member/type reasoning across parser, constexpr, and codegen, while fitting the direction the existing sema-boundary work has already started.
