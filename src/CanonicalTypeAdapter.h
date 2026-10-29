#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"
#include "FrontendContext.h"
#include "TemplateRegistry_Types.h"

#include <limits>
#include <optional>
#include <unordered_set>

inline std::optional<TypeCategory> canonicalBuiltinToTypeCategory(
	CanonicalBuiltinKind builtin) {
	switch (builtin) {
	case CanonicalBuiltinKind::Void: return TypeCategory::Void;
	case CanonicalBuiltinKind::Bool: return TypeCategory::Bool;
	case CanonicalBuiltinKind::Char:
	case CanonicalBuiltinKind::SignedChar: return TypeCategory::Char;
	case CanonicalBuiltinKind::UnsignedChar: return TypeCategory::UnsignedChar;
	case CanonicalBuiltinKind::WChar: return TypeCategory::WChar;
	case CanonicalBuiltinKind::Char8: return TypeCategory::Char8;
	case CanonicalBuiltinKind::Char16: return TypeCategory::Char16;
	case CanonicalBuiltinKind::Char32: return TypeCategory::Char32;
	case CanonicalBuiltinKind::Short: return TypeCategory::Short;
	case CanonicalBuiltinKind::UnsignedShort: return TypeCategory::UnsignedShort;
	case CanonicalBuiltinKind::Int: return TypeCategory::Int;
	case CanonicalBuiltinKind::UnsignedInt: return TypeCategory::UnsignedInt;
	case CanonicalBuiltinKind::Long: return TypeCategory::Long;
	case CanonicalBuiltinKind::UnsignedLong: return TypeCategory::UnsignedLong;
	case CanonicalBuiltinKind::LongLong: return TypeCategory::LongLong;
	case CanonicalBuiltinKind::UnsignedLongLong: return TypeCategory::UnsignedLongLong;
	case CanonicalBuiltinKind::Float: return TypeCategory::Float;
	case CanonicalBuiltinKind::Double: return TypeCategory::Double;
	case CanonicalBuiltinKind::LongDouble: return TypeCategory::LongDouble;
	case CanonicalBuiltinKind::Nullptr: return TypeCategory::Nullptr;
	case CanonicalBuiltinKind::Count: return std::nullopt;
	}
	return std::nullopt;
}

// The categories whose signedness is decided by the canonical classification map
// one-to-one onto a canonical builtin, so a flat compatibility predicate can
// defer to the canonical authority without carrying a category switch of its own.
// `signed char` shares the plain `char` category and is signed as well, so this
// projection loses nothing for the signedness question. It is deliberately not
// the inverse of canonicalBuiltinToTypeCategory: the signed/unsigned decision
// only needs the categories listed here.
inline std::optional<CanonicalBuiltinKind> canonicalBuiltinForSignedness(
	TypeCategory category) {
	switch (category) {
	case TypeCategory::Bool: return CanonicalBuiltinKind::Bool;
	case TypeCategory::Char: return CanonicalBuiltinKind::Char;
	case TypeCategory::WChar: return CanonicalBuiltinKind::WChar;
	case TypeCategory::Char8: return CanonicalBuiltinKind::Char8;
	case TypeCategory::Char16: return CanonicalBuiltinKind::Char16;
	case TypeCategory::Char32: return CanonicalBuiltinKind::Char32;
	case TypeCategory::Short: return CanonicalBuiltinKind::Short;
	case TypeCategory::UnsignedShort: return CanonicalBuiltinKind::UnsignedShort;
	case TypeCategory::Int: return CanonicalBuiltinKind::Int;
	case TypeCategory::UnsignedInt: return CanonicalBuiltinKind::UnsignedInt;
	case TypeCategory::Long: return CanonicalBuiltinKind::Long;
	case TypeCategory::UnsignedLong: return CanonicalBuiltinKind::UnsignedLong;
	case TypeCategory::LongLong: return CanonicalBuiltinKind::LongLong;
	case TypeCategory::UnsignedLongLong: return CanonicalBuiltinKind::UnsignedLongLong;
	case TypeCategory::Float: return CanonicalBuiltinKind::Float;
	case TypeCategory::Double: return CanonicalBuiltinKind::Double;
	case TypeCategory::LongDouble: return CanonicalBuiltinKind::LongDouble;
	default: return std::nullopt;
	}
}

// [meta.unary.prop] signedness is a property of the canonical builtin, so it is
// answered from the canonical identity and never from the category the builtin
// projects onto. Plain `char` is signed on this target and the wide character
// type follows the data model; the floating-point types are signed too, which an
// integer-only reading of the question would miss.
inline bool canonicalBuiltinIsSigned(CanonicalBuiltinKind builtin) {
	switch (builtin) {
	case CanonicalBuiltinKind::Char:
	case CanonicalBuiltinKind::SignedChar:
	case CanonicalBuiltinKind::Short:
	case CanonicalBuiltinKind::Int:
	case CanonicalBuiltinKind::Long:
	case CanonicalBuiltinKind::LongLong:
	case CanonicalBuiltinKind::Float:
	case CanonicalBuiltinKind::Double:
	case CanonicalBuiltinKind::LongDouble:
		return true;
	case CanonicalBuiltinKind::WChar:
		return g_target_data_model != TargetDataModel::LLP64;
	default:
		return false;
	}
}

inline bool canonicalBuiltinIsUnsigned(CanonicalBuiltinKind builtin) {
	switch (builtin) {
	case CanonicalBuiltinKind::Bool:
	case CanonicalBuiltinKind::UnsignedChar:
	case CanonicalBuiltinKind::UnsignedShort:
	case CanonicalBuiltinKind::UnsignedInt:
	case CanonicalBuiltinKind::UnsignedLong:
	case CanonicalBuiltinKind::UnsignedLongLong:
		return true;
	case CanonicalBuiltinKind::WChar:
		return g_target_data_model == TargetDataModel::LLP64;
	default:
		return false;
	}
}

enum class CanonicalTypeImportStatus : uint8_t {
	Supported, UnmigratedArray, UnmigratedCallable, UnmigratedNominal, Unresolved, Invalid,
};

struct CanonicalTypeImport {
	TypeId type;
	CanonicalTypeImportStatus status;
};

static_assert(sizeof(CanonicalTypeImport) == 8);

CanonicalRecordFacts computeCanonicalRecordFacts(
	CanonicalTypeTable& table, const StructTypeInfo& struct_info);

struct CanonicalDeclaratorExport {
	TypeId base;
	std::vector<DeclaratorComponent> components;
	CanonicalTypeImportStatus status;
};

inline CanonicalDeclaratorExport exportCanonicalDeclarator(const CanonicalTypeTable& table, TypeId type);

enum class CanonicalTypeImportContext : uint8_t {
	Exact, FunctionParameter,
};

// Alias cv applies to the aliased type as a whole. For a pointer alias it
// qualifies its outer pointer; otherwise it carries through to the next link.
inline void appendOrderedAliasPointerLevels(
	std::vector<DeclaratorComponent>& components,
	std::span<const PointerLevel> pointer_levels,
	CVQualifier alias_base_cv,
	CVQualifier& pending_cv) {
	for (size_t index = pointer_levels.size(); index-- > 0;) {
		CVQualifier pointer_cv = pointer_levels[index].cv_qualifier;
		if (index == pointer_levels.size() - 1) {
			pointer_cv |= pending_cv;
		}
		components.push_back(DeclaratorComponent::pointer(pointer_cv));
	}
	pending_cv = pointer_levels.empty()
		? pending_cv | alias_base_cv
		: alias_base_cv;
}

// Cv written on an alias sinks to its first pointer or member pointer.
// Arrays let it through. Cv written on a reference alias is ignored, and the
// alias's own cv stays with the referred type.
inline void appendOrderedAliasDeclaratorShape(std::vector<DeclaratorComponent>& components, const TypeSpecifierNode& alias,
	CVQualifier& pending_cv) {
	if (alias.has_ordered_declarator()) {
		bool applied_cv = false;
		for (DeclaratorComponent component : alias.declarator_components()) {
			const bool is_pointer_like = component.kind == DeclaratorComponentKind::Pointer ||
				component.kind == DeclaratorComponentKind::MemberObjectPointer || component.kind == DeclaratorComponentKind::MemberFunctionPointer;
			const bool is_reference = component.kind == DeclaratorComponentKind::LValueReference ||
				component.kind == DeclaratorComponentKind::RValueReference;
			if (!applied_cv && is_pointer_like) {
				component.cv_qualifier |= pending_cv;
				applied_cv = true;
			} else if (!applied_cv && is_reference) {
				applied_cv = true;
				pending_cv = CVQualifier::None;
			}
			components.push_back(component);
		}
		pending_cv = applied_cv ? alias.cv_qualifier() : static_cast<CVQualifier>(pending_cv | alias.cv_qualifier());
		return;
	}
	if (alias.reference_qualifier() == ReferenceQualifier::LValueReference) {
		components.push_back(DeclaratorComponent::lvalueReference());
		pending_cv = CVQualifier::None;
	} else if (alias.reference_qualifier() == ReferenceQualifier::RValueReference) {
		components.push_back(DeclaratorComponent::rvalueReference());
		pending_cv = CVQualifier::None;
	}
	auto append_arrays = [&]() {
		if (alias.array_dimensions().empty()) {
			if (alias.is_array() || alias.has_pointee_array_declarator() || alias.has_unsized_outer_array_dimension()) {
				components.push_back(DeclaratorComponent::unknownBoundArray());
			}
			return;
		}
		for (const size_t extent : alias.array_dimensions()) {
			components.push_back(extent == 0 ? DeclaratorComponent::unknownBoundArray() : DeclaratorComponent::array(extent));
		}
	};
	auto append_pointers = [&]() {
		if (alias.has_member_class() && alias.pointer_depth() != 0) {
			TypeSpecifierNode member_pointer_type = alias;
			tryBindPublishedMemberClassEntity(member_pointer_type);
			CVQualifier member_cv = member_pointer_type.pointer_levels().empty() ?
				CVQualifier::None : member_pointer_type.pointer_levels().front().cv_qualifier;
			member_cv |= pending_cv;
			const bool is_function = member_pointer_type.category() == TypeCategory::MemberFunctionPointer;
			if (member_pointer_type.has_member_class_type_id()) {
				components.push_back(DeclaratorComponent::memberPointer(member_pointer_type.member_class_type_id(), is_function, member_cv));
			} else {
				components.push_back(DeclaratorComponent::memberPointer(EntityId{}, is_function, member_cv));
			}
			pending_cv = alias.cv_qualifier();
			return;
		}
		appendOrderedAliasPointerLevels(components, alias.pointer_levels(), alias.cv_qualifier(), pending_cv);
	};
	if (alias.has_pointee_array_declarator()) {
		append_pointers();
		append_arrays();
	} else {
		if (alias.is_array() || alias.has_unsized_outer_array_dimension()) {
			append_arrays();
		}
		append_pointers();
	}
}

inline TypeId addCanonicalPointerLevels(CanonicalTypeTable& table, TypeId id,
	std::span<const PointerLevel> pointers) {
	for (const auto& pointer : pointers) {
		id = table.qualify(table.pointer(id), pointer.cv_qualifier);
	}
	return id;
}

inline TypeId addCanonicalArrayDimensions(CanonicalTypeTable& table, TypeId id,
	std::span<const size_t> dimensions, size_t first_dimension) {
	for (size_t index = dimensions.size(); index-- > first_dimension;) {
		id = table.array(id, dimensions[index]);
	}
	return id;
}

inline CanonicalTypeImport importCanonicalFunctionSignature(
	CanonicalTypeTable& table,
	const FunctionSignature& signature,
	TypeId return_type_override);

inline CanonicalTypeImport importCanonicalTemplateSpecialization(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	CanonicalTypeImportContext context);

inline CanonicalTypeImport importCanonicalMemberPointerFunctionSignature(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	const FunctionSignature& signature);

inline CanonicalTypeImport importCanonicalFunctionTypeComponent(
	CanonicalTypeTable& table,
	const FunctionType& type,
	CanonicalTypeImportContext context);

inline CanonicalTypeImport importCanonicalFunctionComponentFromProjection(
	CanonicalTypeTable& table,
	TypeIndex type_index,
	int pointer_depth,
	ReferenceQualifier reference_qualifier,
	CanonicalTypeImportContext context);

inline CVQualifier functionSignatureCV(const FunctionSignature& signature);

inline CanonicalCallingConvention toCanonicalCallingConvention(CallingConvention convention);

inline CanonicalDllLinkage toCanonicalDllLinkage(Linkage linkage);

// The cold signature is the outermost callable. A nested Function component's
// signature is the `callable_signature` of the enclosing return FunctionType,
// so descend through every preceding wrapper before selecting it.
inline const FunctionSignature* orderedDeclaratorCallableSignature(const TypeSpecifierNode& syntax,
	std::span<const DeclaratorComponent> components, size_t component_index) {
	if (!syntax.has_function_signature()) {
		return nullptr;
	}
	const FunctionSignature* current = &syntax.function_signature();
	for (size_t index = 0; index < component_index; ++index) {
		if (components[index].kind != DeclaratorComponentKind::Function) {
			continue;
		}
		current = current != nullptr && current->hasStructuredTypes() ? current->return_type().callable_signature.get() : nullptr;
	}
	return current;
}

