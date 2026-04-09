# Known Issues

## Materialization follow-up

- Namespace-scoped out-of-line explicit member specializations that rely on a
  defaulted class-template argument still appear to miss the final owner/member
  binding. A reduced namespaced variant of
  `tests/test_template_spec_outofline_default_arg_ret42.cpp` linked with an
  unresolved member symbol while the global form passed.

## Deferred template dependency detection is incomplete for some expression kinds

`expressionHasDeferredTemplateDependency()` in
`src/Parser_Expr_PrimaryExpr.cpp` only recurses into three `ExpressionNode`
variants: `TemplateParameterReferenceNode`, `NoexceptExprNode`, and
`CallExprNode`.  The remaining 29 variants (`BinaryOperatorNode`,
`UnaryOperatorNode`, `StaticCastNode`, `ConstructorCallNode`,
`MemberAccessNode`, etc.) unconditionally return `false`.

This means a dependent template parameter buried inside an expression like
`static_cast<T>(x)` or `a + T{}` will not be detected by the AST-walk path.
In practice the companion check `argTypesAreDeferredTemplateDependent()` —
which inspects the *resolved type* of each argument rather than the AST
structure — provides a safety net that covers most real-world cases (including
the libstdc++ `swap` / `declval` probes targeted by PR #1187).

If a future standard-library pattern produces an argument whose expression
contains a dependent parameter **and** whose resolved type appears concrete,
the parser will fail to create a deferred placeholder and will hard-error
instead of deferring.  Extending the visitor to handle more variants (at least
`BinaryOperatorNode`, `UnaryOperatorNode`, `StaticCastNode`, and
`ConstructorCallNode`) would close this gap.
