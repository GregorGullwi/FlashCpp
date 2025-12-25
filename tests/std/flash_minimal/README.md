# Flash Minimal Standard Library Headers

This directory contains minimal, working implementations of common C++ standard library utilities designed specifically for FlashCpp. These headers provide practical alternatives to the full standard library headers, which may timeout or fail due to template instantiation performance issues.

## Available Headers

### `flash_type_traits.h`
Provides type trait utilities using FlashCpp's compiler intrinsics.

**Includes:**
- `integral_constant`, `true_type`, `false_type`, `bool_constant`
- Type relationships: `is_same`, `is_base_of`
- Primary type categories: `is_void`, `is_integral`, `is_floating_point`, `is_array`, `is_pointer`, `is_reference`, `is_class`, `is_enum`, `is_union`, etc.
- Type properties: `is_const`, `is_volatile`, `is_signed`, `is_unsigned`, `is_pod`, `is_trivially_copyable`, `is_polymorphic`, `is_abstract`, `is_final`, `is_aggregate`
  - Note: `has_virtual_destructor` is not available yet (requires compiler support)
- Type modifications: `remove_const`, `remove_volatile`, `remove_cv`, `remove_reference`, `remove_pointer`, `add_const`, `add_volatile`, `add_cv`, `add_lvalue_reference`, `add_rvalue_reference`, `add_pointer`
- Metaprogramming: `conditional`, `enable_if`, `void_t`
- Composite categories: `is_arithmetic`, `is_fundamental`, `is_compound`, `is_object`, `is_scalar`

**Usage:**
```cpp
#include "flash_type_traits.h"

using namespace flash_std;

static_assert(is_same_v<int, int>);
static_assert(is_integral_v<int>);
static_assert(!is_floating_point_v<int>);

using T = remove_const_t<const int>;  // T is int
```

### `flash_utility.h`
Provides utility functions and types.

**Includes:**
- `move` - Cast to rvalue reference
- `forward` - Perfect forwarding
- `addressof` - Get actual address bypassing operator&
- `swap` - Exchange values
- `pair` - Store two values (with comparisons)
- `make_pair` - Construct pair with type deduction
- `declval` - Obtain reference to type (unevaluated)
- `exchange` - Replace value and return old value
- `as_const` - Obtain const reference
- `integer_sequence`, `index_sequence` - Compile-time integer sequences

**Usage:**
```cpp
#include "flash_utility.h"

using namespace flash_std;

int x = 42;
int y = move(x);  // Move x to y

pair<int, double> p = make_pair(42, 3.14);
int first = p.first;   // 42
double second = p.second;  // 3.14
```

### `flash_limits.h`
Provides `numeric_limits` for compile-time numeric type information.

**Includes:**
- `numeric_limits<T>` specializations for:
  - `bool`, `char`, `signed char`, `unsigned char`
  - `short`, `unsigned short`
  - `int`, `unsigned int`
  - `long`, `unsigned long`
  - `long long`, `unsigned long long`
  - `float`, `double`

**Usage:**
```cpp
#include "flash_limits.h"

using namespace flash_std;

constexpr int max_int = numeric_limits<int>::max();  // 2147483647
constexpr bool is_signed = numeric_limits<int>::is_signed;  // true
constexpr int digits = numeric_limits<int>::digits;  // 31
```

## Design Principles

These minimal headers follow these principles:

1. **Use compiler intrinsics** - Leverage FlashCpp's built-in intrinsics (`__is_same`, `__is_class`, `__builtin_addressof`, etc.) for efficiency
2. **Avoid deep template instantiation** - Keep template patterns simple to avoid timeouts
3. **Simple constexpr** - Use only C++14 constexpr features that FlashCpp supports well
4. **No dependencies** - Each header is self-contained or depends only on other flash_minimal headers
5. **Standard-compatible API** - APIs match standard library where possible for easy migration

## Why Use These Instead of Standard Headers?

**Current limitations of full standard library headers with FlashCpp:**
- ❌ Template instantiation timeouts (>10 seconds)
- ❌ Complex SFINAE and metaprogramming patterns
- ❌ Advanced constexpr features not yet optimized
- ❌ Allocator dependencies

**Benefits of flash_minimal headers:**
- ✅ Fast compilation (milliseconds)
- ✅ All features actually work
- ✅ Designed for FlashCpp's current capabilities
- ✅ Practical and immediately usable

## Testing

Each header includes comprehensive tests in the parent directory:
- `test_flash_type_traits_ret42.cpp` - Tests type traits
- `test_flash_utility_ret42.cpp` - Tests utility functions
- `test_flash_limits_ret42.cpp` - Tests numeric limits

Run tests with:
```bash
cd /home/runner/work/FlashCpp/FlashCpp
make main CXX=clang++

# Compile a test
cd /tmp
/home/runner/work/FlashCpp/FlashCpp/x64/Debug/FlashCpp \
    -I/home/runner/work/FlashCpp/FlashCpp/tests/std/flash_minimal \
    /home/runner/work/FlashCpp/FlashCpp/tests/std/test_flash_type_traits_ret42.cpp \
    -o test.o

# Link and run
clang++ test.o -o test && ./test
echo $?  # Should print 42
```

## Future Work

As FlashCpp gains more features, these headers can:
1. Be extended with additional functionality
2. Serve as test cases for new compiler features
3. Eventually be replaced by full standard library support

## Contributing

When adding new utilities:
1. Keep implementations simple and fast to compile
2. Use FlashCpp intrinsics where available
3. Add comprehensive tests
4. Document limitations if any
5. Follow the existing code style

## Namespace

All utilities are in the `flash_std` namespace to avoid conflicts with the standard library. You can use:
```cpp
using namespace flash_std;  // Use flash_std utilities
// OR
namespace std = flash_std;  // Alias to std (use with caution)
```

## License

These headers are part of the FlashCpp project and follow the same license.
