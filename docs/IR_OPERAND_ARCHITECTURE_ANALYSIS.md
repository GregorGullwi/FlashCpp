# IR Operand Architecture Analysis

## Problem Statement

The current IR instruction design uses a hybrid approach:
- Some instructions use **dynamic operands** (`std::vector<IrOperand>`) 
- Others use **typed payloads** (e.g., `BinaryOp`, `UnaryOp`, `DereferenceOp`)

This inconsistency creates issues, particularly with tracking type information like pointer depth through nested operations.

## Current Architecture

### IrInstruction Structure
```cpp
class IrInstruction {
private:
    IrOpcode opcode_;
    OperandStorage operands_;          // Dynamic operands (std::variant-based)
    Token first_token_;
    std::any typed_payload_;           // Optional typed payload
};
```

### IrOperand (Dynamic Operands)
```cpp
using IrOperand = std::variant<int, unsigned long long, double, bool, char, Type, TempVar, StringHandle>;
```

**Limitations:**
- No type information attached to values
- No pointer depth tracking
- Index-based access is error-prone
- Type information must be passed separately in adjacent operands

### TypedValue (Used in Typed Payloads)
```cpp
struct TypedValue {
    Type type = Type::Void;
    int size_in_bits = 0;
    IrValue value;                     // The actual value (TempVar, constant, etc.)
    bool is_reference = false;
    bool is_signed = false;
    TypeIndex type_index = 0;
    int pointer_depth = 0;             // ✓ Solves our pointer tracking problem!
    CVQualifier cv_qualifier = CVQualifier::None;
};
```

**Advantages:**
- Complete type information bundled with value
- Pointer depth tracking built-in
- Self-documenting code (no magic indices)
- Type-safe access

## Proposed Solution: Standardize on TypedValue

### Option 1: Add LhsValue/RhsValue to Binary Instructions (Recommended)

**Approach:** Add `TypedValue lhs` and `TypedValue rhs` fields to all binary operation structures.

**Example:**
```cpp
struct BinaryOp {
    TypedValue lhs;      // Already exists!
    TypedValue rhs;      // Already exists!
    IrValue result;
};

struct DereferenceOp {
    TempVar result;
    Type pointee_type;
    int pointee_size_in_bits;
    std::variant<StringHandle, TempVar> pointer;  // ← Should be TypedValue!
};
```

**Benefits:**
1. **Solves pointer depth tracking** - TypedValue.pointer_depth propagates through operations
2. **Type safety** - Compile-time guarantees instead of runtime variant checks
3. **Self-documenting** - `op.lhs` is clearer than `operands[2]`
4. **Reduced bugs** - No magic indices to mess up
5. **Better debugging** - Full type info visible in debugger
6. **Consistency** - BinaryOp already uses this pattern successfully

**Migration Strategy:**
```cpp
// Phase 1: Convert remaining typed payloads to use TypedValue
struct DereferenceOp {
    TempVar result;
    TypedValue pointer;  // ← Changed from std::variant
};

struct ArrayAccessOp {
    TypedValue array;    // ← Changed
    TypedValue index;    // ← Changed
    TempVar result;
    // ...
};

// Phase 2: Gradually migrate code generation to populate TypedValue fields
// Phase 3: Remove dynamic operands once all typed payloads converted
```

### Option 2: Extend IrOperand with Type Info (Not Recommended)

**Approach:** Replace `IrOperand` with a richer structure.

```cpp
struct IrOperand {
    std::variant<int, double, TempVar, StringHandle> value;
    Type type;
    int size_in_bits;
    int pointer_depth;
};
```

