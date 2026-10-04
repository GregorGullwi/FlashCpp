#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"
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

CanonicalRecordPropertyFlags computeCanonicalRecordPropertyFlags(
	const StructTypeInfo& struct_info);

struct CanonicalDeclaratorExport {
	TypeId base;
	std::vector<DeclaratorComponent> components;
	CanonicalTypeImportStatus status;
};

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
			// One cold signature per declarator. The components inside this
			// wrapper are the return type (`id` has already been built from
			// them); parameters come from that signature.
			if (!syntax.has_function_signature()) {
				return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
			}
			size_t function_count = 0;
			for (const DeclaratorComponent& other : components) {
				if (other.kind == DeclaratorComponentKind::Function) {
					++function_count;
				}
			}
			if (function_count != 1) {
				return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
			}
			const FunctionSignature& signature = syntax.function_signature();
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
			if (index != components.size() - 1 || !component.hasMemberOwner() ||
				!isValidCVQualifier(component.cv_qualifier) ||
				!syntax.has_function_signature()) {
				return {{}, CanonicalTypeImportStatus::Invalid};
			}
			FunctionSignature signature = syntax.function_signature();
			signature.class_name = {};
			const CanonicalTypeImport imported_function =
				importCanonicalFunctionSignature(table, signature);
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
		if (syntax.array_dimensions().empty()) {
			id = table.arrayOfUnknownBound(id);
		} else {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
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
	if (type.template_parameter_name.isValid()) {
		spec.set_template_parameter_identity(type.template_parameter_name);
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

inline CanonicalTypeImport importCanonicalFunctionTypeComponent(CanonicalTypeTable& table,
	const FunctionType& type, CanonicalTypeImportContext context) {
	return importCanonicalTypeImpl(
		table,
		typeSpecifierFromFunctionType(type),
		context);
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
inline CanonicalTypeImport importCanonicalRecord(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax,
	CanonicalTypeImportContext context) {
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
	if ((!has_ordinary_array && !has_pointee_array) ||
		syntax.has_unsized_outer_array_dimension() || syntax.array_dimensions().empty()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	for (const size_t extent : syntax.array_dimensions()) {
		if (extent == 0) {
			return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
		}
	}
	if (has_pointee_array && syntax.pointer_levels().empty()) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	CanonicalTypeTransaction transaction(table);
	auto id = is_record ? table.record(entity) : table.enumeration(entity);
	id = table.qualify(id, syntax.cv_qualifier());
	if (has_pointee_array) {
		id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
		id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	} else {
		id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
		if (context == CanonicalTypeImportContext::FunctionParameter &&
			syntax.reference_qualifier() == ReferenceQualifier::None) {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 1);
			id = table.pointer(id);
		} else {
			id = addCanonicalArrayDimensions(table, id, syntax.array_dimensions(), 0);
		}
	}
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
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
	const FunctionSignature& signature) {
	// Retained noexcept(expr) needs a published ExprId before canonical import.
	if (signature.noexcept_expression.has_value() && !signature.dependent_noexcept) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}
	if (signature.is_noexcept && signature.dependent_noexcept) {
		return {{}, CanonicalTypeImportStatus::Unresolved};
	}

	// Structured return uses FunctionType; otherwise recover from the flat
	// TypeIndex projection that older signature writers still publish.
	const CanonicalTypeImport imported_return = signature.hasStructuredTypes()
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
	const std::span<const FunctionType> structured_parameters = signature.parameter_types();
	if (!structured_parameters.empty()) {
		parameters.reserve(structured_parameters.size());
		for (const FunctionType& parameter : structured_parameters) {
			const auto imported_parameter = importCanonicalFunctionTypeComponent(
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
			const auto imported_parameter = importCanonicalFunctionComponentFromProjection(
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
		const DeclaratorComponent& innermost = components.back();
		if (innermost.kind == DeclaratorComponentKind::MemberFunctionPointer) {
			return applyCanonicalOrderedDeclarator(table, TypeId{}, syntax,
				CanonicalTypeImportContext::Exact);
		}
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
		const auto imported_function = importCanonicalFunctionSignature(table, signature);
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
				component.kind == DeclaratorComponentKind::Function;
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
	const auto imported_function = importCanonicalFunctionSignature(table, signature);
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
	// [dcl.fct] parameter adjustment: function type becomes pointer to function.
	if (context == CanonicalTypeImportContext::FunctionParameter &&
		table.node(table.withoutTopLevelQualifiers(id)).kind == CanonicalTypeKind::Function) {
		id = table.pointer(id);
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
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array) ||
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
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array) ||
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
			(syntax.has_unsized_outer_array_dimension() && !has_ordinary_array) ||
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

inline CanonicalTypeImport importCanonicalStructuralTraitOperand(
	CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	const CanonicalTypeImport imported = importCanonicalType(table, syntax);
	if (imported.status != CanonicalTypeImportStatus::UnmigratedNominal ||
		!syntax.type_index().is_valid()) {
		return imported;
	}
	const TypeInfo* type_info = tryGetTypeInfo(syntax.type_index());
	if (type_info == nullptr) {
		return imported;
	}
	const CanonicalTypeImport imported_class =
		importCanonicalClassTypeInfo(table, *type_info);
	if (imported_class.status != CanonicalTypeImportStatus::Supported) {
		return imported_class;
	}
	auto id = table.qualify(imported_class.type, syntax.cv_qualifier());
	if (syntax.has_ordered_declarator()) {
		return applyCanonicalOrderedDeclarator(
			table, id, syntax, CanonicalTypeImportContext::Exact);
	}
	id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
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

inline CanonicalTypeImport importCanonicalClassSource(
	CanonicalTypeTable& table,
	const TypeInfo* root_type_info,
	const StructDeclarationNode* root_declaration) {
	if ((root_type_info == nullptr) == (root_declaration == nullptr)) {
		throw InternalError("canonical type adapter: class source must have one root");
	}
	struct PendingClassImport {
		const StructDeclarationNode* declaration;
		const StructDeclarationNode* pattern;
		size_t next_argument = 0;
		size_t waiting_argument = std::numeric_limits<size_t>::max();
		std::vector<CanonicalTemplateArgument> arguments;
	};

	CanonicalTypeTransaction transaction(table);
	std::vector<PendingClassImport> worklist;
	std::unordered_set<const StructDeclarationNode*> active_declarations;
	const auto enqueue_source = [&](
		const TypeInfo* type_info,
		const StructDeclarationNode* declaration)
		-> std::optional<CanonicalTypeImport> {
		if (type_info != nullptr) {
			if (const TypeSpecifierNode* alias_type =
					type_info->aliasTypeSpecifier()) {
				return importCanonicalType(table, *alias_type);
			}
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info == nullptr || struct_info->declaration_node == nullptr) {
				return CanonicalTypeImport{{},
					CanonicalTypeImportStatus::UnmigratedNominal};
			}
			declaration = struct_info->declaration_node;
		}

		const StructDeclarationNode* pattern =
			declaration->injected_class_pattern_declaration();
		if (pattern == nullptr || !pattern->has_template_decl_id()) {
			if (declaration->has_entity_id()) {
				return CanonicalTypeImport{
					table.record(declaration->entity_id()),
					CanonicalTypeImportStatus::Supported};
			}
			return CanonicalTypeImport{{},
				CanonicalTypeImportStatus::UnmigratedNominal};
		}
		if (!active_declarations.insert(declaration).second) {
			return CanonicalTypeImport{{}, CanonicalTypeImportStatus::Unresolved};
		}
		PendingClassImport pending{};
		pending.declaration = declaration;
		pending.pattern = pattern;
		pending.arguments.reserve(declaration->outer_template_args().size());
		worklist.push_back(std::move(pending));
		return std::nullopt;
	};

	std::optional<CanonicalTypeImport> completed =
		enqueue_source(root_type_info, root_declaration);
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
			if (parent.waiting_argument == std::numeric_limits<size_t>::max()) {
				throw InternalError("canonical type adapter: class import result has no parent");
			}
			if (completed->status != CanonicalTypeImportStatus::Supported) {
				return *completed;
			}
			const TypeInfo::TemplateArgInfo& argument =
				parent.declaration->outer_template_args()[parent.waiting_argument];
			const CanonicalTypeImport imported_argument =
				shapeCanonicalTemplateTypeArgument(
					table, argument, completed->type);
			if (imported_argument.status != CanonicalTypeImportStatus::Supported) {
				return imported_argument;
			}
			parent.arguments.push_back(
				CanonicalTemplateArgument::makeType(imported_argument.type));
			parent.next_argument = parent.waiting_argument + 1;
			parent.waiting_argument = std::numeric_limits<size_t>::max();
			completed.reset();
			continue;
		}

		if (worklist.empty()) {
			throw InternalError("canonical type adapter: class import worklist ended early");
		}
		PendingClassImport& current = worklist.back();
		const auto& source_arguments = current.declaration->outer_template_args();
		if (current.next_argument < source_arguments.size()) {
			const size_t argument_index = current.next_argument;
			const TypeInfo::TemplateArgInfo& argument =
				source_arguments[argument_index];
			const CanonicalTypeImport direct_import =
				importCanonicalTemplateTypeArgumentDirect(table, argument);
			if (direct_import.status == CanonicalTypeImportStatus::Supported) {
				current.arguments.push_back(
					CanonicalTemplateArgument::makeType(direct_import.type));
				++current.next_argument;
				continue;
			}
			if (!argument.type_index.is_valid()) {
				return direct_import;
			}
			const TypeInfo* argument_type_info =
				tryGetTypeInfo(argument.type_index);
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

		const TypeId specialization = table.templateSpecialization(
			current.pattern->template_decl_id(), current.arguments);
		active_declarations.erase(current.declaration);
		worklist.pop_back();
		completed = CanonicalTypeImport{
			specialization, CanonicalTypeImportStatus::Supported};
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
	if (kind != CanonicalTypeKind::Record &&
		kind != CanonicalTypeKind::TemplateSpecialization) {
		return false;
	}
	const CanonicalRecordPropertyFlags flags =
		computeCanonicalRecordPropertyFlags(struct_info);
	table.publishRecordProperties(type, flags);
	return true;
}

inline bool tryPublishCanonicalClassBaseSchema(
	CanonicalTypeTable& table,
	TypeId root_type,
	const StructTypeInfo& root_struct_info) {
	const auto is_class_type = [&table](TypeId type) {
		const CanonicalTypeKind kind = table.node(type).kind;
		return kind == CanonicalTypeKind::Record ||
			kind == CanonicalTypeKind::TemplateSpecialization;
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
		if (current.struct_info == nullptr ||
			!current.struct_info->layout_is_complete ||
			current.struct_info->has_deferred_base_classes) {
			return false;
		}
		if (!tryPublishCanonicalRecordProperties(
				table, current.type, *current.struct_info)) {
			return false;
		}
		if (table.hasClassBaseSchema(current.type)) {
			continue;
		}

		std::vector<CanonicalClassBase> bases;
		bases.reserve(current.struct_info->base_classes.size());
		for (const BaseClassSpecifier& base : current.struct_info->base_classes) {
			if (base.is_deferred || !base.type_index.is_valid()) {
				return false;
			}
			const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
			if (base_type_info == nullptr) {
				return false;
			}
			const CanonicalTypeImport imported_base =
				importCanonicalClassTypeInfo(table, *base_type_info);
			if (imported_base.status != CanonicalTypeImportStatus::Supported ||
				!is_class_type(imported_base.type)) {
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
			bases.push_back({imported_base.type, access, flags, 0});

			if (!table.hasClassBaseSchema(imported_base.type)) {
				const StructTypeInfo* base_struct_info =
					canonicalClassStructInfoFromTypeInfo(*base_type_info);
				if (base_struct_info == nullptr) {
					return false;
				}
				worklist.push_back({imported_base.type, base_struct_info});
			}
		}
		table.publishClassBaseSchema(current.type, bases);
	}
	transaction.commit();
	return true;
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
// returned base retains base cv qualification. Callable/member-pointer export
// remains fail-closed until their AST payloads migrate from FunctionSignature.
// Supplying stop_at_base preserves a semantic alias as the base when nested
// callable nodes cannot be represented by one top-level declarator signature.
inline CanonicalDeclaratorExport exportCanonicalDeclaratorUntil(
	const CanonicalTypeTable& table,
	TypeId type,
	TypeId stop_at_base) {
	CanonicalDeclaratorExport result{type, {}, CanonicalTypeImportStatus::Supported};
	CVQualifier pending_pointer_cv = CVQualifier::None;
	bool saw_function = false;
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
			// A second function type has no second cold signature to recover.
			if (saw_function) {
				result.base = type;
				result.status = CanonicalTypeImportStatus::UnmigratedCallable;
				return result;
			}
			saw_function = true;
			result.components.push_back(DeclaratorComponent::function());
			type = node.child;
			break;
		case CanonicalTypeKind::MemberObjectPointer: {
			const TypeId owner = table.memberPointerOwner(type);
			const CanonicalTypeNode owner_node = table.node(owner);
			if (owner_node.kind == CanonicalTypeKind::Record) {
				result.components.push_back(DeclaratorComponent::memberPointer(
					table.recordEntity(owner), false, pending_pointer_cv));
			} else if (owner_node.kind == CanonicalTypeKind::TemplateSpecialization) {
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
			} else if (owner_node.kind == CanonicalTypeKind::TemplateSpecialization) {
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
