#pragma once

#include <stdexcept>

#include "CompilerIncludes.h"

namespace FlashCpp::Tests {

class ScratchTransactionParserFixture {
public:
	explicit ScratchTransactionParserFixture(FrontendContext& context)
		: lexer(std::string_view{}), semantic_analysis(compile_context, gSymbolTable), parser(lexer, compile_context, semantic_analysis) {
		if (FrontendContext::active() != &context) {
			throw std::logic_error("Scratch parser fixture requires its active frontend context");
		}
	}

	size_t memberContextDepthForTest() const {
		return parser.member_function_context_stack_.size();
	}

	size_t structContextDepthForTest() const {
		return parser.struct_parsing_context_stack_.size();
	}

	const FunctionDeclarationNode* currentFunctionForTest() const {
		return parser.current_function_;
	}

	Linkage currentLinkageForTest() const {
		return parser.current_linkage_;
	}

	void pushMemberContextForTest(StringHandle struct_name) {
		parser.member_function_context_stack_.push_back({struct_name, TypeIndex{}, nullptr, nullptr, false});
	}

	void pushStructContextForTest(StringHandle struct_name) {
		Parser::StructParsingContext context{};
		context.struct_name = StringTable::getStringView(struct_name);
		context.struct_node = nullptr;
		context.namespace_handle = NamespaceHandle{};
		parser.struct_parsing_context_stack_.push_back(std::move(context));
	}

	void setCurrentFunctionForTest(const FunctionDeclarationNode* function) {
		parser.current_function_ = function;
	}

	void setLinkageForTest(Linkage linkage) {
		parser.current_linkage_ = linkage;
	}

	CompileContext compile_context;
	Lexer lexer;
	SemanticAnalysis semantic_analysis;
	Parser parser;
};

} // namespace FlashCpp::Tests
