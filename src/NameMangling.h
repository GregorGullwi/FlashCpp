#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "ChunkedString.h"

// Shared name mangling utilities used by both CodeGen and ObjectFileWriter

namespace NameMangling {

// Mangling style enum for cross-platform support
enum class ManglingStyle {
	MSVC,      // Microsoft Visual C++ name mangling (Windows default)
	Itanium    // Itanium C++ ABI name mangling (Linux/Unix default)
};

// Global mangling style - managed via CompileContext::setManglingStyle()
// Default is platform-dependent but changeable for cross-compilation
inline ManglingStyle g_mangling_style = 
#if defined(_WIN32) || defined(_WIN64)
	ManglingStyle::MSVC;
#else
	ManglingStyle::Itanium;
#endif

// A mangled name stored as a string_view pointing to stable storage
// (typically from StringBuilder::commit() which uses ChunkedStringAllocator)
//
// IMPORTANT: When constructing from string_view, the caller must ensure the underlying
// storage remains valid for the lifetime of the MangledName. Typically this means using
// StringBuilder::commit() which stores strings in the global ChunkedStringAllocator.
class MangledName {
public:
	// Default: empty name
	MangledName() : storage_{} {}
	
	// Construct from committed string_view (preferred - zero allocation)
	// Note: The caller must ensure the string_view's storage outlives this MangledName
	explicit MangledName(std::string_view committed_sv)
		: storage_(StringTable::getOrInternStringHandle(committed_sv)) {}
	explicit MangledName(StringHandle committed_sv) 
		: storage_(committed_sv) {}
	
	// Always returns a string_view (zero-copy)
	std::string_view view() const { return StringTable::getStringView(storage_); }
	
	// Implicit conversion to string_view for convenience
	operator std::string_view() const { return StringTable::getStringView(storage_); }
	operator StringHandle() const { return storage_; }
	
	// Check if empty
	bool empty() const { return !storage_.isValid(); }
	
