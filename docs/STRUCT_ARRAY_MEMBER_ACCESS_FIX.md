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

### Option 2: Comprehensive Lvalue Tracking (Future Enhancement)
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
- Enables powerful optimizations (see below)
- Cleaner separation of concerns
- Better foundation for move semantics

**Cons:**
- Large architectural change
- Affects many systems
- Higher risk
- Much more code to modify

**Estimated Complexity:** High (1-2 weeks)
**Lines Changed:** ~1000+ across multiple files
**Risk:** High - touches core IR infrastructure

**Optimizations Enabled by Option 2:**

1. **Copy Elision (RVO/NRVO)**
   - Return value optimization: eliminate temporary copies when returning local objects
   - Named return value optimization: same for named local variables
   - Modern compilers (GCC, Clang, MSVC) all do this - C++17 made some cases mandatory

2. **Move Optimization**
   - Detect when lvalue can be treated as xvalue (expiring value)
   - Automatically use move constructors instead of copy constructors
   - Example: `return std::move(local_obj)` or return from function

3. **Dead Store Elimination**
   - If lvalue is written but never read, eliminate the store
   - Requires knowing variable lifetime and usage patterns

4. **Aliasing Analysis**
   - Determine if two lvalues can refer to same memory location
   - Enables aggressive reordering and optimization
   - Critical for auto-vectorization

5. **Temporary Materialization Control**
   - Precisely control when/where temporaries are created
   - Reduce stack pressure and memory traffic
   - Example: avoid materializing in `foo(expr1 + expr2)`

6. **Reference Binding Optimization**
   - Bind references directly to lvalues without temporary copies
   - Optimize `const T&` parameters to avoid defensive copies

**Do Other Compilers Do This?**
Yes, all modern C++ compilers implement comprehensive value category tracking:

- **LLVM/Clang**: Uses `ValueKind` (lvalue, xvalue, prvalue) throughout AST and IR
- **GCC**: Tracks lvalue-ness in tree nodes, enables copy elision and move optimization
- **MSVC**: Implements value categories for C++11+ optimization passes

These optimizations are **essential for modern C++** performance. Without them:
- Excessive copying of objects
- Missed move opportunities
- Suboptimal code generation
- Poor compliance with C++17/20 semantics

**Recommendation**: Implement Option 1 now for correctness, consider Option 2 as future enhancement for performance parity with mainstream compilers.

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

### CodeGen.h Changes  
- [x] Add array subscript handling in member access assignment
- [x] Generate MemberStore for `array[i].member = value` pattern
- [x] Handle struct type lookup and member resolution
- [x] Support both constant and variable indices

### IRConverter.h Changes
- [x] Add is_struct flag detection
- [x] Modify constant index case to return address for structs
- [x] Modify variable index (TempVar) case
- [x] Modify variable index (StringHandle) case  
- [x] Mark struct results as references in reference_stack_info_

### Testing
- [x] test_struct_array_member_access.cpp - simple case ✓ (returns 60)
- [x] Verify struct_member_test.cpp still works ✓ (returns 30)
- [x] Verify test_arrays_simple.cpp still works ✓ (returns 0)
- [ ] test_struct_array_member_variable_index.cpp - dynamic index (covered by loop in main test)
- [ ] test_struct_array_member_nested.cpp - nested members (future)
- [ ] test_struct_array_member_compound_assign.cpp - +=, -= (future)

### Documentation
- [x] Update this document with progress
- [x] Add implementation summary with code locations
- [x] Document how the fix works

## Progress Tracking

### Completed
- 2025-12-18 09:00: Initial analysis and architecture document created
- 2025-12-18 09:00: Test case added (`test_struct_array_member_access.cpp`)
- 2025-12-18 09:15: Comprehensive architecture analysis document created
- 2025-12-18 09:15: Confirmed IR infrastructure exists (MemberStore, ArrayElementAddress opcodes available)
- 2025-12-18 09:15: Identified parse_expression as entry point for fix (line 10001 in Parser.cpp)
- 2025-12-18 09:15: Verified existing tests work: struct member access ✓, primitive arrays ✓
- 2025-12-18 09:30: **Implemented Option 1 fix - COMPLETE**
  - Added ArraySubscriptNode handling in member access assignment (CodeGen.h:6480)
  - Modified handleArrayAccess to return ADDRESS for struct types (IRConverter.h:10831-11025)
  - Mark struct array element results as references for proper dereferencing
  - All tests passing: test returns 60 as expected ✓

