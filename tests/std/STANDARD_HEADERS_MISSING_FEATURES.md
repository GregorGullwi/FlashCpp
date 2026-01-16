# Standard Headers Missing Features

This document summarizes the remaining gaps that block full standard header compilation. It intentionally stays compact; detailed implementation history has been removed.

For current header status and timings, see `tests/std/README_STANDARD_HEADERS.md`.

## Primary Blockers

### 1. Template Instantiation Volume (Timeouts)
Heavy template instantiation still causes >10s timeouts on many headers.

**Affected tests:**
- `tests/std/test_std_type_traits.cpp`
- `tests/std/test_std_concepts.cpp`
- `tests/std/test_std_utility.cpp`
- `tests/std/test_std_string_view.cpp`
- `tests/std/test_std_string.cpp`
- `tests/std/test_std_vector.cpp`
- `tests/std/test_std_array.cpp`
- `tests/std/test_std_tuple.cpp`
- `tests/std/test_std_optional.cpp`
- `tests/std/test_std_variant.cpp`
- `tests/std/test_std_memory.cpp`
- `tests/std/test_std_functional.cpp`
- `tests/std/test_std_algorithm.cpp`
- `tests/std/test_std_map.cpp`
- `tests/std/test_std_set.cpp`
- `tests/std/test_std_span.cpp`
- `tests/std/test_std_ranges.cpp`
- `tests/std/test_std_iostream.cpp`
- `tests/std/test_std_chrono.cpp`
- `tests/std/test_std_any.cpp`

### 2. Exception Handling Infrastructure
Incomplete exception runtime support blocks headers that rely on exceptions.

**Affected tests:**
- `tests/std/test_std_string.cpp`
- `tests/std/test_std_vector.cpp`
- `tests/std/test_std_iostream.cpp`
- `tests/std/test_std_memory.cpp`

**Related doc:** `docs/EXCEPTION_HANDLING_PLAN.md`

### 3. Allocator Support
Allocator infrastructure is not implemented, blocking most containers.

**Affected tests:**
- `tests/std/test_std_vector.cpp`
- `tests/std/test_std_string.cpp`
- `tests/std/test_std_map.cpp`
- `tests/std/test_std_set.cpp`
- `tests/std/test_std_memory.cpp`

### 4. Iterator + Ranges Concepts
Iterator traits, concepts, and ranges are still incomplete.

**Affected tests:**
- `tests/std/test_std_algorithm.cpp`
- `tests/std/test_std_ranges.cpp`
- `tests/std/test_std_concepts.cpp`

### 5. Type Erasure + RTTI
Type erasure patterns for `std::any` and `std::function` are incomplete.

**Affected tests:**
- `tests/std/test_std_any.cpp`
- `tests/std/test_std_functional.cpp`
- `tests/std/test_std_variant.cpp`

### 6. `std::initializer_list` Compiler Magic
Initializer-list construction still lacks compiler magic for brace-init overloads.

**Affected tests:**
- `tests/std/test_std_vector.cpp`
- `tests/std/test_std_string.cpp`
- `tests/std/test_std_array.cpp`

## Tracking Files

- `tests/std/README_STANDARD_HEADERS.md` (status + timings)
- `tests/std/test_real_std_headers_fail.cpp` (full include stress test)
- `tests/std/test_std_headers_comprehensive.sh` (per-header timeout sweep)
