#pragma once

#include <array>
#include <cstdio>
#include <source_location>
#include <type_traits>
#include <vector>

#include "CanonicalTypeAdapter.h"

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
		if (a.kind == CanonicalTypeKind::Record || a.kind == CanonicalTypeKind::Enum) {
			return a.array_extent == b.array_extent;
		}
		if (a.kind == CanonicalTypeKind::Function) {
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
		ReferenceQualifier::None, false)));

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
		ReferenceQualifier::LValueReference, false));
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
		ReferenceQualifier::None, true)));
	require(imported_noexcept.type != imported_function.type);
	signature.is_noexcept = false;

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
	TypeSpecifierNode published_enum_array(TypeCategory::Enum, TypeQualifier::None, 16, Token{},
		CVQualifier::None);
	published_enum_array.set_type_entity(EntityId{5});
	const std::array<size_t, 1> enum_array_dimensions{2};
	published_enum_array.set_array_dimensions(enum_array_dimensions);
	const auto enum_array_before = table.size();
	require(importCanonicalType(table, published_enum_array).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);
	require(table.size() == enum_array_before);
}

inline int run() {
	checkTransactions();
	checkAdapter();
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
		std::span<const TypeId>{}, false, CVQualifier::None, ReferenceQualifier::None, false);
	const TypeId int_param[] = {integer};
	const auto int_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false);
	const auto variadic_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, true,
		CVQualifier::None, ReferenceQualifier::None, false);
	const auto const_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::Const, ReferenceQualifier::None, false);
	const auto ref_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::LValueReference, false);
	const auto noexcept_fn = table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, true);
	require(void_fn != int_fn && int_fn != variadic_fn && int_fn != const_fn);
	require(int_fn != ref_fn && int_fn != noexcept_fn && const_fn != ref_fn);
	require(table.qualify(int_fn, CVQualifier::Const) == const_fn);
	require(table.pointer(int_fn) != int_fn);
	require(table.functionParameters(void_fn).value == 0);
	require(table.functionParameterType(table.functionParameters(int_fn)) == integer);
	require(table.function(table.builtin(CanonicalBuiltinKind::Void), int_param, false,
		CVQualifier::None, ReferenceQualifier::None, false) == int_fn);
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
		CVQualifier::None, ReferenceQualifier::None, false) == int_fn);
	require(table.record(EntityId{1}) == owner_a);
	require(table.enumeration(EntityId{3}) == enum_a);
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
			reordered_params, false, CVQualifier::None, ReferenceQualifier::None, false))));
	const auto reordered_owner = reordered.record(EntityId{1});
	const auto reordered_enum = reordered.enumeration(EntityId{3});
	require(sameStructure(table, enum_a, reordered, reordered_enum));
	require(sameStructure(table, mop_a, reordered,
		reordered.memberObjectPointer(reordered_owner, reordered_int)));
	require(sameStructure(table, mfp_a, reordered,
		reordered.memberFunctionPointer(reordered_owner,
			reordered.function(reordered.builtin(CanonicalBuiltinKind::Void), reordered_params, false,
				CVQualifier::None, ReferenceQualifier::None, false))));
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
			ReferenceQualifier::None, false);
	});
	rejects([&] {
		const TypeId bad_param[] = {int_fn};
		table.function(table.builtin(CanonicalBuiltinKind::Void), bad_param, false, CVQualifier::None,
			ReferenceQualifier::None, false);
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
