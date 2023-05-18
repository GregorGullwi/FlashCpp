#pragma once

#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <charconv>
#include <cstdlib>

#include "AstNodeTypes.h"
#include "Lexer.h"
#include "Token.h"

enum class ParserError {
	None,
	UnexpectedToken,
	MissingSemicolon,

	NotImplemented
};

static std::string_view get_parser_error_string(ParserError e) {
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

using ASTNodeHandle = size_t;

class ParseResult {
public:
	ParseResult() = default;
	ParseResult(ASTNodeHandle node_ref) : value_or_error_(node_ref) {}
	ParseResult(std::string error_message, Token token)
		: value_or_error_(Error{ std::move(error_message), std::move(token) }) {}

	bool is_error() const {
		return std::holds_alternative<Error>(value_or_error_);
	}
	ASTNodeHandle node_handle() const {
		return std::get<ASTNodeHandle>(value_or_error_);
	}
	const std::string& error_message() const {
		return std::get<Error>(value_or_error_).error_message_;
	}

	static ParseResult success() { return ParseResult(); }
	static ParseResult success(ASTNodeHandle node_ref_) {
		return ParseResult(node_ref_);
	}
	static ParseResult error(const std::string& error_message, Token token) {
		return ParseResult(error_message, std::move(token));
	}
	static ParseResult error(ParserError e, Token token) {
		return ParseResult(std::string(get_parser_error_string(e)),
			std::move(token));
	}

	struct Error {
		std::string error_message_;
		Token token_;
	};

private:
	std::variant<std::monostate, ASTNodeHandle, Error> value_or_error_;
};

class Parser {
public:
	static constexpr size_t default_ast_tree_size_ = 256 * 1024;

	explicit Parser(Lexer& lexer)
		: lexer_(lexer), current_token_(lexer_.next_token()) {
		ast_nodes_.reserve(default_ast_tree_size_);
		allocated_nodes_.reserve(default_ast_tree_size_);
	}

	ParseResult parse() {
		ParseResult parseResult;
		while (peek_token().has_value() && !parseResult.is_error() &&
			peek_token()->type() != Token::Type::EndOfFile) {
			parseResult = parse_top_level_node();
		}
		return parseResult;
	}

	const auto& get_nodes() { return ast_nodes_; }
	const ASTNode& get_inner_node(ASTNodeHandle node_handle) const {
		return allocated_nodes_.at(node_handle);
	}
	bool is_valid_inner_index(size_t inner_index) const {
		return inner_index < allocated_nodes_.size();
	}

	template <typename T> bool is(ASTNodeHandle node_handle) const {
		return allocated_nodes_[node_handle].is<T>();
	}

	template <typename T> T& as(ASTNodeHandle node_handle) {
		return allocated_nodes_[node_handle].as<T>();
	}

	template <typename T> const T& as(ASTNodeHandle node_handle) const {
		return allocated_nodes_[node_handle].as<T>();
	}

	template <typename T> T& as(ParseResult parse_result) {
		return allocated_nodes_[parse_result.node_handle()].as<T>();
	}

private:
	Lexer& lexer_;
	std::optional<Token> current_token_;
	std::vector<ASTNodeHandle> ast_nodes_;
	std::vector<ASTNode>
		allocated_nodes_; // ASTNodes have pointers into this array

	template <typename T> ASTNodeHandle create_node(T&& node) {
		std::size_t index = allocated_nodes_.size();
		allocated_nodes_.emplace_back(std::forward<T>(node));
		return ASTNodeHandle(index);
	}

	template <typename T>
	std::pair<ASTNodeHandle, T&> create_node_ref(T&& node) {
		std::size_t index = allocated_nodes_.size();
		ASTNode& ast_node = allocated_nodes_.emplace_back(std::forward<T>(node));
		return { ASTNodeHandle(index), ast_node.as<T>() };
	}

	class ScopedTokenPosition {
	public:
		explicit ScopedTokenPosition(class Parser& parser)
			: parser_(parser), saved_position_(parser.save_token_position()) {}

		~ScopedTokenPosition() {
			if (!discarded_) {
				parser_.restore_token_position(saved_position_);
			}
		}

		ParseResult success() {
			discarded_ = true;
			parser_.discard_saved_token(saved_position_);
			return ParseResult::success();
		}

		ParseResult error(std::string_view error_message) {
			discarded_ = true;
			parser_.discard_saved_token(saved_position_);
			return ParseResult::error(std::string(error_message),
				*parser_.peek_token());
		}

	private:
		class Parser& parser_;
		TokenPosition saved_position_;
		bool discarded_ = false;
	};

	struct SavedToken {
		// Token current_token_;
		size_t ast_nodes_size_ = 0;
	};
	std::unordered_map<size_t, SavedToken> saved_tokens_;

	std::optional<Token> consume_token() {
		std::optional<Token> token = peek_token();
		current_token_.reset();
		return token;
	}

	std::optional<Token> peek_token() {
		if (!current_token_.has_value()) {
			current_token_.emplace(lexer_.next_token());
		}

		return current_token_;
	}

	// Parsing functions for different constructs
	ParseResult parse_top_level_node();
	ParseResult parse_type_and_name();
	ParseResult parse_namespace();
	ParseResult parse_type_specifier();
	ParseResult parse_declaration_or_function_definition();
	ParseResult parse_function_declaration(ASTNodeHandle declaration_node_handle);
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
	bool consume_punctuator(const std::string_view& value);
	bool consume_keyword(const std::string_view& value);
	int get_operator_precedence(const std::string_view& op);

	TokenPosition save_token_position();
	void restore_token_position(const TokenPosition& token_position);
	void discard_saved_token(const TokenPosition& token_position);
};

ParseResult Parser::parse_top_level_node() {
	// Save the current token's position to restore later in case of a parsing
	// error
	ScopedTokenPosition saved_position(*this);

	// Check if it's a namespace declaration
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == "namespace") {
		// return parse_namespace_declaration();
		return ParseResult::error(ParserError::NotImplemented, *peek_token());
	}

	// Check if it's a class or struct declaration
	if (peek_token()->type() == Token::Type::Keyword &&
		(peek_token()->value() == "class" || peek_token()->value() == "struct")) {
		// return parse_class_declaration();
		return ParseResult::error(ParserError::NotImplemented, *peek_token());
	}

	// Attempt to parse a function definition, variable declaration, or typedef
	auto result = parse_declaration_or_function_definition();
	if (!result.is_error()) {
		return saved_position.success();
	}

	// If we failed to parse any top-level construct, restore the token position
	// and report an error
	return saved_position.error("Failed to parse top-level construct");
}