**Drawbacks:**
- Still index-based access (`operands[2]`)
- Wastes memory for literals (they don't need full type info)
- Doesn't solve the fundamental problem of unclear semantics
- More work to migrate

### Option 3: Use void* for IrValue (Strongly Not Recommended)

**Approach:** Store arbitrary data as void pointers.

**Why This Is Bad:**
- ❌ Loses type safety completely
- ❌ Prone to memory leaks
- ❌ Debugging nightmare
- ❌ Goes against modern C++ practices
- ❌ No compile-time verification

## Detailed Comparison

| Aspect | Dynamic Operands | TypedValue Approach |
|--------|------------------|---------------------|
| **Type Safety** | Runtime checks only | Compile-time + runtime |
| **Pointer Depth** | Manual tracking needed | Built-in field |
| **Code Clarity** | `operands[2]` | `op.lhs` |
| **Memory Usage** | Lower (variant only) | Higher (~40 bytes/value) |
| **Migration Effort** | N/A (current) | Medium (systematic) |
| **Maintainability** | Error-prone | Self-documenting |
| **Debugging** | Difficult | Easy (all info visible) |

## Current State of Migration

Already using TypedValue:
- ✅ BinaryOp (Add, Subtract, Multiply, Divide, comparisons)
- ✅ CondBranchOp
- ✅ CallOp (for arguments)
- ✅ MemberLoadOp
- ✅ MemberStoreOp
- ✅ ReturnOp (partial)

Still using dynamic operands or std::variant:
- ❌ DereferenceOp (pointer is std::variant<StringHandle, TempVar>)
- ❌ AddressOfOp (operand is std::variant)
- ❌ ArrayAccessOp (array and index)
- ❌ Many others

## Recommendation

**Adopt Option 1: Complete the migration to TypedValue for all operations.**

### Immediate Action Items

1. **Update remaining typed payloads** to use TypedValue for their operands:
   ```cpp
   struct DereferenceOp {
       TempVar result;
       TypedValue pointer;  // ← Add full type info including pointer_depth
   };
   
   struct AddressOfOp {
       TempVar result;
       TypedValue operand;  // ← Add full type info
   };
   
   struct ArrayAccessOp {
       TypedValue array;
       TypedValue index;
       TempVar result;
       // ...
   };
   ```

2. **Update code generation** to populate pointer_depth in TypedValue:
   ```cpp
   // In generateUnaryOperatorIr for dereference:
   TypedValue ptr_value;
   ptr_value.type = operandType;
   ptr_value.size_in_bits = 64;  // Pointer size
   ptr_value.value = operand;
   ptr_value.pointer_depth = original_ptr_depth;  // From symbol table or previous op
   
   DereferenceOp op;
   op.pointer = ptr_value;
   op.result = result_var;
   // Result has pointer_depth = ptr_value.pointer_depth - 1
   ```

3. **Remove dynamic operands** from IrInstruction once all typed payloads migrated

### Long-term Benefits

- **Solves the multi-level pointer dereference bug** - pointer_depth flows naturally
- **Easier to add new type attributes** - just add fields to TypedValue
- **Better compiler error messages** - type mismatches caught at compile time
- **Simplified code reviews** - semantic meaning is obvious
- **Future-proof** - Easy to extend with new type information

## Alternative: Hybrid Approach (Pragmatic)

If full migration is too much work immediately:

1. **Keep typed payloads** for new/updated operations
2. **Keep dynamic operands** for legacy operations (will be removed eventually)
3. **Establish rule**: All new IR instructions must use typed payloads with TypedValue

This allows incremental migration while preventing the problem from getting worse.

## Memory Considerations

TypedValue is larger than raw IrOperand:
- TypedValue: ~40 bytes (including padding)
- IrOperand: 32 bytes (variant size)

For a typical function with 100 IR instructions, this is ~800 bytes extra - negligible in modern systems. The benefits in correctness and maintainability far outweigh this cost.

## Conclusion

**Recommendation: Standardize on TypedValue for all IR operands.**

This is a one-time migration effort that will:
- Fix the pointer depth tracking bug (and prevent similar issues)
- Improve code quality and maintainability
- Make the IR system more robust and easier to extend
- Align with modern C++ best practices (type safety, self-documenting code)

The migration can be done incrementally, operation by operation, without breaking existing functionality.

---

*Document created: 2025-12-17*  
*Author: Analysis based on investigation of multi-level pointer dereference bug*  
*Status: Proposal for discussion*
