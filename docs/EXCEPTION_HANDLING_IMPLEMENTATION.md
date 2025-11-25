# MSVC SEH Exception Handling Implementation Guide

This document provides detailed guidance on implementing MSVC-compatible Structured Exception Handling (SEH) code generation for the FlashCpp compiler's exception handling features.

## Overview

The parser already supports C++ exception handling syntax (try/catch/throw/noexcept). This guide covers implementing the code generation phase to produce MSVC-compatible x64 exception handling.

## Background: Windows x64 Exception Handling

Windows x64 uses table-based exception handling rather than frame-based (as in x86). Key components:

1. **Exception Directory** - Contains function table entries
2. **RUNTIME_FUNCTION** entries - Map code ranges to unwind information
3. **UNWIND_INFO** structures - Describe how to unwind the stack
4. **Language-specific handler data** - C++-specific exception handling info

## Required Components

### 1. Exception Tables (.pdata and .xdata sections)

**`.pdata` section** - Contains RUNTIME_FUNCTION entries:
```cpp
struct RUNTIME_FUNCTION {
    DWORD BeginAddress;      // RVA of function start
    DWORD EndAddress;        // RVA of function end
    DWORD UnwindInfoAddress; // RVA of UNWIND_INFO
};
```

**`.xdata` section** - Contains UNWIND_INFO structures:
```cpp
struct UNWIND_INFO {
    BYTE Version : 3;
    BYTE Flags : 5;
    BYTE SizeOfProlog;
    BYTE CountOfCodes;
    BYTE FrameRegister : 4;
    BYTE FrameOffset : 4;
    UNWIND_CODE UnwindCode[1];
    // Optional: Exception handler RVA (if UNW_FLAG_EHANDLER set)
    // Optional: Language-specific handler data
};
```

### 2. C++ Exception Handler Data

For each function with try/catch blocks, we need:

```cpp
struct FuncInfo {
    DWORD magicNumber;           // 0x19930520 for x64, 0x19930521 for x86
    DWORD maxState;              // Number of try blocks
    DWORD pUnwindMap;            // Offset to UnwindMapEntry array
    DWORD nTryBlocks;            // Number of try blocks
    DWORD pTryBlockMap;          // Offset to TryBlockMapEntry array
    DWORD nIPMapEntries;         // Number of IP-to-state entries
    DWORD pIPtoStateMap;         // Offset to IPtoStateMap
    DWORD pESTypeList;           // Exception specification type list (for noexcept)
    DWORD EHFlags;               // Exception handling flags
};

struct TryBlockMapEntry {
    DWORD tryLow;                // Start state of try block
    DWORD tryHigh;               // End state of try block
    DWORD catchHigh;             // End state of catch blocks
    DWORD nCatches;              // Number of catch handlers
    DWORD pHandlerArray;         // Offset to HandlerType array
};

struct HandlerType {
    DWORD adjectives;            // Catch handler adjectives (const, volatile, reference)
    DWORD pType;                 // Offset to type descriptor (for type matching)
    DWORD dispCatchObj;          // Stack offset for caught object
    DWORD addressOfHandler;      // RVA of catch block code
    DWORD dispFrame;             // Frame pointer displacement
};

struct UnwindMapEntry {
    DWORD toState;               // Target state after unwind
    DWORD action;                // RVA of cleanup/destructor to call
};
```

### 3. Type Descriptors

For RTTI (Runtime Type Information) matching:

```cpp
struct TypeDescriptor {
    const void* pVFTable;        // Pointer to type_info vtable
    void* spare;
    char name[1];                // Mangled type name
};
```

## Implementation Steps

### Step 1: Add IR Instructions for Exception Handling

Add to `IRTypes.h`:

```cpp
enum class IrOpcode {
    // ... existing opcodes ...
    
    // Exception handling
    TryBegin,           // Mark start of try block
    TryEnd,             // Mark end of try block
    CatchBegin,         // Mark start of catch handler
    CatchEnd,           // Mark end of catch handler
    Throw,              // Throw exception
    Rethrow,            // Rethrow current exception
    RegisterCleanup,    // Register destructor for stack unwinding
};

struct TryBeginOp {
    std::string_view try_label;
    std::string_view catch_label;
    int state_number;            // Exception state for unwinding
};

struct CatchBeginOp {
    std::string_view catch_label;
    std::optional<Type> exception_type;  // nullopt for catch(...)
    std::optional<std::string_view> exception_var;
    int state_number;
};

struct ThrowOp {
    TypedValue exception_value;
    Type exception_type;
};
```

