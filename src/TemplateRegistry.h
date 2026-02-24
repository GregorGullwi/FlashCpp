#pragma once

#include "AstNodeTypes.h"
#include "ChunkedString.h"
#include "Lexer.h"  // For TokenPosition
#include "TemplateTypes.h"  // For TypeIndex-based template keys
#include "TemplateProfilingStats.h"  // For StringHandleHash
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <algorithm>

// SaveHandle type for parser save/restore operations
// Matches Parser::SaveHandle typedef in Parser.h
using SaveHandle = size_t;

// Transparent string hash for heterogeneous lookup (C++20)
// Allows unordered_map with StringHandle keys to lookup with string_view
// Use ONLY for maps that need heterogeneous lookup (string_view finding StringHandle keys)
struct TransparentStringHash {
	using is_transparent = void;
	using hash_type = std::hash<std::string_view>;
	
	size_t operator()(const char* str) const { return hash_type{}(str); }
	size_t operator()(std::string_view str) const { return hash_type{}(str); }
	size_t operator()(const std::string& str) const { return hash_type{}(str); }
	size_t operator()(StringHandle sh) const { 
		// Hash the string content, not the handle value, to enable heterogeneous lookup
		return hash_type{}(StringTable::getStringView(sh)); 
	}
};

// Transparent string equality for heterogeneous lookup
// Allows comparing StringHandle with string_view
struct TransparentStringEqual {
	using is_transparent = void;
	
	// StringHandle == StringHandle
	bool operator()(StringHandle lhs, StringHandle rhs) const {
		return lhs == rhs;
	}
	
	// StringHandle == string_view
	bool operator()(StringHandle lhs, std::string_view rhs) const {
		return lhs == rhs;
	}
	
	// string_view == StringHandle
	bool operator()(std::string_view lhs, StringHandle rhs) const {
		return rhs == lhs;
	}
	
	// string_view == string_view
	bool operator()(std::string_view lhs, std::string_view rhs) const {
		return lhs == rhs;
	}
	
	// std::string == anything
	bool operator()(const std::string& lhs, std::string_view rhs) const {
		return std::string_view(lhs) == rhs;
	}
	bool operator()(std::string_view lhs, const std::string& rhs) const {
		return lhs == std::string_view(rhs);
	}
	bool operator()(const std::string& lhs, const std::string& rhs) const {
		return lhs == rhs;
	}
	bool operator()(const std::string& lhs, StringHandle rhs) const {
		return rhs == std::string_view(lhs);
	}
	bool operator()(StringHandle lhs, const std::string& rhs) const {
		return lhs == std::string_view(rhs);
	}
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
	uint8_t pointer_depth;  // 0 = not pointer, 1 = T*, 2 = T**, etc.
	InlineVector<CVQualifier, 4> pointer_cv_qualifiers;  // CV for each pointer level
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
	
	// For template template parameters (e.g., template<typename...> class Op)
	bool is_template_template_arg;  // true if this is a template template argument
	StringHandle template_name_handle;  // name of the template (e.g., "HasType")
	
	TemplateTypeArg()
		: base_type(Type::Invalid)
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, pointer_cv_qualifiers()
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(false)
		, value(0)
		, is_pack(false)
		, is_dependent(false)
		, dependent_name()
		, is_template_template_arg(false)
		, template_name_handle() {}

	explicit TemplateTypeArg(const TypeSpecifierNode& type_spec)
		: base_type(type_spec.type())
		, type_index(type_spec.type_index())
		, is_reference(type_spec.is_reference())
		, is_rvalue_reference(type_spec.is_rvalue_reference())
		, pointer_depth(type_spec.pointer_depth())
		, pointer_cv_qualifiers()
		, cv_qualifier(type_spec.cv_qualifier())
		, is_array(type_spec.is_array())
		, array_size(type_spec.array_size())
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(false)
		, value(0)
		, is_pack(false)
		, is_dependent(false)
		, is_template_template_arg(false)
		, template_name_handle() {
		for (const auto& level : type_spec.pointer_levels()) {
			pointer_cv_qualifiers.push_back(level.cv_qualifier);
		}
	}

	// Constructor for non-type template parameters
	explicit TemplateTypeArg(int64_t val)
		: base_type(Type::Int)  // Default to int for values
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, pointer_cv_qualifiers()
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(true)
		, value(val)
		, is_pack(false)
		, is_dependent(false)
		, is_template_template_arg(false)
		, template_name_handle() {}
	
