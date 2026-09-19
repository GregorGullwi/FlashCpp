#pragma once

#include <array>
#include <cstdio>
#include <source_location>
#include <sstream>
#include <type_traits>
#include <vector>

#include "CanonicalTypeAdapter.h"
#include "TemplateDeclTable.h"

namespace CanonicalTypeTests {

inline void require(bool condition,
	std::source_location location = std::source_location::current()) {
	if (!condition) {
		std::fprintf(stderr, "canonical type regression failed at %s:%u\n",
			location.file_name(), location.line());
		throw InternalError("canonical type regression failed");
	}
}

// Context-local numeric slots are never compared across compilations.
inline bool sameStructure(const CanonicalTypeTable& left, TypeId left_id,
	const CanonicalTypeTable& right, TypeId right_id) {
	for (;;) {
		const auto a = left.node(left_id);
		const auto b = right.node(right_id);
		if (a.kind != b.kind || a.builtin != b.builtin || a.qualifiers != b.qualifiers ||
			a.flags != b.flags) {
			return false;
		}
		if (a.kind == CanonicalTypeKind::Array && a.array_extent != b.array_extent) {
			return false;
		}
		if (a.kind == CanonicalTypeKind::DependentName &&
			left.dependentNameIdentifier(left_id) != right.dependentNameIdentifier(right_id)) {
			return false;
		}
		if (a.kind == CanonicalTypeKind::DependentTemplateMember) {
			if (left.dependentNameIdentifier(left_id) != right.dependentNameIdentifier(right_id)) {
				return false;
			}
			TypeId left_arg = left.dependentTemplateMemberArguments(left_id);
			TypeId right_arg = right.dependentTemplateMemberArguments(right_id);
			while (left_arg || right_arg) {
				if (!left_arg || !right_arg) {
					return false;
				}
				if (!sameStructure(left, left.templateArgumentType(left_arg),
					right, right.templateArgumentType(right_arg))) {
					return false;
				}
				left_arg = left.templateArgumentNext(left_arg);
				right_arg = right.templateArgumentNext(right_arg);
			}
			left_id = a.child;
			right_id = b.child;
			continue;
		}
		if (a.kind == CanonicalTypeKind::Record || a.kind == CanonicalTypeKind::Enum ||
			a.kind == CanonicalTypeKind::TemplateParameter) {
			return a.array_extent == b.array_extent;
		}
		if (a.kind == CanonicalTypeKind::TemplateSpecialization) {
			if (a.array_extent != b.array_extent) {
				return false;
			}
			TypeId left_arg = left.templateSpecializationArguments(left_id);
			TypeId right_arg = right.templateSpecializationArguments(right_id);
			while (left_arg || right_arg) {
				if (!left_arg || !right_arg) {
					return false;
				}
				const CanonicalTemplateArgKind left_kind = left.templateArgumentKind(left_arg);
				const CanonicalTemplateArgKind right_kind = right.templateArgumentKind(right_arg);
				if (left_kind != right_kind) {
					return false;
				}
				if (left_kind == CanonicalTemplateArgKind::Type) {
					if (!sameStructure(left, left.templateArgumentType(left_arg),
						right, right.templateArgumentType(right_arg))) {
						return false;
					}
				} else if (left_kind == CanonicalTemplateArgKind::NonType &&
					left.templateArgumentExpr(left_arg) !=
					right.templateArgumentExpr(right_arg)) {
					return false;
				} else if (left_kind == CanonicalTemplateArgKind::Template &&
					left.templateArgumentTemplate(left_arg) !=
					right.templateArgumentTemplate(right_arg)) {
					return false;
				} else if (left_kind == CanonicalTemplateArgKind::DependentTemplate &&
					(left.templateArgumentDependentTemplateDecl(left_arg) !=
						right.templateArgumentDependentTemplateDecl(right_arg) ||
					 left.templateArgumentDependentTemplateIndex(left_arg) !=
						right.templateArgumentDependentTemplateIndex(right_arg))) {
					return false;
				}
				left_arg = left.templateArgumentNext(left_arg);
				right_arg = right.templateArgumentNext(right_arg);
			}
			return true;
		}
		if (a.kind == CanonicalTypeKind::Function) {
			if ((a.array_extent >> 32) != (b.array_extent >> 32)) {
				return false;
			}
			TypeId left_param = left.functionParameters(left_id);
			TypeId right_param = right.functionParameters(right_id);
			while (left_param || right_param) {
				if (!left_param || !right_param) {
					return false;
				}
				if (!sameStructure(left, left.functionParameterType(left_param),
					right, right.functionParameterType(right_param))) {
					return false;
				}
				left_param = left.functionParameterNext(left_param);
				right_param = right.functionParameterNext(right_param);
			}
			left_id = a.child;
			right_id = b.child;
			continue;
		}
		if (a.kind == CanonicalTypeKind::MemberObjectPointer ||
			a.kind == CanonicalTypeKind::MemberFunctionPointer) {
			if (!sameStructure(left, left.memberPointerOwner(left_id),
				right, right.memberPointerOwner(right_id))) {
				return false;
			}
			left_id = a.child;
			right_id = b.child;
			continue;
		}
		if (a.kind == CanonicalTypeKind::Builtin) {
			return true;
		}
		if (a.kind == CanonicalTypeKind::FunctionParam) {
			return false;
		}
		left_id = a.child;
		right_id = b.child;
	}
}

template<typename Action>
inline void rejects(Action action) {
	bool rejected = false;
	try {
		action();
	} catch (const InternalError&) {
		rejected = true;
	}
	require(rejected);
}

inline void checkTransactions() {
	SemanticArenaAccounting accounting;
	CanonicalTypeTable table(accounting);
	const auto stable = table.builtin(CanonicalBuiltinKind::Int);
	const auto initial_bytes = table.arenaStats().reserved_bytes;
	for (int repeat = 0; repeat < 4096; ++repeat) {
		{
			CanonicalTypeTransaction outer(table);
			const auto pointer = table.pointer(stable);
			require(table.size() == 2);
			{
				CanonicalTypeTransaction inner(table);
				table.reference(pointer, ReferenceQualifier::LValueReference);
				inner.commit();
			}
			require(table.size() == 3);
			outer.rollback();
		}
		require(table.size() == 1);
		require(table.builtin(CanonicalBuiltinKind::Int) == stable);
	}
	require(table.arenaStats().reserved_bytes == initial_bytes);
	require(accounting.snapshot().current_bytes == sizeof(CanonicalTypeNode));
	require(accounting.snapshot().peak_bytes == 3 * sizeof(CanonicalTypeNode));
	{
		CanonicalTypeTransaction committed(table);
		table.pointer(stable);
		committed.commit();
	}
	require(table.size() == 2);
	SemanticArenaAccounting incremental;
	incremental.update(SemanticArenaComponent::Types, 8, 256);
	incremental.update(SemanticArenaComponent::Types, 16, 256);
	require(incremental.snapshot().peak_bytes == 16);
	SemanticArenaAccounting separate;
	separate.update(SemanticArenaComponent::Declarations, 64, 128);
	separate.update(SemanticArenaComponent::Declarations, 0, 128);
	CanonicalTypeTable later(separate);
	later.builtin(CanonicalBuiltinKind::Double);
	require(separate.snapshot().current_bytes == sizeof(CanonicalTypeNode));
	require(separate.snapshot().peak_bytes == 64);
}

inline void checkAdapter() {
	CanonicalTypeTable table;
	TypeSpecifierNode plain(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	TypeSpecifierNode signed_char(TypeCategory::Char, TypeQualifier::Signed, 8, Token{}, CVQualifier::None);
	require(importCanonicalType(table, plain).type != importCanonicalType(table, signed_char).type);
	TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	integer.add_pointer_level(CVQualifier::Volatile);
	const auto imported = importCanonicalType(table, integer);
	require(imported.status == CanonicalTypeImportStatus::Supported);
	require(imported.type == table.qualify(table.pointer(table.qualify(table.builtin(CanonicalBuiltinKind::Int),
		CVQualifier::Const)), CVQualifier::Volatile));
	TypeSpecifierNode ordinary_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	ordinary_array.add_pointer_level(CVQualifier::None);
	const std::array<size_t, 2> dimensions{2, 3};
	ordinary_array.set_array_dimensions(dimensions);
	const auto ordinary = importCanonicalType(table, ordinary_array);
	require(ordinary.status == CanonicalTypeImportStatus::Supported);
	const auto int_type = table.builtin(CanonicalBuiltinKind::Int);
	const auto pointer_to_int = table.pointer(int_type);
	require(ordinary.type == table.array(table.array(pointer_to_int, 3), 2));
	const auto parameter = importCanonicalFunctionParameterType(table, ordinary_array);
	require(parameter.status == CanonicalTypeImportStatus::Supported);
	require(parameter.type == table.pointer(table.array(pointer_to_int, 3)));

	TypeSpecifierNode pointee_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointee_array.add_pointer_level(CVQualifier::Const);
	pointee_array.set_pointee_array_dimensions(dimensions);
	pointee_array.set_pointee_array_declarator(true);
	const auto pointee = importCanonicalType(table, pointee_array);
	require(pointee.status == CanonicalTypeImportStatus::Supported);
	require(pointee.type == table.qualify(table.pointer(table.array(table.array(int_type, 3), 2)),
		CVQualifier::Const));
	require(pointee.type != ordinary.type);

	TypeSpecifierNode unknown_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	unknown_array.set_array(true);
	unknown_array.set_unsized_outer_array_dimension(true);
	const auto unknown = importCanonicalType(table, unknown_array);
	require(unknown.status == CanonicalTypeImportStatus::Supported);
	require(unknown.type == table.arrayOfUnknownBound(int_type));
	require(unknown.type != table.array(int_type, 1));

	TypeSpecifierNode incomplete_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	incomplete_array.set_array(true);
	const auto before = table.size();
	require(importCanonicalType(table, incomplete_array).status == CanonicalTypeImportStatus::UnmigratedArray);
	require(table.size() == before);
	integer.set_pack_expansion(true);
	require(importCanonicalType(table, integer).status == CanonicalTypeImportStatus::Unresolved);
	integer.set_pack_expansion(false);
	integer.set_category(TypeCategory::FunctionPointer);
	require(importCanonicalType(table, integer).status == CanonicalTypeImportStatus::UnmigratedCallable);
	require(table.size() == before);

	FunctionCallableTypes callable_storage;
	callable_storage.return_type = makeFunctionTypeFromSpecifier(
		TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
	FunctionType short_param = makeFunctionTypeFromSpecifier(
		TypeSpecifierNode(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::Const));
	short_param.pointer_qualifiers.push_back(CVQualifier::None);
	callable_storage.parameter_types.push_back(short_param);
	FunctionSignature signature;
	signature.return_type_index = callable_storage.return_type.type_index;
	signature.return_pointer_depth = 0;
	signature.callable_types = &callable_storage;
	TypeSpecifierNode function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64, Token{},
		CVQualifier::None);
	function_pointer.set_function_signature(signature);
	const auto imported_function = importCanonicalType(table, function_pointer);
	require(imported_function.status == CanonicalTypeImportStatus::Supported);
	const auto short_const = table.qualify(table.builtin(CanonicalBuiltinKind::Short), CVQualifier::Const);
	const TypeId expected_params[] = {table.pointer(short_const)};
	require(imported_function.type == table.pointer(table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{})));

	FunctionSignature member_signature = signature;
	member_signature.is_const = true;
	member_signature.function_reference_qualifier = ReferenceQualifier::LValueReference;
	TypeSpecifierNode member_function(TypeCategory::Function, TypeQualifier::None, 64, Token{},
		CVQualifier::None);
	member_function.set_function_signature(member_signature);
	const auto imported_member = importCanonicalType(table, member_function);
	require(imported_member.status == CanonicalTypeImportStatus::Supported);
	require(imported_member.type == table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::Const,
		ReferenceQualifier::LValueReference, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}));
	require(imported_member.type != table.withoutTopLevelQualifiers(imported_function.type));
	const auto imported_as_parameter = importCanonicalFunctionParameterType(table, member_function);
	require(imported_as_parameter.status == CanonicalTypeImportStatus::Supported);
	require(imported_as_parameter.type == table.pointer(imported_member.type));

	signature.is_noexcept = true;
	TypeSpecifierNode noexcept_function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	noexcept_function_pointer.set_function_signature(signature);
	const auto imported_noexcept = importCanonicalType(table, noexcept_function_pointer);
	require(imported_noexcept.status == CanonicalTypeImportStatus::Supported);
	require(imported_noexcept.type == table.pointer(table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::None,
		ReferenceQualifier::None, true, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{})));
	require(imported_noexcept.type != imported_function.type);
	signature.is_noexcept = false;
	signature.calling_convention = CallingConvention::Stdcall;
	TypeSpecifierNode stdcall_function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	stdcall_function_pointer.set_function_signature(signature);
	const auto imported_stdcall = importCanonicalType(table, stdcall_function_pointer);
	require(imported_stdcall.status == CanonicalTypeImportStatus::Supported);
	require(imported_stdcall.type == table.pointer(table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Stdcall,
		CanonicalDllLinkage::None, ExprId{})));
	require(imported_stdcall.type != imported_function.type);
	signature.calling_convention = CallingConvention::Default;
	signature.linkage = Linkage::DllImport;
	TypeSpecifierNode dllimport_function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	dllimport_function_pointer.set_function_signature(signature);
	const auto imported_dllimport = importCanonicalType(table, dllimport_function_pointer);
	require(imported_dllimport.status == CanonicalTypeImportStatus::Supported);
	require(imported_dllimport.type == table.pointer(table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::Import, ExprId{})));
	require(imported_dllimport.type != imported_function.type);
	signature.linkage = Linkage::None;

	signature.dependent_noexcept = ExprId{9};
	TypeSpecifierNode dependent_noexcept_function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	dependent_noexcept_function_pointer.set_function_signature(signature);
	const auto imported_dependent_noexcept = importCanonicalType(table, dependent_noexcept_function_pointer);
	require(imported_dependent_noexcept.status == CanonicalTypeImportStatus::Supported);
	require(imported_dependent_noexcept.type == table.pointer(table.function(
		table.builtin(CanonicalBuiltinKind::Int), expected_params, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{9})));
	require(imported_dependent_noexcept.type != imported_function.type);
	require(imported_dependent_noexcept.type != imported_noexcept.type);
	signature.dependent_noexcept = ExprId{};
	signature.noexcept_expression = ExpressionHandle(ASTNode{});
	TypeSpecifierNode unresolved_dependent_noexcept(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	unresolved_dependent_noexcept.set_function_signature(signature);
	require(importCanonicalType(table, unresolved_dependent_noexcept).status ==
		CanonicalTypeImportStatus::Unresolved);
	signature.noexcept_expression.reset();

	FunctionSignature unstructured_signature;
	unstructured_signature.return_type_index = TypeIndex{0, TypeCategory::Int};
	unstructured_signature.return_pointer_depth = 1;
	unstructured_signature.parameter_type_indices.push_back(TypeIndex{0, TypeCategory::Short});
	unstructured_signature.parameter_type_indices.push_back(TypeIndex{0, TypeCategory::Double});
	unstructured_signature.is_noexcept = true;
	unstructured_signature.calling_convention = CallingConvention::Cdecl;
	require(!unstructured_signature.hasStructuredTypes());
	TypeSpecifierNode unstructured_function_pointer(TypeCategory::FunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	unstructured_function_pointer.set_function_signature(unstructured_signature);
	const auto imported_unstructured = importCanonicalType(table, unstructured_function_pointer);
	require(imported_unstructured.status == CanonicalTypeImportStatus::Supported);
	const TypeId unstructured_params[] = {
		table.builtin(CanonicalBuiltinKind::Short),
		table.builtin(CanonicalBuiltinKind::Double),
	};
	require(imported_unstructured.type == table.pointer(table.function(
		table.pointer(table.builtin(CanonicalBuiltinKind::Int)), unstructured_params, false,
		CVQualifier::None, ReferenceQualifier::None, true, CanonicalCallingConvention::Cdecl,
		CanonicalDllLinkage::None, ExprId{})));
	require(imported_unstructured.type != imported_function.type);

	FunctionSignature hybrid_signature;
	hybrid_signature.return_type_index = TypeIndex{0, TypeCategory::Long};
	hybrid_signature.return_reference_qualifier = ReferenceQualifier::LValueReference;
	FunctionCallableTypes hybrid_storage;
	hybrid_storage.parameter_types.push_back(makeFunctionTypeFromSpecifier(
		TypeSpecifierNode(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None)));
	hybrid_signature.callable_types = &hybrid_storage;
	require(!hybrid_signature.hasStructuredTypes());
	require(!hybrid_signature.parameter_types().empty());
	TypeSpecifierNode hybrid_function(TypeCategory::Function, TypeQualifier::None, 64, Token{},
		CVQualifier::None);
	hybrid_function.set_function_signature(hybrid_signature);
	const auto imported_hybrid = importCanonicalType(table, hybrid_function);
	require(imported_hybrid.status == CanonicalTypeImportStatus::Supported);
	const TypeId hybrid_params[] = {table.builtin(CanonicalBuiltinKind::Char)};
	require(imported_hybrid.type == table.function(
		table.reference(table.builtin(CanonicalBuiltinKind::Long), ReferenceQualifier::LValueReference),
		hybrid_params, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}));

	TypeSpecifierNode member_pointer(TypeCategory::MemberFunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	member_pointer.set_function_signature(member_signature);
	member_pointer.set_member_class_name(StringTable::getOrInternStringHandle("Owner"));
	// Spelling-only owners stay deferred until TypeInfo publishes a class EntityId.
	require(importCanonicalType(table, member_pointer).status == CanonicalTypeImportStatus::UnmigratedCallable);
	TypeSpecifierNode member_object(TypeCategory::MemberObjectPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	member_object.set_member_class_name(StringTable::getOrInternStringHandle("Owner"));
	require(importCanonicalType(table, member_object).status == CanonicalTypeImportStatus::UnmigratedCallable);

	member_pointer.set_member_class_entity(EntityId{7});
	const auto imported_mfp = importCanonicalType(table, member_pointer);
	require(imported_mfp.status == CanonicalTypeImportStatus::Supported);
	require(imported_mfp.type == table.memberFunctionPointer(
		table.record(EntityId{7}), imported_member.type));

	TypeSpecifierNode data_member(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	data_member.set_member_class_name(StringTable::getOrInternStringHandle("Owner"));
	data_member.set_member_class_entity(EntityId{7});
	data_member.add_pointer_level(CVQualifier::None);
	const auto imported_mop = importCanonicalType(table, data_member);
	require(imported_mop.status == CanonicalTypeImportStatus::Supported);
	require(imported_mop.type == table.memberObjectPointer(
		table.record(EntityId{7}), table.builtin(CanonicalBuiltinKind::Int)));
	require(imported_mop.type != imported_mfp.type);

	// A cast/NTTP rewrite publishes the class EntityId but flattens the pointee
	// category. Without the preserved pointee specifier it stays fail-closed;
	// with it the adapter imports the structural member object pointer.
	member_object.set_member_class_entity(EntityId{7});
	require(importCanonicalType(table, member_object).status == CanonicalTypeImportStatus::UnmigratedCallable);
	TypeSpecifierNode recovered_pointee(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	member_object.set_member_object_pointee(&recovered_pointee);
	const auto imported_recovered_mop = importCanonicalType(table, member_object);
	require(imported_recovered_mop.status == CanonicalTypeImportStatus::Supported);
	require(imported_recovered_mop.type == table.memberObjectPointer(
		table.record(EntityId{7}), table.builtin(CanonicalBuiltinKind::Int)));
	require(imported_recovered_mop.type == imported_mop.type);
	require(imported_recovered_mop.type != imported_mfp.type);

	// An unsupported pointee keeps the whole member object pointer fail-closed
	// instead of silently flattening the pointee.
	TypeSpecifierNode unpublished_pointee(TypeCategory::Struct, TypeQualifier::None, 64, Token{},
		CVQualifier::None);
	member_object.set_member_object_pointee(&unpublished_pointee);
	require(importCanonicalType(table, member_object).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);

	TypeSpecifierNode unpublished_record(TypeCategory::Struct, TypeQualifier::None, 64, Token{},
		CVQualifier::Const);
	unpublished_record.set_reference_qualifier(ReferenceQualifier::LValueReference);
	require(importCanonicalType(table, unpublished_record).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);

	unpublished_record.set_type_entity(EntityId{3});
	const auto imported_record = importCanonicalType(table, unpublished_record);
	require(imported_record.status == CanonicalTypeImportStatus::Supported);
	require(imported_record.type == table.reference(
		table.qualify(table.record(EntityId{3}), CVQualifier::Const),
		ReferenceQualifier::LValueReference));
	require(imported_record.type != table.reference(
		table.qualify(table.record(EntityId{4}), CVQualifier::Const),
		ReferenceQualifier::LValueReference));

	TypeSpecifierNode unpublished_enum(TypeCategory::Enum, TypeQualifier::None, 16, Token{},
		CVQualifier::Volatile);
	require(importCanonicalType(table, unpublished_enum).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);
	unpublished_enum.set_type_entity(EntityId{5});
	const auto imported_enum = importCanonicalType(table, unpublished_enum);
	require(imported_enum.status == CanonicalTypeImportStatus::Supported);
	require(imported_enum.type == table.qualify(table.enumeration(EntityId{5}), CVQualifier::Volatile));
	require(imported_enum.type != table.qualify(table.enumeration(EntityId{6}), CVQualifier::Volatile));

	TypeSpecifierNode spelling_only_param(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	spelling_only_param.set_template_parameter_identity(StringTable::getOrInternStringHandle("T"));
	require(importCanonicalType(table, spelling_only_param).status ==
		CanonicalTypeImportStatus::Unresolved);
	spelling_only_param.set_template_parameter_decl(TemplateDeclId{4}, 0);
	spelling_only_param.set_reference_qualifier(ReferenceQualifier::RValueReference);
	const auto imported_template_param = importCanonicalType(table, spelling_only_param);
	require(imported_template_param.status == CanonicalTypeImportStatus::Supported);
	require(imported_template_param.type == table.reference(
		table.templateParameter(TemplateDeclId{4}, 0), ReferenceQualifier::RValueReference));
	require(imported_template_param.type != table.reference(
		table.templateParameter(TemplateDeclId{4}, 1), ReferenceQualifier::RValueReference));
	require(imported_template_param.type != table.reference(
		table.templateParameter(TemplateDeclId{5}, 0), ReferenceQualifier::RValueReference));
	TypeSpecifierNode decl_only_param(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{},
		CVQualifier::Const);
	decl_only_param.set_template_parameter_decl(TemplateDeclId{4}, 0);
	decl_only_param.add_pointer_level(CVQualifier::None);
	const auto imported_decl_only = importCanonicalType(table, decl_only_param);
	require(imported_decl_only.status == CanonicalTypeImportStatus::Supported);
	require(imported_decl_only.type == table.pointer(
		table.qualify(table.templateParameter(TemplateDeclId{4}, 0), CVQualifier::Const)));

	TypeSpecifierNode unstamped_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	require(importCanonicalType(table, unstamped_specialization).status ==
		CanonicalTypeImportStatus::Unresolved);
	TypeSpecifierNode int_arg(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode float_arg(TypeCategory::Float, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	std::vector<TypeSpecifierNode> pair_args{int_arg, float_arg};
	TypeSpecifierNode stamped_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	stamped_specialization.set_template_specialization(TemplateDeclId{11}, pair_args);
	stamped_specialization.add_pointer_level(CVQualifier::None);
	const auto imported_specialization = importCanonicalType(table, stamped_specialization);
	require(imported_specialization.status == CanonicalTypeImportStatus::Supported);
	const TypeId expected_args[] = {
		table.builtin(CanonicalBuiltinKind::Int),
		table.builtin(CanonicalBuiltinKind::Float),
	};
	require(imported_specialization.type == table.pointer(
		table.templateSpecialization(TemplateDeclId{11}, expected_args)));
	require(imported_specialization.type != table.pointer(
		table.templateSpecialization(TemplateDeclId{12}, expected_args)));
	const TypeId swapped_args[] = {
		table.builtin(CanonicalBuiltinKind::Float),
		table.builtin(CanonicalBuiltinKind::Int),
	};
	require(imported_specialization.type != table.pointer(
		table.templateSpecialization(TemplateDeclId{11}, swapped_args)));
	TypeSpecifierNode param_arg(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	param_arg.set_template_parameter_decl(TemplateDeclId{4}, 0);
	std::vector<TypeSpecifierNode> dependent_args{param_arg};
	TypeSpecifierNode dependent_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::Const);
	dependent_specialization.set_template_specialization(TemplateDeclId{11}, dependent_args);
	const auto imported_dependent_spec = importCanonicalType(table, dependent_specialization);
	require(imported_dependent_spec.status == CanonicalTypeImportStatus::Supported);
	const TypeId dependent_arg_ids[] = {table.templateParameter(TemplateDeclId{4}, 0)};
	require(imported_dependent_spec.type == table.qualify(
		table.templateSpecialization(TemplateDeclId{11}, dependent_arg_ids), CVQualifier::Const));
	TypeSpecifierNode dependent_alias_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	dependent_alias_specialization.set_alias_template_specialization(
		TemplateDeclId{31}, dependent_args);
	const auto imported_dependent_alias = importCanonicalType(table, dependent_alias_specialization);
	require(imported_dependent_alias.status == CanonicalTypeImportStatus::Supported);
	require(table.node(imported_dependent_alias.type).kind ==
		CanonicalTypeKind::AliasTemplateSpecialization);
	require(imported_dependent_alias.type == table.aliasTemplateSpecialization(
		TemplateDeclId{31}, dependent_arg_ids));
	require(imported_dependent_alias.type != table.aliasTemplateSpecialization(
		TemplateDeclId{32}, dependent_arg_ids));
	require(imported_dependent_alias.type != table.templateSpecialization(
		TemplateDeclId{31}, dependent_arg_ids));
	const TypeId alias_target_pattern = table.pointer(
		table.templateParameter(TemplateDeclId{31}, 0));
	const CanonicalTemplateArgKind alias_type_parameter[] = {CanonicalTemplateArgKind::Type};
	table.publishAliasTemplateTarget(TemplateDeclId{31}, alias_target_pattern, alias_type_parameter);
	require(table.aliasTemplateTarget(TemplateDeclId{31}) == alias_target_pattern);
	const TypeId alias_concrete_arg[] = {table.builtin(CanonicalBuiltinKind::Int)};
	const TypeId alias_dependent_arg[] = {table.templateParameter(TemplateDeclId{31}, 0)};
	require(table.dependsOnlyOnTemplateParameters(alias_target_pattern, TemplateDeclId{31}));
	require(!table.dependsOnlyOnTemplateParameters(
		table.pointer(table.templateParameter(TemplateDeclId{32}, 0)), TemplateDeclId{31}));
	require(table.substitute(
		table.aliasTemplateSpecialization(TemplateDeclId{31}, alias_dependent_arg),
		TemplateDeclId{31}, alias_concrete_arg) ==
		table.pointer(table.builtin(CanonicalBuiltinKind::Int)));
	table.publishAliasTemplateTarget(TemplateDeclId{31}, alias_target_pattern, alias_type_parameter);
	rejects([&] { table.publishAliasTemplateTarget(
		TemplateDeclId{31}, table.builtin(CanonicalBuiltinKind::Int), alias_type_parameter); });
	require(!table.aliasTemplateTarget(TemplateDeclId{32}).has_value());
	TypeSpecifierNode nttp_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	nttp_specialization.set_template_specialization_mixed(
		TemplateDeclId{11},
		std::vector<SpecTemplateArgKind>{SpecTemplateArgKind::Type, SpecTemplateArgKind::NonType},
		std::vector<TypeSpecifierNode>{int_arg},
		std::vector<ExprId>{ExprId{21}},
		std::vector<TemplateDeclId>{},
		std::vector<SpecDependentTemplateArg>{});
	const auto imported_nttp_spec = importCanonicalType(table, nttp_specialization);
	require(imported_nttp_spec.status == CanonicalTypeImportStatus::Supported);
	const CanonicalTemplateArgument expected_nttp_args[] = {
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
		CanonicalTemplateArgument::makeNonType(ExprId{21}),
	};
	require(imported_nttp_spec.type ==
		table.templateSpecialization(TemplateDeclId{11}, expected_nttp_args));
	const CanonicalTemplateArgument other_nttp_args[] = {
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
		CanonicalTemplateArgument::makeNonType(ExprId{22}),
	};
	require(imported_nttp_spec.type !=
		table.templateSpecialization(TemplateDeclId{11}, other_nttp_args));
	TypeSpecifierNode template_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	template_specialization.set_template_specialization_mixed(
		TemplateDeclId{11},
		std::vector<SpecTemplateArgKind>{SpecTemplateArgKind::Template, SpecTemplateArgKind::Type},
		std::vector<TypeSpecifierNode>{int_arg},
		std::vector<ExprId>{},
		std::vector<TemplateDeclId>{TemplateDeclId{12}},
		std::vector<SpecDependentTemplateArg>{});
	const auto imported_template_spec = importCanonicalType(table, template_specialization);
	require(imported_template_spec.status == CanonicalTypeImportStatus::Supported);
	const CanonicalTemplateArgument expected_template_args[] = {
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{12}),
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
	};
	require(imported_template_spec.type ==
		table.templateSpecialization(TemplateDeclId{11}, expected_template_args));
	require(imported_template_spec.type != table.templateSpecialization(
		TemplateDeclId{11}, std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeTemplate(TemplateDeclId{13}),
			CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
		}));
	TypeSpecifierNode dependent_template_specialization(
		TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	dependent_template_specialization.set_template_specialization_mixed(
		TemplateDeclId{11},
		std::vector<SpecTemplateArgKind>{
			SpecTemplateArgKind::DependentTemplate,
			SpecTemplateArgKind::Type},
		std::vector<TypeSpecifierNode>{int_arg},
		std::vector<ExprId>{},
		std::vector<TemplateDeclId>{},
		std::vector<SpecDependentTemplateArg>{{TemplateDeclId{4}, 1}});
	const auto imported_dependent_template_spec =
		importCanonicalType(table, dependent_template_specialization);
	require(imported_dependent_template_spec.status == CanonicalTypeImportStatus::Supported);
	const CanonicalTemplateArgument expected_dependent_template_args[] = {
		CanonicalTemplateArgument::makeDependentTemplate(TemplateDeclId{4}, 1),
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
	};
	const TypeId dependent_template_spec_id = table.templateSpecialization(
		TemplateDeclId{11}, expected_dependent_template_args);
	require(imported_dependent_template_spec.type == dependent_template_spec_id);
	require(imported_dependent_template_spec.type != table.templateSpecialization(
		TemplateDeclId{11}, std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeDependentTemplate(TemplateDeclId{4}, 0),
			CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
		}));
	const TypeId dependent_template_arg_link =
		table.templateSpecializationArguments(dependent_template_spec_id);
	require(table.templateArgumentKind(dependent_template_arg_link) ==
		CanonicalTemplateArgKind::DependentTemplate);
	require(table.templateArgumentDependentTemplateDecl(dependent_template_arg_link) ==
		TemplateDeclId{4});
	require(table.templateArgumentDependentTemplateIndex(dependent_template_arg_link) == 1);
	require(table.templateArgumentKind(table.templateArgumentNext(dependent_template_arg_link)) ==
		CanonicalTemplateArgKind::Type);
	TypeSpecifierNode empty_nttp(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	rejects([&] {
		empty_nttp.set_template_specialization_mixed(
			TemplateDeclId{11},
			std::vector<SpecTemplateArgKind>{SpecTemplateArgKind::NonType},
			std::vector<TypeSpecifierNode>{},
			std::vector<ExprId>{ExprId{}},
			std::vector<TemplateDeclId>{},
			std::vector<SpecDependentTemplateArg>{});
	});

	TypeSpecifierNode published_enum_array(TypeCategory::Enum, TypeQualifier::None, 16, Token{},
		CVQualifier::None);
	published_enum_array.set_type_entity(EntityId{5});
	const std::array<size_t, 1> enum_array_dimensions{2};
	published_enum_array.set_array_dimensions(enum_array_dimensions);
	const auto enum_array_before = table.size();
	require(importCanonicalType(table, published_enum_array).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);
	require(table.size() == enum_array_before);
	const CanonicalRecordLayout record_layout{
		.entity = EntityId{3},
		.size_bytes = 16,
		.layout_data_size_bytes = 9,
		.non_virtual_size_bytes = 16,
		.alignment = 8,
		.member_count = 2,
		.direct_base_count = 0,
		.flags = CanonicalRecordLayoutFlags::None,
	};
	table.publishRecordLayout(record_layout);
	require(table.recordLayout(EntityId{3}) == record_layout);
	const CanonicalEnumLayout enum_layout{
		.entity = EntityId{5},
		.underlying_type = table.builtin(CanonicalBuiltinKind::UnsignedShort),
		.size_bytes = 2,
		.enumerator_count = 2,
		.flags = CanonicalEnumLayoutFlags::Scoped,
	};
	table.publishEnumLayout(enum_layout);
	require(table.enumLayout(EntityId{5}) == enum_layout);
	require(importCanonicalType(table, published_enum_array).type ==
		table.array(table.enumeration(EntityId{5}), 2));
	TypeSpecifierNode published_record_array(TypeCategory::Struct, TypeQualifier::None, 128, Token{},
		CVQualifier::None);
	published_record_array.set_type_entity(EntityId{3});
	published_record_array.set_array_dimensions(enum_array_dimensions);
	require(importCanonicalType(table, published_record_array).type ==
		table.array(table.record(EntityId{3}), 2));
	CanonicalRecordLayout conflicting_record_layout = record_layout;
	conflicting_record_layout.alignment = 4;
	rejects([&] { table.publishRecordLayout(conflicting_record_layout); });
	CanonicalTypeTransaction layout_transaction(table);
	table.publishRecordLayout({
		.entity = EntityId{9},
		.size_bytes = 4,
		.layout_data_size_bytes = 4,
		.non_virtual_size_bytes = 4,
		.alignment = 4,
		.member_count = 1,
		.direct_base_count = 0,
		.flags = CanonicalRecordLayoutFlags::None,
	});
	layout_transaction.rollback();
	require(!table.hasRecordLayout(EntityId{9}));

	table.publishRecordLayout({
		.entity = EntityId{11},
		.size_bytes = 24,
		.layout_data_size_bytes = 16,
		.non_virtual_size_bytes = 16,
		.alignment = 8,
		.member_count = 2,
		.direct_base_count = 1,
		.flags = CanonicalRecordLayoutFlags::None,
	});
	table.publishRecordLayout({
		.entity = EntityId{10},
		.size_bytes = 8,
		.layout_data_size_bytes = 8,
		.non_virtual_size_bytes = 8,
		.alignment = 8,
		.member_count = 0,
		.direct_base_count = 0,
		.flags = CanonicalRecordLayoutFlags::None,
	});
	const std::array<CanonicalRecordMember, 2> schema_members{{
		{
			.type = table.builtin(CanonicalBuiltinKind::Int),
			.offset_bytes = 0,
			.size_bytes = 4,
			.bit_width = 0,
			.bit_offset = 0,
			.access = CanonicalAccess::Public,
			.flags = CanonicalRecordMemberFlags::None,
		},
		{
			.type = table.pointer(table.builtin(CanonicalBuiltinKind::Double)),
			.offset_bytes = 8,
			.size_bytes = 8,
			.bit_width = 0,
			.bit_offset = 0,
			.access = CanonicalAccess::Private,
			.flags = CanonicalRecordMemberFlags::None,
		},
	}};
	const std::array<CanonicalRecordBase, 1> schema_bases{{
		{
			.entity = EntityId{10},
			.offset_bytes = 16,
			.access = CanonicalAccess::Public,
			.flags = CanonicalRecordBaseFlags::None,
		},
	}};
	table.publishRecordFieldSchema(EntityId{11}, schema_members, schema_bases);
	require(table.hasRecordFieldSchema(EntityId{11}));
	require(table.recordMemberAt(EntityId{11}, 0) == schema_members[0]);
	require(table.recordMemberAt(EntityId{11}, 1) == schema_members[1]);
	require(table.recordBaseAt(EntityId{11}, 0) == schema_bases[0]);
	table.publishRecordFieldSchema(EntityId{11}, schema_members, schema_bases);
	std::array<CanonicalRecordMember, 2> conflicting_members = schema_members;
	conflicting_members[1].offset_bytes = 4;
	rejects([&] {
		table.publishRecordFieldSchema(EntityId{11}, conflicting_members, schema_bases);
	});
	table.publishRecordLayout({
		.entity = EntityId{13},
		.size_bytes = 1,
		.layout_data_size_bytes = 0,
		.non_virtual_size_bytes = 1,
		.alignment = 1,
		.member_count = 1,
		.direct_base_count = 0,
		.flags = CanonicalRecordLayoutFlags::None,
	});
	const std::array<CanonicalRecordMember, 1> zero_width_bitfield{{
		{
			.type = table.builtin(CanonicalBuiltinKind::Int),
			.offset_bytes = 0,
			.size_bytes = 0,
			.bit_width = 0,
			.bit_offset = 0,
			.access = CanonicalAccess::Public,
			.flags = CanonicalRecordMemberFlags::Bitfield,
		},
	}};
	table.publishRecordFieldSchema(EntityId{13}, zero_width_bitfield, {});
	require(table.recordMemberAt(EntityId{13}, 0).flags == CanonicalRecordMemberFlags::Bitfield);
	require(table.recordMemberAt(EntityId{13}, 0).bit_width == 0);
	CanonicalTypeTransaction schema_transaction(table);
	table.publishRecordLayout({
		.entity = EntityId{12},
		.size_bytes = 4,
		.layout_data_size_bytes = 4,
		.non_virtual_size_bytes = 4,
		.alignment = 4,
		.member_count = 1,
		.direct_base_count = 0,
		.flags = CanonicalRecordLayoutFlags::None,
	});
	const std::array<CanonicalRecordMember, 1> rolled_members{{
		{
			.type = table.builtin(CanonicalBuiltinKind::Char),
			.offset_bytes = 0,
			.size_bytes = 1,
			.bit_width = 0,
			.bit_offset = 0,
			.access = CanonicalAccess::Protected,
			.flags = CanonicalRecordMemberFlags::None,
		},
	}};
	table.publishRecordFieldSchema(EntityId{12}, rolled_members, {});
	schema_transaction.rollback();
	require(!table.hasRecordLayout(EntityId{12}));
	require(!table.hasRecordFieldSchema(EntityId{12}));
}

inline void checkTemplateDeclPublication() {
	TemplateDeclTable decls;
	const auto name_a = StringTable::getOrInternStringHandle("AlphaTemplate");
	const auto name_b = StringTable::getOrInternStringHandle("BetaTemplate");
	const auto first = decls.publishPrimaryClassTemplate(OwnerId{1}, name_a);
	require(first.value != 0);
	require(decls.publishPrimaryClassTemplate(OwnerId{1}, name_a) == first);
	require(decls.findPrimaryClassTemplate(OwnerId{1}, name_a) == first);
	require(!decls.findPrimaryClassTemplate(OwnerId{1}, name_b).has_value());
	require(decls.publishPrimaryClassTemplate(OwnerId{1}, name_b) != first);
	require(decls.publishPrimaryClassTemplate(OwnerId{2}, name_a) != first);
	require(decls.findPrimaryClassTemplate(OwnerId{1}, name_b).has_value());
	require(decls.size() == 3);
	rejects([&] { decls.publishPrimaryClassTemplate(OwnerId{}, name_a); });

	// Class- and template-owned OwnerIds share the same table API but cannot
	// collide with each other or namespace OwnerId{1}, even with raw value 1.
	const OwnerId class_owner = ownerIdFromClassEntity(EntityId{1});
	require(class_owner != OwnerId{1});
	const OwnerId template_owner = ownerIdFromTemplateDecl(TemplateDeclId{1});
	require(template_owner != OwnerId{1});
	require(template_owner != class_owner);
	require(isClassOwnedOwnerId(class_owner));
	require(isTemplateOwnedOwnerId(template_owner));
	require(classEntityFromOwnerId(class_owner) == EntityId{1});
	require(templateDeclFromOwnerId(template_owner) == TemplateDeclId{1});
	const auto member = decls.publishPrimaryClassTemplate(class_owner, name_a);
	require(member != first);
	require(decls.publishPrimaryClassTemplate(class_owner, name_a) == member);
	require(decls.findPrimaryClassTemplate(class_owner, name_a) == member);
	require(decls.findPrimaryClassTemplate(OwnerId{1}, name_a) == first);
	require(decls.publishPrimaryClassTemplate(class_owner, name_b) != member);
	const auto template_member = decls.publishPrimaryClassTemplate(template_owner, name_a);
	require(template_member != first);
	require(template_member != member);
	require(decls.publishPrimaryClassTemplate(template_owner, name_a) == template_member);
	require(decls.findPrimaryClassTemplate(template_owner, name_a) == template_member);
	require(decls.size() == 6);

	// Free function templates use a distinct primary kind and a signature
	// index so overloads under the same OwnerId + name publish distinct ids
	// while matching shapes merge.
	const auto fn_name = StringTable::getOrInternStringHandle("AlphaFn");
	const auto fn_first = decls.publishPrimaryFunctionTemplate(OwnerId{1}, fn_name, 0u);
	require(fn_first != first);
	require(decls.publishPrimaryFunctionTemplate(OwnerId{1}, fn_name, 0u) == fn_first);
	require(decls.findPrimaryFunctionTemplate(OwnerId{1}, fn_name, 0u) == fn_first);
	require(!decls.findPrimaryClassTemplate(OwnerId{1}, fn_name).has_value());
	require(decls.nextFunctionSignatureIndex(OwnerId{1}, fn_name) == 1u);
	const auto fn_overload = decls.publishPrimaryFunctionTemplate(OwnerId{1}, fn_name, 1u);
	require(fn_overload != fn_first);
	require(decls.findPrimaryFunctionTemplate(OwnerId{1}, fn_name, 1u) == fn_overload);
	require(decls.nextFunctionSignatureIndex(OwnerId{1}, fn_name) == 2u);
	rejects([&] { (void)decls.publishPrimaryFunctionTemplate(OwnerId{}, fn_name, 0u); });

	// Alias-template primaries use their own kind so they cannot share slots
	// with class or function primaries under the same OwnerId + name.
	const auto alias_name = StringTable::getOrInternStringHandle("AlphaAlias");
	const auto alias_first = decls.publishPrimaryAliasTemplate(OwnerId{1}, alias_name);
	require(alias_first != first);
	require(alias_first != fn_first);
	require(decls.publishPrimaryAliasTemplate(OwnerId{1}, alias_name) == alias_first);
	require(decls.findPrimaryAliasTemplate(OwnerId{1}, alias_name) == alias_first);
	require(!decls.findPrimaryClassTemplate(OwnerId{1}, alias_name).has_value());
	require(decls.publishPrimaryAliasTemplate(OwnerId{1}, name_a) != alias_first);
	rejects([&] { (void)decls.publishPrimaryAliasTemplate(OwnerId{}, alias_name); });
	const auto alias_member = decls.publishPrimaryAliasTemplate(class_owner, name_a);
	require(alias_member != member);
	require(alias_member != alias_first);
	require(decls.findPrimaryAliasTemplate(class_owner, name_a) == alias_member);
	require(decls.findPrimaryClassTemplate(class_owner, name_a) == member);

	// Anchored alias patterns are keyed by the alias TemplateDeclId and stay
	// disjoint from the class-pattern map.
	decls.attachPrimaryAliasPattern(alias_member, ASTNode{});
	require(decls.primaryAliasPattern(alias_member).has_value());
	require(!decls.primaryClassTemplatePattern(alias_member).has_value());
	rejects([&] { decls.attachPrimaryAliasPattern(TemplateDeclId{999999}, ASTNode{}); });

	// Variable-template primaries use their own kind so they cannot share
	// slots with class, function, or alias primaries under the same
	// OwnerId + name.
	const auto variable_name = StringTable::getOrInternStringHandle("AlphaVariable");
	const auto variable_first = decls.publishPrimaryVariableTemplate(OwnerId{1}, variable_name);
	require(variable_first != first);
	require(variable_first != fn_first);
	require(variable_first != alias_first);
	require(decls.publishPrimaryVariableTemplate(OwnerId{1}, variable_name) == variable_first);
	require(decls.findPrimaryVariableTemplate(OwnerId{1}, variable_name) == variable_first);
	require(!decls.findPrimaryClassTemplate(OwnerId{1}, variable_name).has_value());
	require(!decls.findPrimaryAliasTemplate(OwnerId{1}, variable_name).has_value());
	require(decls.publishPrimaryVariableTemplate(OwnerId{1}, name_a) != variable_first);
	rejects([&] { (void)decls.publishPrimaryVariableTemplate(OwnerId{}, variable_name); });
	const auto variable_member = decls.publishPrimaryVariableTemplate(class_owner, name_a);
	require(variable_member != member);
	require(variable_member != alias_member);
	require(variable_member != variable_first);
	require(decls.findPrimaryVariableTemplate(class_owner, name_a) == variable_member);
	require(decls.findPrimaryClassTemplate(class_owner, name_a) == member);
	require(decls.findPrimaryAliasTemplate(class_owner, name_a) == alias_member);

	// Anchored variable patterns are keyed by the variable TemplateDeclId and
	// stay disjoint from the other pattern maps.
	decls.attachPrimaryVariablePattern(variable_member, ASTNode{});
	require(decls.primaryVariablePattern(variable_member).has_value());
	require(!decls.primaryClassTemplatePattern(variable_member).has_value());
	require(!decls.primaryAliasPattern(variable_member).has_value());
	rejects([&] { decls.attachPrimaryVariablePattern(TemplateDeclId{999999}, ASTNode{}); });
}

inline void checkDependentNames() {
	CanonicalTypeTable table;
	const auto owner = table.templateParameter(TemplateDeclId{7}, 0);
	const auto other = table.templateParameter(TemplateDeclId{7}, 1);
	const auto member = table.dependentName(owner, "first");
	std::string same_name = "first";
	require(table.dependentName(owner, same_name) == member);
	require(table.dependentName(other, "first") != member);
	require(table.dependentName(owner, "second") != member);
	require(table.dependentNameQualifier(member) == owner);
	require(table.dependentNameIdentifier(member) == "first");
	const auto nested = table.dependentName(member, "second");
	require(nested != table.dependentName(table.dependentName(owner, "second"), "first"));
	for (const auto name : {"a", "abcdefgh", "abcdefghi", "abcdefghijklmnop", "abcdefghijklmnopq"}) {
		const auto id = table.dependentName(owner, name);
		require(table.dependentNameIdentifier(id) == name);
		require(table.dependentName(owner, std::string(name)) == id);
	}
	rejects([&] { table.dependentName(owner, ""); });
	rejects([&] { table.dependentName(owner, std::string_view("a\0b", 3)); });
	rejects([&] { table.dependentName(table.builtin(CanonicalBuiltinKind::Int), "first"); });
	rejects([&] { table.dependentName(table.record(EntityId{1}), "first"); });
	rejects([&] { table.dependentNameIdentifier(owner); });
	const TypeId pair_int = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId pair_float = table.builtin(CanonicalBuiltinKind::Float);
	const TypeId pair_args[] = {pair_int, pair_float};
	const TypeId pair_swapped_args[] = {pair_float, pair_int};
	const TypeId spec_pair = table.templateSpecialization(TemplateDeclId{11}, pair_args);
	const TypeId spec_swapped = table.templateSpecialization(TemplateDeclId{11}, pair_swapped_args);
	const TypeId spec_other = table.templateSpecialization(TemplateDeclId{12}, pair_args);
	const TypeId spec_member = table.dependentName(spec_pair, "value_type");
	require(table.dependentName(spec_pair, "value_type") == spec_member);
	require(table.dependentName(spec_swapped, "value_type") != spec_member);
	require(table.dependentName(spec_other, "value_type") != spec_member);
	require(table.dependentName(spec_pair, "pointer") != spec_member);
	require(table.dependentNameQualifier(spec_member) == spec_pair);
	require(table.dependentNameIdentifier(spec_member) == "value_type");
	const TypeId spec_nested = table.dependentName(spec_member, "type");
	require(spec_nested != table.dependentName(table.dependentName(spec_pair, "type"), "value_type"));
	const TypeId spec_foo = table.dependentTemplateMember(
		spec_pair, "Foo", std::span<const TypeId>(&pair_int, 1));
	require(spec_foo != table.dependentTemplateMember(
		spec_swapped, "Foo", std::span<const TypeId>(&pair_int, 1)));
	require(table.dependentName(spec_foo, "type") !=
		table.dependentName(table.dependentTemplateMember(
			spec_pair, "Bar", std::span<const TypeId>(&pair_int, 1)), "type"));
	TypeSpecifierNode spec_syntax(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::Const);
	spec_syntax.set_dependent_name_type(spec_member);
	require(importCanonicalType(table, spec_syntax).type ==
		table.qualify(spec_member, CVQualifier::Const));
	spec_syntax.set_dependent_name_type(table.dependentName(spec_foo, "type"));
	require(importCanonicalType(table, spec_syntax).type ==
		table.qualify(table.dependentName(spec_foo, "type"), CVQualifier::Const));
	// Production publication shape for DependentInstantiation owners: tip is a
	// DependentName (or DependentTemplateMember) whose qualifier is Spec, never a
	// bare TemplateSpecialization binding on the member type specifier.
	require(table.node(spec_member).kind == CanonicalTypeKind::DependentName);
	require(table.node(table.dependentNameQualifier(spec_member)).kind ==
		CanonicalTypeKind::TemplateSpecialization);
	require(table.templateSpecializationDecl(spec_pair) == TemplateDeclId{11});
	require(table.node(spec_foo).kind == CanonicalTypeKind::DependentTemplateMember);
	require(table.node(table.dependentNameQualifier(spec_foo)).kind ==
		CanonicalTypeKind::TemplateSpecialization);
	require(table.dependentNameIdentifier(spec_foo) == "Foo");
	require(table.templateArgumentType(table.dependentTemplateMemberArguments(spec_foo)) ==
		pair_int);
	CanonicalTypeTable reordered_specs;
	const TypeId reordered_spec_int = reordered_specs.builtin(CanonicalBuiltinKind::Int);
	const TypeId reordered_spec_float = reordered_specs.builtin(CanonicalBuiltinKind::Float);
	const TypeId reordered_pair_args[] = {reordered_spec_int, reordered_spec_float};
	reordered_specs.dependentName(
		reordered_specs.templateSpecialization(TemplateDeclId{11}, reordered_pair_args),
		"pointer");
	const TypeId reordered_spec = reordered_specs.templateSpecialization(
		TemplateDeclId{11}, reordered_pair_args);
	const TypeId reordered_spec_member = reordered_specs.dependentName(reordered_spec, "value_type");
	require(sameStructure(table, spec_nested, reordered_specs,
		reordered_specs.dependentName(reordered_spec_member, "type")));
	require(!sameStructure(table, spec_member, reordered_specs,
		reordered_specs.dependentName(reordered_spec, "pointer")));
	const TypeId bytes{static_cast<uint32_t>(table.node(member).array_extent)};
	rejects([&] { table.pointer(bytes); });
	rejects([&] { table.qualify(bytes, CVQualifier::Const); });
	rejects([&] { table.reference(bytes, ReferenceQualifier::LValueReference); });
	rejects([&] { table.array(bytes, 2); });
	rejects([&] { table.templateSpecialization(TemplateDeclId{1}, std::span<const TypeId>(&bytes, 1)); });
	rejects([&] { table.memberObjectPointer(table.record(EntityId{1}), bytes); });
	rejects([&] { table.function(bytes, {}, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}); });
	rejects([&] { table.function(table.builtin(CanonicalBuiltinKind::Int), std::span<const TypeId>(&bytes, 1),
		false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}); });
	table.publishRecordLayout({EntityId{1}, 1, 1, 1, 1, 1, 0, CanonicalRecordLayoutFlags::None});
	const CanonicalRecordMember invalid_member{bytes, 0, 1, 0, 0, CanonicalAccess::Public,
		CanonicalRecordMemberFlags::None};
	rejects([&] { table.publishRecordFieldSchema(EntityId{1},
		std::span<const CanonicalRecordMember>(&invalid_member, 1), {}); });
	const auto count = table.size();
	TypeId discarded;
	{
		CanonicalTypeTransaction transaction(table);
		discarded = table.dependentName(member, "discarded_identifier");
		{
			CanonicalTypeTransaction inner(table);
			table.dependentName(discarded, "inner");
			inner.commit();
		}
	}
	require(table.size() == count);
	require(table.dependentName(member, "discarded_identifier") == discarded);
	require(table.dependentName(owner, "first") == member);

	CanonicalTypeTable reordered;
	const auto reordered_owner = reordered.templateParameter(TemplateDeclId{7}, 0);
	reordered.dependentName(reordered_owner, "second");
	reordered.dependentName(reordered_owner, "unrelated");
	const auto reordered_member = reordered.dependentName(reordered_owner, "first");
	require(sameStructure(table, nested, reordered, reordered.dependentName(reordered_member, "second")));
	require(!sameStructure(table, member, reordered, reordered.dependentName(reordered_owner, "second")));

	TypeSpecifierNode syntax(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::Const);
	require(importCanonicalType(table, syntax).status != CanonicalTypeImportStatus::Supported);
	syntax.set_dependent_name_type(member);
	syntax.add_pointer_level(CVQualifier::Volatile);
	syntax.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const auto expected = table.reference(table.qualify(table.pointer(table.qualify(member, CVQualifier::Const)),
		CVQualifier::Volatile), ReferenceQualifier::LValueReference);
	require(importCanonicalType(table, syntax).type == expected);
	TypeSpecifierNode copy(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	copy.copy_binding_identity_from(syntax);
	require(importCanonicalType(table, copy).type == member);
	copy.set_array(true);
	const size_t dimensions[] = {2, 3};
	copy.set_array_dimensions(dimensions);
	require(importCanonicalType(table, copy).type == table.array(table.array(member, 3), 2));
	require(importCanonicalFunctionParameterType(table, copy).type == table.pointer(table.array(member, 3)));
	copy.set_pack_expansion(true);
	require(importCanonicalType(table, copy).status == CanonicalTypeImportStatus::Unresolved);
	copy.set_pack_expansion(false);
	const size_t invalid_dimensions[] = {0};
	copy.set_array_dimensions(invalid_dimensions);
	const auto before_invalid = table.size();
	require(importCanonicalType(table, copy).status == CanonicalTypeImportStatus::UnmigratedArray);
	require(table.size() == before_invalid);
	syntax.set_template_parameter_decl(TemplateDeclId{7}, 0);
	require(!syntax.has_dependent_name_type());
	copy.set_dependent_name_type(owner);
	copy.set_pack_expansion(false);
	rejects([&] { importCanonicalType(table, copy); });

	// Production publication shape: TemplateDeclId + parameter index + plain
	// identifier segments, without StringHandle identity or flat TypeIndex recovery.
	const auto published_owner = table.templateParameter(TemplateDeclId{11}, 0);
	const auto published_nested = table.dependentName(published_owner, "Nested");
	const auto published_item = table.dependentName(published_nested, "item");
	TypeSpecifierNode published(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	require(!published.has_dependent_name_type());
	require(importCanonicalType(table, published).status != CanonicalTypeImportStatus::Supported);
	published.set_dependent_name_type(published_item);
	require(importCanonicalType(table, published).type == published_item);
	require(table.dependentNameQualifier(published_item) == published_nested);
	require(table.dependentNameIdentifier(published_nested) == "Nested");
	require(table.dependentNameIdentifier(published_item) == "item");
	require(table.templateParameterDecl(published_owner) == TemplateDeclId{11});
	require(table.templateParameterIndex(published_owner) == 0);
	TypeSpecifierNode rebound(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::Volatile);
	rebound.copy_binding_identity_from(published);
	require(importCanonicalType(table, rebound).type ==
		table.qualify(published_item, CVQualifier::Volatile));

	const TypeId int_arg = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId double_arg = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId foo_int = table.dependentTemplateMember(published_owner, "Foo", std::span<const TypeId>(&int_arg, 1));
	const TypeId foo_double = table.dependentTemplateMember(published_owner, "Foo", std::span<const TypeId>(&double_arg, 1));
	const TypeId bar_int = table.dependentTemplateMember(published_owner, "Bar", std::span<const TypeId>(&int_arg, 1));
	require(foo_int != foo_double);
	require(foo_int != bar_int);
	require(table.dependentNameQualifier(foo_int) == published_owner);
	require(table.dependentNameIdentifier(foo_int) == "Foo");
	require(table.templateArgumentType(table.dependentTemplateMemberArguments(foo_int)) == int_arg);
	const TypeId nested_type = table.dependentName(foo_int, "type");
	require(nested_type != table.dependentName(foo_double, "type"));
	require(nested_type != table.dependentName(bar_int, "type"));
	TypeSpecifierNode template_member_syntax(
		TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::Const);
	template_member_syntax.set_dependent_name_type(foo_int);
	require(importCanonicalType(table, template_member_syntax).type ==
		table.qualify(foo_int, CVQualifier::Const));
	template_member_syntax.set_dependent_name_type(nested_type);
	require(importCanonicalType(table, template_member_syntax).type ==
		table.qualify(nested_type, CVQualifier::Const));
	rejects([&] {
		table.dependentTemplateMember(table.builtin(CanonicalBuiltinKind::Int), "Foo",
			std::span<const TypeId>(&int_arg, 1));
	});
	rejects([&] { table.dependentTemplateMemberArguments(published_item); });
	CanonicalTypeTable reordered_members;
	const auto reordered_param = reordered_members.templateParameter(TemplateDeclId{11}, 0);
	const TypeId reordered_int = reordered_members.builtin(CanonicalBuiltinKind::Int);
	reordered_members.dependentTemplateMember(reordered_param, "Bar",
		std::span<const TypeId>(&reordered_int, 1));
	const TypeId reordered_foo = reordered_members.dependentTemplateMember(
		reordered_param, "Foo", std::span<const TypeId>(&reordered_int, 1));
	require(sameStructure(table, nested_type, reordered_members,
		reordered_members.dependentName(reordered_foo, "type")));

	const auto shallow = table.size();
	auto deep = owner;
	auto reordered_deep = reordered_owner;
	for (size_t level = 0; level < 65536; ++level) {
		deep = table.dependentName(deep, "next");
		reordered_deep = reordered.dependentName(reordered_deep, "next");
	}
	require(sameStructure(table, deep, reordered, reordered_deep));
	// Trace one completed deep request, not every prefix. Tracing must expand
	// name content, not arena slots, and must not recurse along the qualifier.
	std::ostringstream trace;
	auto* previous_output = FlashCpp::LogConfig::output_stream;
	const auto previous_level = FlashCpp::LogConfig::getLevelForCategory(FlashCpp::LogCategory::Types);
	FlashCpp::LogConfig::setOutputStream(&trace);
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, FlashCpp::LogLevel::Trace);
	table.pointer(deep);
	const auto first_trace = trace.str();
	trace.str("");
	reordered.pointer(reordered_deep);
	FlashCpp::LogConfig::setOutputStream(previous_output);
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, previous_level);
	require(!first_trace.empty());
	require(trace.str() == first_trace);
	for (size_t level = 0; level < 65536; ++level) {
		require(table.dependentNameIdentifier(deep) == "next");
		deep = table.dependentNameQualifier(deep);
	}
	require(deep == owner);
	std::printf("dependent names: shallow=%zu node=%zu syntax=%zu deep=65536\n",
		shallow, sizeof(CanonicalTypeNode), sizeof(TypeSpecifierNode));
}

inline void checkSubstitution() {
	CanonicalTypeTable table;
	const TemplateDeclId env{7};
	const TemplateDeclId other{8};
	const TypeId param0 = table.templateParameter(env, 0);
	const TypeId param1 = table.templateParameter(env, 1);
	const TypeId foreign = table.templateParameter(other, 0);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId args[] = {integer, floating};
	require(table.substitute(param0, env, args) == integer);
	require(table.substitute(param1, env, args) == floating);
	require(table.substitute(foreign, env, args) == foreign);
	require(table.substitute(integer, env, args) == integer);

	const TypeId spec = table.templateSpecialization(TemplateDeclId{11},
		std::array<TypeId, 2>{param0, param1});
	const TypeId subst_spec = table.substitute(spec, env, args);
	require(subst_spec == table.templateSpecialization(TemplateDeclId{11}, args));
	require(table.templateSpecializationDecl(subst_spec) == TemplateDeclId{11});
	const TypeId dependent_template_spec = table.templateSpecialization(
		TemplateDeclId{11}, std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(param0),
			CanonicalTemplateArgument::makeDependentTemplate(TemplateDeclId{17}, 2),
		});
	const TypeId subst_dependent_template_spec =
		table.substitute(dependent_template_spec, env, args);
	require(subst_dependent_template_spec == table.templateSpecialization(
		TemplateDeclId{11}, std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeDependentTemplate(TemplateDeclId{17}, 2),
		}));

	const TypeId member = table.dependentName(param0, "first");
	const TypeId subst_member = table.substitute(member, env, args);
	require(table.node(subst_member).kind == CanonicalTypeKind::DependentName);
	require(table.dependentNameQualifier(subst_member) == integer);
	require(table.dependentNameIdentifier(subst_member) == "first");
	// Production ExpressionSubstitutor restamp overlays DependentName-family
	// tips after substitute+tryResolveDependentTip; collapsed concrete tips
	// project Builtin/Record/Enum onto TypeIndex then clear the stamp.
	rejects([&] { table.dependentName(integer, "first"); });

	const TypeId nested = table.dependentName(table.dependentName(param0, "Nested"), "item");
	const TypeId subst_nested = table.substitute(nested, env, args);
	require(table.dependentNameIdentifier(subst_nested) == "item");
	require(table.dependentNameIdentifier(table.dependentNameQualifier(subst_nested)) == "Nested");
	require(table.dependentNameQualifier(table.dependentNameQualifier(subst_nested)) == integer);

	const TypeId spec_root = table.dependentName(
		table.templateSpecialization(TemplateDeclId{11}, std::span<const TypeId>(&param0, 1)),
		"value_type");
	const TypeId subst_spec_root = table.substitute(spec_root, env, args);
	require(table.dependentNameIdentifier(subst_spec_root) == "value_type");
	require(table.node(table.dependentNameQualifier(subst_spec_root)).kind ==
		CanonicalTypeKind::TemplateSpecialization);
	require(table.templateArgumentType(table.templateSpecializationArguments(
		table.dependentNameQualifier(subst_spec_root))) == integer);

	const TypeId foo = table.dependentTemplateMember(param0, "Foo",
		std::span<const TypeId>(&param1, 1));
	const TypeId subst_foo = table.substitute(foo, env, args);
	require(table.node(subst_foo).kind == CanonicalTypeKind::DependentTemplateMember);
	require(table.dependentNameQualifier(subst_foo) == integer);
	require(table.dependentNameIdentifier(subst_foo) == "Foo");
	require(table.templateArgumentType(table.dependentTemplateMemberArguments(subst_foo)) ==
		floating);

	const TypeId wrapped = table.reference(
		table.array(table.pointer(table.qualify(param0, CVQualifier::Const)), 2),
		ReferenceQualifier::LValueReference);
	const TypeId subst_wrapped = table.substitute(wrapped, env, args);
	require(subst_wrapped == table.reference(
		table.array(table.pointer(table.qualify(integer, CVQualifier::Const)), 2),
		ReferenceQualifier::LValueReference));

	rejects([&] { table.substitute(TypeId{}, env, args); });
	rejects([&] { table.substitute(param0, TemplateDeclId{}, args); });
	rejects([&] { table.substitute(param0, env, {}); });
	const TypeId bytes{static_cast<uint32_t>(table.node(member).array_extent)};
	rejects([&] { table.substitute(bytes, env, args); });
	// A callable with no matching environment parameter is returned unchanged and
	// interns no new node; callable walks are covered by checkCallableSubstitution.
	const TypeId stable_function = table.function(integer, {}, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{});
	const size_t before_stable_function = table.size();
	require(table.substitute(stable_function, env, args) == stable_function);
	require(table.size() == before_stable_function);

	CanonicalTypeTable reordered;
	const TypeId reordered_param = reordered.templateParameter(env, 0);
	const TypeId reordered_int = reordered.builtin(CanonicalBuiltinKind::Int);
	reordered.dependentName(reordered_param, "unrelated");
	const TypeId reordered_member = reordered.dependentName(reordered_param, "first");
	const TypeId reordered_args[] = {reordered_int};
	require(sameStructure(table, subst_member, reordered,
		reordered.substitute(reordered_member, env, reordered_args)));

	auto deep = param0;
	for (size_t level = 0; level < 65536; ++level) {
		deep = table.dependentName(deep, "next");
	}
	const TypeId subst_deep = table.substitute(deep, env, args);
	TypeId cursor = subst_deep;
	for (size_t level = 0; level < 65536; ++level) {
		require(table.dependentNameIdentifier(cursor) == "next");
		cursor = table.dependentNameQualifier(cursor);
	}
	require(cursor == integer);
	std::printf("substitution: node=%zu deep=65536\n", sizeof(CanonicalTypeNode));
}

inline void checkCallableSubstitution() {
	CanonicalTypeTable table;
	const TemplateDeclId env{7};
	const TemplateDeclId foreign_env{99};
	const TypeId param0 = table.templateParameter(env, 0);
	const TypeId foreign = table.templateParameter(foreign_env, 0);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId void_builtin = table.builtin(CanonicalBuiltinKind::Void);
	const TypeId args[] = {integer};

	// Function return and parameter types substitute; calling convention, cv/ref,
	// variadic, and dll linkage all stay on the rebuilt Function identity.
	const TypeId dependent_params[] = {param0};
	const TypeId dependent_function = table.function(void_builtin, dependent_params, true,
		CVQualifier::Const, ReferenceQualifier::LValueReference, false,
		CanonicalCallingConvention::Stdcall, CanonicalDllLinkage::Import, ExprId{});
	const TypeId int_params[] = {integer};
	const TypeId expected_function = table.function(void_builtin, int_params, true,
		CVQualifier::Const, ReferenceQualifier::LValueReference, false,
		CanonicalCallingConvention::Stdcall, CanonicalDllLinkage::Import, ExprId{});
	const TypeId subst_function = table.substitute(dependent_function, env, args);
	require(subst_function == expected_function);
	require(subst_function != dependent_function);
	require(table.node(subst_function).qualifiers == CVQualifier::Const);
	require(table.node(subst_function).builtin ==
		static_cast<CanonicalBuiltinKind>(CanonicalCallingConvention::Stdcall));
	require(hasCanonicalTypeNodeFlag(table.node(subst_function).flags,
		CanonicalTypeNodeFlags::VariadicFunction));
	require(hasCanonicalTypeNodeFlag(table.node(subst_function).flags,
		CanonicalTypeNodeFlags::FunctionLValueRef));
	require(hasCanonicalTypeNodeFlag(table.node(subst_function).flags,
		CanonicalTypeNodeFlags::FunctionDllImport));

	// Substituting an already-concrete callable is identity and interns nothing.
	const size_t before_identity = table.size();
	require(table.substitute(subst_function, env, args) == subst_function);
	require(table.size() == before_identity);

	// Dependent return type and opaque dependent-noexcept ExprId survive.
	const TypeId dependent_return_function = table.function(param0, std::span<const TypeId>{},
		false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{31});
	const TypeId subst_return_function = table.substitute(dependent_return_function, env, args);
	require(table.node(subst_return_function).child == integer);
	require(table.functionDependentNoexcept(subst_return_function).value == 31);
	require(subst_return_function !=
		table.function(integer, std::span<const TypeId>{}, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
			CanonicalDllLinkage::None, ExprId{}));

	// A pointer wrapper over a dependent callable substitutes through into it.
	require(table.substitute(table.pointer(dependent_function), env, args) ==
		table.pointer(subst_function));

	// Member pointers substitute owner and pointee independently.
	const TypeId owner_a = table.record(EntityId{41});
	const TypeId owner_b = table.record(EntityId{42});
	const TypeId mfp = table.memberFunctionPointer(owner_a, dependent_function);
	const TypeId subst_mfp = table.substitute(mfp, env, args);
	require(subst_mfp == table.memberFunctionPointer(owner_a, subst_function));
	require(subst_mfp != table.memberFunctionPointer(owner_b, subst_function));
	const TypeId mop = table.memberObjectPointer(owner_a, param0);
	require(table.substitute(mop, env, args) == table.memberObjectPointer(owner_a, integer));
	require(table.substitute(table.memberObjectPointer(owner_a, foreign), env, args) ==
		table.memberObjectPointer(owner_a, foreign));

	// Interleaved array/pointer/member-function-pointer/function declarator has one
	// structural substitution shape.
	const TypeId nested_function = table.function(param0, dependent_params, false,
		CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId interleaved = table.array(
		table.pointer(table.memberFunctionPointer(owner_a, nested_function)), 3);
	const TypeId expected_interleaved = table.array(
		table.pointer(table.memberFunctionPointer(owner_a,
			table.function(integer, int_params, false, CVQualifier::None,
				ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
				CanonicalDllLinkage::None, ExprId{}))), 3);
	require(table.substitute(interleaved, env, args) == expected_interleaved);

	// A foreign environment is never substituted.
	require(table.substitute(foreign, env, args) == foreign);

	// Fail-closed invalid composition: a member object pointer whose pointee
	// substitutes to a function type must reject rather than build a bad MOP.
	rejects([&] {
		const TypeId function_args[] = {dependent_function};
		table.substitute(mop, env, function_args);
	});

	// Deep pointer nesting stays iterative: logical depth does not map to native
	// call depth.
	auto deep = dependent_function;
	for (size_t level = 0; level < 65536; ++level) {
		deep = table.pointer(deep);
	}
	const TypeId subst_deep = table.substitute(deep, env, args);
	TypeId cursor = subst_deep;
	for (size_t level = 0; level < 65536; ++level) {
		require(table.node(cursor).kind == CanonicalTypeKind::Pointer);
		cursor = table.node(cursor).child;
	}
	require(cursor == subst_function);

	// Reordered construction with unrelated insertions yields the same structure.
	CanonicalTypeTable reordered;
	const TypeId reordered_param = reordered.templateParameter(env, 0);
	const TypeId reordered_int = reordered.builtin(CanonicalBuiltinKind::Int);
	reordered.builtin(CanonicalBuiltinKind::Double);
	const TypeId reordered_params[] = {reordered_param};
	const TypeId reordered_function = reordered.function(
		reordered.builtin(CanonicalBuiltinKind::Void), reordered_params, true,
		CVQualifier::Const, ReferenceQualifier::LValueReference, false,
		CanonicalCallingConvention::Stdcall, CanonicalDllLinkage::Import, ExprId{});
	const TypeId reordered_args[] = {reordered_int};
	require(sameStructure(table, subst_function, reordered,
		reordered.substitute(reordered_function, env, reordered_args)));

	std::printf("callable substitution: node=%zu deep=65536\n", sizeof(CanonicalTypeNode));
}

inline void checkDependentTipResolve() {
	CanonicalTypeTable table;
	const TemplateDeclId env{7};
	const TypeId param0 = table.templateParameter(env, 0);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId owner = table.record(EntityId{21});
	const TypeId nested_record = table.record(EntityId{22});
	const TypeId args[] = {owner};

	const CanonicalNamedTypeMemberSpec owner_members[] = {
		{.name = "type", .type = integer},
		{.name = "Nested", .type = nested_record},
	};
	table.publishRecordNamedTypeMembers(EntityId{21}, owner_members);
	require(table.hasRecordNamedTypeMembers(EntityId{21}));
	table.publishRecordNamedTypeMembers(EntityId{21}, owner_members);
	require(table.tryLookupNamedTypeMember(EntityId{21}, "type") == integer);
	require(table.tryLookupNamedTypeMember(EntityId{21}, "missing") == std::nullopt);

	const CanonicalNamedTypeMemberSpec nested_members[] = {
		{.name = "item", .type = floating},
	};
	table.publishRecordNamedTypeMembers(EntityId{22}, nested_members);

	const CanonicalNamedTypeMemberSpec conflict[] = {
		{.name = "type", .type = floating},
	};
	rejects([&] { table.publishRecordNamedTypeMembers(EntityId{21}, conflict); });
	rejects([&] {
		const CanonicalNamedTypeMemberSpec duplicate[] = {
			{.name = "a", .type = integer},
			{.name = "a", .type = floating},
		};
		table.publishRecordNamedTypeMembers(EntityId{23}, duplicate);
	});
	rejects([&] { table.publishRecordNamedTypeMembers(EntityId{}, owner_members); });

	const TypeId subst_type = table.substitute(table.dependentName(param0, "type"), env, args);
	require(table.node(subst_type).kind == CanonicalTypeKind::DependentName);
	require(table.tryResolveDependentTip(subst_type) == integer);

	const TypeId subst_nested = table.substitute(
		table.dependentName(table.dependentName(param0, "Nested"), "item"), env, args);
	require(table.tryResolveDependentTip(subst_nested) == floating);

	const TypeId miss = table.substitute(table.dependentName(param0, "absent"), env, args);
	require(table.tryResolveDependentTip(miss) == miss);

	const TypeId builtin_tip = table.substitute(
		table.dependentName(table.templateParameter(env, 0), "type"), env,
		std::span<const TypeId>(&integer, 1));
	require(table.node(builtin_tip).kind == CanonicalTypeKind::DependentName);
	require(table.tryResolveDependentTip(builtin_tip) == builtin_tip);

	const TypeId template_member = table.substitute(
		table.dependentTemplateMember(param0, "Foo", std::span<const TypeId>(&integer, 1)),
		env, args);
	require(table.tryResolveDependentTip(template_member) == template_member);

	CanonicalTypeTable reordered;
	const TypeId reordered_owner = reordered.record(EntityId{21});
	const TypeId reordered_int = reordered.builtin(CanonicalBuiltinKind::Int);
	reordered.dependentName(reordered.templateParameter(env, 0), "unrelated");
	const CanonicalNamedTypeMemberSpec reordered_members[] = {
		{.name = "type", .type = reordered_int},
	};
	reordered.publishRecordNamedTypeMembers(EntityId{21}, reordered_members);
	const TypeId reordered_tip = reordered.substitute(
		reordered.dependentName(reordered.templateParameter(env, 0), "type"), env,
		std::span<const TypeId>(&reordered_owner, 1));
	require(reordered.tryResolveDependentTip(reordered_tip) == reordered_int);

	CanonicalTypeTransaction transaction(table);
	const CanonicalNamedTypeMemberSpec rolled[] = {
		{.name = "rolled", .type = integer},
	};
	table.publishRecordNamedTypeMembers(EntityId{24}, rolled);
	require(table.hasRecordNamedTypeMembers(EntityId{24}));
	transaction.rollback();
	require(!table.hasRecordNamedTypeMembers(EntityId{24}));

	std::printf("dependent tip resolve: member=%zu\n", sizeof(CanonicalNamedTypeMember));
}

inline void checkNttpSpecArgs() {
	CanonicalTypeTable table;
	const TemplateDeclId env{7};
	const TypeId param0 = table.templateParameter(env, 0);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const ExprId nttp_a{21};
	const ExprId nttp_b{22};
	const CanonicalTemplateArgument mixed_a[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeNonType(nttp_a),
	};
	const CanonicalTemplateArgument mixed_b[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeNonType(nttp_b),
	};
	const CanonicalTemplateArgument mixed_float[] = {
		CanonicalTemplateArgument::makeType(floating),
		CanonicalTemplateArgument::makeNonType(nttp_a),
	};
	const CanonicalTemplateArgument nttp_only[] = {
		CanonicalTemplateArgument::makeNonType(nttp_a),
	};
	const CanonicalTemplateArgument template_arg[] = {
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{13}),
	};
	const TypeId spec_a = table.templateSpecialization(TemplateDeclId{11}, mixed_a);
	const TypeId spec_b = table.templateSpecialization(TemplateDeclId{11}, mixed_b);
	const TypeId spec_float = table.templateSpecialization(TemplateDeclId{11}, mixed_float);
	const TypeId spec_nttp = table.templateSpecialization(TemplateDeclId{11}, nttp_only);
	const TypeId spec_template = table.templateSpecialization(TemplateDeclId{11}, template_arg);
	require(spec_a != spec_b && spec_a != spec_float && spec_a != spec_nttp &&
		spec_a != spec_template);
	require(table.templateSpecialization(TemplateDeclId{11}, mixed_a) == spec_a);
	require(table.templateSpecializationDecl(spec_a) == TemplateDeclId{11});
	const TypeId first = table.templateSpecializationArguments(spec_a);
	require(table.templateArgumentIsType(first));
	require(table.templateArgumentType(first) == integer);
	const TypeId second = table.templateArgumentNext(first);
	require(!table.templateArgumentIsType(second));
	require(table.templateArgumentExpr(second) == nttp_a);
	require(!table.templateArgumentNext(second));
	rejects([&] { (void)table.templateArgumentType(second); });
	rejects([&] { (void)table.templateArgumentExpr(first); });
	const TypeId template_link = table.templateSpecializationArguments(spec_template);
	require(table.templateArgumentKind(template_link) == CanonicalTemplateArgKind::Template);
	require(table.templateArgumentTemplate(template_link) == TemplateDeclId{13});
	require(!table.templateArgumentIsType(template_link));
	rejects([&] { (void)table.templateArgumentType(template_link); });
	rejects([&] { (void)table.templateArgumentExpr(template_link); });
	rejects([&] {
		const CanonicalTemplateArgument empty_template[] = {
			CanonicalTemplateArgument::makeTemplate(TemplateDeclId{}),
		};
		(void)table.templateSpecialization(TemplateDeclId{11}, empty_template);
	});
	rejects([&] {
		const CanonicalTemplateArgument empty_nttp[] = {
			CanonicalTemplateArgument::makeNonType(ExprId{}),
		};
		(void)table.templateSpecialization(TemplateDeclId{11}, empty_nttp);
	});

	const TypeId dependent = table.templateSpecialization(TemplateDeclId{11},
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(param0),
			CanonicalTemplateArgument::makeNonType(nttp_a),
		});
	const TypeId args[] = {integer};
	const TypeId subst = table.substitute(dependent, env, args);
	require(subst == spec_a);
	require(sameStructure(table, subst, table, spec_a));

	CanonicalTypeTable reordered;
	const TypeId reordered_int = reordered.builtin(CanonicalBuiltinKind::Int);
	reordered.dependentName(reordered.templateParameter(env, 0), "unrelated");
	const CanonicalTemplateArgument reordered_mixed[] = {
		CanonicalTemplateArgument::makeType(reordered_int),
		CanonicalTemplateArgument::makeNonType(nttp_a),
	};
	const TypeId reordered_spec =
		reordered.templateSpecialization(TemplateDeclId{11}, reordered_mixed);
	require(sameStructure(table, spec_a, reordered, reordered_spec));

	std::printf("nttp spec args: node=%zu\n", sizeof(CanonicalTypeNode));
}

inline void checkConcretePackSpecArgs() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId character = table.builtin(CanonicalBuiltinKind::Char);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const CanonicalTemplateArgument concrete_pack[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeType(character),
		CanonicalTemplateArgument::makeType(floating),
	};
	const TypeId spec = table.templateSpecialization(TemplateDeclId{29}, concrete_pack);
	const TypeId first = table.templateSpecializationArguments(spec);
	require(table.templateArgumentKind(first) == CanonicalTemplateArgKind::Type);
	require(table.templateArgumentType(first) == integer);
	const TypeId second = table.templateArgumentNext(first);
	require(table.templateArgumentKind(second) == CanonicalTemplateArgKind::Type);
	require(table.templateArgumentType(second) == character);
	const TypeId third = table.templateArgumentNext(second);
	require(table.templateArgumentKind(third) == CanonicalTemplateArgKind::Type);
	require(table.templateArgumentType(third) == floating);
	require(!table.templateArgumentNext(third));
	require(spec != table.templateSpecialization(TemplateDeclId{29},
		std::array<CanonicalTemplateArgument, 3>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeType(floating),
			CanonicalTemplateArgument::makeType(character),
		}));
}

