# Remaining Test Failures Plan

## Current Status (2026-01-01)
**797/797 tests passing compilation (100%)**

## Recently Fixed

### ~~4. Virtual Function via Reference~~ - **FIXED (2026-01-01)**
- ~~Calling virtual functions through a reference (`base_ref.method()` where `base_ref` is `Base&` bound to `Derived`) crashed~~
- **Fix**: Updated `VirtualCallOp::is_pointer_access` in CodeGen.h to also check for reference types (`is_reference()` and `is_rvalue_reference()`), since references are implemented as pointers internally
- Added test: `test_virtual_via_reference_ret0.cpp`

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

### 4. Access Control Flag (1 file) - **Requires Special Flag**
- `test_no_access_control_flag.cpp` - Works when compiled with `-fno-access-control` flag

### 5. Virtual Destructor Symbol on MSVC (1 file) - **Link Failure on Windows**
- `test_xvalue_all_casts.cpp` - Missing virtual destructor symbol (`??1Base@@QAE@XZ`) when linked with MSVC
- Works on Linux/ELF but fails on Windows/COFF
- Added to expected link failures in `test_reference_files.ps1`

**Effort**: Medium - requires fixing COFF virtual destructor symbol generation

---
*Last Updated: 2026-01-01*
