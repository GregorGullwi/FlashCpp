# Refactoring Plan: Unifying Function Parsing Logic

## Problem Statement
The current compiler implementation suffers from code duplication across several parsing methods:
- `parse_declaration_or_function_definition` (handles global functions, variables, and out-of-line member functions)
- `parse_function_declaration` (handles inline function definitions)
- `parse_member_function_template` (handles template members)
- `try_parse_out_of_line_template_member` (handles out-of-line template members)

This duplication leads to:
1.  **Inconsistency**: Fixes applied to one parser (e.g., better attribute handling) might be missed in others.
2.  **Bugs**: Complex logic like signature validation for out-of-line members is repeated, increasing the surface area for errors.
3.  **Maintenance Burden**: Adding new C++ features (like `requires` clauses or new attributes) requires updating multiple locations.

## Proposed Solution

The goal is to extract common logic into reusable helper methods within the `Parser` class.

### 1. Unified Function Signature Parsing

Create a `FunctionSignature` struct and a `parse_function_signature` method.

```cpp
struct ParsedFunctionSignature {
    TypeSpecifierNode return_type;
    Token name_token;
    std::vector<ASTNode> parameters;
    CVQualifier cv_qualifiers = CVQualifier::None;
    bool is_vararg = false;
    // ... other metadata like attributes, noexcept, etc.
};

// Parses: name(params) cv-quals exception-spec attributes
ParseResult parse_function_signature(TypeSpecifierNode return_type);
```

This method will handle:
- Parameter list parsing `(int a, float b)`
- CV-qualifiers `const volatile`
- Ref-qualifiers `&`, `&&`
- Exception specifications `noexcept`
- Trailing return types `-> int`

### 2. Unified Function Body Parsing

Create a `parse_function_body` method that handles the common setup for entering a function scope.

```cpp
struct FunctionBodyContext {
    FunctionDeclarationNode* func_node;
    StructTypeInfo* parent_struct = nullptr; // For member functions
    bool is_constructor = false;
    bool is_destructor = false;
};

ParseResult parse_function_body(const FunctionBodyContext& context);
```

This method will:
- Enter `ScopeType::Function`.
- Register parameters in the symbol table.
- Register `this` pointer if `parent_struct` is provided.
- Parse the block `{ ... }`.
- Handle constructor initializer lists (if applicable).
- Exit scope.

### 3. Centralized Signature Validation

Extract the logic that compares an out-of-line definition with its declaration.

```cpp
bool validate_signature_match(
    const FunctionDeclarationNode& definition, 
    const FunctionDeclarationNode& declaration,
    std::string_view class_name
);
```

This will encapsulate the complex type comparison logic (ignoring top-level const on value parameters, checking pointer/ref cv-qualifiers, etc.).

### 4. Refactoring `parse_declaration_or_function_definition`

Refactor the main entry point to use these helpers.

**Current Flow:**
1. Parse attributes.
2. Parse type and name.
3. Check for `::` (out-of-line member).
   - If yes, duplicate logic for parsing params, validating, parsing body.
4. Check for `(` (function).
   - If yes, call `parse_function_declaration`.
5. Else, variable declaration.

**New Flow:**
1. Parse attributes.
2. Parse type and name.
3. Check for `::`.
   - If yes, call `parse_out_of_line_member_definition` (new helper).
4. Check for `(`.
   - If yes, call `parse_function_definition` (uses `parse_function_signature` + `parse_function_body`).
5. Else, variable declaration.

### 5. Template Deduplication

Ensure `parse_template_declaration` and `parse_member_function_template` share the same parameter list parsing logic (`parse_template_parameter_list`).

## Implementation Steps

1.  **Step 1**: Implement `ParsedFunctionSignature` struct and `parse_function_signature` helper.
2.  **Step 2**: Refactor `parse_function_declaration` to use `parse_function_signature`.
3.  **Step 3**: Implement `validate_signature_match` helper.
4.  **Step 4**: Refactor the out-of-line member logic in `parse_declaration_or_function_definition` to use `parse_function_signature` and `validate_signature_match`.
5.  **Step 5**: Implement `parse_function_body` and switch all parsers to use it.
6.  **Step 6**: Verify with existing tests (`FlashCppTest`).

## Benefits

-   **Single Source of Truth**: Parsing logic for function signatures exists in one place.
-   **Reduced Complexity**: The main parsing loop becomes a high-level orchestrator rather than a monolithic function.
-   **Easier Extensibility**: Adding support for new syntax (e.g., contracts) will be easier.
