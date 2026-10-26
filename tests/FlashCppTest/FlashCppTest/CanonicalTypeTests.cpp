#include <fstream>
#include "CompilerIncludes.h"
#include "doctest.h"
#include "CanonicalTypeAdapter.h"
#include "ExpressionSubstitutor.h"
#include "ParserTemplateClassShared.h"
#include "../../architecture/CanonicalTypeTests.h"

namespace {
void publishTestClassBases(CanonicalTypeTable& table, EntityId entity, std::span<const CanonicalRecordBase> record_bases) {
	std::vector<CanonicalClassBase> bases;
	for (const CanonicalRecordBase& base : record_bases) {
		bases.push_back({table.record(base.entity), base.access, base.flags, 0, base.offset_bytes});
	}
	table.publishClassBaseSchema(table.record(entity), bases);
}
}

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

TEST_CASE("Binary operator call targets remain stable across context growth") {
	static_assert(sizeof(BinaryOperatorCallTarget) == 32);
	FrontendContext context;
	const DomainByteStats before = context.semanticAccounting().snapshot();
	const BinaryOperatorCallTarget first_target{StringTable::getOrInternStringHandle("operator_plus_probe"), CanonicalTypeId{17},
		{CanonicalTypeId{18}, CanonicalTypeId{19}}};
	const BinaryOperatorCallTargetId first_id = context.publishBinaryOperatorCallTarget(first_target);
	for (uint32_t index = 0; index < 2048; ++index) {
		context.publishBinaryOperatorCallTarget({StringTable::getOrInternStringHandle("operator_growth_probe"), CanonicalTypeId{20},
			{CanonicalTypeId{21}, CanonicalTypeId{22}}});
	}
	const BinaryOperatorCallTarget copied_target = context.binaryOperatorCallTarget(first_id);
	CHECK(copied_target.symbol == first_target.symbol);
	CHECK(copied_target.return_type == first_target.return_type);
	CHECK(copied_target.parameter_types == first_target.parameter_types);
	CHECK(context.semanticAccounting().snapshot().current_bytes == before.current_bytes + 2049 * sizeof(BinaryOperatorCallTarget));
	CHECK_THROWS_AS(context.binaryOperatorCallTarget(BinaryOperatorCallTargetId{}), InternalError);
	CHECK_THROWS_AS(context.binaryOperatorCallTarget(BinaryOperatorCallTargetId{2050}), InternalError);
}

TEST_CASE("Binary operator resolution copies call target IDs and clears them on selection changes") {
	const BinaryOperatorCallTargetId target_id{41};
	BinaryOperatorNode annotated(Token{}, ASTNode{}, ASTNode{});
	annotated.set_resolved_operator_call_target(target_id);
	BinaryOperatorNode copied(Token{}, ASTNode{}, ASTNode{});
	copied.copy_semantic_operator_resolution_from(annotated);
	CHECK(copied.resolved_operator_call_target() == target_id);

	annotated.set_no_match_operator_overload();
	CHECK_FALSE(annotated.resolved_operator_call_target());
	annotated.set_resolved_operator_call_target(target_id);
	annotated.set_ambiguous_operator_overload();
	CHECK_FALSE(annotated.resolved_operator_call_target());
	annotated.set_resolved_operator_call_target(target_id);
	annotated.set_resolved_member_operator_overload(nullptr, TypeId{}, TypeIndex{});
	CHECK_FALSE(annotated.resolved_operator_call_target());
	annotated.set_resolved_operator_call_target(target_id);
	annotated.set_resolved_free_function_operator_overload(nullptr);
	CHECK_FALSE(annotated.resolved_operator_call_target());
	annotated.set_resolved_operator_call_target(target_id);
	annotated.clear_semantic_operator_resolution();
	CHECK_FALSE(annotated.resolved_operator_call_target());
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

TEST_CASE("Pointer to unsized outer array identity carries every bound") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext test_context;
	test_context.setInputFile("canonical_pointer_to_unsized_outer_array_identity.cpp");
	SemanticAnalysis sema(test_context, gSymbolTable);
	const CanonicalTypeTable& table = context.canonicalTypes();

	TypeSpecifierNode pointer_to_unsized_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_to_unsized_array.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::unknownBoundArray(),
		DeclaratorComponent::array(3),
	});
	REQUIRE(pointer_to_unsized_array.has_unsized_outer_array_dimension());
	REQUIRE(pointer_to_unsized_array.has_pointee_array_declarator());

	const CanonicalTypeId pointer_id =
		sema.canonicalizeTypeForImplicitConversion(pointer_to_unsized_array);
	const CanonicalTypeDesc& pointer_desc = sema.typeContext().get(pointer_id);
	REQUIRE(static_cast<bool>(pointer_desc.structural_type_id));
	const CanonicalTypeNode pointer = table.node(pointer_desc.structural_type_id);
	CHECK(pointer.kind == CanonicalTypeKind::Pointer);
	const CanonicalTypeNode outer = table.node(pointer.child);
	CHECK(outer.kind == CanonicalTypeKind::Array);
	CHECK(!hasCanonicalTypeNodeFlag(outer.flags, CanonicalTypeNodeFlags::KnownArrayBound));
	const CanonicalTypeNode inner = table.node(outer.child);
	CHECK(inner.kind == CanonicalTypeKind::Array);
	CHECK(hasCanonicalTypeNodeFlag(inner.flags, CanonicalTypeNodeFlags::KnownArrayBound));
	CHECK(inner.array_extent == 3);
	const CanonicalTypeNode element = table.node(inner.child);
	CHECK(element.kind == CanonicalTypeKind::Builtin);
	CHECK(element.builtin == CanonicalBuiltinKind::Int);

	// The same bound retention applies to an ordinary array with an unknown
	// outer bound (int[][3]); the unknown wrap goes outside the known extent.
	TypeSpecifierNode ordinary_unsized_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	ordinary_unsized_array.set_array(true);
	ordinary_unsized_array.set_array_dimensions(std::array<size_t, 1>{3});
	ordinary_unsized_array.set_unsized_outer_array_dimension(true);
	const CanonicalTypeId ordinary_id =
		sema.canonicalizeTypeForImplicitConversion(ordinary_unsized_array);
	const CanonicalTypeDesc& ordinary_desc = sema.typeContext().get(ordinary_id);
	REQUIRE(static_cast<bool>(ordinary_desc.structural_type_id));
	const CanonicalTypeNode ordinary_outer = table.node(ordinary_desc.structural_type_id);
	CHECK(ordinary_outer.kind == CanonicalTypeKind::Array);
	CHECK(!hasCanonicalTypeNodeFlag(ordinary_outer.flags, CanonicalTypeNodeFlags::KnownArrayBound));
	const CanonicalTypeNode ordinary_inner = table.node(ordinary_outer.child);
	CHECK(ordinary_inner.kind == CanonicalTypeKind::Array);
	CHECK(hasCanonicalTypeNodeFlag(ordinary_inner.flags, CanonicalTypeNodeFlags::KnownArrayBound));
	CHECK(ordinary_inner.array_extent == 3);
}

TEST_CASE("Pointer-to-array shape prefers structural identity over the flat projection") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext test_context;
	test_context.setInputFile("canonical_pointer_to_array_shape.cpp");
	SemanticAnalysis sema(test_context, gSymbolTable);
	CanonicalTypeTable& table = context.canonicalTypes();

	// A published structural id is authoritative even when the flat projection
	// disagrees with it.
	const TypeId int_id = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId pointer_to_unsized_rows = table.pointer(table.arrayOfUnknownBound(table.array(int_id, 3)));
	CanonicalTypeDesc structural;
	structural.type_index = nativeTypeIndex(TypeCategory::Int);
	structural.structural_type_id = pointer_to_unsized_rows;
	structural.pointee_array_declarator = false;
	structural.pointer_levels.push_back(PointerLevel{});
	structural.array_dimensions.push_back(99);
	const PointerToArrayShape from_structural = sema.pointerToArrayShape(structural);
	CHECK(from_structural.is_pointer_to_array);
	CHECK(from_structural.has_unsized_outer_array_dimension);
	REQUIRE(from_structural.known_dimensions.size() == 1);
	CHECK(from_structural.known_dimensions[0] == 3);

	// A descriptor without a structural id is not a pointer-to-array, and a
	// pointer-to-array descriptor that never published identity fails closed
	// rather than reading the flat projection.
	CanonicalTypeDesc flat;
	flat.type_index = nativeTypeIndex(TypeCategory::Int);
	flat.pointer_levels.push_back(PointerLevel{});
	const PointerToArrayShape not_published = sema.pointerToArrayShape(flat);
	CHECK(!not_published.is_pointer_to_array);
	flat.pointee_array_declarator = true;
	flat.array_dimensions.push_back(4);
	CHECK_THROWS_AS(sema.pointerToArrayShape(flat), InternalError);

	// An ordinary array structural id is not a pointer-to-array.
	CanonicalTypeDesc ordinary;
	ordinary.structural_type_id = table.arrayOfUnknownBound(table.array(int_id, 3));
	const PointerToArrayShape not_pointer = sema.pointerToArrayShape(ordinary);
	CHECK(!not_pointer.is_pointer_to_array);
}

TEST_CASE("Published pointer and array materialization follows canonical identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext test_context;
	test_context.setInputFile("canonical_pointer_array_materialization.cpp");
	SemanticAnalysis sema(test_context, gSymbolTable);
	CanonicalTypeTable& table = context.canonicalTypes();
	const Token token(Token::Type::Identifier, std::string_view("materialized_cells"), 0, 0, 0);
	const ASTNode expression = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(token));

	TypeSpecifierNode cells(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::Const);
	cells.set_ordered_declarator({DeclaratorComponent::pointer(CVQualifier::Volatile),
		DeclaratorComponent::unknownBoundArray(), DeclaratorComponent::array(3)});
	const CanonicalTypeId expected_id = sema.canonicalizeTypeForImplicitConversion(cells);
	const CanonicalTypeDesc& expected = sema.typeContext().get(expected_id);
	REQUIRE(hasFlag(expected.flags, CanonicalTypeFlags::PublishedPointerArray));
	const TypeId expected_type = expected.structural_type_id;
	sema.registerCodegenSynthesizedLocalType(token.handle(), cells);
	// Fault injection at the compatibility boundary: stale legacy fields must
	// not override a published identity when a consumer materializes its shape.
	// Mutate the stored value, not TypeContext's separate hash-index key, and do
	// not re-intern it (intern deliberately refreshes flat-writer publication).
	CanonicalTypeDesc& stale_projection = const_cast<CanonicalTypeDesc&>(sema.typeContext().get(expected_id));
	stale_projection.type_index = nativeTypeIndex(TypeCategory::Int);
	stale_projection.base_cv = CVQualifier::Volatile;
	stale_projection.pointer_levels.clear();
	stale_projection.array_dimensions.assign(1, 99);
	stale_projection.pointee_array_declarator = false;
	stale_projection.has_unsized_outer_array_dimension = false;
	const TypeSpecifierQueryResult query = sema.analyzeExpressionTypeQuery(expression);
	REQUIRE(query.hasValue());
	CHECK(query.type->has_pointee_array_declarator());
	CHECK(query.type->has_unsized_outer_array_dimension());
	CHECK(query.type->cv_qualifier() == CVQualifier::Const);
	const CanonicalTypeImport imported = importCanonicalType(table, *query.type);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(canonicalTypeWithoutReference(table, imported.type) == expected_type);

	// A flat writer explicitly publishing its next descriptor version must
	// still refresh that identity, rather than keeping the old pointer shape.
	TypeContext refreshed_types;
	const CanonicalTypeId refreshed_id = refreshed_types.intern(stale_projection);
	const TypeId refreshed_type = refreshed_types.get(refreshed_id).structural_type_id;
	CHECK(refreshed_type != expected_type);
	const CanonicalTypeNode refreshed = table.node(refreshed_type);
	CHECK(refreshed.kind == CanonicalTypeKind::Array);
	CHECK(refreshed.array_extent == 99);
}

TEST_CASE("Published pointer materialization preserves deep declarator identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext test_context;
	test_context.setInputFile("canonical_deep_pointer_materialization.cpp");
	SemanticAnalysis sema(test_context, gSymbolTable);
	TypeSpecifierNode pointer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	for (size_t depth = 0; depth < 4096; ++depth) {
		pointer.add_pointer_level(CVQualifier::None);
	}
	const CanonicalTypeId expected_id = sema.canonicalizeTypeForImplicitConversion(pointer);
	const TypeId expected_type = sema.typeContext().get(expected_id).structural_type_id;
	REQUIRE(expected_type);
	const Token token(Token::Type::Identifier, std::string_view("deep_materialized_pointer"), 0, 0, 0);
	const ASTNode expression = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(token));
	sema.registerCodegenSynthesizedLocalType(token.handle(), pointer);
	const TypeSpecifierQueryResult query = sema.analyzeExpressionTypeQuery(expression);
	REQUIRE(query.hasValue());
	CanonicalTypeTable& table = context.canonicalTypes();
	const CanonicalTypeImport imported = importCanonicalType(table, *query.type);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(canonicalTypeWithoutReference(table, imported.type) == expected_type);
}

TEST_CASE("Struct member TypeId materialization preserves deep pointer qualifiers") {
	FrontendContext context;
	TypeSpecifierNode full_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	for (size_t index = 0; index < 4096; ++index) {
		full_type.add_pointer_level(static_cast<CVQualifier>(index % 4));
	}
	const TypeId member_type = importCanonicalStructMemberType(context.canonicalTypes(), full_type);
	REQUIRE(member_type);
	StructMember member(StringHandle{}, nativeTypeIndex(TypeCategory::Int), member_type, 0, 8, 8,
		AccessSpecifier::Public, std::nullopt, ReferenceQualifier::None, 64, false, {}, false, 4096,
		std::nullopt, CVQualifier::None);
	const TypeSpecifierNode projected = typeSpecifierFromStructMemberProjection(member);
	CHECK(projected.cv_qualifier() == CVQualifier::Const);
	REQUIRE(projected.pointer_levels().size() == 4096);
	for (size_t index = 0; index < projected.pointer_levels().size(); ++index) {
		CHECK(projected.pointer_levels()[index].cv_qualifier == static_cast<CVQualifier>(index % 4));
	}
}

