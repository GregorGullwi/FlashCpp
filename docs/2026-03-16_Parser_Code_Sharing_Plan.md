# Parser Code Sharing Analysis and Refactoring Plan

**Date:** March 16, 2026

## Objective

This document identifies code duplication across parser code paths and proposes refactoring to increase code reuse.

## Current State: Parsing Paths

The compiler has distinct entry points for different kinds of declarations:

| Parsing Path | Primary File | Lines |
|--------------|--------------|-------|
| Free function | Parser_Decl_FunctionOrVar.cpp | 8-1149 |
| Member function (out-of-line) | Parser_Decl_FunctionOrVar.cpp | 123-649 |
| Constructor/Destructor (out-of-line) | Parser_Decl_FunctionOrVar.cpp | 1153-1536 |
| Template function | Parser_Templates_Function.cpp | 8-202 |
| Member function template | Parser_Templates_Function.cpp | 206-1144 |
| Template class member (out-of-line) | Parser_Templates_MemberOutOfLine.cpp | 8-767 |
| Delayed function body | Parser_FunctionBodies.cpp | 185-550 |

## Documented Code Duplication

### 1. Constructor Detection Lookahead

**Location A:** `Parser_Decl_FunctionOrVar.cpp:45-92`
**Location B:** `Parser_Templates_Function.cpp:274-349`

Both sections perform identical lookahead to detect constructor pattern `ClassName::ClassName(...)` vs destructor pattern `ClassName::~ClassName()`. Each uses:
- `save_token_position()` / `restore_token_position()` pattern
- Check for identifier matching struct name
- Check for `::` followed by same identifier or `~`

### 2. 'this' Pointer Setup

Repeated code to inject 'this' pointer into symbol table:

| Location | Lines |
|----------|-------|
| Parser_Decl_FunctionOrVar.cpp | 507-515 |
| Parser_Decl_FunctionOrVar.cpp | 594-603 |
| Parser_Decl_FunctionOrVar.cpp | 1324-1333 |
| Parser_FunctionBodies.cpp | 69-82 |

Each block performs:
1. Create TypeSpecifierNode with pointer level
2. Create DeclarationNode for 'this'
3. Insert into symbol table

### 3. Parameter Registration in Symbol Table

Repeated registration of function parameters:

| Location | Lines |
|----------|-------|
| Parser_Decl_FunctionOrVar.cpp | 518-527 |
| Parser_Decl_FunctionOrVar.cpp | 605-617 |
| Parser_Decl_FunctionOrVar.cpp | 1337-1349 |
| Parser_FunctionBodies.cpp | 85-92 |
| Parser_FunctionBodies.cpp | 213-215 |
| Parser_Templates_Function.cpp | 136-137 |
| Parser_Templates_Function.cpp | 382-389 |

Note: this area already has a shared helper (`register_parameters_in_scope()` in
`Parser_FunctionBodies.cpp`). The duplication problem is not lack of infrastructure,
but inconsistent adoption across parsing paths.

### 4. Member Function Lookup

Similar iteration to find matching member function:

| Location | Lines |
|----------|-------|
| Parser_Decl_FunctionOrVar.cpp | 413-433 |
| Parser_Decl_FunctionOrVar.cpp | 1209-1298 |
| Parser_Templates_MemberOutOfLine.cpp | 627-645 |

### 5. Function Body Parsing

Two separate functions with overlapping logic:

| Function | Location | Handles |
|----------|----------|---------|
| parse_function_body() | Parser_FunctionBodies.cpp:442 | Block, try-block, initializer list |
| parse_delayed_function_body() | Parser_FunctionBodies.cpp:185 | Same patterns |

This duplication is more important than it first appears: body/scope setup
divergence has already been a source of behavior bugs. Because
`parse_function_body_with_context()` already exists, finishing that unification is
the most leverage-per-change item in this plan.

## Existing Shared Infrastructure

The codebase already contains unifying constructs:

| Infrastructure | Location | Usage |
|----------------|----------|-------|
| ParsedParameterList | ParserTypes.h:25-28 | Unified parameter list |
| ParsedFunctionHeader | ParserTypes.h:194-204 | Unified header info |
| FunctionParsingContext | ParserTypes.h:183-191 | Context for function parsing |
| parse_parameter_list() | Parser_FunctionHeaders.cpp:14 | Unified parameter parsing |
| parse_declaration_specifiers() | Parser_TypeSpecifiers.cpp | Unified specifier parsing |
| ~~parse_function_body_with_context()~~ | *Removed in Priority 2* | Had zero callers; superseded by `FunctionParsingScopeGuard` |
| register_parameters_in_scope() | Parser_FunctionBodies.cpp:171 | Parameter registration helper |
| validate_signature_match() | Parser_FunctionBodies.cpp:544 | Signature validation helper |
| skip_function_trailing_specifiers() | Parser_Expr_BinaryPrecedence.cpp:870 | Skip CV/noexcept/etc |
| parse_constructor_exception_specifier() | Parser_Expr_BinaryPrecedence.cpp:838 | Unified noexcept parsing |
| parse_trailing_requires_clause() | Parser_Expr_BinaryPrecedence.cpp:970 | Unified requires clause |

