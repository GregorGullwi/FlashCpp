#pragma once

/**
 * @file PlatformInternals.h
 * @brief Platform-specific runtime function declarations and documentation
 * 
 * This header documents the platform differences between Windows and Linux
 * for C++ runtime functionality. The FlashCpp compiler currently targets
 * Windows (MSVC ABI), but this header provides guidance for Linux support.
 * 
 * Key Platform Differences:
 * 
 * 1. Exception Handling
 *    - Windows: Uses MSVC-specific _CxxThrowException
 *    - Linux:   Uses Itanium C++ ABI (__cxa_throw, __cxa_allocate_exception, etc.)
 * 
 * 2. Process Termination
 *    - Windows: Uses ExitProcess (Windows API)
 *    - Linux:   Uses exit() (POSIX standard)
 * 
 * 3. RTTI (Runtime Type Information)
 *    - Windows: Proprietary MSVC format with ??_R symbols
 *    - Linux:   Itanium C++ ABI typeinfo structures
 */

// ============================================================================
// EXCEPTION HANDLING
// ============================================================================

/**
 * @section Exception Handling - Windows (MSVC ABI)
 * 
 * Windows uses the MSVC-specific exception handling mechanism centered around
 * the _CxxThrowException function. This is currently implemented in FlashCpp.
 * 
 * @subsection Windows Exception Functions
 * 
 * @function _CxxThrowException
 * @brief Throws a C++ exception (MSVC runtime)
 * @param pExceptionObject Pointer to the exception object to throw
 * @param pThrowInfo Pointer to ThrowInfo structure describing the exception type
 *                   Can be NULL for simple cases or rethrow operations
 * @note This function never returns ([[noreturn]])
 * @note When both parameters are NULL, rethrows the current exception
 * 
 * Current Implementation:
 * - Used in handleThrow() in IRConverter.h (line ~10982)
 * - Used in handleRethrow() in IRConverter.h (line ~11076)
 * - Allocates exception object on stack
 * - Calls _CxxThrowException with object pointer and NULL throw info
 * - Stack unwinding handled by Windows SEH via PDATA/XDATA sections
 * 
 * Related Windows Structures:
 * - ThrowInfo: Describes exception type and destructor
 * - CatchableTypeArray: Array of types that can catch this exception
 * - SEH (Structured Exception Handling) tables in PDATA/XDATA sections
 */

