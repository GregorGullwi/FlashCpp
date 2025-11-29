# Comprehensive Parser Refactoring Plan

## Executive Summary

This document presents a comprehensive refactoring plan to eliminate code duplication across function parsing in the FlashCpp C++20 compiler. The plan provides a safe, incremental approach with concrete implementation guidance.

**Key Statistics:**
- Parser.cpp: ~17,075 lines
- Estimated duplicated code: 800-1,200 lines (5-7%)
- Expected reduction after refactoring: 30-40% of duplicated sections

---

## 1. Current State Analysis

### 1.1 Identified Duplication Hotspots

| Area | Locations | Lines Duplicated | Severity |
|------|-----------|------------------|----------|
| Parameter list parsing | 5+ locations | ~200 lines | High |
| Template parameter registration/cleanup | 3+ locations | ~80 lines | High |
| Function scope setup | 4+ locations | ~150 lines | Medium |
| Member function specifier parsing | 3+ locations | ~120 lines | Medium |
| 'this' pointer injection | 2+ locations | ~40 lines | Low |
| Delayed body context setup | 4+ locations | ~200 lines | High |

### 1.2 Primary Parsing Entry Points

```
parse_function_declaration()           # Lines 5792-5921 - Regular functions
parse_struct_declaration()             # Lines ~2100-3500 - Members, ctors, dtors
parse_template_declaration()           # Lines 11404-11600+ - Function/class templates
parse_member_function_template()       # Lines 13453-13575 - Member function templates
parse_declaration_or_function_definition()  # Lines ~1200-1800 - Out-of-line members
try_parse_out_of_line_template_member()    # Lines ~16500+ - Out-of-line template members
```

### 1.3 Core Duplications in Detail

#### A. Parameter List Parsing

**Pattern repeated in 5+ locations:**
```cpp
while (!consume_punctuator(")"sv)) {
    // Check for variadic parameter (...)
    if (peek_token().has_value() && peek_token()->value() == "...") {
        consume_token();
        func_ref.set_is_variadic(true);
        // Validate calling convention...
        break;
    }

    ParseResult type_and_name_result = parse_type_and_name();
    if (type_and_name_result.is_error()) {
        return type_and_name_result;
    }

    if (auto node = type_and_name_result.node()) {
        func_ref.add_parameter_node(*node);
    }

    // Handle default values, comma, closing paren...
}
```

Variations found in:
- `parse_function_declaration()` (lines 5812-5891)
- Constructor parsing in `parse_struct_declaration()` (lines ~2399-2416)
- Out-of-line member parsing (lines ~1445-1463)
- Template out-of-line member (lines ~16568-16584)
- Template member function parsing (lines ~13500+)

#### B. Template Parameter Registration and Cleanup

**Inconsistent cleanup patterns:**

Location 1 (with RAII):
```cpp
std::vector<TypeInfo*> template_type_infos;
// ... registration code ...

struct TemplateParamCleanup {
    std::vector<TypeInfo*>& type_infos;
    ~TemplateParamCleanup() {
        for (const auto* type_info : type_infos) {
            gTypesByName.erase(type_info->name_);
        }
    }
} cleanup_guard{template_type_infos};
```

Location 2 (manual cleanup - error-prone):
```cpp
std::vector<TypeInfo*> template_type_infos;
// ... registration code ...

// Manual cleanup on every return path
for (const auto* type_info : template_type_infos) {
    gTypesByName.erase(type_info->name_);
}
```

#### C. Delayed Function Body Setup

**Pattern repeated for members, constructors, destructors, templates:**
```cpp
gSymbolTable.enter_scope(ScopeType::Function);
current_function_ = ...;
member_function_context_stack_.push_back({
    delayed.struct_name,
    delayed.struct_type_index,
    delayed.struct_node
});

// Add member functions to symbol table
for (const auto& member_func : delayed.struct_node->member_functions()) {
    // ... registration ...
}

// Add parameters to symbol table
for (const auto& param : delayed.func_node->parameter_nodes()) {
    // ... registration ...
}

auto block_result = parse_block();
// ... cleanup ...
```

---

## 2. Proposed Architecture

### 2.1 New Type Definitions

```cpp
// ===== src/ParserTypes.h (new file) =====

#pragma once
#include "AstNodeTypes.h"
#include <vector>
#include <optional>
#include <string_view>

namespace FlashCpp {

// Unified representation of what kind of function we're parsing
enum class FunctionKind {
    Free,           // Global or namespace-scope function
    Member,         // Non-static member function
    StaticMember,   // Static member function
    Constructor,    // Constructor
    Destructor,     // Destructor
    Lambda          // Lambda expression (future)
};

// CV and ref qualifiers for member functions
struct MemberQualifiers {
    bool is_const = false;
    bool is_volatile = false;
    bool is_lvalue_ref = false;   // &
    bool is_rvalue_ref = false;   // &&
};

// Function specifiers (can appear after parameters)
struct FunctionSpecifiers {
    bool is_virtual = false;
    bool is_override = false;
    bool is_final = false;
    bool is_pure_virtual = false;  // = 0
    bool is_defaulted = false;     // = default
    bool is_deleted = false;       // = delete
    bool is_noexcept = false;
    std::optional<ASTNode> noexcept_expr;  // For noexcept(expr)
    bool is_implicit = false;      // Compiler-generated (implicit copy ctor, operator=, etc.)
};

// Storage and linkage specifiers
struct StorageSpecifiers {
    bool is_static = false;
    bool is_inline = false;
    bool is_constexpr = false;
    bool is_consteval = false;
    bool is_constinit = false;
    bool is_extern = false;
    Linkage linkage = Linkage::None;
    CallingConvention calling_convention = CallingConvention::Default;
};

// Context for parsing a function (where it lives)
struct FunctionParsingContext {
    FunctionKind kind = FunctionKind::Free;
    std::string_view parent_struct_name;      // For members
    size_t parent_struct_type_index = 0;      // Type index of parent struct
    StructDeclarationNode* parent_struct = nullptr;
    bool is_out_of_line = false;              // A::f defined outside class
    std::vector<std::string_view> template_params;  // Enclosing template params
    AccessSpecifier access = AccessSpecifier::Public;
};

// Result of parsing a parameter list
struct ParsedParameterList {
    std::vector<ASTNode> parameters;
    bool is_variadic = false;
};

// Result of parsing function header (everything except the body)
struct ParsedFunctionHeader {
    TypeSpecifierNode* return_type = nullptr;
    Token name_token;
    ParsedParameterList params;
    MemberQualifiers member_quals;
    FunctionSpecifiers specifiers;
    StorageSpecifiers storage;
    std::vector<ASTNode> template_params;       // If function template
    std::optional<ASTNode> requires_clause;     // C++20 requires
    std::optional<ASTNode> trailing_return_type;
};

} // namespace FlashCpp
```

