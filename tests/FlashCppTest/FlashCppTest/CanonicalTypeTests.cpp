#include <fstream>
#include "CompilerIncludes.h"
#include "TemplateExpressionEquivalence.h"
#include "doctest.h"
#include "CanonicalTypeAdapter.h"
#include "ExpressionSubstitutor.h"
#include "ParserTemplateClassShared.h"
#include "ParserTemplateHelpers.h"
#include "../../architecture/CanonicalTypeTests.h"

TEST_CASE("Published free function targets retain entity and canonical signature identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_function_targets.cpp");
	Lexer lexer(R"(
int target(short value);
int target(short value) { return value; }
long long target(long long value) { return value; }
int safe(short value) noexcept { return value; }
namespace other { int target(short value) { return value; } }
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const auto candidates = gSymbolTable.lookup_all("target");
	REQUIRE(candidates.size() == 2);
	const FunctionDeclarationNode* narrow = nullptr;
	const FunctionDeclarationNode* wide = nullptr;
	for (const ASTNode& candidate : candidates) {
		REQUIRE(candidate.is<FunctionDeclarationNode>());
		const FunctionDeclarationNode& function = candidate.as<FunctionDeclarationNode>();
		REQUIRE(function.entity_id());
		if (function.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node().category() == TypeCategory::Short) narrow = &function;
		else wide = &function;
	}
	REQUIRE(narrow != nullptr);
	REQUIRE(wide != nullptr);
	CHECK(narrow->entity_id() != wide->entity_id());
	for (const ASTNode& node : parser.get_nodes()) {
		if (node.is<FunctionDeclarationNode>() && node.as<FunctionDeclarationNode>().decl_node().identifier_token().value() == "target" &&
			node.as<FunctionDeclarationNode>().parameter_nodes()[0].as<DeclarationNode>().type_specifier_node().category() == TypeCategory::Short) {
			CHECK(node.as<FunctionDeclarationNode>().entity_id() == narrow->entity_id());
		}
	}
	const TypeId parameters[] = {table.builtin(CanonicalBuiltinKind::Short)};
	const TypeId function_type = table.function(table.builtin(CanonicalBuiltinKind::Int), parameters, false, CVQualifier::None,
		ReferenceQualifier::None, false, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	const TypeId pointer_type = table.pointer(function_type);
	const auto value = tryImportCanonicalFunctionPointerValue(table, pointer_type, *narrow);
	REQUIRE(value.has_value());
	CHECK(table.node(*value).array_extent == narrow->entity_id().value);
	CHECK_FALSE(tryImportCanonicalFunctionPointerValue(table, pointer_type, *wide).has_value());
	const NamespaceHandle other_namespace = gNamespaceRegistry.lookupNamespace(NamespaceRegistry::GLOBAL_NAMESPACE,
		StringTable::getOrInternStringHandle("other"));
	const auto other = gSymbolTable.lookup_qualified(other_namespace, "target");
	REQUIRE(other.has_value());
	const auto other_value = tryImportCanonicalFunctionPointerValue(table, pointer_type, other->as<FunctionDeclarationNode>());
	REQUIRE(other_value.has_value());
	CHECK(*other_value != *value);
	const auto safe = gSymbolTable.lookup("safe");
	REQUIRE(safe.has_value());
	CHECK(tryImportCanonicalFunctionPointerValue(table, pointer_type, safe->as<FunctionDeclarationNode>()).has_value());
	const TypeId safe_type = table.function(table.builtin(CanonicalBuiltinKind::Int), parameters, false, CVQualifier::None,
		ReferenceQualifier::None, true, CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	CHECK_FALSE(tryImportCanonicalFunctionPointerValue(table, table.pointer(safe_type), *narrow).has_value());
	FunctionDeclarationNode unpublished(const_cast<DeclarationNode&>(narrow->decl_node()));
	CHECK_FALSE(tryImportCanonicalFunctionPointerValue(table, pointer_type, unpublished).has_value());
	for (const ASTNode& parameter : narrow->parameter_nodes()) unpublished.add_parameter_node(parameter);
	DeclarationBuilder& builder = frontend.declarationBuilder();
	const ScopeId scope = builder.declaration(builder.entity(narrow->entity_id()).latest_decl_id).lexical_scope_id;
	const size_t entity_count = builder.entityCount();
	const PublishResult rejected = commitParserFreeFunctionPublication(builder, unpublished, scope, true, gSymbolTable);
	CHECK(rejected.status == PublishStatus::Rejected);
	CHECK_FALSE(unpublished.entity_id());
	CHECK(builder.entityCount() == entity_count);
	const PublishResult merged = commitParserFreeFunctionPublication(builder, unpublished, scope, false, gSymbolTable);
	REQUIRE(merged.status == PublishStatus::MergedRedeclaration);
	CHECK(unpublished.entity_id() == narrow->entity_id());
	CHECK(tryImportCanonicalFunctionPointerValue(table, pointer_type, unpublished) == value);
	MESSAGE("FunctionDeclarationNode size=" << sizeof(FunctionDeclarationNode));
}

TEST_CASE("Specialization field schemas preserve substituted types and union declarations") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	std::ifstream input("tests/test_canonical_structural_nttp_specialization_fields_ret0.cpp");
	REQUIRE(input.good());
	const std::string code((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	CompileContext context;
	context.setInputFile("specialization_fields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(context, gSymbolTable);
	Parser parser(lexer, context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const auto value_for = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		const TypeId argument = table.templateSpecializationArguments(imported.type);
		REQUIRE(table.node(argument).kind == CanonicalTypeKind::ValueTemplateArg);
		return table.templateArgumentValue(argument);
	};
	const TypeId union_bits = value_for("UnionBitNamed");
	CHECK(union_bits == value_for("UnionBitLiteral"));
	const TypeId union_bit_owner = table.node(union_bits).child;
	CHECK(table.recordMemberAt(union_bit_owner, 1).bit_offset == 0);
	const TypeId bits = value_for("BitNamed");
	CHECK(bits == value_for("BitLiteral"));
	const TypeId bit_owner = table.node(bits).child;
	REQUIRE(table.hasRecordFieldSchema(bit_owner));
	CHECK(table.recordMemberAt(bit_owner, 0).type == table.builtin(CanonicalBuiltinKind::Short));
	CHECK(table.recordMemberAt(bit_owner, 0).bit_width == 3);
	const TypeId named = value_for("Named");
	CHECK(named == value_for("Literal"));
	CHECK(named != value_for("Other"));
	const TypeId owner = table.node(named).child;
	const TypeId wide_owner = table.node(value_for("Wide")).child;
	CHECK(owner != wide_owner);
	REQUIRE(table.hasRecordFieldSchema(owner));
	REQUIRE(table.hasRecordFieldSchema(wide_owner));
	CHECK(table.recordMemberAt(owner, 0).type == table.builtin(CanonicalBuiltinKind::Short));
	CHECK(table.recordMemberAt(wide_owner, 0).type == table.builtin(CanonicalBuiltinKind::LongLong));
	CHECK(table.recordMemberAt(owner, 0).size_bytes == 2);
	CHECK(table.recordMemberAt(wide_owner, 0).size_bytes == 8);
	CHECK(table.recordMemberAt(owner, 0).offset_bytes == table.recordMemberAt(owner, 1).offset_bytes);
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(owner, 0).flags, CanonicalRecordMemberFlags::UnionGroupStart));
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(owner, 1).flags, CanonicalRecordMemberFlags::UnionGroupEnd));
	const TypeId union_value = value_for("UnionNamed");
	CHECK(union_value == value_for("UnionLiteral"));
	CHECK(table.node(union_value).kind == CanonicalTypeKind::StructuralUnionValue);
	CHECK(unpackStructuralUnionActiveVariant(table.node(union_value).array_extent) == 1);
}

TEST_CASE("Anonymous union groups publish canonical declaration boundaries") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext compile_context;
	compile_context.setInputFile("anonymous_union_groups.cpp");
	Lexer lexer(R"(
struct Variants {
	union { short small; int integer; };
	long long ordinary;
	union { unsigned narrow; long long wide; };
};
union Choice { short small; int integer; };
struct Key { union { int first; int second; }; };
struct Padding { long long guard; };
struct Derived : Padding, Variants {};
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle("Variants"));
	REQUIRE(info != nullptr);
	const StructTypeInfo* record = info->getStructInfo();
	REQUIRE(record != nullptr);
	REQUIRE(record->declaration_node != nullptr);
	REQUIRE(record->declaration_node->has_entity_id());
	CanonicalTypeTable& table = context.canonicalTypes();
	const EntityId entity = record->declaration_node->entity_id();
	REQUIRE(table.hasRecordFieldSchema(table.record(entity)));
	CHECK(table.recordMemberAt(table.record(entity), 0).offset_bytes == table.recordMemberAt(table.record(entity), 1).offset_bytes);
	CHECK(table.recordMemberAt(table.record(entity), 3).offset_bytes == table.recordMemberAt(table.record(entity), 4).offset_bytes);
	for (const size_t index : {size_t{0}, size_t{3}}) {
		CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(entity), index).flags, CanonicalRecordMemberFlags::UnionGroupStart));
		CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(entity), index + 1).flags, CanonicalRecordMemberFlags::UnionGroupEnd));
		CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(entity), index).flags, CanonicalRecordMemberFlags::UnionVariant));
	}
	CHECK(table.recordMemberAt(table.record(entity), 2).flags == CanonicalRecordMemberFlags::None);
	const TypeId owner = table.record(entity);
	CHECK(table.structuralUnionValue(owner, 0, uint16_t{1}, table.integralValue(table.builtin(CanonicalBuiltinKind::Int), 42)) !=
		table.structuralUnionValue(owner, 0, std::nullopt, TypeId{}));
	const TypeInfo* choice_info = findTypeByName(StringTable::getOrInternStringHandle("Choice"));
	REQUIRE(choice_info != nullptr);
	const EntityId choice_entity = choice_info->getStructInfo()->declaration_node->entity_id();
	REQUIRE(table.hasRecordFieldSchema(table.record(choice_entity)));
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(choice_entity), 0).flags, CanonicalRecordMemberFlags::UnionGroupStart));
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(choice_entity), 1).flags, CanonicalRecordMemberFlags::UnionGroupEnd));
	CHECK(table.structuralUnionValue(table.record(choice_entity), 0, uint16_t{1},
		table.integralValue(table.builtin(CanonicalBuiltinKind::Int), 42)) != table.structuralUnionValue(owner, 0, std::nullopt, TypeId{}));
	const TypeInfo* key_info = findTypeByName(StringTable::getOrInternStringHandle("Key"));
	REQUIRE(key_info != nullptr);
	const StructTypeInfo* key_record = key_info->getStructInfo();
	REQUIRE(key_record != nullptr);
	REQUIRE(key_record->declaration_node != nullptr);
	REQUIRE(key_record->declaration_node->has_entity_id());
	const EntityId key_entity = key_record->declaration_node->entity_id();
	REQUIRE(table.hasRecordFieldSchema(table.record(key_entity)));
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(key_entity), 0).flags, CanonicalRecordMemberFlags::UnionGroupStart));
	CHECK(hasCanonicalRecordMemberFlag(table.recordMemberAt(table.record(key_entity), 1).flags, CanonicalRecordMemberFlags::UnionGroupEnd));
	// A payload without an explicit union selection is unresolved.
	FlashCpp::StructuralClassValue unresolved;
	unresolved.type_index = key_info->registeredTypeIndex();
	for (const StructMember& field : key_record->members) {
		FlashCpp::StructuralClassValueMember member;
		member.name = field.getName();
		member.scalar_value = FlashCpp::NonTypeValueIdentity::makeConcrete(0, field.type_index);
		unresolved.members.push_back(std::move(member));
	}
	CHECK_FALSE(tryImportCanonicalStructuralValue(table, &unresolved).has_value());
	const auto make_selected_value = [&](std::string_view member_name) {
		const StructMember* field = key_record->findMember(member_name);
		REQUIRE(field != nullptr);
		REQUIRE(field->canonical_type_id);
		const size_t index = static_cast<size_t>(field - key_record->members.data());
		FlashCpp::StructuralClassValue selected;
		selected.type_index = key_info->registeredTypeIndex();
		FlashCpp::StructuralClassValueMember member;
		member.name = field->getName();
		member.scalar_value = FlashCpp::NonTypeValueIdentity::makeConcrete(0, field->type_index);
		selected.members.push_back(std::move(member));
		selected.union_selections.push_back({0, static_cast<uint16_t>(index)});
		return selected;
	};
	const auto imported_union_value = [&](const FlashCpp::StructuralClassValue& selected) {
		const std::optional<TypeId> imported = tryImportCanonicalStructuralValue(table, &selected);
		REQUIRE(imported.has_value());
		const CanonicalTypeNode record_value = table.node(*imported);
		REQUIRE(record_value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(record_value.array_extent);
		REQUIRE(link);
		const TypeId union_value = table.templateArgumentValue(link);
		REQUIRE(table.node(union_value).kind == CanonicalTypeKind::StructuralUnionValue);
		return table.node(union_value);
	};
	const FlashCpp::StructuralClassValue first_selected = make_selected_value("first");
	const FlashCpp::StructuralClassValue second_selected = make_selected_value("second");
	const CanonicalTypeNode first_union = imported_union_value(first_selected);
	const CanonicalTypeNode second_union = imported_union_value(second_selected);
	CHECK(unpackStructuralUnionFirstVariant(first_union.array_extent) == 0);
	CHECK(unpackStructuralUnionActiveVariant(first_union.array_extent) == 0);
	CHECK(unpackStructuralUnionActiveVariant(second_union.array_extent) == 1);
	CHECK(table.node(table.templateArgumentValue(unpackStructuralValueSubobjects(first_union.array_extent))).kind ==
		CanonicalTypeKind::IntegralValue);
	FlashCpp::StructuralClassValue inactive;
	inactive.type_index = key_info->registeredTypeIndex();
	inactive.union_selections.push_back({0, FlashCpp::kNoStructuralUnionActiveVariant});
	const CanonicalTypeNode inactive_union = imported_union_value(inactive);
	CHECK_FALSE(unpackStructuralUnionActiveVariant(inactive_union.array_extent).has_value());
}

TEST_CASE("Enum structural NTTP fields preserve canonical types values and published offsets") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
enum Tiny : signed char { negative = -7, different = -6 };
enum class Wide : unsigned long long { high = 0xfedcba9876543210ULL, other = 0xfedcba9876543211ULL };
enum class Code : unsigned short { chosen = 60000 };
enum Plain { plain_negative = -9 };
struct EnumBase { Tiny value; };
struct Nested { Wide wide; Code code; Plain plain; };
struct Key : EnumBase { Nested nested; bool enabled; };
constexpr Key key{{negative}, {Wide::high, Code::chosen, plain_negative}, true};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{{negative}, {Wide::high, Code::chosen, plain_negative}, true}>;
using ChangedBase = Base<Key{{different}, {Wide::high, Code::chosen, plain_negative}, true}>;
using ChangedNested = Base<Key{{negative}, {Wide::other, Code::chosen, plain_negative}, true}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nttp_enum_fields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		if (const TypeSpecifierNode* syntax = info->aliasTypeSpecifier()) {
			const CanonicalTypeImport syntax_import = importCanonicalType(table, *syntax);
			REQUIRE(syntax_import.status == CanonicalTypeImportStatus::Supported);
			CHECK(syntax_import.type == imported.type);
		}
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("ChangedBase"));
	CHECK(named != import_type("ChangedNested"));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
	CHECK(table.classBaseAt(derived, 1).offset_bytes == 8);
	const auto first_subobject = [&](TypeId value) {
		return table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(value).array_extent));
	};
	const TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const TypeId base = first_subobject(value);
	const CanonicalTypeNode tiny = table.node(first_subobject(base));
	CHECK(table.node(tiny.child).kind == CanonicalTypeKind::Enum);
	CHECK(tiny.array_extent == static_cast<uint64_t>(-7));
	TypeId link = unpackStructuralValueSubobjects(table.node(value).array_extent);
	link = table.templateArgumentNext(link);
	const TypeId nested = table.templateArgumentValue(link);
	link = unpackStructuralValueSubobjects(table.node(nested).array_extent);
	const uint64_t expected[] = {0xfedcba9876543210ULL, 60000, static_cast<uint64_t>(-9)};
	for (const uint64_t bits : expected) {
		const CanonicalTypeNode field = table.node(table.templateArgumentValue(link));
		CHECK(table.node(field.child).kind == CanonicalTypeKind::Enum);
		CHECK(field.array_extent == bits);
		link = table.templateArgumentNext(link);
	}
	CHECK_FALSE(link);
}

TEST_CASE("Structural NTTP subobjects share syntax declaration and base graph identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Leaf { short value; };
struct Other { unsigned value; };
struct Key : Leaf, Other { Leaf member; bool enabled; };
constexpr Key key{{7}, {4000000000u}, {9}, true};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{{7}, {4000000000u}, {9}, true}>;
using ChangedFirst = Base<Key{{8}, {4000000000u}, {9}, true}>;
using ChangedSecond = Base<Key{{7}, {4000000001u}, {9}, true}>;
using ChangedMember = Base<Key{{7}, {4000000000u}, {8}, true}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nttp_subobjects.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		if (const TypeSpecifierNode* syntax = info->aliasTypeSpecifier()) {
			const CanonicalTypeImport syntax_import = importCanonicalType(table, *syntax);
			REQUIRE(syntax_import.status == CanonicalTypeImportStatus::Supported);
			CHECK(syntax_import.type == imported.type);
		}
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("ChangedFirst"));
	CHECK(named != import_type("ChangedSecond"));
	CHECK(named != import_type("ChangedMember"));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
	CHECK(table.classBaseAt(derived, 1).offset_bytes == 8);
}

TEST_CASE("Structural NTTP imports share typed values and published base offsets") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Key { short code; unsigned count; bool enabled; };
constexpr Key key{7, 4000000000u, true};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{7, 4000000000u, true}>;
using Different = Base<Key{8, 4000000000u, true}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nttp_import.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		if (const TypeSpecifierNode* syntax = info->aliasTypeSpecifier()) {
			const CanonicalTypeImport syntax_import = importCanonicalType(table, *syntax);
			REQUIRE(syntax_import.status == CanonicalTypeImportStatus::Supported);
			CHECK(syntax_import.type == imported.type);
		}
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Different"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId member_link{static_cast<uint32_t>(value.array_extent)};
	const CanonicalBuiltinKind types[] = {CanonicalBuiltinKind::Short, CanonicalBuiltinKind::UnsignedInt, CanonicalBuiltinKind::Bool};
	const uint64_t bits[] = {7, 4000000000u, 1};
	for (size_t index = 0; index < 3; ++index) {
		const CanonicalTypeNode member = table.node(table.templateArgumentValue(member_link));
		CHECK(member.child == table.builtin(types[index]));
		CHECK(member.array_extent == bits[index]);
		member_link = table.templateArgumentNext(member_link);
	}
	CHECK_FALSE(member_link);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
	CHECK(table.classBaseAt(derived, 1).offset_bytes == 8);
}

TEST_CASE("Bit-field structural NTTPs canonicalize widths padding and enum identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
enum class Code : signed char { negative = -3, other = 2 };
struct Inner { unsigned low : 3; int signed_value : 3; bool flag : 1; Code code : 3; };
struct Key : Inner { unsigned upper : 5; unsigned : 2; unsigned : 0; unsigned tail : 4; };
constexpr Key key{{9u, 7, true, Code::negative}, 33u, 17u};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{{9u, 7, true, Code::negative}, 33u, 17u}>;
using Converted = Base<Key{{1u, -1, true, Code::negative}, 1u, 1u}>;
using OtherLow = Base<Key{{2u, 7, true, Code::negative}, 33u, 17u}>;
using OtherSigned = Base<Key{{9u, -2, true, Code::negative}, 33u, 17u}>;
using OtherBool = Base<Key{{9u, 7, false, Code::negative}, 33u, 17u}>;
using OtherEnum = Base<Key{{9u, 7, true, Code::other}, 33u, 17u}>;
using OtherTail = Base<Key{{9u, 7, true, Code::negative}, 33u, 18u}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nttp_bitfields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		if (const TypeSpecifierNode* syntax = info->aliasTypeSpecifier()) {
			const CanonicalTypeImport syntax_import = importCanonicalType(table, *syntax);
			REQUIRE(syntax_import.status == CanonicalTypeImportStatus::Supported);
			CHECK(syntax_import.type == imported.type);
		}
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named == import_type("Converted"));
	CHECK(named != import_type("OtherLow"));
	CHECK(named != import_type("OtherSigned"));
	CHECK(named != import_type("OtherBool"));
	CHECK(named != import_type("OtherEnum"));
	CHECK(named != import_type("OtherTail"));
	const TypeInfo* key_info = findTypeByName(StringTable::getOrInternStringHandle("Key"));
	REQUIRE(key_info != nullptr);
	const StructTypeInfo* key_record = key_info->getStructInfo();
	REQUIRE(key_record != nullptr);
	REQUIRE(key_record->declaration_node != nullptr);
	REQUIRE(key_record->declaration_node != nullptr);
	const EntityId key_entity = key_record->declaration_node->entity_id();
	REQUIRE(table.hasRecordFieldSchema(table.record(key_entity)));
	const CanonicalRecordMember upper = table.recordMemberAt(table.record(key_entity), 0);
	CHECK(hasCanonicalRecordMemberFlag(upper.flags, CanonicalRecordMemberFlags::Bitfield));
	CHECK(upper.bit_width == 5);
	CHECK(upper.type == table.builtin(CanonicalBuiltinKind::UnsignedInt));
	CHECK(table.recordMemberAt(table.record(key_entity), 1).bit_width == 2);
	CHECK(table.recordMemberAt(table.record(key_entity), 2).bit_width == 0);
	CHECK(table.recordMemberAt(table.record(key_entity), 3).bit_width == 4);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
	CHECK(table.classBaseAt(derived, 1).offset_bytes == 8);
	const TypeInfo* derived_alias = findTypeByName(StringTable::getOrInternStringHandle("Derived"));
	REQUIRE(derived_alias != nullptr);
	const TypeInfo* derived_info = resolveAliasTypeInfo(derived_alias->type_index_).terminal_type_info;
	REQUIRE(derived_info != nullptr);
	const StructTypeInfo* derived_record = derived_info->getStructInfo();
	REQUIRE(derived_record != nullptr);
	REQUIRE(derived_record->declaration_node != nullptr);
	const CanonicalTypeImport declaration = importCanonicalClassDeclaration(table, *derived_record->declaration_node);
	REQUIRE(declaration.status == CanonicalTypeImportStatus::Supported);
	CHECK(declaration.type == derived);
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	CHECK(unpackStructuralValueBaseCount(value.array_extent) == 1);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const TypeId inner_id = table.templateArgumentValue(link);
	const CanonicalTypeNode inner = table.node(inner_id);
	REQUIRE(inner.kind == CanonicalTypeKind::StructuralValue);
	TypeId inner_link = unpackStructuralValueSubobjects(inner.array_extent);
	const uint64_t inner_values[] = {1, static_cast<uint64_t>(-1), 1};
	const CanonicalBuiltinKind inner_types[] = {CanonicalBuiltinKind::UnsignedInt, CanonicalBuiltinKind::Int,
		CanonicalBuiltinKind::Bool};
	for (size_t index = 0; index < 3; ++index) {
		const CanonicalTypeNode field = table.node(table.templateArgumentValue(inner_link));
		CHECK(field.child == table.builtin(inner_types[index]));
		CHECK(field.array_extent == inner_values[index]);
		inner_link = table.templateArgumentNext(inner_link);
	}
	const CanonicalTypeNode code_field = table.node(table.templateArgumentValue(inner_link));
	CHECK(table.node(code_field.child).kind == CanonicalTypeKind::Enum);
	CHECK(code_field.array_extent == static_cast<uint64_t>(-3));
	CHECK_FALSE(table.templateArgumentNext(inner_link));
	link = table.templateArgumentNext(link);
	for (size_t index = 0; index < 2; ++index) {
		const CanonicalTypeNode field = table.node(table.templateArgumentValue(link));
		CHECK(field.child == table.builtin(CanonicalBuiltinKind::UnsignedInt));
		CHECK(field.array_extent == 1);
		link = table.templateArgumentNext(link);
	}
	CHECK_FALSE(link);
}

