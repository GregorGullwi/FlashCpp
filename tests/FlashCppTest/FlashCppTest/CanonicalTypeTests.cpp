#include "CompilerIncludes.h"
#include <fstream>
#include "doctest.h"
#include "CanonicalTypeAdapter.h"
#include "ParserTemplateClassShared.h"
#include "../../architecture/CanonicalTypeTests.h"

TEST_CASE("Canonical types participate in scratch rollback") {
	FrontendContext context;
	const auto before = context.canonicalTypes().size();
	{
		auto transaction = context.beginScratchTransaction();
		context.canonicalTypes().builtin(CanonicalBuiltinKind::Int);
		transaction.rollback();
	}
	CHECK(context.canonicalTypes().size() == before);
}

TEST_CASE("Structural TypeId controls semantic type descriptor identity") {
	FrontendContext frontend;
	CanonicalTypeTable& canonical_types = frontend.canonicalTypes();
	const TypeId pointer_to_array = canonical_types.pointer(
		canonical_types.array(canonical_types.builtin(CanonicalBuiltinKind::Int), 3));
	const TypeId pointer_to_pointer_to_array = canonical_types.pointer(pointer_to_array);

	CanonicalTypeDesc structural;
	structural.type_index = nativeTypeIndex(TypeCategory::Int);
	structural.structural_type_id = pointer_to_array;

	CanonicalTypeDesc projected = structural;
	projected.pointer_levels.push_back(PointerLevel{});
	projected.array_dimensions.push_back(3);
	projected.pointee_array_declarator = true;

	TypeContext semantic_types;
	const CanonicalTypeId structural_id = semantic_types.intern(structural);
	const CanonicalTypeId projected_id = semantic_types.intern(projected);
	CanonicalTypeDesc nested_pointer = structural;
	nested_pointer.structural_type_id = pointer_to_pointer_to_array;
	const CanonicalTypeId nested_pointer_id = semantic_types.intern(nested_pointer);

	// Pointer wrappers live in the TypeId. Flat fields are compatibility
	// projections and cannot override that structural identity.
	CHECK(projected_id == structural_id);
	CHECK(nested_pointer_id != structural_id);
}

