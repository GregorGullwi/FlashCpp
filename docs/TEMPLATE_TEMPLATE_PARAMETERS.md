# Template Template Parameters - Design Plan

## Current Status

Template template parameters (e.g., `template<template<typename> class Container, typename T>`) are partially working but have architectural issues that need to be addressed.

## Problems Identified

### 1. Global Type Registry Pollution

**Issue**: Template parameter names are registered in `gTypeInfo` (global type registry), which causes conflicts when multiple templates use the same parameter names.

**Example**:
```cpp
template<template<typename> class Container, typename T>
void foo() { }

template<template<typename> class Container, typename T>  // Same names!
void bar() { }
```

Both register "Container" and "T" in `gTypeInfo`, leading to:
- Name collisions
- Incorrect type lookups
- Stale type information from previous template

**Current Behavior**:
```
gTypeInfo entries:
[18] {name_="Container" type_=UserDefined ...}  // From first template
[19] {name_="T" type_=UserDefined ...}          // From first template
[23] {name_="Container" type_=Int ...}          // From second template?
[24] {name_="Container" type_=Int ...}          // Duplicate/confusion
```

### 2. Template Parameter Scope

**Issue**: Template parameters should be scoped to their template, not globally visible.

**Why**: 
- Different templates can (and commonly do) use the same parameter names
- Template parameters only exist during template instantiation
- They should not pollute global namespace or persist across templates

### 3. Code Generation Challenges

**Issue**: Template template parameters need special handling during instantiation.

**Current**: `Container` is treated like a regular type
**Needed**: `Container` is a template, not a type - `Container<int>` is the actual type

## Proposed Solutions

### Option A: Local Template Parameter Registry (Recommended)

**Approach**: Keep template parameters in a local scope during template parsing/instantiation.

**Changes Needed**:
1. Add `template_param_scope_` stack to Parser
2. Push new scope when entering template declaration
3. Register template parameters in local scope, not gTypeInfo
4. Look up template parameters in local scope first
5. Pop scope when exiting template

**Pros**:
- Clean separation of concerns
- No global pollution
- Natural scoping behavior
- Handles nested templates correctly

**Cons**:
- Requires threading scope through parsing functions
- Moderate refactoring effort

**Implementation Sketch**:
```cpp
struct TemplateParameterScope {
    std::unordered_map<std::string, TypeInfo> params;
    std::unordered_map<std::string, TemplateParameterInfo> template_params;
};

class Parser {
    std::vector<TemplateParameterScope> template_param_scopes_;
    
    void enter_template_scope() {
        template_param_scopes_.push_back({});
    }
    
    void exit_template_scope() {
        template_param_scopes_.pop_back();
    }
    
    std::optional<TypeInfo> lookup_template_param(const std::string& name) {
        for (auto it = template_param_scopes_.rbegin(); 
             it != template_param_scopes_.rend(); ++it) {
            if (auto p = it->params.find(name); p != it->params.end()) {
                return p->second;
            }
        }
        return std::nullopt;
    }
};
```

### Option B: Template Parameter Namespacing

**Approach**: Mangle template parameter names with template context.

**Changes Needed**:
1. Generate unique names: `Container` → `__template_foo_param_Container`
2. Store mapping in template metadata
3. Unmangle during instantiation

**Pros**:
- Minimal changes to existing code
- Uses existing gTypeInfo infrastructure

**Cons**:
- Hacky name mangling
- Harder to debug
- Doesn't solve fundamental scoping issues

### Option C: Delayed Parameter Resolution

**Approach**: Don't register template parameters at all during declaration.

**Changes Needed**:
1. Mark identifiers as "unresolved template parameter"
2. Resolve only during instantiation with concrete arguments
3. Use AST node placeholders instead of TypeInfo

**Pros**:
- Cleanest conceptually
- Matches C++ semantics closely

**Cons**:
- Significant parser refactoring
- Complex implementation

## Recommendation

**Use Option A** (Local Template Parameter Registry) for these reasons:

1. **Clean Architecture**: Properly scoped, matches language semantics
2. **Maintainable**: Clear separation between global types and template parameters
3. **Extensible**: Handles nested templates, template specializations naturally
4. **Debuggable**: Easy to inspect what's in scope at any point

**Implementation Priority**:
- Phase 1: Implement local scope for regular template parameters (typename T)
- Phase 2: Add template template parameter support (template<typename> class C)
- Phase 3: Handle non-type template parameters with proper scoping
- Phase 4: Test with complex nested template scenarios

## Known Limitations (Not Yet Addressed)

### Braced Initializer Lists in Template Contexts

**Issue**: Braced initializers don't work in certain template contexts.

**Example** (from template_template_call.cpp):
```cpp
template<template<typename> class Container, typename T>
void call() {
    // This doesn't work:
    Container<T> v{1, 2, 3};  // ❌ Braced initializer list fails
    
    // Workarounds:
    Container<T> v;            // ✅ Default construction
    v.push_back(1);           // ✅ Member function calls
}
```

**Root Cause**: Braced initializer parsing doesn't interact correctly with template-dependent types.

**Impact**: 
- Limited - can use other initialization syntax
- Workarounds available
- Not blocking for basic template template parameter support

**Future Work**: 
- Enhance braced initializer parsing to handle template contexts
- Add type deduction for initializer lists in templates
- Test with std::initializer_list patterns

## Testing Plan

### Test Cases Needed

1. **Basic Template Template Parameters**
   ```cpp
   template<template<typename> class Container, typename T>
   void test() { Container<T> v; }
   ```

2. **Multiple Templates with Same Parameter Names**
   ```cpp
   template<template<typename> class C, typename T> void foo();
   template<template<typename> class C, typename T> void bar();  // Same names
   ```

3. **Nested Templates**
   ```cpp
   template<template<typename> class Outer>
   struct Test {
       template<typename Inner>
       void method();
   };
   ```

4. **Template Specialization with Template Parameters**
   ```cpp
   template<template<typename> class C>
   struct Wrapper { };
   
   template<>
   struct Wrapper<std::vector> { };  // Specialize for specific template
   ```

## References

- C++ Standard: Template Template Parameters [temp.arg.template]
- Current failing test: tests/template_template_call.cpp
- Related code: src/Parser.cpp parse_template_parameters()
- Type system: src/Parser.h TypeInfo, gTypeInfo

## Status

- ✅ Problem identified and documented
- ✅ Root causes understood
- ⏸️ Implementation deferred (not blocking current SFINAE work)
- 📅 Planned for future enhancement

---

*Document created: 2025-12-13*
*Author: Copilot (via investigation of gTypeInfo debug output)*
*Related PR: Complete SFINAE implementation*
