#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"

ParseResult Parser::parse_concept_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'concept' keyword
	Token concept_token = peek_info();
	if (!consume("concept"_tok)) {
		return ParseResult::error("Expected 'concept' keyword", peek_info());
	}

	// Parse the concept name
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected concept name after 'concept'", current_token_);
	}
	Token concept_name_token = peek_info();
	advance(); // consume concept name

	// For now, we'll support simple concepts without explicit template parameters
	// In full C++20, concepts can have template parameters: template<typename T> concept Name = ...
	// But the simplified syntax is: concept Name = ...;
	// We'll parse the simplified form for now

	// Expect '=' before the constraint expression
	if (peek() != "="_tok) {
		return ParseResult::error("Expected '=' after concept name", current_token_);
	}
	advance(); // consume '='

	// Parse the constraint expression
	// This is typically a requires expression, a type trait, or a boolean expression
	// For now, we'll accept any expression
	auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (constraint_result.is_error()) {
		return constraint_result;
	}

	// Expect ';' at the end
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after concept definition", current_token_);
	}

	// Create the ConceptDeclarationNode
	// For simplified concepts (without template<>), we use an empty template parameter list
	InlineVector<TemplateParameterNode, 4> template_params;

	auto concept_node = emplace_node<ConceptDeclarationNode>(
		concept_name_token,
		std::move(template_params),
		*constraint_result.node(),
		concept_token);

	// Register the concept in the global concept registry
	// This will be done in the semantic analysis phase
	// For now, we just return the node

	return saved_position.success(concept_node);
}

