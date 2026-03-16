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
| parse_function_body_with_context() | Parser_FunctionBodies.cpp:17 | Unified body parsing |
| register_parameters_in_scope() | Parser_FunctionBodies.cpp:171 | Parameter registration helper |
| validate_signature_match() | Parser_FunctionBodies.cpp:544 | Signature validation helper |
| skip_function_trailing_specifiers() | Parser_Expr_BinaryPrecedence.cpp:870 | Skip CV/noexcept/etc |
| parse_constructor_exception_specifier() | Parser_Expr_BinaryPrecedence.cpp:838 | Unified noexcept parsing |
| parse_trailing_requires_clause() | Parser_Expr_BinaryPrecedence.cpp:970 | Unified requires clause |

## Proposed Refactoring

### Priority 1: Unify Delayed Function Body Entry

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

### Priority 2: Replace Standalone 'this' Setup with a Unified Function-Entry Helper

Instead of a narrow helper that only injects `this`, create one shared helper or
RAII object that owns **all** per-function parser entry work:

- enter function scope
- inject `this` when appropriate
- register parameters
- push/pop member-function context
- set/restore `current_function_`

Suggested shape:
```cpp
class FunctionParsingScopeGuard {
public:
    FunctionParsingScopeGuard(Parser& parser,
                             const FunctionParsingContext& ctx,
                             const ParsedFunctionHeader& header,
                             FunctionDeclarationNode* current_function);
    ~FunctionParsingScopeGuard();
};
```

If a full guard is too much for one PR, a helper function returning a small
state/cleanup object is still preferable to a `setup_this_pointer(...)` helper,
because the current duplication is about **entry protocol**, not just one symbol.

**Affected locations:** all current ad-hoc function scope setup blocks, including
`Parser_Decl_FunctionOrVar.cpp` and `Parser_FunctionBodies.cpp`.

### Priority 3: Extract Constructor Detection

Create helper function:
```cpp
std::optional<ConstructorInfo> detect_constructor_or_destructor_pattern(
    std::string_view struct_name, 
    bool is_template_context);
```

**Affected locations:** 2 (estimated 80 lines total)

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

| Metric | Current | Target |
|--------|---------|--------|
| Duplicate 'this' pointer blocks | 4 | 1 |
| Duplicate constructor lookahead | 2 | 1 |
| Duplicate parameter registration | 7 | 1 |
| Shared infrastructure utilization | Partial | Consistent |

## Exit Criteria

The refactor is complete when:

- all function-definition entry paths use the same scope/body entry mechanism
- no function-definition path open-codes `this` injection
- no function-definition path open-codes parameter registration
- delayed and non-delayed function body parsing share the same core body setup flow
- helper APIs are based on the data actually available in template/delayed paths
  (`TypeIndex` / `StructTypeInfo`), not only on convenient AST forms

## Notes

- Existing Phase 1-5 infrastructure was added to unify parsing but is not consistently applied across all code paths.
- The refactoring maintains existing behavior while reducing maintenance burden.
- Each refactoring should be tested independently.
