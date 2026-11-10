#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"
#include "OverloadResolution.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <vector>

// Shared canonical constructor-candidate selection. Callers choose the policy
// for direct vs implicit initialization, explicit constructors, user-defined
// argument conversions, ellipsis, and copy/move inclusion. A definite match is
// never returned when a candidate is undecidable.
enum class CanonicalConstructorSelectionPolicy : uint8_t {
	None = 0,
	AllowExplicit = 1 << 0,
	AllowUserDefinedArgumentConversion = 1 << 1,
	AllowEllipsis = 1 << 2,
	IncludeCopyMoveConstructors = 1 << 3,
	RequireCompleteSchema = 1 << 4,
	LiteralArgumentsOnly = 1 << 5,
};

inline CanonicalConstructorSelectionPolicy operator|(CanonicalConstructorSelectionPolicy a, CanonicalConstructorSelectionPolicy b) {
	return static_cast<CanonicalConstructorSelectionPolicy>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool hasCanonicalConstructorSelectionPolicy(CanonicalConstructorSelectionPolicy flags, CanonicalConstructorSelectionPolicy bit) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(bit)) != 0;
}

enum class CanonicalConstructorSelectionStatus : uint8_t {
	Selected,
	NoMatch,
	Unknown,
};

struct CanonicalConstructorArgument {
	TypeId type;
	bool is_lvalue;
	CanonicalDefaultException exception;
};

struct CanonicalConstructorSelection {
	CanonicalConstructorSelectionStatus status = CanonicalConstructorSelectionStatus::Unknown;
	size_t constructor_index = 0;
	bool is_copy_or_move = false;
	bool is_implicit_or_defaulted = false;
	bool is_noexcept = false;
	uint16_t omitted_default_count = 0;
	bool omitted_defaults_nothrow = false;
	bool omitted_defaults_known_throwing = false;
	bool saw_known_throwing_candidate = false;
	bool selected_uses_class_parameter = false;
	std::vector<ConversionRank> argument_ranks;
};

// [basic.types] and [meta.unary.prop] classify a dependent identity by its
// eventual shape, which the canonical table cannot answer before substitution.
inline bool isDependentCanonicalNode(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::TemplateParameter || kind == CanonicalTypeKind::DependentName ||
		kind == CanonicalTypeKind::DependentTemplateMember || kind == CanonicalTypeKind::DependentMemberAlias;
}

inline bool containsUnresolvedConversionType(const CanonicalTypeTable& table, TypeId type) {
	for (;;) {
		const CanonicalTypeNode node = table.node(type);
		switch (node.kind) {
		case CanonicalTypeKind::Qualified:
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference:
		case CanonicalTypeKind::Pointer:
		case CanonicalTypeKind::Array:
			type = node.child;
			break;
		case CanonicalTypeKind::Builtin:
			return false;
		default:
			return true;
		}
	}
}

inline bool mayNeedCanonicalCompatibilityResolution(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::Record || kind == CanonicalTypeKind::TemplateSpecialization ||
		kind == CanonicalTypeKind::Enum || kind == CanonicalTypeKind::TemplateParameter ||
		kind == CanonicalTypeKind::DependentName || kind == CanonicalTypeKind::DependentTemplateMember ||
		kind == CanonicalTypeKind::DependentMemberAlias;
}

inline bool isCanonicalReferenceToClass(const CanonicalTypeTable& table, TypeId type, TypeId target_class) {
	const CanonicalTypeNode node = table.node(type);
	if ((node.kind != CanonicalTypeKind::LValueReference && node.kind != CanonicalTypeKind::RValueReference) || !node.child) {
		return false;
	}
	return table.withoutTopLevelQualifiers(node.child) == target_class;
}

inline bool isCanonicalCopyOrMoveConstructor(const CanonicalTypeTable& table, TypeId owner, size_t index) {
	return table.recordConstructorAt(owner, index).parameter_count == 1 &&
		isCanonicalReferenceToClass(table, table.recordConstructorParameterAt(owner, index, 0), owner);
}

