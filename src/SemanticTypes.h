#pragma once
#include "AstNodeTypes_TypeSystem.h"
#include "IRTypes_Registers.h"
#include "InlineVector.h"
#include <functional>
#include <unordered_map>

class ConstructorDeclarationNode;

// --- Standard conversion kinds (C++20 [conv]) ---

enum class StandardConversionKind : uint8_t {
	None,						 // No conversion / identity (used as sentinel in ConversionPlan)
	LValueToRValue,
	ArrayToPointer,
	FunctionToPointer,
	PointerConversion,
	IntegralPromotion,
	FloatingPromotion,
	IntegralConversion,
	FloatingConversion,
	FloatingIntegralConversion,
	BooleanConversion,
	QualificationAdjustment,
	DerivedToBase,
	UserDefined,
	TemporaryMaterialization
};

// --- Canonical type identity (interned handle) ---

struct CanonicalTypeId {
	uint32_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(CanonicalTypeId, CanonicalTypeId) = default;
};

// --- Cast-info side-table index ---

struct CastInfoIndex {
	uint16_t value = 0;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(CastInfoIndex, CastInfoIndex) = default;
};

// --- Semantic slot flags (orthogonal boolean facts) ---

enum class SemanticSlotFlags : uint8_t {
	None = 0,
	IsDependent = 1 << 0,
	IsConstantEvaluated = 1 << 1,
	IsOverloadSet = 1 << 2,
};

