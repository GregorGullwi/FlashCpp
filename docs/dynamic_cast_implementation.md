# dynamic_cast Implementation Guide

## Overview

This document describes the implementation of `dynamic_cast` with RTTI (Run-Time Type Information) support in the FlashCpp compiler.

## Implementation Details

### Components

1. **Parser & AST** (`src/Parser.cpp`, `src/AstNodeTypes.h`)
   - Already existed: Parses `dynamic_cast<Type>(expr)` syntax
   - Creates `DynamicCastNode` in AST

2. **IR Generation** (`src/CodeGen.h`)
   - Generates `IrOpcode::DynamicCast` instructions
   - Captures: result var, source ptr, target type name, is_reference flag
   - **Contains inline runtime helpers**: `__dynamic_cast_check()` and `__dynamic_cast_throw_bad_cast()`
   - RTTI structure definition (`RTTIInfo`) used by both compile-time and runtime code

3. **Code Generation** (`src/IRConverter.h`)
   - `handleDynamicCast()`: Emits x64 machine code
   - Calls runtime helper `__dynamic_cast_check()`
   - For pointer casts: Returns source pointer on success, nullptr on failure
   - For reference casts: Returns source reference on success, throws `std::bad_cast` on failure

4. **RTTI Emission** (`src/ObjFileWriter.h`)
   - `add_vtable()`: Emits RTTI structures to `.rdata` section
   - Creates `__rtti_<classname>` symbols
   - Generates relocations for type checking

**Note**: Runtime helpers are now integrated directly into `CodeGen.h` as inline functions, consistent with the compiler's header-only architecture. No separate runtime file is needed.

### RTTI Structure Layout

```cpp
struct RTTIInfo {
    uint64_t class_name_hash;  // Hash of class name for identity
    uint64_t num_bases;        // Number of base classes
    // RTTIInfo* base_ptrs[];  // Inline array of base class RTTI pointers
};
```

Memory layout in `.rdata`:
```
Offset 0:  class_name_hash (8 bytes)
Offset 8:  num_bases (8 bytes)
Offset 16: base_0_rtti_ptr (8 bytes)
Offset 24: base_1_rtti_ptr (8 bytes)
...
```

### VTable Layout

```
Memory layout:
  vtable[-1] (offset -8): RTTI pointer
  vtable[0]  (offset 0):  first virtual function  <- vptr points here
  vtable[1]  (offset 8):  second virtual function
  ...
```

### Object Layout

```
Object memory:
  [vptr: 8 bytes] <- points to vtable[0]
  [member data...]
```

## How It Works

### Runtime Algorithm

**For pointer casts** (`dynamic_cast<Derived*>(base_ptr)`):

1. **Null check**: If `base_ptr` is null, return null
2. **Load vptr**: Read first 8 bytes of object
3. **Load source RTTI**: Read `vtable[-1]` (8 bytes before vptr)
4. **Get target RTTI**: Load address of `__rtti_Derived`
5. **Call helper**: `__dynamic_cast_check(source_rtti, target_rtti)`
6. **Return**: If helper returns true, return `base_ptr`; else return null

**For reference casts** (`dynamic_cast<Derived&>(base_ref)`):

1. **Load vptr**: Read first 8 bytes of object
2. **Load source RTTI**: Read `vtable[-1]` (8 bytes before vptr)
3. **Get target RTTI**: Load address of `__rtti_Derived`
4. **Call helper**: `__dynamic_cast_check(source_rtti, target_rtti)`
5. **Return or throw**: If helper returns true, return `base_ref`; else call `__dynamic_cast_throw_bad_cast()` which throws `std::bad_cast`

### Type Checking Logic

The `__dynamic_cast_check()` function:

1. **Exact match**: Check if source == target (by pointer)
2. **Hash match**: Check if source.hash == target.hash (for duplicate RTTI)
3. **Inheritance check**: Recursively check each base class

```cpp
bool __dynamic_cast_check(RTTIInfo* source, RTTIInfo* target) {
    if (!source || !target) return false;
    if (source == target) return true;  // Exact match
    if (source->hash == target->hash) return true;  // Hash match
    
    // Check bases recursively
    for (each base in source->bases) {
        if (__dynamic_cast_check(base, target)) return true;
    }
    return false;
}
```

