#pragma once

#include <stdexcept>
#include <vector>
#include <optional>
#include <unordered_map>
#include <sstream>

#include "Token.h"
#include "AstNodeTypes.h"
#include "Lexer.h"

enum class ParserError {
	None,
	UnexpectedToken,
	MissingSemicolon,

	NotImplemented
};

static std::string_view get_parser_error_string(ParserError e)
{
	switch (e) {
	case ParserError::None:
	default:
		return "Internal error";

	case ParserError::UnexpectedToken:
		return "Unexpected token";

	case ParserError::MissingSemicolon:
		return "Missing semicolon(;)";

	case ParserError::NotImplemented:
		return "Feature/token type not implemented yet";
	}
}

class ParseResult {
public:
	ParseResult() : is_error_(false) {}
	ParseResult(std::string error_message, Token token) : is_error_(true), error_message_(std::move(error_message)), token_(std::move(token)) {}

	bool is_error() const { return is_error_; }
	const std::string& error_message() const { return error_message_; }

	static ParseResult success() { return ParseResult(); }
	static ParseResult error(const std::string& error_message, Token token) { return ParseResult(error_message, std::move(token)); }
	static ParseResult error(ParserError e, Token token) { return ParseResult(std::string(get_parser_error_string(e)), std::move(token)); }

private:
	bool is_error_;
	std::string error_message_;
	Token token_;
};

class Parser {
public:
	explicit Parser(Lexer& lexer) : lexer_(lexer), current_token_(lexer_.next_token()) {}

	ParseResult parse() {
		ParseResult parseResult;
		while (peek_token().has_value() && !parseResult.is_error()) {
			parseResult = parse_top_level_node();
		}
		return parseResult;
	}

	const std::vector<ASTNode>& get_nodes() { return ast_nodes_; }

private:
	Lexer& lexer_;
	std::optional<Token> current_token_;
	std::vector<ASTNode> ast_nodes_;

	struct SavedToken {
		//Token current_token_;
		size_t ast_nodes_size_ = 0;
	};
	std::unordered_map<size_t, SavedToken> saved_tokens_;

	std::optional<Token> consume_token() {
		/*if (current_token_.type() == Token::Type::EndOfFile)
			return current_token_;

		Token consumed_token = current_token_;
		current_token_ = lexer_.next_token();
		return consumed_token;*/
		std::optional<Token> token = peek_token();
		current_token_.reset();
		return token;
	}

	std::optional<Token> peek_token() {
		// Create a copy of the lexer to avoid consuming the next token
		/*auto saved_pos = lexer_.save_token_position();
		Token peeked_token = lexer_.next_token();
		lexer_.restore_token_position(saved_pos);
		return peeked_token.type() == Token::Type::EndOfFile ? std::nullopt : std::make_optional(peeked_token);*/
		if (!current_token_.has_value())
		{
			current_token_.emplace(lexer_.next_token());
		}

		return current_token_;
	}

	// Parsing functions for different constructs
	ParseResult parse_top_level_node();
	ParseResult parse_namespace();
	ParseResult parse_type_specifier();
	ParseResult parse_declaration_or_function_definition();
	ParseResult parse_declaration(std::string_view expected_end_of_declaration = ";");
	ParseResult parse_function_definition_or_declaration();
	ParseResult parse_block();
	ParseResult parse_statement_or_declaration();
	ParseResult parse_return_statement();
	ParseResult parse_statement();
	ParseResult parse_expression(int precedence);
	ParseResult parse_primary_expression();
	ParseResult parse_unary_expression();
	ParseResult parse_binary_expression(int min_precedence = 0);
	ParseResult parse_parenthesized_expression();

	// Utility functions
	std::optional<std::string_view> consume_identifier();
	std::optional<std::string_view> consume_literal();
	bool consume_punctuator(const std::string_view& value);
	bool consume_keyword(const std::string_view& value);
	int get_operator_precedence(const std::string_view& op);

	TokenPosition save_token_position();
	void restore_token_position(const TokenPosition& token_position);
	void discard_saved_token(const TokenPosition& token_position);
};

