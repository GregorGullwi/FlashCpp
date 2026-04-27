# Known Issues

## std Header Compatibility

### KI-001: `std::rel_ops` operators incorrectly instantiated for pair types (test_std_utility.cpp)

**Symptom:** `Compilation failed due to semantic errors` with messages:
```
Compile error in 'operator<=': Operator< not defined for operand types
Compile error in 'operator>': Operator< not defined for operand types
Compile error in 'operator>=': Operator< not defined for operand types
```

**Root cause:** When `<utility>` is included, `<bits/stl_relops.h>` is also included,
defining `std::rel_ops::operator<=`, `operator>`, `operator>=` as templates taking
`const _Tp& x, const _Tp& y`. FlashCpp instantiates these templates for
`pair<int,float>` during compilation. Their bodies call `operator<` which does not
exist for C++20 `pair` (only `operator<=>` is defined in the C++20 concepts branch).

`std::rel_ops` is a regular (non-inline) nested namespace of `std`, so its operators
should NOT be reachable via ADL for `std::pair` argument types per C++20
[basic.lookup.argdep]. FlashCpp's lookup incorrectly finds them, instantiates them,
and then fails when compiling their bodies.

**Affected tests:** `tests/std/test_std_utility.cpp`

**Workaround:** None currently; the test still fails with exit code 1.

**Correct fix:** In `tryInstantiateOperatorTemplate`
(`src/Parser_Expr_BinaryPrecedence.cpp`) and
`findBinaryOperatorOverloadWithFreeFunction` (`src/OverloadResolution.h`), the ADL
lookup and `lookup_all` must not include operators from non-inline nested namespaces
of the associated namespace. Specifically, `std::rel_ops` symbols must be excluded
when doing ADL for `std::pair` operands.

---

### KI-002: Constructor codegen crashes for uninstantiated template constructors (FIXED)

**Status:** Fixed in `src/IrGenerator_Visitors_Decl.cpp`.

**Root cause:** When generating code for a class template instantiation (e.g.
`pair<int,float>`), the member-function loop in `beginStructDeclarationCodegen` would
visit constructor nodes whose parameter types were still `TypeCategory::UserDefined`
(=23) — meaning the parser failed to record the constructor's own template parameters
(e.g. `template<_U1,_U2> pair(_U1&&, _U2&&)` where `_U1`/`_U2` remain unresolved).
Attempting to generate IR for such a constructor crashed in reference-identifier load
lowering with "Type with no runtime size reached codegen (type=23)".

**Fix applied:**
1. `ConstructorDeclarationNode` path: skip if any parameter has `TypeCategory::UserDefined`.
2. `ConstructorDeclarationNode` path: skip if `ctor.has_template_parameters()`.
3. `TemplateFunctionDeclarationNode` path: skip if `!tmpl.template_parameters().empty()`.