inline void checkMemberAliasOwnerEnvironment() {
	CanonicalTypeTable table;
	const TemplateDeclId owner{80};
	const TemplateDeclId alias{81};
	const TypeId owner_param = table.templateParameter(owner, 0);
	const TypeId alias_param = table.templateParameter(alias, 0);
	const TypeId owner_arg = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId alias_arg = table.builtin(CanonicalBuiltinKind::Char);
	const TypeId owner_args[] = {owner_arg};
	const TypeId alias_args[] = {alias_arg};
	const CanonicalTemplateArgKind kinds[] = {CanonicalTemplateArgKind::Type};
	const TypeId target = table.templateSpecialization(TemplateDeclId{82},
		std::array<TypeId, 2>{owner_param, alias_param});
	require(table.dependsOnlyOnTemplateParameters(target, alias, owner));
	require(!table.dependsOnlyOnTemplateParameters(target, alias));
	require(!table.dependsOnlyOnTemplateParameters(
		table.templateParameter(TemplateDeclId{83}, 0), alias, owner));
	table.publishAliasTemplateTarget(alias, owner, target, kinds);
	const TypeId qualifier = table.templateSpecialization(owner, owner_args);
	const TypeId member_use = table.dependentTemplateMember(qualifier, "Pointer", alias_args);
	const TypeId member_argument = table.templateArgumentType(
		table.dependentTemplateMemberArguments(member_use));
	const TypeId resolved_member_args[] = {member_argument};
	const TypeId expected = table.templateSpecialization(TemplateDeclId{82},
		std::array<TypeId, 2>{owner_arg, alias_arg});
	require(table.resolveMemberAliasTarget(alias,
		table.dependentNameQualifier(member_use), resolved_member_args) == expected);
	require(table.resolveMemberAliasTarget(alias,
		table.dependentNameQualifier(member_use), resolved_member_args) !=
		table.templateSpecialization(TemplateDeclId{82},
			std::array<TypeId, 2>{alias_arg, owner_arg}));
	require(!table.resolveMemberAliasTarget(alias,
		table.templateSpecialization(TemplateDeclId{84}, owner_args), alias_args).has_value());
}

