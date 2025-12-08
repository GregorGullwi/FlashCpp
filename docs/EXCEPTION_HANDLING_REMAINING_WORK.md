# Linux Exception Handling - Remaining Implementation Work

This document provides a detailed breakdown of the **remaining work** needed for full Linux exception handling support in FlashCpp. 

For an overview of what's already completed, see `LINUX_ELF_SUPPORT_PLAN.md` - Milestone 6.

## What's Already Done ✅

Exception throwing infrastructure, RTTI structures, type_info generation, landing pad structure, and testing stubs are all complete. See the main plan document for details.

---

## Testing Stubs for Incremental Development

Located in `tests/linux_exception_stubs.cpp`, these provide minimal stub implementations of Itanium C++ ABI exception handling functions. They allow FlashCpp-compiled code to link and run for basic testing, even though full exception handling (with .eh_frame and .gcc_except_table) is not yet implemented.

### Purpose

- **Testing**: Verify that exception throwing code is correctly generated
- **Debugging**: See diagnostic messages showing exception flow
- **Development**: Work on exception-related features without full unwinding support

### What Works with Stubs

- ✅ Exception allocation (`__cxa_allocate_exception`)
- ✅ Exception throwing starts (`__cxa_throw`)
- ✅ Type_info is passed correctly
- ✅ Diagnostic messages explain what's missing

### What Doesn't Work

- ❌ Stack unwinding (requires .eh_frame section)
- ❌ Finding catch handlers (requires .gcc_except_table section)
- ❌ Landing pad execution (no personality routine integration)
- ❌ Actual exception catching

### Usage Example

Compile and link with your FlashCpp-generated object file:

```bash
# Compile your test
./x64/Debug/FlashCpp tests/test_exceptions_basic.cpp

# Compile the stubs
g++ -c tests/linux_exception_stubs.cpp -o linux_exception_stubs.o

# Link together
gcc -o test_program test_exceptions_basic.obj linux_exception_stubs.o

# Run (will abort with diagnostic message)
./test_program
```

**Output Example:**
```
STUB: __cxa_allocate_exception(8) -> 0x55d5f9f2e2a0
STUB: __cxa_throw(0x55d5f9f2e2a0, 0x55d5f1a20, (nil))
STUB: Exception thrown but no exception tables present!
STUB: Cannot find catch handlers without .eh_frame and .gcc_except_table
Aborted
```

**For production**: Link with the full C++ runtime library instead (`-lstdc++`), though this will also fail at runtime without exception tables.

---

## Remaining Work (Priority Order)

This section details the ~14-23 days of work needed to complete exception handling.

## Phase 1: .eh_frame Generation (DWARF CFI)
**Estimated Effort**: 3-5 days  
**Priority**: CRITICAL - Nothing else works without this  
**Status**: ✅ In Progress  
**Files to Create/Modify**: 
- New: `src/DwarfCFI.h` - ✅ COMPLETED - DWARF CFI encoding utilities
- Modify: `src/ElfFileWriter.h` - Add `.eh_frame` section generation
- Modify: `src/IRConverter.h` - Track CFI state during code generation

### 1.1 DWARF Encoding Utilities (~4 hours) ✅ COMPLETED
**What**: Create helper functions for DWARF variable-length encoding

**File**: `src/DwarfCFI.h` ✅ CREATED

**Implementation**: All encoding functions have been implemented and tested:
- ✅ `encodeULEB128()` - Unsigned LEB128 encoding
- ✅ `encodeSLEB128()` - Signed LEB128 encoding  
- ✅ `encodePointer()` - Pointer encoding with all DW_EH_PE formats
- ✅ `appendULEB128()` / `appendSLEB128()` - Helper functions
- ✅ All DW_EH_PE_* encoding constants defined
- ✅ All DW_CFA_* instruction opcodes defined
- ✅ DWARF register numbers for x86-64 defined
- ✅ Unit tests created in `tests/test_dwarf_cfi.cpp` (20/20 tests pass)

**File**: `src/DwarfCFI.h`

