# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

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

- Dependent pointer-to-template-parameter args (`Wrapper<T*>::tag`) accessed from
  inside another template are not yet handled correctly. A minimal repro is:
  ```cpp
  template <typename T>
  struct Wrapper {
      static constexpr int tag = sizeof(T);
  };

  template <typename T>
  struct Outer {
      static int getTag() { return Wrapper<T>::tag; }
      static int getPointerTag() { return Wrapper<T*>::tag; }
  };

  int main() {
      int result = 0;
      if (Outer<int>::getTag() != (int)sizeof(int)) result |= 1;
      if (Outer<double>::getTag() != (int)sizeof(double)) result |= 2;
      if (Wrapper<int>::tag != (int)sizeof(int)) result |= 4;
      if (Wrapper<double>::tag != (int)sizeof(double)) result |= 8;
      if (Outer<int>::getPointerTag() != (int)sizeof(int*)) result |= 16;
      return result;
  }
  ```
