# FlashCpp Known Bugs - Minimal Reproductions

This directory contains minimal reproduction cases for bugs discovered during C++20 integration testing. Last verified: 2026-02-07 with FlashCpp release build.

## Bug Summary

| # | Bug | File | Status | Stage |
|---|-----|------|--------|-------|
| 1 | Alternative operator tokens | `bug_alternative_tokens.cpp` | Open | Compile |
| 2 | Binary literals (0b prefix) | `bug_binary_literals.cpp` | Open | Compile |
| 3 | Namespace symbol lookup | `bug_namespace_symbol_lookup.cpp` | Open | Link |
| 4 | CTAD (deduction guides) | `bug_ctad.cpp` | Open | Link |
| 5 | if constexpr + sizeof... | `bug_if_constexpr.cpp` | Open | Link |
| 6 | Template specialization | `bug_template_specialization.cpp` | Open | Runtime |
| 7 | Variadic template recursion | `bug_variadic_recursion.cpp` | Open | Runtime |
| 8 | Digit separators | `bug_digit_separators.cpp` | Open | Runtime |
| 9 | new/delete in large files | `bug_new_delete_combined.cpp` | Open | Runtime (crash) |

### Previously reported, now fixed:
- ~~Boolean intermediate variables crash~~ -- **FIXED** (was crashing with assertion in IRTypes.h, now works correctly)

## Detailed Bug Descriptions

### 1. Alternative Operator Tokens (Compile Error)
**File**: `bug_alternative_tokens.cpp`
**Severity**: Medium -- workaround is trivial (use `&` instead of `bitand`, etc.)

The lexer does not recognize ISO 646 alternative operator representations: `bitand`, `bitor`, `xor`, `compl`, `and`, `or`, `not`, `and_eq`, `or_eq`, `xor_eq`, `not_eq`.

**Error**: `Unknown keyword: b` when parsing `a bitand b`

**Fix needed**: Add alternative token recognition to the lexer. These should be treated as operator aliases during tokenization.

### 2. Binary Literals (Compile Error)
**File**: `bug_binary_literals.cpp`
**Severity**: Low -- workaround is to use hex or decimal

The lexer does not recognize the `0b`/`0B` prefix for binary integer literals (C++14 feature).

**Error**: `Missing identifier: b1010` -- interprets `0b1010` as `0` followed by `b1010`

**Fix needed**: Extend numeric literal lexing to handle `0b`/`0B` prefix followed by `[01']+` digits.

### 3. Namespace Symbol Lookup (Link Error)
**File**: `bug_namespace_symbol_lookup.cpp`
**Severity**: High -- namespaces are fundamental to C++

Variables declared inside namespaces are not accessible via qualified names (`ns::value`).

**Error**: Unresolved symbol during linking

**Fix needed**: Ensure code generation emits symbols for namespace-scoped variables and generates correct references for qualified access.

### 4. CTAD - Class Template Argument Deduction (Link Error)
**File**: `bug_ctad.cpp`
**Severity**: Medium -- workaround is to use explicit template arguments

When CTAD is used (`Box box(42)` instead of `Box<int> box(42)`), the compiler fails to instantiate the template with the deduced type.

**Error**: `undefined reference to 'Box::Box(T)'` and `'Box::get()'`

**Fix needed**: Implement basic constructor argument deduction to determine template parameter types and trigger instantiation.

### 5. if constexpr + sizeof... (Link Error)
**File**: `bug_if_constexpr.cpp`
**Severity**: High -- key C++17/20 pattern for compile-time branching in templates

Using `if constexpr (sizeof...(rest) == 0)` in variadic templates compiles but fails to link.

**Error**: Unresolved symbols during linking

**Fix needed**: Ensure `if constexpr` properly evaluates `sizeof...` at compile time and only generates code for the taken branch.

### 6. Template Specialization (Runtime Failure)
**File**: `bug_template_specialization.cpp`
**Severity**: High -- explicit specialization is a core template feature

Explicit template specialization (`template<> int identity<int>(int)`) compiles and links, but the specialization is not selected at runtime.

**Error**: Returns wrong value -- primary template used instead of specialization

**Fix needed**: Template instantiation system must check for explicit specializations before instantiating the primary template.

### 7. Variadic Template Recursion (Runtime Failure)
**File**: `bug_variadic_recursion.cpp`
**Severity**: Medium -- fold expressions work as a workaround

Recursive variadic template expansion using overload resolution (`var_sum(T first, Rest... rest)` calling `var_sum(rest...)`) produces incorrect results.

**Error**: Returns wrong sum value

**Note**: Non-recursive patterns like fold expressions work correctly: `(args + ...)`

**Fix needed**: Investigate parameter pack expansion in recursive template instantiation.

### 8. Digit Separators (Runtime Failure)
**File**: `bug_digit_separators.cpp`
**Severity**: Low -- workaround is to omit the separators

Digit separators (`1'000'000`) compile but produce incorrect numeric values at runtime.

**Error**: `1'000'000 != 1000000` at runtime

**Fix needed**: Ensure the lexer strips all `'` characters from numeric literals before conversion.

### 9. new/delete in Large Translation Units (Runtime Crash)
**File**: `bug_new_delete_combined.cpp`
**Severity**: High -- dynamic allocation is fundamental

`new int(42)` works correctly in small standalone files, but in larger translation units (with many functions/classes), the generated code passes an enormous allocation size to `operator new`, causing a segfault.

**Error**: `mmap(NULL, 139400265555968, ...)` = ENOMEM, then SIGSEGV

**Fix needed**: Investigate how the allocation size argument is computed in code generation. Likely a stack offset or register clobbering issue that manifests with more functions.

## Priority Assessment

### High Priority (core language features)
- **#3 Namespace symbol lookup** -- namespaces are fundamental
- **#5 if constexpr + sizeof...** -- key C++17/20 metaprogramming pattern
- **#6 Template specialization** -- core template feature
- **#9 new/delete in large files** -- dynamic allocation is essential

### Medium Priority (workarounds available)
- **#1 Alternative operator tokens** -- use standard operators instead
- **#4 CTAD** -- use explicit template arguments
- **#7 Variadic recursion** -- use fold expressions instead

### Low Priority (cosmetic / easy workarounds)
- **#2 Binary literals** -- use hex or decimal
- **#8 Digit separators** -- omit separators

## Testing These Bugs

Each file can be tested independently:

```bash
# Verify with standard clang++ (should compile and return 0)
clang++ -std=c++20 bug_alternative_tokens.cpp -o test && ./test
echo $?  # Should print 0

# Test with FlashCpp (will demonstrate the bug)
../../x64/Debug/FlashCpp bug_alternative_tokens.cpp -o test.o
# or release build:
../../x64/Release/FlashCpp bug_alternative_tokens.cpp -o test.o
```

## Contributing

When you find a new FlashCpp bug:
1. Create a minimal reproduction case (smallest possible file that triggers it)
2. Verify it compiles and runs correctly with `clang++ -std=c++20`
3. Add it to this directory following the naming pattern: `bug_description.cpp`
4. Include comments documenting expected vs actual behavior and suggested fix
5. Update this README with the new bug entry
