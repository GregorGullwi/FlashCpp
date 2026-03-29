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
 * 2. TemplateTypeArg: For function template deduction and instantiation tracking
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
 * History:
 *   - Original: Duplicate TemplateTypeArg in TemplateRegistry.h and InstantiationQueue.h
 *   - Consolidation (Tasks 1-4): Unified into single TemplateTypeArg with TypeIndex support
 *   - See docs/TEMPLATE_ARGUMENT_CONSOLIDATION_PLAN.md for full details
 */

// Basic type+index+value triple for template arguments
// Provides a lightweight representation that can be reused across different contexts
// This is distinct from TypedValue (IRTypes.h) which is for IR-level runtime values
struct TemplateArgumentValue {
	TypeCategory type = TypeCategory::Invalid;
	TypeIndex type_index {};
	int64_t value = 0;
	
	// Factory methods
	static TemplateArgumentValue makeType(TypeCategory type, TypeIndex idx) {
		TemplateArgumentValue v;
		v.type = type;
		v.type_index = idx;
		return v;
	}
	
	static TemplateArgumentValue makeValue(int64_t val, TypeCategory value_type) {
		TemplateArgumentValue v;
		v.type = value_type;
		v.value = val;
		return v;
	}

	static TemplateArgumentValue makeValue(int64_t val) {
		return makeValue(val, TypeCategory::Int);
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
	TypeIndex type_index;  // Carries both the gTypeInfo slot and the TypeCategory
	ReferenceQualifier ref_qualifier;
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

	// --- Accessors ---
	// Returns the TypeCategory embedded in type_index.
	TypeCategory category() const noexcept { return type_index.category(); }
	// Returns the legacy Type enum value for APIs that still require it.
	Type typeEnum() const noexcept { return categoryToType(type_index.category()); }
	void setCategory(TypeCategory cat) noexcept { type_index.setCategory(cat); }
	
	bool is_reference() const { return ref_qualifier != ReferenceQualifier::None; }
	bool is_lvalue_reference() const { return ref_qualifier == ReferenceQualifier::LValueReference; }
	bool is_rvalue_reference() const { return ref_qualifier == ReferenceQualifier::RValueReference; }
	bool isTypeArgument() const { return !is_value && !is_template_template_arg; }

	// Helper: builds a TypeIndex carrying the provided category while preserving any
	// already-populated category on the incoming index.
	static TypeIndex makeTypeIndex(TypeCategory cat, TypeIndex idx) noexcept {
		return TypeIndex{ idx.index(), idx.category() != TypeCategory::Invalid ? idx.category() : cat };
	}

	TemplateTypeArg()
		: type_index(TypeIndex{0, TypeCategory::Invalid})
		, ref_qualifier(ReferenceQualifier::None)
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
		: type_index(makeTypeIndex(type_spec.category(), type_spec.type_index()))
		, ref_qualifier(type_spec.reference_qualifier())
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
	
	// Constructor for non-type template parameters with explicit type
	TemplateTypeArg(int64_t val, TypeIndex type_index)
		: type_index(type_index)
		, ref_qualifier(ReferenceQualifier::None)
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
	
	// Factory methods
	static TemplateTypeArg makeType(TypeCategory type, TypeIndex idx) {
		TemplateTypeArg arg;
		arg.type_index = makeTypeIndex(type, idx);
		return arg;
	}

	static TemplateTypeArg makeType(TypeIndex idx) {
		TemplateTypeArg arg;
		arg.type_index = idx;
		return arg;
	}
	
	static TemplateTypeArg makeTypeSpecifier(const TypeSpecifierNode& ts) {
		return TemplateTypeArg(ts);  // delegate to existing constructor
	}
	
	static TemplateTypeArg makeValue(int64_t v, TypeIndex type_index = TypeIndex{0, TypeCategory::Int}) {
		return TemplateTypeArg(v, type_index);
	}
	
	static TemplateTypeArg makeTemplate(StringHandle name) {
		TemplateTypeArg arg;
		arg.is_template_template_arg = true;
		arg.template_name_handle = name;
		return arg;
	}
	
	// Hash for use in maps (used by InstantiationQueue and SpecializationKey)
	size_t hash() const {
		// Normalize Bool/Int to Int to match operator== which treats them as interchangeable
		// for value parameters. This maintains the invariant: a == b → hash(a) == hash(b).
		TypeCategory effective_cat = (is_value && (category() == TypeCategory::Bool || category() == TypeCategory::Int))
		    ? TypeCategory::Int : category();
		size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(effective_cat));
		if (type_index.needsTypeIndex()) {
			h ^= std::hash<size_t>{}(type_index.index()) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(ref_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<size_t>{}(pointer_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(cv_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(is_array) + 0x9e3779b9 + (h << 6) + (h >> 2);
		if (array_size.has_value()) {
			h ^= std::hash<size_t>{}(*array_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(member_pointer_kind)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(is_value) + 0x9e3779b9 + (h << 6) + (h >> 2);
		if (is_value) {
			h ^= std::hash<int64_t>{}(value) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		h ^= std::hash<bool>{}(is_template_template_arg) + 0x9e3779b9 + (h << 6) + (h >> 2);
		if (is_template_template_arg) {
			h ^= std::hash<StringHandle>{}(template_name_handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}
	
	bool operator==(const TemplateTypeArg& other) const {
		// Only compare type_index for user-defined types (Struct, Enum, UserDefined)
		// For primitive types like int, float, etc., the type_index should be ignored
		bool type_index_match = true;
		if (type_index.needsTypeIndex()) {
			type_index_match = (type_index == other.type_index);
		}
		
		// NOTE: is_pack is intentionally NOT compared here.
		// The is_pack flag indicates whether this arg came from a pack expansion,
		// but for type matching purposes (specialization lookup, pattern matching),
		// is_pack should be ignored. For example, when looking up ns::sum<int>
		// from a pack expansion ns::sum<Args...> where Args=int, the lookup arg
		// has is_pack=true but should still match the specialization which has is_pack=false.
		
		// For non-type value parameters, Bool and Int are interchangeable (C++ allows bool as non-type template parameter)
		bool category_match = (category() == other.category());
		if (!category_match && is_value && other.is_value) {
			bool this_is_bool_or_int = (category() == TypeCategory::Bool || category() == TypeCategory::Int);
			bool other_is_bool_or_int = (other.category() == TypeCategory::Bool || other.category() == TypeCategory::Int);
			if (this_is_bool_or_int && other_is_bool_or_int) {
				category_match = true;
			}
		}
		
		return category_match &&
		       type_index_match &&
		       ref_qualifier == other.ref_qualifier &&
		       pointer_depth == other.pointer_depth &&
		       pointer_cv_qualifiers == other.pointer_cv_qualifiers &&
		       cv_qualifier == other.cv_qualifier &&
		       is_array == other.is_array &&
		       array_size == other.array_size &&
		       member_pointer_kind == other.member_pointer_kind &&
		       is_value == other.is_value &&
		       (!is_value || value == other.value) &&  // Only compare value if it's a value
		       is_template_template_arg == other.is_template_template_arg &&
		       (!is_template_template_arg || template_name_handle == other.template_name_handle);
	}

	// Helper method to check if this is a parameter pack
	bool isParameterPack() const {
		return is_pack;
	}
	
	// Get reference qualifier as enum
	ReferenceQualifier reference_qualifier() const {
		return ref_qualifier;
	}

	// Get string representation for mangling
	std::string toString() const {
		if (is_value) {
			// For boolean values, use "true" or "false" instead of "1" or "0"
			// This is important for template specialization matching
			if (category() == TypeCategory::Bool) {
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
			switch (category()) {
				case TypeCategory::Void: result += "void"; break;
				case TypeCategory::Int: result += "int"; break;
				case TypeCategory::Float: result += "float"; break;
				case TypeCategory::Double: result += "double"; break;
				case TypeCategory::Bool: result += "bool"; break;
				case TypeCategory::Char: result += "char"; break;
				case TypeCategory::Long: result += "long"; break;
				case TypeCategory::LongLong: result += "longlong"; break;
				case TypeCategory::Short: result += "short"; break;
				case TypeCategory::UnsignedInt: result += "uint"; break;
				case TypeCategory::UnsignedLong: result += "ulong"; break;
				case TypeCategory::UnsignedLongLong: result += "ulonglong"; break;
				case TypeCategory::UnsignedShort: result += "ushort"; break;
				case TypeCategory::UnsignedChar: result += "uchar"; break;
				case TypeCategory::UserDefined:
				case TypeCategory::TypeAlias:
				case TypeCategory::Struct:
				case TypeCategory::Enum:
					// For user-defined types, look up the name from gTypeInfo
					if (type_index.index() < getTypeInfoCount()) {
						result += StringTable::getStringView(getTypeInfo(type_index).name());
					} else {
						result += "?";
					}
					break;
				default: result += "?"; break;
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
		if (ref_qualifier == ReferenceQualifier::RValueReference) {
			result += "RR";  // rvalue reference
		} else if (ref_qualifier == ReferenceQualifier::LValueReference) {
			result += "R";   // lvalue reference
		}

		return result;
	}

	// Get hash-based string representation for mangling (unambiguous)
	// Uses the same hash algorithm as TemplateTypeArgHash for consistency
	std::string toHashString() const {
		// Compute hash using the same algorithm as TemplateTypeArgHash
		// Normalize Bool/Int to Int (matches operator== interchangeability for value parameters)
		TypeCategory effective_cat = (is_value && (category() == TypeCategory::Bool || category() == TypeCategory::Int))
		    ? TypeCategory::Int : category();
		size_t hash = std::hash<uint8_t>{}(static_cast<uint8_t>(effective_cat));
		if (type_index.needsTypeIndex()) {
			hash ^= std::hash<size_t>{}(type_index.index()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(ref_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
		hash ^= std::hash<bool>{}(is_template_template_arg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (is_template_template_arg) {
			hash ^= std::hash<StringHandle>{}(template_name_handle) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
		// Normalize Bool/Int to Int (matches operator== interchangeability for value parameters)
		TypeCategory effective_cat = (arg.is_value && (arg.category() == TypeCategory::Bool || arg.category() == TypeCategory::Int))
		    ? TypeCategory::Int : arg.category();
		size_t hash = std::hash<uint8_t>{}(static_cast<uint8_t>(effective_cat));
		// Only include type_index in hash for user-defined types (to match operator==)
		if (arg.type_index.needsTypeIndex()) {
			hash ^= std::hash<size_t>{}(arg.type_index.index()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		hash ^= std::hash<uint8_t>{}(static_cast<uint8_t>(arg.ref_qualifier)) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
		hash ^= std::hash<bool>{}(arg.is_template_template_arg) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		if (arg.is_template_template_arg) {
			hash ^= std::hash<StringHandle>{}(arg.template_name_handle) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
		}
		// NOTE: is_pack is intentionally NOT included in the hash to match operator==
		return hash;
	}
};

// Strip pattern modifiers from a concrete argument to recover the deduced type.
// Per C++ deduction rules: for pattern T*, T is deduced as int (not int*);
// for pattern T&, T is deduced as int (not int&); etc.
//
// @param concrete_arg  The fully instantiated type argument (e.g., int* for Container<int*>).
// @param pattern_arg   The pattern from the partial specialization (e.g., T* for Container<T*>).
// @return              The deduced template parameter type with pattern modifiers stripped.
//                      Example: deduceArgFromPattern(int*, T*) → int;
//                               deduceArgFromPattern(const int&, const T&) → int.
//
// Used by TemplateRegistry pattern matching (TemplateRegistry_Pattern.h) and
// deferred body re-parsing during pattern-based instantiation
// (Parser_Templates_Inst_ClassTemplate.cpp).
inline TemplateTypeArg deduceArgFromPattern(const TemplateTypeArg& concrete_arg, const TemplateTypeArg& pattern_arg) {
	TemplateTypeArg deduced = concrete_arg;
	if (pattern_arg.is_reference()) deduced.ref_qualifier = ReferenceQualifier::None;
	if (pattern_arg.pointer_depth > 0 && deduced.pointer_depth >= pattern_arg.pointer_depth) {
		deduced.pointer_depth -= pattern_arg.pointer_depth;
		// Strip the first pattern_arg.pointer_depth CV qualifiers by rebuilding the vector
		InlineVector<CVQualifier, 4> remaining;
		for (size_t pd = pattern_arg.pointer_depth; pd < deduced.pointer_cv_qualifiers.size(); ++pd) {
			remaining.push_back(deduced.pointer_cv_qualifiers[pd]);
		}
		deduced.pointer_cv_qualifiers = std::move(remaining);
	}
	if (pattern_arg.is_array) {
		deduced.is_array = false;
		deduced.array_size = std::nullopt;
	}
	// Strip cv_qualifier contributed by the pattern (e.g., const T → T=int, not T=const int)
	if (pattern_arg.cv_qualifier != CVQualifier::None) {
		deduced.cv_qualifier = static_cast<CVQualifier>(
			static_cast<uint8_t>(deduced.cv_qualifier) & ~static_cast<uint8_t>(pattern_arg.cv_qualifier));
	}
	return deduced;
}

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
	result.base_type = arg.category();
	result.cv_qualifier = arg.cv_qualifier;
	result.ref_qualifier = arg.reference_qualifier();
	result.pointer_depth = std::min(arg.pointer_depth, uint8_t(255));
	// Include array info - critical for differentiating T[] from T[N] from T
	result.is_array = arg.is_array;
	result.array_size = arg.array_size;
	return result;
}

/**
 * Create a TemplateInstantiationKey from template name and TemplateTypeArg vector
 */
inline TemplateInstantiationKey makeInstantiationKey(
	StringHandle template_name,
	const std::vector<TemplateTypeArg>& args) {
	
	TemplateInstantiationKey key(template_name);
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
	
	auto key = makeInstantiationKey(
		StringTable::getOrInternStringHandle(template_name), args);
	return generateInstantiatedName(template_name, key);
}

} // namespace FlashCpp