**Functions to implement**:
```cpp
// Encode unsigned LEB128 (Little Endian Base 128)
std::vector<uint8_t> encodeULEB128(uint64_t value);

// Encode signed LEB128
std::vector<uint8_t> encodeSLEB128(int64_t value);

// Encode pointer values based on encoding type
std::vector<uint8_t> encodePointer(uint64_t value, uint8_t encoding);

// DW_EH_PE_* encoding constants
enum DW_EH_PE {
    DW_EH_PE_absptr   = 0x00,  // Absolute pointer
    DW_EH_PE_uleb128  = 0x01,  // Unsigned LEB128
    DW_EH_PE_udata2   = 0x02,  // Unsigned 2-byte
    DW_EH_PE_udata4   = 0x03,  // Unsigned 4-byte
    DW_EH_PE_udata8   = 0x04,  // Unsigned 8-byte
    DW_EH_PE_sleb128  = 0x09,  // Signed LEB128
    DW_EH_PE_sdata2   = 0x0a,  // Signed 2-byte
    DW_EH_PE_sdata4   = 0x0b,  // Signed 4-byte
    DW_EH_PE_sdata8   = 0x0c,  // Signed 8-byte
    DW_EH_PE_pcrel    = 0x10,  // PC-relative
    DW_EH_PE_datarel  = 0x30,  // Data-relative
};
```

**References**:
- DWARF 4 Standard Section 7.6 (Variable Length Data)
- LSB (Linux Standard Base) Exception Handling Supplement

**Test**: Create unit tests for encoding functions with known values

---

### 1.2 CIE (Common Information Entry) Generation (~6 hours) ✅ COMPLETED
**What**: Generate the Common Information Entry - shared info for all functions

**File**: `src/ElfFileWriter.h` ✅ IMPLEMENTED

**Implementation Complete**:
- ✅ CIE structure with correct header fields
- ✅ Version 1 format
- ✅ Augmentation string "zR" (z=has augmentation data, R=FDE has encoding byte)
- ✅ Code alignment factor = 1 (x86-64)
- ✅ Data alignment factor = -8 (x86-64 stack)
- ✅ Return address register = 16 (RIP)
- ✅ FDE encoding = PC-relative signed 4-byte
- ✅ Initial instructions: DW_CFA_def_cfa (RSP+8), DW_CFA_offset (RIP at CFA-8)
- ✅ Proper 8-byte alignment padding

**Verified**: `readelf -wf` shows correct CIE structure

**Add method**:
```cpp
void generate_eh_frame_cie(std::vector<uint8_t>& eh_frame_data);
```

**CIE Structure** (what to emit):
```
Offset | Size | Field              | Value
-------|------|--------------------|-----------------------
0      | 4    | Length             | CIE size (excluding this field)
4      | 4    | CIE ID             | 0 (marks this as CIE)
8      | 1    | Version            | 1
9      | N    | Augmentation       | "zR\0" (null-terminated)
10+N   | ULEB | Code align factor  | 1
11+N   | SLEB | Data align factor  | -8 (for x86-64)
12+N   | ULEB | Return addr reg    | 16 (RIP on x86-64)
13+N   | ULEB | Augmentation len   | 1
14+N   | 1    | FDE encoding       | DW_EH_PE_pcrel | DW_EH_PE_sdata4
15+N   | Var  | Initial instructions| DW_CFA_def_cfa (RSP, 8)
                                     DW_CFA_offset (RIP, -8)
```

**Key Details**:
- Augmentation "zR": 
  - 'z' = has augmentation data
  - 'R' = FDE has encoding byte
- Code align = 1 (each instruction is 1 byte on x86-64)
- Data align = -8 (stack grows down, 8-byte alignment)
- Initial CFA (Canonical Frame Address) = RSP + 8

**DW_CFA_* instructions** (Call Frame Address instructions):
```cpp
enum DW_CFA {
    DW_CFA_nop = 0x00,
    DW_CFA_set_loc = 0x01,
    DW_CFA_advance_loc = 0x40,  // Low 6 bits = delta
    DW_CFA_offset = 0x80,       // Low 6 bits = register
    DW_CFA_def_cfa = 0x0c,
    DW_CFA_def_cfa_offset = 0x0e,
    DW_CFA_def_cfa_register = 0x0d,
};
```

**Test**: 
1. Generate CIE
2. Use `readelf -wf test.obj` to verify structure
3. Compare with GCC-generated CIE