// [over.best.ics]/[conv.derived]: a by-value class parameter accepts a
// same-class argument as an identity conversion and a publicly derived argument
// through derived-to-base. The base copy/move path is not part of the outer
// constructor schema, so require a usable public copy constructor; an rvalue
// source may also use a move constructor. Unpublished or incomplete base schemas
// defer to the compatibility resolver.
inline bool isUsableCanonicalClassCopyConstructor(CanonicalTypeTable& table, TypeId owner, size_t index, TypeId target_class, bool accept_move) {
	const CanonicalRecordConstructor constructor = table.recordConstructorAt(owner, index);
	if (constructor.parameter_count != 1 || hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::NonPublic) ||
		hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::Explicit) ||
		hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::SchemaIncomplete)) {
		return false;
	}
	const CanonicalTypeNode parameter = table.node(table.recordConstructorParameterAt(owner, index, 0));
	if (!parameter.child || (parameter.kind != CanonicalTypeKind::LValueReference &&
			(parameter.kind != CanonicalTypeKind::RValueReference || !accept_move))) {
		return false;
	}
	return table.withoutTopLevelQualifiers(parameter.child) == target_class;
}

inline std::optional<ConversionPlan> tryBuildCanonicalClassStandardConversionPlan(
	CanonicalTypeTable& table, TypeId source_class, TypeId target_class, bool source_is_lvalue) {
	if (source_class == target_class) {
		return ConversionPlan::exact_match();
	}
	const std::optional<DerivedBaseConversionKind> relation = classifyCanonicalDerivedBaseConversion(table, source_class, target_class);
	if (!relation.has_value()) {
		return std::nullopt;
	}
	if (*relation != DerivedBaseConversionKind::UniquePublicNonVirtual && *relation != DerivedBaseConversionKind::PublicVirtual) {
		return std::nullopt;
	}
	if (!table.hasRecordConstructors(target_class)) {
		// An unpublished constructor schema can still prove the implicit copy/move
		// path from the trivially-copyable fact; otherwise the compatibility
		// resolver owns the answer.
		if (table.hasRecordProperties(target_class) &&
			hasCanonicalRecordFact(table.recordProperties(target_class).facts, CanonicalRecordFacts::TriviallyCopyable)) {
			return ConversionPlan{ConversionRank::Conversion, StandardConversionKind::DerivedToBase, true};
		}
		return std::nullopt;
	}
	const size_t constructor_count = table.recordConstructorCount(target_class);
	for (size_t index = 0; index < constructor_count; ++index) {
		if (hasCanonicalRecordFunctionFlag(table.recordConstructorAt(target_class, index).flags, CanonicalRecordFunctionFlags::SchemaIncomplete)) {
			return std::nullopt;
		}
	}
	for (size_t index = 0; index < constructor_count; ++index) {
		if (isUsableCanonicalClassCopyConstructor(table, target_class, index, target_class, !source_is_lvalue)) {
			return ConversionPlan{ConversionRank::Conversion, StandardConversionKind::DerivedToBase, true};
		}
	}
	return std::nullopt;
}

// One argument of a non-explicit converting constructor must be reachable
// through a standard conversion sequence; a second user-defined conversion is
// not allowed in the same implicit conversion sequence.
inline std::optional<ConversionPlan> tryCanonicalStandardParameterConversionPlan(
	CanonicalTypeTable& table, TypeId source_argument, TypeId parameter_type) {
	const CanonicalTypeKind parameter_kind = table.node(parameter_type).kind;
	if (parameter_kind == CanonicalTypeKind::LValueReference || parameter_kind == CanonicalTypeKind::RValueReference) {
		return tryBuildCanonicalReferenceArgumentConversionPlan(table, source_argument, parameter_type);
	}
	TypeId source_type = source_argument;
	bool source_is_lvalue = false;
	while (table.node(source_type).kind == CanonicalTypeKind::LValueReference || table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
		source_is_lvalue = table.node(source_type).kind == CanonicalTypeKind::LValueReference;
		source_type = table.node(source_type).child;
	}
	const TypeId source_base = table.withoutTopLevelQualifiers(source_type);
	const TypeId target_base = table.withoutTopLevelQualifiers(parameter_type);
	const CanonicalTypeKind source_kind = table.node(source_base).kind;
	const CanonicalTypeKind target_kind = table.node(target_base).kind;
	const bool source_is_class = source_kind == CanonicalTypeKind::Record || source_kind == CanonicalTypeKind::TemplateSpecialization;
	const bool target_is_class = target_kind == CanonicalTypeKind::Record || target_kind == CanonicalTypeKind::TemplateSpecialization;
	if (source_is_class && target_is_class) {
		return tryBuildCanonicalClassStandardConversionPlan(table, source_base, target_base, source_is_lvalue);
	}
	if (source_kind == CanonicalTypeKind::Enum && target_kind == CanonicalTypeKind::Builtin) {
		return tryBuildCanonicalEnumToBuiltinConversionPlan(table, source_argument, parameter_type);
	}
	const ConversionPlan conversion = buildCanonicalStructuralConversionPlan(table, source_argument, parameter_type);
	if (conversion.is_valid) {
		return conversion;
	}
	if (containsUnresolvedConversionType(table, source_argument) || containsUnresolvedConversionType(table, parameter_type)) {
		return std::nullopt;
	}
	return conversion;
}

