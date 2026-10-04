#pragma once

#include "AstNodeTypes.h"
#include "SemanticTypes.h"
#include "CanonicalTypeAdapter.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "FrontendContext.h"
#include "ChunkedString.h"
#include "TemplateExpressionEquivalence.h"
#include "TemplateTypes.h" // For FunctionSignatureKey
#include "MigrationStats.h"
#include "InlineVector.h"
#include <algorithm>
#include <array>
#include <span>
#include <vector>
#include <optional>
#include <unordered_map>
#include <unordered_set>

// Conversion rank for overload resolution
// Lower rank = better match.
// QualificationAdjustment sits between ExactMatch and Promotion: per C++20
// [over.best.ics.general] Table 12, qualification conversions (e.g. T*→const T*)
// are in the "exact match" category, but by [over.ics.rank]/3.2.1 a sequence
// without a qualification step is a proper subsequence of one with it, so it is
// strictly preferred.  Modelling this as a separate rank between ExactMatch and
// Promotion achieves the same observable tie-breaking without any changes to the
// rank-comparison logic throughout overload resolution.
enum class ConversionRank {
	ExactMatch = 0, // Identity — no conversion needed
	QualificationAdjustment = 1, // T*→const T*, ExactMatch category per standard but weaker than identity
	Promotion = 2, // Integral or floating-point promotion
	Conversion = 3, // Standard conversion (int to double, etc.)
	UserDefined = 4, // User-defined conversion via conversion operator
	Ellipsis = 5, // Ellipsis conversion sequence (worst viable conversion)
	NoMatch = 6 // No valid conversion
};

// Result of checking if one type can convert to another
struct TypeConversionResult {
	ConversionRank rank;
	bool is_valid;

	TypeConversionResult(ConversionRank r, bool valid) : rank(r), is_valid(valid) {}

	static TypeConversionResult exact_match() { return {ConversionRank::ExactMatch, true}; }
	static TypeConversionResult qualification_adjustment() { return {ConversionRank::QualificationAdjustment, true}; }
	static TypeConversionResult promotion() { return {ConversionRank::Promotion, true}; }
	static TypeConversionResult conversion() { return {ConversionRank::Conversion, true}; }
	static TypeConversionResult no_match() { return {ConversionRank::NoMatch, false}; }
};

// Check whether two canonical type IDs represent the same type for overload resolution
// signature matching (C++20 [over.match]).  Because all types are interned, equality of
// the handles implies equality of the descriptors — this helper exists so call sites can
// express the intent without hardcoding the == operator on CanonicalTypeId.
inline bool canonical_types_match(CanonicalTypeId a, CanonicalTypeId b) {
	return a == b; // interned: equal IDs ⟺ equal canonical types
}

inline bool isPlainNoexceptFunctionPointerConversion(
	const FunctionSignature& from,
	const FunctionSignature& to) {
	if (!from.is_noexcept || to.is_noexcept ||
		from.noexcept_expression.has_value() ||
		to.noexcept_expression.has_value()) {
		return false;
	}
	FunctionSignature non_noexcept_from = from;
	non_noexcept_from.is_noexcept = false;
	return FlashCpp::equalFunctionSignatureIdentity(non_noexcept_from, to);
}

// Unified conversion plan: combines ConversionRank (for overload resolution ranking)
// with StandardConversionKind (for semantic annotation).
// Replaces the previous two-call pattern of can_convert_type() + determineConversionKind().
struct ConversionPlan {
	ConversionRank rank = ConversionRank::NoMatch;
	StandardConversionKind kind = StandardConversionKind::None;
	bool is_valid = false;
	ConversionRank trailing_standard_rank = ConversionRank::NoMatch;

	// Convert to TypeConversionResult for backward compatibility with callers
	// that only need rank + validity.
	TypeConversionResult toResult() const { return {rank, is_valid}; }

	static ConversionPlan exact_match() {
		return {ConversionRank::ExactMatch, StandardConversionKind::None, true};
	}
	static ConversionPlan qualification_adjustment() {
		return {ConversionRank::QualificationAdjustment,
			StandardConversionKind::QualificationAdjustment, true};
	}
	static ConversionPlan no_match() {
		return {ConversionRank::NoMatch, StandardConversionKind::None, false};
	}
};

struct ArgumentConversionInfo {
	ConversionRank rank = ConversionRank::NoMatch;
	const TypeSpecifierNode* parameter_type = nullptr;
	bool is_valid = false;
	ConversionRank trailing_standard_rank = ConversionRank::NoMatch;

	TypeConversionResult toResult() const { return {rank, is_valid}; }

	static ArgumentConversionInfo no_match() {
		return {ConversionRank::NoMatch, nullptr, false};
	}

	static ArgumentConversionInfo ellipsis_match_variadic() {
		return {ConversionRank::Ellipsis, nullptr, true};
	}
};

using ArgumentConversionInfoVector =
	OverloadVector<ArgumentConversionInfo, 4>;
using OverloadResolutionTypeSpecifierVector =
	OverloadVector<TypeSpecifierNode, 4>;
using OverloadResolutionSizeIndexVector =
	OverloadVector<size_t, 4>;
using OverloadResolutionAstNodePtrVector =
	OverloadVector<const ASTNode*, 4>;

inline TypeId canonicalTypeWithoutReference(
	CanonicalTypeTable& table,
	TypeId type) {
	while (true) {
		const CanonicalTypeNode node = table.node(type);
		if (node.kind != CanonicalTypeKind::LValueReference &&
			node.kind != CanonicalTypeKind::RValueReference) {
			return type;
		}
		type = node.child;
	}
}

inline TypeId canonicalTypeWithoutTopLevelQualifiers(
	CanonicalTypeTable& table,
	TypeId type) {
	return table.withoutTopLevelQualifiers(type);
}

inline size_t canonicalPointerDepth(
	CanonicalTypeTable& table,
	TypeId type) {
	size_t depth = 0;
	type = canonicalTypeWithoutTopLevelQualifiers(table, type);
	while (table.node(type).kind == CanonicalTypeKind::Pointer) {
		++depth;
		type = canonicalTypeWithoutTopLevelQualifiers(table, table.node(type).child);
	}
	return depth;
}

inline CVQualifier canonicalTopLevelCvThroughArrays(
	CanonicalTypeTable& table,
	TypeId type) {
	while (table.node(type).kind == CanonicalTypeKind::Array) {
		type = table.node(type).child;
	}
	const CanonicalTypeNode node = table.node(type);
	return node.kind == CanonicalTypeKind::Qualified
		? node.qualifiers
		: CVQualifier::None;
}

inline bool canonicalTypesMatchIgnoringTopLevelCv(
	CanonicalTypeTable& table,
	TypeId lhs,
	TypeId rhs) {
	while (true) {
		const CanonicalTypeNode lhs_node = table.node(lhs);
		const CanonicalTypeNode rhs_node = table.node(rhs);
		if (lhs_node.kind == CanonicalTypeKind::Array ||
			rhs_node.kind == CanonicalTypeKind::Array) {
			if (lhs_node.kind != CanonicalTypeKind::Array ||
				rhs_node.kind != CanonicalTypeKind::Array ||
				lhs_node.flags != rhs_node.flags ||
				lhs_node.array_extent != rhs_node.array_extent) {
				return false;
			}
			lhs = lhs_node.child;
			rhs = rhs_node.child;
			continue;
		}
		if (lhs_node.kind == CanonicalTypeKind::Qualified) {
			lhs = lhs_node.child;
		}
		if (rhs_node.kind == CanonicalTypeKind::Qualified) {
			rhs = rhs_node.child;
		}
		return lhs == rhs;
	}
}

inline std::optional<bool> tryCanonicalTypesMatchIgnoringTopLevelCvAndRef(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs) {
	CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport lhs_import = importCanonicalType(table, lhs);
	const CanonicalTypeImport rhs_import = importCanonicalType(table, rhs);
	if (lhs_import.status == CanonicalTypeImportStatus::Invalid ||
		rhs_import.status == CanonicalTypeImportStatus::Invalid) {
		return false;
	}
	if (lhs_import.status != CanonicalTypeImportStatus::Supported ||
		rhs_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	return canonicalTypesMatchIgnoringTopLevelCv(
		table,
		canonicalTypeWithoutReference(table, lhs_import.type),
		canonicalTypeWithoutReference(table, rhs_import.type));
}

// Compatibility boundary for types the canonical importer cannot handle yet.
inline bool isSameTypeIgnoringTopLevelCvAndRefCompatibility(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs) {
	const CanonicalTypeAlias lhs_canonical = canonicalize_type_alias(lhs.type_index());
	const CanonicalTypeAlias rhs_canonical = canonicalize_type_alias(rhs.type_index());
	const TypeIndex lhs_resolved = lhs_canonical.resolvedTypeIndex();
	const TypeIndex rhs_resolved = rhs_canonical.resolvedTypeIndex();

	if (lhs.type() != rhs.type() ||
		lhs_resolved != rhs_resolved ||
		lhs.pointer_depth() != rhs.pointer_depth() ||
		lhs.pointer_levels().size() != rhs.pointer_levels().size() ||
		lhs.has_pointee_array_declarator() != rhs.has_pointee_array_declarator() ||
		!std::ranges::equal(lhs.array_dimensions(), rhs.array_dimensions()) ||
		lhs.has_member_class() != rhs.has_member_class() ||
		lhs.has_function_signature() != rhs.has_function_signature()) {
		return false;
	}

	// Skip the outermost pointer level: its cv-qualifiers are the top-level
	// cv-qualifiers of a pointer referent, which this helper deliberately
	// disregards.  Inner pointer cv-qualifiers remain part of the identity.
	for (size_t i = 0; i + 1 < lhs.pointer_levels().size(); ++i) {
		if (lhs.pointer_levels()[i].cv_qualifier != rhs.pointer_levels()[i].cv_qualifier) {
			return false;
		}
	}

	if (lhs.has_member_class() && lhs.member_class_name() != rhs.member_class_name()) {
		return false;
	}

	if (lhs.has_function_signature()) {
		const FunctionSignature& lhs_sig = lhs.function_signature();
		const FunctionSignature& rhs_sig = rhs.function_signature();
		if (!FlashCpp::equalFunctionSignatureIdentity(lhs_sig, rhs_sig)) {
			return false;
		}
	}

	return true;
}

inline bool isSameTypeIgnoringTopLevelCvAndRef(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs) {
	const std::optional<bool> canonical_match =
		tryCanonicalTypesMatchIgnoringTopLevelCvAndRef(lhs, rhs);
	if (canonical_match.has_value()) {
		return *canonical_match;
	}
	return isSameTypeIgnoringTopLevelCvAndRefCompatibility(lhs, rhs);
}

inline std::optional<int> tryCompareCanonicalQualificationConversionDestinations(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& lhs_param,
	const TypeSpecifierNode& rhs_param) {
	CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport argument_import = importCanonicalType(table, argument_type);
	const CanonicalTypeImport lhs_import = importCanonicalType(table, lhs_param);
	const CanonicalTypeImport rhs_import = importCanonicalType(table, rhs_param);
	if (argument_import.status == CanonicalTypeImportStatus::Invalid ||
		lhs_import.status == CanonicalTypeImportStatus::Invalid ||
		rhs_import.status == CanonicalTypeImportStatus::Invalid) {
		return 0;
	}
	if (argument_import.status != CanonicalTypeImportStatus::Supported ||
		lhs_import.status != CanonicalTypeImportStatus::Supported ||
		rhs_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}

	const TypeId argument = canonicalTypeWithoutTopLevelQualifiers(
		table,
		canonicalTypeWithoutReference(table, argument_import.type));
	const TypeId lhs = canonicalTypeWithoutTopLevelQualifiers(
		table,
		canonicalTypeWithoutReference(table, lhs_import.type));
	const TypeId rhs = canonicalTypeWithoutTopLevelQualifiers(
		table,
		canonicalTypeWithoutReference(table, rhs_import.type));
	if (canonicalPointerDepth(table, argument) == 0 ||
		canonicalPointerDepth(table, argument) != canonicalPointerDepth(table, lhs) ||
		canonicalPointerDepth(table, lhs) != canonicalPointerDepth(table, rhs) ||
		table.node(argument).kind != CanonicalTypeKind::Pointer ||
		table.node(lhs).kind != CanonicalTypeKind::Pointer ||
		table.node(rhs).kind != CanonicalTypeKind::Pointer) {
		return 0;
	}

	const TypeId argument_pointee = table.node(argument).child;
	const TypeId lhs_pointee = table.node(lhs).child;
	const TypeId rhs_pointee = table.node(rhs).child;
	if (!canonicalTypesMatchIgnoringTopLevelCv(table, lhs_pointee, rhs_pointee)) {
		return 0;
	}

	const uint8_t from_cv = static_cast<uint8_t>(
		canonicalTopLevelCvThroughArrays(table, argument_pointee));
	const uint8_t lhs_cv = static_cast<uint8_t>(
		canonicalTopLevelCvThroughArrays(table, lhs_pointee));
	const uint8_t rhs_cv = static_cast<uint8_t>(
		canonicalTopLevelCvThroughArrays(table, rhs_pointee));
	const uint8_t lhs_extra = static_cast<uint8_t>(lhs_cv & ~from_cv);
	const uint8_t rhs_extra = static_cast<uint8_t>(rhs_cv & ~from_cv);
	const bool lhs_is_subset = (lhs_extra & ~rhs_extra) == 0;
	const bool rhs_is_subset = (rhs_extra & ~lhs_extra) == 0;
	if (lhs_is_subset && !rhs_is_subset) {
		return -1;
	}
	if (rhs_is_subset && !lhs_is_subset) {
		return 1;
	}
	return 0;
}

// Compatibility boundary for types the canonical importer cannot handle yet.
inline int compareQualificationConversionDestinationsCompatibility(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& lhs_param,
	const TypeSpecifierNode& rhs_param) {
	if (lhs_param.pointer_depth() == 0 ||
		lhs_param.pointer_depth() != rhs_param.pointer_depth() ||
		argument_type.pointer_depth() != lhs_param.pointer_depth()) {
		return 0;
	}

	const CanonicalTypeAlias lhs_canonical = canonicalize_type_alias(lhs_param.type_index());
	const CanonicalTypeAlias rhs_canonical = canonicalize_type_alias(rhs_param.type_index());
	const TypeIndex lhs_resolved = lhs_canonical.resolvedTypeIndex();
	const TypeIndex rhs_resolved = rhs_canonical.resolvedTypeIndex();
	if (lhs_param.type() != rhs_param.type()) {
		return 0;
	}
	if (lhs_resolved.isStruct() && rhs_resolved.isStruct() && lhs_resolved != rhs_resolved) {
		return 0;
	}

	const uint8_t from_cv = static_cast<uint8_t>(argument_type.pointee_cv_for_pointer_conversion());
	const uint8_t lhs_cv = static_cast<uint8_t>(lhs_param.pointee_cv_for_pointer_conversion());
	const uint8_t rhs_cv = static_cast<uint8_t>(rhs_param.pointee_cv_for_pointer_conversion());
	const uint8_t lhs_extra = static_cast<uint8_t>(lhs_cv & ~from_cv);
	const uint8_t rhs_extra = static_cast<uint8_t>(rhs_cv & ~from_cv);
	const bool lhs_is_subset = (lhs_extra & ~rhs_extra) == 0;
	const bool rhs_is_subset = (rhs_extra & ~lhs_extra) == 0;
	if (lhs_is_subset && !rhs_is_subset) {
		return -1;
	}
	if (rhs_is_subset && !lhs_is_subset) {
		return 1;
	}
	return 0;
}

// C++20 [over.ics.rank]/3.2.1: when two conversion sequences have the same rank
// and convert similar pointer types, the destination that adds fewer cv-qualifiers
// is a proper subsequence of the other and is therefore better.  This prefers
// volatile T* over const volatile T* for a T* argument (MSVC <atomic>
// __iso_volatile_store32).
inline int compareQualificationConversionDestinations(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& lhs_param,
	const TypeSpecifierNode& rhs_param) {
	const std::optional<int> canonical_comparison =
		tryCompareCanonicalQualificationConversionDestinations(
			argument_type, lhs_param, rhs_param);
	if (canonical_comparison.has_value()) {
		return *canonical_comparison;
	}
	return compareQualificationConversionDestinationsCompatibility(
		argument_type, lhs_param, rhs_param);
}

inline bool isRvalueLikeArgumentForReferenceBinding(const TypeSpecifierNode& argument_type) {
	return !argument_type.is_reference() || argument_type.is_rvalue_reference();
}

inline std::optional<int> tryCompareFixedEnumPromotionTargets(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& lhs_parameter,
	const TypeSpecifierNode& rhs_parameter);

inline int compareArgumentConversionInfo(
	const TypeSpecifierNode& argument_type,
	const ArgumentConversionInfo& lhs,
	const ArgumentConversionInfo& rhs) {
	if (lhs.rank < rhs.rank) {
		return -1;
	}
	if (lhs.rank > rhs.rank) {
		return 1;
	}
	if (lhs.rank == ConversionRank::UserDefined &&
		lhs.trailing_standard_rank != ConversionRank::NoMatch &&
		rhs.trailing_standard_rank != ConversionRank::NoMatch) {
		if (lhs.trailing_standard_rank < rhs.trailing_standard_rank) {
			return -1;
		}
		if (lhs.trailing_standard_rank > rhs.trailing_standard_rank) {
			return 1;
		}
	}
	if (lhs.rank == ConversionRank::Promotion &&
		lhs.parameter_type != nullptr && rhs.parameter_type != nullptr) {
		const std::optional<int> enum_promotion_comparison =
			tryCompareFixedEnumPromotionTargets(
				argument_type,
				*lhs.parameter_type,
				*rhs.parameter_type);
		if (enum_promotion_comparison.has_value() &&
			*enum_promotion_comparison != 0) {
			return *enum_promotion_comparison;
		}
	}

	if (lhs.parameter_type != nullptr && rhs.parameter_type != nullptr) {
		const int qualification_comparison = compareQualificationConversionDestinations(
			argument_type,
			*lhs.parameter_type,
			*rhs.parameter_type);
		if (qualification_comparison != 0) {
			return qualification_comparison;
		}
	}

	if (lhs.rank != ConversionRank::ExactMatch ||
		lhs.parameter_type == nullptr || rhs.parameter_type == nullptr) {
		return 0;
	}

	const TypeSpecifierNode& lhs_param = *lhs.parameter_type;
	const TypeSpecifierNode& rhs_param = *rhs.parameter_type;

	if (!isSameTypeIgnoringTopLevelCvAndRef(lhs_param, rhs_param)) {
		return 0;
	}

	// [over.ics.rank]/3.2.6: when two reference bindings refer to types that
	// differ only in top-level cv-qualification, binding to the less
	// cv-qualified referenced type is better.  This is what lets T& beat
	// const T& for an lvalue argument now that adding a top-level cv-qualifier
	// through reference binding is ranked as an identity conversion.
	if (lhs_param.is_reference() && rhs_param.is_reference()) {
		const uint8_t lhs_cv =
			static_cast<uint8_t>(lhs_param.top_level_cv_qualifier());
		const uint8_t rhs_cv =
			static_cast<uint8_t>(rhs_param.top_level_cv_qualifier());
		const bool lhs_is_subset = (lhs_cv & ~rhs_cv) == 0;
		const bool rhs_is_subset = (rhs_cv & ~lhs_cv) == 0;
		if (lhs_is_subset && !rhs_is_subset) {
			return -1;
		}
		if (rhs_is_subset && !lhs_is_subset) {
			return 1;
		}
	}

	if (!isRvalueLikeArgumentForReferenceBinding(argument_type)) {
		return 0;
	}

	const bool lhs_is_rvalue_ref = lhs_param.is_rvalue_reference();
	const bool rhs_is_rvalue_ref = rhs_param.is_rvalue_reference();
	const bool lhs_is_const_lvalue_ref = lhs_param.is_lvalue_reference() && lhs_param.is_const();
	const bool rhs_is_const_lvalue_ref = rhs_param.is_lvalue_reference() && rhs_param.is_const();

	if (lhs_is_rvalue_ref && rhs_is_const_lvalue_ref) {
		return -1;
	}
	if (rhs_is_rvalue_ref && lhs_is_const_lvalue_ref) {
		return 1;
	}

	return 0;
}

struct ConversionInfoComparison {
	bool lhs_is_better = false;
	bool lhs_is_worse = false;
};

inline ConversionInfoComparison compareConversionInfoLists(
	std::span<const TypeSpecifierNode> argument_types,
	std::span<const ArgumentConversionInfo> lhs_infos,
	std::span<const ArgumentConversionInfo> rhs_infos) {
	ConversionInfoComparison comparison;
	const size_t count = std::min(argument_types.size(), std::min(lhs_infos.size(), rhs_infos.size()));
	for (size_t i = 0; i < count; ++i) {
		const int arg_comparison = compareArgumentConversionInfo(argument_types[i], lhs_infos[i], rhs_infos[i]);
		if (arg_comparison < 0) {
			comparison.lhs_is_better = true;
		} else if (arg_comparison > 0) {
			comparison.lhs_is_worse = true;
		}
	}
	return comparison;
}

// Build a unified conversion plan for two primitive TypeCategory values.
// Returns both the ConversionRank (for overload resolution) and the
// StandardConversionKind (for semantic annotation) in a single call.
// Implements C++20 [conv], [conv.prom], [conv.rank] rules.
inline ConversionPlan buildConversionPlan(TypeCategory from_category, TypeCategory to_category) {
	// Exact match (including Struct==Struct — same type, different struct variants
	// are handled by the TypeSpecifierNode overload which has type_index).
	if (from_category == to_category) {
		return ConversionPlan::exact_match();
	}

	// --- Target is bool: BooleanConversion [conv.bool] ---
	if (to_category == TypeCategory::Bool) {
		// nullptr_t is not an ordinary conversion to bool for overload resolution.
		if (isIntegralType(from_category) || isFloatingPointType(from_category) || from_category == TypeCategory::Enum) {
			return {ConversionRank::Conversion, StandardConversionKind::BooleanConversion, true};
		}
		if (from_category == TypeCategory::Struct) {
			// Struct → Bool: fall through to user-defined conversion check below (operator bool()).
		} else {
			return ConversionPlan::no_match();
		}
	}

	// --- Source is bool ---
	if (from_category == TypeCategory::Bool) {
		// Bool -> int is integral promotion [conv.prom]/6
		if (to_category == TypeCategory::Int) {
			return {ConversionRank::Promotion, StandardConversionKind::IntegralPromotion, true};
		}
		// Bool -> other integral type is integral conversion
		if (isIntegralType(to_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::IntegralConversion, true};
		}
		// Bool -> floating-point is floating-integral conversion
		if (isFloatingPointType(to_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::FloatingIntegralConversion, true};
		}
		if (to_category == TypeCategory::Struct) {
			// Bool → Struct: fall through to user-defined conversion check below (converting constructor).
		} else {
			return ConversionPlan::no_match();
		}
	}

	// --- Integral -> Integral ---
	if (isIntegralType(from_category) && isIntegralType(to_category)) {
		const int INT_RANK = 3; // rank of int/unsigned int in get_integer_rank()
		const int from_rank = get_integer_rank(from_category);

		// C++20 [conv.prom]: the target of integral promotion is determined from
		// the source type and target data model.  int and unsigned int share an
		// integer conversion rank, but only promote_integer_type(from_category)
		// is a promotion target; conversion to the other type is an integral
		// conversion and therefore ranks lower in overload resolution.
		if (from_rank < INT_RANK &&
			to_category == promote_integer_type(from_category)) {
			return {ConversionRank::Promotion, StandardConversionKind::IntegralPromotion, true};
		}
		return {ConversionRank::Conversion, StandardConversionKind::IntegralConversion, true};
	}

	// --- Floating-point promotion: float -> double [conv.fpprom] ---
	if (from_category == TypeCategory::Float && to_category == TypeCategory::Double) {
		return {ConversionRank::Promotion, StandardConversionKind::FloatingPromotion, true};
	}

	// --- Floating-point -> Floating-point ---
	if (isFloatingPointType(from_category) && isFloatingPointType(to_category)) {
		return {ConversionRank::Conversion, StandardConversionKind::FloatingConversion, true};
	}

	// --- Integral -> Floating-point ---
	if (isIntegralType(from_category) && isFloatingPointType(to_category)) {
		return {ConversionRank::Conversion, StandardConversionKind::FloatingIntegralConversion, true};
	}

	// --- Floating-point -> Integral ---
	if (isFloatingPointType(from_category) && isIntegralType(to_category)) {
		return {ConversionRank::Conversion, StandardConversionKind::FloatingIntegralConversion, true};
	}

	// --- Unscoped enum -> integer/floating-point [conv.prom]/4, [conv.integral] ---
	if (from_category == TypeCategory::Enum) {
		if (to_category == TypeCategory::Int) {
			return {ConversionRank::Promotion, StandardConversionKind::IntegralPromotion, true};
		}
		if (isIntegralType(to_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::IntegralConversion, true};
		}
		if (isFloatingPointType(to_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::FloatingIntegralConversion, true};
		}
		// Enum → Struct: falls through to user-defined conversion check below.
		// Enum → Enum (different types): falls through to no_match() at end; no implicit conversion in C++.
	}

	// Note: Integer to unscoped enum is NOT an implicit conversion in C++11+.
	// It requires a static_cast. Do NOT add a conversion path here.

	// --- User-defined conversions ---
	// Struct-to-primitive: optimistically assume conversion operator exists, CodeGen will verify
	if (from_category == TypeCategory::Struct && to_category != TypeCategory::Struct) {
		return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
	}
	// Primitive-to-struct: converting constructors
	if (to_category == TypeCategory::Struct && from_category != TypeCategory::Struct) {
		return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
	}

	// No valid conversion
	return ConversionPlan::no_match();
}

inline std::pair<TypeId, CVQualifier> stripCanonicalTopCv(
	const CanonicalTypeTable& table,
	TypeId type) {
	CanonicalTypeNode node = table.node(type);
	CVQualifier qualifiers = CVQualifier::None;
	while (node.kind == CanonicalTypeKind::Qualified) {
		qualifiers |= node.qualifiers;
		type = node.child;
		node = table.node(type);
	}
	return {type, qualifiers};
}

inline ConversionPlan buildCanonicalStructuralConversionPlan(
	CanonicalTypeTable& table,
	TypeId source_type,
	TypeId target_type) {
	if (!source_type || !target_type) {
		return ConversionPlan::no_match();
	}
	if (source_type == target_type) {
		return ConversionPlan::exact_match();
	}

	auto topObjectCvThroughArrays = [&table](TypeId type) {
		CVQualifier qualifiers = CVQualifier::None;
		for (;;) {
			const auto [unqualified, top_cv] = stripCanonicalTopCv(table, type);
			qualifiers |= top_cv;
			const CanonicalTypeNode node = table.node(unqualified);
			if (node.kind != CanonicalTypeKind::Array) {
				return qualifiers;
			}
			type = node.child;
		}
	};
	auto compatibleFunctionTarget = [&table](TypeId source, TypeId target) {
		const CanonicalTypeNode source_function = table.node(source);
		const CanonicalTypeNode target_function = table.node(target);
		if (source_function.kind != CanonicalTypeKind::Function ||
			target_function.kind != CanonicalTypeKind::Function ||
			source_function.child != target_function.child ||
			source_function.builtin != target_function.builtin ||
			source_function.qualifiers != target_function.qualifiers) {
			return false;
		}
		const uint8_t source_flags = static_cast<uint8_t>(source_function.flags);
		const uint8_t target_flags = static_cast<uint8_t>(target_function.flags);
		const uint8_t noexcept_flag =
			static_cast<uint8_t>(CanonicalTypeNodeFlags::NoexceptFunction);
		const bool source_is_noexcept = (source_flags & noexcept_flag) != 0;
		const bool target_is_noexcept = (target_flags & noexcept_flag) != 0;
		if (target_is_noexcept && !source_is_noexcept) {
			return false;
		}
		if ((source_flags & ~noexcept_flag) != (target_flags & ~noexcept_flag) ||
			table.functionDependentNoexcept(source) !=
				table.functionDependentNoexcept(target)) {
			return false;
		}
		TypeId source_parameter = table.functionParameters(source);
		TypeId target_parameter = table.functionParameters(target);
		while (source_parameter && target_parameter) {
			if (table.functionParameterType(source_parameter) !=
				table.functionParameterType(target_parameter)) {
				return false;
			}
			source_parameter = table.functionParameterNext(source_parameter);
			target_parameter = table.functionParameterNext(target_parameter);
		}
		return !source_parameter && !target_parameter;
	};
	auto isBuiltin = [&table](TypeId type, CanonicalBuiltinKind builtin) {
		const TypeId unqualified = stripCanonicalTopCv(table, type).first;
		const CanonicalTypeNode node = table.node(unqualified);
		return node.kind == CanonicalTypeKind::Builtin && node.builtin == builtin;
	};
	while (table.node(source_type).kind == CanonicalTypeKind::LValueReference ||
		table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
		source_type = table.node(source_type).child;
	}
	while (table.node(target_type).kind == CanonicalTypeKind::LValueReference ||
		table.node(target_type).kind == CanonicalTypeKind::RValueReference) {
		// Reference binding needs value-category and materialization facts that
		// are not represented by this TypeId-only structural conversion slice.
		return ConversionPlan::no_match();
	}
	if (source_type == target_type) {
		return ConversionPlan::exact_match();
	}

	if (isBuiltin(source_type, CanonicalBuiltinKind::Nullptr)) {
		const CanonicalTypeNode target =
			table.node(stripCanonicalTopCv(table, target_type).first);
		if (target.kind == CanonicalTypeKind::Pointer ||
			target.kind == CanonicalTypeKind::MemberObjectPointer ||
			target.kind == CanonicalTypeKind::MemberFunctionPointer) {
			return {ConversionRank::Conversion,
				StandardConversionKind::PointerConversion, true};
		}
	}

	if (isBuiltin(target_type, CanonicalBuiltinKind::Bool)) {
		const CanonicalTypeNode source =
			table.node(stripCanonicalTopCv(table, source_type).first);
		if (source.kind == CanonicalTypeKind::Pointer ||
			source.kind == CanonicalTypeKind::MemberObjectPointer ||
			source.kind == CanonicalTypeKind::MemberFunctionPointer ||
			source.kind == CanonicalTypeKind::Array ||
			source.kind == CanonicalTypeKind::Function) {
			return {ConversionRank::Conversion,
				StandardConversionKind::BooleanConversion, true};
		}
	}

	const CanonicalTypeNode unqualified_source_node =
		table.node(stripCanonicalTopCv(table, source_type).first);
	const CanonicalTypeNode unqualified_target_node =
		table.node(stripCanonicalTopCv(table, target_type).first);
	if (unqualified_source_node.kind == CanonicalTypeKind::Builtin &&
		unqualified_target_node.kind == CanonicalTypeKind::Builtin) {
		const std::optional<TypeCategory> source_category =
			canonicalBuiltinToTypeCategory(unqualified_source_node.builtin);
		const std::optional<TypeCategory> target_category =
			canonicalBuiltinToTypeCategory(unqualified_target_node.builtin);
		if (source_category.has_value() && target_category.has_value()) {
			const ConversionPlan arithmetic_plan = buildConversionPlan(
				*source_category, *target_category);
			if (arithmetic_plan.is_valid) {
				return arithmetic_plan;
			}
		}
	}

	StandardConversionKind decay_kind = StandardConversionKind::None;
	const auto [unqualified_source, source_qualifiers] =
		stripCanonicalTopCv(table, source_type);
	const CanonicalTypeNode source_node = table.node(unqualified_source);
	if (source_node.kind == CanonicalTypeKind::Array) {
		TypeId element_type = source_node.child;
		if (source_qualifiers != CVQualifier::None) {
			element_type = table.qualify(element_type, source_qualifiers);
		}
		source_type = table.pointer(element_type);
		decay_kind = StandardConversionKind::ArrayToPointer;
	} else if (source_node.kind == CanonicalTypeKind::Function) {
		source_type = table.pointer(unqualified_source);
		decay_kind = StandardConversionKind::FunctionToPointer;
	}

	const CanonicalTypeNode source_pointer =
		table.node(stripCanonicalTopCv(table, source_type).first);
	const CanonicalTypeNode target_pointer =
		table.node(stripCanonicalTopCv(table, target_type).first);
	if (source_pointer.kind == CanonicalTypeKind::Pointer &&
		target_pointer.kind == CanonicalTypeKind::Pointer) {
		const CVQualifier source_pointer_cv =
			stripCanonicalTopCv(table, source_type).second;
		const CVQualifier target_pointer_cv =
			stripCanonicalTopCv(table, target_type).second;
		const TypeId source_function =
			stripCanonicalTopCv(table, source_pointer.child).first;
		const TypeId target_function =
			stripCanonicalTopCv(table, target_pointer.child).first;
		if (table.node(source_function).kind == CanonicalTypeKind::Function &&
			table.node(target_function).kind == CanonicalTypeKind::Function &&
			compatibleFunctionTarget(source_function, target_function)) {
			if (decay_kind != StandardConversionKind::None) {
				const bool needs_qualification_adjustment =
					source_function != target_function ||
					source_pointer_cv != target_pointer_cv;
				return {
					needs_qualification_adjustment
						? ConversionRank::QualificationAdjustment
						: ConversionRank::ExactMatch,
					decay_kind,
					true};
			}
			return ConversionPlan::qualification_adjustment();
		}
		const TypeId source_pointee =
			stripCanonicalTopCv(table, source_pointer.child).first;
		const auto [target_pointee, target_pointee_cv] =
			stripCanonicalTopCv(table, target_pointer.child);
		const CanonicalTypeNode source_pointee_node = table.node(source_pointee);
		const CanonicalTypeNode target_pointee_node = table.node(target_pointee);
		const CVQualifier source_object_cv =
			topObjectCvThroughArrays(source_pointer.child);
		if (source_pointee_node.kind != CanonicalTypeKind::Function &&
			target_pointee_node.kind == CanonicalTypeKind::Builtin &&
			target_pointee_node.builtin == CanonicalBuiltinKind::Void &&
			(static_cast<uint8_t>(source_object_cv) &
				~static_cast<uint8_t>(target_pointee_cv)) == 0) {
			return {ConversionRank::Conversion,
				decay_kind == StandardConversionKind::None
					? StandardConversionKind::PointerConversion
					: decay_kind,
				true};
		}
	}

	std::vector<CVQualifier> target_intermediate_pointer_cv;
	size_t pointer_depth = 0;
	bool qualification_changed = false;
	TypeId from = source_type;
	TypeId to = target_type;
	for (;;) {
		const auto [from_unqualified, from_cv] = stripCanonicalTopCv(table, from);
		const auto [to_unqualified, to_cv] = stripCanonicalTopCv(table, to);
		const CanonicalTypeNode from_node = table.node(from_unqualified);
		const CanonicalTypeNode to_node = table.node(to_unqualified);
		if (pointer_depth != 0) {
			const bool added_qualification =
				(static_cast<uint8_t>(from_cv) &
					~static_cast<uint8_t>(to_cv)) == 0;
			if (!added_qualification) {
				return ConversionPlan::no_match();
			}
			if (from_cv != to_cv) {
				qualification_changed = true;
				for (size_t index = 0; index + 1 < pointer_depth; ++index) {
					if ((static_cast<uint8_t>(target_intermediate_pointer_cv[index]) &
						static_cast<uint8_t>(CVQualifier::Const)) == 0) {
						return ConversionPlan::no_match();
					}
				}
			}
		}
		if (from_node.kind != to_node.kind) {
			return ConversionPlan::no_match();
		}
		switch (from_node.kind) {
		case CanonicalTypeKind::Pointer:
			if (pointer_depth != 0) {
				target_intermediate_pointer_cv.push_back(to_cv);
			}
			++pointer_depth;
			from = from_node.child;
			to = to_node.child;
			break;
		case CanonicalTypeKind::Array:
			if (from_node.array_extent != to_node.array_extent ||
				from_node.flags != to_node.flags) {
				return ConversionPlan::no_match();
			}
			from = from_node.child;
			to = to_node.child;
			break;
		case CanonicalTypeKind::Builtin:
			if (from_node.builtin != to_node.builtin) {
				return ConversionPlan::no_match();
			}
			if (decay_kind != StandardConversionKind::None) {
				return {
					qualification_changed
						? ConversionRank::QualificationAdjustment
						: ConversionRank::ExactMatch,
					decay_kind,
					true};
			}
			return qualification_changed
				? ConversionPlan::qualification_adjustment()
				: ConversionPlan::exact_match();
		case CanonicalTypeKind::Record:
		case CanonicalTypeKind::Enum:
		case CanonicalTypeKind::TemplateParameter:
			if (from_unqualified != to_unqualified ||
				from_node.array_extent != to_node.array_extent) {
				return ConversionPlan::no_match();
			}
			if (decay_kind != StandardConversionKind::None) {
				return {
					qualification_changed
						? ConversionRank::QualificationAdjustment
						: ConversionRank::ExactMatch,
					decay_kind,
					true};
			}
			return qualification_changed
				? ConversionPlan::qualification_adjustment()
				: ConversionPlan::exact_match();
		case CanonicalTypeKind::MemberObjectPointer:
			if (table.memberPointerOwner(from_unqualified) !=
				table.memberPointerOwner(to_unqualified)) {
				return ConversionPlan::no_match();
			}
			++pointer_depth;
			from = from_node.child;
			to = to_node.child;
			break;
		default:
			// Dependent/template composite payloads need their own TypeId
			// conversion rules before this slice can safely compare them.
			if (from_unqualified == to_unqualified) {
				if (decay_kind != StandardConversionKind::None) {
					return {
						qualification_changed
							? ConversionRank::QualificationAdjustment
							: ConversionRank::ExactMatch,
						decay_kind,
						true};
				}
				return qualification_changed
					? ConversionPlan::qualification_adjustment()
					: ConversionPlan::exact_match();
			}
			if (from_node.kind == CanonicalTypeKind::Function &&
				to_node.kind == CanonicalTypeKind::Function &&
				decay_kind == StandardConversionKind::FunctionToPointer &&
				compatibleFunctionTarget(from_unqualified, to_unqualified)) {
				return {ConversionRank::Conversion,
					StandardConversionKind::FunctionToPointer, true};
			}
			if (from_node.kind == CanonicalTypeKind::MemberFunctionPointer &&
				to_node.kind == CanonicalTypeKind::MemberFunctionPointer) {
				const CanonicalTypeNode source_member = table.node(from_unqualified);
				const CanonicalTypeNode target_member = table.node(to_unqualified);
				if (table.memberPointerOwner(from_unqualified) !=
					table.memberPointerOwner(to_unqualified) ||
					!compatibleFunctionTarget(
						stripCanonicalTopCv(table, source_member.child).first,
						stripCanonicalTopCv(table, target_member.child).first)) {
					return ConversionPlan::no_match();
				}
				return ConversionPlan::qualification_adjustment();
			}
			return ConversionPlan::no_match();
		}
	}
}

// Resolve Enum to its underlying integer category.
// Returns the category unchanged if it is not an enum or the TypeIndex is invalid.
inline TypeCategory resolveEnumUnderlyingTypeCategory(TypeIndex type_index) {
	TypeCategory cat = type_index.category();
	if (cat == TypeCategory::Enum && type_index.is_valid()) {
		if (const EnumTypeInfo* ei = getTypeInfo(type_index).getEnumInfo())
			return ei->underlying_type;
	}
	return cat;
}

// Check if one type can be implicitly converted to another.
// Returns the conversion rank. Delegates to buildConversionPlan() for the
// unified conversion logic.
inline TypeConversionResult can_convert_type(TypeCategory from, TypeCategory to) {
	return buildConversionPlan(from, to).toResult();
}

inline TypeConversionResult can_convert_type(const TypeSpecifierNode& from, const TypeSpecifierNode& to);
inline std::optional<ConversionPlan> tryBuildCanonicalProjectableConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to);

