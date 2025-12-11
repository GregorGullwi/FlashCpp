# Missing Features for Standard Library Header Support

This document tracks missing C++20 features that prevent FlashCpp from compiling standard library headers. Features are listed in priority order based on their blocking impact.

**Last Updated**: 2025-12-11  
**Test Reference**: `tests/test_real_std_headers_fail.cpp`

## Summary

Most critical parser/semantic features for basic standard library support are now **complete**:
- ✅ Conversion operators, non-type template parameters, template specialization inheritance
- ✅ Reference members in structs, anonymous template parameters, type alias access from specializations
- ⚠️ Compiler intrinsics (most critical ones implemented including `__is_same`)
- ✅ Perfect forwarding (`std::forward` works correctly)

**Remaining gaps**: Member template aliases (blocking `<type_traits>`), advanced template features (SFINAE, complex metaprogramming), some intrinsic edge cases, and preprocessor expression handling.

## Completed Features Summary ✅

The following features are **fully implemented and tested** (all 627+ existing tests pass):

1. **Conversion Operators** - User-defined conversion operators (`operator T()`) with static member access
2. **Non-Type Template Parameters** - `template<typename T, T v>` patterns with dependent types  
3. **Template Specialization Inheritance** - Static members propagate through base classes
4. **Anonymous Template Parameters** - `template<bool, typename T>` and `template<typename, class>` syntax
5. **Type Alias Access from Specializations** - Accessing `using` type aliases from template specializations (e.g., `enable_if<true>::type`)
6. **Reference Members in Structs** - Reference-type members (`int&`, `char&`, `short&`, `struct&`, template wrappers)
   - Known limitation: `double&` has runtime issues (pre-existing bug)

See **Progress Tracking** section below for detailed test references.

---

## Remaining Work

### Priority 4.5: Member Template Aliases (BLOCKING `<type_traits>`)

**Status**: ❌ **BLOCKING** - Required for standard library headers  
**Test Case**: `/tmp/test_member_template_alias.cpp`

**Problem**: The standard library uses template aliases as members of structs/classes, which is currently not supported:

```cpp
template<bool B>
struct Conditional {
    template<typename T, typename U>
    using type = T;  // Member template alias - not yet supported
};
```

**Current Error**: `Unexpected token in type specifier: 'using'` when parsing member template aliases.

**Impact**: **BLOCKS `<type_traits>`** - The standard library's `__conditional` and other metaprogramming utilities use this pattern extensively.

**Implementation Needed**: 
- Parser support for `using` declarations with template parameters inside struct/class definitions
- Template instantiation for member template aliases
- Qualified name resolution for accessing member template aliases (e.g., `Conditional<true>::type<int, float>`)

---

### Priority 5: Compiler Intrinsics (MOSTLY COMPLETE)

**Status**: ⚠️ **PARTIAL** - Most critical intrinsics implemented  
**Test Case**: `tests/test_type_traits_intrinsics.cpp`, `tests/test_is_same_intrinsic.cpp`

**Implemented** (20+ intrinsics): `__is_same`, `__is_base_of`, `__is_class`, `__is_enum`, `__is_union`, `__is_polymorphic`, `__is_abstract`, `__is_final`, `__is_empty`, `__is_standard_layout`, `__is_trivially_copyable`, `__is_trivial`, `__is_pod`, `__is_void`, `__is_integral`, `__is_floating_point`, `__is_array`, `__is_pointer`, plus math builtins (`__builtin_labs`, `__builtin_llabs`, `__builtin_fabs`, `__builtin_fabsf`) and variadic support (`__builtin_va_start`, `__builtin_va_arg`).

**May need additional work**: Some intrinsics parse but may need runtime validation (`__has_unique_object_representations`, `__is_constructible`, `__is_assignable`, `__is_destructible`, `__is_trivially_destructible`, `__is_layout_compatible`).

