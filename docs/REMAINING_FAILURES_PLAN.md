# Remaining Test Failures Plan

## Current Status (2026-01-01)
**796/796 tests passing compilation (100%)**

## Remaining Runtime Issues

### 1. Exception Handling (2 files) - **Link Failure**
- `test_exceptions_basic.cpp` - Missing typeinfo symbols (_ZTIi)
- `test_exceptions_nested.cpp` - Missing typeinfo symbols (_ZTIi)

**Effort**: Large - requires implementing DWARF exception tables and runtime hooks

### 2. Spaceship Operator (1 file) - **Large Effort**
- `spaceship_default.cpp` - defaulted spaceship operator (segfaults at runtime)

**Effort**: Large - needs proper defaulted operator implementation

### 3. Variadic Arguments (1 file) - **Large Effort**
- `test_va_implementation.cpp` - va_list/va_arg implementation (segfaults at runtime)

**Effort**: Large - requires proper System V ABI va_list handling

### 4. Virtual Function via Reference - **Known Issue**
- Calling virtual functions through a reference (`base_ref.method()` where `base_ref` is `Base&` bound to `Derived`) crashes
- This is different from pointer dispatch which works correctly

### 5. Access Control Flag (1 file) - **Requires Special Flag**
- `test_no_access_control_flag.cpp` - Works when compiled with `-fno-access-control` flag

---
*Last Updated: 2026-01-01*