### 2.2 RAII Scope Guards

```cpp
// ===== src/ParserScopeGuards.h (new file, header-only) =====

#pragma once
#include "SymbolTable.h"
#include "TypeInfo.h"
#include <vector>

namespace FlashCpp {

// RAII guard for template parameter type registration
class TemplateParameterScope {
public:
    TemplateParameterScope() = default;
    
    // Register a type template parameter
    void registerTypeParam(std::string_view name) {
        auto& type_info = gTypeInfo.emplace_back(
            std::string(name), Type::UserDefined, gTypeInfo.size()
        );
        gTypesByName.emplace(type_info.name_, &type_info);
        registered_types_.push_back(&type_info);
        param_names_.push_back(name);
    }
    
    // Register a non-type template parameter (just track name)
    void registerNonTypeParam(std::string_view name) {
        param_names_.push_back(name);
    }
    
    const std::vector<std::string_view>& paramNames() const { 
        return param_names_; 
    }
    
    ~TemplateParameterScope() {
        for (const auto* type_info : registered_types_) {
            gTypesByName.erase(type_info->name_);
        }
    }
    
    // Non-copyable, non-movable
    TemplateParameterScope(const TemplateParameterScope&) = delete;
    TemplateParameterScope& operator=(const TemplateParameterScope&) = delete;

private:
    std::vector<TypeInfo*> registered_types_;
    std::vector<std::string_view> param_names_;
};

// Forward declare Parser to avoid circular include
class Parser;

// RAII guard for function scope and context
// Note: Implementation uses Parser internals, so methods are defined
// after Parser class definition (in Parser.h or at end of this header)
class FunctionScopeGuard {
public:
    FunctionScopeGuard(
        Parser& parser,
        const FunctionParsingContext& ctx,
        FunctionDeclarationNode* func_node = nullptr,
        SymbolTableStack* scope_stack = nullptr  // Optional: use hierarchical scopes
    ) : parser_(parser), ctx_(ctx), func_node_(func_node), scope_stack_(scope_stack) {
        if (scope_stack_) {
            // Use hierarchical symbol tables (parallel-safe)
            function_scope_ = std::make_unique<SymbolScope>();
            scope_stack_->pushFunctionScope(function_scope_.get());
        } else {
            // Fallback to global symbol table (legacy)
            gSymbolTable.enter_scope(ScopeType::Function);
        }
        scope_entered_ = true;
    }
    
    ~FunctionScopeGuard() {
        if (scope_entered_) {
            if (scope_stack_) {
                scope_stack_->popFunctionScope();
            } else {
                gSymbolTable.exit_scope();
            }
        }
    }
    
    // Inject 'this' pointer for member functions - defined inline in Parser.h
    // after Parser class definition to access Parser internals
    void injectThisPointer();
    
    // Register all parameters in the symbol table
    void registerParameters(const std::vector<ASTNode>& params) {
        for (const auto& param : params) {
            if (param.is<DeclarationNode>()) {
                const auto& param_decl = param.as<DeclarationNode>();
                if (scope_stack_) {
                    scope_stack_->insert(param_decl.identifier_token().value(), param);
                } else {
                    gSymbolTable.insert(param_decl.identifier_token().value(), param);
                }
            }
        }
    }
    
    // Non-copyable
    FunctionScopeGuard(const FunctionScopeGuard&) = delete;
    FunctionScopeGuard& operator=(const FunctionScopeGuard&) = delete;

private:
    Parser& parser_;
    FunctionParsingContext ctx_;
    FunctionDeclarationNode* func_node_;
    SymbolTableStack* scope_stack_ = nullptr;
    std::unique_ptr<SymbolScope> function_scope_;  // Owned if using hierarchical
    bool scope_entered_ = false;
};

} // namespace FlashCpp
```

### 2.3 Hierarchical Symbol Tables

**Note:** The current `SymbolTable` implementation already supports nested namespaces and classes comprehensively. It uses `NamespacePath` (vector of string components) for qualified lookups, supports arbitrary nesting depth, and handles namespace aliases. The proposed `ScopedSymbolTable` extends this with per-task isolation for parallel parsing.

Replace the global `gSymbolTable` with a hierarchy that supports nesting and enables parallel parsing:

