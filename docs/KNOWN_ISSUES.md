# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Indirect virtual base construction/layout is still incorrect when the most-derived
  class inherits a virtual base only through an intermediate base. A minimal repro is:
  `struct V { int v; V(int x = 7) : v(x) {} }; struct B : virtual V { B() : V(23) {} }; struct D : B { D() : V(11), B() {} };`
  `D{}.v` currently observes the intermediate-base initialization instead of the
  most-derived virtual-base initialization.

- Debug log left at Error level in `src/Parser_TypeSpecifiers.cpp:1428`.
  `FLASH_LOG(Parser, Error, "DBG alias ...")` has a "DBG" prefix and uses Error level,
  so it appears in production error output during normal alias template resolution.
  Should be changed to Debug level. (PR #1164)

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

- In `src/ConstExprEvaluator_Members.cpp:3476-3479`,
  `evaluate_specialization_static_member` is called immediately when a static member
  is found, **before** lazy instantiation (lines 3486–3498). If a partial
  specialization's AST contains a static member whose initializer still references
  template parameters, `evaluate()` is called on the raw, unsubstituted initializer.
  When that evaluation accidentally succeeds (e.g. parameter names resolve in the
  current scope), it returns a potentially incorrect value, bypassing the lazy
  instantiation path that would produce the correctly-substituted initializer.
  The early call should be moved after the lazy instantiation attempt. (PR #1164)

- Dead `else if` in `src/ExpressionSubstitutor.cpp:744-752` (and identical pattern at
  line 1170-1178). After `instantiate_and_register_base_template` returns empty,
  `instantiated_name` is unconditionally assigned `template_name_to_instantiate`
  (guaranteed non-empty), so the subsequent `else if (instantiated_name.empty())` is
  always false and `get_instantiated_class_name` is never called. This means the code
  falls back to the raw base template name instead of the hash-based instantiated name,
  potentially causing lookup mismatches when the type was registered under the
  hash-based name. (PR #1164)
