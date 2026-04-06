# Constructor Overload Resolution – Architecture Review & Refactor Plan

**Date:** 2026-04-06  
**Context:** Follows PR "Advance std header support by parsing constructor calls through type aliases"

---

## Summary

Constructor overload resolution is currently performed independently at **four distinct pipeline stages** rather than once in SemanticAnalysis.  
The selected constructor is never stored on `ConstructorCallNode`, so every downstream stage must re-run `resolve_constructor_overload` from scratch using its own locally-built argument-type list.  
This creates duplicate logic, mismatched resolution paths, and makes it hard to add proper overload diagnostics.

---

## Current Branch Progress

Status on `copilot/refactor-unifying-constructor-overload-selection` after the semantic-analysis follow-up:

- **Done:** `ConstructorCallNode` stores a sema-resolved constructor pointer.
- **Done:** `InitializerListNode` now also stores a sema-resolved constructor pointer for brace-init constructor paths.
- **Done:** `SemanticAnalysis::tryAnnotateConstructorCallArgConversions` stores the selected constructor on `ConstructorCallNode`.
- **Done:** `SemanticAnalysis::tryAnnotateInitListConstructorArgs` stores the selected constructor on `InitializerListNode`.
- **Done:** several IrGenerator and ConstExprEvaluator paths now consume the sema annotation first and only fall back when it is absent.
- **Done:** parser-time brace-init constructor overload selection was removed from the `ConstructorCallNode` path for user-defined constructors; semantic analysis now reports those no-match / ambiguity diagnostics after parsing instead of during parsing.
- **Remaining:** `src/IRConverter_ConvertMain.cpp` still re-runs constructor overload resolution from lowered IR argument types.
- **Remaining:** the parser still keeps a member-less/non-aggregate brace-init guard outside the unified sema-owned constructor path.
- **Remaining:** some constructor-selection fallbacks still exist in codegen by design for unresolved/dependent cases and should only be reached when sema did not annotate a constructor.

This means the refactor is now past the “annotation plumbing” stage. The remaining work is primarily about eliminating the last non-sema authoritative resolution points rather than introducing new storage/caching mechanisms.

---

## Current Architecture (Where Resolution Happens Today)

### 1. Parser – `src/Parser_Statements.cpp` (line 1671)
`resolve_constructor_overload` is called during statement parsing.  
This is unusual – overload resolution during parsing limits the information available and happens before name binding is complete.

### 2. SemanticAnalysis – `src/SemanticAnalysis.cpp` (lines 4369, 4440)
`tryAnnotateConstructorCallArgConversions` and `tryAnnotateInitListConstructorArgs` call  
`resolve_constructor_overload` with `skip_implicit=true`.  
The result is used **only to annotate implicit argument conversions**; it is never stored back on the node.  
Uses `buildOverloadResolutionArgType` (the sema-side helper).

### 3. IrGenerator – `src/IrGenerator_Stmt_Decl.cpp` (lines 680, 1349, 2049 + 3 more)
`resolve_constructor_overload` / `resolve_constructor_overload_arity` is called **six times** in this file alone for different initialization paths (static data, aggregate, and direct constructor calls).  
Also called in:
- `src/IrGenerator_Visitors_Decl.cpp` (×2, lines 2883, 2891)
- `src/IrGenerator_Visitors_Namespace.cpp` (×2, line 291)
- `src/IrGenerator_Visitors_TypeInit.cpp` (×2, line 941)
Uses `buildCodegenOverloadResolutionArgType` – a codegen-side helper that asks sema first and falls back to the parser's `get_expression_type`.

### 4. ConstExpr Evaluator – `src/ConstExprEvaluator_Members.cpp` (×4)
Independently resolves constructors for compile-time evaluation with its own argument-type extraction.

### 5. IRConverter – `src/IRConverter_ConvertMain.cpp` (lines 4951, 4957)
Performs a **third round** of overload resolution after IR is already emitted, working from `TypedValue` arguments already lowered to IR types.  
This is the latest possible point and is the most fragile: it operates on IR-level type information (`buildTypeSpecFromTypedValue`) rather than AST types.

---

## Key Structural Problems

