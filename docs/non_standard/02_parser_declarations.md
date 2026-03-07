# FlashCpp Non-Standard Behavior — Parser: Declarations

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/Parser_Decl_StructEnum.cpp`, `src/Parser_Decl_DeclaratorCore.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.1a Hidden Friends Also Visible via Ordinary Lookup ⚠️

**Standard (C++20 [basic.lookup.argdep]):** A *hidden friend* (a friend function defined inside a
class body) is deliberately **not** introduced into the enclosing namespace scope for ordinary
unqualified lookup. It should only be found via ADL.

**FlashCpp:** When an inline friend function definition is parsed (e.g.,
`friend int getVal(X x) { return x.val; }`), FlashCpp registers it in
`SymbolTable::namespace_symbols_` via `insert_into_namespace()`. This makes the function also
findable by ordinary unqualified lookup — not just ADL — which is **non-standard**.

In practice this widens rather than narrows what compiles, so no correct program is rejected.
All standard ADL usage still works correctly.

**Location:** `src/Parser_Decl_StructEnum.cpp` — `parse_friend_declaration()`

---

### 5.4 Parenthesized Declarator Form `(*fp)(params)` Not Parsed in All Contexts ⚠️

**Standard:** The full declarator grammar includes `(*fp)(params)` for function pointer
variables and `(*)(params)` for unnamed function pointer parameters in all declaration
contexts.

**FlashCpp:** `parse_direct_declarator` (Parser_Decl_DeclaratorCore.cpp:898) has:

```
// TODO: Handle parenthesized declarators like (*fp)(params) for function pointers
```

Specialized paths handle many function pointer cases, but certain compound forms (e.g.,
function returning a function pointer, arrays of function pointers in some contexts) may fail
to parse.

**Location:** `src/Parser_Decl_DeclaratorCore.cpp:898`