// The member-alias resolver is fail-closed: a partially dependent owner/member
// argument, a non-Type argument layout, or an owner specialization that does not
// cover the target's owner parameter references must return nullopt rather than
// substitute (a short layout would otherwise throw inside the worklist).
inline void checkMemberAliasOwnerEnvironmentFailClosed() {
	CanonicalTypeTable table;
	const TemplateDeclId owner{80};
	const TemplateDeclId alias{81};
	const TypeId owner_param = table.templateParameter(owner, 0);
	const TypeId alias_param = table.templateParameter(alias, 0);
	const TypeId owner_arg = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId alias_arg = table.builtin(CanonicalBuiltinKind::Char);
	const CanonicalTemplateArgKind kinds[] = {CanonicalTemplateArgKind::Type};
	const TypeId target = table.templateSpecialization(TemplateDeclId{82},
		std::array<TypeId, 2>{owner_param, alias_param});
	table.publishAliasTemplateTarget(alias, owner, target, kinds);
	const TypeId owner_args[] = {owner_arg};
	const TypeId alias_args[] = {alias_arg};
	const TypeId owner_spec = table.templateSpecialization(owner, owner_args);
	require(table.resolveMemberAliasTarget(alias, owner_spec, alias_args).has_value());

	// A dependent owner argument defers instead of producing a partially
	// dependent target.
	const TypeId dependent_owner_args[] = {table.templateParameter(TemplateDeclId{83}, 0)};
	require(!table.resolveMemberAliasTarget(alias,
		table.templateSpecialization(owner, dependent_owner_args), alias_args).has_value());

	// A dependent member argument defers for the same reason.
	const TypeId dependent_alias_args[] = {table.templateParameter(TemplateDeclId{83}, 0)};
	require(!table.resolveMemberAliasTarget(alias, owner_spec, dependent_alias_args).has_value());

	// An owner specialization that does not cover the target's owner parameter
	// reference defers instead of reaching substitution and throwing.
	const TypeId uncovered_owner_spec =
		table.templateSpecialization(owner, std::span<const TypeId>{});
	require(!table.resolveMemberAliasTarget(alias, uncovered_owner_spec, alias_args).has_value());

	// A non-Type owner argument kind is not directly representable.
	const CanonicalTemplateArgument non_type_owner[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{71}),
	};
	require(!table.resolveMemberAliasTarget(alias,
		table.templateSpecialization(owner, non_type_owner), alias_args).has_value());

	// A non-Type member parameter layout is not directly representable.
	const TemplateDeclId nttp_alias{84};
	const CanonicalTemplateArgKind nttp_kinds[] = {CanonicalTemplateArgKind::NonType};
	table.publishAliasTemplateTarget(nttp_alias, owner,
		table.templateParameter(nttp_alias, 0), nttp_kinds);
	require(!table.resolveMemberAliasTarget(nttp_alias, owner_spec, alias_args).has_value());

	std::printf("member alias owner environment: fail-closed\n");
}