/**
 * @section Exception Handling - Linux (Itanium C++ ABI)
 * 
 * Linux uses the Itanium C++ ABI for exception handling, which is the standard
 * for most Unix-like systems. This is NOT currently implemented in FlashCpp.
 * 
 * @subsection Linux Exception Functions (TO BE IMPLEMENTED)
 * 
 * @function __cxa_allocate_exception
 * @brief Allocates memory for an exception object
 * @param thrown_size Size of the exception object in bytes
 * @return Pointer to allocated memory for the exception object
 * @note Memory is managed by the runtime and freed during unwinding
 * 
 * Signature:
 *   extern "C" void* __cxa_allocate_exception(size_t thrown_size);
 * 
 * Usage:
 *   void* exc = __cxa_allocate_exception(sizeof(MyException));
 *   new (exc) MyException(...);  // Placement new to construct
 *   __cxa_throw(exc, typeinfo, destructor);
 * 
 * 
 * @function __cxa_throw
 * @brief Throws a C++ exception (Itanium C++ ABI)
 * @param thrown_object Pointer to the exception object (from __cxa_allocate_exception)
 * @param tinfo Pointer to std::type_info for the exception type
 * @param dest Destructor function pointer for the exception object (can be NULL)
 * @note This function never returns ([[noreturn]])
 * 
 * Signature:
 *   extern "C" void __cxa_throw(void* thrown_object,
 *                               std::type_info* tinfo,
 *                               void (*dest)(void*)) __attribute__((noreturn));
 * 
 * Implementation Notes:
 * - Begins stack unwinding process
 * - Calls destructors for objects in unwound stack frames
 * - Searches for matching catch handlers
 * - If no handler found, calls std::terminate()
 * 
 * 
 * @function __cxa_begin_catch
 * @brief Begins exception handling in a catch block
 * @param exc_obj_in Pointer to the exception object being caught
 * @return Adjusted pointer to the exception object for the catch handler
 * 
 * Signature:
 *   extern "C" void* __cxa_begin_catch(void* exc_obj_in);
 * 
 * Usage:
 *   Called automatically at the start of each catch block
 *   Decrements exception reference count
 * 
 * 
 * @function __cxa_end_catch
 * @brief Ends exception handling in a catch block
 * @note Destroys the exception object if no longer needed
 * 
 * Signature:
 *   extern "C" void __cxa_end_catch();
 * 
 * Usage:
 *   Called automatically at the end of each catch block
 *   May destroy the exception object or allow rethrow
 * 
 * 
 * @function __cxa_rethrow
 * @brief Rethrows the current exception
 * @note This function never returns ([[noreturn]])
 * 
 * Signature:
 *   extern "C" void __cxa_rethrow() __attribute__((noreturn));
 * 
 * Usage:
 *   Used to implement 'throw;' statements within catch blocks
 * 
 * 
 * @subsection Comparison: Windows vs Linux Exception Handling
 * 
 * Windows (_CxxThrowException):
 *   - Single function call with exception object and type info
 *   - Runtime allocates and manages exception object
 *   - throw expr;   -> _CxxThrowException(&expr, &ThrowInfo)
 *   - throw;        -> _CxxThrowException(NULL, NULL)
 * 
 * Linux (Itanium C++ ABI):
 *   - Two-step process: allocate then throw
 *   - Explicit allocation of exception object
 *   - throw expr;   -> void* p = __cxa_allocate_exception(sizeof(expr));
 *                      new (p) T(expr);
 *                      __cxa_throw(p, &typeid(T), &destructor);
 *   - throw;        -> __cxa_rethrow();
 * 
 * Both systems:
 *   - Support stack unwinding with automatic destructor calls
 *   - Support exception type matching in catch handlers
 *   - Terminate program if no matching catch handler found
 */

// ============================================================================
// PROCESS TERMINATION
// ============================================================================

/**
 * @section Process Termination - Windows
 * 
 * @function ExitProcess
 * @brief Terminates the calling process and all its threads (Windows API)
 * @param uExitCode Exit code for the process (0 = success)
 * @note This function never returns ([[noreturn]])
 * 
 * Signature:
 *   VOID ExitProcess(UINT uExitCode);
 * 
 * Usage in FlashCpp:
 *   - Currently NOT explicitly used in the compiler
 *   - Would be used for program termination in generated executables
 *   - Typical usage: ExitProcess(0) for successful termination
 * 
 * Characteristics:
 *   - Terminates ALL threads immediately
 *   - Does NOT call destructors for C++ objects
 *   - Does NOT flush I/O buffers
 *   - Releases OS resources (handles, memory, etc.)
 *   - DLLs receive DLL_PROCESS_DETACH notification
 */

/**
 * @section Process Termination - Linux (TO BE IMPLEMENTED)
 * 
 * @function exit
 * @brief Terminates the calling process (POSIX standard)
 * @param status Exit code for the process (0 = success)
 * @note This function never returns ([[noreturn]])
 * 
 * Signature:
 *   void exit(int status);
 * 
 * Usage in FlashCpp (when Linux support is added):
 *   - Replace ExitProcess calls with exit()
 *   - Typical usage: exit(0) for successful termination
 * 
 * Characteristics:
 *   - Calls functions registered with atexit() in reverse order
 *   - Flushes and closes all open FILE streams
 *   - Does NOT call destructors for C++ objects (use _Exit for immediate termination)
 *   - Releases OS resources
 * 
 * @function _Exit
 * @brief Immediately terminates process without cleanup (POSIX)
 * @param status Exit code for the process
 * 
 * Signature:
 *   void _Exit(int status);
 * 
 * Comparison:
 *   - exit():      Performs cleanup (atexit, flush buffers)
 *   - _Exit():     No cleanup, immediate termination (similar to ExitProcess)
 *   - quick_exit(): Calls at_quick_exit() handlers only
 */