enum class CanonicalConstructorParameterMatch : uint8_t {
	Match,
	NoMatch,
	Unknown,
	KnownThrowing,
};

// [over.ics.user]/[class.conv.ctor]: one non-explicit public constructor of the
// target class can provide the user-defined conversion for an argument. Deleted,
// incomplete, ambiguous, or undecidable candidate conversions defer so sema
// keeps the compatibility answer.
inline CanonicalConstructorParameterMatch matchCanonicalUserDefinedClassConversion(
	CanonicalTypeTable& table, TypeId source_argument, TypeId target_class, ConversionPlan& plan) {
	if (!table.hasRecordConstructors(target_class)) {
		return CanonicalConstructorParameterMatch::Unknown;
	}
	const size_t constructor_count = table.recordConstructorCount(target_class);
	for (size_t index = 0; index < constructor_count; ++index) {
		if (hasCanonicalRecordFunctionFlag(table.recordConstructorAt(target_class, index).flags, CanonicalRecordFunctionFlags::SchemaIncomplete)) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
	}
	size_t viable_count = 0;
	bool has_undecidable = false;
	for (size_t index = 0; index < constructor_count; ++index) {
		const CanonicalRecordConstructor constructor = table.recordConstructorAt(target_class, index);
		if (hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::Explicit) ||
			hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::NonPublic) ||
			constructor.minimum_parameter_count > 1 || constructor.parameter_count == 0) {
			continue;
		}
		const TypeId parameter = table.recordConstructorParameterAt(target_class, index, 0);
		// A copy/move constructor cannot convert an unrelated class argument.
		if (isCanonicalReferenceToClass(table, parameter, target_class)) {
			continue;
		}
		const std::optional<ConversionPlan> candidate_plan = tryCanonicalStandardParameterConversionPlan(table, source_argument, parameter);
		if (!candidate_plan.has_value()) {
			has_undecidable = true;
			continue;
		}
		if (candidate_plan->is_valid) {
			++viable_count;
		}
	}
	if (has_undecidable) {
		return CanonicalConstructorParameterMatch::Unknown;
	}
	if (viable_count != 1) {
		return CanonicalConstructorParameterMatch::NoMatch;
	}
	plan = ConversionPlan{ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
	return CanonicalConstructorParameterMatch::Match;
}

inline std::optional<ConversionPlan> tryBuildCanonicalUserDefinedClassConversionPlan(
	CanonicalTypeTable& table, TypeId source_argument, TypeId target_class) {
	ConversionPlan plan;
	if (matchCanonicalUserDefinedClassConversion(table, source_argument, target_class, plan) !=
		CanonicalConstructorParameterMatch::Match) {
		return std::nullopt;
	}
	return plan;
}

inline std::optional<ConversionPlan> tryBuildCanonicalClassParameterConversionPlan(
	CanonicalTypeTable& table, TypeId source_argument, TypeId target_class) {
	TypeId source_type = source_argument;
	bool source_is_lvalue = false;
	while (table.node(source_type).kind == CanonicalTypeKind::LValueReference || table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
		source_is_lvalue = table.node(source_type).kind == CanonicalTypeKind::LValueReference;
		source_type = table.node(source_type).child;
	}
	const TypeId source_class = table.withoutTopLevelQualifiers(source_type);
	if (const std::optional<ConversionPlan> standard =
			tryBuildCanonicalClassStandardConversionPlan(table, source_class, target_class, source_is_lvalue)) {
		return standard;
	}
	return tryBuildCanonicalUserDefinedClassConversionPlan(table, source_argument, target_class);
}

