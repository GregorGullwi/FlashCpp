# Known Issues

## Phase 1 ([temp.res]/9) check downgraded to warning — false negatives on genuine violations

**Introduced by:** conversion of Phase 1 error to `FLASH_LOG(Parser, Warning, ...)` in
`src/Parser_Templates_Inst_Deduction.cpp`.

**Symptom:** Two negative tests that document truly ill-formed programs now compile
without error:

- `tests/test_template_nondependent_not_visible_fail.cpp`
- `tests/test_template_rebind_late_global_fail.cpp`

Both tests use a non-dependent name inside a template body that is declared *after* the
template definition — a clear violation of C++20 [temp.res]/9.  GCC and Clang reject
these programs.  FlashCpp now only warns and continues, producing incorrect object files.

**Root cause:** The Phase 1 cutoff logic in `Parser::createBoundIdentifier()` (`src/Parser.h`)
fires a false positive when a template is instantiated inline during parsing of an enclosing
function, causing outer-scope symbols to appear "after" the inner template's definition
line.  This triggered spurious errors on standard library headers (`<iterator>`,
`<string>`, `<memory>`, `<ranges>`), so the check was weakened to a warning as a
short-term fix.

**Correct fix (TODO):** The Phase 1 cutoff check should distinguish between:
1. Names declared in the *same translation unit* after the template definition (genuine
   violations — should remain a hard error).
2. Names whose declaration token position appears "later" only because of the
   parser's inline instantiation ordering (false positives — should be suppressed).

One approach is to record the token position at which the *outer* function or scope
began parsing and use that as the true cutoff, rather than the template body line alone.
Alternatively, track whether the candidate declaration is in the same file and whether
its source position genuinely post-dates the template keyword.

**Affected tests (unexpected pass):**
```
test_template_nondependent_not_visible_fail.cpp — should have failed
test_template_rebind_late_global_fail.cpp       — should have failed
```
