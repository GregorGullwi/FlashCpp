# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Indirect virtual base construction/layout is still incorrect when the most-derived
  class inherits a virtual base only through an intermediate base. A minimal repro is:
  `struct V { int v; V(int x = 7) : v(x) {} }; struct B : virtual V { B() : V(23) {} }; struct D : B { D() : V(11), B() {} };`
  `D{}.v` currently observes the intermediate-base initialization instead of the
  most-derived virtual-base initialization.

- **Struct-local type alias in static constexpr member initializer** — when a template struct
  uses a struct-local type alias in a `static constexpr` member initializer
  (e.g. `template<bool B> struct S { using C = BoolConst<B>; static constexpr bool v = C::value; }`),
  the ConstExpr evaluator cannot resolve `C::value` during early normalization (logs
  "Undefined qualified identifier in constant expression: C::value"). The lazy fallback path
  then runs but produces the wrong result (always the `true`-instantiation's value). The fix
  requires the ExpressionSubstitutor or the ConstExpr evaluation context to look up struct-local
  type aliases when resolving the namespace part of a `QualifiedIdentifierNode`.
  Minimal repro: see `tests/test_toplevel_alias_chain_nontype_ret42.cpp` for the passing subset
  and the narrowed repro below:
  ```
  template<bool B> struct WA { using C = IC<bool,B>; static constexpr bool v = C::value; };
  // WA<true>::v == 1  OK;  WA<false>::v == 1 (wrong, should be 0)
  ```

- `generateInstantiatedNameFromArgs()` still does not distinguish non-value type args
  that differ only in `is_dependent`. `makeTypeIndexArg()` (`src/TemplateRegistry_Types.h:682-692`)
  does not copy `is_dependent` or `dependent_name` into the `TypeIndexArg` (which lacks
  those fields), so `makeInstantiationKey()` → `makeTypeIndexArg()` → `TypeIndexArg::hash()`
  produces the same instantiation key and the same generated name for two type args that
  differ only in dependency state. This can cause silent incorrect template lookups when
  dependent and resolved args coexist. Either add `is_dependent`/`dependent_name` fields
  to `TypeIndexArg` and propagate them in `makeTypeIndexArg()`, `TypeIndexArg::hash()`,
  and `TypeIndexArg::operator==()`, or document that dependent args are always resolved
  before name generation. (PR #1164, partially addressed in PR #1166)