	// Comparison operators
	bool operator==(const MangledName& other) const { return storage_ == other.storage_; }
	bool operator==(std::string_view other) const { return storage_ == other; }
	bool operator<(const MangledName& other) const { return storage_ < other.storage_; }
	
private:
	StringHandle storage_;
};

// Helper to append CV-qualifier code (A/B/C/D) to output
inline void appendCVQualifier(auto& output, CVQualifier cv) {
	if (cv == CVQualifier::None) {
		output += 'A';
	} else if (cv == CVQualifier::Const) {
		output += 'B';
	} else if (cv == CVQualifier::Volatile) {
		output += 'C';
	} else if (cv == CVQualifier::ConstVolatile) {
		output += 'D';
	}
}

// Generate MSVC type code for mangling
// Works with both std::string and StringBuilder
template<typename OutputType>
void appendTypeCode(OutputType& output, const TypeSpecifierNode& type_node) {
	// Handle references - MSVC uses different prefixes for lvalue vs rvalue references
	// Format: [AE|$$QE][A|B|C|D] where A/B/C/D are CV-qualifiers on the REFERENCED type
	if (type_node.is_lvalue_reference()) {
		output += "AE";
		appendCVQualifier(output, type_node.cv_qualifier());
	} else if (type_node.is_rvalue_reference()) {
		output += "$$QE";
		appendCVQualifier(output, type_node.cv_qualifier());
	}

	// Add pointer prefix for each level of indirection with CV-qualifiers
	// MSVC format: [P|Q|R|S][E][A|B|C|D] where:
	//   P = pointer, Q = const pointer, R = volatile pointer, S = const volatile pointer
	//   E = 64-bit (always E for x64)
	//   A = no CV-quals on pointee, B = const pointee, C = volatile pointee, D = const volatile pointee
	const auto& ptr_levels = type_node.pointer_levels();
	for (size_t i = 0; i < ptr_levels.size(); ++i) {
		const auto& ptr_level = ptr_levels[i];
		
		// Pointer CV-qualifiers (on the pointer itself)
		if (ptr_level.cv_qualifier == CVQualifier::None) {
			output += "PE";
		} else if (ptr_level.cv_qualifier == CVQualifier::Const) {
			output += "QE";
		} else if (ptr_level.cv_qualifier == CVQualifier::Volatile) {
			output += "RE";
		} else if (ptr_level.cv_qualifier == CVQualifier::ConstVolatile) {
			output += "SE";
		}
		
		// Pointee CV-qualifiers (on what the pointer points to)
		// For the last pointer level, use the base type's CV-qualifier
		// For intermediate levels, get CV from the next pointer level
		CVQualifier pointee_cv = (i == ptr_levels.size() - 1) 
			? type_node.cv_qualifier() 
			: ptr_levels[i + 1].cv_qualifier;
			
		appendCVQualifier(output, pointee_cv);
	}

	// Add base type code
	switch (type_node.type()) {
		case Type::Void: output += 'X'; break;
		case Type::Bool: output += "_N"; break;  // bool
		case Type::Char: output += 'D'; break;   // char
		case Type::UnsignedChar: output += 'E'; break;  // unsigned char
		case Type::Short: output += 'F'; break;  // short
		case Type::UnsignedShort: output += 'G'; break;  // unsigned short
		case Type::Int: output += 'H'; break;    // int
		case Type::UnsignedInt: output += 'I'; break;  // unsigned int
		case Type::Long: output += 'J'; break;   // long
		case Type::UnsignedLong: output += 'K'; break;  // unsigned long
		case Type::LongLong: output += "_J"; break;  // long long
		case Type::UnsignedLongLong: output += "_K"; break;  // unsigned long long
		case Type::Float: output += 'M'; break;  // float
		case Type::Double: output += 'N'; break;  // double
		case Type::LongDouble: output += 'O'; break;  // long double
		case Type::Struct:
		case Type::UserDefined: {
			// Struct/class types use format: V<name>@@ or U<name>@@ (V for class, U for struct, but we use V)
			// Get the type name from the global type registry
			if (type_node.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
				output += 'V';
				output += StringTable::getStringView(type_info.name());
				output += "@@";
			} else {
				output += 'H';  // Fallback to int if type not found
			}
			break;
		}
		default: output += 'H'; break;  // Default to int for unknown types
	}
}

// ============================================================================
// Itanium C++ ABI Name Mangling Helpers
// ============================================================================

// Append Itanium-style type encoding for basic types
// Reference: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-type
template<typename OutputType>
inline void appendItaniumTypeCode(OutputType& output, const TypeSpecifierNode& type_node, bool is_function_parameter = false) {
	// Handle pointers first (they modify what comes after)
	for (size_t i = 0; i < type_node.pointer_levels().size(); ++i) {
		output += 'P';
		const auto& ptr_level = type_node.pointer_levels()[i];
		// CV-qualifiers on the pointer itself
		// NOTE: For function parameters, top-level const on the pointer (i == 0) is ignored
		// per C++ standard [dcl.fct]p5. Examples:
		//   int* const p → int* p (top-level const ignored)
		//   const int* const p → const int* p (top-level const ignored, inner const preserved)
		bool skip_pointer_cv = is_function_parameter && (i == 0);
		if (!skip_pointer_cv) {
			if (ptr_level.cv_qualifier == CVQualifier::Const) {
				output += 'K';
			} else if (ptr_level.cv_qualifier == CVQualifier::Volatile) {
				output += 'V';
			} else if (ptr_level.cv_qualifier == CVQualifier::ConstVolatile) {
				output += 'K';  // const first
				output += 'V';  // then volatile
			}
		}
	}
	
	// Handle references (also modify what comes after)
	if (type_node.is_lvalue_reference()) {
		output += 'R';
	} else if (type_node.is_rvalue_reference()) {
		output += 'O';  // rvalue reference
	}
	
	// CV-qualifiers on the base type (come after pointer/reference)
	// According to Itanium ABI: K = const, V = volatile
	// NOTE: For function parameters passed by value (not pointer/reference),
	// top-level const is ignored per C++ standard [dcl.fct]p5
	bool is_by_value = type_node.pointer_levels().empty() && 
	                   !type_node.is_lvalue_reference() && 
	                   !type_node.is_rvalue_reference();
	bool skip_cv = is_function_parameter && is_by_value;
	
	if (!skip_cv) {
		if (type_node.cv_qualifier() == CVQualifier::Const) {
			output += 'K';
		} else if (type_node.cv_qualifier() == CVQualifier::Volatile) {
			output += 'V';
		} else if (type_node.cv_qualifier() == CVQualifier::ConstVolatile) {
			output += 'K';  // const first
			output += 'V';  // then volatile
		}
	}
	
	// Basic type codes (Itanium ABI section 5.1.5)
	switch (type_node.type()) {
		case Type::Void:       output += 'v'; break;
		case Type::Bool:       output += 'b'; break;
		case Type::Char:
			// Char can be signed or unsigned depending on qualifier
			if (type_node.qualifier() == TypeQualifier::Unsigned) {
				output += 'h';  // unsigned char
			} else if (type_node.qualifier() == TypeQualifier::Signed) {
				output += 'a';  // signed char
			} else {
				output += 'c';  // plain char (implementation-defined signedness)
			}
			break;
		case Type::UnsignedChar: output += 'h'; break;
		case Type::Short:      output += 's'; break;
		case Type::UnsignedShort: output += 't'; break;
		case Type::Int:        output += 'i'; break;
		case Type::UnsignedInt: output += 'j'; break;
		case Type::Long:       output += 'l'; break;
		case Type::UnsignedLong: output += 'm'; break;
		case Type::LongLong:   output += 'x'; break;
		case Type::UnsignedLongLong: output += 'y'; break;
		case Type::Float:      output += 'f'; break;
		case Type::Double:     output += 'd'; break;
		case Type::LongDouble: output += 'e'; break;
		case Type::Struct:
		case Type::UserDefined: {
			// For structs/classes, use the struct name
			// Format: <length><name>
			if (type_node.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
				auto struct_name = StringTable::getStringView(type_info.name());
				output += std::to_string(struct_name.size());
				output += struct_name;
			} else {
				// Unknown struct type, use 'v' for void as fallback
				output += 'v';
			}
			break;
		}
		default:
			// Unknown type, use 'i' for int as fallback
			output += 'i';
			break;
	}
}

// Generate Itanium C++ ABI mangled name
// Format: _Z<function-name><parameter-types>
// For namespaced functions: _ZN<namespace-parts><function-name>E<parameter-types>
// For member functions: _ZN<class-name><function-name>E<parameter-types>
template<typename OutputType>
inline void generateItaniumMangledName(
	OutputType& output,
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<TypeSpecifierNode>& param_types,
	bool is_variadic,
	std::string_view struct_name,
	const std::vector<std::string_view>& namespace_path
) {
	// Start with _Z prefix
	output += "_Z";
	
	// Check if we have namespaces or struct (needs nested-name encoding)
	bool has_nested_name = !struct_name.empty() || !namespace_path.empty();
	
	// Special case: std namespace alone uses "St" directly without nested-name encoding
	bool is_std_only = namespace_path.size() == 1 && namespace_path[0] == "std" && struct_name.empty();
	
	if (has_nested_name && !is_std_only) {
		// Use nested-name encoding: _ZN...E
		output += 'N';
		
		// Add namespace parts first (in order)
		for (const auto& ns : namespace_path) {
			// Anonymous namespaces are encoded as "_GLOBAL__N_1" per Itanium C++ ABI
			if (ns.empty()) {
				output += "12_GLOBAL__N_1";
			} else if (ns == "std") {
				// Special handling: std namespace uses "St" in Itanium C++ ABI
				output += "St";
			} else {
				output += std::to_string(ns.size());
				output += ns;
			}
		}
		
		// Add struct/class name if present
		if (!struct_name.empty()) {
			// For nested classes, struct_name may contain "::" separators
			// We need to encode each component separately
			// e.g., "Outer::Inner" -> "5Outer5Inner"
			size_t start = 0;
			while (start < struct_name.size()) {
				size_t end = struct_name.find("::", start);
				if (end == std::string_view::npos) {
					end = struct_name.size();
				}
				
				std::string_view component = struct_name.substr(start, end - start);
				if (!component.empty()) {
					output += std::to_string(component.size());
					output += component;
				}
				
				start = (end == struct_name.size()) ? end : end + 2;  // Skip "::"
			}
		}
		
		// Add function name
		// Check for special constructors and destructors
		if (func_name.size() > 0 && func_name[0] == '~') {
			// Destructor: use D1 for complete destructor per Itanium C++ ABI
			// D0 = deleting, D1 = complete, D2 = base
			output += "D1";
		} else if (!struct_name.empty()) {
			// For nested classes, struct_name might be "Outer::Inner"
			// Extract the last component to compare with func_name
			std::string_view class_name = struct_name;
			auto last_colon = struct_name.rfind("::");
			if (last_colon != std::string_view::npos) {
				class_name = struct_name.substr(last_colon + 2);
			}
			
			if (func_name == class_name) {
				// Constructor: use C1 for complete constructor per Itanium C++ ABI
				// C1 = complete, C2 = base, C3 = allocating
				output += "C1";
			} else {
				// Regular function: <length><name>
				output += std::to_string(func_name.size());
				output += func_name;
			}
		} else {
			// Regular function: <length><name>
			output += std::to_string(func_name.size());
			output += func_name;
		}
		
		// End nested-name
		output += 'E';
	} else if (is_std_only) {
		// Special case: std namespace uses "St" prefix without nested-name encoding
		output += "St";
		output += std::to_string(func_name.size());
		output += func_name;
	} else {
		// Simple function name: <length><name>
		output += std::to_string(func_name.size());
		output += func_name;
	}
	
	// Add parameter types
	if (param_types.empty() && !is_variadic) {
		// No parameters - use 'v' for void parameter list
		output += 'v';
	} else {
		for (const auto& param : param_types) {
			appendItaniumTypeCode(output, param, true);  // true = is function parameter
		}
		
		// Handle variadic parameters
		if (is_variadic) {
			output += 'z';  // ellipsis
		}
	}
}

// Overload for param_nodes (extracts types from DeclarationNodes)
template<typename OutputType>
inline void generateItaniumMangledName(
	OutputType& output,
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<ASTNode>& param_nodes,
	bool is_variadic,
	std::string_view struct_name,
	const std::vector<std::string_view>& namespace_path
) {
	// Extract parameter types from param_nodes
	std::vector<TypeSpecifierNode> param_types;
	param_types.reserve(param_nodes.size());
	for (const auto& param : param_nodes) {
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		param_types.push_back(param_decl.type_node().as<TypeSpecifierNode>());
	}
	
	// Use the main implementation
	generateItaniumMangledName(output, func_name, return_type, param_types, 
	                           is_variadic, struct_name, namespace_path);
}

// Generate MSVC mangled name for a function
// Uses StringBuilder for efficient string construction and returns a committed string_view
// Parameters:
//   func_name: The unmangled function name
//   return_type: Return type for the function
//   param_types: Vector of parameter types
//   is_variadic: True if function has ... ellipsis parameter
//   struct_name: Class/struct name for member functions (empty for free functions)
//   namespace_path: Namespace components for namespace-scoped functions
// Returns: MangledName containing the committed string_view
inline MangledName generateMangledName(
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<TypeSpecifierNode>& param_types,
	bool is_variadic = false,
	std::string_view struct_name = "",
	const std::vector<std::string_view>& namespace_path = {},
	Linkage linkage = Linkage::CPlusPlus
) {
	StringBuilder builder;
	
	// Special case: main function is never mangled
	if (func_name == "main") {
		builder.append("main");
		return MangledName(builder.commit());
	}

	// extern "C" linkage: no mangling, use the function name as-is
	if (linkage == Linkage::C) {
		builder.append(func_name);
		return MangledName(builder.commit());
	}

	// Check mangling style and use appropriate mangler
	if (g_mangling_style == ManglingStyle::Itanium) {
		// Use Itanium C++ ABI name mangling
		generateItaniumMangledName(builder, func_name, return_type, param_types, 
		                           is_variadic, struct_name, namespace_path);
		return MangledName(builder.commit());
	}

	// MSVC-style mangling
	builder.append('?');
	
	// Handle member functions vs free functions
	if (!struct_name.empty()) {
		// Member function: ?name@ClassName@@QA...
		// Extract just the function name (after ::)
		std::string_view func_only_name = func_name;
		size_t pos = func_name.rfind("::");
		if (pos != std::string_view::npos) {
			func_only_name = func_name.substr(pos + 2);
		}
		builder.append(func_only_name);
		builder.append('@');
		
		// For nested classes, reverse the order: "Outer::Inner" -> "Inner@Outer"
		// Use rfind to iterate backwards without any allocation
		std::string_view remaining = struct_name;
		size_t sep_pos;
		while ((sep_pos = remaining.rfind("::")) != std::string_view::npos) {
			builder.append(remaining.substr(sep_pos + 2));
			builder.append('@');
			remaining = remaining.substr(0, sep_pos);
		}
		builder.append(remaining);  // Append the first (outermost) part
		
		builder.append("@@QA");  // @@ + calling convention for member functions (Q = __thiscall-like)
	} else if (!namespace_path.empty()) {
		// Namespace-scoped free function: ?name@Namespace@@YA...
		builder.append(func_name);
		builder.append('@');
		
		// Append namespace parts in reverse order with @ separators
		for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
			builder.append(*it);
			if (std::next(it) != namespace_path.rend()) {
				builder.append('@');
			}
		}
		
		builder.append("@@YA");  // @@ + calling convention (__cdecl)
	} else {
		// Free function in global namespace: ?name@@YA...
		builder.append(func_name);
		builder.append("@@YA");  // @@ + calling convention (__cdecl)
	}

