#pragma once

/**
 * TemplateTypes.h - Core Template Type System
 * ============================================
 * 
 * This file contains the fundamental types for template instantiation lookup
 * using TypeIndex-based keys instead of string-based keys.
 * 
 * ## Key Design Decisions
 * 
 * 1. **TypeIndex-based Keys**: Template instantiation keys use TypeIndex (an index
 *    into gTypeInfo) instead of type name strings. This prevents ambiguity when
 *    type names contain underscores (e.g., "is_arithmetic_int" vs "is_arithmetic" + "_int").
 * 
 * 2. **InlineVector for Efficiency**: Most templates have 1-4 arguments. Using inline
 *    storage avoids heap allocation in ~95% of cases.
 * 
 * 3. **Separate Type/Value/Template Arguments**: Template arguments are categorized
 *    by their kind (type, non-type value, or template template parameter) for
 *    correct hashing and comparison.
 * 
 * ## Usage
 * 
 * ```cpp
 * // Building a template instantiation key
 * TemplateInstantiationKey key;
 * key.base_template = getTypeIndex("vector");  // TypeIndex of the template
 * key.type_args.push_back(TypeIndex(42));      // TypeIndex for "int"
 * 
 * // Looking up instantiation
 * auto it = gTemplateInstantiations.find(key);
 * ```
 */

#include "AstNodeTypes.h"  // For Type, TypeIndex
#include "StringTable.h"   // For StringHandle
#include "InlineVector.h"  // For InlineVector
#include <array>
#include <vector>
#include <cstdint>
#include <functional>  // For std::hash

namespace FlashCpp {

// ============================================================================
// TypeIndexArg - A template type argument represented by TypeIndex
// ============================================================================

/**
 * TypeIndexArg - Represents a type template argument using TypeIndex
 * 
 * This is a simpler representation than TemplateTypeArg, focused purely on
 * identity for lookup purposes. The full type information (references, 
 * pointers, cv-qualifiers) is encoded in the TypeIndex itself.
 * 
 * NOTE: For primitive types (int, float, etc.), type_index may be 0, so we 
 * also store base_type to ensure unique hashes for different primitive types.
 */
struct TypeIndexArg {
	TypeIndex type_index = 0;
	Type base_type = Type::Invalid;  // Needed for primitive types where type_index is 0
	
	// CV-qualifiers and reference info that affect template identity
	// These are stored separately because the same TypeIndex with different
	// qualifiers represents different template arguments (e.g., int vs const int&)
	CVQualifier cv_qualifier = CVQualifier::None;
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
	uint8_t pointer_depth = 0;
	
	// Array information - critical for differentiating T[], T[N], and T
	bool is_array = false;
	std::optional<size_t> array_size;  // nullopt for T[], value for T[N]
	
	TypeIndexArg() = default;
	
	explicit TypeIndexArg(TypeIndex idx) : type_index(idx) {}
	
	TypeIndexArg(TypeIndex idx, Type type, CVQualifier cv, ReferenceQualifier ref, uint8_t ptr_depth)
		: type_index(idx)
		, base_type(type)
		, cv_qualifier(cv)
		, ref_qualifier(ref)
		, pointer_depth(ptr_depth) {}
	
	bool operator==(const TypeIndexArg& other) const {
		return type_index == other.type_index &&
		       base_type == other.base_type &&
		       cv_qualifier == other.cv_qualifier &&
		       ref_qualifier == other.ref_qualifier &&
		       pointer_depth == other.pointer_depth &&
		       is_array == other.is_array &&
		       array_size == other.array_size;
	}
	
	bool operator!=(const TypeIndexArg& other) const {
		return !(*this == other);
	}
	