	// Constructor for non-type template parameters with explicit type
	TemplateTypeArg(int64_t val, Type type)
		: base_type(type)
		, type_index(0)
		, is_reference(false)
		, is_rvalue_reference(false)
		, pointer_depth(0)
		, pointer_cv_qualifiers()
		, cv_qualifier(CVQualifier::None)
		, is_array(false)
		, array_size(std::nullopt)
		, member_pointer_kind(MemberPointerKind::None)
		, is_value(true)
		, value(val)
		, is_pack(false)
		, is_dependent(false)
		, is_template_template_arg(false)
		, template_name_handle() {}
	
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
		
		// For non-type value parameters, Bool and Int are interchangeable (C++ allows bool as non-type template parameter)
		bool base_type_match = (base_type == other.base_type);
		if (!base_type_match && is_value && other.is_value) {
			bool this_is_bool_or_int = (base_type == Type::Bool || base_type == Type::Int);
			bool other_is_bool_or_int = (other.base_type == Type::Bool || other.base_type == Type::Int);
			if (this_is_bool_or_int && other_is_bool_or_int) {
				base_type_match = true;
			}
		}
		
		return base_type_match &&
		       type_index_match &&
		       is_reference == other.is_reference &&
		       is_rvalue_reference == other.is_rvalue_reference &&
		       pointer_depth == other.pointer_depth &&
		       pointer_cv_qualifiers == other.pointer_cv_qualifiers &&
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
	