TEST_CASE("Canonical identity is independent of interning and string order") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const auto build = [&table]() {
		const TypeId param = table.templateParameter(TemplateDeclId{3}, 0);
		const TypeId member = table.dependentName(param, "value");
		const TypeId rows = table.arrayOfUnknownBound(table.array(member, 4));
		return table.pointer(rows);
	};
	const TypeId first = build();
	(void)table.templateSpecialization(TemplateDeclId{9}, std::span<const TypeId>(&first, 1));
	StringTable::getOrInternStringHandle("canonical_identity_order_probe");
	const TypeId second = build();
	CHECK(second == first);
	CHECK(table.node(second).child == table.node(first).child);
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
	table.publishClassBaseSchema(table.record(base_entity), {});
	const std::array<CanonicalClassBase, 1> class_bases{{{table.record(base_entity), CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
	table.publishClassBaseSchema(table.record(derived_entity), class_bases);

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

TEST_CASE("TypeId inheritance classification requires published class-base schemas") {
	using Classifier = decltype(&classifyCanonicalDerivedBaseConversion);
	static_assert(std::is_invocable_v<Classifier, const CanonicalTypeTable&, TypeId, TypeId>);
	static_assert(!std::is_invocable_v<Classifier, const CanonicalTypeTable&, EntityId, EntityId>);
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_entity{98501};
	const EntityId derived_entity{98502};
	const TypeId base = table.record(base_entity);
	const TypeId derived = table.record(derived_entity);
	const TypeId intermediate = table.record(EntityId{98503});
	const std::array<CanonicalClassBase, 1> base_link{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
	const std::array<CanonicalClassBase, 1> intermediate_link{{intermediate, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
	auto publish_entity_schema = [&](bool related) {
		table.publishRecordLayout({base_entity, 1, 1, 1, 1, 0, 0, CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(base_entity, {}, {});
		table.publishRecordLayout({derived_entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(related), CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishRecordFieldSchema(derived_entity, {},
			related ? std::span<const CanonicalRecordBase>(bases) : std::span<const CanonicalRecordBase>{});
	};
	CHECK(classifyCanonicalDerivedBaseConversion(table, base, base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
	CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, TypeId{}, base).has_value());
	CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, table.builtin(CanonicalBuiltinKind::Int), base).has_value());
	SUBCASE("Absent graphs defer") {
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
	}
	SUBCASE("EntityId-only proof cannot classify a TypeId pair") {
		publish_entity_schema(true);
		CHECK(table.recordLayout(derived_entity).direct_base_count == 1);
		CHECK(table.recordBaseAt(derived_entity, 0).entity == base_entity);
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
	}
	SUBCASE("Missing root defers despite EntityId proof") {
		publish_entity_schema(true);
		table.publishClassBaseSchema(base, {});
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
	}
	SUBCASE("Missing target defers despite a published direct edge") {
		table.publishClassBaseSchema(derived, base_link);
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
	}
	SUBCASE("Missing target defers before declaring types unrelated") {
		table.publishClassBaseSchema(derived, {});
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
	}
	SUBCASE("Specialization targets require their own schema") {
		const std::array<CanonicalTemplateArgument, 1> arguments{{CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Int))}};
		const TypeId specialized_base = table.templateSpecialization(TemplateDeclId{98504}, arguments);
		const std::array<CanonicalClassBase, 1> bases{{specialized_base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishClassBaseSchema(derived, bases);
		CHECK(classifyCanonicalDerivedBaseConversion(table, specialized_base, specialized_base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, specialized_base).has_value());
		table.publishClassBaseSchema(specialized_base, {});
		CHECK(classifyCanonicalDerivedBaseConversion(table, derived, specialized_base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
	}
	SUBCASE("Missing intermediate defers") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, intermediate_link);
		CHECK_FALSE(classifyCanonicalDerivedBaseConversion(table, derived, base).has_value());
		table.publishClassBaseSchema(intermediate, base_link);
		CHECK(classifyCanonicalDerivedBaseConversion(table, derived, base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
	}
	SUBCASE("Published unrelated graph overrides contradictory EntityId proof") {
		publish_entity_schema(true);
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, {});
		CHECK(classifyCanonicalDerivedBaseConversion(table, derived, base) == DerivedBaseConversionKind::NotRelated);
	}
	SUBCASE("Published relation overrides unrelated EntityId graph") {
		publish_entity_schema(false);
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, base_link);
		CHECK(table.recordLayout(derived_entity).direct_base_count == 0);
		CHECK(classifyCanonicalDerivedBaseConversion(table, derived, base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
	}
	const size_t types_before = table.size();
	(void)classifyCanonicalDerivedBaseConversion(table, derived, base);
	CHECK(table.size() == types_before);
}

TEST_CASE("Explicit copy initialization diagnostics retain the initializer location") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("explicit_constructor_location.cpp");
	Lexer lexer("struct Box { explicit Box(int); };\nint main() { Box object = 7; return 0; }\n");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	try {
		sema.run();
		FAIL("copy initialization must reject the explicit constructor");
	} catch (const CompileError& error) {
		const Diagnostic* diagnostic = error.structuredDiagnostic();
		REQUIRE(diagnostic != nullptr);
		CHECK(diagnostic->id == DiagnosticId::ExplicitConstructorCopyInitialization);
		CHECK(diagnostic->location.line == 2);
		CHECK(diagnostic->location.column > 0);
	}
}

TEST_CASE("Constructor cloning retains declaration flags independently of noexcept") {
	FrontendContext frontend;
	const StringHandle pattern_name = StringTable::getOrInternStringHandle("Pattern");
	const StringHandle instance_name = StringTable::getOrInternStringHandle("Instance");
	ConstructorDeclarationNode pattern(pattern_name, pattern_name);
	ConstructorDeclarationNode instance(instance_name, instance_name);
	pattern.set_explicit(true);
	pattern.set_constexpr(true);
	pattern.set_is_inline(true);
	pattern.set_noexcept(true);
	instance.set_is_implicit(true);
	instance.set_is_explicitly_defaulted(true);
	instance.set_was_defaulted_on_first_declaration(true);
	copyConstructorDeclarationFlags(pattern, instance);
	CHECK(instance.is_explicit());
	CHECK(instance.is_constexpr());
	CHECK(instance.is_inline());
	CHECK_FALSE(instance.is_implicit());
	CHECK_FALSE(instance.is_explicitly_defaulted());
	CHECK_FALSE(instance.was_defaulted_on_first_declaration());
	CHECK_FALSE(instance.is_noexcept());
	CHECK(instance.struct_name() == instance_name);
}

TEST_CASE("Nominal object conversion queries require published class graphs") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_entity{98501};
	const EntityId derived_entity{98502};
	const TypeId base = table.record(base_entity);
	const TypeId derived = table.record(derived_entity);
	TypeSpecifierNode source(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	source.set_type_entity(derived_entity);
	TypeSpecifierNode target(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	target.set_type_entity(base_entity);
	auto check_standard = [&]() {
		const size_t types_before = table.size();
		const auto plan = tryBuildCanonicalProjectableConversionPlan(source, target);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid);
		CHECK(plan->rank == ConversionRank::Conversion);
		CHECK(plan->kind == StandardConversionKind::DerivedToBase);
		CHECK(table.size() == types_before);
	};
	SUBCASE("Absent schemas stay deferred") {
		CHECK_FALSE(tryBuildCanonicalProjectableConversionPlan(source, target).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("Entity-only graphs cannot stand in for class schemas") {
		table.publishRecordLayout(CanonicalRecordLayout{base_entity, 1, 1, 1, 1, 0, 0, CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(base_entity, {}, {});
		table.publishRecordLayout(CanonicalRecordLayout{derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishRecordFieldSchema(derived_entity, {}, bases);
		CHECK_FALSE(tryBuildCanonicalProjectableConversionPlan(source, target).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("Public and virtual class graphs produce standard conversions") {
		CanonicalRecordBaseFlags flags = CanonicalRecordBaseFlags::None;
		SUBCASE("Nonvirtual") {}
		SUBCASE("Virtual") { flags = CanonicalRecordBaseFlags::Virtual; }
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, flags, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		check_standard();
	}
	SUBCASE("Unrelated and private relations defer to user-defined selection") {
		table.publishClassBaseSchema(base, {});
		SUBCASE("Unrelated") { table.publishClassBaseSchema(derived, {}); }
		SUBCASE("Private") {
			const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0, 0}}};
			table.publishClassBaseSchema(derived, bases);
		}
		CHECK_FALSE(tryBuildCanonicalProjectableConversionPlan(source, target).has_value());
	}
	SUBCASE("Deep published class graphs use the iterative classifier") {
		table.publishClassBaseSchema(base, {});
		TypeId previous = base;
		for (uint32_t depth = 0; depth < 4096; ++depth) {
			const TypeId next = table.record(EntityId{120000 + depth});
			const std::array<CanonicalClassBase, 1> bases{{{previous, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
			table.publishClassBaseSchema(next, bases);
			previous = next;
		}
		source.set_type_entity(table.recordEntity(previous));
		check_standard();
	}
}

TEST_CASE("Record pointer conversion queries consume published TypeId class graphs") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId base_entity{98601};
	const EntityId derived_entity{98602};
	const TypeId base = table.record(base_entity);
	const TypeId derived = table.record(derived_entity);
	const TypeId intermediate = table.record(EntityId{98603});
	TypeSpecifierNode source(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	source.set_type_entity(derived_entity);
	source.add_pointer_level(CVQualifier::None);
	TypeSpecifierNode target(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	target.set_type_entity(base_entity);
	target.add_pointer_level(CVQualifier::None);
	const std::array<CanonicalClassBase, 1> public_bases{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
	auto check_plan = [&](bool valid) {
		const size_t types_before = table.size();
		const auto plan = tryBuildCanonicalProjectableConversionPlan(source, target);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid == valid);
		CHECK(buildConversionPlan(source, target).is_valid == valid);
		if (valid) {
			CHECK(plan->rank == ConversionRank::Conversion);
			CHECK(plan->kind == StandardConversionKind::DerivedToBase);
		}
		CHECK(table.size() == types_before);
	};
	auto publish_entity_graph = [&](bool related) {
		table.publishRecordLayout({base_entity, 1, 1, 1, 1, 0, 0, CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(base_entity, {}, {});
		table.publishRecordLayout({derived_entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(related), CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishRecordFieldSchema(derived_entity, {},
			related ? std::span<const CanonicalRecordBase>(bases) : std::span<const CanonicalRecordBase>{});
	};
	SUBCASE("Absent class graph fails closed despite EntityId proof") {
		publish_entity_graph(true);
		check_plan(false);
		CHECK_FALSE(table.hasClassBaseSchema(base));
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("Published public graph overrides absent or contradictory EntityId facts") {
		SUBCASE("No EntityId schemas") {}
		SUBCASE("EntityId says unrelated") { publish_entity_graph(false); }
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, public_bases);
		check_plan(true);
	}
	SUBCASE("Public virtual base uses the TypeId graph") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::Virtual, 0, 0}};
		table.publishClassBaseSchema(derived, bases);
		check_plan(true);
	}
	SUBCASE("Private protected and unrelated TypeId graphs override EntityId proof") {
		publish_entity_graph(true);
		table.publishClassBaseSchema(base, {});
		SUBCASE("Private") {
			const std::array<CanonicalClassBase, 1> bases{{base, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0, 0}};
			table.publishClassBaseSchema(derived, bases);
		}
		SUBCASE("Protected") {
			const std::array<CanonicalClassBase, 1> bases{{base, CanonicalAccess::Protected, CanonicalRecordBaseFlags::None, 0, 0}};
			table.publishClassBaseSchema(derived, bases);
		}
		SUBCASE("Unrelated") { table.publishClassBaseSchema(derived, {}); }
		check_plan(false);
	}
	SUBCASE("Missing endpoint or intermediate class facts cannot recover EntityId proof") {
		publish_entity_graph(true);
		SUBCASE("Root") { table.publishClassBaseSchema(base, {}); }
		SUBCASE("Target") { table.publishClassBaseSchema(derived, public_bases); }
		SUBCASE("Intermediate") {
			table.publishClassBaseSchema(base, {});
			const std::array<CanonicalClassBase, 1> bases{{intermediate, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
			table.publishClassBaseSchema(derived, bases);
		}
		check_plan(false);
	}
	SUBCASE("Ambiguous graph cannot use the unique EntityId path") {
		publish_entity_graph(true);
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(intermediate, public_bases);
		const std::array<CanonicalClassBase, 2> bases{{public_bases[0],
			{intermediate, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		check_plan(false);
	}
	SUBCASE("Pointee qualification is preserved") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, public_bases);
		SUBCASE("Add const") { target.set_cv_qualifier(CVQualifier::Const); check_plan(true); }
		SUBCASE("Drop const") { source.set_cv_qualifier(CVQualifier::Const); check_plan(false); }
		SUBCASE("Drop volatile") { source.set_cv_qualifier(CVQualifier::Volatile); check_plan(false); }
		SUBCASE("Keep const volatile") {
			source.set_cv_qualifier(CVQualifier::Const | CVQualifier::Volatile);
			target.set_cv_qualifier(CVQualifier::Const | CVQualifier::Volatile);
			check_plan(true);
		}
	}
	SUBCASE("Identical record pointers require no class schema") {
		target.set_type_entity(derived_entity);
		const auto plan = tryBuildCanonicalProjectableConversionPlan(source, target);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid);
		CHECK(plan->rank == ConversionRank::ExactMatch);
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("Class conversion cannot change nested pointer pointees") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, public_bases);
		source.add_pointer_level(CVQualifier::None);
		target.add_pointer_level(CVQualifier::None);
		check_plan(false);
	}
	SUBCASE("A specialization intermediate retains its TypeId identity") {
		const std::array<CanonicalTemplateArgument, 1> arguments{{CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Double))}};
		const TypeId specialization = table.templateSpecialization(TemplateDeclId{98604}, arguments);
		const std::array<CanonicalClassBase, 1> bases{{specialization, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(specialization, public_bases);
		table.publishClassBaseSchema(derived, bases);
		check_plan(true);
	}
	SUBCASE("Deep TypeId inheritance retains bounded native stack") {
		table.publishClassBaseSchema(base, {});
		TypeId previous = base;
		for (uint32_t depth = 0; depth < 4096; ++depth) {
			const TypeId next = table.record(EntityId{130000 + depth});
			const std::array<CanonicalClassBase, 1> bases{{previous, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
			table.publishClassBaseSchema(next, bases);
			previous = next;
		}
		source.set_type_entity(table.recordEntity(previous));
		check_plan(true);
	}
}

TEST_CASE("Specialization object conversion queries cannot publish missing base schemas") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CompileContext compile_context;
	compile_context.setInputFile("canonical_nominal_object_graph.cpp");
	Lexer lexer("struct Base {}; template<class T> struct Derived : Base {}; using Owner = Derived<long long>; static_assert(sizeof(Owner) > 0);");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	{
		CanonicalTypeTransaction publication(table);
		REQUIRE(!parser.parse().is_error());
	}
	const TypeInfo* owner_info = findTypeByName(StringTable::getOrInternStringHandle("Owner"));
	const TypeInfo* base_info = findTypeByName(StringTable::getOrInternStringHandle("Base"));
	REQUIRE(owner_info != nullptr);
	REQUIRE(base_info != nullptr);
	const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *owner_info);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	REQUIRE(table.node(imported.type).kind == CanonicalTypeKind::TemplateSpecialization);
	TypeSpecifierNode source(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	source.set_type_index(canonicalize_type_alias(owner_info->registeredTypeIndex()).resolvedTypeIndex());
	REQUIRE(source.category() == TypeCategory::Struct);
	const TypeSpecifierNode argument(TypeCategory::LongLong, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	source.set_template_specialization(table.templateSpecializationDecl(imported.type), {argument});
	TypeSpecifierNode target(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	target.set_type_index(base_info->registeredTypeIndex());
	tryBindPublishedTypeEntity(target);
	CHECK_FALSE(table.hasClassBaseSchema(imported.type));
	CHECK_FALSE(tryBuildCanonicalProjectableConversionPlan(source, target).has_value());
	CHECK_FALSE(table.hasClassBaseSchema(imported.type));
	REQUIRE(importCanonicalOverloadNominalType(table, source).type == imported.type);
	const TypeId base = importCanonicalType(table, target).type;
	table.publishClassBaseSchema(base, {});
	const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
	table.publishClassBaseSchema(imported.type, bases);
	const auto plan = tryBuildCanonicalProjectableConversionPlan(source, target);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(plan->kind == StandardConversionKind::DerivedToBase);
}

TEST_CASE("Canonical class base publication defers and later fills object facts") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_deferred_class_object_facts.cpp");
	Lexer lexer("struct Base {}; struct Derived : Base {};");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	const TypeInfo* derived_info = findTypeByName(StringTable::getOrInternStringHandle("Derived"));
	REQUIRE(derived_info != nullptr);
	const StructTypeInfo* derived_struct_info = canonicalClassStructInfoFromTypeInfo(*derived_info);
	REQUIRE(derived_struct_info != nullptr);

	CanonicalTypeTable deferred_table;
	const CanonicalTypeImport imported = importCanonicalClassTypeInfo(deferred_table, *derived_info);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	REQUIRE(tryPublishCanonicalClassBaseSchema(deferred_table, imported.type, *derived_struct_info, CanonicalClassSchemaPublication::BasesOnly));
	REQUIRE(deferred_table.hasClassBaseSchema(imported.type));
	REQUIRE(deferred_table.classBaseCount(imported.type) == 1);
	const TypeId base_type = deferred_table.classBaseAt(imported.type, 0).type;
	CHECK_FALSE(deferred_table.hasRecordProperties(imported.type));
	CHECK_FALSE(deferred_table.hasRecordSubobjectTypes(imported.type));
	CHECK_FALSE(deferred_table.hasRecordProperties(base_type));
	CHECK_FALSE(deferred_table.hasRecordSubobjectTypes(base_type));

	REQUIRE(tryPublishCanonicalClassBaseSchema(deferred_table, imported.type, *derived_struct_info));
	CHECK(deferred_table.hasRecordProperties(imported.type));
	CHECK(deferred_table.hasRecordSubobjectTypes(imported.type));
	CHECK(deferred_table.hasRecordProperties(base_type));
	CHECK(deferred_table.hasRecordSubobjectTypes(base_type));
	CHECK(deferred_table.classBaseCount(imported.type) == 1);
	CHECK(deferred_table.classBaseAt(imported.type, 0).type == base_type);
}

TEST_CASE("Canonical record constructor schema publishes by TypeId") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId entity{901};
	table.publishRecordLayout(CanonicalRecordLayout{
		entity, 4, 4, 4, 4, 0, 0, CanonicalRecordLayoutFlags::None, 0});
	const TypeId record_type = table.record(entity);

	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	const std::array<TypeId, 1> one_parameter{int_type};
	const std::array<CanonicalRecordConstructorSpec, 2> constructors{
		CanonicalRecordConstructorSpec{std::span<const TypeId>{},
			CanonicalRecordFunctionFlags::Implicit, true},
		CanonicalRecordConstructorSpec{
			std::span<const TypeId>(one_parameter.data(), one_parameter.size()),
			CanonicalRecordFunctionFlags::None, false},
	};
	table.publishRecordConstructors(record_type, constructors);

	REQUIRE(table.hasRecordConstructors(record_type));
	CHECK(table.recordConstructorCount(record_type) == 2);
	const CanonicalRecordConstructor default_constructor = table.recordConstructorAt(record_type, 0);
	CHECK(default_constructor.parameter_count == 0);
	CHECK(default_constructor.is_noexcept == 1);
	CHECK(default_constructor.flags == CanonicalRecordFunctionFlags::Implicit);
	const CanonicalRecordConstructor value_constructor = table.recordConstructorAt(record_type, 1);
	CHECK(value_constructor.parameter_count == 1);
	CHECK(value_constructor.is_noexcept == 0);
	CHECK(value_constructor.flags == CanonicalRecordFunctionFlags::None);
	CHECK(table.recordConstructorParameterAt(record_type, 1, 0) == int_type);

	// Equal republication is idempotent.
	table.publishRecordConstructors(record_type, constructors);
	CHECK(table.recordConstructorCount(record_type) == 2);
}

TEST_CASE("Canonical record constructor schema requires a class type") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CHECK_FALSE(table.hasRecordConstructors(TypeId{}));
	CHECK_FALSE(table.hasRecordConstructors(table.builtin(CanonicalBuiltinKind::Int)));
	CHECK_THROWS(table.publishRecordConstructors(
		TypeId{}, std::span<const CanonicalRecordConstructorSpec>{}));
	CHECK_THROWS(table.publishRecordConstructors(
		table.builtin(CanonicalBuiltinKind::Int),
		std::span<const CanonicalRecordConstructorSpec>{}));
}

TEST_CASE("Conversion function base tails consume published TypeId graphs") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CompileContext compile_context;
	compile_context.setInputFile("canonical_conversion_tail_graph.cpp");
	Lexer lexer("struct Base {}; struct Derived : Base {};");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	{
		CanonicalTypeTransaction publication(table);
		REQUIRE(!parser.parse().is_error());
	}
	auto class_spec = [](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		TypeSpecifierNode spec(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
		spec.set_type_index(info->registeredTypeIndex());
		tryBindPublishedTypeEntity(spec);
		REQUIRE(spec.has_type_entity());
		return spec;
	};
	const TypeSpecifierNode base_spec = class_spec("Base");
	const TypeSpecifierNode derived_spec = class_spec("Derived");
	const TypeId base = importCanonicalType(table, base_spec).type;
	const TypeId derived = importCanonicalType(table, derived_spec).type;
	TypeSpecifierNode reference_target = base_spec;
	reference_target.set_cv_qualifier(CVQualifier::Const);
	reference_target.set_reference_qualifier(ReferenceQualifier::LValueReference);
	auto check_plan = [](const std::optional<ConversionPlan>& plan, bool valid) {
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid == valid);
		if (valid) {
			CHECK(plan->kind == StandardConversionKind::DerivedToBase);
		}
	};
	auto publish_entity_graph = [&]() {
		const EntityId base_entity = table.recordEntity(base);
		const EntityId derived_entity = table.recordEntity(derived);
		table.publishRecordLayout(CanonicalRecordLayout{base_entity, 1, 1, 1, 1, 0, 0, CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(base_entity, {}, {});
		table.publishRecordLayout(CanonicalRecordLayout{derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishRecordFieldSchema(derived_entity, {}, bases);
	};
	SUBCASE("Missing class schemas defer despite legacy declarations") {
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec).has_value());
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("Entity schemas cannot substitute for missing class schemas") {
		publish_entity_graph();
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec).has_value());
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target).has_value());
	}
	SUBCASE("A published unrelated graph overrides a contradictory entity graph") {
		publish_entity_graph();
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, {});
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec), false);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target), false);
		CHECK_FALSE(tryBuildCanonicalReferenceBindingPlan(derived_spec, reference_target, CanonicalReferenceBindingContext::Initialization).has_value());
	}
	SUBCASE("Published public and virtual graphs support values and references") {
		CanonicalRecordBaseFlags flags = CanonicalRecordBaseFlags::None;
		SUBCASE("Nonvirtual") {}
		SUBCASE("Virtual") { flags = CanonicalRecordBaseFlags::Virtual; }
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, flags, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec), true);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target), true);
		reference_target.set_reference_qualifier(ReferenceQualifier::RValueReference);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target), true);
		TypeSpecifierNode lvalue_source = derived_spec;
		lvalue_source.set_reference_qualifier(ReferenceQualifier::LValueReference);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(lvalue_source, reference_target), false);
		reference_target.set_cv_qualifier(CVQualifier::None);
		reference_target.set_reference_qualifier(ReferenceQualifier::LValueReference);
		lvalue_source.set_cv_qualifier(CVQualifier::Const);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(lvalue_source, reference_target), false);
	}
	SUBCASE("Inaccessible tails rank before access checks") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec), true);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target), true);
		check_plan(tryBuildCanonicalReferenceBindingPlan(derived_spec, reference_target, CanonicalReferenceBindingContext::Initialization), false);
	}
	SUBCASE("A missing intermediate schema defers") {
		table.publishClassBaseSchema(base, {});
		const TypeId middle = table.record(EntityId{98401});
		const std::array<CanonicalClassBase, 1> bases{{{middle, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec).has_value());
		CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target).has_value());
	}
	SUBCASE("Published ambiguous graphs reject values and references") {
		table.publishClassBaseSchema(base, {});
		const TypeId left = table.record(EntityId{98402});
		const TypeId right = table.record(EntityId{98403});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(left, bases);
		table.publishClassBaseSchema(right, bases);
		const std::array<CanonicalClassBase, 2> paths{{
			{left, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0},
			{right, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, paths);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, base_spec), false);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(derived_spec, reference_target), false);
	}
	SUBCASE("Deep class graphs use the existing iterative classifier") {
		table.publishClassBaseSchema(base, {});
		TypeId previous = base;
		for (uint32_t depth = 0; depth < 4096; ++depth) {
			const TypeId next = table.record(EntityId{110000 + depth});
			const std::array<CanonicalClassBase, 1> bases{{{previous, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
			table.publishClassBaseSchema(next, bases);
			previous = next;
		}
		TypeSpecifierNode deep_source = derived_spec;
		deep_source.set_type_entity(table.recordEntity(previous));
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(deep_source, base_spec), true);
		check_plan(tryBuildCanonicalConversionFunctionTailPlan(deep_source, reference_target), true);
	}
}

TEST_CASE("Conversion function lookup consumes the published inheritance graph") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CompileContext compile_context;
	compile_context.setInputFile("canonical_conversion_function_graph.cpp");
	Lexer lexer(R"(
struct Base { operator int() const; };
struct Other { operator int() const; };
struct Derived : Base {};
struct Hidden : Base { operator int(); };
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	{
		CanonicalTypeTransaction publication(table);
		REQUIRE(!parser.parse().is_error());
	}
	auto class_spec = [](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		TypeSpecifierNode spec(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
		spec.set_type_index(info->registeredTypeIndex());
		tryBindPublishedTypeEntity(spec);
		REQUIRE(spec.has_type_entity());
		return spec;
	};
	const TypeSpecifierNode base_spec = class_spec("Base");
	const TypeSpecifierNode derived_spec = class_spec("Derived");
	const TypeSpecifierNode hidden_spec = class_spec("Hidden");
	const TypeSpecifierNode other_spec = class_spec("Other");
	const TypeId base = importCanonicalType(table, base_spec).type;
	const TypeId derived = importCanonicalType(table, derived_spec).type;
	const TypeId hidden = importCanonicalType(table, hidden_spec).type;
	const TypeId other = importCanonicalType(table, other_spec).type;
	const TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
	SUBCASE("Canonical lookup cannot publish an absent graph") {
		CHECK_FALSE(trySelectCanonicalUserDefinedConversionOperator(derived, integer, false).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(base));
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("The wrapper cannot retry an absent graph through compatibility") {
		CHECK_FALSE(trySelectUserDefinedConversionOperator(derived_spec, integer, false).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(base));
		CHECK_FALSE(table.hasClassBaseSchema(derived));
	}
	SUBCASE("A published graph without the legacy base has no candidate") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, {});
		CHECK_FALSE(trySelectCanonicalUserDefinedConversionOperator(derived, integer, false).has_value());
		CHECK_FALSE(trySelectUserDefinedConversionOperator(derived_spec, integer, false).has_value());
		CHECK(table.classBaseCount(derived) == 0);
	}
	SUBCASE("A missing reachable base schema remains unavailable") {
		table.publishClassBaseSchema(hidden, bases);
		CHECK_FALSE(trySelectCanonicalUserDefinedConversionOperator(hidden, integer, false).has_value());
		CHECK_FALSE(trySelectUserDefinedConversionOperator(hidden_spec, integer, false).has_value());
		CHECK_FALSE(table.hasClassBaseSchema(base));
	}
	SUBCASE("A published inherited operator retains its declaration owner") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, bases);
		const auto selected = trySelectUserDefinedConversionOperator(derived_spec, integer, false);
		REQUIRE(selected.has_value());
		CHECK(selected->declaring_type == base);
		CHECK_FALSE(selected->ambiguous);
	}
	SUBCASE("A nearer declaration hides the inherited operator of the same target") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(hidden, bases);
		const auto selected = trySelectUserDefinedConversionOperator(hidden_spec, integer, false);
		REQUIRE(selected.has_value());
		CHECK(selected->declaring_type == hidden);
		CHECK_FALSE(selected->ambiguous);
	}
	SUBCASE("A private base retains its candidate for access checking after selection") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> private_bases{{{base, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, private_bases);
		const auto selected = trySelectUserDefinedConversionOperator(derived_spec, integer, false);
		REQUIRE(selected.has_value());
		CHECK(selected->declaring_type == base);
		CHECK(table.classBaseAt(derived, 0).access == CanonicalAccess::Private);
	}
	SUBCASE("A virtual base retains its candidate") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> virtual_bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::Virtual, 0, 0}}};
		table.publishClassBaseSchema(derived, virtual_bases);
		const auto selected = trySelectUserDefinedConversionOperator(derived_spec, integer, false);
		REQUIRE(selected.has_value());
		CHECK(selected->declaring_type == base);
	}
	SUBCASE("Two published base operators are ambiguous despite a single legacy base") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(other, {});
		const std::array<CanonicalClassBase, 2> ambiguous_bases{{
			{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0},
			{other, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, ambiguous_bases);
		const auto selected = trySelectUserDefinedConversionOperator(derived_spec, integer, false);
		REQUIRE(selected.has_value());
		CHECK(selected->ambiguous);
		CHECK(table.classBaseCount(derived) == 2);
	}
}