	// Add return type code
	appendTypeCode(builder, return_type);

	// Add parameter type codes
	for (const auto& param_type : param_types) {
		appendTypeCode(builder, param_type);
	}

	// End marker - different for variadic vs non-variadic
	if (is_variadic) {
		builder.append("ZZ");  // Variadic functions end with 'ZZ' in MSVC mangling
	} else {
		builder.append("@Z");  // Non-variadic functions end with '@Z'
	}

	return MangledName(builder.commit());
}

// Overload that accepts parameter nodes directly to avoid creating a temporary vector
inline MangledName generateMangledName(
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<ASTNode>& param_nodes,
	bool is_variadic = false,
	std::string_view struct_name = "",
	const std::vector<std::string_view>& namespace_path = {},
	Linkage linkage = Linkage::CPlusPlus
) {
	StringBuilder builder;
	
	// Special case: main function is never mangled
	if (func_name == "main") {
		builder.append("main");
		return MangledName(builder.commit());
	}

	// extern "C" linkage: no mangling, use the function name as-is
	if (linkage == Linkage::C) {
		builder.append(func_name);
		return MangledName(builder.commit());
	}

	// Check mangling style and use appropriate mangler
	if (g_mangling_style == ManglingStyle::Itanium) {
		// Use Itanium C++ ABI name mangling
		generateItaniumMangledName(builder, func_name, return_type, param_nodes, 
		                           is_variadic, struct_name, namespace_path);
		return MangledName(builder.commit());
	}

	// MSVC-style mangling
	builder.append('?');
	
	// Handle member functions vs free functions
	if (!struct_name.empty()) {
		std::string_view func_only_name = func_name;
		size_t pos = func_name.rfind("::");
		if (pos != std::string_view::npos) {
			func_only_name = func_name.substr(pos + 2);
		}
		builder.append(func_only_name);
		builder.append('@');
		
		// For nested classes, reverse the order using rfind
		std::string_view remaining = struct_name;
		size_t sep_pos;
		while ((sep_pos = remaining.rfind("::")) != std::string_view::npos) {
			builder.append(remaining.substr(sep_pos + 2));
			builder.append('@');
			remaining = remaining.substr(0, sep_pos);
		}
		builder.append(remaining);
		
		builder.append("@@QA");
	} else if (!namespace_path.empty()) {
		builder.append(func_name);
		builder.append('@');
		for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
			builder.append(*it);
			if (std::next(it) != namespace_path.rend()) {
				builder.append('@');
			}
		}
		builder.append("@@YA");
	} else {
		builder.append(func_name);
		builder.append("@@YA");
	}

	// Add return type code
	appendTypeCode(builder, return_type);

	// Add parameter type codes directly from param nodes
	for (const auto& param : param_nodes) {
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		appendTypeCode(builder, param_decl.type_node().as<TypeSpecifierNode>());
	}

	// End marker
	if (is_variadic) {
		builder.append("ZZ");
	} else {
		builder.append("@Z");
	}

	return MangledName(builder.commit());
}

