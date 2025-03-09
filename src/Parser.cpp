#include "Parser.h"
#include "LibClangIRGenerator.h"
#include <string_view> // Include string_view header

bool Parser::generate_coff(const std::string& outputFilename) {
    return FlashCpp::GenerateCOFF(ast_nodes_, outputFilename);
}

Parser::Parser(Lexer& lexer)
    : lexer_(lexer), current_token_(lexer_.next_token()) {
    ast_nodes_.reserve(default_ast_tree_size_);
}

Parser::ScopedTokenPosition::ScopedTokenPosition(class Parser& parser)
    : parser_(parser), saved_position_(parser.save_token_position()) {}

Parser::ScopedTokenPosition::~ScopedTokenPosition() {
    if (!discarded_) {
        parser_.restore_token_position(saved_position_);
    }
}

ParseResult Parser::ScopedTokenPosition::success() {
    discarded_ = true;
    parser_.discard_saved_token(saved_position_);
    return ParseResult::success();
}

ParseResult Parser::ScopedTokenPosition::error(std::string_view error_message) {
    discarded_ = true;
    parser_.discard_saved_token(saved_position_);
    return ParseResult::error(std::string(error_message),
        *parser_.peek_token());
}

std::optional<Token> Parser::consume_token() {
    std::optional<Token> token = peek_token();
    current_token_.emplace(lexer_.next_token());
    return token;
}

std::optional<Token> Parser::peek_token() {
    if (!current_token_.has_value()) {
        current_token_.emplace(lexer_.next_token());
    }
    return current_token_;
}

TokenPosition Parser::save_token_position() {
    TokenPosition cur_pos = lexer_.save_token_position();
    saved_tokens_[cur_pos.cursor_] = { ast_nodes_.size() };
    return cur_pos;
}

void Parser::restore_token_position(const TokenPosition& saved_token_pos) {
    lexer_.restore_token_position(saved_token_pos);
    SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
    current_token_.reset();
    ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_,
        ast_nodes_.end());
    saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::discard_saved_token(const TokenPosition& saved_token_pos) {
    saved_tokens_.erase(saved_token_pos.cursor_);
}

ParseResult Parser::parse_top_level_node()
{
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
		if (auto node = result.node()) {
			ast_nodes_.push_back(*node);
		}
		return saved_position.success();
	}

	// If we failed to parse any top-level construct, restore the token position
	// and report an error
	return saved_position.error("Failed to parse top-level construct");
}

ParseResult Parser::parse_type_and_name() {
    // Parse the type specifier
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

    // Unwrap the optional ASTNode before passing it to emplace_node
    if (auto node = type_specifier_result.node()) {
        return ParseResult::success(emplace_node<DeclarationNode>(*node, *identifier_token));
    }
    return ParseResult::error("Invalid type specifier node", *identifier_token);
}

ParseResult Parser::parse_declaration_or_function_definition()
{
	// Save the current token's position to restore later in case of a parsing
	// error
	TokenPosition saved_position = save_token_position();

	// Parse the type specifier and identifier (name)
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error())
		return type_and_name_result;

	// Attempt to parse a function definition
	DeclarationNode& decl_node = as<DeclarationNode>(type_and_name_result);
	const bool is_probably_function = (peek_token()->value() == "(");
	auto function_definition_result = ParseResult();
	if (is_probably_function) {
		function_definition_result = parse_function_declaration(decl_node);
		if (function_definition_result.is_error()) {
			return function_definition_result;
		}
	}

	TypeSpecifierNode& type_specifier = decl_node.type_node().as<TypeSpecifierNode>();
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
		const Token& identifier_token = decl_node.identifier_token();
		if (auto node = type_and_name_result.node()) {
			if (!gSymbolTable.insert(identifier_token.value(), *node))
				return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
		}

		// Is only function declaration
		if (consume_punctuator(";")) {
			return ParseResult::success();
		}

		// Add function parameters to the symbol table within a function scope
		gSymbolTable.enter_scope(ScopeType::Function);
		if (auto funcNode = function_definition_result.node()) {
			const auto& func_decl = funcNode->as<FunctionDeclarationNode>();
			for (const auto& param : func_decl.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl_node = param.as<DeclarationNode>();
					const Token& param_token = param_decl_node.identifier_token();
					gSymbolTable.insert(param_token.value(), param);
				}
			}

			// Parse function body
			auto block_result = parse_block();
			if (block_result.is_error())
				return block_result;

			gSymbolTable.exit_scope();

			if (auto node = function_definition_result.node()) {
				if (auto block = block_result.node()) {
					node->as<FunctionDeclarationNode>().set_definition(block->as<BlockNode>());
					return ParseResult::success(*node);
				}
			}
		}
	}
	// Attempt to parse a simple declaration (variable or typedef)
	if (!consume_punctuator(";")) {
		discard_saved_token(saved_position);
		return ParseResult::error("Expected ;", *current_token_);
	}

	discard_saved_token(saved_position);
	return ParseResult::success();
}

