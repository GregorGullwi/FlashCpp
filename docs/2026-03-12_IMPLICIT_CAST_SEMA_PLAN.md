# Implicit Cast / Semantic Analysis Plan

**Date**: 2026-03-12
**Status**: Proposed
**Related**: `docs/KNOWN_ISSUES.md`, `tests/template_parsing_test_ret0.cpp`

## Short answer

Yes: this should be a separate semantic-analysis pass, not parser-inline logic.

The parser should stay focused on syntax and source-faithful AST construction.
Implicit conversions are semantic decisions that depend on resolved types,
overload selection, value category, reference binding, and context
(`if` condition vs return statement vs function argument). Putting that logic in
the parser would make parsing context-sensitive in the wrong direction and would
either duplicate later type logic or force premature type resolution.

## Why a separate pass is the right shape

Today FlashCpp applies implicit conversions ad-hoc in codegen (`generateBinaryOperatorIr`,
return lowering, direct function-call argument lowering). That fixes individual
sites but leaves the compiler with no single place that answers:

- what type an expression has after contextual conversion
- whether a conversion is standard vs user-defined
- whether an lvalue must bind directly or first materialize a temporary
- whether overload resolution should see the original expression or a converted one

A dedicated semantic pass gives one canonical normalization point before IR
generation, so codegen can lower what the program *means* instead of repeating
type-law decisions per site.

## Recommended architecture

### 1. Add explicit semantic AST nodes

Introduce at least:

- `ImplicitCastNode`
	- wraps one `ExpressionNode`
	- stores source type, target type
	- stores cast kind / conversion category
	- preserves value category if relevant
- optionally later:
	- `MaterializeTemporaryNode`
	- `BoundTemporaryNode`
	- `ImplicitObjectAdjustmentNode` for base/derived `this` or member access adjustments

Suggested cast kinds for the first slice:

- lvalue-to-rvalue
- array-to-pointer decay
- function-to-pointer decay
- integral promotion
- floating-point promotion
- integral conversion
- floating-integral conversion
- boolean conversion
- qualification adjustment
- derived-to-base pointer/reference adjustment
- user-defined conversion

### 2. Add a post-parse, pre-codegen `SemanticAnalysis` pass

Pipeline shape:

1. Parse → syntax AST
2. Name/type resolution already performed today during parsing / deferred lookups
3. **SemanticAnalysis pass** walks expressions/statements and inserts semantic wrapper nodes
4. AstToIr lowers the normalized AST

The pass should mutate/replace expression children in place or rebuild the
subtree with wrapped nodes. The important part is that *all* contexts use the
same conversion insertion logic.

## What should live in this pass

This pass can do more than implicit casts. Once it exists, it becomes the right
home for other semantic normalizations that codegen should not own:

- contextual conversion to `bool` for `if`, `while`, `for`, `do`, `?:`, `!`, `&&`, `||`
- usual arithmetic conversions for binary arithmetic/comparisons
- function call argument conversions after overload resolution chooses a callee
- return-statement conversion to function return type
- initialization conversion for locals, globals, members, and temporaries
- reference binding checks and temporary materialization
- array/function decay where required
- derived-to-base adjustments for object arguments and return paths
- enum promotion / underlying-type normalization where the standard requires it
- generic lambda parameter normalization after deduction / instantiation so the
  lambda body sees concrete parameter types instead of late codegen-time
  fallbacks or synthetic declarations
- diagnostics for narrowing or disallowed implicit conversions
- a single place to classify value category (`lvalue` / `xvalue` / `prvalue`) after wrapping

Longer-term, the same framework could also host:

- constant-expression marking / early constexpr folding hooks
- unreachable semantic diagnostics after overload resolution
- standard conversion ranking data reused by overload resolution explanations

## What should stay out of the parser

Do **not** insert implicit cast nodes while parsing tokens.

Reasons:

- parsing should not need complete type information for every subexpression
- overload resolution often decides which conversion is needed only after the full call shape is known
- parser-inline conversions would entangle syntax recovery with semantic failure
- the same expression may appear in different semantic contexts later (initializer, condition, return, call argument)

The parser can still attach enough source/token info for a later semantic pass,
but it should not decide implicit conversion semantics itself.

## Minimal implementation plan

### Phase A: infrastructure

1. Add `ImplicitCastNode` to the expression AST variants
2. Add helpers:
	- `wrapImplicitCast(expr, target_type, cast_kind)`
	- `applyStandardConversion(expr, target_type, context)`
	- `applyContextualBoolConversion(expr, context)`
3. Teach AST printers/debug dumps to display implicit cast nodes
4. Teach `visitExpressionNode` to lower `ImplicitCastNode`

### Phase B: first semantic pass slice

Implement a small `SemanticAnalysis` walker for only these contexts:

- direct function-call arguments
- return statements
- binary arithmetic/comparison operators

That immediately replaces the current ad-hoc conversion insertion at the sites
most likely to produce ABI-visible wrong code.

### Phase C: initializer/reference slice

Extend the pass to:

- local/global/member initialization
- reference binding
- temporary materialization
- conditional expressions
- generic lambda body normalization:
	- replace instantiated `auto` parameter declarations with their deduced types
	- preserve cv/ref qualifiers and `TypeIndex` for struct/enum cases
	- ensure all identifier/body lookups see the normalized declaration, not the
	  original unresolved syntax-only parameter node

### Phase D: remove codegen-local semantic rules

As coverage grows, delete the ad-hoc conversion calls from codegen:

- `generateBinaryOperatorIr`
- return lowering
- direct function-call argument lowering
- any initializer-specific fallback conversion paths

The end state is:

- semantic pass inserts conversions
- codegen trusts the AST
- `generateTypeConversion` becomes a lowering helper for `ImplicitCastNode`, not a
  policy decision point

## Practical migration strategy for FlashCpp

To keep risk low, the pass can be introduced incrementally behind a small entry point:

```cpp
runSemanticAnalysis(*translation_unit, context);
```

called once after parsing succeeds and before `AstToIr` starts.

That gives a clean seam without rewriting the parser. Early slices can process
only a subset of expression kinds and leave the rest untouched.

For generic lambdas specifically, the pass should run after call-site deduction
metadata is available for an instantiation but before AstToIr lowers the chosen
instantiated body. The pass can then normalize the parameter declarations
themselves instead of forcing codegen to synthesize replacement declarations in
the function symbol table.

## Why not “just do it inline in AstToIr”?

That is better than parser-inline logic, but still inferior to a separate pass.

An inline AstToIr transform would:

- keep semantic policy mixed with lowering
- make it harder to debug/print the normalized tree
- leave later consumers unable to reuse semantic information
- continue the pattern of fixing one codegen context at a time

If a full pass feels too large for the first patch, a reasonable stepping stone is:

1. add `ImplicitCastNode`
2. add a tiny semantic walker that only rewrites call arguments / returns / binary ops
3. lower that node in AstToIr
4. add a tiny generic-lambda normalization hook that rewrites instantiated
   parameter declarations from deduced metadata before body lowering

That still preserves the right architecture and avoids parser entanglement.

## Immediate recommendation

Implement a separate `SemanticAnalysis` pass.

Start with `ImplicitCastNode` plus three contexts:

- call arguments
- returns
- usual arithmetic conversions

Then migrate initializers, reference binding, and contextual-bool conversions.

This gives FlashCpp one canonical place for standard conversions and prevents
future regressions like `template_parsing_test_ret0.cpp` from depending on
which codegen path happened to remember to call `generateTypeConversion(...)`.