// ============================================================================
// RTTI (Runtime Type Information)
// ============================================================================

/**
 * @section RTTI - Windows (MSVC ABI)
 * 
 * Windows uses a proprietary RTTI format with mangled symbol names starting
 * with ??_R. This is currently implemented in FlashCpp.
 * 
 * @subsection Windows RTTI Structures
 * 
 * The MSVC RTTI system uses a multi-level structure hierarchy:
 * 
 * ??_R4 (Complete Object Locator)
 *   ├── Points to ??_R0 (Type Descriptor)
 *   └── Points to ??_R3 (Class Hierarchy Descriptor)
 *         └── Points to ??_R2 (Base Class Array)
 *               └── Contains ??_R1 (Base Class Descriptors)
 *                     └── Each points to ??_R0 (Type Descriptor)
 * 
 * 
 * @struct MSVCTypeDescriptor (??_R0)
 * @brief Type descriptor - simplified type_info equivalent
 * 
 * Structure:
 *   struct MSVCTypeDescriptor {
 *       const void* vtable;              // Pointer to type_info vtable
 *       const void* spare;               // Reserved/spare pointer
 *       char name[1];                    // Variable-length mangled name
 *   };
 * 
 * Current Implementation: src/AstNodeTypes.h (line ~266)
 * Usage: Basic type identification for RTTI and dynamic_cast
 * 
 * 
 * @struct MSVCBaseClassDescriptor (??_R1)
 * @brief Descriptor for a single base class in the hierarchy
 * 
 * Structure:
 *   struct MSVCBaseClassDescriptor {
 *       const MSVCTypeDescriptor* type_descriptor;  // ??_R0 for base class
 *       uint32_t num_contained_bases;    // Number of nested bases
 *       int32_t mdisp;                   // Member displacement (offset)
 *       int32_t pdisp;                   // Vbtable displacement (-1 if not virtual)
 *       int32_t vdisp;                   // Displacement inside vbtable
 *       uint32_t attributes;             // Flags (virtual, ambiguous, etc.)
 *   };
 * 
 * Current Implementation: src/AstNodeTypes.h (line ~273)
 * Usage: Describes each base class for inheritance traversal
 * 
 * 
 * @struct MSVCBaseClassArray (??_R2)
 * @brief Array of pointers to base class descriptors
 * 
 * Structure:
 *   struct MSVCBaseClassArray {
 *       const MSVCBaseClassDescriptor* base_class_descriptors[1];
 *   };
 * 
 * Current Implementation: src/AstNodeTypes.h (line ~283)
 * Usage: Variable-length array of all base classes (including self)
 * 
 * 
 * @struct MSVCClassHierarchyDescriptor (??_R3)
 * @brief Describes the complete class hierarchy
 * 
 * Structure:
 *   struct MSVCClassHierarchyDescriptor {
 *       uint32_t signature;              // Always 0
 *       uint32_t attributes;             // Multiple/virtual inheritance flags
 *       uint32_t num_base_classes;       // Number of bases (including self)
 *       const MSVCBaseClassArray* base_class_array;  // ??_R2
 *   };
 * 
 * Current Implementation: src/AstNodeTypes.h (line ~288)
 * Usage: Top-level hierarchy information
 * 
 * 
 * @struct MSVCCompleteObjectLocator (??_R4)
 * @brief Complete object locator - referenced by vtable
 * 
 * Structure:
 *   struct MSVCCompleteObjectLocator {
 *       uint32_t signature;              // 0 for 32-bit, 1 for 64-bit
 *       uint32_t offset;                 // Offset of vtable in complete class
 *       uint32_t cd_offset;              // Constructor displacement offset
 *       const MSVCTypeDescriptor* type_descriptor;        // ??_R0
 *       const MSVCClassHierarchyDescriptor* hierarchy;    // ??_R3
 *   };
 * 
 * Current Implementation: src/AstNodeTypes.h (line ~296)
 * Usage: Located before vtable in memory, accessed for dynamic_cast/typeid
 * 
 * Building RTTI Structures:
 *   - Implementation: src/AstNodeTypes.cpp build_rtti_info() (line ~840)
 *   - Called during class definition processing
 *   - Structures persist in static storage for program lifetime
 */

