# Known Issues — FlashCpp

This file records confirmed bugs and quality-of-implementation (QoI) deficiencies in the
FlashCpp C++20 compiler. Each entry includes the root cause, the affected code path, and
(where applicable) a recommended fix.

---

## 1) MSVC `<limits>` now stops later on `numeric_limits` constexpr member initialization

- **Symptom**: On Windows/MSVC STL, `tests/std/test_std_limits.cpp` now gets past
  the UCRT `__crt_va_start` wrappers but stops later with
  `Fatal error: static constexpr member initializer for 'std::_Num_float_base::has_denorm' is not a constant expression: initializer is unresolved`.
- **Current frontier**: `limits` around the `_Num_base` / `_Num_float_base`
  `static constexpr float_denorm_style has_denorm = denorm_absent/present;`
  members.
- **Root cause**: Not reduced yet. The parser and preprocessor now survive the
  previous `corecrt_wstdio.h` barrier, so the remaining blocker appears to be
  constexpr/sema handling for enum-valued `static constexpr` data members in the
  MSVC STL numeric-limits hierarchy.
- **Impact**: Windows/MSVC `<limits>` analysis is no longer "time to first UCRT
  parse failure", but the header still does not complete semantic analysis.

---

## 2) `tests/std/test_std_ratio.cpp` — link-time `__security_cookie` conflict

**Remaining blockers** (non-crashing, but prevent `.o` output):

### 2a) Standard-library template-instantiation noise during `<ratio>` compile

- **Symptom**: compiling `tests/std/test_std_ratio.cpp` now emits
  `[ERROR][Templates] [depth=1]: All 2 template overload(s) failed for 'swap'`,
  the same error for `std::swap`, and
  `[ERROR][Templates] [depth=1]: All 1 template overload(s) failed for '_Hash_representation'`
  before the later link failure.
- **Root cause**: still under investigation. These diagnostics appear during
  standard-header template processing, but they are not the first fatal stop in
  the compilation.
- **Impact**: Diagnostic noise only; not a correctness failure in isolation.

### 2b) Link-time `__security_cookie` multiple-definition conflict

- **Symptom**: `tests/std/test_std_ratio.cpp` now compiles successfully but
  fails to link with `LIBCMT.lib(gs_cookie.obj) : error LNK2005:
  __security_cookie already defined`.
- **Root cause**: Still under investigation. FlashCpp's output now collides
  with the CRT-provided GS cookie symbol.
- **Affected path**: Link stage for the object generated from `<ratio>` and the
  headers it pulls in.
- **Impact**: `tests/std/test_std_ratio.cpp` still does not produce a runnable
  executable, even though it now compiles.
- **Fix direction**: Audit where FlashCpp synthesizes or exports the GS cookie
  runtime state and make it coexist with the CRT-provided definition instead of
  emitting a second strong symbol.

---

## 3) Template lambdas with own template parameters not instantiated at call site

- **Symptom**: A template lambda such as `[]<typename T>(T value) { return value; }` compiles
  and produces correct output for simple pass-through cases (e.g. `identity(42)` returns 42),
  but the template parameter `T` is never properly instantiated — it is stored as
  `TypeIndex{0, UserDefined}` (an invalid TypeIndex) throughout the compilation.
- **Root cause**: FlashCpp does not yet implement per-call instantiation for lambdas that
  declare their own template parameters via `[]<typename T>(...)`.  The `outer_template_param_names`
  / `outer_template_args` mechanism in `LambdaInfo` handles template parameters from the
  *enclosing* function template, but the lambda's *own* template parameter list (stored in
  `LambdaExpressionNode::template_params_`) is not propagated into the call-site deduction
  path in `IrGenerator_Call_Indirect.cpp`.
- **Affected path**: `IrGenerator_Visitors_Namespace.cpp` → `visitReturnStatement` (the
  `is_struct_type(expr_category) && !tryGetTypeInfo(expr_type_index)` branch); also
  `IrGenerator_Call_Indirect.cpp` (does not call `setDeducedType` for lambda own-template params).
- **Workaround**: The codegen fallback in `visitReturnStatement` calls `generateTypeConversion`
  when `!expr_type_index.is_valid()`, which happens to produce correct object code for
  simple forwarding lambdas (the ABI-level value is already the right bit pattern).
- **Impact**: Complex lambda own-template-parameter use (non-identity body, multi-parameter
  template lambdas, `T` used in non-trivial type positions) may produce incorrect code or
  crash.
- **Fix direction**: Store `LambdaExpressionNode::template_params_` in `LambdaInfo`; detect
  at the call site in `IrGenerator_Call_Indirect.cpp` that a lambda has own template params;
  deduce each param from the corresponding argument type (similar to the `isPlaceholderAutoType`
  path for generic lambdas); propagate the deduced types into `resolved_param_nodes`.
  Additionally, fix the parser fallback in `Parser_Expr_ControlFlowStmt.cpp` line ~929
  which stores `TypeCategory::Int` instead of `TypeCategory::Auto` when return-type
  deduction fails — this should emit a proper placeholder so sema can re-deduce.

---

## 4) Sema does not annotate implicit conversions for non-standard-arithmetic binary operands

- **Symptom**: Binary expressions where one or both operands are non-standard-arithmetic
  types (unscoped enums, pointer types, function pointer types, user-defined types) fall
  back to `generateTypeConversion` in the codegen layer instead of being annotated by sema.
- **Root cause**: `SemanticAnalysis::normalizeBinaryOperator` (called during sema pass)
  annotates implicit arithmetic conversions for standard arithmetic types (int, float, etc.)
  but does not emit conversion annotations for:
    - Unscoped enum → underlying integer type (e.g. `Status::OK == 1`)
    - Static local / global variable pointer → value type conversions
    - Function pointer nullptr comparisons
- **Affected path**: `IrGenerator_Expr_Operators.cpp` Phase 15 LHS/RHS conversion block
  (lines ~3384–3420); the existing `InternalError` guard fires only for sema-normalized +
  both standard-arithmetic mismatches; the `generateTypeConversion` fallback handles the rest.
- **Impact**: The fallback produces correct code today, but prevents the codegen layer from
  being a strict "no-implicit-conversion" consumer of sema output.
- **Fix direction**: Extend `normalizeBinaryOperator` (or a dedicated sema pass) to emit
  `TypeConversionAnnotation` nodes for unscoped-enum-to-int and other non-arithmetic
  implicit conversions; then promote the `generateTypeConversion` fallback at line 3394 to
  an `InternalError`.

---

## 5) Dependent qualified member variable-template chains are not fully recovered

- **Symptom**: A variable-template initializer such as
  `Outer<T>::template member<U>` can still lower to an unresolved qualified
  identifier when the member is itself a variable template and the owner is a
  dependent class-template instantiation.
- **Root cause**: Replay reparses the initializer under the definition context,
  but the later expression substitution path does not always recover concrete
  explicit template arguments for qualified member variable-template calls.
- **Affected path**: `Parser::try_instantiate_variable_template` initializer
  replay followed by `ExpressionSubstitutor` handling of unresolved qualified
  member variable-template calls.
- **Impact**: The new replay infrastructure covers namespace variable templates
  and member-template function chains, but fully dependent member variable-template
  chains need a follow-up instantiation recovery pass.