// Overload accepting std::vector<std::string> for namespace path (for CodeGen compatibility)
inline MangledName generateMangledName(
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<TypeSpecifierNode>& param_types,
	bool is_variadic,
	std::string_view struct_name,
	const std::vector<std::string>& namespace_path,
	Linkage linkage = Linkage::CPlusPlus
) {
	std::vector<std::string_view> ns_views;
	ns_views.reserve(namespace_path.size());
	for (const auto& ns : namespace_path) {
		ns_views.push_back(ns);
	}
	return generateMangledName(func_name, return_type, param_types, is_variadic, struct_name, ns_views, linkage);
}

// Overload accepting std::vector<std::string> for namespace path (for CodeGen compatibility)
inline MangledName generateMangledName(
	std::string_view func_name,
	const TypeSpecifierNode& return_type,
	const std::vector<ASTNode>& param_nodes,
	bool is_variadic,
	std::string_view struct_name,
	const std::vector<std::string>& namespace_path,
	Linkage linkage = Linkage::CPlusPlus
) {
	std::vector<std::string_view> ns_views;
	ns_views.reserve(namespace_path.size());
	for (const auto& ns : namespace_path) {
		ns_views.push_back(ns);
	}
	return generateMangledName(func_name, return_type, param_nodes, is_variadic, struct_name, ns_views, linkage);
}
// Generate mangled name from a FunctionDeclarationNode
// This is the main entry point for generating mangled names during parsing
// The function extracts all necessary information from the AST node
inline MangledName generateMangledNameFromNode(
	const FunctionDeclarationNode& func_node,
	const std::vector<std::string_view>& namespace_path = {}
) {
	const DeclarationNode& decl_node = func_node.decl_node();
	const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
	std::string_view func_name = decl_node.identifier_token().value();
	
	// Get struct name for member functions
	std::string_view struct_name = func_node.is_member_function() ? func_node.parent_struct_name() : "";
	
	// Use the overload that accepts parameter nodes directly
	// Pass linkage from the function node
	return generateMangledName(func_name, return_type, func_node.parameter_nodes(), 
	                           func_node.is_variadic(), struct_name, namespace_path, func_node.linkage());
}

