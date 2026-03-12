# ExpressionNode Wrapping Normalization Plan

**Date**: 2026-03-12  
**Status**: Proposed  
**Context**: Parser / AST cleanup plan for expression-valued parse results

## Problem Statement

The parser currently returns some expression-valued results as `ExpressionNode`
and others as direct node types stored in `ASTNode`. That inconsistency leaks
into downstream code, which then has to support both shapes.

Concrete examples already visible in the tree:

- qualified identifiers can be returned directly as `QualifiedIdentifierNode`
  (`src\Parser_Expr_QualLookup.cpp:35-38`)
- some primary-expression paths also return a direct
  `QualifiedIdentifierNode` (`src\Parser_Expr_PrimaryExpr.cpp:199-202`)
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
   - direct `QualifiedIdentifierNode` return in
     `src\Parser_Expr_QualLookup.cpp:35-38`
   - direct `QualifiedIdentifierNode` return in
     `src\Parser_Expr_QualLookup.cpp:197-200`
   - direct qualified return in operator-name path
     `src\Parser_Expr_PrimaryExpr.cpp:199-202`

2. **Primary-expression leaf paths**
   - identifier paths already often wrap in `ExpressionNode`
   - qualified identifier paths appear mixed and need full audit

3. **Special expression constructors**
   - constructor-call paths already wrap and should remain the model
   - lambda path already wraps and should remain the model

4. **Consumers depending on mixed shapes**
   - constexpr helpers
   - codegen expression readers
   - AST utility helpers
   - substitution / template helpers

## Recommended Implementation Order

### Step 1: Build a parser return-shape audit

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

### Step 2: Normalize the lowest-risk leaf sites first

Start with direct returns of:

- `QualifiedIdentifierNode`
- direct identifier-like leaves returned from expression parser paths

These are the safest because they change AST shape without changing parse
precedence or ownership logic.

### Step 3: Add a tiny parser-side helper if repetition appears

If the parser has many sites doing:

- `emplace_node<ExpressionNode>(SomeExpressionNode(...))`

then add a tiny local helper such as “wrap expression result” to reduce churn
and keep callsites consistent.

This helper should stay parser-local unless another layer genuinely needs it.

### Step 4: Update downstream consumers to prefer the normalized shape

After the parser side is normalized, re-audit mixed-shape consumers and remove
only the compatibility branches that are no longer needed.

Likely candidates:

- constexpr extraction helpers
- generic AST-name helpers
- codegen expression readers that still special-case direct leaf storage

Do this cautiously and only after parser behavior is validated.

### Step 5: Keep compatibility only where the AST legitimately allows both

Some APIs may still need to accept both forms temporarily or permanently if
they consume older nodes, synthetic nodes, or non-parser-produced AST values.

Do not force a cleanup that breaks valid non-parser construction paths.

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

The next best step is:

1. audit all expression-valued `ParseResult` return sites
2. normalize direct `QualifiedIdentifierNode` expression returns first
3. validate downstream constexpr/codegen consumers
4. only then widen the cleanup to the remaining expression leaves

This keeps the cleanup incremental and makes it much easier to tell whether a
bug comes from parser shape normalization or from unrelated expression logic.