TEST_CASE("Array structural NTTP members share canonical value identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
enum class Code : signed char { negative = -3, other = 2 };
struct Element { short value; };
struct Key { unsigned values[3]; Element elements[2]; int matrix[2][2]; bool flags[2]; Code codes[2]; };
constexpr Key key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>;
using OtherScalar = Base<Key{{1u, 2u, 4u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>;
using OtherRecord = Base<Key{{1u, 2u, 3u}, {{4}, {6}}, {{6, 7}, {8, 9}}, {true, false}, {Code::negative, Code::other}}>;
using OtherNested = Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 10}}, {true, false}, {Code::negative, Code::other}}>;
using OtherBool = Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {false, true}, {Code::negative, Code::other}}>;
using OtherEnum = Base<Key{{1u, 2u, 3u}, {{4}, {5}}, {{6, 7}, {8, 9}}, {true, false}, {Code::other, Code::negative}}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nttp_arrays.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		if (const TypeSpecifierNode* syntax = info->aliasTypeSpecifier()) {
			const CanonicalTypeImport syntax_import = importCanonicalType(table, *syntax);
			REQUIRE(syntax_import.status == CanonicalTypeImportStatus::Supported);
			CHECK(syntax_import.type == imported.type);
		}
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("OtherScalar"));
	CHECK(named != import_type("OtherRecord"));
	CHECK(named != import_type("OtherNested"));
	CHECK(named != import_type("OtherBool"));
	CHECK(named != import_type("OtherEnum"));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
	CHECK(table.classBaseAt(derived, 1).offset_bytes == 8);
	const TypeInfo* derived_alias = findTypeByName(StringTable::getOrInternStringHandle("Derived"));
	REQUIRE(derived_alias != nullptr);
	const TypeInfo* derived_info = resolveAliasTypeInfo(derived_alias->type_index_).terminal_type_info;
	REQUIRE(derived_info != nullptr);
	const StructTypeInfo* derived_record = derived_info->getStructInfo();
	REQUIRE(derived_record != nullptr);
	REQUIRE(derived_record->declaration_node != nullptr);
	const CanonicalTypeImport declaration = importCanonicalClassDeclaration(table, *derived_record->declaration_node);
	REQUIRE(declaration.status == CanonicalTypeImportStatus::Supported);
	CHECK(declaration.type == derived);
	const TypeInfo* key_info = findTypeByName(StringTable::getOrInternStringHandle("Key"));
	REQUIRE(key_info != nullptr);
	const StructTypeInfo* key_record = key_info->getStructInfo();
	REQUIRE(key_record != nullptr);
	REQUIRE(key_record->declaration_node != nullptr);
	REQUIRE(key_record->declaration_node != nullptr);
	const EntityId key_entity = key_record->declaration_node->entity_id();
	REQUIRE(table.hasRecordFieldSchema(table.record(key_entity)));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	CHECK(unpackStructuralValueBaseCount(value.array_extent) == 0);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode values_node = table.node(table.templateArgumentValue(link));
	REQUIRE(values_node.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(values_node.child == table.recordMemberAt(table.record(key_entity), 0).type);
	REQUIRE(table.node(values_node.child).kind == CanonicalTypeKind::Array);
	CHECK(table.node(values_node.child).array_extent == 3);
	CHECK(unpackStructuralArrayElementCount(values_node.array_extent) == 3);
	TypeId element_link = unpackStructuralValueSubobjects(values_node.array_extent);
	const uint64_t expected_values[] = {1, 2, 3};
	for (const uint64_t expected : expected_values) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		CHECK(element.child == table.builtin(CanonicalBuiltinKind::UnsignedInt));
		CHECK(element.array_extent == expected);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode records_node = table.node(table.templateArgumentValue(link));
	REQUIRE(records_node.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(records_node.child == table.recordMemberAt(table.record(key_entity), 1).type);
	CHECK(table.node(records_node.child).array_extent == 2);
	element_link = unpackStructuralValueSubobjects(records_node.array_extent);
	const uint64_t expected_records[] = {4, 5};
	for (const uint64_t expected : expected_records) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		REQUIRE(element.kind == CanonicalTypeKind::StructuralValue);
		CHECK(table.node(element.child).kind == CanonicalTypeKind::Record);
		const TypeId field_link = unpackStructuralValueSubobjects(element.array_extent);
		const CanonicalTypeNode field = table.node(table.templateArgumentValue(field_link));
		CHECK(field.array_extent == expected);
		CHECK_FALSE(table.templateArgumentNext(field_link));
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode matrix_node = table.node(table.templateArgumentValue(link));
	REQUIRE(matrix_node.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(matrix_node.child == table.recordMemberAt(table.record(key_entity), 2).type);
	CHECK(table.node(matrix_node.child).array_extent == 2);
	element_link = unpackStructuralValueSubobjects(matrix_node.array_extent);
	const uint64_t expected_matrix[][2] = {{6, 7}, {8, 9}};
	for (const auto& row : expected_matrix) {
		const CanonicalTypeNode row_node = table.node(table.templateArgumentValue(element_link));
		REQUIRE(row_node.kind == CanonicalTypeKind::StructuralArrayValue);
		CHECK(table.node(row_node.child).array_extent == 2);
		TypeId cell_link = unpackStructuralValueSubobjects(row_node.array_extent);
		for (const uint64_t expected : row) {
			const CanonicalTypeNode cell = table.node(table.templateArgumentValue(cell_link));
			CHECK(cell.child == table.builtin(CanonicalBuiltinKind::Int));
			CHECK(cell.array_extent == expected);
			cell_link = table.templateArgumentNext(cell_link);
		}
		CHECK_FALSE(cell_link);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode flags_node = table.node(table.templateArgumentValue(link));
	REQUIRE(flags_node.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(flags_node.child == table.recordMemberAt(table.record(key_entity), 3).type);
	element_link = unpackStructuralValueSubobjects(flags_node.array_extent);
	const uint64_t expected_flags[] = {1, 0};
	for (const uint64_t expected : expected_flags) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		CHECK(element.child == table.builtin(CanonicalBuiltinKind::Bool));
		CHECK(element.array_extent == expected);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode codes_node = table.node(table.templateArgumentValue(link));
	REQUIRE(codes_node.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(codes_node.child == table.recordMemberAt(table.record(key_entity), 4).type);
	element_link = unpackStructuralValueSubobjects(codes_node.array_extent);
	const uint64_t expected_codes[] = {static_cast<uint64_t>(-3), 2};
	for (const uint64_t expected : expected_codes) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		CHECK(table.node(element.child).kind == CanonicalTypeKind::Enum);
		CHECK(element.array_extent == expected);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	CHECK_FALSE(table.templateArgumentNext(link));
}

namespace {
void publishTestClassBases(CanonicalTypeTable& table, EntityId entity, std::span<const CanonicalRecordBase> record_bases) {
	std::vector<CanonicalClassBase> bases;
	for (const CanonicalRecordBase& base : record_bases) {
		bases.push_back({table.record(base.entity), base.access, base.flags, 0, base.offset_bytes});
	}
	table.publishClassBaseSchema(table.record(entity), bases);
}
}

TEST_CASE("Floating structural NTTP fields import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Key { float small; double wide; float row[2]; };
constexpr Key key{1.5f, 2.25, {3.5f, 4.5f}};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key{1.5f, 2.25, {3.5f, 4.5f}}>;
using ChangedSmall = Base<Key{2.5f, 2.25, {3.5f, 4.5f}}>;
using ChangedWide = Base<Key{1.5f, 2.5, {3.5f, 4.5f}}>;
using ChangedRow = Base<Key{1.5f, 2.25, {3.5f, 5.5f}}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_floating_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("ChangedSmall"));
	CHECK(named != import_type("ChangedWide"));
	CHECK(named != import_type("ChangedRow"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode small = table.node(table.templateArgumentValue(link));
	CHECK(small.kind == CanonicalTypeKind::FloatingValue);
	CHECK(small.child == table.builtin(CanonicalBuiltinKind::Float));
	CHECK(small.array_extent == 0x3fc00000);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode wide = table.node(table.templateArgumentValue(link));
	CHECK(wide.kind == CanonicalTypeKind::FloatingValue);
	CHECK(wide.child == table.builtin(CanonicalBuiltinKind::Double));
	CHECK(wide.array_extent == 0x4002000000000000ULL);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode row = table.node(table.templateArgumentValue(link));
	REQUIRE(row.kind == CanonicalTypeKind::StructuralArrayValue);
	TypeId element_link = unpackStructuralValueSubobjects(row.array_extent);
	const uint64_t expected_row[] = {0x40600000, 0x40900000};
	for (const uint64_t expected : expected_row) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		CHECK(element.kind == CanonicalTypeKind::FloatingValue);
		CHECK(element.child == table.builtin(CanonicalBuiltinKind::Float));
		CHECK(element.array_extent == expected);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Null pointer structural NTTP fields import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Key { int* pointer; int tag; };
constexpr Key first{nullptr, 1};
constexpr Key second{nullptr, 2};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{nullptr, 1}>;
using Changed = Base<Key{nullptr, 2}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_null_pointer_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
	CHECK(pointer.kind == CanonicalTypeKind::NullPointerValue);
	CHECK(table.node(pointer.child).kind == CanonicalTypeKind::Pointer);
	CHECK(table.node(table.node(pointer.child).child).builtin == CanonicalBuiltinKind::Int);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode tag = table.node(table.templateArgumentValue(link));
	CHECK(tag.kind == CanonicalTypeKind::IntegralValue);
	CHECK(tag.array_extent == 1);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Null pointer array elements import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Key { int* pointers[2]; int tag; };
constexpr Key first{{nullptr, nullptr}, 1};
constexpr Key second{{nullptr, nullptr}, 2};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{{nullptr, nullptr}, 1}>;
using Changed = Base<Key{{nullptr, nullptr}, 2}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_null_pointer_array_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode pointers = table.node(table.templateArgumentValue(link));
	REQUIRE(pointers.kind == CanonicalTypeKind::StructuralArrayValue);
	const CanonicalTypeNode pointer_array = table.node(pointers.child);
	REQUIRE(pointer_array.kind == CanonicalTypeKind::Array);
	REQUIRE(table.node(pointer_array.child).kind == CanonicalTypeKind::Pointer);
	TypeId element_link = unpackStructuralValueSubobjects(pointers.array_extent);
	for (size_t index = 0; index < 2; ++index) {
		const CanonicalTypeNode element = table.node(table.templateArgumentValue(element_link));
		CHECK(element.kind == CanonicalTypeKind::NullPointerValue);
		CHECK(element.child == pointer_array.child);
		element_link = table.templateArgumentNext(element_link);
	}
	CHECK_FALSE(element_link);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode tag = table.node(table.templateArgumentValue(link));
	CHECK(tag.kind == CanonicalTypeKind::IntegralValue);
	CHECK(tag.array_extent == 1);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Class template specialization structural NTTP values import canonically") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Key { T value; };
constexpr Key<int> key{42};
template<Key<int> Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<key> {};
using Named = Base<key>;
using Literal = Base<Key<int>{42}>;
using Changed = Base<Key<int>{43}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_class_template_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	CHECK(table.node(value.child).kind == CanonicalTypeKind::TemplateSpecialization);
	const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode member = table.node(table.templateArgumentValue(link));
	CHECK(member.kind == CanonicalTypeKind::IntegralValue);
	CHECK(member.child == table.builtin(CanonicalBuiltinKind::Int));
	CHECK(member.array_extent == 42);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Specialization base subobjects retain canonical identity when nested") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext compile_context;
	compile_context.setInputFile("structural_specialization_bases.cpp");
	Lexer lexer(R"(
template<class T> struct Payload { T value; };
struct Padding { long long guard; };
template<class T> struct Key : Padding, Payload<T> { unsigned short tag; };
struct Wrapper { Key<short> nested; };
template<class T> struct ArrayWrapper { Key<T> elements[2]; };
constexpr Key<short> key{{12345678901LL}, {-7}, 42};
constexpr Key<long long> wide{{12345678901LL}, {-7}, 42};
constexpr Wrapper wrapper{key};
constexpr ArrayWrapper<short> array{{key, {{9}, {8}, 7}}};
template<Key<short> Value> struct Tag {};
template<Wrapper Value> struct NestedTag {};
template<ArrayWrapper<short> Value> struct ArrayTag {};
template<Key<long long> Value> struct WideTag {};
using Named = Tag<key>;
using Literal = Tag<Key<short>{{12345678901LL}, {-7}, 42}>;
using Changed = Tag<Key<short>{{12345678901LL}, {-8}, 42}>;
using Nested = NestedTag<wrapper>;
using Array = ArrayTag<array>;
using Wide = WideTag<wide>;
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto argument_value = [&](std::string_view name) {
		const TypeId owner = import_type(name);
		const TypeId argument = table.templateSpecializationArguments(owner);
		REQUIRE(table.node(argument).kind == CanonicalTypeKind::ValueTemplateArg);
		return table.templateArgumentValue(argument);
	};
	const TypeId named = argument_value("Named");
	CHECK(named == argument_value("Literal"));
	CHECK(named != argument_value("Changed"));
	const CanonicalTypeNode value = table.node(named);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	REQUIRE(table.node(value.child).kind == CanonicalTypeKind::TemplateSpecialization);
	REQUIRE(table.hasClassBaseSchema(value.child));
	REQUIRE(table.classBaseCount(value.child) == 2);
	CHECK(table.classBaseAt(value.child, 1).offset_bytes > 0);
	CHECK(unpackStructuralValueBaseCount(value.array_extent) == 2);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode base = table.node(table.templateArgumentValue(link));
	CHECK(base.child == table.classBaseAt(value.child, 1).type);
	REQUIRE(table.node(base.child).kind == CanonicalTypeKind::TemplateSpecialization);
	const TypeId base_link = unpackStructuralValueSubobjects(base.array_extent);
	const CanonicalTypeNode scalar = table.node(table.templateArgumentValue(base_link));
	CHECK(scalar.child == table.builtin(CanonicalBuiltinKind::Short));
	CHECK(static_cast<int64_t>(scalar.array_extent) == -7);
	const CanonicalTypeNode nested = table.node(argument_value("Nested"));
	const TypeId nested_link = unpackStructuralValueSubobjects(nested.array_extent);
	CHECK(table.templateArgumentValue(nested_link) == named);
	const CanonicalTypeNode array_wrapper = table.node(argument_value("Array"));
	const TypeId array_link = unpackStructuralValueSubobjects(array_wrapper.array_extent);
	const CanonicalTypeNode array = table.node(table.templateArgumentValue(array_link));
	REQUIRE(array.kind == CanonicalTypeKind::StructuralArrayValue);
	const TypeId element_link = unpackStructuralValueSubobjects(array.array_extent);
	CHECK(table.templateArgumentValue(element_link) == named);
	const CanonicalTypeNode wide = table.node(argument_value("Wide"));
	CHECK(wide.child != value.child);
	CHECK(table.classBaseAt(wide.child, 1).type != base.child);
	const auto bridge_value = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const StructTypeInfo* record = canonicalClassStructInfoFromTypeInfo(*info);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		const auto& arguments = record->declaration_node->outer_template_args();
		REQUIRE(arguments.size() == 1);
		REQUIRE(arguments[0].structural_value != nullptr);
		return arguments[0].structural_value;
	};
	FlashCpp::StructuralClassValue mismatched = *bridge_value("Nested");
	REQUIRE(mismatched.members.size() == 1);
	mismatched.members[0].nested_value = bridge_value("Wide");
	CHECK_FALSE(tryImportCanonicalStructuralValue(table, &mismatched).has_value());
	FlashCpp::StructuralClassValue mismatched_array = *bridge_value("Array")->members[0].nested_value;
	mismatched_array.members[0].nested_value = bridge_value("Wide");
	FlashCpp::StructuralClassValue mismatched_owner = *bridge_value("Array");
	mismatched_owner.members[0].nested_value = &mismatched_array;
	CHECK_FALSE(tryImportCanonicalStructuralValue(table, &mismatched_owner).has_value());
	CanonicalTypeTable missing_schemas;
	CHECK_FALSE(tryImportCanonicalStructuralValue(missing_schemas, bridge_value("Named")).has_value());
}

TEST_CASE("Member object pointer structural NTTP fields import by member ordinal") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Object { int first; int second; };
struct Key { int Object::* pointer; int tag; };
constexpr Key first{&Object::first, 1};
constexpr Key second{&Object::second, 1};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{&Object::first, 1}>;
using Changed = Base<Key{&Object::second, 1}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_member_object_pointer_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
	CHECK(pointer.kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(table.node(pointer.child).kind == CanonicalTypeKind::MemberObjectPointer);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(pointer.array_extent) == import_type("Object"));
	CHECK(unpackMemberObjectPointerValueOrdinal(pointer.array_extent) == 0);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode tag = table.node(table.templateArgumentValue(link));
	CHECK(tag.kind == CanonicalTypeKind::IntegralValue);
	CHECK(tag.array_extent == 1);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Member function pointer structural NTTP fields import by function ordinal") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_structural_nttp_member_function_schema_ret0.cpp");
	REQUIRE(fixture.is_open());
	const std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("structural_member_function_pointer_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId unqueried = import_type("Unqueried");
	const StructTypeInfo* unqueried_record = tryResolveCanonicalMemberPointerOwnerRecord(table, unqueried);
	REQUIRE(unqueried_record != nullptr);
	REQUIRE(unqueried_record->layout_is_complete);
	REQUIRE_FALSE(table.hasMemberFunctionSignatures(unqueried));
	StructTypeInfo& pending_record = const_cast<StructTypeInfo&>(*unqueried_record);
	pending_record.layout_is_complete = false;
	CHECK_FALSE(tryPublishCanonicalMemberFunctionSignatures(table, unqueried, pending_record));
	CHECK_FALSE(table.hasMemberFunctionSignatures(unqueried));
	pending_record.layout_is_complete = true;
	CHECK(tryPublishCanonicalMemberFunctionSignatures(table, unqueried, pending_record));
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Braced"));
	CHECK(named != import_type("OtherOverload"));
	CHECK(named != import_type("OtherOwner"));
	CHECK(import_type("GenericNamed") == import_type("GenericBraced"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
	CHECK(pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	CHECK(table.node(pointer.child).kind == CanonicalTypeKind::MemberFunctionPointer);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(pointer.array_extent) == import_type("Object"));
	CHECK(unpackMemberFunctionPointerValueOrdinal(pointer.array_extent) == 0);
	const TypeId owner = unpackMemberFunctionPointerValueDeclaringClass(pointer.array_extent);
	CHECK(table.hasMemberFunctionSignatures(owner));
	const uint16_t ordinal = unpackMemberFunctionPointerValueOrdinal(pointer.array_extent);
	CHECK(table.memberFunctionSignatureCount(owner) > ordinal);
	CHECK(table.memberFunctionSignatureAt(owner, ordinal) == table.memberPointerPointee(pointer.child));
	CHECK(table.node(table.memberFunctionSignatureAt(owner, ordinal)).kind == CanonicalTypeKind::Function);
	const StructTypeInfo* declaration_record = tryResolveCanonicalMemberPointerOwnerRecord(table, owner);
	REQUIRE(declaration_record != nullptr);
	bool saw_static = false;
	for (size_t index = 0; index < declaration_record->member_functions.size(); ++index) {
		const StructMemberFunction& member = declaration_record->member_functions[index];
		const bool is_static = member.function_decl.is<FunctionDeclarationNode>() && member.function_decl.as<FunctionDeclarationNode>().is_static();
		if (is_static || member.is_constructor || member.is_destructor) CHECK_FALSE(table.memberFunctionSignatureAt(owner, index));
		saw_static |= is_static;
	}
	CHECK(saw_static);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode tag = table.node(table.templateArgumentValue(link));
	CHECK(tag.kind == CanonicalTypeKind::IntegralValue);
	CHECK(tag.array_extent == 7);
	CHECK_FALSE(table.templateArgumentNext(link));
	const TypeId generic = import_type("GenericNamed");
	const TypeId generic_value = table.templateArgumentValue(table.templateSpecializationArguments(generic));
	const TypeId generic_link = unpackStructuralValueSubobjects(table.node(generic_value).array_extent);
	const CanonicalTypeNode generic_pointer = table.node(table.templateArgumentValue(generic_link));
	REQUIRE(generic_pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	const TypeId generic_owner = unpackMemberFunctionPointerValueDeclaringClass(generic_pointer.array_extent);
	REQUIRE(table.node(generic_owner).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.hasMemberFunctionSignatures(generic_owner));
	const uint16_t generic_ordinal = unpackMemberFunctionPointerValueOrdinal(generic_pointer.array_extent);
	CHECK(table.memberFunctionSignatureAt(generic_owner, generic_ordinal) == table.memberPointerPointee(generic_pointer.child));
	const TypeId qualified = import_type("Qualified");
	const TypeId qualified_value = table.templateArgumentValue(table.templateSpecializationArguments(qualified));
	const TypeId qualified_link = unpackStructuralValueSubobjects(table.node(qualified_value).array_extent);
	const CanonicalTypeNode qualified_pointer = table.node(table.templateArgumentValue(qualified_link));
	REQUIRE(qualified_pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	const TypeId qualified_owner = unpackMemberFunctionPointerValueDeclaringClass(qualified_pointer.array_extent);
	const uint16_t qualified_ordinal = unpackMemberFunctionPointerValueOrdinal(qualified_pointer.array_extent);
	CHECK(table.memberFunctionSignatureAt(qualified_owner, qualified_ordinal) == table.memberPointerPointee(qualified_pointer.child));
}

enum class MemberPointerFixtureKind { Primary, PointerPartial, NestedPartial };

static void checkTemplatedMemberPointerFields(const char* fixture_path, MemberPointerFixtureKind kind) {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture(fixture_path);
	REQUIRE(fixture.is_open());
	const std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("templated_member_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		REQUIRE_FALSE(table.hasDependentTypeComponents(imported.type));
		return imported.type;
	};
	CHECK(import_type("ShortNamed") == import_type("ShortBraced"));
	CHECK(import_type("LongNamed") == import_type("LongBraced"));
	CHECK(import_type("ShortObjectNamed") == import_type("ShortObjectBraced"));
	CHECK(import_type("LongObjectNamed") == import_type("LongObjectBraced"));
	CHECK(import_type("NestedNamed") == import_type("NestedBraced"));
	if (kind == MemberPointerFixtureKind::Primary) CHECK(import_type("MixedNamed") == import_type("MixedBraced"));
	const auto pointer_value = [&](std::string_view name) {
		const TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(import_type(name)));
		const TypeId first = unpackStructuralValueSubobjects(table.node(value).array_extent);
		return table.node(table.templateArgumentValue(first));
	};
	const CanonicalTypeNode narrow = pointer_value("ShortNamed");
	const CanonicalTypeNode wide = pointer_value("LongNamed");
	REQUIRE(narrow.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	REQUIRE(wide.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	CHECK(table.memberPointerOwner(narrow.child) != table.memberPointerOwner(wide.child));
	const TypeId signature = table.memberPointerPointee(narrow.child);
	const TypeId narrow_type = table.builtin(CanonicalBuiltinKind::Short);
	CHECK(table.node(signature).child == narrow_type);
	CHECK(table.functionParameterType(table.functionParameters(signature)) == narrow_type);
	CHECK(table.node(signature).qualifiers == CVQualifier::Const);
	CHECK(table.node(signature).flags == (CanonicalTypeNodeFlags::FunctionLValueRef | CanonicalTypeNodeFlags::NoexceptFunction));
	CHECK(table.node(table.memberPointerPointee(wide.child)).child == table.builtin(CanonicalBuiltinKind::LongLong));
	const CanonicalTypeNode nested = pointer_value("NestedNamed");
	const TypeId nested_signature = table.memberPointerPointee(nested.child);
	CHECK(table.node(nested_signature).child == table.pointer(narrow_type));
	CHECK(table.functionParameterType(table.functionParameters(nested_signature)) == table.pointer(narrow_type));
	const CanonicalTypeNode object = pointer_value("ShortObjectNamed");
	CHECK(object.kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(table.memberPointerOwner(object.child) == table.memberPointerOwner(narrow.child));
	CHECK(table.memberPointerPointee(object.child) == narrow_type);
	if (kind != MemberPointerFixtureKind::Primary) {
		const auto primary = gTemplateRegistry.lookupTemplate("Key");
		REQUIRE(primary.has_value());
		const TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(import_type("ShortNamed")));
		const TypeId holder = table.node(value).child;
		REQUIRE(table.node(holder).kind == CanonicalTypeKind::TemplateSpecialization);
		CHECK(table.templateSpecializationDecl(holder) == primary->as<TemplateClassDeclarationNode>().template_decl_id());
		const TypeId argument = table.templateArgumentType(table.templateSpecializationArguments(holder));
		if (kind == MemberPointerFixtureKind::PointerPartial) {
			CHECK(argument == table.pointer(narrow_type));
		} else {
			const auto box = gTemplateRegistry.lookupTemplate("Box");
			REQUIRE(box.has_value());
			const TypeId arguments[] = {narrow_type};
			CHECK(argument == table.templateSpecialization(box->as<TemplateClassDeclarationNode>().template_decl_id(), arguments));
			CHECK(import_type("OtherNamed") == import_type("OtherBraced"));
			CHECK(import_type("OtherNamed") != import_type("ShortNamed"));
		}
	}
}

TEST_CASE("Templated member pointer fields publish concrete canonical graphs") {
	checkTemplatedMemberPointerFields("tests/test_canonical_structural_nttp_templated_member_pointer_ret0.cpp", MemberPointerFixtureKind::Primary);
}

TEST_CASE("Partial specialization member pointer fields publish concrete canonical graphs") {
	checkTemplatedMemberPointerFields("tests/test_canonical_structural_nttp_partial_member_pointer_ret0.cpp", MemberPointerFixtureKind::PointerPartial);
}

TEST_CASE("Nested specialization partial patterns publish parameter environments") {
	checkTemplatedMemberPointerFields("tests/test_canonical_structural_nttp_nested_partial_pattern_ret0.cpp", MemberPointerFixtureKind::NestedPartial);
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_structural_nttp_nested_partial_pattern_ret0.cpp");
	REQUIRE(fixture.is_open());
	const std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("nested_partial_pattern.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto template_id = [&](std::string_view name) {
		const auto declaration = gTemplateRegistry.lookupTemplate(name);
		REQUIRE(declaration.has_value());
		return declaration->as<TemplateClassDeclarationNode>().template_decl_id();
	};
	const TemplateDeclId key = template_id("Key");
	const TypeId parameters[] = {table.templateParameter(key, 0)};
	const TypeId box[] = {table.templateSpecialization(template_id("Box"), parameters)};
	const TypeId other[] = {table.templateSpecialization(template_id("OtherBox"), parameters)};
	const size_t declarations_before = context.templateDecls().size();
	const TemplateDeclId box_environment = context.templateDecls().publishClassPartialEnvironment(key, table.templateSpecialization(key, box));
	const TemplateDeclId other_environment = context.templateDecls().publishClassPartialEnvironment(key, table.templateSpecialization(key, other));
	CHECK(context.templateDecls().size() == declarations_before);
	CHECK(box_environment != other_environment);
	bool found_forward = false;
	size_t patterns = 0;
	for (const ASTNode& node : parser.get_nodes()) {
		if (node.is<TemplateClassDeclarationNode>()) {
			const TemplateDeclId environment = node.as<TemplateClassDeclarationNode>().class_decl_node().partial_specialization_environment();
			if (environment) {
				CHECK(environment == box_environment);
				found_forward = true;
			}
		}
		if (!node.is<StructDeclarationNode>()) continue;
		const StructDeclarationNode& pattern = node.as<StructDeclarationNode>();
		if (!gTemplateRegistry.isClassTemplateSpecializationPattern(&pattern)) continue;
		REQUIRE(pattern.partial_specialization_environment());
		CHECK_FALSE(pattern.has_template_decl_id());
		++patterns;
	}
	CHECK(patterns == 5);
	CHECK(found_forward);
}

TEST_CASE("Nested structural NTTP member pointer fields publish concrete canonical schemas") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_structural_nttp_nested_member_pointer_ret0.cpp");
	REQUIRE(fixture.is_open());
	const std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("nested_member_pointer_nttp.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		CHECK_FALSE(table.hasDependentTypeComponents(imported.type));
		return imported.type;
	};
	const auto import_key = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const StructTypeInfo* record = canonicalClassStructInfoFromTypeInfo(*info);
		REQUIRE(record != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		CHECK_FALSE(table.hasDependentTypeComponents(imported.type));
		CHECK(table.node(imported.type).kind == CanonicalTypeKind::NestedRecord);
		REQUIRE(table.hasRecordSubobjectTypes(imported.type));
		REQUIRE(table.hasRecordProperties(imported.type));
		REQUIRE(table.hasRecordFieldSchema(imported.type));
		REQUIRE(table.hasClassBaseSchema(imported.type));
		REQUIRE(table.recordSubobjectMemberCount(imported.type) == 3);
		REQUIRE(record->base_classes.size() == 1);
		CHECK(table.classBaseAt(imported.type, 0).offset_bytes == 0);
		CHECK(table.recordMemberAt(imported.type, 0).offset_bytes > 0);
		return imported.type;
	};
	const TypeId short_key = import_key("ShortKey");
	const TypeId long_key = import_key("LongKey");
	CHECK(short_key != long_key);
	const auto check_self_key = [&](std::string_view alias, std::string_view tag, CanonicalBuiltinKind builtin) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(alias));
		REQUIRE(info != nullptr);
		REQUIRE(canonicalClassStructInfoFromTypeInfo(*info) != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		const TypeId self = imported.type;
		REQUIRE(table.node(self).kind == CanonicalTypeKind::NestedRecord);
		REQUIRE(table.hasRecordFieldSchema(self));
		REQUIRE(table.hasRecordSubobjectTypes(self));
		REQUIRE(table.recordSubobjectMemberCount(self) == 4);
		const TypeId pointer_type = table.recordSubobjectMemberAt(self, 1);
		REQUIRE(table.node(pointer_type).kind == CanonicalTypeKind::MemberObjectPointer);
		CHECK(table.memberPointerOwner(pointer_type) == self);
		CHECK(table.memberPointerPointee(pointer_type) == table.builtin(builtin));
		const TypeId function_type = table.recordSubobjectMemberAt(self, 2);
		REQUIRE(table.node(function_type).kind == CanonicalTypeKind::MemberFunctionPointer);
		CHECK(table.memberPointerOwner(function_type) == self);
		const TypeId signature = table.memberPointerPointee(function_type);
		CHECK(table.node(signature).child == table.builtin(builtin));
		CHECK(table.functionParameterType(table.functionParameters(signature)) == table.builtin(builtin));
		const TypeId array_type = table.recordSubobjectMemberAt(self, 3);
		REQUIRE(table.node(array_type).kind == CanonicalTypeKind::Array);
		CHECK(table.node(array_type).array_extent == 2);
		REQUIRE(table.node(table.node(array_type).child).kind == CanonicalTypeKind::MemberObjectPointer);
		CHECK(table.memberPointerOwner(table.node(array_type).child) == self);
		CHECK(table.memberPointerPointee(table.node(array_type).child) == table.builtin(builtin));
		const TypeId tag_type = import_type(tag);
		const TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(tag_type));
		REQUIRE(table.node(value).kind == CanonicalTypeKind::StructuralValue);
		TypeId value_link = unpackStructuralValueSubobjects(table.node(value).array_extent);
		value_link = table.templateArgumentNext(value_link);
		const TypeId pointer_value = table.templateArgumentValue(value_link);
		REQUIRE(table.node(pointer_value).kind == CanonicalTypeKind::MemberObjectPointerValue);
		CHECK(unpackMemberObjectPointerValueDeclaringClass(table.node(pointer_value).array_extent) == self);
		const TypeId function_value = table.templateArgumentValue(table.templateArgumentNext(value_link));
		REQUIRE(table.node(function_value).kind == CanonicalTypeKind::MemberFunctionPointerValue);
		CHECK(unpackMemberFunctionPointerValueDeclaringClass(table.node(function_value).array_extent) == self);
		const TypeId array_value = table.templateArgumentValue(table.templateArgumentNext(table.templateArgumentNext(value_link)));
		REQUIRE(table.node(array_value).kind == CanonicalTypeKind::StructuralArrayValue);
		CHECK(unpackStructuralArrayElementCount(table.node(array_value).array_extent) == 2);
		CHECK(table.node(array_value).child == array_type);
		REQUIRE(table.node(array_type).kind == CanonicalTypeKind::Array);
		CHECK(table.node(array_type).array_extent == 2);
		const TypeId array_element_type = table.node(array_type).child;
		REQUIRE(table.node(array_element_type).kind == CanonicalTypeKind::MemberObjectPointer);
		TypeId element_link = unpackStructuralValueSubobjects(table.node(array_value).array_extent);
		const CanonicalTypeNode first_element = table.node(table.templateArgumentValue(element_link));
		REQUIRE(first_element.kind == CanonicalTypeKind::MemberObjectPointerValue);
		CHECK(first_element.child == array_element_type);
		CHECK(unpackMemberObjectPointerValueDeclaringClass(first_element.array_extent) == self);
		CHECK(unpackMemberObjectPointerValueOrdinal(first_element.array_extent) == 0);
		element_link = table.templateArgumentNext(element_link);
		const CanonicalTypeNode second_element = table.node(table.templateArgumentValue(element_link));
		REQUIRE(second_element.kind == CanonicalTypeKind::NullPointerValue);
		CHECK(second_element.child == array_element_type);
		return self;
	};
	const TypeId short_self = check_self_key("ShortSelf", "SelfNamed", CanonicalBuiltinKind::Short);
	CHECK(check_self_key("LongSelf", "LongSelfNamed", CanonicalBuiltinKind::LongLong) != short_self);
	CHECK(import_type("SelfNamed") == import_type("SelfBraced"));
	CHECK(import_type("LongSelfNamed") == import_type("LongSelfBraced"));
	const auto check_value = [&](std::string_view alias, CanonicalBuiltinKind builtin) {
		const TypeId key = import_type(alias);
		const TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(key));
		REQUIRE(table.node(value).kind == CanonicalTypeKind::StructuralValue);
		TypeId link = unpackStructuralValueSubobjects(table.node(value).array_extent);
		link = table.templateArgumentNext(link);
		const CanonicalTypeNode object = table.node(table.templateArgumentValue(link));
		REQUIRE(object.kind == CanonicalTypeKind::MemberObjectPointerValue);
		CHECK(table.memberPointerOwner(object.child) == table.memberPointerOwner(table.node(
			table.templateArgumentValue(table.templateArgumentNext(link))).child));
		CHECK(table.memberPointerPointee(object.child) == table.builtin(builtin));
		link = table.templateArgumentNext(link);
		const CanonicalTypeNode function = table.node(table.templateArgumentValue(link));
		REQUIRE(function.kind == CanonicalTypeKind::MemberFunctionPointerValue);
		const TypeId owner = unpackMemberFunctionPointerValueDeclaringClass(function.array_extent);
		REQUIRE(table.node(owner).kind == CanonicalTypeKind::TemplateSpecialization);
		const TypeId signature = table.memberPointerPointee(function.child);
		CHECK(table.node(signature).child == table.builtin(builtin));
		CHECK(table.functionParameterType(table.functionParameters(signature)) == table.builtin(builtin));
		CHECK(table.memberFunctionSignatureAt(owner, unpackMemberFunctionPointerValueOrdinal(function.array_extent)) == signature);
		link = table.templateArgumentNext(link);
		CHECK(table.node(table.templateArgumentValue(link)).array_extent == (builtin == CanonicalBuiltinKind::Short ? 3 : 5));
		return owner;
	};
	const TypeId short_owner = check_value("ShortNamed", CanonicalBuiltinKind::Short);
	const TypeId long_owner = check_value("LongNamed", CanonicalBuiltinKind::LongLong);
	CHECK(short_owner != long_owner);
	CHECK(import_type("ShortNamed") == import_type("ShortBraced"));
	CHECK(import_type("LongNamed") == import_type("LongBraced"));
	CHECK(import_type("ShortNamed") == import_type("ShortRepeatAlias"));
	CHECK(import_type("ShortNamed") != import_type("ChangedObject"));
}

TEST_CASE("Canonical member field substitution rejects incomplete and unresolved environments") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	const TemplateDeclId environment{73};
	const TypeId parameter = table.templateParameter(environment, 0);
	const TypeId owner_arguments[] = {parameter};
	const TypeId owner = table.templateSpecialization(TemplateDeclId{74}, owner_arguments);
	const TypeId pattern = table.memberObjectPointer(owner, table.pointer(parameter));
	TemplateTypeArg arguments[1];
	arguments[0].type_index = nativeTypeIndex(TypeCategory::Short);
	CHECK_FALSE(substituteCanonicalStructMemberType(table, pattern, TemplateDeclId{}, arguments));
	CHECK_FALSE(substituteCanonicalStructMemberType(table, pattern, TemplateDeclId{75}, arguments));
	CHECK_FALSE(substituteCanonicalStructMemberType(table, pattern, environment, std::span<const TemplateTypeArg>{}));
	const TypeId unresolved = table.dependentName(parameter, "missing");
	CHECK_FALSE(substituteCanonicalStructMemberType(table, unresolved, environment, arguments));
	const CanonicalTemplateArgument opaque_arguments[] = {CanonicalTemplateArgument::makeNonType(ExprId{91})};
	const TypeId opaque_owner = table.templateSpecialization(TemplateDeclId{74}, opaque_arguments);
	const TypeId opaque_pointer = table.memberObjectPointer(opaque_owner, table.builtin(CanonicalBuiltinKind::Int));
	CHECK_FALSE(substituteCanonicalStructMemberType(table, opaque_pointer, environment, arguments));
	const TypeId fixed_value = table.integralValue(table.builtin(CanonicalBuiltinKind::Int), 3);
	const CanonicalTemplateArgument fixed_arguments[] = {CanonicalTemplateArgument::makeValue(fixed_value)};
	const TypeId fixed_owner = table.templateSpecialization(TemplateDeclId{74}, fixed_arguments);
	const TypeId fixed_pointer = table.memberObjectPointer(fixed_owner, table.builtin(CanonicalBuiltinKind::Int));
	CHECK(substituteCanonicalStructMemberType(table, fixed_pointer, TemplateDeclId{}, arguments) == fixed_pointer);
	const TemplateTypeArg wrong_kind[] = {TemplateTypeArg(int64_t{3})};
	CHECK_FALSE(substituteCanonicalStructMemberType(table, pattern, environment, wrong_kind));
	arguments[0].is_dependent = true;
	const size_t before_rejection = table.size();
	CHECK_FALSE(substituteCanonicalStructMemberType(table, pattern, environment, arguments));
	CHECK(table.size() == before_rejection);
	arguments[0].is_dependent = false;
	const TypeId concrete = substituteCanonicalStructMemberType(table, pattern, environment, arguments);
	REQUIRE(concrete);
	CHECK_FALSE(table.hasDependentTypeComponents(concrete));
	TypeId deep = pattern;
	TypeId expected = concrete;
	for (size_t depth = 0; depth < 4096; ++depth) {
		deep = table.pointer(deep);
		expected = table.pointer(expected);
	}
	CHECK(substituteCanonicalStructMemberType(table, deep, environment, arguments) == expected);
}

TEST_CASE("Partial specialization member pointer patterns retain published parameter environments") {
	MESSAGE("sizeof(StructDeclarationNode)=", sizeof(StructDeclarationNode), "; sizeof(TemplateDeclTable)=", sizeof(TemplateDeclTable));
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_partial_member_pointer_environment_ret0.cpp");
	REQUIRE(fixture.is_open());
	const std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("partial_member_pointer_environment.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto primary_node = gTemplateRegistry.lookupTemplate("Key");
	REQUIRE(primary_node.has_value());
	const TemplateDeclId primary = primary_node->as<TemplateClassDeclarationNode>().template_decl_id();
	TemplateDeclId forward_environment;
	for (const ASTNode& node : parser.get_nodes()) {
		if (!node.is<TemplateClassDeclarationNode>()) continue;
		const TemplateClassDeclarationNode& declaration = node.as<TemplateClassDeclarationNode>();
		const TemplateDeclId environment = declaration.class_decl_node().partial_specialization_environment();
		if (!environment) continue;
		forward_environment = environment;
		TemplateTypeArg wrong_category;
		wrong_category.type_index = declaration.template_parameters()[0].registered_type_index().withCategory(TypeCategory::Int);
		wrong_category.is_dependent = true;
		const TemplateTypeArg arguments[] = {wrong_category};
		CHECK_FALSE(tryImportCanonicalPartialClassPattern(context, primary, declaration.template_parameters(), arguments, {},
			kCanonicalPartialPatternNone));
	}
	REQUIRE(forward_environment);
	std::vector<TemplateDeclId> environments;
	CanonicalTypeTable& table = context.canonicalTypes();
	size_t patterns = 0;
	for (const ASTNode& node : parser.get_nodes()) {
		if (!node.is<StructDeclarationNode>()) continue;
		const StructDeclarationNode& pattern = node.as<StructDeclarationNode>();
		if (!gTemplateRegistry.isClassTemplateSpecializationPattern(&pattern)) continue;
		++patterns;
		const TemplateDeclId environment = pattern.partial_specialization_environment();
		REQUIRE(environment);
		CHECK(environment != primary);
		CHECK_FALSE(pattern.has_template_decl_id());
		environments.push_back(environment);
		const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		CHECK(field.has_template_parameter_decl());
		CHECK(field.template_decl_id() == environment);
		for (const StructMemberDecl& member : pattern.members()) {
			const TypeId imported = importCanonicalStructMemberType(table, member.declaration.as<DeclarationNode>().type_specifier_node());
			REQUIRE(imported);
			CHECK(table.hasDependentTypeComponents(imported));
			CHECK(table.dependsOnlyOnTemplateParameters(imported, environment));
			const TypeId arguments[] = {table.builtin(CanonicalBuiltinKind::Short)};
			const TypeId concrete = table.substitute(imported, environment, arguments);
			CHECK_FALSE(table.hasDependentTypeComponents(concrete));
			CHECK(table.substitute(imported, primary, arguments) == imported);
		}
	}
	CHECK(patterns == 2);
	REQUIRE(environments.size() == 2);
	CHECK(environments[0] == forward_environment);
	CHECK(environments[0] != environments[1]);
}

TEST_CASE("Partial parameter environments merge canonical patterns and roll back with declarations") {
	FrontendContext context;
	TemplateDeclTable& declarations = context.templateDecls();
	CanonicalTypeTable& types = context.canonicalTypes();
	const OwnerId owner{1};
	const TemplateDeclId primary = declarations.publishPrimaryClassTemplate(owner, StringTable::getOrInternStringHandle("Key"));
	const TypeId parameter = types.templateParameter(primary, 0);
	const TypeId pointer_arguments[] = {types.pointer(parameter)};
	const TypeId reference_arguments[] = {types.reference(parameter, ReferenceQualifier::LValueReference)};
	const TypeId pointer = types.templateSpecialization(primary, pointer_arguments);
	const TypeId reference = types.templateSpecialization(primary, reference_arguments);
	const TemplateDeclId environment = declarations.publishClassPartialEnvironment(primary, pointer);
	CHECK(environment != primary);
	CHECK(declarations.publishClassPartialEnvironment(primary, pointer) == environment);
	const size_t committed_count = declarations.size();
	{
		TemplateDeclTableTransaction transaction(declarations);
		const TemplateDeclId temporary = declarations.publishClassPartialEnvironment(primary, reference);
		CHECK(temporary != environment);
		CHECK(declarations.size() == committed_count + 1);
	}
	CHECK(declarations.size() == committed_count);
	CHECK(declarations.publishClassPartialEnvironment(primary, pointer) == environment);
	CHECK_THROWS_AS(declarations.publishClassPartialEnvironment(TemplateDeclId{}, pointer), InternalError);
	CHECK_THROWS_AS(declarations.publishClassPartialEnvironment(primary, TypeId{}), InternalError);
	CHECK_THROWS_AS(declarations.publishClassPartialEnvironment(TemplateDeclId{99}, pointer), InternalError);
}

TEST_CASE("Partial parameter environment publication defers unsupported declaration families") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext compile_context;
	compile_context.setInputFile("partial_environment_deferral.cpp");
	Lexer lexer(R"(
template<class T, int N> struct Mixed {};
template<class T, int N> struct Mixed<T*, N> { T member; };
template<class... T> struct Pack {};
template<class T, class... Rest> struct Pack<T*, Rest...> { T member; };
template<class T> concept Any = true;
template<class T> struct Constrained {};
template<Any T> struct Constrained<T*> { T member; };
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	size_t patterns = 0;
	for (const ASTNode& node : parser.get_nodes()) {
		if (!node.is<StructDeclarationNode>()) continue;
		const StructDeclarationNode& pattern = node.as<StructDeclarationNode>();
		if (!gTemplateRegistry.isClassTemplateSpecializationPattern(&pattern)) continue;
		CHECK_FALSE(pattern.partial_specialization_environment());
		++patterns;
	}
	CHECK(patterns == 3);
}

TEST_CASE("Partial pattern normalization keeps deep declarators off the native stack") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	CanonicalTypeTable& table = context.canonicalTypes();
	const StringHandle name = StringTable::getOrInternStringHandle("T");
	TypeInfo& parameter_info = add_template_param_type(name, TypeCategory::UserDefined, 0);
	TemplateParameterNode parameters[] = {TemplateParameterNode(name, Token{})};
	parameters[0].set_registered_type_index(parameter_info.type_index_);
	TemplateTypeArg arguments[1];
	arguments[0].type_index = parameter_info.type_index_;
	arguments[0].is_dependent = true;
	arguments[0].dependent_name = name;
	arguments[0].ordered_declarator.assign(4096, DeclaratorComponent::pointer(CVQualifier::Const));
	const TemplateDeclId primary = context.templateDecls().publishPrimaryClassTemplate(OwnerId{1}, StringTable::getOrInternStringHandle("Deep"));
	const std::optional<TypeId> pattern =
		tryImportCanonicalPartialClassPattern(context, primary, parameters, arguments, {},
			kCanonicalPartialPatternNone);
	REQUIRE(pattern.has_value());
	TypeId current = table.templateArgumentType(table.templateSpecializationArguments(*pattern));
	for (size_t depth = 0; depth < 4096; ++depth) {
		const CanonicalTypeNode qualified = table.node(current);
		REQUIRE(qualified.kind == CanonicalTypeKind::Qualified);
		CHECK(qualified.qualifiers == CVQualifier::Const);
		const CanonicalTypeNode pointer = table.node(qualified.child);
		REQUIRE(pointer.kind == CanonicalTypeKind::Pointer);
		current = pointer.child;
	}
	CHECK(current == table.templateParameter(primary, 0));
}

TEST_CASE("Nested partial normalization reclaims scratch and rejects unsupported argument kinds") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	const StringHandle name = StringTable::getOrInternStringHandle("T");
	TypeInfo& parameter = add_template_param_type(name, TypeCategory::UserDefined, 0);
	TypeInfo& second = add_template_param_type(StringTable::getOrInternStringHandle("U"), TypeCategory::UserDefined, 0);
	TemplateParameterNode parameters[] = {TemplateParameterNode(name, Token{}), TemplateParameterNode(second.name(), Token{})};
	parameters[0].set_registered_type_index(parameter.type_index_);
	parameters[1].set_registered_type_index(second.type_index_);
	TemplateTypeArg arguments[1];
	arguments[0].type_index = parameter.type_index_;
	arguments[0].is_dependent = true;
	const TemplateDeclId primary = context.templateDecls().publishPrimaryClassTemplate(OwnerId{1}, StringTable::getOrInternStringHandle("Key"));
	const TemplateDeclId box = context.templateDecls().publishPrimaryClassTemplate(OwnerId{1}, StringTable::getOrInternStringHandle("Box"));
	TypeSpecifierNode leaf(parameter.type_index_, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	TypeSpecifierNode mixed;
	mixed.set_template_specialization_mixed(box, {SpecTemplateArgKind::Type, SpecTemplateArgKind::NonType}, {leaf}, {ExprId{91}}, {}, {});
	const ASTNode mixed_syntax[] = {ASTNode::emplace_node<TypeSpecifierNode>(std::move(mixed))};
	const size_t before = context.canonicalTypes().size();
	const uint64_t scratch_before = context.scratchArena().currentBytes();
	CHECK_FALSE(tryImportCanonicalPartialClassPattern(context, primary, parameters, arguments, mixed_syntax,
		kCanonicalPartialPatternNone));
	CHECK(context.canonicalTypes().size() == before);
	CHECK(context.scratchArena().currentBytes() == scratch_before);
	TypeSpecifierNode alias;
	alias.set_alias_template_specialization(box, {leaf});
	const ASTNode alias_syntax[] = {ASTNode::emplace_node<TypeSpecifierNode>(std::move(alias))};
	CHECK_FALSE(tryImportCanonicalPartialClassPattern(context, primary, parameters, arguments, alias_syntax,
		kCanonicalPartialPatternNone));
	CHECK(context.canonicalTypes().size() == before);
	CHECK(context.scratchArena().currentBytes() == scratch_before);
	TypeSpecifierNode empty;
	empty.set_template_specialization(box, {});
	TypeSpecifierNode second_leaf(second.type_index_, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	TypeSpecifierNode supported;
	supported.set_template_specialization(box, {second_leaf, empty, leaf});
	const ASTNode supported_syntax[] = {ASTNode::emplace_node<TypeSpecifierNode>(std::move(supported))};
	const std::optional<TypeId> result = tryImportCanonicalPartialClassPattern(context, primary, parameters, arguments,
		supported_syntax, kCanonicalPartialPatternNone);
	REQUIRE(result.has_value());
	CHECK(context.scratchArena().currentBytes() == scratch_before);
	CHECK(context.canonicalTypes().hasDependentTypeComponents(*result));
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId expected_arguments[] = {table.templateParameter(primary, 1), table.templateSpecialization(box, std::span<const TypeId>{}),
		table.templateParameter(primary, 0)};
	const TypeId expected_root[] = {table.templateSpecialization(box, expected_arguments)};
	CHECK(*result == table.templateSpecialization(primary, expected_root));
}

TEST_CASE("Nested partial normalization keeps deep syntax traversal off the native stack") {
	static_assert(std::is_nothrow_move_constructible_v<TypeSpecifierNode>);
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	const StringHandle name = StringTable::getOrInternStringHandle("T");
	TypeInfo& parameter = add_template_param_type(name, TypeCategory::UserDefined, 0);
	TemplateParameterNode parameters[] = {TemplateParameterNode(name, Token{})};
	parameters[0].set_registered_type_index(parameter.type_index_);
	TemplateTypeArg arguments[1];
	arguments[0].type_index = parameter.type_index_;
	arguments[0].is_dependent = true;
	const TemplateDeclId primary = context.templateDecls().publishPrimaryClassTemplate(OwnerId{1}, StringTable::getOrInternStringHandle("Key"));
	const TemplateDeclId box = context.templateDecls().publishPrimaryClassTemplate(OwnerId{1}, StringTable::getOrInternStringHandle("Box"));
	TypeSpecifierNode syntax(parameter.type_index_, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	for (size_t depth = 0; depth < 4096; ++depth) {
		std::vector<TypeSpecifierNode> child;
		child.push_back(std::move(syntax));
		syntax = TypeSpecifierNode{};
		syntax.set_template_specialization(box, std::move(child));
	}
	ASTNode syntax_nodes[] = {ASTNode::emplace_node<TypeSpecifierNode>(std::move(syntax))};
	TypeSpecifierNode& root = syntax_nodes[0].as<TypeSpecifierNode>();
	auto destroy_syntax = ScopeGuard([&root]() {
		while (root.has_template_specialization()) {
			TypeSpecifierNode child = std::move(root.specialization_arg_type(0));
			root.clear_template_specialization();
			root = std::move(child);
		}
	});
	const uint64_t before = context.scratchArena().currentBytes();
	const std::optional<TypeId> pattern = tryImportCanonicalPartialClassPattern(context, primary, parameters, arguments,
		syntax_nodes, kCanonicalPartialPatternNone);
	REQUIRE(pattern.has_value());
	CHECK(context.scratchArena().currentBytes() == before);
	CanonicalTypeTable& table = context.canonicalTypes();
	TypeId current = table.templateArgumentType(table.templateSpecializationArguments(*pattern));
	for (size_t depth = 0; depth < 4096; ++depth) {
		REQUIRE(table.node(current).kind == CanonicalTypeKind::TemplateSpecialization);
		CHECK(table.templateSpecializationDecl(current) == box);
		current = table.templateArgumentType(table.templateSpecializationArguments(current));
	}
	CHECK(current == table.templateParameter(primary, 0));
}

TEST_CASE("Inherited member object pointer structural fields keep declaring class identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Base { int first; int second; };
struct Middle : Base { int third; };
struct Object : Middle { int fourth; };
struct Key { int Object::* pointer; int tag; };
constexpr Key first{&Object::first, 1};
constexpr Key second{&Object::second, 1};
constexpr Key fourth{&Object::fourth, 1};
template<Key Value> struct BaseHolder { short member; };
struct Padding { long long guard; };
struct Derived : Padding, BaseHolder<first> {};
using Named = BaseHolder<first>;
using Literal = BaseHolder<Key{&Object::first, 1}>;
using ChangedSecond = BaseHolder<second>;
using ChangedOwner = BaseHolder<fourth>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_inherited_member_object_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_pointer = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		const CanonicalTypeNode value = table.node(value_id);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
		REQUIRE(pointer.kind == CanonicalTypeKind::MemberObjectPointerValue);
		return pointer;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	const TypeId base_type = import_type("Base");
	const TypeId object_type = import_type("Object");
	const CanonicalTypeNode first_pointer = structural_pointer(named);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(first_pointer.array_extent) == base_type);
	CHECK(unpackMemberObjectPointerValueOrdinal(first_pointer.array_extent) == 0);
	const CanonicalTypeNode second_pointer = structural_pointer(import_type("ChangedSecond"));
	CHECK(unpackMemberObjectPointerValueDeclaringClass(second_pointer.array_extent) == base_type);
	CHECK(unpackMemberObjectPointerValueOrdinal(second_pointer.array_extent) == 1);
	const CanonicalTypeNode fourth_pointer = structural_pointer(import_type("ChangedOwner"));
	CHECK(unpackMemberObjectPointerValueDeclaringClass(fourth_pointer.array_extent) == object_type);
	CHECK(unpackMemberObjectPointerValueOrdinal(fourth_pointer.array_extent) == 0);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Overloaded member function pointer structural fields select by signature") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Object { int first(int value); int first(long value); int second(int value); };
struct IntKey { int (Object::*pointer)(int); int tag; };
struct LongKey { int (Object::*pointer)(long); int tag; };
constexpr IntKey int_first{&Object::first, 1};
constexpr IntKey int_second{&Object::second, 1};
constexpr LongKey long_first{&Object::first, 1};
template<IntKey Value> struct IntBase { short member; };
template<LongKey Value> struct LongBase { short member; };
struct Padding { long long guard; };
struct Derived : Padding, IntBase<int_first> {};
using Named = IntBase<int_first>;
using Literal = IntBase<IntKey{&Object::first, 1}>;
using ChangedName = IntBase<int_second>;
using LongNamed = LongBase<long_first>;
using LongLiteral = LongBase<LongKey{&Object::first, 1}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_overloaded_member_function_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_ordinal = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		const CanonicalTypeNode value = table.node(value_id);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
		REQUIRE(pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
		return static_cast<size_t>(unpackMemberFunctionPointerValueOrdinal(pointer.array_extent));
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("ChangedName"));
	const TypeId long_named = import_type("LongNamed");
	CHECK(long_named == import_type("LongLiteral"));
	CHECK(structural_ordinal(named) == 0);
	CHECK(structural_ordinal(import_type("ChangedName")) == 2);
	CHECK(structural_ordinal(long_named) == 1);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Member object pointer array elements import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Object { int first; int second; };
struct Key { int Object::* fields[2]; int tag; };
constexpr Key first{{&Object::first, &Object::second}, 1};
constexpr Key second{{&Object::first, &Object::first}, 1};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{{&Object::first, &Object::second}, 1}>;
using Changed = Base<second>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_member_object_pointer_array.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId object_type = import_type("Object");
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode fields = table.node(table.templateArgumentValue(link));
	REQUIRE(fields.kind == CanonicalTypeKind::StructuralArrayValue);
	TypeId field_link = unpackStructuralValueSubobjects(fields.array_extent);
	const CanonicalTypeNode first_field = table.node(table.templateArgumentValue(field_link));
	CHECK(first_field.kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(first_field.array_extent) == object_type);
	CHECK(unpackMemberObjectPointerValueOrdinal(first_field.array_extent) == 0);
	field_link = table.templateArgumentNext(field_link);
	const CanonicalTypeNode second_field = table.node(table.templateArgumentValue(field_link));
	CHECK(second_field.kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(unpackMemberObjectPointerValueOrdinal(second_field.array_extent) == 1);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Member function pointer array elements import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Object { int first(int value); int second(int value); };
struct Key { int (Object::*methods[2])(int); int tag; };
constexpr Key first{{&Object::first, &Object::second}, 1};
constexpr Key second{{&Object::first, &Object::first}, 1};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{{&Object::first, &Object::second}, 1}>;
using Changed = Base<second>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_member_function_pointer_array.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode methods = table.node(table.templateArgumentValue(link));
	REQUIRE(methods.kind == CanonicalTypeKind::StructuralArrayValue);
	CHECK(table.node(methods.child).kind == CanonicalTypeKind::Array);
	CHECK(table.node(table.node(methods.child).child).kind == CanonicalTypeKind::MemberFunctionPointer);
	TypeId method_link = unpackStructuralValueSubobjects(methods.array_extent);
	const CanonicalTypeNode first_method = table.node(table.templateArgumentValue(method_link));
	CHECK(first_method.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(first_method.array_extent) == import_type("Object"));
	CHECK(unpackMemberFunctionPointerValueOrdinal(first_method.array_extent) == 0);
	method_link = table.templateArgumentNext(method_link);
	const CanonicalTypeNode second_method = table.node(table.templateArgumentValue(method_link));
	CHECK(second_method.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	CHECK(unpackMemberFunctionPointerValueOrdinal(second_method.array_extent) == 1);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Inherited member function pointer structural fields keep declaring class identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Base { int first(int value); int second(int value); };
struct Middle : Base { int third(int value); };
struct Object : Middle { int fourth(int value); };
struct Key { int (Object::*pointer)(int); int tag; };
constexpr Key first{&Object::first, 1};
constexpr Key second{&Object::second, 1};
constexpr Key fourth{&Object::fourth, 1};
template<Key Value> struct BaseHolder { short member; };
struct Padding { long long guard; };
struct Derived : Padding, BaseHolder<first> {};
using Named = BaseHolder<first>;
using Literal = BaseHolder<Key{&Object::first, 1}>;
using ChangedSecond = BaseHolder<second>;
using ChangedOwner = BaseHolder<fourth>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_inherited_member_function_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_target = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		const CanonicalTypeNode value = table.node(value_id);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
		REQUIRE(pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
		return pointer;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	const TypeId base_type = import_type("Base");
	const TypeId object_type = import_type("Object");
	const CanonicalTypeNode first_pointer = structural_target(named);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(first_pointer.array_extent) == base_type);
	CHECK(unpackMemberFunctionPointerValueOrdinal(first_pointer.array_extent) == 0);
	const CanonicalTypeNode second_pointer = structural_target(import_type("ChangedSecond"));
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(second_pointer.array_extent) == base_type);
	CHECK(unpackMemberFunctionPointerValueOrdinal(second_pointer.array_extent) == 1);
	const CanonicalTypeNode fourth_pointer = structural_target(import_type("ChangedOwner"));
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(fourth_pointer.array_extent) == object_type);
	CHECK(unpackMemberFunctionPointerValueOrdinal(fourth_pointer.array_extent) == 0);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Virtual member function pointer structural fields import by declaring class") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Base { virtual int first(int value) { return value; } virtual int second(int value) { return value; } };
struct Middle : Base {};
struct Key { int (Middle::*pointer)(int); int tag; };
struct DirectKey { int (Base::*pointer)(int); int tag; };
constexpr Key first{&Middle::first, 1};
constexpr Key second{&Middle::second, 1};
constexpr DirectKey direct_first{&Base::first, 1};
template<Key Value> struct BaseHolder { short member; };
template<DirectKey Value> struct DirectHolder { short member; };
struct Padding { long long guard; };
struct Derived : Padding, BaseHolder<first> {};
using Named = BaseHolder<first>;
using Literal = BaseHolder<Key{&Middle::first, 1}>;
using Changed = BaseHolder<second>;
using DirectNamed = DirectHolder<direct_first>;
using DirectLiteral = DirectHolder<DirectKey{&Base::first, 1}>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_virtual_member_function_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_target = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		const CanonicalTypeNode value = table.node(value_id);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		const CanonicalTypeNode pointer = table.node(table.templateArgumentValue(link));
		REQUIRE(pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
		return pointer;
	};
	const TypeId base_type = import_type("Base");
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const CanonicalTypeNode first_pointer = structural_target(named);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(first_pointer.array_extent) == base_type);
	CHECK(unpackMemberFunctionPointerValueOrdinal(first_pointer.array_extent) == 0);
	const CanonicalTypeNode second_pointer = structural_target(import_type("Changed"));
	CHECK(unpackMemberFunctionPointerValueOrdinal(second_pointer.array_extent) == 1);
	const TypeId direct_named = import_type("DirectNamed");
	CHECK(direct_named == import_type("DirectLiteral"));
	const CanonicalTypeNode direct_pointer = structural_target(direct_named);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(direct_pointer.array_extent) == base_type);
	CHECK(unpackMemberFunctionPointerValueOrdinal(direct_pointer.array_extent) == 0);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Specialization member pointer structural fields use specialization identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Object { int first; int second; int method(int value); int other(int value); };
struct FieldKey { int Object<int>::* pointer; int tag; };
struct MethodKey { int (Object<int>::*pointer)(int); int tag; };
constexpr FieldKey field_first{&Object<int>::first, 1};
constexpr FieldKey field_second{&Object<int>::second, 1};
constexpr MethodKey method_first{&Object<int>::method, 1};
constexpr MethodKey method_second{&Object<int>::other, 1};
template<FieldKey Value> struct FieldBase { short member; };
template<MethodKey Value> struct MethodBase { short member; };
struct Padding { long long guard; };
struct Derived : Padding, FieldBase<field_first> {};
using FieldNamed = FieldBase<field_first>;
using FieldLiteral = FieldBase<FieldKey{&Object<int>::first, 1}>;
using FieldChanged = FieldBase<field_second>;
using MethodNamed = MethodBase<method_first>;
using MethodLiteral = MethodBase<MethodKey{&Object<int>::method, 1}>;
using MethodChanged = MethodBase<method_second>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_specialization_member_pointer.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_pointer = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		const CanonicalTypeNode value = table.node(value_id);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		return table.node(table.templateArgumentValue(link));
	};
	const TypeId field_named = import_type("FieldNamed");
	CHECK(field_named == import_type("FieldLiteral"));
	CHECK(field_named != import_type("FieldChanged"));
	const CanonicalTypeNode field_pointer = structural_pointer(field_named);
	CHECK(field_pointer.kind == CanonicalTypeKind::MemberObjectPointerValue);
	const CanonicalTypeNode field_type = table.node(field_pointer.child);
	CHECK(field_type.kind == CanonicalTypeKind::MemberObjectPointer);
	const TypeId field_owner{static_cast<uint32_t>(field_type.array_extent)};
	CHECK(table.node(field_owner).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(field_pointer.array_extent) == field_owner);
	CHECK(unpackMemberObjectPointerValueOrdinal(field_pointer.array_extent) == 0);
	CHECK(unpackMemberObjectPointerValueOrdinal(structural_pointer(import_type("FieldChanged")).array_extent) == 1);
	const TypeId method_named = import_type("MethodNamed");
	CHECK(method_named == import_type("MethodLiteral"));
	CHECK(method_named != import_type("MethodChanged"));
	const CanonicalTypeNode method_pointer = structural_pointer(method_named);
	CHECK(method_pointer.kind == CanonicalTypeKind::MemberFunctionPointerValue);
	const CanonicalTypeNode method_type = table.node(method_pointer.child);
	CHECK(method_type.kind == CanonicalTypeKind::MemberFunctionPointer);
	const TypeId method_owner{static_cast<uint32_t>(method_type.array_extent)};
	CHECK(table.node(method_owner).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(unpackMemberFunctionPointerValueDeclaringClass(method_pointer.array_extent) == method_owner);
	CHECK(unpackMemberFunctionPointerValueOrdinal(method_pointer.array_extent) == 0);
	CHECK(unpackMemberFunctionPointerValueOrdinal(structural_pointer(import_type("MethodChanged")).array_extent) == 1);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == field_named);
}

TEST_CASE("Nullptr member structural fields import as typed canonical values") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Key { decltype(nullptr) value; int tag; };
constexpr Key first{nullptr, 1};
constexpr Key second{nullptr, 2};
template<Key Value> struct Base { short member; };
struct Padding { long long guard; };
struct Derived : Padding, Base<first> {};
using Named = Base<first>;
using Literal = Base<Key{nullptr, 1}>;
using Changed = Base<second>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_nullptr_member.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(named));
	const CanonicalTypeNode value = table.node(value_id);
	REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
	TypeId link = unpackStructuralValueSubobjects(value.array_extent);
	const CanonicalTypeNode null_value = table.node(table.templateArgumentValue(link));
	CHECK(null_value.kind == CanonicalTypeKind::NullptrValue);
	CHECK(table.node(null_value.child).builtin == CanonicalBuiltinKind::Nullptr);
	CHECK(null_value.array_extent == 0);
	link = table.templateArgumentNext(link);
	const CanonicalTypeNode tag = table.node(table.templateArgumentValue(link));
	CHECK(tag.kind == CanonicalTypeKind::IntegralValue);
	CHECK(tag.array_extent == 1);
	const TypeId derived = import_type("Derived");
	REQUIRE(table.hasClassBaseSchema(derived));
	CHECK(table.classBaseAt(derived, 1).type == named);
}

TEST_CASE("Derived anonymous union structural values preserve base and active member") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Root { int root; };
struct Key : Root { union { int first; long long second; }; };
constexpr Key make_second() {
	Key value{7, {1}};
	value.second = 2;
	return value;
}
constexpr Key first{7, {1}};
constexpr Key second = make_second();
template<Key Value> struct Base { short member; };
using Named = Base<first>;
using Literal = Base<Key{7, {1}}>;
using Changed = Base<second>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("structural_derived_union_activity.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto structural_value = [&](TypeId specialization) {
		const TypeId value_id = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		return table.node(value_id);
	};
	const auto active_union = [&](const CanonicalTypeNode& value) {
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		const CanonicalTypeNode base = table.node(table.templateArgumentValue(link));
		REQUIRE(base.kind == CanonicalTypeKind::StructuralValue);
		const TypeId base_link = unpackStructuralValueSubobjects(base.array_extent);
		const CanonicalTypeNode root = table.node(table.templateArgumentValue(base_link));
		CHECK(root.kind == CanonicalTypeKind::IntegralValue);
		CHECK(root.array_extent == 7);
		link = table.templateArgumentNext(link);
		const CanonicalTypeNode union_value = table.node(table.templateArgumentValue(link));
		REQUIRE(union_value.kind == CanonicalTypeKind::StructuralUnionValue);
		return union_value;
	};
	const TypeId named = import_type("Named");
	CHECK(named == import_type("Literal"));
	CHECK(named != import_type("Changed"));
	const CanonicalTypeNode first_value = structural_value(named);
	const CanonicalTypeNode first_union = active_union(first_value);
	CHECK(unpackStructuralUnionActiveVariant(first_union.array_extent) == 0);
	const TypeId first_payload = unpackStructuralValueSubobjects(first_union.array_extent);
	const CanonicalTypeNode first_payload_node = table.node(table.templateArgumentValue(first_payload));
	CHECK(first_payload_node.kind == CanonicalTypeKind::IntegralValue);
	CHECK(first_payload_node.array_extent == 1);
	const CanonicalTypeNode second_value = structural_value(import_type("Changed"));
	const CanonicalTypeNode second_union = active_union(second_value);
	CHECK(unpackStructuralUnionActiveVariant(second_union.array_extent) == 1);
	const TypeId second_payload = unpackStructuralValueSubobjects(second_union.array_extent);
	const CanonicalTypeNode second_payload_node = table.node(table.templateArgumentValue(second_payload));
	CHECK(second_payload_node.kind == CanonicalTypeKind::IntegralValue);
	CHECK(second_payload_node.array_extent == 2);
}

