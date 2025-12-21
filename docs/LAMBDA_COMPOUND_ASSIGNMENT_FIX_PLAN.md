# Fix Plan: Lambda [*this] Compound Assignment Bug

## Problem Summary

Compound assignments in `[*this]` mutable lambdas fail when there are multiple lambdas in the same struct, or when there are multiple compound assignments in the same lambda. The second compound assignment generates incorrect IR that references a non-existent variable from an outer scope.

### Symptoms
```cpp
struct Counter {
    int value = 10;
    
    int test() {
        auto lambda = [*this]() mutable {
            value += 5;   // Works - generates: %3 = member_access 32 %this.__copy_this
                          //                    %5 = member_access int32 %3.value
            value *= 2;   // FAILS - generates: %9 = member_access int32 %c.value
                          //         where 'c' is from outer scope (main)
            return value;
        };
        return lambda();
    }
};
```

## Root Cause Analysis

### Current Behavior
1. **First compound assignment works**: Correctly loads `__copy_this` and accesses member through it
2. **Second compound assignment fails**: References variable from outer scope (e.g., `%c.value`)
3. **Pattern**: Issue appears in second lambda of a struct, or second compound assignment

### Technical Details

**File**: `src/CodeGen.h`  
**Function**: `generateIdentifierIr()` (around line 5200-5340)

**The Problem:**
When looking up an identifier (like `value`) inside a `[*this]` lambda, the code has two potential paths:

1. **Regular member access** (lines 5232-5267): 
   - Checks if `current_struct_name_` is valid
   - Looks up member in that struct
   - Generates member access with `"this"` as base
   - Sets LValue metadata with base = StringHandle("this")

2. **[*this] lambda member access** (lines 5293-5337):
   - Checks `isInCopyThisLambda()`
   - Loads `__copy_this` into a TempVar
   - Generates member access with TempVar as base
   - Should set LValue metadata with base = TempVar (added in partial fix)

**The Bug:**
- `current_struct_name_` is set to the enclosing struct ("Counter") and persists across lambda generations
- When processing the second lambda (or second compound assignment), the state management fails
- The check at line 5233 (`!isInCopyThisLambda()`) should prevent taking the wrong path
- But in multi-lambda scenarios, `isInCopyThisLambda()` may return incorrect value due to state corruption

**State Variables:**
- `current_struct_name_`: Set to enclosing struct, not cleared between lambdas
- `current_lambda_closure_type_`: Set/cleared within `generateLambdaOperatorFunction`
- `current_lambda_captures_`, `current_lambda_capture_kinds_`: Set/cleared within lambda processing
- Problem: State cleared too early or not maintained correctly between multiple compound assignments

## Current Partial Fix

Added two changes to `src/CodeGen.h`:

1. **Line 5233**: Added check to skip regular member access for lambdas:
   ```cpp
   if (!symbol.has_value() && current_struct_name_.isValid() && 
       !isInCopyThisLambda() && !current_lambda_closure_type_.isValid())
   ```

2. **Lines 5324-5333**: Added LValue metadata for `[*this]` lambda member access:
   ```cpp
   LValueInfo lvalue_info(
       LValueInfo::Kind::Member,
       *copy_this_temp,  // Use TempVar, not StringHandle
       static_cast<int>(member->offset)
   );
   lvalue_info.member_name = member->getName();
   setTempVarMetadata(result_temp, TempVarMetadata::makeLValue(lvalue_info));
   ```

**Status**: Partial fix works for single lambda, still fails for multiple lambdas in same struct.

## Proper Fix Strategy

### Short-term Workaround (If Architectural Changes Too Complex)

Add better state tracking to ensure lambda context persists:

1. **Track lambda nesting level**:
   ```cpp
   size_t lambda_nesting_level_ = 0;
   ```

2. **Don't clear lambda context immediately**:
   - Keep `current_lambda_closure_type_` valid throughout lambda body generation
   - Clear only when exiting the lambda's scope completely

3. **Add explicit lambda struct detection**:
   ```cpp
   bool isLambdaStruct(StringHandle struct_name) {
       // Check if struct_name starts with "__lambda_"
   }
   ```

