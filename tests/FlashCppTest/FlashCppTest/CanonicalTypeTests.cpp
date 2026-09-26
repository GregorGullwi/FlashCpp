#include "CompilerIncludes.h"
#include <fstream>
#include "doctest.h"
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

	TypeSpecifierNode prvalue = pointer_object;
	CHECK_FALSE(buildConversionPlan(prvalue, lvalue_reference).is_valid);

	TypeSpecifierNode const_pointer_object = pointer_object;
	const_pointer_object.set_cv_qualifier(CVQualifier::Const);
	TypeSpecifierNode const_lvalue_reference = const_pointer_object;
	const_lvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::lvalueReference());
	const ConversionPlan const_plan =
		buildConversionPlan(const_pointer_object, const_lvalue_reference);
	CHECK(const_plan.is_valid);
	CHECK(const_plan.rank == ConversionRank::ExactMatch);

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
	CHECK(qualified_plan.rank == ConversionRank::QualificationAdjustment);

	TypeSpecifierNode rvalue_reference = pointer_object;
	rvalue_reference.prepend_ordered_declarator_component(
		DeclaratorComponent::rvalueReference());
	const ConversionPlan rvalue_plan =
		buildConversionPlan(prvalue, rvalue_reference);
	CHECK(rvalue_plan.is_valid);
	CHECK(rvalue_plan.rank == ConversionRank::ExactMatch);
	CHECK_FALSE(buildConversionPlan(lvalue_argument, rvalue_reference).is_valid);

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

TEST_CASE("Ordered function objects decay to pointers") {
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

	TypeSpecifierNode mismatched_parameter = function_pointer;
	mismatched_parameter.set_function_signature(other_signature);
	CHECK_FALSE(buildConversionPlan(function_object, mismatched_parameter).is_valid);

	TypeSpecifierNode bool_target(
		TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	const ConversionPlan bool_plan = buildConversionPlan(function_object, bool_target);
	CHECK(bool_plan.is_valid);
	CHECK(bool_plan.kind == StandardConversionKind::BooleanConversion);
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
		DeclaratorComponent::memberPointer(EntityId{}, false, CVQualifier::None),
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
