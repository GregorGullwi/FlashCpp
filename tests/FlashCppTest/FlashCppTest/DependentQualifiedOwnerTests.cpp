#include "CompilerIncludes.h"
#include "Parser.h"
#include "ParserTemplateHelpers.h"
#include "doctest.h"

TEST_CASE("Dependent member type records retain parsed owner identity") {
	clearLegacyTypeTablesForTesting();
	gTemplateRegistry.clear();
	gConceptRegistry.clear();
	gSymbolTable.clear();
	FrontendContext frontend;
	CompileContext compile_context;
	compile_context.setInputFile("dependent_member_owner_identity.cpp");
	Lexer lexer(R"(
template<int N> struct Tag { long long value; };
template<class T> struct Holder {
	template<class... U> struct Box {};
};
template<int N> struct Chain {
	typename Holder<Tag<N>>::template Box<int, char> item;
};
)");
	SemanticAnalysis sema(compile_context, gSymbolTable);
	Parser parser(lexer, compile_context, sema);
	REQUIRE(!parser.parse().is_error());

	const TypeInfo::DependentQualifiedNameRecord* box_record = nullptr;
	for (const auto& [name, type_info] : getTypesByNameMap()) {
		(void)name;
		if (type_info == nullptr || !type_info->hasDependentQualifiedName()) {
			continue;
		}
		const TypeInfo::DependentQualifiedNameRecord* record = type_info->dependentQualifiedName();
		if (record == nullptr || record->member_chain.empty()) {
			continue;
		}
		if (StringTable::getStringView(record->member_chain.back().name) != "Box") {
			continue;
		}
		box_record = record;
		break;
	}
	REQUIRE(box_record != nullptr);
	CHECK(box_record->owner_type.is_valid());
	const TypeInfo* owner_type_info = tryGetTypeInfo(box_record->owner_type);
	REQUIRE(owner_type_info != nullptr);
	CHECK(owner_type_info->isTemplateInstantiation());
	CHECK(StringTable::getStringView(owner_type_info->baseTemplateName()) == "Holder");
}