struct UserDefinedConversionOperatorSelection {
	const FunctionDeclarationNode* function = nullptr;
	TypeIndex declaring_type_index{};
	TypeIndex conversion_target_type{};
	StandardConversionKind trailing_standard_kind = StandardConversionKind::None;
	ConversionRank trailing_standard_rank = ConversionRank::NoMatch;
	CVQualifier member_cv_qualifier = CVQualifier::None;
	bool ambiguous = false;
};

inline std::optional<UserDefinedConversionOperatorSelection>
trySelectCanonicalUserDefinedConversionOperator(
	TypeIndex source_type_index,
	CVQualifier source_cv_qualifier,
	const TypeSpecifierNode& target_type);

// Helper function to find a conversion operator in a struct
// Returns true if a conversion operator exists from source_type to target_type
// This version searches both gTypeInfo (for CodeGen) and gSymbolTable (for Parser/overload resolution)
inline bool hasConversionOperator(TypeIndex source_type_index, TypeCategory target_type, TypeIndex target_type_index) {
	TypeIndex canonical_target_type = canonicalize_conversion_target_type(target_type_index, target_type);
	if (!canonical_target_type.is_valid()) {
		return false;
	}

	// First, try to get struct name from gTypeInfo and search gSymbolTable
	// This is needed during parsing when gTypeInfo.member_functions is not yet populated
	if (const TypeInfo* source_type_info_ptr = tryGetTypeInfo(source_type_index)) {
		const TypeInfo& source_type_info = *source_type_info_ptr;
		std::string_view struct_name = StringTable::getStringView(source_type_info.name());

		// Look up the struct in gSymbolTable
		extern SymbolTable gSymbolTable;
		auto struct_symbol = gSymbolTable.lookup(StringTable::getOrInternStringHandle(struct_name));
		if (struct_symbol.has_value() && struct_symbol->is<StructDeclarationNode>()) {
			const StructDeclarationNode& struct_node = struct_symbol->template as<StructDeclarationNode>();

			// Search member functions in the StructDeclarationNode
			for (const auto& member_func_decl : struct_node.member_functions()) {
				const ASTNode& member_func = member_func_decl.function_declaration;
				if (member_func.template is<FunctionDeclarationNode>()) {
					const auto& func_decl = member_func.template as<FunctionDeclarationNode>();
					std::string_view func_name = func_decl.decl_node().identifier_token().value();
					const ASTNode& type_node = func_decl.decl_node().type_node();
					if (func_name.starts_with("operator ") &&
						type_node.template is<TypeSpecifierNode>() &&
						getCanonicalConversionTargetType(type_node.template as<TypeSpecifierNode>()) == canonical_target_type) {
						return true; // Found conversion operator in parsed struct
					}
				}
			}
		}

		// Also check gTypeInfo.member_functions (for CodeGen where it's populated)
		const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
		if (source_struct_info) {
			// Search member functions for the conversion operator
			for (const auto& member_func : source_struct_info->member_functions) {
				if (member_func.conversion_target_type == canonical_target_type) {
					return true;
				}
			}

			// Search base classes recursively
			for (const auto& base_spec : source_struct_info->base_classes) {
				if (base_spec.type_index.is_valid()) {
					if (hasConversionOperator(base_spec.type_index, target_type, target_type_index)) {
						return true;
					}
				}
			}
		}
	}

	return false;
}

// Shared implementation: walks base_classes recursively and tests for derivation.
// When public_only is true, only Public inheritance paths are considered.
inline bool isTransitivelyDerivedFromImpl(TypeIndex source_idx, TypeIndex base_idx, bool public_only) {
	if (!source_idx.is_valid() || !base_idx.is_valid())
		return false;
	const TypeInfo* source_type = tryGetTypeInfo(source_idx);
	if (!source_type)
		return false;
	const StructTypeInfo* source = source_type->getStructInfo();
	if (!source)
		return false;
	for (const auto& b : source->base_classes) {
		if (public_only && b.access != AccessSpecifier::Public)
			continue;
		if (b.type_index == base_idx)
			return true;
		if (isTransitivelyDerivedFromImpl(b.type_index, base_idx, public_only))
			return true;
	}
	return false;
}

// Check if source_idx is transitively derived from base_idx through public bases only.
// Per C++20 [conv.ptr]/3, implicit derived-to-base conversions require the base to be
// accessible, so private/protected inheritance paths are excluded.
inline bool isTransitivelyDerivedFrom(TypeIndex source_idx, TypeIndex base_idx) {
	return isTransitivelyDerivedFromImpl(source_idx, base_idx, /*public_only=*/true);
}

// Like isTransitivelyDerivedFrom, but ignores access specifiers. Useful to
// distinguish "unrelated types" from "related but inaccessible base" so the
// caller can emit an access-specific diagnostic per C++20 [conv.ptr]/3.
inline bool isTransitivelyDerivedFromAnyAccess(TypeIndex source_idx, TypeIndex base_idx) {
	return isTransitivelyDerivedFromImpl(source_idx, base_idx, /*public_only=*/false);
}

// Relationship between a complete derived object and a requested base subobject.
// Keep this classification separate from the byte-offset lookup: a first-match
// walk is incorrect for ambiguous diamonds and for virtual bases.
enum class DerivedBaseConversionKind : uint8_t {
	NotRelated,
	Inaccessible,
	UniquePublicNonVirtual,
	PublicVirtual,
	Ambiguous,
};

struct DerivedBaseConversionInfo {
	DerivedBaseConversionKind kind = DerivedBaseConversionKind::NotRelated;
	int64_t offset = 0;
	// Index in the derived type's finalized virtual_bases collection.  The
	// offset of a virtual base is object-dependent, so callers must use this
	// index with the runtime virtual-base table/vtable rather than treating
	// offset as a fixed adjustment.
	std::optional<size_t> virtual_base_index;
};

// Classify all inheritance paths from derived_idx to base_idx.  A virtual base
// is recorded by type rather than by path because a virtual base subobject is
// shared by a diamond.  A non-virtual path and a virtual path still describe
// different possible subobjects and are therefore ambiguous.
inline DerivedBaseConversionInfo classifyDerivedBaseConversion(
	TypeIndex derived_idx,
	TypeIndex base_idx,
	TypeIndex access_context_idx) {
	if (!derived_idx.is_valid() || !base_idx.is_valid())
		return {};
	if (derived_idx == base_idx)
		return {DerivedBaseConversionKind::UniquePublicNonVirtual, 0, std::nullopt};

	const TypeInfo* derived_type_info = tryGetTypeInfo(derived_idx);
	const StructTypeInfo* derived_struct =
		derived_type_info ? derived_type_info->getStructInfo() : nullptr;
	if (!derived_struct)
		return {};

	bool saw_inaccessible = false;
	uint32_t non_virtual_path_count = 0;
	uint32_t public_non_virtual_path_count = 0;
	int64_t public_non_virtual_offset = 0;
	std::vector<TypeIndex> public_virtual_subobjects;
	std::vector<TypeIndex> inaccessible_virtual_subobjects;
	auto grants_friend_access = [access_context_idx](const StructTypeInfo& owner) {
		if (!owner.declaration_node) {
			return false;
		}
		const TypeInfo* access_context_info = tryGetTypeInfo(access_context_idx);
		const StructTypeInfo* access_context_struct = access_context_info != nullptr
			? access_context_info->getStructInfo()
			: nullptr;
		if (access_context_struct == nullptr ||
			access_context_struct->declaration_node == nullptr) {
			return false;
		}
		const StructDeclarationNode* access_context_declaration =
			access_context_struct->declaration_node;
		const StructDeclarationNode* access_context_pattern =
			access_context_declaration->injected_class_pattern_declaration();
		const StructDeclarationNode* access_context_template =
			access_context_pattern != nullptr
				? access_context_pattern
				: access_context_declaration;
		for (const ASTNode& friend_node : owner.declaration_node->friend_declarations()) {
			if (!friend_node.is<FriendDeclarationNode>()) {
				continue;
			}
			const FriendDeclarationNode& friend_declaration =
				friend_node.as<FriendDeclarationNode>();
			if (friend_declaration.kind() != FriendKind::Class &&
				friend_declaration.kind() != FriendKind::TemplateClass) {
				continue;
			}
			if (friend_declaration.class_type_index().is_valid() &&
				friend_declaration.class_type_index() == access_context_idx) {
				return true;
			}
			if (friend_declaration.class_declaration() != nullptr &&
				(friend_declaration.class_declaration() == access_context_declaration ||
				 friend_declaration.class_declaration() == access_context_pattern)) {
				return true;
			}
			if (friend_declaration.kind() == FriendKind::TemplateClass &&
				friend_declaration.class_template_decl_id() &&
				access_context_template->has_template_decl_id() &&
				access_context_template->template_decl_id() ==
					friend_declaration.class_template_decl_id()) {
				return true;
			}
		}
		return false;
	};

	auto contains_virtual_subobject = [&](TypeIndex type_index) {
		return std::find(public_virtual_subobjects.begin(), public_virtual_subobjects.end(), type_index) !=
			public_virtual_subobjects.end();
	};
	auto contains_inaccessible_virtual_subobject = [&](TypeIndex type_index) {
		return std::find(inaccessible_virtual_subobjects.begin(), inaccessible_virtual_subobjects.end(), type_index) !=
			inaccessible_virtual_subobjects.end();
	};
	auto base_edge_is_accessible = [&](const StructTypeInfo& owner, AccessSpecifier access) {
		if (access == AccessSpecifier::Public) {
			return true;
		}
		if (!access_context_idx.is_valid() || !owner.own_type_index_.has_value()) {
			return false;
		}

		const TypeIndex owner_type_idx = *owner.own_type_index_;
		if (access_context_idx.index() == owner_type_idx.index()) {
			return true;
		}
		if (grants_friend_access(owner)) {
			return true;
		}
		return access == AccessSpecifier::Protected &&
			isTransitivelyDerivedFromAnyAccess(access_context_idx, owner_type_idx);
	};

	auto visit = [&](const auto& self,
					const StructTypeInfo* current_struct,
					int64_t current_offset,
					bool public_path,
					bool virtual_path) -> void {
		if (!current_struct)
			return;
		for (const auto& base : current_struct->base_classes) {
			const bool next_public_path =
				public_path && base_edge_is_accessible(*current_struct, base.access);
			const bool next_virtual_path = virtual_path || base.is_virtual;
			const int64_t base_offset = current_offset + static_cast<int64_t>(base.offset);
			if (base.type_index == base_idx) {
				if (!next_public_path) {
					saw_inaccessible = true;
					if (next_virtual_path) {
						if (!contains_inaccessible_virtual_subobject(base_idx))
							inaccessible_virtual_subobjects.push_back(base_idx);
					} else {
						++non_virtual_path_count;
					}
				} else if (next_virtual_path) {
					if (!contains_virtual_subobject(base_idx))
						public_virtual_subobjects.push_back(base_idx);
				} else {
					++non_virtual_path_count;
					++public_non_virtual_path_count;
					public_non_virtual_offset = base_offset;
				}
			}

			const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
			const StructTypeInfo* base_struct =
				base_type_info ? base_type_info->getStructInfo() : nullptr;
			self(self, base_struct, base_offset, next_public_path, next_virtual_path);
		}
	};

	visit(visit, derived_struct, 0, true, false);

	if (non_virtual_path_count > 1 ||
		(!public_virtual_subobjects.empty() &&
		 (non_virtual_path_count != 0 || !inaccessible_virtual_subobjects.empty()))) {
		return {DerivedBaseConversionKind::Ambiguous, 0, std::nullopt};
	}
	if (public_non_virtual_path_count == 1 && non_virtual_path_count == 1)
		return {DerivedBaseConversionKind::UniquePublicNonVirtual, public_non_virtual_offset, std::nullopt};
	if (!public_virtual_subobjects.empty()) {
		DerivedBaseConversionInfo result{DerivedBaseConversionKind::PublicVirtual, 0, std::nullopt};
		const auto virtual_base_it = std::find_if(
			derived_struct->virtual_bases.begin(),
			derived_struct->virtual_bases.end(),
			[&](const BaseClassSpecifier& base) { return base.type_index == base_idx; });
		if (virtual_base_it != derived_struct->virtual_bases.end()) {
			result.virtual_base_index = static_cast<size_t>(
				std::distance(derived_struct->virtual_bases.begin(), virtual_base_it));
			result.offset = static_cast<int64_t>(virtual_base_it->offset);
		}
		return result;
	}
	if (saw_inaccessible)
		return {DerivedBaseConversionKind::Inaccessible, 0, std::nullopt};
	return {};
}

inline DerivedBaseConversionInfo classifyDerivedBaseConversion(
	TypeIndex derived_idx,
	TypeIndex base_idx) {
	return classifyDerivedBaseConversion(derived_idx, base_idx, TypeIndex{});
}

inline bool hasUsablePublicDerivedBaseConversion(TypeIndex derived_idx, TypeIndex base_idx) {
	const DerivedBaseConversionKind kind =
		classifyDerivedBaseConversion(derived_idx, base_idx).kind;
	return kind == DerivedBaseConversionKind::UniquePublicNonVirtual ||
		kind == DerivedBaseConversionKind::PublicVirtual;
}

struct CanonicalBaseGraphEdgeView {
	uint32_t target;
	CanonicalAccess access;
	CanonicalRecordBaseFlags flags;
};

template<typename HasSchema, typename DirectBaseCount, typename DirectBaseAt>
inline std::optional<DerivedBaseConversionKind> classifyCanonicalBaseSubobjectGraph(
	uint32_t derived_id,
	uint32_t base_id,
	const HasSchema& has_schema,
	const DirectBaseCount& direct_base_count,
	const DirectBaseAt& direct_base_at) {
	if (derived_id == 0 || base_id == 0) {
		return std::nullopt;
	}
	if (derived_id == base_id) {
		return DerivedBaseConversionKind::UniquePublicNonVirtual;
	}
	if (!has_schema(derived_id)) {
		return std::nullopt;
	}

	constexpr uint32_t invalid_node = UINT32_MAX;
	constexpr uint8_t accessible_flag = 1 << 0;
	constexpr uint8_t virtual_path_flag = 1 << 1;
	constexpr uint8_t expanded_flag = 1 << 2;
	struct SubobjectNode {
		uint32_t type;
		uint32_t first_child_slot;
		uint16_t child_count;
		uint8_t flags;
	};

	std::vector<SubobjectNode> subobjects;
	std::vector<uint32_t> child_slots;
	std::vector<uint32_t> worklist;
	std::unordered_map<uint32_t, uint32_t> virtual_subobjects;
	subobjects.push_back(SubobjectNode{derived_id, 0, 0, accessible_flag});
	worklist.push_back(0);

	for (size_t work_index = 0; work_index < worklist.size(); ++work_index) {
		const uint32_t parent_id = worklist[work_index];
		SubobjectNode parent = subobjects[parent_id];
		if (parent.type == base_id) {
			continue;
		}
		if ((parent.flags & expanded_flag) == 0) {
			if (!has_schema(parent.type)) {
				return std::nullopt;
			}
			const size_t base_count = direct_base_count(parent.type);
			if (base_count > std::numeric_limits<uint16_t>::max() ||
				child_slots.size() > UINT32_MAX - base_count) {
				return std::nullopt;
			}
			parent.first_child_slot = static_cast<uint32_t>(child_slots.size());
			parent.child_count = static_cast<uint16_t>(base_count);
			parent.flags |= expanded_flag;
			child_slots.resize(child_slots.size() + parent.child_count, invalid_node);
			subobjects[parent_id] = parent;
		}

		for (uint16_t base_index = 0; base_index < parent.child_count; ++base_index) {
			const CanonicalBaseGraphEdgeView edge =
				direct_base_at(parent.type, base_index);
			if (edge.target == 0) {
				return std::nullopt;
			}
			const uint32_t child_slot = parent.first_child_slot + base_index;
			const bool is_virtual = hasCanonicalRecordBaseFlag(
				edge.flags, CanonicalRecordBaseFlags::Virtual);
			const bool path_accessible =
				(parent.flags & accessible_flag) != 0 &&
				edge.access == CanonicalAccess::Public;
			const bool path_uses_virtual =
				(parent.flags & virtual_path_flag) != 0 || is_virtual;
			uint32_t child_id = child_slots[child_slot];
			if (child_id == invalid_node && is_virtual) {
				const auto found = virtual_subobjects.find(edge.target);
				if (found != virtual_subobjects.end()) {
					child_id = found->second;
				}
			}
			if (child_id == invalid_node) {
				if (subobjects.size() >= invalid_node) {
					return std::nullopt;
				}
				child_id = static_cast<uint32_t>(subobjects.size());
				const uint8_t child_flags =
					(path_accessible ? accessible_flag : 0) |
					(path_uses_virtual ? virtual_path_flag : 0);
				subobjects.push_back(SubobjectNode{edge.target, 0, 0, child_flags});
				worklist.push_back(child_id);
				if (is_virtual) {
					virtual_subobjects.emplace(edge.target, child_id);
				}
			} else {
				const uint8_t previous_flags = subobjects[child_id].flags;
				uint8_t updated_flags = previous_flags;
				updated_flags |= path_accessible ? accessible_flag : 0;
				updated_flags |= path_uses_virtual ? virtual_path_flag : 0;
				if (updated_flags != previous_flags) {
					subobjects[child_id].flags = updated_flags;
					worklist.push_back(child_id);
				}
			}
			child_slots[child_slot] = child_id;
		}
	}

	uint32_t matching_subobject = invalid_node;
	for (uint32_t subobject_id = 0; subobject_id < subobjects.size(); ++subobject_id) {
		if (subobjects[subobject_id].type != base_id) {
			continue;
		}
		if (matching_subobject != invalid_node) {
			return DerivedBaseConversionKind::Ambiguous;
		}
		matching_subobject = subobject_id;
	}
	if (matching_subobject == invalid_node) {
		return DerivedBaseConversionKind::NotRelated;
	}
	const uint8_t flags = subobjects[matching_subobject].flags;
	if ((flags & accessible_flag) == 0) {
		return DerivedBaseConversionKind::Inaccessible;
	}
	return (flags & virtual_path_flag) != 0
		? DerivedBaseConversionKind::PublicVirtual
		: DerivedBaseConversionKind::UniquePublicNonVirtual;
}

// Classify a derived-to-base relation in the EntityId-keyed record schema.
// Virtual subobjects are shared by record identity. The shared worklist avoids
// using native call depth for source inheritance depth.
inline std::optional<DerivedBaseConversionKind> classifyCanonicalDerivedBaseConversion(
	const CanonicalTypeTable& table,
	EntityId derived_entity,
	EntityId base_entity) {
	const auto has_schema = [&table](uint32_t entity_value) {
		const EntityId entity{entity_value};
		return table.hasRecordLayout(entity) && table.hasRecordFieldSchema(entity);
	};
	const auto direct_base_count = [&table](uint32_t entity_value) {
		return table.recordLayout(EntityId{entity_value}).direct_base_count;
	};
	const auto direct_base_at = [&table](uint32_t entity_value, size_t index) {
		const CanonicalRecordBase base = table.recordBaseAt(EntityId{entity_value}, index);
		return CanonicalBaseGraphEdgeView{base.entity.value, base.access, base.flags};
	};
	return classifyCanonicalBaseSubobjectGraph(
		derived_entity.value, base_entity.value,
		has_schema, direct_base_count, direct_base_at);
}

// Class TypeIds keep template-specialization base identity through inheritance.
// Complete records can use the type graph when one is published; legacy native
// tests and partial callers retain the EntityId schema path otherwise.
inline std::optional<DerivedBaseConversionKind> classifyCanonicalDerivedBaseConversion(
	const CanonicalTypeTable& table,
	TypeId derived_type,
	TypeId base_type) {
	if (!derived_type || !base_type) {
		return std::nullopt;
	}
	const CanonicalTypeKind derived_kind = table.node(derived_type).kind;
	const CanonicalTypeKind base_kind = table.node(base_type).kind;
	if ((derived_kind != CanonicalTypeKind::Record &&
			derived_kind != CanonicalTypeKind::TemplateSpecialization) ||
		(base_kind != CanonicalTypeKind::Record &&
			base_kind != CanonicalTypeKind::TemplateSpecialization)) {
		return std::nullopt;
	}
	if (derived_kind == CanonicalTypeKind::Record &&
		base_kind == CanonicalTypeKind::Record &&
		!table.hasClassBaseSchema(derived_type)) {
		return classifyCanonicalDerivedBaseConversion(
			table,
			table.recordEntity(derived_type),
			table.recordEntity(base_type));
	}
	const auto has_schema = [&table](uint32_t type_value) {
		return table.hasClassBaseSchema(TypeId{type_value});
	};
	const auto direct_base_count = [&table](uint32_t type_value) {
		return table.classBaseCount(TypeId{type_value});
	};
	const auto direct_base_at = [&table](uint32_t type_value, size_t index) {
		const CanonicalClassBase base =
			table.classBaseAt(TypeId{type_value}, index);
		return CanonicalBaseGraphEdgeView{base.type.value, base.access, base.flags};
	};
	return classifyCanonicalBaseSubobjectGraph(
		derived_type.value, base_type.value,
		has_schema, direct_base_count, direct_base_at);
}

// Return the byte offset only for a unique, public, non-virtual base.  Callers
// that need diagnostics must use classifyDerivedBaseConversion() first.
inline std::optional<int64_t> findPublicBaseSubobjectOffset(TypeIndex base_idx, TypeIndex derived_idx) {
	const DerivedBaseConversionInfo info = classifyDerivedBaseConversion(derived_idx, base_idx);
	if (info.kind != DerivedBaseConversionKind::UniquePublicNonVirtual)
		return std::nullopt;
	return info.offset;
}

inline size_t countMinRequiredParameters(std::span<const ASTNode> params) {
	size_t min_required = params.size();
	size_t i = params.size();
	while (i > 0) {
		if (!params[i - 1].is<DeclarationNode>())
			break;
		if (!params[i - 1].as<DeclarationNode>().has_default_value())
			break;
		--min_required;
		--i;
	}
	return min_required;
}

// Probe a StructMemberFunction entry to determine whether it is a viable implicit
// converting constructor (non-explicit, at least one parameter, at most one required
// parameter) and return a pointer to its first parameter's TypeSpecifierNode.
// Returns nullptr when the member is not a constructor, is marked explicit, has no
// parameters, or requires more than one argument.
inline const TypeSpecifierNode* getImplicitCtorFirstParamType(const StructMemberFunction& mf) {
	if (!mf.is_constructor)
		return nullptr;
	const auto& ctor_decl = mf.function_decl.as<ConstructorDeclarationNode>();
	if (ctor_decl.is_explicit())
		return nullptr;
	const std::span<const ASTNode> params = ctor_decl.parameter_nodes();
	const size_t min_required = countMinRequiredParameters(params);
	if (params.empty() || min_required > 1)
		return nullptr;
	if (!params[0].is<DeclarationNode>())
		return nullptr;
	const auto& param_type = params[0].as<DeclarationNode>().type_node();
	if (!param_type.is<TypeSpecifierNode>())
		return nullptr;
	return &param_type.as<TypeSpecifierNode>();
}

// Check if target_struct has a non-explicit converting constructor whose first parameter
// accepts source_type and whose remaining parameters are all defaulted, OR if source
// derives from target (implicit derived-to-base conversion).
// Used to determine if struct-to-struct conversions are viable.
// Only checks gTypeInfo (populated at or before IR-gen time).
// Returns false both when struct info is genuinely absent (caller should then check
// getStructInfo() separately and fall back to UserDefined) and when no constructor is found.
inline bool hasConvertingConstructorFrom(TypeIndex target_idx, TypeIndex source_idx) {
	if (!target_idx.is_valid() || !source_idx.is_valid())
		return false;
	const TypeInfo* target_type = tryGetTypeInfo(target_idx);
	if (!target_type || !tryGetTypeInfo(source_idx))
		return false;
	const StructTypeInfo* target = target_type->getStructInfo();
	if (!target)
		return false;
	// Check if source is a (transitively) derived class of target (derived-to-base conversion)
	if (hasUsablePublicDerivedBaseConversion(source_idx, target_idx))
		return true;
	// Check constructors whose first argument consumes the source and whose
	// remaining arguments are defaulted.
	for (const auto& mf : target->member_functions) {
		const TypeSpecifierNode* first_param = getImplicitCtorFirstParamType(mf);
		if (!first_param)
			continue;
		if (first_param->type_index() == source_idx)
			return true;
	}
	return false;
}

inline bool isDirectCtorTemplateParameterMatch(
	const StructMemberFunction& mf,
	const TypeSpecifierNode& param_spec) {
	if (!mf.function_decl.is<ConstructorDeclarationNode>()) {
		return false;
	}

	const auto& ctor_decl = mf.function_decl.as<ConstructorDeclarationNode>();
	if (!ctor_decl.has_template_parameters()) {
		return false;
	}

	const TypeCategory param_category = param_spec.category();
	if (param_category != TypeCategory::Template &&
		param_category != TypeCategory::TypeAlias &&
		param_category != TypeCategory::UserDefined &&
		!isPlaceholderAutoType(param_category)) {
		return false;
	}

	const StringHandle param_name = param_spec.token().handle();
	if (!param_name.isValid()) {
		return false;
	}

	for (const auto& template_param : ctor_decl.template_parameters()) {
		if (template_param.nameHandle() == param_name) {
			return true;
		}
	}

	return false;
}

inline bool isIntegerLiteralZeroNullPointerConstant(const ASTNode& arg_node);
inline ConversionPlan buildConversionPlan(const TypeSpecifierNode& from, const TypeSpecifierNode& to);
inline ConversionPlan buildConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to,
	const ASTNode* argument_node);