## Building and Linking

The runtime helper functions are now inlined directly in `src/CodeGen.h`, so no separate compilation or header inclusion is needed. The functions are automatically available when the generated code is linked with the C++ runtime library.

### On Windows with MSVC

1. Compile your code with FlashCpp:
```batch
x64\Debug\FlashCpp.exe mycode.cpp
```

2. Link with the C++ runtime:
```batch
link.bat mycode.obj kernel32.lib
```

### On Linux/WSL with clang

1. Compile your code with FlashCpp:
```bash
./x64/Debug/FlashCpp mycode.cpp
```

2. Link with the C++ runtime (platform-specific):
```bash
# This is platform-specific and may require additional setup
```

## Example Usage

### Source Code

```cpp
struct Base {
    int value;
    virtual ~Base() {}
    virtual int getValue() { return value; }
};

struct Derived : public Base {
    int extra;
    virtual int getValue() { return value + extra; }
};

int main() {
    Derived d;
    d.value = 10;
    d.extra = 20;
    
    Base* base_ptr = &d;
    
    // Pointer cast - successful downcast
    Derived* derived_ptr = dynamic_cast<Derived*>(base_ptr);
    if (derived_ptr) {
        return derived_ptr->getValue();  // Returns 30
    }
    
    // Pointer cast - failed downcast
    Base b;
    base_ptr = &b;
    derived_ptr = dynamic_cast<Derived*>(base_ptr);
    // derived_ptr is nullptr
    
    // Reference cast - successful downcast
    Base& base_ref = d;
    Derived& derived_ref = dynamic_cast<Derived&>(base_ref);
    // Success: derived_ref refers to d
    
    // Reference cast - failed downcast (throws std::bad_cast)
    Base& base_ref2 = b;
    try {
        Derived& derived_ref2 = dynamic_cast<Derived&>(base_ref2);
        // This line is never reached
    } catch (const std::bad_cast& e) {
        // Exception caught - cast failed
    }
    
    return 0;
}
```

### Generated RTTI

For the above code, FlashCpp generates:

```
__rtti_Base:
  .quad <hash("Base")>
  .quad 0              ; no base classes
  
__rtti_Derived:
  .quad <hash("Derived")>
  .quad 1              ; one base class
  .quad __rtti_Base    ; pointer to base class RTTI
```

### Generated VTable

```
??_7Base@@6B@:       ; vtable for Base
  .quad __rtti_Base  ; RTTI pointer
  .quad ?getValue@Base@@UEAAHXZ   ; Base::getValue()
  .quad ??_GBase@@UEAAPEAXI@Z     ; Base::~Base()

??_7Derived@@6B@:    ; vtable for Derived  
  .quad __rtti_Derived  ; RTTI pointer
  .quad ?getValue@Derived@@UEAAHXZ   ; Derived::getValue() (override)
  .quad ??_GDerived@@UEAAPEAXI@Z     ; Derived::~Derived()
```

## Limitations

1. **Cross-casts**: Casts between sibling classes through virtual inheritance
   - Currently supported via recursive base checking
   - May not work with complex diamond inheritance

2. **Visibility**: All RTTI symbols are external
   - May cause issues with private/protected inheritance
   - Future: Implement access control in RTTI

3. **Exception handling**: Requires linking with C++ runtime library for exception support
   - Reference casts throw `std::bad_cast` on failure
   - Ensure proper exception handling infrastructure is available in target environment

## Performance Considerations

- **Exact type check**: O(1) - just pointer/hash comparison
- **Inheritance check**: O(depth × breadth) - recursive traversal
- **Null check**: O(1) - early exit
- **Security overhead**: O(1) - bounds check on num_bases

## Debugging

To debug RTTI issues:

1. **Check RTTI emission**: Look for `__rtti_<classname>` symbols in object file
2. **Verify vtable layout**: Ensure vtable[-1] contains RTTI pointer
3. **Trace runtime checks**: Add debug output to `__dynamic_cast_check()`
4. **Validate relocations**: Check that base class RTTI pointers are resolved

## Future Enhancements

1. **Optimizations**: Cache successful cast results
2. **Type names**: Store demangled names for better debugging
3. **Standard compliance**: Full C++ RTTI compatibility