```cpp
// ===== src/ScopedSymbolTable.h (header-only) =====

#pragma once
#include <unordered_map>
#include <vector>
#include <optional>
#include <string_view>
#include <memory>

namespace FlashCpp {

// Individual symbol scope for one level (function, class, namespace, or global)
class SymbolScope {
public:
    void insert(std::string_view name, ASTNode node) {
        symbols_[name] = node;
    }
    
    std::optional<ASTNode> lookup(std::string_view name) const {
        auto it = symbols_.find(name);
        return (it != symbols_.end()) ? std::optional{it->second} : std::nullopt;
    }
    
    bool contains(std::string_view name) const {
        return symbols_.find(name) != symbols_.end();
    }
    
    void clear() { symbols_.clear(); }
    
private:
    std::unordered_map<std::string_view, ASTNode> symbols_;
};

// Stack of symbol scopes with hierarchical lookup
// Supports arbitrary nesting: namespace { namespace { class { class { function } } } }
class SymbolTableStack {
public:
    explicit SymbolTableStack(SymbolScope* global_scope) 
        : global_scope_(global_scope) {}
    
    // Lookup with proper C++ scoping rules (innermost to outermost)
    std::optional<ASTNode> lookup(std::string_view name) const {
        // 1. Function scope (innermost)
        if (function_scope_ && function_scope_->contains(name)) {
            return function_scope_->lookup(name);
        }
        
        // 2. Class scopes (innermost to outermost) - supports nested classes
        for (auto it = class_scopes_.rbegin(); it != class_scopes_.rend(); ++it) {
            if ((*it)->contains(name)) {
                return (*it)->lookup(name);
            }
        }
        
        // 3. Namespace scopes (innermost to outermost) - supports nested namespaces
        for (auto it = namespace_scopes_.rbegin(); it != namespace_scopes_.rend(); ++it) {
            if ((*it)->contains(name)) {
                return (*it)->lookup(name);
            }
        }
        
        // 4. Global scope (outermost)
        return global_scope_->lookup(name);
    }
    
    // Insert into current scope (function > class > namespace > global)
    void insert(std::string_view name, ASTNode node) {
        if (function_scope_) {
            function_scope_->insert(name, node);
        } else if (!class_scopes_.empty()) {
            class_scopes_.back()->insert(name, node);
        } else if (!namespace_scopes_.empty()) {
            namespace_scopes_.back()->insert(name, node);
        } else {
            global_scope_->insert(name, node);
        }
    }
    
    // Scope management - supports arbitrary nesting depth
    void pushNamespaceScope(SymbolScope* scope) { namespace_scopes_.push_back(scope); }
    void popNamespaceScope() { if (!namespace_scopes_.empty()) namespace_scopes_.pop_back(); }
    
    void pushClassScope(SymbolScope* scope) { class_scopes_.push_back(scope); }
    void popClassScope() { if (!class_scopes_.empty()) class_scopes_.pop_back(); }
    
    void pushFunctionScope(SymbolScope* scope) { function_scope_ = scope; }
    void popFunctionScope() { function_scope_ = nullptr; }
    
    // Access to current scopes for advanced use
    SymbolScope* currentFunctionScope() { return function_scope_; }
    SymbolScope* currentClassScope() { return class_scopes_.empty() ? nullptr : class_scopes_.back(); }
    SymbolScope* currentNamespaceScope() { return namespace_scopes_.empty() ? nullptr : namespace_scopes_.back(); }
    SymbolScope* globalScope() { return global_scope_; }
    
    size_t classNestingDepth() const { return class_scopes_.size(); }
    size_t namespaceNestingDepth() const { return namespace_scopes_.size(); }
    
private:
    SymbolScope* global_scope_;                    // Always present, not owned
    std::vector<SymbolScope*> namespace_scopes_;   // Stack for nested namespaces
    std::vector<SymbolScope*> class_scopes_;       // Stack for nested classes
    SymbolScope* function_scope_ = nullptr;        // Current function (only one active)
};

// Owns symbol scopes and provides the stack interface
// One per Parser instance, or one per parallel parsing task
class ScopedSymbolTable {
public:
    ScopedSymbolTable() : stack_(&global_scope_) {}
    
    // Main lookup interface
    std::optional<ASTNode> lookup(std::string_view name) const {
        return stack_.lookup(name);
    }
    
    void insert(std::string_view name, ASTNode node) {
        stack_.insert(name, node);
    }
    
    // Create and push a new namespace scope
    void enterNamespace(std::string_view name) {
        auto& scope = namespace_scopes_[std::string(name)];
        stack_.pushNamespaceScope(&scope);
    }
    
    void exitNamespace() {
        stack_.popNamespaceScope();
    }
    
    // Create and push a new class scope
    void enterClass(std::string_view name) {
        auto& scope = class_scopes_[std::string(name)];
        stack_.pushClassScope(&scope);
    }
    
    void exitClass() {
        stack_.popClassScope();
    }
    
    // Get the stack for passing to FunctionScopeGuard
    SymbolTableStack* getStack() { return &stack_; }
    
private:
    SymbolScope global_scope_;
    std::unordered_map<std::string, SymbolScope> namespace_scopes_;
    std::unordered_map<std::string, SymbolScope> class_scopes_;
    mutable SymbolTableStack stack_;
};

} // namespace FlashCpp
```

**Key features:**
- **Arbitrary nesting**: `std::vector` stacks for namespaces and classes
- **Correct lookup order**: function → class (inner to outer) → namespace (inner to outer) → global
- **Parallel-ready**: Each parsing task can have its own `ScopedSymbolTable`
- **Backward compatible**: `FunctionScopeGuard` falls back to `gSymbolTable` if no stack provided

### 2.4 Unified Helper Methods

Add these methods to the `Parser` class:

```cpp
// ===== In Parser.h, private section =====

// Core parameter parsing - shared by all function types
ParseResult parseParameterList(ParsedParameterList& out_params);

// Parse function trailing specifiers (const, &, &&, noexcept, etc.)
ParseResult parseFunctionTrailingSpecifiers(
    MemberQualifiers& out_quals,
    FunctionSpecifiers& out_specs
);

// Parse the complete function header (return type through specifiers)
ParseResult parseFunctionHeader(
    const FunctionParsingContext& ctx,
    ParsedFunctionHeader& out_header
);

// Parse function body with proper scope setup
ParseResult parseFunctionBodyWithContext(
    const FunctionParsingContext& ctx,
    const ParsedFunctionHeader& header,
    ASTNode& out_body
);

// Setup delayed parsing entry for inline member functions
void queueDelayedBody(
    const FunctionParsingContext& ctx,
    const ParsedFunctionHeader& header,
    FunctionDeclarationNode* func_node,
    TokenPosition body_start
);

// Validate out-of-line definition matches declaration
bool validateSignatureMatch(
    const FunctionDeclarationNode& declaration,
    const ParsedFunctionHeader& definition_header,
    std::string& out_error
);

// Template parameter list parsing (already exists, ensure reuse)
ParseResult parse_template_parameter_list(std::vector<ASTNode>& out_params);
```

### 2.5 Special Function Types to Handle

The unified infrastructure must handle these special cases:

| Function Type | Special Handling Required |
|---------------|---------------------------|
| **Operator overloads** | Name is `operator+`, `operator()`, etc. - parse after `operator` keyword |
| **Conversion functions** | `operator int()` - no return type in declaration, name includes target type |
| **User-defined literals** | `operator""_suffix` - special naming |
| **Friend functions** | Declared inside class but not members - different scope handling |
| **Static member functions** | No `this` pointer injection |
| **Defaulted comparison operators** | `operator<=>(const T&) = default` (C++20) |

These should work with the unified `parseFunctionHeader()` by:
1. Detecting `operator` keyword early
2. Setting appropriate flags in `FunctionParsingContext`
3. Using `FunctionKind::Member` vs `FunctionKind::StaticMember` appropriately

```cpp
// Extended FunctionKind enum to handle edge cases
enum class FunctionKind {
    Free,           // Global or namespace-scope function
    Member,         // Non-static member function  
    StaticMember,   // Static member function (no 'this')
    Constructor,    // Constructor
    Destructor,     // Destructor
    Operator,       // Operator overload (can be member or free)
    Conversion,     // Conversion operator (operator int())
    Lambda          // Lambda expression (future)
};
```

### 2.6 Logging Infrastructure

> **Status: IMPLEMENTED** - The logging system is now available in `src/Log.h`.

When writing new code or modifying existing code, use the `FLASH_LOG` macro instead of direct `std::cerr` or `std::cout` calls:

```cpp
#include "Log.h"
using namespace FlashCpp;

// User-facing messages (no prefix, always enabled)
FLASH_LOG(General, Info, "Output file: ", filename);

// Internal logging with category prefixes and colors
FLASH_LOG(Parser, Error, "Parse error at line ", lineNum);   // Error - Red, goes to stderr
FLASH_LOG(Lexer, Warning, "Unexpected token");               // Warning - Yellow
FLASH_LOG(Templates, Debug, "Instantiating template");       // Debug - No color
FLASH_LOG(Codegen, Trace, "Generating instruction");         // Trace - Blue

**Available categories:** `General`, `Parser`, `Lexer`, `Templates`, `Symbols`, `Types`, `Codegen`, `Scope`, `Mangling`

**Log levels:** `Error` < `Warning` < `Info` < `Debug` < `Trace`

**Migration guideline:** Replace `std::cerr << "error: ..."` with `FLASH_LOG(Parser, Error, ...)` etc.

---

## 3. Implementation Phases

### Phase 0: Preparation (Week 1)

**Goals:**
- ~~Add logging infrastructure~~ ✅ **DONE** - See `src/Log.h`
- Add comprehensive tests for existing behavior
- Create baseline AST dumps for regression comparison
- Set up feature flags for gradual rollout

**Tasks:**

0. ~~**Create `src/Log.h`**~~ ✅ **DONE** - Logging infrastructure is implemented.

1. **Expand test coverage in `tests/FlashCppTest/FlashCppTest.cpp`:**
   ```cpp
   TEST_CASE("Parser:ParameterList:Variadic") { /* ... */ }
   TEST_CASE("Parser:MemberFunction:ConstVolatile") { /* ... */ }
   TEST_CASE("Parser:MemberFunction:OverrideFinal") { /* ... */ }
   TEST_CASE("Parser:Constructor:Defaulted") { /* ... */ }
   TEST_CASE("Parser:Destructor:Virtual") { /* ... */ }
   TEST_CASE("Parser:Template:MemberFunction") { /* ... */ }
   TEST_CASE("Parser:Template:OutOfLine") { /* ... */ }
   ```

2. **Create baseline dumps:**
    ```batch
    REM Create reference outputs for regression testing
    x64\Debug\FlashCpp.exe -v tests\Reference\test_member_init_simple.cpp > baseline_member.txt
    x64\Debug\FlashCpp.exe -v tests\Reference\template_simple.cpp > baseline_templates.txt
    ```
    ```cpp
    // Log the baseline creation
    FLASH_LOG(Parser, Info, "Created baseline dumps for regression testing");
    ```

3. **Add compile-time feature flag:**
   ```cpp
   // In Parser.h (or a dedicated ParserConfig.h)
   #ifndef FLASHCPP_USE_UNIFIED_PARSING
   #define FLASHCPP_USE_UNIFIED_PARSING 0  // Start disabled, enable per-phase
   #endif
   ```
   
   Usage pattern during refactoring:
   ```cpp
   ParseResult Parser::parse_function_declaration(...) {
   #if FLASHCPP_USE_UNIFIED_PARSING
       // New unified code path
       ParsedParameterList params;
       auto result = parseParameterList(params);
       // ...
   #else
       // Original code (unchanged)
       while (!consume_punctuator(")"sv)) {
           // ... existing implementation ...
       }
   #endif
   }
   ```
   
   To test new code: rebuild with `-DFLASHCPP_USE_UNIFIED_PARSING=1` or edit the header.
   The flag gets removed in Phase 8 when the old code is deleted.

**Deliverables:**
- [x] 20+ new test cases covering edge cases
- [x] Baseline dumps for regression comparison
- [x] Feature flag in place

---

### Phase 1: Extract Parameter List Parsing (Week 2)

**Goals:**
- Create unified `parseParameterList()` method
- Replace all duplicated parameter parsing with calls to the new method

**Implementation:**

```cpp
// In Parser.cpp