inline SemanticSlotFlags operator|(SemanticSlotFlags a, SemanticSlotFlags b) {
	return static_cast<SemanticSlotFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline SemanticSlotFlags operator&(SemanticSlotFlags a, SemanticSlotFlags b) {
	return static_cast<SemanticSlotFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline bool hasFlag(SemanticSlotFlags flags, SemanticSlotFlags flag) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

// --- Packed semantic slot stored on expression nodes ---
// Target: ~8 bytes. Compact handle layer into interning tables.

struct SemanticSlot {
	CanonicalTypeId type_id{};									// 4 bytes
	CastInfoIndex cast_info_index{};								 // 2 bytes
	ValueCategory value_category = ValueCategory::PRValue;		   // 1 byte
	SemanticSlotFlags flags = SemanticSlotFlags::None;			   // 1 byte
	// Total: 8 bytes

	bool has_cast() const { return static_cast<bool>(cast_info_index); }
	bool has_type() const { return static_cast<bool>(type_id); }
	bool is_empty() const { return !has_type() && !has_cast(); }
};

static_assert(sizeof(SemanticSlot) == 8, "SemanticSlot must be exactly 8 bytes");

// --- Implicit cast info (stored in side table, indexed by CastInfoIndex) ---

struct ImplicitCastInfo {
	CanonicalTypeId source_type_id;
	CanonicalTypeId target_type_id;
	StandardConversionKind cast_kind;
	ValueCategory value_category_after = ValueCategory::PRValue;
	const ConstructorDeclarationNode* selected_constructor = nullptr;
};

// --- Canonical type descriptor (stored in TypeContext, indexed by CanonicalTypeId) ---

enum class CanonicalTypeFlags : uint8_t {
	None = 0,
	IsPackExpansion = 1 << 0,
	IsFunctionType = 1 << 1,
};

inline CanonicalTypeFlags operator|(CanonicalTypeFlags a, CanonicalTypeFlags b) {
	return static_cast<CanonicalTypeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool hasFlag(CanonicalTypeFlags flags, CanonicalTypeFlags flag) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct CanonicalTypeDesc {
	TypeIndex type_index{};
	CVQualifier base_cv = CVQualifier::None;
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
	InlineVector<PointerLevel, 4> pointer_levels;
	InlineVector<size_t, 4> array_dimensions;
	CanonicalTypeFlags flags = CanonicalTypeFlags::None;
	std::optional<FunctionSignature> function_signature;

	bool operator==(const CanonicalTypeDesc& other) const;

	TypeCategory category() const {
		return type_index.category();
	}
};

// --- Hash specialisation for CanonicalTypeDesc (enables O(1) TypeContext::intern) ---

namespace std {
template <>
struct hash<CanonicalTypeDesc> {
	size_t operator()(const CanonicalTypeDesc& d) const noexcept {
		// Simple hash combiner: h = h * 31 + value
		auto combine = [](size_t h, size_t v) -> size_t {
			return h * 31u + v;
		};
		size_t h = 0;
		h = combine(h, static_cast<size_t>(d.type_index.category()));
		h = combine(h, d.type_index.index());
		h = combine(h, static_cast<size_t>(d.base_cv));
		h = combine(h, static_cast<size_t>(d.ref_qualifier));
		h = combine(h, d.pointer_levels.size());
		for (const auto& pl : d.pointer_levels)
			h = combine(h, static_cast<size_t>(pl.cv_qualifier));
		h = combine(h, d.array_dimensions.size());
		for (size_t dim : d.array_dimensions)
			h = combine(h, dim);
		h = combine(h, static_cast<size_t>(d.flags));
		if (d.function_signature) {
			const auto& fs = *d.function_signature;
			h = combine(h, static_cast<size_t>(fs.return_type_index.category()));
			h = combine(h, fs.return_type_index.index());
			h = combine(h, fs.parameter_type_indices.size());
			for (const TypeIndex& pt : fs.parameter_type_indices) {
				h = combine(h, static_cast<size_t>(pt.category()));
				h = combine(h, pt.index());
			}
			h = combine(h, static_cast<size_t>(fs.linkage));
			h = combine(h, fs.is_const ? 1u : 0u);
			h = combine(h, fs.is_volatile ? 1u : 0u);
			if (fs.class_name)
				h = combine(h, std::hash<std::string>{}(*fs.class_name));
		}
		return h;
	}
};
} // namespace std

// --- Type interning context ---

class TypeContext {
public:
	CanonicalTypeId intern(const CanonicalTypeDesc& desc);
	const CanonicalTypeDesc& get(CanonicalTypeId id) const;
	size_t size() const { return types_.size(); }

private:
	std::vector<CanonicalTypeDesc> types_;  // 0-based storage; CanonicalTypeId value 0 is reserved as invalid sentinel
	std::unordered_map<CanonicalTypeDesc, CanonicalTypeId> index_;  // O(1) dedup map
};

// --- Semantic expression info (returned from expression normalization) ---

enum class SemanticExprFlags : uint8_t {
	None = 0,
	IsDependent = 1 << 0,
	IsConstantEvaluated = 1 << 1,
	IsOverloadSet = 1 << 2,
};

struct SemanticExprInfo {
	CanonicalTypeId type_id{};
	ValueCategory value_category = ValueCategory::PRValue;
	SemanticExprFlags flags = SemanticExprFlags::None;
	CastInfoIndex cast_info_index{};
};

// --- Conversion context (what kind of conversion site) ---

enum class ConversionContext : uint8_t {
	None,
	FunctionArgument,
	Return,
	Initialization,
	Assignment,
	Condition,
	BinaryArithmetic,
	Comparison,
	Ternary,
	ReferenceBinding
};

// --- Conversion plan flags ---

enum class ConversionPlanFlags : uint8_t {
	None = 0,
	IsValid = 1 << 0,
	IsUserDefined = 1 << 1,
	BindsReferenceDirectly = 1 << 2,
	MaterializesTemporary = 1 << 3,
};

inline ConversionPlanFlags operator|(ConversionPlanFlags a, ConversionPlanFlags b) {
	return static_cast<ConversionPlanFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool hasFlag(ConversionPlanFlags flags, ConversionPlanFlags flag) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct CallArgReferenceBindingInfo {
	CanonicalTypeId parameter_type_id{};
	CastInfoIndex pre_bind_cast_info_index{};
	ConversionPlanFlags flags = ConversionPlanFlags::None;

	bool is_valid() const { return hasFlag(flags, ConversionPlanFlags::IsValid); }
	bool binds_directly() const { return hasFlag(flags, ConversionPlanFlags::BindsReferenceDirectly); }
	bool materializes_temporary() const { return hasFlag(flags, ConversionPlanFlags::MaterializesTemporary); }
	bool has_pre_bind_cast() const { return static_cast<bool>(pre_bind_cast_info_index); }
};

// --- Conversion step (one step in a conversion sequence) ---

struct ConversionStep {
	StandardConversionKind kind;
	CanonicalTypeId target_type_id{};
};

// --- Semantic context (carried through the pass) ---

enum class UserDefinedConversionPolicy : uint8_t {
	Disallow,
	Allow,
};

enum class SemanticContextFlags : uint8_t {
	None = 0,
	InConstantEvaluatedContext = 1 << 0,
	InsideTemplateDefinition = 1 << 1,
	InsideInstantiatedTemplate = 1 << 2,
};

inline SemanticContextFlags operator|(SemanticContextFlags a, SemanticContextFlags b) {
	return static_cast<SemanticContextFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool hasFlag(SemanticContextFlags flags, SemanticContextFlags flag) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

struct SemanticContext {
	std::optional<CanonicalTypeId> expected_type_id;
	std::optional<CanonicalTypeId> current_function_return_type_id;
	ConversionContext conversion_context = ConversionContext::None;
	SemanticContextFlags flags = SemanticContextFlags::None;
	UserDefinedConversionPolicy user_defined_conversion_policy = UserDefinedConversionPolicy::Allow;
};

// --- Rewrite disposition (did the pass change anything?) ---

enum class RewriteDisposition : uint8_t {
	Unchanged,
	StructurallyChanged,
};

// --- Semantic pass statistics ---

struct SemanticPassStats {
	size_t total_roots = 0;
	size_t roots_visited = 0;
	size_t expressions_visited = 0;
	size_t statements_visited = 0;
	size_t slots_filled = 0;
	size_t structural_rewrites = 0;
	size_t cast_infos_allocated = 0;
	size_t canonical_types_interned = 0;
	size_t op_calls_resolved = 0;  // callable-object operator() calls pre-resolved by sema
};