inline CanonicalTypeImport applyCanonicalOrderedDeclarator(
	CanonicalTypeTable& table,
	TypeId base,
	const TypeSpecifierNode& syntax,
	CanonicalTypeImportContext context) {
	TypeId id = base;
	const std::span<const DeclaratorComponent> components =
		syntax.declarator_components();
	auto arrayElementStatus = [&table](TypeId element) {
		const CanonicalTypeNode node = table.node(
			table.withoutTopLevelQualifiers(element));
		if (node.kind == CanonicalTypeKind::Function) {
			return CanonicalTypeImportStatus::UnmigratedCallable;
		}
		if (node.kind == CanonicalTypeKind::Array &&
			node.flags != CanonicalTypeNodeFlags::KnownArrayBound) {
			return CanonicalTypeImportStatus::UnmigratedArray;
		}
		if (node.kind == CanonicalTypeKind::LValueReference ||
			node.kind == CanonicalTypeKind::RValueReference ||
			(node.kind == CanonicalTypeKind::Builtin &&
				node.builtin == CanonicalBuiltinKind::Void)) {
			return CanonicalTypeImportStatus::Invalid;
		}
		return CanonicalTypeImportStatus::Supported;
	};
	for (size_t index = components.size(); index-- > 0;) {
		const DeclaratorComponent& component = components[index];
		switch (component.kind) {
		case DeclaratorComponentKind::Pointer:
			if (!isValidCVQualifier(component.cv_qualifier)) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			id = table.qualify(table.pointer(id), component.cv_qualifier);
			break;
		case DeclaratorComponentKind::LValueReference:
			id = table.reference(id, ReferenceQualifier::LValueReference);
			break;
		case DeclaratorComponentKind::RValueReference:
			id = table.reference(id, ReferenceQualifier::RValueReference);
			break;
		case DeclaratorComponentKind::Array:
			if (component.payload == 0 ||
				component.payload > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			if (const CanonicalTypeImportStatus status = arrayElementStatus(id);
				status != CanonicalTypeImportStatus::Supported) {
				return {{}, status};
			}
			id = table.array(id, static_cast<size_t>(component.payload));
			break;
		case DeclaratorComponentKind::UnknownBoundArray:
			if (const CanonicalTypeImportStatus status = arrayElementStatus(id);
				status != CanonicalTypeImportStatus::Supported) {
				return {{}, status};
			}
			id = table.arrayOfUnknownBound(id);
			break;
		case DeclaratorComponentKind::Function: {
			// The components inside this wrapper are the return type (`id` has
			// already been built from them); parameters come from this
			// component's signature. A nested callable return has more than one
			// Function component: the node's cold signature is the outermost
			// callable and each inner callable's signature is the
			// `callable_signature` of the enclosing return FunctionType.
			const FunctionSignature* signature_ptr = orderedDeclaratorCallableSignature(syntax, components, index);
			if (signature_ptr == nullptr) {
				return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
			}
			const FunctionSignature& signature = *signature_ptr;
			if (signature.class_name.isValid() ||
				(signature.noexcept_expression.has_value() && !signature.dependent_noexcept) ||
				(signature.is_noexcept && signature.dependent_noexcept)) {
				return {{}, signature.class_name.isValid()
					? CanonicalTypeImportStatus::UnmigratedCallable
					: CanonicalTypeImportStatus::Unresolved};
			}
			std::vector<TypeId> parameters;
			if (signature.hasStructuredTypes()) {
				const std::span<const FunctionType> structured_parameters =
					signature.parameter_types();
				parameters.reserve(structured_parameters.size());
				for (const FunctionType& parameter : structured_parameters) {
					const CanonicalTypeImport imported_parameter =
						importCanonicalFunctionTypeComponent(
							table, parameter, CanonicalTypeImportContext::FunctionParameter);
					if (imported_parameter.status != CanonicalTypeImportStatus::Supported) {
						return imported_parameter;
					}
					TypeId parameter_type = imported_parameter.type;
					if (table.node(parameter_type).kind == CanonicalTypeKind::Function) {
						parameter_type = table.pointer(parameter_type);
					}
					parameters.push_back(parameter_type);
				}
			} else {
				parameters.reserve(signature.parameter_type_indices.size());
				for (const TypeIndex parameter_type_index : signature.parameter_type_indices) {
					const CanonicalTypeImport imported_parameter =
						importCanonicalFunctionComponentFromProjection(
							table,
							parameter_type_index,
							0,
							ReferenceQualifier::None,
							CanonicalTypeImportContext::FunctionParameter);
					if (imported_parameter.status != CanonicalTypeImportStatus::Supported) {
						return imported_parameter;
					}
					TypeId parameter_type = imported_parameter.type;
					if (table.node(parameter_type).kind == CanonicalTypeKind::Function) {
						parameter_type = table.pointer(parameter_type);
					}
					parameters.push_back(parameter_type);
				}
			}
			const CanonicalTypeNode return_node = table.node(id);
			if (return_node.kind == CanonicalTypeKind::Function ||
				return_node.kind == CanonicalTypeKind::Array) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			id = table.function(
				id,
				parameters,
				signature.is_variadic,
				functionSignatureCV(signature),
				signature.function_reference_qualifier,
				signature.is_noexcept,
				toCanonicalCallingConvention(signature.calling_convention),
				toCanonicalDllLinkage(signature.linkage),
				signature.dependent_noexcept);
			break;
		}
		case DeclaratorComponentKind::MemberObjectPointer:
			if (index != components.size() - 1 || !component.hasMemberOwner() ||
				!isValidCVQualifier(component.cv_qualifier)) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			{
				const TypeId owner = component.memberOwnerType()
					? component.memberOwnerType()
					: table.record(component.memberOwnerEntity());
				id = table.qualify(
					table.memberObjectPointer(owner, id),
					component.cv_qualifier);
			}
			break;
		case DeclaratorComponentKind::MemberFunctionPointer: {
			const FunctionSignature* signature_ptr = orderedDeclaratorCallableSignature(syntax, components, index);
			if (!component.hasMemberOwner() ||
				!isValidCVQualifier(component.cv_qualifier) ||
				signature_ptr == nullptr) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			// Components at higher indices were already folded into `id`, so
			// they are the callable's return type. `id` therefore carries the
			// complete return shape and overrides the flat signature
			// projection, which cannot represent an interleaved return (for
			// example `int* (Owner::*)()`).
			FunctionSignature signature = *signature_ptr;
			signature.class_name = {};
			const CanonicalTypeImport imported_function =
				importCanonicalFunctionSignature(table, signature, id);
			if (imported_function.status != CanonicalTypeImportStatus::Supported) {
				return imported_function;
			}
			const TypeId owner = component.memberOwnerType()
				? component.memberOwnerType()
				: table.record(component.memberOwnerEntity());
			id = table.qualify(
				table.memberFunctionPointer(owner, imported_function.type),
				component.cv_qualifier);
			break;
		}
		}
	}
	if (context == CanonicalTypeImportContext::FunctionParameter &&
		!components.empty()) {
		const CanonicalTypeNode outer = table.node(id);
		if (outer.kind == CanonicalTypeKind::Array) {
			id = table.pointer(outer.child);
		} else if (outer.kind == CanonicalTypeKind::Function) {
			id = table.pointer(id);
		}
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

// Shared pointer/array/reference shaping after a base TypeId and top-level cv
// have been formed. Keeps parameter-array decay in one mutation-tested place.
inline TypeId applyCanonicalPointerArrayReference(CanonicalTypeTable& table, TypeId id,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context,
	bool has_ordinary_array, bool has_pointee_array) {
	if (syntax.has_ordered_declarator()) {
		const CanonicalTypeImport imported =
			applyCanonicalOrderedDeclarator(table, id, syntax, context);
		if (imported.status != CanonicalTypeImportStatus::Supported) {
			throw InternalError("canonical type adapter: ordered declarator was not importable");
		}
		return imported.type;
	}
	const auto reference = syntax.reference_qualifier();
	if (has_pointee_array) {
		// T (*)[N] is pointer to array of N T. T (*)[][N] adds an unknown
		// outer bound, so the unknown wrap goes outside the known dimensions.
		if (!syntax.array_dimensions().empty()) {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
		}
		if (syntax.array_dimensions().empty() || syntax.has_unsized_outer_array_dimension()) {
			id = table.arrayOfUnknownBound(id);
		}
		id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	} else {
		id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
		if (has_ordinary_array && context == CanonicalTypeImportContext::FunctionParameter &&
			reference == ReferenceQualifier::None) {
			const size_t first_inner_dimension = syntax.has_unsized_outer_array_dimension() ? 0 : 1;
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), first_inner_dimension);
			id = table.pointer(id);
		} else if (has_ordinary_array) {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
			if (syntax.has_unsized_outer_array_dimension()) {
				id = table.arrayOfUnknownBound(id);
			}
		}
	}
	if (reference != ReferenceQualifier::None) {
		id = table.reference(id, reference);
	}
	return id;
}

// Recover the base independently of compatibility pointer/callable categories.
// Specialization/dependent bases retain their existing TypeIndex bridge until
// those families have a reverse nominal materializer.
inline void applyCanonicalDeclaratorBase(TypeSpecifierNode& type, const CanonicalTypeTable& table, TypeId base) {
	TypeIndex compatibility_index = type.type_index();
	CVQualifier cv = CVQualifier::None;
	CanonicalTypeNode node = table.node(base);
	while (node.kind == CanonicalTypeKind::Qualified) {
		cv |= node.qualifiers;
		base = node.child;
		node = table.node(base);
	}
	EntityId entity;
	if (node.kind == CanonicalTypeKind::Builtin) {
		const std::optional<TypeCategory> category = canonicalBuiltinToTypeCategory(node.builtin);
		if (!category.has_value()) {
			throw InternalError("canonical declarator has an invalid builtin base");
		}
		compatibility_index = nativeTypeIndex(*category);
	} else if (node.kind == CanonicalTypeKind::Record || node.kind == CanonicalTypeKind::Enum) {
		entity = node.kind == CanonicalTypeKind::Record ? table.recordEntity(base) : table.enumEntity(base);
		const TypeInfo* info = tryFindTypeInfoByEntityId(entity);
		if (info == nullptr) {
			throw InternalError("canonical declarator nominal base has no TypeInfo");
		}
		const TypeCategory category = node.kind == CanonicalTypeKind::Record ? TypeCategory::Struct : TypeCategory::Enum;
		compatibility_index = info->registeredTypeIndex().withCategory(category);
	}
	type.set_type_index(compatibility_index);
	type.set_cv_qualifier(cv);
	type.set_type_entity(entity);
}

inline TypeSpecifierNode typeSpecifierFromFunctionType(const FunctionType& type) {
	TypeSpecifierNode spec(type.type_index, TypeQualifier::None, 0, Token{}, type.cv_qualifier);
	if (type.type_entity) {
		spec.set_type_entity(type.type_entity);
	}
	spec.set_reference_qualifier(type.reference_qualifier);
	for (const CVQualifier pointer_cv : type.pointer_qualifiers) {
		spec.add_pointer_level(pointer_cv);
	}
	if (type.pointee_array_declarator) {
		spec.set_pointee_array_declarator(true);
		spec.set_pointee_array_dimensions(type.array_dimensions);
	} else if (!type.array_dimensions.empty()) {
		spec.set_array_dimensions(type.array_dimensions);
	}
	if (!type.ordered_declarator_components.empty()) {
		spec.set_ordered_declarator(type.ordered_declarator_components);
	}
	if (type.has_unsized_outer_array_dimension) {
		spec.set_unsized_outer_array_dimension(true);
	}
	if (type.is_pack_expansion) {
		spec.set_pack_expansion(true);
	}
	if (type.canonical_type_id && type.ordered_declarator_components.empty()) {
		const CanonicalDeclaratorExport exported = exportCanonicalDeclarator(requireFrontendContext().canonicalTypes(), type.canonical_type_id);
		const bool has_non_projectable_component = std::ranges::any_of(
			exported.components,
			[](const DeclaratorComponent& component) {
				return component.kind == DeclaratorComponentKind::Function || component.kind == DeclaratorComponentKind::MemberObjectPointer ||
					component.kind == DeclaratorComponentKind::MemberFunctionPointer;
			});
		if (exported.status == CanonicalTypeImportStatus::Supported && !has_non_projectable_component && !exported.components.empty()) {
			spec.set_ordered_declarator(exported.components);
		}
	}
	if (type.template_parameter_name.isValid()) {
		spec.set_template_parameter_identity(type.template_parameter_name);
	}
	if (type.template_parameter_decl) {
		spec.set_template_parameter_decl(type.template_parameter_decl, type.template_parameter_index);
	}
	if (type.injected_class_declaration != nullptr) {
		spec.set_injected_class_declaration(type.injected_class_declaration);
	}
	if (type.member_class_name.isValid()) {
		spec.set_member_class_name(type.member_class_name);
	}
	if (type.callable_signature) {
		spec.set_function_signature(*type.callable_signature);
	}
	return spec;
}

inline CanonicalTypeImport importCanonicalTypeImpl(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context);

inline CanonicalTypeImport importCanonicalClassTypeInfo(
	CanonicalTypeTable& table,
	const TypeInfo& type_info);

inline CanonicalTypeImport importCanonicalShapedBase(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context, TypeId id);

// A specifier rebuilt from a TypeIndex or FunctionType has no entity stamp.
// When the direct import is UnmigratedNominal, recover the published class and
// reapply this specifier's pointer, cv, array, and parameter adjustment. The
// semantic layer stamps identity onto structured signature components, so this
// stays a fallback for callers that still publish a bare TypeIndex.
inline CanonicalTypeImport importCanonicalNominalComponent(CanonicalTypeTable& table, const TypeSpecifierNode& syntax,
	CanonicalTypeImportContext context) {
	const CanonicalTypeImport imported = importCanonicalTypeImpl(table, syntax, context);
	if (imported.status != CanonicalTypeImportStatus::UnmigratedNominal || !syntax.type_index().is_valid()) {
		return imported;
	}
	const TypeInfo* type_info = tryGetTypeInfo(syntax.type_index());
	if (type_info == nullptr) {
		return imported;
	}
	const CanonicalTypeImport imported_class = importCanonicalClassTypeInfo(table, *type_info);
	if (imported_class.status != CanonicalTypeImportStatus::Supported) {
		return imported_class;
	}
	return importCanonicalShapedBase(table, syntax, context, imported_class.type);
}

inline CanonicalTypeImport importCanonicalFunctionTypeComponent(CanonicalTypeTable& table,
	const FunctionType& type, CanonicalTypeImportContext context) {
	if (type.canonical_type_id) {
		return {type.canonical_type_id, CanonicalTypeImportStatus::Supported};
	}
	return importCanonicalTypeImpl(table, typeSpecifierFromFunctionType(type), context);
}

inline CVQualifier functionSignatureCV(const FunctionSignature& signature) {
	CVQualifier cv = CVQualifier::None;
	if (signature.is_const) {
		cv |= CVQualifier::Const;
	}
	if (signature.is_volatile) {
		cv |= CVQualifier::Volatile;
	}
	return cv;
}

inline EntityId resolveMemberClassEntity(const TypeSpecifierNode& syntax) {
	if (syntax.has_member_class_entity()) {
		return syntax.member_class_entity();
	}
	if (syntax.has_injected_class_declaration() &&
		syntax.injected_class_declaration()->has_entity_id()) {
		return syntax.injected_class_declaration()->entity_id();
	}
	return {};
}

inline EntityId resolveNamedTypeEntity(const TypeSpecifierNode& syntax) {
	if (syntax.has_type_entity()) {
		return syntax.type_entity();
	}
	if (syntax.has_injected_class_declaration() &&
		syntax.injected_class_declaration()->has_entity_id()) {
		return syntax.injected_class_declaration()->entity_id();
	}
	return {};
}

// Opaque Record import for published class/struct types. Alias and unpublished
// nominal forms stay deferred until their EntityId path lands.
inline CanonicalTypeImport importCanonicalRecord(CanonicalTypeTable& table, const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	// An instantiated nested declaration shares its pattern EntityId across
	// enclosing specializations. The class-source importer must recover its owner.
	if (syntax.has_injected_class_declaration()) {
		const StructDeclarationNode* pattern = syntax.injected_class_declaration()->injected_class_pattern_declaration();
		if (pattern != nullptr && pattern->has_entity_id() && !pattern->has_template_decl_id()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
		}
	}
	const EntityId entity = resolveNamedTypeEntity(syntax);
	if (!entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	auto id = table.record(entity);
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.has_ordered_declarator()) {
		return applyCanonicalOrderedDeclarator(
			table, id, syntax, context);
	}
	id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

inline CanonicalTypeImport importCanonicalEnum(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	CanonicalTypeImportContext context) {
	const EntityId entity = resolveNamedTypeEntity(syntax);
	if (!entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	auto id = table.enumeration(entity);
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.has_ordered_declarator()) {
		return applyCanonicalOrderedDeclarator(
			table, id, syntax, context);
	}
	id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

inline CanonicalTypeImport importCanonicalCompleteNominalArray(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	const EntityId entity = resolveNamedTypeEntity(syntax);
	if (!entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	const bool is_record = syntax.category() == TypeCategory::Struct;
	if ((is_record && !table.hasRecordLayout(entity)) ||
		(!is_record && !table.hasEnumLayout(entity))) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
	const bool has_pointee_array = syntax.has_pointee_array_declarator();
	if (!has_ordinary_array && !has_pointee_array) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	if (has_pointee_array && syntax.pointer_levels().empty()) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	if (has_ordinary_array && syntax.array_dimensions().empty() && !syntax.has_unsized_outer_array_dimension()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	for (const size_t extent : syntax.array_dimensions()) {
		if (extent == 0) {
			return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
		}
	}
	CanonicalTypeTransaction transaction(table);
	auto id = is_record ? table.record(entity) : table.enumeration(entity);
	id = table.qualify(id, syntax.cv_qualifier());
	// Share the builtin/dependent array shaping so an unknown outer bound is
	// represented for nominal elements exactly as for builtin ones.
	id = applyCanonicalPointerArrayReference(table, id, syntax, context, has_ordinary_array, has_pointee_array);
	transaction.commit();
	return {id, CanonicalTypeImportStatus::Supported};
}

inline CanonicalCallingConvention toCanonicalCallingConvention(CallingConvention convention) {
	switch (convention) {
	case CallingConvention::Default:
		return CanonicalCallingConvention::Default;
	case CallingConvention::Cdecl:
		return CanonicalCallingConvention::Cdecl;
	case CallingConvention::Stdcall:
		return CanonicalCallingConvention::Stdcall;
	case CallingConvention::Fastcall:
		return CanonicalCallingConvention::Fastcall;
	case CallingConvention::Vectorcall:
		return CanonicalCallingConvention::Vectorcall;
	case CallingConvention::Thiscall:
		return CanonicalCallingConvention::Thiscall;
	case CallingConvention::Clrcall:
		return CanonicalCallingConvention::Clrcall;
	}
	throw InternalError("canonical type adapter: unknown calling convention");
}

inline CanonicalDllLinkage toCanonicalDllLinkage(Linkage linkage) {
	switch (linkage) {
	case Linkage::DllImport:
		return CanonicalDllLinkage::Import;
	case Linkage::DllExport:
		return CanonicalDllLinkage::Export;
	case Linkage::None:
	case Linkage::C:
	case Linkage::CPlusPlus:
		return CanonicalDllLinkage::None;
	}
	throw InternalError("canonical type adapter: unknown linkage");
}

inline TypeSpecifierNode typeSpecifierFromTypeIndexProjection(
	TypeIndex type_index,
	int pointer_depth,
	ReferenceQualifier reference_qualifier) {
	TypeSpecifierNode spec(type_index, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	spec.set_reference_qualifier(reference_qualifier);
	if (pointer_depth > 0) {
		spec.add_pointer_levels(pointer_depth);
	}
	return spec;
}

inline CanonicalTypeImport importCanonicalFunctionComponentFromProjection(
	CanonicalTypeTable& table,
	TypeIndex type_index,
	int pointer_depth,
	ReferenceQualifier reference_qualifier,
	CanonicalTypeImportContext context) {
	if (type_index.category() == TypeCategory::Invalid) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	return importCanonicalTypeImpl(
		table,
		typeSpecifierFromTypeIndexProjection(type_index, pointer_depth, reference_qualifier),
		context);
}

inline CanonicalTypeImport importCanonicalFunctionSignature(
	CanonicalTypeTable& table,
	const FunctionSignature& signature,
	TypeId return_type_override) {
	// Retained noexcept(expr) needs a published ExprId before canonical import.
	if (signature.noexcept_expression.has_value() && !signature.dependent_noexcept) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	if (signature.is_noexcept && signature.dependent_noexcept) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}

	// Structured return uses FunctionType; otherwise recover from the flat
	// TypeIndex projection that older signature writers still publish.
	const CanonicalTypeImport imported_return = return_type_override
		? CanonicalTypeImport{
			return_type_override,
			CanonicalTypeImportStatus::Supported}
		: signature.hasStructuredTypes()
		? importCanonicalFunctionTypeComponent(
			table, signature.return_type(), CanonicalTypeImportContext::Exact)
		: importCanonicalFunctionComponentFromProjection(
			table,
			signature.return_type_index,
			signature.return_pointer_depth,
			signature.return_reference_qualifier,
			CanonicalTypeImportContext::Exact);
	if (imported_return.status != CanonicalTypeImportStatus::Supported) {
		return imported_return;
	}

	std::vector<TypeId> parameters;
	auto appendFunctionParameter = [&table, &parameters](TypeId parameter_type) {
		const CanonicalTypeNode parameter = table.node(parameter_type);
		if (parameter.kind == CanonicalTypeKind::Array) {
			parameter_type = table.pointer(parameter.child);
		} else if (parameter.kind == CanonicalTypeKind::Function) {
			parameter_type = table.pointer(parameter_type);
		}
		// [dcl.fct]/5 removes top-level cv after array and function adjustment.
		parameters.push_back(table.withoutTopLevelQualifiers(parameter_type));
	};
	const std::span<const FunctionType> structured_parameters = signature.parameter_types();
	if (!structured_parameters.empty()) {
		parameters.reserve(structured_parameters.size());
		for (const FunctionType& parameter : structured_parameters) {
			const auto imported_parameter = importCanonicalFunctionTypeComponent(
				table, parameter, CanonicalTypeImportContext::FunctionParameter);
			if (imported_parameter.status != CanonicalTypeImportStatus::Supported) {
				return imported_parameter;
			}
			appendFunctionParameter(imported_parameter.type);
		}
	} else {
		parameters.reserve(signature.parameter_type_indices.size());
		for (const TypeIndex parameter_type_index : signature.parameter_type_indices) {
			const auto imported_parameter = importCanonicalFunctionComponentFromProjection(
				table,
				parameter_type_index,
				0,
				ReferenceQualifier::None,
				CanonicalTypeImportContext::FunctionParameter);
			if (imported_parameter.status != CanonicalTypeImportStatus::Supported) {
				return imported_parameter;
			}
			appendFunctionParameter(imported_parameter.type);
		}
	}

	return {
		table.function(
			imported_return.type,
			parameters,
			signature.is_variadic,
			functionSignatureCV(signature),
			signature.function_reference_qualifier,
			signature.is_noexcept,
			toCanonicalCallingConvention(signature.calling_convention),
			toCanonicalDllLinkage(signature.linkage),
			signature.dependent_noexcept),
		CanonicalTypeImportStatus::Supported};
}

inline CanonicalTypeImport importCanonicalMemberPointerReturnBase(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	const FunctionSignature& signature,
	bool strip_return_declarator) {
	// Ordered member-pointer spines carry the callable's return declarator as
	// components, so only the return base identity is needed here. Preserve a
	// template-specialization owner, otherwise recover the structured return
	// component or its flat projection. When the spine already carries the
	// return declarator, the signature return keeps a compatibility shape that
	// must not be applied a second time.
	if (syntax.has_template_specialization()) {
		TypeSpecifierNode return_base = syntax;
		return_base.clear_declarator_shape();
		return_base.clear_function_signature();
		return_base.clear_member_class_identity();
		return_base.set_cv_qualifier(CVQualifier::None);
		return importCanonicalTemplateSpecialization(
			table,
			return_base,
			CanonicalTypeImportContext::Exact);
	}
	if (signature.hasStructuredTypes()) {
		if (!strip_return_declarator) {
			return importCanonicalFunctionTypeComponent(
				table,
				signature.return_type(),
				CanonicalTypeImportContext::Exact);
		}
		TypeSpecifierNode return_base =
			typeSpecifierFromFunctionType(signature.return_type());
		return_base.clear_declarator_shape();
		return importCanonicalTypeImpl(
			table, return_base, CanonicalTypeImportContext::Exact);
	}
	return importCanonicalFunctionComponentFromProjection(
		table,
		signature.return_type_index,
		strip_return_declarator ? 0 : signature.return_pointer_depth,
		strip_return_declarator
			? ReferenceQualifier::None
			: signature.return_reference_qualifier,
		CanonicalTypeImportContext::Exact);
}

inline CanonicalTypeImport importCanonicalMemberPointerFunctionSignature(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	const FunctionSignature& signature) {
	TypeId return_type_override{};
	if (syntax.has_template_specialization()) {
		TypeSpecifierNode return_base = syntax;
		return_base.clear_declarator_shape();
		return_base.clear_function_signature();
		return_base.clear_member_class_identity();
		return_base.set_cv_qualifier(CVQualifier::None);
		const CanonicalTypeImport imported_return_base =
			importCanonicalTemplateSpecialization(
				table,
				return_base,
				CanonicalTypeImportContext::Exact);
		if (imported_return_base.status !=
			CanonicalTypeImportStatus::Supported) {
			return imported_return_base;
		}
		return_type_override = imported_return_base.type;
		if (signature.hasStructuredTypes()) {
			const TypeSpecifierNode return_shape =
				typeSpecifierFromFunctionType(signature.return_type());
			return_type_override = table.qualify(
				return_type_override,
				return_shape.cv_qualifier());
			const bool has_pointee_array =
				return_shape.has_pointee_array_declarator();
			const bool has_ordinary_array =
				return_shape.is_array() && !has_pointee_array;
			return_type_override = applyCanonicalPointerArrayReference(
				table,
				return_type_override,
				return_shape,
				CanonicalTypeImportContext::Exact,
				has_ordinary_array,
				has_pointee_array);
		}
	}
	return importCanonicalFunctionSignature(
		table,
		signature,
		return_type_override);
}

// Member pointers require a published class EntityId. Spelling-only owners stay
// UnmigratedCallable. MemberObjectPointer category that erased the pointee type
// also stays deferred.
inline CanonicalTypeImport importCanonicalMemberPointer(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	if (syntax.has_ordered_declarator()) {
		const std::span<const DeclaratorComponent> components =
			syntax.declarator_components();
		if (components.empty()) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		// A member function pointer may be followed by the callable's return
		// declarator (for example `int* (Owner::*)()`), so it is not always the
		// innermost component. Its return base still has to be imported before
		// the spine is applied.
		bool has_member_function_pointer = false;
		size_t member_function_pointer_index = components.size();
		for (size_t index = 0; index < components.size(); ++index) {
			if (components[index].kind ==
				DeclaratorComponentKind::MemberFunctionPointer) {
				has_member_function_pointer = true;
				member_function_pointer_index = index;
				break;
			}
		}
		if (has_member_function_pointer) {
			const FunctionSignature* signature_ptr = orderedDeclaratorCallableSignature(syntax, components, member_function_pointer_index);
			if (signature_ptr == nullptr) {
				return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
			}
			FunctionSignature signature = *signature_ptr;
			signature.class_name = {};
			const bool has_trailing_return_declarator =
				member_function_pointer_index + 1 < components.size();
			const CanonicalTypeImport imported_return_base =
				importCanonicalMemberPointerReturnBase(
					table,
					syntax,
					signature,
					has_trailing_return_declarator);
			if (imported_return_base.status !=
				CanonicalTypeImportStatus::Supported) {
				return imported_return_base;
			}
			return applyCanonicalOrderedDeclarator(
				table,
				imported_return_base.type,
				syntax,
				CanonicalTypeImportContext::Exact);
		}
		const DeclaratorComponent& innermost = components.back();
		if (innermost.kind != DeclaratorComponentKind::MemberObjectPointer) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		if (syntax.category() == TypeCategory::MemberObjectPointer &&
			!syntax.has_member_object_pointee()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
		}
		TypeSpecifierNode pointee = syntax.category() == TypeCategory::MemberObjectPointer
			? syntax.member_object_pointee()
			: syntax;
		pointee.clear_member_class_identity();
		pointee.clear_injected_class_declaration();
		pointee.clear_ordered_declarator();
		pointee.limit_pointer_depth(0);
		const CanonicalTypeImport imported_pointee = importCanonicalTypeImpl(
			table, pointee, CanonicalTypeImportContext::Exact);
		if (imported_pointee.status != CanonicalTypeImportStatus::Supported) {
			return imported_pointee;
		}
		return applyCanonicalOrderedDeclarator(table, imported_pointee.type, syntax,
			CanonicalTypeImportContext::Exact);
	}
	CVQualifier member_pointer_cv = syntax.cv_qualifier();
	if (!syntax.pointer_levels().empty()) {
		const CVQualifier outer_pointer_cv =
			syntax.pointer_levels().front().cv_qualifier;
		if (!isValidCVQualifier(outer_pointer_cv)) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		member_pointer_cv = outer_pointer_cv;
	}
	const TypeId owner = syntax.has_member_class_type_id()
		? syntax.member_class_type_id()
		: TypeId{};
	const EntityId owner_entity = resolveMemberClassEntity(syntax);
	if (!owner && !owner_entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	const TypeId canonical_owner = owner ? owner : table.record(owner_entity);
	if (syntax.category() == TypeCategory::MemberFunctionPointer ||
		(syntax.has_function_signature() && syntax.has_member_class())) {
		if (!syntax.has_function_signature()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
		}
		FunctionSignature signature = syntax.function_signature();
		signature.class_name = {};
		const auto imported_function =
			importCanonicalMemberPointerFunctionSignature(
				table,
				syntax,
				signature);
		if (imported_function.status != CanonicalTypeImportStatus::Supported) {
			return imported_function;
		}
		auto id = table.memberFunctionPointer(canonical_owner, imported_function.type);
		id = table.qualify(id, member_pointer_cv);
		if (syntax.reference_qualifier() != ReferenceQualifier::None) {
			id = table.reference(id, syntax.reference_qualifier());
		}
		return {id, CanonicalTypeImportStatus::Supported};
	}
	if (syntax.category() == TypeCategory::MemberObjectPointer) {
		// Cast/NTTP forms overwrite the flat pointee category. The parser keeps
		// the pre-rewrite pointee specifier as syntax; import it structurally and
		// fail closed when it is absent or does not import.
		if (!syntax.has_member_object_pointee()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
		}
		const CanonicalTypeImport imported_pointee = importCanonicalTypeImpl(
			table, syntax.member_object_pointee(), CanonicalTypeImportContext::Exact);
		if (imported_pointee.status != CanonicalTypeImportStatus::Supported) {
			return imported_pointee;
		}
		auto id = table.memberObjectPointer(canonical_owner, imported_pointee.type);
		id = table.qualify(id, member_pointer_cv);
		if (syntax.reference_qualifier() != ReferenceQualifier::None) {
			id = table.reference(id, syntax.reference_qualifier());
		}
		return {id, CanonicalTypeImportStatus::Supported};
	}
	if (!syntax.has_member_class()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	TypeSpecifierNode pointee = syntax;
	pointee.clear_member_class_identity();
	pointee.clear_injected_class_declaration();
	pointee.limit_pointer_depth(0);
	pointee.set_reference_qualifier(ReferenceQualifier::None);
	const auto imported_pointee = importCanonicalTypeImpl(
		table, pointee, CanonicalTypeImportContext::Exact);
	if (imported_pointee.status != CanonicalTypeImportStatus::Supported) {
		return imported_pointee;
	}
	auto id = table.memberObjectPointer(canonical_owner, imported_pointee.type);
	id = table.qualify(id, member_pointer_cv);
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

// Free-function and function-pointer shapes.
inline CanonicalTypeImport importCanonicalCallable(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	if (syntax.has_ordered_declarator()) {
		// A function component owns the signature on this specifier. If the
		// ordered declarator only wraps a callable alias (for example, a pointer
		// to an array of function pointers), the signature instead describes the
		// base and must be imported before applying the outer spine.
		bool has_function_component = false;
		for (const DeclaratorComponent& component : syntax.declarator_components()) {
			has_function_component = has_function_component ||
				component.kind == DeclaratorComponentKind::Function ||
				component.kind == DeclaratorComponentKind::MemberFunctionPointer;
		}
		TypeSpecifierNode base = syntax;
		base.clear_declarator_shape();
		if (has_function_component) {
			base.clear_function_signature();
		}
		const CanonicalTypeImport imported_base = importCanonicalTypeImpl(
			table, base, CanonicalTypeImportContext::Exact);
		if (imported_base.status != CanonicalTypeImportStatus::Supported) {
			return imported_base;
		}
		return applyCanonicalOrderedDeclarator(
			table, imported_base.type, syntax, context);
	}
	if (syntax.has_member_class() ||
		syntax.category() == TypeCategory::MemberFunctionPointer ||
		syntax.category() == TypeCategory::MemberObjectPointer) {
		return importCanonicalMemberPointer(table, syntax);
	}
	if (!syntax.has_function_signature()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	const FunctionSignature& signature = syntax.function_signature();
	if (signature.class_name.isValid()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	const auto imported_function = importCanonicalFunctionSignature(
		table,
		signature,
		TypeId{});
	if (imported_function.status != CanonicalTypeImportStatus::Supported) {
		return imported_function;
	}
	auto id = imported_function.type;
	if (syntax.category() == TypeCategory::FunctionPointer || !syntax.pointer_levels().empty()) {
		// FunctionPointer with empty pointer_levels is a single pointer-to-function
		// (int (*)(Args)): add one wrapper. Non-empty pointer_levels are the full
		// wrapper stack around the function, including nested forms such as
		// int (**)(Args) encoded as FunctionPointer with two levels, so do not
		// also add a category wrap (legacy decltype aliases may carry a
		// redundant level that would otherwise become depth 2).
		if (syntax.pointer_levels().empty()) {
			id = table.pointer(id);
		} else {
			id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
		}
	}
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	// A projectable flat callable shape can carry array bounds around the
	// callable element: `int (*a[2])(int)` is an array of two function
	// pointers. The cold signature shaped the element and any pointer wrappers;
	// wrap the recorded bounds around it ([dcl.array]).
	if (syntax.is_array() && !syntax.has_pointee_array_declarator()) {
		if (!syntax.array_dimensions().empty()) {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
		}
		if (syntax.has_unsized_outer_array_dimension()) {
			id = table.arrayOfUnknownBound(id);
		}
	}
	// [dcl.fct]/5 parameter adjustment: a function type or an array type becomes
	// a pointer to its element/function.
	if (context == CanonicalTypeImportContext::FunctionParameter) {
		const CanonicalTypeNode outer = table.node(table.withoutTopLevelQualifiers(id));
		if (outer.kind == CanonicalTypeKind::Array) {
			id = table.pointer(outer.child);
		} else if (outer.kind == CanonicalTypeKind::Function) {
			id = table.pointer(id);
		}
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

// Shared validation and shaping for opaque canonical bases. Callers own the
// surrounding transaction so a deferred declarator retains no new wrappers.
inline CanonicalTypeImport importCanonicalShapedBase(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context, TypeId id) {
	const auto reference = syntax.reference_qualifier();
	if (!isValidCVQualifier(syntax.cv_qualifier()) ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference)) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (!isValidCVQualifier(pointer.cv_qualifier)) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
	}
	const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
	const bool has_pointee_array = syntax.has_pointee_array_declarator();
	const bool has_array_shape = has_ordinary_array || has_pointee_array ||
		!syntax.array_dimensions().empty() || syntax.has_unsized_outer_array_dimension();
	if (has_array_shape) {
		if ((!has_ordinary_array && !has_pointee_array) ||
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array && !has_pointee_array) ||
			(has_pointee_array && syntax.pointer_levels().empty())) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		if (has_ordinary_array && syntax.array_dimensions().empty() &&
			!syntax.has_unsized_outer_array_dimension()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedArray};
		}
		for (const size_t extent : syntax.array_dimensions()) {
			if (extent == 0) {
				return {{}, CanonicalTypeImportStatus::UnmigratedArray};
			}
		}
	}
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.has_ordered_declarator()) {
		return applyCanonicalOrderedDeclarator(table, id, syntax, context);
	}
	id = applyCanonicalPointerArrayReference(
		table, id, syntax, context, has_ordinary_array, has_pointee_array);
	return {id, CanonicalTypeImportStatus::Supported};
}

// Opaque type-template-parameter import. Spelling-only bindings stay Unresolved
// until TemplateDeclId + parameter index are published onto the specifier.
inline CanonicalTypeImport importCanonicalTemplateParameter(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	if (!syntax.has_template_parameter_decl()) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	return importCanonicalShapedBase(table, syntax, context,
		table.templateParameter(syntax.template_decl_id(), syntax.template_parameter_index()));
}

// Spec import accepts stamped Type and literal-NTTP ExprId args. Unstamped
// template-ids and args that do not import as Supported stay Unresolved.
inline CanonicalTypeImport importCanonicalTemplateSpecialization(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	if (!syntax.has_template_specialization()) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	const auto reference = syntax.reference_qualifier();
	if (!isValidCVQualifier(syntax.cv_qualifier()) ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference)) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (!isValidCVQualifier(pointer.cv_qualifier)) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
	}
	const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
	const bool has_pointee_array = syntax.has_pointee_array_declarator();
	const bool has_array_shape = has_ordinary_array || has_pointee_array ||
		!syntax.array_dimensions().empty() || syntax.has_unsized_outer_array_dimension();
	if (has_array_shape) {
		if ((!has_ordinary_array && !has_pointee_array) ||
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array && !has_pointee_array) ||
			(has_pointee_array && syntax.pointer_levels().empty())) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		if (has_ordinary_array && syntax.array_dimensions().empty() &&
			!syntax.has_unsized_outer_array_dimension()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedArray};
		}
		for (const size_t extent : syntax.array_dimensions()) {
			if (extent == 0) {
				return {{}, CanonicalTypeImportStatus::UnmigratedArray};
			}
		}
	}
	std::vector<CanonicalTemplateArgument> argument_ids;
	argument_ids.reserve(syntax.specialization_arg_count());
	for (size_t index = 0; index < syntax.specialization_arg_count(); ++index) {
		if (syntax.specialization_arg_is_type(index)) {
			const CanonicalTypeImport imported_argument = importCanonicalTypeImpl(
				table, syntax.specialization_arg_type(index), CanonicalTypeImportContext::Exact);
			if (imported_argument.status != CanonicalTypeImportStatus::Supported) {
				return {{}, imported_argument.status == CanonicalTypeImportStatus::Invalid
					? CanonicalTypeImportStatus::Invalid
					: CanonicalTypeImportStatus::Unresolved};
			}
			argument_ids.push_back(CanonicalTemplateArgument::makeType(imported_argument.type));
			continue;
		}
		if (syntax.specialization_arg_is_template(index)) {
			const TemplateDeclId template_decl = syntax.specialization_arg_template(index);
			if (!template_decl) {
				return {{}, CanonicalTypeImportStatus::Unresolved};
			}
			argument_ids.push_back(CanonicalTemplateArgument::makeTemplate(template_decl));
			continue;
		}
		if (syntax.specialization_arg_is_dependent_template(index)) {
			const SpecDependentTemplateArg template_parameter =
				syntax.specialization_arg_dependent_template(index);
			if (!template_parameter.template_decl) {
				return {{}, CanonicalTypeImportStatus::Unresolved};
			}
			argument_ids.push_back(CanonicalTemplateArgument::makeDependentTemplate(
				template_parameter.template_decl,
				template_parameter.parameter_index));
			continue;
		}
		if (syntax.specialization_arg_is_value(index)) {
			argument_ids.push_back(CanonicalTemplateArgument::makeValue(syntax.specialization_arg_value(index)));
			continue;
		}
		const ExprId expr = syntax.specialization_arg_expr(index);
		if (!expr) {
			return {{}, CanonicalTypeImportStatus::Unresolved};
		}
		argument_ids.push_back(CanonicalTemplateArgument::makeNonType(expr));
	}
	auto id = syntax.is_alias_template_specialization()
		? table.aliasTemplateSpecialization(syntax.specialization_template_decl(), argument_ids)
		: table.templateSpecialization(syntax.specialization_template_decl(), argument_ids);
	id = table.qualify(id, syntax.cv_qualifier());
	id = applyCanonicalPointerArrayReference(
		table, id, syntax, context, has_ordinary_array, has_pointee_array);
	return {id, CanonicalTypeImportStatus::Supported};
}

// Boundary-3A adapter: inspect only resolved declarator structure. Unsupported
// families stay explicit; never flatten a dependent type into a supported
// pointee. Spelling, parser state and gTypeInfo are not identity.
inline CanonicalTypeImport importCanonicalTypeImpl(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	if (syntax.is_pack_expansion() || syntax.has_concept_constraint()) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	// A member-pointer declarator wraps the complete callable type. Its base
	// may itself be a template specialization, a template parameter, or a
	// dependent name, so import the pointer shape before interpreting that base.
	if (syntax.has_member_class() ||
		syntax.category() == TypeCategory::MemberFunctionPointer ||
		syntax.category() == TypeCategory::MemberObjectPointer) {
		CanonicalTypeTransaction transaction(table);
		const auto imported = importCanonicalMemberPointer(table, syntax);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	if (syntax.has_dependent_name_type()) {
		const auto base = syntax.dependent_name_type();
		const auto base_kind = table.node(base).kind;
		if (base_kind != CanonicalTypeKind::DependentName &&
			base_kind != CanonicalTypeKind::DependentTemplateMember &&
			base_kind != CanonicalTypeKind::DependentMemberAlias) {
			throw InternalError("canonical type adapter: dependent-name binding has the wrong kind");
		}
		CanonicalTypeTransaction transaction(table);
		const auto imported = importCanonicalShapedBase(table, syntax, context, base);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	if (syntax.has_template_parameter_identity() || syntax.has_template_parameter_decl()) {
		CanonicalTypeTransaction transaction(table);
		const auto imported = importCanonicalTemplateParameter(table, syntax, context);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	if (syntax.has_template_specialization()) {
		CanonicalTypeTransaction transaction(table);
		const auto imported = importCanonicalTemplateSpecialization(table, syntax, context);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	if (syntax.has_function_signature() ||
		syntax.category() == TypeCategory::Function ||
		syntax.category() == TypeCategory::FunctionPointer) {
		CanonicalTypeTransaction transaction(table);
		const auto imported = importCanonicalCallable(table, syntax, context);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	if (syntax.category() == TypeCategory::Struct || syntax.category() == TypeCategory::Enum) {
		const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
		const bool has_pointee_array = syntax.has_pointee_array_declarator();
		const bool has_array_shape = has_ordinary_array || has_pointee_array ||
			!syntax.array_dimensions().empty() || syntax.has_unsized_outer_array_dimension();
		if (has_array_shape) {
			return importCanonicalCompleteNominalArray(table, syntax, context);
		}
		CanonicalTypeTransaction transaction(table);
		const auto imported = syntax.category() == TypeCategory::Struct
			? importCanonicalRecord(table, syntax, context)
			: importCanonicalEnum(table, syntax, context);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	CanonicalBuiltinKind builtin;
	const bool is_unsigned = syntax.qualifier() == TypeQualifier::Unsigned;
	switch (syntax.category()) {
	case TypeCategory::Void: builtin = CanonicalBuiltinKind::Void; break;
	case TypeCategory::Bool: builtin = CanonicalBuiltinKind::Bool; break;
	case TypeCategory::Char:
		builtin = is_unsigned ? CanonicalBuiltinKind::UnsignedChar :
			syntax.qualifier() == TypeQualifier::Signed ? CanonicalBuiltinKind::SignedChar : CanonicalBuiltinKind::Char;
		break;
	case TypeCategory::UnsignedChar: builtin = CanonicalBuiltinKind::UnsignedChar; break;
	case TypeCategory::WChar: builtin = CanonicalBuiltinKind::WChar; break;
	case TypeCategory::Char8: builtin = CanonicalBuiltinKind::Char8; break;
	case TypeCategory::Char16: builtin = CanonicalBuiltinKind::Char16; break;
	case TypeCategory::Char32: builtin = CanonicalBuiltinKind::Char32; break;
	case TypeCategory::Short: builtin = is_unsigned ? CanonicalBuiltinKind::UnsignedShort : CanonicalBuiltinKind::Short; break;
	case TypeCategory::UnsignedShort: builtin = CanonicalBuiltinKind::UnsignedShort; break;
	case TypeCategory::Int: builtin = is_unsigned ? CanonicalBuiltinKind::UnsignedInt : CanonicalBuiltinKind::Int; break;
	case TypeCategory::UnsignedInt: builtin = CanonicalBuiltinKind::UnsignedInt; break;
	case TypeCategory::Long: builtin = is_unsigned ? CanonicalBuiltinKind::UnsignedLong : CanonicalBuiltinKind::Long; break;
	case TypeCategory::UnsignedLong: builtin = CanonicalBuiltinKind::UnsignedLong; break;
	case TypeCategory::LongLong: builtin = is_unsigned ? CanonicalBuiltinKind::UnsignedLongLong : CanonicalBuiltinKind::LongLong; break;
	case TypeCategory::UnsignedLongLong: builtin = CanonicalBuiltinKind::UnsignedLongLong; break;
	case TypeCategory::Float: builtin = CanonicalBuiltinKind::Float; break;
	case TypeCategory::Double: builtin = CanonicalBuiltinKind::Double; break;
	case TypeCategory::LongDouble: builtin = CanonicalBuiltinKind::LongDouble; break;
	case TypeCategory::Nullptr: builtin = CanonicalBuiltinKind::Nullptr; break;
	case TypeCategory::Struct:
	case TypeCategory::Enum:
		// Handled above; keep the case for exhaustiveness diagnostics.
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	case TypeCategory::UserDefined:
	case TypeCategory::TypeAlias:
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	case TypeCategory::Auto:
	case TypeCategory::DeclTypeAuto:
	case TypeCategory::Template:
		return {{}, CanonicalTypeImportStatus::Unresolved};
	case TypeCategory::Invalid:
		return {{}, CanonicalTypeImportStatus::Invalid};
	default:
		throw InternalError("canonical type adapter: unknown type category");
	}
	const auto reference = syntax.reference_qualifier();
	if (!isValidCVQualifier(syntax.cv_qualifier()) ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference) ||
		(builtin == CanonicalBuiltinKind::Void && !syntax.is_pointer() && syntax.is_reference())) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	if (syntax.has_ordered_declarator()) {
		CanonicalTypeTransaction transaction(table);
		TypeId id = table.builtin(builtin);
		id = table.qualify(id, syntax.cv_qualifier());
		CanonicalTypeImport imported = applyCanonicalOrderedDeclarator(
			table,
			id,
			syntax,
			context);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return imported;
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (!isValidCVQualifier(pointer.cv_qualifier)) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
	}
	const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
	const bool has_pointee_array = syntax.has_pointee_array_declarator();
	const bool has_array_shape = has_ordinary_array || has_pointee_array ||
		!syntax.array_dimensions().empty() || syntax.has_unsized_outer_array_dimension();
	if (has_array_shape) {
		if ((!has_ordinary_array && !has_pointee_array) ||
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array && !has_pointee_array) ||
			(has_pointee_array && syntax.pointer_levels().empty())) {
			return {{}, CanonicalTypeImportStatus::Invalid};
		}
		if (has_ordinary_array && syntax.array_dimensions().empty() &&
			!syntax.has_unsized_outer_array_dimension()) {
			return {{}, CanonicalTypeImportStatus::UnmigratedArray};
		}
		for (const size_t extent : syntax.array_dimensions()) {
			if (extent == 0) {
				return {{}, CanonicalTypeImportStatus::UnmigratedArray};
			}
		}
	}
	CanonicalTypeTransaction transaction(table);
	auto id = table.builtin(builtin);
	id = table.qualify(id, syntax.cv_qualifier());
	id = applyCanonicalPointerArrayReference(
		table, id, syntax, context, has_ordinary_array, has_pointee_array);
	transaction.commit();
	return {id, CanonicalTypeImportStatus::Supported};
}

inline CanonicalTypeImport importCanonicalType(CanonicalTypeTable& table, const TypeSpecifierNode& syntax) {
	return importCanonicalTypeImpl(table, syntax, CanonicalTypeImportContext::Exact);
}

// Collapses the common "import this operand, then bail unless it imported as
// Supported" preamble. Returns the imported TypeId on Supported and an empty
// result for every other status, so the caller keeps its compatibility
// fallback. Invalid is folded into that fallback: these operands are
// compiler-constructed projections where a malformed shape is not a hard error,
// and the callers deliberately recover. The caller owns the
// CanonicalTypeTransaction over `table`, so the import still rolls back with
// the caller's scope.
inline std::optional<TypeId> tryImportSupportedCanonical(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	const CanonicalTypeImport imported = importCanonicalType(table, syntax);
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	return imported.type;
}

// Commit declaration identity before storing a TypeId on persistent member metadata.
inline TypeId importCanonicalStructMemberType(CanonicalTypeTable& table, const TypeSpecifierNode& syntax) {
	TypeSpecifierNode bound_syntax = syntax;
	tryBindPublishedTypeEntity(bound_syntax);
	tryBindPublishedMemberClassEntity(bound_syntax);
	CanonicalTypeTransaction transaction(table);
	const std::optional<TypeId> imported = tryImportSupportedCanonical(table, bound_syntax);
	if (!imported.has_value()) {
		return {};
	}
	transaction.commit();
	return *imported;
}

// Declarator bounds may be resolved separately from the source type during layout.
// Import those extents with the original qualifiers and pointer/array ordering.
inline TypeId importCanonicalStructMemberType(CanonicalTypeTable& table, const TypeSpecifierNode& syntax, bool is_array,
	bool pointee_array_declarator, std::span<const size_t> array_dimensions) {
	TypeSpecifierNode normalized = syntax;
	if (normalized.has_ordered_declarator()) {
		if (array_dimensions.empty()) {
			return importCanonicalStructMemberType(table, normalized);
		}
		if (normalized.ordered_declarator_has_legacy_projection()) {
			normalized.clear_ordered_declarator();
		} else {
			std::vector<DeclaratorComponent> components(normalized.declarator_components().begin(), normalized.declarator_components().end());
			size_t array_count = 0;
			for (const DeclaratorComponent& component : components) {
				if (component.kind == DeclaratorComponentKind::Array || component.kind == DeclaratorComponentKind::UnknownBoundArray) {
					++array_count;
				}
			}
			if (array_count == array_dimensions.size()) {
				size_t dimension_index = 0;
				for (DeclaratorComponent& component : components) {
					if (component.kind == DeclaratorComponentKind::Array || component.kind == DeclaratorComponentKind::UnknownBoundArray) {
						component.kind = DeclaratorComponentKind::Array;
						component.payload = array_dimensions[dimension_index++];
					}
				}
				normalized.set_ordered_declarator(std::move(components));
			} else if (array_count == 0 && is_array && !pointee_array_declarator) {
				// A member-pointer spine can carry the member-pointer component while
				// the array bounds remain in layout metadata. Restore the outer array
				// wrappers so the published member type is Array(MemberPointer(...)).
				std::vector<DeclaratorComponent> restored;
				restored.reserve(array_dimensions.size() + components.size());
				for (const size_t extent : array_dimensions) {
					restored.push_back(DeclaratorComponent::array(extent));
				}
				restored.insert(restored.end(), components.begin(), components.end());
				normalized.set_ordered_declarator(std::move(restored));
			}
		}
	}
	if (normalized.has_ordered_declarator()) {
		return importCanonicalStructMemberType(table, normalized);
	}
	if (is_array && !pointee_array_declarator && !array_dimensions.empty() && normalized.has_member_class()) {
		TypeSpecifierNode base = normalized;
		base.set_array_dimensions({});
		const TypeId base_id = importCanonicalStructMemberType(table, base);
		if (!base_id) {
			return {};
		}
		CanonicalTypeTransaction transaction(table);
		const TypeId result = addCanonicalArrayDimensions(table, base_id, array_dimensions, 0);
		transaction.commit();
		return result;
	}
	if (pointee_array_declarator) {
		normalized.set_pointee_array_declarator(true);
		normalized.set_pointee_array_dimensions(array_dimensions);
	} else if (is_array || (syntax.reference_qualifier() != ReferenceQualifier::None && !array_dimensions.empty())) {
		if (array_dimensions.empty()) {
			normalized.set_array(true, std::nullopt);
			normalized.set_unsized_outer_array_dimension(true);
		} else {
			normalized.set_array_dimensions(array_dimensions);
		}
	}
	return importCanonicalStructMemberType(table, normalized);
}

// Trait operands share the component reshape, so a class recovered from TypeInfo keeps its array bounds.
// The previous reshape kept only cv, pointers, and a reference.
inline CanonicalTypeImport importCanonicalTypeWithClassFallback(CanonicalTypeTable& table, const TypeSpecifierNode& syntax) {
	return importCanonicalNominalComponent(table, syntax, CanonicalTypeImportContext::Exact);
}

inline CanonicalTypeImport importCanonicalStructuralTraitOperand(CanonicalTypeTable& table, const TypeSpecifierNode& syntax) {
	return importCanonicalTypeWithClassFallback(table, syntax);
}

inline CanonicalTypeImport importCanonicalTemplateTypeArgumentDirect(
	CanonicalTypeTable& table,
	const TypeInfo::TemplateArgInfo& argument) {
	if (argument.is_value || argument.is_pack ||
		argument.is_template_template_arg) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	const std::optional<TypeSpecifierNode> argument_type =
		makeTypeSpecifierFromTemplateArgInfo(argument, Token{});
	if (!argument_type.has_value()) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	return importCanonicalType(table, *argument_type);
}

inline std::optional<TypeId> tryImportCanonicalIntegralValue(CanonicalTypeTable& table, TypeIndex value_type, int64_t converted_value) {
	const TypeCategory category = value_type.category();
	if (!is_integer_type(category) && category != TypeCategory::Bool && category != TypeCategory::Enum) {
		return std::nullopt;
	}
	TypeSpecifierNode syntax(value_type, 0, Token{}, CVQualifier::None, ReferenceQualifier::None);
	tryBindPublishedTypeEntity(syntax);
	const CanonicalTypeImport imported = importCanonicalType(table, syntax);
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	return table.integralValue(imported.type, static_cast<uint64_t>(converted_value));
}

// Scalar field identity retains the declared type. Enum representation comes
// from its published layout, independent of legacy member storage metadata.
// A bit-field narrows that representation to its published width before the
// signed sign-extension or unsigned truncation is folded into the value.
inline std::optional<TypeId> tryImportCanonicalStructuralScalar(CanonicalTypeTable& table, TypeId field_type, uint64_t bits,
	size_t storage_size, std::optional<uint8_t> bit_width) {
	CanonicalTypeNode representation = table.node(field_type);
	if (representation.kind == CanonicalTypeKind::Enum) {
		const EntityId entity = table.enumEntity(field_type);
		if (!table.hasEnumLayout(entity)) {
			return std::nullopt;
		}
		const CanonicalEnumLayout layout = table.enumLayout(entity);
		representation = table.node(layout.underlying_type);
		storage_size = layout.size_bytes;
	}
	if (representation.kind != CanonicalTypeKind::Builtin || representation.builtin < CanonicalBuiltinKind::Bool ||
		representation.builtin > CanonicalBuiltinKind::UnsignedLongLong || storage_size == 0 || storage_size > sizeof(uint64_t)) {
		return std::nullopt;
	}
	if (bit_width.has_value() && (*bit_width == 0 || *bit_width > 64 || *bit_width > storage_size * 8)) {
		return std::nullopt;
	}
	const auto category = canonicalBuiltinToTypeCategory(representation.builtin);
	if (!category.has_value()) {
		return std::nullopt;
	}
	const int converted_width = bit_width.has_value() ? static_cast<int>(*bit_width) : static_cast<int>(storage_size * 8);
	const uint64_t converted_bits = normalizeIntegralImmediateValue(bits, *category, converted_width);
	return table.integralValue(field_type, converted_bits);
}

// Floating structural fields retain their declared Float/Double type and exact
// representation. Long double needs a wider representation than the evaluator
// carries today, so it fails closed instead of aliasing through double bits.
inline std::optional<TypeId> tryImportCanonicalStructuralFloating(CanonicalTypeTable& table, TypeId field_type, uint64_t bits, size_t storage_size) {
	const CanonicalTypeNode representation = table.node(field_type);
	if (representation.kind != CanonicalTypeKind::Builtin) {
		return std::nullopt;
	}
	if (representation.builtin == CanonicalBuiltinKind::Float) {
		if (storage_size != sizeof(float) || bits > UINT32_MAX) {
			return std::nullopt;
		}
	} else if (representation.builtin == CanonicalBuiltinKind::Double) {
		if (storage_size != sizeof(double)) {
			return std::nullopt;
		}
	} else {
		return std::nullopt;
	}
	return table.floatingValue(field_type, bits);
}

// Address selection supplies the actual declaration. No lookup by spelling or
// ABI symbol may manufacture the target's semantic identity at this boundary.
inline std::optional<TypeId> tryImportCanonicalFunctionPointerValue(CanonicalTypeTable& table, TypeId pointer_type,
	const FunctionDeclarationNode& target) {
	if (!target.entity_id() || target.is_member_function() || target.is_template_pattern() || target.is_deleted() || target.has_noexcept_expression()) {
		return std::nullopt;
	}
	const CanonicalTypeNode pointer = table.node(pointer_type);
	if (pointer.kind != CanonicalTypeKind::Pointer || table.node(pointer.child).kind != CanonicalTypeKind::Function) return std::nullopt;
	CanonicalTypeTransaction transaction(table);
	TypeSpecifierNode return_type = target.decl_node().type_specifier_node();
	tryBindPublishedTypeEntity(return_type);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(return_type));
	OverloadVector<FunctionType, 4> parameters;
	for (const ASTNode& parameter : target.parameter_nodes()) {
		if (!parameter.is<DeclarationNode>()) return std::nullopt;
		TypeSpecifierNode parameter_type = parameter.as<DeclarationNode>().type_specifier_node();
		tryBindPublishedTypeEntity(parameter_type);
		parameters.push_back(makeFunctionTypeFromSpecifier(parameter_type));
	}
	signature.setParameterTypes(std::move(parameters));
	signature.calling_convention = target.calling_convention();
	signature.linkage = target.linkage();
	signature.is_variadic = target.is_variadic();
	signature.is_noexcept = target.is_noexcept();
	const CanonicalTypeImport imported = importCanonicalFunctionSignature(table, signature, TypeId{});
	if (imported.status != CanonicalTypeImportStatus::Supported || table.hasDependentTypeComponents(imported.type) ||
		!isCanonicalFunctionPointerTargetCompatible(table.node(imported.type), table.node(pointer.child))) {
		return std::nullopt;
	}
	const TypeId value = table.functionPointerValue(pointer_type, target.entity_id(), imported.type);
	transaction.commit();
	return value;
}

inline std::optional<TypeId> tryImportCanonicalStructuralNullPointer(CanonicalTypeTable& table, TypeId field_type) {
	const CanonicalTypeKind kind = table.node(field_type).kind;
	if (kind != CanonicalTypeKind::Pointer && kind != CanonicalTypeKind::MemberObjectPointer && kind != CanonicalTypeKind::MemberFunctionPointer) {
		return std::nullopt;
	}
	return table.nullPointerValue(field_type);
}

// Pattern records provide member names and declaration ordinals for a
// specialization owner; the specialization TypeId remains the value identity.
// The pattern declaration pointer verifies the recovered record, so its spelling
// is only a lookup key, never the identity.
inline const StructTypeInfo* tryResolveCanonicalMemberPointerOwnerRecord(CanonicalTypeTable& table, TypeId owner) {
	const CanonicalTypeNode owner_node = table.node(owner);
	if (owner_node.kind == CanonicalTypeKind::Record) {
		const TypeInfo* owner_info = tryFindTypeInfoByEntityId(table.recordEntity(owner));
		return owner_info != nullptr ? owner_info->getStructInfo() : nullptr;
	}
	if (owner_node.kind == CanonicalTypeKind::NestedRecord) {
		const TypeInfo* pattern_info = tryFindTypeInfoByEntityId(table.nestedRecordEntity(owner));
		return pattern_info != nullptr ? pattern_info->getStructInfo() : nullptr;
	}
	if (owner_node.kind != CanonicalTypeKind::TemplateSpecialization) {
		return nullptr;
	}
	const TemplateDeclId primary{static_cast<uint32_t>(owner_node.array_extent)};
	const std::optional<ASTNode> pattern = requireFrontendContext().templateDecls().primaryClassTemplatePattern(primary);
	if (!pattern.has_value() || !pattern->is<TemplateClassDeclarationNode>()) {
		return nullptr;
	}
	const StructDeclarationNode& declaration = pattern->as<TemplateClassDeclarationNode>().class_decl_node();
	const TypeInfo* owner_info = findTypeByName(declaration.name());
	const StructTypeInfo* owner_record = owner_info != nullptr ? owner_info->getStructInfo() : nullptr;
	return owner_record != nullptr && owner_record->declaration_node == &declaration ? owner_record : nullptr;
}

struct CanonicalLookupClass {
	TypeId type;
	const StructTypeInfo* record;
};

// Enqueue the canonical non-virtual bases of one lookup class. Virtual and
// deferred bases, unsupported imports, and non-record canonical owners fail
// closed so member lookup never invents an identity.
inline bool appendCanonicalBaseLookupClasses(CanonicalTypeTable& table, const CanonicalLookupClass& current,
	InlineVector<CanonicalLookupClass, FlashCpp::InlineVectorSpillFamily::ClassLayout, 4>& pending) {
	for (const BaseClassSpecifier& base : current.record->base_classes) {
		if (base.is_virtual || base.is_deferred) {
			return false;
		}
		const TypeInfo* base_info = tryGetTypeInfo(base.type_index);
		const StructTypeInfo* base_record = base_info != nullptr ? base_info->getStructInfo() : nullptr;
		if (base_info == nullptr || base_record == nullptr) {
			return false;
		}
		const CanonicalTypeImport imported_base = importCanonicalClassTypeInfo(table, *base_info);
		if (imported_base.status != CanonicalTypeImportStatus::Supported || table.node(imported_base.type).kind != CanonicalTypeKind::Record) {
			return false;
		}
		pending.push_back({imported_base.type, base_record});
	}
	return true;
}

inline std::optional<TypeId> tryImportCanonicalStructuralMemberObjectPointer(CanonicalTypeTable& table, TypeId field_type,
	StringHandle member_name) {
	const CanonicalTypeNode type = table.node(field_type);
	if (type.kind != CanonicalTypeKind::MemberObjectPointer || !member_name.isValid()) {
		return std::nullopt;
	}
	const TypeId owner{static_cast<uint32_t>(type.array_extent)};
	const StructTypeInfo* owner_record = tryResolveCanonicalMemberPointerOwnerRecord(table, owner);
	if (owner_record == nullptr) {
		return std::nullopt;
	}
	InlineVector<CanonicalLookupClass, FlashCpp::InlineVectorSpillFamily::ClassLayout, 4> pending;
	pending.push_back({owner, owner_record});
	std::unordered_set<uint32_t> visited;
	std::optional<TypeId> declaring_class;
	uint16_t declaring_ordinal = 0;
	while (!pending.empty()) {
		const CanonicalLookupClass current = pending.back();
		pending.pop_back();
		if (!visited.insert(current.type.value).second) {
			continue;
		}
		const StructMember* member = current.record->findMember(member_name);
		if (member != nullptr) {
			if (member->bitfield_width.has_value() || member->isUnnamedOrZeroWidthBitfield()) {
				return std::nullopt;
			}
			const size_t ordinal = static_cast<size_t>(member - current.record->members.data());
			if (declaring_class.has_value() || ordinal > UINT16_MAX) {
				return std::nullopt;
			}
			declaring_class = current.type;
			declaring_ordinal = static_cast<uint16_t>(ordinal);
			continue;
		}
		if (!appendCanonicalBaseLookupClasses(table, current, pending)) {
			return std::nullopt;
		}
	}
	if (!declaring_class.has_value()) {
		return std::nullopt;
	}
	if (!table.hasRecordFieldSchema(*declaring_class)) {
		return std::nullopt;
	}
	return table.memberObjectPointerValue(field_type, *declaring_class, declaring_ordinal);
}

// The schema preserves the verified declaration-record ordinal. Only canonical
// substitution of the owner's published arguments makes a pattern signature concrete.
inline TypeId tryImportCanonicalMemberFunctionSignature(CanonicalTypeTable& table, const StructMemberFunction& function, TemplateDeclId environment,
	std::span<const CanonicalTemplateArgument> arguments) {
	if (function.is_constructor || function.is_destructor || !function.function_decl.is<FunctionDeclarationNode>()) return {};
	const FunctionDeclarationNode& declaration = function.function_decl.as<FunctionDeclarationNode>();
	if (declaration.is_static() || declaration.is_deleted() || declaration.is_template_pattern() || declaration.has_noexcept_expression()) return {};
	CanonicalTypeTransaction transaction(table);
	TypeSpecifierNode return_type = declaration.decl_node().type_specifier_node();
	tryBindPublishedTypeEntity(return_type);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(return_type));
	OverloadVector<FunctionType, 4> parameter_types;
	for (const ASTNode& parameter_node : declaration.parameter_nodes()) {
		if (!parameter_node.is<DeclarationNode>()) return {};
		TypeSpecifierNode parameter_type = parameter_node.as<DeclarationNode>().type_specifier_node();
		tryBindPublishedTypeEntity(parameter_type);
		parameter_types.push_back(makeFunctionTypeFromSpecifier(parameter_type));
	}
	signature.setParameterTypes(std::move(parameter_types));
	signature.calling_convention = declaration.calling_convention();
	signature.is_variadic = declaration.is_variadic();
	signature.is_const = function.is_const();
	signature.is_volatile = function.is_volatile();
	signature.function_reference_qualifier = function.ref_qualifier;
	signature.is_noexcept = declaration.is_noexcept();
	const CanonicalTypeImport imported = importCanonicalFunctionSignature(table, signature, TypeId{});
	if (imported.status != CanonicalTypeImportStatus::Supported) return {};
	const TypeId concrete = environment ? table.substitute(imported.type, environment, arguments) : imported.type;
	if (table.hasDependentTypeComponents(concrete)) return {};
	transaction.commit();
	return concrete;
}

inline bool tryPublishCanonicalMemberFunctionSignatures(CanonicalTypeTable& table, TypeId owner, const StructTypeInfo& record) {
	if (!record.layout_is_complete || tryResolveCanonicalMemberPointerOwnerRecord(table, owner) != &record ||
		record.declaration_node == nullptr || record.declaration_node->is_forward_declaration() || record.member_functions.size() > UINT16_MAX ||
		table.hasDependentTypeComponents(owner)) {
		return false;
	}
	if (table.hasMemberFunctionSignatures(owner)) return table.memberFunctionSignatureCount(owner) == record.member_functions.size();
	CanonicalTypeTransaction transaction(table);
	TemplateDeclId environment;
	std::vector<CanonicalTemplateArgument> arguments;
	TypeId environment_owner = owner;
	while (table.node(environment_owner).kind == CanonicalTypeKind::NestedRecord) environment_owner = table.nestedRecordOwner(environment_owner);
	if (table.node(environment_owner).kind == CanonicalTypeKind::TemplateSpecialization) {
		environment = table.templateSpecializationDecl(environment_owner);
		for (TypeId link = table.templateSpecializationArguments(environment_owner); link; link = table.templateArgumentNext(link)) {
			switch (table.templateArgumentKind(link)) {
			case CanonicalTemplateArgKind::Type:
				arguments.push_back(CanonicalTemplateArgument::makeType(table.templateArgumentType(link)));
				break;
			case CanonicalTemplateArgKind::NonType:
				arguments.push_back(CanonicalTemplateArgument::makeNonType(table.templateArgumentExpr(link)));
				break;
			case CanonicalTemplateArgKind::Value:
				arguments.push_back(CanonicalTemplateArgument::makeValue(table.templateArgumentValue(link)));
				break;
			case CanonicalTemplateArgKind::Template:
				arguments.push_back(CanonicalTemplateArgument::makeTemplate(table.templateArgumentTemplate(link)));
				break;
			case CanonicalTemplateArgKind::DependentTemplate:
				return false;
			}
		}
	}
	std::vector<TypeId> signatures;
	signatures.reserve(record.member_functions.size());
	for (const StructMemberFunction& function : record.member_functions) {
		signatures.push_back(tryImportCanonicalMemberFunctionSignature(table, function, environment, arguments));
	}
	table.publishMemberFunctionSignatures(owner, signatures);
	transaction.commit();
	return true;
}

inline std::optional<TypeId> tryImportCanonicalStructuralMemberFunctionPointer(CanonicalTypeTable& table, TypeId field_type,
	StringHandle member_name) {
	const CanonicalTypeNode type = table.node(field_type);
	if (type.kind != CanonicalTypeKind::MemberFunctionPointer || !member_name.isValid()) {
		return std::nullopt;
	}
	const TypeId owner{static_cast<uint32_t>(type.array_extent)};
	const StructTypeInfo* owner_record = tryResolveCanonicalMemberPointerOwnerRecord(table, owner);
	if (owner_record == nullptr) {
		return std::nullopt;
	}
	const TypeId expected_signature = type.child;
	InlineVector<CanonicalLookupClass, FlashCpp::InlineVectorSpillFamily::ClassLayout, 4> pending;
	pending.push_back({owner, owner_record});
	std::unordered_set<uint32_t> visited;
	std::optional<TypeId> declaring_class;
	uint16_t declaring_ordinal = 0;
	while (!pending.empty()) {
		const CanonicalLookupClass current = pending.back();
		pending.pop_back();
		if (!visited.insert(current.type.value).second) {
			continue;
		}
		if (!tryPublishCanonicalMemberFunctionSignatures(table, current.type, *current.record)) return std::nullopt;
		bool has_name = false;
		std::optional<uint16_t> matched_ordinal;
		for (size_t index = 0; index < current.record->member_functions.size(); ++index) {
			const StructMemberFunction& function = current.record->member_functions[index];
			if (function.getName() != member_name) {
				continue;
			}
			has_name = true;
			if (table.memberFunctionSignatureAt(current.type, index) != expected_signature) {
				continue;
			}
			if (matched_ordinal.has_value() || index > UINT16_MAX) {
				return std::nullopt;
			}
			matched_ordinal = static_cast<uint16_t>(index);
		}
		if (has_name) {
			if (!matched_ordinal.has_value() || declaring_class.has_value()) {
				return std::nullopt;
			}
			declaring_class = current.type;
			declaring_ordinal = *matched_ordinal;
			continue;
		}
		if (!appendCanonicalBaseLookupClasses(table, current, pending)) {
			return std::nullopt;
		}
	}
	if (!declaring_class.has_value()) {
		return std::nullopt;
	}
	return table.memberFunctionPointerValue(field_type, *declaring_class, declaring_ordinal);
}

inline std::optional<TypeId> tryImportCanonicalStructuralFieldValue(CanonicalTypeTable& table, TypeId field_type,
	const FlashCpp::NonTypeValueIdentity& value, size_t storage_size, std::optional<uint8_t> bit_width) {
	if (value.is_dependent) {
		return std::nullopt;
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::Integral) {
		return tryImportCanonicalStructuralScalar(table, field_type, static_cast<uint64_t>(value.value), storage_size, bit_width);
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::Floating && !bit_width.has_value()) {
		return tryImportCanonicalStructuralFloating(table, field_type, static_cast<uint64_t>(value.value), storage_size);
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::Nullptr) {
		const CanonicalTypeNode field_node = table.node(field_type);
		if (field_node.kind == CanonicalTypeKind::Builtin && field_node.builtin == CanonicalBuiltinKind::Nullptr) {
			return table.nullptrValue(field_type);
		}
		return tryImportCanonicalStructuralNullPointer(table, field_type);
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::MemberPointer && value.isNullMemberPointer()) {
		return tryImportCanonicalStructuralNullPointer(table, field_type);
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::MemberPointer && value.member_name.isValid()) {
		const CanonicalTypeKind field_kind = table.node(field_type).kind;
		if (field_kind == CanonicalTypeKind::MemberObjectPointer) {
			return tryImportCanonicalStructuralMemberObjectPointer(table, field_type, value.member_name);
		}
		if (field_kind == CanonicalTypeKind::MemberFunctionPointer) {
			return tryImportCanonicalStructuralMemberFunctionPointer(table, field_type, value.member_name);
		}
	}
	return std::nullopt;
}

inline std::optional<size_t> canonicalScalarStorageSize(CanonicalTypeTable& table, TypeId type) {
	const CanonicalTypeNode node = table.node(type);
	if (node.kind == CanonicalTypeKind::Pointer || node.kind == CanonicalTypeKind::MemberObjectPointer ||
		node.kind == CanonicalTypeKind::MemberFunctionPointer) {
		const SizeInBits bits{get_type_size_bits(TypeCategory::FunctionPointer)};
		if (!bits.is_set() || bits.value % 8 != 0) {
			return std::nullopt;
		}
		return toSizeT(toBytesExact(bits));
	}
	if (node.kind == CanonicalTypeKind::Enum) {
		const EntityId entity = table.enumEntity(type);
		if (!table.hasEnumLayout(entity)) {
			return std::nullopt;
		}
		return static_cast<size_t>(table.enumLayout(entity).size_bytes);
	}
	if (node.kind != CanonicalTypeKind::Builtin) {
		return std::nullopt;
	}
	const auto category = canonicalBuiltinToTypeCategory(node.builtin);
	if (!category.has_value()) {
		return std::nullopt;
	}
	const SizeInBits bits{get_type_size_bits(*category)};
	if (!bits.is_set() || bits.value % 8 != 0) {
		return std::nullopt;
	}
	return toSizeT(toBytesExact(bits));
}

// The evaluator's payload is an input bridge only. Identity uses canonical class
// types and ordered base/member values. Frames and completed subobjects live on
// worklists; logical value nesting does not add native importer call frames.
inline std::optional<TypeId> tryImportCanonicalStructuralValue(CanonicalTypeTable& table, const FlashCpp::StructuralClassValue* value) {
	struct Frame {
		const FlashCpp::StructuralClassValue* source;
		const StructTypeInfo* record;
		TypeId type;
		uint32_t next;
		uint32_t begin;
		uint32_t source_member;
		uint16_t union_first_variant;
		uint16_t union_active_variant;
		bool wraps_union;
	};
	static_assert(sizeof(Frame) == 40);
	std::vector<Frame> pending;
	std::vector<TypeId> subobjects;
	// An active declared class cannot contain itself by value. This detects
	// malformed bridge cycles without making payload addresses semantic keys.
	std::unordered_set<uint32_t> active_classes;
	const auto unqualified = [&](TypeId type) {
		CanonicalTypeNode node = table.node(type);
		while (node.kind == CanonicalTypeKind::Qualified) {
			type = node.child;
			node = table.node(type);
		}
		return type;
	};
	const auto enqueue_record = [&](const FlashCpp::StructuralClassValue* source, TypeId expected) {
		if (source == nullptr || source->is_array) {
			return false;
		}
		const TypeInfo* info = tryGetTypeInfo(source->type_index);
		const StructTypeInfo* record = info != nullptr ? info->getStructInfo() : nullptr;
		if (record == nullptr || record->has_deferred_base_classes ||
			source->members.size() > record->members.size() || record->base_classes.size() != source->base_values.size() ||
			(record->is_union && !record->base_classes.empty())) {
			return false;
		}
		if (source->base_values.size() > UINT32_MAX || source->members.size() > UINT32_MAX - source->base_values.size() ||
			subobjects.size() > UINT32_MAX) {
			throw InternalError("canonical value importer: subobject count exceeds worklist capacity");
		}
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		if (imported.status != CanonicalTypeImportStatus::Supported) return false;
		const CanonicalTypeKind imported_kind = table.node(imported.type).kind;
		if (!isCanonicalClassTypeKind(imported_kind) || (expected && imported.type != expected)) {
			return false;
		}
		if (!record->base_classes.empty() && (!table.hasClassBaseSchema(imported.type) ||
			table.classBaseCount(imported.type) != record->base_classes.size())) {
			return false;
		}
		if (!active_classes.insert(imported.type.value).second) {
			return false;
		}
		pending.push_back({source, record, imported.type, 0, static_cast<uint32_t>(subobjects.size()), 0,
			0, FlashCpp::kNoStructuralUnionActiveVariant, false});
		return true;
	};
	const auto enqueue_array = [&](const FlashCpp::StructuralClassValue* source, TypeId expected) {
		if (source == nullptr || !source->is_array || !source->base_values.empty()) {
			return false;
		}
		const TypeId array_type = unqualified(expected);
		const CanonicalTypeNode array = table.node(array_type);
		if (array.kind != CanonicalTypeKind::Array ||
			!hasCanonicalTypeNodeFlag(array.flags, CanonicalTypeNodeFlags::KnownArrayBound) ||
			array.array_extent == 0 || array.array_extent > UINT32_MAX ||
			source->members.size() != static_cast<size_t>(array.array_extent) || subobjects.size() > UINT32_MAX) {
			return false;
		}
		pending.push_back({source, nullptr, array_type, 0, static_cast<uint32_t>(subobjects.size()), 0,
			0, FlashCpp::kNoStructuralUnionActiveVariant, false});
		return true;
	};
	const auto import_member_value = [&](TypeId owner_type,
		const StructMember& field, size_t member_index, const FlashCpp::StructuralClassValueMember& member,
		bool wrap_union, uint16_t union_first_variant, uint16_t union_active_variant) -> bool {
		if (!field.canonical_type_id) {
			return false;
		}
		TypeId field_type = field.canonical_type_id;
		size_t scalar_storage_size = field.size;
		std::optional<uint8_t> bit_width;
		if (field.bitfield_width.has_value()) {
			if (!table.hasRecordFieldSchema(owner_type)) return false;
			const CanonicalRecordMember published = table.recordMemberAt(owner_type, member_index);
			if (!hasCanonicalRecordMemberFlag(published.flags, CanonicalRecordMemberFlags::Bitfield) || published.bit_width == 0 ||
				*field.bitfield_width != published.bit_width || !published.type) {
				return false;
			}
			field_type = published.type;
			scalar_storage_size = published.size_bytes;
			bit_width = published.bit_width;
		} else if (field.is_array) {
			if (!table.hasRecordSubobjectTypes(owner_type) || member_index >= table.recordSubobjectMemberCount(owner_type)) {
				return false;
			}
			field_type = table.recordSubobjectMemberAt(owner_type, member_index);
			if (!field_type) {
				return false;
			}
		}
		field_type = unqualified(field_type);
		if (field.is_array) {
			if (member.nested_value == nullptr || !member.nested_value->is_array || !enqueue_array(member.nested_value, field_type)) {
				return false;
			}
			if (wrap_union) {
				Frame& child = pending.back();
				child.wraps_union = true;
				child.union_first_variant = union_first_variant;
				child.union_active_variant = union_active_variant;
			}
			return true;
		}
		if (member.nested_value != nullptr) {
			if (bit_width.has_value() || !enqueue_record(member.nested_value, field_type)) {
				return false;
			}
			if (wrap_union) {
				Frame& child = pending.back();
				child.wraps_union = true;
				child.union_first_variant = union_first_variant;
				child.union_active_variant = union_active_variant;
			}
			return true;
		}
		const auto scalar = tryImportCanonicalStructuralFieldValue(table, field_type, member.scalar_value, scalar_storage_size, bit_width);
		if (!scalar.has_value()) {
			return false;
		}
		TypeId imported = *scalar;
		if (wrap_union) {
			imported = table.structuralUnionValue(owner_type, union_first_variant, union_active_variant, imported);
		}
		subobjects.push_back(imported);
		return true;
	};
	if (value == nullptr || value->is_array || !enqueue_record(value, TypeId{})) {
		return std::nullopt;
	}
	while (!pending.empty()) {
		Frame& frame = pending.back();
		const bool is_array = frame.record == nullptr;
		const size_t base_count = is_array ? 0 : frame.source->base_values.size();
		const size_t total = is_array ? static_cast<size_t>(table.node(frame.type).array_extent)
			: base_count + frame.record->members.size();
		if (frame.next == total) {
			if (is_array) {
				if (frame.source->members.size() != total) {
					return std::nullopt;
				}
			} else if (frame.source_member != frame.source->members.size()) {
				return std::nullopt;
			}
			const std::span<const TypeId> values = std::span<const TypeId>(subobjects).subspan(frame.begin);
			TypeId completed;
			if (is_array) {
				completed = table.structuralArrayValue(frame.type, values);
			} else if (frame.record->is_union) {
				// The single union-group step already produced the StructuralUnionValue.
				if (values.size() != 1) {
					return std::nullopt;
				}
				completed = values.front();
			} else {
				completed = table.structuralValue(frame.type, values, static_cast<uint32_t>(base_count));
			}
			const bool wraps_union = frame.wraps_union;
			const uint16_t union_first_variant = frame.union_first_variant;
			const uint16_t union_active_variant = frame.union_active_variant;
			subobjects.resize(frame.begin);
			if (!is_array) {
				active_classes.erase(frame.type.value);
			}
			pending.pop_back();
			if (wraps_union) {
				if (pending.empty() || pending.back().record == nullptr) {
					return std::nullopt;
				}
				completed = table.structuralUnionValue(pending.back().type, union_first_variant, union_active_variant, completed);
			}
			if (pending.empty()) {
				return completed;
			}
			subobjects.push_back(completed);
			continue;
		}
		if (is_array) {
			const size_t element_index = frame.next++;
			if (element_index >= frame.source->members.size()) {
				return std::nullopt;
			}
			const FlashCpp::StructuralClassValueMember& element = frame.source->members[element_index];
			const TypeId expected = unqualified(table.node(frame.type).child);
			const CanonicalTypeNode expected_node = table.node(expected);
			if (element.nested_value != nullptr) {
				if (element.nested_value->is_array) {
					if (expected_node.kind != CanonicalTypeKind::Array || !enqueue_array(element.nested_value, expected)) {
						return std::nullopt;
					}
				} else if (!enqueue_record(element.nested_value, expected)) {
					return std::nullopt;
				}
				continue;
			}
			const CanonicalTypeKind expected_kind = expected_node.kind;
			if (expected_kind != CanonicalTypeKind::Builtin && expected_kind != CanonicalTypeKind::Enum &&
				expected_kind != CanonicalTypeKind::Pointer && expected_kind != CanonicalTypeKind::MemberObjectPointer &&
				expected_kind != CanonicalTypeKind::MemberFunctionPointer) {
				return std::nullopt;
			}
			const std::optional<size_t> storage_size = canonicalScalarStorageSize(table, expected);
			if (!storage_size.has_value()) {
				return std::nullopt;
			}
			const auto scalar = tryImportCanonicalStructuralFieldValue(table, expected, element.scalar_value, *storage_size, std::nullopt);
			if (!scalar.has_value()) {
				return std::nullopt;
			}
			subobjects.push_back(*scalar);
			continue;
		}
		const size_t index = frame.next;
		if (index < base_count) {
			frame.next = static_cast<uint32_t>(index + 1);
			const BaseClassSpecifier& base = frame.record->base_classes[index];
			const TypeId expected = table.classBaseAt(frame.type, index).type;
			const FlashCpp::StructuralClassValue* source = frame.source->base_values[index];
			if (base.is_virtual || base.is_deferred || !enqueue_record(source, expected)) {
				return std::nullopt;
			}
			continue;
		}
		const size_t member_index = index - base_count;
		const StructMember& field = frame.record->members[member_index];
		const bool needs_union_schema = frame.record->is_union || field.anonymous_union_group_index.has_value();
		if (needs_union_schema) {
			if (!table.hasRecordFieldSchema(frame.type)) return std::nullopt;
			const CanonicalRecordMember group_start_member = table.recordMemberAt(frame.type, member_index);
			if (!hasCanonicalRecordMemberFlag(group_start_member.flags, CanonicalRecordMemberFlags::UnionGroupStart)) {
				return std::nullopt;
			}
			size_t group_last = member_index;
			while (!hasCanonicalRecordMemberFlag(table.recordMemberAt(frame.type, group_last).flags, CanonicalRecordMemberFlags::UnionGroupEnd)) {
				++group_last;
				if (group_last >= frame.record->members.size()) {
					return std::nullopt;
				}
			}
			if (member_index > UINT16_MAX || group_last > UINT16_MAX) {
				return std::nullopt;
			}
			const FlashCpp::StructuralUnionSelection* selection = nullptr;
			for (const FlashCpp::StructuralUnionSelection& candidate : frame.source->union_selections) {
				if (candidate.first_variant != member_index) {
					continue;
				}
				if (selection != nullptr) {
					return std::nullopt;
				}
				selection = &candidate;
			}
			if (selection == nullptr ||
				(selection->hasActiveVariant() &&
					(selection->active_variant < member_index || selection->active_variant > group_last))) {
				return std::nullopt;
			}
			const StructTypeInfo* record = frame.record;
			const TypeId owner_type = frame.type;
			frame.next = static_cast<uint32_t>(base_count + group_last + 1);
			if (!selection->hasActiveVariant()) {
				subobjects.push_back(table.structuralUnionValue(owner_type,
					static_cast<uint16_t>(member_index), std::nullopt, TypeId{}));
				continue;
			}
			const size_t active_index = selection->active_variant;
			const StructMember& active_field = record->members[active_index];
			if (frame.source_member >= frame.source->members.size()) {
				return std::nullopt;
			}
			const FlashCpp::StructuralClassValueMember& active_member = frame.source->members[frame.source_member];
			if (active_member.name != active_field.getName()) {
				return std::nullopt;
			}
			++frame.source_member;
			if (!import_member_value(owner_type, active_field, active_index, active_member,
				true, static_cast<uint16_t>(member_index), static_cast<uint16_t>(active_index))) {
				return std::nullopt;
			}
			continue;
		}
		frame.next = static_cast<uint32_t>(index + 1);
		if (field.isUnnamedOrZeroWidthBitfield()) {
			continue;
		}
		if (frame.source_member >= frame.source->members.size()) {
			return std::nullopt;
		}
		const FlashCpp::StructuralClassValueMember& member = frame.source->members[frame.source_member++];
		if (member.name != field.getName()) {
			return std::nullopt;
		}
		if (!import_member_value(frame.type, field, member_index, member,
			false, 0, FlashCpp::kNoStructuralUnionActiveVariant)) {
			return std::nullopt;
		}
	}
	throw InternalError("canonical value importer: worklist ended without a result");
}

inline std::optional<TypeId> tryImportCanonicalValue(CanonicalTypeTable& table, const FlashCpp::NonTypeValueIdentity& value) {
	if (value.is_dependent) {
		return std::nullopt;
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::Integral) {
		return tryImportCanonicalIntegralValue(table, value.value_type_index, value.value);
	}
	if (value.kind == FlashCpp::NonTypeValueIdentityKind::StructuralClass) {
		return tryImportCanonicalStructuralValue(table, value.structural_value);
	}
	return std::nullopt;
}

inline CanonicalTypeImport shapeCanonicalTemplateTypeArgument(
	CanonicalTypeTable& table,
	const TypeInfo::TemplateArgInfo& argument,
	TypeId base) {
	const std::optional<TypeSpecifierNode> argument_type =
		makeTypeSpecifierFromTemplateArgInfo(argument, Token{});
	if (!argument_type.has_value()) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	return importCanonicalShapedBase(
		table,
		*argument_type,
		CanonicalTypeImportContext::Exact,
		base);
}

inline CanonicalTypeImport importCanonicalClassSource(CanonicalTypeTable& table, const TypeInfo* root_type_info,
	const StructDeclarationNode* root_declaration) {
	if ((root_type_info == nullptr) == (root_declaration == nullptr)) {
		throw InternalError("canonical type adapter: class source must have one root");
	}
	struct PendingClassImport {
		const StructDeclarationNode* declaration;
		TemplateDeclId specialization_owner;
		EntityId nested_entity;
		const TypeInfo* alias_source = nullptr;
		const TypeSpecifierNode* alias_syntax = nullptr;
		size_t next_argument = 0;
		size_t waiting_argument = std::numeric_limits<size_t>::max();
		std::vector<CanonicalTemplateArgument> arguments;
		// Implicit instantiations keep pattern bindings in outer_template_args for
		// substitution. The specialization's canonical arguments are the published
		// primary arguments on the TypeInfo.
		TemplateArgInfoVector published_arguments;
		bool use_published_arguments = false;
	};

	CanonicalTypeTransaction transaction(table);
	std::vector<PendingClassImport> worklist;
	std::unordered_set<const void*> active_sources;
	const auto enqueue_source = [&](const TypeInfo* type_info, const StructDeclarationNode* declaration) -> std::optional<CanonicalTypeImport> {
		if (type_info != nullptr) {
			if (const TypeSpecifierNode* alias_type = type_info->aliasTypeSpecifier()) {
				const CanonicalTypeImport direct_import = importCanonicalType(table, *alias_type);
				if (direct_import.status != CanonicalTypeImportStatus::UnmigratedNominal) return direct_import;
				if (!active_sources.insert(type_info).second) return CanonicalTypeImport{{}, CanonicalTypeImportStatus::Unresolved};
				PendingClassImport pending{};
				pending.alias_source = type_info;
				pending.alias_syntax = alias_type;
				worklist.push_back(std::move(pending));
				return std::nullopt;
			}
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info == nullptr || struct_info->declaration_node == nullptr) {
				return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
			}
			declaration = struct_info->declaration_node;
		}

		const StructDeclarationNode* pattern = declaration->injected_class_pattern_declaration();
		TemplateDeclId specialization_owner{};
		EntityId nested_entity{};
		if (pattern != nullptr && pattern->has_entity_id() && !pattern->has_template_decl_id()) {
			if (declaration->enclosing_class() == nullptr) {
				return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
			}
			nested_entity = pattern->entity_id();
		} else if (pattern != nullptr && pattern->has_template_decl_id()) {
			// Member class templates also need enclosing arguments in their identity.
			// Their composed specialization representation is a separate slice.
			if (declaration->enclosing_class() != nullptr) {
				return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
			}
			specialization_owner = pattern->template_decl_id();
		} else if (declaration->has_canonical_specialization_owner()) {
			// An explicit (full) specialization carries the primary's template identity
			// directly, so its canonical type is the same TemplateSpecialization an
			// implicit instantiation would produce.
			specialization_owner = declaration->canonical_specialization_owner_template_decl_id();
		} else {
			if (declaration->has_entity_id()) {
				return CanonicalTypeImport{
					table.record(declaration->entity_id()),
					CanonicalTypeImportStatus::Supported};
			}
			return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
		}
		if (!active_sources.insert(declaration).second) {
			return CanonicalTypeImport{{}, CanonicalTypeImportStatus::Unresolved};
		}
		PendingClassImport pending{};
		pending.declaration = declaration;
		pending.specialization_owner = specialization_owner;
		pending.nested_entity = nested_entity;
		const bool pattern_owns_template = pattern != nullptr && pattern->has_template_decl_id();
		if (!pattern_owns_template && declaration->has_canonical_specialization_owner() &&
			type_info != nullptr && type_info->baseTemplateName().isValid()) {
			pending.published_arguments = type_info->templateArgs();
			pending.use_published_arguments = true;
		}
		if (!nested_entity) {
			pending.arguments.reserve(pending.use_published_arguments ? pending.published_arguments.size() : declaration->outer_template_args().size());
		}
		worklist.push_back(std::move(pending));
		return std::nullopt;
	};
	const auto specialization_arguments = [](const PendingClassImport& pending) -> const TemplateArgInfoVector& {
		return pending.use_published_arguments ? pending.published_arguments : pending.declaration->outer_template_args();
	};

	std::optional<CanonicalTypeImport> completed = enqueue_source(root_type_info, root_declaration);
	if (completed.has_value()) {
		if (completed->status == CanonicalTypeImportStatus::Supported) {
			transaction.commit();
		}
		return *completed;
	}

	while (true) {
		if (completed.has_value()) {
			if (worklist.empty()) {
				if (completed->status == CanonicalTypeImportStatus::Supported) {
					transaction.commit();
				}
				return *completed;
			}
			PendingClassImport& parent = worklist.back();
			if (completed->status != CanonicalTypeImportStatus::Supported) return *completed;
			if (parent.alias_syntax != nullptr) {
				const CanonicalTypeImport shaped = importCanonicalShapedBase(table, *parent.alias_syntax, CanonicalTypeImportContext::Exact, completed->type);
				active_sources.erase(parent.alias_source);
				worklist.pop_back();
				completed = shaped;
				continue;
			}
			if (parent.nested_entity) {
				const CanonicalTypeKind owner_kind = table.node(completed->type).kind;
				if (owner_kind != CanonicalTypeKind::TemplateSpecialization && owner_kind != CanonicalTypeKind::NestedRecord) {
					return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
				}
				const TypeId nested = table.nestedRecord(completed->type, parent.nested_entity);
				active_sources.erase(parent.declaration);
				worklist.pop_back();
				completed = CanonicalTypeImport{nested, CanonicalTypeImportStatus::Supported};
				continue;
			}
			if (parent.waiting_argument == std::numeric_limits<size_t>::max()) {
				throw InternalError("canonical type adapter: class import result has no parent");
			}
			const TypeInfo::TemplateArgInfo& argument = specialization_arguments(parent)[parent.waiting_argument];
			const CanonicalTypeImport imported_argument = shapeCanonicalTemplateTypeArgument(table, argument, completed->type);
			if (imported_argument.status != CanonicalTypeImportStatus::Supported) {
				return imported_argument;
			}
			parent.arguments.push_back(CanonicalTemplateArgument::makeType(imported_argument.type));
			parent.next_argument = parent.waiting_argument + 1;
			parent.waiting_argument = std::numeric_limits<size_t>::max();
			completed.reset();
			continue;
		}

		if (worklist.empty()) {
			throw InternalError("canonical type adapter: class import worklist ended early");
		}
		PendingClassImport& current = worklist.back();
		if (current.alias_syntax != nullptr) {
			const TypeInfo* target = tryGetTypeInfo(current.alias_syntax->type_index());
			if (target == nullptr) return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
			completed = enqueue_source(target, nullptr);
			continue;
		}
		if (current.nested_entity) {
			const StructDeclarationNode* owner = current.declaration->enclosing_class();
			const StructDeclarationNode* owner_pattern = owner->injected_class_pattern_declaration();
			// A selected partial stores deduced pattern bindings, not its primary
			// arguments. Importing it needs a retained TypeInfo bridge first.
			if (owner_pattern != nullptr && !owner_pattern->has_template_decl_id() && owner->has_canonical_specialization_owner()) {
				return CanonicalTypeImport{{}, CanonicalTypeImportStatus::UnmigratedNominal};
			}
			completed = enqueue_source(nullptr, owner);
			continue;
		}
		const TemplateArgInfoVector& source_arguments = specialization_arguments(current);
		if (current.next_argument < source_arguments.size()) {
			const size_t argument_index = current.next_argument;
			const TypeInfo::TemplateArgInfo& argument = source_arguments[argument_index];
			if (argument.is_value && !argument.is_pack && (argument.nttp_kind == FlashCpp::NonTypeValueIdentityKind::Integral ||
				argument.nttp_kind == FlashCpp::NonTypeValueIdentityKind::StructuralClass) &&
				!argument.dependent_name.isValid() && !argument.dependent_expr().has_value()) {
				const std::optional<TypeId> value_id = argument.nttp_kind == FlashCpp::NonTypeValueIdentityKind::Integral
					? tryImportCanonicalIntegralValue(table, argument.type_index, argument.intValue())
					: tryImportCanonicalStructuralValue(table, argument.structural_value);
				if (value_id.has_value()) {
					current.arguments.push_back(CanonicalTemplateArgument::makeValue(*value_id));
					++current.next_argument;
					continue;
				}
			}
			// Dependent and unsupported arguments retain opaque source identity.
			if (argument.is_value && argument.nttp_expr) {
				current.arguments.push_back(CanonicalTemplateArgument::makeNonType(argument.nttp_expr));
				++current.next_argument;
				continue;
			}
			const CanonicalTypeImport direct_import = importCanonicalTemplateTypeArgumentDirect(table, argument);
			if (direct_import.status == CanonicalTypeImportStatus::Supported) {
				current.arguments.push_back(CanonicalTemplateArgument::makeType(direct_import.type));
				++current.next_argument;
				continue;
			}
			if (!argument.type_index.is_valid()) {
				return direct_import;
			}
			const TypeInfo* argument_type_info = tryGetTypeInfo(argument.type_index);
			if (argument_type_info == nullptr ||
				(argument_type_info->getStructInfo() == nullptr &&
					argument_type_info->aliasTypeSpecifier() == nullptr)) {
				return direct_import;
			}
			current.waiting_argument = argument_index;
			completed = enqueue_source(argument_type_info, nullptr);
			if (completed.has_value() &&
				completed->status != CanonicalTypeImportStatus::Supported) {
				return *completed;
			}
			continue;
		}

		const TypeId specialization = table.templateSpecialization(current.specialization_owner, current.arguments);
		active_sources.erase(current.declaration);
		worklist.pop_back();
		completed = CanonicalTypeImport{specialization, CanonicalTypeImportStatus::Supported};
	}
}

inline CanonicalTypeImport importCanonicalClassDeclaration(
	CanonicalTypeTable& table,
	const StructDeclarationNode& declaration) {
	return importCanonicalClassSource(table, nullptr, &declaration);
}

inline CanonicalTypeImport importCanonicalClassTypeInfo(
	CanonicalTypeTable& table,
	const TypeInfo& type_info) {
	return importCanonicalClassSource(table, &type_info, nullptr);
}

// Resolve a derived-to-virtual-base adjustment through the published class
// TypeIds. The returned value is the runtime table index from the derived
// class's finalized ABI schema. Empty means either endpoint cannot be imported
// canonically or the target is not a published virtual base.
inline std::optional<uint32_t> tryFindPublishedVirtualBaseIndex(
	CanonicalTypeTable& table,
	const TypeInfo& derived_type_info,
	const TypeInfo& base_type_info) {
	const CanonicalTypeImport derived_import = importCanonicalClassTypeInfo(table, derived_type_info);
	const CanonicalTypeImport base_import = importCanonicalClassTypeInfo(table, base_type_info);
	if (derived_import.status != CanonicalTypeImportStatus::Supported ||
		base_import.status != CanonicalTypeImportStatus::Supported) {
		return std::nullopt;
	}
	return table.findVirtualBaseIndex(derived_import.type, base_import.type);
}

inline std::optional<uint32_t> tryFindPublishedVirtualBaseIndex(CanonicalTypeTable& table, TypeIndex derived_type_index, TypeIndex base_type_index) {
	const TypeInfo* derived_type_info = tryGetTypeInfo(derived_type_index);
	const TypeInfo* base_type_info = tryGetTypeInfo(base_type_index);
	if (derived_type_info == nullptr || base_type_info == nullptr) {
		return std::nullopt;
	}
	return tryFindPublishedVirtualBaseIndex(table, *derived_type_info, *base_type_info);
}

inline const StructTypeInfo* canonicalClassStructInfoFromTypeInfo(
	const TypeInfo& type_info) {
	const TypeInfo* current = &type_info;
	std::unordered_set<uint32_t> visited_type_indices;
	while (current != nullptr) {
		if (const StructTypeInfo* struct_info = current->getStructInfo();
			struct_info != nullptr) {
			return struct_info;
		}
		const TypeSpecifierNode* alias_type = current->aliasTypeSpecifier();
		if (alias_type == nullptr || !alias_type->type_index().is_valid() ||
			!visited_type_indices.insert(alias_type->type_index().index()).second) {
			return nullptr;
		}
		current = tryGetTypeInfo(alias_type->type_index());
	}
	return nullptr;
}

inline bool tryPublishCanonicalRecordProperties(
	CanonicalTypeTable& table,
	TypeId type,
	const StructTypeInfo& struct_info) {
	if (!type || !struct_info.hasCompleteObjectLayout() ||
		struct_info.has_deferred_base_classes) {
		return false;
	}
	const CanonicalTypeKind kind = table.node(type).kind;
	if (!isCanonicalClassTypeKind(kind)) {
		return false;
	}
	const CanonicalRecordFacts facts = computeCanonicalRecordFacts(table, struct_info);
	table.publishRecordProperties(type, facts);
	return true;
}

inline bool tryPublishCanonicalRecordSubobjectTypes(CanonicalTypeTable& table, TypeId type, const StructTypeInfo& struct_info) {
	std::vector<TypeId> member_types;
	member_types.reserve(struct_info.members.size());
	for (const StructMember& member : struct_info.members) {
		if (!member.canonical_type_id) {
			return false;
		}
		member_types.push_back(member.canonical_type_id);
	}
	table.publishRecordSubobjectTypes(type, member_types);
	return true;
}

// Publish a completed class's constructor schema keyed by its canonical TypeId
// (Record or TemplateSpecialization), read from the declaration node so no
// StructTypeInfo is needed; implicit special members are added to it as well.
// Constructors whose parameter types do not import structurally are skipped; the
// schema stays unpublished when none import.
inline bool tryPublishCanonicalRecordConstructors(CanonicalTypeTable& table, TypeId type, const StructDeclarationNode& struct_decl,
	bool has_deleted_constructor) {
	struct PendingConstructor {
		std::vector<TypeId> parameter_types;
		CanonicalRecordFunctionFlags flags = CanonicalRecordFunctionFlags::None;
		bool is_noexcept = false;
		uint16_t minimum_parameter_count = 0;
	};
	std::vector<PendingConstructor> pending;
	pending.reserve(struct_decl.member_functions().size());
	bool has_incomplete_candidates = has_deleted_constructor;
	for (const StructMemberFunctionDecl& member : struct_decl.member_functions()) {
		if (!member.is_constructor || !member.function_declaration.is<ConstructorDeclarationNode>()) {
			continue;
		}
		const ConstructorDeclarationNode& constructor = member.function_declaration.as<ConstructorDeclarationNode>();
		if (constructor.explicit_condition()) {
			has_incomplete_candidates = true;
			continue;
		}
		if (constructor.parameter_nodes().size() > std::numeric_limits<uint16_t>::max()) {
			has_incomplete_candidates = true;
			continue;
		}
		PendingConstructor entry;
		entry.parameter_types.reserve(constructor.parameter_nodes().size());
		const uint16_t parameter_count = static_cast<uint16_t>(constructor.parameter_nodes().size());
		entry.minimum_parameter_count = parameter_count;
		if (member.access != AccessSpecifier::Public) {
			entry.flags = entry.flags | CanonicalRecordFunctionFlags::NonPublic;
		}
		if (constructor.is_explicit()) {
			entry.flags = entry.flags | CanonicalRecordFunctionFlags::Explicit;
		}
		bool importable = true;
		for (size_t index = 0; index < constructor.parameter_nodes().size(); ++index) {
			const ASTNode& parameter = constructor.parameter_nodes()[index];
			const TypeSpecifierNode* parameter_type = nullptr;
			if (parameter.is<DeclarationNode>()) {
				const DeclarationNode& declaration = parameter.as<DeclarationNode>();
				if (declaration.has_default_value() && entry.minimum_parameter_count == parameter_count) {
					entry.minimum_parameter_count = static_cast<uint16_t>(index);
				}
				parameter_type = &declaration.type_specifier_node();
			} else if (parameter.is<TypeSpecifierNode>()) {
				parameter_type = &parameter.as<TypeSpecifierNode>();
			} else {
				importable = false;
				break;
			}
			std::optional<TypeId> imported = tryImportSupportedCanonical(table, *parameter_type);
			if (!imported.has_value() && constructor.is_implicit() && parameter_type->category() == TypeCategory::Struct &&
				!parameter_type->is_pointer() && (parameter_type->is_lvalue_reference() || parameter_type->is_rvalue_reference())) {
				TypeId parameter_object = type;
				if (parameter_type->cv_qualifier() != CVQualifier::None) {
					parameter_object = table.qualify(parameter_object, parameter_type->cv_qualifier());
				}
				const ReferenceQualifier reference_qualifier = parameter_type->is_rvalue_reference()
					? ReferenceQualifier::RValueReference
					: ReferenceQualifier::LValueReference;
				imported = table.reference(parameter_object, reference_qualifier);
			}
			if (!imported.has_value()) {
				importable = false;
				break;
			}
			entry.parameter_types.push_back(*imported);
		}
		if (!importable) {
			// Skip this constructor; a present entry's answer is still sound and
			// an exact-match miss defers to the compatibility path.
			has_incomplete_candidates = true;
			continue;
		}
		if (constructor.is_implicit()) {
			entry.flags = entry.flags | CanonicalRecordFunctionFlags::Implicit;
		}
		if (constructor.is_explicitly_defaulted()) {
			entry.flags = entry.flags | CanonicalRecordFunctionFlags::ExplicitlyDefaulted;
		}
		entry.is_noexcept = constructor.is_noexcept();
		pending.push_back(std::move(entry));
	}
	if (pending.empty()) {
		return false;  // no importable constructor: leave the schema unpublished
	}
	std::vector<CanonicalRecordConstructorSpec> constructors;
	constructors.reserve(pending.size());
	for (const PendingConstructor& entry : pending) {
		CanonicalRecordFunctionFlags flags = entry.flags;
		if (has_incomplete_candidates) {
			flags = flags | CanonicalRecordFunctionFlags::SchemaIncomplete;
		}
		constructors.push_back({
			.parameter_types = std::span<const TypeId>(entry.parameter_types.data(), entry.parameter_types.size()),
			.flags = flags,
			.is_noexcept = entry.is_noexcept,
			.minimum_parameter_count = entry.minimum_parameter_count,
		});
	}
	table.publishRecordConstructors(type, constructors);
	return true;
}

inline uint32_t canonicalLayoutSize(size_t value) {
	if (value > std::numeric_limits<uint32_t>::max()) {
		throw InternalError("canonical layout exceeds 32-bit object-size limit");
	}
	return static_cast<uint32_t>(value);
}

inline uint16_t canonicalLayoutCount(size_t value) {
	if (value > std::numeric_limits<uint16_t>::max()) {
		throw InternalError("canonical layout exceeds 16-bit member/base count limit");
	}
	return static_cast<uint16_t>(value);
}

inline uint8_t canonicalBitfieldWidth(size_t value) {
	if (value > std::numeric_limits<uint8_t>::max()) {
		throw InternalError("canonical layout exceeds 8-bit bitfield width limit");
	}
	return static_cast<uint8_t>(value);
}

inline CanonicalAccess canonicalAccess(AccessSpecifier access) {
	switch (access) {
	case AccessSpecifier::Public:
		return CanonicalAccess::Public;
	case AccessSpecifier::Protected:
		return CanonicalAccess::Protected;
	case AccessSpecifier::Private:
		return CanonicalAccess::Private;
	}
	throw InternalError("canonical layout: unknown access specifier");
}

inline EntityId resolvePublishedBaseEntity(const BaseClassSpecifier& base) {
	if (base.is_deferred) {
		return {};
	}
	const TypeInfo* type_info = tryGetTypeInfo(base.type_index);
	if (type_info == nullptr || !type_info->isStruct()) {
		return {};
	}
	const StructTypeInfo* base_info = type_info->getStructInfo();
	if (base_info == nullptr || base_info->declaration_node == nullptr || !base_info->declaration_node->has_entity_id()) {
		return {};
	}
	return base_info->declaration_node->entity_id();
}

inline bool tryPublishCanonicalRecordFieldSchema(CanonicalTypeTable& table, TypeId owner, const StructTypeInfo& struct_info) {
	if (!struct_info.layout_is_complete || struct_info.has_deferred_base_classes) return false;
	const CanonicalTypeKind kind = table.node(owner).kind;
	if (kind == CanonicalTypeKind::Record) {
		if (!table.hasRecordLayout(table.recordEntity(owner))) return false;
	} else if (isCanonicalClassTypeKind(kind)) {
		if (!table.hasRecordSubobjectTypes(owner) || !table.hasClassBaseSchema(owner)) return false;
	} else {
		return false;
	}
	std::vector<CanonicalRecordMember> members;
	members.reserve(struct_info.members.size());
	for (size_t index = 0; index < struct_info.members.size(); ++index) {
		const StructMember& member = struct_info.members[index];
		if (!member.canonical_type_id) {
			return false;
		}
		CanonicalRecordMemberFlags flags = CanonicalRecordMemberFlags::None;
		if (member.anonymous_union_group_index.has_value() || struct_info.is_union) {
			const auto group = member.anonymous_union_group_index;
			const bool starts = index == 0 || (!struct_info.is_union && struct_info.members[index - 1].anonymous_union_group_index != group);
			const bool ends = index + 1 == struct_info.members.size() ||
				(!struct_info.is_union && struct_info.members[index + 1].anonymous_union_group_index != group);
			flags = flags | CanonicalRecordMemberFlags::UnionVariant;
			if (starts) flags = flags | CanonicalRecordMemberFlags::UnionGroupStart;
			if (ends) flags = flags | CanonicalRecordMemberFlags::UnionGroupEnd;
		}
		uint8_t bit_width = 0;
		uint8_t bit_offset = 0;
		if (member.bitfield_width.has_value()) {
			flags = flags | CanonicalRecordMemberFlags::Bitfield;
			bit_width = canonicalBitfieldWidth(*member.bitfield_width);
			bit_offset = canonicalBitfieldWidth(member.bitfield_bit_offset);
		}
		if (member.is_no_unique_address) {
			flags = flags | CanonicalRecordMemberFlags::NoUniqueAddress;
		}
		members.push_back({
			.type = member.canonical_type_id,
			.offset_bytes = canonicalLayoutSize(member.offset),
			.size_bytes = canonicalLayoutSize(member.size),
			.bit_width = bit_width,
			.bit_offset = bit_offset,
			.access = canonicalAccess(member.access),
			.flags = flags,
		});
	}
	std::vector<CanonicalRecordBase> bases;
	bases.reserve(struct_info.base_classes.size());
	if (kind == CanonicalTypeKind::Record) {
		for (const BaseClassSpecifier& base : struct_info.base_classes) {
			const EntityId base_entity = resolvePublishedBaseEntity(base);
			if (!base_entity) {
				return false;
			}
			CanonicalRecordBaseFlags flags = CanonicalRecordBaseFlags::None;
			if (base.is_virtual) {
				flags = flags | CanonicalRecordBaseFlags::Virtual;
			}
			bases.push_back({
				.entity = base_entity,
				.offset_bytes = canonicalLayoutSize(base.offset),
				.access = canonicalAccess(base.access),
				.flags = flags,
			});
		}
	}
	table.publishRecordFieldSchema(owner, members, bases);
	return true;
}

enum class CanonicalClassSchemaPublication : uint8_t { BasesOnly, WithObjectFacts };

inline bool tryPublishCanonicalClassBaseSchema(CanonicalTypeTable& table, TypeId root_type, const StructTypeInfo& root_struct_info,
	CanonicalClassSchemaPublication publication) {
	const auto is_class_type = [&table](TypeId type) {
		const CanonicalTypeKind kind = table.node(type).kind;
		return isCanonicalClassTypeKind(kind);
	};
	if (!root_type || !is_class_type(root_type)) {
		return false;
	}

	struct PendingClass {
		TypeId type;
		const StructTypeInfo* struct_info;
	};
	CanonicalTypeTransaction transaction(table);
	std::vector<PendingClass> worklist;
	std::unordered_set<uint32_t> visited;
	worklist.push_back({root_type, &root_struct_info});
	while (!worklist.empty()) {
		const PendingClass current = worklist.back();
		worklist.pop_back();
		if (!visited.insert(current.type.value).second) {
			continue;
		}
		if (current.struct_info == nullptr || !current.struct_info->layout_is_complete || current.struct_info->has_deferred_base_classes) {
			return false;
		}
		if (publication == CanonicalClassSchemaPublication::WithObjectFacts) {
			(void)tryPublishCanonicalRecordSubobjectTypes(table, current.type, *current.struct_info);
			if (!tryPublishCanonicalRecordProperties(table, current.type, *current.struct_info)) {
				return false;
			}
		}
		if (publication == CanonicalClassSchemaPublication::WithObjectFacts && table.hasRecordSubobjectTypes(current.type)) {
			const size_t member_count = table.recordSubobjectMemberCount(current.type);
			if (member_count != current.struct_info->members.size()) {
				throw InternalError("canonical type: record subobject type schema count mismatch");
			}
			for (size_t index = 0; index < member_count; ++index) {
				const StructMember& member = current.struct_info->members[index];
				if (member.pointer_depth > 0 || member.is_reference()) {
					continue;
				}
				TypeId member_type = table.recordSubobjectMemberAt(current.type, index);
				CanonicalTypeNode member_node = table.node(member_type);
				while (member_node.kind == CanonicalTypeKind::Qualified || member_node.kind == CanonicalTypeKind::Array) {
					member_type = member_node.child;
					member_node = table.node(member_type);
				}
				if (!isCanonicalClassTypeKind(member_node.kind)) {
					continue;
				}
				const TypeInfo* member_type_info = tryGetTypeInfo(member.type_index);
				const StructTypeInfo* member_struct_info = member_type_info == nullptr ? nullptr : canonicalClassStructInfoFromTypeInfo(*member_type_info);
				if (member_struct_info == nullptr) {
					continue;
				}
				const bool has_member_schemas = table.hasRecordProperties(member_type) && table.hasClassBaseSchema(member_type) &&
					table.hasRecordSubobjectTypes(member_type);
				if (!has_member_schemas) {
					worklist.push_back({member_type, member_struct_info});
				}
			}
		}
		const bool has_base_schema = table.hasClassBaseSchema(current.type);

		std::vector<CanonicalClassBase> bases;
		bases.reserve(current.struct_info->base_classes.size());
		for (const BaseClassSpecifier& base : current.struct_info->base_classes) {
			if (base.is_deferred || !base.type_index.is_valid() || base.offset > std::numeric_limits<uint32_t>::max()) {
				return false;
			}
			const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
			if (base_type_info == nullptr) {
				return false;
			}
			const CanonicalTypeImport imported_base = importCanonicalClassTypeInfo(table, *base_type_info);
			if (imported_base.status != CanonicalTypeImportStatus::Supported || !is_class_type(imported_base.type)) {
				return false;
			}

			CanonicalAccess access = CanonicalAccess::Public;
			switch (base.access) {
			case AccessSpecifier::Public:
				access = CanonicalAccess::Public;
				break;
			case AccessSpecifier::Protected:
				access = CanonicalAccess::Protected;
				break;
			case AccessSpecifier::Private:
				access = CanonicalAccess::Private;
				break;
			default:
				return false;
			}
			CanonicalRecordBaseFlags flags = CanonicalRecordBaseFlags::None;
			if (base.is_virtual) {
				flags = flags | CanonicalRecordBaseFlags::Virtual;
			}
			bases.push_back({
				imported_base.type,
				access,
				flags,
				0,
				static_cast<uint32_t>(base.offset)});

			const bool has_base_object_facts = table.hasRecordProperties(imported_base.type) && table.hasRecordSubobjectTypes(imported_base.type);
			if (!table.hasClassBaseSchema(imported_base.type) ||
				(publication == CanonicalClassSchemaPublication::WithObjectFacts && !has_base_object_facts)) {
				const StructTypeInfo* base_struct_info = canonicalClassStructInfoFromTypeInfo(*base_type_info);
				if (base_struct_info == nullptr) {
					return false;
				}
				worklist.push_back({imported_base.type, base_struct_info});
			}
		}
		if (!has_base_schema) {
			table.publishClassBaseSchema(current.type, bases);
		}

		if (publication == CanonicalClassSchemaPublication::WithObjectFacts && table.node(current.type).kind != CanonicalTypeKind::Record) {
			(void)tryPublishCanonicalRecordFieldSchema(table, current.type, *current.struct_info);
		}

		const bool has_virtual_base_schema = table.hasVirtualBaseSchema(current.type);
		if (!has_virtual_base_schema) {
			InlineVector<CanonicalVirtualBase, FlashCpp::InlineVectorSpillFamily::ClassLayout, 4> virtual_bases;
			virtual_bases.reserve(current.struct_info->virtual_bases.size());
			for (const BaseClassSpecifier& virtual_base : current.struct_info->virtual_bases) {
				if (virtual_base.is_deferred || !virtual_base.type_index.is_valid() || virtual_base.offset > std::numeric_limits<uint32_t>::max()) {
					return false;
				}
				const TypeInfo* virtual_base_type_info = tryGetTypeInfo(virtual_base.type_index);
				if (virtual_base_type_info == nullptr) {
					return false;
				}
				const CanonicalTypeImport imported_virtual_base = importCanonicalClassTypeInfo(table, *virtual_base_type_info);
				if (imported_virtual_base.status != CanonicalTypeImportStatus::Supported || !is_class_type(imported_virtual_base.type)) {
					return false;
				}
				virtual_bases.push_back({imported_virtual_base.type, static_cast<uint32_t>(virtual_base.offset)});

				const bool has_base_object_facts = table.hasRecordProperties(imported_virtual_base.type) &&
					table.hasRecordSubobjectTypes(imported_virtual_base.type);
				if (!table.hasClassBaseSchema(imported_virtual_base.type) ||
					(publication == CanonicalClassSchemaPublication::WithObjectFacts && !has_base_object_facts)) {
					const StructTypeInfo* base_struct_info = canonicalClassStructInfoFromTypeInfo(*virtual_base_type_info);
					if (base_struct_info == nullptr) {
						return false;
					}
					worklist.push_back({imported_virtual_base.type, base_struct_info});
				}
			}
			table.publishVirtualBaseSchema(current.type, virtual_bases);
		}
	}
	transaction.commit();
	return true;
}

inline bool tryPublishCanonicalClassBaseSchema(CanonicalTypeTable& table, TypeId root_type, const StructTypeInfo& root_struct_info) {
	return tryPublishCanonicalClassBaseSchema(table, root_type, root_struct_info, CanonicalClassSchemaPublication::WithObjectFacts);
}

inline bool tryPublishCanonicalClassBaseSchema(
	CanonicalTypeTable& table,
	TypeId class_type,
	const TypeInfo& type_info) {
	const StructTypeInfo* struct_info =
		canonicalClassStructInfoFromTypeInfo(type_info);
	return struct_info != nullptr &&
		tryPublishCanonicalClassBaseSchema(table, class_type, *struct_info);
}

inline CanonicalTypeImport importCanonicalFunctionParameterType(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	return importCanonicalTypeImpl(table, syntax, CanonicalTypeImportContext::FunctionParameter);
}

// Iteratively decompose the canonical wrapper chain into syntax order. The
// returned base retains base cv qualification. Callable payloads remain on the
// outer signature and its structured return signatures. Supplying stop_at_base
// preserves a semantic alias as the base instead of decomposing its wrappers.
inline CanonicalDeclaratorExport exportCanonicalDeclaratorUntil(
	const CanonicalTypeTable& table,
	TypeId type,
	TypeId stop_at_base) {
	CanonicalDeclaratorExport result{type, {}, CanonicalTypeImportStatus::Supported};
	CVQualifier pending_pointer_cv = CVQualifier::None;
	for (;;) {
		if (stop_at_base && type == stop_at_base) {
			result.base = type;
			return result;
		}
		const CanonicalTypeNode node = table.node(type);
		if (node.kind == CanonicalTypeKind::Qualified) {
			const CanonicalTypeNode child = table.node(node.child);
			if (child.kind != CanonicalTypeKind::Pointer &&
				child.kind != CanonicalTypeKind::MemberObjectPointer &&
				child.kind != CanonicalTypeKind::MemberFunctionPointer) {
				result.base = type;
				if (stop_at_base) {
					result.status = CanonicalTypeImportStatus::Unresolved;
				}
				return result;
			}
			pending_pointer_cv |= node.qualifiers;
			type = node.child;
			continue;
		}
		switch (node.kind) {
		case CanonicalTypeKind::Pointer:
			result.components.push_back(
				DeclaratorComponent::pointer(pending_pointer_cv));
			pending_pointer_cv = CVQualifier::None;
			type = node.child;
			break;
		case CanonicalTypeKind::LValueReference:
			result.components.push_back(DeclaratorComponent::lvalueReference());
			type = node.child;
			break;
		case CanonicalTypeKind::RValueReference:
			result.components.push_back(DeclaratorComponent::rvalueReference());
			type = node.child;
			break;
		case CanonicalTypeKind::Array:
			result.components.push_back(
				hasCanonicalTypeNodeFlag(
					node.flags, CanonicalTypeNodeFlags::KnownArrayBound)
					? DeclaratorComponent::array(
						static_cast<size_t>(node.array_extent))
					: DeclaratorComponent::unknownBoundArray());
			type = node.child;
			break;
		case CanonicalTypeKind::Function:
			// Nested signatures follow the enclosing return FunctionType's
			// callable_signature, in the same outermost-first order as this spine.
			result.components.push_back(DeclaratorComponent::function());
			type = node.child;
			break;
		case CanonicalTypeKind::MemberObjectPointer: {
			const TypeId owner = table.memberPointerOwner(type);
			const CanonicalTypeNode owner_node = table.node(owner);
			if (owner_node.kind == CanonicalTypeKind::Record) {
				result.components.push_back(DeclaratorComponent::memberPointer(
					table.recordEntity(owner), false, pending_pointer_cv));
			} else if (isCanonicalClassTypeKind(owner_node.kind)) {
				result.components.push_back(DeclaratorComponent::memberPointer(
					owner, false, pending_pointer_cv));
			} else {
				result.base = type;
				result.status = CanonicalTypeImportStatus::UnmigratedCallable;
				return result;
			}
			pending_pointer_cv = CVQualifier::None;
			type = node.child;
			break;
		}
		case CanonicalTypeKind::MemberFunctionPointer: {
			const TypeId owner = table.memberPointerOwner(type);
			const CanonicalTypeNode owner_node = table.node(owner);
			if (owner_node.kind == CanonicalTypeKind::Record) {
				result.components.push_back(DeclaratorComponent::memberPointer(
					table.recordEntity(owner), true, pending_pointer_cv));
			} else if (isCanonicalClassTypeKind(owner_node.kind)) {
				result.components.push_back(DeclaratorComponent::memberPointer(
					owner, true, pending_pointer_cv));
			} else {
				result.base = type;
				result.status = CanonicalTypeImportStatus::UnmigratedCallable;
				return result;
			}
			pending_pointer_cv = CVQualifier::None;
			// The function payload is carried out-of-band by
			// CanonicalTypeDesc::function_signature; stop at the function type so
			// no extra Function component is emitted.
			result.base = node.child;
			if (stop_at_base && node.child != stop_at_base) {
				result.status = CanonicalTypeImportStatus::Unresolved;
			}
			return result;
		}
		default:
			result.base = type;
			if (stop_at_base) {
				result.status = CanonicalTypeImportStatus::Unresolved;
			}
			return result;
		}
	}
}

inline CanonicalDeclaratorExport exportCanonicalDeclarator(
	const CanonicalTypeTable& table,
	TypeId type) {
	return exportCanonicalDeclaratorUntil(table, type, TypeId{});
}