TEST_CASE("Selected partial nested class import preserves primary specialization identity") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	CanonicalTypeTable& table = context.canonicalTypes();
	const TemplateDeclId primary{7001};
	const auto make_owner = [&](const char* name, TypeCategory element_category) -> StructDeclarationNode* {
		const StringHandle owner_name = StringTable::getOrInternStringHandle(name);
		const TypeCreationResult created = add_struct_type(owner_name, NamespaceRegistry::GLOBAL_NAMESPACE);
		TypeInfo::TemplateArgInfo argument;
		argument.type_index = nativeTypeIndex(element_category);
		argument.pointer_depth = 1;
		TypeInfo::TemplateArgInfoVector arguments;
		arguments.push_back(argument);
		created.info.setTemplateInstantiationInfo(
			QualifiedIdentifier::fromQualifiedName("Outer", NamespaceRegistry::GLOBAL_NAMESPACE), arguments);
		ASTNode pattern_node = ASTNode::emplace_node<StructDeclarationNode>(owner_name, false, false);
		ASTNode owner_node = ASTNode::emplace_node<StructDeclarationNode>(owner_name, false, false);
		StructDeclarationNode& owner = owner_node.as<StructDeclarationNode>();
		owner.set_canonical_specialization_owner_template_decl_id(primary);
		owner.set_injected_class_pattern_declaration(&pattern_node.as<StructDeclarationNode>());
		StructTypeInfo& record = created.info.createStructInfo(
			owner_name, AccessSpecifier::Public, false, NamespaceRegistry::GLOBAL_NAMESPACE);
		record.declaration_node = &owner;
		record.layout_is_complete = true;
		record.total_size = SizeInBytes{8};
		return &owner;
	};
	StructDeclarationNode* short_owner = make_owner("SelectedPartialShortOwner", TypeCategory::Short);
	StructDeclarationNode* wide_owner = make_owner("SelectedPartialWideOwner", TypeCategory::LongLong);
	const StringHandle nested_name = StringTable::getOrInternStringHandle("Key");
	const EntityId nested_entity{9701};
	const auto make_nested = [&](StructDeclarationNode* owner) -> StructDeclarationNode* {
		ASTNode pattern_node = ASTNode::emplace_node<StructDeclarationNode>(nested_name, false, false);
		pattern_node.as<StructDeclarationNode>().set_entity_id(nested_entity);
		ASTNode nested_node = ASTNode::emplace_node<StructDeclarationNode>(nested_name, false, false);
		StructDeclarationNode& nested = nested_node.as<StructDeclarationNode>();
		nested.set_injected_class_pattern_declaration(&pattern_node.as<StructDeclarationNode>());
		nested.set_enclosing_class(owner);
		return &nested;
	};
	StructDeclarationNode* short_nested = make_nested(short_owner);
	StructDeclarationNode* wide_nested = make_nested(wide_owner);
	const CanonicalTypeImport short_import = importCanonicalClassDeclaration(table, *short_nested);
	REQUIRE(short_import.status == CanonicalTypeImportStatus::Supported);
	const CanonicalTypeImport repeated_import = importCanonicalClassDeclaration(table, *short_nested);
	REQUIRE(repeated_import.status == CanonicalTypeImportStatus::Supported);
	CHECK(repeated_import.type == short_import.type);
	const CanonicalTypeImport wide_import = importCanonicalClassDeclaration(table, *wide_nested);
	REQUIRE(wide_import.status == CanonicalTypeImportStatus::Supported);
	CHECK(wide_import.type != short_import.type);
	REQUIRE(table.node(short_import.type).kind == CanonicalTypeKind::NestedRecord);
	REQUIRE(table.node(wide_import.type).kind == CanonicalTypeKind::NestedRecord);
	CHECK(table.nestedRecordEntity(short_import.type) == nested_entity);
	CHECK(table.nestedRecordEntity(wide_import.type) == nested_entity);
	const TypeId short_owner_type = table.nestedRecordOwner(short_import.type);
	const TypeId wide_owner_type = table.nestedRecordOwner(wide_import.type);
	REQUIRE(table.node(short_owner_type).kind == CanonicalTypeKind::TemplateSpecialization);
	REQUIRE(table.node(wide_owner_type).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_owner_type) == primary);
	CHECK(table.templateSpecializationDecl(wide_owner_type) == primary);
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_owner_type)) ==
		table.pointer(table.builtin(CanonicalBuiltinKind::Short)));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(wide_owner_type)) ==
		table.pointer(table.builtin(CanonicalBuiltinKind::LongLong)));
}