inline bool hasImplicitConvertingConstructorForArgument(
	TypeIndex target_idx,
	const TypeSpecifierNode& source_type,
	const ASTNode* argument_node) {
	if (!target_idx.is_valid()) {
		return false;
	}
	const TypeInfo* target_type = tryGetTypeInfo(target_idx);
	if (!target_type) {
		return false;
	}
	const StructTypeInfo* target = target_type->getStructInfo();
	if (!target) {
		return false;
	}
	for (const auto& mf : target->member_functions) {
		const TypeSpecifierNode* first_param = getImplicitCtorFirstParamType(mf);
		if (!first_param) {
			continue;
		}
		const auto& param_spec = *first_param;
		if (param_spec.is_pointer() || param_spec.is_function_pointer() ||
			param_spec.is_member_function_pointer() || param_spec.is_member_object_pointer()) {
			TypeSpecifierNode effective_source_type = source_type;
			if (argument_node != nullptr &&
				isIntegerLiteralZeroNullPointerConstant(*argument_node) &&
				(source_type.category() == TypeCategory::Nullptr ||
				 isIntegralType(source_type.category()))) {
				effective_source_type.set_type_index(nativeTypeIndex(TypeCategory::Nullptr));
				effective_source_type.set_size_in_bits(get_type_size_bits(TypeCategory::Nullptr));
				effective_source_type.set_reference_qualifier(ReferenceQualifier::None);
			}
			const bool source_is_pointer_like = effective_source_type.is_pointer() ||
				effective_source_type.is_function_pointer() ||
				effective_source_type.is_member_function_pointer() ||
				effective_source_type.is_member_object_pointer() ||
				effective_source_type.category() == TypeCategory::Nullptr;
			if (!source_is_pointer_like) {
				continue;
			}
			const ConversionPlan conversion = buildConversionPlan(effective_source_type, param_spec);
			if (conversion.is_valid) {
				return true;
			}
			continue;
		}
		if (isDirectCtorTemplateParameterMatch(mf, param_spec)) {
			// Constructor templates like `template<class T> _Literal_zero(T)` do
			// not have a concrete first-parameter type to resolve here. Matching
			// the ctor's own template parameter name keeps the viability check
			// narrow while still allowing the later template-deduction path to
			// instantiate the concrete conversion when needed.
			return true;
		}
		if (param_spec.category() == TypeCategory::Struct) {
			if (!source_type.type_index().is_valid() || !param_spec.type_index().is_valid()) {
				continue;
			}
			if (source_type.type_index() == param_spec.type_index() ||
				hasConvertingConstructorFrom(param_spec.type_index(), source_type.type_index())) {
				return true;
			}
			continue;
		}
		const auto conversion = can_convert_type(source_type.category(), param_spec.category());
		if (conversion.is_valid) {
			return true;
		}
	}
	return false;
}

// C++20 [conv.array]/1: an lvalue or rvalue of type "array of N T" or
// "array of unknown bound of T" converts to a prvalue of type "pointer to T".
// For T[N][M], T is itself an array type, so the result is U(*)[M]. Already-
// pointer types, including T(*)[N], are not array objects and do not decay.
inline void applyArrayToPointerConversion(TypeSpecifierNode& spec) {
	if (spec.has_pointee_array_declarator() || !spec.is_array()) {
		return;
	}
	std::vector<size_t> inner_extents;
	const std::span<const size_t> source_dims = spec.array_dimensions();
	if (source_dims.size() > 1) {
		inner_extents.assign(source_dims.begin() + 1, source_dims.end());
	}
	spec.set_array(false, std::nullopt);
	spec.set_reference_qualifier(ReferenceQualifier::None);
	// The new level is the pointer produced by the conversion. Existing
	// levels belong to the array element type (for example, int*[2] has one
	// before decay and becomes int**).
	spec.add_pointer_level();
	if (!inner_extents.empty()) {
		spec.set_pointee_array_dimensions(inner_extents);
		spec.set_pointee_array_declarator(true);
	}
}

inline bool pointerToArrayExtentsCompatible(
	std::span<const size_t> from_extents,
	std::span<const size_t> to_extents) {
	if (from_extents.empty() || to_extents.empty()) {
		return true;
	}
	if (from_extents.size() != to_extents.size()) {
		return false;
	}
	for (size_t i = 0; i < from_extents.size(); ++i) {
		if (from_extents[i] != to_extents[i]) {
			return false;
		}
	}
	return true;
}

// C++20 [conv.ptr]: pointer conversions compare the complete pointee type.
// T* and T(*)[N] are distinct types; there is no implicit conversion between
// them except when the destination is cv void*.
inline bool pointerPointeeArrayTypesMatch(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	if (from.has_pointee_array_declarator() != to.has_pointee_array_declarator()) {
		return false;
	}
	if (!from.has_pointee_array_declarator()) {
		return true;
	}
	return pointerToArrayExtentsCompatible(from.array_dimensions(), to.array_dimensions());
}

// Restore [dcl.ptr]/1 / [dcl.array] shape from a published struct member.
template <typename MemberLike>
inline void applyMemberDeclaratorShape(TypeSpecifierNode& member_type, const MemberLike& member) {
	if (member.pointer_depth > 0) {
		member_type.add_pointer_levels(member.pointer_depth);
	}
	if (member.pointee_array_declarator) {
		member_type.set_pointee_array_declarator(true);
		if (!member.array_dimensions.empty()) {
			member_type.set_pointee_array_dimensions(member.array_dimensions);
		}
		return;
	}
	if (!member.is_array) {
		return;
	}
	if (!member.array_dimensions.empty()) {
		member_type.set_array_dimensions(member.array_dimensions);
	} else {
		member_type.set_array(true, std::nullopt);
	}
}

// StructStaticMember keeps only the flat pointer/array projection, but retains
// the original declaration AST. Recover a non-projectable ordered declarator
// from it so a static member's exact interleaved shape survives into overload
// resolution and lowering.
inline const TypeSpecifierNode* staticMemberDeclaredType(
	const StructStaticMember& member) {
	if (!member.declaration.has_value()) {
		return nullptr;
	}
	const ASTNode& declaration = *member.declaration;
	if (declaration.is<DeclarationNode>()) {
		return &declaration.as<DeclarationNode>().type_specifier_node();
	}
	if (declaration.is<VariableDeclarationNode>()) {
		return &declaration.as<VariableDeclarationNode>()
				.declaration()
				.type_specifier_node();
	}
	return nullptr;
}

inline std::optional<TypeSpecifierNode> orderedTypeFromStaticMemberDeclaration(
	const StructStaticMember& member) {
	const TypeSpecifierNode* declared_type = staticMemberDeclaredType(member);
	if (declared_type == nullptr || !declared_type->has_ordered_declarator() ||
		declared_type->ordered_declarator_has_legacy_projection()) {
		return std::nullopt;
	}
	return *declared_type;
}

// Materialize the single parser-facing compatibility type for a static member.
// Published TypeId owns the declarator shape; the TypeIndex is retained only
// for legacy consumers that still require a base-type projection.
inline TypeSpecifierNode materializeStaticMemberTypeSpecifier(
	CanonicalTypeTable& canonical_types,
	const StructStaticMember& member,
	const Token& token) {
	if (member.canonical_type_id) {
		const CanonicalDeclaratorExport exported = exportCanonicalDeclarator(
			canonical_types,
			member.canonical_type_id);
		if (exported.status != CanonicalTypeImportStatus::Supported) {
			const TypeSpecifierNode* declared_type = staticMemberDeclaredType(member);
			if (exported.status == CanonicalTypeImportStatus::UnmigratedCallable &&
				declared_type != nullptr && declared_type->has_function_signature()) {
				const CanonicalTypeImport imported_declaration = importCanonicalType(
					canonical_types,
					*declared_type);
				if (imported_declaration.status == CanonicalTypeImportStatus::Supported &&
					imported_declaration.type == member.canonical_type_id) {
					return *declared_type;
				}
			}
			throw InternalError(
				"static member published TypeId cannot be materialized for parser lookup");
		}

		TypeId base_type_id = exported.base;
		CVQualifier base_cv = CVQualifier::None;
		CanonicalTypeNode base_node = canonical_types.node(base_type_id);
		while (base_node.kind == CanonicalTypeKind::Qualified) {
			base_cv |= base_node.qualifiers;
			base_type_id = base_node.child;
			base_node = canonical_types.node(base_type_id);
		}

		TypeCategory base_category = member.memberType();
		TypeIndex base_type_index = member.type_index.withCategory(base_category);
		if (base_node.kind == CanonicalTypeKind::Builtin) {
			const std::optional<TypeCategory> builtin_category =
				canonicalBuiltinToTypeCategory(base_node.builtin);
			if (!builtin_category.has_value()) {
				throw InternalError("static member TypeId has an invalid builtin base");
			}
			base_category = *builtin_category;
			base_type_index = nativeTypeIndex(base_category);
		} else if (base_node.kind == CanonicalTypeKind::Record ||
			base_node.kind == CanonicalTypeKind::Enum) {
			const EntityId entity = base_node.kind == CanonicalTypeKind::Record
				? canonical_types.recordEntity(base_type_id)
				: canonical_types.enumEntity(base_type_id);
			const TypeInfo* base_type_info = tryFindTypeInfoByEntityId(entity);
			if (base_type_info == nullptr) {
				throw InternalError("static member TypeId nominal base has no TypeInfo");
			}
			base_category = base_node.kind == CanonicalTypeKind::Record
				? TypeCategory::Struct
				: TypeCategory::Enum;
			base_type_index = base_type_info->registeredTypeIndex()
				.withCategory(base_category);
		}

		TypeSpecifierNode type(
			base_type_index,
			static_cast<int>(member.size * 8),
			token,
			base_cv,
			ReferenceQualifier::None);
		type.set_ordered_declarator(exported.components);
		if (const TypeSpecifierNode* declared_type = staticMemberDeclaredType(member);
			declared_type != nullptr && declared_type->has_function_signature()) {
			type.set_function_signature(declared_type->function_signature());
		}
		return type;
	}

	if (std::optional<TypeSpecifierNode> ordered_type =
			orderedTypeFromStaticMemberDeclaration(member);
		ordered_type.has_value()) {
		return *ordered_type;
	}

	TypeSpecifierNode type(
		member.memberType(),
		TypeQualifier::None,
		static_cast<int>(member.size * 8),
		token,
		member.cv_qualifier);
	type.set_type_index(member.type_index);
	applyMemberDeclaratorShape(type, member);
	type.set_reference_qualifier(member.reference_qualifier);
	return type;
}

// A non-projectable ordered declarator cannot be flattened into the legacy
// pointer/array fields, so overload/conversion resolution must consume the
// ordered spine directly. Compare the resolved base type and callable payload
// that the declarator wraps; the wrapper sequence itself is compared by
// sameOrderedDeclaratorShapeIgnoringCv(), and cv is handled by the
// qualification rule.
inline bool orderedDeclaratorBaseTypeMatches(
	const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	const CanonicalTypeAlias from_canonical =
		canonicalize_type_alias(from.type_index());
	const CanonicalTypeAlias to_canonical =
		canonicalize_type_alias(to.type_index());
	const TypeIndex from_resolved = from_canonical.resolvedTypeIndex();
	const TypeIndex to_resolved = to_canonical.resolvedTypeIndex();
	const TypeCategory from_category = from_resolved.is_valid()
		? from_resolved.category() : from.category();
	const TypeCategory to_category = to_resolved.is_valid()
		? to_resolved.category() : to.category();
	if (from_category != to_category) {
		return false;
	}
	if (needs_type_index(from_category) &&
		from_resolved.is_valid() && to_resolved.is_valid() &&
		from_resolved != to_resolved) {
		return false;
	}
	if (from.has_function_signature() != to.has_function_signature()) {
		return false;
	}
	if (from.has_function_signature() &&
		!FlashCpp::equalFunctionSignatureIdentity(
			from.function_signature(), to.function_signature())) {
		return false;
	}
	return true;
}

// Outer pointer pointee cv for an ordered pointer object. Arrays are transparent
// (an array takes its element type's cv), so this is the cv of the first
// non-array wrapper after the outermost pointer. A function or member-pointer
// pointee is not an object pointer and has no void* conversion.
inline std::optional<CVQualifier> orderedPointerVoidPointeeCv(
	const TypeSpecifierNode& type) {
	const std::span<const DeclaratorComponent> components =
		type.declarator_components();
	if (components.empty() ||
		components.front().kind != DeclaratorComponentKind::Pointer) {
		return std::nullopt;
	}
	for (size_t index = 1; index < components.size(); ++index) {
		switch (components[index].kind) {
		case DeclaratorComponentKind::Array:
		case DeclaratorComponentKind::UnknownBoundArray:
			continue;
		case DeclaratorComponentKind::Pointer:
			return components[index].cv_qualifier;
		default:
			return std::nullopt;
		}
	}
	return std::nullopt;
}

// Same ordered wrapper sequence ignoring per-level cv, so the qualification
// plan can decide cv compatibility separately. Kinds, array extents, callable
// positions, and member owners must match exactly.
inline bool sameOrderedDeclaratorShapeIgnoringCv(
	const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	const std::span<const DeclaratorComponent> from_components =
		from.declarator_components();
	const std::span<const DeclaratorComponent> to_components =
		to.declarator_components();
	if (from_components.size() != to_components.size()) {
		return false;
	}
	for (size_t index = 0; index < from_components.size(); ++index) {
		const DeclaratorComponent& lhs = from_components[index];
		const DeclaratorComponent& rhs = to_components[index];
		if (lhs.kind != rhs.kind || lhs.payload != rhs.payload ||
			lhs.member_owner != rhs.member_owner ||
			lhs.owner_identity_kind != rhs.owner_identity_kind) {
			return false;
		}
	}
	return true;
}

// C++20 [conv.qual] over the ordered pointer chain. Index 0 is the outermost
// pointer object; its cv may be added freely. Deeper pointees (including the
// innermost base) may only add cv when every shallower pointee pointer is
// const in the destination, so `const int (*(*)[3])[4]` is not reachable from
// `int (*(*)[3])[4]` while `int (* const (*)[3])[4]` and
// `const int (* const (*)[3])[4]` are.
inline ConversionPlan orderedDeclaratorCvConversionPlan(
	const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	auto allowsAddition = [](CVQualifier from_cv, CVQualifier to_cv) {
		return (static_cast<uint8_t>(from_cv) & ~static_cast<uint8_t>(to_cv)) == 0;
	};
	auto isConst = [](CVQualifier cv) {
		return (static_cast<uint8_t>(cv) &
			static_cast<uint8_t>(CVQualifier::Const)) != 0;
	};
	std::vector<CVQualifier> from_pointer_cv;
	std::vector<CVQualifier> to_pointer_cv;
	for (const DeclaratorComponent& component : from.declarator_components()) {
		if (component.kind == DeclaratorComponentKind::Pointer) {
			from_pointer_cv.push_back(component.cv_qualifier);
		}
	}
	for (const DeclaratorComponent& component : to.declarator_components()) {
		if (component.kind == DeclaratorComponentKind::Pointer) {
			to_pointer_cv.push_back(component.cv_qualifier);
		}
	}
	if (from_pointer_cv.size() != to_pointer_cv.size()) {
		return ConversionPlan::no_match();
	}
	bool changed = false;
	for (size_t index = 0; index < from_pointer_cv.size(); ++index) {
		if (!allowsAddition(from_pointer_cv[index], to_pointer_cv[index])) {
			return ConversionPlan::no_match();
		}
		if (from_pointer_cv[index] == to_pointer_cv[index]) {
			continue;
		}
		changed = true;
		// Deeper pointer level: every shallower pointee pointer must be const.
		for (size_t shallower = 1; shallower < index; ++shallower) {
			if (!isConst(to_pointer_cv[shallower])) {
				return ConversionPlan::no_match();
			}
		}
	}
	if (!allowsAddition(from.cv_qualifier(), to.cv_qualifier())) {
		return ConversionPlan::no_match();
	}
	if (from.cv_qualifier() != to.cv_qualifier()) {
		changed = true;
		// Changing the innermost base requires every pointee pointer to be const.
		for (size_t index = 1; index < to_pointer_cv.size(); ++index) {
			if (!isConst(to_pointer_cv[index])) {
				return ConversionPlan::no_match();
			}
		}
	}
	return changed ? ConversionPlan::qualification_adjustment()
			   : ConversionPlan::exact_match();
}

// Outermost ordered component is a pointer object. Flat pointer types that
// are not themselves array objects count too, so a decayed ordered array can
// target `T*` or `cv void*`.
inline bool orderedDeclaratorIsPointerObject(const TypeSpecifierNode& type) {
	if (type.has_ordered_declarator() && !type.declarator_components().empty()) {
		return type.declarator_components().front().kind ==
			DeclaratorComponentKind::Pointer;
	}
	return type.is_pointer() && !type.is_array();
}

// Outermost ordered component is a pointer-to-member object. This is a
// [conv.bool] source in its own right but must not be treated as an object
// pointer for decay or pointer-conversion targets, so it stays a separate
// predicate from orderedDeclaratorIsPointerObject.
inline bool orderedDeclaratorIsMemberPointerObject(const TypeSpecifierNode& type) {
	if (type.has_ordered_declarator() && !type.declarator_components().empty()) {
		const DeclaratorComponentKind kind = type.declarator_components().front().kind;
		return kind == DeclaratorComponentKind::MemberObjectPointer ||
			kind == DeclaratorComponentKind::MemberFunctionPointer;
	}
	return type.is_member_object_pointer() || type.is_member_function_pointer();
}

// Outermost ordered component is an array object. Pointer-to-array shapes are
// pointer objects and do not decay ([conv.array]/1).
inline bool orderedDeclaratorIsArrayObject(const TypeSpecifierNode& type) {
	if (!type.has_ordered_declarator() || type.declarator_components().empty()) {
		return false;
	}
	const DeclaratorComponentKind kind = type.declarator_components().front().kind;
	return kind == DeclaratorComponentKind::Array ||
		kind == DeclaratorComponentKind::UnknownBoundArray;
}

// C++20 [conv.array]/1 for a non-projectable array: the outermost bound becomes
// a pointer and the element type, including further interleaving, stays put.
inline void decayOrderedArrayToPointer(TypeSpecifierNode& spec) {
	spec.remove_outermost_ordered_declarator_component();
	spec.prepend_ordered_declarator_component(
		DeclaratorComponent::pointer(CVQualifier::None));
}

// Outermost ordered component is a function object. A pointer to a function
// is already a pointer object and does not decay ([conv.func]/1).
inline bool orderedDeclaratorIsFunctionObject(const TypeSpecifierNode& type) {
	if (!type.has_ordered_declarator() || type.declarator_components().empty()) {
		return false;
	}
	return type.declarator_components().front().kind ==
		DeclaratorComponentKind::Function;
}

// C++20 [conv.func]/1: the function type stays, and a pointer is added outside it.
inline void decayOrderedFunctionToPointer(TypeSpecifierNode& spec) {
	spec.prepend_ordered_declarator_component(
		DeclaratorComponent::pointer(CVQualifier::None));
}

inline bool orderedDeclaratorIsLvalueReference(
	const TypeSpecifierNode& type) {
	if (!type.has_ordered_declarator() || type.declarator_components().empty()) {
		return false;
	}
	return type.declarator_components().front().kind ==
		DeclaratorComponentKind::LValueReference;
}

inline bool orderedDeclaratorIsRvalueReference(
	const TypeSpecifierNode& type) {
	if (!type.has_ordered_declarator() || type.declarator_components().empty()) {
		return false;
	}
	return type.declarator_components().front().kind ==
		DeclaratorComponentKind::RValueReference;
}

inline bool orderedDeclaratorIsReference(const TypeSpecifierNode& type) {
	return orderedDeclaratorIsLvalueReference(type) ||
		orderedDeclaratorIsRvalueReference(type);
}

// Peel an outermost ordered reference and the flat reference qualifier so the
// remaining spine is the referred-to type.
inline void stripOrderedReference(TypeSpecifierNode& spec) {
	if (orderedDeclaratorIsReference(spec)) {
		spec.remove_outermost_ordered_declarator_component();
	}
	spec.set_reference_qualifier(ReferenceQualifier::None);
}

inline TypeId canonicalPromotedFixedEnumUnderlyingType(
	CanonicalTypeTable& table,
	TypeId underlying_type) {
	const CanonicalTypeNode underlying = table.node(
		stripCanonicalTopCv(table, underlying_type).first);
	if (underlying.kind != CanonicalTypeKind::Builtin) {
		return {};
	}
	const std::optional<TypeCategory> underlying_category =
		canonicalBuiltinToTypeCategory(underlying.builtin);
	if (!underlying_category.has_value() ||
		!isIntegralType(*underlying_category)) {
		return {};
	}
	const TypeCategory promoted_category =
		promote_integer_type(*underlying_category);
	if (promoted_category == *underlying_category) {
		return {};
	}
	const TypeSpecifierNode promoted_syntax(
		promoted_category,
		TypeQualifier::None,
		get_type_size_bits(promoted_category),
		Token{},
		CVQualifier::None);
	const CanonicalTypeImport imported = importCanonicalType(table, promoted_syntax);
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		throw InternalError("canonical enum promotion type is not importable");
	}
	return imported.type;
}

