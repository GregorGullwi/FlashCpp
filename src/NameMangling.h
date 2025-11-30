#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "ChunkedString.h"

// Shared MSVC name mangling utilities used by both CodeGen and ObjectFileWriter

namespace NameMangling {

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
		: storage_(committed_sv) {}
	
	// Always returns a string_view (zero-copy)
	std::string_view view() const { return storage_; }
	
	// Implicit conversion to string_view for convenience
	operator std::string_view() const { return storage_; }
	
	// Check if empty
	bool empty() const { return storage_.empty(); }
	
	// Comparison operators
	bool operator==(const MangledName& other) const { return storage_ == other.storage_; }
	bool operator==(std::string_view other) const { return storage_ == other; }
	bool operator<(const MangledName& other) const { return storage_ < other.storage_; }
	
private:
	std::string_view storage_;
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
				output += type_info.name_;
				output += "@@";
			} else {
				output += 'H';  // Fallback to int if type not found
			}
			break;
		}
		default: output += 'H'; break;  // Default to int for unknown types
	}
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
	const std::vector<std::string_view>& namespace_path = {}
) {
	StringBuilder builder;
	
	// Special case: main function is never mangled
	if (func_name == "main") {
		builder.append("main");
		return MangledName(builder.commit());
	}

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
		std::vector<std::string_view> class_parts;
		std::string_view remaining = struct_name;
		size_t class_pos;
		while ((class_pos = remaining.find("::")) != std::string_view::npos) {
			class_parts.push_back(remaining.substr(0, class_pos));
			remaining = remaining.substr(class_pos + 2);
		}
		class_parts.push_back(remaining);  // Add last part
		
		// Append in reverse order with @ separators
		for (auto it = class_parts.rbegin(); it != class_parts.rend(); ++it) {
			builder.append(*it);
			if (std::next(it) != class_parts.rend()) {
				builder.append('@');
			}
		}
		
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
	
	// Extract parameter types
	std::vector<TypeSpecifierNode> param_types;
	param_types.reserve(func_node.parameter_nodes().size());
	for (const auto& param : func_node.parameter_nodes()) {
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		param_types.push_back(param_decl.type_node().as<TypeSpecifierNode>());
	}
	
	// Get struct name for member functions
	std::string_view struct_name = func_node.is_member_function() ? func_node.parent_struct_name() : "";
	
	return generateMangledName(func_name, return_type, param_types, 
	                           func_node.is_variadic(), struct_name, namespace_path);
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

// Generate mangled name for a destructor
// MSVC mangles destructors as ??1ClassName@Namespace@@... where 1 is the destructor marker
inline MangledName generateMangledNameForDestructor(
	std::string_view struct_name,
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
	// Extract parameter types
	std::vector<TypeSpecifierNode> param_types;
	param_types.reserve(ctor_node.parameter_nodes().size());
	for (const auto& param : ctor_node.parameter_nodes()) {
		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		param_types.push_back(param_decl.type_node().as<TypeSpecifierNode>());
	}
	
	return generateMangledNameForConstructor(ctor_node.struct_name(), param_types, namespace_path);
}

// Generate mangled name from a DestructorDeclarationNode
inline MangledName generateMangledNameFromNode(
	const DestructorDeclarationNode& dtor_node,
	const std::vector<std::string_view>& namespace_path = {}
) {
	return generateMangledNameForDestructor(dtor_node.struct_name(), namespace_path);
}

} // namespace NameMangling