ParseResult Parser::parse_type_and_name() {
	// Parse the type specifier (can be a keyword, identifier, or complex type)
	auto type_specifier_result = parse_type_specifier();
	if (type_specifier_result.is_error()) {
		return type_specifier_result;
	}

	// Parse the identifier (name)
	auto identifier_token = consume_token();
	if (!identifier_token ||
		identifier_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier token", *identifier_token);
	}

	return ParseResult::success(create_node(
		DeclarationNode(type_specifier_result.node_handle(), *identifier_token)));
}

ParseResult Parser::parse_declaration_or_function_definition() {
	// Save the current token's position to restore later in case of a parsing
	// error
	TokenPosition saved_position = save_token_position();

	// Parse the type specifier and identifier (name)
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error())
		return type_and_name_result;

	// Attempt to parse a function definition
	const bool is_probably_function = (peek_token()->value() == "(");
	if (is_probably_function) {
		auto function_definition_result =
			parse_function_declaration(type_and_name_result.node_handle());
		if (function_definition_result.is_error()) {
			return function_definition_result;
		}
	}

	TypeSpecifierNode& type_specifier = as<TypeSpecifierNode>(
		as<DeclarationNode>(type_and_name_result).type_handle());
	if (type_specifier.type() == Type::Auto) {
		const bool is_trailing_return_type = (peek_token()->value() == "->");
		if (is_trailing_return_type) {
			consume_token();

			ParseResult trailing_type_specifier = parse_type_specifier();
			if (trailing_type_specifier.is_error())
				return trailing_type_specifier;

			type_specifier = as<TypeSpecifierNode>(trailing_type_specifier);
		}
	}

	if (is_probably_function) {
		// Is only function declaration
		if (consume_punctuator(";")) {
			return ParseResult::success();
		}

		// Parse function body
		return parse_block();
	}
	// Attempt to parse a simple declaration (variable or typedef)
	if (!consume_punctuator(";")) {
		discard_saved_token(saved_position);
		return ParseResult::error("Expected ;", *current_token_);
	}

	discard_saved_token(saved_position);
	return ParseResult::success();
}

