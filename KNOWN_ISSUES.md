# Known Issues

This file documents confirmed root causes for failing tests, along with the affected source
locations and recommended fix directions.

---

## Issue 1 — Fold Expression over Variable Template Specializations (`test_complex_fold_vartempl_ret42.cpp`)

**Error:** `U __cmp_cat_id` — undefined external reference; binary returns 112 instead of 42.

**Root cause:** In `substituteCallExprWithExpressionSubstitutor`
(`src/Parser_Templates_Substitution.cpp:430`), `copyCallMetadataWithTransformedTemplateArguments`
pre-transforms template args via `substituteTemplateParameters`. A
`TemplateParameterReferenceNode("_Ts")` with type arg `{int}` is converted to
`ExpressionNode(IdentifierNode("int"))` (lines 469–474, via `get_type_name()` which maps
`TypeCategory::Int → "int"`). `ExpressionSubstitutor` then receives `IdentifierNode("int")` in the
"non-dependent template argument" branch (`src/ExpressionSubstitutor.cpp:646–678`), which only
handles `NumericLiteralNode` and `BoolLiteralNode`. Since `IdentifierNode("int")` is neither, it
sets `failed_value_extraction = true` and returns the original uninstantiated
`CallExprNode(__cmp_cat_id)` via `wrapOriginalCall()`, causing the undefined symbol.

**Key locations:**
- `src/Parser_Templates_Substitution.cpp:469–474` — converts type arg to `IdentifierNode`
- `src/ExpressionSubstitutor.cpp:646–678` — missing handler for `IdentifierNode` type args
- `src/ExpressionSubstitutor.cpp:812` — `try_instantiate_variable_template` (correct path, never reached)

**Fix directions (prefer Option A or C):**
- **Option A:** In `ExpressionSubstitutor.cpp:646–678`, add a case for `ExpressionNode(IdentifierNode(type_name))` — look up the type by name and synthesize a `TemplateTypeArg`.
- **Option B:** In `Parser_Templates_Substitution.cpp:469–474`, emit a `TypeSpecifierNode` instead of `IdentifierNode` for type args (already handled by ExpressionSubstitutor at lines 557–620).
- **Option C (least-risky):** Do not pre-transform template args in `copyCallMetadataWithTransformedTemplateArguments`; keep `TemplateParameterReferenceNode` intact so `param_map_` lookup works directly.

---

## Issue 2 — `if constexpr` on Constexpr Result of Fold Expression (`test_constexpr_fold_if_constexpr_ret42.cpp`)

**Error:** `[ERROR][Codegen] if constexpr condition is not a constant expression: Undefined variable in constant expression: result`

**Root cause (part 1):** Same fold-expression/variable-template substitution failure as Issue 1. `type_id<Ts>` is a variable template and each `type_id<T>` call fails the same substitution path, leaving `U type_id` undefined.

**Root cause (part 2):** Even after fixing part 1, `constexpr unsigned result = (type_id<Ts> | ...)` initialized from a fold expression result is not propagated to the constant expression evaluator used by `if constexpr`. The codegen (`src/IRConverter_ConvertMain.cpp`) does not find `result` as a compile-time constant when it was produced by a fold expression that underwent template substitution.

**Fix direction:** Fix Issue 1 first. Then verify that constexpr local variables initialized from fold-expression results are properly constant-folded and registered with the constant-expression evaluator before `if constexpr` processing.

---

## Issue 3 — Member Alias Template with Dependent Type Args (`test_member_alias_template_ret0.cpp`)

**Error:** `[ERROR][Parser] Non-type template arguments not supported in alias templates yet` at `src/Parser_TypeSpecifiers.cpp:1180`, col 27 in `result_type = cond_t<T, U>`.

**Root cause:** `parse_member_alias_template` (`src/Parser_Templates_Variable.cpp:104`) calls
`setCurrentTemplateParamNames(template_param_metadata.names)` which internally calls `setNames`,
**clearing the `kinds` vector**. This means `currentTemplateParamKind("T")` returns `std::nullopt`
for any template type parameter.

