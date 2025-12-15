# Investigation: String Interning for Variable Names

## Executive Summary
The user proposed replacing `std::string`/`std::string_view` for variable names with a hash/index-based struct to improve lookup performance. Our investigation confirms this would yield **significant performance and memory benefits**. The current IR implementation heavily relies on `std::string` within `std::variant`, inflating the size of every `IrOperand` and causing frequent, expensive string hashing/copying during compilation.

## Current Bottlenecks
1.  **Bloated IR Operands**:
    - `IrOperand` is defined as:
      `std::variant<..., std::string, std::string_view, ...>`
    - `sizeof(std::string)` is typically 32 bytes (x64).
    - `std::variant` size is `max(sizeof(alternatives)) + alignment_padding`.
    - This makes **every** operand in the IR (even integers!) consume ~40 bytes.
2.  **Expensive Map Lookups**:
    - `IRConverter.h` uses `std::unordered_map<SafeStringKey, VariableInfo>`.
    - `SafeStringKey` wraps `std::string`.
    - Every variable access (load/store) triggers:
      - String hashing (linear time w.r.t length).
      - String equality checks (if hash collides or for bucket search).
      - Potential memory allocation if `SafeStringKey` copies a long string.

## Proposed Solution: Global String Interning with `gChunkedStringAllocator`

We will implement a zero-allocation string handling system utilizing the existing `gChunkedStringAllocator`.

### 1. `StringHandle` Structure
A lightweight, 32-bit handle that acts as a packed reference to the string data in the allocator.
```cpp
struct StringHandle {
    uint32_t handle; // Packed ID: Chunk Index (high bits) + Offset (low bits)
    
    // Example Bit Layout:
    // [31...24] (8 bits) : Chunk Index
    // [23...0]  (24 bits): Byte Offset within that chunk
};
```

### 2. Enhanced Chunk Storage Layout
To support identifying strings by `std::string_view` (which requires length) and accessing their hash quickly, we will store metadata directly preceding or strictly within the allocated slot.

**Proposed Memory Layout for each String:**
```
[Hash (8 bytes)] [Length (4 bytes)] [String Content (N bytes)] [\0]
```
*Note: This adds 12 bytes overhead per string, but enables O(1) `string_view` construction and hash retrieval.*

### 3. Integration with `gChunkedStringAllocator`
We will wrap `gChunkedStringAllocator` with a helper system that provides the following APIs:

#### Storage & Resolution
- **`std::string_view getStringView(StringHandle h)`**:
  - Decodes handle to pointer `p`.
  - Reads `length` from `p + 8`.
  - Returns `std::string_view(p + 12, length)`.
- **`uint64_t getHash(StringHandle h)`**:
  - Decodes handle to pointer `p`.
  - Returns `*(uint64_t*)p` (the pre-computed hash).

#### Creation APIs
- **`createStringHandle(std::string_view str)`**:
  - Computes hash.
  - Allocates `8 + 4 + str.size() + 1` bytes in `gChunkedStringAllocator`.
  - Writes Hash, Length, Content, and Null terminator.
  - Returns new `StringHandle`.
- **`getOrInternStringHandle(std::string_view str)`**:
  - Computes hash.
  - Looks up in `std::unordered_map<std::string_view, StringHandle>`.
  - If found, returns existing handle.
  - If not, calls `createStringHandle` and maps the result.

### 4. IR Optimization
Refactor `IrOperand` to replace `std::string` and `std::string_view` with `StringHandle`.
```cpp
// New Size: ~16 bytes (8 for max(double, uint64) + tag), down from ~40 byte.
using IrOperand = std::variant<int, uint64_t, double, StringHandle, TempVar, ...>;
```

### 5. Backend Optimization
Refactor `StackVariableScope` to map `StringHandle` directly to stack info.
```cpp
// Variable lookup becomes an integer map lookup
std::unordered_map<StringHandle, VariableInfo> variables;
```

## Benefits
1.  **Memory Usage**: `IrOperand` size reduced by **~60%**.
2.  **Performance**:
    - Variable lookups become integer operations.
    - Zero copy: strings are written once to the arena.
    - `string_view` reconstruction is O(1) (simple pointer arithmetic).
3.  **Flexibility**: Dual API allows optimizing for speed (create) or memory logic (intern) as needed.

## Phased Implementation Plan

### Phase 1: Infrastructure
*Goal: Create the StringTable system without breaking existing code.*
1.  **Modify `gChunkedStringAllocator`**: Ensure it exposes necessary methods for creating handles from offsets/indices.
2.  **Create `StringTable.h/cpp`**: Implement `StringHandle` struct and the global singleton/static wrapper around `gChunkedStringAllocator`.
    - Implement the `[Hash][Length]` memory layout logic.
    - Implement `createStringHandle` and `getOrInternStringHandle`.
    - Implement `getStringView` and `getHash` resolution functions.
3.  **Unit Test**: Verify that handles correctly round-trip to strings and that interning deduplicates correctly.

### Phase 2: Variable Naming Update (TempVar)
*Goal: Clean up TempVar generation before the big refactor.*
1.  **Rename TempVars**: Update `TempVar` logic in `IRTypes.h` to use simple numeric strings ("1", "2") instead of "temp_1". (Note: This is independent of interning but requested separately).

### Phase 3: Frontend Integration (AST & Parsing)
*Goal: Start interning strings at the source.*
1.  **Update `AstToIr`**: Systematically update `AstToIr` visitor methods.
    - Instead of passing `std::string` names to IR instructions, call `StringTable::intern(name)` and pass the resulting `StringHandle`.
    - Note: This will temporarily require converting `StringHandle` back to `std::string` when creating the old `IrInstruction` format, or overloading `IrInstruction` constructors to accept both.

### Phase 4: Core IR Refactor (The "Flag Day")
*Goal: Switch the internal representation. This is the breaking change.*
1.  **Update `IrTypes.h`**:
    - Remove `std::string` and `std::string_view` from `IrOperand` variant.
    - Add `StringHandle` to `IrOperand` variant.
    - Update `IrInstruction` constructors/methods to accept `StringHandle`.
    - Update `printTypedValue` to resolve handles to strings for debug printing.
2.  **Fix Logic Errors**: The compiler will now fail to build. Systematically fix all build errors:
    - Update `IRTypes.cpp` (operand printing).
    - Update `AstToIr.cpp` (instruction generation) to strictly use handles.

### Phase 5: Backend Optimization
*Goal: Reap the performance benefits.*
1.  **Refactor `IRConverter.h`**:
    - Update `StackVariableScope` to use `std::unordered_map<StringHandle, VariableInfo>`.
    - Update `SafeStringKey` to be removed or replaced by `StringHandle`.
    - Update `handleVariableDecl`, `handleStore`, `handleLoad` to simple integer lookups.
    - Update `SymbolTable` interaction (if `SymbolTable` expects strings, resolve handle -> string_view temporarily).

### Phase 6: Cleanup & Optimization
1.  **Remove Legacy Code**: Delete any old string helper functions.
2.  **Optimize Hash Map**: Consider replacing `std::unordered_map<StringHandle, ...>` with a flat vector or more optimized integer map if profiling shows it's still hot.