TEST_CASE("Inherited operator lookup requires published class-base graphs") {
	static_assert(!std::is_constructible_v<OperatorOverloadResult, const StructMemberFunction*>);
	std::cout << "unary operator metadata: node=" << sizeof(UnaryOperatorNode) << '\n';
	static_assert(std::is_invocable_r_v<bool, decltype(&appendCanonicalOverloadBaseTypes), const CanonicalTypeTable&, TypeId,
		std::vector<TypeId>&>);
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	std::cout << "operator owner metadata: binary=" << sizeof(BinaryOperatorNode) << " selection=" << sizeof(OperatorOverloadResult) << '\n';
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CompileContext compile_context;
	compile_context.setInputFile("canonical_operator_base_graph.cpp");
	Lexer lexer(R"(
struct Base { int operator-() const; int operator+(int) const; };
struct Other { long long operator-() const; long long operator+(int) const; };
struct Derived : Base {};
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	{
		CanonicalTypeTransaction publication(table);
		REQUIRE(!parser.parse().is_error());
	}
	auto class_spec = [](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		TypeSpecifierNode spec(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
		spec.set_type_index(info->registeredTypeIndex());
		tryBindPublishedTypeEntity(spec);
		REQUIRE(spec.has_type_entity());
		return spec;
	};
	const TypeSpecifierNode base_spec = class_spec("Base");
	const TypeSpecifierNode other_spec = class_spec("Other");
	TypeSpecifierNode derived_spec = class_spec("Derived");
	const TypeId base = importCanonicalType(table, base_spec).type;
	const TypeId other = importCanonicalType(table, other_spec).type;
	const TypeId derived = importCanonicalType(table, derived_spec).type;
	const TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<CanonicalClassBase, 1> bases{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
	auto check_lookup = [&](const TypeInfo* expected_owner) {
		const size_t types_before = table.size();
		const std::array<bool, 3> schemas_before{table.hasClassBaseSchema(base), table.hasClassBaseSchema(other), table.hasClassBaseSchema(derived)};
		const std::array<OperatorOverloadResult, 4> results{
			findUnaryOperatorOverload(derived_spec, OverloadableOperator::Minus),
			findUnaryOperatorOverloadWithFreeFunction(derived_spec, OverloadableOperator::Minus, gSymbolTable),
			findBinaryOperatorOverload(derived_spec, integer, OverloadableOperator::Plus, nullptr),
			findBinaryOperatorOverloadWithFreeFunction(derived_spec, integer, OverloadableOperator::Plus, gSymbolTable, nullptr, nullptr)};
		for (const OperatorOverloadResult& result : results) {
			CHECK(result.has_match == (expected_owner != nullptr));
			if (expected_owner != nullptr) {
				REQUIRE(result.member_overload != nullptr);
				const auto& members = expected_owner->getStructInfo()->member_functions;
				CHECK(std::any_of(members.begin(), members.end(), [&](const auto& member) { return &member == result.member_overload; }));
			}
		}
		if (expected_owner != nullptr) {
			for (size_t index = 0; index < results.size(); ++index) {
				const auto& result = results[index];
				CHECK(result.declaring_type == importCanonicalClassTypeInfo(table, *expected_owner).type);
				CHECK(result.compatibility_declaring_type_index == expected_owner->registeredTypeIndex().withCategory(TypeCategory::Struct));
				BinaryOperatorNode annotated(Token{}, ASTNode{}, ASTNode{});
				annotated.set_resolved_member_operator_overload(result.member_overload, result.declaring_type,
					result.compatibility_declaring_type_index);
				BinaryOperatorNode copied(Token{}, ASTNode{}, ASTNode{});
				copied.copy_semantic_operator_resolution_from(annotated);
				CHECK(copied.resolved_member_operator_owner() == result.declaring_type);
				CHECK(copied.resolved_member_operator_owner_index() == result.compatibility_declaring_type_index);
				copied.set_no_match_operator_overload();
				CHECK_FALSE(copied.resolved_member_operator_owner());
				CHECK_FALSE(copied.resolved_member_operator_owner_index().is_valid());
			}
		}
		CHECK(table.size() == types_before);
		CHECK(table.hasClassBaseSchema(base) == schemas_before[0]);
		CHECK(table.hasClassBaseSchema(other) == schemas_before[1]);
		CHECK(table.hasClassBaseSchema(derived) == schemas_before[2]);
	};
	SUBCASE("Absent graph cannot be repaired or retried through compatibility") { check_lookup(nullptr); }
	SUBCASE("A canonical root without a declaration bridge cannot retry legacy identity") {
		derived_spec.set_type_entity(EntityId{98701});
		check_lookup(nullptr);
	}
	SUBCASE("Missing reachable schema cannot retry legacy bases") {
		table.publishClassBaseSchema(derived, bases);
		check_lookup(nullptr);
	}
	SUBCASE("Published graph can withhold a legacy base") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, {});
		check_lookup(nullptr);
	}
	SUBCASE("Published graph retains inherited declarations") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, bases);
		check_lookup(tryGetTypeInfo(base_spec.type_index()));
	}
	SUBCASE("Published graph can select a different declaration owner") {
		table.publishClassBaseSchema(other, {});
		const std::array<CanonicalClassBase, 1> other_bases{{other, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishClassBaseSchema(derived, other_bases);
		check_lookup(tryGetTypeInfo(other_spec.type_index()));
	}
}

TEST_CASE("Unary operator selection preserves member reference qualification") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("unary_operator_ref_qualification.cpp");
	Lexer lexer("struct Value { int operator-() &; int operator-() &&; };");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle("Value"));
	REQUIRE(info != nullptr);
	TypeSpecifierNode object(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	object.set_type_index(info->registeredTypeIndex());
	tryBindPublishedTypeEntity(object);
	object.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const auto lvalue = findUnaryOperatorOverloadWithFreeFunction(object, OverloadableOperator::Minus, gSymbolTable);
	REQUIRE(lvalue.has_match);
	REQUIRE(lvalue.member_overload != nullptr);
	CHECK(lvalue.member_overload->ref_qualifier == ReferenceQualifier::LValueReference);
	object.set_reference_qualifier(ReferenceQualifier::None);
	const auto rvalue = findUnaryOperatorOverloadWithFreeFunction(object, OverloadableOperator::Minus, gSymbolTable);
	REQUIRE(rvalue.has_match);
	REQUIRE(rvalue.member_overload != nullptr);
	CHECK(rvalue.member_overload->ref_qualifier == ReferenceQualifier::RValueReference);
	CHECK(lvalue.member_overload != rvalue.member_overload);
	CHECK(lvalue.declaring_type == rvalue.declaring_type);
	CHECK(lvalue.compatibility_declaring_type_index == rvalue.compatibility_declaring_type_index);
}

