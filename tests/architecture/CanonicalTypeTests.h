#pragma once

#include <array>
#include <cstdio>
#include <source_location>
#include <sstream>
#include <type_traits>
#include <vector>

#include "CanonicalShapeQueries.h"
#include "CanonicalTypeAdapter.h"
#include "CanonicalTypeIdentityShape.h"
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
		if (a.kind == CanonicalTypeKind::DependentMemberAlias) {
			if (left.dependentMemberAliasDecl(left_id) !=
				right.dependentMemberAliasDecl(right_id)) {
				return false;
			}
			TypeId left_arg = left.dependentMemberAliasArguments(left_id);
			TypeId right_arg = right.dependentMemberAliasArguments(right_id);
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
		if (a.kind == CanonicalTypeKind::NestedRecord) {
			if (left.nestedRecordEntity(left_id) != right.nestedRecordEntity(right_id)) {
				return false;
			}
			left_id = left.nestedRecordOwner(left_id);
			right_id = right.nestedRecordOwner(right_id);
			continue;
		}
		if (a.kind == CanonicalTypeKind::TemplateSpecialization) {
			// The packed extent holds both the primary TemplateDeclId and an
			// optional member owner TypeId. Owner slots are table-local, so the
			// owner has to be compared structurally rather than numerically.
			if (unpackTemplateSpecializationDecl(a.array_extent) !=
				unpackTemplateSpecializationDecl(b.array_extent)) {
				return false;
			}
			const TypeId left_owner = unpackTemplateSpecializationOwner(a.array_extent);
			const TypeId right_owner = unpackTemplateSpecializationOwner(b.array_extent);
			if (static_cast<bool>(left_owner) != static_cast<bool>(right_owner)) {
				return false;
			}
			if (left_owner && !sameStructure(left, left_owner, right, right_owner)) {
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
				} else if (left_kind == CanonicalTemplateArgKind::Value) {
					if (!sameStructure(left, left.templateArgumentValue(left_arg),
						right, right.templateArgumentValue(right_arg))) {
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
		if (a.kind == CanonicalTypeKind::IntegralValue ||
			a.kind == CanonicalTypeKind::FloatingValue) {
			// The extent holds the converted bit pattern, which is independent of
			// table-local ids. Two different non-type arguments must not compare
			// equal just because their value types match.
			if (a.array_extent != b.array_extent) {
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

	TypeSpecifierNode interleaved_syntax(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	interleaved_syntax.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
	});
	require(!interleaved_syntax.ordered_declarator_has_legacy_projection());
	const CanonicalTypeImport interleaved =
		importCanonicalType(table, interleaved_syntax);
	require(interleaved.status == CanonicalTypeImportStatus::Supported);
	const TypeId expected_interleaved = table.pointer(
		table.array(table.pointer(table.array(int_type, 4)), 3));
	require(interleaved.type == expected_interleaved);
	require(interleaved.type != pointee.type);
	require(interleaved.type != ordinary.type);

	TypeSpecifierNode deeper_syntax(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	deeper_syntax.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::Volatile),
		DeclaratorComponent::array(3),
		DeclaratorComponent::array(5),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	const CanonicalTypeImport deeper = importCanonicalType(table, deeper_syntax);
	require(deeper.status == CanonicalTypeImportStatus::Supported);
	require(deeper.type == table.qualify(
		table.pointer(table.array(table.qualify(
			table.pointer(table.array(table.array(table.pointer(int_type), 5), 3)),
			CVQualifier::Volatile), 2)),
		CVQualifier::Const));
	TypeSpecifierNode different_interleaving = interleaved_syntax;
	different_interleaving.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(5),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(6),
	});
	require(!interleaved_syntax.has_same_ordered_declarator(
		different_interleaving));
	TypeSpecifierNode projectable_syntax(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	projectable_syntax.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	require(projectable_syntax.ordered_declarator_has_legacy_projection());
	TypeSpecifierNode legacy_projectable_syntax = projectable_syntax;
	legacy_projectable_syntax.clear_ordered_declarator();
	require(projectable_syntax.has_same_ordered_declarator(
		legacy_projectable_syntax));
	TypeSpecifierNode different_legacy_bound = legacy_projectable_syntax;
	const size_t different_bound[] = {4};
	different_legacy_bound.set_array_dimensions(different_bound);
	require(!projectable_syntax.has_same_ordered_declarator(
		different_legacy_bound));
	// Alias cv sinks onto the first pointer-like component, is consumed by a
	// reference, and otherwise carries to the next level.
	std::vector<DeclaratorComponent> alias_components{
		DeclaratorComponent::pointer(CVQualifier::None)};
	CVQualifier pending_alias_cv = CVQualifier::Const;
	require(sinkAliasCvOntoComponents(alias_components, pending_alias_cv));
	require(alias_components == std::vector<DeclaratorComponent>{
		DeclaratorComponent::pointer(CVQualifier::Const)});

	TypeSpecifierNode plain_pointer_alias(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	plain_pointer_alias.add_pointer_level(CVQualifier::None);
	std::vector<DeclaratorComponent> composed_alias_components;
	CVQualifier composed_pending_cv = CVQualifier::Const;
	appendOrderedAliasDeclaratorShape(composed_alias_components, plain_pointer_alias, composed_pending_cv);
	require(composed_alias_components == std::vector<DeclaratorComponent>{
		DeclaratorComponent::pointer(CVQualifier::Const)});
	require(composed_pending_cv == CVQualifier::None);

	TypeSpecifierNode base_cv_alias(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	composed_alias_components.clear();
	composed_pending_cv = CVQualifier::None;
	appendOrderedAliasDeclaratorShape(composed_alias_components, base_cv_alias, composed_pending_cv);
	appendOrderedAliasDeclaratorShape(composed_alias_components, plain_pointer_alias, composed_pending_cv);
	require(composed_alias_components == std::vector<DeclaratorComponent>{
		DeclaratorComponent::pointer(CVQualifier::Const)});
	require(composed_pending_cv == CVQualifier::None);

	TypeSpecifierNode const_pointer_alias = interleaved_syntax;
	const_pointer_alias.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::Const),
	});
	require(importCanonicalType(table, const_pointer_alias).type ==
		table.pointer(table.array(table.pointer(table.array(
			table.qualify(table.pointer(int_type), CVQualifier::Const), 4)), 3)));
	const CanonicalDeclaratorExport exported =
		exportCanonicalDeclarator(table, interleaved.type);
	require(exported.status == CanonicalTypeImportStatus::Supported);
	require(exported.base == int_type);
	require(exported.components.size() ==
		interleaved_syntax.declarator_components().size());
	require(std::equal(
		exported.components.begin(),
		exported.components.end(),
		interleaved_syntax.declarator_components().begin()));
	TypeSpecifierNode round_trip(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	round_trip.set_ordered_declarator(exported.components);
	require(importCanonicalType(table, round_trip).type == interleaved.type);

	CanonicalTypeTable reordered_interleaving;
	reordered_interleaving.builtin(CanonicalBuiltinKind::Double);
	const TypeId reordered_int =
		reordered_interleaving.builtin(CanonicalBuiltinKind::Int);
	TypeSpecifierNode reordered_syntax = interleaved_syntax;
	const TypeId reordered_type =
		importCanonicalType(reordered_interleaving, reordered_syntax).type;
	require(sameStructure(
		table, interleaved.type, reordered_interleaving, reordered_type));
	TypeSpecifierNode nominal_parameter(
		TypeCategory::Struct, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	nominal_parameter.set_type_entity(EntityId{72});
	nominal_parameter.set_ordered_declarator({
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	const TypeId nominal_base = table.record(EntityId{72});
	require(importCanonicalFunctionParameterType(table, nominal_parameter).type ==
		table.pointer(table.pointer(
			table.array(table.pointer(nominal_base), 3))));
	const TypeId owner = table.record(EntityId{71});
	const TypeId member_variant = table.pointer(table.array(
		table.memberObjectPointer(owner, table.qualify(int_type, CVQualifier::Const)), 3));
	require(member_variant != interleaved.type);
	require(table.memberPointerOwner(table.node(table.node(member_variant).child).child) == owner);
	(void)reordered_int;

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

	// Export projects a structural member pointer back to an ordered declarator
	// carrying the published owner EntityId, and re-importing that spine
	// reproduces the identical canonical node.
	const CanonicalDeclaratorExport exported_mop =
		exportCanonicalDeclarator(table, imported_mop.type);
	require(exported_mop.status == CanonicalTypeImportStatus::Supported);
	require(exported_mop.components.size() == 1);
	require(exported_mop.components.front().kind ==
		DeclaratorComponentKind::MemberObjectPointer);
	require(exported_mop.components.front().member_owner == EntityId{7});
	require(exported_mop.components.front().cv_qualifier == CVQualifier::None);
	require(exported_mop.base == table.builtin(CanonicalBuiltinKind::Int));

	TypeSpecifierNode roundtrip_mop(TypeCategory::Int, TypeQualifier::None, 32, Token{},
		CVQualifier::None);
	roundtrip_mop.set_ordered_declarator(exported_mop.components);
	const auto reimported_mop = importCanonicalType(table, roundtrip_mop);
	require(reimported_mop.status == CanonicalTypeImportStatus::Supported);
	require(reimported_mop.type == imported_mop.type);

	const CanonicalDeclaratorExport exported_mfp =
		exportCanonicalDeclarator(table, imported_mfp.type);
	require(exported_mfp.status == CanonicalTypeImportStatus::Supported);
	require(exported_mfp.components.size() == 1);
	require(exported_mfp.components.front().kind ==
		DeclaratorComponentKind::MemberFunctionPointer);
	require(exported_mfp.components.front().member_owner == EntityId{7});
	require(exported_mfp.base == imported_member.type);

	TypeSpecifierNode roundtrip_mfp(TypeCategory::MemberFunctionPointer, TypeQualifier::None, 64,
		Token{}, CVQualifier::None);
	roundtrip_mfp.set_function_signature(member_signature);
	roundtrip_mfp.set_ordered_declarator(exported_mfp.components);
	const auto reimported_mfp = importCanonicalType(table, roundtrip_mfp);
	require(reimported_mfp.status == CanonicalTypeImportStatus::Supported);
	require(reimported_mfp.type == imported_mfp.type);

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
	TypeSpecifierNode ordered_specialization = stamped_specialization;
	ordered_specialization.set_ordered_declarator({
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	require(!ordered_specialization.ordered_declarator_has_legacy_projection());
	const TypeId pair_type = table.templateSpecialization(TemplateDeclId{11}, expected_args);
	const TypeId pointer_to_pair_pointer_array = table.pointer(table.array(table.pointer(pair_type), 3));
	const TypeId const_element = table.qualify(pointer_to_pair_pointer_array, CVQualifier::Const);
	require(importCanonicalType(table, ordered_specialization).type == table.array(const_element, 2));
	require(importCanonicalFunctionParameterType(table, ordered_specialization).type == table.pointer(const_element));
	constexpr size_t ordered_specialization_depth = 4096;
	std::vector<DeclaratorComponent> deep_specialization_shape(ordered_specialization_depth,
		DeclaratorComponent::pointer(CVQualifier::None));
	deep_specialization_shape[1] = DeclaratorComponent::array(3);
	ordered_specialization.set_ordered_declarator(deep_specialization_shape);
	TypeId expected_deep_specialization = pair_type;
	for (auto component = deep_specialization_shape.rbegin(); component != deep_specialization_shape.rend(); ++component) {
		expected_deep_specialization = component->kind == DeclaratorComponentKind::Array ? table.array(expected_deep_specialization, 3)
			: table.pointer(expected_deep_specialization);
	}
	require(importCanonicalType(table, ordered_specialization).type == expected_deep_specialization);
	const TypeId member_owner_short = table.templateSpecialization(
		TemplateDeclId{401}, std::array<TypeId, 1>{table.builtin(CanonicalBuiltinKind::Short)});
	const TypeId member_owner_long = table.templateSpecialization(
		TemplateDeclId{401}, std::array<TypeId, 1>{table.builtin(CanonicalBuiltinKind::LongLong)});
	const CanonicalTemplateArgument member_int_args[] = {
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int)),
	};
	const CanonicalTemplateArgument member_char_args[] = {
		CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Char)),
	};
	const TypeId box_short = table.memberTemplateSpecialization(
		TemplateDeclId{402}, member_owner_short, member_int_args);
	const TypeId box_long = table.memberTemplateSpecialization(
		TemplateDeclId{402}, member_owner_long, member_int_args);
	const TypeId inner_short = table.memberTemplateSpecialization(
		TemplateDeclId{403}, box_short, member_char_args);
	const TypeId inner_long = table.memberTemplateSpecialization(
		TemplateDeclId{403}, box_long, member_char_args);
	require(inner_short == table.memberTemplateSpecialization(
		TemplateDeclId{403}, box_short, member_char_args));
	require(inner_short != inner_long);
	require(inner_short != box_short);
	require(table.templateSpecializationDecl(inner_short) == TemplateDeclId{403});
	require(table.templateSpecializationOwner(inner_short) == box_short);
	require(table.templateSpecializationDecl(table.templateSpecializationOwner(inner_short)) ==
		TemplateDeclId{402});
	require(table.templateSpecializationOwner(box_short) == member_owner_short);
	require(table.templateArgumentType(table.templateSpecializationArguments(inner_short)) ==
		table.builtin(CanonicalBuiltinKind::Char));
	require(table.templateArgumentType(table.templateSpecializationArguments(box_short)) ==
		table.builtin(CanonicalBuiltinKind::Int));
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
		std::vector<SpecNonTypeArgument>{ExprId{21}},
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
	const TypeId integral_one = table.integralValue(table.builtin(CanonicalBuiltinKind::UnsignedInt), 1);
	const TypeId integral_two = table.integralValue(table.builtin(CanonicalBuiltinKind::UnsignedInt), 2);
	TypeSpecifierNode mixed_values(TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	mixed_values.set_template_specialization_mixed(TemplateDeclId{12},
		{SpecTemplateArgKind::Value, SpecTemplateArgKind::NonType, SpecTemplateArgKind::Value}, {},
		{SpecNonTypeArgument{integral_one}, SpecNonTypeArgument{ExprId{21}}, SpecNonTypeArgument{integral_two}}, {}, {});
	const CanonicalTemplateArgument expected_values[] = {
		CanonicalTemplateArgument::makeValue(integral_one), CanonicalTemplateArgument::makeNonType(ExprId{21}),
		CanonicalTemplateArgument::makeValue(integral_two),
	};
	require(importCanonicalType(table, mixed_values).type == table.templateSpecialization(TemplateDeclId{12}, expected_values));
	require(mixed_values.specialization_arg_value(0) == integral_one);
	require(mixed_values.specialization_arg_expr(1) == ExprId{21});
	require(mixed_values.specialization_arg_value(2) == integral_two);
	TypeSpecifierNode template_specialization(TypeCategory::Template, TypeQualifier::None, 0, Token{},
		CVQualifier::None);
	template_specialization.set_template_specialization_mixed(
		TemplateDeclId{11},
		std::vector<SpecTemplateArgKind>{SpecTemplateArgKind::Template, SpecTemplateArgKind::Type},
		std::vector<TypeSpecifierNode>{int_arg},
		std::vector<SpecNonTypeArgument>{},
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
	const TypeId multidimensional_array_argument = table.array(
		table.array(table.builtin(CanonicalBuiltinKind::Int), 4), 3);
	const CanonicalTemplateArgument array_template_args[] = {
		CanonicalTemplateArgument::makeType(multidimensional_array_argument),
	};
	const TypeId array_type_arguments[] = {multidimensional_array_argument};
	const TypeId array_template_specialization = table.templateSpecialization(
		TemplateDeclId{14}, array_template_args);
	const TypeId array_template_arg_link =
		table.templateSpecializationArguments(array_template_specialization);
	require(table.templateArgumentType(array_template_arg_link) ==
		multidimensional_array_argument);
	const TypeId array_argument_owner = table.templateParameter(TemplateDeclId{15}, 0);
	const TypeId array_dependent_member = table.dependentTemplateMember(
		array_argument_owner, "Member", std::span<const TypeId>(array_type_arguments));
	require(table.templateArgumentType(
		table.dependentTemplateMemberArguments(array_dependent_member)) ==
		multidimensional_array_argument);
	const TypeId array_dependent_alias = table.dependentMemberAlias(
		array_argument_owner, TemplateDeclId{16},
		std::span<const TypeId>(array_type_arguments));
	require(table.templateArgumentType(
		table.dependentMemberAliasArguments(array_dependent_alias)) ==
		multidimensional_array_argument);
	TypeSpecifierNode dependent_template_specialization(
		TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	dependent_template_specialization.set_template_specialization_mixed(
		TemplateDeclId{11},
		std::vector<SpecTemplateArgKind>{
			SpecTemplateArgKind::DependentTemplate,
			SpecTemplateArgKind::Type},
		std::vector<TypeSpecifierNode>{int_arg},
		std::vector<SpecNonTypeArgument>{},
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
			std::vector<SpecNonTypeArgument>{ExprId{}},
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
	const TypeId record_with_properties = table.record(EntityId{3});
	const CanonicalRecordFacts record_property_facts =
		CanonicalRecordFacts::Polymorphic |
		CanonicalRecordFacts::Final;
	const CanonicalRecordProperties record_properties{
		record_with_properties, record_property_facts};
	table.publishRecordProperties(record_with_properties, record_property_facts);
	require(table.hasRecordProperties(record_with_properties));
	require(table.recordProperties(record_with_properties) == record_properties);
	table.publishRecordProperties(record_with_properties, record_property_facts);
	rejects([&] {
		table.publishRecordProperties(
			record_with_properties, CanonicalRecordFacts::Abstract);
	});
	rejects([&] {
		table.publishRecordProperties(
			table.builtin(CanonicalBuiltinKind::Int),
			CanonicalRecordFacts::None);
	});
	const CanonicalEnumLayout enum_layout{
		.entity = EntityId{5},
		.underlying_type = table.builtin(CanonicalBuiltinKind::UnsignedShort),
		.size_bytes = 2,
		.enumerator_count = 2,
		.flags = CanonicalEnumLayoutFlags::Scoped,
	};
	table.publishEnumLayout(enum_layout);
	require(table.enumLayout(EntityId{5}) == enum_layout);
	const CanonicalEnumLayout unfixed_enum_layout{
		.entity = EntityId{6},
		.underlying_type = table.builtin(CanonicalBuiltinKind::Int),
		.unfixed_promotion_type = table.builtin(CanonicalBuiltinKind::UnsignedInt),
		.size_bytes = 4,
		.enumerator_count = 1,
		.flags = CanonicalEnumLayoutFlags::None,
	};
	table.publishEnumLayout(unfixed_enum_layout);
	require(table.enumLayout(EntityId{6}) == unfixed_enum_layout);
	CanonicalEnumLayout invalid_unfixed_enum_layout = unfixed_enum_layout;
	invalid_unfixed_enum_layout.unfixed_promotion_type =
		table.builtin(CanonicalBuiltinKind::Short);
	rejects([&] { table.publishEnumLayout(invalid_unfixed_enum_layout); });
	const CanonicalEnumLayout forward_enum_layout{
		.entity = EntityId{12},
		.underlying_type = table.builtin(CanonicalBuiltinKind::UnsignedChar),
		.size_bytes = 1,
		.enumerator_count = 0,
		.flags = CanonicalEnumLayoutFlags::FixedUnderlying |
			CanonicalEnumLayoutFlags::ForwardDeclaration,
	};
	table.publishEnumLayout(forward_enum_layout);
	const CanonicalEnumLayout completed_enum_layout{
		.entity = EntityId{12},
		.underlying_type = table.builtin(CanonicalBuiltinKind::UnsignedChar),
		.size_bytes = 1,
		.enumerator_count = 2,
		.flags = CanonicalEnumLayoutFlags::FixedUnderlying,
	};
	{
		CanonicalTypeTransaction enum_completion_transaction(table);
		table.publishEnumLayout(completed_enum_layout);
		require(table.enumLayout(EntityId{12}) == completed_enum_layout);
		enum_completion_transaction.rollback();
	}
	require(table.enumLayout(EntityId{12}) == forward_enum_layout);
	table.publishEnumLayout(completed_enum_layout);
	require(table.enumLayout(EntityId{12}) == completed_enum_layout);
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
	table.publishRecordFieldSchema(table.record(EntityId{11}), schema_members, schema_bases);
	require(table.hasRecordFieldSchema(table.record(EntityId{11})));
	require(table.recordMemberAt(table.record(EntityId{11}), 0) == schema_members[0]);
	require(table.recordMemberAt(table.record(EntityId{11}), 1) == schema_members[1]);
	require(table.recordBaseAt(table.record(EntityId{11}), 0) == schema_bases[0]);
	table.publishRecordFieldSchema(table.record(EntityId{11}), schema_members, schema_bases);
	std::array<CanonicalRecordMember, 2> conflicting_members = schema_members;
	conflicting_members[1].offset_bytes = 4;
	rejects([&] {
		table.publishRecordFieldSchema(table.record(EntityId{11}), conflicting_members, schema_bases);
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
	table.publishRecordFieldSchema(table.record(EntityId{13}), zero_width_bitfield, {});
	require(table.recordMemberAt(table.record(EntityId{13}), 0).flags == CanonicalRecordMemberFlags::Bitfield);
	require(table.recordMemberAt(table.record(EntityId{13}), 0).bit_width == 0);
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
	const TypeId rolled_property_type = table.record(EntityId{12});
	table.publishRecordFieldSchema(table.record(EntityId{12}), rolled_members, {});
	table.publishRecordProperties(
		rolled_property_type, CanonicalRecordFacts::Polymorphic);
	schema_transaction.rollback();
	require(!table.hasRecordLayout(EntityId{12}));
	require(!table.hasRecordFieldSchema(table.record(EntityId{12})));
	require(!table.hasRecordProperties(rolled_property_type));
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
	rejects([&] { table.publishRecordFieldSchema(table.record(EntityId{1}),
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
	const TypeId reference = table.reference(integer, ReferenceQualifier::LValueReference);
	const TypeId reference_args[] = {reference};
	rejects([&] { table.substitute(table.pointer(param0), env, reference_args); });

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
	rejects([&] { table.substitute(param0, env, std::span<const TypeId>{}); });
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
	const TypeId pattern_arguments[] = {param0};
	const TypeId pattern_owner = table.templateSpecialization(TemplateDeclId{43}, pattern_arguments);
	const TypeId concrete_owner = table.templateSpecialization(TemplateDeclId{43}, args);
	require(table.substitute(table.memberObjectPointer(pattern_owner, param0), env, args) == table.memberObjectPointer(concrete_owner, integer));
	const TypeId pattern_mfp = table.memberFunctionPointer(pattern_owner, dependent_function);
	require(table.substitute(pattern_mfp, env, args) == table.memberFunctionPointer(concrete_owner, subst_function));

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

// A nested owner+alias argument target (Both<Owner, Value>) publishes under the
// member alias declaration with the owner declaration and resolves with owner
// arguments followed by alias arguments. Swapping the argument order must yield
// a structurally different specialization.
inline void checkMemberAliasOwnerNestedTarget() {
	CanonicalTypeTable table;
	const TemplateDeclId owner{90};
	const TemplateDeclId alias{91};
	const TemplateDeclId nested_primary{92};
	const TypeId owner_param = table.templateParameter(owner, 0);
	const TypeId alias_param = table.templateParameter(alias, 0);
	const TypeId owner_arg = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId alias_arg = table.builtin(CanonicalBuiltinKind::Char);
	const CanonicalTemplateArgKind kinds[] = {CanonicalTemplateArgKind::Type};
	const TypeId target = table.templateSpecialization(nested_primary,
		std::array<TypeId, 2>{owner_param, alias_param});
	require(table.dependsOnlyOnTemplateParameters(target, alias, owner));
	table.publishAliasTemplateTarget(alias, owner, target, kinds);
	const TypeId owner_args[] = {owner_arg};
	const TypeId alias_args[] = {alias_arg};
	const TypeId owner_spec = table.templateSpecialization(owner, owner_args);
	const std::optional<TypeId> resolved =
		table.resolveMemberAliasTarget(alias, owner_spec, alias_args);
	require(resolved.has_value());
	const TypeId expected = table.templateSpecialization(nested_primary,
		std::array<TypeId, 2>{owner_arg, alias_arg});
	require(*resolved == expected);
	require(*resolved != table.templateSpecialization(nested_primary,
		std::array<TypeId, 2>{alias_arg, owner_arg}));

	std::printf("member alias owner nested target: published\n");
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

// A qualified member-alias use with published declaration identity
// auto-redirects through resolveMemberAliasUse and tryResolveDependentTip. The
// owner arguments come from the qualifier's TemplateSpecialization and the alias
// arguments from the use; the owner/alias order is distinguished. An unknown
// member declaration and a dependent alias argument stay unresolved instead of
// producing a partial target.
inline void checkMemberAliasUseAutoRedirect() {
	CanonicalTypeTable table;
	const TemplateDeclId owner{100};
	const TemplateDeclId alias{101};
	const TemplateDeclId target_primary{102};
	const TypeId owner_param = table.templateParameter(owner, 0);
	const TypeId alias_param = table.templateParameter(alias, 0);
	const TypeId owner_arg = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId alias_arg = table.builtin(CanonicalBuiltinKind::Char);
	const CanonicalTemplateArgKind kinds[] = {CanonicalTemplateArgKind::Type};
	const TypeId target = table.templateSpecialization(target_primary,
		std::array<TypeId, 2>{owner_param, alias_param});
	table.publishAliasTemplateTarget(alias, owner, target, kinds);
	const TypeId expected = table.templateSpecialization(target_primary,
		std::array<TypeId, 2>{owner_arg, alias_arg});

	const TypeId qualifier =
		table.templateSpecialization(owner, std::array<TypeId, 1>{owner_arg});
	const TypeId use =
		table.dependentMemberAlias(qualifier, alias, std::array<TypeId, 1>{alias_arg});
	require(table.node(use).kind == CanonicalTypeKind::DependentMemberAlias);
	require(table.dependentMemberAliasDecl(use) == alias);
	require(table.dependentMemberAliasArguments(use) != TypeId{});
	require(table.dependentNameQualifier(use) == qualifier);
	require(table.resolveMemberAliasUse(use).has_value());
	require(*table.resolveMemberAliasUse(use) == expected);
	require(table.tryResolveDependentTip(use) == expected);
	// Owner arguments precede alias arguments; swapping them is a different type.
	require(*table.resolveMemberAliasUse(use) !=
		table.templateSpecialization(target_primary,
			std::array<TypeId, 2>{alias_arg, owner_arg}));
	require(use == table.dependentMemberAlias(qualifier, alias,
		std::array<TypeId, 1>{alias_arg}));

	// An unknown member declaration is not a published alias and stays a
	// dependent tip.
	const TemplateDeclId unknown{103};
	const TypeId unknown_use =
		table.dependentMemberAlias(qualifier, unknown, std::array<TypeId, 1>{alias_arg});
	require(!table.resolveMemberAliasUse(unknown_use).has_value());
	require(table.tryResolveDependentTip(unknown_use) == unknown_use);

	// Substitution of the qualifier's owner argument then auto-resolution
	// redirects through the identity the node carries.
	const TypeId dependent_qualifier =
		table.templateSpecialization(owner, std::array<TypeId, 1>{owner_param});
	const TypeId dependent_use = table.dependentMemberAlias(dependent_qualifier, alias,
		std::array<TypeId, 1>{alias_arg});
	const TypeId substituted = table.substitute(dependent_use, owner,
		std::array<TypeId, 1>{owner_arg});
	require(table.tryResolveDependentTip(substituted) == expected);

	// A dependent (partially concrete) alias argument defers.
	const TemplateDeclId other_env{104};
	const TypeId other_param = table.templateParameter(other_env, 0);
	const TypeId dependent_argument_use = table.dependentMemberAlias(qualifier, alias,
		std::array<TypeId, 1>{other_param});
	require(!table.resolveMemberAliasUse(dependent_argument_use).has_value());
	require(table.tryResolveDependentTip(dependent_argument_use) == dependent_argument_use);

	std::printf("member alias use auto-redirect: resolved\n");
}

// Concrete direct alias specializations nested anywhere in a substituted graph
// redirect to their published targets: wrappers, specialization type arguments,
// and array / qualified / reference nodes. A nested alias with a dependent
// argument or a nested declaration-ID cycle keeps the AliasTemplateSpecialization
// boundary instead of collapsing or looping.
inline void checkNestedAliasGraph() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId floating = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId dummy_args[] = {integer};
	const CanonicalTemplateArgKind type_kind[] = {CanonicalTemplateArgKind::Type};

	// Identity<T> = T, used as a nested argument in several shapes.
	const TemplateDeclId identity{201};
	table.publishAliasTemplateTarget(identity, table.templateParameter(identity, 0), type_kind);
	const CanonicalTemplateArgument identity_int[] = {
		CanonicalTemplateArgument::makeType(integer),
	};
	const TypeId identity_use = table.aliasTemplateSpecialization(identity, identity_int);
	require(table.resolveAliasTemplateSpecializations(table.pointer(identity_use)) == table.pointer(integer));
	TypeId deep_alias = identity_use;
	TypeId expected_deep_alias = integer;
	for (size_t depth = 0; depth < 65536; ++depth) {
		deep_alias = table.pointer(deep_alias);
		expected_deep_alias = table.pointer(expected_deep_alias);
	}
	require(table.resolveAliasTemplateSpecializations(deep_alias) == expected_deep_alias);

	// Callable alias targets preserve the substituted function signature.
	const TemplateDeclId callable_alias{208};
	const TypeId callable_parameter_alias_args[] = {table.templateParameter(callable_alias, 0)};
	const TypeId callable_parameter[] = {table.aliasTemplateSpecialization(identity, callable_parameter_alias_args)};
	const TypeId callable_target = table.pointer(table.function(integer, callable_parameter, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}));
	table.publishAliasTemplateTarget(callable_alias, callable_target, type_kind);
	const TypeId callable_use = table.aliasTemplateSpecialization(callable_alias, identity_int);
	const TypeId expected_callable = table.pointer(table.function(integer, std::array<TypeId, 1>{integer}, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}));
	require(table.resolveAliasTemplateSpecializations(callable_use) == expected_callable);

	// Nested inside a pointer collapses to pointer(int).
	require(table.substitute(table.pointer(identity_use), identity, dummy_args) == table.pointer(integer));

	// Nested as a class-template type argument collapses to int.
	const TypeId nested_argument = table.templateSpecialization(TemplateDeclId{202},
		std::array<CanonicalTemplateArgument, 1>{
			CanonicalTemplateArgument::makeType(identity_use)});
	const TypeId expected_argument = table.templateSpecialization(TemplateDeclId{202},
		std::array<CanonicalTemplateArgument, 1>{
			CanonicalTemplateArgument::makeType(integer)});
	require(table.substitute(nested_argument, identity, dummy_args) == expected_argument);

	// Nested under array, qualified, and reference wrappers collapses.
	require(table.substitute(table.array(table.qualify(identity_use, CVQualifier::Const), 4),
		identity, dummy_args) == table.array(table.qualify(integer, CVQualifier::Const), 4));
	require(table.substitute(table.reference(table.pointer(identity_use),
		ReferenceQualifier::LValueReference), identity, dummy_args) ==
		table.reference(table.pointer(integer), ReferenceQualifier::LValueReference));

	// Swap<T, U> = Pair<U, T> exposes nested aliases in its own published target.
	const TemplateDeclId swap_alias{203};
	const CanonicalTemplateArgument swap_target[] = {
		CanonicalTemplateArgument::makeType(table.templateParameter(swap_alias, 1)),
		CanonicalTemplateArgument::makeType(table.templateParameter(swap_alias, 0)),
	};
	const CanonicalTemplateArgKind swap_kinds[] = {
		CanonicalTemplateArgKind::Type,
		CanonicalTemplateArgKind::Type,
	};
	table.publishAliasTemplateTarget(swap_alias,
		table.templateSpecialization(TemplateDeclId{204}, swap_target), swap_kinds);
	const CanonicalTemplateArgument swap_concrete[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeType(floating),
	};
	const TypeId swap_use = table.aliasTemplateSpecialization(swap_alias, swap_concrete);
	const CanonicalTemplateArgument swap_expected[] = {
		CanonicalTemplateArgument::makeType(floating),
		CanonicalTemplateArgument::makeType(integer),
	};
	// Outer<Swap<int, double>> normalizes the alias nested in the argument.
	const TypeId outer_swap = table.templateSpecialization(TemplateDeclId{205},
		std::array<CanonicalTemplateArgument, 1>{
			CanonicalTemplateArgument::makeType(swap_use)});
	require(table.substitute(outer_swap, swap_alias, dummy_args) ==
		table.templateSpecialization(TemplateDeclId{205},
			std::array<CanonicalTemplateArgument, 1>{
				CanonicalTemplateArgument::makeType(
					table.templateSpecialization(TemplateDeclId{204}, swap_expected))}));
	require(table.substitute(table.pointer(swap_use), swap_alias, dummy_args) ==
		table.pointer(table.templateSpecialization(TemplateDeclId{204}, swap_expected)));

	// A nested alias with a dependent argument keeps its alias identity.
	const TypeId foreign = table.templateParameter(TemplateDeclId{206}, 0);
	const CanonicalTemplateArgument dependent_concrete[] = {
		CanonicalTemplateArgument::makeType(foreign),
	};
	const TypeId dependent_use = table.aliasTemplateSpecialization(identity, dependent_concrete);
	require(table.substitute(table.pointer(dependent_use), identity, dummy_args) ==
		table.pointer(dependent_use));

	// A nested self-cycle terminates and leaves the alias boundary. Cycle<T> =
	// pointer<Cycle<T>>; the nested pointer expansion stops at Cycle<int>.
	const TemplateDeclId cycle{207};
	const CanonicalTemplateArgument cycle_target[] = {
		CanonicalTemplateArgument::makeType(table.templateParameter(cycle, 0)),
	};
	table.publishAliasTemplateTarget(cycle,
		table.pointer(table.aliasTemplateSpecialization(cycle, cycle_target)), type_kind);
	const TypeId cycle_use = table.aliasTemplateSpecialization(cycle, identity_int);
	require(table.substitute(table.pointer(cycle_use), cycle, dummy_args) ==
		table.pointer(table.pointer(cycle_use)));

	std::printf("nested alias graph: normalized\n");
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

inline void checkStructuralEnumScalars() {
	CanonicalTypeTable table;
	const auto make_enum = [&](EntityId entity, CanonicalBuiltinKind underlying, uint32_t size, CanonicalEnumLayoutFlags flags) {
		const TypeId type = table.enumeration(entity);
		table.publishEnumLayout({.entity = entity, .underlying_type = table.builtin(underlying), .size_bytes = size,
			.enumerator_count = 0, .flags = flags});
		return type;
	};
	const auto fixed = CanonicalEnumLayoutFlags::FixedUnderlying;
	const TypeId signed_byte = make_enum(EntityId{40101}, CanonicalBuiltinKind::SignedChar, 1, fixed);
	const TypeId unsigned_short = make_enum(EntityId{40102}, CanonicalBuiltinKind::UnsignedShort, 2, fixed);
	const TypeId unsigned_wide = make_enum(EntityId{40103}, CanonicalBuiltinKind::UnsignedLongLong, 8, fixed);
	const TypeId other_signed = make_enum(EntityId{40104}, CanonicalBuiltinKind::SignedChar, 1, fixed);
	const TypeId boolean = make_enum(EntityId{40105}, CanonicalBuiltinKind::Bool, 1, fixed);
	const TypeId unfixed = make_enum(EntityId{40106}, CanonicalBuiltinKind::Int, 4, CanonicalEnumLayoutFlags::None);
	const TypeId forward = make_enum(EntityId{40107}, CanonicalBuiltinKind::UnsignedChar, 1,
		fixed | CanonicalEnumLayoutFlags::Scoped | CanonicalEnumLayoutFlags::ForwardDeclaration);
	const auto negative = tryImportCanonicalStructuralScalar(table, signed_byte, 0xf9, 8, std::nullopt);
	require(negative.has_value());
	require(table.node(*negative).child == signed_byte);
	require(table.node(*negative).array_extent == static_cast<uint64_t>(-7));
	require(negative == tryImportCanonicalStructuralScalar(table, signed_byte, static_cast<uint64_t>(-7), 0, std::nullopt));
	require(negative != tryImportCanonicalStructuralScalar(table, other_signed, 0xf9, 8, std::nullopt));
	require(negative != tryImportCanonicalStructuralScalar(
		table, table.builtin(CanonicalBuiltinKind::SignedChar), 0xf9, 1, std::nullopt));
	const auto narrow = tryImportCanonicalStructuralScalar(table, unsigned_short, 0xffffffffffffea60ULL, 1, std::nullopt);
	require(narrow.has_value());
	require(table.node(*narrow).child == unsigned_short);
	require(table.node(*narrow).array_extent == 60000);
	const auto wide = tryImportCanonicalStructuralScalar(table, unsigned_wide, 0xfedcba9876543210ULL, 1, std::nullopt);
	require(wide.has_value());
	require(table.node(*wide).child == unsigned_wide);
	require(table.node(*wide).array_extent == 0xfedcba9876543210ULL);
	const auto flag = tryImportCanonicalStructuralScalar(table, boolean, 9, 8, std::nullopt);
	require(flag.has_value());
	require(table.node(*flag).array_extent == 1);
	const auto plain = tryImportCanonicalStructuralScalar(table, unfixed, 0xfffffff7ULL, 8, std::nullopt);
	require(plain.has_value());
	require(table.node(*plain).array_extent == static_cast<uint64_t>(-9));
	const auto opaque = tryImportCanonicalStructuralScalar(table, forward, 0x1234, 8, std::nullopt);
	require(opaque.has_value());
	require(table.node(*opaque).array_extent == 0x34);
	require(!tryImportCanonicalStructuralScalar(table, table.enumeration(EntityId{40108}), 7, 1, std::nullopt));
	require(!tryImportCanonicalStructuralScalar(table, table.builtin(CanonicalBuiltinKind::Double), 7, 8, std::nullopt));
	require(!tryImportCanonicalStructuralScalar(table, table.builtin(CanonicalBuiltinKind::Int), 7, 0, std::nullopt));
	const TargetDataModel previous_model = g_target_data_model;
	g_target_data_model = TargetDataModel::LLP64;
	const TypeId windows_wchar = make_enum(EntityId{40109}, CanonicalBuiltinKind::WChar, 2, fixed);
	const auto windows_value = tryImportCanonicalStructuralScalar(table, windows_wchar, 0xffff, 8, std::nullopt);
	g_target_data_model = TargetDataModel::LP64;
	const TypeId unix_wchar = make_enum(EntityId{40110}, CanonicalBuiltinKind::WChar, 4, fixed);
	const auto unix_value = tryImportCanonicalStructuralScalar(table, unix_wchar, 0xffffffff, 8, std::nullopt);
	g_target_data_model = previous_model;
	require(windows_value.has_value());
	require(unix_value.has_value());
	require(table.node(*windows_value).array_extent == 65535);
	require(table.node(*unix_value).array_extent == static_cast<uint64_t>(-1));
}

inline void checkStructuralBitfieldScalars() {
	CanonicalTypeTable table;
	const TypeId signed_int = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId unsigned_int = table.builtin(CanonicalBuiltinKind::UnsignedInt);
	const TypeId boolean = table.builtin(CanonicalBuiltinKind::Bool);
	const auto signed_negative = tryImportCanonicalStructuralScalar(table, signed_int, 7, 4, 3);
	require(signed_negative.has_value());
	require(table.node(*signed_negative).child == signed_int);
	require(table.node(*signed_negative).array_extent == static_cast<uint64_t>(-1));
	const auto signed_wrap = tryImportCanonicalStructuralScalar(table, signed_int, 4, 4, 3);
	require(signed_wrap.has_value());
	require(table.node(*signed_wrap).array_extent == static_cast<uint64_t>(-4));
	const auto unsigned_narrow = tryImportCanonicalStructuralScalar(table, unsigned_int, 9, 4, 3);
	require(unsigned_narrow.has_value());
	require(table.node(*unsigned_narrow).child == unsigned_int);
	require(table.node(*unsigned_narrow).array_extent == 1);
	const auto flag = tryImportCanonicalStructuralScalar(table, boolean, 2, 1, 1);
	require(flag.has_value());
	require(table.node(*flag).child == boolean);
	require(table.node(*flag).array_extent == 1);
	const TypeId enumeration = table.enumeration(EntityId{40111});
	table.publishEnumLayout({.entity = EntityId{40111}, .underlying_type = table.builtin(CanonicalBuiltinKind::SignedChar),
		.size_bytes = 1, .enumerator_count = 1, .flags = CanonicalEnumLayoutFlags::FixedUnderlying});
	const auto enum_value = tryImportCanonicalStructuralScalar(table, enumeration, 7, 1, 3);
	require(enum_value.has_value());
	require(table.node(*enum_value).child == enumeration);
	require(table.node(*enum_value).array_extent == static_cast<uint64_t>(-1));
	require(!tryImportCanonicalStructuralScalar(table, signed_int, 7, 4, 0));
	require(!tryImportCanonicalStructuralScalar(table, signed_int, 7, 1, 9));
	require(!tryImportCanonicalStructuralScalar(table, signed_int, 7, 4, 65));
}

inline void checkFloatingValues() {
	CanonicalTypeTable table;
	const TypeId float_type = table.builtin(CanonicalBuiltinKind::Float);
	const TypeId double_type = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId one_and_half = table.floatingValue(float_type, 0x3fc00000);
	require(table.node(one_and_half).kind == CanonicalTypeKind::FloatingValue);
	require(table.node(one_and_half).child == float_type);
	require(table.node(one_and_half).array_extent == 0x3fc00000);
	require(one_and_half == table.floatingValue(float_type, 0x3fc00000));
	require(one_and_half != table.floatingValue(float_type, 0x40000000));
	require(one_and_half != table.floatingValue(double_type, 0x3fc00000));
	const TypeId double_value = table.floatingValue(double_type, 0x3ff8000000000000ULL);
	require(table.node(double_value).kind == CanonicalTypeKind::FloatingValue);
	require(table.node(double_value).array_extent == 0x3ff8000000000000ULL);
	const auto rejects = [&](TypeId type, uint64_t bits) {
		try { (void)table.floatingValue(type, bits); } catch (const InternalError&) { return true; }
		return false;
	};
	require(rejects(float_type, static_cast<uint64_t>(UINT32_MAX) + 1));
	require(rejects(table.builtin(CanonicalBuiltinKind::Int), 0));
	require(rejects(table.builtin(CanonicalBuiltinKind::LongDouble), 0));
	const TypeId values[] = {one_and_half, double_value};
	const TypeId record = table.record(EntityId{40301});
	const TypeId structural = table.structuralValue(record, values, 0);
	require(table.node(table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(structural).array_extent))).child ==
		float_type);
}

inline void checkNullPointerValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId pointer = table.pointer(integer);
	const TypeId null_pointer = table.nullPointerValue(pointer);
	require(table.node(null_pointer).kind == CanonicalTypeKind::NullPointerValue);
	require(table.node(null_pointer).child == pointer);
	require(table.node(null_pointer).array_extent == 0);
	require(null_pointer == table.nullPointerValue(pointer));
	const TypeId owner = table.record(EntityId{40311});
	const TypeId member_pointer = table.memberObjectPointer(owner, integer);
	const TypeId null_member = table.nullPointerValue(member_pointer);
	require(table.node(null_member).kind == CanonicalTypeKind::NullPointerValue);
	require(table.node(null_member).child == member_pointer);
	require(null_pointer != null_member);
	const auto rejects = [&](TypeId type) {
		try { (void)table.nullPointerValue(type); } catch (const InternalError&) { return true; }
		return false;
	};
	require(rejects(integer));
	require(rejects(table.reference(pointer, ReferenceQualifier::LValueReference)));
	const TypeId values[] = {null_pointer, null_member};
	const TypeId record = table.record(EntityId{40312});
	const TypeId structural = table.structuralValue(record, values, 0);
	TypeId link = unpackStructuralValueSubobjects(table.node(structural).array_extent);
	require(table.node(table.templateArgumentValue(link)).kind == CanonicalTypeKind::NullPointerValue);
	link = table.templateArgumentNext(link);
	require(table.node(table.templateArgumentValue(link)).kind == CanonicalTypeKind::NullPointerValue);
	require(!table.templateArgumentNext(link));
	const TypeId pointer_array = table.array(pointer, 2);
	const TypeId null_elements[] = {null_pointer, null_pointer};
	const TypeId array_value = table.structuralArrayValue(pointer_array, null_elements);
	require(table.node(array_value).kind == CanonicalTypeKind::StructuralArrayValue);
	require(table.node(array_value).child == pointer_array);
	require(unpackStructuralArrayElementCount(table.node(array_value).array_extent) == 2);
	const TypeId nullptr_type = table.builtin(CanonicalBuiltinKind::Nullptr);
	const TypeId nullptr_value = table.nullptrValue(nullptr_type);
	require(table.node(nullptr_value).kind == CanonicalTypeKind::NullptrValue);
	require(table.node(nullptr_value).child == nullptr_type);
	require(table.node(nullptr_value).array_extent == 0);
	require(nullptr_value == table.nullptrValue(nullptr_type));
	try {
		(void)table.nullptrValue(integer);
		require(false);
	} catch (const InternalError&) {
	}
}

inline void checkStructuralPointerIdentities() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId object_pointer = table.pointer(integer);
	const TypeIndex integer_value{0, TypeCategory::Int};
	const auto import_object = [&](EntityId target, int64_t offset, std::optional<TypeId> field_type) {
		return tryImportCanonicalPointerIdentityPayload(table, FlashCpp::NonTypeValueIdentityKind::ObjectPointer, target,
			offset, TypeId{}, integer_value, field_type);
	};
	const auto object_value = import_object(EntityId{40341}, -3, object_pointer);
	require(object_value.has_value());
	const CanonicalTypeNode object_node = table.node(*object_value);
	require(object_node.kind == CanonicalTypeKind::ObjectPointerValue);
	require(object_node.child == object_pointer);
	require(unpackObjectPointerValueTarget(object_node.array_extent) == EntityId{40341});
	require(unpackObjectPointerValueOffset(object_node.array_extent) == -3);
	require(object_value == import_object(EntityId{40341}, -3, object_pointer));
	// A top-level template argument publishes the pointee type instead of a field type.
	require(object_value == import_object(EntityId{40341}, -3, std::nullopt));
	require(object_value != import_object(EntityId{40342}, -3, object_pointer));
	require(object_value != import_object(EntityId{40341}, -2, object_pointer));
	require(!import_object(EntityId{}, -3, object_pointer).has_value());
	require(!import_object(EntityId{40341}, int64_t{1} << 40, object_pointer).has_value());
	require(!import_object(EntityId{40341}, -3, integer).has_value());

	const TypeId parameters[] = {table.builtin(CanonicalBuiltinKind::Short)};
	const TypeId function = table.function(integer, parameters, false, CVQualifier::None, ReferenceQualifier::None,
		false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId wide_function = table.function(table.builtin(CanonicalBuiltinKind::LongLong), parameters, false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId function_pointer = table.pointer(function);
	const auto import_function = [&](EntityId target, TypeId signature, std::optional<TypeId> field_type) {
		return tryImportCanonicalPointerIdentityPayload(table, FlashCpp::NonTypeValueIdentityKind::FunctionPointer, target,
			0, signature, TypeIndex{}, field_type);
	};
	const auto function_value = import_function(EntityId{40351}, function, function_pointer);
	require(function_value.has_value());
	const CanonicalTypeNode function_node = table.node(*function_value);
	require(function_node.kind == CanonicalTypeKind::FunctionPointerValue);
	require(function_node.child == function_pointer);
	require(function_node.array_extent == EntityId{40351}.value);
	require(function_value == import_function(EntityId{40351}, function, std::nullopt));
	require(!import_function(EntityId{}, function, function_pointer).has_value());
	require(!import_function(EntityId{40351}, TypeId{}, function_pointer).has_value());
	require(!import_function(EntityId{40351}, table.record(EntityId{40352}), function_pointer).has_value());
	require(!import_function(EntityId{40351}, wide_function, function_pointer).has_value());
	require(!import_function(EntityId{40351}, function, table.pointer(wide_function)).has_value());
}

inline void checkTemplateSpecializationStructuralValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeType(integer)};
	const TypeId specialization = table.templateSpecialization(TemplateDeclId{40321}, arguments);
	require(table.node(specialization).kind == CanonicalTypeKind::TemplateSpecialization);
	const TypeId member = table.integralValue(integer, 42);
	const TypeId values[] = {member};
	const TypeId structural = table.structuralValue(specialization, values, 0);
	require(table.node(structural).kind == CanonicalTypeKind::StructuralValue);
	require(table.node(structural).child == specialization);
	require(table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(structural).array_extent)) == member);
	const CanonicalTemplateArgument other_arguments[] = {CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Long))};
	const TypeId other_specialization = table.templateSpecialization(TemplateDeclId{40321}, other_arguments);
	require(structural != table.structuralValue(other_specialization, values, 0));
}

inline void checkMemberObjectPointerValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const EntityId entity{40331};
	const TypeId owner = table.record(entity);
	table.publishRecordLayout({entity, 8, 8, 8, 4, 2, 0, CanonicalRecordLayoutFlags::None});
	const CanonicalRecordMember members[] = {
		{integer, 0, 4, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None},
		{integer, 4, 4, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None},
	};
	table.publishRecordFieldSchema(table.record(entity), members, {});
	const TypeId member_pointer = table.memberObjectPointer(owner, integer);
	const TypeId first = table.memberObjectPointerValue(member_pointer, owner, 0);
	const TypeId second = table.memberObjectPointerValue(member_pointer, owner, 1);
	require(table.node(first).kind == CanonicalTypeKind::MemberObjectPointerValue);
	require(table.node(first).child == member_pointer);
	require(unpackMemberObjectPointerValueDeclaringClass(table.node(first).array_extent) == owner);
	require(unpackMemberObjectPointerValueOrdinal(table.node(first).array_extent) == 0);
	require(unpackMemberObjectPointerValueOrdinal(table.node(second).array_extent) == 1);
	require(first == table.memberObjectPointerValue(member_pointer, owner, 0));
	require(first != second);
	const EntityId base_entity{40335};
	const TypeId base_owner = table.record(base_entity);
	table.publishRecordLayout({base_entity, 4, 4, 4, 4, 1, 0, CanonicalRecordLayoutFlags::None});
	const CanonicalRecordMember base_member{integer, 0, 4, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None};
	table.publishRecordFieldSchema(table.record(base_entity), std::span<const CanonicalRecordMember>(&base_member, 1), {});
	const TypeId inherited = table.memberObjectPointerValue(member_pointer, base_owner, 0);
	require(inherited != first);
	const auto rejects = [&](TypeId type, TypeId declaring_class, uint16_t ordinal) {
		try { (void)table.memberObjectPointerValue(type, declaring_class, ordinal); } catch (const InternalError&) { return true; }
		return false;
	};
	require(rejects(member_pointer, owner, 2));
	require(rejects(integer, owner, 0));
	require(rejects(member_pointer, integer, 0));
	const EntityId bitfield_entity{40332};
	const TypeId bitfield_owner = table.record(bitfield_entity);
	table.publishRecordLayout({bitfield_entity, 4, 4, 4, 4, 1, 0, CanonicalRecordLayoutFlags::None});
	const CanonicalRecordMember bitfield_member{integer, 0, 4, 3, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::Bitfield};
	table.publishRecordFieldSchema(table.record(bitfield_entity), std::span<const CanonicalRecordMember>(&bitfield_member, 1), {});
	require(rejects(table.memberObjectPointer(bitfield_owner, integer), bitfield_owner, 0));
	const TypeId values[] = {first, inherited};
	const TypeId record = table.record(EntityId{40333});
	const TypeId structural = table.structuralValue(record, values, 0);
	TypeId link = unpackStructuralValueSubobjects(table.node(structural).array_extent);
	require(table.node(table.templateArgumentValue(link)).kind == CanonicalTypeKind::MemberObjectPointerValue);
	link = table.templateArgumentNext(link);
	require(unpackMemberObjectPointerValueDeclaringClass(table.node(table.templateArgumentValue(link)).array_extent) == base_owner);
}

inline void checkMemberFunctionPointerValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId function = table.function(integer, std::span<const TypeId>(), false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId owner = table.record(EntityId{40341});
	const TypeId member_function_pointer = table.memberFunctionPointer(owner, function);
	const TypeId signatures[] = {function, function};
	table.publishMemberFunctionSignatures(owner, signatures);
	require(table.hasMemberFunctionSignatures(owner));
	require(table.memberFunctionSignatureCount(owner) == 2);
	require(table.memberFunctionSignatureAt(owner, 0) == function);
	require(table.memberFunctionSignatureAt(owner, 1) == function);
	const TypeId first = table.memberFunctionPointerValue(member_function_pointer, owner, 0);
	const TypeId second = table.memberFunctionPointerValue(member_function_pointer, owner, 1);
	require(table.node(first).kind == CanonicalTypeKind::MemberFunctionPointerValue);
	require(table.node(first).child == member_function_pointer);
	require(unpackMemberFunctionPointerValueDeclaringClass(table.node(first).array_extent) == owner);
	require(unpackMemberFunctionPointerValueOrdinal(table.node(first).array_extent) == 0);
	require(unpackMemberFunctionPointerValueOrdinal(table.node(second).array_extent) == 1);
	require(first == table.memberFunctionPointerValue(member_function_pointer, owner, 0));
	require(first != second);
	const auto rejects = [&](TypeId type, TypeId declaring_class) {
		try { (void)table.memberFunctionPointerValue(type, declaring_class, 0); } catch (const InternalError&) { return true; }
		return false;
	};
	const auto rejects_at = [&](TypeId type, TypeId declaring_class, uint16_t ordinal) {
		try { (void)table.memberFunctionPointerValue(type, declaring_class, ordinal); } catch (const InternalError&) { return true; }
		return false;
	};
	require(rejects(integer, owner));
	require(rejects(table.memberObjectPointer(owner, integer), owner));
	require(rejects(member_function_pointer, integer));
	require(rejects(member_function_pointer, table.record(EntityId{40351})));
	const TypeId wrong_function = table.function(table.builtin(CanonicalBuiltinKind::Long), std::span<const TypeId>(), false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	require(rejects_at(table.memberFunctionPointer(owner, wrong_function), owner, 1));
	const TypeId specialization_args[] = {integer};
	const TypeId specialization_owner = table.templateSpecialization(TemplateDeclId{40343}, specialization_args);
	table.publishMemberFunctionSignatures(specialization_owner, signatures);
	require(table.memberFunctionSignatureAt(specialization_owner, 1) == function);
	require(rejects_at(member_function_pointer, owner, 2));
	const TypeId specialization_pointer = table.memberFunctionPointer(specialization_owner, function);
	require(table.memberFunctionPointerValue(specialization_pointer, specialization_owner, 0) != first);
	const TypeId values[] = {first, second};
	const TypeId structural = table.structuralValue(table.record(EntityId{40342}), values, 0);
	TypeId link = unpackStructuralValueSubobjects(table.node(structural).array_extent);
	require(table.node(table.templateArgumentValue(link)).kind == CanonicalTypeKind::MemberFunctionPointerValue);
	link = table.templateArgumentNext(link);
	require(unpackMemberFunctionPointerValueOrdinal(table.node(table.templateArgumentValue(link)).array_extent) == 1);
}

inline void checkMemberFunctionSignatureTransactions() {
	SemanticArenaAccounting accounting;
	CanonicalTypeTable table(accounting);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId function = table.function(integer, std::span<const TypeId>(), false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId owner = table.record(EntityId{40344});
	const TypeId signatures[] = {TypeId{}, function};
	const TypeId member_types[] = {integer};
	table.publishRecordSubobjectTypes(owner, member_types);
	{
		CanonicalTypeTransaction warmup(table);
		table.publishMemberFunctionSignatures(owner, signatures);
	}
	const size_t initial_size = table.size();
	const auto initial_stats = table.arenaStats();
	const auto initial_accounting = accounting.snapshot();
	TypeId temporary;
	{
		CanonicalTypeTransaction transaction(table);
		temporary = table.record(EntityId{40345});
		table.publishRecordSubobjectTypes(temporary, member_types);
		table.publishMemberFunctionSignatures(temporary, signatures);
	}
	{
		CanonicalTypeTransaction transaction(table);
		table.publishMemberFunctionSignatures(owner, signatures);
	}
	require(table.size() == initial_size);
	require(table.hasRecordSubobjectTypes(owner));
	require(table.recordSubobjectMemberAt(owner, 0) == integer);
	require(!table.hasMemberFunctionSignatures(temporary));
	require(!table.hasRecordSubobjectTypes(temporary));
	require(!table.hasMemberFunctionSignatures(owner));
	require(table.arenaStats().reserved_bytes == initial_stats.reserved_bytes);
	require(accounting.snapshot().current_bytes == initial_accounting.current_bytes);
	require(accounting.snapshot().peak_bytes >= initial_accounting.peak_bytes);
	table.publishMemberFunctionSignatures(owner, signatures);
	table.publishMemberFunctionSignatures(owner, signatures);
	const TypeId conflicting_function = table.function(table.builtin(CanonicalBuiltinKind::Long), std::span<const TypeId>(), false,
		CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId conflicting[] = {TypeId{}, conflicting_function};
	rejects([&] { table.publishMemberFunctionSignatures(owner, conflicting); });
	require(table.memberFunctionSignatureCount(owner) == 2);
	require(table.memberFunctionSignatureAt(owner, 1) == function);
	const TypeId bad[] = {integer};
	rejects([&] { table.publishMemberFunctionSignatures(table.record(EntityId{40346}), bad); });
	const TypeId dependent_return = table.templateParameter(TemplateDeclId{40348}, 0);
	const TypeId dependent[] = {table.function(dependent_return, std::span<const TypeId>(), false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{})};
	rejects([&] { table.publishMemberFunctionSignatures(table.record(EntityId{40349}), dependent); });
	rejects([&] { table.publishMemberFunctionSignatures(integer, signatures); });
	const TypeId dependent_arguments[] = {dependent_return};
	const TypeId dependent_owner = table.templateSpecialization(TemplateDeclId{40352}, dependent_arguments);
	rejects([&] { table.publishMemberFunctionSignatures(dependent_owner, signatures); });
	const TypeId empty_owner = table.record(EntityId{40347});
	table.publishMemberFunctionSignatures(empty_owner, std::span<const TypeId>());
	require(table.hasMemberFunctionSignatures(empty_owner));
	require(table.memberFunctionSignatureCount(empty_owner) == 0);
	rejects([&] { table.memberFunctionSignatureAt(owner, 2); });
	rejects([&] { table.memberFunctionPointerValue(table.memberFunctionPointer(owner, function), owner, 0); });
	const TypeId zero_owner = table.record(EntityId{40350});
	const TypeId zero_signatures[] = {TypeId{}, function};
	table.publishMemberFunctionSignatures(zero_owner, zero_signatures);
	rejects([&] { table.memberFunctionPointerValue(table.memberFunctionPointer(zero_owner, function), zero_owner, 0); });
}

inline void checkStructuralArrayValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId array = table.array(integer, 2);
	const TypeId values[] = {table.integralValue(integer, 1), table.integralValue(integer, 2)};
	const TypeId value = table.structuralArrayValue(array, values);
	require(table.node(value).kind == CanonicalTypeKind::StructuralArrayValue);
	require(table.node(value).child == array);
	require(unpackStructuralArrayElementCount(table.node(value).array_extent) == 2);
	require(value == table.structuralArrayValue(array, values));
	const TypeId reversed[] = {values[1], values[0]};
	require(value != table.structuralArrayValue(array, reversed));
	rejects([&] { (void)table.structuralArrayValue(array, std::span<const TypeId>(values, 1)); });
	rejects([&] { (void)table.structuralArrayValue(integer, values); });
	const TypeId unknown = table.arrayOfUnknownBound(integer);
	rejects([&] { (void)table.structuralArrayValue(unknown, values); });
	const TypeId bad_elements[] = {integer, integer};
	rejects([&] { (void)table.structuralArrayValue(array, bad_elements); });
	const TypeId nested_array = table.array(array, 2);
	const TypeId nested_values[] = {value, value};
	const TypeId nested = table.structuralArrayValue(nested_array, nested_values);
	require(table.node(nested).kind == CanonicalTypeKind::StructuralArrayValue);
	require(table.node(nested).child == nested_array);
	TypeId link = unpackStructuralValueSubobjects(table.node(nested).array_extent);
	require(table.templateArgumentValue(link) == value);
	link = table.templateArgumentNext(link);
	require(table.templateArgumentValue(link) == value);
	require(!table.templateArgumentNext(link));
	const TypeId record = table.record(EntityId{40112});
	const TypeId record_values[] = {nested};
	const TypeId record_value = table.structuralValue(record, record_values, 0);
	require(table.node(record_value).kind == CanonicalTypeKind::StructuralValue);
	rejects([&] { (void)table.structuralValue(record, record_values, 1); });
}

inline void checkUnionValues() {
	static_assert(sizeof(CanonicalTypeNode) == 16);
	static_assert(sizeof(CanonicalRecordMember) == 16);
	CanonicalTypeTable table;
	const EntityId entity{40201};
	const TypeId owner = table.record(entity);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId short_type = table.builtin(CanonicalBuiltinKind::Short);
	const auto variant = CanonicalRecordMemberFlags::UnionVariant;
	const auto start = variant | CanonicalRecordMemberFlags::UnionGroupStart;
	const auto end = variant | CanonicalRecordMemberFlags::UnionGroupEnd;
	std::array<CanonicalRecordMember, 5> members{{
		{integer, 0, 4, 0, 0, CanonicalAccess::Public, start},
		{integer, 0, 4, 0, 0, CanonicalAccess::Public, end},
		{short_type, 4, 2, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None},
		{integer, 8, 4, 0, 0, CanonicalAccess::Public, start},
		{integer, 8, 4, 0, 0, CanonicalAccess::Public, end},
	}};
	table.publishRecordLayout({entity, 12, 12, 12, 4, 5, 0, CanonicalRecordLayoutFlags::None});
	table.publishRecordFieldSchema(table.record(entity), members, {});
	const TypeId zero = table.integralValue(integer, 0);
	const TypeId one = table.integralValue(integer, 1);
	const TypeId inactive = table.structuralUnionValue(owner, 0, std::nullopt, TypeId{});
	const TypeId first = table.structuralUnionValue(owner, 0, uint16_t{0}, zero);
	const TypeId second = table.structuralUnionValue(owner, 0, uint16_t{1}, zero);
	require(first == table.structuralUnionValue(owner, 0, uint16_t{0}, zero));
	require(first != inactive && first != second);
	require(first != table.structuralUnionValue(owner, 0, uint16_t{0}, one));
	require(first != table.structuralUnionValue(owner, 3, uint16_t{3}, zero));
	require(inactive != table.structuralUnionValue(owner, 3, std::nullopt, TypeId{}));
	require(unpackStructuralUnionFirstVariant(table.node(second).array_extent) == 0);
	require(unpackStructuralUnionActiveVariant(table.node(second).array_extent) == 1);
	require(!unpackStructuralUnionActiveVariant(table.node(inactive).array_extent));
	require(table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(first).array_extent)) == zero);
	const auto rejects = [&](uint16_t group, std::optional<uint16_t> active, TypeId value) {
		try { (void)table.structuralUnionValue(owner, group, active, value); } catch (const InternalError&) { return true; }
		return false;
	};
	require(rejects(1, uint16_t{1}, zero));
	require(rejects(0, uint16_t{3}, zero));
	require(rejects(0, std::nullopt, zero));
	require(rejects(0, uint16_t{0}, TypeId{}));
	require(rejects(0, uint16_t{0}, table.integralValue(short_type, 0)));
	require(rejects(0, uint16_t{0}, integer));
	const CanonicalTemplateArgument argument[] = {CanonicalTemplateArgument::makeValue(first)};
	const TypeId specialization = table.templateSpecialization(TemplateDeclId{40202}, argument);
	require(table.templateArgumentValue(table.templateSpecializationArguments(specialization)) == first);
	const TypeId parameter = table.templateParameter(TemplateDeclId{40203}, 0);
	const CanonicalTemplateArgument mixed[] = {CanonicalTemplateArgument::makeType(parameter), argument[0]};
	const TypeId pattern = table.templateSpecialization(TemplateDeclId{40202}, mixed);
	const TypeId substitutions[] = {integer};
	const CanonicalTemplateArgument concrete[] = {CanonicalTemplateArgument::makeType(integer), argument[0]};
	require(table.substitute(pattern, TemplateDeclId{40203}, substitutions) == table.templateSpecialization(TemplateDeclId{40202}, concrete));
	bool type_rejected = false;
	try { (void)table.pointer(first); } catch (const InternalError&) { type_rejected = true; }
	require(type_rejected);
	const TypeId array_type = table.array(integer, 2);
	const TypeId elements[] = {zero, one};
	const TypeId array_value = table.structuralArrayValue(array_type, elements);
	const EntityId array_entity{40205};
	const TypeId array_owner = table.record(array_entity);
	table.publishRecordLayout({array_entity, 8, 8, 8, 4, 1, 0, CanonicalRecordLayoutFlags::Union});
	const CanonicalRecordMember array_member{array_type, 0, 8, 0, 0, CanonicalAccess::Public, start | CanonicalRecordMemberFlags::UnionGroupEnd};
	table.publishRecordFieldSchema(table.record(array_entity), std::span<const CanonicalRecordMember>(&array_member, 1), {});
	const TypeId active_array = table.structuralUnionValue(array_owner, 0, uint16_t{0}, array_value);
	const TypeId compound[] = {active_array};
	require(table.node(table.structuralValue(owner, compound, 0)).kind == CanonicalTypeKind::StructuralValue);
	require(table.node(table.structuralArrayValue(table.array(array_owner, 1), compound)).kind == CanonicalTypeKind::StructuralArrayValue);
	uint32_t invalid_entity = 40210;
	const auto invalid_schema = [&](std::array<CanonicalRecordMember, 5> invalid) {
		const EntityId target{invalid_entity++};
		table.publishRecordLayout({target, 12, 12, 12, 4, 5, 0, CanonicalRecordLayoutFlags::None});
		try { table.publishRecordFieldSchema(table.record(target), invalid, {}); } catch (const InternalError&) { return true; }
		return false;
	};
	auto invalid = members;
	invalid[0].flags = variant;
	require(invalid_schema(invalid));
	invalid = members;
	invalid[1].flags = variant;
	require(invalid_schema(invalid));
	invalid = members;
	invalid[1].offset_bytes = 4;
	require(invalid_schema(invalid));
	invalid = members;
	invalid[4].flags = variant;
	require(invalid_schema(invalid));
	// A failed transaction must not publish a union value or its declaration schema.
	const size_t before = table.size();
	{
		CanonicalTypeTransaction transaction(table);
		const EntityId temporary{40204};
		table.publishRecordLayout({temporary, 12, 12, 12, 4, 5, 0, CanonicalRecordLayoutFlags::None});
		table.publishRecordFieldSchema(table.record(temporary), members, {});
		(void)table.structuralUnionValue(table.record(temporary), 0, uint16_t{1}, one);
	}
	require(table.size() == before);
	require(!table.hasRecordFieldSchema(table.record(EntityId{40204})));
}

inline void checkFunctionPointerValues() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId parameters[] = {table.builtin(CanonicalBuiltinKind::Short)};
	const TypeId function = table.function(integer, parameters, false, CVQualifier::None, ReferenceQualifier::None,
		false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId safe = table.function(integer, parameters, false, CVQualifier::None, ReferenceQualifier::None,
		true, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId pointer = table.pointer(function);
	const TypeId value = table.functionPointerValue(pointer, EntityId{41}, function);
	require(value == table.functionPointerValue(pointer, EntityId{41}, function));
	require(value == table.functionPointerValue(pointer, EntityId{41}, safe));
	require(value != table.functionPointerValue(pointer, EntityId{42}, function));
	require(value != table.functionPointerValue(table.pointer(safe), EntityId{41}, safe));
	require(value != table.nullPointerValue(pointer));
	require(table.node(value).kind == CanonicalTypeKind::FunctionPointerValue);
	require(table.node(value).child == pointer);
	require(table.node(value).array_extent == 41);
	const auto rejects = [&](auto operation) {
		bool rejected = false;
		try { operation(); } catch (const InternalError&) { rejected = true; }
		require(rejected);
	};
	rejects([&] { table.functionPointerValue(pointer, EntityId{}, function); });
	rejects([&] { table.functionPointerValue(table.pointer(integer), EntityId{41}, function); });
	rejects([&] { table.functionPointerValue(table.pointer(safe), EntityId{41}, function); });
	rejects([&] { table.functionPointerValue(pointer, EntityId{41}, integer); });
	rejects([&] { table.pointer(value); });
	rejects([&] { table.substitute(value, TemplateDeclId{1}, std::span<const TypeId>{}); });
	const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(value)};
	const TypeId owner = table.templateSpecialization(TemplateDeclId{1}, arguments);
	require(table.templateArgumentValue(table.templateSpecializationArguments(owner)) == value);
	const TypeId values[] = {value};
	require(table.node(table.structuralValue(table.record(EntityId{9}), values, 0)).kind == CanonicalTypeKind::StructuralValue);
	require(table.node(table.structuralArrayValue(table.array(pointer, 1), values)).kind == CanonicalTypeKind::StructuralArrayValue);
	const size_t baseline_size = table.size();
	{
		CanonicalTypeTransaction transaction(table);
		table.functionPointerValue(pointer, EntityId{99}, function);
	}
	require(table.size() == baseline_size);
}

inline void checkSpecializationFieldSchemas() {
	CanonicalTypeTable table;
	const TypeId small = table.builtin(CanonicalBuiltinKind::Short);
	const TypeId wide = table.builtin(CanonicalBuiltinKind::LongLong);
	const TypeId arguments[] = {small};
	const TypeId owner = table.templateSpecialization(TemplateDeclId{41001}, arguments);
	const TypeId member_types[] = {small, small};
	const CanonicalRecordMemberFlags start = CanonicalRecordMemberFlags::UnionVariant | CanonicalRecordMemberFlags::UnionGroupStart;
	const CanonicalRecordMemberFlags end = CanonicalRecordMemberFlags::UnionVariant | CanonicalRecordMemberFlags::UnionGroupEnd;
	const CanonicalRecordMember members[] = {
		{small, 0, 2, 0, 0, CanonicalAccess::Public, start},
		{small, 0, 2, 0, 0, CanonicalAccess::Public, end},
	};
	const auto rejects = [&](auto operation) {
		bool rejected = false;
		try { operation(); } catch (const InternalError&) { rejected = true; }
		require(rejected);
	};
	const TypeId member_pointer = table.memberObjectPointer(owner, small);
	rejects([&] { table.memberObjectPointerValue(member_pointer, owner, 0); });
	rejects([&] { table.publishRecordFieldSchema(owner, members, {}); });
	table.publishRecordSubobjectTypes(owner, member_types);
	rejects([&] { table.publishRecordFieldSchema(owner, members, {}); });
	table.publishClassBaseSchema(owner, {});
	table.publishRecordFieldSchema(owner, members, {});
	table.publishRecordFieldSchema(owner, members, {});
	require(table.hasRecordFieldSchema(owner));
	require(table.node(table.memberObjectPointerValue(member_pointer, owner, 1)).kind == CanonicalTypeKind::MemberObjectPointerValue);
	rejects([&] { table.memberObjectPointerValue(member_pointer, owner, 2); });
	require(table.recordMemberAt(owner, 0) == members[0]);
	require(table.recordMemberAt(owner, 1) == members[1]);
	const TypeId zero = table.integralValue(small, 0);
	require(table.structuralUnionValue(owner, 0, uint16_t{0}, zero) != table.structuralUnionValue(owner, 0, uint16_t{1}, zero));
	const TypeId wide_arguments[] = {wide};
	const TypeId other = table.templateSpecialization(TemplateDeclId{41001}, wide_arguments);
	const TypeId wide_types[] = {wide, wide};
	table.publishRecordSubobjectTypes(other, wide_types);
	table.publishClassBaseSchema(other, {});
	rejects([&] { table.publishRecordFieldSchema(other, members, {}); });
	const CanonicalRecordMember wide_members[] = {
		{wide, 0, 8, 0, 0, CanonicalAccess::Public, start},
		{wide, 0, 8, 0, 0, CanonicalAccess::Public, end},
	};
	rejects([&] { table.publishRecordFieldSchema(other, std::span<const CanonicalRecordMember>(), {}); });
	table.publishRecordFieldSchema(other, wide_members, {});
	require(table.recordMemberAt(owner, 0).size_bytes == 2);
	require(table.recordMemberAt(other, 0).size_bytes == 8);
	const TypeId bit_owner = table.templateSpecialization(TemplateDeclId{41003}, arguments);
	const TypeId bit_types[] = {small};
	table.publishRecordSubobjectTypes(bit_owner, bit_types);
	table.publishClassBaseSchema(bit_owner, {});
	const CanonicalRecordMember bit_member{small, 0, 2, 3, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::Bitfield};
	table.publishRecordFieldSchema(bit_owner, std::span<const CanonicalRecordMember>(&bit_member, 1), {});
	rejects([&] { table.memberObjectPointerValue(table.memberObjectPointer(bit_owner, small), bit_owner, 0); });
	const size_t before = table.size();
	TypeId temporary;
	{
		CanonicalTypeTransaction transaction(table);
		temporary = table.templateSpecialization(TemplateDeclId{41002}, arguments);
		table.publishRecordSubobjectTypes(temporary, member_types);
		table.publishClassBaseSchema(temporary, {});
		table.publishRecordFieldSchema(temporary, members, {});
		table.structuralUnionValue(temporary, 0, uint16_t{1}, zero);
	}
	require(table.size() == before);
	require(!table.hasRecordFieldSchema(temporary));
}

inline void checkNestedFieldSchemas() {
	CanonicalTypeTable table;
	const TypeId short_type = table.builtin(CanonicalBuiltinKind::Short);
	const TypeId long_long_type = table.builtin(CanonicalBuiltinKind::LongLong);
	const TypeId arguments[] = {short_type};
	const TypeId owner_spec = table.templateSpecialization(TemplateDeclId{41101}, arguments);
	const TypeId nested = table.nestedRecord(owner_spec, EntityId{41102});
	require(isCanonicalClassTypeKind(CanonicalTypeKind::Record));
	require(isCanonicalClassTypeKind(CanonicalTypeKind::TemplateSpecialization));
	require(isCanonicalClassTypeKind(CanonicalTypeKind::NestedRecord));
	require(!isCanonicalClassTypeKind(CanonicalTypeKind::Enum));
	require(!isCanonicalClassTypeKind(CanonicalTypeKind::Builtin));
	table.publishRecordProperties(nested, CanonicalRecordFacts::Aggregate);
	require(table.hasRecordProperties(nested));
	require(table.recordProperties(nested).facts == CanonicalRecordFacts::Aggregate);
	const TypeId subobjects[] = {long_long_type};
	table.publishRecordSubobjectTypes(nested, subobjects);
	table.publishClassBaseSchema(nested, {});
	require(table.hasClassBaseSchema(nested));
	require(table.classBaseCount(nested) == 0);
	const CanonicalRecordMember member{long_long_type, 8, 8, 0, 0, CanonicalAccess::Public,
		CanonicalRecordMemberFlags::None};
	table.publishRecordFieldSchema(nested, std::span<const CanonicalRecordMember>(&member, 1), {});
	require(table.hasRecordFieldSchema(nested));
	require(table.recordMemberAt(nested, 0) == member);
	const TypeId member_pointer = table.memberObjectPointer(nested, long_long_type);
	require(table.node(table.memberObjectPointerValue(member_pointer, nested, 0)).kind == CanonicalTypeKind::MemberObjectPointerValue);
	const TypeId function_type = table.function(short_type, {}, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	table.publishMemberFunctionSignatures(nested, std::span<const TypeId>(&function_type, 1));
	const TypeId function_pointer = table.memberFunctionPointer(nested, function_type);
	require(table.node(table.memberFunctionPointerValue(function_pointer, nested, 0)).kind == CanonicalTypeKind::MemberFunctionPointerValue);
	TypeSpecifierNode ordered(TypeCategory::LongLong, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	ordered.set_ordered_declarator({DeclaratorComponent::array(2), DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::memberPointer(EntityId{41102}, false, CVQualifier::Volatile)});
	ordered.rebindOrderedMemberOwner(EntityId{41102}, table.record(EntityId{41102}), nested);
	const CanonicalTypeImport imported_ordered = importCanonicalType(table, ordered);
	require(imported_ordered.status == CanonicalTypeImportStatus::Supported);
	const TypeId volatile_member = table.qualify(member_pointer, CVQualifier::Volatile);
	const TypeId const_pointer = table.qualify(table.pointer(volatile_member), CVQualifier::Const);
	require(imported_ordered.type == table.array(const_pointer, 2));
	const CanonicalRecordMember wrong_type{short_type, 8, 2, 0, 0, CanonicalAccess::Public,
		CanonicalRecordMemberFlags::None};
	const CanonicalRecordMember second_member{long_long_type, 16, 8, 0, 0, CanonicalAccess::Public,
		CanonicalRecordMemberFlags::None};
	auto rejects = [&](auto operation) {
		bool rejected = false;
		try { operation(); } catch (const InternalError&) { rejected = true; }
		require(rejected);
	};
	rejects([&] { table.publishRecordFieldSchema(nested, std::span<const CanonicalRecordMember>(&wrong_type, 1), {}); });
	rejects([&] { table.publishRecordFieldSchema(nested, std::span<const CanonicalRecordMember>(&second_member, 1), {}); });
	rejects([&] { table.memberObjectPointerValue(member_pointer, nested, 1); });
	require(!table.hasRecordLayout(EntityId{41102}));
	const size_t baseline_size = table.size();
	TypeId temporary;
	{
		CanonicalTypeTransaction transaction(table);
		const TypeId nested_owner = table.nestedRecord(owner_spec, EntityId{41103});
		temporary = nested_owner;
		table.publishRecordProperties(nested_owner, CanonicalRecordFacts::Aggregate);
		table.publishRecordSubobjectTypes(nested_owner, subobjects);
		table.publishClassBaseSchema(nested_owner, {});
		table.publishMemberFunctionSignatures(nested_owner, std::span<const TypeId>(&function_type, 1));
		table.publishRecordFieldSchema(nested_owner, std::span<const CanonicalRecordMember>(&member, 1), {});
		table.memberObjectPointerValue(table.memberObjectPointer(nested_owner, long_long_type), nested_owner, 0);
	}
	require(table.size() == baseline_size);
	require(!table.hasRecordFieldSchema(temporary));
	require(!table.hasRecordProperties(temporary));
	require(!table.hasRecordSubobjectTypes(temporary));
	require(!table.hasClassBaseSchema(temporary));
	require(!table.hasMemberFunctionSignatures(temporary));
	const TypeId republished = table.nestedRecord(owner_spec, EntityId{41103});
	require(republished == temporary);
	require(!table.hasRecordFieldSchema(republished));
}

inline void checkRecordMemberProperties() {
	CanonicalTypeTable table;
	const EntityId entity{40501};
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	table.publishRecordLayout({entity, 8, 8, 8, 4, 2, 0, CanonicalRecordLayoutFlags::None});
	const CanonicalRecordMember members[] = {
		{integer, 0, 4, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None},
		{integer, 4, 4, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None},
	};
	table.publishRecordFieldSchema(table.record(entity), members, {});
	const CanonicalRecordMemberFacts facts[] = {
		CanonicalRecordMemberFacts::TriviallyCopyable | CanonicalRecordMemberFacts::Trivial |
			CanonicalRecordMemberFacts::Destructible | CanonicalRecordMemberFacts::TriviallyDestructible |
			CanonicalRecordMemberFacts::NothrowDestructible | CanonicalRecordMemberFacts::HasTrivialDestructor |
			CanonicalRecordMemberFacts::DefaultConstructible | CanonicalRecordMemberFacts::TriviallyDefaultConstructible |
			CanonicalRecordMemberFacts::NothrowDefaultConstructible | CanonicalRecordMemberFacts::Empty |
			CanonicalRecordMemberFacts::StandardLayout,
		CanonicalRecordMemberFacts::DestructorDeleted,
	};
	table.publishRecordMemberProperties(table.record(entity), facts);
	require(table.hasRecordMemberProperties(table.record(entity)));
	require(table.recordMemberPropertyCount(table.record(entity)) == 2);
	require(table.recordMemberPropertiesAt(table.record(entity), 0) == facts[0]);
	require(table.recordMemberPropertiesAt(table.record(entity), 1) == facts[1]);
	table.publishRecordMemberProperties(table.record(entity), facts);
	CanonicalRecordMemberFacts conflicting[] = {facts[0], facts[0]};
	bool rejected = false;
	try { table.publishRecordMemberProperties(table.record(entity), conflicting); } catch (const InternalError&) { rejected = true; }
	require(rejected);
	const EntityId temporary_entity{40502};
	{
		CanonicalTypeTransaction transaction(table);
		const TypeId temporary = table.record(temporary_entity);
		table.publishRecordLayout({temporary_entity, 8, 8, 8, 4, 2, 0, CanonicalRecordLayoutFlags::None});
		table.publishRecordFieldSchema(temporary, members, {});
		table.publishRecordMemberProperties(temporary, facts);
		require(table.hasRecordMemberProperties(temporary));
		require(table.recordMemberPropertiesAt(temporary, 1) == facts[1]);
	}
	require(!table.hasRecordMemberProperties(table.record(temporary_entity)));
	const CanonicalRecordMemberFacts short_facts[] = {facts[0]};
	rejected = false;
	try { table.publishRecordMemberProperties(table.record(entity), short_facts); } catch (const InternalError&) { rejected = true; }
	require(rejected);
	const TypeId aggregate = table.record(EntityId{40503});
	const CanonicalRecordFacts aggregate_facts = CanonicalRecordFacts::TriviallyCopyable | CanonicalRecordFacts::Trivial | CanonicalRecordFacts::Pod |
		CanonicalRecordFacts::Destructible | CanonicalRecordFacts::TriviallyDestructible |
		CanonicalRecordFacts::NothrowDestructible | CanonicalRecordFacts::DefaultConstructible |
		CanonicalRecordFacts::TriviallyDefaultConstructible | CanonicalRecordFacts::NothrowDefaultConstructible |
		CanonicalRecordFacts::AggregateTraitFacts | CanonicalRecordFacts::AggregateDefaultConstructionFacts;
	table.publishRecordProperties(aggregate, aggregate_facts);
	require(table.recordProperties(aggregate).facts == aggregate_facts);
	table.publishRecordProperties(aggregate, aggregate_facts);
	rejected = false;
	try {
		table.publishRecordProperties(aggregate, aggregate_facts | CanonicalRecordFacts::HasVirtualDestructor);
	} catch (const InternalError&) {
		rejected = true;
	}
	require(rejected);
}

// The native harness links only CanonicalTypes.cpp and the adapter. No Parser
// and no SemanticAnalysis member-context stack exist in this translation unit,
// so every probe below rebuilds identity from stable structural inputs: TypeId,
// EntityId, TemplateDeclId, and identifier bytes.
inline void internUnrelatedCanonicalSpelling(std::string_view spelling) {
	StringTable::getOrInternStringHandle(spelling);
}

inline void checkFunctionIdentityIndependence() {
	CanonicalTypeTable forward;
	const auto void_type = forward.builtin(CanonicalBuiltinKind::Void);
	const auto integer = forward.builtin(CanonicalBuiltinKind::Int);
	const auto long_integer = forward.builtin(CanonicalBuiltinKind::Long);
	const TypeId int_parameter[] = {integer};
	const TypeId long_parameter[] = {long_integer};
	const TypeId mixed_parameters[] = {integer, long_integer};
	const auto plain_function = [&](std::span<const TypeId> parameters, bool variadic) {
		return forward.function(void_type, parameters, variadic, CVQualifier::None, ReferenceQualifier::None,
			false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	};
	const auto void_int = plain_function(int_parameter, false);
	const auto void_long = plain_function(long_parameter, false);
	const auto void_mixed = plain_function(mixed_parameters, false);
	const auto void_variadic = plain_function(int_parameter, true);
	const auto int_pointer = forward.pointer(void_int);
	const auto long_pointer = forward.pointer(void_long);
	const auto mixed_pointer = forward.pointer(void_mixed);
	const auto variadic_pointer = forward.pointer(void_variadic);
	const auto owner = forward.record(EntityId{7601});
	const auto other_owner = forward.record(EntityId{7602});
	const auto member_int = forward.memberFunctionPointer(owner, void_int);
	const auto member_long = forward.memberFunctionPointer(owner, void_long);
	const auto member_mixed = forward.memberFunctionPointer(owner, void_mixed);
	const auto member_other_owner = forward.memberFunctionPointer(other_owner, void_int);
	require(void_int != void_long && void_long != void_mixed && void_int != void_variadic);
	require(int_pointer != long_pointer && int_pointer != mixed_pointer && int_pointer != variadic_pointer);
	require(member_int != member_long && member_int != member_mixed && member_int != member_other_owner);
	require(member_int != int_pointer);

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated function identity spelling E");
	require(forward.memberFunctionPointer(owner, void_int) == member_int);
	internUnrelatedCanonicalSpelling("unrelated function identity spelling D");
	require(plain_function(int_parameter, false) == void_int);
	internUnrelatedCanonicalSpelling("unrelated function identity spelling C");
	require(forward.pointer(void_int) == int_pointer);
	internUnrelatedCanonicalSpelling("unrelated function identity spelling B");
	require(forward.pointer(void_mixed) == mixed_pointer);
	internUnrelatedCanonicalSpelling("unrelated function identity spelling A");
	require(forward.pointer(void_long) == long_pointer);
	require(forward.pointer(void_variadic) == variadic_pointer);
	require(forward.memberFunctionPointer(other_owner, void_int) == member_other_owner);
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	// Opposite order: unrelated structural requests first, then owners, then the
	// long parameter form, with the remaining probes in reverse declaration order.
	reverse.templateParameter(TemplateDeclId{7699}, 0);
	reverse.array(reverse.builtin(CanonicalBuiltinKind::Char), 7);
	const auto reverse_owner = reverse.record(EntityId{7601});
	const auto reverse_other_owner = reverse.record(EntityId{7602});
	const auto reverse_long = reverse.builtin(CanonicalBuiltinKind::Long);
	const auto reverse_void = reverse.builtin(CanonicalBuiltinKind::Void);
	const auto reverse_int = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_int_parameter[] = {reverse_int};
	const TypeId reverse_long_parameter[] = {reverse_long};
	const TypeId reverse_mixed_parameters[] = {reverse_int, reverse_long};
	const auto reverse_plain_function = [&](std::span<const TypeId> parameters, bool variadic) {
		return reverse.function(reverse_void, parameters, variadic, CVQualifier::None, ReferenceQualifier::None,
			false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	};
	const auto reverse_void_long = reverse_plain_function(reverse_long_parameter, false);
	const auto reverse_void_int = reverse_plain_function(reverse_int_parameter, false);
	const auto reverse_void_mixed = reverse_plain_function(reverse_mixed_parameters, false);
	const auto reverse_void_variadic = reverse_plain_function(reverse_int_parameter, true);
	const auto reverse_long_pointer = reverse.pointer(reverse_void_long);
	const auto reverse_int_pointer = reverse.pointer(reverse_void_int);
	const auto reverse_mixed_pointer = reverse.pointer(reverse_void_mixed);
	const auto reverse_variadic_pointer = reverse.pointer(reverse_void_variadic);
	const auto reverse_member_int = reverse.memberFunctionPointer(reverse_owner, reverse_void_int);
	const auto reverse_member_long = reverse.memberFunctionPointer(reverse_owner, reverse_void_long);
	const auto reverse_member_mixed = reverse.memberFunctionPointer(reverse_owner, reverse_void_mixed);
	const auto reverse_member_other_owner = reverse.memberFunctionPointer(reverse_other_owner, reverse_void_int);
	require(sameStructure(forward, void_int, reverse, reverse_void_int));
	require(sameStructure(forward, void_mixed, reverse, reverse_void_mixed));
	require(sameStructure(forward, void_variadic, reverse, reverse_void_variadic));
	require(sameStructure(forward, int_pointer, reverse, reverse_int_pointer));
	require(sameStructure(forward, mixed_pointer, reverse, reverse_mixed_pointer));
	require(sameStructure(forward, variadic_pointer, reverse, reverse_variadic_pointer));
	require(sameStructure(forward, member_int, reverse, reverse_member_int));
	require(sameStructure(forward, member_mixed, reverse, reverse_member_mixed));
	require(sameStructure(forward, member_other_owner, reverse, reverse_member_other_owner));
	require(!sameStructure(forward, int_pointer, reverse, reverse_long_pointer));
	require(!sameStructure(forward, void_int, reverse, reverse_void_long));
	require(!sameStructure(forward, member_int, reverse, reverse_member_long));
	require(!sameStructure(forward, member_int, reverse, reverse_member_other_owner));
}

inline void checkDeclaratorIdentityIndependence() {
	CanonicalTypeTable forward;
	const auto integer = forward.builtin(CanonicalBuiltinKind::Int);
	forward.builtin(CanonicalBuiltinKind::Long);
	const auto int_array_3 = forward.array(integer, 3);
	const auto int_array_2x3 = forward.array(int_array_3, 2);
	const auto int_array_2x3_swapped = forward.array(forward.array(integer, 2), 3);
	const auto pointer_to_int = forward.pointer(integer);
	const auto array_of_pointer_3 = forward.array(pointer_to_int, 3);
	const auto pointer_to_array_3 = forward.pointer(int_array_3);
	const auto pointer_to_array_2x3 = forward.pointer(int_array_2x3);
	const auto array_of_pointer_to_array_4 = forward.array(forward.pointer(forward.array(integer, 4)), 3);
	const auto interleaved_shape = forward.pointer(forward.array(forward.pointer(forward.array(integer, 4)), 3));
	require(int_array_2x3 != int_array_2x3_swapped);
	require(pointer_to_array_3 != array_of_pointer_3);
	require(pointer_to_array_2x3 != interleaved_shape);
	require(array_of_pointer_to_array_4 != interleaved_shape);
	require(pointer_to_array_2x3 != pointer_to_array_3);

	TypeSpecifierNode ordinary_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const size_t dimensions[] = {2, 3};
	ordinary_array.set_array_dimensions(dimensions);
	require(importCanonicalType(forward, ordinary_array).type == int_array_2x3);
	TypeSpecifierNode pointee_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointee_array.add_pointer_level(CVQualifier::None);
	pointee_array.set_pointee_array_dimensions(dimensions);
	pointee_array.set_pointee_array_declarator(true);
	require(importCanonicalType(forward, pointee_array).type == pointer_to_array_2x3);
	TypeSpecifierNode interleaved_syntax(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	interleaved_syntax.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
	});
	require(importCanonicalType(forward, interleaved_syntax).type == interleaved_shape);

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated declarator identity spelling D");
	require(forward.array(integer, 3) == int_array_3);
	internUnrelatedCanonicalSpelling("unrelated declarator identity spelling C");
	require(forward.array(int_array_3, 2) == int_array_2x3);
	internUnrelatedCanonicalSpelling("unrelated declarator identity spelling B");
	require(forward.pointer(int_array_2x3) == pointer_to_array_2x3);
	internUnrelatedCanonicalSpelling("unrelated declarator identity spelling A");
	require(forward.array(forward.pointer(forward.array(integer, 4)), 3) == array_of_pointer_to_array_4);
	require(importCanonicalType(forward, interleaved_syntax).type == interleaved_shape);
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	// The adapter reconstructs the interleaved syntax before any of the direct
	// structural probes, and the long base is interned first.
	TypeSpecifierNode reverse_interleaved_syntax = interleaved_syntax;
	TypeSpecifierNode reverse_pointee_array = pointee_array;
	TypeSpecifierNode reverse_ordinary_array = ordinary_array;
	reverse.builtin(CanonicalBuiltinKind::Long);
	const auto reverse_interleaved = importCanonicalType(reverse, reverse_interleaved_syntax).type;
	const auto reverse_pointee_array_type = importCanonicalType(reverse, reverse_pointee_array).type;
	const auto reverse_ordinary_array_type = importCanonicalType(reverse, reverse_ordinary_array).type;
	const auto reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const auto reverse_array_pointer_to_array_4 = reverse.array(
		reverse.pointer(reverse.array(reverse_integer, 4)), 3);
	const auto reverse_pointer_to_array_2x3 = reverse.pointer(reverse.array(reverse.array(reverse_integer, 3), 2));
	const auto reverse_pointer_to_array_3 = reverse.pointer(reverse.array(reverse_integer, 3));
	const auto reverse_array_of_pointer_3 = reverse.array(reverse.pointer(reverse_integer), 3);
	const auto reverse_int_array_2x3_swapped = reverse.array(reverse.array(reverse_integer, 2), 3);
	const auto reverse_int_array_2x3 = reverse.array(reverse.array(reverse_integer, 3), 2);
	const auto reverse_int_array_3 = reverse.array(reverse_integer, 3);
	require(sameStructure(forward, interleaved_shape, reverse, reverse_interleaved));
	require(sameStructure(forward, pointer_to_array_2x3, reverse, reverse_pointer_to_array_2x3));
	require(sameStructure(forward, int_array_2x3, reverse, reverse_int_array_2x3));
	require(sameStructure(forward, int_array_2x3, reverse, reverse_ordinary_array_type));
	require(sameStructure(forward, pointer_to_array_2x3, reverse, reverse_pointee_array_type));
	require(sameStructure(forward, array_of_pointer_to_array_4, reverse, reverse_array_pointer_to_array_4));
	require(sameStructure(forward, pointer_to_array_3, reverse, reverse_pointer_to_array_3));
	require(sameStructure(forward, array_of_pointer_3, reverse, reverse_array_of_pointer_3));
	require(sameStructure(forward, int_array_3, reverse, reverse_int_array_3));
	require(!sameStructure(forward, pointer_to_array_3, reverse, reverse_array_of_pointer_3));
	require(!sameStructure(forward, int_array_2x3, reverse, reverse_int_array_2x3_swapped));
	require(!sameStructure(forward, interleaved_shape, reverse, reverse_array_pointer_to_array_4));
	require(!sameStructure(forward, pointer_to_array_2x3, reverse, reverse_pointer_to_array_3));

	// The structural export is a declarator spine, not parser or member-context
	// state, so importing it into the opposite-order table rebuilds identity.
	const CanonicalDeclaratorExport exported = exportCanonicalDeclarator(forward, interleaved_shape);
	require(exported.status == CanonicalTypeImportStatus::Supported);
	TypeSpecifierNode reconstructed(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	reconstructed.set_ordered_declarator(exported.components);
	require(sameStructure(forward, interleaved_shape, reverse, importCanonicalType(reverse, reconstructed).type));
}

inline void checkRecordSpecializationIdentityIndependence() {
	CanonicalTypeTable forward;
	const auto integer = forward.builtin(CanonicalBuiltinKind::Int);
	const auto long_integer = forward.builtin(CanonicalBuiltinKind::Long);
	forward.builtin(CanonicalBuiltinKind::Char);
	const auto record_a = forward.record(EntityId{7701});
	const auto record_b = forward.record(EntityId{7702});
	const TypeId first_args[] = {integer, long_integer};
	const TypeId reversed_args[] = {long_integer, integer};
	const auto spec_a = forward.templateSpecialization(TemplateDeclId{7801}, first_args);
	const auto spec_reordered = forward.templateSpecialization(TemplateDeclId{7801}, reversed_args);
	const auto spec_other = forward.templateSpecialization(TemplateDeclId{7802}, first_args);
	const auto nested_a = forward.nestedRecord(spec_a, EntityId{7703});
	const auto nested_a_deep = forward.nestedRecord(nested_a, EntityId{7704});
	const auto nested_other = forward.nestedRecord(spec_other, EntityId{7703});
	const CanonicalTemplateArgument member_int_arg[] = {CanonicalTemplateArgument::makeType(integer)};
	const CanonicalTemplateArgument member_long_arg[] = {CanonicalTemplateArgument::makeType(long_integer)};
	const auto member_a = forward.memberTemplateSpecialization(TemplateDeclId{7803}, spec_a, member_int_arg);
	const auto member_a_long = forward.memberTemplateSpecialization(TemplateDeclId{7803}, spec_a, member_long_arg);
	const auto member_b = forward.memberTemplateSpecialization(TemplateDeclId{7804}, spec_a, member_int_arg);
	const auto member_other_owner = forward.memberTemplateSpecialization(
		TemplateDeclId{7803}, spec_other, member_int_arg);
	const auto inner_member = forward.memberTemplateSpecialization(TemplateDeclId{7805}, member_a, member_int_arg);
	require(record_a != record_b);
	require(spec_a != spec_reordered && spec_a != spec_other);
	require(nested_a != nested_other && nested_a != nested_a_deep);
	require(member_a != member_a_long && member_a != member_b && member_a != member_other_owner);
	require(forward.nestedRecordOwner(nested_a) == spec_a);
	require(forward.nestedRecordEntity(nested_a) == EntityId{7703});
	require(forward.nestedRecordOwner(nested_a_deep) == nested_a);
	require(forward.nestedRecordEntity(nested_a_deep) == EntityId{7704});
	require(forward.templateSpecializationDecl(member_a) == TemplateDeclId{7803});
	require(forward.templateSpecializationOwner(member_a) == spec_a);
	require(forward.templateSpecializationDecl(inner_member) == TemplateDeclId{7805});
	require(forward.templateSpecializationOwner(inner_member) == member_a);

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated specialization identity spelling D");
	require(forward.record(EntityId{7701}) == record_a);
	internUnrelatedCanonicalSpelling("unrelated specialization identity spelling C");
	require(forward.templateSpecialization(TemplateDeclId{7801}, first_args) == spec_a);
	internUnrelatedCanonicalSpelling("unrelated specialization identity spelling B");
	require(forward.nestedRecord(nested_a, EntityId{7704}) == nested_a_deep);
	internUnrelatedCanonicalSpelling("unrelated specialization identity spelling A");
	require(forward.memberTemplateSpecialization(TemplateDeclId{7805}, member_a, member_int_arg) == inner_member);
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	reverse.pointer(reverse.builtin(CanonicalBuiltinKind::Float));
	const auto reverse_long = reverse.builtin(CanonicalBuiltinKind::Long);
	reverse.builtin(CanonicalBuiltinKind::Char);
	const auto reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const auto reverse_record_b = reverse.record(EntityId{7702});
	const auto reverse_record_a = reverse.record(EntityId{7701});
	const TypeId reverse_first_args[] = {reverse_integer, reverse_long};
	const TypeId reverse_reversed_args[] = {reverse_long, reverse_integer};
	const auto reverse_spec_other = reverse.templateSpecialization(TemplateDeclId{7802}, reverse_first_args);
	const auto reverse_spec_reordered = reverse.templateSpecialization(
		TemplateDeclId{7801}, reverse_reversed_args);
	const auto reverse_spec_a = reverse.templateSpecialization(TemplateDeclId{7801}, reverse_first_args);
	const auto reverse_nested_other = reverse.nestedRecord(reverse_spec_other, EntityId{7703});
	const auto reverse_nested_a = reverse.nestedRecord(reverse_spec_a, EntityId{7703});
	const auto reverse_nested_a_deep = reverse.nestedRecord(reverse_nested_a, EntityId{7704});
	const CanonicalTemplateArgument reverse_member_int_arg[] = {
		CanonicalTemplateArgument::makeType(reverse_integer)};
	const CanonicalTemplateArgument reverse_member_long_arg[] = {
		CanonicalTemplateArgument::makeType(reverse_long)};
	const auto reverse_member_other_owner = reverse.memberTemplateSpecialization(
		TemplateDeclId{7803}, reverse_spec_other, reverse_member_int_arg);
	const auto reverse_member_b = reverse.memberTemplateSpecialization(
		TemplateDeclId{7804}, reverse_spec_a, reverse_member_int_arg);
	const auto reverse_member_a_long = reverse.memberTemplateSpecialization(
		TemplateDeclId{7803}, reverse_spec_a, reverse_member_long_arg);
	const auto reverse_member_a = reverse.memberTemplateSpecialization(
		TemplateDeclId{7803}, reverse_spec_a, reverse_member_int_arg);
	const auto reverse_inner_member = reverse.memberTemplateSpecialization(
		TemplateDeclId{7805}, reverse_member_a, reverse_member_int_arg);
	require(sameStructure(forward, record_a, reverse, reverse_record_a));
	require(sameStructure(forward, spec_a, reverse, reverse_spec_a));
	require(sameStructure(forward, nested_a, reverse, reverse_nested_a));
	require(sameStructure(forward, nested_a_deep, reverse, reverse_nested_a_deep));
	require(sameStructure(forward, member_a, reverse, reverse_member_a));
	require(sameStructure(forward, inner_member, reverse, reverse_inner_member));
	require(!sameStructure(forward, record_a, reverse, reverse_record_b));
	require(!sameStructure(forward, spec_a, reverse, reverse_spec_reordered));
	require(!sameStructure(forward, spec_a, reverse, reverse_spec_other));
	require(!sameStructure(forward, nested_a, reverse, reverse_nested_other));
	require(!sameStructure(forward, member_a, reverse, reverse_member_a_long));
	require(!sameStructure(forward, member_a, reverse, reverse_member_b));
	require(!sameStructure(forward, member_a, reverse, reverse_member_other_owner));

	// A published EntityId and a stamped TemplateDeclId plus concrete arguments
	// are enough to rebuild the same identity through the adapter.
	TypeSpecifierNode record_syntax(TypeCategory::Struct, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	record_syntax.set_type_entity(EntityId{7701});
	require(sameStructure(forward, record_a, reverse, importCanonicalType(reverse, record_syntax).type));
	TypeSpecifierNode int_argument(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode long_argument(TypeCategory::Long, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	std::vector<TypeSpecifierNode> specialization_arguments{int_argument, long_argument};
	TypeSpecifierNode spec_syntax(TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	spec_syntax.set_template_specialization(TemplateDeclId{7801}, specialization_arguments);
	require(sameStructure(forward, spec_a, reverse, importCanonicalType(reverse, spec_syntax).type));
}

inline void checkDependentIdentityIndependence() {
	CanonicalTypeTable forward;
	const auto integer = forward.builtin(CanonicalBuiltinKind::Int);
	const auto long_integer = forward.builtin(CanonicalBuiltinKind::Long);
	const auto parameter = forward.templateParameter(TemplateDeclId{7901}, 0);
	const auto other_parameter = forward.templateParameter(TemplateDeclId{7901}, 1);
	const auto foreign_parameter = forward.templateParameter(TemplateDeclId{7902}, 0);
	const auto member = forward.dependentName(parameter, "member");
	const auto other_member = forward.dependentName(parameter, "other");
	const auto parameter_member = forward.dependentName(other_parameter, "member");
	const auto nested_member = forward.dependentName(member, "type");
	const TypeId int_arguments[] = {integer};
	const TypeId long_arguments[] = {long_integer};
	const auto template_member = forward.dependentTemplateMember(parameter, "Member", int_arguments);
	const auto template_member_long = forward.dependentTemplateMember(parameter, "Member", long_arguments);
	const auto other_template_member = forward.dependentTemplateMember(parameter, "Other", int_arguments);
	const auto foreign_template_member = forward.dependentTemplateMember(foreign_parameter, "Member", int_arguments);
	const auto nested_template_member = forward.dependentName(template_member, "type");
	const auto member_alias = forward.dependentMemberAlias(parameter, TemplateDeclId{7903}, int_arguments);
	const auto other_member_alias = forward.dependentMemberAlias(parameter, TemplateDeclId{7904}, int_arguments);
	require(member != other_member && member != parameter_member);
	require(template_member != template_member_long && template_member != other_template_member);
	require(template_member != foreign_template_member);
	require(nested_template_member != forward.dependentName(other_template_member, "type"));
	require(member_alias != other_member_alias && member_alias != member);
	require(forward.dependentNameIdentifier(member) == "member");
	require(forward.dependentNameQualifier(member) == parameter);
	require(forward.dependentNameIdentifier(template_member) == "Member");
	require(forward.dependentNameQualifier(template_member) == parameter);
	require(forward.dependentMemberAliasDecl(member_alias) == TemplateDeclId{7903});
	require(forward.templateArgumentType(forward.dependentTemplateMemberArguments(template_member)) == integer);

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated dependent identity spelling D");
	require(forward.dependentName(parameter, "member") == member);
	internUnrelatedCanonicalSpelling("unrelated dependent identity spelling C");
	require(forward.dependentName(member, "type") == nested_member);
	internUnrelatedCanonicalSpelling("unrelated dependent identity spelling B");
	require(forward.dependentTemplateMember(parameter, "Member", int_arguments) == template_member);
	internUnrelatedCanonicalSpelling("unrelated dependent identity spelling A");
	require(forward.dependentMemberAlias(parameter, TemplateDeclId{7903}, int_arguments) == member_alias);
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	reverse.dependentName(reverse.templateParameter(TemplateDeclId{7999}, 0), "unrelated");
	const auto reverse_foreign_parameter = reverse.templateParameter(TemplateDeclId{7902}, 0);
	const auto reverse_other_parameter = reverse.templateParameter(TemplateDeclId{7901}, 1);
	const auto reverse_parameter = reverse.templateParameter(TemplateDeclId{7901}, 0);
	const auto reverse_long = reverse.builtin(CanonicalBuiltinKind::Long);
	const auto reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_long_arguments[] = {reverse_long};
	const TypeId reverse_int_arguments[] = {reverse_integer};
	const auto reverse_other_member_alias = reverse.dependentMemberAlias(
		reverse_parameter, TemplateDeclId{7904}, reverse_int_arguments);
	const auto reverse_member_alias = reverse.dependentMemberAlias(
		reverse_parameter, TemplateDeclId{7903}, reverse_int_arguments);
	const auto reverse_other_template_member = reverse.dependentTemplateMember(
		reverse_parameter, "Other", reverse_int_arguments);
	const auto reverse_template_member_long = reverse.dependentTemplateMember(
		reverse_parameter, "Member", reverse_long_arguments);
	const auto reverse_foreign_template_member = reverse.dependentTemplateMember(
		reverse_foreign_parameter, "Member", reverse_int_arguments);
	const auto reverse_template_member = reverse.dependentTemplateMember(
		reverse_parameter, "Member", reverse_int_arguments);
	const auto reverse_parameter_member = reverse.dependentName(reverse_other_parameter, "member");
	const auto reverse_other_member = reverse.dependentName(reverse_parameter, "other");
	const auto reverse_member = reverse.dependentName(reverse_parameter, "member");
	const auto reverse_nested_member = reverse.dependentName(reverse_member, "type");
	const auto reverse_nested_template_member = reverse.dependentName(reverse_template_member, "type");
	require(sameStructure(forward, member, reverse, reverse_member));
	require(sameStructure(forward, nested_member, reverse, reverse_nested_member));
	require(sameStructure(forward, template_member, reverse, reverse_template_member));
	require(sameStructure(forward, nested_template_member, reverse, reverse_nested_template_member));
	require(sameStructure(forward, member_alias, reverse, reverse_member_alias));
	require(!sameStructure(forward, member, reverse, reverse_other_member));
	require(!sameStructure(forward, member, reverse, reverse_parameter_member));
	require(!sameStructure(forward, template_member, reverse, reverse_template_member_long));
	require(!sameStructure(forward, template_member, reverse, reverse_other_template_member));
	require(!sameStructure(forward, template_member, reverse, reverse_foreign_template_member));
	require(!sameStructure(forward, member_alias, reverse, reverse_other_member_alias));

	// The adapter consumes a DependentName-family TypeId directly; the only
	// inputs are the canonical qualifier link and the stored identifier bytes.
	TypeSpecifierNode dependent_syntax(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	dependent_syntax.set_dependent_name_type(reverse_nested_template_member);
	require(importCanonicalType(reverse, dependent_syntax).type ==
		reverse.qualify(reverse_nested_template_member, CVQualifier::None));
}

// A member alias resolves through its published owner environment. Two tables
// that see the same declarations in opposite order must resolve to the same
// structural target, and unrelated string-table insertions must not renumber it.
inline void checkMemberAliasOwnerIdentityIndependence() {
	CanonicalTypeTable forward;
	const TemplateDeclId owner{8601};
	const TemplateDeclId alias{8602};
	const TemplateDeclId target_decl{8603};
	const TypeId owner_arg = forward.builtin(CanonicalBuiltinKind::Int);
	const TypeId alias_arg = forward.builtin(CanonicalBuiltinKind::Char);
	const TypeId owner_parameter = forward.templateParameter(owner, 0);
	const TypeId alias_parameter = forward.templateParameter(alias, 0);
	const TypeId target = forward.templateSpecialization(target_decl, std::array<TypeId, 2>{owner_parameter, alias_parameter});
	const CanonicalTemplateArgKind kinds[] = {CanonicalTemplateArgKind::Type};
	forward.publishAliasTemplateTarget(alias, owner, target, kinds);
	const TypeId owner_arguments[] = {owner_arg};
	const TypeId alias_arguments[] = {alias_arg};
	const TypeId qualifier = forward.templateSpecialization(owner, owner_arguments);
	const TypeId member_use = forward.dependentTemplateMember(qualifier, "Pointer", alias_arguments);
	const TypeId member_argument = forward.templateArgumentType(forward.dependentTemplateMemberArguments(member_use));
	const TypeId resolved_arguments[] = {member_argument};
	const std::optional<TypeId> resolved =
		forward.resolveMemberAliasTarget(alias, forward.dependentNameQualifier(member_use), resolved_arguments);
	require(resolved.has_value());
	require(*resolved == forward.templateSpecialization(target_decl, std::array<TypeId, 2>{owner_arg, alias_arg}));

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated member alias identity spelling C");
	require(forward.resolveMemberAliasTarget(alias, forward.dependentNameQualifier(member_use), resolved_arguments) == resolved);
	internUnrelatedCanonicalSpelling("unrelated member alias identity spelling B");
	require(forward.dependentTemplateMember(qualifier, "Pointer", alias_arguments) == member_use);
	internUnrelatedCanonicalSpelling("unrelated member alias identity spelling A");
	require(forward.templateSpecialization(owner, owner_arguments) == qualifier);
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	reverse.templateParameter(TemplateDeclId{8699}, 0);
	reverse.array(reverse.builtin(CanonicalBuiltinKind::Char), 5);
	const TypeId reverse_alias_argument = reverse.builtin(CanonicalBuiltinKind::Char);
	const TypeId reverse_alias_parameter = reverse.templateParameter(alias, 0);
	const TypeId reverse_owner_argument = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_owner_parameter = reverse.templateParameter(owner, 0);
	const TypeId reverse_target = reverse.templateSpecialization(target_decl,
		std::array<TypeId, 2>{reverse_owner_parameter, reverse_alias_parameter});
	reverse.publishAliasTemplateTarget(alias, owner, reverse_target, kinds);
	const TypeId reverse_owner_arguments[] = {reverse_owner_argument};
	const TypeId reverse_alias_arguments[] = {reverse_alias_argument};
	const TypeId reverse_qualifier = reverse.templateSpecialization(owner, reverse_owner_arguments);
	const TypeId reverse_member_use = reverse.dependentTemplateMember(reverse_qualifier, "Pointer", reverse_alias_arguments);
	const TypeId reverse_member_argument = reverse.templateArgumentType(reverse.dependentTemplateMemberArguments(reverse_member_use));
	const TypeId reverse_resolved_arguments[] = {reverse_member_argument};
	const std::optional<TypeId> reverse_resolved = reverse.resolveMemberAliasTarget(alias,
		reverse.dependentNameQualifier(reverse_member_use), reverse_resolved_arguments);
	require(reverse_resolved.has_value());
	require(sameStructure(forward, *resolved, reverse, *reverse_resolved));
	// Swapping owner and alias arguments must not produce the same target.
	require(forward.templateSpecialization(target_decl, std::array<TypeId, 2>{alias_arg, owner_arg}) != *resolved);
}

// Concrete pack arguments keep their ordered identity across intern order and
// unrelated string-table insertions, including when the element types are
// requested in the reverse order in a second table.
inline void checkConcretePackSpecArgIdentityIndependence() {
	CanonicalTypeTable forward;
	const TypeId integer = forward.builtin(CanonicalBuiltinKind::Int);
	const TypeId character = forward.builtin(CanonicalBuiltinKind::Char);
	const TypeId floating = forward.builtin(CanonicalBuiltinKind::Double);
	const CanonicalTemplateArgument pack[] = {
		CanonicalTemplateArgument::makeType(integer),
		CanonicalTemplateArgument::makeType(character),
		CanonicalTemplateArgument::makeType(floating),
	};
	const TypeId spec = forward.templateSpecialization(TemplateDeclId{8610}, pack);
	const TypeId permuted = forward.templateSpecialization(TemplateDeclId{8610},
		std::array<CanonicalTemplateArgument, 3>{
			CanonicalTemplateArgument::makeType(integer),
			CanonicalTemplateArgument::makeType(floating),
			CanonicalTemplateArgument::makeType(character),
		});
	require(spec != permuted);

	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated pack argument identity spelling B");
	require(forward.templateSpecialization(TemplateDeclId{8610}, pack) == spec);
	internUnrelatedCanonicalSpelling("unrelated pack argument identity spelling A");
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	const TypeId reverse_floating = reverse.builtin(CanonicalBuiltinKind::Double);
	reverse.templateParameter(TemplateDeclId{8698}, 0);
	const TypeId reverse_character = reverse.builtin(CanonicalBuiltinKind::Char);
	const TypeId reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const CanonicalTemplateArgument reverse_pack[] = {
		CanonicalTemplateArgument::makeType(reverse_integer),
		CanonicalTemplateArgument::makeType(reverse_character),
		CanonicalTemplateArgument::makeType(reverse_floating),
	};
	const TypeId reverse_spec = reverse.templateSpecialization(TemplateDeclId{8610}, reverse_pack);
	require(sameStructure(forward, spec, reverse, reverse_spec));
	require(!sameStructure(forward, permuted, reverse, reverse_spec));
}

// A direct member class-template specialization identity composes the enclosing
// owner specialization, the member primary, and the member's own arguments.
// Constructor and member schemas keyed by that identity must stay separate when
// only the owner or a non-type argument differs, and must compare structurally
// when the same declaration is rebuilt in a table with opposite interning order.
// The partial-pattern environment is keyed by the same structural inputs, so
// publication order cannot merge two declarations or split one declaration.
inline void checkMemberPartialSchemaIdentityIndependence() {
	const TemplateDeclId owner_primary{8701};
	const TemplateDeclId member_primary{8702};
	const auto publish_schemas = [](CanonicalTypeTable& table, TypeId type, TypeId constructor_parameter, TypeId field_type) {
		const CanonicalRecordConstructorSpec constructor{
			.parameter_types = std::span<const TypeId>(&constructor_parameter, 1),
			.flags = CanonicalRecordFunctionFlags::None,
			.is_noexcept = false,
			.minimum_parameter_count = 1,
			.default_exception_bits = 0,
			.status = CanonicalRecordConstructorStatus::Complete,
			.declared_parameter_count = 1,
		};
		table.publishRecordConstructors(type, std::span<const CanonicalRecordConstructorSpec>(&constructor, 1));
		const TypeId member_types[] = {field_type};
		table.publishRecordSubobjectTypes(type, member_types);
		table.publishClassBaseSchema(type, {});
		const CanonicalRecordMember member{field_type, 0, 8, 0, 0, CanonicalAccess::Public, CanonicalRecordMemberFlags::None};
		table.publishRecordFieldSchema(type, std::span<const CanonicalRecordMember>(&member, 1), {});
		const CanonicalMemberFunctionSpec function{
			.name = "read",
			.signature = field_type,
			.flags = CanonicalMemberFunctionFlags::None,
			.operator_kind = 0,
			.cv_qualifier = CVQualifier::None,
			.ref_qualifier = ReferenceQualifier::None,
			.ordinal = 0,
			.conversion_target = TypeId{},
		};
		table.publishMemberFunctionSchema(type, std::span<const CanonicalMemberFunctionSpec>(&function, 1));
	};
	const auto build = [&](CanonicalTypeTable& table, bool reverse) {
		struct Identity {
			TypeId narrow_five;
			TypeId wide_five;
			TypeId narrow_seven;
		};
		if (reverse) {
			table.pointer(table.builtin(CanonicalBuiltinKind::LongLong));
			table.integralValue(table.builtin(CanonicalBuiltinKind::Int), 7);
		}
		const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId short_type = table.builtin(CanonicalBuiltinKind::Short);
		const TypeId long_long = table.builtin(CanonicalBuiltinKind::LongLong);
		const TypeId pointer = table.pointer(integer);
		const TypeId five = table.integralValue(integer, 5);
		const TypeId seven = table.integralValue(integer, 7);
		const TypeId narrow_owner = table.templateSpecialization(owner_primary, std::array<TypeId, 1>{short_type});
		const TypeId wide_owner = table.templateSpecialization(owner_primary, std::array<TypeId, 1>{long_long});
		const CanonicalTemplateArgument five_args[] = {
			CanonicalTemplateArgument::makeType(pointer), CanonicalTemplateArgument::makeValue(five)};
		const CanonicalTemplateArgument seven_args[] = {
			CanonicalTemplateArgument::makeType(pointer), CanonicalTemplateArgument::makeValue(seven)};
		Identity identity{
			table.memberTemplateSpecialization(member_primary, narrow_owner, five_args),
			table.memberTemplateSpecialization(member_primary, wide_owner, five_args),
			table.memberTemplateSpecialization(member_primary, narrow_owner, seven_args),
		};
		publish_schemas(table, identity.narrow_five, short_type, pointer);
		publish_schemas(table, identity.wide_five, long_long, integer);
		publish_schemas(table, identity.narrow_seven, integer, table.builtin(CanonicalBuiltinKind::Char));
		return identity;
	};

	CanonicalTypeTable forward;
	const auto forward_identity = build(forward, false);
	require(forward_identity.narrow_five != forward_identity.wide_five);
	require(forward_identity.narrow_five != forward_identity.narrow_seven);
	require(forward_identity.wide_five != forward_identity.narrow_seven);
	require(forward.recordConstructorParameterAt(forward_identity.narrow_five, 0, 0) ==
		forward.builtin(CanonicalBuiltinKind::Short));
	require(forward.recordConstructorParameterAt(forward_identity.wide_five, 0, 0) ==
		forward.builtin(CanonicalBuiltinKind::LongLong));
	require(forward.recordConstructorParameterAt(forward_identity.narrow_seven, 0, 0) ==
		forward.builtin(CanonicalBuiltinKind::Int));
	require(forward.recordMemberAt(forward_identity.narrow_five, 0).type == forward.pointer(forward.builtin(CanonicalBuiltinKind::Int)));
	require(forward.recordMemberAt(forward_identity.wide_five, 0).type == forward.builtin(CanonicalBuiltinKind::Int));
	require(forward.hasMemberFunctionSchema(forward_identity.narrow_five));
	require(forward.memberFunctionAt(forward_identity.narrow_five, 0).ordinal == 0);

	// Unrelated string interning must not renumber or repartition the schemas.
	const TypeId forward_narrow_five = forward_identity.narrow_five;
	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated partial schema identity spelling B");
	require(forward.memberTemplateSpecialization(member_primary,
		forward.templateSpecialization(owner_primary, std::array<TypeId, 1>{forward.builtin(CanonicalBuiltinKind::Short)}),
		std::array<CanonicalTemplateArgument, 2>{
			CanonicalTemplateArgument::makeType(forward.pointer(forward.builtin(CanonicalBuiltinKind::Int))),
			CanonicalTemplateArgument::makeValue(forward.integralValue(forward.builtin(CanonicalBuiltinKind::Int), 5))}) ==
		forward_narrow_five);
	internUnrelatedCanonicalSpelling("unrelated partial schema identity spelling A");
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	const auto reverse_identity = build(reverse, true);
	require(reverse_identity.narrow_five != reverse_identity.wide_five);
	require(reverse_identity.narrow_five != reverse_identity.narrow_seven);
	require(sameStructure(forward, forward_identity.narrow_five, reverse, reverse_identity.narrow_five));
	require(sameStructure(forward, forward_identity.wide_five, reverse, reverse_identity.wide_five));
	require(sameStructure(forward, forward_identity.narrow_seven, reverse, reverse_identity.narrow_seven));
	require(sameStructure(forward, forward.recordConstructorParameterAt(forward_identity.narrow_five, 0, 0),
		reverse, reverse.recordConstructorParameterAt(reverse_identity.narrow_five, 0, 0)));
	require(sameStructure(forward, forward.recordConstructorParameterAt(forward_identity.wide_five, 0, 0),
		reverse, reverse.recordConstructorParameterAt(reverse_identity.wide_five, 0, 0)));
	require(sameStructure(forward, forward.recordMemberAt(forward_identity.narrow_seven, 0).type,
		reverse, reverse.recordMemberAt(reverse_identity.narrow_seven, 0).type));
	require(!sameStructure(forward, forward_identity.narrow_five, reverse, reverse_identity.wide_five));
	require(!sameStructure(forward, forward_identity.narrow_five, reverse, reverse_identity.narrow_seven));
	require(reverse.recordConstructorParameterAt(reverse_identity.wide_five, 0, 0) ==
		reverse.builtin(CanonicalBuiltinKind::LongLong));

	// The published partial-pattern environment uses the same structural inputs
	// as the schema identity: two declarations that normalize to one pattern stay
	// distinct, re-publishing one declaration merges, and a different normalized
	// pattern splits.
	const StringHandle first_identity = StringTable::getOrInternStringHandle("partial-schema-declaration-first");
	const StringHandle second_identity = StringTable::getOrInternStringHandle("partial-schema-declaration-second");
	const StringHandle owner_name = StringTable::getOrInternStringHandle("PartialSchemaOwner");
	const auto check_environment_partition = [&](TemplateDeclTable& decls, TemplateDeclId primary, TypeId pattern,
		TypeId other_pattern) {
		const TemplateDeclId first = decls.publishClassPartialEnvironment(primary, pattern, first_identity);
		const TemplateDeclId second = decls.publishClassPartialEnvironment(primary, pattern, second_identity);
		require(first != second);
		require(decls.publishClassPartialEnvironment(primary, pattern, first_identity) == first);
		require(decls.publishClassPartialEnvironment(primary, other_pattern, first_identity) != first);
		require(decls.publishClassPartialEnvironment(primary, other_pattern, second_identity) != second);
	};
	TemplateDeclTable forward_decls;
	check_environment_partition(forward_decls, forward_decls.publishPrimaryClassTemplate(OwnerId{8703}, owner_name),
		forward_identity.narrow_five, forward_identity.narrow_seven);
	TemplateDeclTable reverse_decls;
	check_environment_partition(reverse_decls, reverse_decls.publishPrimaryClassTemplate(OwnerId{8703}, owner_name),
		reverse_identity.narrow_seven, reverse_identity.narrow_five);
}

// A partial-pattern argument that is a bare function type composes its return
// type and ordered parameter types. The published environment is keyed by that
// canonical identity, so two tables that intern the inputs in opposite order
// must produce the same structural identity, while a different parameter list,
// an empty list, a parameter-shaped parameter, or another primary must not
// compare equal.
inline void checkFunctionSignaturePartialPatternIdentityIndependence() {
	const TemplateDeclId primary{8711};
	const TemplateDeclId other_primary{8712};
	const StringHandle first_identity = StringTable::getOrInternStringHandle("function-pattern-declaration-first");
	const StringHandle second_identity = StringTable::getOrInternStringHandle("function-pattern-declaration-second");
	struct Patterns {
		TypeId integer;
		TypeId wide;
		TypeId empty;
		TypeId parameter;
		TypeId other_primary_integer;
	};
	const auto build = [&](CanonicalTypeTable& table, bool reverse) {
		if (reverse) {
			table.pointer(table.builtin(CanonicalBuiltinKind::Char));
			table.builtin(CanonicalBuiltinKind::UnsignedLongLong);
		}
		const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId wide = table.builtin(CanonicalBuiltinKind::Long);
		const TypeId parameter = table.templateParameter(primary, 0);
		const TypeId other_parameter = table.templateParameter(other_primary, 0);
		const auto make_function = [&table](TypeId return_type, std::span<const TypeId> parameters) {
			return table.function(return_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
				CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
		};
		const TypeId integer_parameters[] = {integer};
		const TypeId wide_parameters[] = {wide};
		const TypeId parameter_parameters[] = {parameter};
		const std::span<const TypeId> empty_parameters;
		const auto make_pattern = [&table](TemplateDeclId decl, TypeId function_type) {
			const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeType(function_type)};
			return table.templateSpecialization(decl, arguments);
		};
		return Patterns{
			make_pattern(primary, make_function(parameter, integer_parameters)),
			make_pattern(primary, make_function(parameter, wide_parameters)),
			make_pattern(primary, make_function(parameter, empty_parameters)),
			make_pattern(primary, make_function(parameter, parameter_parameters)),
			make_pattern(other_primary, make_function(other_parameter, integer_parameters)),
		};
	};
	CanonicalTypeTable forward;
	const Patterns forward_patterns = build(forward, false);
	require(forward_patterns.integer != forward_patterns.wide);
	require(forward_patterns.integer != forward_patterns.empty);
	require(forward_patterns.integer != forward_patterns.parameter);
	require(forward_patterns.integer != forward_patterns.other_primary_integer);
	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated function pattern identity spelling B");
	require(forward.templateSpecialization(primary, std::array<CanonicalTemplateArgument, 1>{
		CanonicalTemplateArgument::makeType(forward.function(forward.templateParameter(primary, 0),
			std::array<TypeId, 1>{forward.builtin(CanonicalBuiltinKind::Int)}, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}))}) ==
		forward_patterns.integer);
	internUnrelatedCanonicalSpelling("unrelated function pattern identity spelling A");
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	const Patterns reverse_patterns = build(reverse, true);
	require(sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.integer));
	require(sameStructure(forward, forward_patterns.wide, reverse, reverse_patterns.wide));
	require(sameStructure(forward, forward_patterns.empty, reverse, reverse_patterns.empty));
	require(sameStructure(forward, forward_patterns.parameter, reverse, reverse_patterns.parameter));
	require(sameStructure(forward, forward_patterns.other_primary_integer, reverse, reverse_patterns.other_primary_integer));
	// Paired negative controls: a different parameter list, an empty list, a
	// parameter-shaped parameter, or a swapped primary must not compare equal.
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.wide));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.empty));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.parameter));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.other_primary_integer));

	// The environment key uses the same structural inputs: one normalized
	// pattern and declaration identity merge, while a different parameter list
	// or primary splits, regardless of publication order.
	const auto check_environment_partition = [&](TemplateDeclTable& decls, TemplateDeclId pattern_primary,
		TypeId pattern, TypeId other_pattern, TypeId third_pattern) {
		const TemplateDeclId first = decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity);
		const TemplateDeclId second = decls.publishClassPartialEnvironment(pattern_primary, pattern, second_identity);
		require(first != second);
		require(decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity) == first);
		require(decls.publishClassPartialEnvironment(pattern_primary, other_pattern, first_identity) != first);
		require(decls.publishClassPartialEnvironment(pattern_primary, third_pattern, first_identity) != first);
	};
	const OwnerId owner{8713};
	const StringHandle name = StringTable::getOrInternStringHandle("FnPartialIdentity");
	TemplateDeclTable forward_decls;
	const TemplateDeclId forward_primary = forward_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(forward_decls, forward_primary, forward_patterns.integer, forward_patterns.wide,
		forward_patterns.empty);
	TemplateDeclTable reverse_decls;
	const TemplateDeclId reverse_primary = reverse_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(reverse_decls, reverse_primary, reverse_patterns.wide, reverse_patterns.empty,
		reverse_patterns.integer);
}

// A partial-pattern argument written as a function pointer or function
// reference composes its canonical callable with the pointer/reference wrapper.
// Two tables that intern the inputs in opposite order must produce the same
// structural identity, while a different signature, an empty parameter list, a
// parameter-shaped parameter, another primary, or a swapped callable wrapper
// must not compare equal.
inline void checkFunctionPointerPartialPatternIdentityIndependence() {
	const TemplateDeclId primary{8721};
	const TemplateDeclId other_primary{8722};
	const StringHandle first_identity = StringTable::getOrInternStringHandle("function-pointer-pattern-declaration-first");
	const StringHandle second_identity = StringTable::getOrInternStringHandle("function-pointer-pattern-declaration-second");
	struct Patterns {
		TypeId integer;
		TypeId wide;
		TypeId empty;
		TypeId parameter;
		TypeId other_primary_integer;
		TypeId reference;
	};
	const auto build = [&](CanonicalTypeTable& table, bool reverse) {
		if (reverse) {
			table.pointer(table.builtin(CanonicalBuiltinKind::Char));
			table.builtin(CanonicalBuiltinKind::UnsignedLongLong);
		}
		const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId wide = table.builtin(CanonicalBuiltinKind::Long);
		const TypeId parameter = table.templateParameter(primary, 0);
		const TypeId other_parameter = table.templateParameter(other_primary, 0);
		const auto make_function = [&table](TypeId return_type, std::span<const TypeId> parameters) {
			return table.function(return_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
				CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
		};
		const TypeId integer_parameters[] = {integer};
		const TypeId wide_parameters[] = {wide};
		const TypeId parameter_parameters[] = {parameter};
		const std::span<const TypeId> empty_parameters;
		const auto make_pattern = [&table](TemplateDeclId decl, TypeId callable) {
			const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeType(callable)};
			return table.templateSpecialization(decl, arguments);
		};
		const TypeId integer_pointer = table.pointer(make_function(parameter, integer_parameters));
		return Patterns{
			make_pattern(primary, integer_pointer),
			make_pattern(primary, table.pointer(make_function(parameter, wide_parameters))),
			make_pattern(primary, table.pointer(make_function(parameter, empty_parameters))),
			make_pattern(primary, table.pointer(make_function(parameter, parameter_parameters))),
			make_pattern(other_primary, table.pointer(make_function(other_parameter, integer_parameters))),
			// A function reference is not a function pointer identity.
			make_pattern(primary, table.reference(integer_pointer, ReferenceQualifier::LValueReference)),
		};
	};
	CanonicalTypeTable forward;
	const Patterns forward_patterns = build(forward, false);
	require(forward_patterns.integer != forward_patterns.wide);
	require(forward_patterns.integer != forward_patterns.empty);
	require(forward_patterns.integer != forward_patterns.parameter);
	require(forward_patterns.integer != forward_patterns.other_primary_integer);
	require(forward_patterns.integer != forward_patterns.reference);
	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated function pointer pattern identity spelling B");
	require(forward.templateSpecialization(primary, std::array<CanonicalTemplateArgument, 1>{
		CanonicalTemplateArgument::makeType(forward.pointer(forward.function(forward.templateParameter(primary, 0),
			std::array<TypeId, 1>{forward.builtin(CanonicalBuiltinKind::Int)}, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{})))}) ==
		forward_patterns.integer);
	internUnrelatedCanonicalSpelling("unrelated function pointer pattern identity spelling A");
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	const Patterns reverse_patterns = build(reverse, true);
	require(sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.integer));
	require(sameStructure(forward, forward_patterns.wide, reverse, reverse_patterns.wide));
	require(sameStructure(forward, forward_patterns.empty, reverse, reverse_patterns.empty));
	require(sameStructure(forward, forward_patterns.parameter, reverse, reverse_patterns.parameter));
	require(sameStructure(forward, forward_patterns.other_primary_integer, reverse, reverse_patterns.other_primary_integer));
	require(sameStructure(forward, forward_patterns.reference, reverse, reverse_patterns.reference));
	// Paired negative controls: a different parameter list, an empty list, a
	// parameter-shaped parameter, a swapped primary, or a reference instead of a
	// pointer must not compare equal.
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.wide));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.empty));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.parameter));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.other_primary_integer));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.reference));

	// The environment key uses the same structural inputs: one normalized
	// pattern and declaration identity merge, while a different signature or
	// primary splits, regardless of publication order.
	const auto check_environment_partition = [&](TemplateDeclTable& decls, TemplateDeclId pattern_primary,
		TypeId pattern, TypeId other_pattern, TypeId third_pattern) {
		const TemplateDeclId first = decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity);
		const TemplateDeclId second = decls.publishClassPartialEnvironment(pattern_primary, pattern, second_identity);
		require(first != second);
		require(decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity) == first);
		require(decls.publishClassPartialEnvironment(pattern_primary, other_pattern, first_identity) != first);
		require(decls.publishClassPartialEnvironment(pattern_primary, third_pattern, first_identity) != first);
	};
	const OwnerId owner{8723};
	const StringHandle name = StringTable::getOrInternStringHandle("FnPointerPartialIdentity");
	TemplateDeclTable forward_decls;
	const TemplateDeclId forward_primary = forward_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(forward_decls, forward_primary, forward_patterns.integer, forward_patterns.wide,
		forward_patterns.empty);
	TemplateDeclTable reverse_decls;
	const TemplateDeclId reverse_primary = reverse_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(reverse_decls, reverse_primary, reverse_patterns.wide, reverse_patterns.empty,
		reverse_patterns.integer);
}

