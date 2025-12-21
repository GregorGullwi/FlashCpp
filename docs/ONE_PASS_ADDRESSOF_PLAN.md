# One-Pass AddressOf Implementation Plan

## Overview

This document outlines a plan to refactor AddressOf expression handling to calculate addresses in one pass, similar to how Clang and GCC handle `&arr[i].member.submember` expressions.

**Current Status**: Single-level cases work (`&arr[i].member`), but multi-level nesting has issues.

**Goal**: Compute complete address offset in one pass during IR generation, eliminating separate operations and supporting arbitrary nesting depth.

---

## Current Implementation Analysis

### How It Works Now

**For `&arr[i].x`:**
1. Generate `ArrayElementAddress` IR: computes address of `arr[i]`
2. Generate `Add` IR: adds member offset `x` to result
3. Two separate IR operations combined during code generation

**For `&obj.member`:**
1. Generate `AddressOfMember` IR: computes `LEA [RBP + obj_offset + member_offset]`
2. Single operation with combined offset

### Problems with Current Approach

1. **Multi-level member access fails**: `&arr[i].member1.member2`
   - After computing `arr[i]` address, only first member offset is added
   - Subsequent member offsets are not accumulated
   - Result: wrong address, wrong value

2. **Nested array subscripts hang**: `&arr[i].inner_arr[j].member`
   - Recursion or infinite loop in IR generation
   - Likely due to nested subscript handling within member access context

3. **Inconsistent approaches**:
   - Simple member: uses `AddressOfMember` (combined offset)
   - Array + member: uses `ArrayElementAddress` + `Add` (separate ops)
   - Creates complexity and edge cases

---

## Proposed One-Pass Implementation

### Design Philosophy

**Key Insight**: Address = Base + (Index * ElementSize) + MemberOffset1 + MemberOffset2 + ...

All components can be computed during AST traversal in `visitExpressionNode` for AddressOf expressions.

### New IR Opcode: `ComputeAddress`

```cpp
struct ComputeAddressOp {
    TempVar result;                    // Result temporary variable
    
    // Base address (one of these)
    std::variant<StringHandle, TempVar> base;  // Variable name or temp
    
    // Array indexing (optional, can have multiple for nested arrays)
    struct ArrayIndex {
        std::variant<unsigned long long, TempVar, StringHandle> index;
        int element_size_bits;         // Size of array element
    };
    std::vector<ArrayIndex> array_indices;
    
    // Member offset accumulation (for chained member access)
    int total_member_offset;           // Sum of all member offsets
    
    Type result_type;                  // Type of final address
    int result_size_bits;              // Size in bits
};
```

### Algorithm: Address Calculation Walk

When encountering `&expr` where `expr` involves member access and/or array subscripts:

```
function calculateAddress(expr, accumulated_offset = 0):
    if expr is Identifier:
        return AddressInfo(base=identifier, member_offset=accumulated_offset)
    
    if expr is MemberAccess(obj, member):
        member_info = lookupMember(obj.type, member)
        new_offset = accumulated_offset + member_info.offset
        return calculateAddress(obj, new_offset)
    
    if expr is ArraySubscript(array, index):
        base_info = calculateAddress(array, accumulated_offset)
        element_size = getActualElementSize(array.type)
        base_info.add_array_index(index, element_size)
        return base_info
    
    if expr is PointerDereference(*ptr):
        # Handle *ptr case - compute pointer value, not address
        ...
```

### Code Generation

In `IRConverter.h`, `handleComputeAddress`:

```cpp
void handleComputeAddress(const ComputeAddressOp& op) {
    // Load base address into RAX
    if (holds_alternative<StringHandle>(op.base)) {
        int offset = lookupVariableOffset(get<StringHandle>(op.base));
        emitLeaFromFrame(offset);  // LEA RAX, [RBP + offset]
    } else {
        // Load from temp var
        emitMovFromTemp(get<TempVar>(op.base));
    }
    
    // Process each array index
    for (const auto& arr_idx : op.array_indices) {
        // Load index into RCX
        loadIndexIntoRCX(arr_idx.index);
        
        // Multiply by element size
        emitMultiplyRCXByElementSize(arr_idx.element_size_bits);
        
        // Add to running address
        emitAddRAXRCX();
    }
    
    // Add accumulated member offset (if any)
    if (op.total_member_offset > 0) {
        emitAddImmediateToRAX(op.total_member_offset);
    }
    
    // Store result
    emitMovToTemp(op.result);
}
```