ParseResult Parser::parseParameterList(ParsedParameterList& out_params) {
    out_params.parameters.clear();
    out_params.is_variadic = false;
    
    if (!consume_punctuator("(")) {
        return ParseResult::error("Expected '(' for parameter list", *current_token_);
    }
    
    while (!consume_punctuator(")")) {
        // Check for variadic parameter (...)
        if (peek_token().has_value() && peek_token()->value() == "...") {
            consume_token();
            out_params.is_variadic = true;
            
            if (!consume_punctuator(")")) {
                return ParseResult::error("Expected ')' after variadic '...'", *current_token_);
            }
            break;
        }
        
        // Parse parameter type and name
        auto param_result = parse_type_and_name();
        if (param_result.is_error()) {
            return param_result;
        }
        
        if (auto node = param_result.node()) {
            out_params.parameters.push_back(*node);
        }
        
        // Parse default value if present
        if (consume_punctuator("=")) {
            auto default_result = parse_expression();
            if (default_result.is_error()) {
                return default_result;
            }
            // TODO: Store default value in parameter node
        }
        
        // Handle comma or closing paren
        if (consume_punctuator(",")) {
            // Check for trailing variadic after comma
            if (peek_token().has_value() && peek_token()->value() == "...") {
                consume_token();
                out_params.is_variadic = true;
                if (!consume_punctuator(")")) {
                    return ParseResult::error("Expected ')' after variadic '...'", *current_token_);
                }
                break;
            }
            continue;
        } else if (!peek_token().has_value() || peek_token()->value() != ")") {
            return ParseResult::error("Expected ',' or ')' in parameter list", *current_token_);
        }
    }
    
    return ParseResult::success();
}
```

**Migration Steps:**

1. Implement `parseParameterList()` and add tests
2. Update `parse_function_declaration()` to use it:
   ```cpp
   ParsedParameterList params;
   auto param_result = parseParameterList(params);
   if (param_result.is_error()) return param_result;
   
   for (const auto& param : params.parameters) {
       func_ref.add_parameter_node(param);
   }
   func_ref.set_is_variadic(params.is_variadic);
   ```
3. Update constructor parameter parsing in `parse_struct_declaration()`
4. Update out-of-line member parsing
5. Update template function parsing
6. Remove old inline parameter parsing code

**Testing:**
```batch
.\build_flashcpp.bat
.\link_and_run_test_debug.bat
```

**Deliverables:**
- [x] `parseParameterList()` implementation
- [x] All parameter parsing unified
- [x] Zero behavior changes (diff baseline dumps)

---

### Phase 2: Unified Trailing Specifiers (Week 3)

**Goals:**
- Create `parseFunctionTrailingSpecifiers()` method
- Handle all specifiers consistently across function types

**Implementation:**

```cpp
ParseResult Parser::parseFunctionTrailingSpecifiers(
    MemberQualifiers& out_quals,
    FunctionSpecifiers& out_specs
) {
    // Parse CV qualifiers (for member functions)
    while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
        std::string_view kw = peek_token()->value();
        if (kw == "const") {
            out_quals.is_const = true;
            consume_token();
        } else if (kw == "volatile") {
            out_quals.is_volatile = true;
            consume_token();
        } else {
            break;
        }
    }
    
    // Parse ref qualifiers
    if (peek_token().has_value() && peek_token()->value() == "&") {
        consume_token();
        if (peek_token().has_value() && peek_token()->value() == "&") {
            consume_token();
            out_quals.is_rvalue_ref = true;
        } else {
            out_quals.is_lvalue_ref = true;
        }
    }
    
    // Parse noexcept
    if (peek_token().has_value() && peek_token()->value() == "noexcept") {
        consume_token();
        out_specs.is_noexcept = true;
        
        if (peek_token().has_value() && peek_token()->value() == "(") {
            consume_token();
            auto expr_result = parse_expression();
            if (expr_result.is_error()) return expr_result;
            out_specs.noexcept_expr = expr_result.node();
            if (!consume_punctuator(")")) {
                return ParseResult::error("Expected ')' after noexcept expression", *current_token_);
            }
        }
    }
    
    // Parse override/final
    while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
        std::string_view kw = peek_token()->value();
        if (kw == "override") {
            out_specs.is_override = true;
            consume_token();
        } else if (kw == "final") {
            out_specs.is_final = true;
            consume_token();
        } else {
            break;
        }
    }
    
    // Parse = 0, = default, = delete
    if (peek_token().has_value() && peek_token()->value() == "=") {
        consume_token();
        
        if (peek_token().has_value()) {
            if (peek_token()->value() == "0") {
                consume_token();
                out_specs.is_pure_virtual = true;
            } else if (peek_token()->value() == "default") {
                consume_token();
                out_specs.is_defaulted = true;
            } else if (peek_token()->value() == "delete") {
                consume_token();
                out_specs.is_deleted = true;
            } else {
                return ParseResult::error("Expected '0', 'default', or 'delete' after '='", *current_token_);
            }
        }
    }
    
    // Skip any remaining attributes
    skip_cpp_attributes();
    skip_gcc_attributes();
    
    return ParseResult::success();
}
```

**Deliverables:**
- [x] `parseFunctionTrailingSpecifiers()` implementation
- [x] Member function parsing updated
- [ ] Constructor/destructor parsing updated (deferred - current handling is sufficient)

---

### Phase 3: RAII Scope Guards (Week 4)

**Goals:**
- Implement `TemplateParameterScope` class (header-only)
- Implement `FunctionScopeGuard` class (header-only)
- Replace manual cleanup with RAII

**Implementation:**

Most of `FunctionScopeGuard` is inlined in `ParserScopeGuards.h`. The `injectThisPointer()` method needs access to Parser internals, so it's defined at the bottom of `Parser.h` after the Parser class:

```cpp
// At bottom of Parser.h, after Parser class definition:

inline void FunctionScopeGuard::injectThisPointer() {
    if (ctx_.kind != FunctionKind::Member &&
        ctx_.kind != FunctionKind::Constructor &&
        ctx_.kind != FunctionKind::Destructor) {
        return;
    }
    
    // Create 'this' pointer type
    auto type_it = gTypesByName.find(ctx_.parent_struct_name);
    if (type_it == gTypesByName.end()) return;
    
    auto [this_type_node, this_type_ref] = parser_.emplace_node_ref<TypeSpecifierNode>(
        Type::Struct, type_it->second->type_index_,
        static_cast<int>(64), Token()  // Pointer size
    );
    this_type_ref.add_pointer_level();
    
    Token this_token(Token::Type::Keyword, "this", 0, 0, 0);
    auto [this_decl_node, this_decl_ref] = parser_.emplace_node_ref<DeclarationNode>(
        this_type_node, this_token
    );
    gSymbolTable.insert("this", this_decl_node);
}
```

**Deliverables:**
- [x] `TemplateParameterScope` class
- [x] `FunctionScopeGuard` class (including `SymbolTableScope`)
- [x] Replace key manual cleanup patterns with RAII (constructor parsing, function body parsing, template member functions)

---

### Phase 4: Unified Function Header Parsing (Weeks 5-6)

**Goals:**
- Implement `parseFunctionHeader()` method
- Migrate free functions first, then member functions

**Implementation:**

```cpp
ParseResult Parser::parseFunctionHeader(
    const FunctionParsingContext& ctx,
    ParsedFunctionHeader& out_header
) {
    // Parse return type (if not constructor/destructor)
    if (ctx.kind != FunctionKind::Constructor && 
        ctx.kind != FunctionKind::Destructor) {
        auto type_result = parse_type_specifier();
        if (type_result.is_error()) return type_result;
        // Store return type...
    }
    
    // Parse function name
    if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
        return ParseResult::error("Expected function name", *current_token_);
    }
    out_header.name_token = *peek_token();
    consume_token();
    
    // Parse parameter list
    auto params_result = parseParameterList(out_header.params);
    if (params_result.is_error()) return params_result;
    
    // Parse trailing specifiers
    auto specs_result = parseFunctionTrailingSpecifiers(
        out_header.member_quals,
        out_header.specifiers
    );
    if (specs_result.is_error()) return specs_result;
    
    // Validate specifiers for function kind
    if (ctx.kind == FunctionKind::Free) {
        if (out_header.specifiers.is_virtual) {
            return ParseResult::error("Free functions cannot be virtual", out_header.name_token);
        }
        if (out_header.specifiers.is_override || out_header.specifiers.is_final) {
            return ParseResult::error("Free functions cannot use override/final", out_header.name_token);
        }
    }
    
    return ParseResult::success();
}
```

**Migration Order:**
1. Free functions (`parse_function_declaration`)
2. Member functions (in `parse_struct_declaration`)
3. Constructors
4. Destructors
5. Template functions
6. Template member functions

**Deliverables:**
- [x] `parseFunctionHeader()` implementation
- [x] `createFunctionFromHeader()` bridge method
- [x] Free function trailing specifiers now use `parseFunctionTrailingSpecifiers()` (noexcept applied properly)
- [x] const/volatile support added to member functions
- [x] Full test pass

---

### Phase 5: Unified Body Parsing (Week 7)

**Goals:**
- Implement `parseFunctionBodyWithContext()` method
- Unify delayed body handling

**Implementation:**

```cpp
ParseResult Parser::parseFunctionBodyWithContext(
    const FunctionParsingContext& ctx,
    const ParsedFunctionHeader& header,
    ASTNode& out_body
) {
    // Handle = default
    if (header.specifiers.is_defaulted) {
        auto [block_node, block_ref] = create_node_ref(BlockNode());
        out_body = block_node;
        if (!consume_punctuator(";")) {
            return ParseResult::error("Expected ';' after '= default'", *current_token_);
        }
        return ParseResult::success();
    }
    
    // Handle = delete
    if (header.specifiers.is_deleted) {
        if (!consume_punctuator(";")) {
            return ParseResult::error("Expected ';' after '= delete'", *current_token_);
        }
        return ParseResult::success();  // out_body remains empty
    }
    
    // Handle declaration only (no body)
    if (consume_punctuator(";")) {
        return ParseResult::success();  // Declaration only
    }
    
    // Expect function body
    if (!peek_token().has_value() || peek_token()->value() != "{") {
        return ParseResult::error("Expected '{' or ';' after function declaration", *current_token_);
    }
    
    // Set up scope using RAII guard
    FunctionScopeGuard scope_guard(*this, ctx, /* func_node */ nullptr);
    
    // Inject 'this' for member functions
    scope_guard.injectThisPointer();
    
    // Register parameters
    scope_guard.registerParameters(header.params.parameters);
    
    // Parse the block
    auto block_result = parse_block();
    if (block_result.is_error()) return block_result;
    
    if (block_result.node().has_value()) {
        out_body = *block_result.node();
    }
    
    return ParseResult::success();
}
```

**Deliverables:**
- [x] `parseFunctionBodyWithContext()` implementation
- [x] Delayed body queue unified (helper methods `setupMemberFunctionContext()` and `registerMemberFunctionsInScope()`)
- [x] All body parsing goes through single method (`parseDelayedFunctionBody()` and `registerParametersInScope()`)

---

### Phase 6: Template Consolidation (Week 8) ✅ COMPLETE

**Goals:**
- Ensure all template paths share common parsing infrastructure
- Unify `TemplateParameterScope` usage

**Tasks:**

1. Update `parse_template_declaration()` to use:
   - [x] `TemplateParameterScope` for cleanup (replaced local struct with shared RAII guard)
   - [ ] `parseFunctionHeader()` for function templates (deferred - requires significant restructuring)

2. Update `parse_member_function_template()` to use:
   - [x] `TemplateParameterScope` for cleanup (already implemented in Phase 3)
   - [ ] `parseFunctionHeader()` for the member function (deferred - requires significant restructuring)

3. Update `try_parse_out_of_line_template_member()` to use:
   - [x] `parseParameterList()` (Phase 1) for parameters
   - [x] `TemplateParameterScope` for type cleanup (via template instantiation functions)

4. Template instantiation functions updated to use `TemplateParameterScope`:
   - [x] `instantiate_function_template()`
   - [x] `try_instantiate_template()`
   - [x] `try_instantiate_member_function_template()`
   - [x] `try_instantiate_member_function_template_explicit()`
   - [x] `parseTemplateBody()`

**Deliverables:**
- [x] All template paths use RAII cleanup (`TemplateParameterScope`) - **eliminated all manual gTypesByName.erase() calls**
- [ ] Shared header parsing (deferred - parseFunctionHeader integration requires restructuring)
- [x] Consistent error handling through RAII guards

---

### Phase 7: Signature Validation (Week 9) ✅ COMPLETE

**Goals:**
- Implement `validateSignatureMatch()` method
- Centralize all out-of-line validation

**Implementation:**

```cpp
bool Parser::validateSignatureMatch(
    const FunctionDeclarationNode& declaration,
    const ParsedFunctionHeader& definition_header,
    std::string& out_error
) {
    // Compare return types
    const auto& decl_type = declaration.decl_node().type_specifier();
    // ... type comparison logic ...
    
    // Compare parameter counts
    if (declaration.parameter_nodes().size() != definition_header.params.parameters.size()) {
        out_error = "Parameter count mismatch";
        return false;
    }
    
    // Compare parameter types
    for (size_t i = 0; i < declaration.parameter_nodes().size(); ++i) {
        // ... detailed type comparison ...
        // Handle top-level const differences
        // Handle reference/pointer qualifiers
    }
    
    // Compare cv-qualifiers for member functions
    // ...
    
    return true;
}
```

**Deliverables:**
- [x] `validateSignatureMatch()` implementation - returns `SignatureValidationResult` with detailed mismatch info
- [x] Out-of-line member validation unified - replaced ~80 lines of validation code with single method call
- [ ] Out-of-line template validation unified (deferred - templates use delayed instantiation)

---

### Phase 8: Cleanup and Documentation (Week 10)

**Goals:**
- Remove dead code
- Update documentation
- Final testing

**Tasks:**

1. **Remove legacy code paths** (behind feature flag first)

2. **Update documentation:**
   - Update `AGENTS.md` with new parser architecture
   - Create `docs/PARSER_ARCHITECTURE.md` describing the unified pipeline
   - Update code comments

3. **Performance validation:**
   ```powershell
   .\benchmark_timing.bat  # Compare before/after
   ```

4. **Final cleanup:**
   - Remove feature flag
   - Remove commented-out code
   - Run full test suite

**Deliverables:**
- [ ] All legacy code removed
- [ ] Documentation updated
- [ ] Zero test regressions
- [ ] Performance validated

---

## 4. Risk Mitigation

### 4.1 Safety Principles

1. **Incremental changes**: Each phase should be independently testable
2. **Feature flags**: New code can be toggled on/off
3. **Baseline comparisons**: AST dumps compared at each phase
4. **Test coverage**: New tests added before refactoring

### 4.2 Rollback Strategy

```cpp
// Quick rollback: just flip the flag back to 0 and rebuild
#define FLASHCPP_USE_UNIFIED_PARSING 0