### Step 2: Implement Visitor Methods in CodeGen.h

Add visitor methods for exception nodes:

```cpp
void visitTryStatementNode(const TryStatementNode& node) {
    // 1. Generate unique labels
    static size_t try_counter = 0;
    std::string_view try_begin_label = /* generate */;
    std::string_view try_end_label = /* generate */;
    std::string_view continuation_label = /* generate */;
    
    // 2. Emit TryBegin marker
    int try_state = allocate_exception_state();
    ir_.addInstruction(IrInstruction(IrOpcode::TryBegin, 
        TryBeginOp{try_begin_label, try_end_label, try_state}, Token()));
    
    // 3. Generate try block code
    ir_.addInstruction(IrInstruction(IrOpcode::Label, 
        LabelOp{try_begin_label}, Token()));
    visit(node.try_block());
    
    // 4. Jump to continuation if no exception
    ir_.addInstruction(IrInstruction(IrOpcode::Branch,
        BranchOp{continuation_label}, Token()));
    
    ir_.addInstruction(IrInstruction(IrOpcode::Label,
        LabelOp{try_end_label}, Token()));
    
    // 5. Generate catch handlers
    for (const auto& catch_clause : node.catch_clauses()) {
        visitCatchClauseNode(catch_clause.as<CatchClauseNode>());
    }
    
    // 6. Continuation point
    ir_.addInstruction(IrInstruction(IrOpcode::Label,
        LabelOp{continuation_label}, Token()));
}

void visitCatchClauseNode(const CatchClauseNode& node) {
    // Generate catch handler entry point
    static size_t catch_counter = 0;
    std::string_view catch_label = /* generate */;
    
    int catch_state = allocate_exception_state();
    
    // Extract exception type
    std::optional<Type> exception_type;
    std::optional<std::string_view> exception_var;
    
    if (!node.is_catch_all()) {
        const auto& decl = node.exception_declaration()->as<DeclarationNode>();
        exception_type = decl.type_node().as<TypeSpecifierNode>().type();
        exception_var = decl.identifier_token().value();
    }
    
    ir_.addInstruction(IrInstruction(IrOpcode::CatchBegin,
        CatchBeginOp{catch_label, exception_type, exception_var, catch_state},
        Token()));
    
    ir_.addInstruction(IrInstruction(IrOpcode::Label,
        LabelOp{catch_label}, Token()));
    
    // Generate catch block code
    visit(node.body());
    
    ir_.addInstruction(IrInstruction(IrOpcode::CatchEnd, {}, Token()));
}

void visitThrowStatementNode(const ThrowStatementNode& node) {
    if (node.is_rethrow()) {
        ir_.addInstruction(IrInstruction(IrOpcode::Rethrow, {}, node.throw_token()));
    } else {
        auto exception_operands = visitExpressionNode(
            node.expression()->as<ExpressionNode>());
        
        ThrowOp throw_op;
        throw_op.exception_value = toTypedValue(exception_operands);
        throw_op.exception_type = /* determine type */;
        
        ir_.addInstruction(IrInstruction(IrOpcode::Throw,
            std::move(throw_op), node.throw_token()));
    }
}
```

### Step 3: Track Exception States

Maintain exception state tracking:

```cpp
class AstToIr {
private:
    int current_exception_state_ = 0;
    std::vector<int> exception_state_stack_;
    
    struct ExceptionStateInfo {
        int state_number;
        std::vector<std::string_view> destructors_to_call;
        std::optional<Type> active_exception_type;
    };
    
    std::vector<ExceptionStateInfo> exception_states_;
    
    int allocate_exception_state() {
        return current_exception_state_++;
    }
};
```

### Step 4: Implement IR to Object Code Conversion

In `IRConverter.h`, add methods to generate exception tables:

```cpp
class IrToObjConverter {
private:
    std::vector<RUNTIME_FUNCTION> runtime_functions_;
    std::vector<UNWIND_INFO> unwind_infos_;
    std::vector<FuncInfo> func_infos_;
    
    void generateExceptionTables(const IrFunction& func) {
        // 1. Collect all try/catch blocks
        // 2. Build TryBlockMapEntry for each try block
        // 3. Build HandlerType for each catch handler
        // 4. Build UnwindMapEntry for each state transition
        // 5. Generate FuncInfo structure
        // 6. Add to .xdata section
    }
    
    void emitPDataSection() {
        // Write RUNTIME_FUNCTION entries to .pdata
        for (const auto& rtf : runtime_functions_) {
            obj_file_.addData(".pdata", &rtf, sizeof(rtf));
        }
    }
    
    void emitXDataSection() {
        // Write UNWIND_INFO and handler data to .xdata
        for (const auto& unwind : unwind_infos_) {
            obj_file_.addData(".xdata", &unwind, sizeof(unwind));
        }
    }
};
```

### Step 5: Add Runtime Support Functions

Link with CRT exception handling functions:

- `_CxxThrowException` - Throw an exception
- `__CxxFrameHandler4` - Exception handler (VS2019+)
- `__CxxFrameHandler3` - Exception handler (older)
- `_CxxUnwindHelper` - Unwind stack

These are provided by the MSVC runtime (`vcruntime.lib`).

### Step 6: Handle noexcept

For functions marked `noexcept`:

```cpp
void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
    // ... existing code ...
    
    if (node.is_noexcept()) {
        // Add exception specification to FuncInfo
        // If exception is thrown from noexcept function, call std::terminate
        
        if (node.has_noexcept_expression()) {
            // Evaluate noexcept(expr) at compile time
            // If false, don't add termination behavior
        } else {
            // noexcept is equivalent to noexcept(true)
            // Wrap function body in implicit catch(...) that calls terminate
        }
    }
}
```

## Testing Strategy

### Unit Tests

1. **Basic try/catch**: Single catch handler
2. **Multiple catch handlers**: Type matching order
3. **catch(...)**: Catch-all handler
4. **Nested try/catch**: Exception propagation
5. **Rethrow**: Rethrowing caught exceptions
6. **Stack unwinding**: Destructors called during unwinding
7. **noexcept**: Termination on exception

### Integration Tests

Create test programs that:
- Throw and catch standard types (int, float, custom classes)
- Test exception propagation through multiple stack frames
- Verify destructors are called during unwinding
- Test noexcept enforcement

## References

### MSVC Documentation
- [x64 Exception Handling](https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64)
- [UNWIND_INFO Structure](https://docs.microsoft.com/en-us/cpp/build/unwind-data-structure-definition)
- [C++ Exception Handling](https://docs.microsoft.com/en-us/cpp/cpp/exception-handling-in-visual-cpp)

### Itanium ABI (for comparison)
- [Itanium C++ ABI: Exception Handling](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)

### Tools
- `dumpbin /unwindinfo` - Examine exception tables in .obj files
- `dumpbin /headers` - View section headers including .pdata/.xdata

## Implementation Complexity Estimate

- **IR Instructions & AST Visitors**: 300-400 lines
- **Exception State Tracking**: 200-300 lines
- **Object Code Generation**: 500-700 lines
- **Runtime Function Calls**: 100-200 lines
- **Testing**: 400-500 lines

**Total**: ~1500-2100 lines of code

**Estimated Time**: 3-5 days for experienced developer

## Phased Implementation Approach

### Phase 1: Basic Infrastructure (Day 1)
- Add IR opcodes for exception handling
- Implement visitor methods (stub implementations)
- Add exception state tracking

### Phase 2: Simple Cases (Day 2)
- Implement throw/catch for basic types (int, pointers)
- Generate basic exception tables
- Test with simple try/catch/throw

### Phase 3: Type Matching (Day 3)
- Implement RTTI type matching for catch handlers
- Support catch(...) 
- Handle exception type hierarchy

### Phase 4: Stack Unwinding (Day 4)
- Implement destructor registration
- Generate unwind map
- Test with objects requiring cleanup

### Phase 5: Advanced Features (Day 5)
- Implement noexcept
- Handle nested try/catch
- Optimize exception tables
- Comprehensive testing

## Current Status

✅ **Completed**: Parser support for try/catch/throw/noexcept
⏸️ **Not Started**: Code generation implementation

The parser changes in this PR provide the foundation. Code generation can be implemented in a follow-up PR using this guide.
