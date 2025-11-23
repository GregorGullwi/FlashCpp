// Runtime support for dynamic_cast
// This file provides runtime helper functions for RTTI-based dynamic_cast

#include <cstdint>
#include <cstddef>
#include <typeinfo>

// RTTI structure layout (must match ObjFileWriter.h):
//   - 8 bytes: class name hash
//   - 8 bytes: number of base classes
//   - 8*N bytes: pointers to base class RTTI structures (inline array)
struct RTTIInfo {
    uint64_t class_name_hash;
    uint64_t num_bases;
    // Base class RTTI pointers follow immediately after this structure
    // Access them via: RTTIInfo** base_ptrs = (RTTIInfo**)((char*)this + 16);
};

// Check if source_rtti is the same as or derived from target_rtti
// Returns true if the cast should succeed, false otherwise
extern "C" bool __dynamic_cast_check(RTTIInfo* source_rtti, RTTIInfo* target_rtti) {
    // Null check
    if (!source_rtti || !target_rtti) {
        return false;
    }
    
    // Exact match by pointer
    if (source_rtti == target_rtti) {
        return true;
    }
    
    // Also check by hash (in case RTTI structs are duplicated across translation units)
    if (source_rtti->class_name_hash == target_rtti->class_name_hash) {
        return true;
    }
    
    // Validate num_bases to prevent buffer overflow
    // Reasonable maximum: 64 base classes should be more than enough for any real class hierarchy
    constexpr uint64_t MAX_BASES = 64;
    if (source_rtti->num_bases > MAX_BASES) {
        return false;  // Corrupted RTTI or unreasonably deep hierarchy
    }
    
    // Check base classes recursively
    // Base class RTTI pointers are stored immediately after the RTTIInfo structure
    RTTIInfo** base_ptrs = reinterpret_cast<RTTIInfo**>(
        reinterpret_cast<char*>(source_rtti) + sizeof(RTTIInfo)
    );
    
    for (uint64_t i = 0; i < source_rtti->num_bases; ++i) {
        if (base_ptrs[i]) {
            if (__dynamic_cast_check(base_ptrs[i], target_rtti)) {
                return true;
            }
        }
    }
    
    return false;
}

// Throw bad_cast exception for failed reference casts
// This function never returns - it always throws
extern "C" [[noreturn]] void __dynamic_cast_throw_bad_cast() {
    throw std::bad_cast();
}