---

### 1.3 FDE (Frame Description Entry) Generation (~8 hours) ✅ COMPLETED
**What**: Generate one FDE per function with exception handling

**File**: `src/ElfFileWriter.h` ✅ IMPLEMENTED

**Implementation Complete**:
- ✅ FDEInfo structure to track function info
- ✅ FDE header with CIE pointer
- ✅ PC begin/range fields (placeholders for relocations)
- ✅ Augmentation data length field
- ✅ 8-byte alignment padding
- ✅ Integration with add_function_exception_info()
- ✅ FDE generated for all functions

**Note**: CFI instructions and relocations still need to be added in next steps

**Add method**:
```cpp
struct FDEInfo {
    uint32_t function_start_offset;
    uint32_t function_length;
    std::string function_symbol;
    std::vector<CFIInstruction> cfi_instructions;
};

void generate_eh_frame_fde(
    std::vector<uint8_t>& eh_frame_data,
    uint32_t cie_offset,
    const FDEInfo& fde_info
);
```

**FDE Structure**:
```
Offset | Size | Field              | Value
-------|------|--------------------|-----------------------
0      | 4    | Length             | FDE size
4      | 4    | CIE pointer        | Offset to CIE (relative)
8      | 4    | PC begin           | Function start (PC-relative)
12     | 4    | PC range           | Function length
16     | ULEB | Augmentation len   | 0 (no augmentation data)
17     | Var  | CFI instructions   | Frame state changes
```

**CFI Instructions to track**:
```cpp
struct CFIInstruction {
    enum Type {
        PUSH_RBP,       // push rbp
        MOV_RSP_RBP,    // mov rsp, rbp
        SUB_RSP,        // sub rsp, imm
        POP_RBP,        // pop rbp
    };
    Type type;
    uint32_t offset;  // Offset in function where this occurs
    uint32_t value;   // Immediate value (for SUB_RSP)
};
```

**Corresponding DWARF CFI**:
- `push rbp` → `DW_CFA_offset(RBP, -16)` + `DW_CFA_def_cfa_offset(16)`
- `mov rsp, rbp` → `DW_CFA_def_cfa_register(RBP)`
- `sub rsp, N` → `DW_CFA_def_cfa_offset(N+16)`
- `pop rbp` → `DW_CFA_restore(RBP)` + `DW_CFA_def_cfa_offset(8)`

**Integration Point**:
Modify `IRConverter.h` to track CFI state:
```cpp
void emitPushRBP() {
    // Existing code
    textSectionData.push_back(0x55);
    
    // NEW: Track CFI
    current_function_cfi_.push_back({
        CFIInstruction::PUSH_RBP,
        static_cast<uint32_t>(textSectionData.size() - current_function_offset_),
        0
    });
}
```

**Test**:
1. Compile simple function with try/catch
2. Verify FDE with `readelf -wf`
3. Check unwinding works: `gdb test` → `catch throw` → `bt`

---

### 1.4 .eh_frame Section Integration (~4 hours) ✅ COMPLETED
**What**: Create `.eh_frame` section and add to ELF file

**File**: `src/ElfFileWriter.h` ✅ IMPLEMENTED

**Implementation Complete**:
- ✅ .eh_frame section created with SHT_PROGBITS type
- ✅ Proper section flags (SHF_ALLOC) and alignment (8 bytes)
- ✅ .rela.eh_frame relocation section created
- ✅ PC-relative relocations (R_X86_64_PC32) for FDE PC begin fields
- ✅ Automatic symbol lookup to link FDEs to functions
- ✅ Integration with finalizeSections() pipeline
- ✅ Called for all functions via add_function_exception_info()

**Verified**:
- ✅ `readelf -S` shows .eh_frame and .rela.eh_frame sections
- ✅ `readelf -wf` shows correct CIE and FDE structure
- ✅ `readelf -r` shows relocations pointing to function symbols
- ✅ Object files link successfully with g++
- ✅ Linked programs execute correctly

