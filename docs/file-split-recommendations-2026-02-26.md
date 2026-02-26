# FlashCpp file split recommendations (mono build preserved) - 2026-02-26

## 1) Scope and constraints

This proposal targets maintainability and reviewability while **keeping the current mono/unity build model**:

- Keep `src\main.cpp` as the only compiler compile unit in Make/MSVC.
- Keep implementation shards as `#include`d fragments (current pattern in `FlashCppUnity.h`, `CodeGen.h`, `FileReader.h`, `ConstExprEvaluator.h`, `TemplateRegistry.h`).
- Update Makefile and MSVC project metadata for dependency tracking, discoverability, and lower maintenance cost, **not** to add new compile units.

---

## 2) Measured hotspots (src/)

Size scan summary:

- Total source/header files scanned: **92**
- Total size: **~6264 KB**
- Files over 500 KB: **3**
- Files over 200 KB: **7**
- Top 15 files account for **~71.8%** of total src size

Largest files (size + lines):

| File | Size | Lines |
|---|---:|---:|
| `src\Parser_Templates.cpp` | 911.3 KB | 20,188 |
| `src\IRConverter.h` | 747.5 KB | 15,978 |
| `src\Parser_Expressions.cpp` | 561.8 KB | 11,839 |
| `src\Parser_Declarations.cpp` | 431.5 KB | 9,982 |
| `src\CodeGen_Functions.cpp` | 288.7 KB | 6,042 |
| `src\CodeGen_Expressions.cpp` | 270.8 KB | 5,458 |
| `src\CodeGen_Visitors.cpp` | 249.6 KB | 5,183 |
| `src\CodeGen_Statements.cpp` | 185.7 KB | 3,742 |
| `src\AstNodeTypes.h` | 181.2 KB | 3,767 |
| `src\Parser_Types.cpp` | 167.1 KB | 3,689 |
| `src\ObjFileWriter.h` | 116.6 KB | 2,360 |
| `src\IRTypes.h` | 108.7 KB | 2,856 |

---

## 3) Current build/layout facts relevant to splitting

- `main.cpp` includes `FlashCppUnity.h`, so compiler code is built as a unity TU.
- `FlashCppUnity.h` currently includes parser shards directly and also includes headers that themselves include shard `.cpp`s (CodeGen/FileReader/ConstExpr/TemplateRegistry).
- Makefile keeps `MAIN_SOURCES := src/main.cpp` and tracks unity dependencies via `UNITY_SOURCES` list.
- `FlashCpp.vcxproj` and `FlashCppMSVC.vcxproj` each compile only `src\main.cpp` for compiler builds and list shard `.cpp` files under `<ClInclude>`.
- `FlashCpp.vcxproj.filters` has duplicate `<ClInclude>` entries for 10 shard files (CodeGen/ConstExpr/FileReader/TemplateRegistry shards), which should be cleaned.

---

## 4) Detailed split recommendations

## 4.1 Parser area (highest ROI first)

### A) `src\Parser_Templates.cpp` (20k lines)

Split into thematic shards and keep `Parser_Templates.cpp` as an aggregator:

- `Parser_Templates_Decls.cpp`  
  `parse_template_declaration`, parameter list/forms, explicit template args, member template declaration parsing.
- `Parser_Templates_Concepts.cpp`  
  `parse_concept_declaration`, `parse_requires_expression`, constexpr checks for constraints.
- `Parser_Templates_Instantiation.cpp`  
  `try_instantiate_*`, `instantiate_*`, explicit instantiation paths.
- `Parser_Templates_Substitution.cpp`  
  `substitute*`, non-type substitution helpers, expression substitution.
- `Parser_Templates_Lazy.cpp`  
  `instantiateLazy*`, `evaluateLazyTypeAlias`, phase-based/lazy flows.
- `Parser_Templates_MemberOutOfLine.cpp`  
  out-of-line template member parsing helpers.

Why: this file currently mixes declaration parsing, constraints, substitution, eager/lazy instantiation, and out-of-line resolution in one place.

### B) `src\Parser_Expressions.cpp` (11.8k lines)