inline std::optional<int> tryCompareFixedEnumPromotionTargets(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& lhs_parameter,
	const TypeSpecifierNode& rhs_parameter) {
	FrontendContext* const context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	TypeSpecifierNode canonical_argument = argument_type;
	if (canonical_argument.category() == TypeCategory::Enum) {
		tryBindPublishedTypeEntity(canonical_argument);
	}
	const CanonicalTypeImport argument_import = importCanonicalType(
		table, canonical_argument);
	const CanonicalTypeImport lhs_import = importCanonicalType(
		table, lhs_parameter);
	const CanonicalTypeImport rhs_import = importCanonicalType(
		table, rhs_parameter);
	if (argument_import.status != CanonicalTypeImportStatus::Supported ||
		lhs_import.status != CanonicalTypeImportStatus::Supported ||
		rhs_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const TypeId argument = stripCanonicalTopCv(
		table,
		canonicalTypeWithoutReference(table, argument_import.type)).first;
	if (table.node(argument).kind != CanonicalTypeKind::Enum) {
		return 0;
	}
	const EntityId entity = table.enumEntity(argument);
	if (!table.hasEnumLayout(entity)) {
		return std::nullopt;
	}
	const CanonicalEnumLayout layout = table.enumLayout(entity);
	if (hasCanonicalEnumLayoutFlag(
			layout.flags, CanonicalEnumLayoutFlags::Scoped) ||
		!hasCanonicalEnumLayoutFlag(
			layout.flags, CanonicalEnumLayoutFlags::FixedUnderlying)) {
		return 0;
	}
	const TypeId promoted_underlying =
		canonicalPromotedFixedEnumUnderlyingType(table, layout.underlying_type);
	if (!promoted_underlying || promoted_underlying == layout.underlying_type) {
		return 0;
	}
	const TypeId lhs_target = stripCanonicalTopCv(
		table,
		canonicalTypeWithoutReference(table, lhs_import.type)).first;
	const TypeId rhs_target = stripCanonicalTopCv(
		table,
		canonicalTypeWithoutReference(table, rhs_import.type)).first;
	if (lhs_target == layout.underlying_type &&
		rhs_target == promoted_underlying) {
		return -1;
	}
	if (rhs_target == layout.underlying_type &&
		lhs_target == promoted_underlying) {
		return 1;
	}
	return 0;
}

inline EntityId resolveOverloadRecordEntity(const TypeSpecifierNode& type) {
	if (type.category() != TypeCategory::Struct) {
		return {};
	}
	if (const EntityId bound = resolveNamedTypeEntity(type)) {
		return bound;
	}
	const TypeInfo* type_info = tryGetTypeInfo(type.type_index());
	if (type_info == nullptr || type_info->isTypeAlias()) {
		return {};
	}
	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (struct_info == nullptr || struct_info->declaration_node == nullptr ||
		!struct_info->declaration_node->has_entity_id()) {
		return {};
	}
	return struct_info->declaration_node->entity_id();
}

// Try the structural planner for an ordered overload conversion. Imports are
// speculative: overload candidate checks must not publish temporary TypeIds.
// A null result means the importer or reference-binding rules still need the
// syntax compatibility path; a returned no-match is authoritative for a
// supported non-reference canonical type pair.
inline std::optional<ConversionPlan> tryBuildCanonicalOrderedConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	if (from.is_reference() || from.is_rvalue_reference() ||
		to.is_reference() || to.is_rvalue_reference() ||
		orderedDeclaratorIsReference(from) ||
		orderedDeclaratorIsReference(to)) {
		return std::nullopt;
	}
	FrontendContext* const context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport from_import = importCanonicalType(table, from);
	if (from_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (from_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeImport to_import = importCanonicalType(table, to);
	if (to_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (to_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeKind from_kind = table.node(
		table.withoutTopLevelQualifiers(from_import.type)).kind;
	const CanonicalTypeKind to_kind = table.node(
		table.withoutTopLevelQualifiers(to_import.type)).kind;
	if (from_kind == CanonicalTypeKind::LValueReference ||
		from_kind == CanonicalTypeKind::RValueReference ||
		to_kind == CanonicalTypeKind::LValueReference ||
		to_kind == CanonicalTypeKind::RValueReference) {
		return std::nullopt;
	}
	return buildCanonicalStructuralConversionPlan(
		table, from_import.type, to_import.type);
}

// Use canonical identity for scalar builtin, projectable pointer/array, and
// imported function and member-function pointer pairs. Unsupported callable
// families remain on their compatibility paths.
inline std::optional<ConversionPlan> tryBuildCanonicalProjectableConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	if (from.is_member_object_pointer_type() && to.is_member_object_pointer_type()) {
		FrontendContext* const context = FrontendContext::active();
		if (context == nullptr) {
			return std::nullopt;
		}
		CanonicalTypeTable& table = context->canonicalTypes();
		CanonicalTypeTransaction transaction(table);
		const CanonicalTypeImport from_import = importCanonicalType(table, from);
		if (from_import.status == CanonicalTypeImportStatus::Invalid) {
			return ConversionPlan::no_match();
		}
		if (from_import.status != CanonicalTypeImportStatus::Supported) {
			return std::nullopt;
		}
		const CanonicalTypeImport to_import = importCanonicalType(table, to);
		if (to_import.status == CanonicalTypeImportStatus::Invalid) {
			return ConversionPlan::no_match();
		}
		if (to_import.status != CanonicalTypeImportStatus::Supported) {
			return std::nullopt;
		}
		TypeId source_type = from_import.type;
		while (table.node(source_type).kind == CanonicalTypeKind::LValueReference ||
			table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
			source_type = table.node(source_type).child;
		}
		const CanonicalTypeKind target_kind = table.node(
			stripCanonicalTopCv(table, to_import.type).first).kind;
		if (target_kind == CanonicalTypeKind::LValueReference ||
			target_kind == CanonicalTypeKind::RValueReference) {
			return std::nullopt;
		}
		const TypeId source_member =
			stripCanonicalTopCv(table, source_type).first;
		const TypeId target_member =
			stripCanonicalTopCv(table, to_import.type).first;
		if (table.node(source_member).kind !=
				CanonicalTypeKind::MemberObjectPointer ||
			table.node(target_member).kind !=
				CanonicalTypeKind::MemberObjectPointer) {
			return ConversionPlan::no_match();
		}
		const TypeId source_owner = table.memberPointerOwner(source_member);
		const TypeId target_owner = table.memberPointerOwner(target_member);
		if (source_owner == target_owner) {
			return buildCanonicalStructuralConversionPlan(
				table, source_member, target_member);
		}
		const std::optional<DerivedBaseConversionKind> owner_conversion =
			classifyCanonicalDerivedBaseConversion(
				table, target_owner, source_owner);
		if (!owner_conversion.has_value()) {
			return std::nullopt;
		}
		if (*owner_conversion != DerivedBaseConversionKind::UniquePublicNonVirtual) {
			return ConversionPlan::no_match();
		}

		// [conv.mem] changes the owner from a base to a derived class while
		// preserving the member type. Compare the member type structurally after
		// normalizing only that owner; this also permits a qualification
		// conversion on the member type without consulting TypeIndex identity.
		const TypeId normalized_source = table.memberObjectPointer(
			target_owner, table.memberPointerPointee(source_member));
		const ConversionPlan member_type_plan =
			buildCanonicalStructuralConversionPlan(
				table, normalized_source, target_member);
		if (!member_type_plan.is_valid ||
			(member_type_plan.kind != StandardConversionKind::None &&
				member_type_plan.kind !=
					StandardConversionKind::QualificationAdjustment)) {
			return ConversionPlan::no_match();
		}
		return ConversionPlan{ConversionRank::Conversion,
			StandardConversionKind::PointerConversion, true};
	}
	if (from.has_function_signature() && to.has_function_signature()) {
		if (to.is_reference() || to.is_rvalue_reference() ||
			orderedDeclaratorIsReference(to)) {
			return std::nullopt;
		}
		FrontendContext* const context = FrontendContext::active();
		if (context == nullptr) {
			return std::nullopt;
		}
		CanonicalTypeTable& table = context->canonicalTypes();
		CanonicalTypeTransaction transaction(table);
		const CanonicalTypeImport from_import = importCanonicalType(table, from);
		if (from_import.status == CanonicalTypeImportStatus::Invalid) {
			return ConversionPlan::no_match();
		}
		if (from_import.status != CanonicalTypeImportStatus::Supported) {
			return std::nullopt;
		}
		const CanonicalTypeImport to_import = importCanonicalType(table, to);
		if (to_import.status == CanonicalTypeImportStatus::Invalid) {
			return ConversionPlan::no_match();
		}
		if (to_import.status != CanonicalTypeImportStatus::Supported) {
			return std::nullopt;
		}
		TypeId source_type = from_import.type;
		for (;;) {
			const CanonicalTypeNode source_node = table.node(source_type);
			if (source_node.kind != CanonicalTypeKind::LValueReference &&
				source_node.kind != CanonicalTypeKind::RValueReference) {
				break;
			}
			source_type = source_node.child;
		}
		source_type = stripCanonicalTopCv(table, source_type).first;
		const TypeId target_type =
			stripCanonicalTopCv(table, to_import.type).first;
		auto functionPointerDepth = [&table](TypeId type) -> std::optional<size_t> {
			size_t depth = 0;
			TypeId current = stripCanonicalTopCv(table, type).first;
			while (table.node(current).kind == CanonicalTypeKind::Pointer) {
				++depth;
				current = stripCanonicalTopCv(table, table.node(current).child).first;
			}
			if (table.node(current).kind != CanonicalTypeKind::Function || depth == 0) {
				return std::nullopt;
			}
			return depth;
		};
		auto isMemberFunctionPointer = [&table](TypeId type) {
			return table.node(
				stripCanonicalTopCv(table, type).first).kind ==
				CanonicalTypeKind::MemberFunctionPointer;
		};
		const std::optional<size_t> source_function_pointer_depth =
			functionPointerDepth(source_type);
		const std::optional<size_t> target_function_pointer_depth =
			functionPointerDepth(target_type);
		const bool is_function_pointer_pair =
			source_function_pointer_depth.has_value() &&
			target_function_pointer_depth.has_value();
		const bool is_member_function_pointer_pair =
			isMemberFunctionPointer(source_type) &&
			isMemberFunctionPointer(target_type);
		if (!is_function_pointer_pair && !is_member_function_pointer_pair) {
			return std::nullopt;
		}
		if (is_function_pointer_pair &&
			*source_function_pointer_depth != *target_function_pointer_depth) {
			// int (*)(Args) and int (**)(Args) are distinct pointer types; a
			// depth mismatch is an authoritative no-match, not a reason to fall
			// through to the category-only FunctionPointer compatibility arm.
			return ConversionPlan::no_match();
		}
		if (is_member_function_pointer_pair) {
			const TypeId source_member =
				stripCanonicalTopCv(table, source_type).first;
			const TypeId target_member =
				stripCanonicalTopCv(table, target_type).first;
			const TypeId source_function =
				stripCanonicalTopCv(table, table.node(source_member).child).first;
			const TypeId target_function =
				stripCanonicalTopCv(table, table.node(target_member).child).first;
			const ExprId source_dependent_noexcept =
				table.functionDependentNoexcept(source_function);
			const ExprId target_dependent_noexcept =
				table.functionDependentNoexcept(target_function);
			// An identical dependent exception expression is part of the
			// canonical function identity and can be compared structurally.
			// Different or one-sided expressions still need substitution before
			// overload ranking can decide their relationship.
			if ((source_dependent_noexcept || target_dependent_noexcept) &&
				source_dependent_noexcept != target_dependent_noexcept) {
				return std::nullopt;
			}
			const TypeId source_owner = table.memberPointerOwner(source_member);
			const TypeId target_owner = table.memberPointerOwner(target_member);
			if (source_owner != target_owner) {
				const std::optional<DerivedBaseConversionKind> owner_conversion =
					classifyCanonicalDerivedBaseConversion(
						table, target_owner, source_owner);
				if (!owner_conversion.has_value()) {
					return std::nullopt;
				}
				if (*owner_conversion !=
					DerivedBaseConversionKind::UniquePublicNonVirtual) {
					return ConversionPlan::no_match();
				}
			const TypeId normalized_source =
				table.memberFunctionPointer(target_owner, source_function);
			const ConversionPlan function_plan =
				buildCanonicalStructuralConversionPlan(
					table, normalized_source, target_member);
			if (!function_plan.is_valid ||
				(function_plan.kind != StandardConversionKind::None &&
					function_plan.kind !=
						StandardConversionKind::QualificationAdjustment)) {
				return ConversionPlan::no_match();
			}
			return ConversionPlan{ConversionRank::Conversion,
				StandardConversionKind::PointerConversion, true};
			}
		}
		return buildCanonicalStructuralConversionPlan(
			table, source_type, target_type);
	}
	auto hasNonRecursiveBaseType = [](const TypeSpecifierNode& type) {
		if (type.has_function_signature() || type.has_template_specialization() ||
			type.has_dependent_name_type() || type.has_template_parameter_identity() ||
			type.has_template_parameter_decl() || type.has_member_class() ||
			type.has_concept_constraint() || type.is_pack_expansion()) {
			return false;
		}
		for (const DeclaratorComponent& component : type.declarator_components()) {
			if (component.kind == DeclaratorComponentKind::Function ||
				component.kind == DeclaratorComponentKind::MemberObjectPointer ||
				component.kind == DeclaratorComponentKind::MemberFunctionPointer) {
				return false;
			}
		}
		const TypeCategory category = type.category();
		return is_builtin_type(category) || category == TypeCategory::Struct ||
			category == TypeCategory::Enum;
	};
	const bool may_be_pointer_pair = from.is_pointer() && to.is_pointer();
	const bool may_be_array_decay = from.is_array() && to.is_pointer();
	const bool may_be_boolean_conversion =
		(from.is_pointer() || from.is_array()) &&
		to.category() == TypeCategory::Bool &&
		!to.is_pointer() && !to.is_array();
	const bool may_be_nullptr_pointer_conversion =
		from.category() == TypeCategory::Nullptr && to.is_pointer();
	const bool may_be_builtin_conversion =
		(is_builtin_type(from.category()) || from.category() == TypeCategory::Enum) &&
		is_builtin_type(to.category()) &&
		!from.is_pointer() && !from.is_array() &&
		!to.is_pointer() && !to.is_array();
	const bool may_be_nominal_object_conversion =
		from.category() == TypeCategory::Struct &&
		to.category() == TypeCategory::Struct &&
		!from.is_pointer() && !from.is_array() &&
		!to.is_pointer() && !to.is_array() &&
		!from.has_ordered_declarator() && !to.has_ordered_declarator();
	if (from.is_reference() || from.is_rvalue_reference() ||
		to.is_reference() || to.is_rvalue_reference() ||
		orderedDeclaratorIsReference(from) ||
		orderedDeclaratorIsReference(to) ||
		(!may_be_pointer_pair && !may_be_array_decay &&
			!may_be_boolean_conversion && !may_be_nullptr_pointer_conversion &&
			!may_be_builtin_conversion && !may_be_nominal_object_conversion) ||
		!hasNonRecursiveBaseType(from) || !hasNonRecursiveBaseType(to)) {
		return std::nullopt;
	}
	FrontendContext* const context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	if (may_be_nominal_object_conversion) {
		const EntityId source_entity = resolveOverloadRecordEntity(from);
		const EntityId target_entity = resolveOverloadRecordEntity(to);
		if (source_entity && target_entity) {
			const TypeId source = table.withoutTopLevelQualifiers(
				table.qualify(table.record(source_entity), from.cv_qualifier()));
			const TypeId target = table.withoutTopLevelQualifiers(
				table.qualify(table.record(target_entity), to.cv_qualifier()));
			if (source == target) {
				return ConversionPlan::exact_match();
			}
			const std::optional<DerivedBaseConversionKind> base_conversion =
				classifyCanonicalDerivedBaseConversion(table, source, target);
			if (base_conversion == DerivedBaseConversionKind::UniquePublicNonVirtual ||
				base_conversion == DerivedBaseConversionKind::PublicVirtual) {
				return ConversionPlan{ConversionRank::Conversion,
					StandardConversionKind::DerivedToBase, true};
			}
			// Leave unrelated or inaccessible class pairs to the compatibility path,
			// which also checks converting constructors and context-sensitive access.
			return std::nullopt;
		}
	}
	TypeSpecifierNode canonical_from = from;
	if (from.category() == TypeCategory::Enum) {
		tryBindPublishedTypeEntity(canonical_from);
	}
	const CanonicalTypeImport from_import = importCanonicalType(table, canonical_from);
	if (from_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (from_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeImport to_import = importCanonicalType(table, to);
	if (to_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (to_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeNode from_node = table.node(
		stripCanonicalTopCv(table, from_import.type).first);
	const CanonicalTypeNode to_node = table.node(
		stripCanonicalTopCv(table, to_import.type).first);
	const bool is_enum_arithmetic_conversion =
		from_node.kind == CanonicalTypeKind::Enum &&
		to_node.kind == CanonicalTypeKind::Builtin &&
		(isIntegralType(to.category()) || isFloatingPointType(to.category()));
	if (is_enum_arithmetic_conversion) {
		const EntityId enum_entity = table.enumEntity(
			stripCanonicalTopCv(table, from_import.type).first);
		if (!table.hasEnumLayout(enum_entity)) {
			return std::nullopt;
		}
		const CanonicalEnumLayout layout = table.enumLayout(enum_entity);
		if (hasCanonicalEnumLayoutFlag(
				layout.flags, CanonicalEnumLayoutFlags::Scoped)) {
			// Keep the scoped-enum compatibility route until overload failure can
			// emit ScopedEnumImplicitConversion instead of a generic no-viable-call
			// diagnostic. Semantic analysis still rejects the selected conversion.
			return std::nullopt;
		}
		const bool has_fixed_underlying_type = hasCanonicalEnumLayoutFlag(
			layout.flags, CanonicalEnumLayoutFlags::FixedUnderlying);
		const TypeId promotion_type = has_fixed_underlying_type
			? layout.underlying_type
			: layout.unfixed_promotion_type;
		if (!promotion_type) {
			return std::nullopt;
		}
		const TypeId target_type = stripCanonicalTopCv(
			table, to_import.type).first;
		if (has_fixed_underlying_type) {
			const TypeId promoted_underlying =
				canonicalPromotedFixedEnumUnderlyingType(
					table, layout.underlying_type);
			const TypeId underlying_type = stripCanonicalTopCv(
				table, layout.underlying_type).first;
			if (target_type == underlying_type ||
				(promoted_underlying &&
					stripCanonicalTopCv(table, promoted_underlying).first == target_type)) {
				return ConversionPlan{ConversionRank::Promotion,
					StandardConversionKind::IntegralPromotion, true};
			}
		} else if (stripCanonicalTopCv(table, promotion_type).first == target_type) {
			return ConversionPlan{ConversionRank::Promotion,
				StandardConversionKind::IntegralPromotion, true};
		}
		if (to_node.builtin == CanonicalBuiltinKind::Bool) {
			return ConversionPlan{ConversionRank::Conversion,
				StandardConversionKind::BooleanConversion, true};
		}
		return ConversionPlan{ConversionRank::Conversion,
			isFloatingPointType(to.category())
				? StandardConversionKind::FloatingIntegralConversion
				: StandardConversionKind::IntegralConversion,
			true};
	}
	const bool is_pointer_pair = from_node.kind == CanonicalTypeKind::Pointer &&
		to_node.kind == CanonicalTypeKind::Pointer;
	const bool is_array_decay = from_node.kind == CanonicalTypeKind::Array &&
		to_node.kind == CanonicalTypeKind::Pointer;
	const bool is_boolean_conversion =
		to_node.kind == CanonicalTypeKind::Builtin &&
		to_node.builtin == CanonicalBuiltinKind::Bool &&
		(from_node.kind == CanonicalTypeKind::Pointer ||
			from_node.kind == CanonicalTypeKind::Array);
	const bool is_builtin_conversion =
		from_node.kind == CanonicalTypeKind::Builtin &&
		to_node.kind == CanonicalTypeKind::Builtin;
	const bool is_nullptr_pointer_conversion =
		from_node.kind == CanonicalTypeKind::Builtin &&
		from_node.builtin == CanonicalBuiltinKind::Nullptr &&
		to_node.kind == CanonicalTypeKind::Pointer;
	if (!is_pointer_pair && !is_array_decay && !is_boolean_conversion &&
		!is_builtin_conversion && !is_nullptr_pointer_conversion) {
		return std::nullopt;
	}
	if (is_pointer_pair) {
		const CanonicalTypeNode source_pointer = table.node(
			stripCanonicalTopCv(table, from_import.type).first);
		const CanonicalTypeNode target_pointer = table.node(
			stripCanonicalTopCv(table, to_import.type).first);
		const auto [source_pointee, source_pointee_cv] =
			stripCanonicalTopCv(table, source_pointer.child);
		const auto [target_pointee, target_pointee_cv] =
			stripCanonicalTopCv(table, target_pointer.child);
		const CanonicalTypeNode source_pointee_node = table.node(source_pointee);
		const CanonicalTypeNode target_pointee_node = table.node(target_pointee);
		if (source_pointee_node.kind == CanonicalTypeKind::Record &&
			target_pointee_node.kind == CanonicalTypeKind::Record &&
			source_pointee != target_pointee) {
			if ((static_cast<uint8_t>(source_pointee_cv) &
				~static_cast<uint8_t>(target_pointee_cv)) != 0) {
				return ConversionPlan::no_match();
			}
			const EntityId source_entity = table.recordEntity(source_pointee);
			const EntityId target_entity = table.recordEntity(target_pointee);
			const std::optional<DerivedBaseConversionKind> base_conversion =
				classifyCanonicalDerivedBaseConversion(
					table, source_entity, target_entity);
			if (base_conversion == DerivedBaseConversionKind::UniquePublicNonVirtual ||
				base_conversion == DerivedBaseConversionKind::PublicVirtual) {
				return ConversionPlan{ConversionRank::Conversion,
					StandardConversionKind::DerivedToBase, true};
			}
			// A direct pointer-to-record pair has enough canonical identity to
			// decide viability. Missing hierarchy metadata is fail-closed too.
			return ConversionPlan::no_match();
		}
	}
	const ConversionPlan plan = buildCanonicalStructuralConversionPlan(
		table, from_import.type, to_import.type);
	if (!plan.is_valid) {
		if (is_pointer_pair) {
			return plan;
		}
		return std::nullopt;
	}
	return plan;
}

inline std::optional<UserDefinedConversionOperatorSelection>
trySelectCanonicalUserDefinedConversionOperator(
	TypeIndex source_type_index,
	CVQualifier source_cv_qualifier,
	const TypeSpecifierNode& target_type) {
	if (!source_type_index.is_valid()) {
		return std::nullopt;
	}
	struct PendingType {
		TypeIndex type_index;
		size_t depth;
	};
	struct Candidate {
		UserDefinedConversionOperatorSelection selection;
		size_t depth;
	};
	struct NearestDeclaration {
		TypeIndex conversion_target_type;
		size_t depth;
	};
	std::vector<PendingType> pending_types{{source_type_index, 0}};
	std::vector<TypeIndex> visited_types;
	std::vector<const FunctionDeclarationNode*> visited_functions;
	std::vector<NearestDeclaration> nearest_declarations;
	std::vector<Candidate> candidates;
	const uint8_t source_cv_bits = static_cast<uint8_t>(source_cv_qualifier);
	for (size_t pending_index = 0;
		 pending_index < pending_types.size();
		 ++pending_index) {
		const PendingType pending_type = pending_types[pending_index];
		const TypeIndex current_type = pending_type.type_index;
		if (std::find(visited_types.begin(), visited_types.end(), current_type) !=
			visited_types.end()) {
			continue;
		}
		visited_types.push_back(current_type);
		const TypeInfo* const type_info = tryGetTypeInfo(current_type);
		if (type_info == nullptr) {
			continue;
		}
		const StructTypeInfo* const struct_info = type_info->getStructInfo();
		if (struct_info == nullptr) {
			continue;
		}
		for (const StructMemberFunction& member_function : struct_info->member_functions) {
			if (!member_function.is_conversion_operator() ||
				!member_function.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}
			const TypeIndex conversion_target_type =
				member_function.conversion_target_type;
			if (!conversion_target_type.is_valid()) {
				continue;
			}
			auto nearest_declaration = std::find_if(
				nearest_declarations.begin(),
				nearest_declarations.end(),
				[conversion_target_type](const NearestDeclaration& declaration) {
					return declaration.conversion_target_type == conversion_target_type;
				});
			if (nearest_declaration == nearest_declarations.end()) {
				nearest_declarations.push_back(
					{conversion_target_type, pending_type.depth});
			} else {
				nearest_declaration->depth = std::min(
					nearest_declaration->depth,
					pending_type.depth);
			}
			const FunctionDeclarationNode& function =
				member_function.function_decl.as<FunctionDeclarationNode>();
			if (std::find(visited_functions.begin(), visited_functions.end(), &function) !=
				visited_functions.end()) {
				continue;
			}
			visited_functions.push_back(&function);
			const uint8_t member_cv_bits =
				static_cast<uint8_t>(member_function.cv_qualifier);
			if ((source_cv_bits & ~member_cv_bits) != 0) {
				continue;
			}
			const TypeSpecifierNode& return_type =
				function.decl_node().type_specifier_node();
			FrontendContext& context = requireFrontendContext();
			CanonicalTypeTable& table = context.canonicalTypes();
			CanonicalTypeTransaction return_type_transaction(table);
			TypeSpecifierNode canonical_return_type = return_type;
			tryBindPublishedTypeEntity(canonical_return_type);
			const CanonicalTypeImport return_type_import =
				importCanonicalType(table, canonical_return_type);
			if (return_type_import.status != CanonicalTypeImportStatus::Supported) {
				continue;
			}
			const TypeId unreferenced_return_type = canonicalTypeWithoutReference(
				table, return_type_import.type);
			const CanonicalTypeKind return_type_kind = table.node(
				stripCanonicalTopCv(table, unreferenced_return_type).first).kind;
			if (return_type_kind != CanonicalTypeKind::Pointer &&
				return_type_kind != CanonicalTypeKind::MemberObjectPointer &&
				return_type_kind != CanonicalTypeKind::MemberFunctionPointer &&
				return_type_kind != CanonicalTypeKind::Builtin &&
				return_type_kind != CanonicalTypeKind::Enum) {
				continue;
			}
			const std::optional<ConversionPlan> trailing_plan =
				tryBuildCanonicalProjectableConversionPlan(return_type, target_type);
			if (!trailing_plan.has_value() || !trailing_plan->is_valid ||
				trailing_plan->rank == ConversionRank::UserDefined) {
				continue;
			}

			UserDefinedConversionOperatorSelection candidate;
			candidate.function = &function;
			candidate.declaring_type_index = current_type;
			candidate.conversion_target_type = conversion_target_type;
			candidate.trailing_standard_kind = trailing_plan->kind;
			candidate.trailing_standard_rank = trailing_plan->rank;
			candidate.member_cv_qualifier = member_function.cv_qualifier;
			candidates.push_back({candidate, pending_type.depth});
		}
		for (const BaseClassSpecifier& base_specifier : struct_info->base_classes) {
			if (!base_specifier.is_deferred && base_specifier.type_index.is_valid()) {
				pending_types.push_back(
					{base_specifier.type_index, pending_type.depth + 1});
			}
		}
	}

	std::optional<ConversionRank> best_trailing_rank;
	std::vector<UserDefinedConversionOperatorSelection> visible_candidates;
	for (const Candidate& candidate : candidates) {
		const auto nearest_declaration = std::find_if(
			nearest_declarations.begin(),
			nearest_declarations.end(),
			[&candidate](const NearestDeclaration& declaration) {
				return declaration.conversion_target_type ==
					candidate.selection.conversion_target_type;
			});
		if (nearest_declaration == nearest_declarations.end() ||
			candidate.depth != nearest_declaration->depth) {
			continue;
		}
		if (!best_trailing_rank.has_value() ||
			candidate.selection.trailing_standard_rank < *best_trailing_rank) {
			best_trailing_rank = candidate.selection.trailing_standard_rank;
			visible_candidates.clear();
		}
		if (candidate.selection.trailing_standard_rank == *best_trailing_rank &&
			std::none_of(
				visible_candidates.begin(),
				visible_candidates.end(),
				[&candidate](const UserDefinedConversionOperatorSelection& visible) {
					return visible.function == candidate.selection.function;
				})) {
			visible_candidates.push_back(candidate.selection);
		}
	}
	if (visible_candidates.empty()) {
		return std::nullopt;
	}
	std::vector<UserDefinedConversionOperatorSelection> best_candidates;
	for (const UserDefinedConversionOperatorSelection& candidate : visible_candidates) {
		const uint8_t candidate_cv_bits =
			static_cast<uint8_t>(candidate.member_cv_qualifier);
		const bool is_dominated = std::any_of(
			visible_candidates.begin(),
			visible_candidates.end(),
			[&candidate, candidate_cv_bits](
				const UserDefinedConversionOperatorSelection& other) {
				if (other.function == candidate.function) {
					return false;
				}
				const uint8_t other_cv_bits =
					static_cast<uint8_t>(other.member_cv_qualifier);
				return (other_cv_bits & ~candidate_cv_bits) == 0 &&
					other_cv_bits != candidate_cv_bits;
			});
		if (!is_dominated) {
			best_candidates.push_back(candidate);
		}
	}
	UserDefinedConversionOperatorSelection result = best_candidates.front();
	result.ambiguous = best_candidates.size() != 1;
	return result;
}

// Use canonical TypeIds for reference binding. Value category remains expression
// metadata; same-shape binding preserves qualification ranking, while supported
// standard conversions may materialize a temporary for an eligible reference.
inline std::optional<ConversionPlan> tryBuildCanonicalReferenceBindingPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	const bool to_has_lvalue_reference = to.is_lvalue_reference() ||
		orderedDeclaratorIsLvalueReference(to);
	const bool to_has_rvalue_reference = to.is_rvalue_reference() ||
		orderedDeclaratorIsRvalueReference(to);
	if (!to_has_lvalue_reference && !to_has_rvalue_reference) {
		return std::nullopt;
	}
	FrontendContext* const context = FrontendContext::active();
	if (context == nullptr) {
		return std::nullopt;
	}
	CanonicalTypeTable& table = context->canonicalTypes();
	CanonicalTypeTransaction transaction(table);
	TypeSpecifierNode canonical_source = from;
	if (from.category() == TypeCategory::Enum) {
		tryBindPublishedTypeEntity(canonical_source);
	}
	const CanonicalTypeImport source_import =
		importCanonicalType(table, canonical_source);
	if (source_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (source_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const CanonicalTypeImport target_import = importCanonicalType(table, to);
	if (target_import.status == CanonicalTypeImportStatus::Invalid) {
		return ConversionPlan::no_match();
	}
	if (target_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	const TypeId source_unqualified =
		stripCanonicalTopCv(table, source_import.type).first;
	const TypeId target_unqualified =
		stripCanonicalTopCv(table, target_import.type).first;
	const CanonicalTypeNode source_node = table.node(source_unqualified);
	const CanonicalTypeNode target_node = table.node(target_unqualified);
	const bool target_is_lvalue_reference =
		target_node.kind == CanonicalTypeKind::LValueReference;
	const bool target_is_rvalue_reference =
		target_node.kind == CanonicalTypeKind::RValueReference;
	if (!target_is_lvalue_reference && !target_is_rvalue_reference) {
		return std::nullopt;
	}
	const bool source_is_lvalue = from.is_lvalue_reference() ||
		orderedDeclaratorIsLvalueReference(from) ||
		source_node.kind == CanonicalTypeKind::LValueReference;
	const bool source_is_rvalue = from.is_rvalue_reference() ||
		orderedDeclaratorIsRvalueReference(from) ||
		source_node.kind == CanonicalTypeKind::RValueReference;
	const bool source_has_reference =
		source_node.kind == CanonicalTypeKind::LValueReference ||
		source_node.kind == CanonicalTypeKind::RValueReference;
	const TypeId source_referent = source_has_reference
		? source_node.child
		: source_unqualified;
	const TypeId target_referent = target_node.child;
	const auto [source_type, source_referent_cv] =
		stripCanonicalTopCv(table, source_referent);
	const auto [target_type, target_referent_cv] =
		stripCanonicalTopCv(table, target_referent);
	auto objectCvThroughArrays = [&table](TypeId type) {
		CVQualifier qualifiers = CVQualifier::None;
		for (;;) {
			const auto [unqualified, top_cv] = stripCanonicalTopCv(table, type);
			qualifiers |= top_cv;
			const CanonicalTypeNode node = table.node(unqualified);
			if (node.kind != CanonicalTypeKind::Array) {
				return qualifiers;
			}
			type = node.child;
		}
	};
	const CVQualifier target_object_cv = objectCvThroughArrays(target_referent);
	const bool target_referent_is_const =
		(static_cast<uint8_t>(target_object_cv) &
			static_cast<uint8_t>(CVQualifier::Const)) != 0;
	const bool can_bind_conversion_temporary =
		(target_is_lvalue_reference && target_referent_is_const) ||
		target_is_rvalue_reference;
	if (target_is_lvalue_reference && !source_is_lvalue &&
		!target_referent_is_const) {
		return ConversionPlan::no_match();
	}
	TypeId source_shape = source_type;
	TypeId target_shape = target_type;
	bool same_shape_ignoring_cv = true;
	for (;;) {
		source_shape = stripCanonicalTopCv(table, source_shape).first;
		target_shape = stripCanonicalTopCv(table, target_shape).first;
		const CanonicalTypeNode source_shape_node = table.node(source_shape);
		const CanonicalTypeNode target_shape_node = table.node(target_shape);
		if (source_shape_node.kind != target_shape_node.kind) {
			same_shape_ignoring_cv = false;
			break;
		}
		if (source_shape_node.kind != CanonicalTypeKind::Pointer &&
			source_shape_node.kind != CanonicalTypeKind::Array) {
			same_shape_ignoring_cv = source_shape == target_shape;
			break;
		}
		if (source_shape_node.builtin != target_shape_node.builtin ||
			source_shape_node.flags != target_shape_node.flags ||
			source_shape_node.array_extent != target_shape_node.array_extent) {
			same_shape_ignoring_cv = false;
			break;
		}
		source_shape = source_shape_node.child;
		target_shape = target_shape_node.child;
	}
	if (!same_shape_ignoring_cv) {
		const CanonicalTypeNode unqualified_source_node = table.node(source_type);
		const CanonicalTypeNode unqualified_target_node = table.node(target_type);
		const bool pointer_pair =
			unqualified_source_node.kind == CanonicalTypeKind::Pointer &&
			unqualified_target_node.kind == CanonicalTypeKind::Pointer;
		const bool member_object_pointer_pair =
			unqualified_source_node.kind ==
				CanonicalTypeKind::MemberObjectPointer &&
			unqualified_target_node.kind ==
				CanonicalTypeKind::MemberObjectPointer;
		const bool member_function_pointer_pair =
			unqualified_source_node.kind ==
				CanonicalTypeKind::MemberFunctionPointer &&
			unqualified_target_node.kind ==
				CanonicalTypeKind::MemberFunctionPointer;
		if (pointer_pair || member_object_pointer_pair ||
			member_function_pointer_pair) {
			TypeSpecifierNode source_value = from;
			TypeSpecifierNode target_value = to;
			stripOrderedReference(source_value);
			stripOrderedReference(target_value);
			const std::optional<ConversionPlan> pointer_conversion_plan =
				tryBuildCanonicalProjectableConversionPlan(
					source_value, target_value);
			if (pointer_conversion_plan.has_value()) {
				if (!can_bind_conversion_temporary &&
					pointer_conversion_plan->is_valid) {
					return ConversionPlan::no_match();
				}
				return pointer_conversion_plan;
			}
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Array &&
			unqualified_target_node.kind == CanonicalTypeKind::Pointer) {
			if (!can_bind_conversion_temporary) {
				return ConversionPlan::no_match();
			}
			const ConversionPlan array_decay_plan =
				buildCanonicalStructuralConversionPlan(
					table, source_type, target_type);
			if (!array_decay_plan.is_valid ||
				array_decay_plan.kind != StandardConversionKind::ArrayToPointer) {
				return ConversionPlan::no_match();
			}
			return array_decay_plan;
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Function &&
			unqualified_target_node.kind == CanonicalTypeKind::Pointer) {
			if (!can_bind_conversion_temporary) {
				return ConversionPlan::no_match();
			}
			const ConversionPlan function_decay_plan =
				buildCanonicalStructuralConversionPlan(
					table, source_type, target_type);
			if (!function_decay_plan.is_valid ||
				function_decay_plan.kind !=
					StandardConversionKind::FunctionToPointer) {
				return ConversionPlan::no_match();
			}
			return function_decay_plan;
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Builtin &&
			unqualified_source_node.builtin == CanonicalBuiltinKind::Nullptr &&
			(unqualified_target_node.kind == CanonicalTypeKind::Pointer ||
			 unqualified_target_node.kind == CanonicalTypeKind::MemberObjectPointer ||
			 unqualified_target_node.kind == CanonicalTypeKind::MemberFunctionPointer)) {
			if (!can_bind_conversion_temporary) {
				return ConversionPlan::no_match();
			}
			const ConversionPlan pointer_conversion_plan =
				buildCanonicalStructuralConversionPlan(
					table, source_type, target_type);
			if (!pointer_conversion_plan.is_valid ||
				pointer_conversion_plan.kind !=
					StandardConversionKind::PointerConversion) {
				return ConversionPlan::no_match();
			}
			return pointer_conversion_plan;
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Record &&
			unqualified_target_node.kind == CanonicalTypeKind::Record) {
			if (target_is_rvalue_reference && source_is_lvalue &&
				!source_is_rvalue) {
				return ConversionPlan::no_match();
			}
			if ((static_cast<uint8_t>(source_referent_cv) &
				~static_cast<uint8_t>(target_referent_cv)) != 0) {
				return ConversionPlan::no_match();
			}
			const EntityId source_entity = table.recordEntity(source_type);
			const EntityId target_entity = table.recordEntity(target_type);
			// Resolve the relationship from canonical base edges; do not round-trip
			// nominal TypeIds through the compatibility TypeIndex registry.
			const std::optional<DerivedBaseConversionKind> base_conversion =
				classifyCanonicalDerivedBaseConversion(
					table, source_entity, target_entity);
			if (!base_conversion.has_value()) {
				return std::nullopt;
			}
			if (*base_conversion == DerivedBaseConversionKind::UniquePublicNonVirtual ||
				*base_conversion == DerivedBaseConversionKind::PublicVirtual) {
				return ConversionPlan{ConversionRank::Conversion,
					StandardConversionKind::DerivedToBase, true};
			}
			if (*base_conversion == DerivedBaseConversionKind::Inaccessible ||
				*base_conversion == DerivedBaseConversionKind::Ambiguous) {
				return ConversionPlan::no_match();
			}
			return std::nullopt;
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Array &&
			unqualified_target_node.kind == CanonicalTypeKind::Array) {
			return ConversionPlan::no_match();
		}
		if (unqualified_source_node.kind == CanonicalTypeKind::Enum &&
			unqualified_target_node.kind == CanonicalTypeKind::Builtin) {
			TypeSpecifierNode source_value = from;
			TypeSpecifierNode target_value = to;
			stripOrderedReference(source_value);
			stripOrderedReference(target_value);
			const std::optional<ConversionPlan> conversion_plan =
				tryBuildCanonicalProjectableConversionPlan(
					source_value, target_value);
			if (!conversion_plan.has_value()) {
				return std::nullopt;
			}
			if (!conversion_plan->is_valid) {
				return *conversion_plan;
			}
			if (!can_bind_conversion_temporary) {
				return ConversionPlan::no_match();
			}
			return *conversion_plan;
		}
		const bool is_pointer_like_to_bool =
			unqualified_target_node.kind == CanonicalTypeKind::Builtin &&
			unqualified_target_node.builtin == CanonicalBuiltinKind::Bool &&
			(unqualified_source_node.kind == CanonicalTypeKind::Pointer ||
			 unqualified_source_node.kind ==
				CanonicalTypeKind::MemberObjectPointer ||
			 unqualified_source_node.kind ==
				CanonicalTypeKind::MemberFunctionPointer ||
			 unqualified_source_node.kind == CanonicalTypeKind::Array ||
			 unqualified_source_node.kind == CanonicalTypeKind::Function);
		if (is_pointer_like_to_bool) {
			if (!can_bind_conversion_temporary) {
				return ConversionPlan::no_match();
			}
			return buildCanonicalStructuralConversionPlan(
				table, source_type, target_type);
		}
		if (unqualified_source_node.kind != CanonicalTypeKind::Builtin ||
			unqualified_target_node.kind != CanonicalTypeKind::Builtin) {
			return std::nullopt;
		}
		if (!can_bind_conversion_temporary) {
			return ConversionPlan::no_match();
		}
		return buildCanonicalStructuralConversionPlan(
			table, source_type, target_type);
	}
	if (target_is_rvalue_reference && source_is_lvalue && !source_is_rvalue) {
		return ConversionPlan::no_match();
	}
	if ((static_cast<uint8_t>(source_referent_cv) &
			~static_cast<uint8_t>(target_referent_cv)) != 0) {
		return ConversionPlan::no_match();
	}
	if (table.node(source_type).kind == CanonicalTypeKind::Array) {
		// Adding top-level cv-qualification to the referenced array (or to its
		// ultimate element) through the binding itself is the identity
		// conversion ([over.ics.ref]/1); the mutable-vs-const preference is left
		// to [over.ics.rank]/3.2.6.  Only a structural qualification of an
		// element type, such as a pointer element gaining pointee cv, is a
		// qualification conversion.
		bool qualification_changed = false;
		TypeId source_element = source_type;
		TypeId target_element = target_type;
		for (;;) {
			const auto [source_element_unqualified, source_element_cv] =
				stripCanonicalTopCv(table, source_element);
			const auto [target_element_unqualified, target_element_cv] =
				stripCanonicalTopCv(table, target_element);
			if ((static_cast<uint8_t>(source_element_cv) &
				~static_cast<uint8_t>(target_element_cv)) != 0) {
				return ConversionPlan::no_match();
			}
			const CanonicalTypeNode source_shape_node =
				table.node(source_element_unqualified);
			const CanonicalTypeNode target_shape_node =
				table.node(target_element_unqualified);
			if (source_shape_node.kind != CanonicalTypeKind::Array ||
				target_shape_node.kind != CanonicalTypeKind::Array) {
				if (source_shape_node.kind != target_shape_node.kind) {
					return ConversionPlan::no_match();
				}
				if (source_element_unqualified != target_element_unqualified) {
					const ConversionPlan element_plan =
						buildCanonicalStructuralConversionPlan(
							table, source_element_unqualified,
							target_element_unqualified);
					if (!element_plan.is_valid ||
						(element_plan.kind != StandardConversionKind::None &&
							element_plan.kind !=
							StandardConversionKind::QualificationAdjustment)) {
						return ConversionPlan::no_match();
					}
					qualification_changed |= element_plan.kind ==
						StandardConversionKind::QualificationAdjustment;
				}
				if (qualification_changed && source_is_lvalue) {
					return ConversionPlan::qualification_adjustment();
				}
				return ConversionPlan::exact_match();
			}
			if (source_shape_node.array_extent != target_shape_node.array_extent ||
				source_shape_node.flags != target_shape_node.flags) {
				return ConversionPlan::no_match();
			}
			source_element = source_shape_node.child;
			target_element = target_shape_node.child;
		}
	}
	if (source_type != target_type && !can_bind_conversion_temporary) {
		return ConversionPlan::no_match();
	}
	const ConversionPlan referent_plan =
		buildCanonicalStructuralConversionPlan(table, source_type, target_type);
	if (!referent_plan.is_valid) {
		return ConversionPlan::no_match();
	}
	if (referent_plan.kind != StandardConversionKind::None &&
		referent_plan.kind != StandardConversionKind::QualificationAdjustment) {
		return std::nullopt;
	}
	if (source_referent_cv != target_referent_cv &&
		referent_plan.kind == StandardConversionKind::None) {
		// [over.ics.ref]/1: directly binding to a reference whose referenced
		// type differs only by added top-level cv-qualification is the identity
		// conversion.  The preference of T& over const T& is [over.ics.rank]/3.2.6
		// and is applied when comparing candidate conversion sequences.
		return ConversionPlan::exact_match();
	}
	return referent_plan;
}

// Bounded ordered-declarator conversion path: a null pointer constant to an
// ordered pointer, array-to-pointer decay, function-to-pointer decay, ordered
// reference binding, an ordered object pointer to `cv void*`, array-to-pointer
// or function-to-pointer followed by a boolean conversion, an ordered pointer
// object to `bool`, and same-shape qualification conversions. Derived-to-base
// and further callable-component conversions stay deferred and fail closed
// instead of reaching the flat projection guard.
inline ConversionPlan buildOrderedDeclaratorCompatibilityPlan(
	const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	if (from.category() == TypeCategory::Nullptr) {
		if (!to.declarator_components().empty() &&
			to.declarator_components().front().kind ==
				DeclaratorComponentKind::Pointer) {
			return {ConversionRank::Conversion,
				StandardConversionKind::PointerConversion, true};
		}
		return ConversionPlan::no_match();
	}
	// C++20 [dcl.init.ref]: bind through the outermost ordered reference, then
	// compare the referred-to ordered spines. Derived-to-base stays deferred.
	if (orderedDeclaratorIsReference(to)) {
		const bool to_is_rvalue = orderedDeclaratorIsRvalueReference(to);
		const bool from_is_lvalue = from.is_lvalue_reference() ||
			orderedDeclaratorIsLvalueReference(from);
		const bool from_is_rvalue = from.is_rvalue_reference() ||
			orderedDeclaratorIsRvalueReference(from);
		TypeSpecifierNode referent = to;
		stripOrderedReference(referent);
		TypeSpecifierNode from_value = from;
		stripOrderedReference(from_value);
		auto plan_referent_conversion = [&]() {
			if (from_value.has_ordered_declarator() &&
				referent.has_ordered_declarator() &&
				sameOrderedDeclaratorShapeIgnoringCv(from_value, referent) &&
				orderedDeclaratorBaseTypeMatches(from_value, referent)) {
				return orderedDeclaratorCvConversionPlan(from_value, referent);
			}
			return buildConversionPlan(from_value, referent);
		};
		if (!to_is_rvalue) {
			if (!to.is_const() && !from_is_lvalue) {
				return ConversionPlan::no_match();
			}
			const ConversionPlan plan = plan_referent_conversion();
			if (!plan.is_valid) {
				return ConversionPlan::no_match();
			}
			return plan;
		}
		if (from_is_lvalue && !from_is_rvalue) {
			return ConversionPlan::no_match();
		}
		const ConversionPlan plan = plan_referent_conversion();
		if (!plan.is_valid) {
			return ConversionPlan::no_match();
		}
		return plan;
	}
	if (to.is_reference()) {
		return ConversionPlan::no_match();
	}
	TypeSpecifierNode from_value = from;
	stripOrderedReference(from_value);
	if (orderedDeclaratorIsArrayObject(from_value) &&
		(orderedDeclaratorIsPointerObject(to) ||
			(to.category() == TypeCategory::Void && to.pointer_depth() == 1 &&
				!to.is_reference()))) {
		TypeSpecifierNode decayed_from = from_value;
		decayOrderedArrayToPointer(decayed_from);
		if (to.category() == TypeCategory::Void && to.pointer_depth() == 1 &&
			!to.is_function_pointer() && !to.is_reference()) {
			const std::optional<CVQualifier> pointee_cv =
				orderedPointerVoidPointeeCv(decayed_from);
			if (!pointee_cv.has_value() ||
				(static_cast<uint8_t>(*pointee_cv) &
					~static_cast<uint8_t>(to.cv_qualifier())) != 0) {
				return ConversionPlan::no_match();
			}
			return {ConversionRank::Conversion,
				StandardConversionKind::ArrayToPointer, true};
		}
		const ConversionPlan plan = buildConversionPlan(decayed_from, to);
		if (!plan.is_valid) {
			return ConversionPlan::no_match();
		}
		return {plan.rank, StandardConversionKind::ArrayToPointer, true};
	}
	// C++20 [conv.func]/1: a function object decays to a pointer to that function.
	// The function component stays; only an unqualified pointer is prepended.
	if (orderedDeclaratorIsFunctionObject(from_value) &&
		orderedDeclaratorIsPointerObject(to)) {
		TypeSpecifierNode decayed_from = from_value;
		decayOrderedFunctionToPointer(decayed_from);
		const ConversionPlan plan = buildConversionPlan(decayed_from, to);
		if (!plan.is_valid) {
			return ConversionPlan::no_match();
		}
		return {plan.rank, StandardConversionKind::FunctionToPointer, true};
	}
	// C++20 [conv.bool]: a non-projectable pointer object converts to bool, and
	// an array or function object reaches bool through decay first. The decoded
	// address is tested against zero by the boolean conversion.
	if (to.category() == TypeCategory::Bool && !to.is_reference() &&
		to.pointer_depth() == 0 && !to.has_ordered_declarator() &&
		to.array_dimensions().empty()) {
		TypeSpecifierNode converted_from = from_value;
		if (orderedDeclaratorIsFunctionObject(converted_from)) {
			decayOrderedFunctionToPointer(converted_from);
		}
		if (orderedDeclaratorIsArrayObject(converted_from)) {
			decayOrderedArrayToPointer(converted_from);
		}
		if (orderedDeclaratorIsPointerObject(converted_from) ||
			orderedDeclaratorIsMemberPointerObject(converted_from)) {
			return {ConversionRank::Conversion,
				StandardConversionKind::BooleanConversion, true};
		}
		return ConversionPlan::no_match();
	}
	if (!from_value.has_ordered_declarator()) {
		return ConversionPlan::no_match();
	}
	if (to.category() == TypeCategory::Void && to.pointer_depth() == 1 &&
		!to.is_function_pointer() && !to.is_reference()) {
		const std::optional<CVQualifier> pointee_cv =
			orderedPointerVoidPointeeCv(from_value);
		if (!pointee_cv.has_value()) {
			return ConversionPlan::no_match();
		}
		if ((static_cast<uint8_t>(*pointee_cv) &
				~static_cast<uint8_t>(to.cv_qualifier())) != 0) {
			return ConversionPlan::no_match();
		}
		return {ConversionRank::Conversion,
			StandardConversionKind::PointerConversion, true};
	}
	if (!to.has_ordered_declarator() ||
		!sameOrderedDeclaratorShapeIgnoringCv(from_value, to) ||
		!orderedDeclaratorBaseTypeMatches(from_value, to)) {
		return ConversionPlan::no_match();
	}
	return orderedDeclaratorCvConversionPlan(from_value, to);
}

inline ConversionPlan buildOrderedDeclaratorConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	if (const std::optional<ConversionPlan> canonical_plan =
			tryBuildCanonicalOrderedConversionPlan(from, to);
		canonical_plan.has_value()) {
		return *canonical_plan;
	}
	return buildOrderedDeclaratorCompatibilityPlan(from, to);
}

// Build a unified conversion plan for full TypeSpecifierNode-level conversions.
// Handles the full gamut of TypeSpecifierNode cases:
//   • pointer-to-pointer (depth matching, const qualification, void* conversions)
//   • lvalue / rvalue references (binding rules, ref-qualification compatibility)
//   • user-defined conversions (conversion operators and single-argument constructors)
//   • struct-type matching (by TypeIndex, not just Type::Struct equality)
//   • primitive types — delegates to buildConversionPlan(TypeCategory, TypeCategory)
// Returns ConversionPlan (rank + StandardConversionKind + validity) covering all cases.
//
// IMPORTANT: For correct lvalue-vs-rvalue-reference matching the caller must:
//   • Set is_lvalue_reference(true) on 'from' for lvalue expressions (named variables, etc.)
//   • Leave 'from' as non-reference for rvalue expressions (literals, temporaries, etc.)
inline ConversionPlan buildConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to,
	const ASTNode* argument_node) {
	if (const std::optional<ConversionPlan> reference_plan =
			tryBuildCanonicalReferenceBindingPlan(from, to);
		reference_plan.has_value()) {
		return *reference_plan;
	}
	if (from.category() == TypeCategory::Nullptr && !to.is_reference() &&
		(to.is_pointer() || to.is_function_pointer() ||
		 to.is_member_function_pointer() || to.is_member_object_pointer())) {
		return {ConversionRank::Conversion, StandardConversionKind::PointerConversion, true};
	}
	if ((from.has_ordered_declarator() &&
			!from.ordered_declarator_has_legacy_projection()) ||
		(to.has_ordered_declarator() &&
			!to.ordered_declarator_has_legacy_projection())) {
		return buildOrderedDeclaratorConversionPlan(from, to);
	}
	if (const std::optional<ConversionPlan> canonical_plan =
			tryBuildCanonicalProjectableConversionPlan(from, to);
		canonical_plan.has_value()) {
		return *canonical_plan;
	}
	auto isOrderedPointer = [](const TypeSpecifierNode& type) {
		return type.has_ordered_declarator() &&
			!type.declarator_components().empty() &&
			type.declarator_components().front().kind ==
				DeclaratorComponentKind::Pointer;
	};
	if (from.category() == TypeCategory::Nullptr && isOrderedPointer(to)) {
		return {
			ConversionRank::Conversion,
			StandardConversionKind::PointerConversion,
			true};
	}
	from.require_legacy_declarator_projection("overload/conversion resolution");
	to.require_legacy_declarator_projection("overload/conversion resolution");
	auto stripReferenceQualifier = [](TypeSpecifierNode spec) {
		spec.set_reference_qualifier(ReferenceQualifier::None);
		return spec;
	};
	auto effectiveCategory = [](const TypeSpecifierNode& spec) -> TypeCategory {
		TypeCategory category = spec.category();
		if (spec.type_index().is_valid()) {
			if (const TypeInfo* type_info = tryGetTypeInfo(spec.type_index())) {
				if (const TypeCategory concrete = type_info->typeEnum();
					concrete != TypeCategory::Invalid) {
					category = concrete;
				}
			}
		}
		return category;
	};
	auto hasCompleteStructInfo = [](TypeIndex type_index) -> bool {
		if (!type_index.is_valid() || type_index.index() >= getTypeInfoCount()) {
			return false;
		}
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			return type_info->getStructInfo() != nullptr;
		}
		return false;
	};

	// C++20 [conv.array]/1: array objects (including multidimensional ones)
	// decay to pointer-to-first-element. Remaining extents become the pointee
	// array type. Pointer-to-array declarators are not array objects.
	if (from.is_array() && !from.has_pointee_array_declarator() &&
		!to.is_array() && to.is_pointer()) {
		TypeSpecifierNode decayed_from = from;
		applyArrayToPointerConversion(decayed_from);
		ConversionPlan plan = buildConversionPlan(decayed_from, to, argument_node);
		if (!plan.is_valid)
			return ConversionPlan::no_match();
		return {plan.rank, StandardConversionKind::ArrayToPointer, true};
	}

	if (from.category() == TypeCategory::Nullptr &&
		(to.is_pointer() || to.is_function_pointer() ||
		 to.is_member_function_pointer() || to.is_member_object_pointer())) {
		return {ConversionRank::Conversion, StandardConversionKind::PointerConversion, true};
	}

	// C++20 [conv.bool]: an array lvalue, object pointer, or function pointer
	// converts to bool. Checked before the function-pointer and struct
	// user-defined arms so a pointer with a struct or function pointee is not
	// rejected by their category-only guards. std::nullptr_t is not in the
	// [conv.bool] source list, so it deliberately does not convert here.
	if (to.category() == TypeCategory::Bool && !to.is_reference() &&
		!to.is_array() && to.pointer_depth() == 0 &&
		(from.is_array() || from.is_pointer() || from.is_function_pointer())) {
		return {ConversionRank::Conversion, StandardConversionKind::BooleanConversion, true};
	}

	if (from.is_function_pointer() || to.is_function_pointer()) {
		if (!from.is_function_pointer() || !to.is_function_pointer() ||
			!from.has_function_signature() || !to.has_function_signature()) {
			return ConversionPlan::no_match();
		}
		// Nested function pointers such as int (**)(int) keep their outer
		// wrappers in pointer_levels while remaining FunctionPointer category.
		if (from.pointer_depth() != to.pointer_depth()) {
			return ConversionPlan::no_match();
		}
		const size_t shared_pointer_depth =
			std::min(from.pointer_levels().size(), to.pointer_levels().size());
		for (size_t index = 0; index < shared_pointer_depth; ++index) {
			if (from.pointer_levels()[index].cv_qualifier !=
				to.pointer_levels()[index].cv_qualifier) {
				return ConversionPlan::no_match();
			}
		}
		const FunctionSignature& from_signature = from.function_signature();
		const FunctionSignature& to_signature = to.function_signature();
		if (FlashCpp::equalFunctionSignatureIdentity(from_signature, to_signature)) {
			return ConversionPlan::exact_match();
		}
		if (isPlainNoexceptFunctionPointerConversion(from_signature, to_signature)) {
			return ConversionPlan::qualification_adjustment();
		}
		return ConversionPlan::no_match();
	}

	// Check pointer-to-pointer compatibility FIRST
	// This handles pointer types with lvalue/rvalue flags (which indicate value category, not actual reference types)
	// Pointers with lvalue flags can still be passed to functions expecting pointer parameters
	// IMPORTANT: We use AND (not OR) here. If only one is a pointer, we fall through to allow
	// other conversions like pointer-to-integer for builtins (e.g., __builtin_va_start).
	// Using OR would break va_args since it returns no_match when from is pointer but to is not.
	if (from.is_pointer() && to.is_pointer()) {
		// Pointer depth must match
		if (from.pointer_depth() != to.pointer_depth()) {
			return ConversionPlan::no_match();
		}
		if (from.has_function_signature() != to.has_function_signature()) {
			return ConversionPlan::no_match();
		}
		if (from.has_function_signature()) {
			const FunctionSignature& from_signature = from.function_signature();
			const FunctionSignature& to_signature = to.function_signature();
			if (FlashCpp::equalFunctionSignatureIdentity(from_signature, to_signature)) {
				return ConversionPlan::exact_match();
			}
			if (isPlainNoexceptFunctionPointerConversion(from_signature, to_signature)) {
				return ConversionPlan::qualification_adjustment();
			}
			return ConversionPlan::no_match();
		}

		// Resolve type aliases for both types before comparing
		// This handles cases where template parameters or typedefs resolve to the same underlying type
		// For example: CharT* (where CharT=wchar_t) should match wchar_t*
		const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from.type_index());
		const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to.type_index());
		TypeIndex from_resolved_index = from_canonical.resolvedTypeIndex();
		TypeIndex to_resolved_index = to_canonical.resolvedTypeIndex();
		const TypeCategory from_resolved_category = from_resolved_index.category();
		const TypeCategory to_resolved_category = to_resolved_index.category();

		// C++20 [conv.ptr]: T* and T(*)[N] are distinct pointer types. The
		// only standard exception is conversion of any object pointer to
		// cv void*, which is applied below.
		if (!pointerPointeeArrayTypesMatch(from, to) &&
			to_resolved_category != TypeCategory::Void) {
			return ConversionPlan::no_match();
		}

		const CVQualifier from_pointee_cv = from.pointee_cv_for_pointer_conversion();
		const CVQualifier to_pointee_cv = to.pointee_cv_for_pointer_conversion();
		const uint8_t from_pointee_cv_bits = static_cast<uint8_t>(from_pointee_cv);
		const uint8_t to_pointee_cv_bits = static_cast<uint8_t>(to_pointee_cv);
		const bool pointee_cv_matches = from_pointee_cv_bits == to_pointee_cv_bits;
		const bool pointee_qualification_is_added =
			(from_pointee_cv_bits & ~to_pointee_cv_bits) == 0;

		// Exact type match for pointers (after resolving aliases).
		// For struct types we must additionally compare type_index so that Foo*
		// and Bar* (both Type::Struct) are not treated as the same type.
		if (from_resolved_category == to_resolved_category && pointee_cv_matches) {
			// For struct pointer types, "same resolved Type" is not sufficient —
			// Foo* and Bar* both resolve to Type::Struct.  Compare type_index too.
			if (from_resolved_index.isStruct() &&
				from_resolved_index.is_valid() && to_resolved_index.is_valid() &&
				from_resolved_index != to_resolved_index) {
				// Derived* → Base* is a valid pointer conversion (C++20 [conv.ptr]/3).
				if (from.pointer_depth() == 1 && to.pointer_depth() == 1 &&
					hasUsablePublicDerivedBaseConversion(from_resolved_index, to_resolved_index)) {
					return {ConversionRank::Conversion, StandardConversionKind::DerivedToBase, true};
				}
				return ConversionPlan::no_match();
			}
			return ConversionPlan::exact_match();
		}

		// If base types match but const qualifiers differ.
		// For struct pointer types, different type_index means different types — no match.
		if (from_resolved_category == to_resolved_category) {
			if (from_resolved_index.isStruct() &&
				from_resolved_index.is_valid() && to_resolved_index.is_valid() &&
				from_resolved_index != to_resolved_index) {
				// Derived* → Base* is valid with const qualification adjustment too.
				if (from.pointer_depth() == 1 && to.pointer_depth() == 1 &&
					pointee_qualification_is_added &&
					hasUsablePublicDerivedBaseConversion(from_resolved_index, to_resolved_index)) {
					return {ConversionRank::Conversion, StandardConversionKind::DerivedToBase, true};
				}
				return ConversionPlan::no_match();
			}
			// T* → cv T* is a qualification conversion (C++20 [conv.qual]).
			// Per [over.best.ics.general] Table 12 this is an exact-match-category
			// conversion; we use QualificationAdjustment rank so that a pure-identity
			// match (f(T*)) is still preferred over the adjusted one (f(const T*) or
			// f(volatile T*))
			// via [over.ics.rank]/3.2.1 (proper-subsequence rule).
			if (pointee_qualification_is_added) {
				return {ConversionRank::QualificationAdjustment, StandardConversionKind::QualificationAdjustment, true};
			}
			// Removing const or volatile qualification is not allowed.
			return ConversionPlan::no_match();
		}

		// If one type is still UserDefined after resolution attempt, accept as conversion
		// This allows template parameter types to match concrete types during instantiation
		// Use resolved types here to ensure that resolved typedefs still go through
		// const-correctness checks (e.g., const MyInt* → void* where MyInt is typedef for int)
		if (from_resolved_index.category() == TypeCategory::UserDefined ||
			to_resolved_index.category() == TypeCategory::UserDefined) {
			// Still enforce cv-correctness: cv T* → T* is not allowed.
			if (!pointee_qualification_is_added) {
				return ConversionPlan::no_match();
			}
			return {ConversionRank::Conversion, StandardConversionKind::PointerConversion, true};
		}

		// Pointer conversions: any object pointer can implicitly convert to void*.
		// The conversion may add const/volatile qualification to the pointed-to
		// type, but it cannot remove either qualifier (C++20 [conv.ptr], [conv.qual]).
		if (to_resolved_category == TypeCategory::Void) {
			if (!pointee_qualification_is_added) {
				return ConversionPlan::no_match();
			}
			return {ConversionRank::Conversion, StandardConversionKind::PointerConversion, true};
		}

		return ConversionPlan::no_match();
	}

	// Check reference compatibility
	if (from.is_reference() || to.is_reference()) {
		// If 'to' is a reference, 'from' must be compatible
		if (to.is_reference()) {
			// Check if both are references
			if (from.is_reference()) {
				// Both are references - check reference kind
				bool from_is_rvalue = from.is_rvalue_reference();
				bool to_is_rvalue = to.is_rvalue_reference();

				FLASH_LOG(Parser, Debug, "can_convert_type: both are references. from_is_rvalue=", from_is_rvalue, ", to_is_rvalue=", to_is_rvalue, ", from.type()=", (int)from.type(), ", to.type()=", (int)to.type(), ", from.type_index()=", from.type_index(), ", to.type_index()=", to.type_index());

				// Exact match: both lvalue ref or both rvalue ref, same base type
				const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from.type_index());
				const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to.type_index());
				TypeIndex from_base_index = from_canonical.resolvedTypeIndex();
				TypeIndex to_base_index = to_canonical.resolvedTypeIndex();
				const TypeCategory from_base_category = from_base_index.category();
				const TypeCategory to_base_category = to_base_index.category();
				if (from_is_rvalue == to_is_rvalue && from_base_category == to_base_category) {
					// Two different struct types (e.g. Bar& vs Foo&) both resolve to
					// Type::Struct, so we must also compare type_index.
					if (from_base_index.isStruct() &&
						from_base_index.is_valid() && to_base_index.is_valid() &&
						from_base_index != to_base_index) {
						// Per C++20 [conv.ref]/4: derived lvalue ref binds to base lvalue ref
						// (standard derived-to-base reference conversion).
						if (!from_is_rvalue && !to_is_rvalue &&
							from.pointer_depth() == 0 && to.pointer_depth() == 0 &&
							hasUsablePublicDerivedBaseConversion(from_base_index, to_base_index)) {
							return {ConversionRank::Conversion, StandardConversionKind::DerivedToBase, true};
						}
						return ConversionPlan::no_match();
					}
					const uint8_t from_cv_bits = static_cast<uint8_t>(from.cv_qualifier());
					const uint8_t to_cv_bits = static_cast<uint8_t>(to.cv_qualifier());
					if ((from_cv_bits & ~to_cv_bits) != 0) {
						return ConversionPlan::no_match();
					}
					if (from_cv_bits != to_cv_bits) {
						return {ConversionRank::QualificationAdjustment,
							StandardConversionKind::QualificationAdjustment,
							true};
					}
					return ConversionPlan::exact_match();
				}

				// Reference binding may still be viable through temporary materialization.
				// C++20 [dcl.init.ref]: a const lvalue reference can bind to a temporary
				// materialized from an lvalue/xvalue after a standard conversion, and an
				// rvalue reference can bind to a temporary materialized from an xvalue when
				// a standard conversion is required.
				if (!to_is_rvalue && to.is_const()) {
					auto plan = buildConversionPlan(
						stripReferenceQualifier(from),
						stripReferenceQualifier(to),
						argument_node);
					if (plan.is_valid) {
						// C++20 [over.ics.rank]/3.2.3 prefers binding an rvalue to T&& over
						// binding it to const T& when the implied conversion sequences are
						// otherwise indistinguishable.  Keep the rank as ExactMatch here; the
						// tie-breaker is applied later in resolve_constructor_overload() and
						// resolve_overload() where the competing parameter types are visible.
						return plan;
					}
				}
				if (from_is_rvalue && to_is_rvalue) {
					auto plan = buildConversionPlan(
						stripReferenceQualifier(from),
						stripReferenceQualifier(to),
						argument_node);
					if (plan.is_valid) {
						return plan;
					}
				}

				// Lvalue ref can't bind to rvalue ref parameter, and non-const lvalue refs
				// can't bind to xvalues of a different reference kind.
				return ConversionPlan::no_match();
			} else {
				// 'from' is not a reference, 'to' is a reference
				// Handle binding of non-references to reference parameters

				bool to_is_rvalue = to.is_rvalue_reference();
				bool to_is_const = to.is_const();

				// Check if base types are compatible (resolve aliases like char_type → wchar_t)
				const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from.type_index());
				const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to.type_index());
				TypeIndex from_base_index = from_canonical.resolvedTypeIndex();
				TypeIndex to_base_index = to_canonical.resolvedTypeIndex();
				const TypeCategory from_base_category = from_base_index.category();
				const TypeCategory to_base_category = to_base_index.category();
				bool types_match = (from_base_category == to_base_category);
				// For struct types, "same base type" requires the same type_index.
				if (types_match && from_base_index.isStruct() &&
					from_base_index.is_valid() && to_base_index.is_valid() &&
					from_base_index != to_base_index) {
					types_match = false;
				}
				if (!types_match) {
					// Allow conversions for const lvalue refs and rvalue refs by
					// materializing a temporary of the referred-to type.
					auto plan = buildConversionPlan(
						stripReferenceQualifier(from),
						stripReferenceQualifier(to),
						argument_node);
					if ((!to_is_rvalue && to_is_const && plan.is_valid) ||
						(to_is_rvalue && plan.is_valid)) {
						// Const lvalue ref can bind to values that can be converted
						// and rvalue refs can bind to converted prvalues.
						return plan;
					}
					return ConversionPlan::no_match();
				}

				if (to_is_rvalue) {
					// Rvalue reference can bind to temporaries (prvalues)
					// Non-reference values are treated as rvalues when passed
					return ConversionPlan::exact_match();
				} else {
					// Lvalue reference
					if (to_is_const) {
						// Const lvalue ref can bind to both lvalues and rvalues
						return ConversionPlan::exact_match();
					} else {
						// Non-const lvalue ref can only bind to lvalues
						// In this context, 'from' is not marked as a reference, indicating
						// it represents the value category of a non-lvalue expression (rvalue)
						// Note: The caller must set is_lvalue_reference on 'from' for actual lvalue expressions
						return ConversionPlan::no_match();
					}
				}
			}
		} else {
			// 'from' is a reference, 'to' is not
			// References can be converted to their base type (automatic dereferencing)
			// When copying through a reference, const qualifiers don't matter
			// (e.g., const T& can be copied to T)

			// Resolve type aliases before comparing (e.g., char_type → wchar_t)
			const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from.type_index());
			const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to.type_index());
			TypeIndex from_resolved_index = from_canonical.resolvedTypeIndex();
			TypeIndex to_resolved_index = to_canonical.resolvedTypeIndex();
			const TypeCategory from_resolved_category = from_resolved_index.category();
			const TypeCategory to_resolved_category = to_resolved_index.category();

			if (from_resolved_category == to_resolved_category) {
				// For struct types, "same base type" requires the same type_index.
				// Two different struct types (e.g. Bar& → Foo) both resolve to
				// Type::Struct, so we must also compare type_index.
				if (from_resolved_index.isStruct() &&
					from_resolved_index.is_valid() && to_resolved_index.is_valid() &&
					from_resolved_index != to_resolved_index) {
					// Different struct types: a converting constructor (e.g. Target(const Source&))
					// may allow this conversion. Check gTypeInfo if available.
					if (hasConvertingConstructorFrom(to.type_index(), from.type_index())) {
						return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
					}
					// Struct info not yet finalized (parse-time): optimistically allow.
					if (to.type_index().index() >= getTypeInfoCount() ||
						!getTypeInfo(to.type_index()).getStructInfo()) {
						return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
					}
					return ConversionPlan::no_match();
				}
				return ConversionPlan::exact_match();
			}
			// If one type is still UserDefined after resolution attempt, accept as conversion
			// This handles unresolved template parameter type aliases
			if (from_resolved_index.category() == TypeCategory::UserDefined ||
				to_resolved_index.category() == TypeCategory::UserDefined) {
				return {ConversionRank::Conversion, StandardConversionKind::None, true};
			}
			// Try conversion of the referenced type to target type
			return buildConversionPlan(
				stripReferenceQualifier(from),
				stripReferenceQualifier(to),
				argument_node);
		}
	}

	// Check for user-defined conversion operators
	// For concrete struct/scalar conversions, require a real conversion path once
	// the relevant type metadata exists. Keep optimistic acceptance only for
	// genuinely incomplete parse-time types.
	const TypeCategory effective_from_category = effectiveCategory(from);
	const TypeCategory effective_to_category = effectiveCategory(to);
	std::optional<CanonicalTypeKind> canonical_to_kind;
	// Use canonical shape when the target imports; parser-time unresolved forms
	// still need the compatibility projection below.
	if (effective_from_category == TypeCategory::Struct &&
		(effective_to_category != TypeCategory::Struct || to.is_pointer() ||
			to.has_ordered_declarator())) {
		if (FrontendContext* const context = FrontendContext::active();
			context != nullptr) {
			CanonicalTypeTable& table = context->canonicalTypes();
			CanonicalTypeTransaction transaction(table);
			const CanonicalTypeImport imported_target = importCanonicalType(table, to);
			if (imported_target.status == CanonicalTypeImportStatus::Supported) {
				const TypeId target_type = canonicalTypeWithoutReference(
					table, imported_target.type);
				canonical_to_kind = table.node(
					stripCanonicalTopCv(table, target_type).first).kind;
			}
		}
	}
	const bool target_is_pointer = canonical_to_kind.has_value()
		? *canonical_to_kind == CanonicalTypeKind::Pointer
		: to.is_pointer();
	if (effective_from_category == TypeCategory::Struct &&
		(effective_to_category != TypeCategory::Struct ||
		 target_is_pointer)) {
		if (from.type_index().is_valid()) {
			const bool has_canonical_conversion_target = canonical_to_kind.has_value()
				? (*canonical_to_kind == CanonicalTypeKind::Builtin ||
					*canonical_to_kind == CanonicalTypeKind::Enum ||
					*canonical_to_kind == CanonicalTypeKind::Pointer ||
					*canonical_to_kind == CanonicalTypeKind::MemberObjectPointer ||
					*canonical_to_kind == CanonicalTypeKind::MemberFunctionPointer)
				: (is_builtin_type(effective_to_category) ||
					effective_to_category == TypeCategory::Enum ||
					to.is_pointer() ||
					to.is_function_pointer() ||
					to.is_member_object_pointer_type() ||
					to.is_member_function_pointer());
			if (has_canonical_conversion_target) {
				if (const auto selected_conversion =
					trySelectCanonicalUserDefinedConversionOperator(
						from.type_index(), from.cv_qualifier(), to);
					selected_conversion.has_value()) {
					if (selected_conversion->ambiguous) {
						return ConversionPlan::no_match();
					}
					return {ConversionRank::UserDefined,
						StandardConversionKind::UserDefined,
						true,
						selected_conversion->trailing_standard_rank};
				}
			}
			if (hasConversionOperator(
				from.type_index(), effective_to_category, to.type_index())) {
				return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
			}
		}
		if (!hasCompleteStructInfo(from.type_index())) {
			return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
		}
		return ConversionPlan::no_match();
	}

	// Check for user-defined conversions in reverse: if 'to' is Struct and 'from' is not
	// This handles constructor conversions (not conversion operators, but similar concept).
	if (effective_to_category == TypeCategory::Struct &&
		effective_from_category != TypeCategory::Struct) {
		if (to.type_index().is_valid() &&
			hasImplicitConvertingConstructorForArgument(to.type_index(), from, argument_node)) {
			return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
		}
		if (!hasCompleteStructInfo(to.type_index())) {
			return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
		}
		return ConversionPlan::no_match();
	}

	// Handle UserDefined type aliases:
	// Type aliases like 'size_t' may be stored as Type::UserDefined with type_index=0
	// when they couldn't be fully resolved during parsing. Allow conversions between
	// UserDefined and integral types as they're likely type aliases for integral types.
	const CanonicalTypeAlias from_canonical = canonicalize_type_alias(from.type_index());
	const CanonicalTypeAlias to_canonical = canonicalize_type_alias(to.type_index());
	TypeIndex from_type_index = from_canonical.resolvedTypeIndex();
	TypeIndex to_type_index = to_canonical.resolvedTypeIndex();
	TypeCategory from_type_category = from_type_index.category();
	TypeCategory to_type_category = to_type_index.category();

	// Canonical aliases and parser placeholders can carry a stale category in the
	// TypeIndex while the TypeInfo table has the concrete category (notably enum
	// functional casts parsed through constructor-call syntax). Normalize category
	// from TypeInfo when available so overload resolution compares the real types.
	auto normalizeResolvedCategory = [](TypeIndex& type_index, TypeCategory& category) {
		if (!type_index.is_valid()) {
			return;
		}
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			const TypeCategory concrete = type_info->typeEnum();
			if (concrete != TypeCategory::Invalid && concrete != category) {
				type_index = type_index.withCategory(concrete);
				category = concrete;
			}
		}
	};
	normalizeResolvedCategory(from_type_index, from_type_category);
	normalizeResolvedCategory(to_type_index, to_type_category);

	// If either type is still UserDefined with type_index=0, assume it's an unresolved type alias
	// Allow conversion if the other type is an integral type (common for size_t, ptrdiff_t, etc.)
	if (from_type_index.category() == TypeCategory::UserDefined && !from.type_index().is_valid()) {
		// 'from' is an unresolved type alias - allow if 'to' is integral
		if (isIntegralType(to_type_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::None, true};
		}
	}
	if (to_type_index.category() == TypeCategory::UserDefined && !to.type_index().is_valid()) {
		// 'to' is an unresolved type alias - allow if 'from' is integral
		if (isIntegralType(from_type_category)) {
			return {ConversionRank::Conversion, StandardConversionKind::None, true};
		}
	}

	// Non-pointer, non-reference user-defined categories need resolved TypeIndex
	// checks before falling back to primitive-category conversion.  Structs and
	// enums both share a coarse TypeCategory across distinct concrete types, so
	// overload resolution must compare the resolved type indices to distinguish
	// exact matches from different types.
	if (from_type_index.isStruct() && to_type_index.isStruct() &&
		from_type_index.is_valid() && to_type_index.is_valid()) {
		if (from_type_index == to_type_index) {
			return ConversionPlan::exact_match();
		}
		// Different struct types: check for a converting constructor
		if (hasConvertingConstructorFrom(to.type_index(), from.type_index())) {
			return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
		}
		// Struct info not yet finalized (parse-time): optimistically allow.
		if (to.type_index().index() >= getTypeInfoCount() ||
			!getTypeInfo(to.type_index()).getStructInfo()) {
			return {ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
		}
		return ConversionPlan::no_match();
	}
	if (from_type_index.isEnum() && to_type_index.isEnum() &&
		from_type_index.is_valid() && to_type_index.is_valid()) {
		if (from_type_index == to_type_index) {
			return ConversionPlan::exact_match();
		}
		// C++20 [conv.prom]/4 and [conv.integral] allow enum-to-integral
		// conversions, not implicit conversions between distinct enum types.
		// Unlike structs, enums cannot provide converting constructors.
		return ConversionPlan::no_match();
	}
	if (from_type_index.isEnum() &&
		(isIntegralType(to_type_category) || isFloatingPointType(to_type_category))) {
		FrontendContext* const context = FrontendContext::active();
		if (context != nullptr) {
			CanonicalTypeTable& table = context->canonicalTypes();
			CanonicalTypeTransaction transaction(table);
			TypeSpecifierNode canonical_from = from;
			tryBindPublishedTypeEntity(canonical_from);
			const CanonicalTypeImport imported = importCanonicalType(table, canonical_from);
			if (imported.status == CanonicalTypeImportStatus::Supported) {
				const TypeId enum_type = stripCanonicalTopCv(table, imported.type).first;
				if (table.node(enum_type).kind == CanonicalTypeKind::Enum) {
					const EntityId enum_entity = table.enumEntity(enum_type);
					if (table.hasEnumLayout(enum_entity) &&
						!hasCanonicalEnumLayoutFlag(
							table.enumLayout(enum_entity).flags,
							CanonicalEnumLayoutFlags::Scoped)) {
						recordUnscopedEnumTypeIndexFallback();
					}
				}
			}
		}
	}
	return buildConversionPlan(from_type_category, to_type_category);
}