/**
 * @section RTTI - Linux (Itanium C++ ABI) (TO BE IMPLEMENTED)
 * 
 * Linux uses the Itanium C++ ABI for RTTI, which defines a standard typeinfo
 * hierarchy. This is NOT currently implemented in FlashCpp.
 * 
 * @subsection Linux RTTI Structures
 * 
 * The Itanium C++ ABI RTTI system uses std::type_info as the base:
 * 
 * vtable for std::type_info
 *   ├── std::type_info (base for all type information)
 *   ├── __fundamental_type_info (for built-in types)
 *   ├── __array_type_info (for array types)
 *   ├── __function_type_info (for function types)
 *   ├── __pointer_type_info (for pointer types)
 *   ├── __pbase_type_info (base for pointer-to-member)
 *   │     ├── __pointer_to_member_type_info
 *   │     └── __pointer_type_info (also derived from this)
 *   ├── __class_type_info (for classes without bases)
 *   ├── __si_class_type_info (for classes with single base)
 *   └── __vmi_class_type_info (for classes with multiple/virtual bases)
 * 
 * 
 * @class std::type_info
 * @brief Base class for all RTTI type information
 * 
 * Structure (simplified):
 *   namespace std {
 *       class type_info {
 *       public:
 *           virtual ~type_info();
 *           const char* name() const;
 *           bool operator==(const type_info& rhs) const;
 *           bool operator!=(const type_info& rhs) const;
 *           bool before(const type_info& rhs) const;
 *           size_t hash_code() const;
 *       private:
 *           const char* __name;          // Mangled type name
 *       };
 *   }
 * 
 * Mangling:
 *   - Uses Itanium C++ ABI mangling scheme
 *   - Example: "3Foo" for class Foo, "i" for int
 *   - Different from MSVC mangling (.?AVFoo@@ vs 3Foo)
 * 
 * 
 * @class __class_type_info
 * @brief Type info for classes without base classes
 * 
 * Structure:
 *   namespace __cxxabiv1 {
 *       class __class_type_info : public std::type_info {
 *       public:
 *           virtual ~__class_type_info();
 *       };
 *   }
 * 
 * Usage:
 *   - Simple classes with no inheritance
 *   - Only stores type name, no base class information
 * 
 * 
 * @class __si_class_type_info
 * @brief Type info for classes with single, public, non-virtual base
 * 
 * Structure:
 *   namespace __cxxabiv1 {
 *       class __si_class_type_info : public __class_type_info {
 *       public:
 *           virtual ~__si_class_type_info();
 *           const __class_type_info* __base_type;
 *       };
 *   }
 * 
 * Usage:
 *   - Classes with simple inheritance (one base, non-virtual)
 *   - Stores pointer to base class type_info
 * 
 * 
 * @class __vmi_class_type_info
 * @brief Type info for classes with multiple or virtual bases
 * 
 * Structure:
 *   namespace __cxxabiv1 {
 *       class __vmi_class_type_info : public __class_type_info {
 *       public:
 *           virtual ~__vmi_class_type_info();
 *           unsigned int __flags;
 *           unsigned int __base_count;
 *           __base_class_type_info __base_info[1];  // Variable length
 *       };
 * 
 *       struct __base_class_type_info {
 *           const __class_type_info* __base_type;
 *           long __offset_flags;
 *       };
 *   }
 * 
 * Flags:
 *   - __non_diamond_repeat_mask = 0x1  // Has repeated bases
 *   - __diamond_shaped_mask = 0x2      // Has diamond inheritance
 * 
 * Offset Flags (in __offset_flags):
 *   - Low bits: Base class offset in derived class
 *   - __virtual_mask = 0x1       // Virtual base class
 *   - __public_mask = 0x2        // Public base class
 * 
 * 
 * @subsection Comparison: Windows vs Linux RTTI
 * 
 * Symbol Names:
 *   Windows: ??_R0?AVFoo@@8           (Type Descriptor for class Foo)
 *   Linux:   _ZTI3Foo                 (typeinfo for Foo)
 * 
 * Type Representation:
 *   Windows: Multi-level structure (??_R0 through ??_R4)
 *   Linux:   Single object derived from std::type_info
 * 
 * Base Class Info:
 *   Windows: Separate arrays and descriptors (??_R1, ??_R2)
 *   Linux:   Embedded in typeinfo object (__base_info array)
 * 
 * Mangling:
 *   Windows: .?AVFoo@@, .?AUBar@@ (class/struct prefix)
 *   Linux:   3Foo, 3Bar (length-prefixed names)
 * 
 * vtable Layout:
 *   Windows: ??_R4 located BEFORE vtable at offset -sizeof(void*)
 *   Linux:   typeinfo pointer at offset -sizeof(void*) in vtable
 * 
 * Implementation Strategy for Linux Support:
 *   1. Replace MSVCTypeDescriptor with std::type_info hierarchy
 *   2. Generate appropriate __class_type_info, __si_class_type_info, 
 *      or __vmi_class_type_info based on class inheritance
 *   3. Use Itanium C++ ABI name mangling (already partially supported)
 *   4. Update dynamic_cast runtime to work with type_info pointers
 *   5. Emit typeinfo symbols with _ZTI prefix instead of ??_R
 */