inline CanonicalConstructorSelection selectCanonicalConstructor(
	CanonicalTypeTable& table, TypeId target_class, std::span<const CanonicalConstructorArgument> arguments,
	CanonicalConstructorSelectionPolicy policy);

// A scalar literal can initialize a class-typed constructor parameter through
// one non-explicit public converting constructor. The nested selection reuses
// the same service, and a throwing nested constructor or omitted throwing
// default makes the conversion known throwing.
inline std::optional<bool> tryCanonicalScalarToClassArgumentConversionNoexcept(
	CanonicalTypeTable& table, TypeId argument_type, TypeId target_class) {
	if (!table.hasRecordConstructors(target_class) || !table.hasRecordProperties(target_class)) {
		return std::nullopt;
	}
	if (!hasCanonicalRecordFact(table.recordProperties(target_class).facts, CanonicalRecordFacts::NothrowDestructible)) {
		return false;
	}
	const CanonicalConstructorArgument selection_argument{argument_type, false, CanonicalDefaultException::Unknown};
	const CanonicalConstructorSelection selection = selectCanonicalConstructor(
		table, target_class, std::span<const CanonicalConstructorArgument>(&selection_argument, 1),
		CanonicalConstructorSelectionPolicy::RequireCompleteSchema);
	if (selection.status != CanonicalConstructorSelectionStatus::Selected) {
		return std::nullopt;
	}
	if (!selection.is_noexcept || selection.omitted_defaults_known_throwing) {
		return false;
	}
	if (selection.omitted_default_count > 0 && !selection.omitted_defaults_nothrow) {
		return std::nullopt;
	}
	return true;
}