TEST_CASE("Canonical integral arguments use converted value and canonical type identity") {
	static_assert(sizeof(CanonicalTypeNode) == 16);
	static_assert(sizeof(CanonicalTemplateArgument) == 20);
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId unsigned_integer = table.builtin(CanonicalBuiltinKind::UnsignedInt);
	const TypeId one = table.integralValue(integer, 1);
	CHECK(one == table.integralValue(integer, 0 + 1));
	CHECK(one != table.integralValue(unsigned_integer, 1));
	CHECK(one != table.integralValue(integer, 2));
	const TypeId wide = table.integralValue(table.builtin(CanonicalBuiltinKind::UnsignedLongLong), 0xfedcba9876543210ULL);
	CHECK(table.node(wide).array_extent == 0xfedcba9876543210ULL);
	CHECK(table.node(one).child == integer);
	CHECK(table.node(one).kind == CanonicalTypeKind::IntegralValue);
	const TypeId enum_type = table.enumeration(EntityId{9181});
	CHECK(one != table.integralValue(enum_type, 1));
	const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(one), CanonicalTemplateArgument::makeType(integer)};
	const TypeId specialization = table.templateSpecialization(TemplateDeclId{9182}, arguments);
	const TypeId link = table.templateSpecializationArguments(specialization);
	CHECK(table.templateArgumentKind(link) == CanonicalTemplateArgKind::Value);
	CHECK_FALSE(table.templateArgumentIsType(link));
	CHECK(table.templateArgumentValue(link) == one);
	CHECK(table.templateArgumentType(table.templateArgumentNext(link)) == integer);
	CHECK_THROWS_AS(table.templateArgumentExpr(link), InternalError);
	CHECK_THROWS_AS(table.templateArgumentType(link), InternalError);
	CHECK_THROWS_AS(table.pointer(one), InternalError);
	CHECK_THROWS_AS(table.qualify(one, CVQualifier::Const), InternalError);
	CHECK_THROWS_AS(table.array(one, 2), InternalError);
	CHECK_THROWS_AS(table.reference(one, ReferenceQualifier::LValueReference), InternalError);
	CHECK_THROWS_AS(table.integralValue(table.builtin(CanonicalBuiltinKind::Bool), 2), InternalError);
	CHECK_THROWS_AS(table.integralValue(table.builtin(CanonicalBuiltinKind::Double), 1), InternalError);
	CHECK_THROWS_AS(table.integralValue(table.templateParameter(TemplateDeclId{9183}, 0), 1), InternalError);
	const CanonicalTemplateArgument invalid_type[] = {CanonicalTemplateArgument::makeType(one)};
	CHECK_THROWS_AS(table.templateSpecialization(TemplateDeclId{9182}, invalid_type), InternalError);
	const CanonicalTemplateArgument invalid_value[] = {CanonicalTemplateArgument::makeValue(integer)};
	CHECK_THROWS_AS(table.templateSpecialization(TemplateDeclId{9182}, invalid_value), InternalError);
}