**Implementation**: Parser.cpp (lines 10217-10400), CodeGen.h::generateTypeTraitIr() (lines 10215+)

---

### Priority 8: Complex Preprocessor Expressions (NON-BLOCKING)

**Status**: ⚠️ **NON-BLOCKING** - Causes warnings but doesn't fail compilation  

**Problem**: Complex preprocessor conditionals (e.g., `#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) >= 409`) sometimes trigger "Division by zero in preprocessor expression" warnings when macros are undefined.

**Current Status**: Preprocessor mostly works, warnings don't block compilation, most headers parse successfully.

**Implementation**: May need better undefined macro handling in `evaluate_expression()` - see `src/FileReader.h`

---

### Priority 9: Advanced Template Features (PARTIAL SUPPORT)

**Status**: ⚠️ **PARTIAL** - Some features work, others don't

**Supported**: 
- ✅ Variadic templates (basic support)
- ✅ Template template parameters (partial support)
- ✅ Perfect forwarding (`std::forward` works correctly)

**Missing**:
- ❌ SFINAE (Substitution Failure Is Not An Error) - needed for `std::enable_if` and advanced metaprogramming

**SFINAE Example**:
```cpp
template<typename T>
typename T::value_type func(T t);  // Only valid if T has value_type

template<typename T>
int func(T t);  // Fallback if first template fails
```

**Impact**: Low priority for basic standard header support, needed for advanced metaprogramming.

---

## Progress Tracking

### Completed Features ✅

- Conversion operators, non-type template parameters, template specialization inheritance
- Reference members in structs, anonymous template parameters, type alias access from specializations  
- Most critical compiler intrinsics (20+ intrinsics including `__is_same`, `__is_base_of`, etc.)
- Basic preprocessor support, GCC/Clang builtin type macros, `__attribute__` and `noexcept` parsing

### Remaining Work 🔄

- **Priority 4.5**: Implement member template aliases (blocks `<type_traits>`)
- **Priority 5**: Validate/fix edge cases in some intrinsics (`__has_unique_object_representations`, `__is_constructible`, etc.)
- **Priority 8**: Improve preprocessor expression handling (non-blocking)
- **Priority 9**: SFINAE and advanced template metaprogramming (lower priority)

### Standard Header Status

- `<type_traits>` - ⚠️ May still have some missing intrinsics or edge cases
- `<utility>` - ⚠️ Depends on `<type_traits>`
- `<vector>` - ❌ May need additional features
- `<algorithm>` - ❌ May need additional features

---

## Test References

**Completed Feature Tests** (all passing):
- `tests/test_type_alias_from_specialization.cpp` - Type alias access from specializations
- `tests/test_anonymous_template_params.cpp` - Anonymous template parameters
- `tests/test_is_same_intrinsic.cpp` - `__is_same` type trait intrinsic
- `tests/test_struct_ref_members.cpp` - Reference member support
- `tests/test_struct_ref_member_simple.cpp` - Simple reference member test

**Intrinsics & Standard Headers**:
- `tests/test_type_traits_intrinsics.cpp` - Comprehensive type traits test (needs validation)
- `tests/test_real_std_headers_fail.cpp` - Comprehensive standard header failure analysis

**Implementation References**:
- `src/Parser.cpp` - Type trait intrinsics (lines 10217-10400), anonymous parameters (lines 17657-17756), type alias registration (lines 19168-19220)
- `src/CodeGen.h` - Type trait evaluation (lines 10215+)
- `src/TemplateRegistry.h` - Template instantiation tracking

---

## How to Update This Document

**When working on a feature**:
1. Update the **Status** field (❌ → 🔄 → ✅)
2. Move from "Remaining Work" to "Completed Features" when done
3. Document any new test cases created
4. Update **Last Updated** date at the top

**When adding a new missing feature**:
1. Add to "Remaining Work" section with priority
2. Include example code showing the problem
3. Explain root cause if known
4. List what standard library features depend on it
