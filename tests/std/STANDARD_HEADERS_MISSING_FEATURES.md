# Standard Headers Missing Features

This document summarizes the remaining gaps that block full standard header compilation. It intentionally stays compact; detailed implementation history has been removed.

For current header status, first stops, and timings, see `tests/std/README_STANDARD_HEADERS.md`.

## Primary Blockers

### 1. `subrange` `begin`/`end` CPO materialization (codegen)
`std::begin`/`std::end` of an instantiated `subrange` still lower as `std::ranges::_Begin::_Cpo` with a fake `int` return. This is now the shared stop for `<optional>`, `<any>`, `<array>`, `<span>`, `<numeric>`, and `<iterator>` after concept-id evaluation started working.

### 2. Exception / RTTI runtime symbols (link)
`<new>`, `<exception>`, and `<typeinfo>` now compile, but link still misses `__ExceptionPtrCompare`, `terminate`, `type_info`, and related CRT symbols.

### 3. Allocator / container instantiation
`<vector>` still fails instantiating `_Pocca`. `<deque>` / `<set>` still reject dependent qualified type names without `typename`.

### 4. Iterator + Ranges Concepts
`<ranges>` still hits the template-instantiation iteration limit. `<algorithm>` still cannot find `_Optimistic_count` as a constant array bound.

### 5. MSVC atomic / math lowering
`<atomic>` family is past `__iso_volatile_store32` and stops on `_InterlockedCompareExchange128`. `<cmath>` is past `__ceilf` and stops in `_Bit_cast` IR.

### 6. Alias-template recursion
`<variant>` currently stack-overflows in `materializeAliasTemplateInstantiation`.

## Tracking Files

- `tests/std/README_STANDARD_HEADERS.md` (status + timings)
- `tests/std/test_real_std_headers_fail.cpp` (full include stress test)
- `tests/std/test_std_headers_comprehensive.sh` (per-header timeout sweep)