// Overload accepting std::vector<std::string> for namespace path (for CodeGen compatibility)
inline MangledName generateMangledNameFromNode(
	const FunctionDeclarationNode& func_node,
	const std::vector<std::string>& namespace_path
) {
	// Convert to string_view vector and delegate
	std::vector<std::string_view> ns_views;
	ns_views.reserve(namespace_path.size());
	for (const auto& ns : namespace_path) {
		ns_views.push_back(ns);
	}
	return generateMangledNameFromNode(func_node, ns_views);
}

// Generate mangled name for a constructor
// MSVC mangles constructors as ??0ClassName@Namespace@@... where 0 is the constructor marker
inline MangledName generateMangledNameForConstructor(
	std::string_view struct_name,
	const std::vector<TypeSpecifierNode>& param_types,
	const std::vector<std::string_view>& namespace_path = {}
) {
	StringBuilder builder;
	
	builder.append("??0");  // Constructor marker in MSVC mangling
	builder.append(struct_name);
	
	// Add namespace path if present
	for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
		builder.append('@');
		builder.append(*it);
	}
	
	builder.append("@@QAE");  // @@ + __thiscall calling convention
	
	// Add parameter type codes
	for (const auto& param_type : param_types) {
		appendTypeCode(builder, param_type);
	}
	
	builder.append("@Z");  // End marker
	
	return MangledName(builder.commit());
}