Consequently, in `classifySimpleTemplateArgName` (`src/Parser_Templates_Params.cpp:729–771`):
1. The `currentTemplateParamKind` check returns `std::nullopt` (no kind info).
2. `lookup_symbol_with_template_check("T")` finds `T` in `currentTemplateParamNames()` and returns a `TemplateParameterReferenceNode` — i.e., `has_value() == true`.
3. A non-null symbol lookup result is interpreted as **ValueLike** (line ~756), so `T` and `U` are classified as non-type template arguments.
4. The fast path at lines 806–839 creates `TemplateTypeArg::makeDependentValue(...)` with `is_value=true` for both `T` and `U`.
5. When `tryRebindAliasTargetTemplateArg` is called at `Parser_TypeSpecifiers.cpp:1176`, the resulting arg has `is_value=true`, triggering the error at line 1179.

**Key locations:**
- `src/Parser_Templates_Variable.cpp:104` — calls `setCurrentTemplateParamNames` (clears kinds)
- `src/Parser_Templates_Params.cpp:729–771` — `classifySimpleTemplateArgName` misclassifies type params
- `src/Parser.h:666` — `ActiveTemplateParameterState::setNames` clears `kinds`
- `src/Parser_TypeSpecifiers.cpp:1179–1181` — error trigger

**Fix:** In `src/Parser_Templates_Variable.cpp:104`, replace:
```cpp
setCurrentTemplateParamNames(template_param_metadata.names);
```
with:
```cpp
setCurrentTemplateParameters(template_param_metadata.names, template_param_metadata.kinds);
```
This preserves type-vs-nontype kind info so `currentTemplateParamKind("T")` returns
`TemplateParameterKind::Type`, and `classifySimpleTemplateArgName` correctly returns `TypeLike`.

---

## Issue 4 — Partial Specialization with Brace-Initialized Data Member (`test_member_function_overload_in_ctor_init_ret0.cpp`)

**Error:** `error: member 'head' not found in struct 'TupleImpl$da273b60f90b6312'`; available members: (empty).

**Root cause:** When parsing the partial specialization `TupleImpl<I, Head, Tail...>` body in
`Parser_Templates_Class.cpp` (the `is_partial_specialization` branch, ~line 3763), the data member
`Head head{}` is handled as follows:

1. `parse_type_and_name()` succeeds, returning `DeclarationNode(Head, head)`.
2. `peek() == "{"_tok` triggers the brace-init path at line 3763.
3. `parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal)` is called with `peek() == "{"_tok`.
4. Inside `parse_primary_expression`, `advance()` consumes `{` into `current_token_`. Then the brace-depth loop consumes `}` into `current_token_` (leaving `peek() = ";"`).
5. `consume("}"_tok)` at `src/Parser_Expr_PrimaryExpr.cpp:7066` checks `peek() == "}"_tok` — but `peek()` is `";"`, so it **fails** and returns `ParseResult::error("Expected '}' to close braced initializer")`.
6. This error propagates to line 3767 and then out of the entire `parse_template_declaration` function, **before** `gTemplateRegistry.registerSpecializationPattern(...)` at line 3966 is reached.
7. The partial spec is never registered. `matchSpecializationPattern("TupleImpl", {0u, int, int})` finds no pattern and instantiates from the primary template (forward declaration with no body), producing a struct with zero members.

**Key locations:**
- `src/Parser_Expr_PrimaryExpr.cpp:7048–7075` — brace-init handling in non-function context; `consume("}"_tok)` fails because the while-loop already consumed `}` into `current_token_`
- `src/Parser_Templates_Class.cpp:3763–3768` — data member brace-init branch returns error
- `src/Parser_Templates_Class.cpp:3966` — specialization registration (never reached on error)