**Add to finalize**:
```cpp
void finalize_eh_frame() {
    // 1. Create .eh_frame section
    auto* eh_frame_section = elf_writer_.sections.add(".eh_frame");
    eh_frame_section->set_type(ELFIO::SHT_PROGBITS);
    eh_frame_section->set_flags(ELFIO::SHF_ALLOC);
    eh_frame_section->set_addr_align(8);
    
    // 2. Generate CIE
    std::vector<uint8_t> eh_frame_data;
    generate_eh_frame_cie(eh_frame_data);
    uint32_t cie_offset = 0;
    
    // 3. Generate FDEs for each function
    for (const auto& func : functions_with_exceptions_) {
        generate_eh_frame_fde(eh_frame_data, cie_offset, func);
    }
    
    // 4. Add data to section
    eh_frame_section->set_data((const char*)eh_frame_data.data(), 
                                eh_frame_data.size());
}
```

**Call from**: `write_to_file()` before saving

**Test**:
1. Verify `.eh_frame` section exists: `readelf -S test.obj`
2. Verify structure: `readelf -wf test.obj`
3. Test unwinding: `objdump --dwarf=frames test.obj`

---

## Phase 2: .gcc_except_table Generation (LSDA) ✅ IN PROGRESS
**Estimated Effort**: 4-6 days  
**Priority**: CRITICAL - Needed for catch handler matching  
**Files**: 
- New: `src/LSDAGenerator.h` - ✅ COMPLETED - LSDA encoding  
- Modify: `src/ElfFileWriter.h` - ✅ COMPLETED - Add `.gcc_except_table` section  
- Modify: `src/IRConverter.h` - Track try/catch regions (already implemented)

### 2.1 LSDA Header Generation (~3 hours) ✅ COMPLETED
**What**: Generate Language Specific Data Area header

**File**: `src/LSDAGenerator.h` ✅ CREATED

**Implementation Complete**:
- ✅ LPStart encoding (omitted, using function-relative offsets)
- ✅ TType encoding (absolute pointers)
- ✅ TType base offset calculation
- ✅ Call site table encoding specification
- ✅ Call site table size encoding

**Verified**: .gcc_except_table section generated in object files

**File**: `src/LSDAGenerator.h`

**LSDA Structure**:
```
Offset | Field                    | Description
-------|--------------------------|---------------------------
0      | LPStart encoding         | 0xFF (omitted)
1      | TType encoding           | DW_EH_PE_absptr
2      | TType base offset (ULEB) | Offset to type table
N      | Call site encoding       | DW_EH_PE_uleb128
N+1    | Call site table size     | Size in bytes
...    | Call site table          | List of try regions
...    | Action table             | What to do on exception
...    | Type table               | Exception types
```

**Add method**:
```cpp
class LSDAGenerator {
public:
    std::vector<uint8_t> generate(const FunctionExceptionInfo& info);
    
private:
    void encode_header(std::vector<uint8_t>& data);
    void encode_call_site_table(std::vector<uint8_t>& data);
    void encode_action_table(std::vector<uint8_t>& data);
    void encode_type_table(std::vector<uint8_t>& data);
};
```

**Test**: Generate header for simple function, verify with hexdump

---

### 2.2 Call Site Table Generation (~6 hours) ✅ COMPLETED
**What**: Map code regions to exception handlers

**Implementation Complete**:
- ✅ Call site entry structure with ULEB128 encoding
- ✅ Try block start offset tracking
- ✅ Try block length calculation
- ✅ Landing pad offset linkage
- ✅ Action table index generation
- ✅ Integration with TryBlockInfo from IRConverter

**Call Site Entry Structure**:
```
Field          | Encoding | Description
---------------|----------|---------------------------
Start          | ULEB128  | Try block start offset
Length         | ULEB128  | Try block length
Landing Pad    | ULEB128  | Landing pad offset (or 0)
Action         | ULEB128  | Index into action table (or 0)
```

**Data to track** (in `IRConverter.h`):
```cpp
struct TryRegion {
    uint32_t start_offset;      // Where try block begins
    uint32_t end_offset;        // Where try block ends
    uint32_t landing_pad_offset; // Where catch handler begins
    uint32_t action_index;      // Index in action table
};

std::vector<TryRegion> current_function_try_regions_;
```