TEST_CASE("Canonical integral arguments survive substitution and nested alias rebuilding") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TemplateDeclId environment{9191};
	const TemplateDeclId primary{9192};
	const TypeId parameter = table.templateParameter(environment, 0);
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId negative = table.integralValue(integer, static_cast<uint64_t>(-7));
	const CanonicalTemplateArgument pattern_arguments[] = {
		CanonicalTemplateArgument::makeType(parameter), CanonicalTemplateArgument::makeValue(negative),
		CanonicalTemplateArgument::makeNonType(ExprId{91}), CanonicalTemplateArgument::makeTemplate(TemplateDeclId{9193}),
	};
	const CanonicalTemplateArgument concrete_arguments[] = {
		CanonicalTemplateArgument::makeType(integer), CanonicalTemplateArgument::makeValue(negative),
		CanonicalTemplateArgument::makeNonType(ExprId{91}), CanonicalTemplateArgument::makeTemplate(TemplateDeclId{9193}),
	};
	const TypeId pattern = table.templateSpecialization(primary, pattern_arguments);
	const TypeId concrete = table.templateSpecialization(primary, concrete_arguments);
	const TypeId substitutions[] = {integer};
	CHECK(table.substitute(pattern, environment, substitutions) == concrete);
	const TemplateDeclId alias{9194};
	const TypeId alias_parameter = table.templateParameter(alias, 0);
	const CanonicalTemplateArgKind parameter_kinds[] = {CanonicalTemplateArgKind::Type};
	table.publishAliasTemplateTarget(alias, table.pointer(alias_parameter), parameter_kinds);
	const TypeId alias_arguments[] = {integer};
	const TypeId nested_alias = table.aliasTemplateSpecialization(alias, alias_arguments);
	const CanonicalTemplateArgument nested_arguments[] = {
		CanonicalTemplateArgument::makeType(nested_alias), CanonicalTemplateArgument::makeValue(negative),
	};
	const CanonicalTemplateArgument resolved_arguments[] = {
		CanonicalTemplateArgument::makeType(table.pointer(integer)), CanonicalTemplateArgument::makeValue(negative),
	};
	CHECK(table.resolveAliasTemplateSpecializations(table.templateSpecialization(primary, nested_arguments)) ==
		table.templateSpecialization(primary, resolved_arguments));
	TypeId deep_pattern = pattern;
	TypeId deep_concrete = concrete;
	for (size_t depth = 0; depth < 2048; ++depth) {
		const CanonicalTemplateArgument pattern_level[] = {
			CanonicalTemplateArgument::makeType(deep_pattern), CanonicalTemplateArgument::makeValue(negative),
		};
		const CanonicalTemplateArgument concrete_level[] = {
			CanonicalTemplateArgument::makeType(deep_concrete), CanonicalTemplateArgument::makeValue(negative),
		};
		deep_pattern = table.templateSpecialization(primary, pattern_level);
		deep_concrete = table.templateSpecialization(primary, concrete_level);
	}
	CHECK(table.substitute(deep_pattern, environment, substitutions) == deep_concrete);
}

TEST_CASE("Canonical integral argument traces contain value structure instead of arena slots") {
	CanonicalTypeTable first;
	CanonicalTypeTable reordered;
	const TypeId integer = first.builtin(CanonicalBuiltinKind::Int);
	reordered.builtin(CanonicalBuiltinKind::Double);
	reordered.pointer(reordered.builtin(CanonicalBuiltinKind::Float));
	const TypeId reordered_integer = reordered.builtin(CanonicalBuiltinKind::Int);
	const TypeId one = first.integralValue(integer, 1);
	const TypeId reordered_one = reordered.integralValue(reordered_integer, 1);
	const TypeId two = reordered.integralValue(reordered_integer, 2);
	const auto capture = [](CanonicalTypeTable& table, TypeId value) {
		std::ostringstream trace;
		auto* saved_output = FlashCpp::LogConfig::output_stream;
		const auto saved_level = FlashCpp::LogConfig::getLevelForCategory(FlashCpp::LogCategory::Types);
		FlashCpp::LogConfig::setOutputStream(&trace);
		FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, FlashCpp::LogLevel::Trace);
		ScopeGuard restore([&]() {
			FlashCpp::LogConfig::setOutputStream(saved_output);
			FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, saved_level);
		});
		const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(value)};
		table.templateSpecialization(TemplateDeclId{9202}, arguments);
		table.aliasTemplateSpecialization(TemplateDeclId{9203}, arguments);
		return trace.str();
	};
	const std::string trace = capture(first, one);
	CHECK_FALSE(trace.empty());
	CHECK(trace == capture(reordered, reordered_one));
	CHECK(trace != capture(reordered, two));
}

TEST_CASE("Canonical integral values share the canonical transaction and accounting lifetime") {
	FrontendContext frontend;
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const size_t baseline = table.size();
	{
		CanonicalTypeTransaction transaction(table);
		const TypeId value = table.integralValue(integer, 712);
		const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(value)};
		table.templateSpecialization(TemplateDeclId{9201}, arguments);
		CHECK(table.size() > baseline);
	}
	CHECK(table.size() == baseline);
	const TypeId value = table.integralValue(integer, 712);
	CHECK(value == table.integralValue(integer, 712));
	CHECK(table.size() == baseline + 1);
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
	annotated.set_resolved_member_operator_overload(nullptr, TypeId{});
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
	table.publishRecordFieldSchema(table.record(base_entity), std::span<const CanonicalRecordMember>{}, std::span<const CanonicalRecordBase>{});
	table.publishRecordLayout(CanonicalRecordLayout{
		derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
	table.publishRecordFieldSchema(table.record(derived_entity), std::span<const CanonicalRecordMember>{}, derived_bases);
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
		table.publishRecordFieldSchema(table.record(base_entity), {}, {});
		table.publishRecordLayout({derived_entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(related), CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishRecordFieldSchema(table.record(derived_entity), {},
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
		CHECK(table.recordBaseAt(table.record(derived_entity), 0).entity == base_entity);
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
		table.publishRecordFieldSchema(table.record(base_entity), {}, {});
		table.publishRecordLayout(CanonicalRecordLayout{derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishRecordFieldSchema(table.record(derived_entity), {}, bases);
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
		table.publishRecordFieldSchema(table.record(base_entity), {}, {});
		table.publishRecordLayout({derived_entity, 1, 1, 1, 1, 0, static_cast<uint16_t>(related), CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}};
		table.publishRecordFieldSchema(table.record(derived_entity), {},
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
		table.publishRecordFieldSchema(table.record(base_entity), {}, {});
		table.publishRecordLayout(CanonicalRecordLayout{derived_entity, 1, 1, 1, 1, 0, 1, CanonicalRecordLayoutFlags::None, 0});
		const std::array<CanonicalRecordBase, 1> bases{{{base_entity, 0, CanonicalAccess::Public, CanonicalRecordBaseFlags::None, 0, 0}}};
		table.publishRecordFieldSchema(table.record(derived_entity), {}, bases);
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
				annotated.set_resolved_member_operator_overload(result.member_overload, result.declaring_type);
				BinaryOperatorNode copied(Token{}, ASTNode{}, ASTNode{});
				copied.copy_semantic_operator_resolution_from(annotated);
				CHECK(copied.resolved_member_operator_owner() == result.declaring_type);
				copied.set_no_match_operator_overload();
				CHECK_FALSE(copied.resolved_member_operator_owner());
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
	// No field schema is published (as for an unimportable member record); the constructor schema still publishes from the type node.
	CHECK_FALSE(table.hasRecordFieldSchema(table.record(entity)));
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
	FrontendContext context;
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
		table.publishRecordFieldSchema(table.record(entity), std::span<const CanonicalRecordMember>{}, bases);
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
	const TypeInfo* ordinary_type_info = findTypeByName(StringTable::getOrInternStringHandle("OrdinaryDerived"));
	const TypeInfo* root_type_info = findTypeByName(StringTable::getOrInternStringHandle("VirtualRoot"));
	REQUIRE(ordinary_type_info != nullptr);
	REQUIRE(root_type_info != nullptr);
	const std::optional<uint32_t> ordinary_bridge_index =
		tryFindPublishedVirtualBaseIndex(table, *ordinary_type_info, *root_type_info);
	REQUIRE(ordinary_bridge_index.has_value());
	CHECK(*ordinary_bridge_index == *ordinary_index);
	const std::optional<int64_t> ordinary_bridge_offset = tryFindPublishedDerivedBaseOffset(table, *ordinary_type_info, *root_type_info);
	REQUIRE(ordinary_bridge_offset.has_value());
	CHECK(*ordinary_bridge_offset == ordinary_info->virtual_bases[0].offset);

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
	const TypeInfo* nonvirtual_derived_type_info = findTypeByName(StringTable::getOrInternStringHandle("NonVirtualDerived"));
	const TypeInfo* nonvirtual_base_type_info = findTypeByName(StringTable::getOrInternStringHandle("NonVirtualBase"));
	REQUIRE(nonvirtual_derived_type_info != nullptr);
	REQUIRE(nonvirtual_base_type_info != nullptr);
	CHECK_FALSE(tryFindPublishedVirtualBaseIndex(table, *nonvirtual_derived_type_info, *nonvirtual_base_type_info).has_value());
	const std::optional<int64_t> nonvirtual_bridge_offset =
		tryFindPublishedDerivedBaseOffset(table, *nonvirtual_derived_type_info, *nonvirtual_base_type_info);
	REQUIRE(nonvirtual_bridge_offset.has_value());
	CHECK(*nonvirtual_bridge_offset == nonvirtual_info->base_classes[1].offset);

	const TypeId template_base = class_type("TemplateBaseOwner");
	const TypeId template_owner = class_type("TemplateOwner");
	REQUIRE(table.hasVirtualBaseSchema(template_owner));
	REQUIRE(table.virtualBaseCount(template_owner) == 1);
	CHECK(table.virtualBaseAt(template_owner, 0).type == template_base);
	CHECK(table.virtualBaseAt(template_owner, 0).offset_bytes > 0);
	const std::optional<uint32_t> template_index = table.findVirtualBaseIndex(template_owner, template_base);
	REQUIRE(template_index.has_value());
	CHECK(*template_index == 0);
	const TypeInfo* template_owner_type_info = findTypeByName(StringTable::getOrInternStringHandle("TemplateOwner"));
	const TypeInfo* template_base_type_info = findTypeByName(StringTable::getOrInternStringHandle("TemplateBaseOwner"));
	REQUIRE(template_owner_type_info != nullptr);
	REQUIRE(template_base_type_info != nullptr);
	const std::optional<uint32_t> template_bridge_index =
		tryFindPublishedVirtualBaseIndex(table, *template_owner_type_info, *template_base_type_info);
	REQUIRE(template_bridge_index.has_value());
	CHECK(*template_bridge_index == *template_index);
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
		table.publishRecordFieldSchema(table.record(entity), std::span<const CanonicalRecordMember>{}, bases);
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
		table.publishRecordFieldSchema(table.record(entity), std::span<const CanonicalRecordMember>{}, bases);
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

TEST_CASE("Canonical function-template ordering binds typed integral values") {
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TemplateDeclId owner{965};
	const TemplateDeclId environment{966};
	const auto function = [&](CanonicalTemplateArgument argument) {
		const CanonicalTemplateArgument arguments[] = {argument};
		const TypeId parameters[] = {table.templateSpecialization(owner, arguments)};
		return table.function(integer, parameters, false, CVQualifier::None, ReferenceQualifier::None, false,
			CanonicalCallingConvention::Default, CanonicalDllLinkage::None, ExprId{});
	};
	const TypeId one = table.integralValue(integer, 1);
	const TypeId two = table.integralValue(integer, 2);
	const CanonicalFunctionTemplateTypePattern parameter{
		function(CanonicalTemplateArgument::makeNonType(ExprId{967})), std::nullopt, std::nullopt,
		{{ExprId{967}, CanonicalTemplateNonTypeArgumentKind::TemplateParameter, environment, 0, integer}}};
	const CanonicalFunctionTemplateTypePattern first{
		function(CanonicalTemplateArgument::makeValue(one)), std::nullopt, std::nullopt, {}};
	const CanonicalFunctionTemplateTypePattern second{
		function(CanonicalTemplateArgument::makeValue(two)), std::nullopt, std::nullopt, {}};
	const auto deduction = deduceCanonicalFunctionTemplateType(table, parameter, first, environment);
	REQUIRE(deduction.status == CanonicalTemplateDeductionStatus::Match);
	REQUIRE(deduction.non_type_bindings.size() == 1);
	CHECK(deduction.non_type_bindings.front().argument.is_typed_value);
	CHECK(deduction.non_type_bindings.front().argument.typed_value == one);
	CHECK(deduceCanonicalFunctionTemplateType(table, first, first, environment).status == CanonicalTemplateDeductionStatus::Match);
	CHECK(deduceCanonicalFunctionTemplateType(table, first, second, environment).status == CanonicalTemplateDeductionStatus::Mismatch);
	CHECK(deduceCanonicalFunctionTemplateType(table, first, parameter, environment).status == CanonicalTemplateDeductionStatus::Mismatch);
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
		table.publishRecordFieldSchema(table.record(entity), std::span<const CanonicalRecordMember>{}, bases);
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
	FrontendContext context;
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
		if (unexpanded_pack) {
			CHECK(table.hasDependentTypeComponents(deferred.parameter_types().front().canonical_type_id));
		} else {
			CHECK(deferred.parameter_types().front().canonical_type_id == table.builtin(CanonicalBuiltinKind::Short));
		}
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

TEST_CASE("Integral NTTP syntax and declaration imports share converted identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<unsigned I> struct Cell { short value; };
struct Padding { long long guard; };
template<unsigned I> struct Next : Padding, Cell<I + 1> {};
template<auto Value> struct Deduced {};
template<int Value> struct Fixed {};
template<bool Value> struct Flag {};
template<unsigned Value = 1> struct Defaulted {};
enum class Code : unsigned short { chosen = 7 };
template<Code Value> struct EnumFixed {};
using Spelled = Cell<1u>;
using Derived = Next<0u>;
using Signed = Deduced<1>;
using Unsigned = Deduced<1u>;
using FixedSigned = Fixed<1>;
using FixedUnsigned = Fixed<1u>;
using Boolean = Flag<true>;
using Default = Defaulted<>;
using ExplicitDefault = Defaulted<1u>;
using Enumeration = EnumFixed<Code::chosen>;
int main() { Derived object; object.value = 41; return object.value; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("integral_nttp_import.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId spelled = import_alias("Spelled");
	const TypeId derived = import_alias("Derived");
	const TypeId link = table.templateSpecializationArguments(spelled);
	REQUIRE(table.templateArgumentKind(link) == CanonicalTemplateArgKind::Value);
	const TypeId value = table.templateArgumentValue(link);
	CHECK(table.node(value).child == table.builtin(CanonicalBuiltinKind::UnsignedInt));
	CHECK(table.node(value).array_extent == 1);
	CHECK(import_alias("Signed") != import_alias("Unsigned"));
	CHECK(import_alias("FixedSigned") == import_alias("FixedUnsigned"));
	CHECK(import_alias("Default") == import_alias("ExplicitDefault"));
	const TypeId boolean = table.templateArgumentValue(table.templateSpecializationArguments(import_alias("Boolean")));
	CHECK(table.node(boolean).child == table.builtin(CanonicalBuiltinKind::Bool));
	CHECK(table.node(boolean).array_extent == 1);
	const TypeId enumeration = table.templateArgumentValue(table.templateSpecializationArguments(import_alias("Enumeration")));
	CHECK(table.node(table.node(enumeration).child).kind == CanonicalTypeKind::Enum);
	CHECK(table.node(enumeration).array_extent == 7);
	const TypeInfo* derived_alias = findTypeByName(StringTable::getOrInternStringHandle("Derived"));
	REQUIRE(derived_alias != nullptr);
	const TypeInfo* derived_info = resolveAliasTypeInfo(derived_alias->type_index_).terminal_type_info;
	REQUIRE(derived_info != nullptr);
	const StructTypeInfo* record = derived_info->getStructInfo();
	REQUIRE(record != nullptr);
	REQUIRE(record->declaration_node != nullptr);
	const CanonicalTypeImport declaration = importCanonicalClassDeclaration(table, *record->declaration_node);
	REQUIRE(declaration.status == CanonicalTypeImportStatus::Supported);
	CHECK(declaration.type == derived);
	CHECK(tryFindPublishedDerivedBaseOffset(table, derived, spelled) == 8);
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

TEST_CASE("Canonical nested record identity includes its specialized enclosing class") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream input("tests/test_canonical_nested_class_specialization_identity_ret0.cpp");
	REQUIRE(input.is_open());
	const std::string code((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
	CompileContext compile_context;
	compile_context.setInputFile("tests/test_canonical_nested_class_specialization_identity_ret0.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		INFO("alias: " << name);
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const TypeSpecifierNode syntax(info->registeredTypeIndex(), 0, Token{}, CVQualifier::None, ReferenceQualifier::None);
		const CanonicalTypeImport imported = importCanonicalTypeWithClassFallback(table, syntax);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	CHECK(table.node(import_alias("PlainKey")).kind == CanonicalTypeKind::Record);
	const TypeId short_key = import_alias("ShortKey");
	const TypeId wide_key = import_alias("WideKey");
	const TypeId short_again = import_alias("ShortAgain");
	const TypeId short_chain = import_alias("ShortChain");
	const TypeId short_pointer = import_alias("ShortPointer");
	const TypeId other_key = import_alias("OtherKey");
	const TypeId foreign_key = import_alias("ForeignKey");
	CHECK(short_again == short_key);
	CHECK(short_chain == short_key);
	CHECK(short_pointer == table.pointer(short_key));
	CHECK(short_key != wide_key);
	CHECK(short_key != other_key);
	CHECK(short_key != foreign_key);
	for (const std::string_view name : {"ShortKey", "WideKey", "ShortAgain", "ShortChain", "OtherKey", "ForeignKey"}) {
		const TypeInfo* alias = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(alias != nullptr);
		const TypeInfo* terminal = resolveAliasTypeInfo(alias->registeredTypeIndex()).terminal_type_info;
		REQUIRE(terminal != nullptr);
		const StructTypeInfo* nested = canonicalClassStructInfoFromTypeInfo(*terminal);
		REQUIRE(nested != nullptr);
		REQUIRE(nested->declaration_node != nullptr);
		const StructDeclarationNode* pattern = nested->declaration_node->injected_class_pattern_declaration();
		REQUIRE(pattern != nullptr);
		REQUIRE(pattern->has_entity_id());
		const CanonicalTypeImport declaration = importCanonicalClassDeclaration(table, *nested->declaration_node);
		REQUIRE(declaration.status == CanonicalTypeImportStatus::Supported);
		CHECK(declaration.type == import_alias(name));
		CHECK(table.node(declaration.type).kind == CanonicalTypeKind::NestedRecord);
		CHECK(table.nestedRecordEntity(declaration.type) == pattern->entity_id());
		REQUIRE(nested->declaration_node->enclosing_class() != nullptr);
		const CanonicalTypeImport owner = importCanonicalClassDeclaration(table, *nested->declaration_node->enclosing_class());
		REQUIRE(owner.status == CanonicalTypeImportStatus::Supported);
		CHECK(table.node(owner.type).kind == CanonicalTypeKind::TemplateSpecialization);
		CHECK(table.nestedRecordOwner(declaration.type) == owner.type);
	}
}

TEST_CASE("Member class-template import composes owner and member arguments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Owner {
	template<class U> struct Box { U value; };
};
using ShortInt = Owner<short>::Box<int>;
using ShortIntAgain = Owner<short>::Box<int>;
using ShortLong = Owner<short>::Box<long long>;
using WideInt = Owner<long long>::Box<int>;
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("member_template_composed_identity.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto template_decl = [](std::string_view name) {
		const auto entry = gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle(name));
		REQUIRE(entry.has_value());
		REQUIRE(entry->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& node = entry->as<TemplateClassDeclarationNode>();
		REQUIRE(node.has_template_decl_id());
		return node.template_decl_id();
	};
	const TemplateDeclId owner_decl = template_decl("Owner");
	const TemplateDeclId member_decl = template_decl("Owner::Box");
	const TypeId short_int = import_alias("ShortInt");
	const TypeId short_int_again = import_alias("ShortIntAgain");
	const TypeId short_long = import_alias("ShortLong");
	const TypeId wide_int = import_alias("WideInt");
	CHECK(short_int == short_int_again);
	CHECK(short_int != short_long);
	CHECK(short_int != wide_int);
	REQUIRE(table.node(short_int).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_int) == member_decl);
	const TypeId short_owner = table.templateSpecializationOwner(short_int);
	const TypeId wide_owner = table.templateSpecializationOwner(wide_int);
	REQUIRE(short_owner);
	REQUIRE(wide_owner);
	CHECK(short_owner != wide_owner);
	CHECK(table.node(short_owner).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_owner) == owner_decl);
	CHECK(table.templateSpecializationOwner(short_owner) == TypeId{});
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_int)) == table.builtin(CanonicalBuiltinKind::Int));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_long)) == table.builtin(CanonicalBuiltinKind::LongLong));
}

TEST_CASE("Nested member class-template import composes the owner chain") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Owner {
	template<class U> struct Box {
		template<class V> struct Inner { V value; };
	};
};
using ShortIntChar = Owner<short>::Box<int>::Inner<char>;
using ShortIntCharAgain = Owner<short>::Box<int>::Inner<char>;
using ShortIntLong = Owner<short>::Box<int>::Inner<long long>;
using ShortLongChar = Owner<short>::Box<long long>::Inner<char>;
using WideIntChar = Owner<long long>::Box<int>::Inner<char>;
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("nested_member_template_composed_identity.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto template_decl = [](std::string_view name) {
		const auto entry = gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle(name));
		REQUIRE(entry.has_value());
		REQUIRE(entry->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& node = entry->as<TemplateClassDeclarationNode>();
		REQUIRE(node.has_template_decl_id());
		return node.template_decl_id();
	};
	const TemplateDeclId owner_decl = template_decl("Owner");
	const TemplateDeclId box_decl = template_decl("Owner::Box");
	const TemplateDeclId inner_decl = template_decl("Owner::Box::Inner");
	const TypeId short_int_char = import_alias("ShortIntChar");
	const TypeId short_int_char_again = import_alias("ShortIntCharAgain");
	const TypeId short_int_long = import_alias("ShortIntLong");
	const TypeId short_long_char = import_alias("ShortLongChar");
	const TypeId wide_int_char = import_alias("WideIntChar");
	CHECK(short_int_char == short_int_char_again);
	CHECK(short_int_char != short_int_long);
	CHECK(short_int_char != short_long_char);
	CHECK(short_int_char != wide_int_char);
	REQUIRE(table.node(short_int_char).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_int_char) == inner_decl);
	const TypeId box = table.templateSpecializationOwner(short_int_char);
	const TypeId short_long_box = table.templateSpecializationOwner(short_long_char);
	const TypeId wide_box = table.templateSpecializationOwner(wide_int_char);
	REQUIRE(box);
	REQUIRE(short_long_box);
	REQUIRE(wide_box);
	CHECK(box != short_long_box);
	CHECK(box != wide_box);
	REQUIRE(table.node(box).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(box) == box_decl);
	const TypeId owner = table.templateSpecializationOwner(box);
	const TypeId short_long_owner = table.templateSpecializationOwner(short_long_box);
	const TypeId wide_owner = table.templateSpecializationOwner(wide_box);
	REQUIRE(owner);
	REQUIRE(short_long_owner);
	REQUIRE(wide_owner);
	CHECK(owner == short_long_owner);
	CHECK(owner != wide_owner);
	CHECK(table.templateSpecializationDecl(owner) == owner_decl);
	CHECK(table.templateSpecializationOwner(owner) == TypeId{});
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_int_char)) == table.builtin(CanonicalBuiltinKind::Char));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_int_long)) == table.builtin(CanonicalBuiltinKind::LongLong));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(box)) == table.builtin(CanonicalBuiltinKind::Int));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_long_box)) == table.builtin(CanonicalBuiltinKind::LongLong));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(owner)) == table.builtin(CanonicalBuiltinKind::Short));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(wide_owner)) == table.builtin(CanonicalBuiltinKind::LongLong));
}