// Overload that accepts parameter nodes directly to avoid creating a temporary vector
inline MangledName generateMangledNameForConstructor(
	std::string_view struct_name,
	const std::vector<ASTNode>& param_nodes,
	const std::vector<std::string_view>& namespace_path = {}
) {
	StringBuilder builder;
	
	builder.append("??0");  // Constructor marker in MSVC mangling
	builder.append(struct_name);
	
	// Add namespace path if present
	for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
		builder.append('@');
		builder.append(*it);
	}
	
	builder.append("@@QAE");  // @@ + __thiscall calling convention
	
	// Add parameter type codes directly from param nodes
	for (const auto& param : param_nodes) {
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		appendTypeCode(builder, param_decl.type_node().as<TypeSpecifierNode>());
	}
	
	builder.append("@Z");  // End marker
	
	return MangledName(builder.commit());
}

// Generate mangled name for a destructor
// MSVC mangles destructors as ??1ClassName@Namespace@@... where 1 is the destructor marker
inline MangledName generateMangledNameForDestructor(
	StringHandle struct_name,
	const std::vector<std::string_view>& namespace_path = {}
) {
	StringBuilder builder;
	
	builder.append("??1");  // Destructor marker in MSVC mangling
	builder.append(struct_name);
	
	// Add namespace path if present
	for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
		builder.append('@');
		builder.append(*it);
	}
	
	// @@ = scope terminator, QAE = __thiscall calling convention,
	// @X = void parameters (no params), Z = end marker
	builder.append("@@QAE@XZ");
	
	return MangledName(builder.commit());
}

// Generate mangled name from a ConstructorDeclarationNode
inline MangledName generateMangledNameFromNode(
	const ConstructorDeclarationNode& ctor_node,
	const std::vector<std::string_view>& namespace_path = {}
) {
	// Use the overload that accepts parameter nodes directly
	return generateMangledNameForConstructor(StringTable::getStringView(ctor_node.struct_name()), ctor_node.parameter_nodes(), namespace_path);
}

// Generate mangled name from a DestructorDeclarationNode
inline MangledName generateMangledNameFromNode(
	const DestructorDeclarationNode& dtor_node,
	const std::vector<std::string_view>& namespace_path = {}
) {
	return generateMangledNameForDestructor(dtor_node.struct_name(), namespace_path);
}

} // namespace NameMangling