**Integration**:
Modify `handleTryBegin/handleTryEnd`:
```cpp
void handleTryBegin(...) {
    TryRegion region;
    region.start_offset = textSectionData.size() - current_function_offset_;
    current_function_try_regions_.push_back(region);
}

void handleTryEnd(...) {
    auto& region = current_function_try_regions_.back();
    region.end_offset = textSectionData.size() - current_function_offset_;
}

void handleCatchBegin(...) {
    auto& region = current_function_try_regions_.back();
    region.landing_pad_offset = textSectionData.size() - current_function_offset_;
    // Set action_index based on catch handler type
}
```

**Test**: Verify call site table covers all try blocks

---

### 2.3 Action Table Generation (~4 hours)
**What**: Describe what happens when exception is caught

**Action Entry Structure**:
```
Field          | Encoding | Description
---------------|----------|---------------------------
Type filter    | SLEB128  | Index in type table (negative)
Next action    | SLEB128  | Offset to next action (or 0)
```

**Example**:
```cpp
try {
    // code
} catch (int) {      // Action 1: type filter = -1 (first type)
    // handler
} catch (float) {    // Action 2: type filter = -2 (second type)
    // handler
} catch (...) {      // Action 3: type filter = 0 (catch-all)
    // handler
}
```

Action table:
```
Action 1: [-1, -1]  // catch int, next action at offset -1
Action 2: [-2, -1]  // catch float, next action at offset -1  
Action 3: [0, 0]    // catch all, no next action
```

**Data structure**:
```cpp
struct ActionEntry {
    int32_t type_filter;  // Negative index into type table
    int32_t next_offset;  // To next action or 0
};
```

**Test**: Match actions to catch handlers

---

### 2.4 Type Table Generation (~4 hours)
**What**: List of type_info pointers for catch matching

**Type Table Structure**:
```
Offset | Content
-------|------------------
0      | nullptr (for catch-all)
8      | &_ZTIi  (for catch(int))
16     | &_ZTIf  (for catch(float))
...    | More type_info pointers
```

**Implementation**:
```cpp
void encode_type_table(std::vector<uint8_t>& data,
                      const std::vector<CatchHandler>& handlers) {
    for (const auto& handler : handlers) {
        if (handler.is_catch_all) {
            // Add 8 bytes of zeros (nullptr)
            data.insert(data.end(), 8, 0);
        } else {
            // Add relocation to type_info symbol
            add_lsda_relocation(data.size(), handler.typeinfo_symbol);
            data.insert(data.end(), 8, 0);  // Placeholder
        }
    }
}
```

**Test**: Verify type table has correct type_info pointers

---

### 2.5 .gcc_except_table Section Integration (~3 hours)
**What**: Create section and link to .eh_frame

**Add to `ElfFileWriter.h`**:
```cpp
void finalize_gcc_except_table() {
    auto* except_section = elf_writer_.sections.add(".gcc_except_table");
    except_section->set_type(ELFIO::SHT_PROGBITS);
    except_section->set_flags(ELFIO::SHF_ALLOC);
    except_section->set_addr_align(4);
    
    std::vector<uint8_t> lsda_data;
    for (const auto& func : functions_with_exceptions_) {
        LSDAGenerator gen;
        auto func_lsda = gen.generate(func);
        lsda_data.insert(lsda_data.end(), func_lsda.begin(), func_lsda.end());
    }
    
    except_section->set_data((const char*)lsda_data.data(), lsda_data.size());
}
```

**Link FDE to LSDA**:
Modify FDE generation to include LSDA pointer in augmentation data.

**Test**:
1. Verify section exists: `readelf -S test.obj`
2. Check LSDA: `readelf -wf test.obj` (shows LSDA pointer)

---

## Phase 3: Landing Pad Code Generation ✅ COMPLETED
**Estimated Effort**: 2-3 days  
**Priority**: HIGH - Needed for catch blocks to work  
**Files**: `src/IRConverter.h` ✅ IMPLEMENTED

### 3.1 Uncomment and Implement Landing Pad Code (~6 hours) ✅ COMPLETED
**What**: Generate actual `__cxa_begin_catch` / `__cxa_end_catch` calls

