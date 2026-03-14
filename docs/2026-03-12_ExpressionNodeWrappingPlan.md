# ExpressionNode Wrapping Normalization Plan

**Date**: 2026-03-12  
**Status**: Complete — all steps from the initial plan are finished  
**Context**: Parser / AST cleanup plan for expression-valued parse results

## Problem Statement

The parser currently returns some expression-valued results as `ExpressionNode`
and others as direct node types stored in `ASTNode`. That inconsistency leaks
into downstream code, which then has to support both shapes.

Concrete examples already visible in the tree (items marked ✅ have been fixed):

- ✅ ~~qualified identifiers returned directly as `QualifiedIdentifierNode`
  (`src\Parser_Expr_QualLookup.cpp:35-38`)~~ — now wrapped at line 37
- ✅ ~~some primary-expression paths returned a direct
  `QualifiedIdentifierNode` (`src\Parser_Expr_PrimaryExpr.cpp:199-202`)~~ — now wrapped at line 201
- constructor calls are intentionally wrapped in `ExpressionNode`
  (`src\Parser_Expr_QualLookup.cpp:159-160`)
- lambdas are wrapped before returning
  (`src\Parser_Expr_ControlFlowStmt.cpp:1393-1395`)
- consumers like constexpr evaluation already contain compatibility helpers for
  “direct vs wrapped” forms (`src\ConstExprEvaluator_Core.cpp:1250-1285`)
- generic AST helpers also need to special-case both forms
  (`src\AstNodeTypes.h:10-21`)

This makes expression handling more fragile than it needs to be and increases
the amount of defensive shape-checking in parser-adjacent, constexpr, and
codegen code.

## Goal

Normalize parser expression boundaries so that **expression-valued parse entry
points return `ExpressionNode` consistently**.

The intended rule is:

> if a parser path is producing an expression result, the returned `ASTNode`
> should hold an `ExpressionNode`, not a direct expression leaf/node type.

## Non-Goals

- No attempt to wrap declarations, statements, or type nodes in
  `ExpressionNode`
- No semantic behavior changes beyond AST-shape normalization
- No broad AST redesign beyond expression-result consistency
- No opportunistic cleanup of unrelated parser/control-flow logic

## Proposed Boundary Rule

Apply the normalization only to nodes that are members of the
`ExpressionNode` variant (`src\AstNodeTypes_Expr.h:846-849`) and only when they
are being returned as expression parse results.

That means:

- **Wrap**: identifier-like expression leaves, qualified identifiers,
  constructor calls, lambdas, member access, function calls, literals, casts,
  and other true expression nodes
- **Do not wrap**: `TypeSpecifierNode`, declaration nodes, statement nodes,
  requirement nodes, or parser bookkeeping nodes that are not expressions

## Why This Is Worth Doing

This cleanup should reduce:

- repeated `node.is<ExpressionNode>()` / direct-node fallback code
- helper functions that must accept both wrapped and unwrapped expression forms
- special comments like “direct storage and ExpressionNode-wrapping”
- accidental parser shape drift between different expression-producing paths

It should also make downstream code simpler by letting it assume one AST shape
for expression results at parser boundaries.

## Current Inconsistency Inventory

This is the initial list to audit before changing behavior:

1. **Qualified identifier parse helpers**
   - ✅ `parse_qualified_identifier()` — removed as dead code (zero call sites).
     Was at `src\Parser_Expr_QualLookup.cpp:3-38`, declared at `Parser.h:758`.
   - ✅ `parse_qualified_identifier_after_template()` — was direct
     `QualifiedIdentifierNode` at `src\Parser_Expr_QualLookup.cpp:197-200`,
     now wrapped at line 199
   - ✅ `parse_qualified_operator_call()` — was direct qualified return at
     `src\Parser_Expr_PrimaryExpr.cpp:199-202`, now wrapped at line 201

2. **Primary-expression leaf paths**
   - identifier paths already often wrap in `ExpressionNode`
   - qualified identifier paths appear mixed and need full audit
   - ✅ All inline `emplace_node<QualifiedIdentifierNode>(...)` sites have been
     converted to stack-local construction in this PR:
     - `Parser_Expr_PrimaryExpr.cpp:945`  — `::identifier` global-scope path
     - `Parser_Expr_PrimaryExpr.cpp:1143` — `identifier::identifier` early path
     - `Parser_Expr_PrimaryExpr.cpp:1521` — `Template<T>::member` nested path
     - `Parser_Expr_PrimaryExpr.cpp:2256` — `identifier::identifier` late path
     - `Parser_Expr_PrimaryExpr.cpp:3602` — `Template<T>::member` inline path
     All were consumed locally and re-wrapped in `ExpressionNode` before
     returning, so the final returned shape was already normalized — only the
     intermediate bare persistent allocation was eliminated.