TEST_CASE("Selected member class-template partials compose their owner") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Owner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> { U* pointer; int tag; };
};
using ShortPointer = Owner<short>::Box<int*>;
using ShortPointerAgain = Owner<short>::Box<int*>;
using WidePointer = Owner<long long>::Box<int*>;
using ShortCharPointer = Owner<short>::Box<char*>;
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("member_partial_owner_identity.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto template_decl = [](std::string_view name) {
		const auto entry = gTemplateRegistry.lookupTemplate(StringTable::getOrInternStringHandle(name));
		REQUIRE(entry.has_value());
		REQUIRE(entry->is<TemplateClassDeclarationNode>());
		const TemplateClassDeclarationNode& node = entry->as<TemplateClassDeclarationNode>();
		REQUIRE(node.has_template_decl_id());
		return node.template_decl_id();
	};
	const TemplateDeclId owner_decl = template_decl("Owner");
	const TemplateDeclId box_decl = template_decl("Owner::Box");
	const TypeId short_pointer = import_alias("ShortPointer");
	const TypeId short_pointer_again = import_alias("ShortPointerAgain");
	const TypeId wide_pointer = import_alias("WidePointer");
	const TypeId short_char_pointer = import_alias("ShortCharPointer");
	CHECK(short_pointer == short_pointer_again);
	CHECK(short_pointer != wide_pointer);
	CHECK(short_pointer != short_char_pointer);
	REQUIRE(table.node(short_pointer).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_pointer) == box_decl);
	const TypeId short_owner = table.templateSpecializationOwner(short_pointer);
	const TypeId wide_owner = table.templateSpecializationOwner(wide_pointer);
	REQUIRE(short_owner);
	REQUIRE(wide_owner);
	CHECK(short_owner != wide_owner);
	REQUIRE(table.node(short_owner).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(short_owner) == owner_decl);
	CHECK(table.templateSpecializationOwner(short_owner) == TypeId{});
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_pointer)) ==
		table.pointer(table.builtin(CanonicalBuiltinKind::Int)));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_char_pointer)) ==
		table.pointer(table.builtin(CanonicalBuiltinKind::Char)));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(short_owner)) ==
		table.builtin(CanonicalBuiltinKind::Short));
	CHECK(table.templateArgumentType(table.templateSpecializationArguments(wide_owner)) ==
		table.builtin(CanonicalBuiltinKind::LongLong));
}

TEST_CASE("Selected member partial methods publish concrete outer signatures") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext frontend;
	std::ifstream fixture("tests/test_canonical_member_partial_outer_signature_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append("\nusing NarrowSignature = SignatureOwner<short>::Box<int*>;\nusing WideSignature = SignatureOwner<long long>::Box<int*>;\n");
	CompileContext compile_context;
	compile_context.setInputFile("member_partial_outer_signature.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = frontend.canonicalTypes();
	const auto record_for_alias = [](std::string_view name) {
		const TypeInfo* alias = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(alias != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(alias->registeredTypeIndex());
		REQUIRE(resolved.terminal_type_info != nullptr);
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		return record;
	};
	const auto check_function = [&](const StructTypeInfo& record, std::string_view name, TypeId expected_outer, TypeId expected_parameter) {
		const StructMemberFunction* selected = nullptr;
		for (const StructMemberFunction& member : record.member_functions) {
			if (StringTable::getStringView(member.name) != name || !member.function_decl.is<FunctionDeclarationNode>()) continue;
			selected = &member;
			break;
		}
		REQUIRE(selected != nullptr);
		const FunctionDeclarationNode& function = selected->function_decl.as<FunctionDeclarationNode>();
		const auto result_id = tryImportSupportedCanonical(table, function.decl_node().type_specifier_node());
		REQUIRE(result_id.has_value());
		CHECK(*result_id == expected_outer);
		REQUIRE(!function.parameter_nodes().empty());
		const auto parameter_id = tryImportSupportedCanonical(table, function.parameter_nodes()[0].as<DeclarationNode>().type_specifier_node());
		REQUIRE(parameter_id.has_value());
		CHECK(*parameter_id == expected_parameter);
	};
	const StructTypeInfo* narrow = record_for_alias("NarrowSignature");
	const StructTypeInfo* wide = record_for_alias("WideSignature");
	check_function(*narrow, "echo", table.builtin(CanonicalBuiltinKind::Short), table.builtin(CanonicalBuiltinKind::Short));
	check_function(*narrow, "declared", table.builtin(CanonicalBuiltinKind::Short), table.builtin(CanonicalBuiltinKind::Short));
	check_function(*narrow, "mix", table.builtin(CanonicalBuiltinKind::Short), table.builtin(CanonicalBuiltinKind::Int));
	check_function(*wide, "echo", table.builtin(CanonicalBuiltinKind::LongLong), table.builtin(CanonicalBuiltinKind::LongLong));
	check_function(*wide, "declared", table.builtin(CanonicalBuiltinKind::LongLong), table.builtin(CanonicalBuiltinKind::LongLong));
	check_function(*wide, "mix", table.builtin(CanonicalBuiltinKind::LongLong), table.builtin(CanonicalBuiltinKind::Int));
}

TEST_CASE("Member class-template partial patterns publish canonical environments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Owner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> { U* pointer; int tag; };
};
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("member_partial_environment.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto box_entry = gTemplateRegistry.lookupTemplate("Owner::Box");
	REQUIRE(box_entry.has_value());
	REQUIRE(box_entry->is<TemplateClassDeclarationNode>());
	const TemplateDeclId box_primary = box_entry->as<TemplateClassDeclarationNode>().template_decl_id();
	REQUIRE(box_primary);
	TypeSpecifierNode int_pointer(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	int_pointer.add_pointer_level(CVQualifier::None);
	const TemplateTypeArg concrete_argument(int_pointer);
	const auto pattern_match = gTemplateRegistry.matchSpecializationPatternWithBindings("Owner::Box",
		std::span<const TemplateTypeArg>(&concrete_argument, 1));
	REQUIRE(pattern_match.has_value());
	REQUIRE(pattern_match->node.is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = pattern_match->node.as<TemplateClassDeclarationNode>().class_decl_node();
	const TemplateDeclId environment = pattern.partial_specialization_environment();
	REQUIRE(environment);
	CHECK(environment != box_primary);
	CHECK_FALSE(pattern.has_template_decl_id());
	REQUIRE(pattern.members().size() == 2);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	CHECK(field.has_template_parameter_decl());
	CHECK(field.template_decl_id() == environment);
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId imported_field = importCanonicalStructMemberType(table, field);
	REQUIRE(imported_field);
	CHECK(table.hasDependentTypeComponents(imported_field));
	CHECK(table.dependsOnlyOnTemplateParameters(imported_field, environment));
	const TypeId deduced_argument = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId concrete_arguments[] = {deduced_argument};
	CHECK(table.substitute(imported_field, environment, concrete_arguments) == table.pointer(deduced_argument));
	const TypeId expected_pattern = table.templateSpecialization(box_primary,
		std::array<TypeId, 1>{table.pointer(table.templateParameter(box_primary, 0))});
	const size_t declarations_before = context.templateDecls().size();
	CHECK(context.templateDecls().publishClassPartialEnvironment(box_primary, expected_pattern) == environment);
	CHECK(context.templateDecls().size() == declarations_before);
}

TEST_CASE("Concept-constrained member class-template partials publish environments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> concept Incrementable = requires(T value) { ++value; };
template<class T> struct Owner {
	template<class V> struct Box { V value; };
	template<Incrementable V> struct Box<V> { V* pointer; };
};
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("constrained_member_partial_environment.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto box_entry = gTemplateRegistry.lookupTemplate("Owner::Box");
	REQUIRE(box_entry.has_value());
	REQUIRE(box_entry->is<TemplateClassDeclarationNode>());
	const TemplateDeclId box_primary = box_entry->as<TemplateClassDeclarationNode>().template_decl_id();
	REQUIRE(box_primary);
	const TypeSpecifierNode int_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const TemplateTypeArg concrete_argument(int_type);
	const auto pattern_match = gTemplateRegistry.matchSpecializationPatternWithBindings(
		"Owner::Box", std::span<const TemplateTypeArg>(&concrete_argument, 1));
	REQUIRE(pattern_match.has_value());
	REQUIRE(pattern_match->node.is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = pattern_match->node.as<TemplateClassDeclarationNode>().class_decl_node();
	const TemplateDeclId environment = pattern.partial_specialization_environment();
	REQUIRE(environment);
	CHECK(environment != box_primary);
	REQUIRE(pattern.members().size() == 1);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	CHECK(field.has_template_parameter_decl());
	CHECK(field.template_decl_id() == environment);
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId imported_field = importCanonicalStructMemberType(table, field);
	REQUIRE(imported_field);
	CHECK(table.dependsOnlyOnTemplateParameters(imported_field, environment));
	const TypeId concrete_type = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId concrete_arguments[] = {concrete_type};
	CHECK(table.substitute(imported_field, environment, concrete_arguments) == table.pointer(concrete_type));
}

TEST_CASE("Non-type literal member class-template partials publish environments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Owner {
	template<class T, bool B> struct Select { T value; };
	template<class T> struct Select<T, true> { T* pointer; };
};
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("non_type_member_partial_environment.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto select_entry = gTemplateRegistry.lookupTemplate("Owner::Select");
	REQUIRE(select_entry.has_value());
	REQUIRE(select_entry->is<TemplateClassDeclarationNode>());
	const TemplateDeclId select_primary = select_entry->as<TemplateClassDeclarationNode>().template_decl_id();
	REQUIRE(select_primary);
	const TypeSpecifierNode int_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const TemplateTypeArg arguments[] = {TemplateTypeArg(int_type), TemplateTypeArg(1, TypeCategory::Bool)};
	const auto pattern_match = gTemplateRegistry.matchSpecializationPatternWithBindings(
		"Owner::Select", std::span<const TemplateTypeArg>(arguments, 2));
	REQUIRE(pattern_match.has_value());
	REQUIRE(pattern_match->node.is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = pattern_match->node.as<TemplateClassDeclarationNode>().class_decl_node();
	const TemplateDeclId environment = pattern.partial_specialization_environment();
	REQUIRE(environment);
	CHECK(environment != select_primary);
	REQUIRE(pattern.members().size() == 1);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	CHECK(field.has_template_parameter_decl());
	CHECK(field.template_decl_id() == environment);
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId imported_field = importCanonicalStructMemberType(table, field);
	REQUIRE(imported_field);
	CHECK(table.dependsOnlyOnTemplateParameters(imported_field, environment));
	const TypeId concrete_type = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId concrete_arguments[] = {concrete_type};
	CHECK(table.substitute(imported_field, environment, concrete_arguments) == table.pointer(concrete_type));
}

TEST_CASE("Pack member class-template partials publish environments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Owner {
	template<class... T> struct List {};
	template<class Head, class... Tail> struct List<Head, Tail...> { Head* pointer; };
};
int main() { return 0; }
)";
	CompileContext compile_context;
	compile_context.setInputFile("pack_member_partial_environment.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto list_entry = gTemplateRegistry.lookupTemplate("Owner::List");
	REQUIRE(list_entry.has_value());
	REQUIRE(list_entry->is<TemplateClassDeclarationNode>());
	const TemplateDeclId list_primary = list_entry->as<TemplateClassDeclarationNode>().template_decl_id();
	REQUIRE(list_primary);
	const TypeSpecifierNode int_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const TypeSpecifierNode char_type(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	const TypeSpecifierNode long_type(TypeCategory::Long, TypeQualifier::None, 64, Token{}, CVQualifier::None);
	const TemplateTypeArg arguments[] = {TemplateTypeArg(int_type), TemplateTypeArg(char_type), TemplateTypeArg(long_type)};
	const auto pattern_match = gTemplateRegistry.matchSpecializationPatternWithBindings(
		"Owner::List", std::span<const TemplateTypeArg>(arguments, 3));
	REQUIRE(pattern_match.has_value());
	REQUIRE(pattern_match->node.is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = pattern_match->node.as<TemplateClassDeclarationNode>().class_decl_node();
	const TemplateDeclId environment = pattern.partial_specialization_environment();
	REQUIRE(environment);
	CHECK(environment != list_primary);
	REQUIRE(pattern.members().size() == 1);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	CHECK(field.has_template_parameter_decl());
	CHECK(field.template_decl_id() == environment);
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId imported_field = importCanonicalStructMemberType(table, field);
	REQUIRE(imported_field);
	CHECK(table.dependsOnlyOnTemplateParameters(imported_field, environment));
	const TypeId concrete_arguments[] = {
		table.builtin(CanonicalBuiltinKind::Int), table.builtin(CanonicalBuiltinKind::Char),
		table.builtin(CanonicalBuiltinKind::Long)};
	CHECK(table.substitute(imported_field, environment, concrete_arguments) == table.pointer(table.builtin(CanonicalBuiltinKind::Int)));
}

TEST_CASE("Data member template-id pack expansions preserve canonical argument order") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_member_partial_type_pack_fields_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using Empty = PackOwner<short>::Box<int>;
using Single = PackOwner<short>::Box<int, char>;
using Multiple = PackOwner<long long>::Box<int, char, double>;
using SameArguments = PackOwner<short>::Box<int, char, double>;
using Reordered = PackOwner<short>::Box<int, double, char>;
using Small = short;
using Equivalent = PackOwner<Small>::Box<int, char, double>;
using Structured = PackOwner<short>::Box<int, PackPayload>;
using ExpectedEmpty = TypeList<short, int>;
using ExpectedSingle = TypeList<short, int, char>;
using ExpectedMultiple = TypeList<long long, int, char, double>;
using ExpectedSame = TypeList<short, int, char, double>;
using ExpectedReordered = TypeList<short, int, double, char>;
using ExpectedStructured = TypeList<short, int, PackPayload>;
using StructuredPointers = TypeList<const PackPayload*>;
using EmptyPointers = TypeList<>;
using SinglePointers = TypeList<const char*>;
using MultiplePointers = TypeList<const char*, const double*>;
using ReorderedPointers = TypeList<const double*, const char*>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("canonical_member_partial_type_pack_fields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		INFO("canonical pack alias: " << name);
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	CHECK(imported_alias("Multiple") != imported_alias("SameArguments"));
	CHECK(imported_alias("Equivalent") == imported_alias("SameArguments"));
	const std::string_view names[] = {"Empty", "Single", "Multiple", "SameArguments", "Reordered", "Equivalent", "Structured"};
	const std::string_view values[] = {
		"ExpectedEmpty", "ExpectedSingle", "ExpectedMultiple", "ExpectedSame", "ExpectedReordered", "ExpectedSame", "ExpectedStructured"};
	const size_t member_argument_counts[] = {1, 2, 3, 3, 3, 3, 2};
	const std::string_view pointers[] = {
		"EmptyPointers", "SinglePointers", "MultiplePointers", "MultiplePointers", "ReorderedPointers", "MultiplePointers", "StructuredPointers"};
	for (size_t index = 0; index < std::size(names); ++index) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(names[index]));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->declaration_node->members().size() == 2);
		const TypeId member_type = imported_alias(names[index]);
		const TypeId owner = table.templateSpecializationOwner(member_type);
		REQUIRE(owner);
		const TypeId owner_arguments = table.templateSpecializationArguments(owner);
		REQUIRE(owner_arguments);
		CHECK(table.node(owner_arguments).child == TypeId{});
		CHECK(table.templateArgumentType(owner_arguments) == table.builtin(index == 2 ? CanonicalBuiltinKind::LongLong : CanonicalBuiltinKind::Short));
		size_t argument_count = 0;
		for (TypeId argument = table.templateSpecializationArguments(member_type); argument; argument = table.node(argument).child) ++argument_count;
		CHECK(argument_count == member_argument_counts[index]);
		REQUIRE(table.hasRecordSubobjectTypes(member_type));
		const TypeId expected[] = {imported_alias(values[index]), imported_alias(pointers[index])};
		for (size_t member_index = 0; member_index < 2; ++member_index) {
			CHECK(record->members[member_index].canonical_type_id == expected[member_index]);
			CHECK(table.recordSubobjectMemberAt(member_type, member_index) == expected[member_index]);
			const auto& member = record->declaration_node->members()[member_index].declaration.as<DeclarationNode>();
			CHECK(importCanonicalStructMemberType(table, member.type_specifier_node()) == expected[member_index]);
		}
	}
}

TEST_CASE("Dependent member pack elements stay deferred during substitution") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class T> struct Owner { template<class... U> struct List {}; };
template<class T, class... U> struct Holder { typename Owner<T>::template List<const U*...> field; };
)";
	CompileContext compile_context;
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto holder = gTemplateRegistry.lookupTemplate("Holder");
	REQUIRE(holder.has_value());
	REQUIRE(holder->is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = holder->as<TemplateClassDeclarationNode>().class_decl_node();
	REQUIRE(pattern.members().size() == 1);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	const TypeInfo* dependent_field = tryGetTypeInfo(field.type_index());
	REQUIRE(dependent_field != nullptr);
	REQUIRE(dependent_field->isDependentMemberType());
	TemplateArgSubstitutionMap scalar_args;
	TemplateTypeArg owner_arg;
	owner_arg.type_index = nativeTypeIndex(TypeCategory::Short);
	scalar_args["T"] = owner_arg;
	TemplateArgPackSubstitutionMap pack_args;
	TemplateTypeArg dependent_arg;
	dependent_arg.type_index = nativeTypeIndex(TypeCategory::Int);
	dependent_arg.dependent_name = StringTable::getOrInternStringHandle("StillDependent");
	dependent_arg.is_dependent = true;
	pack_args[StringTable::getOrInternStringHandle("U")].push_back(dependent_arg);
	const std::string_view order[] = {"T", "U"};
	ExpressionSubstitutor substitutor(scalar_args, pack_args, parser, order);
	const TypeSpecifierNode substituted = substitutor.substituteTypeSpecifier(field);
	const TypeInfo* result = tryGetTypeInfo(substituted.type_index());
	REQUIRE(result != nullptr);
	CHECK(result->isDependentMemberType());
}

TEST_CASE("Scoped outer template bindings restore nested lookups") {
	TemplateRegistry registry;
	const StringHandle name = StringTable::getOrInternStringHandle("nested_member_template");
	const StringHandle outer = StringTable::getOrInternStringHandle("Outer");
	const StringHandle inner = StringTable::getOrInternStringHandle("Inner");
	OuterTemplateBinding initial;
	initial.param_names.push_back(outer);
	registry.registerOuterTemplateBinding(name, std::move(initial));
	{
		TemplateRegistry::ScopedOuterTemplateBinding first(registry, name, true);
		CHECK(first.binding().param_names.empty());
		first.binding().param_names.push_back(inner);
		{
			TemplateRegistry::ScopedOuterTemplateBinding second(registry, name, true);
			CHECK(second.binding().param_names.empty());
			second.binding().param_names.push_back(outer);
		}
		REQUIRE(registry.getOuterTemplateBinding(name) != nullptr);
		CHECK(registry.getOuterTemplateBinding(name)->param_names[0] == inner);
	}
	REQUIRE(registry.getOuterTemplateBinding(name) != nullptr);
	CHECK(registry.getOuterTemplateBinding(name)->param_names[0] == outer);
	const StringHandle absent = StringTable::getOrInternStringHandle("unbound_member_template");
	try {
		TemplateRegistry::ScopedOuterTemplateBinding scope(registry, absent, true);
		throw 1;
	} catch (int) {}
	CHECK(registry.getOuterTemplateBinding(absent) == nullptr);
}

TEST_CASE("Expanded member template fields retain their instantiated owner") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_member_pack_instantiated_owner_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using ShortOwner = Owner<short>;
using LongOwner = Owner<long long>;
using ShortBox = Holder<short>::Box<int, char, Payload>;
using LongBox = Holder<long long>::Box<int, char, Payload>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("canonical_member_pack_instantiated_owner.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 2);
		const TypeId type = record->members[0].canonical_type_id;
		REQUIRE(type);
		CHECK(table.recordSubobjectMemberAt(imported_alias(name), 0) == type);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		CHECK(importCanonicalStructMemberType(table, syntax) == type);
		return type;
	};
	const TypeId short_field = field_type("ShortBox");
	const TypeId long_field = field_type("LongBox");
	CHECK(short_field != long_field);
	CHECK(table.templateSpecializationOwner(short_field) == imported_alias("ShortOwner"));
	CHECK(table.templateSpecializationOwner(long_field) == imported_alias("LongOwner"));
	CHECK(table.templateSpecializationArguments(short_field) == table.templateSpecializationArguments(long_field));
}

TEST_CASE("Nontrivial member pack fields retain wrapped elements and owners") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_member_nontrivial_type_pack_fields_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using ShortOwner = Owner<short>;
using LongOwner = Owner<long long>;
using ShortBox = Holder<short>::Box<int, char, Payload>;
using LongBox = Holder<long long>::Box<int, char, Payload>;
using ExpectedShort = Owner<short>::List<int, Wrap<char>, Wrap<Payload>>;
using FlattenedShort = Owner<short>::List<int, char, Payload>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("canonical_member_nontrivial_type_pack.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 4);
		const TypeId type = record->members[0].canonical_type_id;
		REQUIRE(type);
		CHECK(table.recordSubobjectMemberAt(imported_alias(name), 0) == type);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		CHECK(importCanonicalStructMemberType(table, syntax) == type);
		return type;
	};
	const TypeId short_field = field_type("ShortBox");
	const TypeId long_field = field_type("LongBox");
	CHECK(short_field != long_field);
	CHECK(table.templateSpecializationOwner(short_field) == imported_alias("ShortOwner"));
	CHECK(table.templateSpecializationOwner(long_field) == imported_alias("LongOwner"));
	CHECK(table.templateSpecializationArguments(short_field) == table.templateSpecializationArguments(imported_alias("ExpectedShort")));
	CHECK(table.templateSpecializationArguments(short_field) != table.templateSpecializationArguments(imported_alias("FlattenedShort")));
}

TEST_CASE("Interned template arguments preserve builtin type-index projections") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	TemplateArgInfoVector category_arguments;
	TypeInfo::TemplateArgInfo first;
	first.type_index = TypeIndex{0, TypeCategory::Double};
	category_arguments.push_back(first);
	TypeInfo::TemplateArgInfo second;
	second.type_index = TypeIndex{0, TypeCategory::LongLong};
	category_arguments.push_back(second);
	const uint32_t category_list = storeTypeInfoTemplateArgs(category_arguments);
	TemplateArgInfoVector indexed_arguments = category_arguments;
	indexed_arguments[0].type_index = nativeTypeIndex(TypeCategory::Double);
	indexed_arguments[1].type_index = nativeTypeIndex(TypeCategory::LongLong);
	const uint32_t indexed_list = storeTypeInfoTemplateArgs(indexed_arguments);
	CHECK(category_list != indexed_list);
	CHECK(getTypeInfoTemplateArgs(category_list)[0].type_index == first.type_index);
	CHECK(getTypeInfoTemplateArgs(indexed_list)[0].type_index == nativeTypeIndex(TypeCategory::Double));
	CHECK(getTypeInfoTemplateArgs(indexed_list)[1].type_index == nativeTypeIndex(TypeCategory::LongLong));
	CHECK(storeTypeInfoTemplateArgs(indexed_arguments) == indexed_list);
}

TEST_CASE("Enclosing owner and nested member packs preserve canonical lockstep fields") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_enclosing_owner_pack_lockstep_member_field_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using ShortTarget = PackTarget<short>;
using LongTarget = PackTarget<long long>;
using EmptyOwner = PackOwner<short>;
using EmptyBox = PackOwner<short>::Box<>;
using SingleOwner = PackOwner<short, PackPayload>;
using SingleBox = PackOwner<short, PackPayload>::Box<double>;
using MixedOwner = PackOwner<long long, short, char, PackPayload>;
using MixedBox = PackOwner<long long, short, char, PackPayload>::Box<int, long long, double>;
using SameInnerOwner = PackOwner<short, long long, double, unsigned char>;
using SameInnerBox = PackOwner<short, long long, double, unsigned char>::Box<int, long long, double>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("enclosing_owner_lockstep_member_fields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto pair_type = [&](TemplateTypeArg first, TemplateTypeArg second) {
		const TemplateArgumentVector arguments{first, second};
		const auto instance = template_engine.materializeTemplateInstantiationForLookup("PackPair", arguments);
		REQUIRE(instance.resolved_type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *instance.resolved_type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 2);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[1].declaration.as<DeclarationNode>().type_specifier_node();
		const TypeId field = importCanonicalStructMemberType(table, syntax);
		REQUIRE(field);
		CHECK(record->members[1].canonical_type_id == field);
		return field;
	};
	const auto check_box = [&](std::string_view box_name, std::string_view owner_name, std::string_view target_name,
		std::span<const TypeId> inner_types, std::span<const TypeId> expected_pairs) {
		const TypeId box = imported_alias(box_name);
		CHECK(table.templateSpecializationOwner(box) == imported_alias(owner_name));
		TypeId inner = table.templateSpecializationArguments(box);
		for (TypeId expected : inner_types) {
			REQUIRE(inner);
			CHECK(table.templateArgumentType(inner) == expected);
			inner = table.templateArgumentNext(inner);
		}
		CHECK_FALSE(inner);
		const TypeId field = field_type(box_name);
		CHECK(table.templateSpecializationOwner(field) == imported_alias(target_name));
		TypeId argument = table.templateSpecializationArguments(field);
		for (TypeId expected : expected_pairs) {
			REQUIRE(argument);
			CHECK(table.templateArgumentType(argument) == expected);
			argument = table.templateArgumentNext(argument);
		}
		CHECK_FALSE(argument);
	};
	const TypeInfo* payload_info = findTypeByName(StringTable::getOrInternStringHandle("PackPayload"));
	REQUIRE(payload_info != nullptr);
	const TemplateTypeArg payload_arg = TemplateTypeArg::makeType(payload_info->registeredTypeIndex());
	const TypeId pair_payload_double = pair_type(payload_arg, TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double)));
	const TypeId pair_short_int = pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Short)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int)));
	const TypeId pair_char_long = pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::LongLong)));
	const TypeId pair_payload_double_mixed = pair_type(payload_arg, TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double)));
	const TypeId pair_long_int = pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::LongLong)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int)));
	const TypeId pair_double_long = pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::LongLong)));
	const TypeId pair_uchar_double = pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::UnsignedChar)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double)));
	const TypeId int_type = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId long_type = table.builtin(CanonicalBuiltinKind::LongLong);
	const TypeId double_type = table.builtin(CanonicalBuiltinKind::Double);
	const TypeId single_types[] = {double_type};
	const TypeId single_pairs[] = {pair_payload_double};
	const TypeId full_types[] = {int_type, long_type, double_type};
	const TypeId mixed_pairs[] = {pair_short_int, pair_char_long, pair_payload_double_mixed};
	const TypeId same_inner_pairs[] = {pair_long_int, pair_double_long, pair_uchar_double};
	check_box("EmptyBox", "EmptyOwner", "ShortTarget", std::span<const TypeId>{}, std::span<const TypeId>{});
	check_box("SingleBox", "SingleOwner", "ShortTarget", single_types, single_pairs);
	check_box("MixedBox", "MixedOwner", "LongTarget", full_types, mixed_pairs);
	check_box("SameInnerBox", "SameInnerOwner", "ShortTarget", full_types, same_inner_pairs);
}