## Proposed Refactoring

### Priority 1: Unify Delayed Function Body Entry ✅ IMPLEMENTED

Refactor `parse_delayed_function_body()` so it reuses
`parse_function_body_with_context()` (or a shared lower-level helper directly under
it) for scope setup, `this` injection, parameter registration, and body parsing.

Why first:
- This is the highest-value behavior-preserving cleanup already supported by
  existing infrastructure.
- It reduces the chance of scope/context drift between immediate and delayed
  parsing paths.
- It creates the right seam for the smaller extractions below instead of
  introducing one-off helpers first.

**Affected locations:** `Parser_FunctionBodies.cpp` primary, with follow-up call-site
cleanup in declaration/template entry points.

#### Implementation Notes (March 2026)

**Shared helper: `setup_member_function_context()`**
Enhanced to handle all member-function entry work in one place:
1. Push `member_function_context_stack_`
2. Register member functions in symbol table (`register_member_functions_in_scope()`)
3. Inject `this` pointer into the symbol table (C++20 [class.this])

Both `parse_function_body_with_context()` and `parse_delayed_function_body()` now
call this single helper, so `this` injection, member-function registration, and
context-stack management cannot drift between immediate and delayed paths.

**`parse_function_body_with_context()` changes:**
- Replaced inline `this` injection code with `setup_member_function_context()` call
- Replaced inline parameter registration loop with `register_parameters_in_scope()`
- Added `member_function_context_stack_` pop on cleanup

**`parse_delayed_function_body()` changes:**
- Replaced manual `gSymbolTable.enter_scope()`/`exit_scope()` with RAII
  `FlashCpp::SymbolTableScope`
- Added local RAII `MemberContextCleanup` guard that restores `current_function_`
  and pops `member_function_context_stack_` on destruction
- Removed 7 duplicated manual cleanup blocks (previously on every error return)
- Gained `this` injection (was previously missing in the delayed path)

**`Parser_Templates_Class.cpp` partial-specialization path:**
- Replaced ~45 lines of manual scope/context/this/param setup with a single
  `parse_delayed_function_body()` call, eliminating the last open-coded delayed
  body parsing path.

**Regression tests added:**
- `test_delayed_ctor_init_list_ret0.cpp` — constructor with member initializer list
- `test_delayed_member_func_this_ret0.cpp` — member function using `this` pointer
- `test_delayed_out_of_line_member_ret0.cpp` — out-of-line member function definitions
- `test_delayed_template_member_ret0.cpp` — template class member functions

**Metrics after implementation:**
- 1627 tests pass, 0 fail, 67 expected-fail
- Net lines changed: −60 (93 added, 153 removed)

### Priority 2: Replace Standalone 'this' Setup with a Unified Function-Entry Helper ✅ IMPLEMENTED

Instead of a narrow helper that only injects `this`, create one shared helper or
RAII object that owns **all** per-function parser entry work:

- enter function scope
- inject `this` when appropriate
- register parameters
- push/pop member-function context
- set/restore `current_function_`

Implemented shape (differs from suggested in that it takes direct struct info instead
of `FunctionParsingContext`/`ParsedFunctionHeader`, which not all call sites have):
```cpp
class FunctionParsingScopeGuard {
public:
    FunctionParsingScopeGuard(Parser& parser,
                             bool is_member,
                             StructDeclarationNode* struct_node,
                             StringHandle struct_name,
                             TypeIndex struct_type_index,
                             const std::vector<ASTNode>& params,
                             const FunctionDeclarationNode* current_function);
    ~FunctionParsingScopeGuard();
};
```

**Implementation Notes (March 2026)**

**`FunctionParsingScopeGuard`:**
- Declared in `ParserScopeGuards.h`; constructor/destructor implemented as inline functions
  in `Parser.h` (after Parser class definition), same pattern as `FunctionScopeGuard::injectThisPointer()`.
- Parser declares `friend class FlashCpp::FunctionParsingScopeGuard` for private-member access.
- Constructor: enters scope, saves `current_function_`, sets it to the passed value, calls
  `setup_member_function_context()` for member functions, registers parameters.
- Destructor: pops member-function context if member, restores `current_function_`, exits scope.