**Implementation Complete**:
- ✅ Uncommented handleCatchBegin landing pad code
- ✅ Call __cxa_begin_catch with exception pointer from RAX
- ✅ Exception pointer passed in RDI (System V ABI)
- ✅ Result (adjusted exception pointer) returned in RAX
- ✅ Uncommented handleCatchEnd cleanup code
- ✅ Call __cxa_end_catch to complete exception handling
- ✅ Added emitMovFromMemory helper for loading exception values

**Verified**: `readelf -s` shows __cxa_begin_catch and __cxa_end_catch symbols

**Modify** `handleCatchBegin` in `IRConverter.h`:
```cpp
void handleCatchBegin(const IrInstruction& instruction) {
    // ... existing metadata recording ...
    
    if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
        const auto& catch_op = instruction.getTypedPayload<CatchBeginOp>();
        
        // Landing pad entry point (personality routine jumps here)
        // Exception pointer is in RAX
        
        // Call __cxa_begin_catch(void* exceptionObject)
        // RDI = exception pointer from RAX
        emitMovRegReg(X64Register::RDI, X64Register::RAX);
        emitCall("__cxa_begin_catch");
        
        // Result in RAX is adjusted exception pointer
        // Extract the actual value and store to catch variable
        if (catch_op.exception_temp.var_number != 0) {
            int32_t stack_offset = getStackOffsetFromTempVar(catch_op.exception_temp);
            
            // For POD types: load value from [RAX] and store to stack
            // Type size from type_index
            const TypeInfo& type_info = gTypeInfo[catch_op.type_index];
            size_t value_size = /* calculate from type_info */;
            
            if (value_size <= 8) {
                // Small type: load and store
                emitMovFromMemory(X64Register::RCX, X64Register::RAX, 0, value_size);
                emitMovToFrame(X64Register::RCX, stack_offset);
            } else {
                // Large type: memcpy
                // ... (similar to exception throwing)
            }
        }
    }
}
```

**Modify** `handleCatchEnd`:
```cpp
void handleCatchEnd(const IrInstruction& instruction) {
    if constexpr (std::is_same_v<TWriterClass, ElfFileWriter>) {
        // Call __cxa_end_catch() to complete exception handling
        emitCall("__cxa_end_catch");
    }
}
```

**Test**:
1. Verify landing pad calls __cxa_begin_catch
2. Verify catch variable receives exception value
3. Verify __cxa_end_catch is called

---

### 3.2 Exception Value Extraction (~4 hours) ✅ COMPLETED
**What**: Properly extract typed exception value

**Implementation Complete**:
- ✅ Reference exception handling (store pointer directly)
- ✅ POD type exception handling (dereference and load value)
- ✅ Size-appropriate memory loads (1, 2, 4, 8 bytes)
- ✅ Value stored to catch variable's stack location
- ✅ Type size checking from TypeInfo

**Note**: Copy constructors for class types by value not yet implemented (would need additional work)

**Challenge**: Exception pointer points to the exception object, need to:
1. Dereference for POD types
2. Use as-is for class types with references
3. Call copy constructor for class types by value

**Implementation**:
```cpp
void extractExceptionValue(const CatchBeginOp& catch_op, X64Register exception_ptr_reg) {
    const TypeInfo& type_info = gTypeInfo[catch_op.type_index];
    
    if (catch_op.is_reference || catch_op.is_rvalue_reference) {
        // Reference: just store the pointer
        emitMovToFrame(exception_ptr_reg, stack_offset);
    } else if (type_info.type_ == Type::Struct) {
        // Class by value: need to copy
        // For now: memcpy
        // TODO: Call copy constructor
        // ... memcpy implementation ...
    } else {
        // POD by value: dereference and copy
        emitMovFromMemory(X64Register::RCX, exception_ptr_reg, 0, type_size);
        emitMovToFrame(X64Register::RCX, stack_offset);
    }
}
```

**Test**: Catch different exception types (int, float, class by value, class by reference)

---

## Phase 4: Personality Routine Integration ✅ COMPLETED
**Estimated Effort**: 1-2 days  
**Priority**: MEDIUM - Can use runtime-provided one initially  
**Files**: `src/ElfFileWriter.h` ✅ IMPLEMENTED
**Status**: ✅ Complete

### 4.1 Reference __gxx_personality_v0 in FDE (~2 hours) ✅ COMPLETED
**What**: Add personality routine pointer to FDE

