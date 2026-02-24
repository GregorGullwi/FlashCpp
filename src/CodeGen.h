#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "TemplateRegistry.h"
#include "ChunkedString.h"
#include "NameMangling.h"
#include "ConstExprEvaluator.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"
#include "LazyMemberResolver.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_map>
#include <queue>
#include <unordered_set>
#include <assert.h>
#include <stdexcept>
#include <cstdint>
#include <typeinfo>
#include <limits>
#include <charconv>
#include <string_view>
#include "IRConverter.h"
#include "Log.h"

class Parser;

// MSVC RTTI runtime structures (must match ObjFileWriter.h MSVC format):
// These are the actual structures that exist at runtime in the object file

// ??_R0 - Type Descriptor (runtime view)
struct RTTITypeDescriptor {
	const void* vtable;              // Pointer to type_info vtable (usually null)
	const void* spare;               // Reserved/spare pointer (unused)
	char name[1];                    // Variable-length mangled name (null-terminated)
};

// ??_R1 - Base Class Descriptor (runtime view)
struct RTTIBaseClassDescriptor {
	const RTTITypeDescriptor* type_descriptor;  // Pointer to base class type descriptor
	uint32_t num_contained_bases;    // Number of nested base classes
	int32_t mdisp;                   // Member displacement (offset in class)
	int32_t pdisp;                   // Vbtable displacement (-1 if not virtual base)
	int32_t vdisp;                   // Displacement inside vbtable (0 if not virtual base)
	uint32_t attributes;             // Flags (virtual, ambiguous, etc.)
};

// ??_R2 - Base Class Array (runtime view)
struct RTTIBaseClassArray {
	const RTTIBaseClassDescriptor* base_class_descriptors[1]; // Variable-length array
};

// ??_R3 - Class Hierarchy Descriptor (runtime view)
struct RTTIClassHierarchyDescriptor {
	uint32_t signature;              // Always 0
	uint32_t attributes;             // Bit flags (multiple inheritance, virtual inheritance, etc.)
	uint32_t num_base_classes;       // Number of base classes (including self)
	const RTTIBaseClassArray* base_class_array;  // Pointer to base class array
};

// ??_R4 - Complete Object Locator (runtime view)
struct RTTICompleteObjectLocator {
	uint32_t signature;              // 0 for 32-bit, 1 for 64-bit
	uint32_t offset;                 // Offset of this vtable in the complete class
	uint32_t cd_offset;              // Constructor displacement offset
	const RTTITypeDescriptor* type_descriptor;        // Pointer to type descriptor
	const RTTIClassHierarchyDescriptor* hierarchy;    // Pointer to class hierarchy
};

// Legacy RTTIInfo for backward compatibility with old simple format
struct RTTIInfo {
	uint64_t class_name_hash;
	uint64_t num_bases;
	RTTIInfo* base_ptrs[0];  // Flexible array member - base class RTTI pointers follow
	// Access via: auto** bases = reinterpret_cast<RTTIInfo**>((char*)this + 16);
};

// Note: Runtime helpers __dynamic_cast_check() and __dynamic_cast_throw_bad_cast()
// are now auto-generated as native x64 functions by the compiler when dynamic_cast is used.
// See IRConverter.h: emit_dynamic_cast_check_function() and emit_dynamic_cast_throw_function()

// Structure to hold lambda information for deferred generation
struct LambdaInfo {
	std::string_view closure_type_name;      // e.g., "__lambda_0" (persistent via StringBuilder)
	std::string_view operator_call_name;     // e.g., "__lambda_0_operator_call" (persistent via StringBuilder)
	std::string_view invoke_name;            // e.g., "__lambda_0_invoke" (persistent via StringBuilder)
	std::string_view conversion_op_name;     // e.g., "__lambda_0_conversion" (persistent via StringBuilder)
	Type return_type;
	int return_size;
	TypeIndex return_type_index = 0;    // Type index for struct/enum return types
	bool returns_reference = false;     // True if lambda returns a reference type (T& or T&&)
	std::vector<std::tuple<Type, int, int, std::string>> parameters;  // type, size, pointer_depth, name
	std::vector<ASTNode> parameter_nodes;  // Actual parameter AST nodes for symbol table
	ASTNode lambda_body;                // Copy of the lambda body
	std::vector<LambdaCaptureNode> captures;  // Copy of captures
	std::vector<ASTNode> captured_var_decls;  // Declarations of captured variables (for symbol table)
	size_t lambda_id;
	Token lambda_token;
	std::string_view enclosing_struct_name;  // Name of enclosing struct if lambda is in a member function
	TypeIndex enclosing_struct_type_index = 0;  // Type index of enclosing struct for [this] capture
	bool is_mutable = false;            // Whether the lambda is mutable (allows modifying captures)
	
	// Generic lambda support (lambdas with auto parameters)
	bool is_generic = false;                     // True if lambda has any auto parameters
	std::vector<size_t> auto_param_indices;      // Indices of parameters with auto type
	// Deduced types from call site - store full TypeSpecifierNode to preserve struct type_index and reference flags
	mutable std::vector<std::pair<size_t, TypeSpecifierNode>> deduced_auto_types;
	
	// Get deduced type for a parameter at given index, returns nullopt if not deduced
	std::optional<TypeSpecifierNode> getDeducedType(size_t param_index) const {
		for (const auto& [idx, type_node] : deduced_auto_types) {
			if (idx == param_index) {
				return type_node;
			}
		}
		return std::nullopt;
	}
	
	// Set deduced type for a parameter at given index
	void setDeducedType(size_t param_index, const TypeSpecifierNode& type_node) const {
		// Check if already set
		for (auto& [idx, stored_type] : deduced_auto_types) {
			if (idx == param_index) {
				stored_type = type_node;
				return;
			}
		}
		deduced_auto_types.push_back({param_index, type_node});
	}
};

// Expression evaluation context
// Determines how an expression should be evaluated
enum class ExpressionContext {
	Load,           // Evaluate and load the value (default, rvalue context)
	LValueAddress   // Evaluate to get the address without loading (lvalue context for assignment)
};

// Named constants for ABI-specific values used throughout code generation
static constexpr int POINTER_SIZE_BITS = 64;
static constexpr int SYSV_STRUCT_RETURN_THRESHOLD = 128;  // Linux/SysV ABI: structs > 16 bytes returned via hidden pointer
static constexpr int WIN64_STRUCT_RETURN_THRESHOLD = 64;  // Windows x64: structs > 8 bytes returned via hidden pointer

/// Get the struct return threshold for the current platform (bits)
inline int getStructReturnThreshold(bool is_llp64) {
	return is_llp64 ? WIN64_STRUCT_RETURN_THRESHOLD : SYSV_STRUCT_RETURN_THRESHOLD;
}


// The AstToIr class is split across multiple files for maintainability.
// All parts are included here to form the complete class definition.
// This works because FlashCppUnity.h includes everything as one translation unit.

#include "CodeGen_Visitors.cpp"      // Class declaration, setup, declaration/namespace visitors
#include "CodeGen_Statements.cpp"    // Statement visitors (if/for/while/switch/try), variable declarations
#include "CodeGen_Expressions.cpp"   // Expression evaluation, identifiers, operators, type conversions
#include "CodeGen_Functions.cpp"     // Function calls, member access, arrays, sizeof, new/delete, casts
#include "CodeGen_Lambdas.cpp"       // Lambda IR generation, template instantiation, data members
