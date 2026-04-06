# Constructor Overload Resolution – Architecture Review & Refactor Plan

**Date:** 2026-04-06  
**Context:** Follows PR "Advance std header support by parsing constructor calls through type aliases"

---

## Summary

Constructor overload resolution is currently performed independently at **four distinct pipeline stages** rather than once in SemanticAnalysis.  
The selected constructor is never stored on `ConstructorCallNode`, so every downstream stage must re-run `resolve_constructor_overload` from scratch using its own locally-built argument-type list.  
This creates duplicate logic, mismatched resolution paths, and makes it hard to add proper overload diagnostics.

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

---

### Step 6 — Remove the Parser-level overload resolution call

File: src/Parser_Statements.cpp  (line 1671)

Overload resolution during parsing is premature. Investigate why it was added and either:
  (a) Remove it if the resolution result is not used before SemanticAnalysis runs, or
  (b) Replace it with a deferred annotation that SemanticAnalysis fills in during the
      normal sema walk.

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