| Problem | Impact |
|---|---|
| No single authoritative resolution point | Different stages can pick different overloads |
| `ConstructorCallNode` stores no resolved constructor | All downstream stages re-resolve from scratch |
| Two parallel arg-type builders (`buildOverloadResolutionArgType` vs `buildCodegenOverloadResolutionArgType`) | Subtle disagreements in argument type classification |
| IRConverter resolves constructors from lowered IR types | By that point, reference/rvalue qualifiers and type aliases may have been erased |
| Parser calls overload resolution | Resolution before full name binding is unreliable |

---

## Desired Architecture

Overload resolution should happen **once, in SemanticAnalysis**, and the result stored on `ConstructorCallNode`.  
All downstream stages (IrGenerator, ConstExprEvaluator, IRConverter) read the annotation instead of re-resolving.

```
Parser           → produces ConstructorCallNode  (no resolution)
SemanticAnalysis → resolves overload, stores ConstructorDeclarationNode* on node
IrGenerator      → reads node.resolved_constructor(), emits ConstructorCallOp
ConstExprEval    → reads node.resolved_constructor(), evaluates
IRConverter      → reads ConstructorCallOp.resolved_ctor_name (set by IrGenerator from annotation)
```

---

## Comparison: Regular Polymorphic / Function Call Overload Resolution

The same full constructor-centralization plan is **not directly necessary** for ordinary function calls, because normal call sites already keep substantially more resolved information on the AST and in sema side tables.

### What is already better for regular calls

1. **`CallExprNode` already stores a callee descriptor**  
   `src/AstNodeTypes_DeclNodes.h` (`CalleeDescriptor`, `CallExprNode`) can already carry a resolved `FunctionDeclarationNode*` for free/member/static calls rather than just a raw declaration token.

2. **Parser already resolves many non-constructor calls up front**  
   `src/Parser_Expr_PrimaryExpr.cpp` calls `resolve_overload` for ordinary calls in multiple places (for example around the main direct-call path and later operator/member-call recovery paths). The chosen function can be baked into the emitted `CallExprNode`.

3. **SemanticAnalysis already has call-resolution side tables**  
   `src/SemanticAnalysis.cpp` maintains direct-call and `operator()` caches (for example `getResolvedDirectCall`, `op_call_table_`) and separately annotates call-argument conversions via `tryAnnotateCallArgConversions`.

4. **IrGenerator generally consumes the already-resolved target**  
   `src/IrGenerator_Call_Direct.cpp` primarily reads the callee info or sema-provided direct-call cache. Unlike constructors, normal direct calls do not broadly re-run overload resolution in IR conversion from erased IR types.

5. **IR already distinguishes direct, indirect, and virtual dispatch**  
   `CallOp` / `VirtualCallOp` carry more explicit dispatch information than constructor calls do today, so the late pipeline is already less ambiguous.

### Why the exact same refactor is harder for regular calls

A constructor call is always “pick one constructor for one class object creation”. Regular calls have more moving parts:

- **ADL / hidden friends**: some candidates only exist after associated-namespace lookup
- **member vs free vs static member vs indirect calls**: different dispatch kinds already encoded in `CalleeDescriptor`
- **operator calls / functors**: `operator()` can resolve through separate sema tables
- **templates / dependent calls**: some calls must stay deferred until instantiation
- **virtual dispatch**: the declaration may be known statically, but the final implementation is runtime-selected
- **function pointers / callable objects**: not every `f(x)` shape is an overload-set problem

### Practical conclusion

So the constructor plan **can inspire** a follow-up for regular calls, but the goal should be narrower:

- keep parser/name-lookup machinery where it is for ADL-sensitive and dependent cases
- let SemanticAnalysis **upgrade or confirm** the selected regular callee when possible
- have codegen consume a single sema-authoritative result whenever available
- preserve existing fallbacks for dependent/template/indirect/virtual cases

If we do a regular-call follow-up, it should probably be framed as **“strengthen existing call-target caching and sema authority”**, not “move every bit of call resolution out of the parser”.

---

## Applicability to Regular Function Calls

If we want a follow-up architecture pass for ordinary polymorphic/function calls, the analogous plan would look more like this:

1. **Keep `CallExprNode` / `CalleeDescriptor` as the carrier**
   - constructors need a new annotation slot because they currently carry nothing comparable
   - ordinary calls already have a carrier, so a new side table may be optional