---

## Implementation Steps

### Phase 1: Refactor AddressOf Handling (CodeGen.h)

**File**: `src/CodeGen.h`

**Current location**: `generateUnaryOperatorIr()` handles `&` operator

**Changes needed**:

1. **Extract address calculation logic** into new function:
   ```cpp
   struct AddressComponents {
       std::variant<StringHandle, TempVar> base;
       std::vector<ArrayIndex> array_indices;
       int total_member_offset = 0;
       Type final_type;
       int final_size_bits;
   };
   
   std::optional<AddressComponents> analyzeAddressExpression(
       const ExpressionNode& expr);
   ```

2. **Implement recursive traversal**:
   - Start from innermost expression
   - Walk up the AST, accumulating offsets and indices
   - Handle MemberAccess, ArraySubscript, Identifier nodes
   - Detect unsupported patterns (e.g., function calls, complex expressions)

3. **Replace existing AddressOf code paths**:
   - Remove separate `AddressOfMember` generation for simple cases
   - Remove `ArrayElementAddress` + `Add` pattern
   - Generate single `ComputeAddress` IR operation
   - Fall back to old behavior if new analysis fails (safety net)

### Phase 2: Add ComputeAddress IR Opcode

**File**: `src/IRTypes.h`

1. Add `ComputeAddress` to `IrOpcode` enum
2. Define `ComputeAddressOp` struct as shown above
3. Add to `IrInstruction` variant
4. Implement debug printing

### Phase 3: Implement Code Generation

**File**: `src/IRConverter.h`

1. Add `handleComputeAddress()` function
2. Implement as outlined in "Code Generation" section above
3. Reuse existing emit helper functions where possible
4. Add new helpers if needed (e.g., `emitAddImmediateToRAX`)

### Phase 4: Handle Edge Cases

**Special cases to consider**:

1. **Pointer arithmetic**: `&ptr[i]` where ptr is pointer, not array
   - Need to distinguish pointer vs array in base
   - Pointer: load pointer value first, then add offset
   - Array: compute array element address

2. **References**: `&ref.member` where ref is reference
   - Dereference first, then compute offset
   - May need additional flag in `AddressComponents`

3. **Temporary objects**: `&getThing().member`
   - Can't take address of temporary
   - Should generate error or handle specially
   - Analysis function should return `std::nullopt`

4. **Bitfields**: `&obj.bitfield`
   - Can't take address of bitfield
   - Should be caught during analysis

5. **Const/volatile qualifiers**:
   - Preserve type qualifiers in result

### Phase 5: Testing Strategy

**Test progression**:

1. **Verify existing tests still pass** (641/661)
   - Run full test suite after Phase 3
   - Any regressions indicate fallback needed

2. **Test single-level cases**:
   - `&obj.member` (currently works)
   - `&arr[i].member` (currently works)
   - `&arr[5].member` (constant index)

3. **Test multi-level member access**:
   - `&arr[i].member1.member2` (currently broken)
   - `&obj.inner.value`
   - `&arr[i].inner.sub.value`

4. **Test nested array subscripts**:
   - `&arr[i].inner_arr[j]` (currently hangs)
   - `&arr[i].inner_arr[j].member`
   - `&matrix[i][j]`

5. **Test mixed combinations**:
   - `&ptr->member.submember`
   - `&(*ptr_arr)[i].member`
   - `&ref.array[i].member`

6. **Test edge cases**:
   - Pointers vs arrays
   - References
   - Const objects
   - Large offsets (> 127, need disp32)

### Phase 6: Performance Validation

**Compare generated assembly**:

1. Compile test cases with FlashCpp (one-pass impl)
2. Compile same tests with Clang
3. Compare instruction sequences
4. Verify FlashCpp generates similar or better code
5. Check no unnecessary intermediate stores/loads

**Benchmarks**:
- Compile time: should be similar or faster (fewer IR ops)
- Code size: should be similar (same final instructions)
- Runtime: should be identical (same address calculation)