**`parse_function_body_with_context()` decision:**
Removed (zero callers). `FunctionParsingScopeGuard` is the correct replacement abstraction.
The pre-body checks (`= default`, `= delete`, `= 0`, `;`) belong in each call site's
parsing flow, not in a unified body-entry function.

**`parse_delayed_function_body()` changes:**
- Replaced `FlashCpp::SymbolTableScope func_scope` + local `MemberContextCleanup` struct
  (the Priority-1 placeholder) with a single `FlashCpp::FunctionParsingScopeGuard`.
- Pre-determines `func_node` and `params_ptr` before constructing the guard so the
  correct `current_function_` value can be passed at construction time.

**`Parser_Decl_FunctionOrVar.cpp` changes:**
- Inline-member delayed site (~line 495): removed ~30 lines of dead code (scope/context/this/params
  setup that was entered and immediately discarded before `skip_function_body()`).
- Out-of-line member function site (~line 550): converted to `FunctionParsingScopeGuard`;
  also fixes `struct_info->total_size*8` → pointer-size 64-bit bug.
- Out-of-line constructor/destructor site (~line 1245): converted to `FunctionParsingScopeGuard`;
  removes 9 scattered `member_function_context_stack_.pop_back()` manual cleanup calls;
  fixes `struct_info->total_size*8` → pointer-size 64-bit bug.
- Free-function site (~line 783): converted to `FunctionParsingScopeGuard`;
  removes manual `current_function_` save/restore pattern.

**Dead code removed as part of Priority 2b:**
- `FunctionScopeGuard` (Phase 3 predecessor, `ParserScopeGuards.h:163-209`): fully superseded
  by `FunctionParsingScopeGuard`. Had zero callers; deleted along with its inline implementations
  `addParameters()` and `injectThisPointer()` from `Parser.h`.
- `CombinedTemplateAndFunctionScope` (`ParserScopeGuards.h:222-263`): composite of
  `TemplateParameterScope` + `FunctionScopeGuard`. Zero callers; deleted.

**`current_function_` correctness:**
- Set via `FunctionParsingScopeGuard` for all main parser sites (free functions, member
  functions, constructors/destructors, delayed bodies).
- Template instantiation sites still manually save/restore it (not yet converted to
  `FunctionParsingScopeGuard`), but each does so correctly.
- Used for `__func__` / `__PRETTY_FUNCTION__` built-in identifiers
  (`Parser_Expr_PrimaryExpr.cpp`). `__FUNCTION__` is pre-processed to `__func__`
  by the preprocessor (`FileReader_Macros.cpp:1937`), so it is covered too.
- Also used for braced initializer return-type resolution in expression context
  (`Parser_Expr_PrimaryExpr.cpp:5638`).

**Metrics after Priority 2 + 2b:**
- 1630 tests pass, 0 fail, 67 expected-fail
- Net effect: removed ~120 lines of dead code and open-coded duplicated function-entry boilerplate

### Priority 3: Extract Constructor Detection ✅ IMPLEMENTED

Create helper function:
```cpp
struct ConstructorLookaheadResult {
    bool detected = false;
    bool is_destructor = false;
};
ConstructorLookaheadResult lookahead_constructor_or_destructor(std::string_view class_name);
```

**Affected locations:** 2 call sites

#### Implementation Notes (March 2026)

**`consume_constructor_or_destructor_prefix()` and `lookahead_constructor_or_destructor()`:**
- Declared in `Parser.h` (struct + two methods); implemented in `Parser_Decl_FunctionOrVar.cpp`.
- `consume_constructor_or_destructor_prefix()` is the core: advances past
  `ClassName [<...>] :: [~]` and checks that the next token is `ClassName (`.
  On success, leaves the position just before the second `ClassName` so the caller
  can consume it. On failure, position is unspecified (caller must save/restore).
- `lookahead_constructor_or_destructor()` wraps the consume variant with
  save/restore — pure lookahead with no side effects.
- Both handle optional template arguments between the class name and `::`, so they
  detect both non-template (`Foo::Foo(`) and template (`Foo<T>::Foo(`) patterns.

**Site 1 (`Parser_Decl_FunctionOrVar.cpp`) changes:**
- Replaced ~27 lines of manual save/advance/check/restore lookahead with a single
  `lookahead_constructor_or_destructor()` call (3 lines).
- The namespace-qualified type lookup (building `qualified_class_name` from
  `current_namespace_handle`) stays in the call site since it is specific to the
  non-template declaration path.

**Site 2 (`Parser_Templates_MemberOutOfLine.cpp`) changes:**
- Uses `consume_constructor_or_destructor_prefix()` directly as an early check for
  the simple `ClassName[<...>]::[~]ClassName(` pattern before the complex
  namespace/nested-class navigation loop. On success, the prefix is already consumed
  (no double scanning). On failure, position is restored and the complex path runs.