	size_t hash() const {
		size_t h = std::hash<TypeIndex>{}(type_index);
		// Include base_type in hash to differentiate primitive types with type_index=0
		h ^= std::hash<int>{}(static_cast<int>(base_type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(cv_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(ref_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(pointer_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
		// Include array info in hash - critical for differentiating T[] from T[N] from T
		h ^= std::hash<bool>{}(is_array) + 0x9e3779b9 + (h << 6) + (h >> 2);
		if (array_size.has_value()) {
			h ^= std::hash<size_t>{}(*array_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}
};

// ============================================================================
// TemplateInstantiationKeyV2 - TypeIndex-based template instantiation key
// ============================================================================

/**
 * TemplateInstantiationKeyV2 - A template instantiation key using TypeIndex
 * 
 * This replaces string-based template instantiation keys with TypeIndex-based
 * keys. The key components are:
 * 
 * 1. base_template: TypeIndex of the template being instantiated (e.g., "vector")
 * 2. type_args: TypeIndex values for type template parameters
 * 3. value_args: int64_t values for non-type template parameters
 * 4. template_args: StringHandle for template template parameters
 * 
 * ## Why TypeIndex instead of strings?
 * 
 * String-based keys like "vector_int" are ambiguous:
 * - Is it "vector" with arg "int"?
 * - Or "vector_int" with no args?
 * - Or "vector_i" with arg "nt"?
 * 
 * TypeIndex-based keys are unambiguous because TypeIndex is assigned uniquely
 * to each type during parsing.
 */
struct TemplateInstantiationKeyV2 {
	StringHandle base_template;                           // Template name handle
	InlineVector<TypeIndexArg, 4> type_args;              // Type arguments
	InlineVector<int64_t, 4> value_args;                  // Non-type arguments
	InlineVector<StringHandle, 2> template_template_args; // Template template args
	
	TemplateInstantiationKeyV2() = default;
	
	explicit TemplateInstantiationKeyV2(StringHandle template_name)
		: base_template(template_name) {}
	
	bool operator==(const TemplateInstantiationKeyV2& other) const {
		return base_template == other.base_template &&
		       type_args == other.type_args &&
		       value_args == other.value_args &&
		       template_template_args == other.template_template_args;
	}
	
	bool operator!=(const TemplateInstantiationKeyV2& other) const {
		return !(*this == other);
	}
	
	// Check if the key is empty (no template specified)
	[[nodiscard]] bool empty() const {
		return base_template.handle == 0;
	}
	
	// Clear the key
	void clear() {
		base_template = StringHandle{};
		type_args.clear();
		value_args.clear();
		template_template_args.clear();
	}
};

/**
 * Hash function for TemplateInstantiationKeyV2
 */
struct TemplateInstantiationKeyV2Hash {
	size_t operator()(const TemplateInstantiationKeyV2& key) const {
		size_t h = std::hash<uint32_t>{}(key.base_template.handle);
		
		// Hash type arguments
		for (const auto& arg : key.type_args) {
			h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		// Hash value arguments
		for (const auto& val : key.value_args) {
			h ^= std::hash<int64_t>{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		// Hash template template arguments
		for (const auto& tmpl : key.template_template_args) {
			h ^= std::hash<uint32_t>{}(tmpl.handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		return h;
	}
};

// ============================================================================
// FunctionSignatureKey - TypeIndex-based function signature for caching
// ============================================================================

/**
 * FunctionSignatureKey - A function signature key using TypeIndex
 * 
 * This represents a function signature using TypeIndex values instead of
 * type names or TypeSpecifierNode comparisons. Used for:
 * - Caching function lookup results
 * - Fast signature comparison during overload resolution
 * 
 * The key includes:
 * - function_name: StringHandle of the function name
 * - param_types: TypeIndex values for each parameter type
 * - param_qualifiers: CV and reference qualifiers for each parameter
 */
struct FunctionSignatureKey {
	StringHandle function_name;                   // Function name handle
	InlineVector<TypeIndexArg, 8> param_types;    // Parameter types (8 inline for common cases)
	
	FunctionSignatureKey() = default;
	
	explicit FunctionSignatureKey(StringHandle name)
		: function_name(name) {}
	
	bool operator==(const FunctionSignatureKey& other) const {
		return function_name == other.function_name &&
		       param_types == other.param_types;
	}
	
	bool operator!=(const FunctionSignatureKey& other) const {
		return !(*this == other);
	}
	
	[[nodiscard]] bool empty() const {
		return function_name.handle == 0;
	}
	
	void clear() {
		function_name = StringHandle{};
		param_types.clear();
	}
};

/**
 * Hash function for FunctionSignatureKey
 */
struct FunctionSignatureKeyHash {
	size_t operator()(const FunctionSignatureKey& key) const {
		size_t h = std::hash<uint32_t>{}(key.function_name.handle);
		
		// Hash parameter types
		for (const auto& param : key.param_types) {
			h ^= param.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		return h;
	}
};

/**
 * Generate a unique, unambiguous name for a template instantiation
 * 
 * Instead of building names like "is_arithmetic_int" (which is ambiguous with
 * types containing underscores), this generates names using a hash of the 
 * TypeIndex values: "is_arithmetic$12345678" where 12345678 is a hex hash.
 * 
 * Benefits:
 * - Unambiguous: No confusion with types containing underscores
 * - Consistent: Same arguments always produce same name
 * - Fast: Hash-based generation avoids string manipulation
 * 
 * @param template_name The base template name (e.g., "is_arithmetic")
 * @param key The instantiation key with type arguments
 * @return A unique, human-readable name like "is_arithmetic$a1b2c3d4"
 */
inline std::string_view generateInstantiatedName(std::string_view template_name, 
                                                  const TemplateInstantiationKeyV2& key) {
	// Compute the hash of the template arguments
	size_t h = 0;
	for (const auto& arg : key.type_args) {
		h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	for (const auto& arg : key.value_args) {
		h ^= std::hash<int64_t>{}(arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	for (const auto& arg : key.template_template_args) {
		h ^= std::hash<uint32_t>{}(arg.handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	
	// Build the name: template_name$hash (using $ as unambiguous separator)
	// Use lowercase hex for compactness
	static const char hex_chars[] = "0123456789abcdef";
	char hash_str[17];  // 16 hex chars + null terminator
	for (int i = 15; i >= 0; --i) {
		hash_str[i] = hex_chars[h & 0xF];
		h >>= 4;
	}
	hash_str[16] = '\0';
	
	StringBuilder builder;
	builder.append(template_name);
	builder.append("$");  // $ is not valid in C++ identifiers, so unambiguous
	builder.append(std::string_view(hash_str, 16));
	
	return builder.commit();
}

// ============================================================================
// Helper functions for building template keys
// ============================================================================
// NOTE: These functions are implemented in TemplateRegistry.h after the 
// TemplateTypeArg definition to avoid circular dependencies.
// 
// Available functions:
//   - FlashCpp::makeTypeIndexArg(const TemplateTypeArg& arg) -> TypeIndexArg
//   - FlashCpp::makeInstantiationKeyV2(StringHandle, const std::vector<TemplateTypeArg>&) -> TemplateInstantiationKeyV2

} // namespace FlashCpp

// Provide std::hash specialization for use with unordered_map
namespace std {
	template<>
	struct hash<FlashCpp::TemplateInstantiationKeyV2> {
		size_t operator()(const FlashCpp::TemplateInstantiationKeyV2& key) const {
			return FlashCpp::TemplateInstantiationKeyV2Hash{}(key);
		}
	};
	
	template<>
	struct hash<FlashCpp::TypeIndexArg> {
		size_t operator()(const FlashCpp::TypeIndexArg& arg) const {
			return arg.hash();
		}
	};
	
	template<>
	struct hash<FlashCpp::FunctionSignatureKey> {
		size_t operator()(const FlashCpp::FunctionSignatureKey& key) const {
			return FlashCpp::FunctionSignatureKeyHash{}(key);
		}
	};
}