2. **Make sema the authoritative post-parse confirmation step**
   - parser may still build an initial target for normal calls
   - sema should confirm/upgrade/store the chosen `FunctionDeclarationNode*` whenever the call is non-dependent and fully known

3. **Teach codegen to trust sema first**
   - for direct calls this is already partially true
   - remaining fallback-only paths should be limited to genuinely unresolved cases

4. **Do not try to remove parser participation blindly**
   - ADL, hidden friends, explicit template arguments, callable objects, and dependent calls are tightly coupled to parsing/name lookup

5. **Keep dispatch-kind-specific fallbacks**
   - direct call
   - indirect/function-pointer call
   - member/static-member call
   - virtual call
   - `operator()` / functor call

Estimated complexity is meaningfully higher than the constructor refactor because normal calls already support more dispatch modes and more partial-resolution states.

---

## Gemini PR Comment Follow-up

Two review comments from Gemini were worth checking against the latest branch state:

### 1. Type-alias lookup duplication

This was a good suggestion, but it is already addressed on the branch now.  
The alias-to-constructible-class lookup logic was consolidated into:

- `src/Parser_Expr_PrimaryExpr.cpp`
- helper: `tryResolveConstructibleClassAlias(...)`

That helper is now reused by:

- the early constructor-call path for `TypeName(args...)`
- the later alias-based `Alias(args...)` path
- the alias-based brace-init path

So this particular refactor suggestion should be considered **done**.

### 2. Parenthesized constructor argument parsing duplication

This comment is still valid.

There are still two near-identical loops in `src/Parser_Expr_PrimaryExpr.cpp` that parse comma-separated constructor arguments inside `(...)`:

- the main constructor-call path around the direct `consume("(")` constructor branch
- the alias-based constructor-call path added for `Alias(args...)`

Those should be folded into a shared helper in a follow-up, ideally something like:

```cpp
Parser::parse_parenthesized_constructor_arguments(...)
```

or a more general helper shared with function-call argument parsing if the surrounding invariants can be aligned cleanly.

This is only a maintainability cleanup, not a correctness blocker, but it would remove duplicated error handling and keep future parser fixes from drifting between the two paths.

---

## Refactor Prompt for Follow-up PR