// Or via compiler flag:
// cl.exe ... -DFLASHCPP_USE_UNIFIED_PARSING=0
```

If issues are discovered after the old code is deleted (Phase 8), use git:
```batch
git revert HEAD
```

### 4.3 Known Risks

| Risk | Mitigation |
|------|------------|
| Template instantiation breaks | Extensive template tests, incremental migration |
| Out-of-line validation changes | Side-by-side comparison with old code |
| Performance regression | Benchmark at each phase |
| Memory leaks from scope changes | ASAN/MSAN testing |
| Error message changes | Preserve exact error text in new helpers; test error paths |
| Operator/conversion function edge cases | Add specific tests before refactoring those paths |

---

## 5. Success Metrics

| Metric | Target |
|--------|--------|
| Lines of code reduction | 30-40% in duplicated sections |
| Test pass rate | 100% (no regressions) |
| Performance | No regression (shared code paths may improve cache locality) |
| New test coverage | +20 test cases minimum |

---

## 6. Future Enhancement: Early Mangled Name Generation

### 6.1 Problem Statement

Currently, mangled names are generated on-demand throughout the compiler using `std::string` concatenation. This leads to:
- Repeated allocations for the same mangled name
- Inconsistent mangling across different compiler phases
- No single source of truth for a function's mangled identity

### 6.2 Proposed Solution: `MangledName` Type

Introduce a `MangledName` type that:
1. Generates the mangled name **once** during parsing
2. Stores it persistently using `StringBuilder` + `commit()`
3. Provides a `string_view` interface for zero-copy access
4. Supports easy A/B testing between old and new approaches

```cpp
// ===== src/MangledName.h (header-only) =====

#pragma once
#include "StringBuilder.h"
#include <string>
#include <string_view>
#include <variant>

namespace FlashCpp {

// Toggle between old (std::string) and new (string_view) implementations
#ifndef FLASHCPP_USE_STRINGVIEW_MANGLING
#define FLASHCPP_USE_STRINGVIEW_MANGLING 1
#endif

// A mangled name that can be stored as either:
// - A committed string_view (points to StringBuilder's stable storage)
// - A std::string (fallback for compatibility/testing)
class MangledName {
public:
    // Default: empty name
    MangledName() : storage_(std::string_view{}) {}
    
    // Construct from committed string_view (preferred - zero allocation)
    explicit MangledName(std::string_view committed_sv) 
        : storage_(committed_sv) {}
    
    // Construct from string (fallback - owns the data)
    explicit MangledName(std::string owned_str) 
        : storage_(std::move(owned_str)) {}
    
    // Always returns a string_view (zero-copy for both variants)
    std::string_view view() const {
        return std::visit([](const auto& s) -> std::string_view {
            return s;
        }, storage_);
    }
    
    // Implicit conversion to string_view for convenience
    operator std::string_view() const { return view(); }
    
    // Check if empty
    bool empty() const { return view().empty(); }
    
    // For compatibility with code expecting std::string
    std::string to_string() const { return std::string(view()); }
    
    // Comparison operators
    bool operator==(const MangledName& other) const { return view() == other.view(); }
    bool operator==(std::string_view other) const { return view() == other; }
    bool operator<(const MangledName& other) const { return view() < other.view(); }
    
private:
    std::variant<std::string_view, std::string> storage_;
};

// Builder for constructing mangled names
class MangledNameBuilder {
public:
    explicit MangledNameBuilder(StringBuilder& sb) : sb_(sb), start_(sb.size()) {}
    
    // Append components
    MangledNameBuilder& appendNamespace(std::string_view ns) {
        if (!ns.empty()) {
            sb_.append(ns);
            sb_.append("::");
        }
        return *this;
    }
    
    MangledNameBuilder& appendStruct(std::string_view struct_name) {
        if (!struct_name.empty()) {
            sb_.append(struct_name);
            sb_.append("::");
        }
        return *this;
    }
    
