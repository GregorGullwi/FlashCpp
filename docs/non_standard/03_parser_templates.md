# FlashCpp Non-Standard Behavior — Parser: Templates

[← Index](../NON_STANDARD_BEHAVIOR.md)

Covers `src/Parser.h` (`createBoundIdentifier`), `src/Parser_Templates_Inst_Deduction.cpp`.

> **Legend** · ✅ Correct · ⚠️ Partial · ❌ Missing / Wrong

---

### 1.6 Two-Phase Template Name Lookup Still Uses a Unified Binding Path ⚠️

**Standard (C++20 [temp.dep], [temp.res]):** Non-dependent names in a template body must be
resolved at *definition* time (Phase 1); dependent names are deferred to *instantiation*
time (Phase 2).

**FlashCpp:** The previous "everything is looked up once at parse time" behavior has been
narrowed. Template body reparsing now records and diagnoses at least one class of Phase 1
violation: non-dependent names that were declared only after the template definition are
rejected during instantiation. However, `createBoundIdentifier` still uses a unified
lookup/binding path with `IdentifierBinding::Unresolved` as a fallback for names it cannot
classify immediately, rather than a fully separate semantic Phase 1 / Phase 2 pipeline.

This is therefore no longer a blanket absence of two-phase checking, but it is still not a
fully formal two-phase lookup implementation.

**Location:** `src/Parser.h:2696–2858`, `src/Parser_Templates_Inst_Deduction.cpp:956–994`

---

No other currently-audited non-standard template parsing items remain in this file.
