# Standard Headers Missing Features

This document summarizes the remaining gaps that block full standard header compilation. It intentionally stays compact; detailed implementation history has been removed.

For current header status, first stops, and timings, see `tests/std/README_STANDARD_HEADERS.md`.

## Primary Blockers

### 1. `subrange` `begin`/`end` CPO materialization (codegen)
`std::begin`/`std::end` of an instantiated `subrange` still lower as `std::ranges::_Begin::_Cpo` with a fake `int` return. This is now the shared stop for `<optional>`, `<any>`, `<array>`, `<span>`, `<numeric>`, `<iterator>`, `<deque>`, and `<stack>` after C++20 omitted-`typename` parsing started working.

### 2. Exception / RTTI runtime symbols (link)
`<new>`, `<exception>`, and `<typeinfo>` now compile, but link still misses `__ExceptionPtrCompare`, `terminate`, `type_info`, and related CRT symbols.

### 3. Allocator / container instantiation
`<vector>` / `<queue>` still fail instantiating `_Pocca`. `<string>` / `<list>` / `<fstream>` / `<chrono>` / `<stdexcept>` now get past instantiated-`noexcept` type-trait evaluation and stop on the sticky `allocator_traits` instantiation-iteration limit. `<map>` / `<set>` still reject instantiated `noexcept` because `conjunction$…::value` is missing after deferred `_Conjunction` bases. Member-template defaults that use the injected-class-name (`Myself = pair`) now rewrite to the current specialization.

### 4. Iterator + Ranges Concepts
`<ranges>` still hits the template-instantiation iteration limit. `<algorithm>` still cannot find `_Optimistic_count` as a constant array bound. `<string_view>` still fails lazy `_String_view_iterator::operator+` replay.

### 5. MSVC atomic lowering
`<atomic>` family is past `__iso_volatile_store32` and stops on `_InterlockedCompareExchange128`.

### 6. Alias-template recursion
`<variant>` currently stack-overflows in `materializeAliasTemplateInstantiation`.

## Tracking Files

- `tests/std/README_STANDARD_HEADERS.md` (status + timings)
- `tests/test_injected_class_name_member_default_ret0.cpp` (C++20 [temp.local] member-template defaults)
- `tests/std/test_real_std_headers_fail.cpp` (full include stress test)
- `tests/std/test_std_headers_comprehensive.sh` (per-header timeout sweep)