ParseResult Parser::parse_type_specifier() {
	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value() ||
		(current_token_opt->type() != Token::Type::Keyword &&
			current_token_opt->type() != Token::Type::Identifier)) {
		return ParseResult::error("Expected type specifier",
			current_token_opt.value_or(Token()));
	}

	size_t long_count = 0;
	TypeQualifier qualifier = TypeQualifier::None;

	do {
		std::string_view token_value = current_token_opt->value();
		if (token_value == "long") {
			long_count++;
			consume_token();
		}
		else if (token_value == "signed") {
			qualifier = TypeQualifier::Signed;
			consume_token();
		}
		else if (token_value == "unsigned") {
			qualifier = TypeQualifier::Unsigned;
			consume_token();
		}
		else {
			break;
		}
		current_token_opt = peek_token();
	} while (false);

	static const std::unordered_map<std::string_view, std::tuple<Type, size_t>>
		type_map = {
			{"void", {Type::Void, 0}},
			{"bool", {Type::Bool, std::numeric_limits<bool>::digits}},
			{"char", {Type::Char, std::numeric_limits<unsigned char>::digits}},
			{"int", {Type::Int, std::numeric_limits<unsigned int>::digits}},
			{"short", {Type::Int, std::numeric_limits<unsigned short>::digits}},
			{"long", {Type::Int, std::numeric_limits<unsigned long>::digits}},
			{"float", {Type::Float, std::numeric_limits<float>::digits}},
			{"double", {Type::Float, std::numeric_limits<double>::digits}},
			{"auto", {Type::Auto, 0}},
	};

	Type type = Type::UserDefined;
	size_t type_size = 0;
	const auto& it = type_map.find(current_token_opt->value());
	if (it != type_map.end()) {
		type = std::get<0>(it->second);
		type_size = std::get<1>(it->second);

		if (long_count == 1) {
			if (type == Type::Float) {
				type_size = sizeof(long double);
			}
			else if (type == Type::Int) {
				type_size = sizeof(long long);
			}
		}

		consume_token();
		return ParseResult::success(create_node(TypeSpecifierNode(
			type, qualifier, type_size, current_token_opt.value())));
	}
	else if (current_token_opt->type() == Token::Type::Identifier) {
		// Handle user-defined type
		// You can customize how to store user-defined types and their sizes
		consume_token();
		return ParseResult::success(create_node(TypeSpecifierNode(
			type, qualifier, type_size, current_token_opt.value())));
	}

	return ParseResult::error("Unexpected token in type specifier",
		current_token_opt.value_or(Token()));
}

ParseResult
Parser::parse_function_declaration(ASTNodeHandle declaration_node_handle) {

	// Parse parameters
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' for function parameter list",
			*current_token_);
	}

	// Create the function declaration
	auto [func_node, func_ref] =
		create_node_ref(FunctionDeclarationNode(declaration_node_handle));
	ast_nodes_.push_back(func_node);

	while (!consume_punctuator(")")) {
		// Parse parameter type and name (identifier)
		ParseResult type_and_name_result = parse_type_and_name();
		if (type_and_name_result.is_error()) {
			return type_and_name_result;
		}

		func_ref.add_parameter_node_handle(type_and_name_result.node_handle());

		// Parse default parameter value (if present)
		if (current_token_->value() == "=") {
			consume_token(); // consume '='

			// Parse the default value expression
			auto default_value = parse_expression(0);
			// Set the default value
		}

		if (current_token_->value() == ",") {
			consume_token(); // consume ','
		}
		else if (current_token_->value() == ")") {
			break;
		}
		else {
			return ParseResult::error(
				"Expected ',' or ')' in function parameter list", *current_token_);
		}
	}

	return func_node;
}