inline ConversionPlan buildConversionPlan(
	const TypeSpecifierNode& from,
	const TypeSpecifierNode& to) {
	return buildConversionPlan(from, to, nullptr);
}

// Check if one type can be implicitly converted to another (considering pointers and references).
// Delegates to buildConversionPlan(TypeSpecifierNode, TypeSpecifierNode) for the unified
// conversion logic, matching the pattern the primitive overload already uses.
inline TypeConversionResult can_convert_type(const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	return buildConversionPlan(from, to).toResult();
}

inline bool isIntegerLiteralZeroNullPointerConstant(const ASTNode& arg_node);
inline bool parameterSupportsNullPointerConstantOverloadConversion(const TypeSpecifierNode& parameter_type);
inline bool isNullptrTypeParameterForNullPointerConstantOverloadConversion(const TypeSpecifierNode& parameter_type);
inline bool isIntegerLiteralZeroNullptrTypeOverloadConversion(
	const ASTNode* argument_node,
	const TypeSpecifierNode& parameter_type);
inline TypeSpecifierNode normalizeArgumentTypeForNullPointerConstantConversion(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& parameter_type,
	const ASTNode* argument_node = nullptr);
inline bool isNonZeroIntegerLiteralToPointerConversion(
	const ASTNode* argument_node,
	const TypeSpecifierNode& parameter_type);