// [temp.spec.partial]/1 keys a partial by its normalized template argument, so
// both pointer levels of `T (**)(T)` must survive normalization. [dcl.ptr]/1 and
// [dcl.fct]/1 describe the wrapper stack composed around the function type. Two
// tables that intern the inputs in opposite order must agree structurally,
// while a different signature, one pointer level, a reference wrapper, or
// another primary must partition the published environment.
inline void checkDoublePointerPartialPatternIdentityIndependence() {
	const TemplateDeclId primary{8741};
	const TemplateDeclId other_primary{8742};
	const StringHandle first_identity = StringTable::getOrInternStringHandle("double-pointer-pattern-declaration-first");
	const StringHandle second_identity = StringTable::getOrInternStringHandle("double-pointer-pattern-declaration-second");
	struct Patterns {
		TypeId integer;
		TypeId wide;
		TypeId single_pointer;
		TypeId reference;
		TypeId other_primary_integer;
	};
	const auto build = [&](CanonicalTypeTable& table, bool reverse) {
		if (reverse) {
			table.pointer(table.builtin(CanonicalBuiltinKind::Char));
			table.builtin(CanonicalBuiltinKind::UnsignedLongLong);
		}
		const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId wide = table.builtin(CanonicalBuiltinKind::Long);
		const TypeId parameter = table.templateParameter(primary, 0);
		const TypeId other_parameter = table.templateParameter(other_primary, 0);
		const auto make_function = [&table](TypeId return_type, std::span<const TypeId> parameters) {
			return table.function(return_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
				CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
		};
		const TypeId integer_parameters[] = {integer};
		const TypeId wide_parameters[] = {wide};
		const TypeId integer_function = make_function(parameter, integer_parameters);
		const auto make_pattern = [&table](TemplateDeclId decl, TypeId callable) {
			const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeType(callable)};
			return table.templateSpecialization(decl, arguments);
		};
		return Patterns{
			make_pattern(primary, table.pointer(table.pointer(integer_function))),
			make_pattern(primary, table.pointer(table.pointer(make_function(parameter, wide_parameters)))),
			make_pattern(primary, table.pointer(integer_function)),
			make_pattern(primary, table.reference(integer_function, ReferenceQualifier::LValueReference)),
			make_pattern(other_primary, table.pointer(table.pointer(make_function(other_parameter, integer_parameters)))),
		};
	};
	CanonicalTypeTable forward;
	const Patterns forward_patterns = build(forward, false);
	const TypeId forward_argument = forward.templateArgumentType(forward.templateSpecializationArguments(forward_patterns.integer));
	const CanonicalTypeNode outer_pointer = forward.node(forward_argument);
	require(outer_pointer.kind == CanonicalTypeKind::Pointer);
	const CanonicalTypeNode inner_pointer = forward.node(outer_pointer.child);
	require(inner_pointer.kind == CanonicalTypeKind::Pointer);
	require(forward.node(inner_pointer.child).kind == CanonicalTypeKind::Function);
	// [dcl.declarators]/1: the ordered pointer spine and the flat pointer levels
	// are two spellings of the same shape; shaping must apply one of them only.
	TypeSpecifierNode declarator_syntax;
	declarator_syntax.set_ordered_declarator({DeclaratorComponent::pointer(CVQualifier::None), DeclaratorComponent::pointer(CVQualifier::None)});
	const TypeId shaped = shapeCanonicalCallableType(forward,
		forward.function(forward.templateParameter(primary, 0), std::array<TypeId, 1>{forward.builtin(CanonicalBuiltinKind::Int)},
			false, CVQualifier::None, ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}),
		declarator_syntax, CanonicalTypeImportContext::Exact);
	require(shaped == forward_argument);
	const size_t forward_count = forward.size();
	internUnrelatedCanonicalSpelling("unrelated double pointer pattern spelling B");
	require(forward.templateSpecialization(primary, std::array<CanonicalTemplateArgument, 1>{
		CanonicalTemplateArgument::makeType(forward.pointer(forward.pointer(forward.function(forward.templateParameter(primary, 0),
			std::array<TypeId, 1>{forward.builtin(CanonicalBuiltinKind::Int)}, false, CVQualifier::None,
			ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{}))))}) ==
		forward_patterns.integer);
	internUnrelatedCanonicalSpelling("unrelated double pointer pattern spelling A");
	require(forward.size() == forward_count);

	CanonicalTypeTable reverse;
	const Patterns reverse_patterns = build(reverse, true);
	require(sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.integer));
	require(sameStructure(forward, forward_patterns.wide, reverse, reverse_patterns.wide));
	require(sameStructure(forward, forward_patterns.single_pointer, reverse, reverse_patterns.single_pointer));
	require(sameStructure(forward, forward_patterns.reference, reverse, reverse_patterns.reference));
	require(sameStructure(forward, forward_patterns.other_primary_integer, reverse, reverse_patterns.other_primary_integer));
	// Paired negative controls: a different signature, a dropped pointer level,
	// a reference wrapper, or a swapped primary must not compare equal.
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.wide));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.single_pointer));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.reference));
	require(!sameStructure(forward, forward_patterns.integer, reverse, reverse_patterns.other_primary_integer));

	const auto check_environment_partition = [&](TemplateDeclTable& decls, TemplateDeclId pattern_primary,
		TypeId pattern, TypeId other_pattern, TypeId third_pattern) {
		const TemplateDeclId first = decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity);
		const TemplateDeclId second = decls.publishClassPartialEnvironment(pattern_primary, pattern, second_identity);
		require(first != second);
		require(decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity) == first);
		require(decls.publishClassPartialEnvironment(pattern_primary, other_pattern, first_identity) != first);
		require(decls.publishClassPartialEnvironment(pattern_primary, third_pattern, first_identity) != first);
	};
	const OwnerId owner{8743};
	const StringHandle name = StringTable::getOrInternStringHandle("DoublePointerPartialIdentity");
	TemplateDeclTable forward_decls;
	const TemplateDeclId forward_primary = forward_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(forward_decls, forward_primary, forward_patterns.integer, forward_patterns.wide,
		forward_patterns.single_pointer);
	TemplateDeclTable reverse_decls;
	const TemplateDeclId reverse_primary = reverse_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(reverse_decls, reverse_primary, reverse_patterns.wide, reverse_patterns.single_pointer,
		reverse_patterns.integer);
}