ParseResult Parser::parse_top_level_node() {
	// Save the current token's position to restore later in case of a parsing error
	TokenPosition saved_position = save_token_position();

	// Check if it's a namespace declaration
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "namespace") {
		//return parse_namespace_declaration();
		return ParseResult::error(ParserError::NotImplemented, *peek_token());
	}

	// Check if it's a class or struct declaration
	if (peek_token()->type() == Token::Type::Keyword && (peek_token()->value() == "class" || peek_token()->value() == "struct")) {
		//return parse_class_declaration();
		return ParseResult::error(ParserError::NotImplemented, *peek_token());
	}

	// Attempt to parse a function definition, variable declaration, or typedef
	auto result = parse_declaration_or_function_definition();
	if (!result.is_error()) {
		discard_saved_token(saved_position);
		return result;
	}

	// If we failed to parse any top-level construct, restore the token position
	// and report an error
	restore_token_position(saved_position);
	return ParseResult::error("Failed to parse top-level construct", *peek_token());
}

ParseResult Parser::parse_declaration_or_function_definition() {
	// Save the current token's position to restore later in case of a parsing error
	TokenPosition saved_position = save_token_position();

	// Attempt to parse a function definition
	Token function_token = *peek_token();
	auto function_definition_result = parse_function_definition_or_declaration();
	if (!function_definition_result.is_error()) {
		discard_saved_token(saved_position);
		return ParseResult::success();
	}

	// If parsing a function definition failed, restore the token position
	restore_token_position(saved_position);

	// Attempt to parse a simple declaration (variable or typedef)
	auto declaration_result = parse_declaration();
	if (!declaration_result.is_error()) {
		discard_saved_token(saved_position);
		return ParseResult::success();
	}

	// If we failed to parse a function definition or simple declaration,
	// propagate the error
	return declaration_result;
}

ParseResult Parser::parse_declaration(std::string_view expected_end_of_declaration) {
	// Parse the type specifier (can be a keyword, identifier, or complex type)
	auto type_specifier = current_token_.value_or(Token{});
	auto type_specifier_result = parse_type_specifier();
	if (type_specifier_result.is_error()) {
		return type_specifier_result;
	}

	// Parse the identifier (variable name)
	auto identifier_token = consume_token();
	if (!identifier_token || identifier_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier in declaration", current_token_.value_or(Token{}));
	}

	// Check if the declaration has an initializer (optional)
	/*ExpressionNode initializer;
	if (consume_punctuator("=")) {
		auto initializer_result = parse_expression();
		if (initializer_result.is_error()) {
			return initializer_result;
		}
		initializer = initializer_result.get_node();
	}*/

	// Create the DeclarationNode
	DeclarationNode declaration(type_specifier, *identifier_token); //, std::move(initializer));
	ast_nodes_.emplace_back(declaration);
	size_t declaration_index = ast_nodes_.size() - 1;

	// Expect a semicolon at the end of the declaration
	if (!consume_punctuator(expected_end_of_declaration)) {
		std::string err;
		std::ostringstream iss(err);
		iss << "Expected '" << expected_end_of_declaration << "' after declaration";
		return ParseResult::error(err, current_token_.value_or(Token{}));
	}

	return ParseResult::success();
}


ParseResult Parser::parse_type_specifier() {
	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value() ||
		(current_token_opt->type() != Token::Type::Keyword &&
			current_token_opt->type() != Token::Type::Identifier)) {
		return ParseResult::error("Expected type specifier", current_token_opt.value_or(Token()));
	}

	// Check for simple types
	static const std::unordered_set<std::string_view> simple_types = {
		"int", "float", "double", "char", "bool", "void", "short", "long", "unsigned"
	};

	if (simple_types.count(current_token_opt->value())) {
		Token simple_type = current_token_opt.value();
		consume_token();
		ast_nodes_.emplace_back(TypeSpecifierNode(simple_type));
		return ParseResult::success();
	}

	// If the token is an identifier, it might be a user-defined type
	if (current_token_opt->type() == Token::Type::Identifier) {
		Token user_defined_type = current_token_opt.value();
		consume_token();
		ast_nodes_.emplace_back(TypeSpecifierNode(user_defined_type));
		return ParseResult::success();
	}

	// Add more cases here for complex type specifiers (e.g., templates, qualifiers) if needed

	return ParseResult::error("Unexpected token in type specifier", current_token_opt.value_or(Token()));
}

