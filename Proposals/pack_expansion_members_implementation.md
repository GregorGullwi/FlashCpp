# Pack Expansion in Member Declarations - Implementation Guide

**Feature:** Pack expansion syntax for member variable declarations (`Args... values;`)  
**Status:** Implemented in FlashCpp (November 2025), then removed  
**Proposal:** See `pack_expansion_members_proposal.md`

This document provides complete implementation details for reintroducing pack expansion in member declarations.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [AST Changes](#ast-changes)
3. [Parser Changes](#parser-changes)
4. [Template Instantiation](#template-instantiation)
5. [Name Generation](#name-generation)
6. [Testing Strategy](#testing-strategy)
7. [Code Listings](#code-listings)

---

## Architecture Overview

### High-Level Flow

```
Source Code: template<typename... Args> struct Tuple { Args... values; };
     ↓
[PARSER] Detects "..." after type specifier in member declaration
     ↓
[AST] Sets is_pack_expansion flag on DeclarationNode
     ↓
[TEMPLATE INSTANTIATION] Tuple<int, double, char> instantiated
     ↓
[EXPANSION] Loop through pack, generate values0, values1, values2
     ↓
[STRUCT LAYOUT] Calculate offsets and add members to StructInfo
     ↓
[CODEGEN] Members are treated as regular struct fields
```

### Key Components

1. **DeclarationNode** (`src/AstNodeTypes.h`) - AST node for declarations
   - Add: `bool is_pack_expansion_` flag
   - Add: Accessor methods

2. **Parser::parse_type_and_name()** (`src/Parser.cpp`) - Parse member declarations
   - Add: Detection of `...` token after type
   - Add: Set flag before creating DeclarationNode

3. **Parser::instantiate_template()** (`src/Parser.cpp`) - Template instantiation
   - Add: Check for pack expansion during member processing
   - Add: Loop to create indexed members (both primary and pattern specialization)

4. **StringBuilder** - String persistence for token values
   - Used to create persistent strings for generated member names

---

## AST Changes

### File: `src/AstNodeTypes.h`

**Location:** Lines 755-779 (DeclarationNode class)

#### Add Private Member

```cpp
class DeclarationNode : public AstNode {
private:
    // ... existing members ...
    bool is_pack_expansion_;  // NEW: Flag for Args... syntax
```

#### Add Public Methods

```cpp
public:
    // ... existing methods ...
    
    // NEW: Pack expansion accessors
    bool is_pack_expansion() const { 
        return is_pack_expansion_; 
    }
    
    void set_pack_expansion(bool value) { 
        is_pack_expansion_ = value; 
    }
```

#### Update Constructor

```cpp
DeclarationNode(/* ... existing params ... */)
    : AstNode(AstNodeType::Declaration)
    , /* ... existing initializers ... */
    , is_pack_expansion_(false)  // NEW: Initialize to false
{
    // ... existing body ...
}
```

**Why:** Stores whether this declaration uses pack expansion syntax, allowing instantiation logic to expand it into multiple members.

---

## Parser Changes

### 1. Parse Pack Expansion Syntax

**File:** `src/Parser.cpp`  
**Function:** `parse_type_and_name()`  
**Location:** Lines 746-757

#### Original Code (Without Feature)

```cpp
std::tuple<TypeInfo, std::string> Parser::parse_type_and_name() {
    TypeInfo type_info = parse_type();
    
    if (!is_declarator_start(peek_token())) {
        return {type_info, ""};
    }
    
    std::string name = parse_declarator(type_info);
    return {type_info, name};
}
```

#### Modified Code (With Feature)

```cpp
std::tuple<TypeInfo, std::string> Parser::parse_type_and_name() {
    TypeInfo type_info = parse_type();
    
    // NEW: Check for pack expansion (...)
    bool is_pack_expansion = false;
    if (peek_token() && peek_token()->value() == "...") {
        consume_token();  // Consume the ellipsis
        is_pack_expansion = true;
    }
    
    if (!is_declarator_start(peek_token())) {
        return {type_info, ""};
    }
    
    std::string name = parse_declarator(type_info);
    
    // Store the flag (needs to be passed to DeclarationNode creation)
    // Note: This requires threading the flag through the call chain
    
    return {type_info, name};
}
```

**Challenge:** The `is_pack_expansion` flag needs to reach the `DeclarationNode` constructor. This may require:

**Option A:** Add to return tuple (messy, breaks many call sites)
```cpp
std::tuple<TypeInfo, std::string, bool> parse_type_and_name()
```

**Option B:** Store in parser state (simpler)
```cpp
// In Parser class:
bool pending_pack_expansion_ = false;

// In parse_type_and_name():
pending_pack_expansion_ = is_pack_expansion;

// In caller (parse_struct_members, etc.):
auto decl = new DeclarationNode(...);
decl->set_pack_expansion(pending_pack_expansion_);
pending_pack_expansion_ = false;  // Reset
```

**Recommended:** Option B (parser state) for minimal changes.

#### Where DeclarationNode is Created

Look for locations that call `parse_type_and_name()` and create `DeclarationNode`:

```cpp
// Search for patterns like:
auto [type, name] = parse_type_and_name();
auto* decl = new DeclarationNode(type, name, ...);

// Add after creation:
decl->set_pack_expansion(pending_pack_expansion_);
pending_pack_expansion_ = false;
```

---

### 2. Template Instantiation - Primary Template

**File:** `src/Parser.cpp`  
**Function:** `instantiate_template()` (primary template path)  
**Location:** Lines 10461-10540 (approximate)

#### Context

When instantiating `Tuple<int, double, char>` from `template<typename... Args> struct Tuple { Args... values; }`:

1. Parser loops through original template members
2. For each member, checks if it's a pack expansion
3. If yes, expands it into multiple members (one per pack element)

#### Pseudocode

```cpp
// Inside instantiate_template(), when processing members:

for (auto* member : original_template_body->members) {
    if (auto* decl = dynamic_cast<DeclarationNode*>(member)) {
        
        // NEW: Check if this is a pack expansion
        if (decl->is_pack_expansion()) {
            // Find the template parameter pack
            std::string param_name = /* extract from decl->type */;
            
            // Get pack size
            int pack_size = /* from template_args */;
            
            // Expand into multiple members
            for (int i = 0; i < pack_size; ++i) {
                // Generate indexed name: values0, values1, ...
                std::string indexed_name = StringBuilder()
                    .append(decl->name())
                    .append(std::to_string(i))
                    .commit();
                
                // Get type for this pack element
                TypeInfo element_type = /* from template_args[i] */;
                
                // Calculate offset and size
                int member_size = /* calculate from element_type */;
                
                // Add to StructInfo
                struct_info->addMember(indexed_name, element_type, 
                                      current_offset, member_size);
                
                // Update offset for next member
                current_offset = /* align and add size */;
            }
            
            continue;  // Skip normal processing
        }
        
        // Normal member processing (non-pack)
        // ... existing code ...
    }
}
```

#### Actual Code (Simplified)

```cpp
// Look for the loop that processes template members:

for (auto* member_node : template_def->body()->members()) {
    auto* member_decl = dynamic_cast<DeclarationNode*>(member_node);
    if (!member_decl) continue;
    
    // NEW: Handle pack expansion
    if (member_decl->is_pack_expansion()) {
        // Get the base type (the template parameter, e.g., "Args")
        std::string base_type_name = member_decl->type().name;
        
        // Find matching template parameter
        int param_index = -1;
        for (size_t i = 0; i < template_params.size(); ++i) {
            if (template_params[i].name == base_type_name && 
                template_params[i].is_pack) {
                param_index = i;
                break;
            }
        }
        
        if (param_index == -1) {
            // Error: pack parameter not found
            continue;
        }
        
        // Get the pack arguments
        auto& pack_args = template_args[param_index].pack_types;
        int pack_size = pack_args.size();
        
        // Expand into multiple members
        for (int i = 0; i < pack_size; ++i) {
            // Generate name: values0, values1, values2
            std::string member_name = StringBuilder()
                .append(member_decl->name())
                .append(std::to_string(i))
                .commit();
            
            // Get element type
            TypeInfo element_type = pack_args[i].base_type;
            
            // Calculate size and alignment
            int member_size = calculate_type_size(element_type);
            int member_align = calculate_alignment(element_type);
            
            // Align offset
            if (member_align > 0) {
                current_offset = align_to(current_offset, member_align);
            }
            
            // Add member to struct
            struct_info->addMember(member_name, element_type, 
                                  current_offset, member_size);
            
            // Advance offset
            current_offset += member_size;
        }
        
        continue;  // Done with this member
    }
    
    // Regular member (non-pack) - existing code
    // ...
}
```

**Key Functions Used:**
- `StringBuilder()` - Creates persistent strings for token values
- `calculate_type_size()` - Gets size of type in bytes
- `calculate_alignment()` - Gets alignment requirement
- `align_to()` - Rounds offset up to alignment boundary
- `struct_info->addMember()` - Registers member in struct layout

---

### 3. Template Instantiation - Pattern Specialization

**File:** `src/Parser.cpp`  
**Function:** `instantiate_template()` (pattern specialization path)  
**Location:** Lines 10457-10537 (approximate)

#### Context

Pattern specializations like `template<typename... Args> struct Tuple<Args*>` need special handling because:

1. Template parameters come from the **primary template**, not the specialization
2. Must look up the primary template to get parameter names

#### Code Pattern

```cpp
// In pattern specialization instantiation:

// NEW: Get primary template for parameter names
auto* primary_template = gTemplateRegistry.lookupTemplate(
    template_name, 
    /* num_params */ 0  // Get unspecialized version
);

if (!primary_template) {
    // Error handling
}

auto& primary_params = primary_template->parameters();

// Then process members (same as primary template logic above)
for (auto* member_node : spec_template_def->body()->members()) {
    auto* member_decl = dynamic_cast<DeclarationNode*>(member_node);
    if (!member_decl) continue;
    
    if (member_decl->is_pack_expansion()) {
        // Find parameter in PRIMARY template's parameters
        std::string base_type_name = member_decl->type().name;
        
        int param_index = -1;
        for (size_t i = 0; i < primary_params.size(); ++i) {
            if (primary_params[i].name == base_type_name && 
                primary_params[i].is_pack) {
                param_index = i;
                break;
            }
        }
        
        // Rest is same as primary template expansion
        // ...
    }
}
```

**Critical:** Use `primary_params` instead of `spec_template_def->parameters()`.

---

## Name Generation

### StringBuilder Pattern

FlashCpp uses `StringBuilder` to create persistent strings for token values. This is critical because `std::string::c_str()` returns a pointer that becomes invalid when the string is destroyed.

#### ❌ Wrong (Dangling Pointer)

```cpp
std::string name = "values" + std::to_string(i);
Token* tok = new Token(TokenType::Identifier, name.c_str());
// Bug: name is destroyed, tok->value() is dangling
```

#### ✅ Correct (StringBuilder)

```cpp
std::string name = StringBuilder()
    .append("values")
    .append(std::to_string(i))
    .commit();  // Stores in persistent arena

Token* tok = new Token(TokenType::Identifier, name.c_str());
// Safe: name.c_str() points to persistent storage
```

### Implementation

```cpp
// StringBuilder class (simplified):
class StringBuilder {
    std::string buffer_;
    
public:
    StringBuilder& append(const std::string& str) {
        buffer_ += str;
        return *this;
    }
    
    std::string commit() {
        // Store in persistent arena and return
        return gStringArena.intern(buffer_);
    }
};
```

### Usage in Pack Expansion

```cpp
for (int i = 0; i < pack_size; ++i) {
    std::string member_name = StringBuilder()
        .append(original_member_name)  // e.g., "values"
        .append(std::to_string(i))     // e.g., "0"
        .commit();                     // Returns "values0" (persistent)
    
    struct_info->addMember(member_name, ...);
}
```

---

## Testing Strategy

### Test Files

All tests are in `tests/Reference/`:

#### 1. Basic Functionality

**File:** `test_pack_simple_return.cpp`

```cpp
template<typename... Args>
struct Tuple {
    static constexpr int size = sizeof...(Args);
    Args... values;
    
    Tuple(Args... args) : values(args)... {}
};

int main() {
    Tuple<int> t(5);
    return t.values0;  // Should return 5
}
```

**Expected:** Exit code 5  
**Tests:** Single-type pack, member access

#### 2. Multi-Type Pack

**File:** `test_pack_multi_types.cpp`

```cpp
template<typename... Args>
struct Tuple {
    static constexpr int size = sizeof...(Args);
    Args... values;
    
    Tuple(Args... args) : values(args)... {}
};

int main() {
    Tuple<int, double, char> t(10, 3.14, 'A');
    return t.values0 + static_cast<int>(t.values1) + t.values2;
    // 10 + 3 + 65 = 78
}
```

**Expected:** Exit code 78  
**Tests:** Multiple types, type conversions

#### 3. Comprehensive Test

**File:** `test_variadic_summary.cpp`

```cpp
template<typename... Args>
struct Tuple {
    static constexpr int size = sizeof...(Args);
    Args... values;
    
    Tuple(Args... args) : values(args)... {}
};

int main() {
    Tuple<> empty;
    Tuple<int> one(42);
    Tuple<int, double, char> three(10, 2.5, 'x');
    
    return Tuple<int, double, char>::size + three.values0;
    // 3 + 10 = 13
}
```

**Expected:** Exit code 13  
**Tests:** Empty pack, sizeof..., static member access

**Note:** This test currently fails due to symbol table bug (unrelated to pack expansion).

### Build and Run Tests

```batch
@echo off
REM Build test with FlashCpp
x64\Debug\FlashCpp.exe tests\Reference\test_pack_simple_return.cpp

REM Link with MSVC
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe" ^
    test_pack_simple_return.obj ^
    /SUBSYSTEM:CONSOLE ^
    /ENTRY:main ^
    /OUT:test_pack_simple_return.exe

REM Run and check exit code
test_pack_simple_return.exe
echo Exit code: %ERRORLEVEL%
```

### Verification Checklist

- [ ] Compiles without errors
- [ ] Links with MSVC linker
- [ ] Returns expected exit code
- [ ] No memory errors (run with sanitizers if available)
- [ ] Struct layout matches manual layout (use `dumpbin /DISASM`)

---

## Code Listings

### Complete File Modifications

#### 1. `src/AstNodeTypes.h` - DeclarationNode

**Line Range:** ~755-779

```cpp
class DeclarationNode : public AstNode {
private:
    TypeInfo type_;
    std::string name_;
    AstNode* initializer_;
    bool is_static_;
    bool is_const_;
    bool is_pack_expansion_;  // NEW

public:
    DeclarationNode(const TypeInfo& type, const std::string& name,
                   AstNode* initializer = nullptr,
                   bool is_static = false, bool is_const = false)
        : AstNode(AstNodeType::Declaration)
        , type_(type)
        , name_(name)
        , initializer_(initializer)
        , is_static_(is_static)
        , is_const_(is_const)
        , is_pack_expansion_(false)  // NEW
    {
    }

    const TypeInfo& type() const { return type_; }
    const std::string& name() const { return name_; }
    AstNode* initializer() const { return initializer_; }
    bool is_static() const { return is_static_; }
    bool is_const() const { return is_const_; }
    
    // NEW methods
    bool is_pack_expansion() const { return is_pack_expansion_; }
    void set_pack_expansion(bool value) { is_pack_expansion_ = value; }
};
```

#### 2. `src/Parser.cpp` - parse_type_and_name()

**Line Range:** ~746-757

Add parser state member to Parser class:
```cpp
class Parser {
private:
    // ... existing members ...
    bool pending_pack_expansion_ = false;  // NEW
```

Modify `parse_type_and_name()`:
```cpp
std::tuple<TypeInfo, std::string> Parser::parse_type_and_name() {
    TypeInfo type_info = parse_type();
    
    // NEW: Detect pack expansion
    if (peek_token() && peek_token()->value() == "...") {
        consume_token();
        pending_pack_expansion_ = true;
    }
    
    if (!is_declarator_start(peek_token())) {
        return {type_info, ""};
    }
    
    std::string name = parse_declarator(type_info);
    return {type_info, name};
}
```

After creating DeclarationNode (in `parse_struct_members()` or similar):
```cpp
auto* decl = new DeclarationNode(type, name, initializer);
decl->set_pack_expansion(pending_pack_expansion_);  // NEW
pending_pack_expansion_ = false;  // NEW: Reset
```

#### 3. `src/Parser.cpp` - Template Instantiation (Primary)

**Line Range:** ~10461-10540

```cpp
// Inside instantiate_template(), primary template branch:

for (auto* member_node : template_def->body()->members()) {
    auto* member_decl = dynamic_cast<DeclarationNode*>(member_node);
    if (!member_decl) continue;
    
    // NEW: Handle pack expansion
    if (member_decl->is_pack_expansion()) {
        std::string base_type_name = member_decl->type().name;
        
        // Find template parameter pack
        int param_index = -1;
        for (size_t i = 0; i < template_params.size(); ++i) {
            if (template_params[i].name == base_type_name && 
                template_params[i].is_pack) {
                param_index = i;
                break;
            }
        }
        
        if (param_index == -1) {
            std::cerr << "Error: Template parameter pack '" 
                     << base_type_name << "' not found\n";
            continue;
        }
        
        // Get pack arguments
        auto& template_arg = template_args[param_index];
        if (!template_arg.is_pack) {
            std::cerr << "Error: Expected pack argument\n";
            continue;
        }
        
        int pack_size = template_arg.pack_types.size();
        
        // Expand pack into multiple members
        for (int i = 0; i < pack_size; ++i) {
            // Generate indexed name
            std::string member_name = StringBuilder()
                .append(member_decl->name())
                .append(std::to_string(i))
                .commit();
            
            // Get element type
            TypeInfo element_type = template_arg.pack_types[i].base_type;
            
            // Calculate size and alignment
            int member_size = calculate_type_size(element_type);
            int member_align = calculate_alignment(element_type);
            
            // Align current offset
            if (member_align > 0) {
                current_offset = align_to(current_offset, member_align);
            }
            
            // Add member
            struct_info->addMember(member_name, element_type,
                                  current_offset, member_size);
            
            current_offset += member_size;
        }
        
        continue;  // Skip normal processing
    }
    
    // Normal member processing (existing code)
    // ...
}
```

#### 4. `src/Parser.cpp` - Template Instantiation (Pattern Specialization)

**Line Range:** ~10457-10537

```cpp
// Inside instantiate_template(), pattern specialization branch:

// NEW: Get primary template for parameter lookup
auto* primary_template = gTemplateRegistry.lookupTemplate(template_name, 0);
if (!primary_template) {
    std::cerr << "Error: Cannot find primary template\n";
    return nullptr;
}
auto& primary_params = primary_template->parameters();

// Process members
for (auto* member_node : spec_template_def->body()->members()) {
    auto* member_decl = dynamic_cast<DeclarationNode*>(member_node);
    if (!member_decl) continue;
    
    // NEW: Handle pack expansion (same as primary, but use primary_params)
    if (member_decl->is_pack_expansion()) {
        std::string base_type_name = member_decl->type().name;
        
        // Find in PRIMARY template parameters
        int param_index = -1;
        for (size_t i = 0; i < primary_params.size(); ++i) {
            if (primary_params[i].name == base_type_name && 
                primary_params[i].is_pack) {
                param_index = i;
                break;
            }
        }
        
        // Rest is identical to primary template expansion
        // (see above)
    }
    
    // Normal member processing
    // ...
}
```

---

## Helper Functions

### Type Size Calculation

```cpp
int calculate_type_size(const TypeInfo& type) {
    if (type.name == "int") return 4;
    if (type.name == "char") return 1;
    if (type.name == "double") return 8;
    if (type.name == "bool") return 1;
    if (type.is_pointer) return 8;  // x64
    
    // For struct types, look up in type registry
    auto* struct_info = gTypeRegistry.getStruct(type.name);
    if (struct_info) {
        return struct_info->size();
    }
    
    return 0;  // Unknown type
}
```

### Alignment Calculation

```cpp
int calculate_alignment(const TypeInfo& type) {
    if (type.name == "char" || type.name == "bool") return 1;
    if (type.name == "short") return 2;
    if (type.name == "int" || type.name == "float") return 4;
    if (type.name == "double" || type.name == "long long") return 8;
    if (type.is_pointer) return 8;  // x64
    
    // For structs, use largest member alignment (up to 8)
    auto* struct_info = gTypeRegistry.getStruct(type.name);
    if (struct_info) {
        return struct_info->alignment();
    }
    
    return 1;  // Default
}
```

### Alignment Helper

```cpp
int align_to(int offset, int alignment) {
    if (alignment <= 0) return offset;
    return (offset + alignment - 1) & ~(alignment - 1);
}
```

---

## Integration Checklist

When reintroducing this feature:

1. **AST Changes**
   - [ ] Add `is_pack_expansion_` to `DeclarationNode`
   - [ ] Add accessor methods
   - [ ] Update constructor

2. **Parser State**
   - [ ] Add `pending_pack_expansion_` member to `Parser` class
   - [ ] Initialize to `false`

3. **Parsing**
   - [ ] Detect `...` in `parse_type_and_name()`
   - [ ] Set `pending_pack_expansion_` flag
   - [ ] Apply flag to created `DeclarationNode`
   - [ ] Reset flag after use

4. **Template Instantiation - Primary**
   - [ ] Check `is_pack_expansion()` in member loop
   - [ ] Find template parameter pack
   - [ ] Loop through pack elements
   - [ ] Generate indexed names with `StringBuilder`
   - [ ] Calculate size and alignment
   - [ ] Add members to `struct_info`

5. **Template Instantiation - Pattern**
   - [ ] Look up primary template
   - [ ] Use primary template parameters
   - [ ] Same expansion logic as primary

6. **Testing**
   - [ ] Create test files
   - [ ] Build with FlashCpp
   - [ ] Link with MSVC
   - [ ] Verify exit codes
   - [ ] Check struct layout with `dumpbin`

7. **Documentation**
   - [ ] Update user-facing docs
   - [ ] Add to feature list
   - [ ] Document limitations

---

## Known Issues and Limitations

### 1. No Direct Indexed Access

Users cannot write:
```cpp
Tuple<int, double> t(1, 2.5);
auto x = t.values[0];  // ❌ Not supported
```

Must use generated names:
```cpp
auto x = t.values0;  // ✅ Works
```

**Workaround:** Provide `get<N>()` template function.

### 2. No Structured Binding Integration

C++17 structured bindings don't work directly:
```cpp
auto [a, b, c] = t;  // ❌ Doesn't work
```

**Future:** Could be added by implementing `std::tuple_size` and `std::tuple_element` specializations.

### 3. Member Initializers Require Expansion Syntax

```cpp
Tuple(Args... args) : values(args)... {}  // ✅ Correct
Tuple(Args... args) : values0(args)... {}  // ❌ Wrong
```

The pack expansion in member initializers must use the original member name (`values`), not the indexed names (`values0`).

### 4. Symbol Table Bug (Unrelated)

There's a separate bug with static member access that causes assertion failures. This is NOT related to pack expansion but affects comprehensive tests.

**Symptom:** `Assertion failed: src_it != current_scope.identifier_offset.end(), file src\IRConverter.h, line 3969`

**Cause:** Static members looked up in local scope instead of global scope.

---

## Removal Procedure

To remove this feature from the codebase:

### 1. Remove AST Changes

In `src/AstNodeTypes.h`, remove:
- `bool is_pack_expansion_;` member
- `is_pack_expansion()` method
- `set_pack_expansion()` method
- Constructor initialization of `is_pack_expansion_`

### 2. Remove Parser State

In `src/Parser.cpp`, remove:
- `bool pending_pack_expansion_;` member (if added to Parser class)

### 3. Remove Parsing Logic

In `parse_type_and_name()`, remove:
- Ellipsis detection code
- `pending_pack_expansion_ = true;` assignment

Remove flag application:
- `decl->set_pack_expansion(...)` calls
- `pending_pack_expansion_ = false;` resets

### 4. Remove Instantiation Logic

In `instantiate_template()`, remove:
- `if (member_decl->is_pack_expansion()) { ... }` blocks
- All pack expansion loops
- StringBuilder name generation for indexed members

Do this for BOTH:
- Primary template instantiation
- Pattern specialization instantiation

### 5. Update Documentation

Remove or mark as "removed" in:
- `VARIADIC_TEMPLATES_PLAN.md`
- `TEMPLATE_FEATURES_SUMMARY.md`
- Any user-facing documentation

### 6. Archive Test Files

Move test files to archive or remove:
- `tests/Reference/test_pack_simple_return.cpp`
- `tests/Reference/test_pack_multi_types.cpp`
- Related test batch scripts

---

## Reintroduction Procedure

To bring back this feature:

1. **Read Proposal:** Review `pack_expansion_members_proposal.md`
2. **Follow Code Listings:** Use the complete code snippets above
3. **Work Incrementally:**
   - Start with AST changes
   - Add parsing logic
   - Implement instantiation for primary templates
   - Add pattern specialization support
   - Test at each step
4. **Run Tests:** Use the test files in this document
5. **Validate:** Compare with MSVC object code using `dumpbin`

**Estimated Time:** 2-3 hours for experienced developer familiar with FlashCpp codebase.

---

## References

- `pack_expansion_members_proposal.md` - C++ proposal document
- `src/AstNodeTypes.h` - AST node definitions
- `src/Parser.cpp` - Parser and template instantiation
- `tests/Reference/test_*.cpp` - Test cases
- `VARIADIC_TEMPLATES_PLAN.md` - Overall variadic template plan
- `TEMPLATE_FEATURES_SUMMARY.md` - Template feature documentation

---

**Document Version:** 1.0  
**Last Updated:** November 15, 2025  
**Status:** Feature removed, ready for reintroduction