inline ArgumentConversionInfo buildArgumentConversionInfo(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& parameter_type,
	const ASTNode* argument_node = nullptr) {
	const TypeSpecifierNode effective_argument_type =
		normalizeArgumentTypeForNullPointerConstantConversion(
			argument_type,
			parameter_type,
			argument_node);
	// C++20 [conv.ptr]: a pointer parameter only accepts a null pointer
	// constant or another pointer-like value. An integer literal other than the
	// null pointer constant must not become viable merely because the flat
	// category fallback sees matching pointee/integer categories.
	if (isNonZeroIntegerLiteralToPointerConversion(argument_node, parameter_type)) {
		return {ConversionRank::NoMatch, &parameter_type, false};
	}
	if (isIntegerLiteralZeroNullptrTypeOverloadConversion(argument_node, parameter_type)) {
		const ConversionPlan conversion = buildConversionPlan(
			effective_argument_type,
			parameter_type,
			argument_node);
		return {conversion.is_valid ? ConversionRank::Conversion : ConversionRank::NoMatch,
				&parameter_type,
				conversion.is_valid};
	}

	const ConversionPlan conversion = buildConversionPlan(
		effective_argument_type,
		parameter_type,
		argument_node);
	ArgumentConversionInfo result{
		conversion.rank, &parameter_type, conversion.is_valid};
	result.trailing_standard_rank = conversion.trailing_standard_rank;
	return result;
}

inline bool callArgumentsHaveIncompatiblePointerToArrayPointee(
	const FunctionDeclarationNode& func_decl,
	std::span<const TypeSpecifierNode> argument_types) {
	const auto& parameters = func_decl.parameter_nodes();
	const size_t params_to_check = std::min(parameters.size(), argument_types.size());
	for (size_t i = 0; i < params_to_check; ++i) {
		if (!parameters[i].is<DeclarationNode>()) {
			continue;
		}
		const TypeSpecifierNode& param_type = parameters[i].as<DeclarationNode>().type_specifier_node();
		if (param_type.is_reference() || !param_type.is_pointer()) {
			continue;
		}
		TypeSpecifierNode argument_type = argument_types[i];
		applyArrayToPointerConversion(argument_type);
		if (!argument_type.is_pointer()) {
			continue;
		}
		const CanonicalTypeAlias to_canonical = canonicalize_type_alias(param_type.type_index());
		if (to_canonical.resolvedTypeIndex().category() == TypeCategory::Void) {
			continue;
		}
		if (!pointerPointeeArrayTypesMatch(argument_type, param_type)) {
			return true;
		}
	}
	return false;
}

// Result of overload resolution
struct OverloadResolutionResult {
	const ASTNode* selected_overload = nullptr;
	bool is_ambiguous = false;
	bool has_match = false;

	OverloadResolutionResult() = default;
	OverloadResolutionResult(const ASTNode* overload)
		: selected_overload(overload), is_ambiguous(false), has_match(true) {}

	static OverloadResolutionResult ambiguous() {
		OverloadResolutionResult result;
		result.is_ambiguous = true;
		return result;
	}

	static OverloadResolutionResult no_match() {
		return OverloadResolutionResult();
	}
};

struct ConstructorOverloadResolutionResult {
	const ConstructorDeclarationNode* selected_overload = nullptr;
	bool is_ambiguous = false;
	bool has_match = false;

	ConstructorOverloadResolutionResult() = default;
	explicit ConstructorOverloadResolutionResult(const ConstructorDeclarationNode* overload)
		: selected_overload(overload), is_ambiguous(false), has_match(overload != nullptr) {}

	static ConstructorOverloadResolutionResult ambiguous() {
		ConstructorOverloadResolutionResult result;
		result.is_ambiguous = true;
		return result;
	}

	static ConstructorOverloadResolutionResult no_match() {
		return ConstructorOverloadResolutionResult();
	}
};

inline bool is_lvalue_expression_for_overload_resolution(const ASTNode& arg_node) {
	if (!arg_node.is<ExpressionNode>()) {
		return false;
	}

	const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
	return std::visit([](const auto& inner) -> bool {
		using T = std::decay_t<decltype(inner)>;
		if constexpr (std::is_same_v<T, IdentifierNode>) {
			return inner.binding() != IdentifierBinding::EnumConstant;
		} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
			return true;
		} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
			return true;
		} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
			return inner.op() == "*" || inner.op() == "++" || inner.op() == "--";
		} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
			return true;
		} else {
			return false;
		}
	},
					  arg_expr);
}

inline bool isIntegerLiteralZeroNullPointerConstant(const ASTNode& arg_node) {
	if (!arg_node.is<ExpressionNode>()) {
		return false;
	}

	const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
	const auto* numeric_literal = std::get_if<NumericLiteralNode>(&arg_expr);
	if (numeric_literal == nullptr || !isIntegralType(numeric_literal->type())) {
		return false;
	}

	const NumericLiteralValue literal_value = numeric_literal->value();
	if (const auto* integer_value = std::get_if<unsigned long long>(&literal_value)) {
		return *integer_value == 0;
	}

	return false;
}

inline bool parameterSupportsNullPointerConstantOverloadConversion(const TypeSpecifierNode& parameter_type) {
	return parameter_type.is_pointer() ||
		   parameter_type.is_function_pointer() ||
		   parameter_type.is_member_function_pointer() ||
		   parameter_type.is_member_object_pointer() ||
		   isNullptrTypeParameterForNullPointerConstantOverloadConversion(parameter_type);
}

inline bool isNullptrTypeParameterForNullPointerConstantOverloadConversion(const TypeSpecifierNode& parameter_type) {
	return parameter_type.category() == TypeCategory::Nullptr ||
		   resolve_type_alias(parameter_type.type_index()) == TypeCategory::Nullptr;
}

inline bool isIntegerLiteralZeroNullptrTypeOverloadConversion(
	const ASTNode* argument_node,
	const TypeSpecifierNode& parameter_type) {
	return argument_node != nullptr &&
		   isIntegerLiteralZeroNullPointerConstant(*argument_node) &&
		   isNullptrTypeParameterForNullPointerConstantOverloadConversion(parameter_type);
}

inline TypeSpecifierNode normalizeArgumentTypeForNullPointerConstantConversion(
	const TypeSpecifierNode& argument_type,
	const TypeSpecifierNode& parameter_type,
	const ASTNode* argument_node) {
	TypeSpecifierNode effective_argument_type = argument_type;
	if (argument_node != nullptr &&
		parameterSupportsNullPointerConstantOverloadConversion(parameter_type) &&
		isIntegerLiteralZeroNullPointerConstant(*argument_node)) {
		effective_argument_type.set_type_index(nativeTypeIndex(TypeCategory::Nullptr));
		effective_argument_type.set_size_in_bits(get_type_size_bits(TypeCategory::Nullptr));
		effective_argument_type.set_reference_qualifier(ReferenceQualifier::None);
	}
	return effective_argument_type;
}

// True when an integer literal other than the literal null pointer constant
// initializes a pointer-like parameter. Such a conversion is only present in
// the flat category fallback because the pointee category coincides with the
// integer category; overload resolution must not treat it as viable.
inline bool isNonZeroIntegerLiteralToPointerConversion(
	const ASTNode* argument_node,
	const TypeSpecifierNode& parameter_type) {
	if (argument_node == nullptr ||
		!parameterSupportsNullPointerConstantOverloadConversion(parameter_type) ||
		!argument_node->is<ExpressionNode>()) {
		return false;
	}
	const auto* numeric_literal =
		std::get_if<NumericLiteralNode>(&argument_node->as<ExpressionNode>());
	if (numeric_literal == nullptr || !isIntegralType(numeric_literal->type())) {
		return false;
	}
	const NumericLiteralValue literal_value = numeric_literal->value();
	if (const auto* integer_value = std::get_if<unsigned long long>(&literal_value)) {
		return *integer_value != 0;
	}
	return false;
}

// Argument types are incomplete when the front end has not yet bound a
// concrete type index (for example a dependent pointer written before template
// substitution). Overload resolution cannot rank such arguments by type.
inline bool hasIncompleteConstructorArgumentTypes(
	std::span<const TypeSpecifierNode> argument_types) {
	for (const TypeSpecifierNode& argument_type : argument_types) {
		if (!argument_type.type_index().is_valid()) {
			return true;
		}
	}
	return false;
}

inline void adjust_argument_type_for_overload_resolution(const ASTNode& arg_node, TypeSpecifierNode& arg_type) {
	if (is_lvalue_expression_for_overload_resolution(arg_node)) {
		arg_type.set_reference_qualifier(ReferenceQualifier::LValueReference);
	}
}

inline size_t countMinRequiredArgs(const ConstructorDeclarationNode& ctor) {
	const auto& params = ctor.parameter_nodes();
	size_t min_required = params.size();
	size_t i = params.size();

	while (i > 0) {
		if (!params[i - 1].is<DeclarationNode>()) {
			break;
		}
		const auto& param_decl = params[i - 1].as<DeclarationNode>();
		if (!param_decl.has_default_value()) {
			break;
		}
		min_required--;
		--i;
	}

	return min_required;
}

inline bool isSameTypeCopyOrMoveConstructorCandidate(
	const StructTypeInfo& struct_info,
	const ConstructorDeclarationNode& ctor_decl) {
	if (!struct_info.own_type_index_.has_value()) {
		return false;
	}

	const auto& parameters = ctor_decl.parameter_nodes();
	if (parameters.size() != 1 || !parameters[0].is<DeclarationNode>()) {
		return false;
	}

	const auto& param_type_node = parameters[0].as<DeclarationNode>().type_node();
	if (!param_type_node.is<TypeSpecifierNode>()) {
		return false;
	}

	const auto& param_type = param_type_node.as<TypeSpecifierNode>();
	// Implicit copy/move ctors always have exactly 1 param that is a reference
	// (lvalue for copy, rvalue for move) to the struct's own type.
	if (!(param_type.is_lvalue_reference() || param_type.is_rvalue_reference()) ||
		!is_struct_type(param_type.category())) {
		return false;
	}

	return param_type.type_index().is_valid() &&
		   param_type.type_index() == *struct_info.own_type_index_;
}

inline bool isImplicitCopyOrMoveConstructorCandidate(
	const StructTypeInfo& struct_info,
	const ConstructorDeclarationNode& ctor_decl) {
	return ctor_decl.is_implicit() &&
		isSameTypeCopyOrMoveConstructorCandidate(struct_info, ctor_decl);
}

inline int compareConstructorTemplatePreference(
	const ConstructorDeclarationNode& lhs,
	const ConstructorDeclarationNode& rhs) {
	const bool lhs_is_template = lhs.has_template_parameters();
	const bool rhs_is_template = rhs.has_template_parameters();
	if (lhs_is_template == rhs_is_template) {
		return 0;
	}
	return lhs_is_template ? 1 : -1;
}

inline bool tryBuildConstructorConversionInfos(
	const ConstructorDeclarationNode& ctor_decl,
	std::span<const TypeSpecifierNode> argument_types,
	std::span<const ASTNode* const> argument_nodes,
	ArgumentConversionInfoVector& conversion_infos) {
	if (!argument_nodes.empty() && argument_nodes.size() != argument_types.size()) {
		throw InternalError("Constructor argument expression/type count mismatch");
	}
	const auto& parameters = ctor_decl.parameter_nodes();
	size_t min_required = countMinRequiredArgs(ctor_decl);
	if (argument_types.size() < min_required || argument_types.size() > parameters.size()) {
		return false;
	}

	conversion_infos.clear();
	conversion_infos.reserve(argument_types.size());
	for (size_t i = 0; i < argument_types.size(); ++i) {
		if (!parameters[i].is<DeclarationNode>()) {
			return false;
		}

		const auto& param_type = parameters[i].as<DeclarationNode>().type_specifier_node();
		const ArgumentConversionInfo conversion =
			buildArgumentConversionInfo(
				argument_types[i],
				param_type,
				argument_nodes.empty() ? nullptr : argument_nodes[i]);
		if (!conversion.is_valid) {
			return false;
		}
		conversion_infos.push_back(conversion);
	}
	return true;
}

template <typename CompareSourceTemplates>
inline const ConstructorDeclarationNode* selectBestConstructorCandidate(
	std::span<const ConstructorDeclarationNode* const> candidates,
	std::span<const TypeSpecifierNode> argument_types,
	std::span<const ASTNode* const> argument_nodes,
	std::span<const ConstructorDeclarationNode* const> source_templates,
	CompareSourceTemplates&& compare_source_templates,
	bool& is_ambiguous) {
	if (!source_templates.empty() && source_templates.size() != candidates.size()) {
		throw InternalError("Constructor candidate/source-template metadata size mismatch");
	}

	auto compareTemplatePartialOrdering = [&](size_t lhs_index, size_t rhs_index) {
		if (source_templates.empty()) {
			return 0;
		}
		const ConstructorDeclarationNode* lhs_source = source_templates[lhs_index];
		const ConstructorDeclarationNode* rhs_source = source_templates[rhs_index];
		if (lhs_source == nullptr || rhs_source == nullptr || lhs_source == rhs_source) {
			return 0;
		}
		return compare_source_templates(*lhs_source, *rhs_source);
	};

	const ConstructorDeclarationNode* best_match = nullptr;
	size_t best_index = SIZE_MAX;
	ArgumentConversionInfoVector best_infos;
	int num_best_matches = 0;
	OverloadResolutionSizeIndexVector tied_candidate_indices;
	is_ambiguous = false;

	for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
		const ConstructorDeclarationNode* candidate = candidates[candidate_index];
		if (candidate == nullptr) {
			continue;
		}

		ArgumentConversionInfoVector conversion_infos;
		if (!tryBuildConstructorConversionInfos(
				*candidate,
				argument_types,
				argument_nodes,
				conversion_infos)) {
			continue;
		}

		if (!best_match) {
			best_match = candidate;
			best_index = candidate_index;
			best_infos = conversion_infos;
			num_best_matches = 1;
			tied_candidate_indices.clear();
			tied_candidate_indices.push_back(candidate_index);
			continue;
		}

		const ConversionInfoComparison this_vs_best =
			compareConversionInfoLists(argument_types, conversion_infos, best_infos);
		bool this_is_better = this_vs_best.lhs_is_better;
		bool this_is_worse = this_vs_best.lhs_is_worse;
		if (!this_is_better && !this_is_worse) {
			const int template_preference =
				compareConstructorTemplatePreference(*candidate, *best_match);
			if (template_preference < 0) {
				this_is_better = true;
			} else if (template_preference > 0) {
				this_is_worse = true;
			} else {
				const int partial_order = compareTemplatePartialOrdering(candidate_index, best_index);
				this_is_better = partial_order < 0;
				this_is_worse = partial_order > 0;
			}
		}

		if (this_is_better && !this_is_worse) {
			OverloadResolutionSizeIndexVector old_tied = std::move(tied_candidate_indices);
			best_match = candidate;
			best_index = candidate_index;
			best_infos = conversion_infos;
			num_best_matches = 1;
			tied_candidate_indices.clear();
			tied_candidate_indices.push_back(candidate_index);
			for (size_t previous_index : old_tied) {
				const ConstructorDeclarationNode* previous = candidates[previous_index];
				if (previous_index == candidate_index) {
					continue;
				}
				ArgumentConversionInfoVector previous_infos;
				if (!tryBuildConstructorConversionInfos(
						*previous,
						argument_types,
						argument_nodes,
						previous_infos)) {
					continue;
				}

				const ConversionInfoComparison previous_vs_best =
					compareConversionInfoLists(argument_types, previous_infos, best_infos);
				bool previous_better = previous_vs_best.lhs_is_better;
				bool previous_worse = previous_vs_best.lhs_is_worse;
				if (!previous_better && !previous_worse) {
					const int template_preference =
						compareConstructorTemplatePreference(*previous, *best_match);
					if (template_preference < 0) {
						previous_better = true;
					} else if (template_preference > 0) {
						previous_worse = true;
					} else {
						const int partial_order =
							compareTemplatePartialOrdering(previous_index, best_index);
						previous_better = partial_order < 0;
						previous_worse = partial_order > 0;
					}
				}
				if (!previous_better && previous_worse) {
					continue;
				}

				num_best_matches++;
				tied_candidate_indices.push_back(previous_index);
			}
		} else if (!this_is_better && this_is_worse) {
			continue;
		} else {
			num_best_matches++;
			tied_candidate_indices.push_back(candidate_index);
		}
	}

	if (num_best_matches > 1) {
		is_ambiguous = true;
		return nullptr;
	}
	return best_match;
}

template <typename CompareSourceTemplates>
inline const ConstructorDeclarationNode* selectBestConstructorCandidate(
	std::span<const ConstructorDeclarationNode* const> candidates,
	std::span<const TypeSpecifierNode> argument_types,
	std::span<const ConstructorDeclarationNode* const> source_templates,
	CompareSourceTemplates&& compare_source_templates,
	bool& is_ambiguous) {
	return selectBestConstructorCandidate(
		candidates,
		argument_types,
		std::span<const ASTNode* const>{},
		source_templates,
		std::forward<CompareSourceTemplates>(compare_source_templates),
		is_ambiguous);
}

inline const ConstructorDeclarationNode* selectBestConstructorCandidate(
	std::span<const ConstructorDeclarationNode* const> candidates,
	std::span<const TypeSpecifierNode> argument_types,
	std::span<const ASTNode* const> argument_nodes,
	bool& is_ambiguous) {
	return selectBestConstructorCandidate(
		candidates,
		argument_types,
		argument_nodes,
		std::span<const ConstructorDeclarationNode* const>{},
		[](const ConstructorDeclarationNode&, const ConstructorDeclarationNode&) { return 0; },
		is_ambiguous);
}

inline const ConstructorDeclarationNode* selectBestConstructorCandidate(
	std::span<const ConstructorDeclarationNode* const> candidates,
	std::span<const TypeSpecifierNode> argument_types,
	bool& is_ambiguous) {
	return selectBestConstructorCandidate(
		candidates,
		argument_types,
		std::span<const ASTNode* const>{},
		std::span<const ConstructorDeclarationNode* const>{},
		[](const ConstructorDeclarationNode&, const ConstructorDeclarationNode&) { return 0; },
		is_ambiguous);
}

inline ConstructorOverloadResolutionResult resolve_constructor_overload(
	const StructTypeInfo& struct_info,
	std::span<const TypeSpecifierNode> argument_types,
	bool skip_implicit,
	std::span<const ASTNode* const> argument_nodes) {
	if (!argument_nodes.empty() && argument_nodes.size() != argument_types.size()) {
		throw InternalError("Constructor argument expression/type count mismatch");
	}
	std::vector<const ConstructorDeclarationNode*> viable_candidates;
	viable_candidates.reserve(struct_info.member_functions.size());

	for (const auto& member_func : struct_info.member_functions) {
		if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}

		const auto& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
		const bool is_implicit_copy_or_move =
			isImplicitCopyOrMoveConstructorCandidate(struct_info, ctor_decl);
		if (skip_implicit && is_implicit_copy_or_move) {
			continue;
		}

		const auto& parameters = ctor_decl.parameter_nodes();
		size_t min_required = countMinRequiredArgs(ctor_decl);
		if (argument_types.size() < min_required || argument_types.size() > parameters.size()) {
			continue;
		}

		if (is_implicit_copy_or_move && argument_types.size() == 1) {
			const TypeSpecifierNode& arg_type = argument_types[0];
			TypeCategory resolved_arg_type = resolve_type_alias(arg_type.type_index());
			bool is_same_struct_type = is_struct_type(resolved_arg_type) &&
									   arg_type.type_index() == *struct_info.own_type_index_;
			if (!is_same_struct_type) {
				continue;
			}
		}
		viable_candidates.push_back(&ctor_decl);
	}

	bool is_ambiguous = false;
	const ConstructorDeclarationNode* best_match =
		selectBestConstructorCandidate(
			viable_candidates,
			argument_types,
			argument_nodes,
			is_ambiguous);
	if (!best_match) {
		if (is_ambiguous) {
			return ConstructorOverloadResolutionResult::ambiguous();
		}
		return ConstructorOverloadResolutionResult::no_match();
	}
	return ConstructorOverloadResolutionResult(best_match);
}

inline ConstructorOverloadResolutionResult resolve_constructor_overload(
	const StructTypeInfo& struct_info,
	std::span<const TypeSpecifierNode> argument_types,
	bool skip_implicit = false) {
	return resolve_constructor_overload(
		struct_info,
		argument_types,
		skip_implicit,
		std::span<const ASTNode* const>{});
}

template <typename ArgumentRange>
inline ConstructorOverloadResolutionResult resolve_constructor_overload(
	const StructTypeInfo& struct_info,
	std::span<const TypeSpecifierNode> argument_types,
	bool skip_implicit,
	const ArgumentRange& argument_nodes) {
	std::vector<const ASTNode*> argument_node_pointers;
	argument_node_pointers.reserve(argument_nodes.size());
	for (const auto& argument_node : argument_nodes) {
		argument_node_pointers.push_back(&argument_node);
	}
	return resolve_constructor_overload(
		struct_info,
		argument_types,
		skip_implicit,
		std::span<const ASTNode* const>(
			argument_node_pointers.data(),
			argument_node_pointers.size()));
}

// Arity-only constructor overload resolution — used as fallback when argument type
// information is unavailable.  Selects the best constructor where
// min_required_args <= num_args <= params.size().
// When skip_implicit=true, implicit copy/move constructors are skipped.
// Tiebreaking: prefer value-param ctors over same-type-reference ctors (copy/move-like).
// Ambiguous only when multiple non-copy-like explicit ctors match.
inline ConstructorOverloadResolutionResult resolve_constructor_overload_arity(
	const StructTypeInfo& struct_info,
	size_t num_args,
	bool skip_implicit = false) {
	auto is_same_type_ref_ctor = [&](const ConstructorDeclarationNode& ctor) -> bool {
		const auto& params = ctor.parameter_nodes();
		if (params.empty() || !params[0].is<DeclarationNode>())
			return false;
		const auto& ptype_node = params[0].as<DeclarationNode>().type_node();
		if (!ptype_node.is<TypeSpecifierNode>())
			return false;
		const auto& ptype = ptype_node.as<TypeSpecifierNode>();
		if (!(ptype.is_reference() || ptype.is_rvalue_reference()))
			return false;
		if (ptype.category() != TypeCategory::Struct)
			return false;
		return struct_info.isOwnTypeIndex(ptype.type_index());
	};

	const ConstructorDeclarationNode* best_value_explicit = nullptr;
	int num_value_explicit_matches = 0;
	const ConstructorDeclarationNode* first_ref_explicit = nullptr;
	const ConstructorDeclarationNode* first_implicit = nullptr;

	for (const auto& member_func : struct_info.member_functions) {
		if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const auto& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
		const bool is_implicit_copy_or_move =
			isImplicitCopyOrMoveConstructorCandidate(struct_info, ctor_decl);
		if (skip_implicit && is_implicit_copy_or_move) {
			continue;
		}
		if (ctor_decl.has_template_parameters()) {
			// Constructor templates require deduction; arity-only fallback must not
			// treat them as competing non-template overloads (C++ [temp.deduct]).
			continue;
		}
		const auto& parameters = ctor_decl.parameter_nodes();
		size_t min_required = countMinRequiredArgs(ctor_decl);
		if (num_args < min_required || num_args > parameters.size()) {
			continue;
		}
		if (ctor_decl.is_implicit()) {
			if (!first_implicit)
				first_implicit = &ctor_decl;
		} else if (is_same_type_ref_ctor(ctor_decl)) {
			if (!first_ref_explicit)
				first_ref_explicit = &ctor_decl;
		} else {
			best_value_explicit = &ctor_decl;
			num_value_explicit_matches++;
		}
	}

	if (num_value_explicit_matches > 1) {
		return ConstructorOverloadResolutionResult::ambiguous();
	}
	if (best_value_explicit) {
		return ConstructorOverloadResolutionResult(best_value_explicit);
	}
	if (first_ref_explicit) {
		return ConstructorOverloadResolutionResult(first_ref_explicit);
	}
	if (first_implicit) {
		return ConstructorOverloadResolutionResult(first_implicit);
	}
	return ConstructorOverloadResolutionResult::no_match();
}

// countMinRequiredArgs is defined in SymbolTable.h (included above)

inline const ASTNode* getOverloadArgumentNodeOrNull(std::span<const ASTNode> argument_nodes, size_t index) {
	return index < argument_nodes.size() ? &argument_nodes[index] : nullptr;
}

template <typename ArgumentNodeContainer>
inline const ASTNode* getOverloadArgumentNodeOrNull(const ArgumentNodeContainer& argument_nodes, size_t index) {
	return index < argument_nodes.size() ? &argument_nodes[index] : nullptr;
}

// Perform overload resolution for a function call
// Returns the best matching overload, or nullptr if no match or ambiguous
template <typename ArgumentNodeContainer, typename ConversionBuilder>
inline OverloadResolutionResult resolve_overload_with_argument_nodes_using_conversion(
	std::span<const ASTNode> overloads,
	std::span<const TypeSpecifierNode> argument_types,
	const ArgumentNodeContainer& argument_nodes,
	ConversionBuilder&& build_argument_conversion) {
	if (overloads.empty()) {
		return OverloadResolutionResult::no_match();
	}

	// Track the best match found so far
	const ASTNode* best_match = nullptr;
	ArgumentConversionInfoVector best_infos;
	int num_best_matches = 0;
	OverloadResolutionAstNodePtrVector tied_candidates; // All candidates with best rank

	// Evaluate each overload
	for (const auto& overload : overloads) {
		// Extract the function declaration
		const FunctionDeclarationNode* func_decl = nullptr;
		if (overload.is<FunctionDeclarationNode>()) {
			func_decl = &overload.as<FunctionDeclarationNode>();
		} else {
			// Not a function declaration, skip it
			continue;
		}

		// Check parameter count
		const auto& parameters = func_decl->parameter_nodes();
		bool is_variadic = func_decl->is_variadic();

		// For variadic functions, we need at least as many arguments as named parameters
		// For non-variadic functions, argument count must be between min required and total params
		size_t min_required = countMinRequiredArgs(*func_decl);
		if (is_variadic) {
			if (argument_types.size() < min_required) {
				continue; // Too few arguments for variadic function
			}
		} else {
			if (argument_types.size() < min_required || argument_types.size() > parameters.size()) {
				continue; // Argument count mismatch (accounting for default arguments)
			}
		}

		// Check if all provided arguments can be converted to parameters
		// For variadic functions, only check the named parameters
		// The variadic arguments (...) accept any type
		ArgumentConversionInfoVector conversion_infos;
		bool all_convertible = true;

		size_t params_to_check = std::min(parameters.size(), argument_types.size());

		for (size_t i = 0; i < params_to_check; ++i) {
			const auto& param_type = parameters[i].as<DeclarationNode>().type_specifier_node();
			const auto& arg_type = argument_types[i];
			const ASTNode* arg_node = getOverloadArgumentNodeOrNull(argument_nodes, i);

			const ArgumentConversionInfo conversion =
				build_argument_conversion(arg_type, param_type, arg_node);
			if (!conversion.is_valid) {
				all_convertible = false;
				break;
			}
			conversion_infos.push_back(conversion);
		}

		// For variadic functions, arguments consumed by "..." use an ellipsis
		// conversion sequence [over.ics.ellipsis], which is always worse than
		// standard and user-defined conversion sequences [over.ics.rank].
		if (is_variadic) {
			for (size_t i = params_to_check; i < argument_types.size(); ++i) {
				conversion_infos.push_back(ArgumentConversionInfo::ellipsis_match_variadic());
			}
		}

		if (!all_convertible) {
			continue; // This overload doesn't match
		}

		// Compare with the best match so far
		if (best_match == nullptr) {
			// First valid match
			best_match = &overload;
			best_infos = conversion_infos;
			num_best_matches = 1;
			tied_candidates.clear();
			tied_candidates.push_back(&overload);
		} else {
			// Compare conversion ranks
			const ConversionInfoComparison this_vs_best =
				compareConversionInfoLists(argument_types, conversion_infos, best_infos);
			const bool this_is_better = this_vs_best.lhs_is_better;
			const bool this_is_worse = this_vs_best.lhs_is_worse;

			if (this_is_better && !this_is_worse) {
				// This overload is strictly better than the current best.
				// Re-evaluate all previously accumulated tied/incomparable
				// candidates against the new best ranks — any that are not
				// strictly worse must be kept so that ambiguity is detected.
				OverloadResolutionAstNodePtrVector old_tied = std::move(tied_candidates);
				best_match = &overload;
				best_infos = conversion_infos;
				num_best_matches = 1;
				tied_candidates.clear();
				tied_candidates.push_back(&overload);
				for (const auto* prev : old_tied) {
					if (prev == &overload)
						continue;
					// We need the conversion ranks for prev — recompute them.
					const FunctionDeclarationNode* prev_func = &prev->as<FunctionDeclarationNode>();
					const auto& prev_params = prev_func->parameter_nodes();
					size_t prev_params_to_check = std::min(prev_params.size(), argument_types.size());
					ArgumentConversionInfoVector prev_infos;
					bool prev_valid = true;
					for (size_t k = 0; k < prev_params_to_check; ++k) {
						const auto& pt = prev_params[k].as<DeclarationNode>().type_specifier_node();
						const ASTNode* arg_node = getOverloadArgumentNodeOrNull(argument_nodes, k);
						const ArgumentConversionInfo conv =
							build_argument_conversion(argument_types[k], pt, arg_node);
						if (!conv.is_valid) {
							prev_valid = false;
							break;
						}
						prev_infos.push_back(conv);
					}
					if (prev_func->is_variadic()) {
						for (size_t k = prev_params_to_check; k < argument_types.size(); ++k)
							prev_infos.push_back(ArgumentConversionInfo::ellipsis_match_variadic());
					}
					if (!prev_valid)
						continue;
					// Compare prev against the new best.
					const ConversionInfoComparison prev_vs_best =
						compareConversionInfoLists(argument_types, prev_infos, best_infos);
					const bool prev_better = prev_vs_best.lhs_is_better;
					const bool prev_worse = prev_vs_best.lhs_is_worse;
					if (!prev_better && prev_worse) {
						// Strictly worse than new best — discard.
					} else {
						// Tied or incomparable — keep for ambiguity detection.
						num_best_matches++;
						tied_candidates.push_back(prev);
					}
				}
			} else if (!this_is_better && this_is_worse) {
				// This overload is strictly worse — skip it
			} else {
				// This overload is equally good on every argument (exact tie) OR
				// better on some arguments and worse on others (incomparable).
				// In both cases neither this candidate nor the current best dominates
				// the other, so the call is potentially ambiguous.
				num_best_matches++;
				tied_candidates.push_back(&overload);
			}
		}
	}

	if (best_match == nullptr) {
		return OverloadResolutionResult::no_match();
	}

	if (num_best_matches > 1) {
		return OverloadResolutionResult::ambiguous();
	}

	return OverloadResolutionResult(best_match);
}

template <typename ArgumentNodeContainer>
inline OverloadResolutionResult resolve_overload_with_argument_nodes(
	std::span<const ASTNode> overloads,
	std::span<const TypeSpecifierNode> argument_types,
	const ArgumentNodeContainer& argument_nodes) {
	return resolve_overload_with_argument_nodes_using_conversion(
		overloads,
		argument_types,
		argument_nodes,
		[](const TypeSpecifierNode& argument_type,
		   const TypeSpecifierNode& parameter_type,
		   const ASTNode* argument_node) {
			return buildArgumentConversionInfo(
				argument_type,
				parameter_type,
				argument_node);
		});
}

// Receiver object const/volatile for member overload ranking. Pointer receivers
// (including `this`) store cv on the last pointer level; non-pointer receivers
// use the type's base cv. Matches typeSpecifierObjectIsConst in the parser.
inline bool memberObjectTypeIsConst(const TypeSpecifierNode& type) {
	if (!type.pointer_levels().empty()) {
		return hasCVQualifier(type.pointer_levels().back().cv_qualifier, CVQualifier::Const);
	}
	return type.is_const();
}

inline bool memberObjectTypeIsVolatile(const TypeSpecifierNode& type) {
	if (!type.pointer_levels().empty()) {
		return hasCVQualifier(type.pointer_levels().back().cv_qualifier, CVQualifier::Volatile);
	}
	return type.is_volatile();
}