```
Task: Centralize constructor overload resolution in SemanticAnalysis so that IrGenerator,
ConstExprEvaluator, and IRConverter all read a pre-resolved result instead of re-running
resolve_constructor_overload independently.

Repository: GregorGullwi/FlashCpp
Base branch: copilot/implement-cpp20-features-in-standard-headers (or main once merged)

---

### Step 1 — Add a resolved-constructor annotation slot to ConstructorCallNode

File: src/AstNodeTypes_DeclNodes.h
Class: ConstructorCallNode (line 2311)

Add a mutable pointer that SemanticAnalysis writes and all later stages read:

  mutable const ConstructorDeclarationNode* resolved_constructor_ = nullptr;

Add accessor and setter:

  const ConstructorDeclarationNode* resolved_constructor() const {
      return resolved_constructor_;
  }
  void set_resolved_constructor(const ConstructorDeclarationNode* ctor) const {
      resolved_constructor_ = ctor;
  }

Use `mutable` so SemanticAnalysis can annotate a const node reference (same pattern used
by ImplicitCastInfo / CastInfoIndex on ExpressionNode).
Alternatively, use the existing side-table pattern (allocate a slot index and store it on
the node like `cast_info_index`) if you prefer to avoid mutable fields on AST nodes.

---

### Step 2 — Make SemanticAnalysis the single point of constructor overload resolution

File: src/SemanticAnalysis.cpp

Extend `tryAnnotateConstructorCallArgConversions` (line 4323) to store the selected
constructor on the node after resolution, not just annotate conversions:

  auto resolution = resolve_constructor_overload(*struct_info, arg_types, /*skip_implicit=*/true);
  if (resolution.selected_overload) {
      call_node.set_resolved_constructor(resolution.selected_overload);
      // ... existing conversion-annotation loop ...
  }

Also extend `tryAnnotateInitListConstructorArgs` (line 4412) similarly for brace-init.

Ensure `tryAnnotateConstructorCallArgConversions` is called for every ConstructorCallNode
that is walked by the sema visitor (currently gated at SemanticAnalysis.cpp line 2062 –
verify all construction paths reach this gate, including aggregate/P0960 paths).

---

### Step 3 — IrGenerator reads the annotation instead of re-resolving

Files to update (all call resolve_constructor_overload independently today):
  src/IrGenerator_Stmt_Decl.cpp  (lines 668–689, 1274–1370, 1982–2068)
  src/IrGenerator_Visitors_Decl.cpp  (lines 2862–2900)
  src/IrGenerator_Visitors_Namespace.cpp  (line 291 region)
  src/IrGenerator_Visitors_TypeInit.cpp  (line 941 region)

Pattern to apply at each call site:

  const ConstructorDeclarationNode* matching_ctor = ctor_call.resolved_constructor();
  if (!matching_ctor) {
      // sema did not resolve (e.g., template-dependent context) — keep existing fallback
      matching_ctor = localFallbackResolve(ctor_call, struct_info);
  }

Preserve the existing fallback path (call resolve_constructor_overload / arity fallback)
for cases where SemanticAnalysis could not resolve (template-dependent arguments, etc.).

**Current progress:** direct constructor-call paths already follow this pattern, and the
brace-init declaration path now also checks the `InitializerListNode` annotation before
falling back to local overload resolution. Remaining cleanup in this step is mostly about
auditing the last fallback-heavy constructor materialization sites and deleting now-dead
code only after confirming all unresolved/template-dependent cases still behave correctly.

Remove `buildCodegenOverloadResolutionArgType` once no call sites remain that need it
for constructor resolution specifically. The function may still be needed for regular
function-call overload resolution; check callers before deleting.

---

### Step 4 — ConstExprEvaluator reads the annotation

File: src/ConstExprEvaluator_Members.cpp  (line 381 region)

Replace the local `resolve_constructor_overload` call with:

  const ConstructorDeclarationNode* matching_ctor =
      ctor_call.resolved_constructor();
  if (!matching_ctor) {
      // fallback for constexpr contexts without full sema pass
      auto resolution = resolve_constructor_overload(*struct_info, arg_types, false);
      matching_ctor = resolution.selected_overload;
   }

---

### Step 5 — IRConverter reads the selected constructor name from ConstructorCallOp

File: src/IRConverter_ConvertMain.cpp  (lines 4910–4960)

IrGenerator sets ConstructorCallOp with the emitted constructor function name already.
After Step 3, IrGenerator always picks the right constructor before emitting the op, so
IRConverter should be able to look up the function directly by name without needing to
re-run overload resolution.

Audit the IRConverter path to confirm that ConstructorCallOp.struct_name + the already-
emitted argument list uniquely identifies the constructor after Step 3, and remove the
redundant `resolve_constructor_overload` calls at lines 4951 and 4957.

**Current progress:** not started yet. This is now one of the two main remaining
authoritative-resolution gaps.

---

### Step 6 — Remove the Parser-level overload resolution call

File: src/Parser_Statements.cpp  (line 1671)

Overload resolution during parsing is premature. Investigate why it was added and either:
  (a) Remove it if the resolution result is not used before SemanticAnalysis runs, or
  (b) Replace it with a deferred annotation that SemanticAnalysis fills in during the
      normal sema walk.

**Current progress:** partially complete. The parser no longer performs authoritative
brace-init overload resolution for the `ConstructorCallNode` path; it now builds the
node and lets semantic analysis select the overload or emit the diagnostic. The
remaining parser cleanup is the older member-less special-case gate, and the
`InitializerListNode` declaration path still has not been unified with the same
post-parse diagnostic flow.

---

### Step 7 — Regression tests

Run `make main CXX=clang++` then `bash tests/run_all_tests.sh`.
The suite should pass all existing tests (currently ~1899 passing, 121 expected-fail).

Add or verify regression tests for:
  - Constructor with multiple overloads selected by argument type
  - Constructor via typedef/using alias (covered by alias_constructor_ret*.cpp added in PR #1120)
  - Constructor via template alias instantiation
  - Aggregate initialization with no user-defined constructor (P0960)
  - Copy and move constructor selection
  - Converting constructor used in implicit conversion context

---

### Important implementation notes

1. Do NOT use default parameter values in any new function signatures.
   Every argument must be passed explicitly (project convention).

2. Keep the fallback path in IrGenerator for template-dependent calls where
   SemanticAnalysis may not have resolved the overload. Never remove the fallback
   outright — just only reach it when the annotation is absent.

3. The `skip_implicit` flag difference between sema (true) and codegen (false) currently
   matters for implicit copy/move constructors. After centralization, standardize on
   the sema behavior (skip_implicit=true during resolution, but allow the resolved
   constructor to be implicit). Audit every call site to ensure consistent behavior.

4. ConstructorCallNode is currently immutable from callers' perspective. If adding a
   mutable field feels wrong, use the existing CastInfoIndex side-table pattern from
   SemanticTypes.h instead — allocate a ConstructorResolutionInfo entry and store its
   index on the node, the same way ImplicitCastInfo is stored on ExpressionNode.

5. Use FLASH_LOG_FORMAT(Codegen, Debug, ...) to log which constructor was resolved and
   from which source (sema annotation vs. fallback) to aid future debugging.
```

