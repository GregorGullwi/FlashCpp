# Template Function Inlining Implementation Plan

## Problem Statement

Currently, the compiler uses name-based detection to inline template functions like `std::move`. This is a workaround that:
- Relies on hardcoded function names
- Doesn't follow the proper "pure expression" analysis approach used by clang
- Could break with user-defined functions that happen to match the pattern

The proper solution is to analyze the function body to determine if it's a "pure expression" that can be inlined vs. has runtime behavior requiring a function call.

## Current Blocker: Template Body Parsing

**DISCOVERED ISSUE**: The proper implementation is blocked by template body parsing failures.

When `std::move` is instantiated, the body contains:
```cpp
{
    using ReturnType = typename remove_reference<T>::type&&;
    return static_cast<ReturnType>(arg);
}
```

The `using` statement with `typename remove_reference<T>::type&&` fails to parse during template instantiation, resulting in:
- The instantiated function has NO body (`get_definition()` returns empty)
- Without a body, the pure expression analysis cannot work
- The function generates an external call instead of being inlined

**Root Cause**: The parser's `parse_using_directive_or_declaration()` function fails when encountering complex type expressions like `typename remove_reference<T>::type&&` during template instantiation. Even though `T` is bound to a concrete type (e.g., `int`), the parser cannot resolve the nested template member type.

## Implementation Status

### Completed
- ✅ Design document created
- ✅ `isPureExpressionFunction()` helper implemented
- ✅ Integration with `generateFunctionCallIr()` completed
- ✅ Debug logging added

### Blocked
- ❌ Cannot detect `std::move` as pure expression (no function body)
- ❌ Cannot inline `std::move` using body analysis (no function body)

### Current Workaround
The name-based detection (`func_name_view.starts_with("std::move")`) remains necessary because:
1. It works for the immediate problem
2. Proper solution requires fixing template body parsing first
3. All 654 tests pass with the workaround

## Path Forward

### Option 1: Fix Template Body Parsing First (Recommended)
**Prerequisite work needed:**
1. Fix `parse_using_directive_or_declaration()` to handle type-dependent expressions during template instantiation
2. Improve type resolution for `typename T::member` patterns where T is already bound
3. Handle rvalue reference types in using declarations

**Effort**: 10-15 hours
**Benefit**: Enables proper pure expression analysis AND fixes other template issues

### Option 2: Keep Name-Based Workaround
**Pros:**
- Works now
- Simple to maintain
- Low risk

**Cons:**
- Not scalable
- Doesn't follow compiler best practices
- Brittle for edge cases

### Option 3: Hybrid Approach (Recommended for now)
1. **Keep name-based workaround** for std::move (proven to work)
2. **Add pure expression analysis** for future use
3. **Document the blocker** so it can be fixed later
4. **Create separate issue** for template body parsing

This allows incremental progress without blocking the immediate fix.

## Clang's Approach

Clang emits a function call when:
- The function performs computation
- It creates objects
- It has observable side effects
- The ABI requires a call

Clang inlines/substitutes when:
- The function body is a pure expression
- The return is a reference/cast  
- There is no runtime effect

## Implementation Details

### isPureExpressionFunction() (Implemented)

Location: `src/CodeGen.h`

**Algorithm:**
1. Check if function has a definition body
2. If no body, return `false`
3. Iterate through all statements in the body
4. Allow only:
   - Return statements with static_cast, reinterpret_cast, or simple identifiers
   - Typedef declarations (compile-time only)
5. Reject:
   - Variable declarations
   - Function calls
   - Computation
   - Control flow

**Current Limitation**: Requires a parsed function body, which template instantiations currently lack due to parsing issues.

### Integration (Implemented)

Updated `generateFunctionCallIr()`:
```cpp
// Look up function in symbol table
auto func_symbol = symbol_table.lookup(func_name_view);
if (!func_symbol.has_value()) {
    func_symbol = gSymbolTable.lookup(func_name_view);
}

// Check if it's a pure expression
if (func_symbol.has_value() && func_symbol->is<FunctionDeclarationNode>()) {
    const FunctionDeclarationNode& func_decl = func_symbol->as<FunctionDeclarationNode>();
    if (isPureExpressionFunction(func_decl) && args.size() == 1) {
        // Inline by returning the argument
        return visitExpressionNode(arg);
    }
}
```

## Test Results

### std::move Test
- **Status**: ✅ PASSES (with name-based workaround)
- **Pure expression analysis**: ❌ FAILS (no function body)
- **Link**: ✅ SUCCEEDS (name-based inlining works)

### Diagnostic Output
```
[DEBUG][Codegen] Checking function for inlining: std::move_int, found=1, is_func_decl=1
[DEBUG][Codegen]   No function body
[DEBUG][Codegen] Function std::move_int is_pure=0, args=1
```

This confirms the instantiated function has no body due to parsing failure.

## Next Steps

1. **Commit current implementation** with name-based workaround
2. **Create separate issue** for template body parsing
3. **Document limitation** in code comments
4. **Enable pure expression analysis** once template parsing is fixed

## Success Criteria (Updated)

1. ✅ `test_std_move_support.cpp` continues to pass
2. ✅ All existing 654 tests pass
3. ✅ Implementation plan document created
4. ✅ Pure expression analysis implemented (for future use)
5. ⏳ Template body parsing (separate issue)

## Future Enhancements

### Phase 1: Fix Template Body Parsing
- Handle `using` statements with typename-dependent types
- Resolve member types during template instantiation
- Support rvalue reference in type aliases

### Phase 2: Enable Pure Expression Analysis
- Once template bodies parse correctly
- Remove name-based workaround
- Add tests for custom pure expression functions

### Phase 3: Advanced Inlining
- Inline functions with simple computation
- Cost model for inlining
- Cross-module inlining (LTO)

### Phase 4: Constexpr Evaluation
- Fully evaluate constexpr functions at compile time
- Fold constant expressions
- Eliminate dead code from constexpr branches

## References

- Clang inlining heuristics
- C++ standard: constexpr and inline semantics
- Existing compiler code: template instantiation in Parser.cpp
- AST node types: BlockNode, ReturnStatementNode, etc.
- **Known Issue**: Template body parsing failures (see Parser.cpp:6097-6108)