**Implementation Complete**:
- ✅ Updated CIE augmentation from "zR" to "zPLR"
- ✅ Added personality routine encoding in CIE augmentation data
- ✅ Added PC-relative pointer to __gxx_personality_v0 in CIE
- ✅ Added LSDA encoding (PC-relative signed 4-byte) in CIE
- ✅ Added LSDA pointer in FDE augmentation data for functions with exception handling
- ✅ Created relocation to __gxx_personality_v0 symbol (R_X86_64_PC32)
- ✅ Created relocations to .gcc_except_table with LSDA offsets as addends
- ✅ Automatic symbol creation for __gxx_personality_v0 (undefined external)
- ✅ Automatic section symbol creation for .gcc_except_table

**Verified**:
- ✅ `readelf -wf` shows CIE with "zPLR" augmentation
- ✅ FDEs have augmentation data with LSDA pointers
- ✅ `readelf -r` shows R_X86_64_PC32 relocations to __gxx_personality_v0
- ✅ `readelf -r` shows R_X86_64_PC32 relocations to .gcc_except_table with correct offsets
- ✅ `readelf -s` shows __gxx_personality_v0 as undefined external symbol

**Modify** `generate_eh_frame_fde`:
```cpp
// In FDE augmentation data:
augmentation_data.push_back(DW_EH_PE_pcrel | DW_EH_PE_sdata4);

// Add relocation to personality routine
uint32_t personality_offset = fde_data.size();
add_relocation(personality_offset, "__gxx_personality_v0", 
               ELFIO::R_X86_64_PC32);
fde_data.insert(fde_data.end(), 4, 0);  // Placeholder

// Add LSDA pointer
uint32_t lsda_offset = fde_data.size();
add_relocation(lsda_offset, lsda_symbol, ELFIO::R_X86_64_PC32);
fde_data.insert(fde_data.end(), 4, 0);  // Placeholder
```

**Test**:
1. Link with `-lstdc++` (provides __gxx_personality_v0)
2. Verify relocation: `readelf -r test.obj`
3. Run test - personality routine should be called

---

### 4.2 Verify Personality Routine Calling (~2 hours) ⏸ PENDING TESTING
**What**: Debug that personality routine is invoked correctly

**Status**: Infrastructure complete, ready for runtime testing with libstdc++

**Debug steps**:
1. Set breakpoint in `__gxx_personality_v0`
2. Throw exception
3. Verify personality routine receives:
   - Version (1)
   - Actions (search phase, then cleanup phase)
   - Exception class
   - Exception object
   - Context (unwinder state)
4. Verify it returns `_URC_HANDLER_FOUND`

**Test**: Use gdb to trace personality routine execution

---

## Phase 5: Testing and Refinement
**Estimated Effort**: 2-3 days  
**Priority**: HIGH - Ensure everything works together

### 5.1 Basic Exception Test (~4 hours)
**Test**: Simple throw/catch of int

```cpp
int test() {
    try {
        throw 42;
    } catch (int x) {
        return x;  // Should return 42
    }
    return 0;
}
```

**Verify**:
1. Exception is thrown
2. Type matching works
3. Catch handler receives correct value
4. Function returns 42

---

### 5.2 Multiple Catch Handlers Test (~3 hours)
**Test**: Multiple catch blocks

```cpp
int test(int which) {
    try {
        if (which == 1) throw 42;
        if (which == 2) throw 3.14f;
        if (which == 3) throw "error";
    } catch (int x) {
        return 1;
    } catch (float x) {
        return 2;
    } catch (const char* x) {
        return 3;
    }
    return 0;
}
```

**Verify**: Correct handler is chosen based on type

---

### 5.3 Nested Try/Catch Test (~3 hours)
**Test**: Nested exception handling

```cpp
int test() {
    try {
        try {
            throw 42;
        } catch (float) {
            // Wrong handler - should propagate
        }
    } catch (int x) {
        return x;  // Should catch here
    }
    return 0;
}
```

**Verify**: Exception propagates through non-matching handlers

---

### 5.4 Class Exception Test (~4 hours)
**Test**: User-defined exception class

```cpp
class MyException {
public:
    int code;
    MyException(int c) : code(c) {}
};

int test() {
    try {
        throw MyException(99);
    } catch (MyException& e) {
        return e.code;  // Should return 99
    }
    return 0;
}
```

