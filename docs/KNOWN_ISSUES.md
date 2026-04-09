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
  
- The new `AstToIr::IrScopeGuard` RAII helper (introduced for range-for init-statement
  scoping) should be adopted in the other places that manually pair
  `enter_scope`/`enterScope`/`ScopeBegin` with `exitScope`/`ScopeEnd`/`exit_scope`:
  `visitBlockNode`, `visitForStatementNode`, the loop-body scopes in
  `visitRangedForArray` and `visitRangedForBeginEnd`, and catch-clause scopes in
  `CodeGen.h`. `visitForStatementNode` also currently omits `ScopeBegin`/`ScopeEnd`
  IR instructions unlike all other scope sites, which should be fixed at the same
  time. Additionally, `RangedForStatementNode` should gain a `for_token` field
  (like `BreakStatementNode::break_token_`) so the guard can carry a real source
  location; the parser construction site in `parse_for_loop()` would need to pass
  the consumed `for` keyword token. (PR #1178)

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
