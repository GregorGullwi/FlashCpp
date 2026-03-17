# Implicit Cast Semantic Analysis Migration Plan

## Overview

This document tracks the phased migration of implicit type conversions from the
code-generation (IR/backend) layer into the **semantic analysis** pass
(`SemanticAnalysis`).  The goal is to annotate every implicit conversion at the
AST level—before any IR is emitted—so that:

1. Conversions are visible to future diagnostics, linters, and optimisers.
2. Code-gen can consume pre-computed `ImplicitCastInfo` slots instead of
   re-deriving conversion logic on every IR emission path.
3. The compiler moves closer to a clean separation between *semantic validation*
   and *lowering*.

Each expression that requires an implicit conversion is given a `SemanticSlot`
containing a `CastInfoIndex` into the `cast_info_table_`.  The slot records the
source type, target type, `StandardConversionKind`, and post-cast value
category.

---

## Architecture

```
  Parser (AST)
      │
      ▼
  SemanticAnalysis::run()          ← annotates ImplicitCastInfo slots
      │
      ▼
  IrGenerator (IR emission)        ← reads slots via sema_->getSlot(key)
      │                               falls back to can_convert_type() when
      │                               no annotation exists
      ▼
  IRConverter / Backend (x86-64)
```

### Key types (src/SemanticTypes.h)

| Type                      | Purpose |
|---------------------------|---------|
| `CanonicalTypeId`         | Interned handle into `TypeContext` (1-based, 0 = invalid). |
| `CanonicalTypeDesc`       | Full type descriptor: base type, pointer levels, cv, ref, arrays. |
| `SemanticSlot`            | 8-byte packed annotation on an expression node (type + cast index + value category + flags). |
| `ImplicitCastInfo`        | Side-table entry: source/target type IDs, `StandardConversionKind`, value category after cast. |
| `StandardConversionKind`  | C++20 conversion taxonomy (IntegralPromotion, FloatingIntegralConversion, BooleanConversion, PointerConversion, …). |

### Annotation helpers (src/SemanticAnalysis.h)

| Method | Scope |
|--------|-------|
| `tryAnnotateConversion(expr, target_type_id)` | Core: annotates any expression with a standard primitive conversion. |
| `tryAnnotateReturnConversion(expr, ctx)` | Return statements: expr → declared return type. |
| `tryAnnotateBinaryOperandConversions(bin_op)` | Binary arithmetic/comparison: usual arithmetic conversions. |
| `tryAnnotateShiftOperandPromotions(bin_op)` | Shift operators: independent integral promotions per C++20 [expr.shift]. |
| `tryAnnotateContextualBool(expr)` | Control-flow conditions, `!`, `&&`/`||`, ternary condition → bool. |
| `tryAnnotateCallArgConversions(call)` | Function call arguments → parameter types. |
| `tryAnnotateConstructorCallArgConversions(call)` | Constructor call arguments → parameter types (Phase 8). |
| `tryAnnotateInitListConstructorArgs(init_list, struct_info)` | Direct-init `Type obj(args…)` via InitializerListNode (Phase 8). |
| `tryAnnotateTernaryBranchConversions(ternary)` | Ternary branches: usual arithmetic conversions on 2nd/3rd operands. |
| `tryResolveCallableOperator(call)` | Pre-resolve `operator()` for callable objects (functors/closures). |

---

## Phase History

### Phase 1 — Pipeline seam ✅

Established the `SemanticAnalysis` pass as a post-parse, pre-codegen step.
Introduced `SemanticSlot`, `ImplicitCastInfo`, `TypeContext`, and the
`normalizeTopLevelNode` / `normalizeStatement` / `normalizeExpression` walk.
No conversions were annotated yet; the pass only counted nodes and resolved
`auto` return types.

**Files:** `src/SemanticAnalysis.h`, `src/SemanticAnalysis.cpp`, `src/SemanticTypes.h`

### Phase 2 — Return-value conversions ✅

`tryAnnotateReturnConversion`: when a return expression's inferred type differs
from the function's declared return type and a standard (non-user-defined)
conversion exists, a `SemanticSlot` is filled.

**Codegen consumption:** `IrGenerator` reads `sema_->getSlot()` at return
statements and calls `generateTypeConversion()` with the annotated types,
falling back to the old `can_convert_type()` path when no annotation exists.

### Phase 3 — Binary operand conversions ✅

`tryAnnotateBinaryOperandConversions`: for arithmetic (`+`, `-`, `*`, `/`, `%`)
and comparison (`<`, `>`, `<=`, `>=`, `==`, `!=`) operators, computes the
common type via `get_common_type()` and annotates each operand that needs
widening.

### Phase 4 — Shift operand promotions ✅

`tryAnnotateShiftOperandPromotions`: per C++20 [expr.shift], shift operands
undergo **independent** integral promotions (not usual arithmetic conversions).
Each operand is promoted separately; the result type is the promoted LHS type.

### Phase 5 — Contextual bool (primitives) ✅

