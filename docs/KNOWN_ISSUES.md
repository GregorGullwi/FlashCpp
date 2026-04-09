# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

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

- `ConstructorVariant` default parameter values on name-mangling functions.
  Seven overloads in `src/NameMangling.h` (`generateItaniumMangledName`,
  `generateMangledName`, `generateMangledNameForConstructor`,
  `generateMangledNameFromNode`) use
  `ConstructorVariant constructor_variant = ConstructorVariant::Complete`.
  This lets callers silently omit the variant and get C1 (complete) mangling
  even when C2 (base-object) was intended, which would produce a wrong symbol
  at link time. The defaults should be removed and every call site should pass
  the variant explicitly. (PR #1176)
  
- C++20 range-for with an init-statement over a prvalue function-return container is
  still miscompiled. A minimal repro is
  `for (int factor = 2; auto value : makeContainer()) { sum += value * factor; }`,
  which currently produces the wrong runtime result even though plain
  `for (auto value : makeContainer())` works.

- Member access on a ternary object in address-of context can lose its struct
  `type_index` and fail with "struct type info not found for type_index=0".
  A minimal repro is:
  ```cpp
  struct Inner {
  	int value;
  };
  struct Outer {
  	Inner inner;
  };
  int main() {
  	Outer left;
  	Outer right;
  	left.inner.value = 10;
  	right.inner.value = 42;
  	bool pick_left = false;
  	int* ptr = &((pick_left ? left : right).inner.value);
  	if (*ptr != 42)
  		return 1;
  	*ptr = 99;
  	if (right.inner.value != 99)
  		return 2;
  	if (left.inner.value != 10)
  		return 3;
  	return 0;
  }
  ```