TEST_CASE("Lockstep member pack expansion substitutes every referenced pack") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template<class A, class B> struct Pair { A first; B second; };
template<class T> struct Owner { template<class... U> struct List { int marker; }; };
template<class T> struct Holder {
	template<class... U> struct Box {};
	template<class... Tail, class... Other>
	struct Box<Pair<Tail, Other>...> {
		typename Owner<T>::template List<Pair<Tail, Other>...> values;
	};
};
)";
	CompileContext compile_context;
	compile_context.setInputFile("lockstep_member_pack.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const TemplateTypeArg char_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char));
	const TemplateTypeArg int_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	const TemplateTypeArg double_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double));
	const TemplateTypeArg long_arg = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::LongLong));
	const TemplateArgumentVector pair_char_int_args{char_arg, int_arg};
	const TemplateArgumentVector pair_double_long_args{double_arg, long_arg};
	const auto pair_char_int = parser.templateEngine().materializeTemplateInstantiationForLookup("Pair", pair_char_int_args);
	const auto pair_double_long = parser.templateEngine().materializeTemplateInstantiationForLookup("Pair", pair_double_long_args);
	REQUIRE(pair_char_int.resolved_type_info != nullptr);
	REQUIRE(pair_double_long.resolved_type_info != nullptr);
	const TemplateArgumentVector box_args{
		TemplateTypeArg::makeType(pair_char_int.resolved_type_info->registeredTypeIndex()),
		TemplateTypeArg::makeType(pair_double_long.resolved_type_info->registeredTypeIndex())};
	const auto pattern_match = gTemplateRegistry.matchSpecializationPatternWithBindings("Holder::Box", box_args);
	REQUIRE(pattern_match.has_value());
	REQUIRE(pattern_match->node.is<TemplateClassDeclarationNode>());
	const StructDeclarationNode& pattern = pattern_match->node.as<TemplateClassDeclarationNode>().class_decl_node();
	REQUIRE(pattern.members().size() == 1);
	const TypeSpecifierNode& field = pattern.members()[0].declaration.as<DeclarationNode>().type_specifier_node();
	TemplateArgSubstitutionMap scalar_args;
	scalar_args["T"] = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	TemplateArgPackSubstitutionMap pack_args;
	pack_args[StringTable::getOrInternStringHandle("Tail")] = {char_arg, double_arg};
	pack_args[StringTable::getOrInternStringHandle("Other")] = {int_arg, long_arg};
	const std::string_view order[] = {"T", "Tail", "Other"};
	ExpressionSubstitutor substitutor(scalar_args, pack_args, parser, order);
	const TypeSpecifierNode substituted = substitutor.substituteTypeSpecifier(field);
	const TypeInfo* list = tryGetTypeInfo(substituted.type_index());
	REQUIRE(list != nullptr);
	CHECK(!list->isDependentMemberType());
	REQUIRE(list->templateArgs().size() == 2);
	const TypeInfo* first = tryGetTypeInfo(list->templateArgs()[0].type_index);
	const TypeInfo* second = tryGetTypeInfo(list->templateArgs()[1].type_index);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	CHECK(first->baseTemplateName() == StringTable::getOrInternStringHandle("Pair"));
	CHECK(second->baseTemplateName() == StringTable::getOrInternStringHandle("Pair"));
	REQUIRE(first->templateArgs().size() == 2);
	REQUIRE(second->templateArgs().size() == 2);
	CHECK(first->templateArgs()[0].type_index == nativeTypeIndex(TypeCategory::Char));
	CHECK(first->templateArgs()[1].type_index == nativeTypeIndex(TypeCategory::Int));
	CHECK(second->templateArgs()[0].type_index == nativeTypeIndex(TypeCategory::Double));
	CHECK(second->templateArgs()[1].type_index == nativeTypeIndex(TypeCategory::LongLong));

	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId field_type = importCanonicalStructMemberType(table, substituted);
	REQUIRE(field_type);
	const TemplateArgumentVector owner_int_args{int_arg};
	const auto owner_int = parser.templateEngine().materializeTemplateInstantiationForLookup("Owner", owner_int_args);
	REQUIRE(owner_int.resolved_type_info != nullptr);
	const CanonicalTypeImport imported_owner = importCanonicalClassTypeInfo(table, *owner_int.resolved_type_info);
	REQUIRE(imported_owner.status == CanonicalTypeImportStatus::Supported);
	CHECK(table.templateSpecializationOwner(field_type) == imported_owner.type);
	const CanonicalTypeImport imported_pair_char_int = importCanonicalClassTypeInfo(table, *pair_char_int.resolved_type_info);
	const CanonicalTypeImport imported_pair_double_long = importCanonicalClassTypeInfo(table, *pair_double_long.resolved_type_info);
	REQUIRE(imported_pair_char_int.status == CanonicalTypeImportStatus::Supported);
	REQUIRE(imported_pair_double_long.status == CanonicalTypeImportStatus::Supported);
	TypeId argument_link = table.templateSpecializationArguments(field_type);
	REQUIRE(argument_link);
	REQUIRE(table.templateArgumentIsType(argument_link));
	CHECK(table.templateArgumentType(argument_link) == imported_pair_char_int.type);
	argument_link = table.templateArgumentNext(argument_link);
	REQUIRE(argument_link);
	REQUIRE(table.templateArgumentIsType(argument_link));
	CHECK(table.templateArgumentType(argument_link) == imported_pair_double_long.type);
	CHECK_FALSE(table.templateArgumentNext(argument_link));

	// A scalar template parameter inside the pattern expands alongside the pack.
	TemplateArgSubstitutionMap mixed_scalar_args;
	mixed_scalar_args["T"] = TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int));
	mixed_scalar_args["Other"] = long_arg;
	TemplateArgPackSubstitutionMap mixed_pack_args;
	mixed_pack_args[StringTable::getOrInternStringHandle("Tail")] = {char_arg, double_arg};
	const std::string_view mixed_order[] = {"T", "Tail", "Other"};
	ExpressionSubstitutor mixed_substitutor(mixed_scalar_args, mixed_pack_args, parser, mixed_order);
	const TypeSpecifierNode mixed = mixed_substitutor.substituteTypeSpecifier(field);
	const TypeInfo* mixed_list = tryGetTypeInfo(mixed.type_index());
	REQUIRE(mixed_list != nullptr);
	CHECK(!mixed_list->isDependentMemberType());
	REQUIRE(mixed_list->templateArgs().size() == 2);
	const TypeInfo* mixed_first = tryGetTypeInfo(mixed_list->templateArgs()[0].type_index);
	const TypeInfo* mixed_second = tryGetTypeInfo(mixed_list->templateArgs()[1].type_index);
	REQUIRE(mixed_first != nullptr);
	REQUIRE(mixed_second != nullptr);
	REQUIRE(mixed_first->templateArgs().size() == 2);
	REQUIRE(mixed_second->templateArgs().size() == 2);
	CHECK(mixed_first->templateArgs()[0].type_index == nativeTypeIndex(TypeCategory::Char));
	CHECK(mixed_first->templateArgs()[1].type_index == nativeTypeIndex(TypeCategory::LongLong));
	CHECK(mixed_second->templateArgs()[0].type_index == nativeTypeIndex(TypeCategory::Double));
	CHECK(mixed_second->templateArgs()[1].type_index == nativeTypeIndex(TypeCategory::LongLong));
}

TEST_CASE("Namespace-scope lockstep targets preserve canonical arguments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_namespace_scope_lockstep_pack_member_field_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using FullBox = Holder<int>::Box<char, Payload>;
using EmptyBox = Holder<int>::Box<>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("namespace_scope_lockstep_pack.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto wrap_type = [&](TemplateTypeArg argument) {
		const TemplateArgumentVector arguments{argument};
		const auto instance = template_engine.materializeTemplateInstantiationForLookup("Wrap", arguments);
		REQUIRE(instance.resolved_type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *instance.resolved_type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 1);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		const TypeId field = importCanonicalStructMemberType(table, syntax);
		REQUIRE(field);
		CHECK(record->members[0].canonical_type_id == field);
		return field;
	};
	const TypeId full_field = field_type("FullBox");
	const TypeInfo* payload_info = findTypeByName(StringTable::getOrInternStringHandle("Payload"));
	REQUIRE(payload_info != nullptr);
	TypeId argument = table.templateSpecializationArguments(full_field);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == wrap_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char))));
	argument = table.templateArgumentNext(argument);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == wrap_type(TemplateTypeArg::makeType(payload_info->registeredTypeIndex())));
	CHECK_FALSE(table.templateArgumentNext(argument));
	const TypeId empty_field = field_type("EmptyBox");
	CHECK_FALSE(table.templateSpecializationArguments(empty_field));
	CHECK(imported_alias("FullBox") != imported_alias("EmptyBox"));
}
TEST_CASE("Namespace-scope multipack targets preserve canonical lockstep arguments") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_namespace_scope_lockstep_multipack_member_field_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using EmptyBox = Holder<>::Box<>;
using SingleBox = Holder<PackPayload>::Box<double>;
using FullBox = Holder<short, char, PackPayload>::Box<int, long long, double>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("namespace_scope_multipack.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();

	const auto pair_type = [&](TemplateTypeArg first, TemplateTypeArg second) {
		const TemplateArgumentVector arguments{first, second};
		const auto instance = template_engine.materializeTemplateInstantiationForLookup("Pair", arguments);
		REQUIRE(instance.resolved_type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *instance.resolved_type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 1);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		const TypeId field = importCanonicalStructMemberType(table, syntax);
		REQUIRE(field);
		CHECK(record->members[0].canonical_type_id == field);
		return field;
	};
	const TypeInfo* payload_info = findTypeByName(StringTable::getOrInternStringHandle("PackPayload"));
	REQUIRE(payload_info != nullptr);
	const TemplateTypeArg payload_arg = TemplateTypeArg::makeType(payload_info->registeredTypeIndex());
	const TypeId empty_field = field_type("EmptyBox");
	CHECK_FALSE(table.templateSpecializationArguments(empty_field));
	const TypeId single_field = field_type("SingleBox");
	TypeId argument = table.templateSpecializationArguments(single_field);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == pair_type(payload_arg, TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double))));
	CHECK_FALSE(table.templateArgumentNext(argument));
	const TypeId full_field = field_type("FullBox");
	argument = table.templateSpecializationArguments(full_field);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Short)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int))));
	argument = table.templateArgumentNext(argument);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char)),
		TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::LongLong))));
	argument = table.templateArgumentNext(argument);
	REQUIRE(argument);
	CHECK(table.templateArgumentType(argument) == pair_type(payload_arg, TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Double))));
	CHECK_FALSE(table.templateArgumentNext(argument));
}
TEST_CASE("Mixed scalar and pack member fields preserve canonical owners") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_lockstep_mixed_scalar_pack_member_field_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	code.append(R"(
using ShortOwner = Owner<short>;
using LongOwner = Owner<long long>;
using ShortBox = Holder<short>::Box<char, int, Payload>;
using LongBox = Holder<long long>::Box<char, int, Payload>;
)");
	CompileContext compile_context;
	compile_context.setInputFile("lockstep_mixed_scalar_pack.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto pair_type = [&](TemplateTypeArg first, TemplateTypeArg second) {
		const TemplateArgumentVector arguments{first, second};
		const auto instance = template_engine.materializeTemplateInstantiationForLookup("Pair", arguments);
		REQUIRE(instance.resolved_type_info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *instance.resolved_type_info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto field_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(info->registeredTypeIndex());
		const StructTypeInfo* record = tryGetStructTypeInfo(resolved.type_index);
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->members.size() == 1);
		const TypeSpecifierNode& syntax = record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		const TypeId field = importCanonicalStructMemberType(table, syntax);
		REQUIRE(field);
		CHECK(record->members[0].canonical_type_id == field);
		return field;
	};
	const TypeId short_field = field_type("ShortBox");
	const TypeId long_field = field_type("LongBox");
	CHECK(short_field != long_field);
	CHECK(table.templateSpecializationOwner(short_field) == imported_alias("ShortOwner"));
	CHECK(table.templateSpecializationOwner(long_field) == imported_alias("LongOwner"));
	const TypeInfo* payload_info = findTypeByName(StringTable::getOrInternStringHandle("Payload"));
	REQUIRE(payload_info != nullptr);
	const TypeId short_arguments = table.templateSpecializationArguments(short_field);
	REQUIRE(short_arguments);
	CHECK(table.templateArgumentType(short_arguments) ==
		pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char)), TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Int))));
	const TypeId second_argument = table.templateArgumentNext(short_arguments);
	REQUIRE(second_argument);
	CHECK(table.templateArgumentType(second_argument) ==
		pair_type(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Char)), TemplateTypeArg::makeType(payload_info->registeredTypeIndex())));
	CHECK_FALSE(table.templateArgumentNext(second_argument));
}
TEST_CASE("Member template specialization syntax retains its instantiated owner") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
template <class T>
struct Owner {
	template <class... U> struct List { T owner_value; };
	template <class Head, class... Tail> struct List<Head, Tail...> { T owner_value; };
};
using ShortOwner = Owner<short>;
using LongOwner = Owner<long long>;
)";
	CompileContext compile_context;
	compile_context.setInputFile("member_template_owned_syntax.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto list_entry = gTemplateRegistry.lookupTemplate("Owner::List");
	REQUIRE(list_entry.has_value());
	REQUIRE(list_entry->is<TemplateClassDeclarationNode>());
	const TemplateDeclId list_primary = list_entry->as<TemplateClassDeclarationNode>().template_decl_id();
	REQUIRE(list_primary);
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto owner_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId short_owner = owner_type("ShortOwner");
	const TypeId long_owner = owner_type("LongOwner");
	REQUIRE(short_owner != long_owner);
	const TypeSpecifierNode int_type(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
	const TypeSpecifierNode char_type(TypeCategory::Char, TypeQualifier::None, 8, Token{}, CVQualifier::None);
	const std::vector<TypeSpecifierNode> arguments{int_type, char_type};
	TypeSpecifierNode short_syntax;
	short_syntax.set_member_template_specialization(list_primary, short_owner, arguments);
	TypeSpecifierNode long_syntax;
	long_syntax.set_member_template_specialization(list_primary, long_owner, arguments);
	const CanonicalTypeImport short_imported =
		importCanonicalTemplateSpecialization(table, short_syntax, CanonicalTypeImportContext::Exact);
	REQUIRE(short_imported.status == CanonicalTypeImportStatus::Supported);
	const CanonicalTypeImport long_imported =
		importCanonicalTemplateSpecialization(table, long_syntax, CanonicalTypeImportContext::Exact);
	REQUIRE(long_imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(short_imported.type != long_imported.type);
	CHECK(table.templateSpecializationOwner(short_imported.type) == short_owner);
	CHECK(table.templateSpecializationOwner(long_imported.type) == long_owner);
	CHECK(table.templateSpecializationArguments(short_imported.type) == table.templateSpecializationArguments(long_imported.type));
	TypeSpecifierNode ownerless_syntax;
	ownerless_syntax.set_template_specialization(list_primary, arguments);
	const CanonicalTypeImport ownerless_imported =
		importCanonicalTemplateSpecialization(table, ownerless_syntax, CanonicalTypeImportContext::Exact);
	REQUIRE(ownerless_imported.status == CanonicalTypeImportStatus::Supported);
	CHECK(ownerless_imported.type != short_imported.type);
	CHECK(table.templateSpecializationOwner(ownerless_imported.type) == TypeId{});
}

TEST_CASE("Materialized class-template-owned data members publish their owner") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream fixture("tests/test_canonical_member_template_owned_fields_ret0.cpp");
	REQUIRE(fixture.is_open());
	std::string code((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
	CompileContext compile_context;
	compile_context.setInputFile("member_template_owned_fields.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto imported_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const auto member_type = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const StructTypeInfo* record = info->getStructInfo();
		REQUIRE(record != nullptr);
		REQUIRE(record->declaration_node != nullptr);
		REQUIRE(record->declaration_node->members().size() == 1);
		const TypeSpecifierNode& field =
			record->declaration_node->members()[0].declaration.as<DeclarationNode>().type_specifier_node();
		const TypeId imported = importCanonicalStructMemberType(table, field);
		REQUIRE(imported);
		return imported;
	};
	const TypeId short_owner = imported_type("ShortOwner");
	const TypeId long_owner = imported_type("LongOwner");
	const TypeId short_member = member_type("ShortBox");
	const TypeId long_member = member_type("LongBox");
	CHECK(short_member != long_member);
	CHECK(table.templateSpecializationOwner(short_member) == short_owner);
	CHECK(table.templateSpecializationOwner(long_member) == long_owner);
	CHECK(table.templateSpecializationArguments(short_member) == table.templateSpecializationArguments(long_member));
}

TEST_CASE("Direct type pack bindings preserve declaration and argument boundaries") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	const std::string code = R"(
struct Owner {
	template<class... T> struct List {};
	template<class Head, class... Tail> struct List<Head, Tail...> { int member; };
};
)";
	CompileContext compile_context;
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine engine;
	parser.attachTemplateEngine(engine);
	REQUIRE(!parser.parse().is_error());
	const auto arg = [](TypeCategory category) {
		TemplateTypeArg result;
		result.type_index = nativeTypeIndex(category);
		return result;
	};
	TemplateTypeArg arguments[] = {arg(TypeCategory::Int), arg(TypeCategory::Char), arg(TypeCategory::Double), arg(TypeCategory::Short)};
	arguments[1].is_pack = true;
	arguments[2].is_pack = true;
	const std::span<const TemplateTypeArg> bound(arguments);
	const auto match = gTemplateRegistry.matchSpecializationPatternWithBindings("Owner::List", bound.first(3));
	REQUIRE(match.has_value());
	const auto& parameters = match->node.as<TemplateClassDeclarationNode>().template_parameters();
	REQUIRE(parameters.size() == 2);
	TypeInfo::TemplateArgInfo occurrence;
	occurrence.type_index = parameters[1].registered_type_index();
	occurrence.is_pack = true;
	const auto pack = findDirectTypePackExpansion(occurrence, parameters, bound);
	REQUIRE(pack.has_value());
	REQUIRE(pack->size() == 2);
	CHECK((*pack)[0].category() == TypeCategory::Char);
	CHECK((*pack)[1].category() == TypeCategory::Double);
	CHECK(findDirectTypePackExpansion(occurrence, parameters, bound.first(1))->empty());
	CHECK_FALSE(findDirectTypePackExpansion(occurrence, parameters, bound.first(0)).has_value());
	TemplateParameterVector other_parameters = parameters;
	other_parameters[1].set_registered_type_index(parameters[0].registered_type_index());
	CHECK_FALSE(findDirectTypePackExpansion(occurrence, other_parameters, bound).has_value());
	arguments[1].is_dependent = true;
	CHECK_FALSE(findDirectTypePackExpansion(occurrence, parameters, bound).has_value());
	arguments[1].is_dependent = false;
	occurrence.is_pack = false;
	CHECK_FALSE(findDirectTypePackExpansion(occurrence, parameters, bound).has_value());
}

TEST_CASE("Nested structural NTTP member pointers bind the specialized target owner") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	std::ifstream input("tests/test_canonical_structural_nttp_cross_nested_member_pointer_ret0.cpp");
	REQUIRE(input.is_open());
	std::string code((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>{});
	code += "\nusing TargetOne = Outer<short, 1>::Target; using TargetLong = Outer<long long, 1>::Target; ";
	code += "using TargetTwo = Outer<short, 2>::Target; using KeyTwo = Outer<short, 2>::Key; ";
	code += "using LevelOne = Outer<short, 1>::Level; using ChildOne = Outer<short, 1>::Level::Child;\n";
	CompileContext compile_context;
	compile_context.setInputFile("tests/test_canonical_structural_nttp_cross_nested_member_pointer_ret0.cpp");
	Lexer lexer(code);
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	CanonicalTypeTable& table = context.canonicalTypes();
	const auto import_alias = [&](std::string_view name) {
		const TypeInfo* info = findTypeByName(StringTable::getOrInternStringHandle(name));
		REQUIRE(info != nullptr);
		const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *info);
		REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
		return imported.type;
	};
	const TypeId key_one = import_alias("ShortKey1");
	const TypeId key_two = import_alias("KeyTwo");
	const TypeId target_one = import_alias("TargetOne");
	const TypeId target_two = import_alias("TargetTwo");
	CHECK(target_one != target_two);
	CHECK(table.nestedRecordEntity(target_one) == table.nestedRecordEntity(target_two));
	CHECK(table.templateSpecializationDecl(table.nestedRecordOwner(target_one)) ==
		table.templateSpecializationDecl(table.nestedRecordOwner(target_two)));
	const auto check_key_schema = [&](TypeId key, TypeId target) {
		REQUIRE(table.hasRecordFieldSchema(key));
		REQUIRE(table.hasRecordSubobjectTypes(key));
		const TypeId object_type = table.recordSubobjectMemberAt(key, 0);
		REQUIRE(table.node(object_type).kind == CanonicalTypeKind::MemberObjectPointer);
		CHECK(table.memberPointerOwner(object_type) == target);
		const TypeId function_type = table.recordSubobjectMemberAt(key, 1);
		REQUIRE(table.node(function_type).kind == CanonicalTypeKind::MemberFunctionPointer);
		CHECK(table.memberPointerOwner(function_type) == target);
		const TypeId array_type = table.recordSubobjectMemberAt(key, 3);
		REQUIRE(table.node(array_type).kind == CanonicalTypeKind::Array);
		CHECK(table.memberPointerOwner(table.node(array_type).child) == target);
	};
	check_key_schema(key_one, target_one);
	check_key_schema(key_two, target_two);
	const TypeId fixed_target = import_alias("FixedTarget");
	const TypeId fixed_key = import_alias("FixedKey");
	REQUIRE(table.hasRecordFieldSchema(fixed_key));
	REQUIRE(table.hasRecordSubobjectTypes(fixed_key));
	const TypeId fixed_pointer_type = table.recordSubobjectMemberAt(fixed_key, 0);
	REQUIRE(table.node(fixed_pointer_type).kind == CanonicalTypeKind::MemberObjectPointer);
	CHECK(table.memberPointerOwner(fixed_pointer_type) == fixed_target);
	const TypeId level_one = import_alias("LevelOne");
	const TypeId child_one = import_alias("ChildOne");
	REQUIRE(table.hasRecordSubobjectTypes(child_one));
	const TypeId child_object = table.recordSubobjectMemberAt(child_one, 0);
	REQUIRE(table.node(child_object).kind == CanonicalTypeKind::MemberObjectPointer);
	CHECK(table.memberPointerOwner(child_object) == level_one);
	CHECK(table.nestedRecordOwner(child_one) == level_one);
	const TypeId child_named = import_alias("ShortChildNamed");
	CHECK(child_named == import_alias("ShortChildBraced"));
	const TypeId child_value = table.templateArgumentValue(table.templateSpecializationArguments(child_named));
	REQUIRE(table.node(child_value).kind == CanonicalTypeKind::StructuralValue);
	CHECK(table.node(child_value).child == child_one);
	const TypeId child_member = table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(child_value).array_extent));
	REQUIRE(table.node(child_member).kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(table.node(child_member).array_extent) == level_one);
	CHECK(import_alias("LongChildNamed") == import_alias("LongChildBraced"));
	const auto check_value_owner = [&](std::string_view alias, TypeId target) {
		const TypeId specialization = import_alias(alias);
		TypeId value = table.templateArgumentValue(table.templateSpecializationArguments(specialization));
		REQUIRE(table.node(value).kind == CanonicalTypeKind::StructuralValue);
		TypeId link = unpackStructuralValueSubobjects(table.node(value).array_extent);
		const auto check_object = [&](TypeId node) {
			REQUIRE(table.node(node).kind == CanonicalTypeKind::MemberObjectPointerValue);
			CHECK(unpackMemberObjectPointerValueDeclaringClass(table.node(node).array_extent) == target);
		};
		check_object(table.templateArgumentValue(link));
		link = table.templateArgumentNext(link);
		REQUIRE(table.node(table.templateArgumentValue(link)).kind == CanonicalTypeKind::MemberFunctionPointerValue);
		CHECK(unpackMemberFunctionPointerValueDeclaringClass(table.node(table.templateArgumentValue(link)).array_extent) == target);
		link = table.templateArgumentNext(table.templateArgumentNext(link));
		const TypeId array_value = table.templateArgumentValue(link);
		REQUIRE(table.node(array_value).kind == CanonicalTypeKind::StructuralArrayValue);
		TypeId element = unpackStructuralValueSubobjects(table.node(array_value).array_extent);
		check_object(table.templateArgumentValue(element));
	};
	check_value_owner("ShortNamed", target_one);
	check_value_owner("LongNamed", import_alias("TargetLong"));
	const TypeId fixed_named = import_alias("FixedNamed");
	CHECK(fixed_named == import_alias("FixedBraced"));
	const TypeId fixed_value = table.templateArgumentValue(table.templateSpecializationArguments(fixed_named));
	REQUIRE(table.node(fixed_value).kind == CanonicalTypeKind::StructuralValue);
	const TypeId fixed_member = table.templateArgumentValue(unpackStructuralValueSubobjects(table.node(fixed_value).array_extent));
	REQUIRE(table.node(fixed_member).kind == CanonicalTypeKind::MemberObjectPointerValue);
	CHECK(unpackMemberObjectPointerValueDeclaringClass(table.node(fixed_member).array_extent) == fixed_target);
	CHECK(import_alias("ShortNamed") == import_alias("ShortBraced"));
	CHECK(import_alias("ShortNamed") == import_alias("ShortRepeatAlias"));
	CHECK(import_alias("ShortNamed") != import_alias("ChangedTarget"));
}