### Long-term Architectural Fix

**Goal**: Separate lambda context from regular struct context

1. **Create Lambda Context Stack**:
   ```cpp
   struct LambdaContext {
       StringHandle closure_type;
       TypeIndex enclosing_struct_type_index;
       std::unordered_set<std::string> captures;
       std::unordered_map<std::string, LambdaCaptureNode::CaptureKind> capture_kinds;
       bool is_active;
   };
   std::vector<LambdaContext> lambda_context_stack_;
   ```

2. **Push/Pop Lambda Context**:
   - Push when entering lambda body generation
   - Pop when exiting (but keep on stack for nested lambdas)
   - Query stack instead of flat variables

3. **Separate struct_name for lambdas**:
   - `current_struct_name_`: The struct being processed (Counter, __lambda_0, etc.)
   - `current_enclosing_struct_name_`: The enclosing user-defined struct (Counter)
   - Use appropriate one based on context

4. **Refactor identifier resolution**:
   - Check lambda context stack first
   - If in lambda && identifier is enclosing struct member → use `[*this]` path
   - Otherwise → use regular path
   - Clear separation of concerns

## Implementation Steps

### Phase 1: Immediate Fix (Workaround)
1. Add `lambda_nesting_level_` counter
2. Increment on lambda entry, decrement on exit
3. Modify `isInCopyThisLambda()` to check nesting level > 0
4. Keep lambda context valid while nesting level > 0

### Phase 2: Robust Fix (Refactoring)
1. Create `LambdaContext` struct
2. Replace flat lambda state variables with context stack
3. Implement push/pop mechanism in `generateLambdaOperatorFunction`
4. Update all lambda-related code to use context stack
5. Separate `current_struct_name_` into lambda vs. user-defined struct

### Phase 3: Testing
1. Test single lambda with multiple compound assignments
2. Test multiple lambdas in same struct
3. Test nested lambdas (lambda returning lambda)
4. Test lambda with mixed capture modes
5. Ensure no regressions in non-lambda code

## Files to Modify

### Primary
- `src/CodeGen.h`:
  - Add context stack or nesting counter
  - Modify `generateIdentifierIr()` 
  - Modify `generateLambdaOperatorFunction()`
  - Modify `isInCopyThisLambda()`

### Secondary (if refactoring)
- `src/CodeGen.h`: All lambda-related state management
- Review all uses of `current_lambda_*` variables

## Testing Strategy

### Test Cases
1. `test_lambda_copy_this_mutation.cpp` (original failing test)
2. Single lambda, two compound assignments (works with partial fix)
3. Two lambdas in same struct (fails with partial fix)
4. Three lambdas in same struct (fails with partial fix)
5. Nested lambdas with `[*this]`
6. Lambda with `[=]` and `[&]` mixed captures

### Validation
```bash
cd /home/runner/work/FlashCpp/FlashCpp
./x64/Debug/FlashCpp tests/test_lambda_copy_this_mutation.cpp -o /tmp/test.o
clang++ /tmp/test.o -o /tmp/test
./tmp/test
echo $?  # Should be 249 (109 + 40 + 100)
```

## Risk Assessment

### Low Risk (Workaround)
- Adding nesting counter: Low impact, easy to revert
- Extending lambda context lifetime: Moderate risk, test thoroughly

### High Risk (Refactoring)
- Context stack: High impact, touches many code paths
- Requires comprehensive testing
- May uncover other latent bugs
- Recommended: Do in separate PR with extensive review

## Recommendation

1. **Immediate**: Implement Phase 1 workaround with nesting counter
2. **Short-term**: Validate workaround fixes all test cases
3. **Long-term**: Plan Phase 2 refactoring for next major version
4. **Document**: Add regression tests for all scenarios

## Notes

- The partial fix (adding LValue metadata) is correct and should be kept
- The core issue is state management between multiple lambda generations
- Simple tests pass because single lambda context works correctly
- Complex scenarios fail due to state not being isolated properly

---
*Created: 2025-12-21*  
*Author: GitHub Copilot*  
*Status: Analysis Complete, Implementation Pending*
