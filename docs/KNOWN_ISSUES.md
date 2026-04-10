# Known Issues

## Materialization follow-up

- Namespace-scoped out-of-line explicit member specializations that rely on a
  defaulted class-template argument still appear to miss the final owner/member
  binding. A reduced namespaced variant of
  `tests/test_template_spec_outofline_default_arg_ret42.cpp` linked with an
  unresolved member symbol while the global form passed.
  
## EBO / `[[no_unique_address]]` — PR #1184

### Investigations

#### `[[no_unique_address]]` only handles empty types, not tail-padding reuse

`src/AstNodeTypes_DeclNodes.h:185-194` — The current implementation only applies
the zero-size optimization for `isEmptyLayoutLike()` types. Per C++20
\[dcl.attr.nouniqueaddr\], the attribute also permits reusing tail padding of
non-empty members. This is a standards-compliant simplification (the attribute is
permissive, not mandatory) but means `struct S { [[no_unique_address]] NonEmpty a; char c; }`
will not benefit from potential tail padding optimization.

#### `total_size` tracking for NUA empty members at non-zero offsets

`src/AstNodeTypes_DeclNodes.h:216-220` — When a NUA empty member is placed at a
non-zero offset due to same-type overlap, `total_size = offset + layout_member_size`
(with `layout_member_size == 0`) sets `total_size` to the bumped offset. This may
inflate `dsize` beyond what the Itanium ABI specifies. Needs verification against
the ABI specification for the distinction between `dsize` and `sizeof` semantics.

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
