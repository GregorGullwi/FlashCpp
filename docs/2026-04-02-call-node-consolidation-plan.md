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
- [ ] Merge semantic-analysis lookup, overload-recovery, argument-conversion, and reference-binding logic onto the shared call abstraction.
- [ ] Merge IR lowering so there is one common call lowering pipeline with small branches only for receiver passing, virtual dispatch, and indirect-call specifics.
- [ ] Remove temporary call-form conversions such as member-to-function fallback nodes once all downstream code reads the unified representation directly.
- [ ] Delete the legacy duplicate call nodes after all call sites and visitors stop depending on them.

### Current status

- Parser normalization is now complete for the ordinary free-call and member-call paths; those parser sites emit `CallExprNode` and the rebased Windows suite is green after the downstream compatibility fixes.
- Template substitution/rebinding now keeps the migrated parser output in unified form through the static-member rebinder and the receiverless substitution path; the remaining temporary bridges are concentrated in constexpr evaluation and direct/member IR lowering.
- The remaining work is centered on collapsing semantic-analysis and IR lowering onto direct `CallExprNode` handling, then removing those last legacy conversions before deleting the duplicate legacy nodes.

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