	// Get reference qualifier as enum instead of bools
	ReferenceQualifier reference_qualifier() const {
		if (is_rvalue_reference) {
			return ReferenceQualifier::RValueReference;
		} else if (is_reference) {
			return ReferenceQualifier::LValueReference;
		}
		return ReferenceQualifier::None;
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

		// Add base type name - for dependent types, use dependent_name if available
		if (is_dependent && dependent_name.isValid()) {
			result += StringTable::getStringView(dependent_name);
		} else {
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

	// Get hash-based string representation for mangling (unambiguous)
	// Uses the same hash algorithm as TemplateTypeArgHash for consistency
	std::string toHashString() const {
		// Compute hash using the same algorithm as TemplateTypeArgHash
		size_t hash = std::hash<int>{}(static_cast<int>(base_type));
		if (base_type == Type::Struct || base_type == Type::Enum || base_type == Type::UserDefined) {
			hash ^= std::hash<size_t>{}(type_index) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<bool>{}(is_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(is_rvalue_reference) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<size_t>{}(pointer_depth) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(cv_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(is_array) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (array_size.has_value()) {
			hash ^= std::hash<size_t>{}(*array_size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(member_pointer_kind)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		hash ^= std::hash<bool>{}(is_value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (is_value) {
			hash ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		
		// Convert to hex string
		char buf[17];
		snprintf(buf, sizeof(buf), "%016zx", hash);
		return std::string(buf);
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

// ============================================================================
// Implementation of TemplateTypes.h helper functions
// ============================================================================

namespace FlashCpp {

/**
 * Create a TypeIndexArg from a TemplateTypeArg
 * 
 * This converts the rich TemplateTypeArg representation to the simpler
 * TypeIndexArg used for template instantiation lookup keys.
 */
inline TypeIndexArg makeTypeIndexArg(const TemplateTypeArg& arg) {
	TypeIndexArg result;
	result.type_index = arg.type_index;
	result.base_type = arg.base_type;  // Include base_type for primitive types
	result.cv_qualifier = arg.cv_qualifier;
	result.ref_qualifier = arg.reference_qualifier();
	result.pointer_depth = std::min(arg.pointer_depth, uint8_t(255));
	// Include array info - critical for differentiating T[] from T[N] from T
	result.is_array = arg.is_array;
	result.array_size = arg.array_size;
	return result;
}

/**
 * Create a TemplateInstantiationKeyV2 from template name and TemplateTypeArg vector
 */
inline TemplateInstantiationKeyV2 makeInstantiationKeyV2(
	StringHandle template_name,
	const std::vector<TemplateTypeArg>& args) {
	
	TemplateInstantiationKeyV2 key(template_name);
	key.type_args.reserve(args.size());
	
	for (const auto& arg : args) {
		if (arg.is_value) {
			// Non-type template argument
			key.value_args.push_back(arg.value);
		} else if (arg.is_template_template_arg) {
			// Template template argument
			key.template_template_args.push_back(arg.template_name_handle);
		} else {
			// Type template argument
			key.type_args.push_back(makeTypeIndexArg(arg));
		}
	}
	
	return key;
}

/**
 * Generate instantiated name from template name and arguments directly
 * 
 * This is a convenience function that builds the key internally and generates
 * an unambiguous hash-based name.
 * 
 * @param template_name The base template name (e.g., "is_arithmetic")
 * @param args The template arguments
 * @return A unique name like "is_arithmetic$a1b2c3d4"
 */
inline std::string_view generateInstantiatedNameFromArgs(
	std::string_view template_name,
	const std::vector<TemplateTypeArg>& args) {
	
	auto key = makeInstantiationKeyV2(
		StringTable::getOrInternStringHandle(template_name), args);
	return generateInstantiatedName(template_name, key);
}

} // namespace FlashCpp

// Template instantiation key - uniquely identifies a template instantiation
struct TemplateInstantiationKey {
	StringHandle template_name;
	InlineVector<Type> type_arguments;  // For type parameters (Type enum)
	InlineVector<TypeIndex> type_index_arguments;  // TypeIndex per type arg (differentiates struct types)
	InlineVector<int64_t> value_arguments;  // For non-type parameters
	InlineVector<StringHandle> template_arguments;  // For template template parameters
	
	bool operator==(const TemplateInstantiationKey& other) const {
		return template_name == other.template_name &&
		       type_arguments == other.type_arguments &&
		       type_index_arguments == other.type_index_arguments &&
		       value_arguments == other.value_arguments &&
		       template_arguments == other.template_arguments;
	}
};

// Hash function for TemplateInstantiationKey
struct TemplateInstantiationKeyHash {
	std::size_t operator()(const TemplateInstantiationKey& key) const {
		std::size_t hash = std::hash<StringHandle>{}(key.template_name);
		for (const auto& type : key.type_arguments) {
			hash ^= std::hash<int>{}(static_cast<int>(type)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& idx : key.type_index_arguments) {
			hash ^= std::hash<TypeIndex>{}(idx) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& value : key.value_arguments) {
			hash ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		for (const auto& tmpl : key.template_arguments) {
			hash ^= std::hash<StringHandle>{}(tmpl) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
	StringHandle template_name;  // For template template arguments (name of the template)
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
	
	static TemplateArgument makeTemplate(StringHandle template_name) {
		TemplateArgument arg;
		arg.kind = Kind::Template;
		arg.template_name = template_name;
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
			result.pointer_cv_qualifiers.reserve(ts.pointer_levels().size());
			for (const auto& level : ts.pointer_levels()) {
				result.pointer_cv_qualifiers.push_back(level.cv_qualifier);
			}
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
		if (!arg.pointer_cv_qualifiers.empty()) {
			for (const auto cv : arg.pointer_cv_qualifiers) {
				ts.add_pointer_level(cv);
			}
		} else {
			ts.add_pointer_levels(arg.pointer_depth);
		}
		
		// Set reference type
		ts.set_reference_qualifier(arg.reference_qualifier());
		
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
	// For nested templates (member function templates of class templates):
	// template<typename T> template<typename U> T Container<T>::convert(U u) { ... }
	// inner_template_params stores the inner template params (U), while template_params stores the outer (T)
	std::vector<ASTNode> inner_template_params;
	std::vector<StringHandle> inner_template_param_names;
	// Function specifiers from out-of-line definition (= default, = delete)
	bool is_defaulted = false;
	bool is_deleted = false;
};

// Outer template parameter bindings for member function templates of class templates.
// Stored when a TemplateFunctionDeclarationNode is copied during class template instantiation.
// Used during inner template instantiation to resolve outer template params (e.g., T→int).
struct OuterTemplateBinding {
	std::vector<StringHandle> param_names;  // Outer param names (e.g., ["T"])
	std::vector<TemplateTypeArg> param_args;  // Concrete types (e.g., [int])
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
	             std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>>& param_substitutions) const
	{
		FLASH_LOG(Templates, Trace, "      matches(): pattern has ", pattern_args.size(), " args, concrete has ", concrete_args.size(), " args");
		
		// Handle variadic templates: pattern may have fewer args if last template param is a pack
		// Check if the last template parameter is variadic (a pack)
		bool has_variadic_pack = false;
		[[maybe_unused]] size_t pack_param_index = 0;
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
					// For non-type value parameters, Bool and Int are interchangeable
					// (e.g., template<bool B> with default false stored as Bool vs Int)
					bool compatible_value_types = pattern_arg.is_value && concrete_arg.is_value &&
						((pattern_arg.base_type == Type::Bool && concrete_arg.base_type == Type::Int) ||
						 (pattern_arg.base_type == Type::Int && concrete_arg.base_type == Type::Bool));
					if (!compatible_value_types) {
						FLASH_LOG(Templates, Trace, "    FAILED: base types don't match");
						return false;
					}
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
			StringHandle param_name;
			bool found_param = false;
			
			if (pattern_arg.type_index > 0 && pattern_arg.type_index < gTypeInfo.size()) {
				const TypeInfo& param_type_info = gTypeInfo[pattern_arg.type_index];
				param_name = param_type_info.name();
				found_param = true;
				FLASH_LOG(Templates, Trace, "  Found parameter name '", StringTable::getStringView(param_name), "' from pattern_arg.type_index=", pattern_arg.type_index);
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
					param_name = template_param.nameHandle();
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
					FLASH_LOG(Templates, Trace, "  FAILED: Inconsistent substitution for parameter ", StringTable::getStringView(param_name));
					return false;  // Inconsistent substitution (different base types)
				}
				FLASH_LOG(Templates, Trace, "  SUCCESS: Reused parameter ", StringTable::getStringView(param_name), " - consistency check passed");
				// Don't increment param_index - we reused an existing parameter binding
			} else {
				// Bind this parameter to the concrete type
				param_substitutions[param_name] = concrete_arg;
				FLASH_LOG(Templates, Trace, "  SUCCESS: Bound parameter ", StringTable::getStringView(param_name), " to concrete type");
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
		registerTemplate(StringTable::getOrInternStringHandle(name), template_node);
	}

	void registerTemplate(StringHandle name, ASTNode template_node) {
		templates_[name].push_back(template_node);
		// Track class template names separately so callers can ask "is this name a class
		// template?" without matching unrelated function templates that share the same
		// unqualified name.
		if (template_node.is<TemplateClassDeclarationNode>()) {
			class_template_names_.insert(name);
		}
	}

	// Returns true if 'name' (exact StringHandle) was registered as a class template.
	// Used in codegen to skip uninstantiated class template pattern structs in
	// gTypesByName without accidentally skipping non-template structs that share an
	// unqualified name with a template in a different namespace.
	bool isClassTemplate(StringHandle name) const {
		return class_template_names_.count(name) > 0;
	}

	// Register a template using QualifiedIdentifier (Phase 2).
	// Stores under the unqualified name for backward-compatible lookups.
	// If the identifier has a non-global namespace, also stores under the
	// fully-qualified name (e.g. "std::vector") so that namespace-qualified
	// lookups work without manual dual registration by the caller.
	void registerTemplate(QualifiedIdentifier qi, ASTNode template_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerTemplate(name, template_node);
		});
	}

	// Register template parameter names for a template
	void registerTemplateParameters(StringHandle key, const std::vector<StringHandle>& param_names) {
		template_parameters_[key] = std::vector<StringHandle>(param_names.begin(), param_names.end());
	}

	// Register an alias template: template<typename T> using Ptr = T*;
	void register_alias_template(std::string_view name, ASTNode alias_node) {
		StringHandle key = StringTable::getOrInternStringHandle(name);
		alias_templates_[key] = alias_node;
	}

	void register_alias_template(StringHandle name, ASTNode alias_node) {
		alias_templates_[name] = alias_node;
	}

	// Register an alias template using QualifiedIdentifier (Phase 2).
	void register_alias_template(QualifiedIdentifier qi, ASTNode alias_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			register_alias_template(name, alias_node);
		});
	}

	// Register a variable template: template<typename T> constexpr T pi = T(3.14159...);
	void registerVariableTemplate(std::string_view name, ASTNode variable_template_node) {
		StringHandle key = StringTable::getOrInternStringHandle(name);
		variable_templates_[key] = variable_template_node;
	}

	void registerVariableTemplate(StringHandle name, ASTNode variable_template_node) {
		variable_templates_[name] = variable_template_node;
	}

	// Register a variable template using QualifiedIdentifier (Phase 2).
	void registerVariableTemplate(QualifiedIdentifier qi, ASTNode variable_template_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerVariableTemplate(name, variable_template_node);
		});
	}

	// Look up a variable template by name
	std::optional<ASTNode> lookupVariableTemplate(std::string_view name) const {
		return lookupVariableTemplate(StringTable::getOrInternStringHandle(name));
	}

	std::optional<ASTNode> lookupVariableTemplate(StringHandle name) const {
		auto it = variable_templates_.find(name);
		if (it != variable_templates_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Look up an alias template by name
	std::optional<ASTNode> lookup_alias_template(std::string_view name) const {
		return lookup_alias_template(StringTable::getOrInternStringHandle(name));
	}

	std::optional<ASTNode> lookup_alias_template(StringHandle name) const {
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
		for (const auto& [name_handle, node] : alias_templates_) {
			std::string_view name = StringTable::getStringView(name_handle);
			if (name.starts_with(prefix)) {
				result.push_back(name);
			}
		}
		return result;
	}

	// Register a deduction guide: template<typename T> ClassName(T) -> ClassName<T>;
	void register_deduction_guide(std::string_view class_name, ASTNode guide_node) {
		register_deduction_guide(StringTable::getOrInternStringHandle(class_name), guide_node);
	}

	void register_deduction_guide(StringHandle class_name, ASTNode guide_node) {
		deduction_guides_[class_name].push_back(guide_node);
	}

	// Look up deduction guides for a class template
	std::vector<ASTNode> lookup_deduction_guides(std::string_view class_name) const {
		return lookup_deduction_guides(StringTable::getOrInternStringHandle(class_name));
	}

	std::vector<ASTNode> lookup_deduction_guides(StringHandle class_name) const {
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
	// If multiple overloads exist, returns the first one registered
	// For all overloads, use lookupAllTemplates()
	std::optional<ASTNode> lookupTemplate(std::string_view name) const {
		return lookupTemplate(StringTable::getOrInternStringHandle(name));
	}
	
	std::optional<ASTNode> lookupTemplate(StringHandle name) const {
		auto it = templates_.find(name);
		if (it != templates_.end() && !it->second.empty()) {
			return it->second.front();
		}
		return std::nullopt;
	}

	// Look up a template using QualifiedIdentifier (Phase 2).
	// Tries the qualified name first, then falls back to unqualified.
	std::optional<ASTNode> lookupTemplate(QualifiedIdentifier qi) const {
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			auto result = lookupTemplate(qualified);
			if (result.has_value()) return result;
		}
		return lookupTemplate(qi.identifier_handle);
	}

	// Look up all template overloads for a given name
	const std::vector<ASTNode>* lookupAllTemplates(std::string_view name) const {
		return lookupAllTemplates(StringTable::getOrInternStringHandle(name));
	}

	const std::vector<ASTNode>* lookupAllTemplates(StringHandle name) const {
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
		for (const auto& [name_handle, _] : templates_) {
			result.push_back(StringTable::getStringView(name_handle));
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
	
	// ============================================================================
	// V2 TypeIndex-based template instantiation API
	// ============================================================================
	
	// Get an existing instantiation using V2 key
	std::optional<ASTNode> getInstantiationV2(const FlashCpp::TemplateInstantiationKeyV2& key) const {
		auto it = instantiations_v2_.find(key);
		if (it != instantiations_v2_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Register a new instantiation using V2 key
	void registerInstantiationV2(const FlashCpp::TemplateInstantiationKeyV2& key, ASTNode instantiated_node) {
		instantiations_v2_[key] = instantiated_node;
	}
	
	// Convenience method: register instantiation using template name and args
	void registerInstantiationV2(StringHandle template_name, 
	                              const std::vector<TemplateTypeArg>& args,
	                              ASTNode instantiated_node) {
		auto key = FlashCpp::makeInstantiationKeyV2(template_name, args);
		instantiations_v2_[key] = instantiated_node;
	}
	
	// Convenience method: lookup instantiation using template name and args
	std::optional<ASTNode> getInstantiationV2(StringHandle template_name,
	                                           const std::vector<TemplateTypeArg>& args) const {
		auto key = FlashCpp::makeInstantiationKeyV2(template_name, args);
		return getInstantiationV2(key);
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

	// Generate a mangled name for a template instantiation using hash-based naming
	// Example: max<int> -> max$a1b2c3d4, max<int, 5> -> max$e5f6g7h8
	// This avoids collisions from underscore-based naming (e.g., type names with underscores)
	static std::string_view mangleTemplateName(std::string_view base_name, const std::vector<TemplateArgument>& args) {
		// Convert TemplateArgument to TemplateTypeArg for hash-based naming
		std::vector<TemplateTypeArg> type_args;
		type_args.reserve(args.size());
		
		for (const auto& arg : args) {
			TemplateTypeArg ta;
			if (arg.kind == TemplateArgument::Kind::Type) {
				ta.base_type = arg.type_value;
				ta.type_index = arg.type_index;
				if (arg.type_specifier.has_value()) {
					const auto& ts = *arg.type_specifier;
					ta.is_reference = ts.is_reference();
					ta.is_rvalue_reference = ts.is_rvalue_reference();
					ta.cv_qualifier = ts.cv_qualifier();
					ta.pointer_depth = static_cast<uint8_t>(ts.pointer_levels().size());
				}
			} else if (arg.kind == TemplateArgument::Kind::Value) {
				ta.is_value = true;
				ta.value = arg.int_value;
				ta.base_type = arg.value_type;
			} else if (arg.kind == TemplateArgument::Kind::Template) {
				// For template template arguments, mark as template template arg
				ta.is_template_template_arg = true;
				ta.template_name_handle = arg.template_name;
			}
			type_args.push_back(ta);
		}
		
		return FlashCpp::generateInstantiatedNameFromArgs(base_name, type_args);
	}

	// Register an out-of-line template member function definition (StringHandle overload)
	void registerOutOfLineMember(StringHandle class_name, OutOfLineMemberFunction member_func) {
		out_of_line_members_[class_name].push_back(std::move(member_func));
	}

	// Register an out-of-line template member function definition (string_view overload)
	void registerOutOfLineMember(std::string_view class_name, OutOfLineMemberFunction member_func) {
		StringHandle key = StringTable::getOrInternStringHandle(class_name);
		registerOutOfLineMember(key, std::move(member_func));
	}

	// Get out-of-line member functions for a class (StringHandle overload)
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(StringHandle class_name) const {
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Get out-of-line member functions for a class (string_view overload)
	std::vector<OutOfLineMemberFunction> getOutOfLineMemberFunctions(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_members_.find(class_name);
		if (it != out_of_line_members_.end()) {
			return it->second;
		}
		return {};
	}

	// Register an out-of-line template static member variable definition (StringHandle overload)
	void registerOutOfLineMemberVariable(StringHandle class_name, OutOfLineMemberVariable member_var) {
		out_of_line_variables_[class_name].push_back(std::move(member_var));
	}

	// Register an out-of-line template static member variable definition (string_view overload)
	void registerOutOfLineMemberVariable(std::string_view class_name, OutOfLineMemberVariable member_var) {
		StringHandle key = StringTable::getOrInternStringHandle(class_name);
		registerOutOfLineMemberVariable(key, std::move(member_var));
	}

	// Get out-of-line member variables for a class (StringHandle overload)
	std::vector<OutOfLineMemberVariable> getOutOfLineMemberVariables(StringHandle class_name) const {
		auto it = out_of_line_variables_.find(class_name);
		if (it != out_of_line_variables_.end()) {
			return it->second;
		}
		return {};
	}

	// Get out-of-line member variables for a class (string_view overload)
	std::vector<OutOfLineMemberVariable> getOutOfLineMemberVariables(std::string_view class_name) const {
		// Heterogeneous lookup - string_view accepted directly
		auto it = out_of_line_variables_.find(class_name);
		if (it != out_of_line_variables_.end()) {
			return it->second;
		}
		return {};
	}

	// Register outer template parameter bindings for a member function template
	// of an instantiated class template (e.g., Container<int>::convert has T→int)
	void registerOuterTemplateBinding(std::string_view qualified_name, OuterTemplateBinding binding) {
		registerOuterTemplateBinding(StringTable::getOrInternStringHandle(qualified_name), std::move(binding));
	}

	void registerOuterTemplateBinding(StringHandle qualified_name, OuterTemplateBinding binding) {
		outer_template_bindings_[qualified_name] = std::move(binding);
	}

	// Get outer template parameter bindings for a member function template
	const OuterTemplateBinding* getOuterTemplateBinding(std::string_view qualified_name) const {
		return getOuterTemplateBinding(StringTable::getOrInternStringHandle(qualified_name));
	}

	const OuterTemplateBinding* getOuterTemplateBinding(StringHandle qualified_name) const {
		auto it = outer_template_bindings_.find(qualified_name);
		if (it != outer_template_bindings_.end()) {
			return &it->second;
		}
		return nullptr;
	}

	// Register a template specialization pattern (StringHandle overload)
	void registerSpecializationPattern(StringHandle template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		FLASH_LOG(Templates, Debug, "registerSpecializationPattern: template_name='", StringTable::getStringView(template_name), 
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
		
		specialization_patterns_[template_name].push_back(std::move(pattern));
		FLASH_LOG(Templates, Debug, "  Total patterns for '", StringTable::getStringView(template_name), "': ", specialization_patterns_[template_name].size());
		if (pattern.sfinae_condition.has_value()) {
			// Note: pattern has been moved, we need to access the stored one
			const auto& stored_pattern = specialization_patterns_[template_name].back();
			if (stored_pattern.sfinae_condition.has_value()) {
				FLASH_LOG(Templates, Debug, "  SFINAE condition set: check param[", stored_pattern.sfinae_condition->template_param_index, 
				          "]::", StringTable::getStringView(stored_pattern.sfinae_condition->member_name));
			}
		}
	}

	// Register a template specialization pattern (string_view overload)
	void registerSpecializationPattern(std::string_view template_name, 
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args, 
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		StringHandle key = StringTable::getOrInternStringHandle(template_name);
		registerSpecializationPattern(key, template_params, pattern_args, specialized_node, sfinae_cond);
	}

	// Register a template specialization pattern using QualifiedIdentifier (Phase 4).
	void registerSpecializationPattern(QualifiedIdentifier qi,
	                                   const std::vector<ASTNode>& template_params,
	                                   const std::vector<TemplateTypeArg>& pattern_args,
	                                   ASTNode specialized_node,
	                                   std::optional<SfinaeCondition> sfinae_cond = std::nullopt) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerSpecializationPattern(name, template_params, pattern_args, specialized_node, sfinae_cond);
		});
	}

	// Register a template specialization (exact match)
	void registerSpecialization(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		SpecializationKey key{std::string(template_name), template_args};
		specializations_[key] = specialized_node;
		FLASH_LOG(Templates, Debug, "registerSpecialization: '", template_name, "' with ", template_args.size(), " args");
	}

	// Register a template specialization using QualifiedIdentifier (Phase 4).
	void registerSpecialization(QualifiedIdentifier qi, const std::vector<TemplateTypeArg>& template_args, ASTNode specialized_node) {
		forEachQualifiedName(qi, [&](std::string_view name) {
			registerSpecialization(name, template_args, specialized_node);
		});
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

	// Look up a template specialization using QualifiedIdentifier (Phase 4).
	// Tries qualified name first, then falls back to unqualified.
	std::optional<ASTNode> lookupSpecialization(QualifiedIdentifier qi, const std::vector<TemplateTypeArg>& template_args) const {
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			auto result = lookupSpecialization(StringTable::getStringView(qualified), template_args);
			if (result.has_value()) return result;
		}
		return lookupSpecialization(StringTable::getStringView(qi.identifier_handle), template_args);
	}
	
	// Find a matching specialization pattern (StringHandle overload)
	std::optional<ASTNode> matchSpecializationPattern(StringHandle template_name, 
	                                                  const std::vector<TemplateTypeArg>& concrete_args) const {
		auto patterns_it = specialization_patterns_.find(template_name);
		if (patterns_it == specialization_patterns_.end()) {
			FLASH_LOG(Templates, Debug, "    No patterns registered for template '", StringTable::getStringView(template_name), "'");
			return std::nullopt;  // No patterns for this template
		}
		
		const std::vector<TemplatePattern>& patterns = patterns_it->second;
		FLASH_LOG(Templates, Debug, "    Found ", patterns.size(), " pattern(s) for template '", StringTable::getStringView(template_name), "'");
		
		const TemplatePattern* best_match = nullptr;
		int best_specificity = -1;
		
		// Find the most specific matching pattern
		for (size_t i = 0; i < patterns.size(); ++i) {
			const auto& pattern = patterns[i];
			FLASH_LOG(Templates, Debug, "    Checking pattern #", i, " (specificity=", pattern.specificity(), ")");
			std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> substitutions;
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

	// Find a matching specialization pattern (string_view overload)
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
			std::unordered_map<StringHandle, TemplateTypeArg, StringHandleHash, std::equal_to<>> substitutions;
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
		instantiations_v2_.clear();
		out_of_line_variables_.clear();
		out_of_line_members_.clear();
		specializations_.clear();
		specialization_patterns_.clear();
		alias_templates_.clear();
		variable_templates_.clear();
		deduction_guides_.clear();
		instantiation_to_pattern_.clear();
		class_template_names_.clear();
		outer_template_bindings_.clear();
	}

	// Public access to specialization patterns for pattern matching in Parser
	std::unordered_map<StringHandle, std::vector<TemplatePattern>, TransparentStringHash, TransparentStringEqual> specialization_patterns_;
	
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
	// Helper: Given a QualifiedIdentifier, call `fn` with both the unqualified name
	// and (if the identifier has a non-global namespace) the fully-qualified name.
	// Used by all QualifiedIdentifier registration overloads to eliminate duplication.
	template<typename Fn>
	void forEachQualifiedName(QualifiedIdentifier qi, Fn&& fn) {
		std::string_view simple = StringTable::getStringView(qi.identifier_handle);
		fn(simple);
		if (qi.hasNamespace()) {
			StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
				qi.namespace_handle, qi.identifier_handle);
			std::string_view qualified_name = StringTable::getStringView(qualified);
			if (qualified_name != simple) {
				fn(qualified_name);
			}
		}
	}

	// Map from template name to template declaration nodes (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<ASTNode>, StringHandleHash, std::equal_to<>> templates_;

	// Map from template name to template parameter names (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<StringHandle>, StringHandleHash, std::equal_to<>> template_parameters_;

	// Map from alias template name to TemplateAliasNode (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, ASTNode, StringHandleHash, std::equal_to<>> alias_templates_;

	// Map from variable template name to TemplateVariableDeclarationNode (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, ASTNode, StringHandleHash, std::equal_to<>> variable_templates_;

	// Map from class template name to deduction guides (StringHandle key for fast lookup)
	std::unordered_map<StringHandle, std::vector<ASTNode>, StringHandleHash, std::equal_to<>> deduction_guides_;

	// Map from instantiation key to instantiated function node
	std::unordered_map<TemplateInstantiationKey, ASTNode, TemplateInstantiationKeyHash> instantiations_;
	
	// V2: TypeIndex-based template instantiation cache (replaces string-based keys)
	// This provides O(1) lookup without string concatenation and avoids ambiguity
	// when type names contain underscores
	std::unordered_map<FlashCpp::TemplateInstantiationKeyV2, ASTNode, FlashCpp::TemplateInstantiationKeyV2Hash> instantiations_v2_;

	// Map from class name to out-of-line member function definitions (StringHandle key for efficient lookup)
	std::unordered_map<StringHandle, std::vector<OutOfLineMemberFunction>, TransparentStringHash, TransparentStringEqual> out_of_line_members_;

	// Map from class name to out-of-line static member variable definitions (StringHandle key for efficient lookup)
	std::unordered_map<StringHandle, std::vector<OutOfLineMemberVariable>, TransparentStringHash, TransparentStringEqual> out_of_line_variables_;

	// Map from qualified member function template name (e.g., "Container$hash::convert") to
	// outer template parameter bindings (e.g., T→int). Used during nested template instantiation.
	std::unordered_map<StringHandle, OuterTemplateBinding, StringHandleHash, std::equal_to<>> outer_template_bindings_;

	// Map from (template_name, template_args) to specialized class node (exact matches)
	std::unordered_map<SpecializationKey, ASTNode, SpecializationKeyHash> specializations_;
	
	// Map from instantiated struct name to the pattern struct name used (for partial specializations)
	// Example: "Wrapper_int_0" -> "Wrapper_pattern__"
	// This allows looking up member aliases from the correct specialization
	std::unordered_map<StringHandle, StringHandle, StringHandleHash, std::equal_to<>> instantiation_to_pattern_;

	// Set of StringHandles that were registered as class templates (TemplateClassDeclarationNode).
	// Used by isClassTemplate() for O(1) exact-name lookup, avoiding substring searches
	// and false positives from unqualified-name fallbacks in lookupTemplate().
	std::unordered_set<StringHandle, StringHandleHash> class_template_names_;
};

// Global template registry
extern TemplateRegistry gTemplateRegistry;

// ============================================================================
// Lazy Template Member Function Instantiation Registry
// ============================================================================

// Information needed to instantiate a template member function on-demand

// Lazy registries and ConceptRegistry split out for maintainability.
#include "TemplateRegistry_Lazy.cpp"  // LazyMemberInstantiationRegistry, LazyClassInstantiationRegistry, ConceptRegistry, etc.