`tryAnnotateContextualBool`: conditions in `if`/`while`/`for`/`do-while`,
ternary conditions, and logical operator (`&&`/`||`) operands are annotated
with `BooleanConversion` when the expression type is a non-bool primitive
(int, float, char, etc.).

### Phase 6 — Function call argument conversions ✅

`tryAnnotateCallArgConversions`: for each argument in a `FunctionCallNode`,
resolves the target function (via symbol table or `op_call_table_`), compares
argument types against parameter types, and annotates mismatches.

Also: `tryResolveCallableOperator` pre-resolves `operator()` for callable
objects so that codegen does not need to perform its own member-function lookup.

### Phase 7 — Ternary branch conversions & assignment RHS ✅

`tryAnnotateTernaryBranchConversions`: per C++20 [expr.cond]/7, applies usual
arithmetic conversions to the second and third operands of `?:`.

Simple assignment (`=`): the RHS is annotated with the LHS type when they
differ.

Variable initialiser: `tryAnnotateConversion(init, decl_type_id)` annotates
the initialiser expression with the declared variable type.

### Phase 8 — Constructor args, enum/pointer bool, float→int folding ✅

**PR #935** — three sub-features:

#### 8a: Constructor call argument conversions

Two new annotation methods mirror the existing `tryAnnotateCallArgConversions`:

- `tryAnnotateConstructorCallArgConversions(ConstructorCallNode)` — handles
  rvalue constructor calls like `Pair(42, 3.14)`.
- `tryAnnotateInitListConstructorArgs(InitializerListNode, StructTypeInfo)` —
  handles direct-init variable declarations like `Pair p(42, 3.14)` where the
  parser stores arguments in an `InitializerListNode`.

Both use `resolve_constructor_overload()` to find the matching constructor,
then iterate arguments against parameter types and call `tryAnnotateConversion`
for each mismatch.  Wrapped in `try/catch` for best-effort semantics.

**Codegen consumption** (sema-first + fallback pattern):

- `src/IrGenerator_Visitors_Decl.cpp` — `ConstructorCallNode` visitor
- `src/IrGenerator_Stmt_Decl.cpp` — `InitializerListNode` path for
  variable declarations with struct types

```cpp
// Pseudocode (identical at both sites):
init = visitExpressionNode(arg);
if (param is not a reference) {
    slot = sema_->getSlot(&arg);
    if (slot has cast)
        init = generateTypeConversion(init, from_t, to_t, ...);
    else if (arg_type != param_type)
        // fallback: standard conversion
        init = generateTypeConversion(init, arg_type, param_base_type, ...);
}
```

**Test:** `tests/test_ctor_call_arg_implicit_cast_ret0.cpp`

#### 8b: Enum and pointer contextual bool

`tryAnnotateContextualBool` extended: when `tryAnnotateConversion` returns
false (it only handles primitive scalars), the method now checks for enum and
pointer types and manually creates a `BooleanConversion` or
`PointerConversion` annotation.

- `StandardConversionKind::PointerConversion` added to `SemanticTypes.h`.
- Per C++20 [conv.bool]/1: zero/null → false, non-zero/non-null → true.
- The backend `TEST` instruction already handles these correctly; the
  annotations record semantic intent for future codegen migration.

**Tests:** `tests/test_contextual_bool_enum_ret0.cpp`,
`tests/test_contextual_bool_pointer_ret0.cpp`

#### 8c: Literal float→int constant folding

`generateTypeConversion` in `src/IrGenerator_Expr_Conversions.cpp` now
constant-folds `double` literal → integer at compile time instead of emitting a
`FloatToInt` IR instruction.  This fixes a latent crash: `handleFloatToInt` in
the backend only supports `TempVar`/`StringHandle` operands, not raw `double`
`IrValue`s.

- Float literal → int: folded at compile time (`static_cast<long long>(src_val)`).
- Int literal → float: still emits `IntToFloat` IR (the backend's
  `loadTypedValueIntoRegister` handles integer literals correctly).

---

## Future Work

- **Phase 9:** Migrate remaining codegen-only conversion sites (e.g. member
  initialiser lists, default argument evaluation, template argument conversions).
- **Phase 10:** Unify `determineConversionKind()` and `can_convert_type()` into
  a single `buildConversionPlan()` helper (see TODO in `SemanticAnalysis.cpp`).
- **Phase 11:** Emit diagnostics (warnings) for narrowing conversions based on
  the annotated `StandardConversionKind` (e.g. `double→int` in braced-init).
- **Phase 12:** Remove fallback conversion logic from codegen once all sites are
  covered by sema annotations.

---

## Test Results (as of Phase 8)

**1548 pass / 0 fail / 52 expected-fail**

New tests added in Phase 8:
- `tests/test_ctor_call_arg_implicit_cast_ret0.cpp`
- `tests/test_contextual_bool_enum_ret0.cpp`
- `tests/test_contextual_bool_pointer_ret0.cpp`
