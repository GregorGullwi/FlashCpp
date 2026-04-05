# Call node consolidation plan

## What the investigation found

- `FunctionCallNode` and `MemberFunctionCallNode` duplicate the same core payload: callee information, argument storage, and source token tracking (`src/AstNodeTypes_DeclNodes.h`, `src/AstNodeTypes_Expr.h`).
- The parser has repeated call-building paths for free calls, member calls, static-member fallback calls, qualified calls, and template-call variants. The `.` and `->` postfix handling alone duplicates most of the member-call flow (`src/Parser_Expr_PostfixCalls.cpp`).
- Template rebinding and substitution already have to translate between call forms or manually copy call metadata, which is a sign that the current split is fighting later passes (`src/Parser_Templates_Inst_ClassTemplate.cpp`, `src/Parser_Templates_Substitution.cpp`, `src/ExpressionSubstitutor.cpp`).
- Semantic analysis keeps separate call-specific side tables and near-duplicate argument-conversion paths for free and member calls (`src/SemanticAnalysis.cpp`).
- IR generation also has separate direct/member paths and explicit fallback conversions from member calls back into free-function calls (`src/IrGenerator_Call_Direct.cpp`, `src/IrGenerator_Call_Indirect.cpp`).

## Recommended direction

Introduce one shared call-expression node for ordinary function-like calls and keep only genuinely different nodes separate.

### Shared node scope

The unified call node should cover:

- free-function calls
- static member function calls
- non-static member function calls
- operator calls that are currently represented as either free or member calls (including operator[] and operator())
- explicit-template calls
- indirect callable-object calls once the callee shape can describe them cleanly

`ConstructorCallNode` and `PseudoDestructorCallNode` should stay separate unless a later cleanup shows they can share infrastructure without making parsing or codegen less clear.

### Shared node shape

Use one node with:

- a common callee descriptor
- shared argument storage
- shared source token
- shared optional metadata currently living only on `FunctionCallNode` (qualified name, mangled name, explicit template arguments, indirect-call marker)
- an optional receiver/object expression for member-style calls
- explicit call-kind/category metadata only where it changes semantics

The callee descriptor should be rich enough to distinguish:

- resolved free function declaration
- resolved member function declaration
- unresolved placeholder declaration
- calls through a named callable object, function pointer, or member function pointer

## Migration plan

- [x] Add a new shared call node and callee descriptor without removing existing nodes yet.
- [x] Add compatibility helpers so parser, substitution, semantic analysis, constexpr evaluation, and IR generation can read call information through one common interface.
- [x] Convert metadata copying utilities to operate on the shared call abstraction instead of only `FunctionCallNode`.
- [x] Refactor postfix/member parsing so `.` and `->` share one call/member-access builder.
- [x] Change free-call and member-call parser paths to emit the shared node in parallel with existing handling or behind a narrow compatibility layer.
- [x] Merge semantic-analysis lookup, overload-recovery, argument-conversion, and reference-binding logic onto the shared call abstraction.
- [x] Merge the remaining constexpr evaluation, substitution, and direct IR entry points onto direct `CallExprNode` handling so the old `materializeLegacy*` adapters are no longer needed there.
- [x] Merge the deeper shared IR lowering pipeline so the remaining member-call lowering, virtual dispatch, and member-to-free fallback paths stop depending on `MemberFunctionCallNode` / `FunctionCallNode` internals.
- [x] Remove temporary call-form conversions such as member-to-function fallback nodes in constexpr evaluation, substitution, and the top-level direct/member IR entry points once those downstream sites read the unified representation directly.
- [x] Delete the legacy duplicate call nodes after all call sites and visitors stop depending on them.

### Current status

- Parser normalization is now complete for the ordinary free-call and member-call paths; those parser sites emit `CallExprNode` and the rebased Windows suite is green after the downstream compatibility fixes.
- Template substitution/rebinding now keeps the migrated parser output in unified form through the static-member rebinder and the receiverless substitution path without re-materializing legacy free-call nodes.
- Constexpr evaluation now consumes `CallExprNode` directly for free/member dispatch, bound-call handling, noexcept lookup, and static-member cycle detection.
- Semantic analysis now routes callable-operator lookup, overload recovery, argument conversion, and reference-binding annotation through shared `CallInfo`-based helpers; the legacy per-node entry points remain only as thin wrappers.
- IR lowering unification is now complete: the deeper member-call lowering, virtual dispatch, and member-to-free fallback paths have been flipped so the 3-arg `generateMemberFunctionCallIr` and `convertMemberCallToFunctionCall` both take `const CallExprNode&` as the primary type, exactly matching the pattern already done for `generateFunctionCallIr`. The 2-arg `MemberFunctionCallNode` entry is now a thin forwarding wrapper. The `materializeLegacyFunctionCall` and `materializeLegacyMemberFunctionCall` helpers have been removed from `CallNodeHelpers.h` as there are no remaining callers.
- The three synthetic `MemberFunctionCallNode` constructions in range-for lowering (`IrGenerator_Stmt_Control.cpp`) have been replaced with `makeResolvedMemberCallExpr` calls.
- **All construction sites** that previously emitted `FunctionCallNode` or `MemberFunctionCallNode` have been converted to produce `CallExprNode`: `ExpressionSubstitutor.cpp`, `Parser_Templates_Inst_ClassTemplate.cpp` (rebind-static), `Parser_Templates_Substitution.cpp` (template-param substitution + pack-identifier replacement), and `Parser_Expr_PrimaryExpr.cpp` (deferred concept evaluation).
- Several IR-generator consumer sites now use `CallInfo::tryFrom` instead of per-type variant dispatch: `IrGenerator_Call_Indirect.cpp` (merged three object-type-extraction branches into one), `IrGenerator_MemberAccess.cpp` (removed dead `FunctionCallNode` branch), and `IrGenerator_Visitors_TypeInit.cpp` (static-member lazy instantiation).
- Legacy-node deletion is complete: `ExpressionNode` now only carries `CallExprNode` for ordinary calls, the legacy node classes are gone, and the remaining source/test comments have been refreshed to describe the unified representation instead of the removed split nodes.

## Important implementation notes

- Preserve the distinction between “has receiver” and “no receiver”; collapsing that into ad-hoc null checks will make virtual dispatch and implicit-`this` handling harder to reason about.
- Move call metadata to the shared node early. Much of the current friction comes from only `FunctionCallNode` carrying qualified-name/template/mangled-name state.
- Keep parser resolution permissive enough for placeholders, but make later passes consume a single normalized shape.
- Keep the `ExpressionNode` surface small and consistent while migrating, because many visitors branch on exact variant alternatives today.

## Suggested rollout order

1. Shared AST representation and helpers
2. Parser normalization
3. Template substitution/rebinding updates
4. Semantic-analysis unification
5. IR lowering unification
6. Legacy node removal and cleanup

## Expected payoff

- less parser duplication for call construction
- fewer metadata copy bugs in template flows
- one place to improve overload recovery and argument conversion
- fewer synthetic conversions between member and free calls in codegen
- a cleaner base for future call forms such as callable objects and more template-heavy call sites