TEST_CASE("Converting constructor class checks cannot recover unpublished inheritance") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	CompileContext compile_context;
	compile_context.setInputFile("canonical_constructor_base_proof.cpp");
	Lexer lexer("struct Base {}; struct Derived : Base {}; struct Receiver { Receiver(const Derived&); };");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	{
		// Retain the legacy proof while deliberately withholding canonical publication.
		CanonicalTypeTransaction publication(table);
		REQUIRE(!parser.parse().is_error());
	}
	auto class_spec = [](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		TypeSpecifierNode spec(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
		spec.set_type_index(info->registeredTypeIndex());
		tryBindPublishedTypeEntity(spec);
		REQUIRE(spec.has_type_entity());
		return spec;
	};
	const TypeSpecifierNode base_spec = class_spec("Base");
	TypeSpecifierNode derived_spec = class_spec("Derived");
	const TypeSpecifierNode receiver_spec = class_spec("Receiver");
	const TypeId base = importCanonicalType(table, base_spec).type;
	const TypeId derived = importCanonicalType(table, derived_spec).type;
	const TypeId receiver = importCanonicalType(table, receiver_spec).type;
	REQUIRE(base);
	REQUIRE(derived);
	REQUIRE(receiver);
	REQUIRE(hasUsablePublicDerivedBaseConversion(derived_spec.type_index(), base_spec.type_index()));
	REQUIRE_FALSE(table.hasClassBaseSchema(base));
	REQUIRE_FALSE(table.hasClassBaseSchema(derived));
	SUBCASE("Absent constructor and base schemas cannot use legacy inheritance") {
		CHECK_FALSE(tryHasCanonicalImplicitConvertingConstructorForArgument(base_spec, derived_spec, nullptr).has_value());
		CHECK_FALSE(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("A complete constructor answer cannot trigger base publication") {
		table.publishRecordConstructors(base, {});
		CHECK_FALSE(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("Published unrelated graphs override legacy inheritance without constructor facts") {
		table.publishClassBaseSchema(base, {});
		table.publishClassBaseSchema(derived, {});
		CHECK_FALSE(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("Published public inheritance needs no constructor facts") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		CHECK(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("Published private inheritance overrides legacy public inheritance") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		CHECK_FALSE(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("Published virtual inheritance remains viable") {
		table.publishClassBaseSchema(base, {});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::Virtual, 0, 0}}};
		table.publishClassBaseSchema(derived, bases);
		CHECK(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("Published ambiguous inheritance overrides legacy public inheritance") {
		table.publishClassBaseSchema(base, {});
		const TypeId left = table.record(EntityId{98301});
		const TypeId right = table.record(EntityId{98302});
		const std::array<CanonicalClassBase, 1> bases{{{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(left, bases);
		table.publishClassBaseSchema(right, bases);
		const std::array<CanonicalClassBase, 2> paths{{
			{left, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0},
			{right, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishClassBaseSchema(derived, paths);
		CHECK_FALSE(hasConvertingConstructorFrom(base_spec, derived_spec));
	}
	SUBCASE("A published converting constructor needs no inheritance proof") {
		const std::array<TypeId, 1> parameters{derived};
		const std::array<CanonicalRecordConstructorSpec, 1> constructors{{{parameters, CanonicalRecordFunctionFlags::None, false}}};
		table.publishRecordConstructors(receiver, constructors);
		CHECK(hasConvertingConstructorFrom(receiver_spec, derived_spec));
	}
	SUBCASE("A complete constructor rejection overrides compatibility declarations") {
		table.publishRecordConstructors(receiver, {});
		CHECK_FALSE(hasConvertingConstructorFrom(receiver_spec, derived_spec));
	}
	SUBCASE("Incomplete constructor schemas retain compatibility selection") {
		const std::array<CanonicalRecordConstructorSpec, 1> constructors{{{{}, CanonicalRecordFunctionFlags::SchemaIncomplete, false}}};
		table.publishRecordConstructors(receiver, constructors);
		derived_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);
		CHECK_FALSE(tryHasCanonicalImplicitConvertingConstructorForArgument(receiver_spec, derived_spec, nullptr).has_value());
		CHECK(hasConvertingConstructorFrom(receiver_spec, derived_spec));
	}
	CHECK_FALSE(table.hasRecordProperties(derived));
}

TEST_CASE("Canonical constructor arguments bind base references without compatibility types") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId base_type = table.record(EntityId{910});
	const TypeId derived_type = table.record(EntityId{911});
	const TypeId consumer_type = table.record(EntityId{912});
	const std::array<CanonicalClassBase, 1> bases{{{base_type, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0}}};
	table.publishClassBaseSchema(base_type, {});
	table.publishClassBaseSchema(derived_type, bases);
	const std::array<TypeId, 1> parameters{table.reference(base_type, ReferenceQualifier::LValueReference)};
	const std::array<CanonicalRecordConstructorSpec, 1> constructors{{{parameters, CanonicalRecordFunctionFlags::None, true}}};
	table.publishRecordConstructors(consumer_type, constructors);
	TypeSpecifierNode consumer(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	consumer.set_type_entity(EntityId{912});
	TypeSpecifierNode argument(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	argument.set_type_entity(EntityId{911});
	argument.set_reference_qualifier(ReferenceQualifier::LValueReference);

	const std::optional<bool> converting = tryHasCanonicalImplicitConvertingConstructorForArgument(consumer, argument, nullptr);
	REQUIRE(converting.has_value());
	CHECK(*converting);
	const std::array<TypeSpecifierNode, 1> arguments{argument};
	const auto constructible = evaluateConstructibility(TypeTraitKind::IsConstructible, consumer, arguments, ConstructibilityFallback::None);
	REQUIRE(constructible.has_value());
	CHECK(constructible->success);
	CHECK(constructible->value);
	const auto nothrow = evaluateConstructibility(TypeTraitKind::IsNothrowConstructible, consumer, arguments, ConstructibilityFallback::None);
	REQUIRE(nothrow.has_value());
	CHECK(nothrow->success);
	CHECK(nothrow->value);

	argument.set_cv_qualifier(CVQualifier::Const);
	const std::optional<bool> rejected = tryHasCanonicalImplicitConvertingConstructorForArgument(consumer, argument, nullptr);
	REQUIRE(rejected.has_value());
	CHECK_FALSE(*rejected);
}

TEST_CASE("Canonical reference argument plans rank scalar conversion temporaries") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId short_type = table.builtin(CanonicalBuiltinKind::Short);
	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId long_type = table.builtin(CanonicalBuiltinKind::Long);
	const TypeId short_lvalue = table.reference(short_type, ReferenceQualifier::LValueReference);
	const TypeId const_short_lvalue = table.reference(table.qualify(short_type, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const TypeId const_int_lvalue = table.reference(table.qualify(int_type, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const TypeId const_long_lvalue = table.reference(table.qualify(long_type, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const TypeId mutable_long_lvalue = table.reference(long_type, ReferenceQualifier::LValueReference);
	const TypeId long_rvalue = table.reference(long_type, ReferenceQualifier::RValueReference);
	const TypeId const_long_rvalue = table.reference(table.qualify(long_type, CVQualifier::Const), ReferenceQualifier::RValueReference);
	auto plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, short_lvalue, const_int_lvalue);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(plan->rank == ConversionRank::Promotion);
	CHECK(plan->kind == StandardConversionKind::IntegralPromotion);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, const_short_lvalue, const_long_lvalue);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(plan->rank == ConversionRank::Conversion);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, short_lvalue, long_rvalue);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(plan->rank == ConversionRank::Conversion);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, const_short_lvalue, long_rvalue);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(plan->rank == ConversionRank::Conversion);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, short_lvalue, mutable_long_lvalue);
	REQUIRE(plan.has_value());
	CHECK_FALSE(plan->is_valid);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, long_type, const_long_rvalue);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, const_long_lvalue, long_rvalue);
	REQUIRE(plan.has_value());
	CHECK_FALSE(plan->is_valid);
	const TypeId null_pointer = table.builtin(CanonicalBuiltinKind::Nullptr);
	const TypeId const_bool_lvalue = table.reference(table.qualify(table.builtin(CanonicalBuiltinKind::Bool), CVQualifier::Const),
		ReferenceQualifier::LValueReference);
	plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, null_pointer, const_bool_lvalue);
	REQUIRE(plan.has_value());
	CHECK_FALSE(plan->is_valid);
}

TEST_CASE("Canonical reference argument plans use structural conversion temporaries without TypeInfo") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId boolean = table.builtin(CanonicalBuiltinKind::Bool);
	const TypeId pointer = table.pointer(integer);
	const TypeId member = table.memberObjectPointer(table.record(EntityId{920}), integer);
	const TypeId function = table.function(integer, {}, false, CVQualifier::None, ReferenceQualifier::None, true,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId throwing_function = table.function(integer, {}, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const auto check_plan = [&table](TypeId source, TypeId target, StandardConversionKind kind) {
		const TypeId reference = table.reference(table.qualify(target, CVQualifier::Const), ReferenceQualifier::LValueReference);
		const auto plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, source, reference);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid);
		CHECK(plan->kind == kind);
	};
	check_plan(table.builtin(CanonicalBuiltinKind::Nullptr), pointer, StandardConversionKind::PointerConversion);
	check_plan(table.builtin(CanonicalBuiltinKind::Nullptr), member, StandardConversionKind::PointerConversion);
	check_plan(pointer, boolean, StandardConversionKind::BooleanConversion);
	check_plan(member, boolean, StandardConversionKind::BooleanConversion);
	check_plan(table.reference(table.array(integer, 3), ReferenceQualifier::LValueReference), pointer, StandardConversionKind::ArrayToPointer);
	check_plan(table.reference(function, ReferenceQualifier::LValueReference), table.pointer(throwing_function),
		StandardConversionKind::FunctionToPointer);
	check_plan(table.pointer(function), table.pointer(throwing_function), StandardConversionKind::QualificationAdjustment);
	check_plan(table.memberFunctionPointer(table.record(EntityId{920}), function),
		table.memberFunctionPointer(table.record(EntityId{920}), throwing_function), StandardConversionKind::QualificationAdjustment);
	const TypeId enum_type = table.enumeration(EntityId{921});
	table.publishEnumLayout(CanonicalEnumLayout{.entity = EntityId{921}, .underlying_type = table.builtin(CanonicalBuiltinKind::Short),
		.size_bytes = 2, .enumerator_count = 1, .flags = CanonicalEnumLayoutFlags::FixedUnderlying});
	check_plan(table.reference(enum_type, ReferenceQualifier::LValueReference), integer, StandardConversionKind::IntegralPromotion);
	check_plan(enum_type, table.builtin(CanonicalBuiltinKind::Double), StandardConversionKind::FloatingIntegralConversion);
	const TypeId scoped_enum = table.enumeration(EntityId{922});
	table.publishEnumLayout(CanonicalEnumLayout{.entity = EntityId{922}, .underlying_type = integer, .size_bytes = 4, .enumerator_count = 1,
		.flags = CanonicalEnumLayoutFlags::Scoped | CanonicalEnumLayoutFlags::FixedUnderlying});
	const TypeId const_integer_reference = table.reference(table.qualify(integer, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const auto scoped_plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, scoped_enum, const_integer_reference);
	REQUIRE(scoped_plan.has_value());
	CHECK_FALSE(scoped_plan->is_valid);
	const auto mutable_plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, pointer,
		table.reference(boolean, ReferenceQualifier::LValueReference));
	REQUIRE(mutable_plan.has_value());
	CHECK_FALSE(mutable_plan->is_valid);
	const TypeId pointer_lvalue = table.reference(pointer, ReferenceQualifier::LValueReference);
	const TypeId qualified_pointer = table.pointer(table.qualify(integer, CVQualifier::Const));
	const TypeId compatible_reference = table.reference(table.qualify(qualified_pointer, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const auto direct_plan = tryBuildCanonicalDirectReferenceBindingPlan(table, pointer_lvalue, compatible_reference);
	REQUIRE(direct_plan.has_value());
	CHECK(direct_plan->is_valid);
	CHECK(direct_plan->kind == StandardConversionKind::QualificationAdjustment);
	const auto related_plan = tryBuildCanonicalReferenceArgumentConversionPlan(table, pointer_lvalue,
		table.reference(qualified_pointer, ReferenceQualifier::RValueReference));
	REQUIRE(related_plan.has_value());
	CHECK_FALSE(related_plan->is_valid);
	const TypeId unknown_enum = table.enumeration(EntityId{923});
	CHECK_FALSE(tryBuildCanonicalReferenceArgumentConversionPlan(table, unknown_enum, const_integer_reference).has_value());
	CHECK_FALSE(tryBuildCanonicalReferenceArgumentConversionPlan(table, table.record(EntityId{924}), const_integer_reference).has_value());
	const TypeId const_pointer_reference = table.reference(table.qualify(pointer, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const auto different_pointer = tryBuildCanonicalReferenceArgumentConversionPlan(table, table.pointer(boolean), const_pointer_reference);
	REQUIRE(different_pointer.has_value());
	CHECK_FALSE(different_pointer->is_valid);
	const TypeId record_pointer = table.pointer(table.record(EntityId{924}));
	const TypeId other_record_pointer = table.pointer(table.record(EntityId{925}));
	const TypeId nominal_reference = table.reference(table.qualify(other_record_pointer, CVQualifier::Const), ReferenceQualifier::LValueReference);
	CHECK_FALSE(tryBuildCanonicalReferenceArgumentConversionPlan(table, record_pointer, nominal_reference).has_value());
	const TypeId dependent_pointer = table.pointer(table.templateParameter(TemplateDeclId{926}, 0));
	CHECK_FALSE(tryBuildCanonicalReferenceArgumentConversionPlan(table, dependent_pointer, const_pointer_reference).has_value());
	const TypeId other_member = table.memberObjectPointer(table.record(EntityId{925}), integer);
	const TypeId member_reference = table.reference(table.qualify(member, CVQualifier::Const), ReferenceQualifier::LValueReference);
	CHECK_FALSE(tryBuildCanonicalReferenceArgumentConversionPlan(table, other_member, member_reference).has_value());
	const auto const_array = tryBuildCanonicalReferenceArgumentConversionPlan(table,
		table.reference(table.array(table.qualify(integer, CVQualifier::Const), 3), ReferenceQualifier::LValueReference), const_pointer_reference);
	REQUIRE(const_array.has_value());
	CHECK_FALSE(const_array->is_valid);
}

TEST_CASE("Canonical constructibility shares structural reference temporary plans without TypeInfo") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId boolean = table.builtin(CanonicalBuiltinKind::Bool);
	const TypeId boolean_reference = table.reference(table.qualify(boolean, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const EntityId consumer_entity{927};
	const std::array<TypeId, 1> parameters{boolean_reference};
	const std::array<CanonicalRecordConstructorSpec, 1> constructors{{{parameters, CanonicalRecordFunctionFlags::None, true}}};
	table.publishRecordConstructors(table.record(consumer_entity), constructors);
	TypeSpecifierNode consumer(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	consumer.set_type_entity(consumer_entity);
	TypeSpecifierNode pointer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer.add_pointer_level(CVQualifier::None);
	pointer.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::array<TypeSpecifierNode, 1> arguments{pointer};
	const auto constructible = evaluateConstructibility(TypeTraitKind::IsConstructible, consumer, arguments, ConstructibilityFallback::None);
	REQUIRE(constructible.has_value());
	CHECK(constructible->success);
	CHECK(constructible->value);
	TypeSpecifierNode boolean_target(TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::Const);
	boolean_target.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const auto scalar = evaluateConstructibility(TypeTraitKind::IsConstructible, boolean_target, arguments, ConstructibilityFallback::None);
	REQUIRE(scalar.has_value());
	CHECK(scalar->success);
	CHECK(scalar->value);
	boolean_target.set_cv_qualifier(CVQualifier::None);
	const auto mutable_binding = evaluateConstructibility(TypeTraitKind::IsConstructible, boolean_target, arguments, ConstructibilityFallback::None);
	REQUIRE(mutable_binding.has_value());
	CHECK(mutable_binding->success);
	CHECK_FALSE(mutable_binding->value);
}

TEST_CASE("Canonical reference qualification plans keep deep declarators off the native stack") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	TypeId source = table.builtin(CanonicalBuiltinKind::Int);
	TypeId target = table.qualify(source, CVQualifier::Const);
	for (size_t depth = 0; depth < 4096; ++depth) {
		source = table.pointer(source);
		target = table.qualify(table.pointer(target), CVQualifier::Const);
	}
	const auto binding = tryBuildCanonicalDirectReferenceBindingPlan(table, table.reference(source, ReferenceQualifier::LValueReference),
		table.reference(target, ReferenceQualifier::LValueReference));
	REQUIRE(binding.has_value());
	CHECK(binding->is_valid);
	CHECK(binding->kind == StandardConversionKind::QualificationAdjustment);
}

TEST_CASE("Canonical constructor schemas rank scalar reference temporaries without TypeInfo") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId ranked_entity{916};
	const TypeId int_reference = table.reference(table.qualify(table.builtin(CanonicalBuiltinKind::Int), CVQualifier::Const),
		ReferenceQualifier::LValueReference);
	const TypeId long_reference = table.reference(table.qualify(table.builtin(CanonicalBuiltinKind::Long), CVQualifier::Const),
		ReferenceQualifier::LValueReference);
	const std::array<TypeId, 1> int_parameters{int_reference};
	const std::array<TypeId, 1> long_parameters{long_reference};
	const std::array<CanonicalRecordConstructorSpec, 2> ranked_constructors{{
		{int_parameters, CanonicalRecordFunctionFlags::None, true},
		{long_parameters, CanonicalRecordFunctionFlags::None, false},
	}};
	table.publishRecordConstructors(table.record(ranked_entity), ranked_constructors);
	TypeSpecifierNode ranked_target(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	ranked_target.set_type_entity(ranked_entity);
	TypeSpecifierNode short_argument(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::None);
	short_argument.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::array<TypeSpecifierNode, 1> arguments{short_argument};
	const std::optional<bool> converting = tryHasCanonicalImplicitConvertingConstructorForArgument(ranked_target, short_argument, nullptr);
	REQUIRE(converting.has_value());
	CHECK(*converting);
	const auto constructible = evaluateConstructibility(TypeTraitKind::IsConstructible, ranked_target, arguments, ConstructibilityFallback::None);
	REQUIRE(constructible.has_value());
	CHECK(constructible->success);
	CHECK(constructible->value);
	const auto nothrow = evaluateConstructibility(TypeTraitKind::IsNothrowConstructible, ranked_target, arguments, ConstructibilityFallback::None);
	REQUIRE(nothrow.has_value());
	CHECK(nothrow->success);
	CHECK(nothrow->value);
	const EntityId throwing_entity{919};
	const std::array<CanonicalRecordConstructorSpec, 1> throwing_constructors{{
		{long_parameters, CanonicalRecordFunctionFlags::None, false},
	}};
	table.publishRecordConstructors(table.record(throwing_entity), throwing_constructors);
	TypeSpecifierNode throwing_target(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	throwing_target.set_type_entity(throwing_entity);
	const auto throwing_nothrow = evaluateConstructibility(TypeTraitKind::IsNothrowConstructible, throwing_target, arguments,
		ConstructibilityFallback::None);
	REQUIRE(throwing_nothrow.has_value());
	CHECK(throwing_nothrow->success);
	CHECK_FALSE(throwing_nothrow->value);

	const EntityId mutable_entity{917};
	const TypeId mutable_reference = table.reference(table.builtin(CanonicalBuiltinKind::Long), ReferenceQualifier::LValueReference);
	const std::array<TypeId, 1> mutable_parameters{mutable_reference};
	const std::array<CanonicalRecordConstructorSpec, 1> mutable_constructors{{
		{mutable_parameters, CanonicalRecordFunctionFlags::None, true},
	}};
	table.publishRecordConstructors(table.record(mutable_entity), mutable_constructors);
	TypeSpecifierNode mutable_target(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	mutable_target.set_type_entity(mutable_entity);
	const std::optional<bool> mutable_conversion = tryHasCanonicalImplicitConvertingConstructorForArgument(mutable_target, short_argument, nullptr);
	REQUIRE(mutable_conversion.has_value());
	CHECK_FALSE(*mutable_conversion);
	const auto mutable_result = evaluateConstructibility(TypeTraitKind::IsConstructible, mutable_target, arguments, ConstructibilityFallback::None);
	REQUIRE(mutable_result.has_value());
	CHECK(mutable_result->success);
	CHECK_FALSE(mutable_result->value);

	const EntityId bool_entity{918};
	const TypeId bool_reference = table.reference(table.qualify(table.builtin(CanonicalBuiltinKind::Bool), CVQualifier::Const),
		ReferenceQualifier::LValueReference);
	const std::array<TypeId, 1> bool_parameters{bool_reference};
	const std::array<CanonicalRecordConstructorSpec, 1> bool_constructors{{
		{bool_parameters, CanonicalRecordFunctionFlags::None, true},
	}};
	table.publishRecordConstructors(table.record(bool_entity), bool_constructors);
	TypeSpecifierNode bool_target(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	bool_target.set_type_entity(bool_entity);
	TypeSpecifierNode null_pointer_argument(TypeCategory::Nullptr, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	const std::array<TypeSpecifierNode, 1> null_args{null_pointer_argument};
	const std::optional<bool> bool_conversion = tryHasCanonicalImplicitConvertingConstructorForArgument(bool_target, null_pointer_argument, nullptr);
	REQUIRE(bool_conversion.has_value());
	CHECK_FALSE(*bool_conversion);
	const auto bool_result = evaluateConstructibility(TypeTraitKind::IsConstructible, bool_target, null_args, ConstructibilityFallback::None);
	REQUIRE(bool_result.has_value());
	CHECK(bool_result->success);
	CHECK_FALSE(bool_result->value);
}

TEST_CASE("Canonical direct reference binding preserves access cv and value category") {
	CanonicalTypeTable table;
	const TypeId base_type = table.record(EntityId{913});
	const TypeId derived_type = table.record(EntityId{914});
	const TypeId private_type = table.record(EntityId{915});
	const std::array<CanonicalClassBase, 1> public_base{{{base_type, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0}}};
	const std::array<CanonicalClassBase, 1> private_base{{{base_type, CanonicalAccess::Private, CanonicalRecordBaseFlags::None, 0}}};
	table.publishClassBaseSchema(base_type, {});
	table.publishClassBaseSchema(derived_type, public_base);
	table.publishClassBaseSchema(private_type, private_base);
	const TypeId const_base_ref = table.reference(table.qualify(base_type, CVQualifier::Const), ReferenceQualifier::LValueReference);
	const TypeId base_ref = table.reference(base_type, ReferenceQualifier::LValueReference);
	const TypeId base_rvalue_ref = table.reference(base_type, ReferenceQualifier::RValueReference);
	const TypeId derived_ref = table.reference(derived_type, ReferenceQualifier::LValueReference);
	const auto binding = tryBuildCanonicalDirectReferenceBindingPlan(table, derived_ref, const_base_ref);
	REQUIRE(binding.has_value());
	CHECK(binding->is_valid);
	CHECK(binding->kind == StandardConversionKind::DerivedToBase);
	CHECK(binding->rank == ConversionRank::Conversion);
	const auto check_binding = [&table](TypeId source, TypeId target, bool expected) {
		const auto plan = tryBuildCanonicalDirectReferenceBindingPlan(table, source, target);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid == expected);
	};
	check_binding(derived_type, base_ref, false);
	check_binding(derived_ref, base_rvalue_ref, false);
	check_binding(derived_type, base_rvalue_ref, true);
	check_binding(table.qualify(derived_type, CVQualifier::Const), base_rvalue_ref, false);
	check_binding(private_type, const_base_ref, false);
	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	CHECK_FALSE(tryBuildCanonicalDirectReferenceBindingPlan(table, int_type, const_base_ref).has_value());

	TypeId deepest_type = derived_type;
	for (uint32_t depth = 0; depth < 1024; ++depth) {
		const TypeId next_type = table.templateSpecialization(TemplateDeclId{10000 + depth}, std::span<const TypeId>{});
		const std::array<CanonicalClassBase, 1> next_base{{{deepest_type, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0}}};
		table.publishClassBaseSchema(next_type, next_base);
		deepest_type = next_type;
	}
	const auto deep_binding = tryBuildCanonicalDirectReferenceBindingPlan(table, deepest_type, const_base_ref);
	REQUIRE(deep_binding.has_value());
	CHECK(deep_binding->is_valid);
	CHECK(deep_binding->kind == StandardConversionKind::DerivedToBase);
}

TEST_CASE("Canonical function references bind function lvalues directly") {
	CanonicalTypeTable table;
	TypeId parameter = table.builtin(CanonicalBuiltinKind::Short);
	for (size_t depth = 0; depth < 4096; ++depth) {
		parameter = table.pointer(parameter);
	}
	const std::array<TypeId, 1> parameters{parameter};
	const TypeId result = table.builtin(CanonicalBuiltinKind::Long);
	const auto function_type = [&table, &parameters](TypeId return_type, bool is_noexcept) {
		return table.function(return_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, is_noexcept,
			CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	};
	const TypeId function = function_type(result, false);
	const TypeId safe_function = function_type(result, true);
	const TypeId wrong_function = function_type(table.builtin(CanonicalBuiltinKind::Char), false);
	const TypeId lvalue = table.reference(function, ReferenceQualifier::LValueReference);
	const TypeId rvalue = table.reference(function, ReferenceQualifier::RValueReference);
	const auto check_binding = [&table](TypeId source, TypeId target, bool expected) {
		const auto binding = tryBuildCanonicalDirectReferenceBindingPlan(table, source, target);
		REQUIRE(binding.has_value());
		CHECK(binding->is_valid == expected);
		if (expected) {
			CHECK(binding->rank == ConversionRank::ExactMatch);
			CHECK(binding->kind == StandardConversionKind::None);
		}
	};
	for (const TypeId source : {function, lvalue, rvalue}) {
		check_binding(source, lvalue, true);
		check_binding(source, rvalue, true);
	}
	check_binding(table.reference(safe_function, ReferenceQualifier::LValueReference), rvalue, true);
	check_binding(lvalue, table.reference(safe_function, ReferenceQualifier::RValueReference), false);
	check_binding(table.reference(wrong_function, ReferenceQualifier::LValueReference), rvalue, false);
	const TypeId pointer = table.pointer(function);
	check_binding(table.reference(pointer, ReferenceQualifier::LValueReference), rvalue, false);
	check_binding(table.reference(pointer, ReferenceQualifier::LValueReference), table.reference(pointer, ReferenceQualifier::RValueReference), false);
}

TEST_CASE("Canonical record constructor schema is independent of the field schema") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const EntityId entity{903};
	table.publishRecordLayout(CanonicalRecordLayout{
		entity, 4, 4, 4, 4, 0, 0, CanonicalRecordLayoutFlags::None, 0});
	// No field schema is published (as for an anonymous-union / unimportable
	// member record); the constructor schema still publishes from the type node.
	CHECK_FALSE(table.hasRecordFieldSchema(entity));
	const TypeId record_type = table.record(entity);
	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	const std::array<TypeId, 1> one_parameter{int_type};
	const std::array<CanonicalRecordConstructorSpec, 1> constructors{
		CanonicalRecordConstructorSpec{
			std::span<const TypeId>(one_parameter.data(), one_parameter.size()),
			CanonicalRecordFunctionFlags::None, false},
	};
	table.publishRecordConstructors(record_type, constructors);
	REQUIRE(table.hasRecordConstructors(record_type));
	CHECK(table.recordConstructorCount(record_type) == 1);
	CHECK(table.recordConstructorParameterAt(record_type, 0, 0) == int_type);
}

TEST_CASE("Canonical adapter keys an explicit specialization by its primary template") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();

	// A specialization without the primary's identity has no canonical type.
	StructDeclarationNode specialization(StringHandle{}, false, false);
	CHECK(importCanonicalClassDeclaration(table, specialization).status ==
		CanonicalTypeImportStatus::UnmigratedNominal);

	// With the recorded owner it is the same TemplateSpecialization an implicit
	// instantiation of the primary would produce, not a fresh nominal entity.
	const TemplateDeclId primary_decl{4242};
	specialization.set_canonical_specialization_owner_template_decl_id(primary_decl);
	const CanonicalTypeImport imported = importCanonicalClassDeclaration(table, specialization);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(table.node(imported.type).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(imported.type == table.templateSpecialization(primary_decl, std::span<const TypeId>{}));
}

TEST_CASE("Concrete NTTP reconstruction preserves its source expression identity") {
	TemplateTypeArg source = TemplateTypeArg::makeDependentValue(StringTable::getOrInternStringHandle("N"), TypeCategory::Int, 0);
	source.is_pack = true;
	source.nttp_expr = ExprId{17};
	const TemplateTypeArg concrete = makeConcreteNttpArg(FlashCpp::NonTypeValueIdentity::makeConcrete(4, TypeCategory::Int), source);
	CHECK(concrete.is_value);
	CHECK(concrete.value == 4);
	CHECK_FALSE(concrete.is_dependent);
	CHECK_FALSE(concrete.dependent_name.isValid());
	CHECK_FALSE(concrete.dependent_expr.has_value());
	CHECK(concrete.is_pack);
	CHECK(concrete.nttp_expr == ExprId{17});
	CHECK(toTemplateArgInfo(concrete).nttp_expr == ExprId{17});
	const StringHandle function_name = StringTable::getOrInternStringHandle("pack_function");
	const FlashCpp::NonTypeValueIdentity function_identity = FlashCpp::NonTypeValueIdentity::makeFunctionPointer(
		TypeIndex{42, TypeCategory::FunctionPointer},
		function_name);
	source.nttp_expr = ExprId{18};
	const TemplateTypeArg concrete_function = makeConcreteNttpArg(function_identity, source);
	CHECK(concrete_function.has_typed_value_identity);
	CHECK(concrete_function.valueIdentity() == function_identity);
	CHECK(concrete_function.is_pack);
	CHECK(concrete_function.nttp_expr == ExprId{18});
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

TEST_CASE("Ordered declarator base matching prefers canonical record identity") {
	FrontendContext frontend;
	TypeSpecifierNode first(TypeCategory::Struct, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	first.set_type_index(TypeIndex{900, TypeCategory::Struct});
	first.set_type_entity(EntityId{803});
	first.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	TypeSpecifierNode second = first;
	second.set_type_entity(EntityId{804});

	CHECK_FALSE(orderedDeclaratorBaseTypeMatches(first, second));
	CHECK(orderedDeclaratorBaseTypeMatches(first, first));
}

TEST_CASE("Qualification ranking reads pointee cv from canonical pointer-to-array types") {
	FrontendContext frontend;
	TypeSpecifierNode argument(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
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
		tryBuildCanonicalReferenceBindingPlan(lvalue_argument, lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(canonical_lvalue_plan.has_value());
	CHECK(canonical_lvalue_plan->is_valid);
	CHECK(canonical_lvalue_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode prvalue = pointer_object;
	CHECK_FALSE(buildConversionPlan(prvalue, lvalue_reference).is_valid);
	const std::optional<ConversionPlan> canonical_prvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, lvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(const_lvalue_argument, const_lvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(lvalue_argument, qualified_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(prvalue, rvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(canonical_rvalue_plan.has_value());
	CHECK(canonical_rvalue_plan->is_valid);
	CHECK(canonical_rvalue_plan->rank == ConversionRank::ExactMatch);
	const std::optional<ConversionPlan> canonical_lvalue_to_rvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue_argument, rvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(prvalue, const_lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(canonical_plan.has_value());
	CHECK(canonical_plan->is_valid);
	CHECK(canonical_plan->rank == ConversionRank::ExactMatch);

	TypeSpecifierNode nonconst_lvalue_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	nonconst_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> rejected_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, nonconst_lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(rejected_plan.has_value());
	CHECK_FALSE(rejected_plan->is_valid);

	TypeSpecifierNode double_prvalue(
		TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	TypeSpecifierNode const_double_lvalue_reference(
		TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::Const);
	const_double_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> converting_prvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, const_double_lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(converting_prvalue_plan.has_value());
	CHECK(converting_prvalue_plan->is_valid);
	CHECK(converting_prvalue_plan->rank == ConversionRank::Conversion);
	CHECK(converting_prvalue_plan->kind ==
		StandardConversionKind::FloatingIntegralConversion);
	TypeSpecifierNode nonconst_double_lvalue_reference = double_prvalue;
	nonconst_double_lvalue_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> nonconst_conversion_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, nonconst_double_lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(nonconst_conversion_plan.has_value());
	CHECK_FALSE(nonconst_conversion_plan->is_valid);

	TypeSpecifierNode lvalue = prvalue;
	lvalue.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> converting_lvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue, const_double_lvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(converting_lvalue_plan.has_value());
	CHECK(converting_lvalue_plan->is_valid);
	CHECK(converting_lvalue_plan->rank == ConversionRank::Conversion);

	TypeSpecifierNode double_rvalue_reference = double_prvalue;
	double_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> converting_rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(prvalue, double_rvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(converting_rvalue_reference_plan.has_value());
	CHECK(converting_rvalue_reference_plan->is_valid);
	CHECK(converting_rvalue_reference_plan->rank == ConversionRank::Conversion);
	const std::optional<ConversionPlan> lvalue_to_rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(lvalue, double_rvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(int_pointer, const_bool_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(int_pointer, bool_rvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(int_pointer, bool_lvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, array_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, const_array_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(qualified_array_plan.has_value());
	CHECK(qualified_array_plan->is_valid);
	// The referenced array differs only by top-level element cv-qualification,
	// so direct binding is the identity conversion ([over.ics.ref]/1); the
	// mutable-vs-const preference is applied by [over.ics.rank]/3.2.6.
	CHECK(qualified_array_plan->rank == ConversionRank::ExactMatch);
	CHECK(qualified_array_plan->kind == StandardConversionKind::None);
	const std::optional<ConversionPlan> const_array_xvalue_plan =
		tryBuildCanonicalReferenceBindingPlan(array_type, const_array_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(pointer_array_lvalue, const_pointee_array_type, CanonicalReferenceBindingContext::Argument);
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
			const_nested_pointee_array_type, CanonicalReferenceBindingContext::Argument);
	REQUIRE(const_nested_pointee_array_plan.has_value());
	CHECK_FALSE(const_nested_pointee_array_plan->is_valid);

	TypeSpecifierNode different_extent = array_type;
	different_extent.set_ordered_declarator({DeclaratorComponent::array(4)});
	different_extent.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> extent_mismatch_plan =
		tryBuildCanonicalReferenceBindingPlan(array_lvalue, different_extent, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(function_lvalue, const_pointer_lvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(function_lvalue, nonconst_pointer_lvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(function_lvalue, pointer_rvalue_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(rvalue_plan.has_value());
	CHECK(rvalue_plan->is_valid);
	CHECK(rvalue_plan->kind == StandardConversionKind::FunctionToPointer);

	TypeSpecifierNode mismatched_pointer_reference =
		const_pointer_lvalue_reference;
	mismatched_pointer_reference.set_function_signature(mismatched_signature);
	const std::optional<ConversionPlan> mismatched_signature_plan =
		tryBuildCanonicalReferenceBindingPlan(function_lvalue, mismatched_pointer_reference, CanonicalReferenceBindingContext::Argument);
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
	auto publish_record = [&table](EntityId entity, std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity, std::span<const CanonicalRecordMember>{}, bases);
		publishTestClassBases(table, entity, bases);
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
		tryBuildCanonicalReferenceBindingPlan(derived_pointer_lvalue, const_base_pointer, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(derived_pointer_lvalue, const_void_pointer, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(derived_pointer_lvalue, mutable_base_pointer, CanonicalReferenceBindingContext::Argument);
	REQUIRE(nonconst_lvalue_plan.has_value());
	CHECK_FALSE(nonconst_lvalue_plan->is_valid);

	TypeSpecifierNode base_pointer_rvalue_reference = mutable_base_pointer;
	base_pointer_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(derived_pointer_lvalue, base_pointer_rvalue_reference, CanonicalReferenceBindingContext::Argument);
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
		tryBuildCanonicalReferenceBindingPlan(integer_pointer_lvalue, const_pointee_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(mutable_reference_plan.has_value());
	CHECK_FALSE(mutable_reference_plan->is_valid);

	TypeSpecifierNode const_pointer_reference(
		TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::Const);
	const_pointer_reference.add_pointer_level(CVQualifier::Const);
	const_pointer_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> const_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(integer_pointer_lvalue, const_pointer_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(const_reference_plan.has_value());
	CHECK(const_reference_plan->is_valid);
	CHECK(const_reference_plan->kind ==
		StandardConversionKind::QualificationAdjustment);
}

TEST_CASE("Canonical reference binding preserves cv on non-reference pointer sources") {
	FrontendContext frontend;
	TypeSpecifierNode integer_pointer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	integer_pointer.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
	});
	integer_pointer.set_reference_qualifier(ReferenceQualifier::LValueReference);

	TypeSpecifierNode mutable_pointer_reference(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	mutable_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
	});
	mutable_pointer_reference.prepend_ordered_declarator_component(DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> mutable_plan =
		tryBuildCanonicalReferenceBindingPlan(integer_pointer, mutable_pointer_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(mutable_plan.has_value());
	CHECK_FALSE(mutable_plan->is_valid);

	TypeSpecifierNode const_pointer_reference = mutable_pointer_reference;
	const_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::Const),
	});
	const_pointer_reference.prepend_ordered_declarator_component(DeclaratorComponent::lvalueReference());
	const std::optional<ConversionPlan> const_plan =
		tryBuildCanonicalReferenceBindingPlan(integer_pointer, const_pointer_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(const_plan.has_value());
	CHECK(const_plan->is_valid);
	CHECK(const_plan->rank == ConversionRank::ExactMatch);
}

TEST_CASE("Itanium parameter mangling preserves pointer CV through references") {
	const auto encode = [](const TypeSpecifierNode& type) {
		std::string result;
		NameMangling::appendItaniumTypeCode(result, type, true);
		return result;
	};
	TypeSpecifierNode mutable_pointer_reference(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	mutable_pointer_reference.set_ordered_declarator({DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::None)});
	CHECK(encode(mutable_pointer_reference) == "RPi");
	TypeSpecifierNode const_pointer_reference = mutable_pointer_reference;
	const_pointer_reference.set_ordered_declarator({DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::Const)});
	CHECK(encode(const_pointer_reference) == "RKPi");
	TypeSpecifierNode volatile_pointer_reference = mutable_pointer_reference;
	volatile_pointer_reference.set_ordered_declarator({DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::Volatile)});
	CHECK(encode(volatile_pointer_reference) == "RVPi");
	TypeSpecifierNode const_volatile_pointer_reference = mutable_pointer_reference;
	const_volatile_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::ConstVolatile)});
	CHECK(encode(const_volatile_pointer_reference) == "RVKPi");
	TypeSpecifierNode pointer_rvalue_reference = mutable_pointer_reference;
	pointer_rvalue_reference.set_ordered_declarator({DeclaratorComponent::rvalueReference(), DeclaratorComponent::pointer(CVQualifier::None)});
	CHECK(encode(pointer_rvalue_reference) == "OPi");
	TypeSpecifierNode pointer_pointer_reference = mutable_pointer_reference;
	pointer_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::None)});
	CHECK(encode(pointer_pointer_reference) == "RPPi");
	TypeSpecifierNode const_pointer_pointer_reference = pointer_pointer_reference;
	const_pointer_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::Const),
		DeclaratorComponent::pointer(CVQualifier::None)});
	CHECK(encode(const_pointer_pointer_reference) == "RKPPi");
	TypeSpecifierNode pointer_to_const_pointer_reference = pointer_pointer_reference;
	pointer_to_const_pointer_reference.set_ordered_declarator({
		DeclaratorComponent::lvalueReference(), DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::pointer(CVQualifier::Const)});
	CHECK(encode(pointer_to_const_pointer_reference) == "RPKPi");
	TypeSpecifierNode const_pointer_by_value = mutable_pointer_reference;
	const_pointer_by_value.set_ordered_declarator({DeclaratorComponent::pointer(CVQualifier::Const)});
	CHECK(encode(const_pointer_by_value) == "Pi");
	const auto encodeLegacy = [&encode](std::initializer_list<CVQualifier> pointer_cv, ReferenceQualifier reference) {
		TypeSpecifierNode type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
		for (const CVQualifier cv : pointer_cv) {
			type.add_pointer_level(cv);
		}
		type.set_reference_qualifier(reference);
		return encode(type);
	};
	CHECK(encodeLegacy({CVQualifier::None}, ReferenceQualifier::LValueReference) == "RPi");
	CHECK(encodeLegacy({CVQualifier::Const}, ReferenceQualifier::LValueReference) == "RKPi");
	CHECK(encodeLegacy({CVQualifier::None, CVQualifier::None}, ReferenceQualifier::LValueReference) == "RPPi");
	CHECK(encodeLegacy({CVQualifier::None, CVQualifier::Const}, ReferenceQualifier::LValueReference) == "RKPPi");
	CHECK(encodeLegacy({CVQualifier::Const, CVQualifier::None}, ReferenceQualifier::LValueReference) == "RPKPi");
	CHECK(encodeLegacy({CVQualifier::ConstVolatile}, ReferenceQualifier::LValueReference) == "RVKPi");
	CHECK(encodeLegacy({CVQualifier::Const}, ReferenceQualifier::None) == "Pi");
	std::vector<CVQualifier> pointer_cv_levels;
	pointer_cv_levels.reserve(4096);
	TypeSpecifierNode deep_legacy(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<CVQualifier, 4> cv_pattern{
		CVQualifier::None, CVQualifier::Const, CVQualifier::Volatile, CVQualifier::ConstVolatile};
	for (size_t index = 0; index < 4096; ++index) {
		const CVQualifier cv = cv_pattern[index % cv_pattern.size()];
		pointer_cv_levels.push_back(cv);
		deep_legacy.add_pointer_level(cv);
	}
	deep_legacy.set_reference_qualifier(ReferenceQualifier::LValueReference);
	std::string expected_deep = "R";
	expected_deep.reserve(8194);
	for (size_t cycle = 0; cycle < 1024; ++cycle) {
		expected_deep += "VKPVPKPP";
	}
	expected_deep += 'i';
	CHECK(encode(deep_legacy) == expected_deep);
	std::vector<DeclaratorComponent> ordered_components;
	ordered_components.reserve(pointer_cv_levels.size() + 1);
	ordered_components.push_back(DeclaratorComponent::lvalueReference());
	for (auto level = pointer_cv_levels.rbegin(); level != pointer_cv_levels.rend(); ++level) {
		ordered_components.push_back(DeclaratorComponent::pointer(*level));
	}
	TypeSpecifierNode deep_ordered(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	deep_ordered.set_ordered_declarator(std::move(ordered_components));
	CHECK(encode(deep_ordered) == expected_deep);
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

TEST_CASE("Alias-normalized callable tails use canonical noexcept compatibility") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId short_type = table.builtin(CanonicalBuiltinKind::Short);
	const TypeId record_type = table.record(EntityId{941});
	const std::array<CanonicalTemplateArgKind, 1> parameter_kinds{CanonicalTemplateArgKind::Type};
	const TypeSpecifierNode int_spec(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	auto make_function = [&](TypeId result, TypeId argument, bool is_noexcept) {
		const std::array<TypeId, 1> parameters{argument};
		return table.function(result, parameters, false, CVQualifier::None, ReferenceQualifier::None, is_noexcept,
			CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	};
	auto make_alias = [&](std::string_view name, TypeId target) {
		const TemplateDeclId declaration = frontend.templateDecls().publishPrimaryAliasTemplate(OwnerId{1}, StringTable::getOrInternStringHandle(name));
		table.publishAliasTemplateTarget(declaration, target, parameter_kinds);
		TypeSpecifierNode spec(TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
		spec.set_alias_template_specialization(declaration, {int_spec});
		return spec;
	};
	const TypeId safe_function = make_function(int_type, short_type, true);
	const TypeId throwing_function = make_function(int_type, short_type, false);
	const TypeSpecifierNode safe = make_alias("native_tail_safe", table.pointer(safe_function));
	const TypeSpecifierNode throwing = make_alias("native_tail_throwing", table.pointer(throwing_function));
	auto check_plan = [&](const TypeSpecifierNode& source, const TypeSpecifierNode& target, bool valid, ConversionRank rank) {
		const auto plan = tryBuildCanonicalConversionFunctionTailPlan(source, target);
		REQUIRE(plan.has_value());
		CHECK(plan->is_valid == valid);
		if (valid) {
			CHECK(plan->rank == rank);
		}
	};
	check_plan(safe, safe, true, ConversionRank::ExactMatch);
	check_plan(safe, throwing, true, ConversionRank::QualificationAdjustment);
	check_plan(throwing, safe, false, ConversionRank::NoMatch);
	check_plan(safe, make_alias("native_tail_argument", table.pointer(make_function(int_type, int_type, false))), false, ConversionRank::NoMatch);
	check_plan(safe, make_alias("native_tail_result", table.pointer(make_function(record_type, short_type, false))), false, ConversionRank::NoMatch);
	check_plan(safe, make_alias("native_tail_depth", table.pointer(table.pointer(throwing_function))), false, ConversionRank::NoMatch);
	const TypeId owner = table.record(EntityId{942});
	const TypeSpecifierNode safe_member = make_alias("native_tail_safe_member", table.memberFunctionPointer(owner, safe_function));
	const TypeSpecifierNode throwing_member = make_alias("native_tail_throwing_member", table.memberFunctionPointer(owner, throwing_function));
	check_plan(safe_member, safe_member, true, ConversionRank::ExactMatch);
	check_plan(safe_member, throwing_member, true, ConversionRank::QualificationAdjustment);
	check_plan(throwing_member, safe_member, false, ConversionRank::NoMatch);
	check_plan(safe_member, make_alias("native_tail_member_argument", table.memberFunctionPointer(owner, make_function(int_type, int_type, false))),
		false, ConversionRank::NoMatch);
	TypeSpecifierNode corrupted = safe;
	corrupted.set_type_index(nativeTypeIndex(TypeCategory::Double));
	FunctionSignature stale_signature;
	stale_signature.setReturnType(makeFunctionTypeFromSpecifier(int_spec));
	corrupted.set_function_signature(stale_signature);
	check_plan(corrupted, throwing, true, ConversionRank::QualificationAdjustment);
	corrupted.clear_template_specialization();
	const auto mutated = tryBuildCanonicalConversionFunctionTailPlan(corrupted, throwing);
	CHECK((!mutated.has_value() || !mutated->is_valid));
	TypeSpecifierNode dependent_argument(TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	dependent_argument.set_template_parameter_decl(safe.specialization_template_decl(), 0);
	TypeSpecifierNode dependent = safe;
	dependent.set_alias_template_specialization(safe.specialization_template_decl(), {dependent_argument});
	CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(dependent, throwing).has_value());
	const TypeId dependent_result = table.templateParameter(safe.specialization_template_decl(), 0);
	const TypeSpecifierNode dependent_signature = make_alias("native_tail_dependent", table.pointer(make_function(dependent_result, short_type, true)));
	CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(dependent_signature, throwing).has_value());
	check_plan(dependent_signature, dependent_signature, true, ConversionRank::ExactMatch);
	const std::array<TypeId, 1> short_parameters{short_type};
	const TypeId opaque_function = table.function(int_type, short_parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{701});
	CHECK_FALSE(tryBuildCanonicalConversionFunctionTailPlan(make_alias("native_tail_opaque", table.pointer(opaque_function)), throwing).has_value());
	TemplateDeclId previous = safe.specialization_template_decl();
	for (size_t depth = 0; depth < 4096; ++depth) {
		const StringHandle layer_name = StringTable::getOrInternStringHandle(StringBuilder().append("Layer").append(depth).commit());
		const TemplateDeclId layer = frontend.templateDecls().publishPrimaryAliasTemplate(OwnerId{1}, layer_name);
		const std::array<TypeId, 1> arguments{table.templateParameter(layer, 0)};
		table.publishAliasTemplateTarget(layer, table.aliasTemplateSpecialization(previous, arguments), parameter_kinds);
		previous = layer;
	}
	TypeSpecifierNode deep = safe;
	deep.set_alias_template_specialization(previous, {int_spec});
	check_plan(deep, throwing, true, ConversionRank::QualificationAdjustment);
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
		tryBuildCanonicalReferenceBindingPlan(noexcept_pointer, const_throwing_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(relaxation_plan.has_value());
	CHECK(relaxation_plan->is_valid);
	CHECK(relaxation_plan->kind ==
		StandardConversionKind::QualificationAdjustment);

	TypeSpecifierNode nonconst_throwing_reference =
		make_function_pointer(throwing_signature);
	nonconst_throwing_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> nonconst_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(noexcept_pointer, nonconst_throwing_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(nonconst_reference_plan.has_value());
	CHECK_FALSE(nonconst_reference_plan->is_valid);

	TypeSpecifierNode noexcept_reference =
		make_function_pointer(noexcept_signature);
	noexcept_reference.set_cv_qualifier(CVQualifier::Const);
	noexcept_reference.set_reference_qualifier(
		ReferenceQualifier::LValueReference);
	const std::optional<ConversionPlan> reverse_plan =
		tryBuildCanonicalReferenceBindingPlan(make_function_pointer(throwing_signature), noexcept_reference, CanonicalReferenceBindingContext::Argument);
	REQUIRE(reverse_plan.has_value());
	CHECK_FALSE(reverse_plan->is_valid);

	TypeSpecifierNode throwing_rvalue_reference =
		make_function_pointer(throwing_signature);
	throwing_rvalue_reference.set_reference_qualifier(
		ReferenceQualifier::RValueReference);
	const std::optional<ConversionPlan> rvalue_reference_plan =
		tryBuildCanonicalReferenceBindingPlan(noexcept_pointer, throwing_rvalue_reference, CanonicalReferenceBindingContext::Argument);
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

TEST_CASE("Completed member pointer owners publish base schemas before ranking") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	const std::string code = R"(
struct Base { int member; };
struct Unfinished;
template<class T> struct Primary : Base { T payload; };
template<class T> struct Partial;
template<class T> struct Partial<T*> : Base { T payload; };
template<> struct Partial<int> : Base {};
struct Late;
using EarlyMember = int Late::*;
struct Late : Base {};
using PartialOwner = Partial<long long*>;
using PrimaryOwner = Primary<long long>;
using FullOwner = Partial<int>;
static_assert(sizeof(PartialOwner) > 0);
static_assert(sizeof(PrimaryOwner) > 0);
static_assert(sizeof(FullOwner) > 0);
)";
	CompileContext compile_context;
	compile_context.setInputFile("member_owner_schema_completion.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	auto owner_type = [&](std::string_view name) {
		const TypeInfo* type_info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId base = owner_type("Base");
	const TypeId primary = owner_type("PrimaryOwner");
	const TypeId partial = owner_type("PartialOwner");
	const TypeId full = owner_type("FullOwner");
	const TypeId late = owner_type("Late");
	for (TypeId owner : {base, primary, partial, full, late}) {
		CAPTURE(owner.value);
		CHECK(table.hasClassBaseSchema(owner));
	}
	CHECK(classifyCanonicalDerivedBaseConversion(table, partial, base) == DerivedBaseConversionKind::UniquePublicNonVirtual);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId source = table.memberObjectPointer(base, integer);
	const TypeId target = table.memberObjectPointer(partial, integer);
	const size_t types_before = table.size();
	const auto plan = tryBuildCanonicalMemberPointerConversionPlan(table, source, target);
	REQUIRE(plan.has_value());
	CHECK(plan->is_valid);
	CHECK(table.size() == types_before);
	const TypeId unfinished = owner_type("Unfinished");
	CHECK_FALSE(table.hasClassBaseSchema(unfinished));
	const auto unavailable = tryBuildCanonicalMemberPointerConversionPlan(table, source, table.memberObjectPointer(unfinished, integer));
	REQUIRE(unavailable.has_value());
	CHECK_FALSE(unavailable->is_valid);
	CHECK_FALSE(table.hasClassBaseSchema(unfinished));
}

TEST_CASE("Completed classes publish finalized virtual-base ABI facts by TypeId") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	const std::string code = R"(
struct VirtualRoot { long long value; };
struct VirtualFirst { int first; };
struct VirtualSecond { long long second; };
struct OrdinaryDerived : virtual VirtualRoot { int tag; };
struct MultiVirtual : virtual VirtualFirst, virtual VirtualSecond { short tag; };
struct Padding { char byte; };
struct NonVirtualBase { long long value; };
struct NonVirtualDerived : Padding, NonVirtualBase { int tag; };
template<class T> struct TemplateBase { T value; };
template<class T> struct TemplateDerived : virtual TemplateBase<T> { short tag; };
using TemplateBaseOwner = TemplateBase<long long>;
using TemplateOwner = TemplateDerived<long long>;
static_assert(sizeof(OrdinaryDerived) > 0);
static_assert(sizeof(MultiVirtual) > 0);
static_assert(sizeof(NonVirtualDerived) > 0);
static_assert(sizeof(TemplateOwner) > 0);
)";
	CompileContext compile_context;
	compile_context.setInputFile("virtual_base_abi_facts.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	auto class_type = [&](std::string_view name) {
		const TypeInfo* type_info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	auto legacy_struct = [](std::string_view name) -> const StructTypeInfo* {
		const TypeInfo* type_info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(type_info != nullptr);
		const StructTypeInfo* struct_info = canonicalClassStructInfoFromTypeInfo(*type_info);
		REQUIRE(struct_info != nullptr);
		return struct_info;
	};

	const TypeId root = class_type("VirtualRoot");
	const TypeId ordinary = class_type("OrdinaryDerived");
	const StructTypeInfo* ordinary_info = legacy_struct("OrdinaryDerived");
	REQUIRE(ordinary_info->virtual_bases.size() == 1);
	REQUIRE(table.hasVirtualBaseSchema(ordinary));
	REQUIRE(table.virtualBaseCount(ordinary) == 1);
	CHECK(table.virtualBaseAt(ordinary, 0).type == root);
	CHECK(table.virtualBaseAt(ordinary, 0).offset_bytes == ordinary_info->virtual_bases[0].offset);
	const std::optional<uint32_t> ordinary_index = table.findVirtualBaseIndex(ordinary, root);
	REQUIRE(ordinary_index.has_value());
	CHECK(*ordinary_index == 0);

	const TypeId multi = class_type("MultiVirtual");
	const StructTypeInfo* multi_info = legacy_struct("MultiVirtual");
	REQUIRE(multi_info->virtual_bases.size() == 2);
	REQUIRE(table.virtualBaseCount(multi) == multi_info->virtual_bases.size());
	for (size_t index = 0; index < multi_info->virtual_bases.size(); ++index) {
		const TypeInfo* base_info = tryGetTypeInfo(multi_info->virtual_bases[index].type_index);
		REQUIRE(base_info != nullptr);
		const CanonicalTypeImport imported_base = importCanonicalClassTypeInfo(table, *base_info);
		REQUIRE(imported_base.status == CanonicalTypeImportStatus::Supported);
		const CanonicalVirtualBase entry = table.virtualBaseAt(multi, index);
		CHECK(entry.type == imported_base.type);
		CHECK(entry.offset_bytes == multi_info->virtual_bases[index].offset);
		const std::optional<uint32_t> entry_index = table.findVirtualBaseIndex(multi, imported_base.type);
		REQUIRE(entry_index.has_value());
		CHECK(*entry_index == index);
	}

	const TypeId nonvirtual_base = class_type("NonVirtualBase");
	const TypeId nonvirtual_derived = class_type("NonVirtualDerived");
	const StructTypeInfo* nonvirtual_info = legacy_struct("NonVirtualDerived");
	REQUIRE(nonvirtual_info->base_classes.size() == 2);
	CHECK(nonvirtual_info->base_classes[1].offset > 0);
	REQUIRE(table.hasVirtualBaseSchema(nonvirtual_derived));
	CHECK(table.virtualBaseCount(nonvirtual_derived) == 0);
	CHECK_FALSE(table.findVirtualBaseIndex(nonvirtual_derived, nonvirtual_base).has_value());
	CHECK_FALSE(table.findVirtualBaseIndex(nonvirtual_derived, nonvirtual_derived).has_value());

	const TypeId template_base = class_type("TemplateBaseOwner");
	const TypeId template_owner = class_type("TemplateOwner");
	REQUIRE(table.hasVirtualBaseSchema(template_owner));
	REQUIRE(table.virtualBaseCount(template_owner) == 1);
	CHECK(table.virtualBaseAt(template_owner, 0).type == template_base);
	CHECK(table.virtualBaseAt(template_owner, 0).offset_bytes > 0);
	const std::optional<uint32_t> template_index = table.findVirtualBaseIndex(template_owner, template_base);
	REQUIRE(template_index.has_value());
	CHECK(*template_index == 0);
}

TEST_CASE("Virtual-base ABI publication is transactional and rejects conflicting order") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId first = table.record(EntityId{98301});
	const TypeId second = table.record(EntityId{98302});
	const TypeId derived = table.record(EntityId{98303});
	for (TypeId base : {first, second}) {
		table.publishClassBaseSchema(base, {});
	}
	const std::array<CanonicalVirtualBase, 2> ordered{{CanonicalVirtualBase{second, 24}, CanonicalVirtualBase{first, 0}}};
	table.publishVirtualBaseSchema(derived, ordered);
	REQUIRE(table.hasVirtualBaseSchema(derived));
	REQUIRE(table.virtualBaseCount(derived) == 2);
	CHECK(table.virtualBaseAt(derived, 0).type == second);
	CHECK(table.virtualBaseAt(derived, 1).type == first);
	CHECK(table.findVirtualBaseIndex(derived, second) == std::optional<uint32_t>{0});
	CHECK(table.findVirtualBaseIndex(derived, first) == std::optional<uint32_t>{1});
	table.publishVirtualBaseSchema(derived, ordered);
	const std::array<CanonicalVirtualBase, 2> reversed{{CanonicalVirtualBase{first, 0}, CanonicalVirtualBase{second, 24}}};
	CHECK_THROWS_AS(table.publishVirtualBaseSchema(derived, reversed), InternalError);
	const std::array<CanonicalVirtualBase, 2> duplicate{{CanonicalVirtualBase{first, 0}, CanonicalVirtualBase{first, 8}}};
	CHECK_THROWS_AS(table.publishVirtualBaseSchema(derived, duplicate), InternalError);
	const TypeId rolled_back = table.record(EntityId{98304});
	{
		CanonicalTypeTransaction transaction(table);
		table.publishVirtualBaseSchema(rolled_back, ordered);
		REQUIRE(table.hasVirtualBaseSchema(rolled_back));
	}
	CHECK_FALSE(table.hasVirtualBaseSchema(rolled_back));
}

TEST_CASE("Canonical member pointer owner planning rejects absent schemas despite unrelated legacy proof") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_member_owner_proof.cpp");
	Lexer lexer("struct LegacyBase {}; struct LegacyDerived : LegacyBase {};");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId base = table.record(EntityId{98201});
	const TypeId derived = table.record(EntityId{98202});
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId source = table.memberObjectPointer(base, integer);
	const TypeId target = table.memberObjectPointer(derived, integer);
	const TypeInfo* legacy_base = findTypeByName(StringTable::getOrInternStringHandle("LegacyBase"));
	const TypeInfo* legacy_derived = findTypeByName(StringTable::getOrInternStringHandle("LegacyDerived"));
	REQUIRE(legacy_base != nullptr);
	REQUIRE(legacy_derived != nullptr);
	CHECK(hasUsablePublicDerivedBaseConversion(legacy_derived->registeredTypeIndex(), legacy_base->registeredTypeIndex()));
	const auto unavailable = tryBuildCanonicalMemberPointerConversionPlan(table, source, target);
	REQUIRE(unavailable.has_value());
	CHECK_FALSE(unavailable->is_valid);
	auto stale_owner_spec = [](EntityId owner, std::string_view name) {
		TypeSpecifierNode spec(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
		spec.add_pointer_level(CVQualifier::None);
		spec.set_member_class_name(StringTable::getOrInternStringHandle(name));
		spec.set_member_class_entity(owner);
		return spec;
	};
	TypeSpecifierNode source_spec = stale_owner_spec(table.recordEntity(base), "LegacyBase");
	TypeSpecifierNode target_spec = stale_owner_spec(table.recordEntity(derived), "LegacyDerived");
	CHECK(importCanonicalType(table, source_spec).type == source);
	CHECK(importCanonicalType(table, target_spec).type == target);
	const auto projectable = tryBuildCanonicalProjectableConversionPlan(source_spec, target_spec);
	REQUIRE(projectable.has_value());
	CHECK_FALSE(projectable->is_valid);
	CHECK_FALSE(buildConversionPlan(source_spec, target_spec).is_valid);
	source_spec.set_ordered_declarator({DeclaratorComponent::memberPointer(table.recordEntity(base), false, CVQualifier::None)});
	target_spec.set_ordered_declarator({DeclaratorComponent::memberPointer(table.recordEntity(derived), false, CVQualifier::None)});
	const auto ordered = tryBuildCanonicalOrderedConversionPlan(source_spec, target_spec);
	REQUIRE(ordered.has_value());
	CHECK_FALSE(ordered->is_valid);
	CHECK_FALSE(buildOrderedDeclaratorConversionPlan(source_spec, target_spec).is_valid);
	FunctionSignature signature;
	const TypeSpecifierNode int_spec(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	signature.setReturnType(makeFunctionTypeFromSpecifier(int_spec));
	auto stale_function_owner_spec = [&](EntityId owner, std::string_view name) {
		TypeSpecifierNode spec(TypeCategory::MemberFunctionPointer, TypeQualifier::None, 64, Token{}, CVQualifier::None);
		spec.set_member_class_name(StringTable::getOrInternStringHandle(name));
		spec.set_member_class_entity(owner);
		spec.set_function_signature(signature);
		return spec;
	};
	const TypeSpecifierNode source_function = stale_function_owner_spec(table.recordEntity(base), "LegacyBase");
	const TypeSpecifierNode target_function = stale_function_owner_spec(table.recordEntity(derived), "LegacyDerived");
	const auto function_plan = tryBuildCanonicalProjectableConversionPlan(source_function, target_function);
	REQUIRE(function_plan.has_value());
	CHECK_FALSE(function_plan->is_valid);
	CHECK_FALSE(buildConversionPlan(source_function, target_function).is_valid);
	table.publishClassBaseSchema(base, {});
	const std::array<CanonicalClassBase, 1> bases{CanonicalClassBase{base, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
	table.publishClassBaseSchema(derived, bases);
	const auto published = tryBuildCanonicalMemberPointerConversionPlan(table, source, target);
	REQUIRE(published.has_value());
	CHECK(published->is_valid);
	CHECK(published->rank == ConversionRank::Conversion);
	const TypeId unrelated = table.record(EntityId{98203});
	table.publishClassBaseSchema(unrelated, {});
	const auto rejected = tryBuildCanonicalMemberPointerConversionPlan(table, source, table.memberObjectPointer(unrelated, integer));
	REQUIRE(rejected.has_value());
	CHECK_FALSE(rejected->is_valid);
	TypeId deep_owner = derived;
	for (uint32_t depth = 0; depth < 4096; ++depth) {
		const TypeId next = table.record(EntityId{100000 + depth});
		const std::array<CanonicalClassBase, 1> next_bases{CanonicalClassBase{deep_owner, CanonicalAccess::Public,
			CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishClassBaseSchema(next, next_bases);
		deep_owner = next;
	}
	const auto deep = tryBuildCanonicalMemberPointerConversionPlan(table, source, table.memberObjectPointer(deep_owner, integer));
	REQUIRE(deep.has_value());
	CHECK(deep->is_valid);
	CHECK(deep->rank == ConversionRank::Conversion);
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

	auto publish_record = [&table](EntityId entity, std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity, std::span<const CanonicalRecordMember>{}, bases);
		publishTestClassBases(table, entity, bases);
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
		publishTestClassBases(table, entity, bases);
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
	auto publish_record = [&table](EntityId entity, std::span<const CanonicalRecordBase> bases) {
		table.publishRecordLayout(CanonicalRecordLayout{
			entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(bases.size()),
			CanonicalRecordLayoutFlags::None, 0});
		table.publishRecordFieldSchema(entity, std::span<const CanonicalRecordMember>{}, bases);
		publishTestClassBases(table, entity, bases);
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
		tryBuildCanonicalReferenceBindingPlan(base_member_pointer, const_derived_member_reference, CanonicalReferenceBindingContext::Argument);
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
			const_derived_member_function_reference, CanonicalReferenceBindingContext::Argument);
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

	TypeSpecifierNode argument_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	argument_type.add_pointer_level(CVQualifier::None);
	TemplateArgumentVector template_args;
	template_args.push_back(TemplateTypeArg(argument_type));

	FunctionType return_pattern;
	return_pattern.type_index = nativeTypeIndex(TypeCategory::Int);
	return_pattern.template_parameter_name = template_name;
	return_pattern.template_parameter_decl = TemplateDeclId{911};
	return_pattern.ordered_declarator_components = {
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
	};
	FunctionSignature signature;
	signature.setReturnType(return_pattern);

	const FunctionSignature substituted = substituteTemplateFunctionSignatureTypes(signature, template_params, template_args, frontend.canonicalTypes());
	const std::vector<DeclaratorComponent> expected_components = {
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(2),
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(3),
		DeclaratorComponent::pointer(CVQualifier::None),
	};
	const FunctionType& substituted_return = substituted.return_type();
	CHECK(substituted_return.type_index.category() == TypeCategory::Int);
	CHECK(substituted_return.ordered_declarator_components.empty());
	REQUIRE(substituted_return.canonical_type_id);
	FunctionType legacy_projection = substituted_return;
	legacy_projection.canonical_type_id = {};
	CHECK(FlashCpp::equalFunctionTypeIdentity(substituted_return, legacy_projection));
	CHECK(FlashCpp::hashFunctionTypeIdentity(substituted_return) == FlashCpp::hashFunctionTypeIdentity(legacy_projection));
	const CanonicalDeclaratorExport canonical_export = exportCanonicalDeclarator(frontend.canonicalTypes(), substituted_return.canonical_type_id);
	CHECK(canonical_export.status == CanonicalTypeImportStatus::Supported);
	CHECK(canonical_export.components == expected_components);
	FunctionType different_canonical_identity = substituted_return;
	different_canonical_identity.canonical_type_id = frontend.canonicalTypes().builtin(CanonicalBuiltinKind::Float);
	CHECK_FALSE(FlashCpp::equalFunctionTypeIdentity(substituted_return, different_canonical_identity));

	const TypeSpecifierNode substituted_type = typeSpecifierFromFunctionType(substituted_return);
	CHECK(std::ranges::equal(substituted_type.declarator_components(), expected_components));
	const CanonicalTypeImport imported = importCanonicalType(frontend.canonicalTypes(), substituted_type);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	const CanonicalDeclaratorExport exported = exportCanonicalDeclarator(frontend.canonicalTypes(), imported.type);
	CHECK(exported.status == CanonicalTypeImportStatus::Supported);
	CHECK(exported.components == expected_components);

	TypeSpecifierNode pointer_to_array(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	pointer_to_array.set_ordered_declarator({
		DeclaratorComponent::pointer(CVQualifier::None),
		DeclaratorComponent::array(4),
	});
	const TemplateTypeArg pointer_to_array_arg(pointer_to_array);
	const TypeInfo::TemplateArgInfo stored_arg = toTemplateArgInfo(pointer_to_array_arg);
	const TemplateTypeArg restored_arg = toTemplateTypeArg(stored_arg);
	CHECK(restored_arg.pointee_array_declarator);
	const TypeSpecifierNode restored_arg_type = typeSpecifierFromTemplateTypeArgProjection(restored_arg);
	CHECK(restored_arg_type.has_pointee_array_declarator());
	CHECK(std::ranges::equal(restored_arg_type.array_dimensions(), pointer_to_array.array_dimensions()));

	constexpr size_t nested_callable_depth = 2048;
	FunctionSignature deep_signature = signature;
	for (size_t depth = 0; depth < nested_callable_depth; ++depth) {
		FunctionType nested_callable;
		nested_callable.type_index = nativeTypeIndex(TypeCategory::FunctionPointer);
		nested_callable.callable_signature = std::make_shared<FunctionSignature>(std::move(deep_signature));
		FunctionSignature outer_signature;
		outer_signature.setReturnType(std::move(nested_callable));
		deep_signature = std::move(outer_signature);
	}
	const FunctionSignature substituted_deep_signature = substituteTemplateFunctionSignatureTypes(deep_signature, template_params,
		template_args, frontend.canonicalTypes());
	const FunctionType* deepest_return = &substituted_deep_signature.return_type();
	for (size_t depth = 0; depth < nested_callable_depth; ++depth) {
		REQUIRE(deepest_return->callable_signature != nullptr);
		deepest_return = &deepest_return->callable_signature->return_type();
	}
	CHECK(deepest_return->ordered_declarator_components.empty());
	CHECK(deepest_return->canonical_type_id == substituted_return.canonical_type_id);
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

TEST_CASE("Callable alias materialization retains its own return binding") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class Content> using Parameter = Content;
template<class Result, class Input> using Component = Result (*)(Parameter<Input>) noexcept;
template<class Result> using Bare = Result(short);
template<class Output> using Pointer = Output (*)(short) noexcept;
template<class Value> using Forwarded = Pointer<Value>;
template<class Yield> using Nested = Forwarded<Yield>;
template<class Outer> using Use = Nested<Outer>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("callable_alias_return_binding.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId long_type = table.builtin(CanonicalBuiltinKind::Long);
	const std::array<TypeId, 1> parameters{table.builtin(CanonicalBuiltinKind::Short)};
	const TypeSpecifierNode long_spec(TypeCategory::Long, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<TemplateTypeArg, 1> arguments{TemplateTypeArg(long_spec)};
	const TypeSpecifierNode short_spec(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::None);
	const std::array<TemplateTypeArg, 2> component_arguments{TemplateTypeArg(long_spec), TemplateTypeArg(short_spec)};
	for (std::string_view name : {"Bare", "Pointer", "Nested", "Component"}) {
		CAPTURE(name);
		const std::span<const TemplateTypeArg> bindings = name == "Component" ? std::span<const TemplateTypeArg>(component_arguments) : arguments;
		const auto materialized = parser.templateEngine().materializeAliasTemplateInstantiation(name, bindings);
		REQUIRE(materialized.resolved_type_specifier.has_value());
		const TypeSpecifierNode& concrete = *materialized.resolved_type_specifier;
		REQUIRE(concrete.has_function_signature());
		CHECK_FALSE(concrete.has_template_parameter_identity());
		const bool is_pointer = name != "Bare";
		TypeId expected = table.function(long_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, is_pointer,
			CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
		if (is_pointer) {
			expected = table.pointer(expected);
		}
		const auto imported = importCanonicalType(table, concrete);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		CHECK(imported.type == expected);
		CHECK_FALSE(concrete.function_signature().parameter_types().front().template_parameter_name.isValid());
		FunctionSignature corrupted = concrete.function_signature();
		corrupted.updateReturnType([](FunctionType& result) { result.type_index = nativeTypeIndex(TypeCategory::Double); });
		TypeSpecifierNode stale = concrete;
		stale.set_function_signature(corrupted);
		CHECK(importCanonicalType(table, stale).type == expected);
		corrupted.updateReturnType([](FunctionType& result) { result.canonical_type_id = {}; });
		stale.set_function_signature(corrupted);
		CHECK(importCanonicalType(table, stale).type != expected);
	}
	const OwnerId namespace_owner = ownerIdFromNamespaceHandle(gSymbolTable.get_current_namespace_handle());
	const auto pointer_alias = context.templateDecls().findPrimaryAliasTemplate(namespace_owner, StringTable::getOrInternStringHandle("Pointer"));
	REQUIRE(pointer_alias.has_value());
	TypeSpecifierNode dependent(TypeCategory::Template, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	dependent.set_template_parameter_identity(StringTable::getOrInternStringHandle("Foreign"));
	TemplateTypeArg dependent_argument(dependent);
	dependent_argument.is_dependent = true;
	dependent_argument.dependent_name = dependent.template_parameter_name();
	const std::array<TemplateTypeArg, 1> dependent_arguments{dependent_argument};
	const auto deferred = parser.templateEngine().materializeAliasTemplateInstantiation("Pointer", dependent_arguments);
	REQUIRE(deferred.resolved_type_specifier.has_value());
	CHECK(deferred.resolved_type_specifier->is_alias_template_specialization());
	CHECK(deferred.resolved_type_specifier->specialization_template_decl() == *pointer_alias);
	CHECK_FALSE(deferred.resolved_type_specifier->has_function_signature());
	const auto use_alias = context.templateDecls().findPrimaryAliasTemplate(namespace_owner, StringTable::getOrInternStringHandle("Use"));
	REQUIRE(use_alias.has_value());
	const auto use_pattern = context.templateDecls().primaryAliasPattern(*use_alias);
	REQUIRE(use_pattern.has_value());
	const std::unordered_map<std::string_view, TemplateTypeArg> bindings{{"Outer", TemplateTypeArg(long_spec)}};
	ExpressionSubstitutor substitutor(bindings, parser);
	const uint64_t scratch_before = context.scratchArena().currentBytes();
	const uint64_t discarded_before = context.scratchArena().discardedBytes();
	const ASTNode substituted_node = substitutor.substitute(ASTNode::emplace_node<TypeSpecifierNode>(
		use_pattern->as<TemplateAliasNode>().target_type_node()));
	REQUIRE(substituted_node.is<TypeSpecifierNode>());
	const TypeSpecifierNode& substituted = substituted_node.as<TypeSpecifierNode>();
	CHECK(context.scratchArena().currentBytes() == scratch_before);
	CHECK(context.scratchArena().discardedBytes() > discarded_before);
	const auto imported_substitution = importCanonicalType(table, substituted);
	REQUIRE(imported_substitution.status == CanonicalTypeImportStatus::Supported);
	const TypeId expected_function = table.function(long_type, parameters, false, CVQualifier::None, ReferenceQualifier::None, true,
		CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	CHECK(imported_substitution.type == table.pointer(expected_function));

}

TEST_CASE("Callable alias return binding walks deep canonical wrappers iteratively") {
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	const TemplateDeclId owner{711};
	const StringHandle result_name = StringTable::getOrInternStringHandle("Result");
	FunctionType result;
	result.type_index = TypeIndex{0, TypeCategory::Template};
	result.template_parameter_name = result_name;
	result.template_parameter_decl = owner;
	result.template_parameter_index = 0;
	result.canonical_type_id = table.templateParameter(owner, 0);
	TypeId expected_result = table.builtin(CanonicalBuiltinKind::Long);
	for (size_t depth = 0; depth < 4096; ++depth) {
		result.canonical_type_id = table.pointer(result.canonical_type_id);
		expected_result = table.pointer(expected_result);
	}
	FunctionSignature signature;
	signature.setReturnType(result);
	const std::array<TemplateParameterNode, 1> parameters{TemplateParameterNode(result_name, Token{})};
	const TypeSpecifierNode long_spec(TypeCategory::Long, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<TemplateTypeArg, 1> arguments{TemplateTypeArg(long_spec)};
	const FunctionSignature substituted = substituteTemplateFunctionSignatureTypes(signature, parameters, arguments, table, owner);
	CHECK(substituted.return_type().canonical_type_id == expected_result);
	const auto imported = importCanonicalFunctionTypeComponent(table, substituted.return_type(), CanonicalTypeImportContext::Exact);
	CHECK(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(imported.type == expected_result);
}

TEST_CASE("Mixed callable alias bindings retain value positions and canonical component authority") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> using Parameter = T;
template<int N, class R, bool B, class A> using Callback = R (*)(Parameter<A>) noexcept;
)";
	CompileContext test_context;
	test_context.setInputFile("mixed_callable_alias.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeSpecifierNode long_spec(TypeCategory::Long, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const TypeSpecifierNode short_spec(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::None);
	TemplateTypeArg count(7);
	count.nttp_expr = context.dependentExpressions().intern(ASTNode::emplace_node<ExpressionNode>(
		NumericLiteralNode(Token{}, 7ull, TypeCategory::Int, TypeQualifier::None, 32)));
	TemplateTypeArg flag(1, TypeCategory::Bool);
	flag.nttp_expr = context.dependentExpressions().intern(ASTNode::emplace_node<ExpressionNode>(BoolLiteralNode(Token{}, true)));
	const std::array<TemplateTypeArg, 4> arguments{count, TemplateTypeArg(long_spec), flag, TemplateTypeArg(short_spec)};
	const auto materialized = parser.templateEngine().materializeAliasTemplateInstantiation("Callback", arguments);
	REQUIRE(materialized.resolved_type_specifier.has_value());
	const TypeSpecifierNode& concrete = *materialized.resolved_type_specifier;
	REQUIRE(concrete.has_function_signature());
	const std::array<TypeId, 1> parameter_types{table.builtin(CanonicalBuiltinKind::Short)};
	const TypeId expected_function = table.function(table.builtin(CanonicalBuiltinKind::Long), parameter_types, false, CVQualifier::None,
		ReferenceQualifier::None, true, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId expected = table.pointer(expected_function);
	const auto imported = importCanonicalType(table, concrete);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(imported.type == expected);
	FunctionSignature corrupted = concrete.function_signature();
	corrupted.updateParameterTypes([](auto& parameters) { parameters.front().type_index = nativeTypeIndex(TypeCategory::Double); });
	TypeSpecifierNode stale = concrete;
	stale.set_function_signature(corrupted);
	CHECK(importCanonicalType(table, stale).type == expected);
	corrupted.updateParameterTypes([](auto& parameters) { parameters.front().canonical_type_id = {}; });
	stale.set_function_signature(corrupted);
	CHECK(importCanonicalType(table, stale).type != expected);
	const OwnerId namespace_owner = ownerIdFromNamespaceHandle(gSymbolTable.get_current_namespace_handle());
	const auto alias = context.templateDecls().findPrimaryAliasTemplate(namespace_owner, StringTable::getOrInternStringHandle("Callback"));
	REQUIRE(alias.has_value());
	const auto pattern = context.templateDecls().primaryAliasPattern(*alias);
	REQUIRE(pattern.has_value());
	const TemplateAliasNode& alias_node = pattern->as<TemplateAliasNode>();
	for (bool unexpanded_pack : {false, true}) {
		auto deferred_arguments = arguments;
		if (unexpanded_pack) {
			deferred_arguments.front().is_pack = true;
		} else {
			deferred_arguments.front().nttp_expr = {};
		}
		const FunctionSignature deferred = substituteTemplateFunctionSignatureTypes(alias_node.target_type_node().function_signature(),
			alias_node.template_parameters(), deferred_arguments, table, *alias);
		CHECK(table.hasDependentTypeComponents(deferred.parameter_types().front().canonical_type_id));
	}
	FunctionSignature deep = alias_node.target_type_node().function_signature();
	TypeId expected_parameter = parameter_types.front();
	deep.updateParameterTypes([&](auto& parameters) {
		REQUIRE(parameters.front().canonical_type_id);
		for (size_t depth = 0; depth < 4096; ++depth) {
			parameters.front().canonical_type_id = table.pointer(parameters.front().canonical_type_id);
			expected_parameter = table.pointer(expected_parameter);
		}
	});
	const FunctionSignature substituted = substituteTemplateFunctionSignatureTypes(deep, alias_node.template_parameters(), arguments, table, *alias);
	CHECK(substituted.parameter_types().front().canonical_type_id == expected_parameter);
	const auto imported_parameter = importCanonicalFunctionTypeComponent(table, substituted.parameter_types().front(),
		CanonicalTypeImportContext::Exact);
	REQUIRE(imported_parameter.status == CanonicalTypeImportStatus::Supported);
	CHECK(imported_parameter.type == expected_parameter);
	const std::array<CanonicalTemplateArgument, 4> canonical_arguments{
		CanonicalTemplateArgument::makeNonType(count.nttp_expr), CanonicalTemplateArgument::makeType(table.builtin(CanonicalBuiltinKind::Long)),
		CanonicalTemplateArgument::makeNonType(flag.nttp_expr), CanonicalTemplateArgument::makeType(parameter_types.front())};
	CHECK(table.substitute(table.templateParameter(*alias, 3), *alias, canonical_arguments) == parameter_types.front());
	CHECK_THROWS_AS(table.substitute(table.templateParameter(*alias, 0), *alias, canonical_arguments), InternalError);
	std::array<CanonicalTemplateArgument, 4> invalid = canonical_arguments;
	invalid.front().expr = {};
	CHECK_THROWS_AS(table.substitute(table.templateParameter(*alias, 3), *alias, invalid), InternalError);
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

TEST_CASE("Callable alias parameters retain their enclosing declaration identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class Value> using Parameter = Value;
template<class Argument> using Callback = long (*)(Parameter<Argument>);
template<class Element> using ChainedCallback = Callback<Element>;
)";
	CompileContext test_context;
	test_context.setInputFile("callable_alias_parameter_owner.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(test_context, gSymbolTable);
	Parser parser(lexer, test_context, sema);
	REQUIRE(!parser.parse().is_error());
	const OwnerId owner = ownerIdFromNamespaceHandle(gSymbolTable.get_current_namespace_handle());
	const auto parameter = context.templateDecls().findPrimaryAliasTemplate(owner, StringTable::getOrInternStringHandle("Parameter"));
	const auto callback = context.templateDecls().findPrimaryAliasTemplate(owner, StringTable::getOrInternStringHandle("Callback"));
	const auto chained = context.templateDecls().findPrimaryAliasTemplate(owner, StringTable::getOrInternStringHandle("ChainedCallback"));
	REQUIRE(parameter.has_value());
	REQUIRE(callback.has_value());
	REQUIRE(chained.has_value());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto callback_target = table.aliasTemplateTarget(*callback);
	REQUIRE(callback_target.has_value());
	CHECK(table.dependsOnlyOnTemplateParameters(*callback_target, *callback));
	REQUIRE(table.node(*callback_target).kind == CanonicalTypeKind::Pointer);
	const TypeId function = table.node(*callback_target).child;
	REQUIRE(table.node(function).kind == CanonicalTypeKind::Function);
	const TypeId nested_alias = table.functionParameterType(table.functionParameters(function));
	REQUIRE(table.node(nested_alias).kind == CanonicalTypeKind::AliasTemplateSpecialization);
	CHECK(table.templateSpecializationDecl(nested_alias) == *parameter);
	const TypeId argument = table.templateArgumentType(table.templateSpecializationArguments(nested_alias));
	REQUIRE(table.node(argument).kind == CanonicalTypeKind::TemplateParameter);
	CHECK(table.templateParameterDecl(argument) == *callback);
	CHECK(table.templateParameterIndex(argument) == 0u);
	const auto pattern = context.templateDecls().primaryAliasPattern(*callback);
	REQUIRE(pattern.has_value());
	const FunctionSignature& signature = pattern->as<TemplateAliasNode>().target_type_node().function_signature();
	REQUIRE(!signature.parameter_types().empty());
	FunctionType corrupted_projection = signature.parameter_types().front();
	corrupted_projection.template_parameter_decl = *parameter;
	corrupted_projection.template_parameter_index = 0;
	corrupted_projection.type_index = nativeTypeIndex(TypeCategory::Double);
	const auto authoritative = importCanonicalFunctionTypeComponent(table, corrupted_projection, CanonicalTypeImportContext::Exact);
	CHECK(authoritative.status == CanonicalTypeImportStatus::Supported);
	CHECK(authoritative.type == nested_alias);
	corrupted_projection.canonical_type_id = {};
	const auto mutated = importCanonicalFunctionTypeComponent(table, corrupted_projection, CanonicalTypeImportContext::Exact);
	CHECK(mutated.type != nested_alias);
	TypeId deep_argument = table.builtin(CanonicalBuiltinKind::Int);
	for (size_t depth = 0; depth < 4096; ++depth) {
		deep_argument = table.pointer(deep_argument);
	}
	const std::vector<TypeId> concrete_types{
		table.builtin(CanonicalBuiltinKind::Short), table.builtin(CanonicalBuiltinKind::Int), table.record(EntityId{901}), deep_argument};
	for (const TemplateDeclId alias : {*callback, *chained}) {
		const auto target = table.aliasTemplateTarget(alias);
		REQUIRE(target.has_value());
		CHECK(table.dependsOnlyOnTemplateParameters(*target, alias));
		for (const TypeId concrete : concrete_types) {
			const std::span<const TypeId> arguments(&concrete, 1);
			const TypeId resolved = table.resolveAliasTemplateSpecializations(table.substitute(*target, alias, arguments));
			const TypeId expected_function = table.function(table.builtin(CanonicalBuiltinKind::Long), arguments, false, CVQualifier::None,
				ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
			CHECK(resolved == table.pointer(expected_function));
		}
	}
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

TEST_CASE("Member function ref qualifiers are retained by ABI mangling") {
	struct ManglingStyleGuard {
		NameMangling::ManglingStyle original = NameMangling::g_mangling_style;
		~ManglingStyleGuard() { NameMangling::g_mangling_style = original; }
	} style_guard;
	const TypeSpecifierNode return_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const std::array<TypeSpecifierNode, 0> param_types{};
	const std::array<ASTNode, 0> param_nodes{};
	const StringHandle struct_name = StringTable::getOrInternStringHandle("A");
	const std::array<std::string_view, 0> namespace_path{};
	auto mangle_types = [&](bool is_const, ReferenceQualifier ref_qualifier) {
		return NameMangling::generateMangledName("g", return_type, param_types, false, struct_name, namespace_path,
			Linkage::CPlusPlus, is_const, ref_qualifier, false, NameMangling::ConstructorVariant::Complete).view();
	};
	auto mangle_nodes = [&](bool is_const, ReferenceQualifier ref_qualifier) {
		return NameMangling::generateMangledName("g", return_type, param_nodes, false, struct_name, namespace_path,
			Linkage::CPlusPlus, is_const, ref_qualifier, false, NameMangling::ConstructorVariant::Complete).view();
	};
	NameMangling::g_mangling_style = NameMangling::ManglingStyle::MSVC;
	CHECK(mangle_types(false, ReferenceQualifier::None) == "?g@A@@QEAAHXZ");
	CHECK(mangle_nodes(false, ReferenceQualifier::None) == "?g@A@@QEAAHXZ");
	CHECK(mangle_types(true, ReferenceQualifier::None) == "?g@A@@QEBAHXZ");
	CHECK(mangle_nodes(true, ReferenceQualifier::None) == "?g@A@@QEBAHXZ");
	CHECK(mangle_types(false, ReferenceQualifier::LValueReference) == "?g@A@@QEGAAHXZ");
	CHECK(mangle_nodes(false, ReferenceQualifier::LValueReference) == "?g@A@@QEGAAHXZ");
	CHECK(mangle_types(false, ReferenceQualifier::RValueReference) == "?g@A@@QEHAAHXZ");
	CHECK(mangle_nodes(false, ReferenceQualifier::RValueReference) == "?g@A@@QEHAAHXZ");
	CHECK(mangle_types(true, ReferenceQualifier::LValueReference) == "?g@A@@QEGBAHXZ");
	CHECK(mangle_nodes(true, ReferenceQualifier::LValueReference) == "?g@A@@QEGBAHXZ");
	CHECK(mangle_types(true, ReferenceQualifier::RValueReference) == "?g@A@@QEHBAHXZ");
	CHECK(mangle_nodes(true, ReferenceQualifier::RValueReference) == "?g@A@@QEHBAHXZ");
	CHECK(NameMangling::generateMangledName("g", return_type, param_types, false, StringHandle{}, namespace_path,
		Linkage::CPlusPlus, false, ReferenceQualifier::None, false, NameMangling::ConstructorVariant::Complete).view() ==
		"?g@@YAHXZ");
	NameMangling::g_mangling_style = NameMangling::ManglingStyle::Itanium;
	CHECK(mangle_types(false, ReferenceQualifier::None) == "_ZN1A1gEv");
	CHECK(mangle_nodes(false, ReferenceQualifier::None) == "_ZN1A1gEv");
	CHECK(mangle_types(true, ReferenceQualifier::None) == "_ZNK1A1gEv");
	CHECK(mangle_nodes(true, ReferenceQualifier::None) == "_ZNK1A1gEv");
	CHECK(mangle_types(false, ReferenceQualifier::LValueReference) == "_ZNR1A1gEv");
	CHECK(mangle_nodes(false, ReferenceQualifier::LValueReference) == "_ZNR1A1gEv");
	CHECK(mangle_types(false, ReferenceQualifier::RValueReference) == "_ZNO1A1gEv");
	CHECK(mangle_nodes(false, ReferenceQualifier::RValueReference) == "_ZNO1A1gEv");
	CHECK(mangle_types(true, ReferenceQualifier::LValueReference) == "_ZNKR1A1gEv");
	CHECK(mangle_nodes(true, ReferenceQualifier::LValueReference) == "_ZNKR1A1gEv");
	CHECK(mangle_types(true, ReferenceQualifier::RValueReference) == "_ZNKO1A1gEv");
	CHECK(mangle_nodes(true, ReferenceQualifier::RValueReference) == "_ZNKO1A1gEv");
	CHECK(NameMangling::generateMangledName("g", return_type, param_types, false, StringHandle{}, namespace_path,
		Linkage::CPlusPlus, false, ReferenceQualifier::None, false, NameMangling::ConstructorVariant::Complete).view() ==
		"_Z1gv");
}

TEST_CASE("Native binary operator publication owns its symbol and canonical signature") {
	MESSAGE("native publication node sizes: binary=" << sizeof(BinaryOperatorNode) << " target=" << sizeof(BinaryOperatorCallTarget));
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear();
	gConceptRegistry.clear();
	gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_binary_operator_publication.cpp");
	Lexer lexer("struct Value { long long operator+(short&) const; };");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	const TypeInfo* value_info = findTypeByName(StringTable::getOrInternStringHandle("Value"));
	REQUIRE(value_info != nullptr);
	TypeSpecifierNode value(TypeCategory::Struct, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	value.set_type_index(value_info->registeredTypeIndex());
	tryBindPublishedTypeEntity(value);
	REQUIRE(value.has_type_entity());
	TypeSpecifierNode short_ref(TypeCategory::Short, TypeQualifier::None, 16, Token{}, CVQualifier::None);
	short_ref.set_reference_qualifier(ReferenceQualifier::LValueReference);
	const OperatorOverloadResult overload = findBinaryOperatorOverload(value, short_ref, OverloadableOperator::Plus, nullptr);
	REQUIRE(overload.has_match);
	REQUIRE(overload.member_overload != nullptr);
	BinaryOperatorNode binary(Token{}, ASTNode{}, ASTNode{});
	binary.set_resolved_member_operator_overload(overload.member_overload, overload.declaring_type, overload.compatibility_declaring_type_index);
	const BinaryOperatorCallTarget first = sema.ensureBinaryOperatorCallTarget(binary);
	REQUIRE(first.symbol.isValid());
	const TypeSpecifierNode published_return = materializeCanonicalTypeSpecifier(sema.typeContext().get(first.return_type));
	CHECK(published_return.category() == TypeCategory::LongLong);
	const TypeSpecifierNode published_parameter = materializeCanonicalTypeSpecifier(sema.typeContext().get(first.parameter_types[0]));
	CHECK(published_parameter.category() == TypeCategory::Short);
	CHECK(published_parameter.reference_qualifier() == ReferenceQualifier::LValueReference);
	FunctionDeclarationNode& function = const_cast<FunctionDeclarationNode&>(
		overload.member_overload->function_decl.as<FunctionDeclarationNode>());
	function.decl_node().type_specifier_node() = TypeSpecifierNode(TypeCategory::Double, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	function.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node() =
		TypeSpecifierNode(TypeCategory::Long, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	function.set_mangled_name("poisoned_operator_symbol");
	const BinaryOperatorCallTarget second = sema.ensureBinaryOperatorCallTarget(binary);
	CHECK(second.symbol == first.symbol);
	CHECK(second.return_type == first.return_type);
	CHECK(second.parameter_types == first.parameter_types);
	CHECK(frontend.binaryOperatorCallTarget(binary.resolved_operator_call_target()).symbol == first.symbol);
}

TEST_CASE("Inline typedef records publish canonical declaration owners before assignment selection") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_typedef_record_owners.cpp");
	Lexer lexer("typedef struct Named { short value; } Named; typedef union { long long value; char byte; } Choice;");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	for (const std::string_view name : {"Named", "Choice"}) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const StructTypeInfo* record = canonicalClassStructInfoFromTypeInfo(*info);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->declaration_node->has_entity_id());
		const CanonicalTypeImport imported = importCanonicalClassDeclaration(frontend.canonicalTypes(), *record->declaration_node);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		CHECK(frontend.canonicalTypes().recordEntity(imported.type) == record->declaration_node->entity_id());
		CHECK(frontend.canonicalTypes().hasClassBaseSchema(imported.type));
	}
}

TEST_CASE("Unqualified base template instantiation retains the persistent declaration namespace") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_template_owner_namespace.cpp");
	Lexer lexer(R"(
namespace home {
	template<class T> struct Base { T value; };
	template<class T> struct Derived : Base<T> {};
}
home::Derived<short> object;
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	const std::optional<ASTNode> object_symbol = gSymbolTable.lookup("object");
	REQUIRE(object_symbol.has_value());
	const DeclarationNode* object = get_decl_from_symbol(*object_symbol);
	REQUIRE(object != nullptr);
	const TypeInfo* derived_info = tryGetTypeInfo(object->type_specifier_node().type_index());
	REQUIRE(derived_info != nullptr);
	const StructTypeInfo* derived = derived_info->getStructInfo();
	REQUIRE(derived != nullptr);
	REQUIRE(derived->base_classes.size() == 1);
	const TypeInfo* base = tryGetTypeInfo(derived->base_classes[0].type_index);
	REQUIRE(base != nullptr);
	const NamespaceHandle home = gNamespaceRegistry.lookupNamespace(NamespaceRegistry::GLOBAL_NAMESPACE,
		StringTable::getOrInternStringHandle("home"));
	REQUIRE(home.isValid());
	CHECK(base->namespaceHandle() == home);
	CHECK(base->sourceNamespace() == home);
}
