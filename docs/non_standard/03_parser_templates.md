# FlashCpp Non-Standard Behavior — Parser: Templates

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/Parser.h` (`createBoundIdentifier`), `src/Parser_Templates_Inst_Deduction.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.6 Two-Phase Template Name Lookup Not Formally Separated ⚠️

**Standard (C++20 [temp.dep]):** Non-dependent names in a template body must be resolved at
*definition* time (Phase 1); dependent names are deferred to *instantiation* time (Phase 2).

**FlashCpp:** `createBoundIdentifier` (Parser.h:1190) performs a single lookup at parse time
and returns `IdentifierBinding::Unresolved` for anything it cannot immediately classify,
including types and structs. There is no formal Phase 1 / Phase 2 separation. In practice
most non-dependent names bind correctly because codegen falls back to runtime lookup, but
certain non-dependent names that happen to be types fall through to `Unresolved` and rely on
the codegen fallback path.

**Location:** `src/Parser.h:1186–1246`
**Plan:** Phase 3 of the Identifier Resolution Plan (`docs/2026-03-06_IdentifierResolutionPlan.md`).

---

### 3.1 Non-Type Template Parameter (NTTP) Deduction Not Implemented ❌

**Standard (C++20 [temp.deduct]):** Template argument deduction must handle NTTPs, deducing
their values from the corresponding function arguments treated as constant expressions.

**FlashCpp:** `deduceTemplateArguments` (Parser_Templates_Inst_Deduction.cpp:813) has:

```
// TODO: Add support for non-type parameters and more complex deduction
```

NTTPs without default values that cannot be deduced cause a logged error and `std::nullopt`.
NTTPs with defaults are silently skipped. Users must always supply NTTP values explicitly.

**Location:** `src/Parser_Templates_Inst_Deduction.cpp:813, 1114`

---

### 3.2 Complex Dependent-Type Instantiation Falls Back Silently ⚠️

**Standard:** Template instantiation must correctly substitute all dependent type forms.

**FlashCpp:** When the primary type-resolution path fails during instantiation,
Parser_Templates_Inst_Deduction.cpp:1413 falls back to a "simple substitution (old
behavior)" path without emitting a diagnostic. Incorrectly instantiated templates can reach
codegen and link.

**Location:** `src/Parser_Templates_Inst_Deduction.cpp:1413`