This file currently contains both expression parsing and multiple statement/declaration-adjacent routines (`parse_if_statement`, loops, try/throw, `parse_extern_block`, attribute helpers, etc.). Split by grammar domain:

- `Parser_Expr_PrimaryUnary.cpp` (primary/unary/casts/lambda entry)
- `Parser_Expr_PostfixCalls.cpp` (postfix ops, call forms, template-call helpers)
- `Parser_Expr_BinaryPrecedence.cpp` (precedence climbing and operator table paths)
- `Parser_Expr_ControlFlowStmt.cpp` (if/for/while/switch/try/throw/return/break/continue/goto)
- `Parser_Expr_QualLookup.cpp` (qualified IDs, template-aware lookup helpers)

### C) `src\Parser_Declarations.cpp` (10k lines)

Split by declaration class:

- `Parser_Decl_TopLevel.cpp` (`parse_top_level_node`, namespace/using/static_assert dispatch)
- `Parser_Decl_StructEnum.cpp` (struct/class/union/enum/friend/template-friend)
- `Parser_Decl_DeclaratorCore.cpp` (declarator parsing chain)
- `Parser_Decl_FunctionOrVar.cpp` (`parse_declaration_or_function_definition`, out-of-line ctor/dtor)
- `Parser_Decl_TypedefUsing.cpp` (typedef/type alias related)

### D) `src\Parser_Types.cpp` (3.7k lines, but high churn)

- `Parser_TypeSpecifiers.cpp` (`parse_type_specifier`, cv/ref qualifiers, decltype)
- `Parser_FunctionHeaders.cpp` (header/signature/trailing specifiers/mangled-name computation)
- `Parser_FunctionBodies.cpp` (delayed body, parameter registration, scope setup/validation)

---

## 4.2 Code generation and lowering

### E) CodeGen shards (already split once; perform second-level split)

Current files are still 3.7k-6k lines each. Keep `CodeGen.h` as the public umbrella, but split internals:

- `CodeGen_Visitors.cpp` -> `CodeGen_Visitors_Decl.cpp`, `CodeGen_Visitors_Namespace.cpp`, `CodeGen_Visitors_TypeInit.cpp`
- `CodeGen_Statements.cpp` -> `CodeGen_Stmt_Control.cpp`, `CodeGen_Stmt_Decl.cpp`, `CodeGen_Stmt_TryCatchSeh.cpp`
- `CodeGen_Expressions.cpp` -> `CodeGen_Expr_Primitives.cpp`, `CodeGen_Expr_Operators.cpp`, `CodeGen_Expr_Conversions.cpp`
- `CodeGen_Functions.cpp` -> `CodeGen_Call_Direct.cpp`, `CodeGen_Call_Indirect.cpp`, `CodeGen_MemberAccess.cpp`, `CodeGen_NewDeleteCast.cpp`
- `CodeGen_Lambdas.cpp` -> `CodeGen_Lambda_Closure.cpp`, `CodeGen_Lambda_Generic.cpp`

### F) `src\IRConverter.h` (16k lines)

This is the largest concentration of backend logic and should be split with strict ordering:

- `IRConverter_Encoding.h` (opcode byte encoding helpers currently near file top)
- `IRConverter_ABI.h` (Win64/SysV register maps and ABI helper templates)
- `IRConverter_Emit_MovLoadStore.h`
- `IRConverter_Emit_ArithmeticBitwise.h`
- `IRConverter_Emit_CompareBranch.h`
- `IRConverter_Emit_CallReturn.h`
- `IRConverter_Emit_FloatSIMD.h`
- `IRConverter_Emit_EHSeh.h`
- `IRConverter_ConvertMain.h` (high-level IR walking/dispatch)

Keep `IRConverter.h` as umbrella and include these in required dependency order.

---

## 4.3 Core model and object writer headers

### G) `src\AstNodeTypes.h`

Split into domain headers (still included by `AstNodeTypes.h` umbrella):

- `AstNodeTypes_Core.h` (ASTNode, common enums/handles/types)
- `AstNodeTypes_TypeSystem.h` (Type, qualifiers, struct/enum type info)
- `AstNodeTypes_Expr.h`
- `AstNodeTypes_Stmt.h`
- `AstNodeTypes_Decl.h`
- `AstNodeTypes_Template.h`