TEST_CASE("Canonical TypeIds rank by-value derived-to-base conversions") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_entity{801};
	const EntityId derived_entity{802};
	const CanonicalRecordBase base_link{
		base_entity, 0, CanonicalAccess::Public,
		CanonicalRecordBaseFlags::None, 0, 0};
	const std::array<CanonicalRecordBase, 1> derived_bases{base_link};
	table.publishRecordLayout(CanonicalRecordLayout{
		base_entity, 1, 1, 1, 1, 0, 0, CanonicalRecordLayoutFlags::None, 0});
	table.publishRecordFieldSchema(base_entity,
		std::span<const CanonicalRecordMember>{},
		std::span<const CanonicalRecordBase>{});
	table.publishRecordLayout(CanonicalRecordLayout{
		derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
	table.publishRecordFieldSchema(derived_entity,
		std::span<const CanonicalRecordMember>{}, derived_bases);

	TypeSpecifierNode derived(
		TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	derived.set_type_entity(derived_entity);
	TypeSpecifierNode base(
		TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	base.set_type_entity(base_entity);

	const std::optional<ConversionPlan> base_conversion =
		tryBuildCanonicalProjectableConversionPlan(derived, base);
	REQUIRE(base_conversion.has_value());
	CHECK(base_conversion->is_valid);
	CHECK(base_conversion->rank == ConversionRank::Conversion);
	CHECK(base_conversion->kind == StandardConversionKind::DerivedToBase);

	const std::optional<ConversionPlan> exact_conversion =
		tryBuildCanonicalProjectableConversionPlan(derived, derived);
	REQUIRE(exact_conversion.has_value());
	CHECK(exact_conversion->is_valid);
	CHECK(exact_conversion->rank == ConversionRank::ExactMatch);
}

TEST_CASE("Overload ranking compares non-projectable parameter shapes structurally") {
	FrontendContext frontend;
	TypeSpecifierNode smaller_array(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	smaller_array.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	TypeSpecifierNode larger_array = smaller_array;
	larger_array.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::None),
	});

	// Both interleaved declarators have the same empty legacy projection. Their
	// array bounds remain part of the canonical parameter type identity.
	CHECK_FALSE(isSameTypeIgnoringTopLevelCvAndRef(smaller_array, larger_array));
	CHECK(isSameTypeIgnoringTopLevelCvAndRef(smaller_array, smaller_array));
}

TEST_CASE("Qualification ranking reads pointee cv from canonical pointer-to-array types") {
	FrontendContext frontend;
	TypeSpecifierNode argument(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	argument.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	TypeSpecifierNode volatile_parameter = argument;
	volatile_parameter.set_cv_qualifier(CVQualifier::Volatile);
	TypeSpecifierNode const_volatile_parameter = argument;
	const_volatile_parameter.set_cv_qualifier(CVQualifier::ConstVolatile);

	CHECK(compareQualificationConversionDestinations(
		argument, volatile_parameter, const_volatile_parameter) == -1);
}

TEST_CASE("Ordered declarator array conversion preserves the element spine") {
	TypeSpecifierNode source(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	source.set_ordered_declarator({
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	TypeSpecifierNode target(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	target.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});

	const ConversionPlan plan = buildConversionPlan(source, target);
	CHECK(plan.is_valid);
	CHECK(plan.rank == ConversionRank::ExactMatch);
	CHECK(plan.kind == StandardConversionKind::ArrayToPointer);

	TypeSpecifierNode unknown_bound = source;
	unknown_bound.set_ordered_declarator({
		DeclaratorComponent::unknownBoundArray(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	CHECK(buildConversionPlan(unknown_bound, target).kind ==
		StandardConversionKind::ArrayToPointer);

	TypeSpecifierNode qualified = target;
	qualified.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	const ConversionPlan qualified_plan = buildConversionPlan(source, qualified);
	CHECK(qualified_plan.rank == ConversionRank::QualificationAdjustment);
	CHECK(qualified_plan.kind == StandardConversionKind::ArrayToPointer);

	TypeSpecifierNode mismatched = target;
	mismatched.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	CHECK_FALSE(buildConversionPlan(source, mismatched).is_valid);
}

TEST_CASE("Ordered references bind to ordered pointer objects") {
	FrontendContext frontend;
	TypeSpecifierNode pointer_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_object.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});

	TypeSpecifierNode lvalue_reference = pointer_object;
	lvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());

	TypeSpecifierNode lvalue_argument = pointer_object;
	lvalue_argument.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const ConversionPlan lvalue_plan =
		buildConversionPlan(lvalue_argument, lvalue_reference);
	CHECK(lvalue_plan.is_valid);
	CHECK(lvalue_plan.rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> canonical_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue_argument, lvalue_reference);
	REQUIRE(canonical_lvalue_plan.has_value());
	CHECK(canonical_lvalue_plan->is_valid);
	CHECK(canonical_lvalue_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode prvalue = pointer_object;
	CHECK_FALSE(buildConversionPlan(prvalue, lvalue_reference).is_valid);
	const std::optional<ConversionPlan> canonical_prvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, lvalue_reference);
	REQUIRE(canonical_prvalue_plan.has_value());
	CHECK_FALSE(canonical_prvalue_plan->is_valid);

	TypeSpecifierNode const_pointer_object = pointer_object;
	const_pointer_object.set_cv_qualifier(CVQualifier::Const);
	TypeSpecifierNode const_lvalue_argument = const_pointer_object;
	const_lvalue_argument.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_lvalue_reference = const_pointer_object;
	const_lvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const ConversionPlan const_plan =
		buildConversionPlan(const_lvalue_argument, const_lvalue_reference);
	CHECK(const_plan.is_valid);
	CHECK(const_plan.rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> canonical_const_plan =
		tryBuildCanonicalReferenceBindingPlan(
			const_lvalue_argument, const_lvalue_reference);
	REQUIRE(canonical_const_plan.has_value());
	CHECK(canonical_const_plan->is_valid);
	CHECK(canonical_const_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode qualified_pointer = pointer_object;
	qualified_pointer.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	TypeSpecifierNode qualified_reference = qualified_pointer;
	qualified_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const ConversionPlan qualified_plan =
		buildConversionPlan(lvalue_argument, qualified_reference);
	CHECK(qualified_plan.is_valid);
	// qualified_pointer differs from pointer_object only in the cv-qualifier of
	// its outermost pointer.  Per [over.ics.ref]/1, directly binding to a
	// reference whose referenced type differs only by added top-level
	// cv-qualification is the identity conversion, so this stays ExactMatch.
	CHECK(qualified_plan.rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> canonical_qualified_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue_argument, qualified_reference);
	REQUIRE(canonical_qualified_plan.has_value());
	CHECK(canonical_qualified_plan->is_valid);
	CHECK(canonical_qualified_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode rvalue_reference = pointer_object;
	rvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::rvalueReference());
	const ConversionPlan rvalue_plan =
		buildConversionPlan(prvalue, rvalue_reference);
	CHECK(rvalue_plan.is_valid);
	CHECK(rvalue_plan.rank == ConversionRank::ExactMatch);
	CHECK_FALSE(buildConversionPlan(lvalue_argument, rvalue_reference).is_valid);
	const std::optional<ConversionPlan> canonical_rvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, rvalue_reference);
	REQUIRE(canonical_rvalue_plan.has_value());
	CHECK(canonical_rvalue_plan->is_valid);
	CHECK(canonical_rvalue_plan->rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> canonical_lvalue_to_rvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(
			lvalue_argument, rvalue_reference);
	REQUIRE(canonical_lvalue_to_rvalue_plan.has_value());
	CHECK_FALSE(canonical_lvalue_to_rvalue_plan->is_valid);

	TypeSpecifierNode mismatched = lvalue_reference;
	mismatched.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	CHECK_FALSE(buildConversionPlan(lvalue_argument, mismatched).is_valid);
}

TEST_CASE("Canonical TypeIds plan prvalue materialization for const references") {
	FrontendContext frontend;
	TypeSpecifierNode prvalue(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode const_lvalue_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	const_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);

	const std::optional<ConversionPlan> canonical_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, const_lvalue_reference);
	REQUIRE(canonical_plan.has_value());
	CHECK(canonical_plan->is_valid);
	CHECK(canonical_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode nonconst_lvalue_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	nonconst_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> rejected_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, nonconst_lvalue_reference);
	REQUIRE(rejected_plan.has_value());
	CHECK_FALSE(rejected_plan->is_valid);

	TypeSpecifierNode double_prvalue(
		TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode const_double_lvalue_reference(
		TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::Const);
	const_double_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> converting_prvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, const_double_lvalue_reference);
	REQUIRE(converting_prvalue_plan.has_value());
	CHECK(converting_prvalue_plan->is_valid);
	CHECK(converting_prvalue_plan->rank == ConversionRank::Conversion);
	CHECK(converting_prvalue_plan->kind ==
		StandardConversionKind::FloatingIntegralConversion);
	TypeSpecifierNode nonconst_double_lvalue_reference = double_prvalue;
	nonconst_double_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> nonconst_conversion_plan =
		tryBuildCanonicalReferenceBindingPlan(
			prvalue, nonconst_double_lvalue_reference);
	REQUIRE(nonconst_conversion_plan.has_value());
	CHECK_FALSE(nonconst_conversion_plan->is_valid);

	TypeSpecifierNode lvalue = prvalue;
	lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> converting_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue, const_double_lvalue_reference);
	REQUIRE(converting_lvalue_plan.has_value());
	CHECK(converting_lvalue_plan->is_valid);
	CHECK(converting_lvalue_plan->rank == ConversionRank::Conversion);

	TypeSpecifierNode double_rvalue_reference = double_prvalue;
	double_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> converting_rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, double_rvalue_reference);
	REQUIRE(converting_rvalue_reference_plan.has_value());
	CHECK(converting_rvalue_reference_plan->is_valid);
	CHECK(converting_rvalue_reference_plan->rank == ConversionRank::Conversion);
	const std::optional<ConversionPlan> lvalue_to_rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue, double_rvalue_reference);
	REQUIRE(lvalue_to_rvalue_reference_plan.has_value());
	CHECK(lvalue_to_rvalue_reference_plan->is_valid);
	CHECK(lvalue_to_rvalue_reference_plan->rank == ConversionRank::Conversion);
	CHECK(lvalue_to_rvalue_reference_plan->kind ==
		StandardConversionKind::FloatingIntegralConversion);
}

TEST_CASE("Canonical TypeIds bind pointer-to-bool conversion temporaries") {
	FrontendContext frontend;
	TypeSpecifierNode int_pointer(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	int_pointer.add_pointer_level(CVQualifier::None);
	int_pointer.set_reference_qualifier(ReferenceQualifier::LValueReference);

	TypeSpecifierNode const_bool_reference(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::Const);
	const_bool_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> const_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(int_pointer, const_bool_reference);
	REQUIRE(const_reference_plan.has_value());
	CHECK(const_reference_plan->is_valid);
	CHECK(const_reference_plan->rank == ConversionRank::Conversion);
	CHECK(const_reference_plan->kind ==
		StandardConversionKind::BooleanConversion);

	TypeSpecifierNode bool_rvalue_reference(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	bool_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(int_pointer, bool_rvalue_reference);
	REQUIRE(rvalue_reference_plan.has_value());
	CHECK(rvalue_reference_plan->is_valid);
	CHECK(rvalue_reference_plan->rank == ConversionRank::Conversion);
	CHECK(rvalue_reference_plan->kind ==
		StandardConversionKind::BooleanConversion);

	TypeSpecifierNode bool_lvalue_reference(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	bool_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> mutable_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(int_pointer, bool_lvalue_reference);
	REQUIRE(mutable_reference_plan.has_value());
	CHECK_FALSE(mutable_reference_plan->is_valid);
}

TEST_CASE("Canonical TypeIds bind array references without decay") {
	FrontendContext frontend;
	TypeSpecifierNode array_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	array_type.set_ordered_declarator({DeclaratorComponent::array(3)});
	TypeSpecifierNode array_lvalue = array_type;
	array_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	TypeSpecifierNode array_reference = array_type;
	array_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());

	const std::optional<ConversionPlan> exact_array_plan =
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, array_reference);
	REQUIRE(exact_array_plan.has_value());
	CHECK(exact_array_plan->is_valid);
	CHECK(exact_array_plan->rank == ConversionRank::ExactMatch);
	CHECK(exact_array_plan->kind == StandardConversionKind::None);

	TypeSpecifierNode const_array_type = array_type;
	const_array_type.set_cv_qualifier(CVQualifier::Const);
	TypeSpecifierNode const_array_reference = const_array_type;
	const_array_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> qualified_array_plan =
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, const_array_reference);
	REQUIRE(qualified_array_plan.has_value());
	CHECK(qualified_array_plan->is_valid);
	// The referenced array differs only by top-level element cv-qualification,
	// so direct binding is the identity conversion ([over.ics.ref]/1); the
	// mutable-vs-const preference is applied by [over.ics.rank]/3.2.6.
	CHECK(qualified_array_plan->rank == ConversionRank::ExactMatch);
	CHECK(qualified_array_plan->kind == StandardConversionKind::None);
	const std::optional<ConversionPlan> const_array_xvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(array_type, const_array_reference);
	REQUIRE(const_array_xvalue_plan.has_value());
	CHECK(const_array_xvalue_plan->is_valid);
	CHECK(const_array_xvalue_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode pointer_array_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_array_type.set_ordered_declarator({
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	TypeSpecifierNode pointer_array_lvalue = pointer_array_type;
	pointer_array_lvalue.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_pointee_array_type = pointer_array_type;
	const_pointee_array_type.set_cv_qualifier(CVQualifier::Const);
	const_pointee_array_type.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> const_pointee_array_plan =
		tryBuildCanonicalReferenceBindingPlan(
			pointer_array_lvalue, const_pointee_array_type);
	REQUIRE(const_pointee_array_plan.has_value());
	CHECK(const_pointee_array_plan->is_valid);
	CHECK(const_pointee_array_plan->rank ==
		ConversionRank::QualificationAdjustment);

	TypeSpecifierNode pointer_to_pointer_array_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_to_pointer_array_type.set_ordered_declarator({
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	TypeSpecifierNode pointer_to_pointer_array_lvalue =
		pointer_to_pointer_array_type;
	pointer_to_pointer_array_lvalue.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_nested_pointee_array_type =
		pointer_to_pointer_array_type;
	const_nested_pointee_array_type.set_cv_qualifier(CVQualifier::Const);
	const_nested_pointee_array_type.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> const_nested_pointee_array_plan =
		tryBuildCanonicalReferenceBindingPlan(
			pointer_to_pointer_array_lvalue,
			const_nested_pointee_array_type);
	REQUIRE(const_nested_pointee_array_plan.has_value());
	CHECK_FALSE(const_nested_pointee_array_plan->is_valid);

	TypeSpecifierNode different_extent = array_type;
	different_extent.set_ordered_declarator({DeclaratorComponent::array(4)});
	different_extent.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> extent_mismatch_plan =
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, different_extent);
	REQUIRE(extent_mismatch_plan.has_value());
	CHECK_FALSE(extent_mismatch_plan->is_valid);
}

TEST_CASE("Canonical TypeIds bind function decay temporaries to pointer references") {
	FrontendContext frontend;
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	signature.setParameterTypes(std::move(parameters));
	FunctionSignature mismatched_signature;
	TypeSpecifierNode char_type(
		TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	mismatched_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> mismatched_parameters;
	mismatched_parameters.push_back(makeFunctionTypeFromSpecifier(char_type));
	mismatched_signature.setParameterTypes(std::move(mismatched_parameters));

	TypeSpecifierNode function_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	function_object.set_ordered_declarator({DeclaratorComponent::function()});
	function_object.set_function_signature(signature);
	TypeSpecifierNode function_lvalue = function_object;
	function_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);

	TypeSpecifierNode const_pointer_lvalue_reference = function_object;
	const_pointer_lvalue_reference.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::function(),
	});
	const_pointer_lvalue_reference.set_function_signature(signature);
	const_pointer_lvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> const_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(
			function_lvalue, const_pointer_lvalue_reference);
	REQUIRE(const_lvalue_plan.has_value());
	CHECK(const_lvalue_plan->is_valid);
	CHECK(const_lvalue_plan->kind == StandardConversionKind::FunctionToPointer);

	TypeSpecifierNode nonconst_pointer_lvalue_reference = function_object;
	nonconst_pointer_lvalue_reference.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::function(),
	});
	nonconst_pointer_lvalue_reference.set_function_signature(signature);
	nonconst_pointer_lvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> nonconst_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(
			function_lvalue, nonconst_pointer_lvalue_reference);
	REQUIRE(nonconst_lvalue_plan.has_value());
	CHECK_FALSE(nonconst_lvalue_plan->is_valid);

	TypeSpecifierNode pointer_rvalue_reference = nonconst_pointer_lvalue_reference;
	pointer_rvalue_reference.set_ordered_declarator({
		DeclaratorComponent::rvalueReference(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::function(),
	});
	pointer_rvalue_reference.set_function_signature(signature);
	const std::optional<ConversionPlan> rvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(
			function_lvalue, pointer_rvalue_reference);
	REQUIRE(rvalue_plan.has_value());
	CHECK(rvalue_plan->is_valid);
	CHECK(rvalue_plan->kind == StandardConversionKind::FunctionToPointer);

	TypeSpecifierNode mismatched_pointer_reference =
		const_pointer_lvalue_reference;
	mismatched_pointer_reference.set_function_signature(mismatched_signature);
	const std::optional<ConversionPlan> mismatched_signature_plan =
		tryBuildCanonicalReferenceBindingPlan(
			function_lvalue, mismatched_pointer_reference);
	REQUIRE(mismatched_signature_plan.has_value());
	CHECK_FALSE(mismatched_signature_plan->is_valid);
}

TEST_CASE("Canonical TypeIds bind pointer conversion temporaries to references") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_entity{828};
	const EntityId derived_entity{829};
	const std::array<CanonicalRecordBase, 1> derived_bases{
		CanonicalRecordBase{
			base_entity, 0, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None, 0, 0}};
	auto publish_record = [&table](EntityId entity,
		std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity,
			std::span<const CanonicalRecordMember>{}, bases);
	};
	publish_record(base_entity, {});
	publish_record(derived_entity, derived_bases);

	TypeSpecifierNode derived_pointer(
		TypeCategory::Struct, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	derived_pointer.set_type_entity(derived_entity);
	derived_pointer.add_pointer_level(CVQualifier::None);
	TypeSpecifierNode derived_pointer_lvalue = derived_pointer;
	derived_pointer_lvalue.set_reference_qualifier(
		ReferenceQualifier::LValueReference);

	TypeSpecifierNode const_base_pointer(
		TypeCategory::Struct, TypeQualifier::None, 0, Token{}, CVQualifier::Const);
	const_base_pointer.set_type_entity(base_entity);
	const_base_pointer.add_pointer_level(CVQualifier::Const);
	const_base_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> derived_to_base_plan =
		tryBuildCanonicalReferenceBindingPlan(
			derived_pointer_lvalue, const_base_pointer);
	REQUIRE(derived_to_base_plan.has_value());
	CHECK(derived_to_base_plan->is_valid);
	CHECK(derived_to_base_plan->rank == ConversionRank::Conversion);
	CHECK(derived_to_base_plan->kind == StandardConversionKind::DerivedToBase);

	TypeSpecifierNode const_void_pointer(
		TypeCategory::Void, TypeQualifier::None, 0, Token{}, CVQualifier::Const);
	const_void_pointer.add_pointer_level(CVQualifier::Const);
	const_void_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> object_to_void_plan =
		tryBuildCanonicalReferenceBindingPlan(
			derived_pointer_lvalue, const_void_pointer);
	REQUIRE(object_to_void_plan.has_value());
	CHECK(object_to_void_plan->is_valid);
	CHECK(object_to_void_plan->rank == ConversionRank::Conversion);
	CHECK(object_to_void_plan->kind == StandardConversionKind::PointerConversion);

	TypeSpecifierNode mutable_base_pointer(
		TypeCategory::Struct, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	mutable_base_pointer.set_type_entity(base_entity);
	mutable_base_pointer.add_pointer_level(CVQualifier::None);
	mutable_base_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> nonconst_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(
			derived_pointer_lvalue, mutable_base_pointer);
	REQUIRE(nonconst_lvalue_plan.has_value());
	CHECK_FALSE(nonconst_lvalue_plan->is_valid);

	TypeSpecifierNode base_pointer_rvalue_reference = mutable_base_pointer;
	base_pointer_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(
			derived_pointer_lvalue, base_pointer_rvalue_reference);
	REQUIRE(rvalue_reference_plan.has_value());
	CHECK(rvalue_reference_plan->is_valid);
	CHECK(rvalue_reference_plan->rank == ConversionRank::Conversion);
	CHECK(rvalue_reference_plan->kind == StandardConversionKind::DerivedToBase);
}

TEST_CASE("Canonical TypeIds reject pointee qualification through mutable references") {
	FrontendContext frontend;
	TypeSpecifierNode integer_pointer(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	integer_pointer.add_pointer_level(CVQualifier::None);
	TypeSpecifierNode integer_pointer_lvalue = integer_pointer;
	integer_pointer_lvalue.set_reference_qualifier(
		ReferenceQualifier::LValueReference);

	TypeSpecifierNode const_pointee_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	const_pointee_reference.add_pointer_level(CVQualifier::None);
	const_pointee_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> mutable_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(
			integer_pointer_lvalue, const_pointee_reference);
	REQUIRE(mutable_reference_plan.has_value());
	CHECK_FALSE(mutable_reference_plan->is_valid);

	TypeSpecifierNode const_pointer_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	const_pointer_reference.add_pointer_level(CVQualifier::Const);
	const_pointer_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> const_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(
			integer_pointer_lvalue, const_pointer_reference);
	REQUIRE(const_reference_plan.has_value());
	CHECK(const_reference_plan->is_valid);
	CHECK(const_reference_plan->kind ==
		StandardConversionKind::QualificationAdjustment);
}

TEST_CASE("Canonical TypeIds compare projectable function pointer pairs") {
	FrontendContext frontend;
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode char_type(
		TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	FunctionSignature throwing_signature;
	throwing_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	throwing_signature.setParameterTypes(std::move(parameters));
	FunctionSignature noexcept_signature = throwing_signature;
	noexcept_signature.is_noexcept = true;
	FunctionSignature mismatched_signature;
	mismatched_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> mismatched_parameters;
	mismatched_parameters.push_back(makeFunctionTypeFromSpecifier(char_type));
	mismatched_signature.setParameterTypes(std::move(mismatched_parameters));

	TypeSpecifierNode noexcept_pointer(
		TypeCategory::FunctionPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	noexcept_pointer.set_function_signature(noexcept_signature);
	TypeSpecifierNode throwing_pointer(
		TypeCategory::FunctionPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	throwing_pointer.set_function_signature(throwing_signature);
	const std::optional<ConversionPlan> exact_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_pointer, noexcept_pointer);
	REQUIRE(exact_plan.has_value());
	CHECK(exact_plan->is_valid);
	CHECK(exact_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode noexcept_lvalue = noexcept_pointer;
	noexcept_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> noexcept_relaxation_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_lvalue, throwing_pointer);
	REQUIRE(noexcept_relaxation_plan.has_value());
	CHECK(noexcept_relaxation_plan->is_valid);
	CHECK(noexcept_relaxation_plan->rank ==
		ConversionRank::QualificationAdjustment);
	CHECK(noexcept_relaxation_plan->kind ==
		StandardConversionKind::QualificationAdjustment);

	TypeSpecifierNode throwing_lvalue = throwing_pointer;
	throwing_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> rejected_noexcept_plan =
		tryBuildCanonicalProjectableConversionPlan(
			throwing_lvalue, noexcept_pointer);
	REQUIRE(rejected_noexcept_plan.has_value());
	CHECK_FALSE(rejected_noexcept_plan->is_valid);

	TypeSpecifierNode const_throwing_lvalue = throwing_lvalue;
	const_throwing_lvalue.set_cv_qualifier(CVQualifier::Const);
	const std::optional<ConversionPlan> top_level_cv_plan =
		tryBuildCanonicalProjectableConversionPlan(
			const_throwing_lvalue, throwing_pointer);
	REQUIRE(top_level_cv_plan.has_value());
	CHECK(top_level_cv_plan->is_valid);
	CHECK(top_level_cv_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode mismatched_pointer(
		TypeCategory::FunctionPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	mismatched_pointer.set_function_signature(mismatched_signature);
	const std::optional<ConversionPlan> mismatched_signature_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_pointer, mismatched_pointer);
	REQUIRE(mismatched_signature_plan.has_value());
	CHECK_FALSE(mismatched_signature_plan->is_valid);
}

TEST_CASE("Canonical TypeIds bind function pointer conversions to references") {
	FrontendContext frontend;
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature throwing_signature;
	throwing_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	throwing_signature.setParameterTypes(std::move(parameters));
	FunctionSignature noexcept_signature = throwing_signature;
	noexcept_signature.is_noexcept = true;
	auto make_function_pointer = [](const FunctionSignature& signature) {
		TypeSpecifierNode type(TypeCategory::FunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_function_signature(signature);
		return type;
	};
	TypeSpecifierNode noexcept_pointer =
		make_function_pointer(noexcept_signature);
	noexcept_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_throwing_reference =
		make_function_pointer(throwing_signature);
	const_throwing_reference.set_cv_qualifier(CVQualifier::Const);
	const_throwing_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> relaxation_plan =
		tryBuildCanonicalReferenceBindingPlan(
			noexcept_pointer, const_throwing_reference);
	REQUIRE(relaxation_plan.has_value());
	CHECK(relaxation_plan->is_valid);
	CHECK(relaxation_plan->kind ==
		StandardConversionKind::QualificationAdjustment);

	TypeSpecifierNode nonconst_throwing_reference =
		make_function_pointer(throwing_signature);
	nonconst_throwing_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> nonconst_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(
			noexcept_pointer, nonconst_throwing_reference);
	REQUIRE(nonconst_reference_plan.has_value());
	CHECK_FALSE(nonconst_reference_plan->is_valid);

	TypeSpecifierNode noexcept_reference =
		make_function_pointer(noexcept_signature);
	noexcept_reference.set_cv_qualifier(CVQualifier::Const);
	noexcept_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> reverse_plan =
		tryBuildCanonicalReferenceBindingPlan(
			make_function_pointer(throwing_signature), noexcept_reference);
	REQUIRE(reverse_plan.has_value());
	CHECK_FALSE(reverse_plan->is_valid);

	TypeSpecifierNode throwing_rvalue_reference =
		make_function_pointer(throwing_signature);
	throwing_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(
			noexcept_pointer, throwing_rvalue_reference);
	REQUIRE(rvalue_reference_plan.has_value());
	CHECK(rvalue_reference_plan->is_valid);
	CHECK(rvalue_reference_plan->kind ==
		StandardConversionKind::QualificationAdjustment);
}

TEST_CASE("Canonical TypeIds compare same-owner member function pointer pairs") {
	FrontendContext frontend;
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode char_type(
		TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	FunctionSignature throwing_signature;
	throwing_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	throwing_signature.setParameterTypes(std::move(parameters));
	FunctionSignature noexcept_signature = throwing_signature;
	noexcept_signature.is_noexcept = true;
	FunctionSignature mismatched_signature;
	mismatched_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> mismatched_parameters;
	mismatched_parameters.push_back(makeFunctionTypeFromSpecifier(char_type));
	mismatched_signature.setParameterTypes(std::move(mismatched_parameters));
	auto make_member_function_pointer = [](EntityId owner,
		const FunctionSignature& signature) {
		TypeSpecifierNode type(
			TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_entity(owner);
		type.set_function_signature(signature);
		return type;
	};
	const EntityId owner{805};
	TypeSpecifierNode noexcept_pointer =
		make_member_function_pointer(owner, noexcept_signature);
	TypeSpecifierNode throwing_pointer =
		make_member_function_pointer(owner, throwing_signature);
	const std::optional<ConversionPlan> exact_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_pointer, noexcept_pointer);
	REQUIRE(exact_plan.has_value());
	CHECK(exact_plan->is_valid);
	CHECK(exact_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode noexcept_lvalue = noexcept_pointer;
	noexcept_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> relaxation_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_lvalue, throwing_pointer);
	REQUIRE(relaxation_plan.has_value());
	CHECK(relaxation_plan->is_valid);
	CHECK(relaxation_plan->rank == ConversionRank::QualificationAdjustment);
	CHECK(relaxation_plan->kind ==
		StandardConversionKind::QualificationAdjustment);

	TypeSpecifierNode throwing_lvalue = throwing_pointer;
	throwing_lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> rejected_relaxation_plan =
		tryBuildCanonicalProjectableConversionPlan(
			throwing_lvalue, noexcept_pointer);
	REQUIRE(rejected_relaxation_plan.has_value());
	CHECK_FALSE(rejected_relaxation_plan->is_valid);

	TypeSpecifierNode mismatched_pointer =
		make_member_function_pointer(owner, mismatched_signature);
	const std::optional<ConversionPlan> mismatched_signature_plan =
		tryBuildCanonicalProjectableConversionPlan(
			noexcept_pointer, mismatched_pointer);
	REQUIRE(mismatched_signature_plan.has_value());
	CHECK_FALSE(mismatched_signature_plan->is_valid);

	TypeSpecifierNode const_noexcept_lvalue = noexcept_lvalue;
	const_noexcept_lvalue.set_cv_qualifier(CVQualifier::Const);
	const std::optional<ConversionPlan> top_level_cv_plan =
		tryBuildCanonicalProjectableConversionPlan(
			const_noexcept_lvalue, noexcept_pointer);
	REQUIRE(top_level_cv_plan.has_value());
	CHECK(top_level_cv_plan->is_valid);
	CHECK(top_level_cv_plan->rank == ConversionRank::ExactMatch);
}

TEST_CASE("Canonical TypeIds preserve dependent noexcept member pointer identity") {
	FrontendContext frontend;
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature throwing_signature;
	throwing_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	Token dependent_expression_token(
		Token::Type::Identifier, std::string_view("IsNoexcept"), 0, 0, 0);
	const ASTNode dependent_expression = ASTNode::emplace_node<ExpressionNode>(
		IdentifierNode(dependent_expression_token));
	const ExprId dependent_noexcept =
		frontend.dependentExpressions().intern(dependent_expression);
	FunctionSignature dependent_signature = throwing_signature;
	dependent_signature.dependent_noexcept = dependent_noexcept;
	auto make_member_function_pointer = [](EntityId owner,
		const FunctionSignature& signature) {
		TypeSpecifierNode type(
			TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_entity(owner);
		type.set_function_signature(signature);
		return type;
	};
	const EntityId owner{828};
	const TypeSpecifierNode dependent_pointer =
		make_member_function_pointer(owner, dependent_signature);
	const std::optional<ConversionPlan> identical_plan =
		tryBuildCanonicalProjectableConversionPlan(
			dependent_pointer, dependent_pointer);
	REQUIRE(identical_plan.has_value());
	CHECK(identical_plan->is_valid);
	CHECK(identical_plan->rank == ConversionRank::ExactMatch);

	Token other_expression_token(
		Token::Type::Identifier, std::string_view("CanThrow"), 0, 0, 0);
	const ASTNode other_expression = ASTNode::emplace_node<ExpressionNode>(
		IdentifierNode(other_expression_token));
	FunctionSignature other_dependent_signature = dependent_signature;
	other_dependent_signature.dependent_noexcept =
		frontend.dependentExpressions().intern(other_expression);
	const std::optional<ConversionPlan> distinct_expression_plan =
		tryBuildCanonicalProjectableConversionPlan(
			dependent_pointer,
			make_member_function_pointer(owner, other_dependent_signature));
	CHECK_FALSE(distinct_expression_plan.has_value());

	const std::optional<ConversionPlan> unresolved_relaxation_plan =
		tryBuildCanonicalProjectableConversionPlan(
			dependent_pointer,
			make_member_function_pointer(owner, throwing_signature));
	CHECK_FALSE(unresolved_relaxation_plan.has_value());
}

TEST_CASE("Canonical TypeIds plan member function pointer base conversions") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_owner{820};
	const EntityId derived_owner{821};
	const EntityId private_derived_owner{822};
	const EntityId virtual_derived_owner{823};
	const EntityId left_owner{824};
	const EntityId right_owner{825};
	const EntityId ambiguous_derived_owner{826};
	const EntityId unrelated_owner{827};

	auto publish_record = [&table](EntityId entity,
		std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity,
			std::span<const CanonicalRecordMember>{}, bases);
	};
	auto public_base = [](EntityId entity, CanonicalRecordBaseFlags flags) {
		return CanonicalRecordBase{
			entity, 0, CanonicalAccess::Public, flags, 0, 0};
	};
	const std::array<CanonicalRecordBase, 1> base_link{
		public_base(base_owner, CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalRecordBase, 1> private_base_link{
		CanonicalRecordBase{
			base_owner, 0, CanonicalAccess::Private,
			CanonicalRecordBaseFlags::None, 0, 0}};
	const std::array<CanonicalRecordBase, 1> virtual_base_link{
		public_base(base_owner, CanonicalRecordBaseFlags::Virtual)};
	const std::array<CanonicalRecordBase, 2> ambiguous_base_links{
		public_base(left_owner, CanonicalRecordBaseFlags::None),
		public_base(right_owner, CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalRecordBase, 1> left_base_link{
		public_base(base_owner, CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalRecordBase, 1> right_base_link{
		public_base(base_owner, CanonicalRecordBaseFlags::None)};
	publish_record(base_owner, {});
	publish_record(derived_owner, base_link);
	publish_record(private_derived_owner, private_base_link);
	publish_record(virtual_derived_owner, virtual_base_link);
	publish_record(left_owner, left_base_link);
	publish_record(right_owner, right_base_link);
	publish_record(ambiguous_derived_owner, ambiguous_base_links);
	publish_record(unrelated_owner, {});

	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode char_type(
		TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	FunctionSignature throwing_signature;
	throwing_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	throwing_signature.setParameterTypes(std::move(parameters));
	FunctionSignature noexcept_signature = throwing_signature;
	noexcept_signature.is_noexcept = true;
	FunctionSignature mismatched_signature;
	mismatched_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> mismatched_parameters;
	mismatched_parameters.push_back(makeFunctionTypeFromSpecifier(char_type));
	mismatched_signature.setParameterTypes(std::move(mismatched_parameters));
	auto make_member_function_pointer = [](EntityId owner,
		const FunctionSignature& signature) {
		TypeSpecifierNode type(
			TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_entity(owner);
		type.set_function_signature(signature);
		return type;
	};
	const TypeSpecifierNode base_pointer =
		make_member_function_pointer(base_owner, throwing_signature);
	const TypeSpecifierNode derived_pointer =
		make_member_function_pointer(derived_owner, throwing_signature);
	const std::optional<ConversionPlan> base_to_derived_plan =
		tryBuildCanonicalProjectableConversionPlan(base_pointer, derived_pointer);
	REQUIRE(base_to_derived_plan.has_value());
	CHECK(base_to_derived_plan->is_valid);
	CHECK(base_to_derived_plan->rank == ConversionRank::Conversion);
	CHECK(base_to_derived_plan->kind == StandardConversionKind::PointerConversion);

	const TypeSpecifierNode derived_noexcept_pointer =
		make_member_function_pointer(derived_owner, noexcept_signature);
	const std::optional<ConversionPlan> noexcept_relaxation_plan =
		tryBuildCanonicalProjectableConversionPlan(
			make_member_function_pointer(base_owner, noexcept_signature),
			derived_pointer);
	REQUIRE(noexcept_relaxation_plan.has_value());
	CHECK(noexcept_relaxation_plan->is_valid);
	CHECK(noexcept_relaxation_plan->rank == ConversionRank::Conversion);
	const std::optional<ConversionPlan> rejected_noexcept_plan =
		tryBuildCanonicalProjectableConversionPlan(
			base_pointer, derived_noexcept_pointer);
	REQUIRE(rejected_noexcept_plan.has_value());
	CHECK_FALSE(rejected_noexcept_plan->is_valid);
	const std::optional<ConversionPlan> rejected_signature_plan =
		tryBuildCanonicalProjectableConversionPlan(
			base_pointer,
			make_member_function_pointer(derived_owner, mismatched_signature));
	REQUIRE(rejected_signature_plan.has_value());
	CHECK_FALSE(rejected_signature_plan->is_valid);

	const TypeSpecifierNode private_derived_pointer =
		make_member_function_pointer(private_derived_owner, throwing_signature);
	const TypeSpecifierNode virtual_derived_pointer =
		make_member_function_pointer(virtual_derived_owner, throwing_signature);
	const TypeSpecifierNode ambiguous_derived_pointer =
		make_member_function_pointer(ambiguous_derived_owner, throwing_signature);
	const TypeSpecifierNode unrelated_pointer =
		make_member_function_pointer(unrelated_owner, throwing_signature);
	const std::array<const TypeSpecifierNode*, 4> rejected_targets{
		&private_derived_pointer,
		&virtual_derived_pointer,
		&ambiguous_derived_pointer,
		&unrelated_pointer,
	};
	for (const TypeSpecifierNode* target : rejected_targets) {
		const std::optional<ConversionPlan> rejected_plan =
			tryBuildCanonicalProjectableConversionPlan(base_pointer, *target);
		REQUIRE(rejected_plan.has_value());
		CHECK_FALSE(rejected_plan->is_valid);
	}
	const std::optional<ConversionPlan> rejected_reverse_plan =
		tryBuildCanonicalProjectableConversionPlan(derived_pointer, base_pointer);
	REQUIRE(rejected_reverse_plan.has_value());
	CHECK_FALSE(rejected_reverse_plan->is_valid);
}

TEST_CASE("Canonical TypeIds convert non-projectable ordered member-pointer owners") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_owner{840};
	const EntityId derived_owner{841};
	const EntityId private_derived_owner{842};
	const CanonicalRecordBase public_base{
		base_owner, 0, CanonicalAccess::Public,
		CanonicalRecordBaseFlags::None, 0, 0};
	const CanonicalRecordBase private_base{
		base_owner, 0, CanonicalAccess::Private,
		CanonicalRecordBaseFlags::None, 0, 0};
	auto publish_record = [&table](EntityId entity, std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity, std::span<const CanonicalRecordMember>{}, bases);
	};
	publish_record(base_owner, {});
	publish_record(derived_owner, std::span<const CanonicalRecordBase>(&public_base, 1));
	publish_record(private_derived_owner, std::span<const CanonicalRecordBase>(&private_base, 1));
	auto make_ordered_member_pointer = [](EntityId owner) {
		TypeSpecifierNode type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
		type.set_ordered_declarator({DeclaratorComponent::memberPointer(owner, false, CVQualifier::None)});
		return type;
	};
	TypeSpecifierNode base_member = make_ordered_member_pointer(base_owner);
	base_member.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const TypeSpecifierNode derived_member = make_ordered_member_pointer(derived_owner);
	const TypeSpecifierNode private_derived_member = make_ordered_member_pointer(private_derived_owner);
	CHECK_FALSE(base_member.ordered_declarator_has_legacy_projection());
	CHECK_FALSE(derived_member.ordered_declarator_has_legacy_projection());

	const std::optional<ConversionPlan> base_to_derived_plan = tryBuildCanonicalOrderedConversionPlan(base_member, derived_member);
	REQUIRE(base_to_derived_plan.has_value());
	CHECK(base_to_derived_plan->is_valid);
	CHECK(base_to_derived_plan->rank == ConversionRank::Conversion);
	CHECK(base_to_derived_plan->kind == StandardConversionKind::PointerConversion);
	const std::optional<ConversionPlan> inaccessible_plan = tryBuildCanonicalOrderedConversionPlan(base_member, private_derived_member);
	REQUIRE(inaccessible_plan.has_value());
	CHECK_FALSE(inaccessible_plan->is_valid);

	TypeSpecifierNode int_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature noexcept_signature;
	noexcept_signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	noexcept_signature.is_noexcept = true;
	FunctionSignature throwing_signature = noexcept_signature;
	throwing_signature.is_noexcept = false;
	auto make_ordered_member_function_pointer = [](EntityId owner, const FunctionSignature& signature) {
		TypeSpecifierNode type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
		type.set_function_signature(signature);
		type.set_ordered_declarator({DeclaratorComponent::memberPointer(owner, true, CVQualifier::None)});
		return type;
	};
	TypeSpecifierNode base_noexcept_member_function = make_ordered_member_function_pointer(base_owner, noexcept_signature);
	base_noexcept_member_function.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const TypeSpecifierNode derived_throwing_member_function = make_ordered_member_function_pointer(derived_owner, throwing_signature);
	CHECK_FALSE(base_noexcept_member_function.ordered_declarator_has_legacy_projection());

	const std::optional<ConversionPlan> callable_conversion_plan =
		tryBuildCanonicalOrderedConversionPlan(
			base_noexcept_member_function,
			derived_throwing_member_function);
	REQUIRE(callable_conversion_plan.has_value());
	CHECK(callable_conversion_plan->is_valid);
	CHECK(callable_conversion_plan->rank == ConversionRank::Conversion);
	CHECK(callable_conversion_plan->kind == StandardConversionKind::PointerConversion);
}

TEST_CASE("Canonical TypeIds retain class specialization member function pointer owners") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId int_argument[] = {
		table.builtin(CanonicalBuiltinKind::Int),
	};
	const TypeId char_argument[] = {
		table.builtin(CanonicalBuiltinKind::Char),
	};
	const TypeId int_owner = table.templateSpecialization(
		TemplateDeclId{910}, int_argument);
	const TypeId char_owner = table.templateSpecialization(
		TemplateDeclId{910}, char_argument);
	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	auto make_member_function_pointer = [&signature](TypeId owner) {
		TypeSpecifierNode type(
			TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_type_id(owner);
		type.set_function_signature(signature);
		return type;
	};
	const TypeSpecifierNode int_pointer =
		make_member_function_pointer(int_owner);
	const TypeSpecifierNode char_pointer =
		make_member_function_pointer(char_owner);
	const CanonicalTypeImport imported = importCanonicalType(table, int_pointer);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(table.memberPointerOwner(imported.type) == int_owner);
	CHECK(table.memberPointerPointee(imported.type) == importCanonicalFunctionSignature(
		table, signature, TypeId{}).type);

	const std::optional<ConversionPlan> exact_plan =
		tryBuildCanonicalProjectableConversionPlan(int_pointer, int_pointer);
	REQUIRE(exact_plan.has_value());
	CHECK(exact_plan->is_valid);
	CHECK(exact_plan->rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> different_owner_plan =
		tryBuildCanonicalProjectableConversionPlan(int_pointer, char_pointer);
	REQUIRE(different_owner_plan.has_value());
	CHECK_FALSE(different_owner_plan->is_valid);
}

TEST_CASE("Canonical TypeIds classify class specialization member pointer bases") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId int_argument = table.builtin(CanonicalBuiltinKind::Int);
	auto make_owner = [&table, int_argument](uint32_t declaration) {
		const CanonicalTemplateArgument arguments[] = {
			CanonicalTemplateArgument::makeType(int_argument),
		};
		return table.templateSpecialization(
			TemplateDeclId{declaration}, arguments);
	};
	const TypeId base_owner = make_owner(920);
	const TypeId derived_owner = make_owner(921);
	const TypeId private_derived_owner = make_owner(922);
	const TypeId virtual_derived_owner = make_owner(923);
	const TypeId left_owner = make_owner(924);
	const TypeId right_owner = make_owner(925);
	const TypeId ambiguous_derived_owner = make_owner(926);
	const TypeId unrelated_owner = make_owner(927);
	auto publish_bases = [&table](TypeId owner,
		std::span<const CanonicalClassBase> bases) {
		table.publishClassBaseSchema(owner, bases);
	};
	auto base_link = [](TypeId type, CanonicalAccess access,
		CanonicalRecordBaseFlags flags) {
		return CanonicalClassBase{type, access, flags, 0};
	};
	const std::array<CanonicalClassBase, 1> public_base_link{
		base_link(base_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalClassBase, 1> private_base_link{
		base_link(base_owner, CanonicalAccess::Private,
			CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalClassBase, 1> virtual_base_link{
		base_link(base_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::Virtual)};
	const std::array<CanonicalClassBase, 1> left_base_link{public_base_link[0]};
	const std::array<CanonicalClassBase, 1> right_base_link{public_base_link[0]};
	const std::array<CanonicalClassBase, 2> ambiguous_base_links{
		base_link(left_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None),
		base_link(right_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None),
	};
	publish_bases(base_owner, {});
	publish_bases(derived_owner, public_base_link);
	publish_bases(private_derived_owner, private_base_link);
	publish_bases(virtual_derived_owner, virtual_base_link);
	publish_bases(left_owner, left_base_link);
	publish_bases(right_owner, right_base_link);
	publish_bases(ambiguous_derived_owner, ambiguous_base_links);
	publish_bases(unrelated_owner, {});

	CHECK(classifyCanonicalDerivedBaseConversion(
		table, derived_owner, base_owner) ==
		DerivedBaseConversionKind::UniquePublicNonVirtual);
	CHECK(classifyCanonicalDerivedBaseConversion(
		table, private_derived_owner, base_owner) ==
		DerivedBaseConversionKind::Inaccessible);
	CHECK(classifyCanonicalDerivedBaseConversion(
		table, virtual_derived_owner, base_owner) ==
		DerivedBaseConversionKind::PublicVirtual);
	CHECK(classifyCanonicalDerivedBaseConversion(
		table, ambiguous_derived_owner, base_owner) ==
		DerivedBaseConversionKind::Ambiguous);
	CHECK(classifyCanonicalDerivedBaseConversion(
		table, unrelated_owner, base_owner) ==
		DerivedBaseConversionKind::NotRelated);

	const TypeId record_base_owner = table.record(EntityId{929});
	const TypeId record_derived_owner = table.record(EntityId{930});
	const TypeId specialization_intermediate_owner = make_owner(931);
	const std::array<CanonicalClassBase, 1> record_base_link{
		base_link(record_base_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None)};
	const std::array<CanonicalClassBase, 1> specialization_link{
		base_link(specialization_intermediate_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None)};
	publish_bases(record_base_owner, {});
	publish_bases(record_derived_owner, specialization_link);
	publish_bases(specialization_intermediate_owner, record_base_link);
	CHECK(classifyCanonicalDerivedBaseConversion(
		table, record_derived_owner, record_base_owner) ==
		DerivedBaseConversionKind::UniquePublicNonVirtual);

	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	auto make_member_function_pointer = [&signature](TypeId owner) {
		TypeSpecifierNode type(TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_type_id(owner);
		type.set_function_signature(signature);
		return type;
	};
	const TypeSpecifierNode base_function_pointer =
		make_member_function_pointer(base_owner);
	const TypeSpecifierNode derived_function_pointer =
		make_member_function_pointer(derived_owner);
	const std::optional<ConversionPlan> function_plan =
		tryBuildCanonicalProjectableConversionPlan(
			base_function_pointer, derived_function_pointer);
	REQUIRE(function_plan.has_value());
	CHECK(function_plan->is_valid);
	CHECK(function_plan->rank == ConversionRank::Conversion);
	CHECK(function_plan->kind == StandardConversionKind::PointerConversion);
	auto make_member_object_pointer = [&int_type](TypeId owner) {
		TypeSpecifierNode type(TypeCategory::MemberObjectPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_type_id(owner);
		type.set_member_object_pointee(&int_type);
		return type;
	};
	const std::optional<ConversionPlan> object_plan =
		tryBuildCanonicalProjectableConversionPlan(
			make_member_object_pointer(base_owner),
			make_member_object_pointer(derived_owner));
	REQUIRE(object_plan.has_value());
	CHECK(object_plan->is_valid);
	CHECK(object_plan->rank == ConversionRank::Conversion);
	CHECK(object_plan->kind == StandardConversionKind::PointerConversion);
	const std::optional<ConversionPlan> mixed_owner_plan =
		tryBuildCanonicalProjectableConversionPlan(
			make_member_function_pointer(record_base_owner),
			make_member_function_pointer(record_derived_owner));
	REQUIRE(mixed_owner_plan.has_value());
	CHECK(mixed_owner_plan->is_valid);
	CHECK(mixed_owner_plan->kind == StandardConversionKind::PointerConversion);
	for (const TypeId rejected_owner : {
		private_derived_owner, virtual_derived_owner,
		ambiguous_derived_owner, unrelated_owner}) {
		const std::optional<ConversionPlan> rejected_plan =
			tryBuildCanonicalProjectableConversionPlan(
				base_function_pointer,
				make_member_function_pointer(rejected_owner));
		REQUIRE(rejected_plan.has_value());
		CHECK_FALSE(rejected_plan->is_valid);
	}

	const TypeId rollback_owner = make_owner(928);
	{
		CanonicalTypeTransaction transaction(table);
		publish_bases(rollback_owner, public_base_link);
	}
	CHECK_FALSE(table.hasClassBaseSchema(rollback_owner));
}

TEST_CASE("Canonical function-template ordering handles trailing type packs") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TemplateDeclId all_pack_template{940};
	const TemplateDeclId leading_pack_template{941};
	const TypeId all_pack_type = table.templateParameter(all_pack_template, 0);
	const TypeId leading_type = table.templateParameter(leading_pack_template, 0);
	const TypeId trailing_pack_type = table.templateParameter(leading_pack_template, 1);
	const TypeId marker_type = table.record(EntityId{942});
	const TypeId character = table.builtin(CanonicalBuiltinKind::Char);
	const std::array<TypeId, 1> all_pack_parameters{all_pack_type};
	const std::array<TypeId, 2> leading_pack_parameters{
		leading_type, trailing_pack_type};
	const std::array<TypeId, 3> target_parameters{
		integer, marker_type, character};
	auto make_function = [&table, integer](std::span<const TypeId> parameters) {
		return table.function(
			integer,
			parameters,
			false,
			CVQualifier::None,
			ReferenceQualifier::None,
			false,
			CanonicalCallingConvention::Default,
			CanonicalDllLinkage::None,
			ExprId{});
	};
	const CanonicalFunctionTemplateTypePattern all_pack_pattern{
		make_function(all_pack_parameters),
		std::optional<size_t>{0},
		std::optional<uint32_t>{0}};
	const CanonicalFunctionTemplateTypePattern leading_pack_pattern{
		make_function(leading_pack_parameters),
		std::optional<size_t>{1},
		std::optional<uint32_t>{1}};
	const CanonicalFunctionTemplateTypePattern target_pattern{
		make_function(target_parameters), std::nullopt, std::nullopt};

	const CanonicalTemplateTypeDeduction all_pack_target =
		deduceCanonicalFunctionTemplateType(
			table, all_pack_pattern, target_pattern, all_pack_template);
	CHECK(all_pack_target.status == CanonicalTemplateDeductionStatus::Match);
	CHECK(all_pack_target.bindings.size() == 3);
	CHECK(std::ranges::all_of(
		all_pack_target.bindings,
		[](const CanonicalTemplateTypeBinding& binding) {
			return binding.parameter_index == 0 &&
				binding.pack_element_index.has_value();
		}));
	CHECK(std::ranges::any_of(
		all_pack_target.bindings,
		[integer](const CanonicalTemplateTypeBinding& binding) {
			return binding.argument == integer;
		}));
	CHECK(std::ranges::any_of(
		all_pack_target.bindings,
		[marker_type](const CanonicalTemplateTypeBinding& binding) {
			return binding.argument == marker_type;
		}));
	CHECK(std::ranges::any_of(
		all_pack_target.bindings,
		[character](const CanonicalTemplateTypeBinding& binding) {
			return binding.argument == character;
		}));

	const CanonicalTemplatePartialOrdering ordering =
		compareCanonicalFunctionTemplateTypes(
			table,
			all_pack_pattern,
			all_pack_template,
			leading_pack_pattern,
			leading_pack_template);
	CHECK(ordering == CanonicalTemplatePartialOrdering::SecondMoreSpecialized);
}

TEST_CASE("Canonical function-template ordering binds direct integral NTTPs by identity") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TemplateDeclId buffer_template{950};
	const TemplateDeclId first_function_template{951};
	const TemplateDeclId second_function_template{952};
	const std::array<CanonicalTemplateArgument, 2> first_arguments{
		CanonicalTemplateArgument::makeNonType(ExprId{1}),
		CanonicalTemplateArgument::makeNonType(ExprId{5})};
	const std::array<CanonicalTemplateArgument, 2> second_arguments{
		CanonicalTemplateArgument::makeNonType(ExprId{5}),
		CanonicalTemplateArgument::makeNonType(ExprId{2})};
	const std::array<CanonicalTemplateArgument, 2> target_arguments{
		CanonicalTemplateArgument::makeNonType(ExprId{6}),
		CanonicalTemplateArgument::makeNonType(ExprId{5})};
	const TypeId first_buffer =
		table.templateSpecialization(buffer_template, first_arguments);
	const TypeId second_buffer =
		table.templateSpecialization(buffer_template, second_arguments);
	const TypeId target_buffer =
		table.templateSpecialization(buffer_template, target_arguments);
	const auto make_function = [&table, integer](TypeId parameter) {
		const std::array<TypeId, 1> parameters{parameter};
		return table.function(
			integer,
			parameters,
			false,
			CVQualifier::None,
			ReferenceQualifier::None,
			false,
			CanonicalCallingConvention::Default,
			CanonicalDllLinkage::None,
			ExprId{});
	};
	const CanonicalFunctionTemplateTypePattern first_pattern{
		make_function(first_buffer),
		std::nullopt,
		std::nullopt,
		{
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{1},
				CanonicalTemplateNonTypeArgumentKind::TemplateParameter,
				first_function_template,
				0,
				integer},
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{5},
				CanonicalTemplateNonTypeArgumentKind::Literal,
				TemplateDeclId{},
				0,
				TypeId{}}}};
	const CanonicalFunctionTemplateTypePattern second_pattern{
		make_function(second_buffer),
		std::nullopt,
		std::nullopt,
		{
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{5},
				CanonicalTemplateNonTypeArgumentKind::Literal,
				TemplateDeclId{},
				0,
				TypeId{}},
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{2},
				CanonicalTemplateNonTypeArgumentKind::TemplateParameter,
				second_function_template,
				0,
				integer}}};
	const CanonicalFunctionTemplateTypePattern target_pattern{
		make_function(target_buffer),
		std::nullopt,
		std::nullopt,
		{
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{6},
				CanonicalTemplateNonTypeArgumentKind::Literal,
				TemplateDeclId{},
				0,
				TypeId{}},
			CanonicalTemplateNonTypeArgumentPattern{
				ExprId{5},
				CanonicalTemplateNonTypeArgumentKind::Literal,
				TemplateDeclId{},
				0,
				TypeId{}}}};

	const CanonicalTemplateTypeDeduction target_deduction =
		deduceCanonicalFunctionTemplateType(
			table, first_pattern, target_pattern, first_function_template);
	CHECK(target_deduction.status == CanonicalTemplateDeductionStatus::Match);
	REQUIRE(target_deduction.non_type_bindings.size() == 1);
	CHECK(target_deduction.non_type_bindings.front().parameter_index == 0);
	CHECK(target_deduction.non_type_bindings.front().argument.expression == ExprId{6});

	const CanonicalTemplatePartialOrdering ordering =
		compareCanonicalFunctionTemplateTypes(
			table,
			first_pattern,
			first_function_template,
			second_pattern,
			second_function_template);
	CHECK(ordering == CanonicalTemplatePartialOrdering::Neither);
}

TEST_CASE("Canonical TypeIds compare member object pointer pairs") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId owner = table.record(EntityId{806});
	const TypeId other_owner = table.record(EntityId{807});
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId const_integer = table.qualify(integer, CVQualifier::Const);
	const TypeId integer_member = table.memberObjectPointer(owner, integer);
	const TypeId const_integer_member =
		table.memberObjectPointer(owner, const_integer);
	const TypeId other_owner_member =
		table.memberObjectPointer(other_owner, integer);

	const ConversionPlan exact_plan = buildCanonicalStructuralConversionPlan(
		table, integer_member, integer_member);
	CHECK(exact_plan.is_valid);
	CHECK(exact_plan.rank == ConversionRank::ExactMatch);

	const ConversionPlan qualification_plan =
		buildCanonicalStructuralConversionPlan(
			table, integer_member, const_integer_member);
	CHECK(qualification_plan.is_valid);
	CHECK(qualification_plan.rank == ConversionRank::QualificationAdjustment);

	const ConversionPlan rejected_qualification_plan =
		buildCanonicalStructuralConversionPlan(
			table, const_integer_member, integer_member);
	CHECK_FALSE(rejected_qualification_plan.is_valid);

	const ConversionPlan rejected_owner_plan =
		buildCanonicalStructuralConversionPlan(
			table, integer_member, other_owner_member);
	CHECK_FALSE(rejected_owner_plan.is_valid);
}

TEST_CASE("Canonical TypeIds bind member-pointer conversion temporaries to references") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_owner{830};
	const EntityId derived_owner{831};
	const std::array<CanonicalRecordBase, 1> derived_bases{
		CanonicalRecordBase{
			base_owner, 0, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None, 0, 0}};
	auto publish_record = [&table](EntityId entity,
		std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity,
			std::span<const CanonicalRecordMember>{}, bases);
	};
	publish_record(base_owner, {});
	publish_record(derived_owner, derived_bases);

	TypeSpecifierNode int_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	auto make_member_object_pointer = [&int_type](EntityId owner) {
		TypeSpecifierNode type(TypeCategory::MemberObjectPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_entity(owner);
		type.set_member_object_pointee(&int_type);
		return type;
	};
	TypeSpecifierNode base_member_pointer =
		make_member_object_pointer(base_owner);
	base_member_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_derived_member_reference =
		make_member_object_pointer(derived_owner);
	const_derived_member_reference.set_cv_qualifier(CVQualifier::Const);
	const_derived_member_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> member_object_plan =
		tryBuildCanonicalReferenceBindingPlan(
			base_member_pointer, const_derived_member_reference);
	REQUIRE(member_object_plan.has_value());
	CHECK(member_object_plan->is_valid);
	CHECK(member_object_plan->rank == ConversionRank::Conversion);
	CHECK(member_object_plan->kind == StandardConversionKind::PointerConversion);

	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(int_type));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(int_type));
	signature.setParameterTypes(std::move(parameters));
	auto make_member_function_pointer = [&signature](EntityId owner) {
		TypeSpecifierNode type(TypeCategory::MemberFunctionPointer,
			TypeQualifier::None, 64, Token{}, CVQualifier::None);
		type.set_member_class_entity(owner);
		type.set_function_signature(signature);
		return type;
	};
	TypeSpecifierNode base_member_function_pointer =
		make_member_function_pointer(base_owner);
	base_member_function_pointer.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	TypeSpecifierNode const_derived_member_function_reference =
		make_member_function_pointer(derived_owner);
	const_derived_member_function_reference.set_cv_qualifier(
		CVQualifier::Const);
	const_derived_member_function_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> member_function_plan =
		tryBuildCanonicalReferenceBindingPlan(
			base_member_function_pointer,
			const_derived_member_function_reference);
	REQUIRE(member_function_plan.has_value());
	CHECK(member_function_plan->is_valid);
	CHECK(member_function_plan->rank == ConversionRank::Conversion);
	CHECK(member_function_plan->kind == StandardConversionKind::PointerConversion);
}

TEST_CASE("Ordered function objects decay to pointers") {
	FrontendContext frontend;
	TypeSpecifierNode parameter(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode other_parameter(
		TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	FunctionSignature signature;
	signature.setReturnType(makeFunctionTypeFromSpecifier(parameter));
	OverloadVector<FunctionType, 4> parameters;
	parameters.push_back(makeFunctionTypeFromSpecifier(parameter));
	signature.setParameterTypes(std::move(parameters));
	FunctionSignature other_signature;
	other_signature.setReturnType(makeFunctionTypeFromSpecifier(parameter));
	OverloadVector<FunctionType, 4> other_parameters;
	other_parameters.push_back(makeFunctionTypeFromSpecifier(other_parameter));
	other_signature.setParameterTypes(std::move(other_parameters));

	TypeSpecifierNode function_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	function_object.set_ordered_declarator({
		DeclaratorComponent::function(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	function_object.set_function_signature(signature);

	TypeSpecifierNode function_pointer = function_object;
	function_pointer.prepend_ordered_declarator_component(
		DeclaratorComponent::pointer(CVQualifier::None));
	function_pointer.set_function_signature(signature);

	const ConversionPlan plan = buildConversionPlan(function_object, function_pointer);
	CHECK(plan.is_valid);
	CHECK(plan.rank == ConversionRank::ExactMatch);
	CHECK(plan.kind == StandardConversionKind::FunctionToPointer);
	const std::optional<ConversionPlan> canonical_plan =
		tryBuildCanonicalOrderedConversionPlan(function_object, function_pointer);
	REQUIRE(canonical_plan.has_value());
	CHECK(canonical_plan->is_valid);
	CHECK(canonical_plan->rank == ConversionRank::ExactMatch);
	CHECK(canonical_plan->kind == StandardConversionKind::FunctionToPointer);

	TypeSpecifierNode qualified_pointer = function_pointer;
	qualified_pointer.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::function(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	qualified_pointer.set_function_signature(signature);
	const ConversionPlan qualified_plan =
		buildConversionPlan(function_object, qualified_pointer);
	CHECK(qualified_plan.rank == ConversionRank::QualificationAdjustment);
	CHECK(qualified_plan.kind == StandardConversionKind::FunctionToPointer);

	TypeSpecifierNode mismatched_extent = function_pointer;
	mismatched_extent.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::function(),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	mismatched_extent.set_function_signature(signature);
	CHECK_FALSE(buildConversionPlan(function_object, mismatched_extent).is_valid);
	const std::optional<ConversionPlan> mismatched_canonical_plan =
		tryBuildCanonicalOrderedConversionPlan(function_object, mismatched_extent);
	REQUIRE(mismatched_canonical_plan.has_value());
	CHECK_FALSE(mismatched_canonical_plan->is_valid);

	TypeSpecifierNode mismatched_parameter = function_pointer;
	mismatched_parameter.set_function_signature(other_signature);
	CHECK_FALSE(buildConversionPlan(function_object, mismatched_parameter).is_valid);

	TypeSpecifierNode bool_target(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	const ConversionPlan bool_plan = buildConversionPlan(function_object, bool_target);
	CHECK(bool_plan.is_valid);
	CHECK(bool_plan.kind == StandardConversionKind::BooleanConversion);
}

TEST_CASE("Function signatures preserve non-projectable return declarators") {
	TypeSpecifierNode ordered_return(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	ordered_return.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	REQUIRE_FALSE(ordered_return.ordered_declarator_has_legacy_projection());

	const FunctionType function_return = makeFunctionTypeFromSpecifier(ordered_return);
	CHECK(std::ranges::equal(
		function_return.ordered_declarator_components,
		ordered_return.declarator_components()));
	const TypeSpecifierNode restored_return =
		typeSpecifierFromFunctionType(function_return);
	CHECK(restored_return.has_ordered_declarator());
	CHECK(std::ranges::equal(
		restored_return.declarator_components(),
		ordered_return.declarator_components()));

	TypeSpecifierNode different_return = ordered_return;
	different_return.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	});
	const FunctionType mismatched_function_return =
		makeFunctionTypeFromSpecifier(different_return);
	CHECK_FALSE(FlashCpp::equalFunctionTypeIdentity(
		function_return, mismatched_function_return));
}

TEST_CASE("Deep callable signature identity and hashing use bounded native stack") {
	constexpr size_t signature_depth = 8192;
	const auto make_nested_signature = [](
		TypeCategory leaf_type, size_t depth_limit) {
		auto signature = std::make_shared<FunctionSignature>();
		signature->return_type_index = nativeTypeIndex(leaf_type);
		for (size_t depth = 0; depth < depth_limit; ++depth) {
			FunctionType nested_type;
			nested_type.type_index = nativeTypeIndex(TypeCategory::Int);
			nested_type.callable_signature = signature;

			FunctionSignature enclosing_signature;
			if ((depth & 1u) == 0) {
				enclosing_signature.setReturnType(std::move(nested_type));
			} else {
				FunctionType return_type;
				return_type.type_index = nativeTypeIndex(TypeCategory::Int);
				enclosing_signature.setReturnType(std::move(return_type));
				OverloadVector<FunctionType, 4> parameter_types;
				parameter_types.push_back(std::move(nested_type));
				enclosing_signature.setParameterTypes(std::move(parameter_types));
			}
			signature = std::make_shared<FunctionSignature>(
				std::move(enclosing_signature));
		}
		return signature;
	};

	const std::shared_ptr<FunctionSignature> lhs =
		make_nested_signature(TypeCategory::Int, signature_depth);
	const std::shared_ptr<FunctionSignature> equivalent =
		make_nested_signature(TypeCategory::Int, signature_depth);
	const std::shared_ptr<FunctionSignature> different_leaf =
		make_nested_signature(TypeCategory::Float, signature_depth);

	CHECK(FlashCpp::equalFunctionSignatureIdentity(*lhs, *equivalent));
	CHECK(
		FlashCpp::hashFunctionSignatureIdentity(*lhs) ==
		FlashCpp::hashFunctionSignatureIdentity(*equivalent));
	CHECK_FALSE(FlashCpp::equalFunctionSignatureIdentity(*lhs, *different_leaf));

	FunctionType lhs_type;
	lhs_type.type_index = nativeTypeIndex(TypeCategory::Int);
	lhs_type.callable_signature = lhs;
	FunctionType equivalent_type;
	equivalent_type.type_index = nativeTypeIndex(TypeCategory::Int);
	equivalent_type.callable_signature = equivalent;
	CHECK(FlashCpp::equalFunctionTypeIdentity(lhs_type, equivalent_type));
	CHECK(
		FlashCpp::hashFunctionTypeIdentity(lhs_type) ==
		FlashCpp::hashFunctionTypeIdentity(equivalent_type));
}

TEST_CASE("Template substitution composes ordered callable return declarators") {
	FrontendContext frontend;
	const StringHandle template_name =
		StringTable::getOrInternStringHandle("T");
	const Token template_token(
		Token::Type::Identifier, std::string_view("T"), 0, 0, 0);
	std::vector<TemplateParameterNode> template_params;
	template_params.emplace_back(template_name, template_token);

	TypeSpecifierNode argument_type(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	argument_type.add_pointer_level(CVQualifier::None);
	TemplateArgumentVector template_args;
	template_args.push_back(TemplateTypeArg(argument_type));

	FunctionType return_pattern;
	return_pattern.type_index = nativeTypeIndex(TypeCategory::Int);
	return_pattern.template_parameter_name = template_name;
	return_pattern.ordered_declarator_components = {
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	};
	FunctionSignature signature;
	signature.setReturnType(return_pattern);

	const FunctionSignature substituted = substituteTemplateFunctionSignatureTypes(
		signature, template_params, template_args);
	const std::vector<DeclaratorComponent> expected_components = {
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	};
	const FunctionType& substituted_return = substituted.return_type();
	CHECK(substituted_return.type_index.category() == TypeCategory::Int);
	CHECK(substituted_return.ordered_declarator_components == expected_components);

	const TypeSpecifierNode substituted_type =
		typeSpecifierFromFunctionType(substituted_return);
	const CanonicalTypeImport imported = importCanonicalType(
		frontend.canonicalTypes(), substituted_type);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	const CanonicalDeclaratorExport exported = exportCanonicalDeclarator(
		frontend.canonicalTypes(), imported.type);
	CHECK(exported.status == CanonicalTypeImportStatus::Supported);
	CHECK(exported.components == expected_components);

	TypeSpecifierNode pointer_to_array(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_to_array.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
	});
	const TemplateTypeArg pointer_to_array_arg(pointer_to_array);
	const TypeInfo::TemplateArgInfo stored_arg =
		toTemplateArgInfo(pointer_to_array_arg);
	const TemplateTypeArg restored_arg = toTemplateTypeArg(stored_arg);
	CHECK(restored_arg.pointee_array_declarator);
	const TypeSpecifierNode restored_arg_type =
		typeSpecifierFromTemplateTypeArgProjection(restored_arg);
	CHECK(restored_arg_type.has_pointee_array_declarator());
	CHECK(std::ranges::equal(
		restored_arg_type.array_dimensions(),
		pointer_to_array.array_dimensions()));

	constexpr size_t nested_callable_depth = 2048;
	FunctionSignature deep_signature = signature;
	for (size_t depth = 0; depth < nested_callable_depth; ++depth) {
		FunctionType nested_callable;
		nested_callable.type_index = nativeTypeIndex(TypeCategory::FunctionPointer);
		nested_callable.callable_signature =
			std::make_shared<FunctionSignature>(std::move(deep_signature));
		FunctionSignature outer_signature;
		outer_signature.setReturnType(std::move(nested_callable));
		deep_signature = std::move(outer_signature);
	}
	const FunctionSignature substituted_deep_signature =
		substituteTemplateFunctionSignatureTypes(
			deep_signature, template_params, template_args);
	const FunctionType* deepest_return =
		&substituted_deep_signature.return_type();
	for (size_t depth = 0; depth < nested_callable_depth; ++depth) {
		REQUIRE(deepest_return->callable_signature != nullptr);
		deepest_return =
			&deepest_return->callable_signature->return_type();
	}
	CHECK(deepest_return->ordered_declarator_components == expected_components);
}

TEST_CASE("Ordered pointer and array conversions reach bool") {
	TypeSpecifierNode bool_target(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);

	TypeSpecifierNode array_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	array_object.set_ordered_declarator({
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	const ConversionPlan array_plan = buildConversionPlan(array_object, bool_target);
	CHECK(array_plan.is_valid);
	CHECK(array_plan.kind == StandardConversionKind::BooleanConversion);

	TypeSpecifierNode pointer_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_object.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	const ConversionPlan pointer_plan = buildConversionPlan(pointer_object, bool_target);
	CHECK(pointer_plan.is_valid);
	CHECK(pointer_plan.kind == StandardConversionKind::BooleanConversion);

	TypeSpecifierNode member_object(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	member_object.set_ordered_declarator({
		DeclaratorComponent::memberPointer(EntityId{1}, false, CVQualifier::None),
	});
	const ConversionPlan member_pointer_plan =
		buildConversionPlan(member_object, bool_target);
	CHECK(member_pointer_plan.is_valid);
	CHECK(member_pointer_plan.kind == StandardConversionKind::BooleanConversion);
}

TEST_CASE("Projectable pointer and array conversions reach bool") {
	TypeSpecifierNode bool_target(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);

	TypeSpecifierNode pointer(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer.add_pointer_level(CVQualifier::None);
	const ConversionPlan pointer_plan = buildConversionPlan(pointer, bool_target);
	CHECK(pointer_plan.is_valid);
	CHECK(pointer_plan.kind == StandardConversionKind::BooleanConversion);

	TypeSpecifierNode array(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const size_t dimensions[] = {3};
	array.set_array_dimensions(dimensions);
	const ConversionPlan array_plan = buildConversionPlan(array, bool_target);
	CHECK(array_plan.is_valid);
	CHECK(array_plan.kind == StandardConversionKind::BooleanConversion);

	// A function pointer must not be rejected by the function-pointer arm,
	// which is reached before the primitive category fallback.
	TypeSpecifierNode function_pointer(
		TypeCategory::FunctionPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	const ConversionPlan function_pointer_plan =
		buildConversionPlan(function_pointer, bool_target);
	CHECK(function_pointer_plan.is_valid);
	CHECK(function_pointer_plan.kind == StandardConversionKind::BooleanConversion);

	// std::nullptr_t is not a [conv.bool] source.
	TypeSpecifierNode null_pointer(
		TypeCategory::Nullptr, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	CHECK_FALSE(buildConversionPlan(null_pointer, bool_target).is_valid);
}

TEST_CASE("Projectable nullptr conversions use canonical pointer rules") {
	FrontendContext context;
	TypeSpecifierNode null_pointer(
		TypeCategory::Nullptr, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode integer_pointer(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	integer_pointer.add_pointer_level(CVQualifier::None);
	const size_t initial_type_count = context.canonicalTypes().size();

	const std::optional<ConversionPlan> canonical_plan =
		tryBuildCanonicalProjectableConversionPlan(null_pointer, integer_pointer);
	REQUIRE(canonical_plan.has_value());
	CHECK(canonical_plan->is_valid);
	CHECK(canonical_plan->rank == ConversionRank::Conversion);
	CHECK(canonical_plan->kind == StandardConversionKind::PointerConversion);
	CHECK(context.canonicalTypes().size() == initial_type_count);

	const ConversionPlan parser_plan = buildConversionPlan(null_pointer, integer_pointer);
	CHECK(parser_plan.is_valid);
	CHECK(parser_plan.kind == StandardConversionKind::PointerConversion);
	CHECK_FALSE(buildConversionPlan(integer_pointer, null_pointer).is_valid);

	TypeSpecifierNode bool_target(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	CHECK_FALSE(buildConversionPlan(null_pointer, bool_target).is_valid);
}

TEST_CASE("Semantic peak excludes nonoverlapping allocations") {
	FrontendContext context;
	auto& builder = context.declarationBuilder();
	SymbolTable table;
	const auto request = makeFunctionDeclRequest(table.currentScopeId(),
		StringTable::getOrInternStringHandle("review_peak"), TelemetryTypeId{11}, TelemetryTypeId{21},
		FunctionDeclForm::Declaration, LanguageLinkage::CPlusPlus);
	{
		PublicationTransaction transaction(builder);
		auto prepared = builder.prepareFunctionPublication(request,table);
		REQUIRE(builder.commitFunctionPublication(prepared, transaction).status==PublishStatus::Created);
		transaction.rollback();
	}
	context.refreshSemanticDomainStats();
	const auto actual_peak = context.domainStats(AllocationDomain::Semantic).peak_bytes;
	context.canonicalTypes().builtin(CanonicalBuiltinKind::Int);
	context.refreshSemanticDomainStats();
	const auto stats = context.domainStats(AllocationDomain::Semantic);

	CHECK(stats.peak_bytes == actual_peak);
}

TEST_CASE("Canonical type core identity contract") {
	CHECK(CanonicalTypeTests::run() == 0);
}

TEST_CASE("Canonical adapter imports the source corpus at publication") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream input("tests/test_canonical_type_adapter_ret0.cpp");
	REQUIRE(input.is_open());
	const std::string code((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
	CompileContext test_context;
	test_context.setInputFile("tests/test_canonical_type_adapter_ret0.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, sema);
	REQUIRE(!parser.parse().is_error());
	CHECK(context.canonicalTypes().size() > 0);
	CHECK(context.declarationBuilder().canonicalDeclaratorRequests() > 0);
	bool found_record = false;
	bool found_enum = false;
	const CanonicalTypeTable& types = context.canonicalTypes();
	for (uint32_t index = 1; index <= types.size(); ++index) {
		if (types.node(TypeId{index}).kind == CanonicalTypeKind::Record) {
			found_record = true;
		}
		if (types.node(TypeId{index}).kind == CanonicalTypeKind::Enum) {
			found_enum = static_cast<bool>(types.enumEntity(TypeId{index}));
		}
	}
	CHECK(found_record);
	CHECK(found_enum);
}

TEST_CASE("DeclarationBuilder distinguishes nested function parameter signatures") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();

	FrontendContext context;
	const std::string code = R"(
void accept(void (*callback)(int));
void accept(void (*callback)(double));
)";
	CompileContext test_context;
	test_context.setInputFile("nested_function_parameter_signature.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, sema);
	REQUIRE(!parser.parse().is_error());

	DeclarationBuilder& builder = context.declarationBuilder();
	CHECK(builder.declarationCount() == 2u);
	CHECK(builder.entityCount() == 2u);
	CHECK(builder.canonicalDeclaratorRequests() == 4u);
	CHECK(builder.unmigratedDeclaratorRequests() == 0u);
}

TEST_CASE("Parser publishes namespace enum declarations through DeclarationBuilder") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();

	const std::string code = R"(
namespace canonical_enum_publication {
enum class Phase : unsigned short;
enum class Phase : unsigned short { cold = 3, hot = 7 };
}
)";
	FrontendContext context;
	CompileContext test_context;
	test_context.setInputFile("canonical_enum_publication.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, sema);
	REQUIRE(!parser.parse().is_error());
	REQUIRE(context.declarationCount() == 2u);
	REQUIRE(context.entityCount() == 1u);

	DeclarationBuilder& builder = context.declarationBuilder();
	const DeclarationRecord& forward = builder.declaration(DeclId{1});
	const DeclarationRecord& definition = builder.declaration(DeclId{2});
	const EntityRecord& entity = builder.entity(EntityId{1});
	CHECK(forward.kind == static_cast<uint8_t>(DeclKind::Enum));
	CHECK(definition.kind == static_cast<uint8_t>(DeclKind::Enum));
	CHECK(forward.entity_id == entity.id);
	CHECK(definition.entity_id == entity.id);
	CHECK(definition.previous_decl_id == forward.id);
	CHECK((entity.flags & DeclarationFlags::IsDefinition) != 0);
	const CanonicalEnumLayout layout = context.canonicalTypes().enumLayout(entity.id);
	CHECK(hasCanonicalEnumLayoutFlag(layout.flags, CanonicalEnumLayoutFlags::Scoped));
	CHECK(hasCanonicalEnumLayoutFlag(
		layout.flags, CanonicalEnumLayoutFlags::FixedUnderlying));
	CHECK(layout.underlying_type ==
		context.canonicalTypes().builtin(CanonicalBuiltinKind::UnsignedShort));
	CHECK(layout.enumerator_count == 2);
}

TEST_CASE("Canonical adapter preserves supported identity and defers entire unsupported shapes") {
	FrontendContext context;
	auto& types = context.canonicalTypes();
	TypeSpecifierNode character(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	TypeSpecifierNode signed_character(TypeCategory::Char, TypeQualifier::Signed, 8, Token{}, CVQualifier::None);
	CHECK(context.declarationBuilder().internDeclaratorType(character) !=
		context.declarationBuilder().internDeclaratorType(signed_character));
	TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	integer.add_pointer_level(CVQualifier::Volatile);
	const auto imported = importCanonicalType(types, integer);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(imported.type == types.qualify(types.pointer(types.qualify(types.builtin(CanonicalBuiltinKind::Int),
		CVQualifier::Const)), CVQualifier::Volatile));
	TypeSpecifierNode array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<size_t, 2> dimensions{2, 3};
	array.set_array_dimensions(dimensions);
	const auto array_import = importCanonicalType(types, array);
	REQUIRE(array_import.status == CanonicalTypeImportStatus::Supported);
	CHECK(array_import.type == types.array(types.array(types.builtin(CanonicalBuiltinKind::Int), 3), 2));
	const auto parameter_import = importCanonicalFunctionParameterType(types, array);
	REQUIRE(parameter_import.status == CanonicalTypeImportStatus::Supported);
	CHECK(parameter_import.type == types.pointer(types.array(types.builtin(CanonicalBuiltinKind::Int), 3)));
	const auto before = types.size();
	integer.set_pack_expansion(true);
	CHECK(importCanonicalType(types, integer).status == CanonicalTypeImportStatus::Unresolved);
	CHECK(types.size() == before);
}

TEST_CASE("Frontend scratch rolls back nested declaration and canonical registries together") {
	FrontendContext context;
	auto& builder = context.declarationBuilder();
	auto& template_decls = context.templateDecls();
	const auto stable = context.canonicalTypes().builtin(CanonicalBuiltinKind::Double);
	const TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const OwnerId template_owner{1};
	const StringHandle template_name =
		StringTable::getOrInternStringHandle("ScratchTemplate");
	const OwnerId stable_template_owner{2};
	const StringHandle stable_template_name =
		StringTable::getOrInternStringHandle("StableTemplate");
	const TemplateDeclId stable_template = template_decls.publishPrimaryClassTemplate(
		stable_template_owner, stable_template_name);
	TypeSpecifierNode stable_pattern(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	TypeSpecifierNode tentative_pattern(
		TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	template_decls.attachPrimaryClassTemplatePattern(
		stable_template, ASTNode(&stable_pattern));
	{
		auto outer = context.beginScratchTransaction();
		{
			auto inner = context.beginScratchTransaction();
			builder.internDeclaratorType(integer);
			template_decls.publishPrimaryClassTemplate(template_owner, template_name);
			template_decls.attachPrimaryClassTemplatePattern(
				stable_template, ASTNode(&tentative_pattern));
			inner.commit();
		}
		CHECK(builder.telemetryDeclaratorInternCount() == 1);
		CHECK(template_decls.findPrimaryClassTemplate(
			template_owner, template_name).has_value());
		CHECK(template_decls.primaryClassTemplatePattern(
			stable_template)->raw_pointer() == &tentative_pattern);
		outer.rollback();
	}
	CHECK(builder.telemetryDeclaratorInternCount() == 0);
	CHECK_FALSE(template_decls.findPrimaryClassTemplate(
		template_owner, template_name).has_value());
	CHECK(template_decls.primaryClassTemplatePattern(
		stable_template)->raw_pointer() == &stable_pattern);
	CHECK(context.canonicalTypes().size() == 1);
	CHECK(context.canonicalTypes().builtin(CanonicalBuiltinKind::Double) == stable);
	CHECK(builder.internDeclaratorType(integer).value == 1);
}
