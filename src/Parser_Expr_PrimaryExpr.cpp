// Shared helper: parse operator symbol/name after the 'operator' keyword has been consumed.
// Handles all operator forms: symbols (+, =, <<, etc.), (), [], new/delete, user-defined
// literals, and conversion operators.
// On success returns std::nullopt and sets operator_name_out; on error returns a ParseResult.
std::optional<ParseResult> Parser::parse_operator_name(const Token& operator_keyword_token, std::string_view& operator_name_out) {
	// Check for operator()
	if (peek() == "("_tok) {
		advance(); // consume '('
		if (peek() != ")"_tok) {
			return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
		}
		advance(); // consume ')'
		static constexpr std::string_view operator_call_name = "operator()";
		operator_name_out = operator_call_name;
	}
	// Check for operator[]
	else if (peek() == "["_tok) {
		advance(); // consume '['
		if (peek() != "]"_tok) {
			return ParseResult::error("Expected ']' after 'operator['", operator_keyword_token);
		}
		advance(); // consume ']'
		static constexpr std::string_view operator_subscript_name = "operator[]";
		operator_name_out = operator_subscript_name;
	}
	// Check for operator symbols (+, -, =, ==, +=, <<, etc.)
	else if (!peek().is_eof() && peek_info().type() == Token::Type::Operator) {
		Token operator_symbol_token = peek_info();
		std::string_view operator_symbol = operator_symbol_token.value();
		advance(); // consume operator symbol

		// Build operator name like "operator=" or "operator<<"
		static const std::unordered_map<std::string_view, std::string_view> operator_names = {
			{"=", "operator="},
			{"<=>", "operator<=>"},
			{"<<", "operator<<"},
			{">>", "operator>>"},
			{"+", "operator+"},
			{"-", "operator-"},
			{"*", "operator*"},
			{"/", "operator/"},
			{"%", "operator%"},
			{"&", "operator&"},
			{"|", "operator|"},
			{"^", "operator^"},
			{"~", "operator~"},
			{"!", "operator!"},
			{"<", "operator<"},
			{">", "operator>"},
			{"<=", "operator<="},
			{">=", "operator>="},
			{"==", "operator=="},
			{"!=", "operator!="},
			{"&&", "operator&&"},
			{"||", "operator||"},
			{"++", "operator++"},
			{"--", "operator--"},
			{"->", "operator->"},
			{"->*", "operator->*"},
			{"[]", "operator[]"},
			{",", "operator,"},
			// Compound assignment operators
			{"+=", "operator+="},
			{"-=", "operator-="},
			{"*=", "operator*="},
			{"/=", "operator/="},
			{"%=", "operator%="},
			{"&=", "operator&="},
			{"|=", "operator|="},
			{"^=", "operator^="},
			{"<<=", "operator<<="},
			{">>=", "operator>>="},
		};

		auto it = operator_names.find(operator_symbol);
		if (it != operator_names.end()) {
			operator_name_out = it->second;
		} else {
			return ParseResult::error("Unsupported operator overload: operator" + std::string(operator_symbol), operator_symbol_token);
		}
	}
	// Check for operator new, delete, new[], delete[]
	else if (peek().is_keyword() &&
			 (peek() == "new"_tok || peek() == "delete"_tok)) {
		std::string_view keyword_value = peek_info().value();
		advance(); // consume 'new' or 'delete'

		// Check for array version: new[] or delete[]
		bool is_array = false;
		if (peek() == "["_tok) {
			advance(); // consume '['
			if (peek() == "]"_tok) {
				advance(); // consume ']'
				is_array = true;
			} else {
				return ParseResult::error("Expected ']' after 'operator " + std::string(keyword_value) + "['", operator_keyword_token);
			}
		}

		// Build operator name
		if (keyword_value == "new") {
			static constexpr std::string_view op_new = "operator new";
			static constexpr std::string_view op_new_array = "operator new[]";
			operator_name_out = is_array ? op_new_array : op_new;
		} else {
			static constexpr std::string_view op_delete = "operator delete";
			static constexpr std::string_view op_delete_array = "operator delete[]";
			operator_name_out = is_array ? op_delete_array : op_delete;
		}
	}
	// Check for user-defined literal operator: operator""suffix or operator "" suffix
	else if (peek().is_string_literal()) {
		Token string_token = peek_info();
		advance(); // consume ""

		// Parse the suffix identifier
		if (peek().is_identifier()) {
			std::string_view suffix = peek_info().value();
			advance(); // consume suffix

			StringBuilder op_name_builder;
			operator_name_out = op_name_builder.append("operator\"\"").append(suffix).commit();
		} else {
			return ParseResult::error("Expected identifier suffix after operator\"\"", string_token);
		}
	}
	else {
		// Try to parse conversion operator: operator type()
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type specifier after 'operator' keyword", operator_keyword_token);
		}

		// Now expect "()"
		if (peek() != "("_tok) {
			return ParseResult::error("Expected '(' after conversion operator type", operator_keyword_token);
		}
		advance(); // consume '('

		if (peek() != ")"_tok) {
			return ParseResult::error("Expected ')' after '(' in conversion operator", operator_keyword_token);
		}
		advance(); // consume ')'

		// Create operator name like "operator int" using StringBuilder
		const TypeSpecifierNode& conversion_type_spec = type_result.node()->as<TypeSpecifierNode>();
		StringBuilder op_name_builder;
		op_name_builder.append("operator ");
		op_name_builder.append(conversion_type_spec.getReadableString());
		operator_name_out = op_name_builder.commit();
	}

	return std::nullopt; // success
}

// Shared helper: parse a qualified operator call after the 'operator' keyword has been
// consumed.  Builds the operator name (e.g. "operator=", "operator()"), parses arguments
// if followed by '(', and returns a FunctionCallNode.  `context_token` is used for
// location information in the generated AST nodes.
ParseResult Parser::parse_qualified_operator_call(const Token& context_token, const std::vector<StringType<32>>& namespaces) {
	// Build operator name using the shared helper
	std::string_view op_name;
	if (auto err = parse_operator_name(context_token, op_name)) {
		return std::move(*err);
	}
	Token op_token(Token::Type::Identifier, op_name,
		context_token.line(), context_token.column(), context_token.file_index());
	// Resolve namespace qualification
	NamespaceHandle ns_handle = namespaces.empty()
		? NamespaceRegistry::GLOBAL_NAMESPACE
		: gSymbolTable.resolve_namespace_handle(namespaces);
	// Check for function call
	if (peek() == "("_tok) {
		advance(); // consume '('
		auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
			.handle_pack_expansion = true,
			.collect_types = true,
			.expand_simple_packs = false
		});
		if (!args_result.success) {
			return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
		}
		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after operator call arguments", current_token_);
		}
		auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, op_token);
		auto& op_decl = emplace_node<DeclarationNode>(type_spec, op_token).as<DeclarationNode>();
		auto func_call = FunctionCallNode(op_decl, std::move(args_result.args), op_token);
		if (!namespaces.empty()) {
			std::string_view qualified_name = buildQualifiedNameFromHandle(ns_handle, op_name);
			func_call.set_qualified_name(qualified_name);
		}
		auto result = emplace_node<ExpressionNode>(std::move(func_call));
		return ParseResult::success(result);
	}
	// Not a call — return the operator name as a (qualified) identifier
	if (!namespaces.empty()) {
		auto result = emplace_node<QualifiedIdentifierNode>(ns_handle, op_token);
		return ParseResult::success(result);
	}
	auto result = emplace_node<ExpressionNode>(IdentifierNode(op_token));
	return ParseResult::success(result);
}

