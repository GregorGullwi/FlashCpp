# Struct Array Member Access Fix - Analysis and Implementation Plan

## Problem Statement

Array element member access for struct types causes segmentation faults when used as lvalues (assignment targets):

```cpp
struct P { int x; };
P p[3];
p[0].x = 10;  // SEGFAULT
```

However, both components work independently:
- Struct member access: `p.x = 10` ✓ works
- Primitive array access: `numbers[0] = 10` ✓ works
- Array element member read: `int v = p[0].x` ✓ works (confirmed in for loop)

The issue is specifically with **lvalue usage of struct array element member access**.

## Root Cause Analysis

### Current IR Generation Pattern (BROKEN)

For `p[0].x = 10`, the Parser generates:

```
%2 = array_access [struct][32] %p, [int][32] 0
%3 = member_access int32 %2.x (offset: 0)
assign %3 = 10
```

**Problem**: `%3` is an rvalue (temporary holding a value), not an lvalue (memory location). The `assign` instruction attempts to write to `%3`'s stack location, but also tries to write back to an uninitialized offset (INT_MIN), causing the segfault.

### Why This Happens

1. **ArrayAccess** returns the struct value (not address) in `%2`
2. **MemberAccess** extracts the member value into `%3`
3. **Assignment** to `%3` fails because:
   - It writes to `%3`'s temp variable location (first write - works)
   - It attempts a "write-back" to source location with INT_MIN offset (second write - SEGFAULT)

### Architecture Issue

The IR lacks **lvalue/rvalue distinction**. The same `member_access` opcode is used for:
- Rvalue context: `int v = p[0].x` → load value
- Lvalue context: `p[0].x = 10` → need address for store

## Existing Infrastructure

### IR Opcodes Available
- `ArrayAccess` - load array element value
- `ArrayStore` - store to array element  
- `ArrayElementAddress` - compute element address without loading
- `MemberAccess` - load struct member value
- `MemberStore` - store to struct member
- `Assignment` - generic assignment

### Key Structures (IRTypes.h)
```cpp
struct MemberStoreOp {
    TypedValue value;                          // Value to store
    std::variant<StringHandle, TempVar> object; // Target object
    StringHandle member_name;                   // Member to access
    int offset;                                 // Byte offset
    // ...
};

struct ArrayAccessOp {
    TempVar result;                            // Result temp
    Type element_type;                          // Element type
    std::variant<StringHandle, TempVar> array;  // Array source
    TypedValue index;                           // Index value
    int64_t member_offset;                      // For member arrays
    bool is_pointer_to_array;                   // Pointer vs array
};
```

## Solution Options

### Option 1: Simple IR Fix (RECOMMENDED)
**Modify Parser to detect lvalue member access and emit `MemberStore` directly**

For `p[0].x = 10`:
```
%2 = array_element_address %p, 0  // Get address of p[0]
member_store int32 %2.x, 10        // Store to member at that address
```

**Pros:**
- Matches existing pattern (separate load/store opcodes)
- No architectural changes needed
- Minimal code impact
- Aligns with C++20 value categories

**Cons:**
- Parser needs lvalue detection logic
- Must handle all member access contexts

**Implementation Steps:**
1. Add lvalue context tracking to expression parsing
2. When parsing `=` operator, check if LHS is member access
3. If member access LHS, emit `MemberStore` instead of `MemberAccess + Assignment`
4. Handle nested cases: `p[i].x`, `(*ptr).x`, etc.

**Estimated Complexity:** Medium (1-2 days)
**Lines Changed:** ~200-300 in Parser.cpp
**Risk:** Low - contained change

### Option 2: Comprehensive Lvalue Tracking
**Add lvalue metadata throughout IR system**

```cpp
struct LValueInfo {
    enum class Kind { Direct, Indirect, Member, ArrayElement };
    Kind kind;
    std::variant<StringHandle, TempVar> base;
    int offset;
};

struct TempVarMetadata {
    bool is_lvalue;
    std::optional<LValueInfo> lvalue_info;
};
```

**Pros:**
- Full C++20 value category support
- Enables future optimizations
- Cleaner separation of concerns

**Cons:**
- Large architectural change
- Affects many systems
- Higher risk
- Much more code to modify

**Estimated Complexity:** High (1-2 weeks)
**Lines Changed:** ~1000+ across multiple files
**Risk:** High - touches core IR infrastructure

### Option 3: Runtime Lvalue Resolution (NOT RECOMMENDED)
**Track lvalue sources at runtime in codegen**

**Pros:**
- No IR changes

**Cons:**
- Adds complexity to codegen
- Worse performance
- Harder to debug
- Doesn't solve root cause

## Recommended Approach: Option 1

### Phase 1: Foundation (Current)
- [x] Identify problem
- [x] Create test case
- [x] Document architecture
- [ ] Implement lvalue detection in Parser

### Phase 2: Implementation
1. **Add context flag to expression parsing** (~50 lines)
   ```cpp
   enum class ExprContext { RValue, LValue };
   IrValue parseExpression(ExprContext ctx = ExprContext::RValue);
   ```

2. **Detect member access in assignment LHS** (~100 lines)
   ```cpp
   if (is_assignment_op(op) && is_member_access(lhs_expr)) {
       // Extract object and member
       // Emit MemberStore instead of MemberAccess + Assignment
   }
   ```

