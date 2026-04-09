# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

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

- `visitForStatementNode` (`src/IrGenerator_Stmt_Control.cpp:155-236`) calls
  `enter_scope`/`enterScope` and `exitScope`/`exit_scope` but does **not** emit
  `ScopeBegin`/`ScopeEnd` IR instructions, unlike every other scope site
  (`visitBlockNode`, `visitRangedForArray`, `visitRangedForBeginEnd`). This is
  pre-existing and has not caused a known failure yet, but any downstream IR pass
  or destructor-emission logic that relies on `ScopeBegin`/`ScopeEnd` for scope
  boundary detection would silently skip for-loop scopes. Should be investigated
  and fixed, ideally by adopting `IrScopeGuard` there. (PR #1178)
