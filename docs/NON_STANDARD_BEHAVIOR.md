# FlashCpp — Non-Standard and Deviating C++20 Behavior

This document catalogues every known place where FlashCpp's behavior deviates from
the ISO C++20 standard, produces incorrect results, or deliberately accepts non-standard
extensions. Items already fully documented in another `docs/` file are cross-referenced
rather than duplicated.

> **Legend**
> - ✅ Correct — standard-compliant
> - ⚠️ Partial — works in common cases; edge cases deviate
> - ❌ Missing / Wrong — standard requires this; FlashCpp does not implement it or
>   produces incorrect output

---

## Sections

Each section corresponds to a compiler subsystem so that parallel work stays conflict-free.

| # | Subsystem | Source files | Section |
|---|-----------|-------------|---------|
| 1 | Semantic Analysis | `SymbolTable.h` · `OverloadResolution.h` · `NameMangling.h` | [01_semantic_analysis.md](non_standard/01_semantic_analysis.md) |
| 2 | Parser — Declarations | `Parser_Decl_StructEnum.cpp` · `Parser_Decl_DeclaratorCore.cpp` | [02_parser_declarations.md](non_standard/02_parser_declarations.md) |
| 3 | Parser — Templates | `Parser.h` · `Parser_Templates_Inst_Deduction.cpp` | [03_parser_templates.md](non_standard/03_parser_templates.md) |
| 4 | Lambdas | `Parser_Expr_ControlFlowStmt.cpp` · `CodeGen_Lambdas.cpp` | [04_lambdas.md](non_standard/04_lambdas.md) |
| 5 | Constant Expressions | `ConstExprEvaluator_Core.cpp` · `ConstExprEvaluator_Members.cpp` | [05_constexpr.md](non_standard/05_constexpr.md) |
| 6 | Code Generation | `CodeGen_MemberAccess.cpp` · `CodeGen_Expr_*.cpp` · `CodeGen_Visitors_*.cpp` · `CodeGen_NewDeleteCast.cpp` | [06_codegen.md](non_standard/06_codegen.md) |
| 7 | IR Backend / Calling Convention | `IRConverter_Conv_Calls.h` · `IRConverter_Emit_CompareBranch.h` | [07_ir_backend.md](non_standard/07_ir_backend.md) |
| 8 | Preprocessor & Macros | `FileReader_Macros.cpp` | [08_preprocessor.md](non_standard/08_preprocessor.md) |
| 9 | Exception Handling | cross-refs to `EXCEPTION_HANDLING.md` | [09_exceptions.md](non_standard/09_exceptions.md) |
| 10 | Cross-Cutting Summary | — | [10_cross_cutting.md](non_standard/10_cross_cutting.md) |

---

*To add a new finding: edit the section file whose **Source files** column matches where
the fix would be made. Add an entry to `10_cross_cutting.md` only if the issue is already
tracked in another `docs/` file.*
