# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- Polymorphic virtual-inheritance hierarchies still use the older embedded virtual-base
  layout/constructor path. The new most-derived virtual-base sharing fix is currently
  only enabled for non-polymorphic hierarchies, because polymorphic virtual bases still
  need ABI-correct secondary vtables / RTTI offsets for exception handling and related
  runtime adjustments.

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