TEST_CASE("Missing nested member-pointer owners roll back canonical schema publication") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear(); gConceptRegistry.clear(); gSymbolTable.clear();
	FrontendContext context;
	CompileContext compile_context;
	compile_context.setInputFile("canonical_nested_owner_rollback.cpp");
	Lexer lexer("template<class T> struct RollbackOwner { struct Target { int value; }; struct Key { T Target::* pointer; }; };");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	TemplateEngine template_engine;
	parser.attachTemplateEngine(template_engine);
	REQUIRE(!parser.parse().is_error());
	const auto owner_node = gTemplateRegistry.lookupTemplate("RollbackOwner");
	REQUIRE(owner_node.has_value());
	REQUIRE(owner_node->is<TemplateClassDeclarationNode>());
	const TemplateClassDeclarationNode* owner_template = &owner_node->as<TemplateClassDeclarationNode>();
	const TemplateDeclId environment = owner_template->template_decl_id();
	REQUIRE(environment);
	const StructDeclarationNode& owner_pattern = owner_template->class_decl_node();
	const StructDeclarationNode* target_pattern = nullptr;
	const StructDeclarationNode* key_pattern = nullptr;
	for (const ASTNode& nested_node : owner_pattern.nested_classes()) {
		if (!nested_node.is<StructDeclarationNode>()) continue;
		const StructDeclarationNode& nested = nested_node.as<StructDeclarationNode>();
		if (nested.name() == StringTable::getOrInternStringHandle("Target")) target_pattern = &nested;
		if (nested.name() == StringTable::getOrInternStringHandle("Key")) key_pattern = &nested;
	}
	REQUIRE(target_pattern != nullptr);
	REQUIRE(key_pattern != nullptr);
	REQUIRE(target_pattern->has_entity_id());
	REQUIRE(key_pattern->has_entity_id());
	CHECK(context.declarationBuilder().entity(target_pattern->entity_id()).owner_id == ownerIdFromTemplateDecl(environment));
	const StructMemberDecl& key_member = key_pattern->members()[0];
	REQUIRE(key_member.declaration.is<DeclarationNode>());
	const TypeSpecifierNode& member_syntax = key_member.declaration.as<DeclarationNode>().type_specifier_node();
	CHECK(resolveMemberClassEntity(member_syntax) == target_pattern->entity_id());
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId pattern_member_type = importCanonicalStructMemberType(table, member_syntax);
	REQUIRE(pattern_member_type);
	MonotonicScratchArena& scratch = context.scratchArena();
	const ScratchArenaState scratch_mark = scratch.mark();
	StructDeclarationNode* root_declaration = scratch.allocateObject<StructDeclarationNode>(StringTable::getOrInternStringHandle("RollbackOwner"));
	root_declaration->set_canonical_specialization_owner_template_decl_id(environment);
	const StringHandle outer_names[] = {StringTable::getOrInternStringHandle("T")};
	TemplateTypeArg int_argument;
	int_argument.type_index = nativeTypeIndex(TypeCategory::Int);
	const TemplateTypeArg arguments[] = {int_argument};
	root_declaration->set_outer_template_bindings(std::span<const StringHandle>(outer_names), std::span<const TemplateTypeArg>(arguments));
	StructDeclarationNode* key_declaration = scratch.allocateObject<StructDeclarationNode>(StringTable::getOrInternStringHandle("Key"));
	key_declaration->set_injected_class_pattern_declaration(key_pattern);
	key_declaration->set_enclosing_class(root_declaration);
	key_declaration->set_outer_template_bindings(std::span<const StringHandle>(outer_names), std::span<const TemplateTypeArg>(arguments));
	StructTypeInfo* root = scratch.allocateObject<StructTypeInfo>(root_declaration->name(), AccessSpecifier::Public, false, NamespaceHandle{});
	root->declaration_node = root_declaration;
	root->layout_is_complete = true;
	root->total_size = SizeInBytes{8};
	StructTypeInfo* key = scratch.allocateObject<StructTypeInfo>(key_declaration->name(), AccessSpecifier::Public, false, NamespaceHandle{});
	key->declaration_node = key_declaration;
	key->layout_is_complete = true;
	key->total_size = SizeInBytes{8};
	key->addMember(StringTable::getOrInternStringHandle("pointer"), nativeTypeIndex(TypeCategory::Int), pattern_member_type,
		8, 8, AccessSpecifier::Public, std::nullopt, ReferenceQualifier::None, 0, false, {}, false, 1, std::nullopt,
		std::nullopt, false, CVQualifier::None);
	CHECK(key->members[0].canonical_type_id == pattern_member_type);
	root->addNestedClass(key);
	const uint64_t scratch_before = scratch.currentBytes();
	tryPublishCanonicalNestedClassSchemas(context, *root, environment, arguments);
	CHECK(scratch.currentBytes() == scratch_before);
	const CanonicalTypeImport root_type = importCanonicalClassDeclaration(table, *root_declaration);
	REQUIRE(root_type.status == CanonicalTypeImportStatus::Supported);
	const TypeId key_type = table.nestedRecord(root_type.type, key_pattern->entity_id());
	CHECK_FALSE(key->members[0].canonical_type_id);
	CHECK_FALSE(table.hasRecordFieldSchema(key_type));
	CHECK_FALSE(table.hasRecordSubobjectTypes(key_type));
	scratch.rollbackTo(scratch_mark);
}

TEST_CASE("Canonical nested record owner chains scale with logical nesting depth") {
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	const ScratchArenaState scratch_mark = context.scratchArena().mark();
	StructDeclarationNode* owner_declaration = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	owner_declaration->set_canonical_specialization_owner_template_decl_id(TemplateDeclId{1});
	for (uint32_t index = 0; index < 4096; ++index) {
		StructDeclarationNode* pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
		pattern->set_entity_id(EntityId{index + 10000});
		StructDeclarationNode* nested = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
		nested->set_injected_class_pattern_declaration(pattern);
		nested->set_enclosing_class(owner_declaration);
		owner_declaration = nested;
	}
	const CanonicalTypeImport imported = importCanonicalClassDeclaration(table, *owner_declaration);
	REQUIRE(imported.status == CanonicalTypeImportStatus::Supported);
	TypeId cursor = imported.type;
	for (uint32_t index = 0; index < 4096; ++index) {
		CHECK(table.node(cursor).kind == CanonicalTypeKind::NestedRecord);
		CHECK(table.nestedRecordEntity(cursor) == EntityId{10000 + 4095 - index});
		cursor = table.nestedRecordOwner(cursor);
	}
	CHECK(table.node(cursor).kind == CanonicalTypeKind::TemplateSpecialization);
	CHECK(table.templateSpecializationDecl(cursor) == TemplateDeclId{1});
	context.scratchArena().rollbackTo(scratch_mark);
}

TEST_CASE("Nested class schema publication scales without retaining scratch frames") {
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	ScratchTransaction scratch_scope(context.scratchArena(), context.scratchRegistry());
	MonotonicScratchArena& scratch = scratch_scope.arena();
	StructDeclarationNode* root_declaration = scratch.allocateObject<StructDeclarationNode>(StringHandle{});
	root_declaration->set_canonical_specialization_owner_template_decl_id(TemplateDeclId{91});
	StructTypeInfo* root = scratch.allocateObject<StructTypeInfo>(StringHandle{}, AccessSpecifier::Public, false, NamespaceHandle{});
	root->declaration_node = root_declaration;
	root->layout_is_complete = true;
	root->total_size = SizeInBytes{1};
	StructTypeInfo* parent_record = root;
	StructDeclarationNode* parent_declaration = root_declaration;
	for (uint32_t index = 0; index < 4096; ++index) {
		const StringHandle name = StringTable::getOrInternStringHandle("Nested");
		StructDeclarationNode* pattern = scratch.allocateObject<StructDeclarationNode>(name);
		pattern->set_entity_id(EntityId{20000 + index});
		StructDeclarationNode* declaration = scratch.allocateObject<StructDeclarationNode>(name);
		declaration->set_injected_class_pattern_declaration(pattern);
		declaration->set_enclosing_class(parent_declaration);
		StructTypeInfo* record = scratch.allocateObject<StructTypeInfo>(name, AccessSpecifier::Public, false, NamespaceHandle{});
		record->declaration_node = declaration;
		record->layout_is_complete = true;
		record->total_size = SizeInBytes{1};
		parent_record->addNestedClass(record);
		parent_record = record;
		parent_declaration = declaration;
	}
	const CanonicalTypeImport imported_root = importCanonicalClassDeclaration(table, *root_declaration);
	REQUIRE(imported_root.status == CanonicalTypeImportStatus::Supported);
	const uint64_t scratch_before = scratch.currentBytes();
	tryPublishCanonicalNestedClassSchemas(context, *root, TemplateDeclId{91}, std::span<const TemplateTypeArg>{});
	CHECK(scratch.currentBytes() == scratch_before);
	TypeId nested = table.nestedRecord(imported_root.type, EntityId{20000});
	for (uint32_t index = 0; index < 4096; ++index) {
		REQUIRE(table.node(nested).kind == CanonicalTypeKind::NestedRecord);
		CHECK(table.hasRecordFieldSchema(nested));
		CHECK(table.hasRecordSubobjectTypes(nested));
		CHECK(table.hasRecordProperties(nested));
		CHECK(table.hasClassBaseSchema(nested));
		if (index != 4095) nested = table.nestedRecord(nested, EntityId{20001 + index});
	}
}

TEST_CASE("Canonical nested class import rejects missing, cyclic, and partial owners transactionally") {
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	const ScratchArenaState scratch_mark = context.scratchArena().mark();
	StructDeclarationNode* missing_pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	missing_pattern->set_entity_id(EntityId{31});
	StructDeclarationNode* missing_parent = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	missing_parent->set_injected_class_pattern_declaration(missing_pattern);
	const size_t before_missing = table.size();
	const CanonicalTypeImport missing = importCanonicalClassDeclaration(table, *missing_parent);
	CHECK(missing.status == CanonicalTypeImportStatus::UnmigratedNominal);
	CHECK(table.size() == before_missing);
	StructDeclarationNode* cyclic_pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	cyclic_pattern->set_entity_id(EntityId{32});
	StructDeclarationNode* cyclic = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	cyclic->set_injected_class_pattern_declaration(cyclic_pattern);
	cyclic->set_enclosing_class(cyclic);
	const size_t before_cycle = table.size();
	const CanonicalTypeImport cycle = importCanonicalClassDeclaration(table, *cyclic);
	CHECK(cycle.status == CanonicalTypeImportStatus::Unresolved);
	CHECK(table.size() == before_cycle);
	StructDeclarationNode* partial_pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	partial_pattern->set_entity_id(EntityId{33});
	CHECK_FALSE(partial_pattern->has_template_decl_id());
	StructDeclarationNode* partial_owner = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	partial_owner->set_injected_class_pattern_declaration(partial_pattern);
	partial_owner->set_canonical_specialization_owner_template_decl_id(TemplateDeclId{3});
	StructDeclarationNode* nested_pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	nested_pattern->set_entity_id(EntityId{34});
	StructDeclarationNode* nested = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	nested->set_injected_class_pattern_declaration(nested_pattern);
	nested->set_enclosing_class(partial_owner);
	const size_t before_partial = table.size();
	const CanonicalTypeImport partial = importCanonicalClassDeclaration(table, *nested);
	CHECK(partial.status == CanonicalTypeImportStatus::UnmigratedNominal);
	CHECK(table.size() == before_partial);
	StructDeclarationNode* member_template_pattern = context.scratchArena().allocateObject<StructDeclarationNode>(StringHandle{});
	member_template_pattern->set_template_decl_id(TemplateDeclId{4});
	nested->set_injected_class_pattern_declaration(member_template_pattern);
	const size_t before_member_template = table.size();
	CHECK(importCanonicalClassDeclaration(table, *nested).status == CanonicalTypeImportStatus::UnmigratedNominal);
	CHECK(table.size() == before_member_template);
	context.scratchArena().rollbackTo(scratch_mark);
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
	binary.set_resolved_member_operator_overload(overload.member_overload, overload.declaring_type);
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

TEST_CASE("Dependent initializer list identity preserves style designators and nested elements") {
	using Style = InitializerListNode::InitializationStyle;
	const auto value = [] { return ASTNode::emplace_node<NumericLiteralNode>(Token{}, 7ULL, TypeCategory::Int, TypeQualifier::None, 32); };
	auto make_list = [&](Style style, bool designated, std::string_view name) {
		InitializerListNode list(style);
		if (designated) list.add_designated_initializer(StringTable::getOrInternStringHandle(name), value());
		else list.add_initializer(value());
		return ASTNode::emplace_node<InitializerListNode>(std::move(list));
	};
	const ASTNode brace = make_list(Style::Brace, true, "key");
	const ASTNode equal = make_list(Style::Brace, true, "key");
	CHECK(FlashCpp::equalDependentExpressionIdentity(brace, equal));
	CHECK(FlashCpp::hashDependentExpressionIdentity(brace) == FlashCpp::hashDependentExpressionIdentity(equal));
	CHECK_FALSE(FlashCpp::equalDependentExpressionIdentity(brace, make_list(Style::Paren, true, "key")));
	CHECK_FALSE(FlashCpp::equalDependentExpressionIdentity(brace, make_list(Style::Brace, false, "key")));
	CHECK_FALSE(FlashCpp::equalDependentExpressionIdentity(brace, make_list(Style::Brace, true, "other")));
	InitializerListNode nested_a;
	nested_a.add_initializer(make_list(Style::Brace, true, "key"));
	InitializerListNode nested_b;
	nested_b.add_initializer(make_list(Style::Brace, true, "key"));
	const ASTNode nested_left = ASTNode::emplace_node<InitializerListNode>(std::move(nested_a));
	const ASTNode nested_right = ASTNode::emplace_node<InitializerListNode>(std::move(nested_b));
	CHECK(FlashCpp::equalDependentExpressionIdentity(nested_left, nested_right));
	CHECK(FlashCpp::hashDependentExpressionIdentity(nested_left) == FlashCpp::hashDependentExpressionIdentity(nested_right));
	ASTNode deep_left = brace;
	ASTNode deep_right = equal;
	for (size_t depth = 0; depth < 4096; ++depth) {
		InitializerListNode left;
		InitializerListNode right;
		left.add_initializer(deep_left);
		right.add_initializer(deep_right);
		deep_left = ASTNode::emplace_node<InitializerListNode>(std::move(left));
		deep_right = ASTNode::emplace_node<InitializerListNode>(std::move(right));
	}
	CHECK(FlashCpp::equalDependentExpressionIdentity(deep_left, deep_right));
	CHECK(FlashCpp::hashDependentExpressionIdentity(deep_left) == FlashCpp::hashDependentExpressionIdentity(deep_right));
}

TEST_CASE("Structural value importer bounds native stack across deep subobject graphs") {
	clearLegacyTypeTablesForTesting();
	FrontendContext context;
	initialize_native_types();
	CanonicalTypeTable& table = context.canonicalTypes();
	constexpr uint32_t depth = 4096;
	std::vector<FlashCpp::StructuralClassValue> values(depth);
	TypeIndex previous_index = nativeTypeIndex(TypeCategory::Short);
	TypeId previous_type = table.builtin(CanonicalBuiltinKind::Short);
	const StringHandle field_name = StringTable::getOrInternStringHandle("value");
	for (uint32_t index = 0; index < depth; ++index) {
		const StringHandle name = StringTable::getOrInternStringHandle("DeepStructural" + std::to_string(index));
		const TypeCreationResult created = add_struct_type(name, NamespaceRegistry::GLOBAL_NAMESPACE);
		StructTypeInfo& record = created.info.createStructInfo(name, AccessSpecifier::Public, false, NamespaceRegistry::GLOBAL_NAMESPACE);
		ASTNode declaration = ASTNode::emplace_node<StructDeclarationNode>(name, false, false);
		declaration.as<StructDeclarationNode>().set_entity_id(EntityId{420000 + index});
		record.declaration_node = &declaration.as<StructDeclarationNode>();
		record.addMember(field_name, previous_index, previous_type, 2, 2, AccessSpecifier::Public, std::nullopt,
			ReferenceQualifier::None, 0, false, {}, false, 0, std::nullopt, std::nullopt, false, CVQualifier::None);
		values[index].type_index = created.index;
		FlashCpp::StructuralClassValueMember member;
		member.name = field_name;
		if (index == 0) {
			member.scalar_value = FlashCpp::NonTypeValueIdentity::makeConcrete(-7, TypeCategory::Short);
		} else {
			member.nested_value = &values[index - 1];
		}
		values[index].members.push_back(std::move(member));
		previous_index = created.index;
		previous_type = table.record(EntityId{420000 + index});
	}
	const std::optional<TypeId> imported = tryImportCanonicalStructuralValue(table, &values.back());
	REQUIRE(imported.has_value());
	values.clear();
	TypeId cursor = *imported;
	for (uint32_t index = depth; index-- > 0;) {
		const CanonicalTypeNode value = table.node(cursor);
		REQUIRE(value.kind == CanonicalTypeKind::StructuralValue);
		CHECK(value.child == table.record(EntityId{420000 + index}));
		CHECK(unpackStructuralValueBaseCount(value.array_extent) == 0);
		const TypeId link = unpackStructuralValueSubobjects(value.array_extent);
		CHECK_FALSE(table.templateArgumentNext(link));
		cursor = table.templateArgumentValue(link);
	}
	CHECK(table.node(cursor).child == table.builtin(CanonicalBuiltinKind::Short));
	CHECK(table.node(cursor).array_extent == static_cast<uint64_t>(-7));
	std::ostringstream trace;
	const FlashCpp::LogLevel previous_level = FlashCpp::LogConfig::getLevelForCategory(FlashCpp::LogCategory::Types);
	std::ostream* previous_output = FlashCpp::LogConfig::output_stream;
	FlashCpp::LogConfig::output_stream = &trace;
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, FlashCpp::LogLevel::Trace);
	const CanonicalTemplateArgument arguments[] = {CanonicalTemplateArgument::makeValue(*imported)};
	(void)table.templateSpecialization(TemplateDeclId{490000}, arguments);
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, previous_level);
	FlashCpp::LogConfig::output_stream = previous_output;
	CHECK(trace.str().find("canonical-request-v2") != std::string::npos);
}

TEST_CASE("Canonical union value tracing bounds native stack across deep variants") {
	FrontendContext context;
	CanonicalTypeTable& table = context.canonicalTypes();
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	TypeId payload = table.integralValue(integer, 42);
	TypeId member_type = integer;
	const auto flags = CanonicalRecordMemberFlags::UnionVariant | CanonicalRecordMemberFlags::UnionGroupStart |
		CanonicalRecordMemberFlags::UnionGroupEnd;
	constexpr uint32_t depth = 4096;
	for (uint32_t index = 0; index < depth; ++index) {
		const EntityId entity{500000 + index};
		const TypeId owner = table.record(entity);
		table.publishRecordLayout({entity, 4, 4, 4, 4, 1, 0, CanonicalRecordLayoutFlags::Union, 0});
		const CanonicalRecordMember member{member_type, 0, 4, 0, 0, CanonicalAccess::Public, flags};
		table.publishRecordFieldSchema(table.record(entity), std::span<const CanonicalRecordMember>(&member, 1), {});
		payload = table.structuralUnionValue(owner, 0, uint16_t{0}, payload);
		member_type = owner;
	}
	TypeId cursor = payload;
	for (uint32_t index = depth; index-- > 0;) {
		const CanonicalTypeNode node = table.node(cursor);
		REQUIRE(node.kind == CanonicalTypeKind::StructuralUnionValue);
		CHECK(node.child == table.record(EntityId{500000 + index}));
		CHECK(unpackStructuralUnionActiveVariant(node.array_extent) == 0);
		cursor = table.templateArgumentValue(unpackStructuralValueSubobjects(node.array_extent));
	}
	CHECK(table.node(cursor).array_extent == 42);
	std::ostringstream trace;
	const FlashCpp::LogLevel level = FlashCpp::LogConfig::getLevelForCategory(FlashCpp::LogCategory::Types);
	std::ostream* output = FlashCpp::LogConfig::output_stream;
	FlashCpp::LogConfig::output_stream = &trace;
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, FlashCpp::LogLevel::Trace);
	const CanonicalTemplateArgument argument[] = {CanonicalTemplateArgument::makeValue(payload)};
	(void)table.templateSpecialization(TemplateDeclId{504100}, argument);
	FlashCpp::LogConfig::setLevel(FlashCpp::LogCategory::Types, level);
	FlashCpp::LogConfig::output_stream = output;
	CHECK(trace.str().find("canonical-request-v2") != std::string::npos);
}
