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
 *    into gTypeInfo) instead of type name strings. Combined with hash-based naming
 *    (e.g., "is_arithmetic$a1b2c3d4"), this prevents ambiguity with underscore-containing types.
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

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>  // For std::hash
#include <variant>
#include <vector>
#include "AstNodeTypes.h"  // For Type, TypeIndex
#include "InlineVector.h"  // For InlineVector
#include "StringBuilder.h"
#include "StringTable.h"	 // For StringHandle
namespace FlashCpp {

// Forward declarations for dependent expression identity (defined in TemplateExpressionEquivalence.cpp)
bool equalDependentExpressionIdentity(const ASTNode& lhs, const ASTNode& rhs);
size_t hashDependentExpressionIdentity(const ASTNode& node);

inline bool equalTypeIndexIdentity(TypeIndex lhs, TypeIndex rhs) {
	// TypeIndex::operator== compares only the index slot; category must be checked separately
	// so primitive/default TypeIndex values don't collapse incorrectly.
	return lhs == rhs && lhs.category() == rhs.category();
}

inline size_t hashTypeIndexIdentity(TypeIndex idx) {
	size_t h = std::hash<TypeIndex>{}(idx);
	h ^= std::hash<int>{}(static_cast<int>(idx.category())) + 0x9e3779b9 + (h << 6) + (h >> 2);
	return h;
}

inline bool equalFunctionSignatureIdentity(const FunctionSignature& lhs, const FunctionSignature& rhs) {
	// Compare every field that contributes to function type identity in the AST signature model:
	// return type, parameter types, linkage, member class name, and cv-qualification.
	if (!equalTypeIndexIdentity(lhs.return_type_index, rhs.return_type_index) ||
		lhs.return_pointer_depth != rhs.return_pointer_depth ||
		lhs.return_reference_qualifier != rhs.return_reference_qualifier ||
		lhs.parameter_type_indices.size() != rhs.parameter_type_indices.size() ||
		lhs.linkage != rhs.linkage ||
		lhs.class_name != rhs.class_name ||
		lhs.calling_convention != rhs.calling_convention ||
		lhs.is_variadic != rhs.is_variadic ||
		lhs.is_const != rhs.is_const ||
		lhs.is_volatile != rhs.is_volatile ||
		lhs.function_reference_qualifier != rhs.function_reference_qualifier ||
		lhs.is_noexcept != rhs.is_noexcept) {
		return false;
	}
	for (size_t i = 0; i < lhs.parameter_type_indices.size(); ++i) {
		if (!equalTypeIndexIdentity(lhs.parameter_type_indices[i], rhs.parameter_type_indices[i])) {
			return false;
		}
	}
	return true;
}

inline size_t hashFunctionSignatureIdentity(const FunctionSignature& sig) {
	size_t h = hashTypeIndexIdentity(sig.return_type_index);
	h ^= std::hash<int>{}(sig.return_pointer_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(sig.return_reference_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
	for (const auto& pt : sig.parameter_type_indices) {
		h ^= hashTypeIndexIdentity(pt) + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(sig.linkage)) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<bool>{}(sig.class_name.isValid()) + 0x9e3779b9 + (h << 6) + (h >> 2);
	if (sig.class_name.isValid()) {
		h ^= sig.class_name.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
	}
	h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(sig.calling_convention)) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<bool>{}(sig.is_variadic) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<bool>{}(sig.is_const) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<bool>{}(sig.is_volatile) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(sig.function_reference_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
	h ^= std::hash<bool>{}(sig.is_noexcept) + 0x9e3779b9 + (h << 6) + (h >> 2);
	return h;
}

// ============================================================================
// TypeIndexArg - A template type argument represented by TypeIndex
// ============================================================================

/**
 * TypeIndexArg - Represents a type template argument using TypeIndex
 * 
 * This is a simpler representation than TemplateTypeArg, focused purely on
 * identity for lookup purposes. The category of the type is obtained via
 * type_index.category(); no separate base_type field is needed.
 */
struct TypeIndexArg {
	TypeIndex type_index{};

	// CV-qualifiers and reference info that affect template identity
	// These are stored separately because the same TypeIndex with different
	// qualifiers represents different template arguments (e.g., int vs const int&)
	CVQualifier cv_qualifier = CVQualifier::None;
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
	uint8_t pointer_depth = 0;

	// Array information - critical for differentiating T[], T[N], and T
	bool is_array = false;
	InlineVector<size_t, 2> array_sizes;  // Empty for T, full dimension list for arrays
	std::optional<FunctionSignature> function_signature; // Needed for function pointer identity
	bool is_dependent = false;
	StringHandle dependent_name{};

	TypeIndexArg() = default;

	explicit TypeIndexArg(TypeIndex idx) : type_index(idx) {}

	bool operator==(const TypeIndexArg& other) const {
		return type_index == other.type_index &&
			   type_index.category() == other.type_index.category() &&
			   cv_qualifier == other.cv_qualifier &&
			   ref_qualifier == other.ref_qualifier &&
			   pointer_depth == other.pointer_depth &&
			   is_array == other.is_array &&
			   array_sizes == other.array_sizes &&
			   function_signature.has_value() == other.function_signature.has_value() &&
			   (!function_signature.has_value() ||
				equalFunctionSignatureIdentity(*function_signature, *other.function_signature)) &&
			   is_dependent == other.is_dependent &&
			   // dependent_name only contributes when the arg is still dependent.
			   (!is_dependent || dependent_name == other.dependent_name);
	}

	bool operator!=(const TypeIndexArg& other) const {
		return !(*this == other);
	}

	size_t hash() const {
		size_t h = hashTypeIndexIdentity(type_index);
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(cv_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(static_cast<uint8_t>(ref_qualifier)) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(pointer_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
		// Include array info in hash - critical for differentiating T[] from T[N] from T
		h ^= std::hash<bool>{}(is_array) + 0x9e3779b9 + (h << 6) + (h >> 2);
		for (size_t array_size : array_sizes) {
			h ^= std::hash<size_t>{}(array_size) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		if (function_signature.has_value()) {
			h ^= hashFunctionSignatureIdentity(*function_signature) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		h ^= std::hash<bool>{}(is_dependent) + 0x9e3779b9 + (h << 6) + (h >> 2);
		if (is_dependent && dependent_name.isValid()) {
			h ^= std::hash<StringHandle>{}(dependent_name) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}
};

// ============================================================================
// NonTypeValueIdentity - Canonical carrier for non-type template argument identity
// ============================================================================

/**
 * NonTypeValueIdentity: Canonical carrier for non-type template argument identity.
 * 
 * Phase 1 of template-instantiation identity cleanup (see docs/2026-04-08-template-instantiation-materialization-plan.md).
 * 
 * This structure captures the identity of a non-type template argument in one place:
 * - For CONCRETE values: value + value_type_index define identity; dependent_name is invalid
 * - For DEPENDENT values: dependent_name defines identity; value/value_type_index are placeholders
 * 
 * Key invariants:
 * - is_dependent == true implies dependent_name.isValid()
 * - is_dependent == false implies !dependent_name.isValid() (concrete arg)
 * - Concrete non-type values keep their full type identity (including bool and integral width/signedness)
 * 
 * This replaces the scattered `is_value + value + is_dependent + dependent_name` fields in:
 * - TemplateTypeArg (when is_value==true)
 * - ValueArgKey (deprecated alias, now forwards to NonTypeValueIdentity)
 * 
 * The goal is one canonical representation that TemplateInstantiationKey consumes directly.
 */
// NonTypeValueIdentityKind is defined in AstNodeTypes_TypeSystem.h (included via AstNodeTypes.h).

struct NonTypeValueIdentity {
	NonTypeValueIdentityKind kind = NonTypeValueIdentityKind::Integral;
	int64_t value = 0;              // Integral value or ABI offset/sentinel for pointer-to-member
	TypeIndex value_type_index = nativeTypeIndex(TypeCategory::Int);  // The full type identity of the value
	StringHandle entity_name{};     // Named object/function for pointer/reference/function-pointer identities
	StringHandle member_name{};     // Class member for pointer-to-member identities
	StringHandle member_class_name{}; // Declaring class for pointer-to-member identities
	int64_t pointer_offset = 0;     // Array-element offset for object pointer identities
	StringHandle dependent_name{};  // Name when dependent (e.g., "N" for template<int N>)
	std::optional<ASTNode> dependent_expression{}; // Structural dependent expression identity when no single name represents the argument
	std::optional<FunctionSignature> function_signature{}; // Member/function pointer type identity when available
	bool is_dependent = false;      // True if this is a dependent (not yet substituted) value

	TypeCategory valueTypeCategory() const {
		return value_type_index.category();
	}

	// Factory methods for common cases
	static NonTypeValueIdentity makeConcrete(int64_t val, TypeCategory type) {
		return makeConcrete(val, TypeIndex{0, type});
	}

	static NonTypeValueIdentity makeConcrete(int64_t val, TypeIndex type_index) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::Integral;
		id.value = val;
		id.value_type_index = type_index;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	static NonTypeValueIdentity makeFloating(double val, TypeIndex type_index) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::Floating;
		uint64_t bits = 0;
		if (type_index.category() == TypeCategory::Float) {
			float float_value = static_cast<float>(val);
			uint32_t float_bits = 0;
			std::memcpy(&float_bits, &float_value, sizeof(float_bits));
			bits = float_bits;
		} else {
			std::memcpy(&bits, &val, sizeof(bits));
		}
		id.value = static_cast<int64_t>(bits);
		id.value_type_index = type_index;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	static NonTypeValueIdentity makeNullptr(TypeIndex type_index) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::Nullptr;
		id.value = 0;
		id.value_type_index = type_index;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	static NonTypeValueIdentity makeObjectPointer(TypeIndex type_index, StringHandle target_name, int64_t offset) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::ObjectPointer;
		id.value = 0;
		id.value_type_index = type_index;
		id.entity_name = target_name;
		id.pointer_offset = offset;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	static NonTypeValueIdentity makeReference(TypeIndex type_index, StringHandle target_name) {
		NonTypeValueIdentity id = makeObjectPointer(type_index, target_name, 0);
		id.kind = NonTypeValueIdentityKind::Reference;
		return id;
	}

	static NonTypeValueIdentity makeFunctionPointer(TypeIndex type_index, StringHandle function_name) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::FunctionPointer;
		id.value = 0;
		id.value_type_index = type_index;
		id.entity_name = function_name;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	static NonTypeValueIdentity makeMemberPointer(TypeIndex type_index, StringHandle target_member, int64_t offset) {
		return makeMemberPointer(type_index, target_member, offset, {});
	}

	static NonTypeValueIdentity makeMemberPointer(
		TypeIndex type_index,
		StringHandle target_member,
		int64_t offset,
		StringHandle declaring_class) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::MemberPointer;
		id.value = offset;
		id.value_type_index = type_index;
		id.member_name = target_member;
		id.member_class_name = declaring_class;
		id.is_dependent = false;
		id.dependent_name = {};
		return id;
	}

	bool isNullMemberPointer() const {
		return kind == NonTypeValueIdentityKind::MemberPointer && !member_name.isValid();
	}

	static NonTypeValueIdentity makeDependent(StringHandle name) {
		return makeDependent(name, nativeTypeIndex(TypeCategory::Int));
	}

	static NonTypeValueIdentity makeDependent(StringHandle name, TypeIndex type_index) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::Integral;
		id.value = 0;
		id.value_type_index = type_index;
		id.is_dependent = true;
		id.dependent_name = name;
		return id;
	}

	static NonTypeValueIdentity makeDependentWithPlaceholder(StringHandle name, int64_t placeholder_value, TypeCategory type) {
		return makeDependentWithPlaceholder(name, placeholder_value, TypeIndex{0, type});
	}

	static NonTypeValueIdentity makeDependentWithPlaceholder(StringHandle name, int64_t placeholder_value, TypeIndex type_index) {
		NonTypeValueIdentity id;
		id.kind = NonTypeValueIdentityKind::Integral;
		id.value = placeholder_value;
		id.value_type_index = type_index;
		id.is_dependent = true;
		id.dependent_name = name;
		return id;
	}

	static NonTypeValueIdentity makeDependentExpression(std::optional<ASTNode> expr, StringHandle name, int64_t placeholder_value, TypeIndex type_index) {
		NonTypeValueIdentity id = makeDependentWithPlaceholder(name, placeholder_value, type_index);
		id.dependent_expression = std::move(expr);
		return id;
	}

	// Helper retained for older call sites; value identity is exact, so no category
	// is normalized away. C++20 template-argument equivalence for converted constant
	// template arguments must preserve the parameter type, including bool and
	// integral signedness/width.
	static TypeCategory normalizedTypeForComparison(TypeCategory t) {
		return t;
	}

	static bool equalValueTypeIdentity(TypeIndex lhs, TypeIndex rhs) {
		// Comparison tiers:
		// 1. Different categories never match.
		// 2. User-defined / index-backed types must match by full TypeIndex identity.
		// 3. Other native types match by category alone.
		TypeCategory lhs_category = normalizedTypeForComparison(lhs.category());
		TypeCategory rhs_category = normalizedTypeForComparison(rhs.category());
		if (lhs_category != rhs_category) {
			return false;
		}
		if (lhs.needsTypeIndex() || rhs.needsTypeIndex()) {
			return equalTypeIndexIdentity(lhs, rhs);
		}
		return true;
	}

	static size_t hashValueTypeIdentity(TypeIndex type_index) {
		TypeCategory normalized_category = normalizedTypeForComparison(type_index.category());
		size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(normalized_category));
		if (type_index.needsTypeIndex()) {
			h ^= hashTypeIndexIdentity(type_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}

	bool operator==(const NonTypeValueIdentity& other) const;
	size_t hash() const;
	std::string toString() const;
};

// ============================================================================
// Ordered Template Instantiation Identity - Phase 7
// ============================================================================

/**
 * TemplateArgKind - Categorizes template arguments by their parameter kind
 * 
 * Phase 7 adds ordered identity tracking to TemplateInstantiationKey.
 * Template arguments must be distinguished by their parameter type:
 * - Type parameters bind type arguments
 * - Non-type parameters bind value arguments
 * - Template-template parameters bind template names
 * 
 * This enum preserves that distinction in the ordered identity.
 */
enum class TemplateArgKind : uint8_t {
	Type,     // Type template parameter
	Value,    // Non-type template parameter
	Template  // Template template parameter
};

/**
 * TemplateArgIdentity - Single argument in ordered identity
 * 
 * This structure preserves one template argument in full detail:
 * - kind: What kind of parameter this argument binds to
 * - payload: Exactly one of:
 *   - TypeIndexArg for type arguments (when kind == Type)
 *   - NonTypeValueIdentity for value arguments (when kind == Value)
 *   - StringHandle for template arguments (when kind == Template)
 */
struct TemplateArgIdentity {
	using Payload = std::variant<TypeIndexArg, NonTypeValueIdentity, StringHandle>;

	TemplateArgKind kind;
	Payload payload;

	TemplateArgIdentity()
		: kind(TemplateArgKind::Type), payload(TypeIndexArg{}) {}

	TemplateArgIdentity(const TypeIndexArg& type_in)
		: kind(TemplateArgKind::Type), payload(type_in) {}

	TemplateArgIdentity(const NonTypeValueIdentity& value_in)
		: kind(TemplateArgKind::Value), payload(value_in) {}

	TemplateArgIdentity(StringHandle template_name_in)
		: kind(TemplateArgKind::Template), payload(template_name_in) {}

	static TemplateArgIdentity makeType(const TypeIndexArg& type_in) {
		return TemplateArgIdentity(type_in);
	}

	static TemplateArgIdentity makeValue(const NonTypeValueIdentity& value_in) {
		return TemplateArgIdentity(value_in);
	}

	static TemplateArgIdentity makeTemplate(StringHandle template_name_in) {
		return TemplateArgIdentity(template_name_in);
	}

	const TypeIndexArg& type() const {
		return std::get<TypeIndexArg>(payload);
	}

	const NonTypeValueIdentity& value() const {
		return std::get<NonTypeValueIdentity>(payload);
	}

	StringHandle templateName() const {
		return std::get<StringHandle>(payload);
	}

	bool operator==(const TemplateArgIdentity& other) const {
		if (kind != other.kind)
			return false;
		switch (kind) {
		case TemplateArgKind::Type:
			return type() == other.type();
		case TemplateArgKind::Value:
			return value() == other.value();
		case TemplateArgKind::Template:
			return templateName() == other.templateName();
		}
		return false;
	}

	bool operator!=(const TemplateArgIdentity& other) const {
		return !(*this == other);
	}

	size_t hash() const {
		size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(kind));
		switch (kind) {
		case TemplateArgKind::Type:
			h ^= type().hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
			break;
		case TemplateArgKind::Value:
			h ^= value().hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
			break;
		case TemplateArgKind::Template:
			h ^= std::hash<uint32_t>{}(templateName().handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
			break;
		}
		return h;
	}
};

/**
 * OrderedTemplateInstantiationIdentity - Template arguments in source order
 * 
 * Phase 7 goal: Cache/specialization identity should match C++ template
 * argument source order rather than split type/value/template grouping.
 * 
 * This structure preserves the original argument sequence:
 * - base_template: StringHandle of the template being instantiated
 * - args: Sequence of TemplateArgIdentity in source order
 * 
 * This complements the split TemplateInstantiationKey fields (type_args,
 * value_args, template_template_args) which remain for algorithms like
 * partial specialization matching that need grouped arguments.
 */
struct OrderedTemplateInstantiationIdentity {
	StringHandle base_template{};
	InlineVector<TemplateArgIdentity, 4> args;

	bool operator==(const OrderedTemplateInstantiationIdentity& other) const {
		return base_template == other.base_template &&
			   args == other.args;
	}

	bool operator!=(const OrderedTemplateInstantiationIdentity& other) const {
		return !(*this == other);
	}

	size_t hash() const {
		size_t h = std::hash<uint32_t>{}(base_template.handle);
		for (const auto& arg : args) {
			h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		return h;
	}

	[[nodiscard]] bool empty() const {
		return base_template.handle == 0;
	}

	void clear() {
		base_template = StringHandle{};
		args.clear();
	}
};

/**
 * Hash function for OrderedTemplateInstantiationIdentity
 */
struct OrderedTemplateInstantiationIdentityHash {
	size_t operator()(const OrderedTemplateInstantiationIdentity& id) const {
		return id.hash();
	}
};

// ============================================================================
// TemplateInstantiationKey - TypeIndex-based template instantiation key
// ============================================================================

/**
 * TemplateInstantiationKey - A template instantiation key using TypeIndex
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

// ValueArgKey is now an alias for NonTypeValueIdentity for backward compatibility
// during Phase 1 migration. New code should use NonTypeValueIdentity directly.
using ValueArgKey = NonTypeValueIdentity;

struct TemplateInstantiationKey {
	StringHandle base_template;							// Template name handle
	InlineVector<TypeIndexArg, 4> type_args;				 // Type arguments
	InlineVector<ValueArgKey, 4> value_args;				 // Non-type arguments
	InlineVector<StringHandle, 2> template_template_args; // Template template args
	OrderedTemplateInstantiationIdentity ordered_identity; // Phase 7: ordered argument identity

	TemplateInstantiationKey() = default;

	explicit TemplateInstantiationKey(StringHandle template_name)
		: base_template(template_name) {}

	bool operator==(const TemplateInstantiationKey& other) const {
		return base_template == other.base_template &&
			   type_args == other.type_args &&
			   value_args == other.value_args &&
			   template_template_args == other.template_template_args &&
			   ordered_identity == other.ordered_identity;
	}

	bool operator!=(const TemplateInstantiationKey& other) const {
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
		ordered_identity.clear();
	}
};

/**
 * Hash function for TemplateInstantiationKey
 */
struct TemplateInstantiationKeyHash {
	size_t operator()(const TemplateInstantiationKey& key) const {
		size_t h = std::hash<uint32_t>{}(key.base_template.handle);

		// Hash type arguments
		for (const auto& arg : key.type_args) {
			h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}

		// Hash value arguments
		for (const auto& val : key.value_args) {
			h ^= val.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}

		// Hash template template arguments
		for (const auto& tmpl : key.template_template_args) {
			h ^= std::hash<uint32_t>{}(tmpl.handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}

		// Hash ordered identity (Phase 7)
		h ^= key.ordered_identity.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);

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
	StringHandle function_name;					// Function name handle
	InlineVector<TypeIndexArg, 8> param_types;	   // Parameter types (8 inline for common cases)

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
												 const TemplateInstantiationKey& key) {
	// Compute the hash of the template arguments
	size_t h = 0;
	if (!key.ordered_identity.empty()) {
		h = key.ordered_identity.hash();
	} else {
		for (const auto& arg : key.type_args) {
			h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		for (const auto& arg : key.value_args) {
			h ^= arg.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		for (const auto& arg : key.template_template_args) {
			h ^= std::hash<uint32_t>{}(arg.handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
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
	builder.append("$");	 // $ is not valid in C++ identifiers, so unambiguous
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
//   - FlashCpp::makeInstantiationKey(StringHandle, std::span<const TemplateTypeArg>) -> TemplateInstantiationKey

} // namespace FlashCpp

// Provide std::hash specialization for use with unordered_map
namespace std {
template <>
struct hash<FlashCpp::TemplateInstantiationKey> {
	size_t operator()(const FlashCpp::TemplateInstantiationKey& key) const {
		return FlashCpp::TemplateInstantiationKeyHash{}(key);
	}
};

template <>
struct hash<FlashCpp::TypeIndexArg> {
	size_t operator()(const FlashCpp::TypeIndexArg& arg) const {
		return arg.hash();
	}
};

template <>
struct hash<FlashCpp::FunctionSignatureKey> {
	size_t operator()(const FlashCpp::FunctionSignatureKey& key) const {
		return FlashCpp::FunctionSignatureKeyHash{}(key);
	}
};
} // namespace std
