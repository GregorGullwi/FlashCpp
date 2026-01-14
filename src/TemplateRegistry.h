#pragma once

#include "AstNodeTypes.h"
#include "ChunkedString.h"
#include "Lexer.h"  // For TokenPosition
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>

// SaveHandle type for parser save/restore operations
// Matches Parser::SaveHandle typedef in Parser.h
using SaveHandle = size_t;

// Transparent string hash for heterogeneous lookup (C++20)
// Allows unordered_map<string, T, TransparentStringHash, equal_to<>> to lookup with string_view
struct TransparentStringHash {
	using is_transparent = void;
	using hash_type = std::hash<std::string_view>;
	
	size_t operator()(const char* str) const { return hash_type{}(str); }
	size_t operator()(std::string_view str) const { return hash_type{}(str); }
	size_t operator()(const std::string& str) const { return hash_type{}(str); }
	size_t operator()(StringHandle sh) const { return std::hash<uint32_t>{}(sh.handle); }
};

// Member pointer classification for template arguments
enum class MemberPointerKind : uint8_t {
	None = 0,
	Object,
	Function
};

/**
 * Template Argument Type System
 * ==============================
 * 
 * This file defines three related but distinct types for representing template arguments:
 * 
 * 1. TemplateArgumentValue: Basic type+index+value triple for simple contexts
 *    - Lightweight representation with Type, TypeIndex, and value fields
 *    - Use when you need a simple container for type and value information
 *    - Distinct from TypedValue (IRTypes.h) which is for IR-level runtime values
 * 
 * 2. TemplateArgument: For function template deduction and instantiation tracking
 *    - Supports Type, Value, and Template template parameters (Kind enum)
 *    - Has both legacy (type_value) and modern (type_specifier) type representation
 *    - Includes TypeIndex for complex types (added in consolidation Task 2)
 *    - Has hash() and operator==() for use in containers (e.g., InstantiationQueue)
 *    - Use for: function template deduction, mangling, instantiation tracking
 * 
 * 3. TemplateTypeArg: Rich type representation for template instantiation
 *    - Complete qualifiers: const, volatile, reference, pointer, array
 *    - Supports dependent types, parameter packs, and member pointers
 *    - Most comprehensive - used by substitute_template_parameter()
 *    - Use for: pattern matching, specialization selection, template instantiation
 * 
 * Conversion Functions:
 *   - toTemplateTypeArg(TemplateArgument) -> TemplateTypeArg
 *   - toTemplateArgument(TemplateTypeArg) -> TemplateArgument
 *   These provide explicit, type-safe conversions preserving all type information
 * 
 * Design Rationale:
 *   - Keeping types separate maintains clarity of purpose
 *   - TemplateTypeArg's complexity not needed in all contexts
 *   - TemplateArgument's template template parameter support not needed in TemplateTypeArg
 *   - Conversion functions make interoperability straightforward
 * 
 * History:
 *   - Original: Duplicate TemplateArgument in TemplateRegistry.h and InstantiationQueue.h
 *   - Consolidation (Tasks 1-4): Unified into single TemplateArgument with TypeIndex support
 *   - See docs/TEMPLATE_ARGUMENT_CONSOLIDATION_PLAN.md for full details
 */

// Basic type+index+value triple for template arguments
// Provides a lightweight representation that can be reused across different contexts
// This is distinct from TypedValue (IRTypes.h) which is for IR-level runtime values
struct TemplateArgumentValue {
	Type type = Type::Invalid;
	TypeIndex type_index = 0;
	int64_t value = 0;
	
	// Factory methods
	static TemplateArgumentValue makeType(Type t, TypeIndex idx = 0) {
		TemplateArgumentValue v;
		v.type = t;
		v.type_index = idx;
		return v;
	}
	
	static TemplateArgumentValue makeValue(int64_t val, Type value_type = Type::Int) {
		TemplateArgumentValue v;
		v.type = value_type;
		v.value = val;
		return v;
	}
	
	bool operator==(const TemplateArgumentValue& other) const {
		return type == other.type && 
		       type_index == other.type_index && 
		       value == other.value;
	}
	
	size_t hash() const {
		size_t h = std::hash<int>{}(static_cast<int>(type));
		h ^= std::hash<TypeIndex>{}(type_index) << 1;
		h ^= std::hash<int64_t>{}(value) << 2;
		return h;
	}
};

// Full type representation for template arguments
// Captures base type, references, pointers, cv-qualifiers, etc.
// Can also represent non-type template parameters (values)
struct TemplateTypeArg {
	Type base_type;
	TypeIndex type_index;  // For user-defined types
	bool is_reference;
	bool is_rvalue_reference;
	size_t pointer_depth;  // 0 = not pointer, 1 = T*, 2 = T**, etc.
	CVQualifier cv_qualifier;  // const/volatile qualifiers
	bool is_array;
	std::optional<size_t> array_size;  // Known array size if available
	MemberPointerKind member_pointer_kind;

	// For non-type template parameters
	bool is_value;  // true if this represents a value instead of a type
	int64_t value;  // the value for non-type parameters

	// For variadic templates (parameter packs)
	bool is_pack;  // true if this represents a parameter pack (typename... Args)
	
	// For dependent types (types that depend on template parameters)
	bool is_dependent;  // true if this type depends on uninstantiated template parameters
	StringHandle dependent_name;  // name of the dependent template parameter or type name (set when is_dependent is true)
	
	TemplateTypeArg()
		: base_type(Type::Invalid)
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(false)
		, value(0)
		, is_pack(false)
		, is_dependent(false)
		, dependent_name() {}

	explicit TemplateTypeArg(const TypeSpecifierNode& type_spec)
		: base_type(type_spec.type())
		, type_index(type_spec.type_index())
		, is_reference(type_spec.is_reference())
		, is_rvalue_reference(type_spec.is_rvalue_reference())
		, pointer_depth(type_spec.pointer_depth())
		, cv_qualifier(type_spec.cv_qualifier())
		, is_array(type_spec.is_array())
		, array_size(type_spec.array_size())
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(false)
		, value(0)
		, is_pack(false)
		, is_dependent(false) {}

	// Constructor for non-type template parameters
	explicit TemplateTypeArg(int64_t val)
		: base_type(Type::Int)  // Default to int for values
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(true)
		, value(val)
		, is_pack(false)
		, is_dependent(false) {}
	
	// Constructor for non-type template parameters with explicit type
	TemplateTypeArg(int64_t val, Type type)
		: base_type(type)
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(true)
		, value(val)
		, is_pack(false)
		, is_dependent(false) {}
	
	bool operator==(const TemplateTypeArg& other) const {
		// Only compare type_index for user-defined types (Struct, Enum, UserDefined)
		// For primitive types like int, float, etc., the type_index should be ignored
		bool type_index_match = true;
		if (base_type == Type::Struct || base_type == Type::Enum || base_type == Type::UserDefined) {
			type_index_match = (type_index == other.type_index);
		}
		
		// NOTE: is_pack is intentionally NOT compared here.
		// The is_pack flag indicates whether this arg came from a pack expansion,
		// but for type matching purposes (specialization lookup, pattern matching),
		// is_pack should be ignored. For example, when looking up ns::sum<int>
		// from a pack expansion ns::sum<Args...> where Args=int, the lookup arg
		// has is_pack=true but should still match the specialization which has is_pack=false.
		
		return base_type == other.base_type &&
		       type_index_match &&
		       is_reference == other.is_reference &&
		       is_rvalue_reference == other.is_rvalue_reference &&
		       pointer_depth == other.pointer_depth &&
		       cv_qualifier == other.cv_qualifier &&
		       is_array == other.is_array &&
		       array_size == other.array_size &&
		       member_pointer_kind == other.member_pointer_kind &&
		       is_value == other.is_value &&
		       (!is_value || value == other.value);  // Only compare value if it's a value
	}

	// Helper method to check if this is a parameter pack
	bool isParameterPack() const {
		return is_pack;
	}
	