inline CanonicalConstructorParameterMatch matchCanonicalConstructorParameter(
	CanonicalTypeTable& table, const CanonicalConstructorArgument& argument, TypeId parameter_type,
	CanonicalConstructorSelectionPolicy policy, ConversionPlan& plan) {
	if (hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::LiteralArgumentsOnly)) {
		if (argument.exception != CanonicalDefaultException::Nothrow) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
		TypeId literal_parameter_object = parameter_type;
		while (table.node(literal_parameter_object).kind == CanonicalTypeKind::LValueReference ||
			table.node(literal_parameter_object).kind == CanonicalTypeKind::RValueReference) {
			literal_parameter_object = table.node(literal_parameter_object).child;
		}
		const TypeId literal_target = table.withoutTopLevelQualifiers(literal_parameter_object);
		const CanonicalTypeKind literal_target_kind = table.node(literal_target).kind;
		if (literal_target_kind == CanonicalTypeKind::Builtin) {
			plan = ConversionPlan::exact_match();
			return CanonicalConstructorParameterMatch::Match;
		}
		if (!argument.type) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
		if (isCanonicalClassTypeKind(literal_target_kind)) {
			const std::optional<bool> conversion =
				tryCanonicalScalarToClassArgumentConversionNoexcept(table, argument.type, literal_target);
			if (!conversion.has_value()) {
				return CanonicalConstructorParameterMatch::Unknown;
			}
			if (!*conversion) {
				return CanonicalConstructorParameterMatch::KnownThrowing;
			}
			plan = ConversionPlan{ConversionRank::UserDefined, StandardConversionKind::UserDefined, true};
			return CanonicalConstructorParameterMatch::Match;
		}
		return CanonicalConstructorParameterMatch::Unknown;
	}
	TypeId source_type = argument.type;
	while (table.node(source_type).kind == CanonicalTypeKind::LValueReference || table.node(source_type).kind == CanonicalTypeKind::RValueReference) {
		source_type = table.node(source_type).child;
	}
	const TypeId source_base = table.withoutTopLevelQualifiers(source_type);
	TypeId parameter_object = parameter_type;
	while (table.node(parameter_object).kind == CanonicalTypeKind::LValueReference ||
		table.node(parameter_object).kind == CanonicalTypeKind::RValueReference) {
		parameter_object = table.node(parameter_object).child;
	}
	const TypeId target_base = table.withoutTopLevelQualifiers(parameter_object);
	const CanonicalTypeKind source_kind = table.node(source_base).kind;
	const CanonicalTypeKind target_kind = table.node(target_base).kind;
	const bool source_is_class = source_kind == CanonicalTypeKind::Record || source_kind == CanonicalTypeKind::TemplateSpecialization;
	const bool target_is_class = target_kind == CanonicalTypeKind::Record || target_kind == CanonicalTypeKind::TemplateSpecialization;
	const CanonicalTypeKind parameter_kind = table.node(parameter_type).kind;
	if ((source_kind == CanonicalTypeKind::Builtin || source_kind == CanonicalTypeKind::Enum) && target_is_class &&
		hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::AllowUserDefinedArgumentConversion)) {
		ConversionPlan user_defined_plan;
		const CanonicalConstructorParameterMatch user_defined = matchCanonicalUserDefinedClassConversion(
			table, argument.type, target_base, user_defined_plan);
		if (user_defined == CanonicalConstructorParameterMatch::Match) {
			plan = user_defined_plan;
			return CanonicalConstructorParameterMatch::Match;
		}
		if (user_defined == CanonicalConstructorParameterMatch::NoMatch) {
			return CanonicalConstructorParameterMatch::NoMatch;
		}
		return CanonicalConstructorParameterMatch::Unknown;
	}
	if (parameter_kind == CanonicalTypeKind::LValueReference || parameter_kind == CanonicalTypeKind::RValueReference) {
		if (source_kind == CanonicalTypeKind::Enum && target_is_class) {
			return CanonicalConstructorParameterMatch::NoMatch;
		}
		const std::optional<ConversionPlan> binding = tryBuildCanonicalReferenceArgumentConversionPlan(table, argument.type, parameter_type);
		if (!binding.has_value()) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
		if (!binding->is_valid) {
			return CanonicalConstructorParameterMatch::NoMatch;
		}
		plan = *binding;
		return CanonicalConstructorParameterMatch::Match;
	}
	if (source_kind == CanonicalTypeKind::Enum && target_kind == CanonicalTypeKind::Builtin) {
		const std::optional<ConversionPlan> conversion = tryBuildCanonicalEnumToBuiltinConversionPlan(table, argument.type, parameter_type);
		if (!conversion.has_value()) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
		if (!conversion->is_valid) {
			return CanonicalConstructorParameterMatch::NoMatch;
		}
		plan = *conversion;
		return CanonicalConstructorParameterMatch::Match;
	}
	if ((source_kind == CanonicalTypeKind::Enum && !target_is_class && !isDependentCanonicalNode(target_kind)) ||
		(target_kind == CanonicalTypeKind::Enum && !source_is_class && !isDependentCanonicalNode(source_kind))) {
		return CanonicalConstructorParameterMatch::NoMatch;
	}
	if (source_is_class && target_is_class) {
		std::optional<ConversionPlan> class_plan;
		if (hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::AllowUserDefinedArgumentConversion)) {
			class_plan = tryBuildCanonicalClassParameterConversionPlan(table, argument.type, target_base);
		} else {
			class_plan = tryBuildCanonicalClassStandardConversionPlan(table, source_base, target_base, argument.is_lvalue);
		}
		if (!class_plan.has_value()) {
			return CanonicalConstructorParameterMatch::Unknown;
		}
		if (!class_plan->is_valid) {
			return CanonicalConstructorParameterMatch::NoMatch;
		}
		plan = *class_plan;
		return CanonicalConstructorParameterMatch::Match;
	}
	if (mayNeedCanonicalCompatibilityResolution(source_kind) || mayNeedCanonicalCompatibilityResolution(target_kind)) {
		return CanonicalConstructorParameterMatch::Unknown;
	}
	const ConversionPlan conversion = buildCanonicalStructuralConversionPlan(table, argument.type, parameter_type);
	if (conversion.is_valid) {
		plan = conversion;
		return CanonicalConstructorParameterMatch::Match;
	}
	if (containsUnresolvedConversionType(table, argument.type) || containsUnresolvedConversionType(table, parameter_type)) {
		return CanonicalConstructorParameterMatch::Unknown;
	}
	return CanonicalConstructorParameterMatch::NoMatch;
}