// Resolve non-static member candidates while ranking the implicit object
// conversion alongside the explicit arguments. C++20 [over.match.funcs] makes
// that conversion part of best-viable-function selection; filtering solely by
// explicit parameters loses const/volatile receiver preferences.
template <typename ArgumentNodeContainer>
inline OverloadResolutionResult resolve_member_overload_with_argument_nodes(
	std::span<const ASTNode> overloads,
	const TypeSpecifierNode& object_type,
	std::span<const TypeSpecifierNode> argument_types,
	const ArgumentNodeContainer& argument_nodes) {
	struct ViableMemberCandidate {
		const ASTNode* overload = nullptr;
		ArgumentConversionInfoVector conversion_infos;
	};

	OverloadResolutionTypeSpecifierVector comparison_argument_types;
	comparison_argument_types.push_back(object_type);
	for (const TypeSpecifierNode& argument_type : argument_types) {
		comparison_argument_types.push_back(argument_type);
	}

	std::vector<ViableMemberCandidate> viable_candidates;
	viable_candidates.reserve(overloads.size());

	const bool object_is_const = memberObjectTypeIsConst(object_type);
	const bool object_is_volatile = memberObjectTypeIsVolatile(object_type);

	for (const ASTNode& overload : overloads) {
		if (!overload.is<FunctionDeclarationNode>()) {
			continue;
		}
		const FunctionDeclarationNode& function = overload.as<FunctionDeclarationNode>();
		if ((object_is_const && !function.is_const_member_function()) ||
			(object_is_volatile && !function.is_volatile_member_function())) {
			continue;
		}

		const auto& parameters = function.parameter_nodes();
		const size_t min_required = countMinRequiredArgs(function);
		if (argument_types.size() < min_required ||
			(!function.is_variadic() && argument_types.size() > parameters.size())) {
			continue;
		}

		ViableMemberCandidate candidate;
		candidate.overload = &overload;
		const bool adds_object_qualification =
			(!object_is_const && function.is_const_member_function()) ||
			(!object_is_volatile && function.is_volatile_member_function());
		candidate.conversion_infos.push_back({
			adds_object_qualification
				? ConversionRank::QualificationAdjustment
				: ConversionRank::ExactMatch,
			nullptr,
			true});

		const size_t params_to_check = std::min(parameters.size(), argument_types.size());
		bool all_convertible = true;
		for (size_t i = 0; i < params_to_check; ++i) {
			const TypeSpecifierNode& parameter_type =
				parameters[i].as<DeclarationNode>().type_specifier_node();
			const ASTNode* argument_node =
				getOverloadArgumentNodeOrNull(argument_nodes, i);
			const ArgumentConversionInfo conversion = buildArgumentConversionInfo(
				argument_types[i],
				parameter_type,
				argument_node);
			if (!conversion.is_valid) {
				all_convertible = false;
				break;
			}
			candidate.conversion_infos.push_back(conversion);
		}
		if (!all_convertible) {
			continue;
		}
		if (function.is_variadic()) {
			for (size_t i = params_to_check; i < argument_types.size(); ++i) {
				candidate.conversion_infos.push_back(
					ArgumentConversionInfo::ellipsis_match_variadic());
			}
		}
		viable_candidates.push_back(std::move(candidate));
	}

	if (viable_candidates.empty()) {
		return OverloadResolutionResult::no_match();
	}

	const ViableMemberCandidate* best_candidate = nullptr;
	for (size_t i = 0; i < viable_candidates.size(); ++i) {
		bool is_dominated = false;
		for (size_t j = 0; j < viable_candidates.size(); ++j) {
			if (i == j) {
				continue;
			}
			const ConversionInfoComparison comparison = compareConversionInfoLists(
				comparison_argument_types,
				viable_candidates[j].conversion_infos,
				viable_candidates[i].conversion_infos);
			if (comparison.lhs_is_better && !comparison.lhs_is_worse) {
				is_dominated = true;
				break;
			}
		}
		if (is_dominated) {
			continue;
		}
		if (best_candidate != nullptr) {
			return OverloadResolutionResult::ambiguous();
		}
		best_candidate = &viable_candidates[i];
	}

	return best_candidate != nullptr
		? OverloadResolutionResult(best_candidate->overload)
		: OverloadResolutionResult::no_match();
}

// Result of operator overload resolution
struct OperatorOverloadResult {
	const StructMemberFunction* member_overload = nullptr;
	const FunctionDeclarationNode* free_function_overload = nullptr; // For free-function operators
	bool is_ambiguous = false;
	bool has_match = false;
	bool is_free_function = false; // True when free_function_overload is the active match

	OperatorOverloadResult() = default;
	explicit OperatorOverloadResult(const StructMemberFunction* overload)
		: member_overload(overload), has_match(overload != nullptr) {}
	explicit OperatorOverloadResult(const FunctionDeclarationNode* free_func)
		: free_function_overload(free_func), has_match(free_func != nullptr), is_free_function(free_func != nullptr) {}

	static OperatorOverloadResult ambiguous() {
		OperatorOverloadResult result;
		result.is_ambiguous = true;
		return result;
	}

	static OperatorOverloadResult no_match() {
		return OperatorOverloadResult();
	}

	static OperatorOverloadResult no_overload() {
		return no_match();
	}
};

// Resolve a current-instantiation pattern TypeIndex to its concrete owner.
// Instantiated StructDeclarationNodes retain the exact source pattern declaration,
// so this does not depend on source spelling or template-name suffixes.
inline TypeIndex resolveSelfRefParamIndex(TypeIndex param_idx, TypeIndex left_type_index) {
	const size_t type_info_size = getTypeInfoCount();
	if (!param_idx.is_valid() || param_idx.index() >= type_info_size || left_type_index.index() >= type_info_size)
		return param_idx;
	if (param_idx == left_type_index)
		return left_type_index;
	const StructTypeInfo* param_struct_info = getTypeInfo(param_idx).getStructInfo();
	const StructTypeInfo* owner_struct_info = getTypeInfo(left_type_index).getStructInfo();
	if (param_struct_info == nullptr || owner_struct_info == nullptr ||
		param_struct_info->declaration_node == nullptr ||
		owner_struct_info->declaration_node == nullptr) {
		return param_idx;
	}
	const StructDeclarationNode* param_declaration =
		param_struct_info->declaration_node->injected_class_pattern_declaration() != nullptr
			? param_struct_info->declaration_node->injected_class_pattern_declaration()
			: param_struct_info->declaration_node;
	const StructDeclarationNode* owner_declaration =
		owner_struct_info->declaration_node->injected_class_pattern_declaration() != nullptr
			? owner_struct_info->declaration_node->injected_class_pattern_declaration()
			: owner_struct_info->declaration_node;
	if (param_declaration != owner_declaration) {
		return param_idx;
	}
	const bool param_is_concrete_instantiation =
		param_struct_info->declaration_node->injected_class_pattern_declaration() != nullptr;
	if (param_is_concrete_instantiation) {
		const auto& param_args = getTypeInfo(param_idx).templateArgs();
		const auto& owner_args = getTypeInfo(left_type_index).templateArgs();
		if (!FlashCpp::equalTemplateArgInfoListIdentity(
				std::span<const TypeInfo::TemplateArgInfo>(
					param_args.data(), param_args.size()),
				std::span<const TypeInfo::TemplateArgInfo>(
					owner_args.data(), owner_args.size()))) {
			return param_idx;
		}
	}
	return left_type_index;
}

// Some injected-class types are represented by a dependent TypeInfo that has no
// StructInfo.  In that case the TypeSpecifierNode's parser-bound declaration is
// the authoritative identity for the current instantiation.
inline TypeIndex resolveSelfRefParamIndex(
	TypeIndex param_idx,
	TypeIndex left_type_index,
	const StructDeclarationNode* injected_class_declaration) {
	TypeIndex resolved = resolveSelfRefParamIndex(param_idx, left_type_index);
	if (resolved != param_idx || injected_class_declaration == nullptr ||
		!left_type_index.is_valid() ||
		left_type_index.index() >= getTypeInfoCount()) {
		return resolved;
	}

	const StructTypeInfo* owner_struct_info =
		getTypeInfo(left_type_index).getStructInfo();
	if (owner_struct_info == nullptr ||
		owner_struct_info->declaration_node == nullptr) {
		return param_idx;
	}

	const StructDeclarationNode* param_declaration =
		injected_class_declaration->injected_class_pattern_declaration() != nullptr
			? injected_class_declaration->injected_class_pattern_declaration()
			: injected_class_declaration;
	const StructDeclarationNode* owner_declaration =
		owner_struct_info->declaration_node->injected_class_pattern_declaration() != nullptr
			? owner_struct_info->declaration_node->injected_class_pattern_declaration()
			: owner_struct_info->declaration_node;
	if (param_declaration != owner_declaration) {
		return param_idx;
	}

	const TypeInfo* param_type_info = tryGetTypeInfo(param_idx);
	const TypeInfo* owner_type_info = tryGetTypeInfo(left_type_index);
	const bool param_is_concrete_instantiation =
		param_type_info != nullptr &&
		param_type_info->isTemplateInstantiation() &&
		!param_type_info->templateArgs().empty();
	if (param_is_concrete_instantiation) {
		if (owner_type_info == nullptr ||
			!FlashCpp::equalTemplateArgInfoListIdentity(
				std::span<const TypeInfo::TemplateArgInfo>(
					param_type_info->templateArgs().data(),
					param_type_info->templateArgs().size()),
				std::span<const TypeInfo::TemplateArgInfo>(
					owner_type_info->templateArgs().data(),
					owner_type_info->templateArgs().size()))) {
			// A different explicit specialization is not an injected-class-name
			// reference to the current owner, even when both share the same pattern
			// declaration. Preserve its registered type identity.
			return param_idx;
		}
	}
	return left_type_index;
}

inline TypeIndex typeIndexForRegisteredStructName(StringHandle name) {
	if (!name.isValid()) {
		return TypeIndex{};
	}
	auto type_it = getTypesByNameMap().find(name);
	if (type_it == getTypesByNameMap().end() || type_it->second == nullptr) {
		return TypeIndex{};
	}
	TypeIndex index = type_it->second->registeredTypeIndex();
	if (!index.is_valid()) {
		index = type_it->second->type_index_;
	}
	return index.is_valid()
		? index.withCategory(TypeCategory::Struct)
		: TypeIndex{};
}

// Look up the TypeInfo for a class being defined/instantiated from its pattern
// StructDeclarationNode. Member class templates are often registered under the
// leaf name ("iterator") while pattern_struct.name() is qualified
// ("outer::iterator"), so try every identity the defining class may use.
inline TypeIndex resolveDefiningClassTypeIndex(const StructDeclarationNode& owner_decl) {
	if (TypeIndex by_name = typeIndexForRegisteredStructName(owner_decl.name());
		by_name.is_valid()) {
		return by_name;
	}
	if (TypeIndex by_semantic = typeIndexForRegisteredStructName(owner_decl.semantic_name());
		by_semantic.is_valid()) {
		return by_semantic;
	}

	std::string_view owner_name_view = StringTable::getStringView(owner_decl.name());
	std::string_view leaf_view = simpleBaseName(owner_name_view);
	if (!leaf_view.empty() && leaf_view != owner_name_view) {
		if (TypeIndex by_leaf = typeIndexForRegisteredStructName(
				StringTable::getOrInternStringHandle(leaf_view));
			by_leaf.is_valid()) {
			return by_leaf;
		}
	}
	return TypeIndex{};
}

inline TypeIndex instantiatedOwnerTypeIndex(std::string_view struct_name) {
	return typeIndexForRegisteredStructName(
		StringTable::getOrInternStringHandle(struct_name));
}

// Explicit self-type rewrite requires a matched from/to pair. When either side
// is unavailable, disable the pair (callers fall back to name-based rewrite).
inline void bindSelfTypeRewritePair(
	TypeIndex from_index,
	TypeIndex to_index,
	TypeIndex& self_rewrite_from,
	TypeIndex& self_rewrite_to) {
	if (from_index.is_valid() && to_index.is_valid()) {
		self_rewrite_from = from_index;
		self_rewrite_to = to_index;
		return;
	}
	self_rewrite_from = TypeIndex{};
	self_rewrite_to = TypeIndex{};
}

inline bool binaryOperatorUsesTypeIndexIdentity(TypeCategory cat) {
	return needs_type_index(cat);
}

inline TypeCategory effectiveBinaryOperatorTypeFromSpec(const TypeSpecifierNode& spec) {
	TypeCategory type = spec.category();
	if (spec.type_index().is_valid()) {
		if (const TypeInfo* ti = tryGetTypeInfo(spec.type_index())) {
			const TypeCategory actual_type = ti->category();
			const bool actual_type_is_usable =
				actual_type != TypeCategory::Invalid && actual_type != TypeCategory::Void;
			const bool should_replace_current_type =
				type == TypeCategory::Invalid || type == TypeCategory::Void ||
				(type == TypeCategory::Struct && actual_type == TypeCategory::Enum);
			if (actual_type_is_usable && should_replace_current_type) {
				type = actual_type;
			}
		}
	}
	if (type == TypeCategory::Invalid || type == TypeCategory::Void) {
		return TypeCategory::Struct;
	}
	return type;
}

inline bool isConcreteBinaryOperatorOperandType(const TypeSpecifierNode& spec) {
	TypeCategory type = effectiveBinaryOperatorTypeFromSpec(spec);
	if (type == TypeCategory::Invalid || type == TypeCategory::Void) {
		return false;
	}
	if (binaryOperatorUsesTypeIndexIdentity(type)) {
		return spec.type_index().is_valid();
	}
	return true;
}

inline bool isUserDefinedBinaryOperatorOperandType(const TypeSpecifierNode& spec) {
	if (spec.is_function_pointer() || spec.is_member_function_pointer() || spec.is_member_object_pointer()) {
		return false;
	}
	size_t total_pointer_depth = spec.pointer_depth();
	TypeCategory type = effectiveBinaryOperatorTypeFromSpec(spec);
	if (spec.type_index().is_valid()) {
		const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(spec.type_index());
		total_pointer_depth += alias_info.pointer_depth;
		if (alias_info.type_index.is_valid()) {
			type = alias_info.typeEnum();
		}
	}
	if (total_pointer_depth > 0) {
		return false;
	}
	return binaryOperatorUsesTypeIndexIdentity(type) && spec.type_index().is_valid();
}

inline TypeSpecifierNode makeBinaryOperatorTypeSpecifier(TypeIndex type_index) {
	TypeCategory effective_type = type_index.category();
	SizeInBits size_bits{};

	if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
		if (effective_type == TypeCategory::Invalid || effective_type == TypeCategory::Void || binaryOperatorUsesTypeIndexIdentity(effective_type)) {
			if (type_info->category() != TypeCategory::Invalid && !type_info->isVoid()) {
				effective_type = type_info->category();
			} else if (effective_type == TypeCategory::Invalid || effective_type == TypeCategory::Void) {
				effective_type = TypeCategory::Struct;
			}
		}

		if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
			size_bits = struct_info->sizeInBits();
		} else if (type_info->hasStoredSize()) {
			size_bits = type_info->sizeInBits();
		}
	}

	if (!size_bits.is_set() && effective_type != TypeCategory::Invalid && effective_type != TypeCategory::Void) {
		size_bits = SizeInBits{get_type_size_bits(effective_type)};
	}

	if (binaryOperatorUsesTypeIndexIdentity(effective_type) || type_index.is_valid()) {
		return TypeSpecifierNode(type_index.withCategory(effective_type), size_bits, Token{}, CVQualifier::None, ReferenceQualifier::None);
	}

	return TypeSpecifierNode(effective_type, TypeQualifier::None, size_bits, Token{}, CVQualifier::None);
}

inline TypeSpecifierNode resolveTypeSpecifierForSelfReference(const TypeSpecifierNode& type_spec, TypeIndex enclosing_type_index) {
	TypeSpecifierNode resolved = type_spec;
	TypeCategory resolved_type = resolved.category();
	if (resolved.type_index().is_valid()) {
		if (const TypeInfo* ti = tryGetTypeInfo(resolved.type_index())) {
			const TypeCategory actual_type = ti->category();
			if (actual_type != TypeCategory::Invalid && actual_type != TypeCategory::Void) {
				resolved_type = actual_type;
			}
		}
	}
	if (needs_type_index(resolved_type)) {
		resolved.set_type_index(resolveSelfRefParamIndex(
			resolved.type_index(),
			enclosing_type_index,
			resolved.injected_class_declaration()));
	}
	return resolved;
}

inline TypeSpecifierNode resolveBinaryOperatorTypeForSelfReference(const TypeSpecifierNode& type_spec, TypeIndex enclosing_type_index) {
	return resolveTypeSpecifierForSelfReference(type_spec, enclosing_type_index);
}

inline ConversionRank rankBinaryOperatorOperandMatch(
	const TypeSpecifierNode& arg_spec,
	const TypeSpecifierNode& param_spec,
	TypeIndex enclosing_type_index,
	const ASTNode* argument_node) {
	TypeSpecifierNode resolved_param_spec = resolveBinaryOperatorTypeForSelfReference(param_spec, enclosing_type_index);
	if (!arg_spec.is_pointer() &&
		!resolved_param_spec.is_pointer() &&
		arg_spec.category() != TypeCategory::Struct &&
		resolved_param_spec.category() == TypeCategory::Struct) {
		if (!resolved_param_spec.type_index().is_valid() ||
			resolved_param_spec.type_index().index() >= getTypeInfoCount() ||
			!getTypeInfo(resolved_param_spec.type_index()).getStructInfo()) {
			auto conversion = buildConversionPlan(arg_spec, resolved_param_spec, argument_node);
			return conversion.is_valid ? conversion.rank : ConversionRank::NoMatch;
		}
		if (!hasImplicitConvertingConstructorForArgument(
				resolved_param_spec.type_index(),
				arg_spec,
				argument_node)) {
			return ConversionRank::NoMatch;
		}
		return ConversionRank::UserDefined;
	}
	auto conversion = buildConversionPlan(arg_spec, resolved_param_spec, argument_node);
	return conversion.is_valid ? conversion.rank : ConversionRank::NoMatch;
}

inline ConversionRank rankBinaryOperatorOperandMatch(
	const TypeSpecifierNode& arg_spec,
	const TypeSpecifierNode& param_spec,
	TypeIndex enclosing_type_index) {
	return rankBinaryOperatorOperandMatch(arg_spec, param_spec, enclosing_type_index, nullptr);
}

inline ConversionRank rankImplicitObjectToOperator(
	const TypeSpecifierNode& object_spec,
	const StructMemberFunction& member_func,
	TypeIndex actual_object_type_index,
	TypeIndex member_owner_type_index) {
	if (object_spec.is_const() && !member_func.is_const()) {
		return ConversionRank::NoMatch;
	}
	if (object_spec.is_volatile() && !member_func.is_volatile()) {
		return ConversionRank::NoMatch;
	}

	bool uses_base_member = actual_object_type_index.is_valid() && member_owner_type_index.is_valid() && actual_object_type_index != member_owner_type_index;

	if (uses_base_member) {
		return ConversionRank::Conversion;
	}
	if ((!object_spec.is_const() && member_func.is_const()) ||
		(!object_spec.is_volatile() && member_func.is_volatile())) {
		return ConversionRank::QualificationAdjustment;
	}

	return ConversionRank::ExactMatch;
}

enum class BinaryOperatorCandidateComparison {
	Better,
	Worse,
	Equivalent,
	Incomparable,
};

inline std::vector<ASTNode> collectFreeOperatorCandidates(
	std::string_view operator_name,
	std::span<const TypeSpecifierNode> operand_types,
	const SymbolTable& symbol_table) {
	auto overloads = symbol_table.lookup_all(operator_name);
	std::vector<TypeSpecifierNode> adl_arg_types(operand_types.begin(), operand_types.end());
	auto adl_candidates = symbol_table.lookup_adl(operator_name, adl_arg_types);

	std::unordered_set<std::string_view> existing_mangled;
	std::unordered_set<const FunctionDeclarationNode*> existing_ptrs;
	existing_mangled.reserve(overloads.size());
	existing_ptrs.reserve(overloads.size());
	for (const ASTNode& node : overloads) {
		if (!node.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& function = node.as<FunctionDeclarationNode>();
		if (function.has_mangled_name()) {
			existing_mangled.insert(function.mangled_name());
		} else {
			existing_ptrs.insert(&function);
		}
	}

	for (ASTNode& candidate : adl_candidates) {
		if (!candidate.is<FunctionDeclarationNode>()) {
			overloads.push_back(std::move(candidate));
			continue;
		}
		const auto& function = candidate.as<FunctionDeclarationNode>();
		const bool is_duplicate = function.has_mangled_name()
			? !existing_mangled.insert(function.mangled_name()).second
			: !existing_ptrs.insert(&function).second;
		if (!is_duplicate) {
			overloads.push_back(std::move(candidate));
		}
	}
	return overloads;
}

inline BinaryOperatorCandidateComparison compareBinaryOperatorCandidateRanks(
	ConversionRank lhs_lhs_rank,
	ConversionRank lhs_rhs_rank,
	ConversionRank rhs_lhs_rank,
	ConversionRank rhs_rhs_rank) {
	bool lhs_is_better = false;
	bool lhs_is_worse = false;

	if (lhs_lhs_rank < rhs_lhs_rank)
		lhs_is_better = true;
	else if (lhs_lhs_rank > rhs_lhs_rank)
		lhs_is_worse = true;

	if (lhs_rhs_rank < rhs_rhs_rank)
		lhs_is_better = true;
	else if (lhs_rhs_rank > rhs_rhs_rank)
		lhs_is_worse = true;

	if (lhs_is_better && !lhs_is_worse)
		return BinaryOperatorCandidateComparison::Better;
	if (!lhs_is_better && lhs_is_worse)
		return BinaryOperatorCandidateComparison::Worse;
	if (!lhs_is_better && !lhs_is_worse)
		return BinaryOperatorCandidateComparison::Equivalent;
	return BinaryOperatorCandidateComparison::Incomparable;
}

// Same rank comparison, then C++20 [over.ics.rank]/3.2.3 rvalue→T&& vs const T&
// preference on the right-hand operand (shared with free-function overload resolution).
inline BinaryOperatorCandidateComparison compareBinaryOperatorCandidatesWithRefTiebreak(
	ConversionRank lhs_lhs_rank,
	ConversionRank lhs_rhs_rank,
	const TypeSpecifierNode* lhs_rhs_param,
	ConversionRank rhs_lhs_rank,
	ConversionRank rhs_rhs_rank,
	const TypeSpecifierNode* rhs_rhs_param,
	const TypeSpecifierNode& right_arg_spec) {
	BinaryOperatorCandidateComparison rank_cmp = compareBinaryOperatorCandidateRanks(
		lhs_lhs_rank,
		lhs_rhs_rank,
		rhs_lhs_rank,
		rhs_rhs_rank);
	if (rank_cmp != BinaryOperatorCandidateComparison::Equivalent) {
		return rank_cmp;
	}
	if (lhs_rhs_param == nullptr || rhs_rhs_param == nullptr) {
		return BinaryOperatorCandidateComparison::Equivalent;
	}

	ArgumentConversionInfo lhs_info{lhs_rhs_rank, lhs_rhs_param, true};
	ArgumentConversionInfo rhs_info{rhs_rhs_rank, rhs_rhs_param, true};
	const int tie = compareArgumentConversionInfo(right_arg_spec, lhs_info, rhs_info);
	if (tie < 0) {
		return BinaryOperatorCandidateComparison::Better;
	}
	if (tie > 0) {
		return BinaryOperatorCandidateComparison::Worse;
	}
	return BinaryOperatorCandidateComparison::Equivalent;
}

// Find operator overload in a struct type
// Returns the member function that overloads the given operator, or nullptr if not found
inline OperatorOverloadResult findUnaryOperatorOverload(TypeIndex operand_type_index, OverloadableOperator operator_kind) {
	// Only struct types can have operator overloads
	if (!operand_type_index.is_valid() || operand_type_index.index() >= getTypeInfoCount()) {
		return OperatorOverloadResult::no_overload();
	}

	const TypeInfo& type_info = getTypeInfo(operand_type_index);
	const StructTypeInfo* struct_info = type_info.getStructInfo();

	if (!struct_info) {
		return OperatorOverloadResult::no_overload();
	}

	// Search for the operator overload in member functions
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.operator_kind == operator_kind) {
			return OperatorOverloadResult(&member_func);
		}
	}

	// Search base classes recursively
	for (const auto& base_spec : struct_info->base_classes) {
		if (base_spec.type_index.is_valid()) {
			auto result = findUnaryOperatorOverload(base_spec.type_index, operator_kind);
			if (result.has_match || result.is_ambiguous) {
				return result;
			}
		}
	}

	return OperatorOverloadResult::no_overload();
}

// C++20 [over.match.oper]: member and non-member unary operator candidates form
// one overload set. The operand expression supplies the implicit-object
// conversion for members and the first-parameter conversion for non-members.
inline OperatorOverloadResult findUnaryOperatorOverloadWithFreeFunction(
	const TypeSpecifierNode& operand_type_spec,
	OverloadableOperator operator_kind,
	const SymbolTable& symbol_table) {
	if (operator_kind == OverloadableOperator::None) {
		return OperatorOverloadResult::no_overload();
	}

	struct UnaryOperatorCandidate {
		ConversionRank rank = ConversionRank::NoMatch;
		const StructMemberFunction* member_function = nullptr;
		const FunctionDeclarationNode* free_function = nullptr;
	};
	std::vector<UnaryOperatorCandidate> candidates;

	const TypeIndex operand_type_index = operand_type_spec.type_index();
	const size_t type_info_count = getTypeInfoCount();
	auto gatherMemberCandidates = [&](auto& self, TypeIndex owner_type_index) -> void {
		if (!owner_type_index.is_valid() || owner_type_index.index() >= type_info_count) {
			return;
		}
		const StructTypeInfo* struct_info = getTypeInfo(owner_type_index).getStructInfo();
		if (struct_info == nullptr) {
			return;
		}

		for (const StructMemberFunction& member_function : struct_info->member_functions) {
			if (member_function.operator_kind != operator_kind ||
				!member_function.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}
			const auto& function = member_function.function_decl.as<FunctionDeclarationNode>();
			if (!function.parameter_nodes().empty()) {
				continue;
			}
			const ConversionRank rank = rankImplicitObjectToOperator(
				operand_type_spec,
				member_function,
				operand_type_index,
				owner_type_index);
			if (rank != ConversionRank::NoMatch) {
				candidates.push_back({rank, &member_function, nullptr});
			}
		}

		for (const BaseClassSpecifier& base : struct_info->base_classes) {
			self(self, base.type_index);
		}
	};
	gatherMemberCandidates(gatherMemberCandidates, operand_type_index);

	StringBuilder operator_name_builder;
	operator_name_builder.append("operator").append(overloadableOperatorToString(operator_kind));
	const std::array<TypeSpecifierNode, 1> operand_types{operand_type_spec};
	const std::vector<ASTNode> free_candidates = collectFreeOperatorCandidates(
		operator_name_builder.commit(),
		operand_types,
		symbol_table);
	for (const ASTNode& candidate : free_candidates) {
		if (!candidate.is<FunctionDeclarationNode>()) {
			continue;
		}
		const auto& function = candidate.as<FunctionDeclarationNode>();
		const auto& parameters = function.parameter_nodes();
		if (parameters.size() != 1 || !parameters[0].is<DeclarationNode>()) {
			continue;
		}
		const ASTNode& parameter_type_node = parameters[0].as<DeclarationNode>().type_node();
		if (!parameter_type_node.is<TypeSpecifierNode>()) {
			continue;
		}
		const ConversionRank rank = rankBinaryOperatorOperandMatch(
			operand_type_spec,
			parameter_type_node.as<TypeSpecifierNode>(),
			operand_type_index);
		if (rank != ConversionRank::NoMatch) {
			candidates.push_back({rank, nullptr, &function});
		}
	}

	if (candidates.empty()) {
		return OperatorOverloadResult::no_overload();
	}
	const ConversionRank best_rank = std::ranges::min(
		candidates,
		{},
		&UnaryOperatorCandidate::rank).rank;
	const UnaryOperatorCandidate* best_candidate = nullptr;
	for (const UnaryOperatorCandidate& candidate : candidates) {
		if (candidate.rank != best_rank) {
			continue;
		}
		if (best_candidate != nullptr) {
			return OperatorOverloadResult::ambiguous();
		}
		best_candidate = &candidate;
	}
	if (best_candidate == nullptr) {
		return OperatorOverloadResult::no_overload();
	}
	return best_candidate->free_function != nullptr
		? OperatorOverloadResult(best_candidate->free_function)
		: OperatorOverloadResult(best_candidate->member_function);
}

// Find binary operator overload in a struct type (member function form)
// For binary operators like operator+, operator-, etc.
// Returns the member function that overloads the given operator, or nullptr if not found
// This handles the member function form: a.operator+(b)
inline OperatorOverloadResult findBinaryOperatorOverload(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind,
	const ASTNode* right_argument) {
	TypeIndex left_type_index = left_type_spec.type_index();
	if (!left_type_index.is_valid() || left_type_index.index() >= getTypeInfoCount()) {
		return OperatorOverloadResult::no_overload();
	}

	const StructTypeInfo* left_struct_info = getTypeInfo(left_type_index).getStructInfo();
	if (!left_struct_info) {
		return OperatorOverloadResult::no_overload();
	}

	struct OperatorCandidate {
		ConversionRank lhs_rank;
		ConversionRank rhs_rank;
		const StructMemberFunction* member_func = nullptr;
		const TypeSpecifierNode* rhs_param_type = nullptr;
	};
	std::vector<OperatorCandidate> candidates;

	auto gatherMemberCandidates = [&](auto& self, TypeIndex struct_idx) -> void {
		if (!struct_idx.is_valid() || struct_idx.index() >= getTypeInfoCount())
			return;
		const StructTypeInfo* si = getTypeInfo(struct_idx).getStructInfo();
		if (!si)
			return;

		for (const auto& member_func : si->member_functions) {
			if (operator_kind == OverloadableOperator::Assign) {
				if (!isAssignOperator(member_func.operator_kind))
					continue;
			} else if (member_func.operator_kind != operator_kind) {
				continue;
			}

			if (!member_func.function_decl.is<FunctionDeclarationNode>())
				continue;

			const auto& params = member_func.function_decl.as<FunctionDeclarationNode>().parameter_nodes();
			if (params.empty() || !params[0].is<DeclarationNode>())
				continue;
			if (countMinRequiredArgs(member_func.function_decl.as<FunctionDeclarationNode>()) > 1)
				continue;

			const auto& param_type_node = params[0].as<DeclarationNode>().type_node();
			if (!param_type_node.is<TypeSpecifierNode>())
				continue;

			ConversionRank lhs_rank = rankImplicitObjectToOperator(
				left_type_spec,
				member_func,
				left_type_index,
				struct_idx);
			if (lhs_rank == ConversionRank::NoMatch)
				continue;

			const TypeSpecifierNode& rhs_param_spec = param_type_node.as<TypeSpecifierNode>();
			ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(
				right_type_spec,
				rhs_param_spec,
				struct_idx,
				right_argument);
			if (rhs_rank == ConversionRank::NoMatch)
				continue;

			candidates.push_back({lhs_rank, rhs_rank, &member_func, &rhs_param_spec});
		}

		for (const auto& base_spec : si->base_classes) {
			if (base_spec.type_index.is_valid()) {
				self(self, base_spec.type_index);
			}
		}
	};
	gatherMemberCandidates(gatherMemberCandidates, left_type_index);

	if (candidates.empty()) {
		return OperatorOverloadResult::no_overload();
	}

	std::vector<const OperatorCandidate*> best_candidates;
	best_candidates.reserve(candidates.size());

	for (size_t i = 0; i < candidates.size(); ++i) {
		const auto& candidate = candidates[i];
		bool is_dominated = false;

		for (size_t j = 0; j < candidates.size(); ++j) {
			if (i == j)
				continue;
			if (compareBinaryOperatorCandidatesWithRefTiebreak(
					candidates[j].lhs_rank,
					candidates[j].rhs_rank,
					candidates[j].rhs_param_type,
					candidate.lhs_rank,
					candidate.rhs_rank,
					candidate.rhs_param_type,
					right_type_spec) == BinaryOperatorCandidateComparison::Better) {
				is_dominated = true;
				break;
			}
		}

		if (!is_dominated) {
			best_candidates.push_back(&candidate);
		}
	}

	if (best_candidates.empty()) {
		return OperatorOverloadResult::no_match();
	}

	if (best_candidates.size() != 1) {
		return OperatorOverloadResult::ambiguous();
	}

	return OperatorOverloadResult(best_candidates[0]->member_func);
}

inline OperatorOverloadResult findBinaryOperatorOverload(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind) {
	return findBinaryOperatorOverload(
		left_type_spec,
		right_type_spec,
		operator_kind,
		nullptr);
}

inline OperatorOverloadResult findBinaryOperatorOverload(
	TypeIndex left_type_index,
	TypeIndex right_type_index,
	OverloadableOperator operator_kind,
	TypeCategory right_type,
	const ASTNode* right_argument) {
	TypeCategory effective_right_type = right_type;
	if (right_type_index.is_valid()) {
		TypeCategory indexed_right_type = resolve_type_alias(right_type_index);
		if (binaryOperatorUsesTypeIndexIdentity(indexed_right_type)) {
			effective_right_type = TypeCategory::Invalid;
		}
	}
	return findBinaryOperatorOverload(
		makeBinaryOperatorTypeSpecifier(left_type_index.withCategory(TypeCategory::Invalid)),
		makeBinaryOperatorTypeSpecifier(right_type_index.withCategory(effective_right_type)),
		operator_kind,
		right_argument);
}