	// Get string representation for mangling
	std::string toString() const {
		if (is_value) {
			// For boolean values, use "true" or "false" instead of "1" or "0"
			// This is important for template specialization matching
			if (base_type == Type::Bool) {
				return value != 0 ? "true" : "false";
			}
			// For non-boolean values, return the numeric value as string
			return std::to_string(value);
		}

		std::string result;

		// Add const/volatile prefix if present
		if ((static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
			result += "C";  // const
		}
		if ((static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
			result += "V";  // volatile
		}

		// Add base type name
		switch (base_type) {
			case Type::Void: result += "void"; break;
			case Type::Int: result += "int"; break;
			case Type::Float: result += "float"; break;
			case Type::Double: result += "double"; break;
			case Type::Bool: result += "bool"; break;
			case Type::Char: result += "char"; break;
			case Type::Long: result += "long"; break;
			case Type::LongLong: result += "longlong"; break;
			case Type::Short: result += "short"; break;
			case Type::UnsignedInt: result += "uint"; break;
			case Type::UnsignedLong: result += "ulong"; break;
			case Type::UnsignedLongLong: result += "ulonglong"; break;
			case Type::UnsignedShort: result += "ushort"; break;
			case Type::UnsignedChar: result += "uchar"; break;
			case Type::UserDefined:
			case Type::Struct:
			case Type::Enum:
				// For user-defined types, look up the name from gTypeInfo
				if (type_index < gTypeInfo.size()) {
					result += StringTable::getStringView(gTypeInfo[type_index].name());
				} else {
					result += "unknown";
				}
				break;
			default: result += "unknown"; break;
		}

		// Add pointer markers
		for (size_t i = 0; i < pointer_depth; ++i) {
			result += "P";  // P for pointer
		}

		if (is_array) {
			result += "A";  // Array marker
			if (array_size.has_value()) {
				result += "[" + std::to_string(*array_size) + "]";
			} else {
				result += "[]";
			}
		}

		if (member_pointer_kind == MemberPointerKind::Object) {
			result += "MPO";
		} else if (member_pointer_kind == MemberPointerKind::Function) {
			result += "MPF";
		}

		// Add reference markers
		if (is_rvalue_reference) {
			result += "RR";  // rvalue reference
		} else if (is_reference) {
			result += "R";   // lvalue reference
		}

		return result;
	}
};

// Hash function for TemplateTypeArg
struct TemplateTypeArgHash {
	size_t operator()(const TemplateTypeArg& arg) const {
		size_t hash = std::hash<int>{}(static_cast<int>(arg.base_type));
		// Only include type_index in hash for user-defined types (to match operator==)
		if (arg.base_type == Type::Struct || arg.base_type == Type::Enum || arg.base_type == Type::UserDefined) {
			hash ^= std::hash<size_t>{}(arg.type_index) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<bool>{}(arg.is_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_rvalue_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<size_t>{}(arg.pointer_depth) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(arg.cv_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_array) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (arg.array_size.has_value()) {
			hash ^= std::hash<size_t>{}(*arg.array_size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(arg.member_pointer_kind)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(arg.is_value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (arg.is_value) {
			hash ^= std::hash<int64_t>{}(arg.value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		// NOTE: is_pack is intentionally NOT included in the hash to match operator==
		return hash;
	}
};

// Template instantiation key - uniquely identifies a template instantiation
struct TemplateInstantiationKey {
	std::string template_name;
	std::vector<Type> type_arguments;  // For type parameters
	std::vector<int64_t> value_arguments;  // For non-type parameters
	std::vector<std::string> template_arguments;  // For template template parameters
	
	bool operator==(const TemplateInstantiationKey& other) const {
		return template_name == other.template_name &&
		       type_arguments == other.type_arguments &&
		       value_arguments == other.value_arguments &&
		       template_arguments == other.template_arguments;
	}
};

// Hash function for TemplateInstantiationKey
struct TemplateInstantiationKeyHash {
	std::size_t operator()(const TemplateInstantiationKey& key) const {
		std::size_t hash = std::hash<std::string>{}(key.template_name);
		for (const auto& type : key.type_arguments) {
			hash ^= std::hash<int>{}(static_cast<int>(type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& value : key.value_arguments) {
			hash ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& tmpl : key.template_arguments) {
			hash ^= std::hash<std::string>{}(tmpl) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

// Template argument - can be a type, a value, or a template
struct TemplateArgument {
	enum class Kind {
		Type,
		Value,
		Template   // For template template parameters
	};
	
	Kind kind;
	Type type_value;  // For type arguments (legacy - enum only, kept for backwards compatibility)
	TypeIndex type_index = 0;  // For type arguments - index into gTypeInfo for complex types (NEW in Task 2)
	int64_t int_value;  // For non-type integer arguments
	Type value_type;  // For non-type arguments: the type of the value (bool, int, etc.)
	std::string template_name;  // For template template arguments (name of the template)
	std::optional<TypeSpecifierNode> type_specifier;  // Full type info including references, pointers, CV qualifiers
	
	static TemplateArgument makeType(Type t, TypeIndex idx = 0) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = t;
		arg.type_index = idx;  // Store TypeIndex for complex types
		return arg;
	}
	
	static TemplateArgument makeTypeSpecifier(const TypeSpecifierNode& type_spec) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type_value = type_spec.type();  // Keep legacy field populated
		arg.type_index = type_spec.type_index();  // Extract and store TypeIndex
		arg.type_specifier = type_spec;
		return arg;
	}
	
	static TemplateArgument makeValue(int64_t v, Type type = Type::Int) {
		TemplateArgument arg;
		arg.kind = Kind::Value;
		arg.int_value = v;
		arg.value_type = type;
		return arg;
	}
	
	static TemplateArgument makeTemplate(std::string_view template_name) {
		TemplateArgument arg;
		arg.kind = Kind::Template;
		arg.template_name = std::string(template_name);
		return arg;
	}
	
	// Hash for use in maps (needed for InstantiationQueue)
	size_t hash() const {
		size_t h = std::hash<int>{}(static_cast<int>(kind));
		h ^= std::hash<int>{}(static_cast<int>(type_value)) << 1;
		h ^= std::hash<TypeIndex>{}(type_index) << 2;
		h ^= std::hash<int64_t>{}(int_value) << 3;
		return h;
	}
	
	// Equality operator (needed for InstantiationQueue)
	bool operator==(const TemplateArgument& other) const {
		if (kind != other.kind) return false;
		switch (kind) {
			case Kind::Type:
				return type_value == other.type_value && type_index == other.type_index;
			case Kind::Value:
				return int_value == other.int_value && value_type == other.value_type;
			case Kind::Template:
				return template_name == other.template_name;
		}
		return false;
	}
};

/**
 * Conversion Helper Functions
 * ============================
 * 
 * These functions provide explicit, type-safe conversions between TemplateArgument
 * and TemplateTypeArg. They preserve as much type information as possible during
 * the conversion.
 * 
 * Usage Examples:
 *   // Convert TemplateArgument to TemplateTypeArg
 *   TemplateArgument arg = TemplateArgument::makeType(Type::Int, 0);
 *   TemplateTypeArg type_arg = toTemplateTypeArg(arg);
 * 
 *   // Convert TemplateTypeArg to TemplateArgument
 *   TemplateTypeArg type_arg;
 *   type_arg.base_type = Type::Float;
 *   TemplateArgument arg = toTemplateArgument(type_arg);
 */

/**
 * Convert TemplateArgument to TemplateTypeArg
 * 
 * Extracts type information from TemplateArgument and creates a TemplateTypeArg.
 * - If arg has type_specifier (modern path): Extracts full type info including
 *   references, pointers, cv-qualifiers, and arrays
 * - If arg lacks type_specifier (legacy path): Uses basic type_value and type_index
 * - For value arguments: Sets is_value=true and copies the value
 * - Template template parameters are not directly supported in TemplateTypeArg
 * 
 * @param arg The TemplateArgument to convert
 * @return TemplateTypeArg with extracted type information
 */
inline TemplateTypeArg toTemplateTypeArg(const TemplateArgument& arg) {
	TemplateTypeArg result;
	
	if (arg.kind == TemplateArgument::Kind::Type) {
		if (arg.type_specifier.has_value()) {
			// Modern path: use full type info from TypeSpecifierNode
			const auto& ts = *arg.type_specifier;
			result.base_type = ts.type();
			result.type_index = ts.type_index();
			result.is_reference = ts.is_reference();
			result.is_rvalue_reference = ts.is_rvalue_reference();
			result.pointer_depth = ts.pointer_levels().size();
			result.cv_qualifier = ts.cv_qualifier();
			result.is_array = ts.is_array();
			if (ts.is_array() && ts.array_size().has_value()) {
				result.array_size = ts.array_size();
			}
			// Note: member_pointer_kind not stored in TypeSpecifierNode, defaults to None
		} else {
			// Legacy path: use basic type info only
			result.base_type = arg.type_value;
			result.type_index = arg.type_index;
			// Other fields remain at default values
		}
	} else if (arg.kind == TemplateArgument::Kind::Value) {
		result.is_value = true;
		result.value = arg.int_value;
		result.base_type = arg.value_type;
	}
	// Template template parameters: not directly supported in TemplateTypeArg
	
	return result;
}

/**
 * Convert TemplateTypeArg to TemplateArgument
 * 
 * Creates a TemplateArgument with a TypeSpecifierNode containing complete type
 * information from the TemplateTypeArg.
 * - For value arguments: Creates TemplateArgument with makeValue()
 * - For type arguments: Creates TypeSpecifierNode with all qualifiers:
 *   - CV-qualifiers (const, volatile)
 *   - Pointer levels
 *   - Reference type (lvalue or rvalue)
 *   - Array dimensions
 * - Returns TemplateArgument with embedded TypeSpecifierNode (modern representation)
 * 
 * @param arg The TemplateTypeArg to convert
 * @return TemplateArgument with complete type information
 */
inline TemplateArgument toTemplateArgument(const TemplateTypeArg& arg) {
	if (arg.is_value) {
		// Non-type template parameter
		return TemplateArgument::makeValue(arg.value, arg.base_type);
	} else {
		// Type template parameter - create TypeSpecifierNode for full info
		TypeSpecifierNode ts(arg.base_type, TypeQualifier::None, 
		                    get_type_size_bits(arg.base_type), Token(), arg.cv_qualifier);
		ts.set_type_index(arg.type_index);
		
		// Add pointer levels
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			ts.add_pointer_level(CVQualifier::None);
		}
		
		// Set reference type
		// Check is_rvalue_reference FIRST because is_reference is true for BOTH lvalue and rvalue refs
		if (arg.is_rvalue_reference) {
			ts.set_reference(true);   // rvalue reference
		} else if (arg.is_reference) {
			ts.set_reference(false);  // lvalue reference
		}
		
		// Set array info if present
		if (arg.is_array) {
			ts.set_array(true, arg.array_size);
		}
		
		return TemplateArgument::makeTypeSpecifier(ts);
	}
}

// Out-of-line template member function definition
struct OutOfLineMemberFunction {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., <typename T>)
	ASTNode function_node;                  // FunctionDeclarationNode
	SaveHandle body_start;                  // Handle to saved position of function body for re-parsing
	std::vector<StringHandle> template_param_names;  // Names of template parameters
};

// Out-of-line template static member variable definition
struct OutOfLineMemberVariable {
	std::vector<ASTNode> template_params;       // Template parameters (e.g., <typename T>)
	StringHandle member_name;               // Name of the static member variable
	ASTNode type_node;                          // Type of the variable (TypeSpecifierNode)
	std::optional<ASTNode> initializer;         // Initializer expression
	std::vector<StringHandle> template_param_names;  // Names of template parameters
};

// SFINAE condition for void_t patterns
// Stores information about dependent member type checks like "typename T::type"
struct SfinaeCondition {
	size_t template_param_index;  // Which template parameter (e.g., 0 for T in has_type<T>)
	StringHandle member_name;     // The member type name to check (e.g., "type")
	
	SfinaeCondition() : template_param_index(0), member_name() {}
	SfinaeCondition(size_t idx, StringHandle name) : template_param_index(idx), member_name(name) {}
};

// Template specialization pattern - represents a pattern like T&, T*, const T, etc.
struct TemplatePattern {
	std::vector<ASTNode> template_params;  // Template parameters (e.g., typename T)
	std::vector<TemplateTypeArg> pattern_args;  // Pattern like T&, T*, etc.
	ASTNode specialized_node;  // The AST node for the specialized template
	std::optional<SfinaeCondition> sfinae_condition;  // Optional SFINAE check for void_t patterns
	
	// Check if this pattern matches the given concrete arguments
	// For example, pattern T& matches int&, float&, etc.
	// Returns true if match succeeds, and fills param_substitutions with T->int mapping
	bool matches(const std::vector<TemplateTypeArg>& concrete_args, 
	             std::unordered_map<std::string, TemplateTypeArg>& param_substitutions) const
	{
		FLASH_LOG(Templates, Trace, "      matches(): pattern has ", pattern_args.size(), " args, concrete has ", concrete_args.size(), " args");
		
		// Handle variadic templates: pattern may have fewer args if last template param is a pack
		// Check if the last template parameter is variadic (a pack)
		bool has_variadic_pack = false;
		size_t pack_param_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (template_params[i].is<TemplateParameterNode>()) {
				const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
				if (param.is_variadic()) {
					has_variadic_pack = true;
					pack_param_index = i;
					break;
				}
			}
		}
		
		// For non-variadic patterns, sizes must match exactly
		// For variadic patterns, concrete_args.size() >= pattern_args.size() - 1 
		// (pack can be empty, matching 0 or more args)
		if (!has_variadic_pack) {
			if (pattern_args.size() != concrete_args.size()) {
				FLASH_LOG(Templates, Trace, "      Size mismatch: pattern_args.size()=", pattern_args.size(), 
				          " != concrete_args.size()=", concrete_args.size());
				return false;
			}
		} else {
			// With variadic pack: need at least (pattern_args.size() - 1) concrete args
			// Pattern <First, Rest...> has 2 pattern_args, but can match 1+ concrete args
			// (Rest can be empty matching 0 args, or Rest can match 1+ args)
			if (concrete_args.size() < pattern_args.size() - 1) {
				return false;  // Not enough args for non-pack parameters
			}
		}
	
		param_substitutions.clear();
	
		// Check each pattern argument against the corresponding concrete argument
		// Track template parameter index separately from pattern argument index
		size_t param_index = 0;  // Tracks which template parameter we're binding
		for (size_t i = 0; i < pattern_args.size(); ++i) {
			const TemplateTypeArg& pattern_arg = pattern_args[i];
			
			// Handle variadic pack case: if i >= concrete_args.size(), 
			// this pattern arg corresponds to a pack that matches 0 args (empty pack)
			if (i >= concrete_args.size()) {
				// This should only happen for the variadic pack parameter
				// Check if this pattern position corresponds to a variadic pack
				if (param_index < template_params.size() && template_params[param_index].is<TemplateParameterNode>()) {
					const TemplateParameterNode& param = template_params[param_index].as<TemplateParameterNode>();
					if (param.is_variadic()) {
						// Empty pack is valid - continue without error
						continue;
					}
				}
				// Not a variadic pack but no concrete arg - pattern doesn't match
				return false;
			}
			
			const TemplateTypeArg& concrete_arg = concrete_args[i];
		
			FLASH_LOG(Templates, Trace, "Matching pattern arg[", i, "] against concrete arg[", i, "]");
		
			// Find the template parameter name for this pattern position
			// The pattern_arg contains the type from the pattern (e.g., T for pattern T&)
			// We need to check if the base types match and the modifiers match
		
			// Pattern matching rules:
			// 1. If pattern is "T&" and concrete is "int&", then T=int (reference match)
			// 2. If pattern is "T&&" and concrete is "int&&", then T=int (rvalue reference match)
			// 3. If pattern is "T*" and concrete is "int*", then T=int (pointer match)
			// 4. If pattern is "T**" and concrete is "int**", then T=int (double pointer match)
			// 5. If pattern is "const T" and concrete is "const int", then T=int (const match)
			// 6. If pattern is "T" and concrete is "int", then T=int (exact match)
			// 7. Reference/pointer/const modifiers must match
		
			// Check if modifiers match
			if (pattern_arg.is_reference != concrete_arg.is_reference) {
				FLASH_LOG(Templates, Trace, "  FAILED: is_reference mismatch");
				return false;
			}
			if (pattern_arg.is_rvalue_reference != concrete_arg.is_rvalue_reference) {
				FLASH_LOG(Templates, Trace, "  FAILED: is_rvalue_reference mismatch");
				return false;
			}
			if (pattern_arg.pointer_depth != concrete_arg.pointer_depth) {
				FLASH_LOG(Templates, Trace, "  FAILED: pointer_depth mismatch");
				return false;
			}
			if (pattern_arg.cv_qualifier != concrete_arg.cv_qualifier) {
				FLASH_LOG(Templates, Trace, "  FAILED: cv_qualifier mismatch");
				return false;
			}
			if (pattern_arg.is_array != concrete_arg.is_array) {
				FLASH_LOG(Templates, Trace, "  FAILED: array-ness mismatch");
				return false;
			}
			// Check array size matching
			// - If pattern has no size (T[]), it matches any array
			// - If pattern has SIZE_MAX (T[N] where N is template param), it matches any sized array but not unsized arrays
			// - If pattern has a specific size (T[3]), it must match exactly
			if (pattern_arg.is_array && pattern_arg.array_size.has_value() && concrete_arg.array_size.has_value()) {
				// Both have sizes - check if they match
				// SIZE_MAX in pattern means "any size" (template parameter like N)
				if (*pattern_arg.array_size != SIZE_MAX && *pattern_arg.array_size != *concrete_arg.array_size) {
					FLASH_LOG(Templates, Trace, "  FAILED: array size mismatch");
					return false;
				}
			} else if (pattern_arg.is_array && pattern_arg.array_size.has_value() && *pattern_arg.array_size == SIZE_MAX) {
				// Pattern has SIZE_MAX (like T[N]) but concrete has no size (like int[])
				// This should not match - T[N] requires a sized array
				if (!concrete_arg.array_size.has_value()) {
					FLASH_LOG(Templates, Trace, "  FAILED: pattern requires sized array but concrete is unsized");
					return false;
				}
			}
			if (pattern_arg.member_pointer_kind != concrete_arg.member_pointer_kind) {
				FLASH_LOG(Templates, Trace, "  FAILED: member pointer kind mismatch");
				return false;
			}
		
			// For pattern matching, we need to extract the template parameter name
			// The pattern_arg.base_type is UserDefined and represents the template parameter
			// We need to get the parameter name from template_params
		
			// The pattern_arg.base_type tells us which template parameter this is
			// For partial specialization Derived<T*, T>, both pattern args refer to the SAME
			// template parameter T, so we can't use position i
		
			// Find which template parameter this pattern arg refers to
			// base_type == Type::UserDefined (15) means it's a template parameter reference
			if (pattern_arg.base_type != Type::UserDefined) {
				// This is a concrete type or value in the pattern
				// (e.g., partial specialization Container<int, T> or enable_if<true, T>)
				// The concrete type/value must match exactly
				FLASH_LOG(Templates, Trace, "  Pattern arg[", i, "]: concrete type/value check");
				FLASH_LOG(Templates, Trace, "    pattern_arg.base_type=", static_cast<int>(pattern_arg.base_type), 
				          " concrete_arg.base_type=", static_cast<int>(concrete_arg.base_type));
				FLASH_LOG(Templates, Trace, "    pattern_arg.is_value=", pattern_arg.is_value, 
				          " concrete_arg.is_value=", concrete_arg.is_value);
				if (pattern_arg.is_value && concrete_arg.is_value) {
					FLASH_LOG(Templates, Trace, "    pattern_arg.value=", pattern_arg.value, 
					          " concrete_arg.value=", concrete_arg.value);
				}
				if (pattern_arg.base_type != concrete_arg.base_type) {
					FLASH_LOG(Templates, Trace, "    FAILED: base types don't match");
					return false;
				}
				// For non-type template parameters, also check the value matches
				if (pattern_arg.is_value && concrete_arg.is_value) {
					if (pattern_arg.value != concrete_arg.value) {
						FLASH_LOG(Templates, Trace, "    FAILED: values don't match");
						return false;  // Different values - no match
					}
				} else if (pattern_arg.is_value != concrete_arg.is_value) {
					FLASH_LOG(Templates, Trace, "    FAILED: is_value flags don't match");
					return false;  // One is value, one is type - no match
				}
				FLASH_LOG(Templates, Trace, "    SUCCESS: concrete type/value matches");
				continue;  // No substitution needed for concrete types/values - don't increment param_index
			}
		
			// Find the template parameter name for this pattern arg
			// First, try to get the name from the pattern arg's type_index (for reused parameters)
			// For is_same<T, T>, both pattern args point to the same TypeInfo for T
			std::string param_name;
			bool found_param = false;
			
			if (pattern_arg.type_index > 0 && pattern_arg.type_index < gTypeInfo.size()) {
				const TypeInfo& param_type_info = gTypeInfo[pattern_arg.type_index];
				param_name = std::string(StringTable::getStringView(param_type_info.name()));
				found_param = true;
				FLASH_LOG(Templates, Trace, "  Found parameter name '", param_name, "' from pattern_arg.type_index=", pattern_arg.type_index);
			}
			
			if (!found_param) {
				// Fallback: use param_index to get the template parameter
				// This is needed when type_index isn't set properly
				if (param_index >= template_params.size()) {
					FLASH_LOG(Templates, Trace, "  FAILED: param_index ", param_index, " >= template_params.size() ", template_params.size());
					return false;  // More template params needed than available - invalid pattern
				}
				
				if (template_params[param_index].is<TemplateParameterNode>()) {
					const TemplateParameterNode& template_param = template_params[param_index].as<TemplateParameterNode>();
					param_name = std::string(template_param.name());
					found_param = true;
				}
			
				if (!found_param) {
					FLASH_LOG(Templates, Trace, "  FAILED: Template parameter at param_index ", param_index, " is not a TemplateParameterNode");
					return false;  // Template parameter at position param_index is not a TemplateParameterNode
				}
			}
		
			// Check if we've already seen this parameter
			// For consistency checking, we need to compare the BASE TYPE only,
			// because Derived<T*, T> means both args bind to the same T, but with different modifiers
			auto it = param_substitutions.find(param_name);
			if (it != param_substitutions.end()) {
				// Parameter already bound - check consistency of BASE TYPE only
				if (it->second.base_type != concrete_arg.base_type) {
					FLASH_LOG(Templates, Trace, "  FAILED: Inconsistent substitution for parameter ", param_name);
					return false;  // Inconsistent substitution (different base types)
				}
				FLASH_LOG(Templates, Trace, "  SUCCESS: Reused parameter ", param_name, " - consistency check passed");
				// Don't increment param_index - we reused an existing parameter binding
			} else {
				// Bind this parameter to the concrete type
				param_substitutions[param_name] = concrete_arg;
				FLASH_LOG(Templates, Trace, "  SUCCESS: Bound parameter ", param_name, " to concrete type");
				// Increment param_index since we bound a new template parameter
				++param_index;
			}
		}
		
		// SFINAE check: If this pattern has a SFINAE condition (e.g., void_t<typename T::type>),
		// verify that the condition is satisfied with the substituted types.
		// This enables proper void_t detection behavior.
		if (sfinae_condition.has_value()) {
			const SfinaeCondition& cond = *sfinae_condition;
			
			// Get the concrete type for the template parameter
			if (cond.template_param_index < concrete_args.size()) {
				const TemplateTypeArg& concrete_arg = concrete_args[cond.template_param_index];
				
				// Check if the concrete type has the required member type
				if (concrete_arg.type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[concrete_arg.type_index];
					
					// Build the qualified member name (e.g., "WithType::type")
					StringBuilder qualified_name;
					qualified_name.append(type_info.name());
					qualified_name.append("::");
					qualified_name.append(cond.member_name);
					StringHandle qualified_handle = StringTable::getOrInternStringHandle(qualified_name.commit());
					
					// Check if this member type exists
					auto type_it = gTypesByName.find(qualified_handle);
					if (type_it == gTypesByName.end()) {
						FLASH_LOG(Templates, Debug, "SFINAE condition failed: ", 
						          StringTable::getStringView(qualified_handle), " does not exist");
						return false;  // SFINAE failure - pattern doesn't match
					}
					FLASH_LOG(Templates, Debug, "SFINAE condition passed: ", 
					          StringTable::getStringView(qualified_handle), " exists");
				}
			}
		}
	
		return true;  // All patterns matched
	}
	
	// Calculate specificity score (higher = more specialized)
	// T = 0, T& = 1, T* = 1, const T = 1, const T& = 2, T[N] = 2, T[] = 1, etc.
	int specificity() const
	{
		int score = 0;
	
		for (const auto& arg : pattern_args) {
			// Base score: any pattern parameter = 0
		
			// Pointer modifier adds specificity (T* is more specific than T)
			score += arg.pointer_depth;  // T* = +1, T** = +2, etc.
		
			// Reference modifier adds specificity
			if (arg.is_reference) {
				score += 1;  // T& is more specific than T
			}
			if (arg.is_rvalue_reference) {
				score += 1;  // T&& is more specific than T
			}
		
			// Array modifiers add specificity
			if (arg.is_array) {
				if (arg.array_size.has_value()) {
					// SIZE_MAX indicates "array with size expression but value unknown" (like T[N])
					// Concrete sizes (like T[3]) and template parameter sizes (like T[N]) both get score of 2
					score += 2;  // T[N] or T[3] is more specific than T[]
				} else {
					score += 1;  // T[] is more specific than T
				}
			}
		
			// CV-qualifiers add specificity
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
				score += 1;  // const T is more specific than T
			}
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
				score += 1;  // volatile T is more specific than T
			}
		}
	
		return score;
	}
};

// Key for template specializations
struct SpecializationKey {
	std::string template_name;
	std::vector<TemplateTypeArg> template_args;

	bool operator==(const SpecializationKey& other) const {
		return template_name == other.template_name && template_args == other.template_args;
	}
};

// Hash function for SpecializationKey
struct SpecializationKeyHash {
	size_t operator()(const SpecializationKey& key) const {
		size_t hash = std::hash<std::string>{}(key.template_name);
		TemplateTypeArgHash arg_hasher;
		for (const auto& arg : key.template_args) {
			hash ^= arg_hasher(arg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		return hash;
	}
};

// Template registry - stores template declarations and manages instantiations
class TemplateRegistry {
public:
	// Register a template function declaration
	void registerTemplate(std::string_view name, ASTNode template_node) {
		std::string key(name);
		templates_[key].push_back(template_node);
	}

	// Register template parameter names for a template
	void registerTemplateParameters(StringHandle key, const std::vector<StringHandle>& param_names) {
		template_parameters_[key] = std::vector<StringHandle>(param_names.begin(), param_names.end());
	}

	// Register an alias template: template<typename T> using Ptr = T*;
	void register_alias_template(std::string_view name, ASTNode alias_node) {
		std::string key(name);
		alias_templates_[key] = alias_node;
	}

	// Register a variable template: template<typename T> constexpr T pi = T(3.14159...);
	void registerVariableTemplate(std::string_view name, ASTNode variable_template_node) {
		std::string key(name);
		variable_templates_[key] = variable_template_node;
	}

	// Look up a variable template by name
	std::optional<ASTNode> lookupVariableTemplate(std::string_view name) const {
		auto it = variable_templates_.find(name);
		if (it != variable_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Look up an alias template by name
	std::optional<ASTNode> lookup_alias_template(std::string_view name) const {
		// Heterogeneous lookup - string_view accepted directly without temporary string allocation
		auto it = alias_templates_.find(name);
		if (it != alias_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Get all alias template names with a given prefix (for template instantiation)
	// Used to copy member template aliases from primary template to instantiated template
	std::vector<std::string_view> get_alias_templates_with_prefix(std::string_view prefix) const {
		std::vector<std::string_view> result;
		for (const auto& [name, node] : alias_templates_) {
			if (name.starts_with(prefix)) {
				result.push_back(name);
			}
		}
		return result;
	}

	// Register a deduction guide: template<typename T> ClassName(T) -> ClassName<T>;
	void register_deduction_guide(std::string_view class_name, ASTNode guide_node) {
		std::string key(class_name);
		deduction_guides_[key].push_back(guide_node);
	}

	// Look up deduction guides for a class template
	std::vector<ASTNode> lookup_deduction_guides(std::string_view class_name) const {
		auto it = deduction_guides_.find(class_name);
		if (it != deduction_guides_.end()) {
			return it->second;
		}
		return {};
	}

	// Get template parameter names for a template
	std::vector<StringHandle> getTemplateParameters(StringHandle name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = template_parameters_.find(name);
		if (it != template_parameters_.end()) {
			return it->second;
		}
		return {};
	}
	
	// Look up a template by name
	// Look up a template by name
	// If multiple overloads exist, returns the first one registered
	// For all overloads, use lookupAllTemplates()
	std::optional<ASTNode> lookupTemplate(std::string_view name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = templates_.find(name);
		if (it != templates_.end() && !it->second.empty()) {
			return it->second.front();  // Return first overload
		}
		return std::nullopt;
	}
	
	std::optional<ASTNode> lookupTemplate(StringHandle name) const {
		return lookupTemplate(StringTable::getStringView(name));
	}

	// Look up all template overloads for a given name
	// Returns a reference to the internal vector for efficiency
	const std::vector<ASTNode>* lookupAllTemplates(std::string_view name) const {
		auto it = templates_.find(name);
		if (it != templates_.end()) {
			return &it->second;
		}
		return nullptr;
	}
	
	// Get all registered template names (for smart re-instantiation)
	std::vector<std::string_view> getAllTemplateNames() const {
		std::vector<std::string_view> result;
		result.reserve(templates_.size());
		for (const auto& [name, _] : templates_) {
			result.push_back(name);
		}
		return result;
	}
	
	// Check if a template instantiation already exists
	bool hasInstantiation(const TemplateInstantiationKey& key) const {
		return instantiations_.find(key) != instantiations_.end();
	}
	
	// Get an existing instantiation
	std::optional<ASTNode> getInstantiation(const TemplateInstantiationKey& key) const {
		auto it = instantiations_.find(key);
		if (it != instantiations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Register a new instantiation
	void registerInstantiation(const TemplateInstantiationKey& key, ASTNode instantiated_node) {
		instantiations_[key] = instantiated_node;
	}
	
	// Helper to convert Type to string for mangling
	static std::string_view typeToString(Type type) {
		switch (type) {
			case Type::Int: return "int";
			case Type::Float: return "float";
			case Type::Double: return "double";
			case Type::Bool: return "bool";
			case Type::Char: return "char";
			case Type::Long: return "long";
			case Type::LongLong: return "longlong";
			case Type::Short: return "short";
			case Type::UnsignedInt: return "uint";
			case Type::UnsignedLong: return "ulong";
			case Type::UnsignedLongLong: return "ulonglong";
			case Type::UnsignedShort: return "ushort";
			case Type::UnsignedChar: return "uchar";
			default: return "unknown";
		}
	}

	// Helper to convert string to Type for parsing mangled names
	static Type stringToType(std::string_view str) {
		if (str == "int") return Type::Int;
		if (str == "float") return Type::Float;
		if (str == "double") return Type::Double;
		if (str == "bool") return Type::Bool;
		if (str == "char") return Type::Char;
		if (str == "long") return Type::Long;
		if (str == "longlong") return Type::LongLong;
		if (str == "short") return Type::Short;
		if (str == "uint") return Type::UnsignedInt;
		if (str == "ulong") return Type::UnsignedLong;
		if (str == "ulonglong") return Type::UnsignedLongLong;
		if (str == "ushort") return Type::UnsignedShort;
		if (str == "uchar") return Type::UnsignedChar;
		return Type::Invalid;
	}

	// Generate a mangled name for a template instantiation
	// Example: max<int> -> max_int, max<int, 5> -> max_int_5, max<vector> -> max_vector
	static std::string_view mangleTemplateName(std::string_view base_name, const std::vector<TemplateArgument>& args) {
		StringBuilder mangled;
		mangled.append(base_name).append("_");

		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0) mangled.append("_");

			if (args[i].kind == TemplateArgument::Kind::Type) {
				mangled.append(typeToString(args[i].type_value));
				// Include reference qualifiers in mangled name for unique instantiations
				// This ensures int, int&, and int&& generate different mangled names
				if (args[i].type_specifier.has_value()) {
					const auto& ts = *args[i].type_specifier;
					if (ts.is_rvalue_reference()) {
						mangled.append("RR");  // Rvalue reference suffix
					} else if (ts.is_reference()) {
						mangled.append("R");   // Lvalue reference suffix
					}
				}
			} else if (args[i].kind == TemplateArgument::Kind::Value) {
				mangled.append(args[i].int_value);
			} else if (args[i].kind == TemplateArgument::Kind::Template) {
				// For template template arguments, use the template name
				mangled.append(args[i].template_name);
			}
		}

		return mangled.commit();
	}

	// Register an out-of-line template member function definition
	void registerOutOfLineMember(std::string_view class_name, OutOfLineMemberFunction member_func) {
		std::string key(class_name);
		out_of_line_members_[key].push_back(std::move(member_func));
	}

	// Get out-of-line member functions for a class
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Register an out-of-line template static member variable definition
	void registerOutOfLineMemberVariable(std::string_view class_name, OutOfLineMemberVariable member_var) {
		std::string key(class_name);
		out_of_line_variables_[key].push_back(std::move(member_var));
	}

	// Get out-of-line member variables for a class
	std::vector<OutOfLineMemberVariable> getOutOfLineMemberVariables(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_variables_.find(class_name);
		if (it != out_of_line_variables_.end()) {
			return it->second;
		}
		return {};
	}

	// Register a template specialization pattern
	void registerSpecializationPattern(std::string_view template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		std::string key(template_name);
		FLASH_LOG(Templates, Debug, "registerSpecializationPattern: template_name='", template_name, 
		          "', num_template_params=", template_params.size(), ", num_pattern_args=", pattern_args.size());
		
		// Debug: log each pattern arg
		for (size_t i = 0; i < pattern_args.size(); ++i) {
			const auto& arg = pattern_args[i];
			std::string_view dep_name_view = arg.dependent_name.isValid() ? StringTable::getStringView(arg.dependent_name) : "";
			FLASH_LOG(Templates, Debug, "  pattern_arg[", i, "]: base_type=", static_cast<int>(arg.base_type),
			          ", type_index=", arg.type_index, ", is_dependent=", arg.is_dependent,
			          ", is_value=", arg.is_value, ", dependent_name='", dep_name_view, "'");
		}
		
		// Debug: log each template param type
		for (size_t i = 0; i < template_params.size(); ++i) {
			FLASH_LOG(Templates, Debug, "  template_param[", i, "]: type_name=", template_params[i].type_name(), 
			          ", is_TemplateParameterNode=", template_params[i].is<TemplateParameterNode>());
		}
		
		TemplatePattern pattern;
		pattern.template_params = template_params;
		pattern.pattern_args = pattern_args;
		pattern.specialized_node = specialized_node;
		pattern.sfinae_condition = sfinae_cond;
		
		// Auto-detect void_t SFINAE patterns if no explicit condition provided.
		// Heuristic: patterns with 2 args where first is dependent and second is void
		// indicate void_t<...> usage. The member name to check is extracted from the
		// first arg's dependent_name if available, otherwise defaults to "type".
		if (!sfinae_cond.has_value() && pattern_args.size() == 2) {
			const auto& first_arg = pattern_args[0];
			const auto& second_arg = pattern_args[1];
			
			// Check: first arg is dependent (template param), second arg is void (from void_t expansion)
			if (first_arg.is_dependent && !second_arg.is_dependent && 
			    second_arg.base_type == Type::Void) {
				// This looks like a void_t SFINAE pattern.
				// Try to extract the member name from available information.
				StringHandle member_name;
				
				// Check if the first arg's dependent_name contains a qualified name like "T::type"
				if (first_arg.dependent_name.isValid()) {
					std::string_view dep_name = StringTable::getStringView(first_arg.dependent_name);
					size_t scope_pos = dep_name.rfind("::");
					if (scope_pos != std::string_view::npos && scope_pos + 2 < dep_name.size()) {
						// Extract the member name after "::"
						std::string_view extracted_member = dep_name.substr(scope_pos + 2);
						member_name = StringTable::getOrInternStringHandle(extracted_member);
						FLASH_LOG(Templates, Debug, "Extracted SFINAE member name '", extracted_member, "' from dependent_name '", dep_name, "'");
					}
				}
				
				// If no member name was extracted, check the type name via type_index
				if (!member_name.isValid() && first_arg.type_index > 0 && first_arg.type_index < gTypeInfo.size()) {
					std::string_view type_name = StringTable::getStringView(gTypeInfo[first_arg.type_index].name());
					size_t scope_pos = type_name.rfind("::");
					if (scope_pos != std::string_view::npos && scope_pos + 2 < type_name.size()) {
						std::string_view extracted_member = type_name.substr(scope_pos + 2);
						member_name = StringTable::getOrInternStringHandle(extracted_member);
						FLASH_LOG(Templates, Debug, "Extracted SFINAE member name '", extracted_member, "' from type_name '", type_name, "'");
					}
				}
				
				// Default to "type" if no member name could be extracted
				// This is the most common pattern (e.g., void_t<typename T::type>)
				if (!member_name.isValid()) {
					member_name = StringTable::getOrInternStringHandle("type");
					FLASH_LOG(Templates, Debug, "Using default SFINAE member name 'type'");
				}
				
				pattern.sfinae_condition = SfinaeCondition(0, member_name);
				FLASH_LOG(Templates, Debug, "Auto-detected void_t SFINAE pattern: checking for ::", 
				          StringTable::getStringView(member_name), " member");
			}
		}
		
		specialization_patterns_[key].push_back(std::move(pattern));
		FLASH_LOG(Templates, Debug, "  Total patterns for '", template_name, "': ", specialization_patterns_[key].size());
		if (pattern.sfinae_condition.has_value()) {
			// Note: pattern has been moved, we need to access the stored one
			const auto& stored_pattern = specialization_patterns_[key].back();
			if (stored_pattern.sfinae_condition.has_value()) {
				FLASH_LOG(Templates, Debug, "  SFINAE condition set: check param[", stored_pattern.sfinae_condition->template_param_index, 
				          "]::", StringTable::getStringView(stored_pattern.sfinae_condition->member_name));
			}
		}
	}

	// Register a template specialization (exact match)
	void registerSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		SpecializationKey key{std::string(template_name), template_args};
		specializations_[key] = specialized_node;
		FLASH_LOG(Templates, Debug, "registerSpecialization: '", template_name, "' with ", template_args.size(), " args");
	}

	// Look up an exact template specialization (no pattern matching)
	std::optional<ASTNode> lookupExactSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		SpecializationKey key{std::string(template_name), template_args};
		
		FLASH_LOG(Templates, Debug, "lookupExactSpecialization: '", template_name, "' with ", template_args.size(), " args");
		
		auto it = specializations_.find(key);
		if (it != specializations_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Look up a template specialization (exact match first, then pattern match)
	std::optional<ASTNode> lookupSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) const {
		FLASH_LOG(Templates, Debug, "lookupSpecialization: template_name='", template_name, "', num_args=", template_args.size());
		
		// First, try exact match
		auto exact = lookupExactSpecialization(template_name, template_args);
		if (exact.has_value()) {
			FLASH_LOG(Templates, Debug, "  Found exact specialization match");
			return exact;
		}
		
		// No exact match - try pattern matching
		FLASH_LOG(Templates, Debug, "  No exact match, trying pattern matching...");
		auto pattern_result = matchSpecializationPattern(template_name, template_args);
		if (pattern_result.has_value()) {
			FLASH_LOG(Templates, Debug, "  Found pattern match!");
		} else {
			FLASH_LOG(Templates, Debug, "  No pattern match found");
		}
		return pattern_result;
	}
	
	// Find a matching specialization pattern
	std::optional<ASTNode> matchSpecializationPattern(std::string_view template_name, 
	                                                  const std::vector<TemplateTypeArg>& concrete_args) const {
		// Heterogeneous lookup - string_view accepted directly
		auto patterns_it = specialization_patterns_.find(template_name);
		if (patterns_it == specialization_patterns_.end()) {
			FLASH_LOG(Templates, Debug, "    No patterns registered for template '", template_name, "'");
			return std::nullopt;  // No patterns for this template
		}
		
		const std::vector<TemplatePattern>& patterns = patterns_it->second;
		FLASH_LOG(Templates, Debug, "    Found ", patterns.size(), " pattern(s) for template '", template_name, "'");
		
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		
		// Find the most specific matching pattern
		for (size_t i = 0; i < patterns.size(); ++i) {
			const auto& pattern = patterns[i];
			FLASH_LOG(Templates, Debug, "    Checking pattern #", i, " (specificity=", pattern.specificity(), ")");
			std::unordered_map<std::string, TemplateTypeArg> substitutions;
			if (pattern.matches(concrete_args, substitutions)) {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " MATCHES!");
				int spec = pattern.specificity();
				if (spec > best_specificity) {
					best_match = &pattern;
					best_specificity = spec;
					FLASH_LOG(Templates, Debug, "      New best match (specificity=", spec, ")");
				}
			} else {
				FLASH_LOG(Templates, Debug, "      Pattern #", i, " does not match");
			}
		}
		
		if (best_match) {
			FLASH_LOG(Templates, Debug, "    Selected best pattern (specificity=", best_specificity, ")");
			return best_match->specialized_node;
		}
		
		FLASH_LOG(Templates, Debug, "    No matching pattern found");
		return std::nullopt;
	}

	// Clear all templates and instantiations
	void clear() {
		templates_.clear();
		template_parameters_.clear();
		instantiations_.clear();
		out_of_line_members_.clear();
		specializations_.clear();
		specialization_patterns_.clear();
		alias_templates_.clear();
		variable_templates_.clear();
		deduction_guides_.clear();
		instantiation_to_pattern_.clear();
	}

	// Public access to specialization patterns for pattern matching in Parser
	std::unordered_map<std::string, std::vector<TemplatePattern>, TransparentStringHash, std::equal_to<>> specialization_patterns_;
	
	// Register mapping from instantiated name to pattern name (for partial specializations)
	void register_instantiation_pattern(StringHandle instantiated_name, StringHandle pattern_name) {
		instantiation_to_pattern_[instantiated_name] = pattern_name;
	}
	
	// Look up which pattern was used for an instantiation
	std::optional<StringHandle> get_instantiation_pattern(StringHandle instantiated_name) const {
		auto it = instantiation_to_pattern_.find(instantiated_name);
		if (it != instantiation_to_pattern_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

private:
	// Map from template name to template declaration nodes - supports multiple overloads (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<ASTNode>, TransparentStringHash, std::equal_to<>> templates_;

	// Map from template name to template parameter names (supports heterogeneous lookup)
	std::unordered_map<StringHandle, std::vector<StringHandle>, TransparentStringHash, std::equal_to<>> template_parameters_;

	// Map from alias template name to TemplateAliasNode (supports heterogeneous lookup)
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> alias_templates_;

	// Map from variable template name to TemplateVariableDeclarationNode (supports heterogeneous lookup)
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> variable_templates_;

	// Map from class template name to deduction guides (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<ASTNode>, TransparentStringHash, std::equal_to<>> deduction_guides_;

	// Map from instantiation key to instantiated function node
	std::unordered_map<TemplateInstantiationKey, ASTNode, TemplateInstantiationKeyHash> instantiations_;

	// Map from class name to out-of-line member function definitions (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<OutOfLineMemberFunction>, TransparentStringHash, std::equal_to<>> out_of_line_members_;

	// Map from class name to out-of-line static member variable definitions (supports heterogeneous lookup)
	std::unordered_map<std::string, std::vector<OutOfLineMemberVariable>, TransparentStringHash, std::equal_to<>> out_of_line_variables_;

	// Map from (template_name, template_args) to specialized class node (exact matches)
	std::unordered_map<SpecializationKey, ASTNode, SpecializationKeyHash> specializations_;
	
	// Map from instantiated struct name to the pattern struct name used (for partial specializations)
	// Example: "Wrapper_int_0" -> "Wrapper_pattern__"
	// This allows looking up member aliases from the correct specialization
	std::unordered_map<StringHandle, StringHandle, TransparentStringHash, std::equal_to<>> instantiation_to_pattern_;
};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

// ============================================================================
// Lazy Template Member Function Instantiation Registry
// ============================================================================

// Information needed to instantiate a template member function on-demand
struct LazyMemberFunctionInfo {
	StringHandle class_template_name;          // Original template name (e.g., "vector")
	StringHandle instantiated_class_name;      // Instantiated class name (e.g., "vector_int")
	StringHandle member_function_name;         // Member function name
	ASTNode original_function_node;            // Original function from template
	std::vector<ASTNode> template_params;      // Template parameters from class template
	std::vector<TemplateTypeArg> template_args; // Concrete template arguments used for instantiation
	AccessSpecifier access;                    // Access specifier (public/private/protected)
	bool is_virtual;                           // Virtual function flag
	bool is_pure_virtual;                      // Pure virtual flag
	bool is_override;                          // Override flag
	bool is_final;                             // Final flag
	bool is_const_method;                      // Const member function flag
	bool is_constructor;                       // Constructor flag
	bool is_destructor;                        // Destructor flag
};

// Information needed to instantiate a MEMBER FUNCTION TEMPLATE on-demand
// This is for cases like: template<typename T> struct S { template<typename U> U convert(); };
struct LazyMemberFunctionTemplateInfo {
	StringHandle qualified_template_name;      // Qualified name (e.g., "vector_int::convert")
	ASTNode template_node;                     // TemplateFunctionDeclarationNode
	std::vector<TemplateTypeArg> pending_template_args; // Template args to use for instantiation
};

// Registry for tracking uninstantiated template member functions
// Allows lazy (on-demand) instantiation for better compilation performance
class LazyMemberInstantiationRegistry {
public:
	static LazyMemberInstantiationRegistry& getInstance() {
		static LazyMemberInstantiationRegistry instance;
		return instance;
	}
	
	// Register a member function for lazy instantiation
	// Key format: "instantiated_class_name::member_function_name"
	void registerLazyMember(LazyMemberFunctionInfo info) {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(info.instantiated_class_name)
			.append("::")
			.append(info.member_function_name)
			.commit();
		
		lazy_members_[StringTable::getOrInternStringHandle(key)] = std::move(info);
	}
	
	// Check if a member function needs lazy instantiation
	bool needsInstantiation(StringHandle instantiated_class_name, StringHandle member_function_name) const {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();  // Changed from preview() to commit()
		
		auto handle = StringTable::getOrInternStringHandle(key);
		return lazy_members_.find(handle) != lazy_members_.end();
	}
	
	// Get lazy member info for instantiation
	std::optional<LazyMemberFunctionInfo> getLazyMemberInfo(StringHandle instantiated_class_name, StringHandle member_function_name) {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		auto it = lazy_members_.find(handle);
		if (it != lazy_members_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Mark a member function as instantiated (remove from lazy registry)
	void markInstantiated(StringHandle instantiated_class_name, StringHandle member_function_name) {
		StringBuilder key_builder;
		std::string_view key = key_builder
			.append(instantiated_class_name)
			.append("::")
			.append(member_function_name)
			.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		lazy_members_.erase(handle);
	}
	
	// Clear all lazy members (for testing)
	void clear() {
		lazy_members_.clear();
	}
	
	// Get count of uninstantiated members (for diagnostics)
	size_t getUninstantiatedCount() const {
		return lazy_members_.size();
	}

private:
	LazyMemberInstantiationRegistry() = default;
	
	// Map from "instantiated_class::member_function" to lazy instantiation info
	std::unordered_map<StringHandle, LazyMemberFunctionInfo, TransparentStringHash, std::equal_to<>> lazy_members_;
};

// Registry for tracking uninstantiated MEMBER FUNCTION TEMPLATES
// Handles cases like: template<typename T> struct S { template<typename U> U convert(); };
class LazyMemberFunctionTemplateRegistry {
public:
	static LazyMemberFunctionTemplateRegistry& getInstance() {
		static LazyMemberFunctionTemplateRegistry instance;
		return instance;
	}
	
	// Register a member function template for lazy instantiation
	// Key format: "qualified_name::template_args_encoded"
	void registerLazyMemberTemplate(LazyMemberFunctionTemplateInfo info) {
		// Create a unique key that includes both the function name and template arguments
		StringBuilder key_builder;
		key_builder.append(info.qualified_template_name).append("<");
		for (size_t i = 0; i < info.pending_template_args.size(); ++i) {
			if (i > 0) key_builder.append(",");
			// Use type index and base type to create a unique identifier
			key_builder.append(std::to_string(static_cast<int>(info.pending_template_args[i].base_type)));
			key_builder.append("_");
			key_builder.append(std::to_string(info.pending_template_args[i].type_index));
		}
		key_builder.append(">");
		std::string_view key = key_builder.commit();
		
		lazy_member_templates_[StringTable::getOrInternStringHandle(key)] = std::move(info);
	}
	
	// Check if a member function template needs lazy instantiation
	bool needsInstantiation(StringHandle qualified_name, const std::vector<TemplateTypeArg>& template_args) const {
		StringBuilder key_builder;
		key_builder.append(qualified_name).append("<");
		for (size_t i = 0; i < template_args.size(); ++i) {
			if (i > 0) key_builder.append(",");
			key_builder.append(std::to_string(static_cast<int>(template_args[i].base_type)));
			key_builder.append("_");
			key_builder.append(std::to_string(template_args[i].type_index));
		}
		key_builder.append(">");
		std::string_view key = key_builder.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		return lazy_member_templates_.find(handle) != lazy_member_templates_.end();
	}
	
	// Get lazy member function template info
	std::optional<LazyMemberFunctionTemplateInfo> getLazyMemberTemplateInfo(
		StringHandle qualified_name, 
		const std::vector<TemplateTypeArg>& template_args) {
		
		StringBuilder key_builder;
		key_builder.append(qualified_name).append("<");
		for (size_t i = 0; i < template_args.size(); ++i) {
			if (i > 0) key_builder.append(",");
			key_builder.append(std::to_string(static_cast<int>(template_args[i].base_type)));
			key_builder.append("_");
			key_builder.append(std::to_string(template_args[i].type_index));
		}
		key_builder.append(">");
		std::string_view key = key_builder.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		auto it = lazy_member_templates_.find(handle);
		if (it != lazy_member_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Mark a member function template as instantiated
	void markInstantiated(StringHandle qualified_name, const std::vector<TemplateTypeArg>& template_args) {
		StringBuilder key_builder;
		key_builder.append(qualified_name).append("<");
		for (size_t i = 0; i < template_args.size(); ++i) {
			if (i > 0) key_builder.append(",");
			key_builder.append(std::to_string(static_cast<int>(template_args[i].base_type)));
			key_builder.append("_");
			key_builder.append(std::to_string(template_args[i].type_index));
		}
		key_builder.append(">");
		std::string_view key = key_builder.commit();
		
		auto handle = StringTable::getOrInternStringHandle(key);
		lazy_member_templates_.erase(handle);
	}
	
	// Clear all lazy member templates (for testing)
	void clear() {
		lazy_member_templates_.clear();
	}

private:
	LazyMemberFunctionTemplateRegistry() = default;
	
	// Map from "qualified_name<args>" to lazy instantiation info
	std::unordered_map<StringHandle, LazyMemberFunctionTemplateInfo, TransparentStringHash, std::equal_to<>> lazy_member_templates_;
};

// Global lazy member instantiation registry
// Note: Use LazyMemberInstantiationRegistry::getInstance() to access
// (cannot use global variable due to singleton pattern)

// ============================================================================
// C++20 Concepts Registry (inline with TemplateRegistry since they're related)
// ============================================================================

// Concept registry for storing and looking up C++20 concept declarations
// Concepts are named constraints that can be used to constrain template parameters
class ConceptRegistry {
public:
	ConceptRegistry() = default;

	// Register a concept declaration
	// concept_name: The name of the concept (e.g., "Integral", "Addable")
	// concept_node: The ConceptDeclarationNode AST node
	void registerConcept(std::string_view concept_name, ASTNode concept_node) {
		std::string key(concept_name);
		concepts_[key] = concept_node;
	}

	// Look up a concept by name
	// Returns the ConceptDeclarationNode if found, std::nullopt otherwise
	std::optional<ASTNode> lookupConcept(std::string_view concept_name) const {
		auto it = concepts_.find(concept_name);
		if (it != concepts_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Check if a concept exists
	bool hasConcept(std::string_view concept_name) const {
		return concepts_.find(concept_name) != concepts_.end();
	}

	// Clear all concepts (for testing)
	void clear() {
		concepts_.clear();
	}

	// Get all concept names (for debugging)
	std::vector<std::string> getAllConceptNames() const {
		std::vector<std::string> names;
		names.reserve(concepts_.size());
		for (const auto& pair : concepts_) {
			names.push_back(pair.first);
		}
		return names;
	}

private:
	// Map from concept name to ConceptDeclarationNode
	// Using TransparentStringHash for heterogeneous lookup with string_view
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> concepts_;
};

// Global concept registry
extern ConceptRegistry gConceptRegistry;

// ============================================================================
// Concept Subsumption for C++20
// ============================================================================

// Check if constraint A subsumes constraint B
// A subsumes B if whenever A is satisfied, B is also satisfied
// In practice: A subsumes B if A's requirements are a superset of B's
inline bool constraintSubsumes(const ASTNode& constraintA, const ASTNode& constraintB) {
	// Advanced subsumption rules:
	// 1. Identical constraints subsume each other
	// 2. A && B subsumes A (conjunction implies the parts)
	// 3. A && B subsumes B (conjunction implies the parts)
	// 4. A subsumes A || B (A is stronger than disjunction with A)
	// 5. A && !B does not subsume A (negation creates incompatibility)
	// 6. Transitivity: if A subsumes B and B subsumes C, then A subsumes C
	// 7. A && B && C subsumes A && B (more constraints = more specific)
	
	// If constraints are identical, they subsume each other
	// This is a simplified check - full implementation would need deep comparison
	if (constraintA.type_name() == constraintB.type_name()) {
		// Same type - might be the same constraint
		// For full correctness, we'd need to compare the actual expressions
		return true;
	}
	
	// Check if A is a conjunction that includes B
	if (constraintA.is<BinaryOperatorNode>()) {
		const auto& binop = constraintA.as<BinaryOperatorNode>();
		if (binop.op() == "&&") {
			// A = X && Y, check if X or Y subsumes B
			if (constraintSubsumes(binop.get_lhs(), constraintB)) {
				return true;
			}
			if (constraintSubsumes(binop.get_rhs(), constraintB)) {
				return true;
			}
			
			// Check transitive subsumption: (A && B) subsumes C if A subsumes C or B subsumes C
			// Already handled above
		}
		
		// Handle negation: !A does not subsume A
		if (binop.op() == "||") {
			// A = X || Y does not generally subsume anything
			// (disjunction is weaker than either branch)
			return false;
		}
	}
	
	// Check if A is a unary negation operator
	if (constraintA.is<UnaryOperatorNode>()) {
		const auto& unop = constraintA.as<UnaryOperatorNode>();
		if (unop.op() == "!") {
			// !A does not subsume A (they're contradictory)
			// !A subsumes !(A && B) is complex, skip for now
			return false;
		}
	}
	
	// Check if B is a disjunction where A subsumes one branch
	if (constraintB.is<BinaryOperatorNode>()) {
		const auto& binop = constraintB.as<BinaryOperatorNode>();
		if (binop.op() == "||") {
			// B = X || Y, A subsumes B if A subsumes both X and Y
			if (constraintSubsumes(constraintA, binop.get_lhs()) && 
			    constraintSubsumes(constraintA, binop.get_rhs())) {
				return true;
			}
		}
		
		// Check if B is a conjunction where A subsumes the whole conjunction
		if (binop.op() == "&&") {
			// B = X && Y, A subsumes B if A subsumes (X && Y) as a whole
			// This is tricky: A subsumes (X && Y) if A subsumes at least one of them
			// Example: A subsumes (A && B) because A is less restrictive
			// But we already check if constraintA matches constraintB above
			// So skip detailed analysis here
		}
	}
	
	return false;  // Conservative: assume no subsumption
}

// Compare two concepts for subsumption ordering
// Returns: -1 if A subsumes B, 1 if B subsumes A, 0 if neither
inline int compareConceptSubsumption(const ASTNode& conceptA, const ASTNode& conceptB) {
	// Get constraint expressions from concepts
	const ASTNode* exprA = nullptr;
	const ASTNode* exprB = nullptr;
	
	if (conceptA.is<ConceptDeclarationNode>()) {
		exprA = &conceptA.as<ConceptDeclarationNode>().constraint_expr();
	}
	if (conceptB.is<ConceptDeclarationNode>()) {
		exprB = &conceptB.as<ConceptDeclarationNode>().constraint_expr();
	}
	
	if (!exprA || !exprB) {
		return 0;  // Can't compare
	}
	
	bool a_subsumes_b = constraintSubsumes(*exprA, *exprB);
	bool b_subsumes_a = constraintSubsumes(*exprB, *exprA);
	
	if (a_subsumes_b && !b_subsumes_a) {
		return -1;  // A is more specific (subsumes B)
	}
	if (b_subsumes_a && !a_subsumes_b) {
		return 1;   // B is more specific (subsumes A)
	}
	
	return 0;  // Neither subsumes the other (or both do - equivalent)
}

// ============================================================================
// Constraint Evaluation for C++20 Concepts
// ============================================================================

// Result of constraint evaluation
struct ConstraintEvaluationResult {
	bool satisfied;
	std::string error_message;
	std::string failed_requirement;
	std::string suggestion;
	
	static ConstraintEvaluationResult success() {
		return ConstraintEvaluationResult{true, "", "", ""};
	}
	
	static ConstraintEvaluationResult failure(std::string_view error_msg, 
	                                          std::string_view failed_req = "",
	                                          std::string_view suggestion = "") {
		return ConstraintEvaluationResult{
			false, 
			std::string(error_msg), 
			std::string(failed_req),
			std::string(suggestion)
		};
	}
};

// Helper function to check if a type is integral
inline bool isIntegralType(Type type) {
	switch (type) {
		case Type::Bool:
		case Type::Char:
		case Type::Short:
		case Type::Int:
		case Type::Long:
		case Type::LongLong:
		case Type::UnsignedChar:
		case Type::UnsignedShort:
		case Type::UnsignedInt:
		case Type::UnsignedLong:
		case Type::UnsignedLongLong:
			return true;
		default:
			return false;
	}
}

// Helper function to check if a type is floating point
inline bool isFloatingPointType(Type type) {
	switch (type) {
		case Type::Float:
		case Type::Double:
		case Type::LongDouble:
			return true;
		default:
			return false;
	}
}

// Helper function to evaluate type traits like std::is_integral_v<T>
inline bool evaluateTypeTrait(std::string_view trait_name, const std::vector<TemplateTypeArg>& type_args) {
	if (type_args.empty()) {
		return false;  // Type traits need at least one argument
	}
	
	Type arg_type = type_args[0].base_type;
	
	// Handle common type traits
	if (trait_name == "is_integral_v" || trait_name == "is_integral") {
		return isIntegralType(arg_type);
	}
	else if (trait_name == "is_floating_point_v" || trait_name == "is_floating_point") {
		return isFloatingPointType(arg_type);
	}
	else if (trait_name == "is_arithmetic_v" || trait_name == "is_arithmetic") {
		return isIntegralType(arg_type) || isFloatingPointType(arg_type);
	}
	else if (trait_name == "is_signed_v" || trait_name == "is_signed") {
		// Check if type is signed
		switch (arg_type) {
			case Type::Char:  // char signedness is implementation-defined, but typically signed
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return true;
			default:
				return false;
		}
	}
	else if (trait_name == "is_unsigned_v" || trait_name == "is_unsigned") {
		switch (arg_type) {
			case Type::Bool:
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				return true;
			default:
				return false;
		}
	}
	
	// Unknown type trait - assume satisfied (conservative approach)
	return true;
}

// Enhanced constraint evaluator for C++20 concepts
// Evaluates constraints and provides detailed error messages when they fail
inline ConstraintEvaluationResult evaluateConstraint(
	const ASTNode& constraint_expr, 
	const std::vector<TemplateTypeArg>& template_args,
	const std::vector<std::string_view>& template_param_names = {}) {
	
	// Handle ExpressionNode wrapper - unwrap it and evaluate the inner node
	if (constraint_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = constraint_expr.as<ExpressionNode>();
		// ExpressionNode is a variant, visit it to get the actual inner node
		return std::visit([&](const auto& inner) -> ConstraintEvaluationResult {
			using T = std::decay_t<decltype(inner)>;
			// Create an ASTNode wrapper around the inner node
			ASTNode inner_ast_node(const_cast<T*>(&inner));
			return evaluateConstraint(inner_ast_node, template_args, template_param_names);
		}, expr_variant);
	}
	
	// For BoolLiteralNode (true/false keywords parsed as boolean literals)
	if (constraint_expr.is<BoolLiteralNode>()) {
		const auto& literal = constraint_expr.as<BoolLiteralNode>();
		bool value = literal.value();
		
		if (!value) {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		return ConstraintEvaluationResult::success();
	}
	
	// For boolean literals (true/false), evaluate directly
	if (constraint_expr.is<NumericLiteralNode>()) {
		const auto& literal = constraint_expr.as<NumericLiteralNode>();
		// Check if the value is 0 (false) or non-zero (true)
		bool value = true;  // default to true
		if (std::holds_alternative<unsigned long long>(literal.value())) {
			value = std::get<unsigned long long>(literal.value()) != 0;
		} else if (std::holds_alternative<double>(literal.value())) {
			value = std::get<double>(literal.value()) != 0.0;
		}
		
		if (!value) {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		return ConstraintEvaluationResult::success();
	}
	
	// For identifier nodes (concept names or type trait variables)
	if (constraint_expr.is<IdentifierNode>()) {
		const auto& ident = constraint_expr.as<IdentifierNode>();
		std::string_view name = ident.name();
		
		// Check for boolean literals written as identifiers (true/false)
		if (name == "false") {
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: literal constraint is false",
				"false",
				"use 'true' or a valid concept expression"
			);
		}
		if (name == "true") {
			return ConstraintEvaluationResult::success();
		}
		
		// Check if it's a type trait variable (e.g., is_integral_v)
		if (name.find("_v") != std::string_view::npos || 
		    name.find("is_") == 0) {
			// Try to evaluate as type trait
			bool result = evaluateTypeTrait(name, template_args);
			if (!result) {
				return ConstraintEvaluationResult::failure(
					std::string("constraint not satisfied: type trait '") + std::string(name) + "' evaluated to false",
					std::string(name),
					"check that the template argument satisfies the type trait"
				);
			}
			return ConstraintEvaluationResult::success();
		}
		
		// Otherwise, look up as a concept
		auto concept_opt = gConceptRegistry.lookupConcept(name);
		if (!concept_opt.has_value()) {
			return ConstraintEvaluationResult::failure(
				std::string("constraint not satisfied: concept '") + std::string(name) + "' not found",
				std::string(name),
				std::string("declare the concept before using it in a requires clause")
			);
		}
		
		// Concept found - evaluate its constraint expression with the template arguments
		const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
		return evaluateConstraint(concept_node.constraint_expr(), template_args, template_param_names);
	}
	
	// For member access nodes (e.g., std::is_integral_v<T>)
	if (constraint_expr.is<MemberAccessNode>()) {
		const auto& member = constraint_expr.as<MemberAccessNode>();
		// Try to get the member name for type trait evaluation
		// This handles std::is_integral_v syntax
		// For now, we'll accept these as satisfied
		return ConstraintEvaluationResult::success();
	}
	
	// For function call nodes (concept with template arguments like Integral<T>)
	// This handles the Concept<T> syntax in requires clauses
	if (constraint_expr.is<FunctionCallNode>()) {
		const auto& func_call = constraint_expr.as<FunctionCallNode>();
		std::string_view concept_name = func_call.called_from().value();
		
		// Look up the concept
		auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
		if (!concept_opt.has_value()) {
			// Not a concept - might be a function call, assume satisfied
			return ConstraintEvaluationResult::success();
		}
		
		// Get the concept's constraint expression and evaluate it
		const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
		
		// The template arguments in the function call should be used to substitute
		// into the concept's constraint expression
		// For now, pass through the original template_args
		return evaluateConstraint(concept_node.constraint_expr(), template_args, template_param_names);
	}
	
	// For binary operators (&&, ||)
	if (constraint_expr.is<BinaryOperatorNode>()) {
		const auto& binop = constraint_expr.as<BinaryOperatorNode>();
		std::string_view op = binop.op();
		
		if (op == "&&") {
			// Conjunction - both must be satisfied
			auto left_result = evaluateConstraint(binop.get_lhs(), template_args, template_param_names);
			if (!left_result.satisfied) {
				return left_result;  // Return first failure
			}
			
			auto right_result = evaluateConstraint(binop.get_rhs(), template_args, template_param_names);
			if (!right_result.satisfied) {
				return right_result;
			}
			
			return ConstraintEvaluationResult::success();
		}
		else if (op == "||") {
			// Disjunction - at least one must be satisfied
			auto left_result = evaluateConstraint(binop.get_lhs(), template_args, template_param_names);
			if (left_result.satisfied) {
				return ConstraintEvaluationResult::success();
			}
			
			auto right_result = evaluateConstraint(binop.get_rhs(), template_args, template_param_names);
			if (right_result.satisfied) {
				return ConstraintEvaluationResult::success();
			}
			
			// Both failed
			return ConstraintEvaluationResult::failure(
				"constraint not satisfied: neither alternative of disjunction is satisfied",
				left_result.failed_requirement + " || " + right_result.failed_requirement,
				"ensure at least one of the constraints is met"
			);
		}
	}
	
	// For unary operators (!)
	if (constraint_expr.is<UnaryOperatorNode>()) {
		const auto& unop = constraint_expr.as<UnaryOperatorNode>();
		if (unop.op() == "!") {
			auto operand_result = evaluateConstraint(unop.get_operand(), template_args, template_param_names);
			if (operand_result.satisfied) {
				return ConstraintEvaluationResult::failure(
					"constraint not satisfied: negated constraint is true",
					"!" + operand_result.failed_requirement,
					"remove the negation or use a different constraint"
				);
			}
			return ConstraintEvaluationResult::success();
		}
	}
	
	// For requires expressions: requires { expr; ... } or requires(params) { expr; ... }
	if (constraint_expr.is<RequiresExpressionNode>()) {
		const auto& requires_expr = constraint_expr.as<RequiresExpressionNode>();
		// Evaluate each requirement in the requires expression
		// For now, we check if each requirement is a valid expression
		// This is a simplified check - full implementation would need to verify
		// that the expressions are well-formed with the substituted template arguments
		for (const auto& requirement : requires_expr.requirements()) {
			// Check different types of requirements
			if (requirement.is<CompoundRequirementNode>()) {
				// Compound requirement: { expression } -> Type
				// For now, assume satisfied - full implementation would check
				// that the expression is valid and the return type matches
				continue;
			}
			if (requirement.is<RequiresClauseNode>()) {
				// Nested requirement: requires constraint
				const auto& nested_req = requirement.as<RequiresClauseNode>();
				auto nested_result = evaluateConstraint(nested_req.constraint_expr(), template_args, template_param_names);
				if (!nested_result.satisfied) {
					return nested_result;
				}
				continue;
			}
			// Simple requirement: expression must be valid
			// For binary operator expressions like a + b, we need to check if the operation is valid for the type
			if (requirement.is<ExpressionNode>()) {
				// The expression parsing succeeded, so it's syntactically valid
				// For semantic validation, we would need to check if the types support the operations
				// For now, we consider it satisfied if parsing succeeded
				continue;
			}
			if (requirement.is<BinaryOperatorNode>()) {
				// Binary operation like a + b
				// For now, assume satisfied - full implementation would check
				// if the types support the operation
				continue;
			}
		}
		// All requirements satisfied
		return ConstraintEvaluationResult::success();
	}
	
	// Default: assume satisfied for unknown expressions
	// This allows templates to compile even with complex constraints
	return ConstraintEvaluationResult::success();
}