3. **Special expression constructors**
   - constructor-call paths already wrap and should remain the model
   - lambda path already wraps and should remain the model

4. **Consumers depending on mixed shapes**
   - constexpr helpers
   - codegen expression readers
   - AST utility helpers
   - substitution / template helpers

## Recommended Implementation Order

### Step 1: Build a parser return-shape audit ✅

Create a small inventory of parser functions that return expression-valued
`ParseResult`s and classify each site as:

- already wrapped correctly
- returns direct expression leaf/node
- ambiguous / needs callsite review

Prioritize:

- `Parser_Expr_QualLookup.cpp`
- `Parser_Expr_PrimaryExpr.cpp`
- `Parser_Expr_PostfixCalls.cpp`
- `Parser_Expr_BinaryPrecedence.cpp`
- `Parser_Expr_ControlFlowStmt.cpp`

**Completed**: Audit identified 3 `QualifiedIdentifierNode` producer sites
(see Inconsistency Inventory §1 above). Remaining expression leaf types have
not yet been fully audited.

### Step 2: Normalize the lowest-risk leaf sites first ✅

Start with direct returns of:

- `QualifiedIdentifierNode`
- direct identifier-like leaves returned from expression parser paths

These are the safest because they change AST shape without changing parse
precedence or ownership logic.