ParseResult Parser::parse_function_definition_or_declaration() {
	// Parse return type
	if (peek_token()->type() != Token::Type::Keyword) {
		return ParseResult::error("Expected return type for function definition", *current_token_);
	}
	auto return_type = consume_token();

	// Parse function name (identifier)
	auto function_name_token = consume_token(); // Store the function name token
	if (!function_name_token || function_name_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected function name (identifier)", *current_token_);
	}

	// Create the function declaration
	auto& func_node = ast_nodes_.emplace_back(FunctionDeclarationNode(*function_name_token, *return_type));

	// Parse parameters
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' for function parameter list", *current_token_);
	}

	std::vector<size_t> parameter_indices;
	while (!consume_punctuator(")")) {
		// Parse parameter type
		if (current_token_->type() != Token::Type::Keyword) {
			return ParseResult::error("Expected parameter type", *current_token_);
		}
		Token parameter_type = *current_token_;
		consume_token(); // consume parameter type

		// Parse parameter name (identifier)
		auto parameter_name_token = consume_token(); // Store the parameter name token
		if (!parameter_name_token || parameter_name_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected parameter name (identifier)", *current_token_);
		}

		size_t parameter_index = ast_nodes_.size();
		DeclarationNode parameter_declaration(parameter_type, *parameter_name_token);
		ast_nodes_.emplace_back(parameter_declaration);
		func_node.as<FunctionDeclarationNode>().add_parameter_ast_index(parameter_index);

		// Parse default parameter value (if present)
		if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == "=") {
			consume_token(); // consume '='

			// Parse the default value expression
			auto default_value = parse_expression(0);
			// Set the default value for the parameter_declaration if needed
		}

		if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == ",") {
			consume_token(); // consume ','
		}
		else if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == ")") {
			break;
		}
		else {
			return ParseResult::error("Expected ',' or ')' in function parameter list", *current_token_);
		}
	}

	// Is only function declaration
	if (consume_punctuator(";")) {
		return ParseResult::success();
	}

	// Parse function body
	return parse_block();
}

ParseResult Parser::parse_block() {
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' for block", *current_token_);
	}

	size_t start_index = ast_nodes_.size();
	auto& block_node = ast_nodes_.emplace_back(BlockNode(start_index));

	while (!consume_punctuator("}")) {
		// Parse statements or declarations
		ParseResult parse_result = parse_statement_or_declaration();
		if (parse_result.is_error())
			return parse_result;
	}

	block_node.as<BlockNode>().set_num_statements(ast_nodes_.size() - start_index);

	return ParseResult::success();
}

ParseResult Parser::parse_statement_or_declaration() {
	// Define a function pointer type for parsing functions
	using ParsingFunction = ParseResult(Parser::*)();

	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value()) {
		return ParseResult::error("Expected a statement or declaration", *current_token_);
	}
	const Token& current_token = current_token_opt.value();

	if (current_token.type() == Token::Type::Keyword) {
		static const std::unordered_map<std::string_view, ParsingFunction> keyword_parsing_functions = {
			//{"if", &Parser::parse_if_statement},
			//{"for", &Parser::parse_for_loop},
			//{"while", &Parser::parse_while_loop},
			//{"do", &Parser::parse_do_while_loop},
			{"return", &Parser::parse_return_statement},
			//{"struct", &Parser::parse_struct_declaration}
		};

		auto keyword_iter = keyword_parsing_functions.find(current_token.value());
		if (keyword_iter != keyword_parsing_functions.end()) {
			// Call the appropriate parsing function
			return (this->*(keyword_iter->second))();
		}
		else {
			// If it's not a known keyword, assume it's a type specifier (e.g. int, float, etc.) and parse a variable declaration
			//return parse_variable_declaration();
		}
	}
	else if (current_token.type() == Token::Type::Identifier) {
		// If it starts with an identifier, it could be an assignment, expression, or function call statement
		return parse_expression(0);
	}
	else {
		return ParseResult::error("Expected a statement or declaration", *current_token_);
	}

	return ParseResult::success();
}

ParseResult Parser::parse_return_statement() {
	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value() || current_token_opt.value().type() != Token::Type::Keyword || current_token_opt.value().value() != "return") {
		return ParseResult::error(ParserError::UnexpectedToken, current_token_opt.value_or(Token()));
	}
	consume_token(); // Consume the 'return' keyword

	// Parse the return expression (if any)
	std::optional<size_t> return_expr_index;
	auto next_token_opt = peek_token();
	if (!next_token_opt.has_value() || (next_token_opt.value().type() != Token::Type::Punctuator || next_token_opt.value().value() != ";")) {
		return_expr_index = ast_nodes_.size();
		auto expr_error = parse_expression(0);
		if (expr_error.is_error()) {
			return expr_error;
		}
	}

	// Consume the semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error(ParserError::MissingSemicolon, peek_token().value_or(Token()));
	}

	ast_nodes_.emplace_back(ReturnStatementNode(return_expr_index));
	return ParseResult::success();
}

