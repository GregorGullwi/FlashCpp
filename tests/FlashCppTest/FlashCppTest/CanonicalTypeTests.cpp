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
	CHECK_FALSE(buildConversionPlan(member_object, bool_target).is_valid);
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
	const auto stable = context.canonicalTypes().builtin(CanonicalBuiltinKind::Double);
	const TypeSpecifierNode integer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	{
		auto outer = context.beginScratchTransaction();
		{
			auto inner = context.beginScratchTransaction();
			builder.internDeclaratorType(integer);
			inner.commit();
		}
		CHECK(builder.telemetryDeclaratorInternCount() == 1);
		outer.rollback();
	}
	CHECK(builder.telemetryDeclaratorInternCount() == 0);
	CHECK(context.canonicalTypes().size() == 1);
	CHECK(context.canonicalTypes().builtin(CanonicalBuiltinKind::Double) == stable);
	CHECK(builder.internDeclaratorType(integer).value == 1);
}
