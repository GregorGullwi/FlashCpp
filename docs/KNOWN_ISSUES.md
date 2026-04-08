# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Indirect virtual base construction/layout is still incorrect when the most-derived
  class inherits a virtual base only through an intermediate base. A minimal repro is:
  `struct V { int v; V(int x = 7) : v(x) {} }; struct B : virtual V { B() : V(23) {} }; struct D : B { D() : V(11), B() {} };`
  `D{}.v` currently observes the intermediate-base initialization instead of the
  most-derived virtual-base initialization.

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

- C++20 range-for with an init-statement over a prvalue function-return container is
  still miscompiled. A minimal repro is
  `for (int factor = 2; auto value : makeContainer()) { sum += value * factor; }`,
  which currently produces the wrong runtime result even though plain
  `for (auto value : makeContainer())` works.
