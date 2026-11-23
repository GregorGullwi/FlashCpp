#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "AstNodeTypes.h"
#include "CanonicalTypeAdapter.h"
#include "TemplateTypes.h"

namespace FlashCpp {

namespace detail {

template <typename T>
inline void canonicalIdentityHashCombine(size_t& seed, const T& value) {
	seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

inline void canonicalIdentityHashCombineStringView(size_t& seed, std::string_view value) {
	canonicalIdentityHashCombine(seed, std::hash<std::string_view>{}(value));
}

} // namespace detail

inline bool canonicalTypeSpecifierIdentityEqual(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs);
inline size_t canonicalTypeSpecifierIdentityHash(const TypeSpecifierNode& type);

// Canonical identity is one declarator structure plus one terminal identity.
// The structure is outermost-first declarator components, so per-level pointer
// cv and array extents (including an unknown bound, which is its own component
// kind) stay structural instead of being re-derived from the flat projection.
// Terminal identity is a builtin kind, a published record/enum EntityId, a
// callable signature, a class-template specialization identity, or the opaque
// TypeIndex reserved for shapes that have no canonical form yet.
//
// EntityIds are stamped before a nominal layout is published, and no
// CanonicalTypeTable TypeId is hashed: these imports roll back, so the same
// shape can receive different TypeIds at different times.
enum class CanonicalTypeIdentityTerminalKind : uint8_t {
	Builtin,
	Record,
	Enum,
	Callable,
	TemplateSpecialization,
	Opaque,
};

struct CanonicalTypeIdentityShape {
	std::vector<DeclaratorComponent> components;
	CVQualifier base_cv = CVQualifier::None;
	TypeQualifier builtin_qualifier = TypeQualifier::None;
	TypeCategory terminal_category = TypeCategory::Invalid;
	CanonicalTypeIdentityTerminalKind terminal_kind = CanonicalTypeIdentityTerminalKind::Opaque;
	EntityId nominal_entity{};
	TypeIndex opaque_type_index{};
	TemplateDeclId specialization_template_decl{};
	bool specialization_is_alias = false;
	std::vector<SpecTemplateArgKind> specialization_arg_kinds;
	std::vector<TypeSpecifierNode> specialization_type_args;
	std::vector<TemplateDeclId> specialization_template_args;
	std::vector<SpecDependentTemplateArg> specialization_dependent_template_args;
	std::vector<uint64_t> specialization_value_payloads;
	bool has_function_signature = false;
	std::optional<FunctionSignature> function_signature;
	bool is_pack_expansion = false;
	bool has_concept_constraint = false;
	std::string_view concept_constraint;
	bool has_member_class_name = false;
	StringHandle member_class_name;
};

inline bool isCanonicalIdentityBuiltinCategory(TypeCategory category) {
	return is_builtin_type(category) || category == TypeCategory::Nullptr;
}

inline bool isCanonicalIdentityMemberPointerCategory(TypeCategory category) {
	return category == TypeCategory::MemberObjectPointer || category == TypeCategory::MemberFunctionPointer;
}

// Mirrors the member-pointer arm of appendDeclaratorShapeForSubstitution without
// that helper's published-owner requirement: identity must stay available for an
// unpublished owner too, where the member class name is the last resort.
inline void appendCanonicalMemberPointerIdentityComponents(const TypeSpecifierNode& type, std::vector<DeclaratorComponent>& components) {
	const bool is_function_pointer = type.category() == TypeCategory::MemberFunctionPointer;
	const CVQualifier outer_cv = type.top_level_cv_qualifier();
	if (type.has_member_class_entity()) {
		components.push_back(DeclaratorComponent::memberPointer(type.member_class_entity(), is_function_pointer, outer_cv));
	} else if (type.has_member_class_type_id()) {
		components.push_back(DeclaratorComponent::memberPointer(type.member_class_type_id(), is_function_pointer, outer_cv));
	} else {
		components.push_back(DeclaratorComponent::memberPointer(EntityId{}, is_function_pointer, outer_cv));
	}
}

inline CanonicalTypeIdentityShape buildCanonicalTypeIdentityShape(const TypeSpecifierNode& type) {
	CanonicalTypeIdentityShape shape;
	if (isCanonicalIdentityMemberPointerCategory(type.category()) || type.has_member_class()) {
		appendCanonicalMemberPointerIdentityComponents(type, shape.components);
	} else {
		appendDeclaratorShapeForSubstitution(type, shape.components);
	}

	// An alias contributes its own declarator shape inside the use-site shape.
	// appendOrderedAliasDeclaratorShape also sinks the pending cv onto the first
	// pointer level, matching the canonical adapter's alias composition.
	CVQualifier pending_cv = type.cv_qualifier();
	TypeIndex alias_index = type.type_index();
	size_t alias_depth_limit = getTypeInfoCount();
	while (alias_index.is_valid() && alias_depth_limit-- > 0) {
		const TypeInfo* alias_info = tryGetTypeInfo(alias_index);
		if (alias_info == nullptr || !alias_info->isTypeAlias()) {
			break;
		}
		const TypeSpecifierNode* alias_spec = alias_info->aliasTypeSpecifier();
		if (alias_spec == nullptr) {
			break;
		}
		appendOrderedAliasDeclaratorShape(shape.components, *alias_spec, pending_cv);
		alias_index = alias_spec->type_index().is_valid() ? alias_spec->type_index() : alias_info->type_index_;
	}
	shape.base_cv = pending_cv;
	shape.is_pack_expansion = type.is_pack_expansion();
	if (type.has_concept_constraint()) {
		shape.has_concept_constraint = true;
		shape.concept_constraint = type.concept_constraint();
	}
	if (type.has_member_class() &&
		std::ranges::none_of(shape.components, [](const DeclaratorComponent& component) {
			return component.hasMemberOwner();
		})) {
		shape.has_member_class_name = true;
		shape.member_class_name = type.member_class_name();
	}

	const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(type.type_index());
	TypeIndex terminal_index = resolved.type_index.is_valid() ? resolved.type_index : type.type_index();
	TypeCategory terminal_category = type.category();
	if (resolved.terminal_type_info != nullptr) {
		terminal_category = resolved.terminal_type_info->typeEnum();
	} else if (terminal_index.is_valid() && terminal_index.category() != TypeCategory::Invalid) {
		terminal_category = terminal_index.category();
	}
	shape.terminal_category = terminal_category;
	shape.builtin_qualifier = type.qualifier();
	shape.opaque_type_index = terminal_index;

	if (type.has_template_specialization()) {
		shape.terminal_kind = CanonicalTypeIdentityTerminalKind::TemplateSpecialization;
		shape.specialization_template_decl = type.specialization_template_decl();
		shape.specialization_is_alias = type.is_alias_template_specialization();
		const size_t argument_count = type.specialization_arg_count();
		shape.specialization_arg_kinds.reserve(argument_count);
		for (size_t index = 0; index < argument_count; ++index) {
			if (type.specialization_arg_is_type(index)) {
				shape.specialization_arg_kinds.push_back(SpecTemplateArgKind::Type);
				shape.specialization_type_args.push_back(type.specialization_arg_type(index));
			} else if (type.specialization_arg_is_template(index)) {
				shape.specialization_arg_kinds.push_back(SpecTemplateArgKind::Template);
				shape.specialization_template_args.push_back(type.specialization_arg_template(index));
			} else if (type.specialization_arg_is_dependent_template(index)) {
				shape.specialization_arg_kinds.push_back(SpecTemplateArgKind::DependentTemplate);
				shape.specialization_dependent_template_args.push_back(type.specialization_arg_dependent_template(index));
			} else if (type.specialization_arg_is_value(index)) {
				shape.specialization_arg_kinds.push_back(SpecTemplateArgKind::Value);
				shape.specialization_value_payloads.push_back(type.specialization_arg_value(index).value);
			} else {
				shape.specialization_arg_kinds.push_back(SpecTemplateArgKind::NonType);
				shape.specialization_value_payloads.push_back(type.specialization_arg_expr(index).value);
			}
		}
		return shape;
	}
	const bool terminal_is_instantiation = resolved.terminal_type_info != nullptr && resolved.terminal_type_info->isTemplateInstantiation();
	if (!terminal_is_instantiation && (terminal_category == TypeCategory::Struct || terminal_category == TypeCategory::Enum)) {
		EntityId entity = publishedNominalEntity(type);
		if (!entity) {
			TypeSpecifierNode terminal_spec(terminal_index, TypeQualifier::None, SizeInBits{0}, Token{}, CVQualifier::None);
			entity = publishedNominalEntity(terminal_spec);
		}
		if (entity) {
			shape.terminal_kind =
				terminal_category == TypeCategory::Struct ? CanonicalTypeIdentityTerminalKind::Record : CanonicalTypeIdentityTerminalKind::Enum;
			shape.nominal_entity = entity;
			return shape;
		}
	}

	if (type.has_function_signature() || resolved.function_signature.has_value() ||
		terminal_category == TypeCategory::Function || terminal_category == TypeCategory::FunctionPointer) {
		shape.terminal_kind = CanonicalTypeIdentityTerminalKind::Callable;
		if (type.has_function_signature()) {
			shape.has_function_signature = true;
			shape.function_signature = type.function_signature();
		} else if (resolved.function_signature.has_value()) {
			shape.has_function_signature = true;
			shape.function_signature = *resolved.function_signature;
		}
		return shape;
	}

	if (isCanonicalIdentityBuiltinCategory(terminal_category)) {
		shape.terminal_kind = CanonicalTypeIdentityTerminalKind::Builtin;
		return shape;
	}
	shape.terminal_kind = CanonicalTypeIdentityTerminalKind::Opaque;
	return shape;
}

inline bool canonicalTypeIdentityComponentsEqual(const std::vector<DeclaratorComponent>& lhs, const std::vector<DeclaratorComponent>& rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t index = 0; index < lhs.size(); ++index) {
		const DeclaratorComponent& left = lhs[index];
		const DeclaratorComponent& right = rhs[index];
		if (left.kind != right.kind || left.cv_qualifier != right.cv_qualifier) {
			return false;
		}
		if (left.kind == DeclaratorComponentKind::Array && left.payload != right.payload) {
			return false;
		}
		if (left.kind != DeclaratorComponentKind::MemberObjectPointer && left.kind != DeclaratorComponentKind::MemberFunctionPointer) {
			continue;
		}
		if (left.owner_identity_kind != right.owner_identity_kind) {
			return false;
		}
		if (left.owner_identity_kind == DeclaratorOwnerIdentityKind::Entity && left.member_owner != right.member_owner) {
			return false;
		}
		if (left.owner_identity_kind == DeclaratorOwnerIdentityKind::Type && left.payload != right.payload) {
			return false;
		}
	}
	return true;
}

inline bool canonicalTypeIdentityShapesEqual(const CanonicalTypeIdentityShape& lhs, const CanonicalTypeIdentityShape& rhs) {
	if (lhs.terminal_kind != rhs.terminal_kind || !canonicalTypeIdentityComponentsEqual(lhs.components, rhs.components) ||
		lhs.base_cv != rhs.base_cv || lhs.builtin_qualifier != rhs.builtin_qualifier || lhs.terminal_category != rhs.terminal_category ||
		lhs.is_pack_expansion != rhs.is_pack_expansion || lhs.has_concept_constraint != rhs.has_concept_constraint ||
		lhs.has_member_class_name != rhs.has_member_class_name || lhs.has_function_signature != rhs.has_function_signature) {
		return false;
	}
	if (lhs.has_member_class_name && lhs.member_class_name != rhs.member_class_name) {
		return false;
	}
	if (lhs.has_concept_constraint && lhs.concept_constraint != rhs.concept_constraint) {
		return false;
	}
	if (lhs.has_function_signature && !equalFunctionSignatureIdentity(*lhs.function_signature, *rhs.function_signature)) {
		return false;
	}
	switch (lhs.terminal_kind) {
	case CanonicalTypeIdentityTerminalKind::Builtin:
		return true;
	case CanonicalTypeIdentityTerminalKind::Record:
	case CanonicalTypeIdentityTerminalKind::Enum:
		return lhs.nominal_entity == rhs.nominal_entity;
	case CanonicalTypeIdentityTerminalKind::Callable:
		if (lhs.has_function_signature && rhs.has_function_signature) {
			return true;
		}
		return !lhs.has_function_signature && !rhs.has_function_signature && equalTypeIndexIdentity(lhs.opaque_type_index, rhs.opaque_type_index);
	case CanonicalTypeIdentityTerminalKind::TemplateSpecialization:
		if (lhs.specialization_template_decl != rhs.specialization_template_decl ||
			lhs.specialization_is_alias != rhs.specialization_is_alias ||
			lhs.specialization_arg_kinds != rhs.specialization_arg_kinds ||
			lhs.specialization_template_args != rhs.specialization_template_args ||
			lhs.specialization_value_payloads != rhs.specialization_value_payloads) {
			return false;
		}
		if (lhs.specialization_dependent_template_args.size() != rhs.specialization_dependent_template_args.size()) {
			return false;
		}
		for (size_t index = 0; index < lhs.specialization_dependent_template_args.size(); ++index) {
			if (!(lhs.specialization_dependent_template_args[index] == rhs.specialization_dependent_template_args[index])) {
				return false;
			}
		}
		if (lhs.specialization_type_args.size() != rhs.specialization_type_args.size()) {
			return false;
		}
		for (size_t index = 0; index < lhs.specialization_type_args.size(); ++index) {
			if (!canonicalTypeSpecifierIdentityEqual(lhs.specialization_type_args[index], rhs.specialization_type_args[index])) {
				return false;
			}
		}
		return true;
	case CanonicalTypeIdentityTerminalKind::Opaque:
		return equalTypeIndexIdentity(lhs.opaque_type_index, rhs.opaque_type_index);
	}
	return false;
}

inline size_t canonicalTypeIdentityComponentsHash(const std::vector<DeclaratorComponent>& components) {
	size_t seed = std::hash<size_t>{}(components.size());
	for (const DeclaratorComponent& component : components) {
		detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(component.kind)));
		detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(component.cv_qualifier)));
		if (component.kind == DeclaratorComponentKind::Array) {
			detail::canonicalIdentityHashCombine(seed, std::hash<uint64_t>{}(component.payload));
		}
		if (component.kind == DeclaratorComponentKind::MemberObjectPointer || component.kind == DeclaratorComponentKind::MemberFunctionPointer) {
			detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(component.owner_identity_kind)));
		}
	}
	return seed;
}

