# FlashCpp Non-Standard Behavior — Parser: Declarations

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/Parser_Decl_DeclaratorCore.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 5.4 Parenthesized Declarator Form `(*fp)(params)` Not Parsed in All Contexts ⚠️

**Standard:** The full declarator grammar includes `(*fp)(params)` for function pointer
variables and `(*)(params)` for unnamed function pointer parameters in all declaration
contexts.

**FlashCpp:** `parse_direct_declarator` (Parser_Decl_DeclaratorCore.cpp:958) has:

```
// TODO: Handle parenthesized declarators like (*fp)(params) for function pointers
```

Specialized paths handle many function pointer cases, but certain compound forms (e.g.,
function returning a function pointer, arrays of function pointers in some contexts) may fail
to parse.

**Location:** `src/Parser_Decl_DeclaratorCore.cpp:958`