---

## Possible Separate Prompt for Regular Function Calls

Use this only as a later, separate cleanup once the constructor-specific refactor is done.

```
Task: Audit and strengthen regular function-call target resolution so SemanticAnalysis
becomes the authoritative post-parse source of truth for non-dependent direct calls,
without breaking parser-coupled ADL, hidden-friend, operator(), template, indirect, or
virtual-call behavior.

Repository: GregorGullwi/FlashCpp
Suggested base: after the constructor-overload-centralization PR lands

Goals:
1. Inventory every place regular calls are resolved or re-resolved.
2. Confirm where `CallExprNode` / `CalleeDescriptor` already carries a resolved
   `FunctionDeclarationNode*`.
3. Teach sema to upgrade/store the selected direct callee whenever the call is
   non-dependent and fully known.
4. Make IrGenerator prefer the sema-authoritative result and only fall back for
   unresolved/dependent/indirect/virtual cases.
5. Do not remove parser participation for ADL-sensitive and template-dependent call
   formation unless you can prove behavior stays identical.

Important complications to audit explicitly:
- ADL and hidden friends
- member vs static member vs free function calls
- `operator()` resolution and callable objects
- indirect/function-pointer calls
- virtual dispatch (`VirtualCallOp`)
- explicit template arguments and dependent calls

Nice-to-have cleanup:
- see whether direct-call fallback/remap logic in codegen can be reduced once sema-owned
  call target caching is authoritative more often
- evaluate whether the current global overload-resolution cache can be used more
  consistently for ordinary calls

Validation:
- `make main CXX=clang++`
- `bash tests/run_all_tests.sh`
- targeted regression tests covering direct overloads, member overloads, ADL-only hidden
  friends, callable objects, virtual calls, and function-pointer calls
```

---

## Effort Estimate

| Step | Estimated Size | Risk |
|---|---|---|
| 1 – Add annotation slot | Small (< 20 lines) | Low |
| 2 – Sema stores resolved ctor | Medium (~50 lines) | Low |
| 3 – IrGenerator reads annotation | Medium-Large (~150 lines across 4 files) | Medium – keep fallback paths |
| 4 – ConstExprEvaluator | Small (~20 lines) | Low |
| 5 – IRConverter cleanup | Small-Medium (~40 lines) | Medium – audit required |
| 6 – Parser cleanup | Small (~20 lines) | Low-Medium |
| 7 – Tests | Small | Low |

Total: ~300–400 lines changed. No new features required.

---

## Exit Criteria

This refactor should be considered complete once all of the following are true:

- `ConstructorCallNode` and brace-init constructor carriers (`InitializerListNode`) both preserve the sema-selected constructor for downstream consumers.
- Semantic analysis is the authoritative constructor-overload selection point for non-dependent constructor calls.
- IrGenerator prefers the sema annotation in every constructor emission path and only falls back when sema genuinely could not resolve.
- Constexpr evaluation prefers the sema annotation in every constructor-evaluation path and only falls back when running outside a full sema flow.
- IRConverter no longer calls `resolve_constructor_overload` for `ConstructorCallOp`.
- Parser no longer performs constructor overload selection for the `ConstructorCallNode` brace-init path; any remaining parser gate must be limited to structural validation or legacy non-unified paths that are explicitly tracked.
- `make main CXX=clang++` succeeds.
- `bash tests/run_all_tests.sh` succeeds.