inline size_t canonicalTypeIdentityShapeHash(const CanonicalTypeIdentityShape& shape) {
	size_t seed = canonicalTypeIdentityComponentsHash(shape.components);
	detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(shape.terminal_kind)));
	detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(shape.terminal_category)));
	detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(shape.base_cv)));
	detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(shape.builtin_qualifier)));
	detail::canonicalIdentityHashCombine(seed, std::hash<bool>{}(shape.is_pack_expansion));
	detail::canonicalIdentityHashCombine(seed, std::hash<bool>{}(shape.has_concept_constraint));
	if (shape.has_concept_constraint) {
		detail::canonicalIdentityHashCombineStringView(seed, shape.concept_constraint);
	}
	if (shape.has_member_class_name) {
		detail::canonicalIdentityHashCombine(seed, std::hash<StringHandle>{}(shape.member_class_name));
	}
	switch (shape.terminal_kind) {
	case CanonicalTypeIdentityTerminalKind::Builtin:
		break;
	case CanonicalTypeIdentityTerminalKind::Record:
	case CanonicalTypeIdentityTerminalKind::Enum:
		detail::canonicalIdentityHashCombine(seed, std::hash<uint32_t>{}(shape.nominal_entity.value));
		break;
	case CanonicalTypeIdentityTerminalKind::Callable:
		if (shape.has_function_signature) {
			detail::canonicalIdentityHashCombine(seed, hashFunctionSignatureIdentity(*shape.function_signature));
		} else {
			detail::canonicalIdentityHashCombine(seed, hashTypeIndexIdentity(shape.opaque_type_index));
		}
		break;
	case CanonicalTypeIdentityTerminalKind::TemplateSpecialization:
		detail::canonicalIdentityHashCombine(seed, std::hash<uint32_t>{}(shape.specialization_template_decl.value));
		detail::canonicalIdentityHashCombine(seed, std::hash<bool>{}(shape.specialization_is_alias));
		for (const SpecTemplateArgKind kind : shape.specialization_arg_kinds) {
			detail::canonicalIdentityHashCombine(seed, std::hash<uint8_t>{}(static_cast<uint8_t>(kind)));
		}
		for (const TypeSpecifierNode& argument : shape.specialization_type_args) {
			detail::canonicalIdentityHashCombine(seed, canonicalTypeSpecifierIdentityHash(argument));
		}
		for (const TemplateDeclId argument : shape.specialization_template_args) {
			detail::canonicalIdentityHashCombine(seed, std::hash<uint32_t>{}(argument.value));
		}
		for (const SpecDependentTemplateArg& argument : shape.specialization_dependent_template_args) {
			detail::canonicalIdentityHashCombine(seed, std::hash<uint32_t>{}(argument.template_decl.value));
			detail::canonicalIdentityHashCombine(seed, std::hash<uint32_t>{}(argument.parameter_index));
		}
		break;
	case CanonicalTypeIdentityTerminalKind::Opaque:
		detail::canonicalIdentityHashCombine(seed, hashTypeIndexIdentity(shape.opaque_type_index));
		break;
	}
	return seed;
}

inline bool canonicalTypeSpecifierIdentityEqual(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
	return canonicalTypeIdentityShapesEqual(buildCanonicalTypeIdentityShape(lhs), buildCanonicalTypeIdentityShape(rhs));
}

inline size_t canonicalTypeSpecifierIdentityHash(const TypeSpecifierNode& type) {
	return canonicalTypeIdentityShapeHash(buildCanonicalTypeIdentityShape(type));
}

} // namespace FlashCpp