- The complex namespace-qualified loop, template-argument skipping, and nested-class
  member handling remain unchanged — the helper is not forced into those paths.
- Restructured using a `past_scope_op` flag so that both the simple and complex
  paths share a single copy of the nested-class and constructor/destructor handling
  code, avoiding any duplication of the ~90-line handling block.

**Metrics after Priority 3:**
- 1633 tests pass, 0 fail, 67 expected-fail
- Net effect: constructor detection lookahead extracted into shared helper; site 1
  reduced by ~24 lines; site 2 restructured to use helper for simple case

### Priority 4: Member Function Lookup Helper

Create helper function:
```cpp
const StructMemberFunction* find_member_function_by_signature(
    TypeIndex struct_type_index,
    StringHandle name,
    const MemberQualifiers& quals,
    size_t param_count);
```

Prefer a `TypeIndex` / `StructTypeInfo`-based API over a
`StructDeclarationNode`-only API, so the helper works for:

- template instantiations
- inherited members
- delayed/lazy member materialization paths
- code that no longer has a convenient AST declaration node

If signature matching grows beyond simple arity/qualifier checks, consider passing
the already-parsed header or a small comparison struct instead of only `param_count`.

**Affected locations:** 3 primary call sites, with likely follow-up reuse elsewhere.

### Priority 5: Consistent Function Header Parsing

Refactor template and constructor parsing paths to use `ParsedFunctionHeader` structure consistently.

**Affected locations:** Multiple paths in Parser_Templates_Function.cpp, Parser_Templates_Class.cpp

## Out of Scope

The following are intentionally NOT part of this code sharing plan:

### Variadic Parameters (`...`)

Variadic parameters are already handled consistently through shared infrastructure:

- **Location:** `Parser_FunctionHeaders.cpp:41-60`
- **Mechanism:** `ParsedParameterList.is_variadic` field
- **Status:** Already unified - `parse_parameter_list()` is used by all function parsing paths

### Function Pointers (`int (*fp)(int)`)

Function pointers are **types**, not function definitions:

- **Location:** `Parser_Decl_DeclaratorCore.cpp`
- **Mechanism:** `parse_declarator()` creates `Type::FunctionPointer`
- **Status:** Already shared - declarator system handles all type declarations uniformly

### Member Function Pointers (`int (C::*mp)(int)`)

Member function pointers are **types**, handled in declarator parsing:

- **Location:** `Parser_Decl_DeclaratorCore.cpp:244-289`
- **Status:** Already shared - same declarator system

### Expression/Postfix Call Parsing Refactors

Do **not** mix expression-call parsing cleanup into this PR series.

The duplication around callable-object `operator()` resolution, function-pointer
call classification, and postfix/member-call construction overlaps current
semantic-analysis / codegen cleanup work and should remain a separate PR stream.
Keeping that out of scope reduces behavior risk and makes this plan a true parser
code-sharing refactor rather than a semantic behavior change.

### Summary

This code sharing plan addresses **function definition parsing** (constructors,
destructors, member functions, templates). Variadic parameters and function
pointers fall under **type declaration parsing**, which already uses shared
declarator infrastructure. Expression/postfix call parsing should stay separate.

## Metrics

| Metric | Before Priority 1 | After Priority 1 | After Priority 2 | After Priority 3 | Target |
|--------|--------------------|-------------------|------------------|------------------|--------|
| Duplicate 'this' pointer blocks | 4 | 3 (setup_member_function_context covers 2 paths) | 0 (FunctionParsingScopeGuard covers all) | 0 | 0 |
| Duplicate constructor lookahead | 2 | 2 | 2 | 0 (lookahead_constructor_or_destructor shared) | 0 |
| Duplicate parameter registration | 7 | 5 (delayed + immediate + partial-spec now share) | 3 (remaining template paths) | 3 | 1 |
| Shared infrastructure utilization | Partial | Improved (delayed path unified) | Consistent (all main paths unified) | Consistent | Consistent |

## Exit Criteria

The refactor is complete when:

- all function-definition entry paths use the same scope/body entry mechanism
- no function-definition path open-codes `this` injection
- no function-definition path open-codes parameter registration
- delayed and non-delayed function body parsing share the same core body setup flow
- helper APIs are based on the data actually available in template/delayed paths
  (`TypeIndex` / `StructTypeInfo`), not only on convenient AST forms
- no dead/superseded scope guard classes remain in `ParserScopeGuards.h`

## Notes

- Existing Phase 1-5 infrastructure was added to unify parsing but is not consistently applied across all code paths.
- The refactoring maintains existing behavior while reducing maintenance burden.
- Each refactoring should be tested independently.