### H) `src\IRTypes.h`

- `IRTypes_Opcode.h` (opcode enum/layout constants)
- `IRTypes_Value.h` (TypedValue/temp metadata)
- `IRTypes_Ops_Control.h`
- `IRTypes_Ops_Memory.h`
- `IRTypes_Ops_CallEH.h`
- `IRTypes_Formatters.h`

### I) `src\TemplateRegistry.h`

Keep API stable, but split implementation-heavy inline sections:

- `TemplateRegistry_Types.h` (TemplateTypeArg/TemplateArgument/hash/key types)
- `TemplateRegistry_Patterns.h` (specialization pattern matching/scoring/SFINAE)
- `TemplateRegistry_Core.h` (registry class, maps, lookup/register APIs)
- `TemplateRegistry_InstantiationCache.h`
- keep existing `TemplateRegistry_Lazy.cpp` include pattern, and further split it if needed.

### J) `src\ObjFileWriter.h` and `src\ElfFileWriter.h`

Both mix section management, relocation, EH, RTTI, and debug generation. Split by responsibility:

- `*_SectionsSymbols.h` (section creation, symbol add/get/cache)
- `*_Relocations.h`
- `*_Exceptions.h` (SEH/Itanium EH metadata generation)
- `*_DebugInfo.h` (CodeView/DWARF paths)
- `*_RTTI.h` (RTTI/type-descriptor/vtable emission)

Shared helpers should move to `ObjectFileCommon.h` when duplicated across COFF/ELF.

---

## 5) Mono-build-safe implementation pattern

For each oversized file, use an umbrella + shards model:

```cpp
// Example: Parser_Templates.cpp
#include "Parser_Templates_Decls.cpp"
#include "Parser_Templates_Concepts.cpp"
#include "Parser_Templates_Substitution.cpp"
#include "Parser_Templates_Instantiation.cpp"
#include "Parser_Templates_Lazy.cpp"
```

This keeps one unity TU while reducing per-file size and improving navigation/review locality.

---

## 6) Makefile recommendations (dependency tracking only, no new compile units)

Keep:

- `MAIN_SOURCES := $(SRCDIR)/main.cpp`

Improve:

1. Move `UNITY_SOURCES` into a dedicated include fragment (e.g. `build/unity_sources.mk`) to avoid growth in main Makefile.
2. Group by glob where safe for dependency tracking (e.g. parser/codegen shard prefixes), while preserving explicit umbrella files.
3. Add a validation target that checks every included shard in unity headers is present in dependency tracking.
4. Keep `main`/`release` command line unchanged (`$(CXX) ... $(MAIN_SOURCES)`).

---

## 7) MSVC project recommendations (no new compile units)

Applies to both:

- `FlashCpp.vcxproj`
- `FlashCppMSVC.vcxproj`

Keep:

- only `<ClCompile Include="src\main.cpp">` for compiler build.

Improve:

1. Centralize shared unity `<ClInclude>` shard list in one imported props file (e.g. `build\FlashCpp.UnityIncludes.props`) and import in both vcxproj files.
2. Keep test compile units gated exactly as today.
3. Deduplicate `FlashCpp.vcxproj.filters` (currently has duplicate entries for 10 shard files).
4. Add subsystem filter folders (`Parser`, `CodeGen`, `IR`, `ObjectWriter`, `Templates`) so additional shards remain navigable without compile-unit changes.

---

## 8) Suggested rollout order

1. Parser templates + expressions split (largest parser pain).
2. IRConverter split (largest backend concentration).
3. CodeGen second-level split.
4. Ast/IR type headers split.
5. Object writer splits (COFF/ELF/EH/RTTI/debug).
6. Build metadata consolidation (Makefile include fragment + shared props + filters cleanup).

At each step: preserve behavior by keeping umbrella include entrypoints stable.

---

## 9) Practical size budget targets (to prevent regression)

- Hard warning: implementation shard > 3,500 lines or > 220 KB.
- Hard warning: umbrella header > 2,500 lines or > 180 KB.
- Prefer adding a new shard once a file exceeds either threshold.

These thresholds are tuned to current repository scale and should be enforced by convention (or lightweight CI check later).