ParseResult Parser::parse_type_specifier()
{
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
	unsigned char type_size = 0;
	const auto& it = type_map.find(current_token_opt->value());
	if (it != type_map.end()) {
		type = std::get<0>(it->second);
		type_size = static_cast<unsigned char>(std::get<1>(it->second));

		if (long_count == 1) {
			if (type == Type::Float) {
				type_size = sizeof(long double);
			}
			else if (type == Type::Int) {
				type_size = sizeof(long long);
			}
		}

		consume_token();
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, current_token_opt.value()));
	}
	else if (current_token_opt->type() == Token::Type::Identifier) {
		// Handle user-defined type
		// You can customize how to store user-defined types and their sizes
		consume_token();
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, current_token_opt.value()));
	}

	return ParseResult::error("Unexpected token in type specifier",
		current_token_opt.value_or(Token()));
}

ParseResult Parser::parse_function_declaration(DeclarationNode& declaration_node)
{
	// Parse parameters
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' for function parameter list",
			*current_token_);
	}

	// Create the function declaration
	auto [func_node, func_ref] =
		create_node_ref<FunctionDeclarationNode>(declaration_node);

	while (!consume_punctuator(")"sv)) {
		// Parse parameter type and name (identifier)
		ParseResult type_and_name_result = parse_type_and_name();
		if (type_and_name_result.is_error()) {
			return type_and_name_result;
		}

		if (auto node = type_and_name_result.node()) {
			func_ref.add_parameter_node(*node);
		}

		// Parse default parameter value (if present)
		if (consume_punctuator("="sv)) {
			consume_token(); // consume '='

			// Parse the default value expression
			auto default_value = parse_expression(0);
			// Set the default value
		}

		if (consume_punctuator(","sv)) {
			continue;
		}
		else if (consume_punctuator(")"sv)) {
			break;
		}
		else {
			return ParseResult::error(
				"Expected ',' or ')' in function parameter list", *current_token_);
		}
	}

	return func_node;
}

ParseResult Parser::parse_block()
{
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' for block", *current_token_);
	}

	auto [block_node, block_ref] = create_node_ref(BlockNode());

	while (!consume_punctuator("}")) {
		// Parse statements or declarations
		ParseResult parse_result = parse_statement_or_declaration();
		if (parse_result.is_error())
			return parse_result;

		if (auto node = parse_result.node()) {
			block_ref.add_statement_node(*node);  // Unwrap optional before passing
		}

		consume_punctuator(";");
	}

	return ParseResult::success(block_node);
}

ParseResult Parser::parse_statement_or_declaration()
{
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

ParseResult Parser::parse_return_statement()
{
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

	if (return_expr_result.has_value()) {
		return ParseResult::success(
			emplace_node<ReturnStatementNode>(return_expr_result.node()));
	}
	else {
		return ParseResult::success(emplace_node<ReturnStatementNode>());
	}
}

ParseResult Parser::parse_expression(int precedence)
{
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

		if (auto leftNode = result.node()) {
			if (auto rightNode = rhs_result.node()) {
				return ParseResult::success(emplace_node<ExpressionNode>(
					BinaryOperatorNode(operator_token, *leftNode, *rightNode)));
			}
		}
		return result;
	}

	return result;
}