// [temp.variadic]/1: a trailing signature pack is one alpha-normalized
// placeholder in the canonical Function node; a declaration-identity marker
// separates it from a fixed single-parameter pattern. The structural shape must
// stay independent of interning order and split from other arities and owners.
inline void checkSignaturePackPartialPatternIdentityIndependence() {
	const TemplateDeclId primary{8761};
	const TemplateDeclId other_primary{8762};
	const StringHandle first_identity = StringTable::getOrInternStringHandle("signature-pack-pattern-first");
	const StringHandle second_identity = StringTable::getOrInternStringHandle("signature-pack-pattern-second");
	struct Patterns {
		TypeId pack;
		TypeId empty;
		TypeId fixed;
		TypeId other_primary_pack;
	};
	const auto build = [&](CanonicalTypeTable& table, bool reverse) {
		if (reverse) {
			table.builtin(CanonicalBuiltinKind::Char);
		}
		const TypeId parameter = table.templateParameter(primary, 0);
		const TypeId other_parameter = table.templateParameter(other_primary, 0);
		const auto make_function = [&table](TypeId return_type, std::span<const TypeId> parameters) {
			return table.function(return_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
				CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
		};
		const TypeId one_parameter[] = {parameter};
		const std::span<const TypeId> empty_parameters;
		const auto make_pattern = [&table](TemplateDeclId decl, TypeId callable) {
			const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeType(callable)};
			return table.templateSpecialization(decl, arguments);
		};
		return Patterns{
			make_pattern(primary, make_function(parameter, one_parameter)),
			make_pattern(primary, make_function(parameter, empty_parameters)),
			make_pattern(primary, make_function(parameter, std::array<TypeId, 2>{parameter, parameter})),
			make_pattern(other_primary, make_function(other_parameter, one_parameter)),
		};
	};
	CanonicalTypeTable forward;
	const Patterns forward_patterns = build(forward, false);
	require(forward_patterns.pack != forward_patterns.empty);
	require(forward_patterns.pack != forward_patterns.fixed);
	require(forward_patterns.pack != forward_patterns.other_primary_pack);
	CanonicalTypeTable reverse;
	const Patterns reverse_patterns = build(reverse, true);
	require(sameStructure(forward, forward_patterns.pack, reverse, reverse_patterns.pack));
	require(!sameStructure(forward, forward_patterns.pack, reverse, reverse_patterns.empty));
	require(!sameStructure(forward, forward_patterns.pack, reverse, reverse_patterns.fixed));
	require(!sameStructure(forward, forward_patterns.pack, reverse, reverse_patterns.other_primary_pack));
	const auto check_environment_partition = [&](TemplateDeclTable& decls, TemplateDeclId pattern_primary,
		TypeId pattern, TypeId other_pattern, TypeId third_pattern) {
		const TemplateDeclId first = decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity);
		const TemplateDeclId second = decls.publishClassPartialEnvironment(pattern_primary, pattern, second_identity);
		require(first != second);
		require(decls.publishClassPartialEnvironment(pattern_primary, pattern, first_identity) == first);
		require(decls.publishClassPartialEnvironment(pattern_primary, other_pattern, first_identity) != first);
		require(decls.publishClassPartialEnvironment(pattern_primary, third_pattern, first_identity) != first);
	};
	const OwnerId owner{8763};
	const StringHandle name = StringTable::getOrInternStringHandle("SignaturePackPartialIdentity");
	TemplateDeclTable forward_decls;
	const TemplateDeclId forward_primary = forward_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(forward_decls, forward_primary, forward_patterns.pack, forward_patterns.empty,
		forward_patterns.fixed);
	TemplateDeclTable reverse_decls;
	const TemplateDeclId reverse_primary = reverse_decls.publishPrimaryClassTemplate(owner, name);
	check_environment_partition(reverse_decls, reverse_primary, reverse_patterns.pack, reverse_patterns.fixed,
		reverse_patterns.empty);
}

