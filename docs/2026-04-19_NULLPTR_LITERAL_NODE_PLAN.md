# NullptrLiteralNode AST Cleanup Plan

**Date**: 2026-04-19  
**Status**: Proposed  
**Context**: Follow-up to the `nullptr` overload-resolution fix for pointer parameters

## Problem Statement

The parser currently represents the `nullptr` keyword as a `NumericLiteralNode`
with value zero and `TypeCategory::Nullptr`.

That is good enough to preserve the semantic type for overload resolution, but
it still mixes two different concepts in one AST node:

- integer and floating literals, which are numeric values
- `nullptr`, which is a prvalue of type `std::nullptr_t`

This shape makes downstream code depend on a convention: a numeric literal may
not really be numeric. Any visitor that handles `NumericLiteralNode` must either
know about the `TypeCategory::Nullptr` special case or accidentally treat
`nullptr` like an integer zero.

The immediate bug was visible in code like:

```cpp
int func_void_ptr(void* stream) {
	return 0;
}

int main() {
	return func_void_ptr(nullptr);
}
```

When `nullptr` was represented as an `int` literal, overload resolution rejected
the call to `void*`. The narrow fix is to preserve `TypeCategory::Nullptr` and
teach overload resolution about null pointer conversions. A dedicated
`NullptrLiteralNode` would make that distinction explicit throughout the AST.

## Why This Is Worth Doing

A real `NullptrLiteralNode` would improve the compiler in several ways:

- It makes the AST model closer to C++20: `nullptr` is not an integer literal.
- It prevents accidental integer-literal treatment in codegen, constexpr
  evaluation, template substitution, and overload helpers.
- It makes overload-sensitive behavior easier to audit, especially cases like
  `f(nullptr)` where `f(void*)` is viable but `f(bool)` is not.
- It gives semantic analysis a clean leaf expression to infer as
  `TypeCategory::Nullptr`, instead of relying on a numeric-literal convention.
- It creates a better place for future support of `decltype(nullptr)`,
  `std::nullptr_t`, type traits, direct-initialization of `bool`, and member
  pointer null conversions.
- It reduces the chance that future numeric-literal cleanup changes `nullptr`
  behavior by accident.

## Current Behavior To Preserve

The current targeted fix should remain valid after the AST cleanup:

- Parser produces an expression whose semantic type is `TypeCategory::Nullptr`.
- Parser-time overload resolution can match `nullptr` to object pointers,
  function pointers, member object pointers, and member function pointers.
- Ordinary overload conversion from `nullptr` to `bool` is not viable:

```cpp
void take_bool(bool);
take_bool(nullptr); // should fail
```

- The literal still lowers to a zero value where codegen needs a runtime value.

## Target Architecture

Represent `nullptr` as its own expression leaf:

```cpp
class NullptrLiteralNode {
public:
	explicit NullptrLiteralNode(Token token);

	const Token& token() const;
	TypeCategory type() const;
};
```

Then add it to the `ExpressionNode` variant.

The intended pipeline becomes:

```text
Parser
  -> NullptrLiteralNode

Parser get_expression_type / SemanticAnalysis inferExpressionType
  -> TypeCategory::Nullptr

OverloadResolution buildConversionPlan
  -> nullptr_t can convert to pointer and member pointer targets
  -> nullptr_t does not become an ordinary bool overload argument

IR / constexpr
  -> null value representation when a value is required
```

Semantic analysis should still be involved for final expression typing and
conversion annotation. The dedicated node should not move overload policy into
the parser; it should only make the parsed expression unambiguous.

## Implementation Plan

### Step 1: Add the AST node

- Add `NullptrLiteralNode` near the other literal nodes.
- Store the original token for diagnostics and source locations.
- Add it to the `ExpressionNode` variant.
- Update any AST helper that enumerates expression alternatives.

Expected files:

- `src/AstNodeTypes_DeclNodes.h` or `src/AstNodeTypes_Expr.h`
- `src/AstTraversal.h`
- `src/Parser_Core.cpp` if debug/type-name visitors need a new branch

### Step 2: Parse `nullptr` into the new node

- Change the `nullptr` keyword path in primary-expression parsing to create a
  `NullptrLiteralNode`.
- Do not represent it as `NumericLiteralNode(0, TypeCategory::Nullptr, ...)`.

Expected file:

- `src/Parser_Expr_PrimaryExpr.cpp`

### Step 3: Teach parser-side type queries

- Update `Parser::get_expression_type` to return `TypeCategory::Nullptr` for
  `NullptrLiteralNode`.
- Keep it as a prvalue with no pointer depth.

Expected file:

- `src/Parser_Expr_QualLookup.cpp`

### Step 4: Teach semantic analysis

- Update `SemanticAnalysis::inferExpressionType` for wrapped
  `NullptrLiteralNode`.
- Update any direct-node compatibility path only if direct expression leaves are
  still accepted there.
- Ensure `buildOverloadResolutionArgType` materializes the type as
  `TypeCategory::Nullptr`.

Expected file:

- `src/SemanticAnalysis.cpp`

### Step 5: Keep conversion policy in overload resolution

- Keep null pointer conversion in `buildConversionPlan(const TypeSpecifierNode&,
  const TypeSpecifierNode&)`.