ParseResult Parser::parse_block() {
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' for block", *current_token_);
	}

	size_t start_index = ast_nodes_.size();
	auto [block_node, block_ref] = create_node_ref(BlockNode(start_index));
	ast_nodes_.push_back(block_node);

	while (!consume_punctuator("}")) {
		// Parse statements or declarations
		ParseResult parse_result = parse_statement_or_declaration();
		if (parse_result.is_error())
			return parse_result;

		ast_nodes_.push_back(parse_result.node_handle());
	}

	block_ref.set_num_statements(ast_nodes_.size() - start_index);

	return ParseResult::success();
}

ParseResult Parser::parse_statement_or_declaration() {
	// Define a function pointer type for parsing functions
	using ParsingFunction = ParseResult(Parser::*)();

	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value()) {
		return ParseResult::error("Expected a statement or declaration",
			*current_token_);
	}
	const Token& current_token = current_token_opt.value();

	if (current_token.type() == Token::Type::Keyword) {
		static const std::unordered_map<std::string_view, ParsingFunction>
			keyword_parsing_functions = {
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
			// If it's not a known keyword, assume it's a type specifier (e.g. int,
			// float, etc.) and parse a variable declaration
			// return parse_variable_declaration();
		}
	}
	else if (current_token.type() == Token::Type::Identifier) {
		// If it starts with an identifier, it could be an assignment, expression,
		// or function call statement
		return parse_expression(0);
	}
	else {
		return ParseResult::error("Expected a statement or declaration",
			*current_token_);
	}

	return ParseResult::success();
}

ParseResult Parser::parse_return_statement() {
	auto current_token_opt = peek_token();
	if (!current_token_opt.has_value() ||
		current_token_opt.value().type() != Token::Type::Keyword ||
		current_token_opt.value().value() != "return") {
		return ParseResult::error(ParserError::UnexpectedToken,
			current_token_opt.value_or(Token()));
	}
	consume_token(); // Consume the 'return' keyword

	// Parse the return expression (if any)
	ParseResult return_expr_result;
	auto next_token_opt = peek_token();
	if (!next_token_opt.has_value() ||
		(next_token_opt.value().type() != Token::Type::Punctuator ||
			next_token_opt.value().value() != ";")) {
		return_expr_result = parse_expression(0);
		if (return_expr_result.is_error()) {
			return return_expr_result;
		}
	}

	// Consume the semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error(ParserError::MissingSemicolon,
			peek_token().value_or(Token()));
	}

	return ParseResult::success(
		create_node(ReturnStatementNode(return_expr_result.node_handle())));
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
		int current_operator_precedence =
			get_operator_precedence(peek_token()->value());

		// If the current operator has lower precedence than the provided
		// precedence, stop parsing the expression
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
		size_t lhs_index = ast_nodes_.size() -
			2; // The left-hand side expression was already added
		size_t rhs_index =
			ast_nodes_.size() - 1; // The right-hand side expression was just added
		return create_node(ExpressionNode{
			BinaryOperatorNode(operator_token, lhs_index, rhs_index) });
	}

	return result;
}

struct TypedNumeric {
	Type type = Type::Int;
	TypeQualifier typeQualifier = TypeQualifier::None;
	unsigned char sizeInBits = 0;
	NumericLiteralValue value = 0ULL;
};