inline void checkAliasRedirection() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId dummy_args[] = {integer};

	// Mixed concrete argument layouts are matched positionally against the
	// published parameter kinds. The non-type and template-template positions
	// keep their opaque ExprId / TemplateDeclId identity while the trailing type
	// position drives the redirect.
	const TemplateDeclId mixed_alias{41};
	const CanonicalTemplateArgKind mixed_kinds[] = {
		CanonicalTemplateArgKind::NonType,
		CanonicalTemplateArgKind::Template,
		CanonicalTemplateArgKind::Type,
	};
	table.publishAliasTemplateTarget(
		mixed_alias, table.templateParameter(mixed_alias, 2), mixed_kinds);
	const CanonicalTemplateArgument mixed_int[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{71}),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{72}),
		CanonicalTemplateArgument::makeType(integer),
	};
	const CanonicalTemplateArgument mixed_float[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{73}),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{74}),
		CanonicalTemplateArgument::makeType(floating),
	};
	require(table.substitute(
		table.aliasTemplateSpecialization(mixed_alias, mixed_int),
		mixed_alias, dummy_args) == integer);
	require(table.substitute(
		table.aliasTemplateSpecialization(mixed_alias, mixed_float),
		mixed_alias, dummy_args) == floating);

	// Unresolved alias specializations keep distinct ExprId identity even when
	// they eventually redirect to the same concrete type.
	const CanonicalTemplateArgument mixed_other_expr[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{75}),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{72}),
		CanonicalTemplateArgument::makeType(integer),
	};
	require(table.aliasTemplateSpecialization(mixed_alias, mixed_int) !=
		table.aliasTemplateSpecialization(mixed_alias, mixed_other_expr));
	require(table.substitute(
		table.aliasTemplateSpecialization(mixed_alias, mixed_other_expr),
		mixed_alias, dummy_args) == integer);

	// Publishing the same alias with a different parameter layout conflicts.
	const CanonicalTemplateArgKind conflicting_kinds[] = {
		CanonicalTemplateArgKind::Type,
		CanonicalTemplateArgKind::NonType,
		CanonicalTemplateArgKind::Type,
	};
	rejects([&] { table.publishAliasTemplateTarget(
		mixed_alias, table.templateParameter(mixed_alias, 2), conflicting_kinds); });

	// A target that names the alias's own template-template parameter by owner
	// and index is replaced by the concrete TemplateDeclId argument.
	const TemplateDeclId ttp_alias{51};
	const CanonicalTemplateArgKind ttp_kinds[] = {
		CanonicalTemplateArgKind::Type,
		CanonicalTemplateArgKind::Template,
	};
	const CanonicalTemplateArgument ttp_target_args[] = {
		CanonicalTemplateArgument::makeType(table.templateParameter(ttp_alias, 0)),
		CanonicalTemplateArgument::makeDependentTemplate(ttp_alias, 1),
	};
	table.publishAliasTemplateTarget(ttp_alias,
		table.templateSpecialization(TemplateDeclId{52}, ttp_target_args), ttp_kinds);
	const CanonicalTemplateArgument ttp_concrete[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{53}),
	};
	const CanonicalTemplateArgument ttp_expected_args[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{53}),
	};
	require(table.substitute(
		table.aliasTemplateSpecialization(ttp_alias, ttp_concrete),
		ttp_alias, dummy_args) ==
		table.templateSpecialization(TemplateDeclId{52}, ttp_expected_args));

	// Chains resolve iteratively across the mixed type/template layout.
	const TemplateDeclId chain_outer{81};
	const TemplateDeclId chain_inner{82};
	const CanonicalTemplateArgKind chain_kinds[] = {
		CanonicalTemplateArgKind::Type,
		CanonicalTemplateArgKind::Template,
	};
	const CanonicalTemplateArgument chain_outer_target_args[] = {
		CanonicalTemplateArgument::makeType(table.templateParameter(chain_outer, 0)),
		CanonicalTemplateArgument::makeDependentTemplate(chain_outer, 1),
	};
	table.publishAliasTemplateTarget(chain_outer,
		table.aliasTemplateSpecialization(chain_inner, chain_outer_target_args),
		chain_kinds);
	table.publishAliasTemplateTarget(chain_inner,
		table.templateParameter(chain_inner, 0), chain_kinds);
	const CanonicalTemplateArgument chain_concrete[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeTemplate(TemplateDeclId{83}),
	};
	require(table.substitute(
		table.aliasTemplateSpecialization(chain_outer, chain_concrete),
		chain_outer, dummy_args) == integer);

	// Self and mutual declaration-ID cycles leave the alias boundary instead of
	// looping.
	const TemplateDeclId self_cycle{91};
	const CanonicalTemplateArgKind cycle_kinds[] = {CanonicalTemplateArgKind::Type};
	const TypeId self_cycle_args[] = {table.templateParameter(self_cycle, 0)};
	table.publishAliasTemplateTarget(self_cycle,
		table.aliasTemplateSpecialization(self_cycle, self_cycle_args), cycle_kinds);
	const CanonicalTemplateArgument self_cycle_concrete[] = {
		CanonicalTemplateArgument::makeType(integer),
	};
	const TypeId self_cycle_spec =
		table.aliasTemplateSpecialization(self_cycle, self_cycle_concrete);
	require(table.substitute(self_cycle_spec, self_cycle, dummy_args) == self_cycle_spec);

	const TemplateDeclId mutual_a{92};
	const TemplateDeclId mutual_b{93};
	const TypeId mutual_a_args[] = {table.templateParameter(mutual_a, 0)};
	const TypeId mutual_b_args[] = {table.templateParameter(mutual_b, 0)};
	table.publishAliasTemplateTarget(mutual_a,
		table.aliasTemplateSpecialization(mutual_b, mutual_a_args), cycle_kinds);
	table.publishAliasTemplateTarget(mutual_b,
		table.aliasTemplateSpecialization(mutual_a, mutual_b_args), cycle_kinds);
	const TypeId mutual_result = table.substitute(
		table.aliasTemplateSpecialization(mutual_a, self_cycle_concrete),
		mutual_a, dummy_args);
	require(table.node(mutual_result).kind == CanonicalTypeKind::AliasTemplateSpecialization);

	// Fail-closed: arity mismatches, argument-kind mismatches, dependent type
	// arguments, dependent template-template arguments, and targets whose
	// non-type references cannot be proven concrete keep the alias boundary.
	const TemplateDeclId arity_alias{94};
	table.publishAliasTemplateTarget(arity_alias,
		table.templateParameter(arity_alias, 0), cycle_kinds);
	const TypeId arity_spec = table.aliasTemplateSpecialization(
		arity_alias,
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeType(floating),
		});
	require(table.substitute(arity_spec, arity_alias, dummy_args) == arity_spec);

	const TemplateDeclId kind_alias{95};
	const CanonicalTemplateArgKind kind_kinds[] = {
		CanonicalTemplateArgKind::NonType,
		CanonicalTemplateArgKind::Type,
	};
	table.publishAliasTemplateTarget(kind_alias,
		table.templateParameter(kind_alias, 1), kind_kinds);
	const TypeId kind_spec = table.aliasTemplateSpecialization(
		kind_alias,
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeType(floating),
		});
	require(table.substitute(kind_spec, kind_alias, dummy_args) == kind_spec);

	const TypeId dependent_arg_spec = table.aliasTemplateSpecialization(
		kind_alias,
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeNonType(ExprId{96}),
			CanonicalTemplateArgument::makeType(
				table.templateParameter(TemplateDeclId{97}, 0)),
		});
	require(table.substitute(dependent_arg_spec, kind_alias, dummy_args) ==
		dependent_arg_spec);

	const TypeId dependent_template_spec = table.aliasTemplateSpecialization(
		ttp_alias,
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeDependentTemplate(TemplateDeclId{98}, 0),
		});
	require(table.substitute(dependent_template_spec, ttp_alias, dummy_args) ==
		dependent_template_spec);

	const TemplateDeclId nttp_alias{99};
	const CanonicalTemplateArgKind nttp_kinds[] = {CanonicalTemplateArgKind::NonType};
	const CanonicalTemplateArgument nttp_target_args[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{100}),
	};
	table.publishAliasTemplateTarget(nttp_alias,
		table.templateSpecialization(TemplateDeclId{101}, nttp_target_args), nttp_kinds);
	const CanonicalTemplateArgument nttp_concrete[] = {
		CanonicalTemplateArgument::makeNonType(ExprId{102}),
	};
	const TypeId nttp_spec = table.aliasTemplateSpecialization(nttp_alias, nttp_concrete);
	require(table.substitute(nttp_spec, nttp_alias, dummy_args) == nttp_spec);

	// An opaque non-type argument in an intermediate alias target is preserved
	// by ExprId identity when the outer redirect rebuilds it. The inner alias has
	// no published target, so the chain stops and the rebuilt argument is
	// observable.
	const TemplateDeclId opaque_outer{103};
	const TemplateDeclId opaque_inner{104};
	const CanonicalTemplateArgKind opaque_outer_kinds[] = {CanonicalTemplateArgKind::Type};
	const CanonicalTemplateArgument opaque_outer_target[] = {
		CanonicalTemplateArgument::makeType(table.templateParameter(opaque_outer, 0)),
		CanonicalTemplateArgument::makeNonType(ExprId{105}),
	};
	table.publishAliasTemplateTarget(opaque_outer,
		table.aliasTemplateSpecialization(opaque_inner, opaque_outer_target),
		opaque_outer_kinds);
	const CanonicalTemplateArgument opaque_concrete[] = {
		CanonicalTemplateArgument::makeType(integer),
	};
	const TypeId opaque_result = table.substitute(
		table.aliasTemplateSpecialization(opaque_outer, opaque_concrete),
		opaque_outer, dummy_args);
	require(table.node(opaque_result).kind == CanonicalTypeKind::AliasTemplateSpecialization);
	const TypeId opaque_args = table.templateSpecializationArguments(opaque_result);
	require(table.templateArgumentType(opaque_args) == integer);
	const TypeId opaque_next = table.templateArgumentNext(opaque_args);
	require(table.templateArgumentKind(opaque_next) == CanonicalTemplateArgKind::NonType);
	require(table.templateArgumentExpr(opaque_next) == ExprId{105});

	std::printf("alias redirection: mixed args\n");
}