ParseResult Parser::parse_primary_expression(ExpressionContext context)
{
	std::optional<ASTNode> result;
	
	// Check for 'typename' keyword in expression context: typename T::type{} or typename T::type()
	// This handles dependent type constructor calls used as function arguments
	// Pattern: typename Result::__invoke_type{} creates a temporary of the dependent type
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "typename") {
		Token typename_token = current_token_;
		advance(); // consume 'typename'
		
		// Parse the dependent type name: T::type or Result::__invoke_type
		// This should be an identifier followed by :: and more identifiers
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected type name after 'typename' keyword", typename_token);
		}
		
		// Build the full qualified type name using StringBuilder
		StringBuilder type_name_sb;
		type_name_sb.append(current_token_.value());
		Token first_type_token = current_token_;
		advance(); // consume first identifier
		
		// Handle template arguments after identifier: typename __promote<_Tp>::__type(0)
		if (current_token_.value() == "<") {
			type_name_sb.append("<");
			advance(); // consume '<'
			
			// Parse template arguments, handling nested template arguments
			int angle_bracket_depth = 1;
			while (!current_token_.kind().is_eof() && angle_bracket_depth > 0) {
				if (current_token_.value() == "<") {
					angle_bracket_depth++;
				} else if (current_token_.value() == ">") {
					angle_bracket_depth--;
					if (angle_bracket_depth == 0) {
						type_name_sb.append(">");
						advance(); // consume final '>'
						break;
					}
				}
				type_name_sb.append(current_token_.value());
				advance();
			}
		}
		
		// Parse :: and subsequent identifiers (with optional template args)
		while (current_token_.value() == "::") {
			type_name_sb.append("::");
			advance(); // consume '::'
			
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected identifier after '::' in typename", typename_token);
			}
			type_name_sb.append(current_token_.value());
			advance(); // consume identifier
			
			// Handle template arguments after the identifier
			if (current_token_.value() == "<") {
				type_name_sb.append("<");
				advance(); // consume '<'
				
				// Parse template arguments, handling nested template arguments
				int angle_bracket_depth = 1;
				while (!current_token_.kind().is_eof() && angle_bracket_depth > 0) {
					if (current_token_.value() == "<") {
						angle_bracket_depth++;
					} else if (current_token_.value() == ">") {
						angle_bracket_depth--;
						if (angle_bracket_depth == 0) {
							type_name_sb.append(">");
							advance(); // consume final '>'
							break;
						}
					}
					type_name_sb.append(current_token_.value());
					advance();
				}
			}
		}
		
		// Now we should have either '{}' (brace init) or '()' (paren init)
		ChunkedVector<ASTNode> args;
		Token init_token = typename_token;
		
		if (current_token_.value() == "{") {
			init_token = current_token_;
			advance(); // consume '{'
			
			// Parse brace initializer arguments
			while (current_token_.value() != "}") {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					type_name_sb.reset(); // Must reset before early return
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}
				
				if (current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
					type_name_sb.reset(); // Must reset before early return
					return ParseResult::error("Expected ',' or '}' in brace initializer", typename_token);
				}
			}
			
			if (!consume("}"_tok)) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected '}' after brace initializer", typename_token);
			}
		} else if (current_token_.value() == "(") {
			init_token = current_token_;
			advance(); // consume '('
			
			// Parse parenthesized arguments
			while (current_token_.value() != ")") {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					type_name_sb.reset(); // Must reset before early return
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}
				
				if (current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.value() != ")") {
					type_name_sb.reset(); // Must reset before early return
					return ParseResult::error("Expected ',' or ')' in constructor call", typename_token);
				}
			}
			
			if (!consume(")"_tok)) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected ')' after constructor arguments", typename_token);
			}
		} else {
			type_name_sb.reset(); // Must reset before early return
			return ParseResult::error("Expected '{' or '(' after typename type expression", typename_token);
		}
		
		// Create a TypeSpecifierNode for the dependent type
		// Store the full type name so it can be resolved during template instantiation
		std::string_view interned_type_name = StringTable::getOrInternStringHandle(type_name_sb.commit()).view();
		Token type_token(Token::Type::Identifier, interned_type_name, 
		                 first_type_token.line(), first_type_token.column(), first_type_token.file_index());
		
		// Create a dependent/placeholder type (Type::UserDefined with special marker)
		auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, type_token);
		
		// Create ConstructorCallNode with the dependent type
		result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), init_token));
		return ParseResult::success(*result);
	}
	
	// Check for functional-style cast with keyword type names: bool(x), int(x), etc.
	// This must come early because these are keywords, not identifiers
	if (current_token_.type() == Token::Type::Keyword) {
		std::string_view kw = current_token_.value();
		bool is_builtin_type = (kw == "bool" || kw == "char" || kw == "int" || 
		                        kw == "short" || kw == "long" || kw == "float" || 
		                        kw == "double" || kw == "void" || kw == "wchar_t" ||
		                        kw == "char8_t" || kw == "char16_t" || kw == "char32_t" ||
		                        kw == "signed" || kw == "unsigned");
		
		if (is_builtin_type) {
			Token type_token = current_token_;
			std::string_view type_kw = current_token_.value();
			advance(); // consume the type keyword
			
			// Check if followed by '(' for functional cast
			if (current_token_.value() == "(") {
				ParseResult cast_result = parse_functional_cast(type_kw, type_token);
				if (!cast_result.is_error() && cast_result.node().has_value()) {
					return cast_result;
				}
			} else {
				// Not a functional cast - restore and continue with normal keyword handling
				// Actually, we can't easily restore here. This is a problem.
				// For now, return an error
				return ParseResult::error("Unexpected keyword in expression context", type_token);
			}
		}
	}

	// Check for 'operator' keyword in expression context: operator==(other), operator+=(x), etc.
	// This is used to call operators as member functions by name, e.g., return !operator==(other);
	// This pattern is common in standard library headers like <typeinfo>
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "operator") {
		Token operator_keyword_token = current_token_;
		advance(); // consume 'operator'

		std::string operator_name = "operator";

		// Check for operator() - function call operator
		if (current_token_.type() == Token::Type::Punctuator &&
		    current_token_.value() == "(") {
			advance(); // consume '('
			if (current_token_.kind().is_eof() || current_token_.value() != ")") {
				return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
			}
			advance(); // consume ')'
			operator_name = "operator()";
		}
		// Check for operator[] - subscript operator
		else if (current_token_.type() == Token::Type::Punctuator &&
		         current_token_.value() == "[") {
			advance(); // consume '['
			if (current_token_.kind().is_eof() || current_token_.value() != "]") {
				return ParseResult::error("Expected ']' after 'operator['", operator_keyword_token);
			}
			advance(); // consume ']'
			operator_name = "operator[]";
		}
		// Check for other operators
		else if (current_token_.type() == Token::Type::Operator) {
			std::string_view operator_symbol = current_token_.value();
			advance(); // consume operator symbol
			operator_name += std::string(operator_symbol);
		}
		else {
			return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
		}

		// Now expect '(' and arguments
		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after operator name in expression", operator_keyword_token);
		}

		// Parse arguments
		ChunkedVector<ASTNode> args;
		if (current_token_.value() != ")") {
			while (true) {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}

				if (current_token_.kind().is_eof()) {
					return ParseResult::error("Expected ',' or ')' in operator call", operator_keyword_token);
				}

				if (current_token_.value() == ")") {
					break;
				}

				if (!consume(","_tok)) {
					return ParseResult::error("Expected ',' between operator call arguments", operator_keyword_token);
				}
			}
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after operator call arguments", operator_keyword_token);
		}

		// Create a token with the full operator name for the identifier
		Token operator_name_token(Token::Type::Identifier, StringBuilder().append(operator_name).commit(), 
		                          operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());

		// Check if we're inside a member function context
		if (!member_function_context_stack_.empty()) {
			// Inside a member function - this is a member operator call
			// Create this->operator_name(args) pattern
			// First create 'this' identifier
			Token this_token(Token::Type::Keyword, "this"sv, 
			                operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());
			auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));

			// Look up the operator function in the current struct type
			const auto& member_ctx = member_function_context_stack_.back();
			if (member_ctx.struct_type_index < gTypeInfo.size()) {
				TypeInfo& type_info = gTypeInfo[member_ctx.struct_type_index];
				if (type_info.struct_info_) {
					// Search for the operator member function
					for (auto& member_func : type_info.struct_info_->member_functions) {
						if (StringTable::getStringView(member_func.name) == operator_name) {
							// Found the operator function - check if it's a FunctionDeclarationNode
							if (member_func.function_decl.is<FunctionDeclarationNode>()) {
								FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
								result = emplace_node<ExpressionNode>(
									MemberFunctionCallNode(this_node, func_decl, std::move(args), operator_name_token));
								return ParseResult::success(*result);
							}
						}
					}
				}
			}

			// If we couldn't find the operator in the current type, create a generic member access + call
			// This handles cases where the operator might be inherited or template-dependent
			// Look up the function in symbol table as fallback
			auto func_lookup = gSymbolTable.lookup(operator_name);
			if (func_lookup.has_value() && func_lookup->is<FunctionDeclarationNode>()) {
				auto& func_decl = func_lookup->as<FunctionDeclarationNode>();
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(this_node, func_decl, std::move(args), operator_name_token));
				return ParseResult::success(*result);
			}

			// Create a deferred function call for template contexts
			// We create a MemberAccessNode followed by postfix call handling
			// The codegen will handle this as this->operator_name(args)
			auto member_access = emplace_node<ExpressionNode>(
				MemberAccessNode(this_node, operator_name_token, true)); // true = arrow access
			
			// Create a placeholder type spec and decl for the deferred call
			auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, operator_name_token);
			auto& operator_decl = emplace_node<DeclarationNode>(type_spec, operator_name_token).as<DeclarationNode>();
			auto& func_decl_node = emplace_node<FunctionDeclarationNode>(operator_decl).as<FunctionDeclarationNode>();
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(this_node, func_decl_node, std::move(args), operator_name_token));
			return ParseResult::success(*result);
		}
		else {
			// Not in a member function context - create a free-standing operator call
			// This is valid C++ for calling operator as a function, commonly used in requires expressions
			// e.g., operator<=>(a, b) or requires { operator<=>(t, u); }
			
			// Look up the operator as a free function
			auto func_lookup = gSymbolTable.lookup(operator_name);
			if (func_lookup.has_value() && func_lookup->is<FunctionDeclarationNode>()) {
				auto& func_decl = func_lookup->as<FunctionDeclarationNode>();
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(func_decl.decl_node(), std::move(args), operator_name_token));
				return ParseResult::success(*result);
			}
			
			// Operator function not found - create a deferred call that will be resolved at instantiation
			// This is common in template/requires contexts where the operator is dependent
			auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, operator_name_token);
			auto& operator_decl = emplace_node<DeclarationNode>(type_spec, operator_name_token).as<DeclarationNode>();
			result = emplace_node<ExpressionNode>(
				FunctionCallNode(operator_decl, std::move(args), operator_name_token));
			return ParseResult::success(*result);
		}
	}

	// Check for requires expression: requires(params) { requirements; } or requires { requirements; }
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "requires") {
		ParseResult requires_result = parse_requires_expression();
		if (requires_result.is_error()) {
			return requires_result;
		}
		result = requires_result.node();
		// Don't return here - continue to handle potential postfix operators
	}
	// Check for lambda expression first (starts with '[')
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "[") {
		ParseResult lambda_result = parse_lambda_expression();
		if (lambda_result.is_error()) {
			return lambda_result;
		}
		result = lambda_result.node();
		// Don't return here - continue to postfix operator handling
		// This allows immediately invoked lambdas: []() { ... }()
	}
	// Check for offsetof builtin first (before general identifier handling)
	else if (current_token_.type() == Token::Type::Identifier && current_token_.value() == "offsetof") {
		// Handle offsetof builtin: offsetof(struct_type, member)
		Token offsetof_token = current_token_;
		advance(); // consume 'offsetof'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after 'offsetof'", current_token_);
		}

		// Parse the struct type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected struct type in offsetof", current_token_);
		}

		if (!consume(","_tok)) {
			return ParseResult::error("Expected ',' after struct type in offsetof", current_token_);
		}

		// Parse the member name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected member name in offsetof", current_token_);
		}
		Token member_name = peek_info();
		advance(); // consume member name

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after offsetof arguments", current_token_);
		}

		result = emplace_node<ExpressionNode>(
			OffsetofExprNode(*type_result.node(), member_name, offsetof_token));
	}
	// Check for type trait intrinsics: __is_void(T), __is_integral(T), __has_unique_object_representations(T), etc.
	// Also support GCC/Clang __builtin_ prefix variants (e.g., __builtin_is_constant_evaluated)
	// But exclude regular builtin functions like __builtin_labs, __builtin_abs, etc.
	// IMPORTANT: Only treat as type trait intrinsic if followed by '(' - if followed by '<', it's a
	// template class name (e.g., __is_swappable<T> from the standard library)
	// ALSO: Skip if this identifier is already registered as a function template in the template registry
	// (e.g., __is_complete_or_unbounded is a library function template, not a compiler intrinsic)
	else if (current_token_.type() == Token::Type::Identifier && 
	         (current_token_.value().starts_with("__is_") || 
	          current_token_.value().starts_with("__has_") ||
	          (current_token_.value().starts_with("__builtin_") && 
	           (current_token_.value().starts_with("__builtin_is_") || 
	            current_token_.value().starts_with("__builtin_has_")))) &&
	         // Only parse as intrinsic if NEXT token is '(' - otherwise it's a template class name
	         peek(1) == "("_tok &&
	         // Only parse as intrinsic if the name is a KNOWN type trait.
	         // This prevents regular functions like __is_single_threaded() from being misidentified.
	         is_known_type_trait_name(current_token_.value())) {
		// Check if this is actually a declared function template (library function, not intrinsic)
		// If so, skip this branch and let it fall through to normal function call parsing
		std::string_view trait_name = current_token_.value();
		
		bool is_declared_template = gTemplateRegistry.lookupTemplate(trait_name).has_value();
		
		// Also check namespace-qualified name if in namespace
		if (!is_declared_template) {
			NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
			if (!current_namespace_handle.isGlobal()) {
				StringHandle trait_name_handle = StringTable::getOrInternStringHandle(trait_name);
				StringHandle qualified_name_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, trait_name_handle);
				is_declared_template = gTemplateRegistry.lookupTemplate(StringTable::getStringView(qualified_name_handle)).has_value();
			}
		}
		
		if (!is_declared_template) {
			// Parse type trait intrinsics
			Token trait_token = current_token_;
			advance(); // consume the trait name

			auto it = trait_map.find(normalize_trait_name(trait_name));
			if (it == trait_map.end()) {
				// Unknown type trait intrinsic - this shouldn't happen since we only reach here
				// if followed by '(' which means it was intended as a type trait call
				return ParseResult::error("Unknown type trait intrinsic", trait_token);
			}

			TypeTraitKind kind = it->second.kind;
			bool is_binary_trait = it->second.is_binary;
			bool is_variadic_trait = it->second.is_variadic;
			bool is_no_arg_trait = it->second.is_no_arg;

			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after type trait intrinsic", current_token_);
			}

			if (is_no_arg_trait) {
				// No-argument trait like __is_constant_evaluated()
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' for no-argument type trait", current_token_);
				}

				result = emplace_node<ExpressionNode>(
					TypeTraitExprNode(kind, trait_token));
			} else {
				// Parse the first type argument
				ParseResult type_result = parse_type_specifier();
				if (type_result.is_error() || !type_result.node().has_value()) {
					return ParseResult::error("Expected type in type trait intrinsic", current_token_);
				}

				// Parse pointer/reference modifiers after the base type (ptr-operator in C++20 grammar)
				// e.g., int* or int&& in type trait arguments
				TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(type_spec);

				// Parse array specifications ([N] or [])
				if (peek() == "["_tok) {
					advance();  // consume '['
					
					// Check for array size expression or empty brackets
					std::optional<size_t> array_size_val;
					if (!peek().is_eof() && peek() != "]"_tok) {
						// Parse array size expression
						ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (size_result.is_error()) {
							return ParseResult::error("Expected array size expression", current_token_);
						}
						
						// Try to evaluate the array size as a constant expression
						if (size_result.node().has_value()) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
							if (eval_result.success()) {
								array_size_val = static_cast<size_t>(eval_result.as_int());
							}
						}
					}
					
					if (!consume("]"_tok)) {
						return ParseResult::error("Expected ']' after array size", current_token_);
					}
					
					type_spec.set_array(true, array_size_val);
				}

				// Check for pack expansion (...) after the first type argument
				if (peek() == "..."_tok) {
					advance();  // consume '...'
					type_spec.set_pack_expansion(true);
				}

				if (is_variadic_trait) {
					// Variadic trait: parse comma-separated additional types
					std::vector<ASTNode> additional_types;
					while (peek() == ","_tok) {
						consume(","_tok);
						ParseResult arg_type_result = parse_type_specifier();
						if (arg_type_result.is_error() || !arg_type_result.node().has_value()) {
							return ParseResult::error("Expected type argument in variadic type trait", current_token_);
						}
						
						// Parse pointer/reference modifiers for additional type arguments (ptr-operator in C++20 grammar)
						TypeSpecifierNode& arg_type_spec = arg_type_result.node()->as<TypeSpecifierNode>();
						consume_pointer_ref_modifiers(arg_type_spec);
						
						// Parse array specifications ([N] or []) for variadic trait additional args
						std::optional<size_t> array_size_val;
						if (peek() == "["_tok) {
							advance();  // consume '['
							
							if (!peek().is_eof() && peek() != "]"_tok) {
								ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
								if (size_result.is_error()) {
									return ParseResult::error("Expected array size expression", current_token_);
								}
								if (size_result.node().has_value()) {
									ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
									auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
									if (eval_result.success()) {
										array_size_val = static_cast<size_t>(eval_result.as_int());
									}
								}
							}
							
							if (!consume("]"_tok)) {
								return ParseResult::error("Expected ']' after array size", current_token_);
							}
							
							arg_type_spec.set_array(true, array_size_val);
						}

						// Check for pack expansion (...) after the type argument
						if (peek() == "..."_tok) {
							advance();  // consume '...'
							arg_type_spec.set_pack_expansion(true);
						}
						
						additional_types.push_back(*arg_type_result.node());
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait arguments", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), std::move(additional_types), trait_token));
				} else if (is_binary_trait) {
					// Binary trait: parse comma and second type
					if (!consume(","_tok)) {
						return ParseResult::error("Expected ',' after first type in binary type trait", current_token_);
					}

					ParseResult second_type_result = parse_type_specifier();
					if (second_type_result.is_error() || !second_type_result.node().has_value()) {
						return ParseResult::error("Expected second type in binary type trait", current_token_);
					}

					// Parse pointer/reference modifiers for second type (ptr-operator in C++20 grammar)
					TypeSpecifierNode& second_type_spec = second_type_result.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(second_type_spec);

					// Parse array specifications ([N] or []) for binary trait second type
					std::optional<size_t> array_size_val;
					if (peek() == "["_tok) {
						advance();  // consume '['
						
						if (!peek().is_eof() && peek() != "]"_tok) {
							ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (size_result.is_error()) {
								return ParseResult::error("Expected array size expression", current_token_);
							}
							if (size_result.node().has_value()) {
								ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
								auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
								if (eval_result.success()) {
									array_size_val = static_cast<size_t>(eval_result.as_int());
								}
							}
						}
						
						if (!consume("]"_tok)) {
							return ParseResult::error("Expected ']' after array size", current_token_);
						}
						
						second_type_spec.set_array(true, array_size_val);
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait arguments", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), *second_type_result.node(), trait_token));
				} else {
					// Unary trait: just close paren
					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait argument", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), trait_token));
				}
			}
		} // end if (!is_declared_template)
	}
	// Check for global namespace scope operator :: at the beginning
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "::") {
		advance(); // consume ::

		// Handle ::operator new(...) and ::operator delete(...) as function call expressions
		// Used by libstdc++ allocators: static_cast<_Tp*>(::operator new(__n * sizeof(_Tp)))
		if (current_token_.type() == Token::Type::Keyword &&
		    current_token_.value() == "operator") {
			Token operator_token = current_token_;
			advance(); // consume 'operator'

			// Expect 'new' or 'delete'
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Keyword ||
			    (current_token_.value() != "new" && current_token_.value() != "delete")) {
				return ParseResult::error("Expected 'new' or 'delete' after '::operator'", current_token_);
			}

			StringBuilder op_name_sb;
			op_name_sb.append("operator ");
			op_name_sb.append(current_token_.value());
			advance(); // consume 'new' or 'delete'

			// Check for array variant: operator new[] or operator delete[]
			if (current_token_.value() == "[") {
				advance(); // consume '['
				if (current_token_.value() == "]") {
					advance(); // consume ']'
					op_name_sb.append("[]");
				}
			}

			std::string_view op_name = op_name_sb.commit();
			Token op_identifier(Token::Type::Identifier, op_name,
			                    operator_token.line(), operator_token.column(), operator_token.file_index());

			// Expect '(' for function call
			if (current_token_.kind().is_eof() || current_token_.value() != "(") {
				return ParseResult::error("Expected '(' after '::operator new/delete'", current_token_);
			}
			advance(); // consume '('

			// Parse function arguments
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after operator new/delete arguments", current_token_);
			}

			// Create a forward declaration for the operator (returns void* for new, void for delete)
			bool is_new = op_name.find("new") != std::string_view::npos;
			auto type_node = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0, Token());
			if (is_new) {
				type_node.as<TypeSpecifierNode>().add_pointer_level(); // void* return type for new
			}
			auto forward_decl = emplace_node<DeclarationNode>(type_node, op_identifier);
			DeclarationNode& decl_ref = forward_decl.as<DeclarationNode>();

			auto call_node = emplace_node<ExpressionNode>(
				FunctionCallNode(decl_ref, std::move(args_result.args), op_identifier));
			return ParseResult::success(call_node);
		}

		// Expect an identifier after ::
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier after '::'", current_token_);
		}

		Token first_identifier = current_token_;
		advance(); // consume identifier

		// Helper to get DeclarationNode from either DeclarationNode, FunctionDeclarationNode, VariableDeclarationNode, or TemplateFunctionDeclarationNode
		auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
			if (node.is<DeclarationNode>()) {
				return &node.as<DeclarationNode>();
			} else if (node.is<FunctionDeclarationNode>()) {
				return &node.as<FunctionDeclarationNode>().decl_node();
			} else if (node.is<VariableDeclarationNode>()) {
				return &node.as<VariableDeclarationNode>().declaration();
			} else if (node.is<TemplateFunctionDeclarationNode>()) {
				// Handle template function declarations - extract the inner function declaration
				return &node.as<TemplateFunctionDeclarationNode>().function_declaration().as<FunctionDeclarationNode>().decl_node();
			}
			return nullptr;
		};

		// Check if there are more :: following (e.g., ::ns::func)
		std::vector<StringType<32>> namespaces;
		Token final_identifier = first_identifier;

		while (current_token_.value() == "::"sv) {
			// Current identifier is a namespace part
			namespaces.emplace_back(StringType<32>(final_identifier.value()));
			advance(); // consume ::

			// Get next identifier
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after '::'", current_token_);
			}
			final_identifier = current_token_;
			advance(); // consume the identifier
		}

		// Create a QualifiedIdentifierNode with namespace handle
		// If namespaces is empty, it means ::identifier (global namespace)
		// If namespaces is not empty, it means ::ns::identifier
		// force_global=true because :: prefix means resolve from global namespace
		NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces, /*force_global=*/true);
		auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
		const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

		// Try to look up the qualified identifier
		// For global namespace (empty namespaces), lookup_qualified handles it correctly
		// by looking in the global namespace (namespace_symbols_[empty_path])
		std::optional<ASTNode> identifierType;
		// Always use lookup_symbol_qualified - it handles both cases:
		// - Global namespace (handle index 0) -> looks in global namespace only
		// - Non-global namespace -> looks in specified namespace
		identifierType = lookup_symbol_qualified(qual_id.namespace_handle(), qual_id.name());

		// Check if followed by '(' for function call
		if (current_token_.value() == "(") {
			advance(); // consume '('

			// Parse function arguments using unified helper (collect types for template deduction)
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// If not found and we're not in extern "C", try template instantiation
			if (!identifierType && current_linkage_ != Linkage::C) {
				// Build qualified template name (e.g., "::move" or "::std::move")
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, qual_id.name());
				
				// Apply lvalue reference for forwarding deduction on arg_types
				std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
				
				// Try to instantiate the qualified template function
				if (!arg_types.empty()) {
					std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
					if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *template_inst;
						FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template: ", qualified_name);
					}
				}
			}
			
			// If still not found, create a forward declaration
			if (!identifierType) {
				// Validate namespace exists before creating forward declaration (catches f2::func when f2 undeclared)
				if (!validateQualifiedNamespace(qual_id.namespace_handle(), qual_id.identifier_token(), parsing_template_body_)) {
					return ParseResult::error(
						std::string(StringBuilder().append("Use of undeclared identifier '")
							.append(buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name()))
							.append("'").commit()),
						qual_id.identifier_token());
				}
				auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
				auto forward_decl = emplace_node<DeclarationNode>(type_node, qual_id.identifier_token());
				identifierType = forward_decl;
			}

			// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
			const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
			if (!decl_ptr) {
				return ParseResult::error("Invalid function declaration (global namespace path)", qual_id.identifier_token());
			}

			// Create function call node with the qualified identifier
			auto function_call_node = emplace_node<ExpressionNode>(
				FunctionCallNode(*decl_ptr, std::move(args), qual_id.identifier_token()));
			// If the function has a pre-computed mangled name, set it on the FunctionCallNode
			if (identifierType->is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
				FLASH_LOG(Parser, Debug, "Qualified function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
				if (func_decl.has_mangled_name()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
					FLASH_LOG(Parser, Debug, "Set mangled name on qualified FunctionCallNode: {}", func_decl.mangled_name());
				}
			}
			result = function_call_node;
		} else {
			// Just a qualified identifier reference (e.g., ::globalValue)
			result = emplace_node<ExpressionNode>(qual_id);
		}

		if (result.has_value())
			return ParseResult::success(*result);
	}
	else if (current_token_.type() == Token::Type::Identifier) {
		Token idenfifier_token = current_token_;

		// Check for __func__, __PRETTY_FUNCTION__ (compiler builtins)
		if (idenfifier_token.value() == "__func__"sv ||
		    idenfifier_token.value() == "__PRETTY_FUNCTION__"sv) {

			if (!current_function_) {
				return ParseResult::error(
					std::string(idenfifier_token.value()) + " can only be used inside a function",
					idenfifier_token);
			}

			// Create a string literal with the function name or signature
			// For __PRETTY_FUNCTION__, use the full signature; for others, use simple name
			std::string_view persistent_name;
			if (idenfifier_token.value() == "__PRETTY_FUNCTION__"sv) {
				persistent_name = context_.storeFunctionNameLiteral(buildPrettyFunctionSignature(*current_function_));
			} else {
				// For __func__, just use the simple function name
				persistent_name = current_function_->decl_node().identifier_token().value();
			}

			// Store the function name string in CompileContext so it persists
			// Note: Unlike string literals from source code (which include quotes in the token),
			// __func__/__PRETTY_FUNCTION__ are predefined identifiers that expand
			// to the string content directly, without quotes. This matches MSVC/GCC/Clang behavior.
			Token string_token(Token::Type::StringLiteral,
			                   persistent_name,
			                   idenfifier_token.line(),
			                   idenfifier_token.column(),
			                   idenfifier_token.file_index());

			result = emplace_node<ExpressionNode>(StringLiteralNode(string_token));
			advance();

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Check if this is a qualified identifier (namespace::identifier)
		// Helper to get DeclarationNode from either DeclarationNode, FunctionDeclarationNode, or VariableDeclarationNode
		auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
			if (node.is<DeclarationNode>()) {
				return &node.as<DeclarationNode>();
			} else if (node.is<FunctionDeclarationNode>()) {
				return &node.as<FunctionDeclarationNode>().decl_node();
			} else if (node.is<VariableDeclarationNode>()) {
				return &node.as<VariableDeclarationNode>().declaration();
			} else if (node.is<TemplateFunctionDeclarationNode>()) {
				// Handle template function declarations - extract the inner function declaration
				return &node.as<TemplateFunctionDeclarationNode>().function_declaration().as<FunctionDeclarationNode>().decl_node();
			}
			return nullptr;
		};

		// We need to consume the identifier first to check what comes after it
		advance();
		
		// Check for functional-style cast: Type(expression)
		// This is needed for patterns like bool(x), int(y), etc.
		// Check if this identifier is a type name and followed by '('
		// NOTE: Only treat BUILT-IN types as functional casts here.
		// User-defined types with Type(args) syntax are constructor calls, not casts,
		// and should be handled by the normal identifier/function call path below.
		if (current_token_.value() == "(" &&
		    !current_token_.value().starts_with("::")) {
			std::string_view id_name = idenfifier_token.value();
			
			// Only check for built-in type names (not user-defined types)
			// User-defined Type(args) is a constructor call, not a functional cast
			auto type_info = get_builtin_type_info(id_name);
			if (type_info.has_value()) {
				// This is a built-in type followed by '(' - parse as functional cast
				ParseResult cast_result = parse_functional_cast(id_name, idenfifier_token);
				if (!cast_result.is_error() && cast_result.node().has_value()) {
					return cast_result;
				}
			}
		}
		
		if (current_token_.value() == "::"sv) {
			// Build the qualified identifier manually
			std::vector<StringType<32>> namespaces;
			Token final_identifier = idenfifier_token;

			// Collect namespace parts
			while (current_token_.value() == "::"sv) {
				// Current identifier is a namespace part
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::

				// Handle qualified operator call: Type::operator=()
				if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "operator") {
					advance(); // consume 'operator' — now peek() is the operator symbol
					return parse_qualified_operator_call(final_identifier, namespaces);
				}

				// Get next identifier
				if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
					return ParseResult::error("Expected identifier after '::'", current_token_);
				}
				final_identifier = current_token_;
				advance(); // consume the identifier to check for the next ::
			}

		// current_token_ is now the token after the final identifier

		// Create a QualifiedIdentifierNode
		NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
		auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
		const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

		// Check for std::forward intrinsic
		// std::forward<T>(arg) is a compiler intrinsic for perfect forwarding
		// Check if namespace is "std" (single-level namespace with name "std")
		std::string_view ns_qualified_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
		if (ns_qualified_name == "std" && qual_id.name() == "forward") {
			
			// Handle std::forward<T>(arg)
			// For now, we'll treat it as an identity function that preserves references
			// Skip template arguments if present
			if (current_token_.value() == "<") {
				// Skip template arguments: <T> or <iter_reference_t<_It>>
				int angle_bracket_depth = 1;
				advance(); // consume <
				
				while (angle_bracket_depth > 0 && !current_token_.kind().is_eof()) {
					if (current_token_.value() == "<") angle_bracket_depth++;
					else if (current_token_.value() == ">") angle_bracket_depth--;
					else if (current_token_.value() == ">>") angle_bracket_depth -= 2;
					advance();
				}
			}
			
			// Now expect (arg)
			if (current_token_.kind().is_eof() || current_token_.value() != "(") {
				return ParseResult::error("Expected '(' after std::forward", final_identifier);
			}
			advance(); // consume '('
			
			// Parse the single argument
			auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (arg_result.is_error()) {
				return arg_result;
			}
			
			if (current_token_.kind().is_eof() || current_token_.value() != ")") {
				return ParseResult::error("Expected ')' after std::forward argument", current_token_);
			}
			advance(); // consume ')'
			
			// std::forward<T>(arg) is essentially an identity function
			// Just return the argument expression itself
			// The type system already preserves the reference type
			result = arg_result.node();
			return ParseResult::success(*result);
		}

		// Check if qualified identifier is followed by template arguments: ns::Template<Args>
		// This must come BEFORE we try to use current_token_ as an operator
		// Phase 1: C++20 Template Argument Disambiguation - try to parse template arguments
		// after qualified identifiers, BUT check if the member is actually a template first
		// to avoid misinterpreting comparisons like _R1::num < _R2::num
		std::optional<std::vector<TemplateTypeArg>> template_args;
		std::vector<ASTNode> template_arg_nodes;  // Store the actual expression nodes
		if (current_token_.value() == "<") {
			// Build the qualified name from namespace handle
			std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
			std::string_view member_name = qual_id.name();
			
			// Check if the member is a known template before parsing < as template arguments
			// This prevents misinterpreting patterns like _R1::num < _R2::num> where < is comparison
			auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
			auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
			auto member_alias_template_opt = gTemplateRegistry.lookup_alias_template(member_name);
			auto full_template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
			auto full_var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
			auto full_alias_template_opt = gTemplateRegistry.lookup_alias_template(qualified_name);
			
			bool is_known_template = member_template_opt.has_value() || member_var_template_opt.has_value() ||
			                         member_alias_template_opt.has_value() ||
			                         full_template_opt.has_value() || full_var_template_opt.has_value() ||
			                         full_alias_template_opt.has_value();
			
			// Also check if the base is a template parameter - if so, the member is likely NOT a template
			bool base_is_template_param = false;
			if (!qual_id.namespace_handle().isGlobal()) {
				std::string_view base_name = gNamespaceRegistry.getRootNamespaceName(qual_id.namespace_handle());
				for (const auto& param_name : current_template_param_names_) {
					if (StringTable::getStringView(param_name) == base_name) {
						base_is_template_param = true;
						break;
					}
				}
			}
			
			// Decide whether to parse template arguments
			bool should_parse_template_args = true;
			if (!is_known_template && (context == ExpressionContext::TemplateArgument || base_is_template_param)) {
				// Member is NOT a known template and we're in a context where < is likely comparison
				FLASH_LOG_FORMAT(Parser, Debug, 
				    "Qualified identifier '{}' member is not a known template - treating '<' as comparison operator (context={}, base_is_param={})",
				    qualified_name, static_cast<int>(context), base_is_template_param);
				should_parse_template_args = false;
			}
			
			if (should_parse_template_args) {
				FLASH_LOG_FORMAT(Parser, Debug, "Qualified identifier '{}' followed by '<', attempting template argument parsing", qualified_name);
				template_args = parse_explicit_template_arguments(&template_arg_nodes);
			}
			
			if (template_args.has_value()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Successfully parsed {} template arguments for '{}'", template_args->size(), qualified_name);
				
				// First, check if this is a variable template (most common case for traits like is_reference_v<T>)
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
				if (!var_template_opt.has_value()) {
					// Try with simple name
					var_template_opt = gTemplateRegistry.lookupVariableTemplate(qual_id.name());
				}
				
				// If still not found, check if the base is a struct/class name (not a namespace)
				// For patterns like StructName::member_template<Args>, we need to build the qualified name manually
				std::string_view struct_qualified_name;
				if (!var_template_opt.has_value() && !namespaces.empty()) {
					// Build struct-qualified name: "StructName::member"
					struct_qualified_name = buildQualifiedNameFromStrings(namespaces, qual_id.name());
					
					FLASH_LOG_FORMAT(Templates, Debug, "Trying struct-qualified variable template lookup: '{}'", struct_qualified_name);
					var_template_opt = gTemplateRegistry.lookupVariableTemplate(struct_qualified_name);
					if (var_template_opt.has_value()) {
						FLASH_LOG(Templates, Debug, "Found variable template with struct-qualified name!");
					} else {
						FLASH_LOG(Templates, Debug, "Variable template NOT found with struct-qualified name");
					}
				}
				
				if (var_template_opt.has_value()) {
					// Determine which name to use for instantiation
					std::string_view template_name_for_instantiation = qualified_name;
					if (!struct_qualified_name.empty()) {
						template_name_for_instantiation = struct_qualified_name;
					}
					
					FLASH_LOG(Templates, Debug, "Found variable template, instantiating: ", template_name_for_instantiation);
					// Try instantiation with determined name first, fall back to simple name
					auto instantiated_var = try_instantiate_variable_template(template_name_for_instantiation, *template_args);
					if (!instantiated_var.has_value()) {
						instantiated_var = try_instantiate_variable_template(qual_id.name(), *template_args);
					}
					if (instantiated_var.has_value()) {
						// Get the instantiated variable name
						std::string_view inst_name;
						if (instantiated_var->is<VariableDeclarationNode>()) {
							const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
							const auto& decl = var_decl.declaration();
							inst_name = decl.identifier_token().value();
						} else if (instantiated_var->is<DeclarationNode>()) {
							const auto& decl = instantiated_var->as<DeclarationNode>();
							inst_name = decl.identifier_token().value();
						} else {
							inst_name = qualified_name;  // Fallback
						}
						
						FLASH_LOG(Templates, Debug, "Successfully instantiated variable template: ", qualified_name);
						
						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name, 
						                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						return ParseResult::success(*result);
					}
				}
				
				// Check if this is a concept application (e.g., std::same_as<T, U>)
				// Concepts evaluate to boolean values at compile time
				auto concept_opt = gConceptRegistry.lookupConcept(qualified_name);
				if (!concept_opt.has_value()) {
					// Try with simple name
					concept_opt = gConceptRegistry.lookupConcept(qual_id.name());
				}
				
				if (concept_opt.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with template arguments (qualified lookup)", qualified_name);
					
					// Evaluate the concept constraint with the provided template arguments
					auto constraint_result = evaluateConstraint(
						concept_opt->as<ConceptDeclarationNode>().constraint_expr(),
						*template_args,
						{}  // No template param names needed for concrete types
					);
					
					// Create a BoolLiteralNode with the result
					bool concept_satisfied = constraint_result.satisfied;
					Token bool_token(Token::Type::Keyword, concept_satisfied ? "true"sv : "false"sv,
					                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
					result = emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, concept_satisfied));
					return ParseResult::success(*result);
				}
				
				// Check if this is an alias template (like detail::cref<int> -> int)
				// Alias templates should resolve to their underlying type
				auto alias_opt = gTemplateRegistry.lookup_alias_template(qualified_name);
				if (!alias_opt.has_value()) {
					// Try with simple name
					alias_opt = gTemplateRegistry.lookup_alias_template(qual_id.name());
				}
				
				if (alias_opt.has_value()) {
					FLASH_LOG(Templates, Debug, "Found alias template, resolving: ", qualified_name);
					const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
					
					// Get the target type of the alias
					// For a simple alias like `template<typename T> using cref = T;`, the target type is T
					// We need to substitute the template parameter with the actual argument
					const TypeSpecifierNode& target_type = alias_node.target_type_node();
					const auto& param_names = alias_node.template_param_names();
					
					// Check if the target type is one of the template parameters
					Token target_token = target_type.token();
					if (target_token.type() == Token::Type::Identifier) {
						std::string_view target_name = target_token.value();
						for (size_t i = 0; i < param_names.size() && i < template_args->size(); ++i) {
							if (target_name == param_names[i].view()) {
								// The target type is the i-th template parameter
								// Substitute it with the actual argument
								const TemplateTypeArg& arg = (*template_args)[i];
								if (!arg.is_value && arg.type_index < gTypeInfo.size()) {
									// It's a type argument - get the type name and create an identifier
									StringHandle type_name_handle = gTypeInfo[arg.type_index].name();
									std::string_view type_name = StringTable::getStringView(type_name_handle);
									FLASH_LOG_FORMAT(Templates, Debug, "Alias template parameter '{}' resolved to type '{}'", target_name, type_name);
									
									// Return an IdentifierNode for the resolved type
									Token resolved_token(Token::Type::Identifier, type_name, 
									                     final_identifier.line(), final_identifier.column(), final_identifier.file_index());
									result = emplace_node<ExpressionNode>(IdentifierNode(resolved_token));
									return ParseResult::success(*result);
								}
								break;
							}
						}
					}
					
					// If the target type is not a direct parameter reference, fall through to other handling
					FLASH_LOG(Templates, Debug, "Alias template target is not a direct parameter, continuing with class template instantiation");
				}
				
				// Try to instantiate the template with these arguments
				// Note: try_instantiate_class_template returns nullopt on success (type registered in gTypesByName)
				// Try class template instantiation first (for struct/class templates)
				auto instantiation_result = try_instantiate_class_template(qual_id.name(), *template_args);
				if (instantiation_result.has_value()) {
					// Simple name failed, try with qualified name
					instantiation_result = try_instantiate_class_template(qualified_name, *template_args);
					if (instantiation_result.has_value()) {
						// Class instantiation didn't work, try function template
						instantiation_result = try_instantiate_template_explicit(qual_id.name(), *template_args);
						if (instantiation_result.has_value()) {
							instantiation_result = try_instantiate_template_explicit(qualified_name, *template_args);
							if (instantiation_result.has_value()) {
								// Template instantiation failed - this might not be a template after all
								// But we successfully parsed template arguments, so continue with the parsed args
								FLASH_LOG_FORMAT(Parser, Warning, "Parsed template arguments but instantiation failed for '{}'", qualified_name);
							}
						}
					}
				}
				// If we reach here, instantiation succeeded (returned nullopt)
				
				// Check if followed by :: for member access (Template<T>::member)
				if (current_token_.value() == "::") {
					// Fill in default template arguments to get the actual instantiated name
					std::vector<TemplateTypeArg> filled_template_args = *template_args;
					auto template_lookup_result = gTemplateRegistry.lookupTemplate(qual_id.name());
					if (template_lookup_result.has_value() && template_lookup_result->is<TemplateClassDeclarationNode>()) {
						const auto& template_class = template_lookup_result->as<TemplateClassDeclarationNode>();
						const auto& template_params = template_class.template_parameters();
						
						// Helper lambda to build instantiated template name suffix
						// Fill in defaults for missing parameters
						for (size_t param_idx = filled_template_args.size(); param_idx < template_params.size(); ++param_idx) {
							const TemplateParameterNode& param = template_params[param_idx].as<TemplateParameterNode>();
							if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
								const ASTNode& default_node = param.default_value();
								if (default_node.is<TypeSpecifierNode>()) {
									const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
									filled_template_args.push_back(TemplateTypeArg(default_type));
								}
							} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
								const ASTNode& default_node = param.default_value();
								if (default_node.is<ExpressionNode>()) {
									const ExpressionNode& expr_default = default_node.as<ExpressionNode>();
									
									if (std::holds_alternative<QualifiedIdentifierNode>(expr_default)) {
										const QualifiedIdentifierNode& qual_id_default = std::get<QualifiedIdentifierNode>(expr_default);
										
									if (!qual_id_default.namespace_handle().isGlobal()) {
										std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id_default.namespace_handle());
										std::string_view default_member_name = qual_id_default.name();
										
										// Check for dependent placeholder using TypeInfo-based detection
										auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
										if (is_dependent_placeholder && !filled_template_args.empty()) {
											// Build the instantiated template name using hash-based naming
											std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
												
												try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
												
												auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
												if (type_it != gTypesByName.end()) {
													const TypeInfo* type_info = type_it->second;
													if (type_info->getStructInfo()) {
														const StructTypeInfo* struct_info = type_info->getStructInfo();
														for (const auto& static_member : struct_info->static_members) {
															if (StringTable::getStringView(static_member.getName()) == default_member_name) {
																if (static_member.initializer.has_value()) {
																	const ASTNode& init_node = *static_member.initializer;
																	if (init_node.is<ExpressionNode>()) {
																		const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																		if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																			bool val = std::get<BoolLiteralNode>(init_expr).value();
																			filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
																		} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																			const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																			const auto& val = lit.value();
																			if (std::holds_alternative<unsigned long long>(val)) {
																				filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																			}
																		}
																	}
																}
																break;
															}
														}
													}
												}
											}
										}
									} else if (std::holds_alternative<NumericLiteralNode>(expr_default)) {
										const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr_default);
										const auto& val = lit.value();
										if (std::holds_alternative<unsigned long long>(val)) {
											filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
										} else if (std::holds_alternative<double>(val)) {
											filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
										}
									} else if (std::holds_alternative<BoolLiteralNode>(expr_default)) {
										const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr_default);
										filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
									}
								}
							}
						}
					}
					
					// Get the instantiated class name to use in qualified identifier (with defaults filled in)
					std::string_view instantiated_name = get_instantiated_class_name(qual_id.name(), filled_template_args);
					
					// Build the full namespace path including the instantiated template name
					// For "my_ns::Wrapper<int>::value", we want namespace path "my_ns::Wrapper_int" and name="value"
					// Build namespace path from the original namespace handle plus the instantiated template name
					NamespaceHandle base_ns = qual_id.namespace_handle();
					StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
					NamespaceHandle full_ns_handle = gNamespaceRegistry.getOrCreateNamespace(base_ns, instantiated_name_handle);
					
					// Parse the :: and the member name
					advance(); // consume ::
					if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
						return ParseResult::error("Expected identifier after '::'", current_token_);
					}
					
					Token member_token = current_token_;
					advance(); // consume member identifier
					
					// Handle additional :: if present (nested member access)
					while (current_token_.value() == "::") {
						// Add current member to namespace path
						StringHandle member_handle = member_token.handle();
						full_ns_handle = gNamespaceRegistry.getOrCreateNamespace(full_ns_handle, member_handle);
						advance(); // consume ::
						if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
							return ParseResult::error("Expected identifier after '::'", current_token_);
						}
						member_token = current_token_;
						advance(); // consume identifier
					}
					
					// Create QualifiedIdentifierNode with the complete path
					auto full_qualified_node = emplace_node<QualifiedIdentifierNode>(full_ns_handle, member_token);
					
					// Look up the member in the instantiated struct's symbol table
					auto member_lookup = gSymbolTable.lookup_qualified(full_ns_handle, member_token.value());
					
					// If followed by '(', handle as function call (e.g., Template<T>::method())
					if (current_token_.value() == "(") {
						advance(); // consume '('
						
						auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
							.handle_pack_expansion = true,
							.collect_types = true,
							.expand_simple_packs = true
						});
						if (!args_result.success) {
							return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
						}
						ChunkedVector<ASTNode> args = std::move(args_result.args);
						
						if (!consume(")"_tok)) {
							return ParseResult::error("Expected ')' after function call arguments", current_token_);
						}
						
						// Get the declaration node for the function
						const DeclarationNode* decl_ptr = nullptr;
						if (member_lookup.has_value()) {
							decl_ptr = getDeclarationNode(*member_lookup);
						}
						if (!decl_ptr) {
							// Member may not be in namespace symbol table - resolve from instantiated struct members.
							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
							if (type_it != gTypesByName.end() && type_it->second) {
								const StructTypeInfo* struct_info = type_it->second->getStructInfo();
								if (struct_info) {
									StringHandle member_name_handle = member_token.handle();
									const FunctionDeclarationNode* first_name_match = nullptr;
									size_t call_arg_count = args.size();
									for (const auto& member_func : struct_info->member_functions) {
										if (member_func.getName() == member_name_handle && member_func.function_decl.is<FunctionDeclarationNode>()) {
											const FunctionDeclarationNode& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
											if (!first_name_match) {
												first_name_match = &candidate;
											}
											if (candidate.parameter_nodes().size() == call_arg_count) {
												member_lookup = member_func.function_decl;
												decl_ptr = &candidate.decl_node();
												break;
											}
										}
									}
									if (!decl_ptr && first_name_match) {
										decl_ptr = &first_name_match->decl_node();
									}
								}
							}
						}
						if (member_lookup.has_value() && member_lookup->is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = member_lookup->as<FunctionDeclarationNode>();
							StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
							auto inst_type_it = gTypesByName.find(class_name_handle);
							if (!func_decl.get_definition().has_value() && inst_type_it != gTypesByName.end() && inst_type_it->second->isTemplateInstantiation()) {
								StringHandle member_name_handle = member_token.handle();
								if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
									auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
									if (lazy_info_opt.has_value()) {
										auto instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
										if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
											member_lookup = instantiated_func;
											decl_ptr = &instantiated_func->as<FunctionDeclarationNode>().decl_node();
											LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
										}
									}
								}
							}
						}
						if (!decl_ptr) {
							// Create a forward declaration
							auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
							auto forward_decl = emplace_node<DeclarationNode>(type_node, member_token);
							member_lookup = forward_decl;
							decl_ptr = &forward_decl.as<DeclarationNode>();
						}
						
						result = emplace_node<ExpressionNode>(
							FunctionCallNode(*decl_ptr, std::move(args), member_token));
						
						// Set mangled name if available
						if (member_lookup.has_value() && member_lookup->is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = member_lookup->as<FunctionDeclarationNode>();
							if (func_decl.has_mangled_name()) {
								std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
							}
						}
						
						return ParseResult::success(*result);
					}
					
					result = emplace_node<ExpressionNode>(full_qualified_node.as<QualifiedIdentifierNode>());
					return ParseResult::success(*result);
				}
				
				// Template instantiation succeeded
				// Don't return early - let it fall through to normal lookup which will find the instantiated type
			}
			// Not a template - let it fall through to be parsed as operator<
		}

		// Try to look up the qualified identifier
		auto identifierType = gSymbolTable.lookup_qualified(qual_id.qualifiedIdentifier());
		
		// Check if this is a brace initialization: ns::Template<Args>{}
		if (template_args.has_value() && current_token_.value() == "{") {
			// Parse the brace initialization using the helper
			ParseResult brace_init_result = parse_template_brace_initialization(*template_args, qual_id.name(), final_identifier);
			if (!brace_init_result.is_error() && brace_init_result.node().has_value()) {
				return brace_init_result;
			}
			// If parsing failed, fall through to function call check
		}

		// Check if this is a non-template brace initialization: ns::Type{args}
		if (!template_args.has_value() && current_token_.value() == "{") {
			std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
			StringHandle qualified_handle = StringTable::getOrInternStringHandle(qualified_name);
			auto type_it = gTypesByName.find(qualified_handle);
			if (type_it == gTypesByName.end()) {
				type_it = gTypesByName.find(final_identifier.handle());
			}
			if (type_it != gTypesByName.end()) {
				const TypeInfo* type_info_ptr = type_it->second;
				const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
				TypeIndex type_index = type_info_ptr->type_index_;

				advance(); // consume '{'

				ChunkedVector<ASTNode> args;
				while (!current_token_.kind().is_eof() && current_token_.value() != "}") {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					if (current_token_.value() == ",") {
						advance();
					} else if (current_token_.value() != "}") {
						return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
					}
				}

				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after brace initializer", current_token_);
				}

				int type_size = struct_info ? static_cast<int>(struct_info->total_size * 8) : 0;
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, final_identifier);
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), final_identifier));
				return ParseResult::success(*result);
			}
		}
		
		// Check if followed by '(' for function call
		if (current_token_.value() == "(") {
			advance(); // consume '('

			// Parse function arguments using unified helper (expand simple packs for qualified calls)
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = true
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// If not found OR if it's a template (not an instantiated function), try template instantiation
			// Also try if explicit template arguments were provided (to handle overload resolution)
			if (((!identifierType.has_value() || identifierType->is<TemplateFunctionDeclarationNode>()) ||
			     (template_args.has_value() && !template_args->empty())) && 
			    current_linkage_ != Linkage::C) {
				// Build qualified template name
				std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
				
				// Phase 1 C++20: If we have explicit template arguments, use them instead of deducing
				if (template_args.has_value() && !template_args->empty()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Using explicit template arguments for function call to '{}'", qualified_name);
					// Try to instantiate with explicit template arguments
					std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args);
					if (!template_inst.has_value()) {
						// Try with simple name
						template_inst = try_instantiate_template_explicit(qual_id.name(), *template_args);
					}
					
					if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *template_inst;
						FLASH_LOG_FORMAT(Parser, Debug, "Successfully instantiated function template '{}' with explicit arguments", qualified_name);
					}
				}
				
				// If still not found and no explicit template arguments, try deducing from function arguments
				// Apply lvalue reference for forwarding deduction on arg_types
				if (!identifierType.has_value() || identifierType->is<TemplateFunctionDeclarationNode>()) {
					std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
					
					// Try to instantiate the qualified template function
					if (!arg_types.empty()) {
						std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							identifierType = *template_inst;
						}
					}
				}
			}
			
			// If still not found, create a forward declaration
			if (!identifierType) {
				// Validate namespace exists before creating forward declaration (catches f2::func when f2 undeclared)
				if (!validateQualifiedNamespace(qual_id.namespace_handle(), qual_id.identifier_token(), parsing_template_body_)) {
					return ParseResult::error(
						std::string(StringBuilder().append("Use of undeclared identifier '")
							.append(buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name()))
							.append("'").commit()),
						qual_id.identifier_token());
				}
				auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
				auto forward_decl = emplace_node<DeclarationNode>(type_node, qual_id.identifier_token());
				identifierType = forward_decl;
			}

			// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
			const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
			if (!decl_ptr) {
				return ParseResult::error("Invalid function declaration (template args path)", current_token_);
			}

				FLASH_LOG(Parser, Debug, "Creating FunctionCallNode for qualified identifier with template args");
				// Create function call node with the qualified identifier
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(*decl_ptr, std::move(args), qual_id.identifier_token()));
				
				// If explicit template arguments were provided, store them in the FunctionCallNode
				// This is needed for deferred template-dependent expressions (e.g., decltype(base_trait<T>()))
				bool has_explicit_template_args = template_args.has_value() && !template_args->empty() && !template_arg_nodes.empty();
				if (has_explicit_template_args) {
					FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
					func_call.set_template_arguments(std::move(template_arg_nodes));
					FLASH_LOG(Templates, Debug, "Stored ", template_arg_nodes.size(), " template argument nodes in FunctionCallNode (path 1)");
				}
				
				// Store the qualified source name for template lookup during constexpr evaluation
				std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
				FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
				func_call.set_qualified_name(qualified_name);
				FLASH_LOG(Parser, Debug, "Set qualified name on FunctionCallNode: ", qualified_name);
				
				// If the function has a pre-computed mangled name, set it on the FunctionCallNode
				if (identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					if (func_decl.has_mangled_name()) {
						func_call.set_mangled_name(func_decl.mangled_name());
					}
				}
			} else {
				// Just a qualified identifier reference
				result = emplace_node<ExpressionNode>(qual_id);
			}

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Get the identifier's type information from the symbol table
		// Use template-aware lookup if we're parsing a template body OR if we have template parameters
		// in scope (e.g., when parsing template parameter defaults that reference earlier parameters)
		std::optional<ASTNode> identifierType;
		if (!current_template_param_names_.empty()) {
			// Template-aware lookup: checks if identifier is a template parameter first
			identifierType = gSymbolTable.lookup(idenfifier_token.handle(), gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
			FLASH_LOG_FORMAT(Parser, Debug, "Template-aware lookup for '{}', template_params_count={}", idenfifier_token.value(), current_template_param_names_.size());
		} else {
			identifierType = lookup_symbol(idenfifier_token.handle());
		}

		FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' lookup result: {}, peek='{}', member_function_context_stack_ size={}",
			idenfifier_token.value(), identifierType.has_value() ? "found" : "not found",
			!peek().is_eof() ? peek_info().value() : "N/A",
			member_function_context_stack_.size());

		// BUGFIX: Check if we're in a member function context and this identifier is a member function
		// This handles the case where register_member_functions_in_scope already added the function to the symbol table
		// so identifierType is set, but we still need to detect it as a member function call with implicit 'this'
		// Declare this flag here so it's visible throughout the rest of the function
		bool found_member_function_in_context = false;
		if (!member_function_context_stack_.empty() && identifierType.has_value() &&
		    identifierType->is<FunctionDeclarationNode>() && peek() == "("_tok) {
			const auto& mf_ctx = member_function_context_stack_.back();
			const StructDeclarationNode* struct_node = mf_ctx.struct_node;
			if (struct_node) {
				// Check if this function is a member function of the current struct
				for (const auto& member_func : struct_node->member_functions()) {
					if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
							found_member_function_in_context = true;
							FLASH_LOG_FORMAT(Parser, Debug, "EARLY CHECK: Detected member function call '{}' with implicit 'this'", idenfifier_token.value());
							break;
						}
					}
				}
				
				// If not found in current struct, search in base classes
				if (!found_member_function_in_context) {
					// Get the struct's base classes and search recursively
					TypeIndex struct_type_index = mf_ctx.struct_type_index;
					if (struct_type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[struct_type_index];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						if (struct_info) {
							// Collect base classes to search (breadth-first to handle multiple inheritance)
							std::vector<TypeIndex> base_classes_to_search;
							for (const auto& base : struct_info->base_classes) {
								base_classes_to_search.push_back(base.type_index);
							}
							
							// Search through base classes
							for (size_t i = 0; i < base_classes_to_search.size() && !found_member_function_in_context; ++i) {
								TypeIndex base_idx = base_classes_to_search[i];
								if (base_idx >= gTypeInfo.size()) continue;
								
								const TypeInfo& base_type_info = gTypeInfo[base_idx];
								const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
								if (!base_struct_info) continue;
								
								// Check member functions in this base class
								// StructMemberFunction has function_decl which is an ASTNode
								for (const auto& member_func : base_struct_info->member_functions) {
									if (member_func.getName() == idenfifier_token.handle()) {
										// Found matching member function in base class
										if (member_func.function_decl.is<FunctionDeclarationNode>()) {
											// Update identifierType to point to the base class function
											gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
											identifierType = member_func.function_decl;
											found_member_function_in_context = true;
											FLASH_LOG_FORMAT(Parser, Debug, "EARLY CHECK: Detected base class member function call '{}' with implicit 'this'", idenfifier_token.value());
											break;
										}
									}
								}
								
								// Add this base's base classes to search list (for multi-level inheritance)
								for (const auto& nested_base : base_struct_info->base_classes) {
									// Avoid duplicates (relevant for diamond inheritance)
									bool already_in_list = false;
									for (TypeIndex existing : base_classes_to_search) {
										if (existing == nested_base.type_index) {
											already_in_list = true;
											break;
										}
									}
									if (!already_in_list) {
										base_classes_to_search.push_back(nested_base.type_index);
									}
								}
							}
						}
					}
				}
			}
		}

		// BUGFIX: If we detected a member function call with implicit 'this', handle it here
		// This must be done BEFORE the `if (!identifierType)` block, because identifierType IS set
		if (found_member_function_in_context && peek() == "("_tok) {
			FLASH_LOG_FORMAT(Parser, Debug, "Handling member function call '{}' with implicit 'this'", idenfifier_token.value());
			advance(); // consume '('

			// Parse function arguments
			ChunkedVector<ASTNode> args;
			while (!current_token_.kind().is_eof() &&
			       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
				ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (argResult.is_error()) {
					return argResult;
				}
				if (auto node = argResult.node()) {
					// Check for pack expansion (...) after the argument
					if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "...") {
						Token ellipsis_token = current_token_;
						advance(); // consume '...'
						auto pack_expr = emplace_node<ExpressionNode>(
							PackExpansionExprNode(*node, ellipsis_token));
						args.push_back(pack_expr);
					} else {
						args.push_back(*node);
					}
				}

				if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					return ParseResult::error("Expected ',' or ')' in function arguments", current_token_);
				}
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function arguments", current_token_);
			}

			// Create implicit 'this' expression
			Token this_token(Token::Type::Keyword, "this"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
			auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));

			// Get the FunctionDeclarationNode
			FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();

			// Create MemberFunctionCallNode with implicit 'this'
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(this_node, func_decl, std::move(args), idenfifier_token));

			FLASH_LOG_FORMAT(Parser, Debug, "Created MemberFunctionCallNode for '{}'", idenfifier_token.value());
			return ParseResult::success(*result);
		}

		// BUGFIX: If identifier not found in symbol table, check static members of current struct first.
		// This handles cases like: static_assert(value == 42, "msg"); where value is a static member.
		// Static members should be visible in expressions within the same struct.
		bool found_as_type_alias = false;
		if (!identifierType && !struct_parsing_context_stack_.empty()) {
			StringHandle identifier_handle = idenfifier_token.handle();
			const auto& ctx = struct_parsing_context_stack_.back();
			FLASH_LOG_FORMAT(Parser, Debug, "Checking struct context for '{}': struct_node={}, local_struct_info={}", 
				idenfifier_token.value(), ctx.struct_node != nullptr, ctx.local_struct_info != nullptr);
			
			// Check the struct_node's static_members (for non-template structs)
			if (ctx.struct_node != nullptr) {
				for (const auto& static_member : ctx.struct_node->static_members()) {
					if (static_member.name == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in current struct node (early lookup)", 
							idenfifier_token.value());
						found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
						break;
					}
				}
			}
			
			// Check local_struct_info (for template classes being parsed)
			if (!found_as_type_alias && ctx.local_struct_info != nullptr) {
				for (const auto& static_member : ctx.local_struct_info->static_members) {
					if (static_member.getName() == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in local_struct_info (early lookup)", 
							idenfifier_token.value());
						found_as_type_alias = true;
						break;
					}
				}
			}
			
			// BUGFIX: Check for members imported via using-declarations
			// This handles cases like: using BaseClass::__value;
			// where the base class is a dependent template type that can't be resolved yet
			if (!found_as_type_alias) {
				for (const auto& imported_member : ctx.imported_members) {
					if (imported_member == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as imported member via using-declaration", 
							idenfifier_token.value());
						found_as_type_alias = true;
						break;
					}
				}
			}
			
			// Also search base classes for static members (if base classes are resolved)
			// This handles using-declarations like: using BaseClass::__value;
			// which make base class static members accessible by their simple name
			if (!found_as_type_alias && ctx.local_struct_info != nullptr && !ctx.local_struct_info->base_classes.empty()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Searching base classes for '{}', num_bases={}", 
					idenfifier_token.value(), ctx.local_struct_info->base_classes.size());
				auto [base_static_member, owner_struct] = ctx.local_struct_info->findStaticMemberRecursive(identifier_handle);
				if (base_static_member) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in base class '{}'", 
						idenfifier_token.value(), StringTable::getStringView(owner_struct->getName()));
					found_as_type_alias = true;  // Found it, suppress "Missing identifier" error
				}
			}
		}
		
		// BUGFIX: If identifier not found in symbol table, check if it's a type alias in gTypesByName
		// This allows type aliases like false_type, true_type, enable_if_t to be used in specific contexts
		// Only apply this fallback when the identifier is followed by '::' or '(' to ensure
		// we don't break legitimate cases where an identifier should be an error
		// ENHANCED: In TemplateArgument context, also check for ',' or '>' or '<' because type aliases
		// and template class names are commonly used as template arguments in <type_traits>
		if (!identifierType && !found_as_type_alias && !peek().is_eof()) {
			std::string_view peek = peek_info().value();
			// Check gTypesByName if identifier is followed by :: (qualified name), ( (constructor call), or { (brace init)
			bool should_check_types = (peek == "::" || peek == "(" || peek == "{");
			
			// In template argument context, also check for various tokens that indicate a type context.
			// Type aliases and template class names are commonly used as template arguments
			// (e.g., first_t<false_type, ...>, __or_<is_reference<T>, is_function<T>>, declval<_Tp&>())
			// The '&' and '&&' handle reference type declarators like T& or T&&
			if (!should_check_types && context == ExpressionContext::TemplateArgument) {
				should_check_types = (peek == "," || peek == ">" || peek == ">>" || peek == "<" || 
				                      peek == "&" || peek == "&&");
			}
			
			if (should_check_types) {
				StringHandle identifier_handle = idenfifier_token.handle();
				auto type_it = gTypesByName.find(identifier_handle);
				if (type_it != gTypesByName.end()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as type alias in gTypesByName (peek='{}', context={})", 
						idenfifier_token.value(), peek, context == ExpressionContext::TemplateArgument ? "TemplateArgument" : "other");
					found_as_type_alias = true;
					// Mark that we found it as a type so it can be used for type references
					// The actual type info will be retrieved later when needed
				} else {
					// Try namespace-qualified lookup: if we're inside a namespace, the type alias
					// might be registered with a qualified name (e.g., "std::size_t")
					NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
					if (!current_namespace.isGlobal()) {
						StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, identifier_handle);
						auto qualified_type_it = gTypesByName.find(qualified_handle);
						if (qualified_type_it != gTypesByName.end()) {
							FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as namespace-qualified type alias '{}' in gTypesByName", 
								idenfifier_token.value(), StringTable::getStringView(qualified_handle));
							found_as_type_alias = true;
						}
					}
					
					// If still not found, check for member type aliases in the current struct/class being parsed
					// This handles cases like: using inner_type = int; using outer_type = wrapper<inner_type>;
					if (!found_as_type_alias) {
						// Try member_function_context_stack_ first (for code inside member function bodies)
						if (!member_function_context_stack_.empty()) {
							const auto& ctx = member_function_context_stack_.back();
							if (ctx.struct_node != nullptr) {
								for (const auto& alias : ctx.struct_node->type_aliases()) {
									if (alias.alias_name == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member type alias in current struct (member func context)", 
											idenfifier_token.value());
										found_as_type_alias = true;
										break;
									}
								}
							}
						}
						
						// Then try struct_parsing_context_stack_ (for code inside struct body, e.g., type alias definitions)
						if (!found_as_type_alias && !struct_parsing_context_stack_.empty()) {
							const auto& ctx = struct_parsing_context_stack_.back();
							if (ctx.struct_node != nullptr) {
								for (const auto& alias : ctx.struct_node->type_aliases()) {
									if (alias.alias_name == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member type alias in current struct (struct parsing context)", 
											idenfifier_token.value());
										found_as_type_alias = true;
										break;
									}
								}
							}
						}
					}
					
					// If still not found, check for static data members in the current struct/class being parsed
					// This handles cases like: using type = typename aligned_storage<_S_len, alignment_value>::type;
					// where _S_len and alignment_value are static const members of the same struct
					if (!found_as_type_alias && !struct_parsing_context_stack_.empty()) {
						const auto& ctx = struct_parsing_context_stack_.back();
						// First try the struct_node's static_members (for member struct templates)
						if (ctx.struct_node != nullptr) {
							for (const auto& static_member : ctx.struct_node->static_members()) {
								if (static_member.name == identifier_handle) {
									FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in current struct node (struct parsing context)", 
										idenfifier_token.value());
									found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
									break;
								}
							}
						}
						
						// Then check local_struct_info (for template classes being parsed where static members are added)
						if (!found_as_type_alias && ctx.local_struct_info != nullptr) {
							for (const auto& static_member : ctx.local_struct_info->static_members) {
								if (static_member.getName() == identifier_handle) {
									FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in local_struct_info (struct parsing context)", 
										idenfifier_token.value());
									found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
									break;
								}
							}
						}
						
						// Finally check StructTypeInfo from gTypesByName (for already-registered types)
						if (!found_as_type_alias) {
							StringHandle struct_name_handle = StringTable::getOrInternStringHandle(ctx.struct_name);
							auto struct_type_it = gTypesByName.find(struct_name_handle);
							if (struct_type_it != gTypesByName.end() && struct_type_it->second->getStructInfo()) {
								const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
								for (const auto& static_member : struct_info->static_members) {
									if (static_member.getName() == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in StructTypeInfo (struct parsing context)", 
											idenfifier_token.value());
										found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
										break;
									}
								}
							}
						}
					}
				}
			}
		}
		
		// If identifier is followed by '<' and we're inside a struct context, check if it's a member struct template
		// This handles patterns like: template<typename T> struct Outer<_Tp, Inner<T>> { }
		// where Inner is a member struct template of the enclosing class
		if (!identifierType && !found_as_type_alias && peek() == "<"_tok) {
			if (!struct_parsing_context_stack_.empty()) {
				const auto& ctx = struct_parsing_context_stack_.back();
				// Build qualified name: EnclosingClass::MemberTemplate
				StringBuilder qualified_name;
				qualified_name.append(ctx.struct_name).append("::"sv).append(idenfifier_token.value());
				std::string_view qualified_name_sv = qualified_name.commit();
				auto member_template = gTemplateRegistry.lookupTemplate(qualified_name_sv);
				if (member_template.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member struct template '{}' in enclosing class", 
						idenfifier_token.value(), qualified_name_sv);
					found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
				}
			}
		}
		
		// If identifier is followed by ::, it might be a namespace-qualified identifier
		// This handles both: 
		// 1. Identifier not found (might be namespace name)
		// 2. Identifier found but followed by :: (namespace or class scope resolution)
		if (peek() == "::"_tok) {
			// Parse as qualified identifier: Namespace::identifier
			// Even if we don't know if it's a namespace, try parsing it as a qualified identifier
			std::vector<StringType<32>> namespaces;
			Token final_identifier = idenfifier_token;
			
			// Collect the qualified path
			while (peek() == "::"_tok) {
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::
				
				// Handle qualified operator call: Type::operator=()
				if (peek() == "operator"_tok) {
					advance(); // consume 'operator'
					return parse_qualified_operator_call(final_identifier, namespaces);
				}

				// Get next identifier
				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier after '::'", peek_info());
				}
				final_identifier = peek_info();
				advance(); // consume the identifier
			}
			
			FLASH_LOG(Parser, Debug, "Qualified identifier: final name = '{}'", final_identifier.value());
			
			// Check if final identifier is followed by template arguments: ns::Template<Args>
			std::optional<std::vector<TemplateTypeArg>> template_args;
			std::vector<ASTNode> template_arg_nodes;  // Store the actual expression nodes
			if (peek() == "<"_tok) {
				// Before parsing < as template arguments, check if the identifier is actually a template
				// This prevents misinterpreting patterns like R1<T>::num < R2<T>::num> where < is comparison
				
				// Build the full qualified name for template lookup
				StringBuilder lookup_name_builder;
				for (const auto& ns : namespaces) {
					lookup_name_builder.append(ns.c_str()).append("::");
				}
				lookup_name_builder.append(final_identifier.value());
				std::string_view qualified_lookup_name = lookup_name_builder.preview();
				
				// Check if this is a known template (class or variable template)
				auto template_opt = gTemplateRegistry.lookupTemplate(qualified_lookup_name);
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_lookup_name);
				auto alias_template_opt = gTemplateRegistry.lookup_alias_template(qualified_lookup_name);
				
				// Also check with just the simple name
				auto simple_template_opt = gTemplateRegistry.lookupTemplate(final_identifier.value());
				auto simple_var_template_opt = gTemplateRegistry.lookupVariableTemplate(final_identifier.value());
				auto simple_alias_template_opt = gTemplateRegistry.lookup_alias_template(final_identifier.value());
				
				bool is_known_template = template_opt.has_value() || var_template_opt.has_value() ||
				                         alias_template_opt.has_value() ||
				                         simple_template_opt.has_value() || simple_var_template_opt.has_value() ||
				                         simple_alias_template_opt.has_value();
				
				lookup_name_builder.reset();
				
				if (is_known_template) {
					FLASH_LOG(Parser, Debug, "Qualified identifier followed by '<', attempting to parse template arguments");
					template_args = parse_explicit_template_arguments(&template_arg_nodes);
					// If parsing failed, it might be a less-than operator, continue normally
				} else if (context == ExpressionContext::TemplateArgument) {
					// In template argument context, if the identifier is NOT a known template,
					// treat '<' as a comparison operator (e.g., R1<T>::num < R2<T>::num>)
					FLASH_LOG_FORMAT(Parser, Debug, 
					    "In TemplateArgument context, qualified identifier '{}' is not a known template - treating '<' as comparison operator",
					    final_identifier.value());
					// Don't parse template arguments - let the binary operator loop handle '<' as comparison
				} else {
					// Not in template argument context and not a known template
					// Try parsing template arguments anyway (might be a forward-declared template)
					FLASH_LOG(Parser, Debug, "Qualified identifier followed by '<', attempting to parse template arguments (unknown template)");
					template_args = parse_explicit_template_arguments(&template_arg_nodes);
					// If parsing failed, it might be a less-than operator, continue normally
				}
			}
			
			// Create a QualifiedIdentifierNode with namespace handle
			NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
			auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
			const auto& qual_id = qualified_node_ast.as<QualifiedIdentifierNode>();
			
			// Look up the qualified identifier (either the template name or instantiated template)
			if (template_args.has_value()) {
				// Try to instantiate the template with namespace qualification
				// Build the qualified template name for lookup
				std::string_view qualified_template_name = buildQualifiedNameFromHandle(ns_handle, final_identifier.value());
				
				FLASH_LOG_FORMAT(Parser, Debug, "Looking up template '{}' with {} template arguments", qualified_template_name, template_args->size());
				
				// First, check if this is a variable template
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_template_name);
				if (var_template_opt.has_value()) {
					// Instantiate the variable template
					auto instantiated_var = try_instantiate_variable_template(qualified_template_name, *template_args);
					if (instantiated_var.has_value()) {
						// Get the instantiated variable name
						std::string_view inst_name;
						if (instantiated_var->is<VariableDeclarationNode>()) {
							const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
							const auto& decl = var_decl.declaration();
							inst_name = decl.identifier_token().value();
						} else if (instantiated_var->is<DeclarationNode>()) {
							const auto& decl = instantiated_var->as<DeclarationNode>();
							inst_name = decl.identifier_token().value();
						} else {
							inst_name = qualified_template_name;  // Fallback
						}
						
						FLASH_LOG(Templates, Debug, "Successfully instantiated qualified variable template: ", qualified_template_name);
						
						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name, 
						                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						return ParseResult::success(*result);
					}
				}
				
				// Try to instantiate as class template with qualified name first
				auto instantiated = try_instantiate_class_template(qualified_template_name, *template_args);
				
				// If that didn't work, try with simple name (for backward compatibility)
				if (!instantiated.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Qualified name lookup failed, trying simple name '{}'", final_identifier.value());
					instantiated = try_instantiate_class_template(final_identifier.value(), *template_args);
				}
				
				if (instantiated.has_value()) {
					const auto& inst_struct = instantiated->as<StructDeclarationNode>();
					
					// Look up the instantiated template
					identifierType = gSymbolTable.lookup(StringTable::getStringView(inst_struct.name()));
					
					// Check for :: after template arguments (Template<T>::member)
					if (peek() == "::"_tok) {
						auto qualified_result = parse_qualified_identifier_after_template(final_identifier);
						if (!qualified_result.is_error() && qualified_result.node().has_value()) {
							auto qualified_node2 = qualified_result.node()->as<QualifiedIdentifierNode>();
							auto member_call_result = try_parse_member_template_function_call(
								StringTable::getStringView(inst_struct.name()),
								qualified_node2.name(),
								qualified_node2.identifier_token());
							if (member_call_result.has_value()) {
								if (member_call_result->is_error()) {
									return *member_call_result;
								}
								return ParseResult::success(*member_call_result->node());
							}
							result = emplace_node<ExpressionNode>(qualified_node2);
							return ParseResult::success(*result);
						}
					}
					
					// Check if this is a brace initialization: ns::Template<Args>{}
					if (peek() == "{"_tok) {
						advance(); // consume '{'
						
						ChunkedVector<ASTNode> args;
						while (!peek().is_eof() && peek() != "}"_tok) {
							auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (argResult.is_error()) {
								return argResult;
							}
							if (auto node = argResult.node()) {
								args.push_back(*node);
							}
							
							if (peek() == ","_tok) {
								advance(); // consume ','
							} else if (peek() != "}"_tok) {
								return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
							}
						}
						
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after brace initializer", current_token_);
						}
						
						// Look up the instantiated type
						auto type_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(inst_struct.name()));
						auto type_it = gTypesByName.find(type_handle);
						if (type_it != gTypesByName.end()) {
							// Create TypeSpecifierNode for the instantiated class
							const TypeInfo& type_info = *type_it->second;
							TypeIndex type_index = type_info.type_index_;
							int type_size = 0;
							if (type_info.struct_info_) {
								type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
							}
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, final_identifier);
							
							// Create ConstructorCallNode
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), final_identifier));
							return ParseResult::success(*result);
						} else {
							return ParseResult::error("Failed to look up instantiated template type", final_identifier);
						}
					}
					
					// Return identifier reference to the instantiated template
					Token inst_token(Token::Type::Identifier, StringTable::getStringView(inst_struct.name()), 
					                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
					result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
					return ParseResult::success(*result);
				}
				
				// If class/variable template instantiation failed, try function template instantiation
				// This handles cases like: ns::func<int, int>()
				if (!identifierType.has_value()) {
					FLASH_LOG_FORMAT(Templates, Debug, "Trying function template instantiation for '{}' with {} args", 
					                 qualified_template_name, template_args->size());
					auto func_template_inst = try_instantiate_template_explicit(qualified_template_name, *template_args);
					if (func_template_inst.has_value() && func_template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *func_template_inst;
						FLASH_LOG(Templates, Debug, "Successfully instantiated function template with explicit arguments");
					}
				}
			} else {
				// No template arguments, lookup as regular qualified identifier
				identifierType = gSymbolTable.lookup_qualified(qual_id.qualifiedIdentifier());
			}
			
			FLASH_LOG(Parser, Debug, "Qualified lookup result: {}", identifierType.has_value() ? "found" : "not found");
			
			// Check if this is a function call (even if not found - might be a template)
			if (peek() == "("_tok) {
				advance(); // consume '('
				
				// Parse function arguments using unified helper (collect types for template deduction)
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = true,
					.expand_simple_packs = false
				});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}
				
				// If not found and we're not in extern "C", try template instantiation
				if (!identifierType.has_value() && current_linkage_ != Linkage::C) {
					// Build qualified template name
					std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
					
					// If explicit template arguments were provided, use them for instantiation
					if (template_args.has_value() && !template_args->empty()) {
						FLASH_LOG_FORMAT(Templates, Debug, "Instantiating function template '{}' with {} explicit template arguments", 
						                 qualified_name, template_args->size());
						std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args);
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							identifierType = *template_inst;
							FLASH_LOG(Templates, Debug, "Successfully instantiated function template with explicit arguments");
						}
					} else {
						// Apply lvalue reference for forwarding deduction on arg_types
						std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
						
						// Try to instantiate the qualified template function using argument deduction
						if (!arg_types.empty()) {
							std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
							if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
								identifierType = *template_inst;
							} else {
							}
						} else {
						}
					}
				}
				
				// Get the DeclarationNode
				if (identifierType.has_value() && identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					if (!func_decl.get_definition().has_value()) {
						std::string_view qualified_scope = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
						StringHandle class_name_handle = StringTable::getOrInternStringHandle(qualified_scope);
						auto scope_type_it = gTypesByName.find(class_name_handle);
						if (scope_type_it != gTypesByName.end() && scope_type_it->second->isTemplateInstantiation()) {
							StringHandle member_name_handle = qual_id.identifier_token().handle();
							if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
								auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
								if (lazy_info_opt.has_value()) {
									auto instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
									if (instantiated_func.has_value()) {
										identifierType = *instantiated_func;
										LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
									}
								}
							}
						}
					}
				}

				const DeclarationNode* decl_ptr = identifierType.has_value() ? getDeclarationNode(*identifierType) : nullptr;
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration (qualified id path)", final_identifier);
				}
				
				// Create function call node with the qualified identifier
				auto function_call_node = emplace_node<ExpressionNode>(
					FunctionCallNode(*decl_ptr, std::move(args), final_identifier));
				
				// If explicit template arguments were provided, store them in the FunctionCallNode
				// This is needed for deferred template-dependent expressions (e.g., decltype(base_trait<T>()))
				if (template_args.has_value() && !template_args->empty() && !template_arg_nodes.empty()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_template_arguments(std::move(template_arg_nodes));
					FLASH_LOG(Templates, Debug, "Stored ", template_arg_nodes.size(), " template argument nodes in FunctionCallNode");
				}
				
				// If the function has a pre-computed mangled name, set it on the FunctionCallNode
				if (identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					FLASH_LOG(Parser, Debug, "Namespace-qualified function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
					if (func_decl.has_mangled_name()) {
						std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
						FLASH_LOG(Parser, Debug, "Set mangled name on namespace-qualified FunctionCallNode: {}", func_decl.mangled_name());
					}
				}
				
				result = function_call_node;
				return ParseResult::success(*result);
			} else if (identifierType.has_value()) {
				// Just a qualified identifier reference (e.g., Namespace::globalValue)
				result = emplace_node<ExpressionNode>(qual_id);
				return ParseResult::success(*result);
			}
			// If identifierType is still not found, fall through to error handling below
		}
		
		// If identifier not found in symbol table, check if it's a class/struct type name
		// This handles constructor calls like Widget(42)
		if (!identifierType.has_value()) {
			auto type_it = gTypesByName.find(idenfifier_token.handle());
			if (type_it != gTypesByName.end() && peek() == "("_tok) {
				// This is a constructor call - handle it directly here
				advance();  // consume '('
				
				// Parse constructor arguments
				ChunkedVector<ASTNode> args;
				while (!current_token_.kind().is_eof() && 
				       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					
					if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
						advance();  // consume ','
					} else if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						return ParseResult::error("Expected ',' or ')' in constructor arguments", current_token_);
					}
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after constructor arguments", current_token_);
				}
				
				// Create TypeSpecifierNode for the class
				TypeIndex type_index = type_it->second->type_index_;
				int type_size = 0;
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						type_size = static_cast<unsigned char>(type_info.struct_info_->total_size * 8);
					}
				}
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
				
				// Create ConstructorCallNode
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				return ParseResult::success(*result);
			}
		}
		
		// If the identifier is a template parameter reference, check for constructor calls
		// This handles both T(42) and T{} patterns for dependent type construction
		if (identifierType.has_value() && identifierType->is<TemplateParameterReferenceNode>()) {
			const auto& tparam_ref = identifierType->as<TemplateParameterReferenceNode>();
			
			// Check for brace initialization: T{} or T{args}
			if (peek() == "{"_tok) {
				advance(); // consume '{'
				
				// Parse brace initializer arguments
				ChunkedVector<ASTNode> args;
				while (current_token_.value() != "}") {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					
					if (current_token_.value() == ",") {
						advance(); // consume ','
					} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
						return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
					}
				}
				
				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after brace initializer", current_token_);
				}
				
				// Create TypeSpecifierNode for the template parameter (dependent type)
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, idenfifier_token);
				
				// Create ConstructorCallNode for brace initialization
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				return ParseResult::success(*result);
			}
			
			// Wrap it in an ExpressionNode, but continue checking for '(' constructor calls below
			result = emplace_node<ExpressionNode>(tparam_ref);
			// Don't return - let it fall through to check for '(' below
		}
		
		// Special case: if the identifier is not found but is followed by '...', 
		// it might be a pack parameter that was expanded (e.g., "args" -> "args_0", "args_1", etc.)
		// Allow it to proceed so pack expansion can handle it
		bool is_pack_expansion = false;
		if (!identifierType.has_value() && peek() == "..."_tok) {
			is_pack_expansion = true;
		}

		// Check if this is a template function call
		// First, check if the name matches a static member function of the current class
		// This implements C++ name resolution: class scope takes priority over enclosing namespace scope
		if (identifierType && identifierType->is<TemplateFunctionDeclarationNode>() &&
		    peek() == "("_tok) {
			auto check_class_members = [&](const StructDeclarationNode* struct_node) -> bool {
				if (!struct_node) return false;
				for (const auto& member_func : struct_node->member_functions()) {
					if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
							identifierType = member_func.function_declaration;
							// Register in symbol table so overload resolution can find it
							gSymbolTable.insert(idenfifier_token.value(), member_func.function_declaration);
							// Mark that we found a static member to prevent MemberFunctionCallNode path
							found_member_function_in_context = false;
							FLASH_LOG_FORMAT(Parser, Debug, "Resolved '{}' as static member function of current class (overrides namespace template)", idenfifier_token.value());
							return true;
						}
					}
				}
				return false;
			};
			
			// Check struct_parsing_context_stack_ (inline member function parsing)
			if (!struct_parsing_context_stack_.empty()) {
				check_class_members(struct_parsing_context_stack_.back().struct_node);
			}
			// Check member_function_context_stack_ (delayed function body parsing)
			if (identifierType->is<TemplateFunctionDeclarationNode>() && !member_function_context_stack_.empty()) {
				check_class_members(member_function_context_stack_.back().struct_node);
			}
		}
		if (identifierType && identifierType->is<TemplateFunctionDeclarationNode>() &&
		    consume("("_tok)) {
			
			// Parse arguments to deduce template parameters
			if (peek().is_eof())
				return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

			ChunkedVector<ASTNode> args;
			std::vector<TypeSpecifierNode> arg_types;
			
			while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
				ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (argResult.is_error()) {
					return argResult;
				}

				if (auto node = argResult.node()) {
					args.push_back(*node);
					
					// Try to deduce the type of this argument
					if (node->is<ExpressionNode>()) {
						const auto& expr = node->as<ExpressionNode>();
						std::optional<TypeSpecifierNode> arg_type_node_opt;
						Type arg_type = Type::Int;  // Default assumption
						bool is_lvalue = false;  // Track if this is an lvalue for perfect forwarding
					
						std::visit([&](const auto& inner) {
							using T = std::decay_t<decltype(inner)>;
							if constexpr (std::is_same_v<T, BoolLiteralNode>) {
								arg_type = Type::Bool;
								// Boolean literals are rvalues
							} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
								arg_type = inner.type();
								// Literals are rvalues
							} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
								arg_type = Type::Char;  // const char*
								// String literals are lvalues (but typically decay to pointers)
							} else if constexpr (std::is_same_v<T, IdentifierNode>) {
								// Look up the identifier's type
								auto id_type = lookup_symbol(StringTable::getOrInternStringHandle(inner.name()));
								if (id_type.has_value()) {
									if (const DeclarationNode* decl = get_decl_from_symbol(*id_type)) {
										if (decl->type_node().template is<TypeSpecifierNode>()) {
											// Preserve the full TypeSpecifierNode to retain type_index for structs
											const auto& type_spec = decl->type_node().template as<TypeSpecifierNode>();
											arg_type_node_opt = type_spec;
											arg_type = type_spec.type();
											// Named variables are lvalues
											is_lvalue = true;
										}
									}
								}
							}
						}, expr);
					
						TypeSpecifierNode arg_type_node = arg_type_node_opt.value_or(
							TypeSpecifierNode(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token())
						);
						if (is_lvalue) {
							// Mark as lvalue reference for perfect forwarding template deduction
							arg_type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);
						}
						arg_types.push_back(arg_type_node);
					}
				}
				
				// Check for pack expansion (...) after the argument in variadic template function calls
				// Only expand if the argument is an identifier matching a known pack parameter name
				if (peek() == "..."_tok && !pack_param_info_.empty() && !args.empty()) {
					// Check if the last argument is an identifier matching a pack parameter
					const PackParamInfo* matching_pack = nullptr;
					ASTNode& last_arg = args[args.size() - 1];
					if (last_arg.is<ExpressionNode>()) {
						const auto& expr = last_arg.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							for (const auto& pack_info : pack_param_info_) {
								if (id.name() == pack_info.original_name && pack_info.pack_size > 0) {
									matching_pack = &pack_info;
									break;
								}
							}
						}
					}
					
					if (matching_pack) {
						advance(); // consume '...'
						
						size_t pre_pack_size = args.size();
						bool first_element = true;
						for (size_t pi = 0; pi < matching_pack->pack_size; ++pi) {
							StringBuilder param_name_builder;
							param_name_builder.append(matching_pack->original_name);
							param_name_builder.append('_');
							param_name_builder.append(pi);
							std::string_view expanded_name = param_name_builder.commit();
							
							auto sym = lookup_symbol(StringTable::getOrInternStringHandle(expanded_name));
							if (sym.has_value()) {
								Token id_token(Token::Type::Identifier, expanded_name, 0, 0, 0);
								auto id_node = emplace_node<ExpressionNode>(IdentifierNode(id_token));
								
								if (first_element && pre_pack_size > 0) {
									// Overwrite the last element (the unexpanded pack name)
									args[pre_pack_size - 1] = id_node;
									if (!arg_types.empty()) {
										if (const DeclarationNode* decl = get_decl_from_symbol(*sym)) {
											if (decl->type_node().is<TypeSpecifierNode>()) {
												arg_types.back() = decl->type_node().as<TypeSpecifierNode>();
												arg_types.back().set_reference_qualifier(ReferenceQualifier::LValueReference);
											}
										}
									}
									first_element = false;
								} else {
									args.push_back(id_node);
									if (const DeclarationNode* decl = get_decl_from_symbol(*sym)) {
										if (decl->type_node().is<TypeSpecifierNode>()) {
											TypeSpecifierNode arg_type_node_pack = decl->type_node().as<TypeSpecifierNode>();
											arg_type_node_pack.set_reference_qualifier(ReferenceQualifier::LValueReference);
											arg_types.push_back(arg_type_node_pack);
										}
									}
								}
							}
						}
					} else {
						// Complex pack expansion: the argument is a complex expression
						// containing a pack parameter (e.g., identity(args)..., static_cast<Args>(args)...)
						// Wrap the expression in a PackExpansionExprNode for expansion during template substitution
						advance(); // consume '...'
						Token ellipsis_token(Token::Type::Punctuator, "..."sv, 0, 0, 0);
						ASTNode& last_arg_ref = args[args.size() - 1];
						auto pack_expansion = emplace_node<ExpressionNode>(
							PackExpansionExprNode(last_arg_ref, ellipsis_token));
						last_arg_ref = pack_expansion;
					}
				}
				
				if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
					advance(); // Consume comma
				}
				else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
				}

				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, Token());
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// Try to instantiate the template function (skip in extern "C" contexts - C has no templates)
			std::optional<ASTNode> template_func_inst;
			if (current_linkage_ != Linkage::C) {
				template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
			}
			
			if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
				const auto& func = template_func_inst->as<FunctionDeclarationNode>();
				auto function_call_node = emplace_node<ExpressionNode>(
					FunctionCallNode(func.decl_node(), std::move(args), idenfifier_token));
				
				// Set the mangled name on the function call if the instantiated function has one
				if (func.has_mangled_name()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func.mangled_name());
				}
				
				result = function_call_node;
				return ParseResult::success(*result);
			} else {
				// Template instantiation failed - always return error.
				// In SFINAE context (e.g., requires expression), the caller
				// (parse_requires_expression) handles errors by marking the
				// requirement as unsatisfied (false node).
				FLASH_LOG(Parser, Error, "Template instantiation failed");
				return ParseResult::error("Failed to instantiate template function", idenfifier_token);
			}
		}

		if (!identifierType) {
			// Check if this is a template function before treating it as missing
			if (current_token_.value() == "(" &&
			    gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
				// Don't set identifierType - fall through to the function call handling below
				// which will trigger template instantiation
			}
			// If we're inside a member function, check if this is a member variable
			else if (!member_function_context_stack_.empty()) {
				const auto& member_func_ctx = member_function_context_stack_.back();
				const StructDeclarationNode* struct_node = member_func_ctx.struct_node;

				// Check if this identifier matches any data member in the struct (including inherited members)
				// First try AST node members (for regular structs), then fall back to TypeInfo (for template instantiations)
				bool found_in_ast = false;
				if (struct_node && !struct_node->members().empty()) {
					// First check direct members
					for (const auto& member_decl : struct_node->members()) {
						const ASTNode& member_node = member_decl.declaration;
						if (member_node.is<DeclarationNode>()) {
							const DeclarationNode& decl = member_node.as<DeclarationNode>();
							if (decl.identifier_token().value() == idenfifier_token.value()) {
								// This is a member variable! Transform it into this->member
								// Create a "this" token with the correct value
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								found_in_ast = true;
								goto found_member_variable;
							}
						}
					}

					// Also check base class members
					for (const auto& base : struct_node->base_classes()) {
						// Look up the base class type
						auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
						if (base_type_it != gTypesByName.end()) {
							const TypeInfo* base_type_info = base_type_it->second;
							TypeIndex base_type_index = base_type_info->type_index_;

							// Check if the identifier is a member of the base class (recursively)
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(base_type_index, idenfifier_token.handle());
							if (member_result) {
								// This is an inherited member variable! Transform it into this->member
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								found_in_ast = true;
								goto found_member_variable;
							}
						}
					}
				}
				
				// If not found in AST, try TypeInfo (or local_struct_info for static member initializers)
				// This handles template class instantiations and static member initializers
				if (!found_in_ast) {
					// First try local_struct_info (for static member initializers where TypeInfo::struct_info_ isn't populated yet)
					const StructTypeInfo* struct_info = member_func_ctx.local_struct_info;
					
					// Fall back to TypeInfo lookup if no local_struct_info
					if (!struct_info && member_func_ctx.struct_type_index != 0 && member_func_ctx.struct_type_index < gTypeInfo.size()) {
						const TypeInfo& struct_type_info = gTypeInfo[member_func_ctx.struct_type_index];
						struct_info = struct_type_info.getStructInfo();
					}
					
					if (struct_info) {
						// FIRST check static members (these don't use this->)
						// Use findStaticMemberRecursive to also search base classes
						
						// Trigger lazy static member instantiation if needed
						StringHandle member_name_handle = idenfifier_token.handle();
						instantiateLazyStaticMember(struct_info->name, member_name_handle);
						
						auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
						if (static_member) {
							// Found static member! Create a simple identifier node
							// Static members are accessed directly, not via this->
							result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
							// Set identifierType to prevent "Missing identifier" error
							identifierType = emplace_node<DeclarationNode>(
								emplace_node<TypeSpecifierNode>(
									static_member->type,
									static_member->type_index,
									static_cast<unsigned char>(static_member->size * 8),
									idenfifier_token
								),
								idenfifier_token
							);
							goto found_member_variable;
						}
						
						// Check instance members (these use this->)
						for (const auto& member : struct_info->members) {
							if (member.getName() == idenfifier_token.handle()) {
								// This is a member variable! Transform it into this->member
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								goto found_member_variable;
							}
						}
						
						// Also check base class members
						auto member_result = FlashCpp::gLazyMemberResolver.resolve(member_func_ctx.struct_type_index, idenfifier_token.handle());
						if (member_result) {
							// This is an inherited member variable! Transform it into this->member
							Token this_token(Token::Type::Keyword, "this"sv,
							                 idenfifier_token.line(), idenfifier_token.column(),
							                 idenfifier_token.file_index());
							auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

							// Create member access node: this->member
							result = emplace_node<ExpressionNode>(
								MemberAccessNode(this_ident, idenfifier_token));

							// Don't return - let it fall through to postfix operator parsing
							goto found_member_variable;
						}
					}
				}
			}

			// Check if this is a member function call (identifier not found but matches a member function)
			// This handles the complete-class context where member functions declared later can be called
			// We need to track if we found a member function so we can create MemberFunctionCallNode with implicit 'this'
			if (!member_function_context_stack_.empty() && peek() == "("_tok) {
				FLASH_LOG_FORMAT(Parser, Debug, "Checking member function context for '{}', stack size: {}",
					idenfifier_token.value(), member_function_context_stack_.size());
				const auto& mf_ctx = member_function_context_stack_.back();
				const StructDeclarationNode* struct_node = mf_ctx.struct_node;
				if (struct_node) {
					FLASH_LOG_FORMAT(Parser, Debug, "Struct node available, member_functions count: {}",
						struct_node->member_functions().size());
					// Helper lambda to search for member function in a struct and its base classes
					// Returns true if found and sets identifierType
					bool found = false;

					// First, check the current struct's member functions
					for (const auto& member_func : struct_node->member_functions()) {
						if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
							const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
							FLASH_LOG_FORMAT(Parser, Debug, "Comparing '{}' with member function '{}'",
								idenfifier_token.value(), func_decl.decl_node().identifier_token().value());
							if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
								// Found matching member function - add it to symbol table and set identifierType
								FLASH_LOG_FORMAT(Parser, Debug, "FOUND member function '{}' in context!", idenfifier_token.value());
								gSymbolTable.insert(idenfifier_token.value(), member_func.function_declaration);
								identifierType = member_func.function_declaration;
								found = true;
								found_member_function_in_context = true;
								break;
							}
						}
					}
					FLASH_LOG_FORMAT(Parser, Debug, "After search: found={}, found_member_function_in_context={}",
						found, found_member_function_in_context);

					// If not found in current struct, search in base classes
					if (!found) {
						// Get the struct's base classes and search recursively
						TypeIndex struct_type_index = mf_ctx.struct_type_index;
						if (struct_type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[struct_type_index];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								// Collect base classes to search (breadth-first to handle multiple inheritance)
								std::vector<TypeIndex> base_classes_to_search;
								for (const auto& base : struct_info->base_classes) {
									base_classes_to_search.push_back(base.type_index);
								}
								
								// Search through base classes
								for (size_t i = 0; i < base_classes_to_search.size() && !found; ++i) {
									TypeIndex base_idx = base_classes_to_search[i];
									if (base_idx >= gTypeInfo.size()) continue;
									
									const TypeInfo& base_type_info = gTypeInfo[base_idx];
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (!base_struct_info) continue;
									
									// Check member functions in this base class
									// StructMemberFunction has function_decl which is an ASTNode
									for (const auto& member_func : base_struct_info->member_functions) {
										if (member_func.getName() == idenfifier_token.handle()) {
											// Found matching member function in base class
											if (member_func.function_decl.is<FunctionDeclarationNode>()) {
												gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
												identifierType = member_func.function_decl;
												found = true;
												found_member_function_in_context = true;
												break;
											}
										}
									}
									
									// Add this base's base classes to search list (for multi-level inheritance)
									for (const auto& nested_base : base_struct_info->base_classes) {
										// Avoid duplicates (relevant for diamond inheritance)
										bool already_in_list = false;
										for (TypeIndex existing : base_classes_to_search) {
											if (existing == nested_base.type_index) {
												already_in_list = true;
												break;
											}
										}
										if (!already_in_list) {
											base_classes_to_search.push_back(nested_base.type_index);
										}
									}
								}
							}
						}
					}
				}
			}

			// Check if the identifier is a lambda variable
			// Lambda variables should not be treated as function calls here,
			// but should fall through to postfix operator parsing which will handle operator() calls
			bool is_lambda_variable = false;
			if (identifierType.has_value()) {
				// Check if this is a variable declaration with a lambda type
				if (identifierType->is<VariableDeclarationNode>()) {
					const auto& var_decl = identifierType->as<VariableDeclarationNode>();
					const DeclarationNode& decl = var_decl.declaration();
					const ASTNode& type_node = decl.type_node();
					if (type_node.is<TypeSpecifierNode>()) {
						const auto& type_spec = type_node.as<TypeSpecifierNode>();
						// Check if it's a struct type (lambdas are represented as structs)
						if (type_spec.type() == Type::Struct) {
							// Get the type index to look up the type name
							TypeIndex type_idx = type_spec.type_index();
							FLASH_LOG_FORMAT(Parser, Debug, "Checking if '{}' is lambda variable: type_idx={}, gTypeInfo.size()={}", 
								idenfifier_token.value(), type_idx, gTypeInfo.size());
							if (type_idx < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_idx];
								if (type_info.struct_info_) {
									// Check if the struct name starts with "__lambda_"
									std::string_view type_name = StringTable::getStringView(type_info.struct_info_->name);
									FLASH_LOG_FORMAT(Parser, Debug, "Type name for '{}': '{}', starts_with __lambda_: {}", 
										idenfifier_token.value(), type_name, type_name.starts_with("__lambda_"));
									if (type_name.starts_with("__lambda_")) {
										is_lambda_variable = true;
									}
								}
							}
						}
					}
				}
			}
			
			FLASH_LOG_FORMAT(Parser, Debug, "is_lambda_variable for '{}': {}", idenfifier_token.value(), is_lambda_variable);

			// Check if this is a function call or constructor call (forward reference)
			// Identifier already consumed at line 1621
			// Skip this check for lambda variables - they should be handled by postfix operator parsing
			if (!is_lambda_variable && consume("("_tok)) {
				// First, check if this is a type name (constructor call)
				auto type_it = gTypesByName.find(idenfifier_token.handle());
				if (type_it != gTypesByName.end()) {
					// This is a constructor call: TypeName(args)
					// Parse constructor arguments
					ChunkedVector<ASTNode> args;
					while (!current_token_.kind().is_eof() && 
					       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
						ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (argResult.is_error()) {
							return argResult;
						}
						if (auto node = argResult.node()) {
							args.push_back(*node);
						}
						
						if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
							advance(); // Consume comma
						}
						else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
							return ParseResult::error("Expected ',' or ')' after constructor argument", current_token_);
						}
					}
					
					if (!consume(")"_tok)) {
						FLASH_LOG(Parser, Error, "Failed to consume ')' after constructor arguments, current token: ", 
						          current_token_.value());
						return ParseResult::error("Expected ')' after constructor arguments", current_token_);
					}
				
					// Create TypeSpecifierNode for the constructor call
					TypeIndex type_index = type_it->second->type_index_;
					int type_size = 0;
					// Look up the size
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_) {
							type_size = static_cast<unsigned char>(type_info.struct_info_->total_size * 8);
						}
					}
					auto type_spec_node = emplace_node<TypeSpecifierNode>(
						Type::Struct, type_index, type_size, idenfifier_token);
				
					result = emplace_node<ExpressionNode>(
						ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Not a constructor - check if this is a template function that needs instantiation
				// Skip template lookup if we already found this as a member function in the class context
				// to avoid namespace-scope template functions shadowing class member function overloads
				std::optional<ASTNode> template_func_inst;
				if (!found_member_function_in_context && gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
					// Parse arguments to deduce template parameters
					if (peek().is_eof())
						return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

					ChunkedVector<ASTNode> args;
					std::vector<TypeSpecifierNode> arg_types;
					
					while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (argResult.is_error()) {
							return argResult;
						}

						if (auto node = argResult.node()) {
							args.push_back(*node);
							
							// Try to deduce the type of this argument
							// For now, we'll use a simple heuristic
							if (node->is<ExpressionNode>()) {
								const auto& expr = node->as<ExpressionNode>();
								Type arg_type = Type::Int;  // Default assumption
								
								std::visit([&](const auto& inner) {
									using T = std::decay_t<decltype(inner)>;
									if constexpr (std::is_same_v<T, BoolLiteralNode>) {
										arg_type = Type::Bool;
									} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
										arg_type = inner.type();
									} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
										arg_type = Type::Char;  // const char*
									} else if constexpr (std::is_same_v<T, IdentifierNode>) {
										// Look up the identifier's type
										auto id_type = lookup_symbol(StringTable::getOrInternStringHandle(inner.name()));
										if (id_type.has_value()) {
											if (const DeclarationNode* decl = get_decl_from_symbol(*id_type)) {
												if (decl->type_node().template is<TypeSpecifierNode>()) {
													arg_type = decl->type_node().template as<TypeSpecifierNode>().type();
												}
											}
										}
									}
								}, expr);
								
								arg_types.emplace_back(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token());
							}
						}

						if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
							advance(); // Consume comma
						}
						else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
							return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
						}

						if (peek().is_eof())
							return ParseResult::error(ParserError::NotImplemented, Token());
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after function call arguments", current_token_);
					}

					// Try to instantiate the template function (skip in extern "C" contexts - C has no templates)
					if (current_linkage_ != Linkage::C) {
						template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
					}
					
					if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
						const auto& func = template_func_inst->as<FunctionDeclarationNode>();
						result = emplace_node<ExpressionNode>(
							FunctionCallNode(func.decl_node(), std::move(args), idenfifier_token));
						return ParseResult::success(*result);
					} else {
						FLASH_LOG(Parser, Error, "Template instantiation failed or didn't return FunctionDeclarationNode");
						// Fall through to forward declaration
					}
				}
				
				// Not a template function, or instantiation failed
				// Create a forward declaration for the function (only if we haven't already found it)
				// Skip if we already found this as a member function in the class context
				if (!found_member_function_in_context && !identifierType.has_value()) {
					// We'll assume it returns int for now (this is a simplification)
					auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
					auto forward_decl = emplace_node<DeclarationNode>(type_node, idenfifier_token);

					// Add to GLOBAL symbol table as a forward declaration
					// Using insertGlobal ensures it persists after scope exits
					gSymbolTable.insertGlobal(idenfifier_token.value(), forward_decl);
					identifierType = forward_decl;
				}

				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}

					// Check for pack expansion: expr...
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						
						// Pack expansion: need to expand the expression for each pack element
						if (auto arg_node = argResult.node()) {
							// Simple case: if the expression is just a single identifier that looks
							// like a pack parameter, try to expand it
							if (arg_node->is<IdentifierNode>()) {
								std::string_view pack_name = arg_node->as<IdentifierNode>().name();
								
								// Try to find pack_name_0, pack_name_1, etc. in the symbol table
								size_t pack_size = 0;
								
								StringBuilder sb;
								for (size_t i = 0; i < 100; ++i) {  // reasonable limit
									// Use StringBuilder to create a persistent string
									std::string_view element_name = sb
										.append(pack_name)
										.append("_")
										.append(i)
										.preview();
									
									if (gSymbolTable.lookup(element_name).has_value()) {
										++pack_size;
									} else {
										break;
									}

									sb.reset();
								}
								sb.reset();
								
								if (pack_size > 0) {
									// Add each pack element as a separate argument
									for (size_t i = 0; i < pack_size; ++i) {
										// Use StringBuilder to create a persistent string for the token
										std::string_view element_name = sb
											.append(pack_name)
											.append("_")
											.append(i)
											.commit();
										
										Token elem_token(Token::Type::Identifier, element_name, 0, 0, 0);
										auto elem_node = emplace_node<ExpressionNode>(IdentifierNode(elem_token));
										args.push_back(elem_node);
									}
								} else {
									if (auto node = argResult.node()) {
										args.push_back(*node);
									}
								}
							} else {
								// TODO Complex expression: need full rewriting (not implemented yet)
								FLASH_LOG(Parser, Error, "Complex pack expansion not yet implemented");
								if (auto node = argResult.node()) {
									args.push_back(*node);
								}
							}
						}
					} else {
						// Regular argument
						if (auto node = argResult.node()) {
							args.push_back(*node);
						}
					}

					if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
						advance(); // Consume comma
					}
					else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
					}

					if (peek().is_eof())
						return ParseResult::error(ParserError::NotImplemented, Token());
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}

				// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
				const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration", idenfifier_token);
				}

				// If we found this member function in the current class context (or base class),
				// create a MemberFunctionCallNode with implicit 'this' as the object
				if (found_member_function_in_context && identifierType->is<FunctionDeclarationNode>()) {
					// Create implicit 'this' expression
					Token this_token(Token::Type::Keyword, "this"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));
					
					// Get the FunctionDeclarationNode
					FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					
					// Create MemberFunctionCallNode with implicit 'this'
					result = emplace_node<ExpressionNode>(
						MemberFunctionCallNode(this_node, func_decl, std::move(args), idenfifier_token));
				} else {
					auto function_call_node = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
					// If the function has a pre-computed mangled name, set it on the FunctionCallNode
					if (identifierType->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
						FLASH_LOG(Parser, Debug, "Function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
						if (func_decl.has_mangled_name()) {
							std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
							FLASH_LOG(Parser, Debug, "Set mangled name on FunctionCallNode: {}", func_decl.mangled_name());
						}
					}
					result = function_call_node;
				}
			}
			else {
				// Lambda variables should create an identifier node and return immediately
				// so postfix operator parsing can handle the operator() call
				if (is_lambda_variable) {
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Not a function call - could be a template with `<` or just missing identifier
				// Check if this might be a template: identifier<...>
				// BUT: Don't attempt for regular variables (< could be comparison)
				bool should_try_template = true;  // Default: try template parsing
				if (identifierType) {
					// Check if it's a regular variable
					bool is_regular_var = identifierType->is<VariableDeclarationNode>() || 
					                     identifierType->is<DeclarationNode>();
					should_try_template = !is_regular_var;  // Don't try for variables
				}
				// If identifierType is null (not found), default to true (might be a template)
				
				if (should_try_template && peek() == "<"_tok) {
					// Try to parse as template instantiation with member access
					auto explicit_template_args = parse_explicit_template_arguments();
					
					if (explicit_template_args.has_value()) {
						// Store parsed template args in member variable for cross-function access
						// ONLY if the next token is '(' (function call) or '::' (qualified name that might lead to function call)
						// For other cases (brace init, etc.), the args will be consumed locally
						if (!peek().is_eof() && (peek() == "("_tok || peek() == "::"_tok)) {
							pending_explicit_template_args_ = explicit_template_args;
						}
						
						// Successfully parsed template arguments
						// Now check for :: to handle Template<T>::member syntax
						if (peek() == "::"_tok) {
							// Instantiate the template to get the actual instantiated name
							std::string_view template_name = idenfifier_token.value();
							
							// Fill in default template arguments to get the actual instantiated name
							std::vector<TemplateTypeArg> filled_template_args = *explicit_template_args;
							auto template_lookup_result = gTemplateRegistry.lookupTemplate(template_name);
							if (template_lookup_result.has_value() && template_lookup_result->is<TemplateClassDeclarationNode>()) {
								const auto& template_class = template_lookup_result->as<TemplateClassDeclarationNode>();
								const auto& template_params = template_class.template_parameters();
								
								// Helper lambda to build instantiated template name suffix
								// Fill in defaults for missing parameters
								for (size_t param_idx = filled_template_args.size(); param_idx < template_params.size(); ++param_idx) {
									const TemplateParameterNode& param = template_params[param_idx].as<TemplateParameterNode>();
									if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
										const ASTNode& default_node = param.default_value();
										if (default_node.is<TypeSpecifierNode>()) {
											const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
											filled_template_args.push_back(TemplateTypeArg(default_type));
										}
									} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
										const ASTNode& default_node = param.default_value();
										if (default_node.is<ExpressionNode>()) {
											const ExpressionNode& expr_default = default_node.as<ExpressionNode>();
											
											if (std::holds_alternative<QualifiedIdentifierNode>(expr_default)) {
												const QualifiedIdentifierNode& qual_id_default = std::get<QualifiedIdentifierNode>(expr_default);
												
												if (!qual_id_default.namespace_handle().isGlobal()) {
													std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id_default.namespace_handle());
													std::string_view member_name = qual_id_default.name();
													
													// Check for dependent placeholder using TypeInfo-based detection
													auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
													
													if (is_dependent_placeholder && !filled_template_args.empty()) {
														// Build instantiated name using hash-based naming
														std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
														
														try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
														
														auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
														if (type_it != gTypesByName.end()) {
															const TypeInfo* type_info = type_it->second;
															if (type_info->getStructInfo()) {
																const StructTypeInfo* struct_info = type_info->getStructInfo();
																for (const auto& static_member : struct_info->static_members) {
																	if (StringTable::getStringView(static_member.getName()) == member_name) {
																		if (static_member.initializer.has_value()) {
																			const ASTNode& init_node = *static_member.initializer;
																			if (init_node.is<ExpressionNode>()) {
																				const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																				if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																					bool val = std::get<BoolLiteralNode>(init_expr).value();
																					filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
																				} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																					const auto& val = lit.value();
																					if (std::holds_alternative<unsigned long long>(val)) {
																						filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																					}
																				}
																			}
																		}
																		break;
																	}
																}
															}
														}
													}
												}
											} else if (std::holds_alternative<NumericLiteralNode>(expr_default)) {
												const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr_default);
												const auto& val = lit.value();
												if (std::holds_alternative<unsigned long long>(val)) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
												} else if (std::holds_alternative<double>(val)) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
												}
											} else if (std::holds_alternative<BoolLiteralNode>(expr_default)) {
												const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr_default);
												filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
											}
										}
									}
								}
							}
							
							std::string_view instantiated_name = get_instantiated_class_name(template_name, filled_template_args);
							try_instantiate_class_template(template_name, filled_template_args);
							
							// Parse qualified identifier after template, using the instantiated name
							// We need to collect the :: path ourselves since we have the instantiated name
							std::vector<StringType<32>> namespaces;
							Token final_identifier = idenfifier_token;
							
							// Collect the qualified path after ::
							while (peek() == "::"_tok) {
								// Current identifier becomes a namespace part (but use instantiated name for first part)
								if (namespaces.empty()) {
									namespaces.emplace_back(StringType<32>(instantiated_name));
								} else {
									namespaces.emplace_back(StringType<32>(final_identifier.value()));
								}
								advance(); // consume ::
								
								// Handle ::template syntax for dependent names (e.g., __xref<T>::template __type)
								if (peek() == "template"_tok) {
									advance(); // consume 'template' keyword
								}
								
								// Get next identifier
								if (!peek().is_identifier()) {
									pending_explicit_template_args_.reset(); // Clear pending to avoid leaking to unrelated calls
									return ParseResult::error("Expected identifier after '::'", peek_info());
								}
								final_identifier = peek_info();
								advance(); // consume the identifier
							}
							
							// Try to parse member template function call: Template<T>::member<U>()
							auto func_call_result = try_parse_member_template_function_call(
								instantiated_name, final_identifier.value(), final_identifier);
							if (func_call_result.has_value()) {
								if (func_call_result->is_error()) {
									return *func_call_result;
								}
								result = *func_call_result->node();
								pending_explicit_template_args_.reset();
								return ParseResult::success(*result);
							}
							
							// Create a QualifiedIdentifierNode with the instantiated type name
							NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
							auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
							const auto& qualified_node = qualified_node_ast.as<QualifiedIdentifierNode>();
							result = emplace_node<ExpressionNode>(qualified_node);
							// Clear pending template args since they were used for this qualified identifier
							pending_explicit_template_args_.reset();
							return ParseResult::success(*result);
						}
						
						// Template arguments parsed but NOT followed by ::
						// Check for template class brace initialization: Template<T>{}
						// This creates a temporary object using value-initialization or aggregate-initialization
						if (!identifierType && peek() == "{"_tok) {
							// This is template class brace initialization (e.g., type_identity<int>{})
							// Check if any template arguments are dependent
							bool has_dependent_args = false;
							for (const auto& arg : *explicit_template_args) {
								if (arg.is_dependent || arg.is_pack) {
									has_dependent_args = true;
									break;
								}
							}
							
							auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
							if (class_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Template brace initialization detected for '", idenfifier_token.value(), "', has_dependent_args=", has_dependent_args);
								
								if (has_dependent_args) {
									// Dependent template arguments - create a placeholder for now
									// The actual instantiation will happen when the outer template is instantiated
									advance(); // consume '{'
									
									// Skip the brace content - should be empty {} for value-initialization
									ChunkedVector<ASTNode> args;
									while (!peek().is_eof() && peek() != "}"_tok) {
										auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (argResult.is_error()) {
											return argResult;
										}
										if (auto node = argResult.node()) {
											args.push_back(*node);
										}
										
										if (peek() == ","_tok) {
											advance(); // consume ','
										} else if (peek() != "}"_tok) {
											return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
										}
									}
									
									if (!consume("}"_tok)) {
										return ParseResult::error("Expected '}' after brace initializer", current_token_);
									}
									
									// For dependent args, create a placeholder ConstructorCallNode
									// The actual type will be resolved during template instantiation
									// Use a placeholder type for now
									auto placeholder_type_node = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, idenfifier_token);
									result = emplace_node<ExpressionNode>(ConstructorCallNode(placeholder_type_node, std::move(args), idenfifier_token));
									return ParseResult::success(*result);
								}
								
								// Non-dependent template arguments - instantiate the class template
								try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
								
								// Build the instantiated type name to look up the type
								std::string_view instantiated_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
								
								// Look up the instantiated type
								auto type_handle = StringTable::getOrInternStringHandle(instantiated_name);
								auto type_it = gTypesByName.find(type_handle);
								
								// If not found, the type may have been registered with filled-in default template args
								// (e.g., basic_string_view<char> → basic_string_view<char, char_traits<char>>)
								// Check the cache for the instantiated struct node to get the correct name
								if (type_it == gTypesByName.end()) {
									auto cached = gTemplateRegistry.getInstantiation(
										StringTable::getOrInternStringHandle(idenfifier_token.value()),
										*explicit_template_args);
									if (cached.has_value() && cached->is<StructDeclarationNode>()) {
										StringHandle cached_name = cached->as<StructDeclarationNode>().name();
										auto cached_it = gTypesByName.find(cached_name);
										if (cached_it != gTypesByName.end()) {
											type_handle = cached_name;
											type_it = cached_it;
										}
									}
								}
								
								if (type_it != gTypesByName.end()) {
									// Found the instantiated type - now parse the brace initializer
									advance(); // consume '{'
									
									ChunkedVector<ASTNode> args;
									while (!peek().is_eof() && peek() != "}"_tok) {
										auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (argResult.is_error()) {
											return argResult;
										}
										if (auto node = argResult.node()) {
											args.push_back(*node);
										}
										
										if (peek() == ","_tok) {
											advance(); // consume ','
										} else if (peek() != "}"_tok) {
											return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
										}
									}
									
									if (!consume("}"_tok)) {
										return ParseResult::error("Expected '}' after brace initializer", current_token_);
									}
									
									// Create TypeSpecifierNode for the instantiated class
									const TypeInfo& type_info = *type_it->second;
									TypeIndex type_index = type_info.type_index_;
									int type_size = 0;
									if (type_info.struct_info_) {
										type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
									}
									auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
									
									// Create ConstructorCallNode
									result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
									return ParseResult::success(*result);
								}
							}
						}
						
						// Handle functional-style cast for class templates: Template<Args>()
						// This creates a temporary object of the instantiated class type
						// Pattern: hash<_Tp>() creates a temporary hash<_Tp> object
						if (!identifierType && peek() == "("_tok) {
							auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
							if (class_template_opt.has_value() && class_template_opt->is<TemplateClassDeclarationNode>()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Functional-style cast for class template '{}' with template args", idenfifier_token.value());
								
								// Build the instantiated type name using hash-based naming
								std::string_view instantiated_type_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
								
								// Try to instantiate the class template (may fail for dependent args, which is OK)
								try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
								
								// Consume '(' and parse constructor arguments
								advance(); // consume '('
								
								// Parse constructor arguments
								ChunkedVector<ASTNode> args;
								if (current_token_.value() != ")") {
									while (true) {
										auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (arg_result.is_error()) {
											return arg_result;
										}
										if (auto arg = arg_result.node()) {
											args.push_back(*arg);
										}
										
										if (current_token_.kind().is_eof() || current_token_.value() != ",") {
											break;
										}
										advance(); // consume ','
									}
								}
								
								if (!consume(")"_tok)) {
									return ParseResult::error("Expected ')' after constructor arguments", current_token_);
								}
								
								// Create TypeSpecifierNode for the instantiated template type
								Token inst_type_token(Token::Type::Identifier, instantiated_type_name,
								                      idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
								auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, inst_type_token);
								
								// Create ConstructorCallNode for functional-style cast
								result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), inst_type_token));
								return ParseResult::success(*result);
							}
						}
						
						// Check if this is a template alias - if so, treat as valid dependent expression
						// This handles patterns like: __enable_if_t<...> in template argument contexts
						if (!identifierType) {
							auto alias_opt = gTemplateRegistry.lookup_alias_template(idenfifier_token.value());
							
							// If not found directly, try looking up as a member alias template of the enclosing class
							if (!alias_opt.has_value() && !struct_parsing_context_stack_.empty()) {
								const auto& sp_ctx = struct_parsing_context_stack_.back();
								StringBuilder qualified_alias_name;
								qualified_alias_name.append(sp_ctx.struct_name).append("::"sv).append(idenfifier_token.value());
								std::string_view qualified_alias_name_sv = qualified_alias_name.commit();
								alias_opt = gTemplateRegistry.lookup_alias_template(qualified_alias_name_sv);
								if (alias_opt.has_value()) {
									FLASH_LOG_FORMAT(Parser, Debug, "Found member template alias '{}' as '{}'", idenfifier_token.value(), qualified_alias_name_sv);
								}
							}
							
							if (alias_opt.has_value()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Found template alias '{}' with template arguments (no ::)", idenfifier_token.value());
								// For template aliases used in expression/template contexts, create a simple identifier
								// The template instantiation will be handled during type resolution
								result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
								return ParseResult::success(*result);
							}
							
							// Check if this is a variable template (e.g., is_reference_v<T>)
							auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
							
							// If not found directly, try namespace-qualified lookup
							if (!var_template_opt.has_value()) {
								NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
								if (!current_namespace.isGlobal()) {
									StringHandle name_handle = idenfifier_token.handle();
									StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
									std::string_view qualified_name = StringTable::getStringView(qualified_handle);
									var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
									if (var_template_opt.has_value()) {
										FLASH_LOG_FORMAT(Parser, Debug, "Found variable template '{}' as '{}'", idenfifier_token.value(), qualified_name);
										// Use the qualified name for instantiation
										auto instantiated_var = try_instantiate_variable_template(qualified_name, *explicit_template_args);
										if (instantiated_var.has_value()) {
											std::string_view inst_name;
											if (instantiated_var->is<VariableDeclarationNode>()) {
												const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
												const auto& decl = var_decl.declaration();
												inst_name = decl.identifier_token().value();
											} else if (instantiated_var->is<DeclarationNode>()) {
												const auto& decl = instantiated_var->as<DeclarationNode>();
												inst_name = decl.identifier_token().value();
											} else {
												inst_name = idenfifier_token.value();
											}
											Token inst_token(Token::Type::Identifier, inst_name, 
											                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
											result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
											return ParseResult::success(*result);
										} else {
											// Variable template found but couldn't instantiate (likely dependent args).
											// Preserve template args in a FunctionCallNode so the ExpressionSubstitutor
											// can instantiate it after substituting the template parameter (e.g. _Size→size_t).
											FLASH_LOG_FORMAT(Parser, Debug, "Variable template '{}' (qualified as '{}') found but not instantiated (dependent args)", idenfifier_token.value(), qualified_name);
											TypeSpecifierNode& stub_type_sv = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(Type::Auto, TypeQualifier::None, 0, idenfifier_token);
											DeclarationNode& stub_decl_sv = gChunkedAnyStorage.emplace_back<DeclarationNode>(ASTNode(&stub_type_sv), idenfifier_token);
											ChunkedVector<ASTNode> no_args_sv;
											FunctionCallNode& var_call_sv = gChunkedAnyStorage.emplace_back<FunctionCallNode>(stub_decl_sv, std::move(no_args_sv), idenfifier_token);
											std::vector<ASTNode> targ_nodes_sv;
											for (const auto& targ : *explicit_template_args) {
												if (targ.is_dependent && targ.dependent_name.isValid()) {
													Token dep_token(Token::Type::Identifier, targ.dependent_name.view(),
													                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
													ExpressionNode& dep_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
														TemplateParameterReferenceNode(targ.dependent_name, dep_token));
													targ_nodes_sv.push_back(ASTNode(&dep_expr));
												} else {
													TypeSpecifierNode& tts = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(targ.base_type, targ.type_index, get_type_size_bits(targ.base_type), idenfifier_token);
													targ_nodes_sv.push_back(ASTNode(&tts));
												}
											}
											if (!targ_nodes_sv.empty())
												var_call_sv.set_template_arguments(std::move(targ_nodes_sv));
											var_call_sv.set_qualified_name(qualified_name);
											result = emplace_node<ExpressionNode>(var_call_sv);
											return ParseResult::success(*result);
										}
									}
								}
							}
							
							if (var_template_opt.has_value()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Found variable template '{}' with template arguments (no ::)", idenfifier_token.value());
								auto instantiated_var = try_instantiate_variable_template(idenfifier_token.value(), *explicit_template_args);
								if (instantiated_var.has_value()) {
									std::string_view inst_name;
									if (instantiated_var->is<VariableDeclarationNode>()) {
										const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
										const auto& decl = var_decl.declaration();
										inst_name = decl.identifier_token().value();
									} else if (instantiated_var->is<DeclarationNode>()) {
										const auto& decl = instantiated_var->as<DeclarationNode>();
										inst_name = decl.identifier_token().value();
									} else {
										inst_name = idenfifier_token.value();
									}
									Token inst_token(Token::Type::Identifier, inst_name, 
									                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
									result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
									return ParseResult::success(*result);
								} else {
									// Variable template found but couldn't instantiate (likely dependent args).
									// Preserve template args in a FunctionCallNode so the ExpressionSubstitutor
									// can instantiate it after substituting the template parameter (e.g. _Size→size_t).
									FLASH_LOG_FORMAT(Parser, Debug, "Variable template '{}' found but not instantiated (dependent args)", idenfifier_token.value());
									TypeSpecifierNode& stub_type_vt = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(Type::Auto, TypeQualifier::None, 0, idenfifier_token);
									DeclarationNode& stub_decl_vt = gChunkedAnyStorage.emplace_back<DeclarationNode>(ASTNode(&stub_type_vt), idenfifier_token);
									ChunkedVector<ASTNode> no_args_vt;
									FunctionCallNode& var_call_vt = gChunkedAnyStorage.emplace_back<FunctionCallNode>(stub_decl_vt, std::move(no_args_vt), idenfifier_token);
									std::vector<ASTNode> targ_nodes_vt;
									for (const auto& targ : *explicit_template_args) {
										if (targ.is_dependent && targ.dependent_name.isValid()) {
											Token dep_token(Token::Type::Identifier, targ.dependent_name.view(),
											                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
											ExpressionNode& dep_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
												TemplateParameterReferenceNode(targ.dependent_name, dep_token));
											targ_nodes_vt.push_back(ASTNode(&dep_expr));
										} else {
											TypeSpecifierNode& tts = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(targ.base_type, targ.type_index, get_type_size_bits(targ.base_type), idenfifier_token);
											targ_nodes_vt.push_back(ASTNode(&tts));
										}
									}
									if (!targ_nodes_vt.empty())
										var_call_vt.set_template_arguments(std::move(targ_nodes_vt));
									result = emplace_node<ExpressionNode>(var_call_vt);
									return ParseResult::success(*result);
								}
							}
							
							// Check if this is a concept application (e.g., default_constructible<HasDefault>)
							// Concepts evaluate to boolean values at compile time
							auto concept_opt = gConceptRegistry.lookupConcept(idenfifier_token.value());
							if (concept_opt.has_value() && explicit_template_args.has_value()) {
								// Check if any template arguments are dependent (referencing template parameters)
								// If so, we can't evaluate the concept yet - defer to instantiation time
								bool has_dependent_args = false;
								for (const auto& arg : *explicit_template_args) {
									if (arg.is_dependent) {
										has_dependent_args = true;
										break;
									}
								}
								
								if (has_dependent_args) {
									// Defer evaluation - create a FunctionCallNode to preserve the concept application
									FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with DEPENDENT template arguments - deferring evaluation", idenfifier_token.value());
									
									// Create a FunctionCallNode that will be evaluated during instantiation
									// The concept name is stored in the token, template args are already parsed
									Token concept_token = idenfifier_token;
									
									// Create a dummy declaration for the concept call
									Token void_token(Token::Type::Keyword, "void"sv, concept_token.line(), concept_token.column(), concept_token.file_index());
									auto void_type = emplace_node<TypeSpecifierNode>(
										Type::Void, 0, 0, void_token, CVQualifier::None);
									auto concept_decl = emplace_node<DeclarationNode>(void_type, concept_token);
									
									auto [func_call_node, func_call_ref] = emplace_node_ref<FunctionCallNode>(
										concept_decl.as<DeclarationNode>(), 
										ChunkedVector<ASTNode>(),
										concept_token);
									
									// Store the template arguments for later evaluation
									std::vector<ASTNode> template_arg_nodes;
									for (const auto& arg : *explicit_template_args) {
										// Convert TemplateTypeArg to an appropriate expression node
										if (arg.is_dependent && arg.dependent_name.isValid()) {
											Token dep_token(Token::Type::Identifier, arg.dependent_name.view(),
											               concept_token.line(), concept_token.column(), concept_token.file_index());
											auto dep_node = emplace_node<ExpressionNode>(IdentifierNode(dep_token));
											template_arg_nodes.push_back(dep_node);
										} else if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
											std::string_view type_name = StringTable::getStringView(gTypeInfo[arg.type_index].name_);
											Token type_token(Token::Type::Identifier, type_name,
											                concept_token.line(), concept_token.column(), concept_token.file_index());
											auto type_node = emplace_node<ExpressionNode>(IdentifierNode(type_token));
											template_arg_nodes.push_back(type_node);
										}
									}
									func_call_ref.set_template_arguments(std::move(template_arg_nodes));
									
									result = emplace_node<ExpressionNode>(func_call_node.as<FunctionCallNode>());
									return ParseResult::success(*result);
								}
								
								FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with concrete template arguments", idenfifier_token.value());
								
								// Evaluate the concept constraint with the provided template arguments
								auto constraint_result = evaluateConstraint(
									concept_opt->as<ConceptDeclarationNode>().constraint_expr(),
									*explicit_template_args,
									{}  // No template param names needed for concrete types
								);
								
								// Create a BoolLiteralNode with the result
								bool concept_satisfied = constraint_result.satisfied;
								Token bool_token(Token::Type::Keyword, concept_satisfied ? "true"sv : "false"sv,
								                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
								result = emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, concept_satisfied));
								return ParseResult::success(*result);
							}
							
							// Check for member template function (including current struct and inherited from base classes)
							// Example: __helper<_Tp>({}) where __helper is in the same struct or base class
							// Template args already parsed at this point
							if (!struct_parsing_context_stack_.empty() && peek() == "("_tok) {
								const auto& sp_ctx2 = struct_parsing_context_stack_.back();
								const StructDeclarationNode* struct_node = sp_ctx2.struct_node;
								if (struct_node) {
									StringHandle id_handle = idenfifier_token.handle();
									
									// First, check the current struct's member functions (including those parsed so far)
									for (const auto& member_func_decl : struct_node->member_functions()) {
										const ASTNode& func_node = member_func_decl.function_declaration;
										// Check if this is a template function
										if (func_node.is<TemplateFunctionDeclarationNode>()) {
											const TemplateFunctionDeclarationNode& template_func = func_node.as<TemplateFunctionDeclarationNode>();
											const FunctionDeclarationNode& func_decl = template_func.function_declaration().as<FunctionDeclarationNode>();
											StringHandle func_name = func_decl.decl_node().identifier_token().handle();
											if (func_name == id_handle) {
												FLASH_LOG(Parser, Debug, "Found member template function '", idenfifier_token.value(), "' in current struct");
												gSymbolTable.insert(idenfifier_token.value(), func_node);
												identifierType = func_node;
												goto inherited_template_found;
											}
										}
									}
									
									// If not found in current struct, check base classes
									for (const auto& base : struct_node->base_classes()) {
										auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
										if (base_type_it != gTypesByName.end()) {
											const TypeInfo* base_type_info = base_type_it->second;
											const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
											if (base_struct_info) {
												for (const auto& member_func : base_struct_info->member_functions) {
													if (member_func.getName() == id_handle) {
														if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
															FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class '", base.name, "'");
															gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
															identifierType = member_func.function_decl;
															// Don't return - let normal function call parsing proceed
															// The template args are already parsed, and we need to parse the function call
															goto inherited_template_found;
														}
													}
												}
											}
										}
									}
								}
							}
						}
					inherited_template_found:;
					}
				}
			
				// Check if we're parsing a template and this identifier is a template parameter
				if (!identifierType && (parsing_template_class_ || !current_template_param_names_.empty())) {
					// Check if this identifier matches any template parameter name
					for (const auto& param_name : current_template_param_names_) {
						if (param_name == idenfifier_token.value()) {
							// This is a template parameter reference
							// Check if we have a substitution value (for deferred template body parsing)
							bool substituted = false;
							for (const auto& subst : template_param_substitutions_) {
								if (subst.param_name == param_name && subst.is_value_param) {
									// Substitute with actual value - return immediately
									// Use StringBuilder.append(int64_t) to persist the string value (avoids temporary strings)
									StringBuilder value_str;
									value_str.append(subst.value);  // Directly append int64_t without std::to_string()
									std::string_view value_view = value_str.commit();
									Token num_token(Token::Type::Literal, value_view, 
									                idenfifier_token.line(), idenfifier_token.column(), 
									                idenfifier_token.file_index());
									result = emplace_node<ExpressionNode>(
										NumericLiteralNode(num_token, 
										                   static_cast<unsigned long long>(subst.value), 
										                   subst.value_type, 
										                   TypeQualifier::None, 
										                   get_type_size_bits(subst.value_type))
									);
									FLASH_LOG(Templates, Debug, "Substituted template parameter '", param_name, 
									          "' with value ", subst.value);
									// Return the substituted value immediately
									return ParseResult::success(*result);
								}
							}
							
							if (!substituted) {
								// No substitution - create TemplateParameterReferenceNode as before
								// Don't return yet - we need to check if this is a constructor call T(...)
								result = emplace_node<ExpressionNode>(TemplateParameterReferenceNode(param_name, idenfifier_token));
								// Set identifierType so the constructor call logic below can detect it
								identifierType = result;
							}
							break;
						}
					}
				}
				
				// Check if this identifier is a concept name
				// Concepts are used in requires clauses: requires Concept<T>
				if (!identifierType && gConceptRegistry.hasConcept(idenfifier_token.value())) {
					// Try to parse template arguments: Concept<T>
					if (peek() == "<"_tok) {
						auto template_args = parse_explicit_template_arguments();
						if (template_args.has_value()) {
							// Create a concept check expression
							// We'll represent this as an identifier with the concept name and args attached
							// The constraint evaluator will handle the actual check
							// For now, just wrap it in an identifier node
							result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
							return ParseResult::success(*result);
						}
					}
					// Concept without template args - just an identifier reference
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}

				// Not a function call, template member access, or template parameter reference
				// But allow pack expansion (identifier...)
				if (!identifierType && is_pack_expansion) {
					// Create a simple identifier node - the pack expansion will be handled by the caller
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Before reporting error, check if this could be a template alias or class template usage
				// Example: remove_const_t<T> where remove_const_t is defined as "using remove_const_t = typename remove_const<T>::type;"
				// Or: type_identity<T>{} for template class brace initialization
				if (!identifierType && peek() == "<"_tok) {
					// Check if this is an alias template
					auto alias_opt = gTemplateRegistry.lookup_alias_template(idenfifier_token.value());
					
					// If not found directly, try looking up as a member alias template of the enclosing class
					// This handles patterns like: template<typename T, typename U> using cond_t = decltype(...);
					// used within the same struct as: decltype(cond_t<T, U>())
					if (!alias_opt.has_value() && !struct_parsing_context_stack_.empty()) {
						const auto& sp_ctx3 = struct_parsing_context_stack_.back();
						// Build qualified name: EnclosingClass::MemberAliasTemplate
						StringBuilder qualified_alias_name;
						qualified_alias_name.append(sp_ctx3.struct_name).append("::"sv).append(idenfifier_token.value());
						std::string_view qualified_alias_name_sv = qualified_alias_name.commit();
						alias_opt = gTemplateRegistry.lookup_alias_template(qualified_alias_name_sv);
						if (alias_opt.has_value()) {
							FLASH_LOG(Parser, Debug, "Found member alias template '", idenfifier_token.value(), "' as '", qualified_alias_name_sv, "'");
						}
					}
					
					if (alias_opt.has_value()) {
						// This is an alias template like "remove_const_t<T>"
						// We need to instantiate it, which will happen in the normal template arg parsing flow below
						// Set a marker that we found an alias template so we can handle it later
						// For now, create a placeholder node and let the template instantiation logic handle it
						FLASH_LOG(Parser, Debug, "Found alias template '", idenfifier_token.value(), "' in expression context");
						// Don't return yet - let it fall through to template argument parsing below
					} else {
						// Check if this is a class template (for expressions like type_identity<T>{})
						auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
						FLASH_LOG(Parser, Debug, "Looking up class template '", idenfifier_token.value(), "', found=", class_template_opt.has_value());
						
						// If not found directly, try looking up as a member struct template of the enclosing class
						// This handles patterns like: template<typename T> struct Select<Wrapper<T>> { };
						// where Wrapper is a member struct template of the same class
						if (!class_template_opt.has_value() && !struct_parsing_context_stack_.empty()) {
							const auto& sp_ctx4 = struct_parsing_context_stack_.back();
							// Build qualified name: EnclosingClass::MemberTemplate
							StringBuilder qualified_name;
							qualified_name.append(sp_ctx4.struct_name).append("::"sv).append(idenfifier_token.value());
							std::string_view qualified_name_sv = qualified_name.commit();
							class_template_opt = gTemplateRegistry.lookupTemplate(qualified_name_sv);
							if (class_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Found member struct template '", idenfifier_token.value(), "' as '", qualified_name_sv, "'");
							}
						}
						
						if (class_template_opt.has_value()) {
							FLASH_LOG(Parser, Debug, "Found class template '", idenfifier_token.value(), "' in expression context");
							// Mark as found to prevent "Missing identifier" error
							found_as_type_alias = true;  // Reuse this flag - class template acts like a type name
							// Don't return - let it fall through to template argument parsing below
						} else {
							// Check if this is a variable template (e.g., is_reference_v<T>)
							auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
							
							// If not found directly, try namespace-qualified lookup
							if (!var_template_opt.has_value()) {
								NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
								if (!current_namespace.isGlobal()) {
									StringHandle name_handle = idenfifier_token.handle();
									StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
									std::string_view qualified_name = StringTable::getStringView(qualified_handle);
									var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
									if (var_template_opt.has_value()) {
										FLASH_LOG(Parser, Debug, "Found variable template '", idenfifier_token.value(), "' as '", qualified_name, "'");
									}
								}
							}
							
							if (var_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Found variable template '", idenfifier_token.value(), "' in expression context");
								// Don't return - let it fall through to template argument parsing below
							} else if (!found_as_type_alias) {
								// Check if this is an inherited member template function (e.g., __test<_Tp>(0) from <type_traits>)
								// This pattern is used for SFINAE detection where a derived class calls a base class template function
								bool found_inherited_template = false;
								
								// First try member_function_context_stack_ (for code inside member function bodies)
								if (!member_function_context_stack_.empty()) {
									const auto& mf_ctx2 = member_function_context_stack_.back();
									TypeIndex struct_type_index = mf_ctx2.struct_type_index;
									if (struct_type_index < gTypeInfo.size()) {
										const TypeInfo& type_info = gTypeInfo[struct_type_index];
										const StructTypeInfo* struct_info = type_info.getStructInfo();
										if (struct_info) {
											// Search through base classes for member template functions
											std::vector<TypeIndex> base_classes_to_search;
											for (const auto& base : struct_info->base_classes) {
												base_classes_to_search.push_back(base.type_index);
											}
											
											StringHandle id_handle = idenfifier_token.handle();
											for (size_t i = 0; i < base_classes_to_search.size() && !found_inherited_template; ++i) {
												TypeIndex base_idx = base_classes_to_search[i];
												if (base_idx >= gTypeInfo.size()) continue;
												
												const TypeInfo& base_type_info = gTypeInfo[base_idx];
												const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
												if (!base_struct_info) continue;
												
												// Check member functions in this base class for template functions
												for (const auto& member_func : base_struct_info->member_functions) {
													if (member_func.getName() == id_handle) {
														// Found a match - check if it's a template function
														if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
															FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class (member function context)");
															// Add to symbol table and set identifierType
															gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
															identifierType = member_func.function_decl;
															found_inherited_template = true;
															break;
														}
													}
												}
												
												// Add this base's base classes to search list (for multi-level inheritance)
												for (const auto& nested_base : base_struct_info->base_classes) {
													bool already_in_list = false;
													for (TypeIndex existing : base_classes_to_search) {
														if (existing == nested_base.type_index) {
															already_in_list = true;
															break;
														}
													}
													if (!already_in_list) {
														base_classes_to_search.push_back(nested_base.type_index);
													}
												}
											}
										}
									}
								}
								
								// If not found in member function context, try struct_parsing_context_stack_
								// This handles expressions in type aliases like: using type = decltype(__test<_Tp>(0));
								if (!found_inherited_template && !struct_parsing_context_stack_.empty()) {
									const auto& sp_ctx5 = struct_parsing_context_stack_.back();
									const StructDeclarationNode* struct_node = sp_ctx5.struct_node;
									if (struct_node) {
										// Get base classes from the struct AST node
										StringHandle id_handle = idenfifier_token.handle();
										for (const auto& base : struct_node->base_classes()) {
											// Look up the base class type
											auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
											if (base_type_it != gTypesByName.end()) {
												const TypeInfo* base_type_info = base_type_it->second;
												const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
												if (base_struct_info) {
													// Check member functions for template functions
													for (const auto& member_func : base_struct_info->member_functions) {
														if (member_func.getName() == id_handle) {
															// Found a match - check if it's a template function
															if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
																FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class (struct parsing context)");
																// Add to symbol table and set identifierType
																gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
																identifierType = member_func.function_decl;
																found_inherited_template = true;
																break;
															}
														}
													}
													if (found_inherited_template) break;
												}
											}
										}
									}
								}
								
								if (!found_inherited_template) {
									// Not an alias template, class template, variable template, inherited member template, or found anywhere
									FLASH_LOG(Parser, Error, "Missing identifier: ", idenfifier_token.value());
									return ParseResult::error("Missing identifier", idenfifier_token);
								}
							}
						}
					}
				} else if (!identifierType && !found_as_type_alias) {
					// Not a function call, template member access, template parameter reference, pack expansion, or alias template
					// In template context, treat unknown identifiers as potentially member references that will resolve at instantiation time
					// Also check if the identifier is a pack parameter name - during template body re-parsing,
					// pack parameters (e.g., "args") are expanded to args_0, args_1, etc. but the original
					// name must still be accepted when used in pack expansion patterns like func(transform(args)...)
					bool is_pack_param = false;
					for (const auto& pack_info : pack_param_info_) {
						if (idenfifier_token.value() == pack_info.original_name) {
							is_pack_param = true;
							break;
						}
					}
					if (parsing_template_body_ || !current_template_param_names_.empty() || !struct_parsing_context_stack_.empty() || is_pack_param) {
						FLASH_LOG(Parser, Debug, "Treating unknown identifier '", idenfifier_token.value(), "' as dependent in template context");
						result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
						// Don't return error - let it continue as a dependent expression
					} else {
						FLASH_LOG(Parser, Error, "Missing identifier: ", idenfifier_token.value());
						return ParseResult::error("Missing identifier", idenfifier_token);
					}
				}
			}
		}
		if (identifierType && (!identifierType->is<DeclarationNode>() && 
					!identifierType->is<FunctionDeclarationNode>() && 
					!identifierType->is<VariableDeclarationNode>() &&
					!identifierType->is<TemplateFunctionDeclarationNode>() &&
					!identifierType->is<TemplateVariableDeclarationNode>() &&
					!identifierType->is<TemplateParameterReferenceNode>())) {
			FLASH_LOG(Parser, Error, "Identifier type check failed, type_name=", identifierType->type_name());
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, current_token_);
		}
		else {
			// Identifier already consumed at line 1621

			// Check for explicit template arguments: identifier<type1, type2>(args)
			// BUT: Don't attempt template argument parsing for regular variables (could be < comparison)
			std::optional<std::vector<TemplateTypeArg>> explicit_template_args;
			std::vector<ASTNode> explicit_template_arg_nodes;  // Store AST nodes for template arguments
			bool should_try_template_args = true;  // Default: try template parsing
			
			// Only skip template argument parsing if we KNOW it's a regular variable
			if (identifierType) {
				// Check if it's a regular variable
				bool is_regular_var = identifierType->is<VariableDeclarationNode>() || 
				                     identifierType->is<DeclarationNode>();
				
				if (is_regular_var) {
					// It's definitely a variable, don't try template args
					should_try_template_args = false;
				}
				// For all other cases (templates, functions, unknown), try template args
			}
			// If identifierType is null (not found), default to true (might be a template)
			
			if (should_try_template_args && peek() == "<"_tok) {
				explicit_template_args = parse_explicit_template_arguments(&explicit_template_arg_nodes);
				// If parsing failed, it might be a less-than operator, so continue normally
				
				// After template arguments, check for :: to handle Template<T>::member syntax
				if (explicit_template_args.has_value() && peek() == "::"_tok) {
					// Instantiate the template to ensure defaults are filled in
					// This returns the instantiated struct node
					auto instantiation_result = try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
					
					// Get the instantiated class name with defaults filled in
					std::string_view instantiated_class_name;
					if (instantiation_result.has_value() && instantiation_result->is<StructDeclarationNode>()) {
						// Get the name from the instantiated struct
						const StructDeclarationNode& inst_struct = instantiation_result->as<StructDeclarationNode>();
						instantiated_class_name = StringTable::getStringView(inst_struct.name());
					} else {
						// Fallback: build name from explicit args (may be missing defaults)
						instantiated_class_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
					}
					
					// Create a token with the instantiated name to pass to parse_qualified_identifier_after_template
					Token instantiated_token(Token::Type::Identifier, instantiated_class_name,
					                        idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					
					// Parse qualified identifier after template
					auto qualified_result = parse_qualified_identifier_after_template(instantiated_token);
					if (!qualified_result.is_error() && qualified_result.node().has_value()) {
						auto qualified_node = qualified_result.node()->as<QualifiedIdentifierNode>();
						
						// Try to parse member template function call: Template<T>::member<U>()
						auto func_call_result = try_parse_member_template_function_call(
							instantiated_class_name, qualified_node.name(), qualified_node.identifier_token());
						if (func_call_result.has_value()) {
							if (func_call_result->is_error()) {
								return *func_call_result;
							}
							result = *func_call_result->node();
							return ParseResult::success(*result);
						}
						
						// Not a function call - return as qualified identifier
						result = emplace_node<ExpressionNode>(qualified_node);
						return ParseResult::success(*result);
					}
				}
				
				// Check if this is a variable template usage (identifier<args> without following '(')
				if (explicit_template_args.has_value() && 
				    (peek() != "("_tok)) {
					// Try to instantiate as variable template
					// First try unqualified name
					auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
					std::string_view template_name_to_use = idenfifier_token.value();
					
					// If not found, try namespace-qualified lookup
					if (!var_template_opt.has_value()) {
						NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
						if (!current_namespace.isGlobal()) {
							StringHandle name_handle = idenfifier_token.handle();
							StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
							std::string_view qualified_name = StringTable::getStringView(qualified_handle);
							var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
							if (var_template_opt.has_value()) {
								template_name_to_use = qualified_name;
								FLASH_LOG_FORMAT(Templates, Debug, "Found variable template with namespace-qualified name: {}", qualified_name);
							}
						}
					}
					
					if (var_template_opt.has_value()) {
						auto instantiated_var = try_instantiate_variable_template(template_name_to_use, *explicit_template_args);
						if (instantiated_var.has_value()) {
							// Could be VariableDeclarationNode (first instantiation) or DeclarationNode (already instantiated)
							std::string_view inst_name;
							if (instantiated_var->is<VariableDeclarationNode>()) {
								const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
								const auto& decl = var_decl.declaration();
								inst_name = decl.identifier_token().value();
							} else if (instantiated_var->is<DeclarationNode>()) {
								const auto& decl = instantiated_var->as<DeclarationNode>();
								inst_name = decl.identifier_token().value();
							} else {
								inst_name = idenfifier_token.value();  // Fallback
							}
							
							// Return identifier reference to the instantiated variable
							Token inst_token(Token::Type::Identifier, inst_name, 
							                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
							return ParseResult::success(*result);
						} else {
							// Variable template found but couldn't instantiate (likely dependent args).
							// Preserve template args in a FunctionCallNode for later substitution.
							FLASH_LOG_FORMAT(Parser, Debug, "Variable template '{}' found but not instantiated (dependent args, path 3)", template_name_to_use);
							TypeSpecifierNode& stub_type_vt3 = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(Type::Auto, TypeQualifier::None, 0, idenfifier_token);
							DeclarationNode& stub_decl_vt3 = gChunkedAnyStorage.emplace_back<DeclarationNode>(ASTNode(&stub_type_vt3), idenfifier_token);
							ChunkedVector<ASTNode> no_args_vt3;
							FunctionCallNode& var_call_vt3 = gChunkedAnyStorage.emplace_back<FunctionCallNode>(stub_decl_vt3, std::move(no_args_vt3), idenfifier_token);
							if (!explicit_template_arg_nodes.empty())
								var_call_vt3.set_template_arguments(std::move(explicit_template_arg_nodes));
							if (!template_name_to_use.empty() && template_name_to_use != idenfifier_token.value())
								var_call_vt3.set_qualified_name(template_name_to_use);
							result = emplace_node<ExpressionNode>(var_call_vt3);
							return ParseResult::success(*result);
						}
					}
				}
			}
			
			// Handle functional-style cast for class templates: ClassName<Args>()
			// This creates a temporary object of the instantiated class type
			// Pattern: hash<_Tp>() creates a temporary hash<_Tp> object
			if (explicit_template_args.has_value() && peek() == "("_tok) {
				// Check if this is a class template
				auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
				if (class_template_opt.has_value() && class_template_opt->is<TemplateClassDeclarationNode>()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Functional-style cast for class template '{}' with template args", idenfifier_token.value());
					
					// Build the instantiated type name using hash-based naming
					std::string_view instantiated_type_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
					
					// Try to instantiate the class template
					try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
					
					// Consume '(' and parse constructor arguments
					advance(); // consume '('
					
					// Parse constructor arguments
					ChunkedVector<ASTNode> args;
					if (current_token_.value() != ")") {
						while (true) {
							auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (arg_result.is_error()) {
								return arg_result;
							}
							if (auto arg = arg_result.node()) {
								args.push_back(*arg);
							}
							
							if (current_token_.kind().is_eof() || current_token_.value() != ",") {
								break;
							}
							advance(); // consume ','
						}
					}
					
					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after constructor arguments", current_token_);
					}
					
					// Create TypeSpecifierNode for the instantiated template type
					Token inst_type_token(Token::Type::Identifier, instantiated_type_name,
					                      idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, inst_type_token);
					
					// Create ConstructorCallNode for functional-style cast
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), inst_type_token));
					return ParseResult::success(*result);
				}
			}

			// Handle brace initialization for type names: TypeName{} or TypeName{args}
			// This handles expressions like "throw bad_any_cast{}" where bad_any_cast is a class
			if (found_as_type_alias && !identifierType && peek() == "{"_tok) {
				// Look up the actual type info to determine if this is an aggregate
				StringHandle identifier_handle = idenfifier_token.handle();
				auto type_it = gTypesByName.find(identifier_handle);
				if (type_it == gTypesByName.end()) {
					// Try namespace-qualified lookup
					NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
					if (!current_namespace.isGlobal()) {
						StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, identifier_handle);
						type_it = gTypesByName.find(qualified_handle);
					}
				}

				if (type_it != gTypesByName.end()) {
					const TypeInfo* type_info_ptr = type_it->second;
					const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
					TypeIndex type_index = type_info_ptr->type_index_;
					
					// Check if this is an aggregate type (no user-declared constructors, all public, no vtable)
					bool is_aggregate = false;
					if (struct_info) {
						bool has_user_ctors = false;
						for (const auto& func : struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								if (!func.function_decl.as<ConstructorDeclarationNode>().is_implicit()) {
									has_user_ctors = true;
									break;
								}
							}
						}
						bool all_public = true;
						for (const auto& member : struct_info->members) {
							if (member.access == AccessSpecifier::Private || member.access == AccessSpecifier::Protected) {
								all_public = false;
								break;
							}
						}
						is_aggregate = !has_user_ctors && !struct_info->has_vtable && all_public && !struct_info->members.empty();
					}
					
					if (is_aggregate) {
						// For aggregates, use parse_brace_initializer which creates proper InitializerListNode
						unsigned char type_size = struct_info ? static_cast<unsigned char>(struct_info->total_size * 8) : 0;
						auto type_spec = TypeSpecifierNode(Type::Struct, type_index, type_size, idenfifier_token);
						ParseResult init_result = parse_brace_initializer(type_spec);
						if (init_result.is_error()) {
							return init_result;
						}
						// Wrap the result in a ConstructorCallNode so codegen knows the target type
						if (init_result.node().has_value() && init_result.node()->is<InitializerListNode>()) {
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
							// Convert InitializerListNode initializers to ConstructorCallNode args
							const InitializerListNode& init_list = init_result.node()->as<InitializerListNode>();
							ChunkedVector<ASTNode> args;
							for (const auto& init : init_list.initializers()) {
								args.push_back(init);
							}
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
							return ParseResult::success(*result);
						}
						return init_result;
					} else {
						// Non-aggregate: use constructor call with proper type info
						advance(); // consume '{'
						
						ChunkedVector<ASTNode> args;
						while (current_token_.value() != "}") {
							auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (arg_result.is_error()) {
								return arg_result;
							}
							if (auto arg = arg_result.node()) {
								args.push_back(*arg);
							}
							
							if (current_token_.value() == ",") {
								advance(); // consume ','
							} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
								return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
							}
						}
						
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after brace initializer", current_token_);
						}
						
						unsigned char type_size = struct_info ? static_cast<unsigned char>(struct_info->total_size * 8) : 0;
						auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
						result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
						return ParseResult::success(*result);
					}
				} else {
					// Type not found - fall back to generic constructor call
					advance(); // consume '{'
					
					ChunkedVector<ASTNode> args;
					while (current_token_.value() != "}") {
						auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (arg_result.is_error()) {
							return arg_result;
						}
						if (auto arg = arg_result.node()) {
							args.push_back(*arg);
						}
						
						if (current_token_.value() == ",") {
							advance(); // consume ','
						} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
							return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
						}
					}
					
					if (!consume("}"_tok)) {
						return ParseResult::error("Expected '}' after brace initializer", current_token_);
					}
					
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, idenfifier_token);
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
					return ParseResult::success(*result);
				}
			}

			// Initially set result to a simple identifier - will be upgraded to FunctionCallNode if it's a function call
			if (!result.has_value()) {
				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}

			// Check if this looks like a function call
			// Only consume '(' if the identifier is actually a function OR a function pointer OR has operator()
			FLASH_LOG_FORMAT(Parser, Debug, "FUNCTION_CALL_CHECK for '{}', identifierType.has_value()={}", 
				idenfifier_token.value(), identifierType.has_value());
			bool is_function_decl = identifierType && (identifierType->is<FunctionDeclarationNode>() || identifierType->is<TemplateFunctionDeclarationNode>());
			bool is_function_pointer = false;
			bool has_operator_call = false;
			if (identifierType) {
				FLASH_LOG_FORMAT(Parser, Debug, "identifierType exists for '{}'", idenfifier_token.value());
				const DeclarationNode* decl = get_decl_from_symbol(*identifierType);
				if (decl) {
					FLASH_LOG_FORMAT(Parser, Debug, "decl exists for '{}'", idenfifier_token.value());
					const auto& type_node = decl->type_node().as<TypeSpecifierNode>();
					FLASH_LOG_FORMAT(Parser, Debug, "type_node.type()={} for '{}'", static_cast<int>(type_node.type()), idenfifier_token.value());
					// Check for function pointers or function references (both have function_signature)
					is_function_pointer = type_node.is_function_pointer() || type_node.has_function_signature();
					FLASH_LOG_FORMAT(Parser, Debug, "is_function_pointer={} (is_fp={}, has_sig={}) for '{}'", 
						is_function_pointer, type_node.is_function_pointer(), type_node.has_function_signature(), idenfifier_token.value());

					// Check if this is a struct with operator()
					// Note: Lambda variables have Type::Auto (from auto lambda = [...]), not Type::Struct
					if (type_node.type() == Type::Struct || type_node.type() == Type::UserDefined || type_node.type() == Type::Auto) {
						TypeIndex type_index = type_node.type_index();
						FLASH_LOG_FORMAT(Parser, Debug, "Checking identifier '{}' for operator(): type_index={}", idenfifier_token.value(), type_index);
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								FLASH_LOG_FORMAT(Parser, Debug, "Struct '{}' has {} member functions", 
									StringTable::getStringView(type_info.struct_info_->name), type_info.struct_info_->member_functions.size());
								// Check if struct has operator()
								for (const auto& member_func : type_info.struct_info_->member_functions) {
									FLASH_LOG_FORMAT(Parser, Debug, "Member function: is_operator={}, symbol='{}'", 
										member_func.is_operator_overload, member_func.operator_symbol);
									if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
										has_operator_call = true;
										break;
									}
								}
								FLASH_LOG_FORMAT(Parser, Debug, "has_operator_call for '{}': {}", idenfifier_token.value(), has_operator_call);
							}
						}
					}
					// Treat Type::Auto as a callable type (function pointer-like)
					// This handles generic lambda parameters: [](auto&& func) { func(); }
					else if (type_node.type() == Type::Auto) {
						is_function_pointer = true;
					}
				}
			}
			// Check if this is a template parameter (for constructor calls like T(...))
			bool is_template_parameter = identifierType && identifierType->is<TemplateParameterReferenceNode>();
			
			bool is_function_call = peek() == "("_tok &&
			                        (is_function_decl || is_function_pointer || has_operator_call || explicit_template_args.has_value() || is_template_parameter);

			if (is_function_call && consume("("_tok)) {
				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				// Use parse_function_arguments to handle all argument parsing including brace-init-list
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = false,
					.expand_simple_packs = true,
					.callee_name = idenfifier_token.value()
				});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}
				
				FLASH_LOG_FORMAT(Parser, Debug, "After parsing args: size={}, has_operator_call={}, is_template_parameter={}, is_function_pointer={}", 
					args.size(), has_operator_call, is_template_parameter, is_function_pointer);

				// For operator() calls, create a member function call
				if (has_operator_call) {
					// Create a member function call: object.operator()(args)
					auto object_expr = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));

					// Find the operator() function declaration in the struct
					const DeclarationNode* decl = get_decl_from_symbol(*identifierType);
					if (!decl) {
						return ParseResult::error("Invalid declaration for operator() call", idenfifier_token);
					}
					const auto& type_node = decl->type_node().as<TypeSpecifierNode>();
					TypeIndex type_index = type_node.type_index();
					const TypeInfo& type_info = gTypeInfo[type_index];

					// Find operator() in member functions
					const FunctionDeclarationNode* operator_call_func = nullptr;
					for (const auto& member_func : type_info.struct_info_->member_functions) {
						if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
							operator_call_func = &member_func.function_decl.as<FunctionDeclarationNode>();
							break;
						}
					}

					if (!operator_call_func) {
						return ParseResult::error("operator() not found in struct", idenfifier_token);
					}

					Token operator_token(Token::Type::Identifier, "operator()"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					result = emplace_node<ExpressionNode>(MemberFunctionCallNode(object_expr, *operator_call_func, std::move(args), operator_token));
				}
				// For template parameter constructor calls, create ConstructorCallNode
				else if (is_template_parameter) {
					// This is a constructor call: T(args)
					const auto& template_param = identifierType->as<TemplateParameterReferenceNode>();
					// Create a TypeSpecifierNode for the template parameter
					Token param_token(Token::Type::Identifier, template_param.param_name().view(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				}
				// For function pointers, skip overload resolution and create FunctionCallNode directly
				else if (is_function_pointer) {
					const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
					if (!decl_ptr) {
						return ParseResult::error("Invalid function pointer declaration", idenfifier_token);
					}
					result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
					
					// Mark this as an indirect call (function pointer/reference)
					std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_indirect_call(true);
					
					// Copy mangled name if available
					if (identifierType->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
						if (func_decl.has_mangled_name()) {
							std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
						}
					}
				}
				else {
					// Check if this is a constructor call on a template parameter
					if (result.has_value() && result->is<ExpressionNode>()) {
						const ExpressionNode& expr = result->as<ExpressionNode>();
						FLASH_LOG_FORMAT(Parser, Debug, "Checking if result is TemplateParameterReferenceNode, expr_index={}", expr.index());
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							// This is a constructor call: T(args)
							FLASH_LOG(Parser, Debug, "result IS TemplateParameterReferenceNode, moving args");
							const auto& template_param = std::get<TemplateParameterReferenceNode>(expr);
							// Create a TypeSpecifierNode for the template parameter
							Token param_token(Token::Type::Identifier, template_param.param_name().view(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
						} else {
							FLASH_LOG_FORMAT(Parser, Debug, "result is NOT TemplateParameterReferenceNode, proceeding to overload resolution, args.size()={}", args.size());
							// Perform overload resolution for regular functions
							// First, get all overloads of this function
							auto all_overloads = gSymbolTable.lookup_all(idenfifier_token.value());

							// Extract argument types
							std::vector<TypeSpecifierNode> arg_types;
							for (size_t i = 0; i < args.size(); ++i) {
								auto arg_type = get_expression_type(args[i]);
								if (!arg_type.has_value()) {
									// If we can't determine the type, fall back to old behavior
									const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
									if (!decl_ptr) {
										return ParseResult::error("Invalid function declaration", idenfifier_token);
									}
									result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
									
									// Copy mangled name if available
									if (identifierType->is<FunctionDeclarationNode>()) {
										const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
										if (func_decl.has_mangled_name()) {
											std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
										}
									}
									// Return early - we've created the FunctionCallNode with the args
									if (result.has_value())
										return ParseResult::success(*result);
									break;
								}
							
								TypeSpecifierNode arg_type_node = *arg_type;
							
								FLASH_LOG(Parser, Debug, "  get_expression_type returned: type=", (int)arg_type_node.type(), ", is_ref=", arg_type_node.is_reference(), ", is_rvalue_ref=", arg_type_node.is_rvalue_reference());
							
								// For perfect forwarding: check if argument is an lvalue
								// Lvalues: named variables, array subscripts, member access, dereferences, string literals
								// Rvalues: numeric/bool literals, temporaries, function calls returning non-reference
								if (args[i].is<ExpressionNode>()) {
									const ExpressionNode& arg_expr = args[i].as<ExpressionNode>();
									bool is_lvalue = std::visit([](const auto& inner) -> bool {
										using T = std::decay_t<decltype(inner)>;
										if constexpr (std::is_same_v<T, IdentifierNode>) {
											// Named variables are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
											// Array subscript expressions are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
											// Member access expressions are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
											// Dereference (*ptr) is an lvalue
											// Other unary operators like ++, --, etc. may also be lvalues
											return inner.op() == "*" || inner.op() == "++" || inner.op() == "--";
										} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
											// String literals are lvalues in C++
											return true;
										} else {
											// All other expressions (literals, temporaries, etc.) are rvalues
											return false;
										}
									}, arg_expr);
									
									if (is_lvalue) {
										// For forwarding reference deduction: Args&& deduces to T& for lvalues
										arg_type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);
									}
								}
							
								arg_types.push_back(arg_type_node);
							}
							
							// If we successfully extracted all argument types, perform overload resolution
							if (arg_types.size() == args.size()) {
								// Check for explicit template arguments: either from local variable or pending member variable
								std::optional<std::vector<TemplateTypeArg>> effective_template_args;
								if (explicit_template_args.has_value()) {
									effective_template_args = explicit_template_args;
								} else if (pending_explicit_template_args_.has_value()) {
									effective_template_args = pending_explicit_template_args_;
									// Clear the pending args after using them
									pending_explicit_template_args_.reset();
								}
								
								// If explicit template arguments were provided, use them directly
								if (effective_template_args.has_value()) {
									// Check if any template arguments are dependent (contain template parameters)
									// In that case, we cannot instantiate the template now - it will be done at instantiation time
									bool has_dependent_template_args = false;
									for (const auto& targ : *effective_template_args) {
										if (targ.is_dependent) {
											has_dependent_template_args = true;
											break;
										}
									}
									
									// Skip template instantiation in extern "C" contexts - C has no templates
									std::optional<ASTNode> instantiated_func;
									if (current_linkage_ != Linkage::C && !has_dependent_template_args) {
										instantiated_func = try_instantiate_template_explicit(idenfifier_token.value(), *effective_template_args, args.size());
									}
									if (instantiated_func.has_value()) {
										// Check if the function is deleted
										const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
										if (func_check && func_check->is_deleted()) {
											return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										}
										// Successfully instantiated template
										const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
										if (!decl_ptr) {
											return ParseResult::error("Invalid template instantiation", idenfifier_token);
										}
										result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
										
										// Copy mangled name if available
										if (instantiated_func->is<FunctionDeclarationNode>()) {
											const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
											if (func_decl.has_mangled_name()) {
												std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
											}
										}
									} else if (has_dependent_template_args) {
										// Template arguments are dependent - this is a template-dependent expression
										// Create a FunctionCallNode with a placeholder declaration that will be resolved during template instantiation
										// IMPORTANT: We must create a FunctionCallNode (not just IdentifierNode) to preserve the information
										// that this is a function call with template arguments. This is needed for non-type template arguments
										// like: bool_constant<test_func<T>()> where the function call result is used as a constant expression.
										FLASH_LOG(Templates, Debug, "Creating dependent FunctionCallNode for call to '", idenfifier_token.value(), "'");
										
										// Create a placeholder declaration for the dependent function call
										auto type_node = emplace_node<TypeSpecifierNode>(Type::Bool, TypeQualifier::None, 1, idenfifier_token);
										auto placeholder_decl = emplace_node<DeclarationNode>(type_node, idenfifier_token);
										const DeclarationNode& decl_ref = placeholder_decl.as<DeclarationNode>();
										
										// Create FunctionCallNode with the placeholder
										result = emplace_node<ExpressionNode>(FunctionCallNode(decl_ref, std::move(args), idenfifier_token));
										
										// Store the template arguments in the FunctionCallNode for later resolution
										FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
										if (!explicit_template_arg_nodes.empty()) {
											func_call.set_template_arguments(std::move(explicit_template_arg_nodes));
										}
									} else {
										return ParseResult::error("No matching template for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
									}
								} else {
									// No explicit template arguments - try overload resolution first
									FLASH_LOG(Parser, Debug, "Function call to '", idenfifier_token.value(), "': found ", all_overloads.size(), " overload(s), ", arg_types.size(), " argument(s)");
									for (size_t i = 0; i < arg_types.size(); ++i) {
										const auto& arg = arg_types[i];
										FLASH_LOG(Parser, Debug, "  Arg[", i, "]: type=", (int)arg.type(), ", is_ref=", arg.is_reference(), ", is_rvalue_ref=", arg.is_rvalue_reference(), ", is_lvalue_ref=", arg.is_lvalue_reference(), ", is_ptr=", arg.is_pointer(), ", ptr_depth=", arg.pointer_depth());
									}
									if (all_overloads.empty()) {
										// No overloads found - try template instantiation (skip in extern "C" - C has no templates)
										std::optional<ASTNode> instantiated_func;
										if (current_linkage_ != Linkage::C) {
											instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
										}
										if (instantiated_func.has_value()) {
											// Check if the function is deleted
											const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
											if (func_check && func_check->is_deleted()) {
												return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
											}
											// Successfully instantiated template
											const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
											if (!decl_ptr) {
												return ParseResult::error("Invalid template instantiation", idenfifier_token);
											}
											result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
											
											// Copy mangled name if available
											if (instantiated_func->is<FunctionDeclarationNode>()) {
												const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
												if (func_decl.has_mangled_name()) {
													std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
												}
											}
										} else {
											// In SFINAE context (e.g., requires expression), function lookup failure
											// means the constraint is not satisfied - not an error
											if (in_sfinae_context_) {
												// Create a placeholder node to indicate failed lookup
												// The requires expression will treat this as "constraint not satisfied"
												result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
											} else {
												return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
											}
										}
									} else {
										// Have overloads - do overload resolution
										auto resolution_result = resolve_overload(all_overloads, arg_types);

										FLASH_LOG(Parser, Debug, "Overload resolution result: has_match=", resolution_result.has_match, ", is_ambiguous=", resolution_result.is_ambiguous);
										
										if (resolution_result.is_ambiguous) {
											return ParseResult::error("Ambiguous call to overloaded function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										} else if (!resolution_result.has_match) {
											// No matching regular function found - try template instantiation with deduction (skip in extern "C" - C has no templates)
											std::optional<ASTNode> instantiated_func;
											if (current_linkage_ != Linkage::C) {
												instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
											}
											if (instantiated_func.has_value()) {
												// Check if the function is deleted
												const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
												if (func_check && func_check->is_deleted()) {
													return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
												}
												// Successfully instantiated template
												const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
												if (!decl_ptr) {
													return ParseResult::error("Invalid template instantiation", idenfifier_token);
												}
												result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
												
												// Copy mangled name if available
												if (instantiated_func->is<FunctionDeclarationNode>()) {
													const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
													if (func_decl.has_mangled_name()) {
														std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
													}
												}
											} else {
												// In SFINAE context (e.g., requires expression), function lookup failure
												// means the constraint is not satisfied - not an error
												if (in_sfinae_context_) {
													// Create a placeholder node to indicate failed lookup
													result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
												} else {
													return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
												}
											}
										} else {
											// Get the selected overload
											const DeclarationNode* decl_ptr = getDeclarationNode(*resolution_result.selected_overload);
											if (!decl_ptr) {
												return ParseResult::error("Invalid function declaration", idenfifier_token);
											}

											result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), idenfifier_token));
											
											// If the function has a pre-computed mangled name, set it on the FunctionCallNode
											// This is important for functions in namespaces accessed via using directives
											if (resolution_result.selected_overload->is<FunctionDeclarationNode>()) {
												const FunctionDeclarationNode& func_decl = resolution_result.selected_overload->as<FunctionDeclarationNode>();
												if (func_decl.has_mangled_name()) {
													std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
												}
											}
										}
									}
								}
							}
						}
					}
				}

			}
			else {
				// Regular identifier
				// Additional type checking and verification logic can be performed here using identifierType

				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}
		}
	}
	else if (current_token_.type() == Token::Type::Literal) {
		auto literal_type = get_numeric_literal_type(current_token_.value());
		if (!literal_type) {
			return ParseResult::error("Expected numeric literal", current_token_);
		}
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_, literal_type->value, literal_type->type, literal_type->typeQualifier, literal_type->sizeInBits));
		advance();
	}
	else if (current_token_.type() == Token::Type::StringLiteral) {
		// Handle adjacent string literal concatenation
		// C++ allows "Hello " "World" to be concatenated into "Hello World"
		Token first_string = current_token_;
		std::string concatenated_value(first_string.value());
		advance();

		// Check for adjacent string literals
		while (peek().is_string_literal()) {
			Token next_string = peek_info();
			// Remove quotes from both strings and concatenate
			// First string: remove trailing quote
			// Next string: remove leading quote
			std::string_view first_content = concatenated_value;
			if (first_content.size() >= 2 && first_content.back() == '"') {
				first_content.remove_suffix(1);
			}
			std::string_view next_content = next_string.value();
			if (next_content.size() >= 2 && next_content.front() == '"') {
				next_content.remove_prefix(1);
			}

			// Concatenate: first_content (without trailing ") + next_content (without leading ")
			concatenated_value = std::string(first_content) + std::string(next_content);
			advance();
		}

		// Store the concatenated string in CompileContext so it persists
		std::string_view persistent_string = context_.storeFunctionNameLiteral(concatenated_value);
		Token concatenated_token(Token::Type::StringLiteral,
		                         persistent_string,
		                         first_string.line(),
		                         first_string.column(),
		                         first_string.file_index());

		result = emplace_node<ExpressionNode>(StringLiteralNode(concatenated_token));
		
		// Check for user-defined literal suffix: "hello"_suffix or "hello"sv
		if (peek_info().type() == Token::Type::Identifier) {
			std::string_view suffix = peek_info().value();
			// UDL suffixes start with _ (user-defined) or are standard (sv, s, etc.)
			if (suffix.size() > 0 && (suffix[0] == '_' || suffix == "sv" || suffix == "s")) {
				// Save position before consuming suffix in case the operator is not found
				SaveHandle pre_suffix_pos = save_token_position();
				Token suffix_token = peek_info();
				advance(); // consume suffix
				
				// Build the operator name: operator""_suffix
				std::string_view operator_name = StringBuilder().append("operator\"\""sv).append(suffix).commit();
				
				// Look up the UDL operator in the symbol table
				auto udl_lookup = gSymbolTable.lookup(operator_name);
				if (udl_lookup.has_value() && udl_lookup->is<FunctionDeclarationNode>()) {
					FunctionDeclarationNode& func_decl = udl_lookup->as<FunctionDeclarationNode>();
					
					// Build arguments: the string literal and its length
					ChunkedVector<ASTNode> args;
					args.push_back(*result);  // string literal
					
					// Calculate string length (excluding quotes)
					std::string_view str_val = persistent_string;
					size_t str_len = 0;
					if (str_val.size() >= 2) {
						str_len = str_val.size() - 2;  // Remove opening and closing quotes
					}
					
					// Create a NumericLiteralNode for the length
					std::string_view len_placeholder_sv = "0";
					Token len_token(Token::Type::Literal, len_placeholder_sv, suffix_token.line(), suffix_token.column(), suffix_token.file_index());
					auto len_node = emplace_node<ExpressionNode>(
						NumericLiteralNode(len_token, static_cast<unsigned long long>(str_len), Type::UnsignedLong, TypeQualifier::None, 64));
					args.push_back(len_node);
					
					result = emplace_node<ExpressionNode>(
						FunctionCallNode(func_decl.decl_node(), std::move(args), suffix_token));
					
					// Set mangled name if available
					if (func_decl.has_mangled_name()) {
						std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
					}
				} else {
					// Operator not found - restore position so suffix token is not lost
					restore_token_position(pre_suffix_pos);
				}
			}
		}
	}
	else if (current_token_.type() == Token::Type::CharacterLiteral) {
		// Parse character literal and convert to numeric value
		std::string_view value = current_token_.value();

		// Character literal format:
		// - Regular: 'x' or '\x' (char_offset = 1)
		// - Wide: L'x' or L'\x' (char_offset = 2)
		// - char8_t: u8'x' (char_offset = 3)
		// - char16_t: u'x' (char_offset = 2)
		// - char32_t: U'x' (char_offset = 2)
		size_t char_offset = 1;  // Default: regular char literal 'x'
		Type char_type = Type::Char;
		int char_size_bits = 8;

		// Check for prefix (wide character literals)
		if (value.size() > 0 && value[0] == 'L') {
			char_offset = 2;  // L'x'
			char_type = Type::WChar;
			char_size_bits = get_wchar_size_bits();
		} else if (value.size() > 1 && value[0] == 'u' && value[1] == '8') {
			char_offset = 3;  // u8'x'
			char_type = Type::Char8;
			char_size_bits = 8;
		} else if (value.size() > 0 && value[0] == 'u') {
			char_offset = 2;  // u'x'
			char_type = Type::Char16;
			char_size_bits = 16;
		} else if (value.size() > 0 && value[0] == 'U') {
			char_offset = 2;  // U'x'
			char_type = Type::Char32;
			char_size_bits = 32;
		}

		// Minimum size check: prefix + quote + char + quote
		if (value.size() < char_offset + 2) {
			return ParseResult::error("Invalid character literal", current_token_);
		}

		uint32_t char_value = 0;  // Use uint32_t for wide chars
		if (value[char_offset] == '\\') {
			// Escape sequence
			if (value.size() < char_offset + 3) {
				return ParseResult::error("Invalid escape sequence in character literal", current_token_);
			}
			char escape_char = value[char_offset + 1];
			switch (escape_char) {
				case 'n': char_value = '\n'; break;
				case 't': char_value = '\t'; break;
				case 'r': char_value = '\r'; break;
				case '0': char_value = '\0'; break;
				case '\\': char_value = '\\'; break;
				case '\'': char_value = '\''; break;
				case '"': char_value = '"'; break;
				default:
					return ParseResult::error("Unknown escape sequence in character literal", current_token_);
			}
		}
		else {
			// Single character
			char_value = static_cast<unsigned char>(value[char_offset]);
		}

		// Create a numeric literal node with the character's value
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_,
			static_cast<unsigned long long>(char_value),
			char_type, TypeQualifier::None, char_size_bits));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword &&
			 (current_token_.value() == "true"sv || current_token_.value() == "false"sv)) {
		// Handle bool literals
		bool value = (current_token_.value() == "true");
		result = emplace_node<ExpressionNode>(BoolLiteralNode(current_token_, value));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "nullptr"sv) {
		// Handle nullptr literal - represented as null pointer constant (0)
		// The actual type will be determined by context (can convert to any pointer type)
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_,
			0ULL, Type::Int, TypeQualifier::None, 64));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "this"sv) {
		// Handle 'this' keyword - represents a pointer to the current object
		// Only valid inside member functions
		if (member_function_context_stack_.empty()) {
			return ParseResult::error("'this' can only be used inside a member function", current_token_);
		}

		Token this_token = current_token_;
		advance();

		// Create an identifier node for 'this'
		result = emplace_node<ExpressionNode>(IdentifierNode(this_token));
	}
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "{") {
		// Handle braced initializer in expression context
		// Examples:
		//   return { .a = 5 };  // Aggregate initialization with return type
		//   func({})            // Braced initializer as function argument (type inferred from parameter)
		
		// Check if we're parsing a function argument by looking at the expression context
		// In a function call argument, braced initializers are valid and their type is inferred
		// from the function parameter type. Since we're doing a single-pass parser and we might
		// not have resolved the function yet, we just accept it as a placeholder.
		
		// For now, if we don't have a current_function_ context (which means we're not in a
		// return statement), just parse it as an empty braced initializer placeholder.
		// This handles cases like: decltype(func({})) in template default parameters
		if (!current_function_) {
			Token brace_token = current_token_;
			advance(); // consume '{'
			
			// Skip the contents of the braced initializer
			// We need to match braces to find the closing '}'
			int brace_depth = 1;
			while (brace_depth > 0 && !current_token_.kind().is_eof()) {
				if (current_token_.value() == "{") {
					brace_depth++;
				} else if (current_token_.value() == "}") {
					brace_depth--;
				}
				if (brace_depth > 0) {
					advance();
				}
			}
			
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' to close braced initializer", current_token_);
			}
			
			// Create a placeholder literal node - the type will be inferred from context
			// (e.g., function parameter type, variable declaration type, etc.)
			// The actual value doesn't matter, only that it represents a braced initializer
			NumericLiteralValue val = static_cast<unsigned long long>(0);
			result = emplace_node<ExpressionNode>(NumericLiteralNode(brace_token, val, Type::Int, TypeQualifier::None, 32));
			return ParseResult::success(*result);
		}
		
		// We're in a function body (current_function_ is set)
		// Get the return type from the current function
		const DeclarationNode& func_decl = current_function_->decl_node();
		const ASTNode& return_type_node = func_decl.type_node();
		
		if (!return_type_node.is<TypeSpecifierNode>()) {
			return ParseResult::error("Cannot determine return type for braced initializer", current_token_);
		}
		
		const TypeSpecifierNode& return_type = return_type_node.as<TypeSpecifierNode>();
		
		// Parse the braced initializer with the return type
		ParseResult init_result = parse_brace_initializer(return_type);
		if (init_result.is_error()) {
			return init_result;
		}
		
		if (!init_result.node().has_value()) {
			return ParseResult::error("Expected initializer expression", current_token_);
		}
		
		// For scalar types, parse_brace_initializer already returns an expression
		// Just return it directly
		return init_result;
	}
	else if (consume("("_tok)) {
		// Could be either:
		// 1. C-style cast: (Type)expression
		// 2. Parenthesized expression: (expression)
		// 3. C++17 Fold expression: (...op pack), (pack op...), (init op...op pack), (pack op...op init)
		
		// Check for fold expression patterns
		SaveHandle fold_check_pos = save_token_position();
		bool is_fold = false;
		
		// Pattern 1: Unary left fold: (... op pack)
		if (peek() == "..."_tok) {
			advance(); // consume ...
			
			// Next should be an operator
			if (peek().is_operator()) {
				std::string_view fold_op = peek_info().value();
				Token op_token = peek_info();
				advance(); // consume operator
				
				// Next should be the pack identifier
				if (peek().is_identifier()) {
					std::string_view pack_name = peek_info().value();
					advance(); // consume pack name
					
					if (consume(")"_tok)) {
						// Valid unary left fold: (... op pack)
						discard_saved_token(fold_check_pos);
						result = emplace_node<ExpressionNode>(
							FoldExpressionNode(pack_name, fold_op, 
								FoldExpressionNode::Direction::Left, op_token));
						is_fold = true;
					}
				}
			}
		}
		
		if (!is_fold) {
			restore_token_position(fold_check_pos);
			
			// Pattern 2 & 4: Check if starts with identifier (could be pack or init)
			if (peek().is_identifier()) {
				std::string_view first_id = peek_info().value();
				advance(); // consume identifier
				
				// Check what follows
				if (peek().is_operator()) {
					std::string_view fold_op = peek_info().value();
					Token op_token = peek_info();
					advance(); // consume operator
					
					// Check for ... (fold expression)
					if (peek() == "..."_tok) {
						advance(); // consume ...
						
						// Check if binary fold or unary right fold
						if (peek().is_operator() &&
							peek_info().value() == fold_op) {
							// Binary fold: (X op ... op Y)
							// Need to determine direction: if first_id is a pack parameter, it's
							// a binary right fold (pack op ... op init). If first_id is NOT a pack
							// but Y is, it's a binary left fold (init op ... op pack).
							SaveHandle binary_pos = save_token_position();
							advance(); // consume second operator
							
							// Check if second operand is a simple identifier (potential pack name)
							if (peek().is_identifier()) {
								std::string_view second_id = peek_info().value();
								SaveHandle after_second = save_token_position();
								advance(); // consume second identifier
								
								if (peek() == ")"_tok) {
									advance(); // consume )
									
									// Determine direction by checking which identifier is a pack
									bool first_is_pack = get_pack_size(first_id).has_value();
									bool second_is_pack = get_pack_size(second_id).has_value();
									
									if (second_is_pack && !first_is_pack) {
										// Binary left fold: (init op ... op pack)
										Token init_token(Token::Type::Identifier, first_id, 0, 0, 0);
										ASTNode init_expr = emplace_node<ExpressionNode>(IdentifierNode(init_token));
										discard_saved_token(fold_check_pos);
										discard_saved_token(binary_pos);
										discard_saved_token(after_second);
										result = emplace_node<ExpressionNode>(
											FoldExpressionNode(second_id, fold_op,
												FoldExpressionNode::Direction::Left, init_expr, op_token));
										is_fold = true;
									} else if (first_is_pack && !second_is_pack) {
										// Binary right fold: (pack op ... op init)
										Token init_token(Token::Type::Identifier, second_id, 0, 0, 0);
										ASTNode init_expr = emplace_node<ExpressionNode>(IdentifierNode(init_token));
										discard_saved_token(fold_check_pos);
										discard_saved_token(binary_pos);
										discard_saved_token(after_second);
										result = emplace_node<ExpressionNode>(
											FoldExpressionNode(first_id, fold_op,
												FoldExpressionNode::Direction::Right, init_expr, op_token));
										is_fold = true;
									} else {
										// Neither or both identifiers recognized as packs in pack_param_info_.
										// This can happen for template parameter packs (e.g., Ns...) which are
										// tracked differently. Don't guess direction here — restore position and
										// let Pattern 3 (complex-expression binary left fold) or the complex-
										// expression fallback below handle it correctly.
										restore_token_position(after_second);
									}
								} else {
									restore_token_position(after_second);
								}
							}
							
							if (!is_fold) {
								// Second operand is a complex expression - parse it
								restore_token_position(binary_pos);
								advance(); // consume second operator again
								
								ParseResult init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
								if (!init_result.is_error() && init_result.node().has_value() &&
									consume(")"_tok)) {
								discard_saved_token(fold_check_pos);
									result = emplace_node<ExpressionNode>(
										FoldExpressionNode(first_id, fold_op,
											FoldExpressionNode::Direction::Right, *init_result.node(), op_token));
									is_fold = true;
								}
							}
						} else if (consume(")"_tok)) {
							// Unary right fold: (pack op ...)
							discard_saved_token(fold_check_pos);
							result = emplace_node<ExpressionNode>(
								FoldExpressionNode(first_id, fold_op,
									FoldExpressionNode::Direction::Right, op_token));
							is_fold = true;
						}
					}
				}
			}
		}
		
		// Pattern 3: Binary left fold: (init op ... op pack)
		// This is tricky because init can be a complex expression
		// For now, we'll handle simple cases where init is a literal or identifier
		if (!is_fold) {
			restore_token_position(fold_check_pos);
			
			// Try to parse as a simple expression
			SaveHandle init_pos = save_token_position();
			ParseResult init_result = parse_primary_expression(ExpressionContext::Normal);
			
			if (!init_result.is_error() && init_result.node().has_value()) {
				if (peek().is_operator()) {
					std::string_view fold_op = peek_info().value();
					Token op_token = peek_info();
					advance(); // consume operator
					
					if (peek() == "..."_tok) {
						advance(); // consume ...
						
						if (peek().is_operator() &&
							peek_info().value() == fold_op) {
							advance(); // consume second operator
							
							if (peek().is_identifier()) {
								std::string_view pack_name = peek_info().value();
								advance(); // consume pack name
								
								if (consume(")"_tok)) {
									// Valid binary left fold: (init op ... op pack)
									discard_saved_token(fold_check_pos);
									discard_saved_token(init_pos);
									result = emplace_node<ExpressionNode>(
										FoldExpressionNode(pack_name, fold_op,
											FoldExpressionNode::Direction::Left, *init_result.node(), op_token));
									is_fold = true;
								}
							}
						} else if (consume(")"_tok)) {
							// Unary right fold with complex expression: (expr op ...)
							// The expression is a pack expansion that will be folded
							discard_saved_token(fold_check_pos);
							discard_saved_token(init_pos);
							// For unary right fold, we need to extract the pack name from the expression
							// If the expression contains a pack expansion, use that
							// For now, we'll create a fold expression with the expression as the pack
							result = emplace_node<ExpressionNode>(
								FoldExpressionNode(*init_result.node(), fold_op,
									FoldExpressionNode::Direction::Right, op_token));
							is_fold = true;
						}
					}
				}
			}
			
			if (!is_fold) {
				restore_token_position(init_pos);
			}
		}
		
		// If not a fold expression, parse as parenthesized expression
		if (!is_fold) {
			restore_token_position(fold_check_pos);
		
			// Parse as parenthesized expression
			// Note: C-style casts are now handled in parse_unary_expression()
			// Allow comma operator in parenthesized expressions
			// Pass the context so the expression parser knows how to handle special tokens
			ParseResult paren_result = parse_expression(MIN_PRECEDENCE, context);
			if (paren_result.is_error()) {
				return paren_result;
			}
			
			// In TemplateArgument or Decltype context, allow pack expansion (...) before closing paren
			// Pattern: (expr...) where ... is pack expansion operator
			// This is needed for patterns like decltype((expr...)) in template contexts
			if ((context == ExpressionContext::TemplateArgument || context == ExpressionContext::Decltype) &&
			    peek() == "..."_tok) {
				// Consume the ... and create a PackExpansionExprNode
				Token ellipsis_token = peek_info();
				advance(); // consume '...'
				
				// Wrap the expression in a PackExpansionExprNode
				if (paren_result.node().has_value()) {
					result = emplace_node<ExpressionNode>(
						PackExpansionExprNode(*paren_result.node(), ellipsis_token));
				} else {
					return ParseResult::error("Expected expression before '...'", current_token_);
				}
				
				FLASH_LOG(Parser, Debug, "Created PackExpansionExprNode for parenthesized pack expansion");
			} else {
				result = paren_result.node();
			}
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after parenthesized expression",
					current_token_);
			}
		}  // End of fold expression check
	}
	else {
		return ParseResult::error("Expected primary expression", current_token_);
	}

found_member_variable:  // Label for member variable detection - jump here to skip error checking
	// Phase 3: Postfix operators are now handled in parse_postfix_expression()
	// Return the primary expression result
	if (result.has_value())
		return ParseResult::success(*result);

	// No result was produced - this should not happen in a well-formed expression
	return ParseResult();  // Return monostate instead of empty success
}

