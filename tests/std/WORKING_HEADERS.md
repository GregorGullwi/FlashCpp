# Working Standard Headers in FlashCpp

## Successfully Compiling Headers ✅

### C Library Wrappers
These headers compile successfully as they are mostly simple wrappers around C library headers:

- **`<cstddef>`** - Compiles in ~790ms ✅
  - Provides `std::size_t`, `std::ptrdiff_t`, `std::nullptr_t`
  
- **`<cstdint>`** - Compiles in ~200ms ✅
  - Provides `std::int32_t`, `std::uint64_t`, etc.
  
## Known Blockers for Other Headers ❌

### Main Blocker: Complex Template Metaprogramming in `<type_traits>`

Line 175 of `<type_traits>` contains advanced template metaprogramming that the parser currently cannot handle:

```cpp
template<typename... _Bn>
auto __or_fn(int) -> __first_t<false_type,
                               __enable_if_t<!bool(_Bn::value)>...>;
```

This pattern requires parsing:
1. Negation operator `!` in template argument context
2. Type conversion `bool(...)` as a template argument expression  
3. Parameter pack member access `_Bn::value`
4. Parameter pack expansion `...`

**Impact**: Since `<type_traits>` is included by most C++ standard library headers (`<utility>`, `<algorithm>`, etc.), this single parsing issue blocks almost all C++  standard library headers.

### Affected Headers
The following headers all include `<type_traits>` and therefore cannot compile:
- `<utility>`
- `<limits>` (also times out due to template complexity)
- `<memory>`
- `<vector>`
- `<string>`
- `<algorithm>`
- `<functional>`
- Most other C++ standard library headers

## Recommendations

### For Users
Until the template metaprogramming parser is improved:
1. Use C library wrapper headers (`<cstddef>`, `<cstdint>`, `<cstdio>`, etc.)
2. Write simple custom utilities instead of using C++ standard library
3. Use compiler intrinsics directly where available

### For Development
Priority fixes to enable standard library support:
1. **Improve template argument expression parsing** to handle complex expressions like `!bool(_Bn::value)`
2. **Optimize template instantiation performance** to reduce timeouts
3. **Add support for advanced SFINAE patterns** used in standard library metaprogramming

## Testing

To test header compatibility:
```bash
cd /tmp
cat > test.cpp << 'ENDFILE'
#include <header_name>
int main() { return 0; }
ENDFILE

timeout 20 /path/to/FlashCpp test.cpp -o test.o
```

---

*Last Updated: December 25, 2024*
*FlashCpp Version: Development Build*
