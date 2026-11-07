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

	CompileContext compile_context;
	Lexer lexer;
	SemanticAnalysis semantic_analysis;
	Parser parser;
};

} // namespace FlashCpp::Tests