// only handles unsigned integer types for now
static std::optional<TypedNumeric> get_numeric_literal_type(std::string_view text) {
	// Convert the text to lowercase
	std::string lowerText(text);
	std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

	TypedNumeric typeInfo;

	// Check for prefixes
	if (lowerText.find("0x") == 0) {
		// Hexadecimal literal
		typeInfo.sizeInBits = std::ceil((lowerText.length() - 2) * 4.0 / 8) * 8; // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), nullptr, 16); // Parse hexadecimal
	}
	else if (lowerText.find("0b") == 0) {
		// Binary literal
		typeInfo.sizeInBits = std::ceil((lowerText.length() - 2) * 1.0 / 8) * 8; // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), nullptr, 2); // Parse binary
	}
	else if (lowerText.find("0") == 0) {
		// Octal literal
		typeInfo.sizeInBits = std::ceil((lowerText.length() - 1) * 3.0 / 8) * 8; // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(1).c_str(), nullptr, 8); // Parse octal
	}
	else {
		typeInfo.sizeInBits = sizeof(int) * 8;
		typeInfo.value = std::strtoull(lowerText.c_str(), nullptr, 10); // Parse integer
	}

	// Check for valid suffixes
	static const std::string_view suffixCharacters = "ul";

	std::string_view suffix;
	size_t suffixStart = lowerText.find_last_of(suffixCharacters);
	if (suffixStart != std::string_view::npos) {
		suffix = lowerText.substr(suffixStart);
	}

	if (!suffix.empty() && suffix.find_first_not_of(suffixCharacters) == std::string_view::npos) {
		typeInfo.typeQualifier = static_cast<TypeQualifier>(static_cast<int>(TypeQualifier::Signed) + (suffix.find('u') != std::string_view::npos) * 1);

		// Extract the size information from the suffix
		std::string_view sizeText = suffix;
		sizeText.remove_prefix(suffix.find_first_of(suffixCharacters) + 1);

		if (sizeText.empty()) {
			typeInfo.sizeInBits = sizeof(int) * 8;
		}
		else {
			// Determine the size based on the suffix
			unsigned long long specifiedSize = 0;
			std::from_chars(sizeText.data(), sizeText.data() + sizeText.length(), specifiedSize);

			// Round the specified size to the nearest 8, 16, 32, or 64-bit boundary
			typeInfo.sizeInBits = std::round(specifiedSize / 8.0) * 8;
		}
	}

	return typeInfo;
}


ParseResult Parser::parse_primary_expression() {
	std::optional<ASTNodeHandle> result;
	if (current_token_->type() == Token::Type::Identifier) {
		// Parse identifier
		result = create_node(ExpressionNode{ IdentifierNode(*current_token_) });
		consume_token();
	}
	else if (current_token_->type() == Token::Type::Literal) {
		auto literal_type = get_numeric_literal_type(current_token_->value());
		if (!literal_type) {
			return ParseResult::error("Expected numeric literal", *current_token_);
		}
		result = create_node(ExpressionNode{ NumericLiteralNode(*current_token_, literal_type->value, literal_type->type, literal_type->typeQualifier, literal_type->sizeInBits) });
		consume_token();
	}
	else if (current_token_->type() == Token::Type::StringLiteral) {
		result = create_node(ExpressionNode{ StringLiteralNode(*current_token_) });
		consume_token();
	}
	else if (consume_punctuator("(")) {
		// Parse parenthesized expression
		ParseResult result = parse_expression(0);
		if (result.is_error()) {
			return result;
		}
		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after parenthesized expression",
				*current_token_);
		}
	}
	else {
		return ParseResult::error("Expected primary expression", *current_token_);
	}

	if (result.has_value())
		return ParseResult::success(*result);

	return ParseResult::success();
}

// Utility functions
bool Parser::consume_punctuator(const std::string_view& value) {
	if (peek_token()->type() == Token::Type::Punctuator &&
		peek_token()->value() == value) {
		consume_token(); // consume punctuator
		return true;
	}
	return false;
}

bool Parser::consume_keyword(const std::string_view& value) {
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == value) {
		consume_token(); // consume keyword
		return true;
	}
	return false;
}

int Parser::get_operator_precedence(const std::string_view& op) {
	static const std::unordered_map<std::string_view, int> precedence_map = {
		{"*", 5},  {"/", 5},  {"%", 5},  {"+", 4},  {"-", 4},
		{"<<", 3}, {">>", 3}, {"<", 2},  {"<=", 2}, {">", 2},
		{">=", 2}, {"==", 1}, {"!=", 1}, {"&&", 0}, {"||", -1},
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
	saved_tokens_[cur_pos.cursor_] = {/*current_token_,*/ ast_nodes_.size() };
	return cur_pos;
}

void Parser::restore_token_position(const TokenPosition& saved_token_pos) {
	lexer_.restore_token_position(saved_token_pos);
	SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
	current_token_.reset(); // = saved_token.current_token_;
	ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_,
		ast_nodes_.end());
	saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::discard_saved_token(const TokenPosition& saved_token_pos) {
	saved_tokens_.erase(saved_token_pos.cursor_);
}
