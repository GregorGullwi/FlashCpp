# Flash Minimal Standard Library - Implementation Notes

## Summary

This directory contains experimental minimal standard library headers for FlashCpp. During implementation, several fundamental limitations were discovered that prevent these headers from being fully functional.

## Current Status: ⚠️ NOT FULLY WORKING

### Issues Discovered

#### 1. Static Constexpr Member Access in Templates (CRITICAL)
**Status**: Known limitation documented in STANDARD_HEADERS_MISSING_FEATURES.md line 188-202

FlashCpp cannot reliably access static constexpr members in template classes, even in the simplest cases:

```cpp
template<int v>
struct MyConst {
    static constexpr int value = v;
};

int main() {
    return MyConst<42>::value;  // ❌ Returns 1 instead of 42
}
```

**Impact**: This breaks the fundamental `integral_constant` pattern that all type traits depend on.

**Document Reference**: See "Static Constexpr Members in Templates" in STANDARD_HEADERS_MISSING_FEATURES.md

#### 2. Parser Limitations with Type Aliases

FlashCpp's parser cannot handle pointer or reference types in `using` declarations within templates:

```cpp
template<typename T>
struct Test {
    using type = T*;    // ❌ Parser error
    typedef T* type2;   // ✅ Works with typedef
};
```

**Workaround**: Use `typedef` instead of `using` for type aliases (already applied in headers).

#### 3. Template Base Class with Expressions

Cannot use expressions in template base class arguments:

```cpp
template<typename T>
struct Test : bool_constant<is_int<T> || is_float<T>> {};  // ❌ Parser error
```

**Workaround**: Move logic into static constexpr member instead (already applied in headers).

#### 4. Template Instantiation Performance

Even simplified headers with moderate template complexity timeout (>30 seconds):
- Root cause: Volume of template instantiations, not speed of individual templates
- Profiling shows individual templates are fast (20-50μs) but headers need hundreds/thousands

### What Does Work

Based on existing passing tests in the main test suite:

✅ Simple template classes without static constexpr members  
✅ Template functions  
✅ Type trait compiler intrinsics (`__is_same`, `__is_class`, etc.)  
✅ Conversion operators (when not combined with static members in templates)  
✅ Basic template specialization  
✅ `typedef` for type aliases (not `using`)  

### Recommendations

**For Users**:
1. DO NOT use these headers in production - they are incomplete
2. Use FlashCpp's compiler intrinsics directly: `__is_same(T, U)`, `__is_class(T)`, etc.
3. Write simple custom utilities specific to your needs
4. Avoid patterns that require static constexpr members in templates

**For FlashCpp Development**:
1. Fix static constexpr member access in templates (highest priority for std library support)
2. Continue template instantiation performance optimization
3. Once #1 is fixed, these headers can be revisited

## Files in This Directory

- `flash_type_traits.h` - Type traits (NON-FUNCTIONAL due to static member issue)
- `flash_utility.h` - Utilities like move, forward, pair (PARTIALLY WORKING)
- `flash_limits.h` - Numeric limits (NON-FUNCTIONAL due to static member issue)
- `README.md` - Original documentation (now superseded by this file)

## Testing

Tests in parent directory will not work reliably:
- `test_flash_type_traits_ret42.cpp` - Times out or fails
- `test_flash_utility_ret42.cpp` - Untested
- `test_flash_limits_ret42.cpp` - Would fail due to static member issue

Simple demonstration:
- `demo_simple_type_traits_ret42.cpp` - Compiles but returns wrong value
- `demo_simple_int_const_ret42.cpp` - Simplest case, demonstrates the core issue

## Conclusion

The work in this directory demonstrates that **standard library support in FlashCpp requires fixing the static constexpr member access issue first**. The headers themselves are well-designed and would work once that core limitation is resolved.

**Next Steps for FlashCpp Project**:
1. Prioritize fixing static constexpr member access in templates
2. Continue template performance optimization  
3. Once fixed, use these headers as a foundation for standard library support

## References

- Main documentation: `tests/std/STANDARD_HEADERS_MISSING_FEATURES.md`
- Known issue documented at lines 188-202: "Static Constexpr Members in Templates"
- Recent fixes documented from line 840 onward

---

*Last Updated: December 25, 2024*  
*Status: Experimental / Non-functional due to compiler limitations*