inline int compareCanonicalConversionRanks(std::span<const ConversionRank> lhs, std::span<const ConversionRank> rhs) {
	bool lhs_better = false;
	bool rhs_better = false;
	for (size_t index = 0; index < lhs.size(); ++index) {
		lhs_better = lhs_better || lhs[index] < rhs[index];
		rhs_better = rhs_better || rhs[index] < lhs[index];
	}
	if (lhs_better && !rhs_better) {
		return 1;
	}
	if (rhs_better && !lhs_better) {
		return -1;
	}
	return 0;
}

inline CanonicalConstructorSelection selectCanonicalConstructor(
	CanonicalTypeTable& table, TypeId target_class, std::span<const CanonicalConstructorArgument> arguments,
	CanonicalConstructorSelectionPolicy policy) {
	CanonicalConstructorSelection result;
	if (!table.hasRecordConstructors(target_class)) {
		return result;
	}
	const size_t constructor_count = table.recordConstructorCount(target_class);
	if (constructor_count == 0) {
		return result;
	}
	if (hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::RequireCompleteSchema)) {
		for (size_t index = 0; index < constructor_count; ++index) {
			if (hasCanonicalRecordFunctionFlag(table.recordConstructorAt(target_class, index).flags, CanonicalRecordFunctionFlags::SchemaIncomplete)) {
				return result;
			}
		}
	}
	struct Candidate {
		size_t index;
		CanonicalRecordConstructor constructor;
		bool is_copy_or_move;
		bool is_implicit_or_defaulted;
		bool is_noexcept;
		uint16_t omitted_default_count;
		bool omitted_defaults_nothrow;
		bool omitted_defaults_known_throwing;
		bool uses_class_parameter;
		std::vector<ConversionRank> argument_ranks;
	};
	const bool include_copy_move = hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::IncludeCopyMoveConstructors);
	const bool allow_explicit = hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::AllowExplicit);
	const bool allow_ellipsis = hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::AllowEllipsis);
	const bool literal_only = hasCanonicalConstructorSelectionPolicy(policy, CanonicalConstructorSelectionPolicy::LiteralArgumentsOnly);
	std::vector<Candidate> best_candidates;
	best_candidates.reserve(constructor_count);
	bool saw_unknown = false;
	bool saw_known_throwing = false;
	for (size_t index = 0; index < constructor_count; ++index) {
		const CanonicalRecordConstructor constructor = table.recordConstructorAt(target_class, index);
		if (!allow_explicit && hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::Explicit)) {
			continue;
		}
		if (hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::NonPublic)) {
			continue;
		}
		const bool is_copy_or_move = isCanonicalCopyOrMoveConstructor(table, target_class, index);
		if (!include_copy_move && is_copy_or_move) {
			continue;
		}
		const bool is_variadic = hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::Variadic);
		if (arguments.size() < constructor.minimum_parameter_count) {
			continue;
		}
		if (arguments.size() > constructor.parameter_count && (!is_variadic || !allow_ellipsis)) {
			continue;
		}
		Candidate candidate{index, constructor, is_copy_or_move,
			hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::Implicit) ||
				hasCanonicalRecordFunctionFlag(constructor.flags, CanonicalRecordFunctionFlags::ExplicitlyDefaulted),
			constructor.is_noexcept != 0, 0, false, false, false, {}};
		candidate.argument_ranks.reserve(arguments.size());
		bool matches = true;
		const size_t matched_parameter_count = std::min(arguments.size(), static_cast<size_t>(constructor.parameter_count));
		for (size_t parameter = 0; parameter < matched_parameter_count; ++parameter) {
			const TypeId parameter_type = table.recordConstructorParameterAt(target_class, index, parameter);
			if (literal_only && isCanonicalReferenceToClass(table, parameter_type, target_class)) {
				matches = false;
				break;
			}
			ConversionPlan plan;
			const CanonicalConstructorParameterMatch match = matchCanonicalConstructorParameter(table, arguments[parameter], parameter_type, policy, plan);
			if (match == CanonicalConstructorParameterMatch::Unknown) {
				saw_unknown = true;
				matches = false;
				break;
			}
			if (match == CanonicalConstructorParameterMatch::KnownThrowing) {
				saw_known_throwing = true;
				matches = false;
				break;
			}
			if (match == CanonicalConstructorParameterMatch::NoMatch) {
				matches = false;
				break;
			}
			if (literal_only) {
				TypeId parameter_object = parameter_type;
				while (table.node(parameter_object).kind == CanonicalTypeKind::LValueReference ||
					table.node(parameter_object).kind == CanonicalTypeKind::RValueReference) {
					parameter_object = table.node(parameter_object).child;
				}
				candidate.uses_class_parameter = candidate.uses_class_parameter ||
					isCanonicalClassTypeKind(table.node(table.withoutTopLevelQualifiers(parameter_object)).kind);
			}
			candidate.argument_ranks.push_back(plan.rank);
		}
		if (!matches) {
			continue;
		}
		if (arguments.size() > constructor.parameter_count) {
			for (size_t parameter = matched_parameter_count; parameter < arguments.size(); ++parameter) {
				if (literal_only) {
					if (arguments[parameter].exception != CanonicalDefaultException::Nothrow) {
						saw_unknown = true;
						matches = false;
						break;
					}
					candidate.argument_ranks.push_back(ConversionRank::Ellipsis);
					continue;
				}
				TypeId argument_type = arguments[parameter].type;
				while (table.node(argument_type).kind == CanonicalTypeKind::LValueReference ||
					table.node(argument_type).kind == CanonicalTypeKind::RValueReference || table.node(argument_type).kind == CanonicalTypeKind::Qualified) {
					argument_type = table.node(argument_type).child;
				}
				const CanonicalTypeNode argument_node = table.node(argument_type);
				if (argument_node.kind == CanonicalTypeKind::Builtin && argument_node.builtin == CanonicalBuiltinKind::Void) {
					matches = false;
					break;
				}
				if (isDependentCanonicalNode(argument_node.kind)) {
					saw_unknown = true;
					matches = false;
					break;
				}
				candidate.argument_ranks.push_back(ConversionRank::Ellipsis);
			}
		}
		if (!matches) {
			continue;
		}
		if (arguments.size() < constructor.parameter_count) {
			candidate.omitted_default_count = static_cast<uint16_t>(static_cast<size_t>(constructor.parameter_count) - arguments.size());
			candidate.omitted_defaults_nothrow = true;
			candidate.omitted_defaults_known_throwing = false;
			for (size_t parameter = arguments.size(); parameter < constructor.parameter_count; ++parameter) {
				const size_t default_index = parameter - constructor.minimum_parameter_count;
				const CanonicalDefaultException exception = canonicalConstructorDefaultException(constructor, default_index);
				if (exception == CanonicalDefaultException::KnownThrowing) {
					candidate.omitted_defaults_nothrow = false;
					candidate.omitted_defaults_known_throwing = true;
					break;
				}
				if (exception == CanonicalDefaultException::Unknown) {
					candidate.omitted_defaults_nothrow = false;
				}
			}
		}
		if (std::ranges::any_of(best_candidates, [&candidate](const Candidate& current) {
			return compareCanonicalConversionRanks(current.argument_ranks, candidate.argument_ranks) > 0;
		})) {
			continue;
		}
		std::erase_if(best_candidates, [&candidate](const Candidate& current) {
			return compareCanonicalConversionRanks(candidate.argument_ranks, current.argument_ranks) > 0;
		});
		best_candidates.push_back(std::move(candidate));
	}
	result.saw_known_throwing_candidate = saw_known_throwing;
	if (saw_unknown) {
		result.status = CanonicalConstructorSelectionStatus::Unknown;
		return result;
	}
	if (best_candidates.empty()) {
		result.status = CanonicalConstructorSelectionStatus::NoMatch;
		return result;
	}
	if (best_candidates.size() > 1) {
		result.status = CanonicalConstructorSelectionStatus::Unknown;
		return result;
	}
	const Candidate& selected = best_candidates.front();
	result.status = CanonicalConstructorSelectionStatus::Selected;
	result.constructor_index = selected.index;
	result.is_copy_or_move = selected.is_copy_or_move;
	result.is_implicit_or_defaulted = selected.is_implicit_or_defaulted;
	result.is_noexcept = selected.is_noexcept;
	result.omitted_default_count = selected.omitted_default_count;
	result.omitted_defaults_nothrow = selected.omitted_defaults_nothrow;
	result.omitted_defaults_known_throwing = selected.omitted_defaults_known_throwing;
	result.selected_uses_class_parameter = selected.uses_class_parameter;
	result.argument_ranks = selected.argument_ranks;
	return result;
}