---

## Rollback Strategy

**If implementation causes regressions**:

1. Keep old code paths behind feature flag
2. Add `--use-legacy-addressof` compiler option
3. Allow gradual migration of test cases
4. Only remove old code when all tests pass with new implementation

**Feature flag implementation**:
```cpp
bool use_one_pass_addressof = true;  // Default to new implementation

if (use_one_pass_addressof) {
    auto addr_components = analyzeAddressExpression(operand);
    if (addr_components) {
        // Generate ComputeAddress IR
    } else {
        // Fall back to legacy
    }
} else {
    // Legacy implementation
}
```

---

## Expected Benefits

1. **Correctness**: Multi-level member access and nested arrays will work
2. **Simplicity**: Single code path for all AddressOf cases
3. **Performance**: Fewer IR operations, more optimization opportunities
4. **Maintainability**: Easier to understand and extend
5. **Consistency**: Matches how Clang/GCC handle these expressions

---

## Risks and Mitigations

### Risk 1: Breaking Working Tests

**Mitigation**:
- Implement feature flag for gradual rollout
- Keep legacy code as fallback
- Extensive testing at each phase

### Risk 2: Missing Edge Cases

**Mitigation**:
- Comprehensive test suite covering all patterns
- Static analysis to detect unsupported expressions
- Clear error messages when analysis fails

### Risk 3: Performance Regression

**Mitigation**:
- Benchmark before and after
- Profile compile time and code generation
- Compare assembly output with Clang

### Risk 4: Complexity in analyzeAddressExpression

**Mitigation**:
- Break into smaller helper functions
- Unit test the analysis logic separately
- Document invariants and assumptions
- Add assertions to catch violations

---

## Timeline Estimate

**Phase 1** (Refactor AddressOf): 2-3 days
- Extract and implement analyzeAddressExpression
- Handle all node types (Identifier, MemberAccess, ArraySubscript)
- Integration with existing code

**Phase 2** (Add IR Opcode): 1 day
- Define structures
- Add to variant
- Debug printing

**Phase 3** (Code Generation): 2-3 days
- Implement handleComputeAddress
- Handle all index types (constant, TempVar, StringHandle)
- Test with simple cases

**Phase 4** (Edge Cases): 2-3 days
- Implement special case handling
- Error detection
- Type qualifier preservation

**Phase 5** (Testing): 3-4 days
- Create comprehensive test suite
- Fix bugs found during testing
- Verify all existing tests pass

**Phase 6** (Performance): 1-2 days
- Benchmark and compare
- Optimize if needed

**Total**: 11-16 days

---

## Success Criteria

1. ✅ All existing 641 tests continue to pass
2. ✅ Multi-level member access works: `&arr[i].m1.m2` returns correct value
3. ✅ Nested array subscripts work: `&arr[i].arr2[j]` compiles without hanging
4. ✅ Generated assembly matches Clang quality
5. ✅ No performance regressions (compile time or runtime)
6. ✅ Code is cleaner and more maintainable than before
7. ✅ Default-init bug (if related) is fixed
8. ✅ Documentation updated to reflect new approach

---

## Related Issues to Investigate

While implementing one-pass address calculation, also investigate:

1. **Default-initialization bug** (`test_struct_default_init_addressof.cpp`)
   - May be unrelated to address calculation
   - But worth checking if one-pass fixes it
   - Could be constructor call ordering issue

2. **While loop bug** (`test_pointer_loop.cpp`)
   - Separate from AddressOf
   - Should not be affected by this change
   - Document if behavior changes

3. **Other pointer-related crashes**
   - Check if one-pass approach fixes any other failing tests
   - Update test count if more tests pass

---

## Future Enhancements

After one-pass implementation is stable:

1. **Optimize constant folding**: When all indices are constants, compute offset at compile time
2. **LEA optimization**: For simple cases, use single LEA instruction with SIB byte
3. **Strength reduction**: Convert multiplies by power-of-2 to shifts
4. **Common subexpression elimination**: Reuse computed addresses

---

*Document created: 2025-12-21*
*Related PR: Fix AddressOf member access and ArrayElementAddress bugs*
*Status: Planning phase - not yet implemented*