/**
 * @section Symbol Naming Summary
 * 
 * Windows RTTI Symbols (MSVC):
 *   ??_R0?AVClassName@@8           - Type Descriptor (??_R0)
 *   ??_R1...                        - Base Class Descriptor (??_R1)
 *   ??_R2...                        - Base Class Array (??_R2)
 *   ??_R3...                        - Class Hierarchy Descriptor (??_R3)
 *   ??_R4ClassName@@6B@             - Complete Object Locator (??_R4)
 * 
 * Linux RTTI Symbols (Itanium C++ ABI):
 *   _ZTI3Foo                        - typeinfo for Foo
 *   _ZTIN3std8bad_castE             - typeinfo for std::bad_cast
 *   _ZTV3Foo                        - vtable for Foo
 *   _ZTS3Foo                        - typeinfo name for Foo
 * 
 * Exception Handling Symbols:
 *   Windows: _CxxThrowException
 *   Linux:   __cxa_throw, __cxa_allocate_exception, __cxa_begin_catch, 
 *            __cxa_end_catch, __cxa_rethrow
 */

/**
 * @section References
 * 
 * Windows (MSVC) ABI:
 *   - No official public documentation
 *   - Reverse-engineered from MSVC implementation
 *   - See: Raymond Chen's "The Old New Thing" blog
 * 
 * Itanium C++ ABI (Linux, macOS, most Unix):
 *   - Official specification: https://itanium-cxx-abi.github.io/cxx-abi/abi.html
 *   - Exception handling: https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
 *   - RTTI: Chapter 2.9 of the Itanium C++ ABI specification
 * 
 * Current FlashCpp Implementation:
 *   - Exception Handling: src/IRConverter.h (handleThrow, handleRethrow)
 *   - RTTI Structures: src/AstNodeTypes.h (MSVCTypeDescriptor, etc.)
 *   - RTTI Building: src/AstNodeTypes.cpp (build_rtti_info)
 *   - dynamic_cast: src/IRConverter.h (handleDynamicCast, emit_dynamic_cast_runtime_helpers)
 */