// The shape is a by-value query result; a future field must not quietly re-add
// padding to it.
static_assert(sizeof(CanonicalPointerShape) == 10, "pointer shape stays a packed by-value result");

// Presence, depth, and per-level cv must be one answer. A callable pointer is a
// canonical Pointer, a member pointer is not, and the level order is
// outermost-first so a consumer cannot pair a depth from one walk with cv from
// another.
inline void checkPointerShapeQuery() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId const_inner = table.pointer(table.qualify(integer, CVQualifier::Const));
	const TypeId outer = table.qualify(table.pointer(const_inner), CVQualifier::Volatile);
	const CanonicalPointerShape plain = canonicalPointerShape(table, outer);
	require(plain.isPointer());
	require(plain.pointee == CanonicalPointeeKind::None);
	require(plain.depth == 2);
	require(plain.levels[0] == CVQualifier::Volatile);
	require(plain.levels[1] == CVQualifier::None);
	require(canonicalIsPointer(table, outer));
	require(canonicalPointerLevelCount(table, outer) == 2);
	CVQualifier levels[2] = {};
	canonicalCopyPointerLevelCvs(table, outer, std::span<CVQualifier>(levels, 2));
	require(levels[0] == plain.levels[0] && levels[1] == plain.levels[1]);
	const CanonicalPointerShape referenced = canonicalPointerShape(table, table.reference(outer, ReferenceQualifier::RValueReference));
	require(referenced.depth == 2 && referenced.levels[0] == CVQualifier::Volatile);

	const TypeId callable = table.function(integer, std::span<const TypeId>(), false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const CanonicalPointerShape function_pointer = canonicalPointerShape(table, table.pointer(callable));
	require(function_pointer.isPointer());
	require(function_pointer.pointee == CanonicalPointeeKind::Function);
	require(function_pointer.depth == 1);
	const CanonicalPointerShape nested_function_pointer = canonicalPointerShape(table, table.pointer(table.pointer(callable)));
	require(nested_function_pointer.depth == 2 && nested_function_pointer.pointee == CanonicalPointeeKind::Function);
	require(!canonicalPointerShape(table, callable).isPointer());

	const TypeId owner = table.record(EntityId{40011});
	const TypeId member = table.memberObjectPointer(owner, integer);
	const CanonicalPointerShape member_shape = canonicalPointerShape(table, table.qualify(member, CVQualifier::Const));
	require(!member_shape.isPointer());
	require(member_shape.pointee == CanonicalPointeeKind::MemberPointer);
	require(member_shape.depth == 0);
	const CanonicalPointerShape member_pointer_pointer = canonicalPointerShape(table, table.pointer(member));
	require(member_pointer_pointer.isPointer());
	require(member_pointer_pointer.pointee == CanonicalPointeeKind::MemberPointer);
	require(member_pointer_pointer.depth == 1);
	const CanonicalPointerShape member_function_pointer = canonicalPointerShape(table, table.memberFunctionPointer(owner, callable));
	require(!member_function_pointer.isPointer());
	require(member_function_pointer.pointee == CanonicalPointeeKind::MemberPointer);
}