**Verify**: Class exceptions work correctly

---

### 5.5 Rethrow Test (~2 hours)
**Test**: Rethrowing from catch block

```cpp
int test() {
    try {
        try {
            throw 42;
        } catch (int x) {
            if (x > 0) throw;  // Rethrow
        }
    } catch (int x) {
        return x;  // Should catch rethrown exception
    }
    return 0;
}
```

**Verify**: `__cxa_rethrow` works correctly

---

## Phase 6: Optimization and Edge Cases
**Estimated Effort**: 2-4 days  
**Priority**: LOW - Nice to have

### 6.1 Cleanup Actions (~6 hours)
**What**: Support for destructors during unwinding

Currently we only support catching - need to also call destructors for local variables when unwinding past a frame.

**Implementation**: Add cleanup actions to LSDA action table

---

### 6.2 noexcept Support (~4 hours)
**What**: Handle noexcept functions

noexcept functions shouldn't have exception tables - save space.

---

### 6.3 Exception Specifications (~4 hours)
**What**: Support throw() specifications (deprecated but might exist)

---

## Useful References

### Specifications
- **Itanium C++ ABI**: https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
- **DWARF 4 Standard**: http://dwarfstd.org/doc/DWARF4.pdf
- **LSB Exception Frames**: https://refspecs.linuxfoundation.org/LSB_3.0.0/LSB-Core-generic/LSB-Core-generic/ehframechpt.html

### Tools for Verification
- `readelf -wf file.obj` - Show .eh_frame contents
- `readelf -S file.obj` - Show sections
- `objdump --dwarf=frames file.obj` - Detailed frame info
- `gdb` with `catch throw` / `catch catch` - Debug exception flow

### Example Code to Study
- Look at GCC output: `g++ -S -o test.s test.cpp`
- Compare exception tables: `g++ -c test.cpp && readelf -wf test.o`
- Study libstdc++ source code for `__cxa_*` implementations

### Key Concepts
- **CFA (Canonical Frame Address)**: The value of the stack pointer at call site
- **LSDA (Language Specific Data Area)**: The .gcc_except_table content
- **Personality Routine**: Function that decides what to do during unwinding
- **Landing Pad**: Code that receives control when exception is caught
- **Type Filter**: Index into type table for catch matching

---

## Implementation Strategy

### Recommended Order:
1. **Start with Phase 1.1-1.4** (.eh_frame) - This is foundational
2. **Then Phase 2.1-2.5** (.gcc_except_table) - Enables catch matching
3. **Then Phase 3** (Landing pads) - Makes catches work
4. **Then Phase 4** (Personality routine) - Ties it all together
5. **Then Phase 5** (Testing) - Verify everything works
6. **Finally Phase 6** (Optimizations) - Polish

### Working Incrementally:
- After each phase, commit and test
- Use stubs initially, replace with real implementation gradually
- Compare with GCC output at each step
- Test with simple cases before complex ones

### Getting Help:
- Study how GCC generates exception tables
- Use `readelf` and `objdump` extensively
- Test with gdb to see runtime behavior
- Check LLVM's exception handling code for reference

---

## Estimated Total Timeline
- **Phase 1**: 3-5 days (.eh_frame)
- **Phase 2**: 4-6 days (.gcc_except_table)
- **Phase 3**: 2-3 days (Landing pads)
- **Phase 4**: 1-2 days (Personality routine)
- **Phase 5**: 2-3 days (Testing)
- **Phase 6**: 2-4 days (Optimizations - optional)

**Total**: ~14-23 days of focused work for an experienced compiler engineer

With part-time effort or learning curve, expect 4-6 weeks total.

---

## Files To Create/Modify

**New files to create**:
- `src/DwarfCFI.h` - DWARF encoding utilities  
- `src/LSDAGenerator.h` - LSDA generation  

**Existing files to modify**:
- `src/ElfFileWriter.h` - Add .eh_frame and .gcc_except_table generation  
- `src/IRConverter.h` - Track CFI state, implement landing pads  

---

*For an overview of completed work, see `LINUX_ELF_SUPPORT_PLAN.md` - Milestone 6.*