ParseResult Parser::parse_expression(int precedence) {
	ParseResult result = parse_primary_expression();
	if (result.is_error()) {
		return result;
	}

	while (true) {
		// Check if the current token is a binary operator
		if (peek_token()->type() != Token::Type::Operator) {
			break;
		}

		// Get the precedence of the current operator
		int current_operator_precedence = get_operator_precedence(peek_token()->value());

		// If the current operator has lower precedence than the provided precedence,
		// stop parsing the expression
		if (current_operator_precedence < precedence) {
			break;
		}

		// Consume the operator token
		Token operator_token = *current_token_;
		consume_token();

		// Parse the right-hand side expression
		ParseResult rhs_result = parse_expression(current_operator_precedence + 1);
		if (rhs_result.is_error()) {
			return rhs_result;
		}

		// Create a BinaryOperatorNode and add it to ast_nodes_
		size_t lhs_index = ast_nodes_.size() - 2;  // The left-hand side expression was already added
		size_t rhs_index = ast_nodes_.size() - 1;  // The right-hand side expression was just added
		ast_nodes_.emplace_back(BinaryOperatorNode(operator_token, lhs_index, rhs_index));
	}

	return ParseResult::success();
}

ParseResult Parser::parse_primary_expression() {
	if (current_token_->type() == Token::Type::Identifier) {
		// Parse identifier
		ast_nodes_.emplace_back(IdentifierNode(*current_token_));
		consume_token();
	}
	else if (current_token_->type() == Token::Type::Literal) {
		// Parse literal
		ast_nodes_.emplace_back(StringLiteralNode(*current_token_));
		consume_token();
	}
	else if (consume_punctuator("(")) {
		// Parse parenthesized expression
		ParseResult result = parse_expression(0);
		if (result.is_error()) {
			return result;
		}
		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after parenthesized expression", *current_token_);
		}
	}
	else {
		return ParseResult::error("Expected primary expression", *current_token_);
	}

	return ParseResult::success();
}


// Utility functions
std::optional<std::string_view> Parser::consume_identifier() {
	if (peek_token()->type() == Token::Type::Identifier) {
		std::string_view identifier = current_token_->value();
		consume_token(); // consume identifier
		return identifier;
	}
	return std::nullopt;
}

std::optional<std::string_view> Parser::consume_literal() {
	if (peek_token()->type() == Token::Type::Literal) {
		std::string_view value = current_token_->value();
		consume_token(); // consume literal
		return value;
	}
	return std::nullopt;
}

bool Parser::consume_punctuator(const std::string_view& value) {
	if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == value) {
		consume_token(); // consume punctuator
		return true;
	}
	return false;
}

bool Parser::consume_keyword(const std::string_view& value) {
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == value) {
		consume_token(); // consume keyword
		return true;
	}
	return false;
}

int Parser::get_operator_precedence(const std::string_view& op) {
	static const std::unordered_map<std::string_view, int> precedence_map = {
		{"*", 5},
		{"/", 5},
		{"%", 5},
		{"+", 4},
		{"-", 4},
		{"<<", 3},
		{">>", 3},
		{"<", 2},
		{"<=", 2},
		{">", 2},
		{">=", 2},
		{"==", 1},
		{"!=", 1},
		{"&&", 0},
		{"||", -1},
	};

	auto it = precedence_map.find(op);
	if (it != precedence_map.end()) {
		return it->second;
	}
	else {
		throw std::runtime_error("Invalid operator");
	}
}

TokenPosition Parser::save_token_position() {
	TokenPosition cur_pos = lexer_.save_token_position();
	saved_tokens_[cur_pos.cursor_] = { /*current_token_,*/ ast_nodes_.size()};
	return cur_pos;
}

void Parser::restore_token_position(const TokenPosition& saved_token_pos) {
	lexer_.restore_token_position(saved_token_pos);
	SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
	current_token_.reset();// = saved_token.current_token_;
	ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_, ast_nodes_.end());
	saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::discard_saved_token(const TokenPosition& saved_token_pos) {
	saved_tokens_.erase(saved_token_pos.cursor_);
}