// [expr.const.cast]/3 needs two object-pointer types to be similar with
// identical pointer, array, and member-pointer components. cv may differ at
// every level and references are stripped, but pointer depth, array extents,
// and the terminal type still distinguish. Identity equality keeps per-level
// cv distinct, so it cannot answer this question.
inline void checkCvSimilarityQuery() {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId long_integer = table.builtin(CanonicalBuiltinKind::Long);
	const TypeId const_integer = table.qualify(integer, CVQualifier::Const);
	const TypeId plain_pointer = table.pointer(integer);
	const TypeId const_pointee = table.pointer(const_integer);
	require(canonicalSimilarIgnoringCvAndRef(table, const_integer, integer));
	require(canonicalSimilarIgnoringCvAndRef(table, const_pointee, plain_pointer));
	require(canonicalSimilarIgnoringCvAndRef(table, plain_pointer, const_pointee));

	const TypeId inner_const_pointer = table.pointer(table.qualify(plain_pointer, CVQualifier::Const));
	const TypeId plain_pointer_pointer = table.pointer(plain_pointer);
	const TypeId const_deep_pointee = table.pointer(table.pointer(const_integer));
	require(canonicalSimilarIgnoringCvAndRef(table, inner_const_pointer, plain_pointer_pointer));
	require(canonicalSimilarIgnoringCvAndRef(table, plain_pointer_pointer, inner_const_pointer));
	require(canonicalSimilarIgnoringCvAndRef(table, plain_pointer_pointer, const_deep_pointee));
	require(canonicalSimilarIgnoringCvAndRef(table, const_deep_pointee, plain_pointer_pointer));
	// Similarity ignores the cv that the top-level rule and identity keep distinct.
	require(!canonicalSameIgnoringTopCvRef(table, inner_const_pointer, plain_pointer_pointer));
	require(!canonicalSameIgnoringTopCvRef(table, plain_pointer_pointer, const_deep_pointee));
	require(inner_const_pointer != plain_pointer_pointer);
	require(!canonicalSimilarIgnoringCvAndRef(table, plain_pointer, plain_pointer_pointer));
	require(!canonicalSimilarIgnoringCvAndRef(table, integer, plain_pointer));
	require(!canonicalSimilarIgnoringCvAndRef(table, plain_pointer, table.pointer(long_integer)));

	require(canonicalSimilarIgnoringCvAndRef(table, table.reference(integer, ReferenceQualifier::LValueReference), integer));
	require(canonicalSimilarIgnoringCvAndRef(table, integer, table.reference(const_integer, ReferenceQualifier::RValueReference)));
	require(!canonicalSimilarIgnoringCvAndRef(table, table.reference(integer, ReferenceQualifier::LValueReference), long_integer));

	const TypeId array_three = table.array(integer, 3);
	require(canonicalSimilarIgnoringCvAndRef(table, array_three, table.array(integer, 3)));
	require(canonicalSimilarIgnoringCvAndRef(table, array_three, table.array(const_integer, 3)));
	require(!canonicalSimilarIgnoringCvAndRef(table, array_three, table.array(integer, 4)));
	const TypeId unknown_array = table.arrayOfUnknownBound(integer);
	require(canonicalSimilarIgnoringCvAndRef(table, unknown_array, table.arrayOfUnknownBound(const_integer)));
	require(!canonicalSimilarIgnoringCvAndRef(table, unknown_array, array_three));
	require(!canonicalSimilarIgnoringCvAndRef(table, array_three, unknown_array));

	const TypeId record = table.record(EntityId{40021});
	const TypeId other_record = table.record(EntityId{40022});
	require(canonicalSimilarIgnoringCvAndRef(table, table.qualify(record, CVQualifier::Volatile), record));
	require(!canonicalSimilarIgnoringCvAndRef(table, record, other_record));
	require(!canonicalSimilarIgnoringCvAndRef(table, record, integer));

	const TypeId member = table.memberObjectPointer(record, integer);
	require(canonicalSimilarIgnoringCvAndRef(table, table.qualify(member, CVQualifier::Const), member));
	require(canonicalSimilarIgnoringCvAndRef(table, member, table.memberObjectPointer(record, const_integer)));
	require(!canonicalSimilarIgnoringCvAndRef(table, member, table.memberObjectPointer(other_record, integer)));
}

