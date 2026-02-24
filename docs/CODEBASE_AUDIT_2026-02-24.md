# FlashCpp2 Codebase Audit - February 24, 2026

Comprehensive audit of the FlashCpp2 C++20 compiler front-end (~133K lines) targeting speed, readability, and code reuse improvements.

**Codebase Statistics:**
| Subsystem | Files | Lines | Primary Concerns |
|-----------|-------|-------|------------------|
| IRConverter | 1 | 17,546 | Emit helpers, dispatch, duplication |
| Parser | 7 | ~50,000 | Long functions, nesting, repeated patterns |
| CodeGen | 5 | ~23,635 | Symbol lookups, type extraction, dispatch |
| File Writers | 2 + common | ~4,781 | Byte packing, RTTI duplication, caching |
| Utilities | 6 | ~3,450 | Growth strategy, unused optimizations |

---

## Table of Contents
1. [Critical: Cross-Cutting Issues](#1-critical-cross-cutting-issues)
2. [IRConverter.h (Codegen Backend)](#2-irconverterh-codegen-backend)
3. [Parser Subsystem](#3-parser-subsystem)
4. [CodeGen (AST-to-IR)](#4-codegen-ast-to-ir)
5. [File Writers & Utilities](#5-file-writers--utilities)
6. [Prioritized Action Plan](#6-prioritized-action-plan)

---

## 1. Critical Cross-Cutting Issues

These patterns appear across multiple subsystems and offer the highest ROI if addressed.

### 1.1 Unnecessary `std::string` Allocations from `string_view`

**Scope:** 352+ occurrences in Parser, 70+ in CodeGen, scattered in IRConverter

Throughout the codebase, `std::string_view` or `StringHandle` values are wrapped in `std::string()` for map lookups, logging, or comparisons. Each creates a heap allocation on a hot path.

**Examples:**
- Parser: `std::string(current_token_.value())` in logging (Parser_Expressions.cpp lines 10-26)
- IRConverter: `function_spans[std::string(current_func_name)]` (line 4796)
- CodeGen: `StringTable::getOrInternStringHandle()` called repeatedly in loops

**Fix:** Use `std::unordered_map` with transparent hash/comparator (`std::string_view`-compatible), or cache `StringHandle` values at function entry.

### 1.2 Symbol / Type Lookup Duplication

**Scope:** 43+ occurrences in CodeGen, 10+ in IRConverter

The two-step "local then global" lookup pattern is copy-pasted everywhere:
```cpp
std::optional<ASTNode> symbol = symbol_table.lookup(handle);
if (!symbol.has_value() && global_symbol_table_) {
    symbol = global_symbol_table_->lookup(handle);
}
```

Similarly, `gTypesByName.find(handle)` followed by `->getStructInfo()` appears 20+ times.

**Fix:** Extract into shared helpers:
```cpp
std::optional<ASTNode> lookupSymbol(StringHandle handle);
const StructTypeInfo* getStructInfo(StringHandle handle);
const TypeSpecifierNode* getTypeSpecFromSymbol(const std::optional<ASTNode>& symbol);
```

### 1.3 Linear Searches Where Hash Lookups Should Be Used

- IRConverter: `variable_scopes` reverse iteration for variable lookup (lines 10262, 11719, 15647) - O(depth) per lookup
- ElfFileWriter: `getSectionByName()` iterates all sections (line 1639) - called 20+ times per file
- ObjFileWriter: `get_or_create_symbol_index()` does O(n) linear scan of symbol table (line 2747)

**Fix:** Add caches (`std::unordered_map`) for all three cases.

---

## 2. IRConverter.h (Codegen Backend)

**File:** `src/IRConverter.h` - 17,546 lines

### 2.1 Duplicate Emit Patterns (HIGH - ~2000 lines reducible)

#### REX Prefix + Displacement Encoding
The same REX prefix calculation and displacement field logic (offset == 0 needs disp8 for RBP, little-endian encoding) appears in 10+ functions across lines 253-1487:
- `generatePtrMovFromFrame`, `generateMovFromFrame32`, `generateMovzxFromFrame8/16`
- `generateMovsxFromFrame_8to64/16to64/32to64`, `generateMovToFrame8/16/32`

**Fix:** Extract helpers:
```cpp
struct ModRMEncoding { uint8_t mod_field; bool has_disp; bool disp_is_32bit; };
ModRMEncoding calculateModRM(int32_t offset, bool is_rbp_base = true);
inline void encodeDisplacement(uint8_t*& ptr, size_t& size, int32_t offset);
uint8_t buildRexPrefix(bool w, bool r, bool x, bool b);
```

#### SSE Instruction Generation
Three near-identical functions (lines 93-237): `generateSSEInstruction`, `generateSSEInstructionNoPrefix`, `generateSSEInstructionDouble`.

**Fix:** Merge into one with optional prefix parameter.

#### Float Comparison Handlers (~600 lines)
`handleFloatEqual`, `handleFloatNotEqual`, `handleFloatLessThan`, etc. (lines 11397-11610) all repeat the same SSE compare + SETcc + store pattern.

**Fix:** Parameterize:
```cpp
void handleFloatComparison(const IrInstruction& instruction, uint8_t setcc_opcode, const char* op_name);
```

#### Arithmetic Handler Boilerplate
`handleAdd` (lines 10893-10907) is nearly identical to `handleSubtract` (10909-10923), `handleBitwiseAnd`, etc.

**Fix:**
```cpp
void handleBinaryArithmetic(const IrInstruction& i, uint8_t opcode, const char* op_name);
```

### 2.2 Performance Issues

#### `textSectionData.insert()` - 1055 occurrences
Heavy use of vector insert with iterators can cause reallocations on hot paths.

**Fix:** Use batch `appendOpcodes()` helper or pre-reserve capacity.

#### `reference_stack_info_` Repeated Lookups
Pattern `auto ref_it = reference_stack_info_.find(offset); if (ref_it != ...)` appears 10+ times without caching (lines 4131, 4181, 4339, 4403, 4659, 4695, 6649, 6660, 6840, 6887).

**Fix:** Create `std::optional<ReferenceStackInfo> getReferenceInfo(int offset)` helper.

### 2.3 Readability

#### Giant Switch Statement (lines 3209-3577)
Main dispatch loop has 167 cases (~370 lines). Hard to navigate.

**Fix:** Consider a function-pointer dispatch table keyed on `IrOpcode`.

#### Magic Numbers Throughout
REX prefix values like `0x41`, `0x44`, `0x48` appear without documentation.

**Fix:** Define named constants:
```cpp
constexpr uint8_t REX_BASE = 0x40, REX_B = 0x41, REX_R = 0x44, REX_W = 0x48;
```

#### Long Functions
- `setupAndLoadArithmeticOperation()` (line 4070): ~250 lines, deeply nested
- `handleFunctionCall()` (line 6498): ~700 lines with multiple ABI cases

**Fix:** Split by concern (int vs float paths, per-ABI logic).

### 2.4 Estimated Impact
- **Code reduction:** 2,000-3,000 lines (15-20% smaller)
- **Performance:** 5-10% faster codegen (fewer allocations, less indirection)

---

## 3. Parser Subsystem

**Files:** `src/Parser.h`, `Parser_Core.cpp`, `Parser_Declarations.cpp`, `Parser_Expressions.cpp`, `Parser_Statements.cpp`, `Parser_Templates.cpp`, `Parser_Types.cpp` (~50K lines total)

### 3.1 Repeated Parse-and-Register Pattern (HIGH - ~100 lines reducible)

In `parse_top_level_node()` (Parser_Declarations.cpp lines 115-163), this block repeats 20+ times with only the parse function name changing:
```cpp
if (peek() == "using"_tok) {
    auto result = parse_using_directive_or_declaration();
    if (!result.is_error()) {
        if (auto node = result.node()) { ast_nodes_.push_back(*node); }
        return saved_position.success();
    }
    return saved_position.propagate(std::move(result));
}
```

**Fix:** Extract `try_parse_and_register(TokenKind, ParseResult (Parser::*parser_fn)())` helper. Or adopt the dispatch table pattern already used successfully in `Parser_Statements.cpp` lines 79-140 (`keyword_parsing_functions` map).

### 3.2 Sequential Token Checking (MEDIUM)

`if (peek() == "X"_tok)` chains: 231+ in Declarations, 140+ in Expressions, 338+ in Templates. Sequential if-else chains hurt branch prediction.

**Fix:** Dispatch tables (already proven in the codebase at Parser_Statements.cpp:79).

### 3.3 Skip Functions Duplication

`skip_balanced_braces()`, `skip_balanced_parens()`, `skip_template_arguments()` (Parser_Core.cpp lines 607-682) all follow the same depth-counter pattern.

**Fix:** Template:
```cpp
template<TokenKind Open, TokenKind Close>
void skip_balanced_delimiters();
```
Eliminates ~55 lines of duplicate code.

### 3.4 Save/Restore Token Position Overhead

21 occurrences of `SaveHandle saved_pos = save_token_position()` with immediate advance + peek + restore patterns. Many could use simple lookahead instead.

**Fix:** Add `peek_token(int offset)` for 1-2 token lookahead without save/restore overhead.

### 3.5 Excessively Long Functions

| Function | File | ~Lines |
|----------|------|--------|
| `parse_struct_member()` | Parser_Declarations.cpp | 500+ |
| `parse_out_of_line_definition()` | Parser_Declarations.cpp | 350 |
| `parse_expression()` | Parser_Expressions.cpp | 1000+ |
| `parse_primary_expression()` | Parser_Expressions.cpp | 800+ |
| `parse_template_declaration()` | Parser_Templates.cpp | 800+ |

### 3.6 Static Member Initialization Triplication

Three nearly identical blocks in `parse_out_of_line_definition()` (lines 2065-2220) handle parenthesized, brace, and copy initialization of static members.

**Fix:** Extract `parse_static_member_initializer()` (~60 lines vs ~155).

### 3.7 Deep Nesting in Statement Parsing

`parse_statement_or_declaration()` identifier handling (Parser_Statements.cpp lines 155-275) reaches 5+ levels of nesting with qualified name resolution + template argument skipping.

**Fix:** Extract `parse_qualified_type_name()` helper that returns a structured result.

---

## 4. CodeGen (AST-to-IR)

**Files:** `CodeGen_Functions.cpp` (6479), `CodeGen_Expressions.cpp` (5867), `CodeGen_Visitors.cpp` (5386), `CodeGen_Statements.cpp` (4003), `CodeGen_Lambdas.cpp` (1900)

### 4.1 Expression Dispatch Chains (MEDIUM)

40+ `if (std::holds_alternative<...>)` branches in `visitExpressionNode()` (CodeGen_Expressions.cpp lines 1-200).

**Fix:** Replace with `std::visit()` and a visitor struct. Eliminates the chain and improves cache behavior.

### 4.2 Address-of / Dereference IR Emission Duplication

The pattern for emitting `AddressOfOp` IR appears at lines 1059, 2502, 2574, 2597, 6060 in CodeGen_Functions.cpp. Same for `DereferenceOp`.

**Fix:**
```cpp
TempVar emitAddressOf(const IrOperand& operand, Type type, int size_bits);
TempVar emitDereference(const TypedValue& pointer, Type pointee_type, int size_bits);
```

### 4.3 `lookupSymbol()` → DeclarationNode Extraction Duplication (MEDIUM - ~80 lines reducible)

`lookupSymbol()` returns `std::optional<ASTNode>`, but 15+ call sites in CodeGen immediately extract a `DeclarationNode*` via inline `is<DeclarationNode>() / is<VariableDeclarationNode>()` checks — duplicating what `get_decl_from_symbol()` (AstNodeTypes.h) already provides. A redundant `getDeclarationFromSymbol()` (CodeGen_Expressions.cpp) also wraps the same logic.

**Examples (inline extraction — 6-8 lines each):**
- CodeGen_Functions.cpp: lines 639-645, 851-862, 2651-2657, 3052-3058
- CodeGen_Expressions.cpp: lines 1894-1901, 2075-2081, 2353-2359

**Fix:** Add `lookupDeclaration()` that combines `lookupSymbol()` + `get_decl_from_symbol()`:
```cpp
const DeclarationNode* lookupDeclaration(StringHandle handle) const;
const DeclarationNode* lookupDeclaration(std::string_view name) const;
```
Remove `getDeclarationFromSymbol()` from CodeGen_Expressions.cpp and replace all inline extraction with calls to `lookupDeclaration()` or `get_decl_from_symbol()`.

### 4.4 TempVar Allocation Pattern (222 occurrences)

`TempVar result_var = var_counter.next()` is called without any pooling or reuse of dead temporaries.

**Fix:** Consider a TempVar pool/arena scoped to function compilation. Could batch-allocate for complex expressions.

### 4.4 Vector Copies in Argument Building

`irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end())` appears 8-10 times per function call path without prior `reserve()`.

**Fix:** Reserve capacity upfront based on argument count.

### 4.5 Functions Too Long

| Function | File | ~Lines | Concern |
|----------|------|--------|---------|
| `generateFunctionCallIr()` | CodeGen_Functions.cpp | ~1265 | Intrinsics + overloads + args + ABI |
| `generateMemberFunctionCallIr()` | CodeGen_Functions.cpp | ~1375 | Similar |
| `generateMemberAccessIr()` | CodeGen_Functions.cpp | ~2500 | Identifier + member + operators |

**Fix:** Split into focused sub-functions: `resolveFunctionOverloads()`, `buildArgumentList()`, `emitFunctionCall()`.

### 4.6 Magic Constants

- `64` for pointer size in bits (lines 1015, 1159)
- `128` for Linux struct return threshold (line 1170)

**Fix:** Named constants:
```cpp
static constexpr int POINTER_SIZE_BITS = 64;
static constexpr int SYSV_STRUCT_RETURN_THRESHOLD = 128;
static constexpr int WIN64_STRUCT_RETURN_THRESHOLD = 64;
```

---

## 5. File Writers & Utilities

### 5.1 ObjFileWriter: RTTI/Vtable Byte Packing (HIGH - ~40% reducible)

`add_vtable()` (ObjFileWriter.h lines 2295-2800) contains ~500 lines of hand-crafted RTTI building with:
- Inline `for (int i = 0; i < 8; ++i) data.push_back(0)` loops (40+ occurrences)
- Identical symbol creation pattern repeated 15+ times
- Repeated `rdata_section->get_data_size()` calls

**Fix:**
```cpp
template<typename T>
void appendLE(std::vector<char>& buf, T value);   // Replace 40+ inline loops
void addSectionSymbol(const std::string& name, ...);  // Replace 15+ repeated blocks
```

### 5.2 ObjFileWriter: Auxiliary Symbol Boilerplate (lines 102-311)

10 identical copies of the auxiliary symbol creation pattern (~12 lines each).

**Fix:** Extract `addSectionAuxSymbol(symbol, section, name)`.

### 5.3 ObjFileWriter: Exception Handling Function Too Long

`add_function_exception_info()` spans ~1200 lines (lines 1002-2200) with 8+ levels of nesting.

**Fix:** Split into `emitPdataEntry()`, `emitXdataEntry()`, `emitCppExceptionInfo()`, `emitSEHExceptionInfo()`.

### 5.4 ElfFileWriter: Missing Section Cache

`getSectionByName()` (line 1639) does O(n) iteration, called 20+ times per file.

**Fix:** Add `std::unordered_map<std::string, ELFIO::section*> section_cache_`.

### 5.5 Section Metadata: Dual Maps

ObjFileWriter maintains two separate maps (lines 2775-2776):
```cpp
std::unordered_map<SectionType, std::string> sectiontype_to_name;
std::unordered_map<SectionType, int32_t> sectiontype_to_index;
```

**Fix:** Single `struct SectionInfo { std::string name; int32_t index; section* ptr; }` map.

### 5.6 ChunkedString: Aggressive Growth Factor

StringBuilder (ChunkedString.h lines 354-361) grows by 16x. For strings >1MB this wastes significant memory.

**Fix:** Adaptive growth: 16x for <4KB, 4x for <1MB, 2x above.

### 5.7 StackString: Dead Code

`USE_OLD_STRING_APPROACH` is hardcoded to 1 (StackString.h line 13), so the optimized `StackString` path is never used. Either benchmark and enable it, or remove the dead code.

---

## 6. Prioritized Action Plan

### Phase 1: Quick Wins (Low risk, high ROI)

| # | Change | Files | Est. Lines Saved | Status |
|---|--------|-------|-------------------|--------|
| 1 | Extract `lookupSymbol()` / `getStructInfo()` helpers | CodeGen_*.cpp | ~200 | ✅ Done |
| 2 | Extract `encodeDisplacement()` / `buildRexPrefix()` | IRConverter.h | ~300 | ✅ Done |
| 3 | Extract byte-packing helpers (`appendLE`, `appendZeros`) | ObjectFileCommon.h, ObjFileWriter.h | ~400 | ✅ Done |
| 4 | Parameterize float comparison handlers | IRConverter.h | ~170 | ✅ Done |
| 5 | Parameterize binary arithmetic handlers | IRConverter.h | ~35 | ✅ Done |
| 6 | Define named REX/ABI constants | IRConverter.h, CodeGen.h | ~0 (readability) | ✅ Done |
| 7 | Add section cache to ElfFileWriter | ElfFileWriter.h | ~0 (perf) | ✅ Done |
| 8 | Add symbol lookup cache to ObjFileWriter | ObjFileWriter.h | ~0 (perf) | ✅ Done |

### Phase 2: Medium Refactoring

| # | Change | Files | Est. Lines Saved | Status |
|---|--------|-------|-------------------|--------|
| 9 | Merge SSE instruction generators | IRConverter.h | ~100 | ✅ Done |
| 10 | Simplify `parse_top_level_node()` with `try_parse_and_push` helper | Parser_Declarations.cpp | ~45 | ✅ Done |
| 11 | Extract `skip_balanced_delimiters()` generic helper | Parser_Core.cpp | ~25 | ✅ Done |
| 12 | Extract `emitAddressOf()` / `emitDereference()` helpers | CodeGen_*.cpp | ~100 | ✅ Done |
| 13 | Extract `addSectionAuxSymbol()` helper | ObjFileWriter.h | ~100 | ✅ Done |
| 14 | Extract `finalize_static_member_init()` | Parser_Declarations.cpp | ~100 | ✅ Done |
| 15 | Reserve vector capacity in argument building | CodeGen_Functions.cpp | ~0 (perf) | ✅ Done (already has reserve at entry) |

### Phase 3: Larger Refactoring

| # | Change | Files | Impact | Status |
|---|--------|-------|--------|--------|
| 16 | Convert expression dispatch to `std::visit()` | CodeGen_Expressions.cpp | Readability + perf | TODO |
| 17 | Extract `resolveMangledName()` + deduplicate overload resolution | CodeGen_Functions.cpp | ~85 lines saved | ✅ Done |
| 18 | Split `add_function_exception_info()` | ObjFileWriter.h | Readability | TODO |
| 19 | Extract `isTwoRegisterStruct()` + `shouldPassStructByAddress()` ABI helpers | IRConverter.h | ~50 lines saved | ✅ Done |
| 20 | Extract `findVariableInfo()` to flatten variable scope lookups | IRConverter.h | ~15 lines saved | ✅ Done |
| 21 | Replace `std::string` map keys with `string_view`-compatible | Multiple | Perf (352+ allocs) | TODO |
| 26 | Unify `lookupSymbol()` → DeclarationNode extraction into `lookupDeclaration()` | CodeGen_*.cpp | ~80 lines saved | ✅ Done |

### Phase 4: Architectural (Optional)

| # | Change | Impact | Status |
|---|--------|--------|--------|
| 22 | Create `SymbolResolver` abstraction | Testability, separation | TODO |
| 23 | Create shared `FileWriter` base class | Code sharing ELF/COFF | TODO |
| 24 | TempVar pooling/arena | Memory performance | TODO |
| 25 | Evaluate/enable StackString optimization | Memory performance | TODO |

---

### Implementation Progress

**Completed in Phase 1+2 PR #762 (commit d52eb12):**
- **IRConverter.h:** `emitFloatComparisonInstruction()`, `handleBinaryArithmetic()`, `handleBitwiseArithmetic()`, REX named constants, unified `generateSSEInstructionWithPrefix()`, `calcModField()`/`encodeDisplacement()` helpers
- **ElfFileWriter.h:** `section_name_cache_` for O(1) `getSectionByName()` lookups
- **ObjFileWriter.h:** `symbol_index_cache_` for O(1) `get_or_create_symbol_index()`, `addSectionAuxSymbol()` helper
- **ObjectFileCommon.h:** `appendLE<T>()` and `appendZeros()` byte-packing helpers
- **Parser_Core.cpp:** `skip_balanced_delimiters()` generic helper
- **Parser_Declarations.cpp:** `finalize_static_member_init()` helper, `lookupSymbol()` helper replacing 21 lookup pattern instances
- **CodeGen.h:** `POINTER_SIZE_BITS`, `SYSV_STRUCT_RETURN_THRESHOLD`, `WIN64_STRUCT_RETURN_THRESHOLD`, `getStructReturnThreshold()`
- **CodeGen_Functions.cpp:** `emitAddressOf()` helper replacing 11 AddressOfOp sites

**Completed in Phase 1-3 continuation (PR #765):**
- **IRConverter.h:** `findVariableInfo()` centralizing variable scope reverse iteration (3 sites → 1), `isTwoRegisterStruct()` and `shouldPassStructByAddress()` ABI helpers deduplicating struct-passing logic
- **CodeGen_Lambdas.cpp:** `emitDereference()` helper (mirrors `emitAddressOf`)
- **CodeGen_Functions.cpp:** 6 `DereferenceOp` sites simplified with `emitDereference()`, `resolveMangledName()` lambda deduplicating 6 mangled name resolution blocks
- **CodeGen_Expressions.cpp:** 5 `DereferenceOp` sites simplified with `emitDereference()`
- **Parser_Declarations.cpp:** `try_parse_and_push` lambda in `parse_top_level_node()` (6 identical 7-line blocks → single-line calls)

**Net lines reduced:** ~760 lines across 12 files

**Completed in Phase 3 continuation (lookupDeclaration refactoring):**
- **CodeGen_Lambdas.cpp:** Added `lookupDeclaration(StringHandle)` and `lookupDeclaration(string_view)` helpers combining `lookupSymbol()` + `get_decl_from_symbol()` in one call
- **CodeGen_Functions.cpp:** 10 sites refactored: replaced `lookupSymbol()` + inline `is<DeclarationNode>()/is<VariableDeclarationNode>()` extraction or `get_decl_from_symbol()` with `lookupDeclaration()` (~67 lines removed)
- **CodeGen_Expressions.cpp:** 8 sites refactored: removed redundant `getDeclarationFromSymbol()` static function, replaced inline extraction patterns with `lookupDeclaration()` or `get_decl_from_symbol()` (~51 lines removed)

### Estimated Total Impact

- **Lines of code reduced:** ~878 through deduplication (Phases 1-3)
- **Readability:** Significant improvement from shorter functions, named constants, less nesting
- **Performance:** O(1) caches replacing O(n) linear scans in ElfFileWriter and ObjFileWriter
- **Maintenance:** Easier to add new IR opcodes, parse constructs, and file format features

---

*Generated by automated codebase audit. All line numbers reference the codebase as of commit 24adeca3.*
*Last updated with implementation progress as of PR #765 (Phase 1-3 continuation).*