std::optional<TypedNumeric> get_numeric_literal_type(std::string_view text)
{
	// Convert the text to lowercase
	std::string lowerText(text);
	std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

	TypedNumeric typeInfo;
	char* end_ptr = nullptr;
	// Check for prefixes
	if (lowerText.find("0x") == 0) {
		// Hexadecimal literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 4.0 / 8) * 8); // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), &end_ptr, 16); // Parse hexadecimal
	}
	else if (lowerText.find("0b") == 0) {
		// Binary literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 1.0 / 8) * 8); // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), &end_ptr, 2); // Parse binary
	}
	else if (lowerText.find("0") == 0) {
		// Octal literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 1) * 3.0 / 8) * 8); // Round to the nearest 8-bit boundary
		typeInfo.value = std::strtoull(lowerText.substr(1).c_str(), &end_ptr, 8); // Parse octal
	}
	else {
		typeInfo.sizeInBits = static_cast<unsigned char>(sizeof(int) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str(), &end_ptr, 10); // Parse integer
	}

	// Check for valid suffixes
	static constexpr std::string_view suffixCharacters = "ul";
	std::string_view suffix = end_ptr;
	if (!suffix.empty() && suffix.find_first_not_of(suffixCharacters) == std::string_view::npos) {
		typeInfo.typeQualifier = static_cast<TypeQualifier>(static_cast<int>(TypeQualifier::Signed) + (suffix.find('u') != std::string_view::npos) * 1);

		// Count the number of 'l' characters
		auto l_count = std::count(suffix.begin(), suffix.end(), 'l');
		typeInfo.sizeInBits = sizeof(long) * static_cast<size_t>(8 + (l_count & 2) * 8);
	}

	return typeInfo;
}


int Parser::get_operator_precedence(const std::string_view& op)
{
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

bool Parser::consume_keyword(const std::string_view& value)
{
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == value) {
		consume_token(); // consume keyword
		return true;
	}
	return false;
}

bool Parser::consume_punctuator(const std::string_view& value)
{
	if (peek_token()->type() == Token::Type::Punctuator &&
		peek_token()->value() == value) {
		consume_token(); // consume punctuator
		return true;
	}
	return false;
}

ParseResult Parser::parse_primary_expression()
{
	std::optional<ASTNode> result;
	if (current_token_->type() == Token::Type::Identifier) {
		Token idenfifier_token = *current_token_;

		// Get the identifier's type information from the symbol table
		auto identifierType = gSymbolTable.lookup(idenfifier_token.value());
		if (!identifierType) {
			return ParseResult::error("Missing identifier", *current_token_);
		}
		else if (!identifierType->is<DeclarationNode>()) {
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, *current_token_);
		}
		else {
			consume_token();

			if (consume_punctuator("(")) {
				if (!peek_token().has_value())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")") {
					ParseResult argResult = parse_expression(0);
					if (argResult.is_error()) {
						return argResult;
					}

					if (auto node = argResult.node()) {
						args.push_back(*node);
					}

					if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == ",") {
						consume_token(); // Consume comma
					}
					else if (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")") {
						return ParseResult::error("Expected ',' or ')' after function argument", *current_token_);
					}

					if (!peek_token().has_value())
						return ParseResult::error(ParserError::NotImplemented, Token());
				}

				if (!consume_punctuator(")")) {
					return ParseResult::error("Expected ')' after function call arguments", *current_token_);
				}

				// Additional type checking and verification logic can be performed here using identifierType

				result = emplace_node<ExpressionNode>(FunctionCallNode(identifierType->as<DeclarationNode>(), std::move(args)));
			}
			else {
				// Regular identifier
				// Additional type checking and verification logic can be performed here using identifierType

				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}
		}
	}
	else if (current_token_->type() == Token::Type::Literal) {
		auto literal_type = get_numeric_literal_type(current_token_->value());
		if (!literal_type) {
			return ParseResult::error("Expected numeric literal", *current_token_);
		}
		result = emplace_node<ExpressionNode>(NumericLiteralNode(*current_token_, literal_type->value, literal_type->type, literal_type->typeQualifier, literal_type->sizeInBits));
		consume_token();
	}
	else if (current_token_->type() == Token::Type::StringLiteral) {
		result = emplace_node<ExpressionNode>(StringLiteralNode(*current_token_));
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