**Fix:** In `src/Parser_Expr_PrimaryExpr.cpp:7066`, replace:
```cpp
if (!consume("}"_tok)) {
    return ParseResult::error("Expected '}' to close braced initializer", current_token_);
}
```
with:
```cpp
if (current_token_.value() != "}") {
    return ParseResult::error("Expected '}' to close braced initializer", current_token_);
}
// current_token_ IS '}' — it was already processed by the depth loop above
```
This correctly validates that `}` was consumed by the loop without trying to re-consume it via `peek()`.

---

## Issue 5 — NTTP-Based Inherited Static Constexpr Member (`test_nttp_base_class_substitution_ret0.cpp`)

**Error:** `[ERROR][Codegen] Missing variable name: 'value', not in local or global scope` at `src/IRConverter_ConvertMain.cpp:936`; binary exits 112.

**Pattern:**
```cpp
template <typename T, T v>
struct integral_constant { static constexpr T value = v; };
template <unsigned long long N>
struct extent_helper : integral_constant<unsigned long long, N> {};
// access: extent_helper<42>::value
```

**Root cause:** The static constexpr member `value` from `integral_constant<unsigned long long, 42>`
is registered in the global symbol table under a mangled/hash-qualified name (something like
`integral_constant$<hash>::value`). The lookup for `extent_helper<42>::value` in
`IRConverter_ConvertMain.cpp:936` uses suffix matching (looking for a global ending with `::value`
or `value`) but the registered name doesn't match the expected suffix pattern given the base class
hash. The NTTP substitution (`v = 42`, `T = unsigned long long`) produces a concrete base class,
but the IR-level global lookup does not follow the inheritance chain to find `value` from the base.

**Fix direction:** In `IRConverter_ConvertMain.cpp` near line 936, extend the global name lookup to
follow the inheritance chain of the instantiated struct: when `extent_helper<42>::value` is not
found directly, look up `value` in each concrete base class of `extent_helper<42>` (i.e., also try
`integral_constant$<hash>::value`). Alternatively, during instantiation of `extent_helper<42>`,
register an alias mapping `extent_helper$<hash>::value → integral_constant$<hash>::value`.

---

## Issue 6 — Implicit Integer Widening in Partial Specialization Static Member Init (`test_partial_spec_static_member_visibility_ret0.cpp`)

**Error:** `IR conversion failed for node 'main': Phase 15: sema missed variable init conversion (int -> unsigned long long)` at `src/IrGenerator_Stmt_Decl.cpp:2029`.

**Pattern:**
```cpp
static constexpr unsigned long long __d2 = 10 / __g;
// 10 is int literal, __g is unsigned long long
```

**Root cause:** In a partial specialization body, the expression `10 / __g` (where `10` is an `int`
literal and `__g` is `unsigned long long`) results in a division node whose inferred type is `int`
(since the literal type dominates before usual arithmetic conversions are applied). The declaration
type of `__d2` is `unsigned long long`. Sema does not annotate an implicit `int → unsigned long long`
widening conversion on the initializer expression in this context. Phase 15 of the IR lowering
(`IrGenerator_Stmt_Decl.cpp:2029`) detects the type mismatch and aborts.

**Fix direction:** In sema / type-checking for static constexpr member declarations in partial
specialization bodies, apply usual arithmetic conversions to binary expressions involving mixed
signed/unsigned types, and insert an implicit conversion node when the initializer type differs from
the declared type. This likely requires a fix in the expression type inference for division
(`/`) involving non-type template parameters of `unsigned long long` type.

---

## Unity Debug Build Broken (separate issue)

The `x64/Debug/FlashCpp` unity build does not compile:
- **g++:** shadowed member errors in `src/AstNodeTypes_Stmt.h`
- **clang++:** duplicate definition of `applyDeclarationArrayBoundsToTypeSpec`

Use `x64/Sharded/FlashCpp` (4-file unity) as the working binary for all testing.