3. **Handle array element addressing** (~50 lines)
   ```cpp
   // For p[i].x assignment:
   // 1. Generate array_element_address → gets &p[i]
   // 2. Generate member_store with that address
   ```

4. **Add test coverage** (~100 lines)
   - Simple case: `p[0].x = 10`
   - Variable index: `p[i].x = value`
   - Nested: `arr[i].member.submember = val`
   - Compound: `p[i].x += 5`

### Phase 3: Validation
- [ ] Run existing test suite
- [ ] Verify no regressions
- [ ] Test edge cases
- [ ] Performance check

## C++20 Compliance Impact

This fix addresses a **fundamental C++20 requirement**: proper lvalue/rvalue distinction.

**Current Status:**
- Lvalue categories: ❌ Partially broken (this bug)
- Rvalue categories: ✓ Working
- Xvalue categories: ⚠️ Not tested

**After Fix:**
- Lvalue categories: ✓ Working
- Rvalue categories: ✓ Working  
- Xvalue categories: ⚠️ Still needs separate work

**Future Work for Full C++20 Value Categories:**
- Move semantics (xvalues)
- Reference collapsing
- Perfect forwarding
- Decltype(auto) support

## Implementation Checklist

### Parser.cpp Changes
- [ ] Add `ExprContext` enum
- [ ] Thread context through `parse_expression`
- [ ] Add `is_lvalue_member_access()` helper
- [ ] Modify assignment operator handling
- [ ] Handle compound assignments (`+=`, etc.)
- [ ] Support pointer-to-member access (`->`)

### Testing
- [ ] test_struct_array_member_access.cpp - simple case
- [ ] test_struct_array_member_variable_index.cpp - dynamic index
- [ ] test_struct_array_member_nested.cpp - nested members
- [ ] test_struct_array_member_compound_assign.cpp - +=, -=, etc.

### Documentation
- [ ] Update this document with progress
- [ ] Add code comments explaining lvalue detection
- [ ] Update IR specification if needed

## Progress Tracking

### Completed
- 2025-12-18 09:00: Initial analysis and architecture document created
- 2025-12-18 09:00: Test case added (`test_struct_array_member_access.cpp`)
- 2025-12-18 09:15: Comprehensive architecture analysis document created
- 2025-12-18 09:15: Confirmed IR infrastructure exists (MemberStore, ArrayElementAddress opcodes available)
- 2025-12-18 09:15: Identified parse_expression as entry point for fix (line 10001 in Parser.cpp)
- 2025-12-18 09:15: Verified existing tests work: struct member access ✓, primitive arrays ✓

### In Progress
- Implementing Option 1 (Simple IR Fix) - NEXT STEP
  - Need to add lvalue context detection in parse_expression
  - Need to modify binary operator handling for assignments to member access

### Blocked
- None

## Next Steps (Detailed Implementation Plan)

### Step 1: Locate Assignment Handling in parse_expression
**File:** src/Parser.cpp, line ~10046-10060
- Binary operators are parsed in the while loop
- Operator token consumed at line 10046-10047
- RHS parsed at line 10050
- BinaryOperatorNode created at line 10056-10059

**Action needed:** Add special handling BEFORE creating BinaryOperatorNode for assignment operators

### Step 2: Detect Member Access in LHS
When operator is `=` (or compound assignment), check if LHS is:
- `MemberAccessNode` → extract object and member
- Object could be simple variable OR array access result

### Step 3: Emit MemberStore Instead
For pattern: `object.member = value` or `array[i].member = value`
```cpp
if (is_assignment && is_member_access(lhs)) {
    // Don't create BinaryOperatorNode
    // Instead, emit MemberStore IR directly
    emit_member_store(object, member_name, offset, rhs_value);
    return;  // Skip normal binary operator handling
}
```

### Step 4: Handle Array Element Case
For `p[0].x = 10`:
- LHS is `MemberAccessNode`
- Object is result of `ArraySubscriptNode`
- Need to emit: `array_element_address` + `member_store`

### Key Code Locations Identified
- `parse_expression`: Line 10001 (entry point)
- Binary operator loop: Line 10016-10063
- Operator consumption: Line 10046-10047
- BinaryOperatorNode creation: Line 10056-10059
- `get_operator_precedence`: Referenced at line 10037

### Required Helper Functions (To Be Created)
```cpp
bool is_assignment_operator(std::string_view op);
bool is_lhs_member_access(const ASTNode& lhs);
void emit_member_store_ir(const MemberAccessNode& member_access, const ASTNode& rhs);
```

### Testing Strategy
1. Verify existing tests still pass
2. Test new case: `p[0].x = 10`
3. Test variable index: `p[i].x = value`
4. Test nested: `p[0].obj.member = value`
5. Test compound: `p[0].x += 5`
- Issue #188: Member access through array subscript crash
- Future: Move semantics and xvalue support
- Future: Complete value category implementation

## References
- C++20 Standard: [expr.prop] (value categories)
- IRTypes.h: Lines 102-111 (opcodes)
- IRTypes.h: Lines 641-687 (op structures)
- Parser.cpp: ~976KB (IR generation)