inline OperatorOverloadResult findBinaryOperatorOverload(
	TypeIndex left_type_index,
	TypeIndex right_type_index,
	OverloadableOperator operator_kind,
	TypeCategory right_type) {
	return findBinaryOperatorOverload(
		left_type_index,
		right_type_index,
		operator_kind,
		right_type,
		nullptr);
}

// Find binary operator overload, including free-function operators in the given symbol table.
// Per C++20 [over.match.oper]/2, both member and non-member candidates are collected into
// a single candidate set and ranked together per [over.best.ics] and [over.match.best].
// Member and non-member candidates remain indistinguishable when their conversion
// sequences are equivalent; neither candidate receives a category-based preference.
inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind,
	const SymbolTable& symbol_table,
	const ASTNode* left_argument,
	const ASTNode* right_argument) {
	if (operator_kind == OverloadableOperator::None) {
		return OperatorOverloadResult::no_overload();
	}

	// --- Unified candidate set per C++20 [over.match.oper]/2 ---
	struct OperatorCandidate {
		ConversionRank lhs_rank;
		ConversionRank rhs_rank;
		const StructMemberFunction* member_func = nullptr;
		const FunctionDeclarationNode* free_func = nullptr;
		bool is_free_function = false;
		const TypeSpecifierNode* rhs_param_type = nullptr;
	};
	std::vector<OperatorCandidate> candidates;

	TypeIndex left_type_index = left_type_spec.type_index();
	TypeIndex right_type_index = right_type_spec.type_index();
	const size_t type_info_size = getTypeInfoCount();

	// --- 1. Gather member-function candidates (recursive through base classes) ---
	// Uses self-referencing lambda pattern to avoid std::function overhead.
	auto gatherMemberCandidates = [&](auto& self, TypeIndex struct_idx) -> void {
		if (!struct_idx.is_valid() || struct_idx.index() >= type_info_size)
			return;
		const StructTypeInfo* si = getTypeInfo(struct_idx).getStructInfo();
		if (!si)
			return;

		for (const auto& member_func : si->member_functions) {
			if (operator_kind == OverloadableOperator::Assign) {
				if (!isAssignOperator(member_func.operator_kind))
					continue;
			} else {
				if (member_func.operator_kind != operator_kind)
					continue;
			}

			if (!member_func.function_decl.is<FunctionDeclarationNode>())
				continue;
			const auto& params = member_func.function_decl.as<FunctionDeclarationNode>().parameter_nodes();
			if (params.empty() || !params[0].is<DeclarationNode>())
				continue;
			if (countMinRequiredArgs(member_func.function_decl.as<FunctionDeclarationNode>()) > 1)
				continue;
			const auto& param_type_node = params[0].as<DeclarationNode>().type_node();
			if (!param_type_node.is<TypeSpecifierNode>())
				continue;

			ConversionRank lhs_rank = rankImplicitObjectToOperator(
				left_type_spec,
				member_func,
				left_type_index,
				struct_idx);
			if (lhs_rank == ConversionRank::NoMatch)
				continue;

			const TypeSpecifierNode& rhs_param_spec = param_type_node.as<TypeSpecifierNode>();
			ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(
				right_type_spec,
				rhs_param_spec,
				struct_idx,
				right_argument);
			if (rhs_rank != ConversionRank::NoMatch) {
				candidates.push_back({lhs_rank, rhs_rank, &member_func, nullptr, false, &rhs_param_spec});
			}
		}

		// Recurse into base classes
		for (const auto& base_spec : si->base_classes) {
			if (base_spec.type_index.is_valid() && base_spec.type_index.index() < type_info_size) {
				self(self, base_spec.type_index);
			}
		}
	};
	gatherMemberCandidates(gatherMemberCandidates, left_type_index);

	StringBuilder op_name_sb;
	op_name_sb.append("operator").append(overloadableOperatorToString(operator_kind));
	const std::array<TypeSpecifierNode, 2> operand_types{left_type_spec, right_type_spec};
	const std::vector<ASTNode> overloads = collectFreeOperatorCandidates(
		op_name_sb.commit(),
		operand_types,
		symbol_table);

	for (const auto& overload : overloads) {
		if (!overload.is<FunctionDeclarationNode>())
			continue;
		const auto& func_decl = overload.as<FunctionDeclarationNode>();
		const auto& params = func_decl.parameter_nodes();
		if (params.size() < 2)
			continue;

		if (!params[0].is<DeclarationNode>())
			continue;
		const auto& p0_type = params[0].as<DeclarationNode>().type_node();
		if (!p0_type.is<TypeSpecifierNode>())
			continue;
		const auto& p0_spec = p0_type.as<TypeSpecifierNode>();

		if (!params[1].is<DeclarationNode>())
			continue;
		const auto& p1_type = params[1].as<DeclarationNode>().type_node();
		if (!p1_type.is<TypeSpecifierNode>())
			continue;
		const auto& p1_spec = p1_type.as<TypeSpecifierNode>();

		ConversionRank lhs_rank = rankBinaryOperatorOperandMatch(
			left_type_spec,
			p0_spec,
			left_type_index,
			left_argument);
		if (lhs_rank == ConversionRank::NoMatch)
			continue;

		ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(
			right_type_spec,
			p1_spec,
			right_type_index,
			right_argument);
		if (rhs_rank == ConversionRank::NoMatch)
			continue;

		candidates.push_back({lhs_rank, rhs_rank, nullptr, &func_decl, true, &p1_spec});
	}

	// --- 3. Rank all candidates per [over.match.best]/2 ---
	if (candidates.empty()) {
		return OperatorOverloadResult::no_overload();
	}

	std::vector<const OperatorCandidate*> best_candidates;
	best_candidates.reserve(candidates.size());

	for (size_t i = 0; i < candidates.size(); ++i) {
		const auto& candidate = candidates[i];
		bool is_dominated = false;

		for (size_t j = 0; j < candidates.size(); ++j) {
			if (i == j)
				continue;
			if (compareBinaryOperatorCandidatesWithRefTiebreak(
					candidates[j].lhs_rank,
					candidates[j].rhs_rank,
					candidates[j].rhs_param_type,
					candidate.lhs_rank,
					candidate.rhs_rank,
					candidate.rhs_param_type,
					right_type_spec) == BinaryOperatorCandidateComparison::Better) {
				is_dominated = true;
				break;
			}
		}

		if (!is_dominated) {
			best_candidates.push_back(&candidate);
		}
	}

	if (best_candidates.empty()) {
		return OperatorOverloadResult::no_match();
	}

	if (best_candidates.size() != 1) {
		return OperatorOverloadResult::ambiguous();
	}

	const OperatorCandidate* best = best_candidates[0];

	// Return the winner
	if (best->is_free_function) {
		return OperatorOverloadResult(best->free_func);
	} else {
		return OperatorOverloadResult(best->member_func);
	}
}

inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind,
	const SymbolTable& symbol_table) {
	return findBinaryOperatorOverloadWithFreeFunction(
		left_type_spec,
		right_type_spec,
		operator_kind,
		symbol_table,
		nullptr,
		nullptr);
}

inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	TypeIndex left_type_index,
	TypeIndex right_type_index,
	OverloadableOperator operator_kind,
	const SymbolTable& symbol_table,
	TypeCategory right_type,
	const ASTNode* left_argument,
	const ASTNode* right_argument) {
	TypeCategory effective_right_type = right_type;
	if (right_type_index.is_valid()) {
		TypeCategory indexed_right_type = resolve_type_alias(right_type_index);
		if (binaryOperatorUsesTypeIndexIdentity(indexed_right_type)) {
			effective_right_type = TypeCategory::Invalid;
		}
	}
	return findBinaryOperatorOverloadWithFreeFunction(
		makeBinaryOperatorTypeSpecifier(left_type_index.withCategory(TypeCategory::Invalid)),
		makeBinaryOperatorTypeSpecifier(right_type_index.withCategory(effective_right_type)),
		operator_kind,
		symbol_table,
		left_argument,
		right_argument);
}

inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	TypeIndex left_type_index,
	TypeIndex right_type_index,
	OverloadableOperator operator_kind,
	const SymbolTable& symbol_table,
	TypeCategory right_type) {
	return findBinaryOperatorOverloadWithFreeFunction(
		left_type_index,
		right_type_index,
		operator_kind,
		symbol_table,
		right_type,
		nullptr,
		nullptr);
}

// ============================================================================
// TypeIndex-based Function Signature Utilities (Phase 3)
// ============================================================================

/**
 * Create a TypeIndexArg from a TypeSpecifierNode
 * 
 * Extracts type information into a compact TypeIndexArg for fast comparison
 * and hashing in function signature caching.
 */
inline FlashCpp::TypeIndexArg makeTypeIndexArgFromSpec(const TypeSpecifierNode& spec) {
	FlashCpp::TypeIndexArg arg;
	arg.type_index = FlashCpp::canonicalizeTemplateIdentityTypeIndex(spec.type_index());
	arg.cv_qualifier = spec.cv_qualifier();
	arg.ref_qualifier = spec.reference_qualifier();
	arg.pointer_depth = static_cast<uint8_t>(std::min(spec.pointer_depth(), size_t(255)));
	// Include array info - critical for differentiating T[] from T[N] from T
	arg.is_array = spec.is_array();
	arg.pointee_array_declarator = spec.has_pointee_array_declarator();
	arg.array_sizes.assign(spec.array_dimensions().begin(), spec.array_dimensions().end());
	return arg;
}

/**
 * Create a FunctionSignatureKey from function name and argument types
 * 
 * This creates a TypeIndex-based key for function lookup caching.
 * The key can be used as a hash map key for O(1) function resolution cache lookups.
 */
inline FlashCpp::FunctionSignatureKey makeFunctionSignatureKey(
	StringHandle function_name,
	std::span<const TypeSpecifierNode> argument_types) {

	FlashCpp::FunctionSignatureKey key(function_name);
	key.param_types.reserve(argument_types.size());

	for (const auto& arg_type : argument_types) {
		key.param_types.push_back(makeTypeIndexArgFromSpec(arg_type));
	}

	return key;
}

/**
 * Global function resolution cache
 * 
 * Caches resolved function overloads keyed by function name + argument signature.
 * This avoids repeated overload resolution for the same function call patterns.
 * 
 * Key: FunctionSignatureKey (function name + TypeIndex-based parameter types)
 * Value: Pointer to the selected function declaration ASTNode (or nullptr if no match)
 */
inline std::unordered_map<FlashCpp::FunctionSignatureKey, OverloadResolutionResult,
						  FlashCpp::FunctionSignatureKeyHash>&
getFunctionResolutionCache() {
	static std::unordered_map<FlashCpp::FunctionSignatureKey, OverloadResolutionResult,
							  FlashCpp::FunctionSignatureKeyHash>
		cache;
	return cache;
}

/**
 * Clear the function resolution cache
 * 
 * Should be called when starting a new compilation unit or when
 * the symbol table changes (e.g., after parsing new declarations).
 */
inline void clearFunctionResolutionCache() {
	getFunctionResolutionCache().clear();
}

/**
 * Resolve overload with caching
 * 
 * First checks the cache for a previous resolution. If not found,
 * performs full overload resolution and caches the result.
 * 
 * @param function_name The function name handle
 * @param overloads Vector of candidate function overloads
 * @param argument_types Vector of argument TypeSpecifierNodes
 * @return OverloadResolutionResult with selected overload or no_match/ambiguous
 */
inline OverloadResolutionResult resolve_overload_cached(
	StringHandle function_name,
	std::span<const ASTNode> overloads,
	std::span<const TypeSpecifierNode> argument_types) {
	// Build signature key for cache lookup
	auto key = makeFunctionSignatureKey(function_name, argument_types);

	// Check cache first
	auto& cache = getFunctionResolutionCache();
	auto it = cache.find(key);
	if (it != cache.end()) {
		// Cache hit - return the cached result directly (preserves ambiguous/no_match/match states)
		return it->second;
	}

	// Cache miss - perform full overload resolution
	auto result = resolve_overload_with_argument_nodes(
		overloads,
		argument_types,
		std::span<const ASTNode>{});

	// Cache the result
	cache[key] = result;

	return result;
}

// Compute a "specificity score" for a function template used in partial ordering.
// Higher score means the template is more constrained / more specialized.
// Rules (additive):
//   +pointer_depth  per parameter  (U* > U)
//   +1 lvalue-ref, +1 rvalue-ref, +1 const  per parameter
//   +2 for concrete (non-bare-template-param) struct/user-defined parameter type
//   +2+N for a template specialisation with N args
// A bare template parameter (e.g. plain 'T') contributes 0 for the type category but
// still adds to the pointer / qualifier score when wrapped (e.g. 'T*' → +1 ptr).
struct CanonicalTemplateTypeBinding {
	uint32_t parameter_index;
	TypeId argument;
	std::optional<size_t> pack_element_index;
};

enum class CanonicalTemplateNonTypeArgumentKind : uint8_t {
	Literal,
	TemplateParameter,
	Unsupported,
};

// ExprId retains a literal's canonical expression identity. A direct function
// template NTTP additionally carries declaration/index identity and its
// canonical integral type; other dependent expression shapes fail closed.
struct CanonicalTemplateNonTypeArgumentPattern {
	ExprId expression;
	CanonicalTemplateNonTypeArgumentKind kind;
	TemplateDeclId parameter_decl;
	uint32_t parameter_index;
	TypeId parameter_type;
};

struct CanonicalTemplateNonTypeArgumentIdentity {
	ExprId expression;
	TemplateDeclId parameter_decl;
	uint32_t parameter_index;
	bool is_template_parameter;
	friend bool operator==(
		CanonicalTemplateNonTypeArgumentIdentity,
		CanonicalTemplateNonTypeArgumentIdentity) = default;
};

struct CanonicalTemplateNonTypeBinding {
	uint32_t parameter_index;
	CanonicalTemplateNonTypeArgumentIdentity argument;
};

struct CanonicalFunctionTemplateTypePattern {
	TypeId function_type;
	// The canonical function stores a pack's element type; these indices retain
	// where that element expands and which template parameter it binds.
	std::optional<size_t> function_parameter_pack_position;
	std::optional<uint32_t> template_parameter_pack_index;
	std::vector<CanonicalTemplateNonTypeArgumentPattern> non_type_arguments;
};

enum class CanonicalTemplateDeductionStatus : uint8_t {
	Match,
	Mismatch,
	Unsupported,
};

struct CanonicalTemplateTypeDeduction {
	CanonicalTemplateDeductionStatus status =
		CanonicalTemplateDeductionStatus::Unsupported;
	std::vector<CanonicalTemplateTypeBinding> bindings;
	std::vector<CanonicalTemplateNonTypeBinding> non_type_bindings;
};

// Deduce the function-template parameters in pattern_function from
// argument_function. TemplateParameter TypeIds from every other declaration
// remain rigid identities, which gives each declaration the unique types
// required by [temp.deduct.partial] without manufacturing semantic identities.
inline CanonicalTemplateTypeDeduction deduceCanonicalFunctionTemplateType(
	CanonicalTypeTable& table,
	const CanonicalFunctionTemplateTypePattern& pattern_function,
	const CanonicalFunctionTemplateTypePattern& argument_function,
	TemplateDeclId pattern_template) {
	CanonicalTemplateTypeDeduction result;
	if (!pattern_template || !pattern_function.function_type ||
		!argument_function.function_type ||
		(pattern_function.function_parameter_pack_position.has_value() !=
			pattern_function.template_parameter_pack_index.has_value()) ||
		(argument_function.function_parameter_pack_position.has_value() !=
			argument_function.template_parameter_pack_index.has_value())) {
		return result;
	}
	const CanonicalTypeNode pattern_function_node =
		table.node(pattern_function.function_type);
	const CanonicalTypeNode argument_function_node =
		table.node(argument_function.function_type);
	if (pattern_function_node.kind != CanonicalTypeKind::Function ||
		argument_function_node.kind != CanonicalTypeKind::Function) {
		return result;
	}

	struct TypePair {
		TypeId pattern;
		TypeId argument;
		std::optional<size_t> pack_element_index;
	};
	std::vector<TypePair> pending;
	std::vector<CanonicalTemplateTypeBinding> bindings;
	std::vector<CanonicalTemplateNonTypeBinding> non_type_bindings;
	auto find_non_type_argument_pattern = [](
		const CanonicalFunctionTemplateTypePattern& function,
		ExprId expression) -> const CanonicalTemplateNonTypeArgumentPattern* {
		const auto found = std::ranges::find_if(
			function.non_type_arguments,
			[expression](const CanonicalTemplateNonTypeArgumentPattern& argument) {
				return argument.expression == expression;
			});
		return found == function.non_type_arguments.end() ? nullptr : &*found;
	};
	auto strip_top_level_parameter_qualifiers = [&](TypeId type) {
		const CanonicalTypeNode node = table.node(type);
		if (node.kind == CanonicalTypeKind::LValueReference ||
			node.kind == CanonicalTypeKind::RValueReference) {
			type = node.child;
		}
		return table.withoutTopLevelQualifiers(type);
	};
	auto collect_function_parameters = [&](TypeId function) {
		std::vector<TypeId> parameters;
		TypeId parameter = table.functionParameters(function);
		while (parameter) {
			parameters.push_back(table.functionParameterType(parameter));
			parameter = table.functionParameterNext(parameter);
		}
		return parameters;
	};
	auto append_function_pairs = [
		&table,
		&pending,
		&collect_function_parameters,
		&strip_top_level_parameter_qualifiers](
			TypeId pattern,
			TypeId argument,
			std::optional<size_t> pattern_pack_position,
			std::optional<size_t> argument_pack_position,
			std::optional<size_t> enclosing_pack_element_index) {
		const CanonicalTypeNode pattern_node = table.node(pattern);
		const CanonicalTypeNode argument_node = table.node(argument);
		if (pattern_node.kind != CanonicalTypeKind::Function ||
			argument_node.kind != CanonicalTypeKind::Function ||
			pattern_node.builtin != argument_node.builtin ||
			pattern_node.qualifiers != argument_node.qualifiers ||
			pattern_node.flags != argument_node.flags ||
			(pattern_node.array_extent >> 32) != (argument_node.array_extent >> 32)) {
			return false;
		}
		pending.push_back(TypePair{
			pattern_node.child, argument_node.child,
			enclosing_pack_element_index});
		const std::vector<TypeId> pattern_parameters =
			collect_function_parameters(pattern);
		const std::vector<TypeId> argument_parameters =
			collect_function_parameters(argument);
		if ((pattern_pack_position.has_value() &&
				(*pattern_pack_position >= pattern_parameters.size() ||
				 *pattern_pack_position + 1 != pattern_parameters.size())) ||
			(argument_pack_position.has_value() &&
				(*argument_pack_position >= argument_parameters.size() ||
				 *argument_pack_position + 1 != argument_parameters.size()))) {
			return false;
		}
		auto append_parameter_pair = [&](size_t pattern_index,
									 size_t argument_index,
									 std::optional<size_t> pack_element_index) {
			pending.push_back(TypePair{
				strip_top_level_parameter_qualifiers(
					pattern_parameters[pattern_index]),
				strip_top_level_parameter_qualifiers(
					argument_parameters[argument_index]),
				pack_element_index});
		};
		if (pattern_pack_position.has_value()) {
			const size_t pattern_pack = *pattern_pack_position;
			if (argument_pack_position.has_value() &&
				*argument_pack_position < pattern_pack) {
				return false;
			}
			if (argument_parameters.size() < pattern_pack) {
				return false;
			}
			for (size_t index = 0; index < pattern_pack; ++index) {
				if (argument_pack_position.has_value() &&
					index >= *argument_pack_position) {
					return false;
				}
				append_parameter_pair(
					index, index, enclosing_pack_element_index);
			}
			for (size_t index = pattern_pack;
				 index < argument_parameters.size();
				 ++index) {
				if (argument_pack_position.has_value() &&
					index > *argument_pack_position) {
					return false;
				}
				append_parameter_pair(
					pattern_pack,
					index,
					index - pattern_pack);
			}
			return true;
		}
		if (argument_pack_position.has_value() ||
			pattern_parameters.size() != argument_parameters.size()) {
			return false;
		}
		for (size_t index = 0; index < pattern_parameters.size(); ++index) {
			append_parameter_pair(
				index, index, enclosing_pack_element_index);
		}
		return true;
	};
	if (!append_function_pairs(
			pattern_function.function_type,
			argument_function.function_type,
			pattern_function.function_parameter_pack_position,
			argument_function.function_parameter_pack_position,
			std::nullopt)) {
		result.status = CanonicalTemplateDeductionStatus::Mismatch;
		return result;
	}

	while (!pending.empty()) {
		const TypePair pair = pending.back();
		pending.pop_back();
		const CanonicalTypeNode pattern = table.node(pair.pattern);
		const CanonicalTypeNode argument = table.node(pair.argument);
		if (pattern.kind == CanonicalTypeKind::TemplateParameter &&
			table.templateParameterDecl(pair.pattern) == pattern_template) {
			const uint32_t parameter_index =
				table.templateParameterIndex(pair.pattern);
			const bool is_template_parameter_pack =
				pattern_function.template_parameter_pack_index.has_value() &&
				parameter_index ==
					*pattern_function.template_parameter_pack_index;
			if (is_template_parameter_pack &&
				!pair.pack_element_index.has_value()) {
				result.status = CanonicalTemplateDeductionStatus::Unsupported;
				return result;
			}
			const std::optional<size_t> binding_pack_element_index =
				is_template_parameter_pack
					? pair.pack_element_index
					: std::nullopt;
			auto existing = std::find_if(
				bindings.begin(), bindings.end(),
				[&](const CanonicalTemplateTypeBinding& binding) {
					return binding.parameter_index == parameter_index &&
						binding.pack_element_index == binding_pack_element_index;
				});
			if (existing == bindings.end()) {
				bindings.push_back(CanonicalTemplateTypeBinding{
					parameter_index, pair.argument,
					binding_pack_element_index});
			} else if (existing->argument != pair.argument) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			continue;
		}
		if (pattern.kind != argument.kind ||
			pattern.builtin != argument.builtin ||
			pattern.qualifiers != argument.qualifiers ||
			pattern.flags != argument.flags) {
			result.status = CanonicalTemplateDeductionStatus::Mismatch;
			return result;
		}
		switch (pattern.kind) {
		case CanonicalTypeKind::Builtin:
			break;
		case CanonicalTypeKind::Qualified:
		case CanonicalTypeKind::Pointer:
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference:
			pending.push_back(TypePair{
				pattern.child, argument.child, pair.pack_element_index});
			break;
		case CanonicalTypeKind::Array:
			if (pattern.array_extent != argument.array_extent) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			pending.push_back(TypePair{
				pattern.child, argument.child, pair.pack_element_index});
			break;
		case CanonicalTypeKind::Function:
			if (!append_function_pairs(
					pair.pattern,
					pair.argument,
					std::nullopt,
					std::nullopt,
					pair.pack_element_index)) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			break;
		case CanonicalTypeKind::Record:
		case CanonicalTypeKind::Enum:
			if (pattern.array_extent != argument.array_extent) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			break;
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer:
			pending.push_back(TypePair{
				table.memberPointerOwner(pair.pattern),
				table.memberPointerOwner(pair.argument),
				pair.pack_element_index});
			pending.push_back(TypePair{
				pattern.child, argument.child, pair.pack_element_index});
			break;
		case CanonicalTypeKind::TemplateParameter:
			if (pattern.array_extent != argument.array_extent) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			break;
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			if (pattern.array_extent != argument.array_extent) {
				result.status = CanonicalTemplateDeductionStatus::Mismatch;
				return result;
			}
			TypeId pattern_arg = table.templateSpecializationArguments(pair.pattern);
			TypeId argument_arg = table.templateSpecializationArguments(pair.argument);
			while (pattern_arg || argument_arg) {
				if (!pattern_arg || !argument_arg) {
					result.status = CanonicalTemplateDeductionStatus::Mismatch;
					return result;
				}
				const CanonicalTemplateArgKind pattern_kind =
					table.templateArgumentKind(pattern_arg);
				const CanonicalTemplateArgKind argument_kind =
					table.templateArgumentKind(argument_arg);
				if (pattern_kind != argument_kind) {
					result.status = CanonicalTemplateDeductionStatus::Mismatch;
					return result;
				}
				if (pattern_kind == CanonicalTemplateArgKind::Type) {
					pending.push_back(TypePair{
						table.templateArgumentType(pattern_arg),
						table.templateArgumentType(argument_arg),
						pair.pack_element_index});
				} else if (pattern_kind == CanonicalTemplateArgKind::NonType) {
					const ExprId pattern_expression =
						table.templateArgumentExpr(pattern_arg);
					const ExprId argument_expression =
						table.templateArgumentExpr(argument_arg);
					const CanonicalTemplateNonTypeArgumentPattern* pattern_argument =
						find_non_type_argument_pattern(
							pattern_function,
							pattern_expression);
					const CanonicalTemplateNonTypeArgumentPattern* argument_pattern =
						find_non_type_argument_pattern(
							argument_function,
							argument_expression);
					if (pattern_argument == nullptr || argument_pattern == nullptr) {
						result.status = CanonicalTemplateDeductionStatus::Unsupported;
						return result;
					}
					if (pattern_argument->kind ==
						CanonicalTemplateNonTypeArgumentKind::TemplateParameter) {
						CanonicalTemplateNonTypeArgumentIdentity identity{};
						if (argument_pattern->kind ==
							CanonicalTemplateNonTypeArgumentKind::TemplateParameter) {
							if (pattern_argument->parameter_type !=
								argument_pattern->parameter_type) {
								result.status = CanonicalTemplateDeductionStatus::Unsupported;
								return result;
							}
							identity = CanonicalTemplateNonTypeArgumentIdentity{
								ExprId{},
								argument_pattern->parameter_decl,
								argument_pattern->parameter_index,
								true};
						} else if (argument_pattern->kind ==
							CanonicalTemplateNonTypeArgumentKind::Literal) {
							identity = CanonicalTemplateNonTypeArgumentIdentity{
								argument_expression,
								TemplateDeclId{},
								0,
								false};
						} else {
							result.status = CanonicalTemplateDeductionStatus::Unsupported;
							return result;
						}
						const auto existing = std::ranges::find_if(
							non_type_bindings,
							[pattern_argument](const CanonicalTemplateNonTypeBinding& binding) {
								return binding.parameter_index ==
									pattern_argument->parameter_index;
							});
						if (existing == non_type_bindings.end()) {
							non_type_bindings.push_back(CanonicalTemplateNonTypeBinding{
								pattern_argument->parameter_index,
								identity});
						} else if (existing->argument != identity) {
							result.status = CanonicalTemplateDeductionStatus::Mismatch;
							return result;
						}
					} else if (pattern_argument->kind ==
						CanonicalTemplateNonTypeArgumentKind::Literal) {
						if (argument_pattern->kind ==
							CanonicalTemplateNonTypeArgumentKind::Literal) {
							if (pattern_expression != argument_expression) {
								// ExprId preserves expression identity, not converted
								// NTTP value equality across different literal spellings.
								result.status = CanonicalTemplateDeductionStatus::Unsupported;
								return result;
							}
						} else if (argument_pattern->kind ==
							CanonicalTemplateNonTypeArgumentKind::TemplateParameter) {
							result.status = CanonicalTemplateDeductionStatus::Mismatch;
							return result;
						} else {
							result.status = CanonicalTemplateDeductionStatus::Unsupported;
							return result;
						}
					} else {
						result.status = CanonicalTemplateDeductionStatus::Unsupported;
						return result;
					}
				} else if (pattern_kind == CanonicalTemplateArgKind::Template) {
					if (table.templateArgumentTemplate(pattern_arg) !=
						table.templateArgumentTemplate(argument_arg)) {
						result.status = CanonicalTemplateDeductionStatus::Unsupported;
						return result;
					}
				} else if (
					table.templateArgumentDependentTemplateDecl(pattern_arg) !=
						table.templateArgumentDependentTemplateDecl(argument_arg) ||
					table.templateArgumentDependentTemplateIndex(pattern_arg) !=
						table.templateArgumentDependentTemplateIndex(argument_arg)) {
					result.status = CanonicalTemplateDeductionStatus::Unsupported;
					return result;
				}
				pattern_arg = table.templateArgumentNext(pattern_arg);
				argument_arg = table.templateArgumentNext(argument_arg);
			}
			break;
		}
		case CanonicalTypeKind::DependentName:
		case CanonicalTypeKind::DependentTemplateMember:
		case CanonicalTypeKind::DependentMemberAlias:
		case CanonicalTypeKind::TemplateArg:
		case CanonicalTypeKind::NonTypeTemplateArg:
		case CanonicalTypeKind::TemplateTemplateArg:
		case CanonicalTypeKind::DependentTemplateTemplateArg:
		case CanonicalTypeKind::NameBytes:
			if (pair.pattern != pair.argument) {
				result.status = CanonicalTemplateDeductionStatus::Unsupported;
				return result;
			}
			break;
		case CanonicalTypeKind::FunctionParam:
			result.status = CanonicalTemplateDeductionStatus::Unsupported;
			return result;
		}
	}
	result.status = CanonicalTemplateDeductionStatus::Match;
	result.bindings = std::move(bindings);
	result.non_type_bindings = std::move(non_type_bindings);
	return result;
}

inline CanonicalTemplateTypeDeduction deduceCanonicalFunctionTemplateType(
	CanonicalTypeTable& table,
	TypeId pattern_function,
	TypeId argument_function,
	TemplateDeclId pattern_template) {
	return deduceCanonicalFunctionTemplateType(
		table,
		CanonicalFunctionTemplateTypePattern{
			pattern_function, std::nullopt, std::nullopt, {}},
		CanonicalFunctionTemplateTypePattern{
			argument_function, std::nullopt, std::nullopt, {}},
		pattern_template);
}

enum class CanonicalTemplatePartialOrdering : uint8_t {
	Neither,
	Equivalent,
	FirstMoreSpecialized,
	SecondMoreSpecialized,
	Unsupported,
};

inline CanonicalTemplatePartialOrdering compareCanonicalFunctionTemplateTypes(
	CanonicalTypeTable& table,
	const CanonicalFunctionTemplateTypePattern& first_function,
	TemplateDeclId first_template,
	const CanonicalFunctionTemplateTypePattern& second_function,
	TemplateDeclId second_template) {
	const CanonicalTemplateTypeDeduction second_from_first =
		deduceCanonicalFunctionTemplateType(
			table, second_function, first_function, second_template);
	const CanonicalTemplateTypeDeduction first_from_second =
		deduceCanonicalFunctionTemplateType(
			table, first_function, second_function, first_template);
	if (second_from_first.status == CanonicalTemplateDeductionStatus::Unsupported ||
		first_from_second.status == CanonicalTemplateDeductionStatus::Unsupported) {
		return CanonicalTemplatePartialOrdering::Unsupported;
	}
	const bool first_is_deducible_from_second =
		second_from_first.status == CanonicalTemplateDeductionStatus::Match;
	const bool second_is_deducible_from_first =
		first_from_second.status == CanonicalTemplateDeductionStatus::Match;
	if (first_is_deducible_from_second && !second_is_deducible_from_first) {
		return CanonicalTemplatePartialOrdering::FirstMoreSpecialized;
	}
	if (second_is_deducible_from_first && !first_is_deducible_from_second) {
		return CanonicalTemplatePartialOrdering::SecondMoreSpecialized;
	}
	if (first_is_deducible_from_second && second_is_deducible_from_first) {
		return CanonicalTemplatePartialOrdering::Equivalent;
	}
	return CanonicalTemplatePartialOrdering::Neither;
}

inline CanonicalTemplatePartialOrdering compareCanonicalFunctionTemplateTypes(
	CanonicalTypeTable& table,
	TypeId first_function,
	TemplateDeclId first_template,
	TypeId second_function,
	TemplateDeclId second_template) {
	return compareCanonicalFunctionTemplateTypes(
		table,
		CanonicalFunctionTemplateTypePattern{
			first_function, std::nullopt, std::nullopt, {}},
		first_template,
		CanonicalFunctionTemplateTypePattern{
			second_function, std::nullopt, std::nullopt, {}},
		second_template);
}

inline int computeFunctionTemplateSpecificity(const TemplateFunctionDeclarationNode& template_func) {
	std::unordered_set<StringHandle, StringHash> param_name_handles;
	for (const auto& tp : template_func.template_parameters()) {
		param_name_handles.insert(tp.nameHandle());
	}

	int score = 0;
	for (const auto& p : template_func.function_decl_node().parameter_nodes()) {
		if (!p.is<DeclarationNode>()) {
			continue;
		}
		const TypeSpecifierNode& ts = p.as<DeclarationNode>().type_specifier_node();
		StringHandle tok_handle = ts.token().handle();
		bool is_bare_template_param = tok_handle.isValid() && param_name_handles.count(tok_handle) > 0;

		if (is_struct_type(ts.category()) || ts.category() == TypeCategory::UserDefined) {
			if (!is_bare_template_param) {
				if (const TypeInfo* ti = tryGetTypeInfo(ts.type_index())) {
					if (ti->isTemplateInstantiation()) {
						score += 2 + static_cast<int>(ti->templateArgs().size());
					} else {
						score += 2;
					}
				} else {
					score += 2;
				}
			}
		} else if (ts.category() != TypeCategory::Invalid) {
			score += 1;
		}

		score += static_cast<int>(ts.pointer_depth());
		if (ts.is_lvalue_reference()) score += 1;
		if (ts.is_rvalue_reference()) score += 1;
		if (ts.is_const()) score += 1;
	}
	return score;
}