inline int run() {
	checkDependentNames();
	checkSubstitution();
	checkCallableSubstitution();
	checkDependentTipResolve();
	checkNttpSpecArgs();
	checkConcretePackSpecArgs();
	checkAliasRedirection();
	checkMemberAliasOwnerEnvironment();
	checkMemberAliasOwnerEnvironmentFailClosed();
	checkTransactions();
	checkAdapter();
	checkTemplateDeclPublication();
	static_assert(!std::is_copy_constructible_v<CanonicalTypeTable>);
	static_assert(!std::is_move_constructible_v<CanonicalTypeTable>);
	static_assert(!std::is_convertible_v<TelemetryTypeId, TypeId>);
	static_assert(!std::is_convertible_v<TypeId, TelemetryTypeId>);
	static_assert(!std::is_invocable_v<decltype(&CanonicalTypeTable::pointer),
		CanonicalTypeTable&, TelemetryTypeId>);
	static_assert(!LegacyChunkedAnyStorageTraits<CanonicalTypeNode, true>::allowed);
	CanonicalTypeTable table;
	std::array<TypeId, static_cast<size_t>(CanonicalBuiltinKind::Count)> builtins{};
	for (size_t i = 0; i < builtins.size(); ++i) {
		builtins[i] = table.builtin(static_cast<CanonicalBuiltinKind>(i));
		for (size_t j = 0; j < i; ++j) {
			require(builtins[i] != builtins[j]);
		}
	}
	const auto integer = table.builtin(CanonicalBuiltinKind::Int);
	const auto floating = table.builtin(CanonicalBuiltinKind::Double);
	const auto ci = table.qualify(integer, CVQualifier::Const);
	const auto vi = table.qualify(integer, CVQualifier::Volatile);
	const auto cvi = table.qualify(ci, CVQualifier::Volatile);
	require(cvi == table.qualify(vi, CVQualifier::Const));
	require(ci == table.qualify(ci, CVQualifier::Const));
	require(integer == table.qualify(integer, CVQualifier::None));
	require(ci != vi && cvi != ci && cvi != vi);
	const auto pointer = table.pointer(integer);
	require(pointer != table.pointer(floating));
	require(table.pointer(ci) != table.qualify(pointer, CVQualifier::Const));
	require(table.node(table.pointer(ci)).child == ci);
	const auto array2 = table.array(integer, 2);
	const auto array3 = table.array(integer, 3);
	const auto unknown_array = table.arrayOfUnknownBound(integer);
	require(array2 != array3 && array2 != unknown_array && array3 != unknown_array);
	require(table.array(floating, 2) != array2);
	require(table.array(table.array(integer, 3), 2) != table.array(table.array(integer, 2), 3));
	require(table.array(pointer, 2) != table.pointer(array2));
	require(table.qualify(array2, CVQualifier::Const) == table.array(ci, 2));
	const auto lref = table.reference(integer, ReferenceQualifier::LValueReference);
	const auto rref = table.reference(integer, ReferenceQualifier::RValueReference);
	require(lref != rref && lref != pointer && rref != pointer);
	for (auto outer : {ReferenceQualifier::LValueReference, ReferenceQualifier::RValueReference}) {
		require(table.reference(lref, outer) == lref);
		require(table.reference(rref, outer) ==
			(outer == ReferenceQualifier::LValueReference ? lref : rref));
	}
	require(table.qualify(lref, CVQualifier::ConstVolatile) == lref);
	require(table.reference(ci, ReferenceQualifier::LValueReference) != lref);
	require(table.reference(array2, ReferenceQualifier::LValueReference) != array2);
	const auto void_fn = table.function(table.builtin(CanonicalBuiltinKind::Void),
		std::span<const TypeId>{}, false, CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId int_param[] = {integer};
	const auto int_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto variadic_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, true,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto const_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::Const, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto ref_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::LValueReference, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto noexcept_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, true, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto stdcall_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Stdcall,
		CanonicalDllLinkage::None, ExprId{});
	const auto dllimport_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::Import, ExprId{});
	const auto dependent_noexcept_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{11});
	const auto other_dependent_noexcept_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{12});
	require(void_fn != int_fn && int_fn != variadic_fn && int_fn != const_fn);
	require(int_fn != ref_fn && int_fn != noexcept_fn && const_fn != ref_fn);
	require(int_fn != stdcall_fn && int_fn != dllimport_fn && stdcall_fn != dllimport_fn);
	require(int_fn != dependent_noexcept_fn && dependent_noexcept_fn != other_dependent_noexcept_fn);
	require(dependent_noexcept_fn != noexcept_fn);
	require(table.functionDependentNoexcept(dependent_noexcept_fn).value == 11);
	require(table.functionDependentNoexcept(int_fn).value == 0);
	require(hasCanonicalTypeNodeFlag(table.node(dependent_noexcept_fn).flags,
		CanonicalTypeNodeFlags::DependentNoexceptFunction));
	require(table.node(stdcall_fn).builtin ==
		static_cast<CanonicalBuiltinKind>(CanonicalCallingConvention::Stdcall));
	require(hasCanonicalTypeNodeFlag(table.node(dllimport_fn).flags,
		CanonicalTypeNodeFlags::FunctionDllImport));
	require(table.qualify(int_fn, CVQualifier::Const) == const_fn);
	require(table.pointer(int_fn) != int_fn);
	require(table.functionParameters(void_fn).value == 0);
	require(table.functionParameterType(table.functionParameters(int_fn)) == integer);
	require(table.functionParameters(dependent_noexcept_fn).value != 0);
	require(table.functionParameterType(table.functionParameters(dependent_noexcept_fn)) == integer);
	require(table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default,
		CanonicalDllLinkage::None, ExprId{11}) == dependent_noexcept_fn);
	rejects([&] {
		table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
			CVQualifier::None, ReferenceQualifier::None, true, CanonicalCallingConvention::Default,
			CanonicalDllLinkage::None, ExprId{11});
	});
	require(table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}) == int_fn);
	const auto owner_a = table.record(EntityId{1});
	const auto owner_b = table.record(EntityId{2});
	const auto enum_a = table.enumeration(EntityId{3});
	const auto enum_b = table.enumeration(EntityId{4});
	require(owner_a != owner_b);
	require(enum_a != enum_b && enum_a != owner_a);
	require(table.record(EntityId{1}) == owner_a);
	require(table.enumeration(EntityId{3}) == enum_a);
	require(table.recordEntity(owner_a).value == 1);
	require(table.enumEntity(enum_a).value == 3);
	const auto tmpl_a = table.templateParameter(TemplateDeclId{8}, 0);
	const auto tmpl_b = table.templateParameter(TemplateDeclId{8}, 1);
	const auto tmpl_other = table.templateParameter(TemplateDeclId{9}, 0);
	require(tmpl_a != tmpl_b && tmpl_a != tmpl_other && tmpl_a != owner_a && tmpl_a != enum_a);
	require(table.templateParameter(TemplateDeclId{8}, 0) == tmpl_a);
	require(table.templateParameterDecl(tmpl_a).value == 8);
	require(table.templateParameterIndex(tmpl_a) == 0);
	require(table.templateParameterIndex(tmpl_b) == 1);
	require(table.pointer(tmpl_a) != tmpl_a);
	require(table.array(tmpl_a, 2) != table.array(tmpl_b, 2));
	rejects([&] { table.templateParameter(TemplateDeclId{}, 0); });
	rejects([&] { table.templateParameterDecl(owner_a); });
	const TypeId pair_int_float[] = {integer, floating};
	const TypeId pair_float_int[] = {floating, integer};
	const auto spec_a = table.templateSpecialization(TemplateDeclId{11}, pair_int_float);
	const auto spec_b = table.templateSpecialization(TemplateDeclId{11}, pair_float_int);
	const auto spec_other = table.templateSpecialization(TemplateDeclId{12}, pair_int_float);
	const auto spec_empty = table.templateSpecialization(TemplateDeclId{11}, std::span<const TypeId>{});
	require(spec_a != spec_b && spec_a != spec_other && spec_a != spec_empty && spec_a != tmpl_a);
	require(table.templateSpecialization(TemplateDeclId{11}, pair_int_float) == spec_a);
	require(table.templateSpecializationDecl(spec_a).value == 11);
	require(table.templateArgumentType(table.templateSpecializationArguments(spec_a)) == integer);
	require(table.templateArgumentType(table.templateArgumentNext(
		table.templateSpecializationArguments(spec_a))) == floating);
	require(!table.templateSpecializationArguments(spec_empty));
	require(table.pointer(spec_a) != spec_a);
	rejects([&] { table.templateSpecialization(TemplateDeclId{}, pair_int_float); });
	rejects([&] { table.templateSpecializationDecl(tmpl_a); });
	const auto mop_a = table.memberObjectPointer(owner_a, integer);
	const auto mop_b = table.memberObjectPointer(owner_b, integer);
	const auto mop_float = table.memberObjectPointer(owner_a, floating);
	require(mop_a != mop_b && mop_a != mop_float && mop_a != pointer);
	require(table.memberPointerOwner(mop_a) == owner_a);
	require(table.memberPointerPointee(mop_a) == integer);
	const auto mfp_a = table.memberFunctionPointer(owner_a, int_fn);
	const auto mfp_b = table.memberFunctionPointer(owner_b, int_fn);
	const auto mfp_const = table.memberFunctionPointer(owner_a, const_fn);
	require(mfp_a != mfp_b && mfp_a != mfp_const && mfp_a != mop_a);
	require(table.memberPointerOwner(mfp_a) == owner_a);
	require(table.memberPointerPointee(mfp_a) == int_fn);
	require(table.qualify(mop_a, CVQualifier::Const) != mop_a);
	const auto array17_pointer_ci = table.array(table.pointer(ci), 17);
	const auto count = table.size();
	// Reordered requests, with unrelated spelling-table insertions between them.
	StringTable::getOrInternStringHandle("unrelated canonical type spelling B");
	StringTable::getOrInternStringHandle("unrelated canonical type spelling A");
	for (size_t i = builtins.size(); i-- > 0;) {
		require(table.builtin(static_cast<CanonicalBuiltinKind>(i)) == builtins[i]);
	}
	require(table.pointer(integer) == pointer);
	require(table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}) == int_fn);
	require(table.record(EntityId{1}) == owner_a);
	require(table.enumeration(EntityId{3}) == enum_a);
	require(table.templateParameter(TemplateDeclId{8}, 0) == tmpl_a);
	require(table.templateSpecialization(TemplateDeclId{11}, pair_int_float) == spec_a);
	require(table.memberObjectPointer(owner_a, integer) == mop_a);
	require(table.memberFunctionPointer(owner_a, int_fn) == mfp_a);
	require(table.size() == count);
	CanonicalTypeTable reordered;
	for (size_t i = builtins.size(); i-- > 0;) {
		reordered.builtin(static_cast<CanonicalBuiltinKind>(i));
	}
	const auto reordered_int = reordered.builtin(CanonicalBuiltinKind::Int);
	const auto reordered_ci = reordered.qualify(reordered_int, CVQualifier::Const);
	require(sameStructure(table, table.pointer(ci), reordered, reordered.pointer(reordered_ci)));
	require(sameStructure(table, array17_pointer_ci, reordered,
		reordered.array(reordered.pointer(reordered_ci), 17)));
	require(!sameStructure(table, table.qualify(pointer, CVQualifier::Const),
		reordered, reordered.pointer(reordered_ci)));
	const TypeId reordered_params[] = {reordered_int};
	require(sameStructure(table, table.pointer(int_fn), reordered,
		reordered.pointer(reordered.function(reordered.builtin(CanonicalBuiltinKind::Void),
			reordered_params, false, CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}))));
	const auto reordered_owner = reordered.record(EntityId{1});
	const auto reordered_enum = reordered.enumeration(EntityId{3});
	const auto reordered_tmpl = reordered.templateParameter(TemplateDeclId{8}, 0);
	require(sameStructure(table, enum_a, reordered, reordered_enum));
	require(sameStructure(table, tmpl_a, reordered, reordered_tmpl));
	require(!sameStructure(table, tmpl_a, reordered, reordered.templateParameter(TemplateDeclId{8}, 1)));
	const TypeId reordered_pair[] = {
		reordered_int,
		reordered.builtin(CanonicalBuiltinKind::Double),
	};
	const auto reordered_spec = reordered.templateSpecialization(TemplateDeclId{11}, reordered_pair);
	require(sameStructure(table, spec_a, reordered, reordered_spec));
	require(!sameStructure(table, spec_a, reordered,
		reordered.templateSpecialization(TemplateDeclId{12}, reordered_pair)));
	require(sameStructure(table, mop_a, reordered,
		reordered.memberObjectPointer(reordered_owner, reordered_int)));
	require(sameStructure(table, mfp_a, reordered,
		reordered.memberFunctionPointer(reordered_owner,
			reordered.function(reordered.builtin(CanonicalBuiltinKind::Void), reordered_params, false,
				CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}))));
	require(!sameStructure(table, mop_a, reordered,
		reordered.memberObjectPointer(reordered.record(EntityId{2}), reordered_int)));
	rejects([&] { table.node(TypeId{}); });
	rejects([&] { table.pointer(TypeId{0xFFFFFFFFu}); });
	rejects([&] { table.pointer(lref); });
	rejects([&] { table.array(lref, 2); });
	rejects([&] { table.array(table.builtin(CanonicalBuiltinKind::Void), 2); });
	rejects([&] { table.array(integer, 0); });
	rejects([&] { table.array(int_fn, 2); });
	rejects([&] { table.reference(table.builtin(CanonicalBuiltinKind::Void), ReferenceQualifier::LValueReference); });
	rejects([&] { table.reference(integer, ReferenceQualifier::None); });
	rejects([&] { table.qualify(integer, static_cast<CVQualifier>(4)); });
	rejects([&] { table.builtin(CanonicalBuiltinKind::Count); });
	rejects([&] {
		table.function(int_fn, std::span<const TypeId>{}, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	});
	rejects([&] {
		const TypeId bad_param[] = {int_fn};
		table.function(table.builtin(CanonicalBuiltinKind::Void), bad_param, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	});
	rejects([&] { table.record(EntityId{}); });
	rejects([&] { table.enumeration(EntityId{}); });
	rejects([&] { table.enumEntity(owner_a); });
	rejects([&] { table.memberObjectPointer(integer, integer); });
	rejects([&] { table.memberObjectPointer(owner_a, int_fn); });
	rejects([&] { table.memberFunctionPointer(owner_a, integer); });
	require(table.size() == count);
	// Source-controlled nesting does not grow the native stack in the table.
	std::vector<TypeId> chain{integer};
	for (int i = 0; i < 65536; ++i) {
		chain.push_back((i & 1) == 0 ? table.pointer(chain.back()) : table.array(chain.back(), 2));
	}
	for (size_t i = chain.size(); --i > 0;) {
		require(table.node(chain[i]).child == chain[i - 1]);
		require(((i - 1) & 1) == 0 ? table.pointer(chain[i - 1]) == chain[i] :
			table.array(chain[i - 1], 2) == chain[i]);
	}
	const auto stats = table.arenaStats();
	require(stats.used_bytes == table.size() * sizeof(CanonicalTypeNode));
	require(stats.reserved_bytes >= stats.used_bytes);
	std::printf("canonical types: shallow=%zu deep=%zu record=%zu table=%zu used=%llu reserved=%llu\n",
		count, table.size(), sizeof(CanonicalTypeNode), sizeof(CanonicalTypeTable),
		static_cast<unsigned long long>(stats.used_bytes), static_cast<unsigned long long>(stats.reserved_bytes));
	return 0;
}

} // namespace CanonicalTypeTests