- Do not add `TypeCategory::Nullptr` to the ordinary `TypeCategory::Bool`
  conversion path.
- Add or keep comments explaining that contextual/direct initialization of
  `bool` is a separate rule from ordinary overload viability.

Expected file:

- `src/OverloadResolution.h`

### Step 6: Update IR generation

- Add a `NullptrLiteralNode` lowering branch.
- Lower it to zero with the representation expected by pointer contexts.
- Audit direct `NumericLiteralNode` branches in call argument lowering,
  primitive expression lowering, member access helpers, and statement
  initialization.

Expected files to audit:

- `src/IrGenerator_Expr_Primitives.cpp`
- `src/IrGenerator_Call_Direct.cpp`
- `src/IrGenerator_Call_Indirect.cpp`
- `src/IrGenerator_Stmt_Decl.cpp`
- `src/IrGenerator_MemberAccess.cpp`
- `src/AstToIr.h`

### Step 7: Update constexpr evaluation

- Add evaluation support for `NullptrLiteralNode`.
- Return a null pointer/null value result that preserves
  `TypeCategory::Nullptr` where the evaluator carries type information.
- Audit places that currently special-case `NumericLiteralNode` as a constant
  expression.

Expected files to audit:

- `src/ConstExprEvaluator.h`
- `src/ConstExprEvaluator_Core.cpp`
- `src/ConstExprEvaluator_Members.cpp`

### Step 8: Update templates and substitution

- Add cloning/substitution support for `NullptrLiteralNode`.
- Audit variant visitors that copy, rebind, compare, fold, or materialize
  expression nodes.
- Ensure non-type template argument paths do not classify `nullptr` as an
  integer argument unless the standard permits the exact use case being handled.

Expected files to audit:

- `src/ExpressionSubstitutor.cpp`
- `src/ExpressionSubstitutor.h`
- `src/Parser_Templates_Substitution.cpp`
- `src/Parser_Templates_Inst_Substitution.cpp`
- `src/Parser_Templates_Inst_ClassTemplate.cpp`
- `src/RebindStaticMemberAst.h`

### Step 9: Update type traits and diagnostics

- Ensure `__is_nullptr` and scalar/fundamental traits still work.
- Add better diagnostics where invalid conversions mention `nullptr_t` instead
  of an integer type.

Expected files to audit:

- `src/TypeTraitEvaluator.cpp`
- `src/ConstExprEvaluator_Members.cpp`
- `src/CompileError.h` consumers that format expression types

### Step 10: Add regression coverage

Add focused tests before or alongside the implementation:

- `nullptr` to `void*`
- `nullptr` to `const void*`
- `nullptr` to typed object pointer
- `nullptr` to function pointer, if function pointer calls are supported
- `nullptr` to member object pointer, if member pointer support is complete
- `nullptr` to member function pointer, if member pointer support is complete
- overloaded `void*` vs typed pointer behavior
- negative `nullptr` to `bool` overload test
- `decltype(nullptr)` or type-trait coverage if those features are available
- constexpr/null comparisons such as `ptr == nullptr`

## Suggested Implementation Order

1. Add the node and parser support behind the existing behavior.
2. Update parser and sema type inference.
3. Update IR and constexpr visitors until the existing root suite builds.
4. Update template substitution and expression copying visitors.
5. Add the negative bool overload regression.
6. Add pointer/member-pointer coverage.
7. Run the full root test suite.
8. Re-check standard-header repros that originally exposed the issue.

## Risks

The change is not hard conceptually, but it has moderate blast radius because
`ExpressionNode` is a widely visited variant.

Likely failure modes:

- missed `std::visit` branch causing compile errors
- fallback paths treating `nullptr` as unsupported
- constexpr evaluator losing the type and returning plain integer zero
- template substitution dropping or misclassifying the node
- codegen assuming all literal runtime values are `NumericLiteralNode`

This should be treated as a cleanup pass with broad tests, not as a tiny bug
fix.

## Non-Goals

- Do not move all function overload resolution from parser to semantic analysis
  as part of this task.
- Do not redesign all literal nodes.
- Do not implement incomplete standard-library or UCRT support unless a new
  failure is directly caused by the `NullptrLiteralNode` change.
- Do not add fallback paths that silently reinterpret invalid uses of
  `nullptr`.

## Open Questions

- Should `NullptrLiteralNode` live beside other literal node definitions, or in
  the expression-node header once more literal leaves move there?
- Should direct-initialization of `bool` from `nullptr` be implemented in the
  same cleanup or kept as a separate conversion-rule task?
- Does the type system need an explicit builtin `std::nullptr_t` alias entry for
  `decltype(nullptr)` and standard header compatibility?
- Which member-pointer cases are already complete enough to test without
  introducing unrelated failures?

## Validation Commands

Use the normal project checks after implementation:

```powershell
.\build_flashcpp.bat
pwsh tests/run_all_tests.ps1 test_nullptr_void_ptr_arg_ret0.cpp
pwsh tests/run_all_tests.ps1 test_nullptr_bool_overload_fail.cpp
pwsh tests/run_all_tests.ps1
```

If standard-header support is being rechecked, compile the specific repro
directly after the root suite passes.
