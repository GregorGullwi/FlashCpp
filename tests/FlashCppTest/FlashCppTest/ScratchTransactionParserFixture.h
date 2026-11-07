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

	size_t classTemplatePackDepthForTest() const {
		return parser.class_template_pack_stack_.size();
	}

	size_t lambdaCaptureDepthForTest() const {
		return parser.lambda_capture_stack_.size();
	}

	size_t classTemplatePackSizeForTest(size_t stack_index, size_t pack_index) const {
		return parser.class_template_pack_stack_[stack_index][pack_index].pack_size;
	}

	bool lambdaCaptureContainsForTest(
		size_t stack_index, StringHandle name, LambdaCaptureNode::CaptureKind kind) const {
		const auto& captures = parser.lambda_capture_stack_[stack_index];
		const auto found = captures.find(name);
		return found != captures.end() && found->second == kind;
	}

	void pushClassTemplatePackForTest(std::string_view pack_name, size_t pack_size) {
		parser.class_template_pack_stack_.push_back({Parser::ClassTemplatePackInfo{pack_name, pack_size}});
	}

	void pushLambdaCaptureForTest(StringHandle name, LambdaCaptureNode::CaptureKind kind) {
		Parser::LambdaCaptureBindings captures;
		captures.emplace(name, kind);
		parser.lambda_capture_stack_.push_back(std::move(captures));
	}

	void openClassTemplatePackGuardForTest(std::string_view pack_name, size_t pack_size) {
		class_pack_guard_for_test_.emplace(parser.class_template_pack_stack_);
		class_pack_guard_for_test_->push({Parser::ClassTemplatePackInfo{pack_name, pack_size}});
	}

	void closeClassTemplatePackGuardForTest() {
		class_pack_guard_for_test_.reset();
	}

	void openLambdaCaptureGuardForTest(StringHandle name, LambdaCaptureNode::CaptureKind kind) {
		lambda_capture_guard_for_test_.emplace(parser.lambda_capture_stack_);
		Parser::LambdaCaptureBindings captures;
		captures.emplace(name, kind);
		lambda_capture_guard_for_test_->push(std::move(captures));
	}

	void closeLambdaCaptureGuardForTest() {
		lambda_capture_guard_for_test_.reset();
	}

	void setPendingTemplateArgsForTest(TemplateArgumentVector args) {
		parser.pending_explicit_template_args_.emplace(std::move(args));
	}

	bool hasPendingTemplateArgsForTest() const {
		return parser.pending_explicit_template_args_.has_value();
	}

	size_t pendingTemplateArgCountForTest() const {
		return parser.pending_explicit_template_args_->size();
	}

	int64_t pendingTemplateArgValueForTest(size_t index) const {
		return (*parser.pending_explicit_template_args_)[index].value;
	}

	CallingConvention callingConventionForTest() const {
		return parser.last_calling_convention_;
	}

	void setCallingConventionForTest(CallingConvention convention) {
		parser.last_calling_convention_ = convention;
	}

	CompileContext compile_context;
	Lexer lexer;
	SemanticAnalysis semantic_analysis;
	Parser parser;
	std::optional<Parser::ClassTemplatePackGuard> class_pack_guard_for_test_;
	std::optional<Parser::LambdaCaptureStackGuard> lambda_capture_guard_for_test_;
};

} // namespace FlashCpp::Tests
