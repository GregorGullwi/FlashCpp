#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"

enum class CanonicalTypeImportStatus : uint8_t {
	Supported, UnmigratedArray, UnmigratedCallable, UnmigratedNominal, Unresolved, Invalid,
};

struct CanonicalTypeImport {
	TypeId type;
	CanonicalTypeImportStatus status;
};

static_assert(sizeof(CanonicalTypeImport) == 8);

enum class CanonicalTypeImportContext : uint8_t {
	Exact, FunctionParameter,
};

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

// Shared pointer/array/reference shaping after a base TypeId and top-level cv
// have been formed. Keeps parameter-array decay in one mutation-tested place.
inline TypeId applyCanonicalPointerArrayReference(CanonicalTypeTable& table, TypeId id,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context,
	bool has_ordinary_array, bool has_pointee_array) {
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
	spec.set_reference_qualifier(type.reference_qualifier);
	for (const CVQualifier pointer_cv : type.pointer_qualifiers) {
		spec.add_pointer_level(pointer_cv);
	}
	if (!type.array_dimensions.empty()) {
		spec.set_array_dimensions(type.array_dimensions);
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

inline CanonicalTypeImport importCanonicalFunctionTypeComponent(CanonicalTypeTable& table,
	const FunctionType& type, CanonicalTypeImportContext context) {
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
inline CanonicalTypeImport importCanonicalRecord(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	const EntityId entity = resolveNamedTypeEntity(syntax);
	if (!entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	auto id = table.record(entity);
	id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

inline CanonicalTypeImport importCanonicalEnum(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	const EntityId entity = resolveNamedTypeEntity(syntax);
	if (!entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	auto id = table.enumeration(entity);
	id = addCanonicalPointerLevels(table, id, syntax.pointer_levels());
	id = table.qualify(id, syntax.cv_qualifier());
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
	const EntityId owner_entity = resolveMemberClassEntity(syntax);
	if (!owner_entity) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	const TypeId owner = table.record(owner_entity);
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
		auto id = table.memberFunctionPointer(owner, imported_function.type);
		id = table.qualify(id, syntax.cv_qualifier());
		if (syntax.reference_qualifier() != ReferenceQualifier::None) {
			id = table.reference(id, syntax.reference_qualifier());
		}
		return {id, CanonicalTypeImportStatus::Supported};
	}
	if (syntax.category() == TypeCategory::MemberObjectPointer) {
		// Cast/MOP forms overwrite the pointee category; recover it later.
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	if (!syntax.has_member_class()) {
		return {{}, CanonicalTypeImportStatus::UnmigratedCallable};
	}
	TypeSpecifierNode pointee = syntax;
	pointee.clear_member_class_identity();
	pointee.clear_injected_class_declaration();
	pointee.limit_pointer_depth(0);
	const auto imported_pointee = importCanonicalTypeImpl(
		table, pointee, CanonicalTypeImportContext::Exact);
	if (imported_pointee.status != CanonicalTypeImportStatus::Supported) {
		return imported_pointee;
	}
	auto id = table.memberObjectPointer(owner, imported_pointee.type);
	id = table.qualify(id, syntax.cv_qualifier());
	if (syntax.reference_qualifier() != ReferenceQualifier::None) {
		id = table.reference(id, syntax.reference_qualifier());
	}
	return {id, CanonicalTypeImportStatus::Supported};
}

// Free-function and function-pointer shapes.
inline CanonicalTypeImport importCanonicalCallable(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
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
	if (static_cast<uint8_t>(syntax.cv_qualifier()) > 3 ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference)) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (static_cast<uint8_t>(pointer.cv_qualifier) > 3) {
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
	if (static_cast<uint8_t>(syntax.cv_qualifier()) > 3 ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference)) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (static_cast<uint8_t>(pointer.cv_qualifier) > 3) {
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
		const ExprId expr = syntax.specialization_arg_expr(index);
		if (!expr) {
			return {{}, CanonicalTypeImportStatus::Unresolved};
		}
		argument_ids.push_back(CanonicalTemplateArgument::makeNonType(expr));
	}
	auto id = table.templateSpecialization(syntax.specialization_template_decl(), argument_ids);
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
			base_kind != CanonicalTypeKind::DependentTemplateMember) {
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
			? importCanonicalRecord(table, syntax)
			: importCanonicalEnum(table, syntax);
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
	if (static_cast<uint8_t>(syntax.cv_qualifier()) > 3 ||
		(reference != ReferenceQualifier::None && reference != ReferenceQualifier::LValueReference &&
			reference != ReferenceQualifier::RValueReference) ||
		(builtin == CanonicalBuiltinKind::Void && !syntax.is_pointer() && syntax.is_reference())) {
		return {{}, CanonicalTypeImportStatus::Invalid};
	}
	for (const auto& pointer : syntax.pointer_levels()) {
		if (static_cast<uint8_t>(pointer.cv_qualifier) > 3) {
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

inline CanonicalTypeImport importCanonicalFunctionParameterType(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax) {
	return importCanonicalTypeImpl(table, syntax, CanonicalTypeImportContext::FunctionParameter);
}