// Parse C++20 requires expression: requires(params) { requirements; } or requires { requirements; }
ParseResult Parser::parse_requires_expression() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'requires' keyword
	Token requires_token = current_token_;
	if (!consume("requires"_tok)) {
		return ParseResult::error("Expected 'requires' keyword", current_token_);
	}

	// Enter a new scope for the requires expression parameters
	gSymbolTable.enter_scope(ScopeType::Block);

	// RAII guard to ensure scope is exited on all code paths (success or error)
	ScopeGuard scope_guard([&]() { gSymbolTable.exit_scope(); });

	// Check if there are parameters: requires(T a, T b) { ... }
	// or no parameters: requires { ... }
	std::vector<ASTNode> parameters;
	if (peek() == "("_tok) {
		FlashCpp::ParsedParameterList parsed_params;
		ParseResult params_result = parse_parameter_list(parsed_params, CallingConvention::Default);
		if (params_result.is_error()) {
			return params_result;
		}

		if (parsed_params.is_variadic) {
			return ParseResult::error("Requires expression parameters cannot be variadic", requires_token);
		}

		for (const auto& param : parsed_params.parameters) {
			if (!param.is<DeclarationNode>()) {
				return ParseResult::error("Expected declaration in requires expression parameter list", requires_token);
			}

			const auto& decl = param.as<DeclarationNode>();
			if (decl.has_default_value()) {
				Token diagnostic_token = !decl.identifier_token().value().empty()
					? decl.identifier_token()
					: requires_token;
				return ParseResult::error("Requires expression parameters cannot have default arguments", diagnostic_token);
			}

			parameters.push_back(param);
			if (!decl.identifier_token().value().empty()) {
				gSymbolTable.insert(decl.identifier_token().value(), param);
			}
		}
	}

	// Expect '{'
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' to begin requires expression body", current_token_);
	}

	// Enter SoftProbe mode for the requires expression body.
	// In requires expressions, function lookup failures and type errors should not produce errors -
	// they indicate that the constraint is not satisfied (the expression is invalid)
	ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});

	// Parse requirements (expressions that must be valid)
	std::vector<ASTNode> requirements;
	while (peek() != "}"_tok) {
		// Check for different types of requirements:
		// 1. Type requirement: typename TypeName;
		// 2. Compound requirement: { expression } -> Type; or just { expression };
		// 3. Nested requirement: requires constraint;
		// 4. Simple requirement: expression;

		if (peek().is_keyword() && peek() == "typename"_tok) {
			// Type requirement: typename T::type; or typename Op<Args...>;
			advance(); // consume 'typename'

			// Parse the type name - can be identifier, qualified name, or template instantiation
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected type name after 'typename' in requires expression", current_token_);
			}
			Token type_name = peek_info();
			advance();

			// Handle qualified names (T::type) and template arguments (Op<Args...>)
			// Only continue parsing if we see :: or <
			while (!peek().is_eof() &&
				   (peek() == "::"_tok || peek() == "<"_tok)) {
				if (peek() == "::"_tok) {
					advance(); // consume '::'
					if (peek().is_identifier()) {
						advance(); // consume qualified name part
					}
				} else if (peek() == "<"_tok) {
					// Parse template arguments using balanced bracket parsing
					advance(); // consume '<'
					int angle_depth = 1;
					while (angle_depth > 0 && !peek().is_eof()) {
						if (peek() == "<"_tok) {
							angle_depth++;
						} else if (peek() == ">"_tok) {
							angle_depth--;
						} else if (peek() == ">>"_tok) {
							// Handle >> as two >
							angle_depth -= 2;
						}
						advance();
					}
				}
			}

			// Create an identifier node for the type requirement
			auto type_req_node = emplace_node<IdentifierNode>(type_name);
			requirements.push_back(type_req_node);

			// Expect ';' after type requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after type requirement in requires expression", current_token_);
			}
			continue;
		}

		if (peek() == "{"_tok) {
			// Compound requirement: { expression } noexcept_opt -> type-constraint_opt ;
			Token lbrace_token = peek_info();
			advance(); // consume '{'

			// Parse the expression - in SFINAE context, failures mean the requirement is not satisfied
			auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (expr_result.is_error()) {
				// In a requires expression, expression failure means the requirement is not satisfied
				// Skip the rest of this compound requirement: } noexcept_opt -> type-constraint_opt ;
				int brace_depth = 1;
				while (brace_depth > 0 && !peek().is_eof()) {
					if (peek() == "{"_tok)
						brace_depth++;
					else if (peek() == "}"_tok)
						brace_depth--;
					if (brace_depth > 0)
						advance();
				}
				if (peek() == "}"_tok)
					advance(); // consume '}'
				// Skip optional noexcept
				if (peek() == "noexcept"_tok)
					advance();
				// Skip optional -> type-constraint
				if (peek() == "->"_tok) {
					advance(); // consume '->'
					// Skip to semicolon
					while (!peek().is_eof() && peek() != ";"_tok)
						advance();
				}
				if (peek() == ";"_tok)
					advance(); // consume ';'

				// Create a false boolean literal to indicate unsatisfied requirement
				Token false_token(Token::Type::Keyword, "false"sv, lbrace_token.line(), lbrace_token.column(), lbrace_token.file_index());
				auto false_node = emplace_node<ExpressionNode>(BoolLiteralNode(false_token, false));
				requirements.push_back(false_node);
				continue;
			}

			// Expect '}'
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after compound requirement expression", current_token_);
			}

			// Check for optional noexcept specifier
			bool is_noexcept = false;
			if (peek() == "noexcept"_tok) {
				advance(); // consume 'noexcept'
				is_noexcept = true;
			}

			// Check for optional return type constraint: -> ConceptName or -> Type
			std::optional<ASTNode> return_type_constraint;
			if (peek() == "->"_tok) {
				advance(); // consume '->'

				// Parse the return type constraint (concept name or type)
				// This can be a concept name (identifier) or a type specifier
				auto type_result = parse_type_specifier();
				if (type_result.is_error()) {
					return type_result;
				}
				return_type_constraint = *type_result.node();
			}

			// Create CompoundRequirementNode
			auto compound_req = emplace_node<CompoundRequirementNode>(
				*expr_result.node(),
				return_type_constraint,
				is_noexcept,
				lbrace_token);
			requirements.push_back(compound_req);

			// Expect ';' after compound requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after compound requirement in requires expression", current_token_);
			}
			continue;
		}

		if (peek().is_keyword() && peek() == "requires"_tok) {
			// Nested requirement: requires constraint;
			Token nested_requires_token = peek_info();
			advance(); // consume 'requires'

			// Parse the nested constraint expression
			auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (constraint_result.is_error()) {
				return constraint_result;
			}

			// Create a RequiresClauseNode for the nested requirement
			auto nested_req = emplace_node<RequiresClauseNode>(
				*constraint_result.node(),
				nested_requires_token);
			requirements.push_back(nested_req);

			// Expect ';' after nested requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after nested requirement in requires expression", current_token_);
			}
			continue;
		}

		// Simple requirement: just an expression
		auto req_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (req_result.is_error()) {
			// In a requires expression, expression failure means the requirement is not satisfied
			// Skip to the next ';' and add a false requirement
			while (!peek().is_eof() && peek() != ";"_tok && peek() != "}"_tok)
				advance();
			if (peek() == ";"_tok)
				advance();

			Token false_token(Token::Type::Keyword, "false"sv, requires_token.line(), requires_token.column(), requires_token.file_index());
			auto false_node = emplace_node<ExpressionNode>(BoolLiteralNode(false_token, false));
			requirements.push_back(false_node);
			continue;
		}
		requirements.push_back(*req_result.node());

		// Expect ';' after each requirement
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after requirement in requires expression", current_token_);
		}
	}

	// Expect '}'
	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' to end requires expression", current_token_);
	}

	// Scope will be exited automatically by scope_guard

	// Create RequiresExpressionNode
	auto requires_expr_node = emplace_node<RequiresExpressionNode>(
		std::move(requirements),
		requires_token);

	return saved_position.success(requires_expr_node);
}

// Parse template parameter list: typename T, int N, ...