#if defined(FLASHCPP_CANONICAL_IDENTITY_HARNESS)
// The harness gives this translation unit a tiny type registry so an alias can
// otherwise be expressed at all; records use the normal set_type_entity hook.
TypeIndex publishHarnessAliasType(const TypeSpecifierNode& underlying);

// The identity pair is one structure: outer-first declarator components plus a
// terminal identity. Equality merges a typedef with its underlying type without
// reading a flat projection, and the hash must agree with it in both
// directions, so an opaque TypeIndex fallback or a collision cannot pass.
inline void checkCanonicalTypeIdentityHash() {
	auto same_identity = [](const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
		require(FlashCpp::canonicalTypeSpecifierIdentityEqual(lhs, rhs));
		require(FlashCpp::canonicalTypeSpecifierIdentityHash(lhs) == FlashCpp::canonicalTypeSpecifierIdentityHash(rhs));
	};
	auto distinct_identity = [](const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
		require(!FlashCpp::canonicalTypeSpecifierIdentityEqual(lhs, rhs));
		require(FlashCpp::canonicalTypeSpecifierIdentityHash(lhs) != FlashCpp::canonicalTypeSpecifierIdentityHash(rhs));
	};
	auto require_identity_error = [](auto&& operation) {
		bool threw = false;
		try {
			operation();
		} catch (const InternalError&) {
			threw = true;
		}
		require(threw);
	};

	TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode wider_integer(TypeCategory::Long, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	same_identity(integer, integer);
	distinct_identity(integer, wider_integer);

	// A typedef and its underlying builtin are one identity even though their
	// TypeIndex values differ.
	TypeSpecifierNode integer_alias(publishHarnessAliasType(integer), TypeQualifier::None, 32, Token{}, CVQualifier::None);
	same_identity(integer, integer_alias);
	TypeSpecifierNode pointer_alias_underlying = integer;
	pointer_alias_underlying.add_pointer_level(CVQualifier::Const);
	pointer_alias_underlying.add_pointer_level();
	TypeSpecifierNode pointer_alias(
		publishHarnessAliasType(pointer_alias_underlying), TypeQualifier::None, 64, Token{}, CVQualifier::None);
	same_identity(pointer_alias_underlying, pointer_alias);

	TypeSpecifierNode record(TypeCategory::Struct, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	record.set_type_entity(EntityId{5201});
	TypeSpecifierNode record_alias = record;
	record_alias.set_type_index(TypeIndex{9001, TypeCategory::Struct});
	TypeSpecifierNode registry_record_alias(
		publishHarnessAliasType(record), TypeQualifier::None, 64, Token{}, CVQualifier::None);
	registry_record_alias.set_type_entity(EntityId{5201});
	TypeSpecifierNode other_record(TypeCategory::Struct, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	other_record.set_type_entity(EntityId{5202});
	same_identity(record, record_alias);
	same_identity(record, registry_record_alias);
	distinct_identity(record, other_record);

	// using ConstInt = const int; using ConstIntPointer = ConstInt*;
	TypeSpecifierNode const_integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	const TypeIndex const_integer_alias = publishHarnessAliasType(const_integer);
	TypeSpecifierNode pointer_to_const_integer_alias(const_integer_alias, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	pointer_to_const_integer_alias.add_pointer_level();
	TypeSpecifierNode const_integer_pointer(
		publishHarnessAliasType(pointer_to_const_integer_alias), TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode expected_pointer_to_const_integer = const_integer;
	expected_pointer_to_const_integer.add_pointer_level();
	same_identity(expected_pointer_to_const_integer, const_integer_pointer);

	// using IntPointer = int*; using ConstIntPointer = const IntPointer;
	TypeSpecifierNode int_pointer_unqualified = integer;
	int_pointer_unqualified.add_pointer_level();
	const TypeIndex int_pointer_alias = publishHarnessAliasType(int_pointer_unqualified);
	TypeSpecifierNode const_alias_of_pointer(int_pointer_alias, TypeQualifier::None, 64, Token{}, CVQualifier::Const);
	TypeSpecifierNode alias_of_const_pointer(
		publishHarnessAliasType(const_alias_of_pointer), TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode expected_const_pointer = integer;
	expected_const_pointer.add_pointer_level(CVQualifier::Const);
	same_identity(expected_const_pointer, alias_of_const_pointer);

	TypeSpecifierNode int_pointer = integer;
	int_pointer.add_pointer_level();
	TypeSpecifierNode int_const_inner_pointer = integer;
	int_const_inner_pointer.add_pointer_level(CVQualifier::Const);
	TypeSpecifierNode int_const_pointer_type = int_const_inner_pointer;
	int_const_pointer_type.add_pointer_level();
	TypeSpecifierNode int_double_pointer = int_pointer;
	int_double_pointer.add_pointer_level();
	distinct_identity(int_double_pointer, int_const_pointer_type);

	TypeSpecifierNode array_two = integer;
	array_two.set_array(true, 2);
	TypeSpecifierNode array_three = integer;
	array_three.set_array(true, 3);
	TypeSpecifierNode array_unknown_bound = integer;
	array_unknown_bound.set_array(true, std::nullopt);
	same_identity(array_two, array_two);
	distinct_identity(array_two, array_three);
	distinct_identity(array_two, array_unknown_bound);

	// The constexpr object-reuse decision only has a TypeIndex row to identify an
	// object's type, so the alias row itself must carry the same distinctions as
	// the underlying specifier: an alias of one type still shares, while a
	// per-level pointer cv difference and a different array extent stay apart.
	const TypeIndex integer_alias_row = publishHarnessAliasType(integer);
	TypeSpecifierNode integer_alias_row_type(integer_alias_row, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	same_identity(integer, integer_alias_row_type);
	const TypeIndex int_double_pointer_alias = publishHarnessAliasType(int_double_pointer);
	const TypeIndex int_const_pointer_alias = publishHarnessAliasType(int_const_pointer_type);
	TypeSpecifierNode int_double_pointer_alias_type(int_double_pointer_alias, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode int_const_pointer_alias_type(int_const_pointer_alias, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	distinct_identity(int_double_pointer_alias_type, int_const_pointer_alias_type);
	const TypeIndex array_two_alias = publishHarnessAliasType(array_two);
	const TypeIndex array_three_alias = publishHarnessAliasType(array_three);
	TypeSpecifierNode array_two_alias_type(array_two_alias, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode array_three_alias_type(array_three_alias, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	distinct_identity(array_two_alias_type, array_three_alias_type);

	FunctionSignature int_returning_signature;
	int_returning_signature.return_type_index = nativeTypeIndex(TypeCategory::Int);
	FunctionSignature long_returning_signature;
	long_returning_signature.return_type_index = nativeTypeIndex(TypeCategory::Long);
	TypeSpecifierNode int_returning_function(TypeCategory::Function, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	int_returning_function.set_function_signature(int_returning_signature);
	TypeSpecifierNode long_returning_function(TypeCategory::Function, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	long_returning_function.set_function_signature(long_returning_signature);
	distinct_identity(int_returning_function, long_returning_function);

	// A callable terminal without any signature has no identity; the categor-
	// only TypeIndex fallback would merge different function types.
	TypeSpecifierNode signatureless_function(TypeCategory::Function, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	require_identity_error([&] { (void)FlashCpp::canonicalTypeSpecifierIdentityHash(signatureless_function); });

	// Member pointers without a published owner discriminate by class name;
	// an ownerless pointer with no name fails closed instead of merging.
	TypeSpecifierNode member_owner_a(TypeCategory::MemberObjectPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	member_owner_a.set_member_class_name(StringTable::getOrInternStringHandle("OwnerA"));
	TypeSpecifierNode member_owner_b(TypeCategory::MemberObjectPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	member_owner_b.set_member_class_name(StringTable::getOrInternStringHandle("OwnerB"));
	distinct_identity(member_owner_a, member_owner_b);
	TypeSpecifierNode ownerless_member(TypeCategory::MemberObjectPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	require_identity_error([&] { (void)FlashCpp::canonicalTypeSpecifierIdentityHash(ownerless_member); });

	// An invalid TypeIndex is not identity for unresolved names: the spelling
	// discriminates, and a nameless unresolved name fails closed.
	TypeSpecifierNode unresolved_a(TypeCategory::UserDefined, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("MissingA"), 0, 0, 0), CVQualifier::None);
	TypeSpecifierNode unresolved_b(TypeCategory::UserDefined, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("MissingB"), 0, 0, 0), CVQualifier::None);
	TypeSpecifierNode unresolved_a_copy(TypeCategory::UserDefined, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("MissingA"), 0, 0, 0), CVQualifier::None);
	same_identity(unresolved_a, unresolved_a_copy);
	distinct_identity(unresolved_a, unresolved_b);
	TypeSpecifierNode nameless_user_defined(TypeCategory::UserDefined, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	require_identity_error([&] { (void)FlashCpp::canonicalTypeSpecifierIdentityHash(nameless_user_defined); });

	// Placeholder categories with an invalid TypeIndex use the same spelling
	// discriminator, so different unresolved autos do not silently merge.
	TypeSpecifierNode auto_a(TypeCategory::Auto, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("auto_a"), 0, 0, 0), CVQualifier::None);
	TypeSpecifierNode auto_b(TypeCategory::Auto, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("auto_b"), 0, 0, 0), CVQualifier::None);
	TypeSpecifierNode auto_a_copy(TypeCategory::Auto, TypeQualifier::None, 0,
		Token(Token::Type::Identifier, std::string_view("auto_a"), 0, 0, 0), CVQualifier::None);
	same_identity(auto_a, auto_a_copy);
	distinct_identity(auto_a, auto_b);
	TypeSpecifierNode nameless_auto(TypeCategory::Auto, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	require_identity_error([&] { (void)FlashCpp::canonicalTypeSpecifierIdentityHash(nameless_auto); });
}

// The publication-independent similarity path applies the same rule to shapes
// the canonical table leaves UnmigratedNominal: cv at every level and references
// are ignored, while pointer depth, array extents, and the terminal stay exact.
// Identity equality keeps each cv difference distinct, so it cannot stand in.
inline void checkTypeSpecifierSimilarityQuery() {
	auto similar = [](const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
		return FlashCpp::canonicalTypeSpecifierSimilarIgnoringCvAndRef(lhs, rhs);
	};
	TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode const_integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	TypeSpecifierNode long_integer(TypeCategory::Long, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	require(similar(integer, const_integer));

	TypeSpecifierNode int_pointer = integer;
	int_pointer.add_pointer_level();
	TypeSpecifierNode const_pointer = const_integer;
	const_pointer.add_pointer_level();
	require(similar(int_pointer, const_pointer));
	require(similar(const_pointer, int_pointer));
	require(!FlashCpp::canonicalTypeSpecifierIdentityEqual(int_pointer, const_pointer));

	TypeSpecifierNode int_pointer_pointer = integer;
	int_pointer_pointer.add_pointer_level();
	int_pointer_pointer.add_pointer_level();
	TypeSpecifierNode inner_const_pointer_pointer = integer;
	inner_const_pointer_pointer.add_pointer_level();
	inner_const_pointer_pointer.add_pointer_level(CVQualifier::Const);
	TypeSpecifierNode deep_const_pointer_pointer = const_integer;
	deep_const_pointer_pointer.add_pointer_level();
	deep_const_pointer_pointer.add_pointer_level();
	require(similar(int_pointer_pointer, inner_const_pointer_pointer));
	require(similar(int_pointer_pointer, deep_const_pointer_pointer));
	require(!FlashCpp::canonicalTypeSpecifierIdentityEqual(int_pointer_pointer, inner_const_pointer_pointer));
	require(!similar(int_pointer, int_pointer_pointer));
	require(!similar(int_pointer_pointer, int_pointer));

	TypeSpecifierNode int_reference = integer;
	int_reference.set_reference_qualifier(ReferenceQualifier::LValueReference);
	require(similar(int_reference, integer));
	require(similar(int_reference, const_integer));
	require(!similar(int_reference, long_integer));

	TypeSpecifierNode int_array_three = integer;
	int_array_three.add_array_dimension(3);
	TypeSpecifierNode const_int_array_three = const_integer;
	const_int_array_three.add_array_dimension(3);
	TypeSpecifierNode int_array_four = integer;
	int_array_four.add_array_dimension(4);
	require(similar(int_array_three, const_int_array_three));
	require(!similar(int_array_three, int_array_four));

	TypeSpecifierNode record(TypeCategory::Struct, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	record.set_type_entity(EntityId{5301});
	TypeSpecifierNode other_record(TypeCategory::Struct, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	other_record.set_type_entity(EntityId{5302});
	require(!similar(record, other_record));
	require(!similar(record, integer));
}
#endif

inline int run() {
#if defined(FLASHCPP_CANONICAL_IDENTITY_HARNESS)
	checkCanonicalTypeIdentityHash();
	checkTypeSpecifierSimilarityQuery();
#endif
	checkPointerShapeQuery();
	checkCvSimilarityQuery();
	checkFunctionIdentityIndependence();
	checkDeclaratorIdentityIndependence();
	checkRecordSpecializationIdentityIndependence();
	checkDependentIdentityIndependence();
	checkMemberAliasOwnerIdentityIndependence();
	checkConcretePackSpecArgIdentityIndependence();
	checkMemberPartialSchemaIdentityIndependence();
	checkFunctionSignaturePartialPatternIdentityIndependence();
	checkFunctionPointerPartialPatternIdentityIndependence();
	checkDoublePointerPartialPatternIdentityIndependence();
	checkSignaturePackPartialPatternIdentityIndependence();
	checkSpecializationFieldSchemas();
	checkNestedFieldSchemas();
	checkRecordMemberProperties();
	checkMemberFunctionSignatureTransactions();
	checkFunctionPointerValues();
	checkStructuralPointerIdentities();
	checkUnionValues();
	checkStructuralEnumScalars();
	checkStructuralBitfieldScalars();
	checkFloatingValues();
	checkNullPointerValues();
	checkTemplateSpecializationStructuralValues();
	checkMemberObjectPointerValues();
	checkMemberFunctionPointerValues();
	checkStructuralArrayValues();
	{
		CanonicalTypeTable table;
		const TypeId record = table.record(EntityId{40001});
		const TypeId other_record = table.record(EntityId{40002});
		const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
		const TypeId one = table.integralValue(integer, 1);
		const TypeId two = table.integralValue(integer, 2);
		const TypeId values[] = {one, two};
		const TypeId reversed[] = {two, one};
		const TypeId structural = table.structuralValue(record, values, 0);
		require(structural == table.structuralValue(record, values, 0));
		require(structural != table.structuralValue(record, reversed, 0));
		require(structural != table.structuralValue(other_record, values, 0));
		const TypeId fewer[] = {one};
		require(structural != table.structuralValue(record, fewer, 0));
		const TypeId unsigned_values[] = {table.integralValue(table.builtin(CanonicalBuiltinKind::UnsignedInt), 1), two};
		require(structural != table.structuralValue(record, unsigned_values, 0));
		const TypeId children[] = {structural, structural};
		const TypeId nested = table.structuralValue(other_record, children, 1);
		require(nested != table.structuralValue(other_record, children, 0));
		require(nested != table.structuralValue(other_record, children, 2));
		require(unpackStructuralValueBaseCount(table.node(nested).array_extent) == 1);
		TypeId subobjects = unpackStructuralValueSubobjects(table.node(nested).array_extent);
		require(table.templateArgumentValue(subobjects) == structural);
		subobjects = table.templateArgumentNext(subobjects);
		require(table.templateArgumentValue(subobjects) == structural);
		require(!table.templateArgumentNext(subobjects));
		bool invalid_bases = false;
		try { (void)table.structuralValue(record, values, 1); } catch (const InternalError&) { invalid_bases = true; }
		require(invalid_bases);
		bool invalid_count = false;
		try { (void)table.structuralValue(other_record, children, 3); } catch (const InternalError&) { invalid_count = true; }
		require(invalid_count);
		const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(structural)};
		const TypeId specialization = table.templateSpecialization(TemplateDeclId{40003}, arguments);
		require(table.templateArgumentValue(table.templateSpecializationArguments(specialization)) == structural);
		const TypeId parameter = table.templateParameter(TemplateDeclId{40004}, 0);
		const CanonicalTemplateArgument mixed[] = {CanonicalTemplateArgument::makeType(parameter), arguments[0]};
		const TypeId pattern = table.templateSpecialization(TemplateDeclId{40003}, mixed);
		const CanonicalTemplateArgument concrete[] = {CanonicalTemplateArgument::makeType(integer), arguments[0]};
		const TypeId substitution[] = {integer};
		require(table.substitute(pattern, TemplateDeclId{40004}, substitution) == table.templateSpecialization(TemplateDeclId{40003}, concrete));
		bool rejected = false;
		try { (void)table.pointer(structural); } catch (const InternalError&) { rejected = true; }
		require(rejected);
	}
	checkDependentNames();
	checkSubstitution();
	checkCallableSubstitution();
	checkDependentTipResolve();
	checkNttpSpecArgs();
	checkConcretePackSpecArgs();
	checkAliasRedirection();
	checkNestedAliasGraph();
	checkMemberAliasOwnerEnvironment();
	checkMemberAliasOwnerNestedTarget();
	checkMemberAliasOwnerEnvironmentFailClosed();
	checkMemberAliasUseAutoRedirect();
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
	const TypeId nested_a = table.nestedRecord(spec_a, EntityId{51});
	const TypeId nested_a_same = table.nestedRecord(spec_a, EntityId{51});
	const TypeId nested_a_other = table.nestedRecord(spec_a, EntityId{52});
	require(nested_a == nested_a_same);
	require(nested_a != nested_a_other);
	require(nested_a != table.nestedRecord(spec_other, EntityId{51}));
	require(table.nestedRecordOwner(nested_a) == spec_a);
	require(table.nestedRecordEntity(nested_a) == EntityId{51});
	require(table.nestedRecord(table.qualify(spec_a, CVQualifier::Const), EntityId{51}) == nested_a);
	rejects([&] { table.nestedRecord(owner_a, EntityId{51}); });
	rejects([&] { table.nestedRecord(tmpl_a, EntityId{51}); });
	const size_t nested_rollback_size = table.size();
	{
		CanonicalTypeTransaction nested_transaction(table);
		table.nestedRecord(nested_a, EntityId{54});
		nested_transaction.rollback();
	}
	require(table.size() == nested_rollback_size);
	const TypeId nested_reordered = reordered.nestedRecord(reordered_spec, EntityId{51});
	require(sameStructure(table, nested_a, reordered, nested_reordered));
	const TemplateDeclId nested_env{9100};
	const TypeId nested_param = table.templateParameter(nested_env, 0);
	const TypeId dependent_owner_args[] = {nested_param};
	const TypeId dependent_owner = table.templateSpecialization(TemplateDeclId{9101}, dependent_owner_args);
	const TypeId dependent_nested = table.nestedRecord(dependent_owner, EntityId{53});
	const TypeId nested_bindings[] = {integer};
	const TypeId expected_nested_owner_args[] = {integer};
	const TypeId expected_nested = table.nestedRecord(table.templateSpecialization(TemplateDeclId{9101}, expected_nested_owner_args), EntityId{53});
	require(table.hasDependentTypeComponents(dependent_nested));
	require(table.dependsOnlyOnTemplateParameters(dependent_nested, nested_env));
	require(table.substitute(dependent_nested, nested_env, nested_bindings) == expected_nested);
	const TemplateDeclId nested_alias{9102};
	const TypeId alias_nested_args[] = {table.templateParameter(nested_alias, 0)};
	const TypeId alias_nested_owner = table.templateSpecialization(TemplateDeclId{9101}, alias_nested_args);
	const TypeId alias_nested_target = table.nestedRecord(alias_nested_owner, EntityId{55});
	const CanonicalTemplateArgKind nested_alias_kind[] = {CanonicalTemplateArgKind::Type};
	table.publishAliasTemplateTarget(nested_alias, alias_nested_target, nested_alias_kind);
	const TypeId alias_nested_input[] = {integer};
	const TypeId alias_nested_expected_owner_args[] = {integer};
	const TypeId alias_nested_expected = table.nestedRecord(
		table.templateSpecialization(TemplateDeclId{9101}, alias_nested_expected_owner_args), EntityId{55});
	require(table.substitute(table.aliasTemplateSpecialization(nested_alias, alias_nested_input), nested_alias,
		alias_nested_input) == alias_nested_expected);
	TypeId deep_nested = dependent_owner;
	for (uint32_t depth = 0; depth < 4096; ++depth) {
		deep_nested = table.nestedRecord(deep_nested, EntityId{1000 + depth});
	}
	const TypeId deep_substituted = table.substitute(deep_nested, nested_env, nested_bindings);
	require(!table.hasDependentTypeComponents(deep_substituted));
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
