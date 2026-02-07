# FlashCpp Known Bugs - Minimal Reproductions

This directory contains minimal reproduction cases for bugs discovered during C++20 integration testing. Last verified: 2026-02-07 with FlashCpp release build.

## Bug Summary

| # | Bug | File | Status | Stage |
|---|-----|------|--------|-------|
| 1 | Alternative operator tokens | *(moved to tests/)* | **FIXED** | ~~Compile~~ |
| 2 | Binary literals (0b prefix) | *(moved to tests/)* | **FIXED** | ~~Compile~~ |
| 3 | Namespace symbol lookup | *(moved to tests/)* | **FIXED** | ~~Link~~ |
| 4 | CTAD (deduction guides) | *(moved to tests/)* | **FIXED** | ~~Link~~ |
| 5 | if constexpr + sizeof... | `bug_if_constexpr.cpp` | Open | Link |
| 6 | Template specialization | *(moved to tests/)* | **FIXED** | ~~Runtime~~ |
| 7 | Variadic template recursion | `bug_variadic_recursion.cpp` | Open | Runtime |
| 8 | Digit separators | *(moved to tests/)* | **FIXED** | ~~Runtime~~ |
| 9 | new/delete in large files | *(moved to tests/)* | **FIXED** | ~~Runtime~~ |

### Previously reported, now fixed:
- ~~Boolean intermediate variables crash~~ -- **FIXED** (was crashing with assertion in IRTypes.h, now works correctly)
- ~~Alternative operator tokens~~ -- **FIXED** (lexer now maps alternative tokens to standard operator spellings)
- ~~Binary literals~~ -- **FIXED** (lexer now handles 0b/0B prefix at same level as 0x)
- ~~Namespace symbol lookup~~ -- **FIXED** (codegen now uses fully qualified names for namespace-scoped global variables)
- ~~Template specialization~~ -- **FIXED** (deduction-based instantiation now checks for explicit specializations)
- ~~Digit separators~~ -- **FIXED** (digit separators are stripped before numeric value conversion)
- ~~CTAD~~ -- **FIXED** (implicit CTAD now deduces template arguments from constructor parameter types)
- ~~new/delete in large files~~ -- **FIXED** (heap alloc/free now uses platform-correct calling convention register)

## Remaining Open Bugs

### 5. if constexpr + sizeof... (Link Error)
**File**: `bug_if_constexpr.cpp`
**Severity**: High -- key C++17/20 pattern for compile-time branching in templates

Using `if constexpr (sizeof...(rest) == 0)` in variadic templates compiles but fails to link.

**Error**: Unresolved symbols during linking

**Root cause**: Variadic template function body re-parsing does not properly expand parameter packs. The `sizeof...(rest)` expression requires the pack size to be known at instantiation time, and recursive calls like `variadic_sum(rest...)` require pack expansion.

**Fix needed**: Implement proper variadic template function body re-parsing with pack expansion support, including `sizeof...` evaluation and recursive instantiation.

### 7. Variadic Template Recursion (Runtime Failure)
**File**: `bug_variadic_recursion.cpp`
**Severity**: Medium -- fold expressions work as a workaround

Recursive variadic template expansion using overload resolution (`var_sum(T first, Rest... rest)` calling `var_sum(rest...)`) produces incorrect results. The overload resolution now correctly selects the variadic overload (via argument count check), but the variadic function body is not properly re-parsed due to the same pack expansion limitation as Bug #5.

**Error**: Compiles but the variadic template body is empty after re-parsing

**Root cause**: Same as Bug #5 -- variadic template parameter pack expansion in function bodies is not yet implemented. The template body re-parsing registers `Rest` as a single type instead of a pack, and `rest...` expansion is not performed.

**Workaround**: Use fold expressions instead of recursive expansion.

## Testing These Bugs

Each file can be tested independently:

```bash
# Verify with standard clang++ (should compile and return 0)
clang++ -std=c++20 bug_if_constexpr.cpp -o test && ./test
echo $?  # Should print 0

# Test with FlashCpp (will demonstrate the bug)
../../x64/Debug/FlashCpp bug_if_constexpr.cpp -o test.o
```

## Contributing

When you find a new FlashCpp bug:
1. Create a minimal reproduction case (smallest possible file that triggers it)
2. Verify it compiles and runs correctly with `clang++ -std=c++20`
3. Add it to this directory following the naming pattern: `bug_description.cpp`
4. Include comments documenting expected vs actual behavior and suggested fix
5. Update this README with the new bug entry