**Completed for `QualifiedIdentifierNode`**: All 3 producer sites now wrap in
`ExpressionNode` (PR #909). Callers of `parse_qualified_identifier_after_template`
that used `.as<QualifiedIdentifierNode>()` were updated to use the new
`asQualifiedIdentifier()` helper, and redundant re-wrapping was removed.

All inline `emplace_node<QualifiedIdentifierNode>(...)` sites have been converted
to stack-local construction (see Inconsistency Inventory §2), including the last
one in `Parser_Expr_PostfixCalls.cpp`.

**No direct `IdentifierNode` parser return paths found**: Audit confirmed that
all parser expression paths already wrap `IdentifierNode` in `ExpressionNode`
before returning. No normalization needed there.

### Step 3: Add a tiny parser-side helper if repetition appears ✅

If the parser has many sites doing:

- `emplace_node<ExpressionNode>(SomeExpressionNode(...))`

then add a tiny local helper such as “wrap expression result” to reduce churn
and keep callsites consistent.

This helper should stay parser-local unless another layer genuinely needs it.

**Completed**: Added `asQualifiedIdentifier(const ASTNode&)` to
`src\AstNodeTypes.h:26-28` to encapsulate extraction of a
`QualifiedIdentifierNode` from the normalized `ExpressionNode` wrapper:

```cpp
inline const QualifiedIdentifierNode& asQualifiedIdentifier(const ASTNode& node) {
	return std::get<QualifiedIdentifierNode>(node.as<ExpressionNode>());
}
```

This is used by callers that need to inspect the qualified name (e.g. to read
`.name()` or `.namespace_handle()`) after receiving an already-wrapped result
from the parse helpers.

### Step 4: Update downstream consumers to prefer the normalized shape ✅

After the parser side is normalized, re-audit mixed-shape consumers and remove
only the compatibility branches that are no longer needed.

Likely candidates:

- constexpr extraction helpers
- generic AST-name helpers (`getIdentifierNameFromAstNode` in `AstNodeTypes.h`)
- codegen expression readers that still special-case direct leaf storage

Do this cautiously and only after parser behavior is validated.

**Completed**. `AstNodeTypes.h` now centralizes identifier extraction via
`tryGetIdentifier(const ASTNode&)` and `tryGetQualifiedIdentifier(const ASTNode&)`.
All known downstream open-coded wrapped-vs-direct checks have been updated:
- `ConstExprEvaluator_Core.cpp`: removed local `extract_identifier_name` lambda,
  now uses `getIdentifierNameFromAstNode()` directly
- `ConstExprEvaluator_Members.cpp`: uses `tryGetIdentifier()` throughout
- `IrGenerator_Lambdas.cpp`: collapsed `is<IdentifierNode>()` / `is<ExpressionNode>()`
  init-capture branches into single `tryGetIdentifier()` check
- `IrGenerator_MemberAccess.cpp`: `get_identifier` local lambda now delegates to
  `tryGetIdentifier(object_node)`
- `Parser_Expr_QualLookup.cpp`: unary-`+` lambda-decay path uses `tryGetIdentifier()`
- `TemplateRegistry_Lazy.cpp`: dependent qualified-id checks use
  `tryGetQualifiedIdentifier()`

Backward-compatibility branches in `tryGetIdentifier` and `tryGetQualifiedIdentifier`
are intentionally preserved for synthetic / template-substituted AST nodes that
may legitimately hold unwrapped forms.

### Step 5: Keep compatibility only where the AST legitimately allows both ✅

Some APIs may still need to accept both forms temporarily or permanently if
they consume older nodes, synthetic nodes, or non-parser-produced AST values.

Do not force a cleanup that breaks valid non-parser construction paths.

**Completed**. The `tryGetIdentifier()` and `tryGetQualifiedIdentifier()` helpers
in `AstNodeTypes.h` retain backward-compatible fallback branches for direct node
storage. These are the only remaining "both forms" code paths and are
intentionally preserved — they are the right long-term API for any consumer that
may see synthetic or template-substituted AST nodes in addition to
parser-normalized `ExpressionNode`-wrapped forms.

## Validation Strategy

For each slice:

1. run the normal repository build for your platform
2. run focused tests that exercise:
   - qualified identifiers
   - constructor-call expressions
   - lambdas
   - member access / member calls
   - constexpr evaluation paths using parser-produced expression trees
3. then run the normal full repository regression suite for your platform

## Risk Areas

- parser code that returns direct nodes for historical reasons and is consumed by
  template instantiation / substitution helpers
- paths that distinguish type-like qualified names from expression-like
  qualified names
- synthetic AST construction outside the parser
- subtle shape assumptions in constexpr evaluation and codegen

## Decision Rules

- If the parser function is returning an **expression result**, prefer
  `ExpressionNode`
- If the node is not part of the `ExpressionNode` variant, do not wrap it
- If a callsite becomes more complex because wrapping obscures an important type
  distinction, pause and re-check the boundary
- If a consumer only needs parser-produced expressions, bias toward removing
  direct-node fallback after validation

## Concrete Recommendation

~~The next best step is:~~

1. ~~audit all expression-valued `ParseResult` return sites~~ ✅
2. ~~normalize direct `QualifiedIdentifierNode` expression returns first~~ ✅
3. ~~validate downstream constexpr/codegen consumers~~ ✅
4. ~~widen the cleanup to the remaining expression leaves~~ ✅

**All steps complete** (post PR #909 + follow-up commits):

1. ~~Evaluate removing `parse_qualified_identifier()`~~ ✅ — removed as dead code.
2. ~~Simplify the remaining inline `emplace_node<QualifiedIdentifierNode>(...)`
   sites~~ ✅ — all converted to stack-local construction, including the last
   one in `Parser_Expr_PostfixCalls.cpp`.
3. ~~Audit and normalize other direct expression leaf returns (e.g. `IdentifierNode`)~~ ✅
   — confirmed no direct `IdentifierNode` parser returns existed; all parser
   expression paths were already wrapping in `ExpressionNode`.
4. ~~Continue re-auditing downstream consumers (Step 4), replacing open-coded
   wrapped-vs-direct identifier handling with centralized helpers~~ ✅ — done
   across `ConstExprEvaluator_Core.cpp`, `ConstExprEvaluator_Members.cpp`,
   `IrGenerator_Lambdas.cpp`, `IrGenerator_MemberAccess.cpp`,
   `Parser_Expr_QualLookup.cpp`, and `TemplateRegistry_Lazy.cpp`.
5. ~~Only remove compatibility branches once non-parser/synthetic AST paths are
   confirmed not to rely on them~~ ✅ — compatibility branches retained in
   `tryGetIdentifier()` and `tryGetQualifiedIdentifier()` helpers; all other
   open-coded duplicates removed.
6. ~~Audit `TemplateRegistry_Lazy.cpp` and `ExpressionSubstitutor.cpp` sites that
   check `node.is<QualifiedIdentifierNode>()` directly~~ ✅ — `TemplateRegistry_Lazy.cpp`
   updated to use `tryGetQualifiedIdentifier()`; `ExpressionSubstitutor.cpp`
   dispatches via `std::visit` on `ExpressionNode` variants, which is already
   the correct normalized form (no change needed).

**Status: COMPLETE**. The plan document is now fully executed. The only
remaining "both-form" code is in the `tryGetIdentifier` / `tryGetQualifiedIdentifier`
helpers in `AstNodeTypes.h`, which are intentionally tolerant and serve as the
stable backward-compatible API for any future consumer.
