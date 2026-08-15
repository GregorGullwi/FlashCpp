# Standard Headers Missing Features

This document summarizes the remaining gaps that block full standard header compilation. It intentionally stays compact; detailed implementation history has been removed.

For current header status, first stops, and timings, see `tests/std/README_STANDARD_HEADERS.md`.

## Primary Blockers

### 1. `subrange` `begin`/`end` CPO materialization (codegen)
`std::begin`/`std::end` of an instantiated `subrange` still lower as `std::ranges::_Begin::_Cpo` with a fake `int` return. This is now the shared stop for `<optional>`, `<any>`, `<array>`, `<span>`, `<numeric>`, `<iterator>`, `<deque>`, and `<stack>` after C++20 omitted-`typename` parsing started working.

### 2. Exception / RTTI runtime symbols (link)
`<new>`, `<exception>`, and `<typeinfo>` now compile, but link still misses `__ExceptionPtrCompare`, `terminate`, `type_info`, and related CRT symbols.

### 3. Allocator / container instantiation
`<vector>` / `<queue>` still fail instantiating `_Pocca`. `<list>` / `<functional>` still fail `_Pocma`. `<map>` / `<set>` still reject instantiated `noexcept` as non-constant.

### 4. Iterator + Ranges Concepts
`<ranges>` still hits the template-instantiation iteration limit. `<algorithm>` still cannot find `_Optimistic_count` as a constant array bound. `<string_view>` still fails lazy `_String_view_iterator::operator+` replay.

### 5. String layout `sizeof`
`<string>`, `<fstream>`, `<chrono>`, and `<stdexcept>` now parse MSVC `explicit` deduction guides, then stop because `sizeof(_Ty)` in `_String_val` stays incomplete after substitution.

### 6. MSVC atomic lowering
`<atomic>` family is past `__iso_volatile_store32` and stops on `_InterlockedCompareExchange128`.

### 7. Alias-template recursion
`<variant>` currently stack-overflows in `materializeAliasTemplateInstantiation`.

## Tracking Files

- `tests/std/README_STANDARD_HEADERS.md` (status + timings)
- `tests/std/test_real_std_headers_fail.cpp` (full include stress test)
- `tests/std/test_std_headers_comprehensive.sh` (per-header timeout sweep)