    MangledNameBuilder& appendFunction(std::string_view func_name) {
        sb_.append(func_name);
        return *this;
    }
    
    MangledNameBuilder& appendTemplateArgs(const std::vector<std::string_view>& args) {
        if (!args.empty()) {
            sb_.append("<");
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) sb_.append(", ");
                sb_.append(args[i]);
            }
            sb_.append(">");
        }
        return *this;
    }
    
    MangledNameBuilder& appendParamTypes(const std::vector<std::string_view>& types) {
        sb_.append("(");
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) sb_.append(", ");
            sb_.append(types[i]);
        }
        sb_.append(")");
        return *this;
    }
    
    MangledNameBuilder& appendCVQualifiers(bool is_const, bool is_volatile) {
        if (is_const) sb_.append(" const");
        if (is_volatile) sb_.append(" volatile");
        return *this;
    }
    
    // Commit and return the mangled name
    MangledName commit() {
#if FLASHCPP_USE_STRINGVIEW_MANGLING
        std::string_view result = sb_.commit();
        // Extract just our portion (from start_ to current position)
        return MangledName(result.substr(result.size() - (sb_.size() - start_)));
#else
        // Fallback: copy to string
        std::string_view built = sb_.commit();
        return MangledName(std::string(built.substr(built.size() - (sb_.size() - start_))));
#endif
    }
    
private:
    StringBuilder& sb_;
    size_t start_;  // Position where this name started
};

} // namespace FlashCpp
```

### 6.3 Integration with Function Parsing

Add `MangledName` to the parsed function header:

```cpp
// In ParsedFunctionHeader (ParserTypes.h)
struct ParsedFunctionHeader {
    // ... existing fields ...
    
    MangledName mangled_name;  // Generated during parsing, reused everywhere
};
```

Generate during `parseFunctionHeader()`:

```cpp
ParseResult Parser::parseFunctionHeader(
    const FunctionParsingContext& ctx,
    ParsedFunctionHeader& out_header
) {
    // ... parse return type, name, params, etc. ...
    
    // Generate mangled name immediately
    MangledNameBuilder builder(string_builder_);
    
    // Add namespace prefix (if any)
    if (!current_namespace_.empty()) {
        builder.appendNamespace(current_namespace_);
    }
    
    // Add struct prefix for member functions
    if (ctx.kind == FunctionKind::Member || 
        ctx.kind == FunctionKind::Constructor ||
        ctx.kind == FunctionKind::Destructor) {
        builder.appendStruct(ctx.parent_struct_name);
    }
    
    // Add function name
    builder.appendFunction(out_header.name_token.value());
    
    // Add template arguments (if template specialization)
    // builder.appendTemplateArgs(...);
    
    // Add parameter types for overload resolution
    std::vector<std::string_view> param_types;
    for (const auto& param : out_header.params.parameters) {
        if (param.is<DeclarationNode>()) {
            param_types.push_back(param.as<DeclarationNode>().type_specifier().type_name());
        }
    }
    builder.appendParamTypes(param_types);
    
    // Add CV qualifiers for member functions
    builder.appendCVQualifiers(
        out_header.member_quals.is_const,
        out_header.member_quals.is_volatile
    );
    
    out_header.mangled_name = builder.commit();
    
    return ParseResult::success();
}
```

### 6.4 Benefits

1. **Single Source of Truth**: Mangled name generated once, used everywhere
2. **Zero-Copy Access**: `string_view` interface avoids allocations in hot paths
3. **Easy A/B Testing**: Toggle `FLASHCPP_USE_STRINGVIEW_MANGLING` to compare
4. **Consistent Mangling**: Same code path for all function types
5. **Early Availability**: Name available immediately after parsing header

### 6.5 Migration Strategy

1. **Phase 1**: Add `MangledName` type, keep existing mangling code
2. **Phase 2**: Generate `MangledName` in `parseFunctionHeader()`, ignore it
3. **Phase 3**: Update one consumer (e.g., symbol table) to use `MangledName`
4. **Phase 4**: Benchmark and validate correctness
5. **Phase 5**: Migrate remaining consumers, remove old mangling code

### 6.6 Risk Mitigation

| Risk | Mitigation |
|------|------------|
| `string_view` dangling | StringBuilder storage is stable after commit |
| Different mangling results | A/B test with flag, compare outputs |
| Performance regression | Benchmark shows improvement expected |
| Complex template mangling | Start with simple cases, extend incrementally |

---

## 7. Appendix: File Organization

### New Files to Create:
```
src/
├── Log.h                   # ✅ DONE - Logging infrastructure (header-only, zero-overhead)
├── ParserTypes.h           # New type definitions (header-only)
├── ParserScopeGuards.h     # RAII guard declarations + inline implementations (header-only)
├── ScopedSymbolTable.h     # Hierarchical symbol tables (header-only)
└── MangledName.h           # MangledName type and builder (header-only)

docs/
├── PARSER_ARCHITECTURE.md  # New documentation (after refactoring)
└── REFACTORING_PLAN_OPUS45.md  # This document
```

**Note:** All new code is header-only to minimize build complexity. The RAII guards, logging macros, and type definitions are small enough that inlining is appropriate and avoids adding new .cpp files to the build system.

### Modified Files:
```
src/
├── Parser.h                # Add new method declarations, #include new headers
├── Parser.cpp              # Implement new methods, update existing, migrate std::cerr to FLASH_LOG
└── [Other files as needed]
```

---

## 8. Conclusion

This refactoring plan provides a comprehensive, safe, and incremental approach to eliminating code duplication in the FlashCpp parser. By introducing proper RAII patterns, unified parsing methods, hierarchical symbol tables, and maintaining strict test coverage, the codebase will become significantly more maintainable while preserving all existing functionality.

The estimated 10-week timeline allows for careful validation at each step, with clear rollback options if issues arise. The end result will be a parser that:
- Is easier to extend with new C++ features
- Has less code duplication and bug surface area
- Supports parallel function body parsing (future)
- Has proper logging for debugging