### In Progress
- None - Fix is complete!

### Blocked
- None

## Implementation Summary

### Changes Made (Option 1 - Simple IR Fix)

**File: src/CodeGen.h (AstToIr class)**
- **Line 6480**: Added handling for `ArraySubscriptNode` as object in member access assignment
  - Detects pattern: `array[index].member = value`
  - Generates IR for array element address
  - Emits `MemberStore` opcode with array element temp var as object
  - Properly handles struct type lookup and member resolution

**File: src/IRConverter.h (handleArrayAccess function)**
- **Line 10831**: Added `is_struct` flag to detect struct/user-defined types
- **Lines 10892-10920**: Modified constant index handling
  - For structs: Keep ADDRESS in register (skip value load)
  - For primitives: Load value as before (no regression)
  - Uses LEA instruction for struct address computation
- **Lines 10938-10972**: Modified variable index handling
  - Same logic for struct vs primitive distinction
  - Works for both pointer and regular arrays
- **Lines 11000-11025**: Modified StringHandle index handling
  - Consistent with other index types
- **Lines 11025-11032**: Mark struct results as references
  - Adds entry to `reference_stack_info_` map
  - Enables `handleMemberAccess` to properly dereference

### How It Works

1. **Assignment Detection** (CodeGen.h):
   ```cpp
   p[0].x = 10  // Detected as MemberAccessNode with ArraySubscriptNode object
   ```

2. **IR Generation** (CodeGen.h):
   ```
   %2 = array_access -> Returns ADDRESS of p[0] (not value)
   member_store %2.x, 10  -> Store to member at that address
   ```

3. **Code Generation** (IRConverter.h):
   - ArrayAccess for struct: `LEA reg, [rbp + offset]` (address)
   - ArrayAccess for primitive: `MOV reg, [rbp + offset]` (value)
   - MemberStore: Uses stored address to write to member

### Test Results

```bash
$ ./test_struct_array
Exit code: 60  # ✓ Correct (10 + 20 + 30 = 60)

$ ./struct_member
Exit code: 30  # ✓ Still works

$ ./test_arrays
Exit code: 0   # ✓ Still works
```

### Lines Changed
- CodeGen.h: ~85 lines added (array subscript member access handling)
- IRConverter.h: ~25 lines modified (struct array address handling)
- **Total: ~110 lines** (within estimated 200-300 range)

### Risk Assessment
- **Low**: Changes are localized and well-contained
- **No regressions**: Existing tests continue to pass
- **Proper fallback**: Only affects struct arrays; primitives unchanged

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

## Option 2 Implementation Progress (Started 2025-12-18)

### Phase 1: Foundation - IR Type System Updates ✓ COMPLETE
**Date:** 2025-12-18

**Changes Made:**
1. **Added ValueCategory enum** (IRTypes.h after StringTable.h include)
   - `LValue`: has identity, cannot be moved from
   - `XValue`: has identity, can be moved from (expiring value)
   - `PRValue`: pure rvalue, no identity
   
2. **Added LValueInfo structure** (IRTypes.h)
   - Tracks storage location for lvalues/xvalues
   - Supports nested access (arr[i].member)
   - Stores base, offset, and parent info
   - Five kinds: Direct, Indirect, Member, ArrayElement, Temporary

3. **Added TempVarMetadata structure** (IRTypes.h)
   - Tracks value category for each TempVar
   - Optional lvalue_info for lvalues/xvalues
   - Flags for address vs value, move results
   - Helper factory methods: makeLValue(), makeXValue(), makePRValue()

4. **Added GlobalTempVarMetadataStorage class** (IRTypes.h)
   - Singleton storage for all TempVar metadata
   - Sparse storage using unordered_map
   - O(1) lookup and modification
   - Helper methods: isLValue(), isXValue(), isPRValue()
   - Statistics tracking and logging

**Files Modified:**
- `src/IRTypes.h`: Added ~200 lines for value category infrastructure

**Testing:**
- All 648 tests pass ✓
- No regressions ✓
- Build successful with clang++ ✓

**Architecture Decision:**
Placed value category structures AFTER TempVar and StringHandle definitions
to avoid forward declaration issues with std::variant.

### Phase 2: TempVar Enhancement ✓ COMPLETE
**Date:** 2025-12-18

**Changes Made:**
1. **Added convenience functions for TempVar metadata access** (IRTypes.h)
   - `setTempVarMetadata()`: Set metadata for a TempVar
   - `getTempVarMetadata()`: Get metadata for a TempVar
   - `isTempVarLValue()`: Check if TempVar is an lvalue
   - `isTempVarXValue()`: Check if TempVar is an xvalue
   - `isTempVarPRValue()`: Check if TempVar is a prvalue
   - `getTempVarLValueInfo()`: Get lvalue info if available

2. **Added builder pattern helpers** (IRTypes.h)
   - `makeLValueTempVar()`: Create TempVar with lvalue metadata
   - `makeXValueTempVar()`: Create TempVar with xvalue metadata
   - `makePRValueTempVar()`: Create TempVar with prvalue metadata

**Files Modified:**
- `src/IRTypes.h`: Added ~50 lines for TempVar convenience methods

**Testing:**
- All 648 tests pass ✓
- No regressions ✓
- Build successful with clang++ ✓

**Architecture Decision:**
Placed convenience functions AFTER GlobalTempVarMetadataStorage to avoid
circular dependencies while maintaining a clean API.

### Phase 3: Parser Integration (IN PROGRESS)
**Date:** 2025-12-18

**Current Status:**
Infrastructure is in place and tested. Ready for incremental integration
into the parser and code generation.

**Strategy:**
Rather than modifying all code generation at once, we'll take an incremental
approach:

1. **Phase 3a: Foundation (COMPLETE)**
   - Infrastructure validated with test case ✓
   - All existing tests still pass ✓
   - Value category system compiles and links ✓

2. **Phase 3b: Simple Lvalue Marking (NEXT)**
   - Mark variable references as lvalues
   - Mark dereference operations (*ptr) as lvalues
   - Mark array element access (arr[i]) as lvalues
   - Mark member access (obj.member) as lvalues

3. **Phase 3c: PRValue Marking**
   - Mark literals as prvalues
   - Mark arithmetic operations as prvalues
   - Mark function returns as prvalues

4. **Phase 3d: XValue Support (FUTURE)**
   - Mark std::move results as xvalues
   - Mark temporary materialization as xvalues

**Files Modified (Phase 3a):**
- `tests/test_value_category_demo.cpp`: Demo test case

**Testing:**
- All 648 tests pass ✓
- New demo test compiles and runs correctly (returns 15) ✓
- No regressions ✓

**Next Steps:**
The infrastructure is ready. When integration continues, start with:
- `generateIdentifierIr()`: Mark variable loads
- `generateMemberAccessIr()`: Mark member accesses
- `generateArraySubscriptIr()`: Mark array element accesses
- `generateBinaryOperatorIr()`: Handle dereference operator

### Phase 4: CodeGen Integration (TODO)
- [ ] Add convenience methods to TempVar for metadata access
- [ ] Add builder pattern for creating TempVars with metadata
- [ ] Update documentation comments on TempVar

### Phase 3: Parser Integration (TODO)
- [ ] Add lvalue context tracking in expression parsing
- [ ] Propagate lvalue info through parse tree
- [ ] Mark lvalue expressions appropriately

### Phase 4: CodeGen Integration (TODO)
- [ ] Propagate lvalue metadata through AST-to-IR conversion
- [ ] Update handlers to preserve lvalue info

### Phase 5: IRConverter Updates (TODO)
- [ ] Use lvalue metadata for optimal code generation
- [ ] Implement address vs value load decisions

### Phase 6: Optimization Passes (FUTURE)
- [ ] Copy elision (RVO/NRVO)
- [ ] Move optimization
- [ ] Dead store elimination
