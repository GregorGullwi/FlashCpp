// Phase 1: Unified parameter list parsing
// This method handles all the common parameter parsing logic:
// - Basic parameters: (int x, float y)
// - Variadic parameters: (int x, ...)
// - Default values: (int x = 0, float y = 1.0)
// - Empty parameter lists: ()
ParseResult Parser::parse_parameter_list(FlashCpp::ParsedParameterList& out_params, CallingConvention calling_convention)
{
	out_params.parameters.clear();
	out_params.is_variadic = false;

	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' for parameter list", current_token_);
	}

	while (!consume(")"_tok)) {
		// Handle C-style (void) parameter list meaning "no parameters"
		// In C/C++, f(void) is equivalent to f()
		if (out_params.parameters.empty() && peek() == "void"_tok) {
			// Check if this is exactly "(void)" - void followed by ')'
			SaveHandle void_check = save_token_position();
			advance(); // consume 'void'
			if (peek() == ")"_tok) {
				// This is (void) - empty parameter list
				discard_saved_token(void_check);
				advance(); // consume ')'
				break;
			}
			// Not (void), restore and continue with normal parameter parsing
			restore_token_position(void_check);
		}

		// Check for variadic parameter (...)
		if (peek() == "..."_tok) {
			advance(); // consume '...'
			out_params.is_variadic = true;

			// Validate calling convention for variadic functions
			// Only __cdecl and __vectorcall support variadic parameters (caller cleanup)
			if (calling_convention != CallingConvention::Default &&
			    calling_convention != CallingConvention::Cdecl &&
			    calling_convention != CallingConvention::Vectorcall) {
				return ParseResult::error(
					"Variadic functions must use __cdecl or __vectorcall calling convention "
					"(other conventions use callee cleanup which is incompatible with variadic arguments)",
					current_token_);
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after variadic '...'", current_token_);
			}
			break;
		}

		// Parse parameter type and name
		ParseResult type_and_name_result = parse_type_and_name();
		if (type_and_name_result.is_error()) {
			return type_and_name_result;
		}

		if (auto node = type_and_name_result.node()) {
			// Apply array-to-pointer decay for function parameters
			// In C++, function parameters declared as T arr[N] are treated as T* arr
			if (node->is<DeclarationNode>()) {
				auto& decl = node->as<DeclarationNode>();
				if (decl.array_size().has_value()) {
					// This is an array parameter - convert to pointer
					// Get the underlying type and add a pointer level
					const TypeSpecifierNode& orig_type = decl.type_node().as<TypeSpecifierNode>();
					TypeSpecifierNode param_type = orig_type;  // Copy needed since we modify
					param_type.add_pointer_level();
					
					// Create new declaration without array size (now a pointer)
					ASTNode new_decl = emplace_node<DeclarationNode>(
						emplace_node<TypeSpecifierNode>(param_type),
						decl.identifier_token()
					);
					
					// Copy over any other attributes
					if (decl.has_default_value()) {
						new_decl.as<DeclarationNode>().set_default_value(decl.default_value());
					}
					if (decl.is_parameter_pack()) {
						new_decl.as<DeclarationNode>().set_parameter_pack(true);
					}
					
					out_params.parameters.push_back(new_decl);
				} else {
					out_params.parameters.push_back(*node);
				}
			} else {
				out_params.parameters.push_back(*node);
			}
		}

		// Parse default parameter value (if present)
		// Note: '=' is an Operator token, not a Punctuator token
		if (peek() == "="_tok) {
			advance(); // consume '='
			// Parse the default value expression
			auto default_value = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (default_value.is_error()) {
				return default_value;
			}
			// Store default value in parameter node
			if (default_value.node().has_value() && !out_params.parameters.empty()) {
				auto& last_param = out_params.parameters.back();
				if (last_param.is<DeclarationNode>()) {
					last_param.as<DeclarationNode>().set_default_value(*default_value.node());
				}
			}
		}

		// Skip GCC attributes on parameters (e.g., __attribute__((__unused__)))
		skip_gcc_attributes();

		if (consume(","_tok)) {
			// After a comma, check if the next token is '...' for variadic parameters
			if (peek() == "..."_tok) {
				advance(); // consume '...'
				out_params.is_variadic = true;

				// Validate calling convention for variadic functions
				if (calling_convention != CallingConvention::Default &&
				    calling_convention != CallingConvention::Cdecl &&
				    calling_convention != CallingConvention::Vectorcall) {
					return ParseResult::error(
						"Variadic functions must use __cdecl or __vectorcall calling convention "
						"(other conventions use callee cleanup which is incompatible with variadic arguments)",
						current_token_);
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after variadic '...'", current_token_);
				}
				break;
			}
			continue;
		}
		else if (consume(")"_tok)) {
			break;
		}
		else {
			return ParseResult::error("Expected ',' or ')' in parameter list", current_token_);
		}
	}

	return ParseResult::success();
}

// Unified function call argument parsing
// This method consolidates the 6+ places where function call arguments are parsed in the codebase.
// It handles:
// - Comma-separated argument list parsing
// - Pack expansion (...) after arguments
// - Optional argument type collection for template deduction
// - Simple pack identifier expansion (for already-expanded packs in symbol table)
FlashCpp::ParsedFunctionArguments Parser::parse_function_arguments(const FlashCpp::FunctionArgumentContext& ctx)
{
	using namespace FlashCpp;
	
	// Check if function call has arguments (not empty parentheses)
	if (peek().is_eof() || peek() == ")"_tok) {
		// Empty argument list - return empty result without allocating
		ParsedFunctionArguments result;
		result.success = true;
		return result;
	}
	
	// We have arguments, so allocate storage
	ChunkedVector<ASTNode> args;
	std::vector<TypeSpecifierNode> arg_types;
	
	while (true) {
		// Handle brace-init-list argument: func({.x=1}) -> func(ParamType{.x=1})
		// When a '{' is encountered as an argument, infer the parameter type from the function signature
		if (peek() == "{"_tok && !ctx.callee_name.empty()) {
			// Look up the function to get the parameter type at the current argument index
			auto func_lookup = gSymbolTable.lookup(ctx.callee_name);
			if (func_lookup.has_value() && func_lookup->is<FunctionDeclarationNode>()) {
				const auto& func_decl = func_lookup->as<FunctionDeclarationNode>();
				size_t arg_index = args.size();
				const auto& params = func_decl.parameter_nodes();
				if (arg_index < params.size() && params[arg_index].is<DeclarationNode>()) {
					const auto& param_decl = params[arg_index].as<DeclarationNode>();
					if (param_decl.type_node().is<TypeSpecifierNode>()) {
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						// Only handle struct/user-defined types
						if (param_type.type() == Type::Struct || param_type.type() == Type::UserDefined) {
							// Save position before parse_brace_initializer since it consumes '{'
							SaveHandle brace_pos = save_token_position();
							auto init_result = parse_brace_initializer(param_type);
							if (!init_result.is_error() && init_result.node()) {
								discard_saved_token(brace_pos);
								if (init_result.node()->is<InitializerListNode>()) {
									// Convert InitializerListNode to ConstructorCallNode
									auto type_node = emplace_node<TypeSpecifierNode>(param_type);
									const InitializerListNode& init_list = init_result.node()->as<InitializerListNode>();
									ChunkedVector<ASTNode> ctor_args;
									for (const auto& init : init_list.initializers()) {
										ctor_args.push_back(init);
									}
									args.push_back(emplace_node<ExpressionNode>(
										ConstructorCallNode(type_node, std::move(ctor_args), peek_info())));
								} else {
									args.push_back(*init_result.node());
								}
								if (ctx.collect_types) {
									arg_types.push_back(param_type);
								}
								// Check for comma or end
								if (peek() == ","_tok) {
									advance(); // consume ','
									continue;
								}
								break;
							} else {
								// parse_brace_initializer failed - restore token stream
								restore_token_position(brace_pos);
							}
						}
					}
				}
			}
		}
		
		auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (arg_result.is_error()) {
			return ParsedFunctionArguments::make_error(arg_result.error_message(), 
				arg_result.error_token());
		}
		
		if (auto arg = arg_result.node()) {
			// Check for pack expansion (...) after the argument
			if (ctx.handle_pack_expansion && peek() == "..."_tok) {
				Token ellipsis_token = peek_info();
				advance(); // consume '...'
				
				// Handle simple pack expansion if enabled
				bool expanded = false;
				if (ctx.expand_simple_packs) {
					std::string_view pack_name;
					if (arg->is<IdentifierNode>()) {
						pack_name = arg->as<IdentifierNode>().name();
					} else if (arg->is<ExpressionNode>() && std::holds_alternative<IdentifierNode>(arg->as<ExpressionNode>())) {
						pack_name = std::get<IdentifierNode>(arg->as<ExpressionNode>()).name();
					}
					if (!pack_name.empty()) {
					
					// Try to find expanded pack elements in the symbol table
					// Pattern: pack_name_0, pack_name_1, etc.
					size_t pack_size = 0;
					StringBuilder sb;
					for (size_t i = 0; i < MAX_PACK_ELEMENTS; ++i) {
						std::string_view element_name = sb
							.append(pack_name)
							.append("_")
							.append(i)
							.preview();
						
						if (gSymbolTable.lookup(element_name).has_value()) {
							++pack_size;
							sb.reset();
						} else {
							break;
						}
					}
					sb.reset();
					
					if (pack_size > 0) {
						// Add each pack element as a separate argument
						for (size_t i = 0; i < pack_size; ++i) {
							std::string_view element_name = StringBuilder()
								.append(pack_name)
								.append("_")
								.append(i)
								.commit();
							
							// Use ellipsis token position for proper error reporting
							Token elem_token(Token::Type::Identifier, element_name, 
								ellipsis_token.line(), ellipsis_token.column(), ellipsis_token.file_index());
							auto elem_node = emplace_node<ExpressionNode>(IdentifierNode(elem_token));
							args.push_back(elem_node);
							
							// Collect type if needed
							if (ctx.collect_types) {
								std::optional<TypeSpecifierNode> elem_type = get_expression_type(elem_node);
								if (elem_type.has_value()) {
									arg_types.push_back(*elem_type);
								} else {
									arg_types.emplace_back(Type::Int, TypeQualifier::None, 32, ellipsis_token);
								}
							}
						}
						expanded = true;
					}
					} // !pack_name.empty()
				}
				
				if (!expanded) {
					// Wrap the argument in a PackExpansionExprNode
					auto pack_expr = emplace_node<ExpressionNode>(
						PackExpansionExprNode(*arg, ellipsis_token));
					args.push_back(pack_expr);
					
					// For pack expansions, we can't reliably determine the type
					if (ctx.collect_types) {
						std::optional<TypeSpecifierNode> arg_type = get_expression_type(*arg);
						if (arg_type.has_value()) {
							arg_types.push_back(*arg_type);
						} else {
							arg_types.emplace_back(Type::Int, TypeQualifier::None, 32, ellipsis_token);
						}
					}
				}
				
				FLASH_LOG(Parser, Debug, "Handled pack expansion for function argument");
			} else {
				args.push_back(*arg);
				
				// Collect argument type if requested
				if (ctx.collect_types) {
					std::optional<TypeSpecifierNode> arg_type = get_expression_type(*arg);
					if (arg_type.has_value()) {
						arg_types.push_back(*arg_type);
					} else {
						// Fallback: try to deduce from the expression
						// Use current_token_ for error location since we've just parsed the expression
						Type deduced_type = Type::Int;
						if (arg->is<ExpressionNode>()) {
							const ExpressionNode& expr = arg->as<ExpressionNode>();
							if (std::holds_alternative<NumericLiteralNode>(expr)) {
								deduced_type = std::get<NumericLiteralNode>(expr).type();
							} else if (std::holds_alternative<IdentifierNode>(expr)) {
								const auto& ident = std::get<IdentifierNode>(expr);
								auto symbol = lookup_symbol(StringTable::getOrInternStringHandle(ident.name()));
								if (symbol.has_value()) {
									if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
										deduced_type = decl->type_node().as<TypeSpecifierNode>().type();
									}
								}
							}
						}
						arg_types.emplace_back(deduced_type, TypeQualifier::None, get_type_size_bits(deduced_type), 
							current_token_);
					}
				}
			}
		}
		
		if (peek().is_eof()) {
			return ParsedFunctionArguments::make_error("Expected ',' or ')' in function call", current_token_);
		}
		
		if (peek() == ")"_tok) {
			break;
		}
		
		if (!consume(","_tok)) {
			return ParsedFunctionArguments::make_error("Expected ',' between function arguments", current_token_);
		}
	}
	
	if (ctx.collect_types) {
		return ParsedFunctionArguments::make_success(std::move(args), std::move(arg_types));
	}
	return ParsedFunctionArguments::make_success(std::move(args));
}

// Helper to apply lvalue reference for perfect forwarding deduction
// This is used when collecting argument types for template instantiation.
// In perfect forwarding (T&&), lvalues should deduce to T& while rvalues deduce to T.
std::vector<TypeSpecifierNode> Parser::apply_lvalue_reference_deduction(
	const ChunkedVector<ASTNode>& args, 
	const std::vector<TypeSpecifierNode>& arg_types)
{
	std::vector<TypeSpecifierNode> result;
	result.reserve(arg_types.size());
	
	for (size_t i = 0; i < arg_types.size(); ++i) {
		TypeSpecifierNode arg_type_node = arg_types[i];
		
		// Check if this is an lvalue (for perfect forwarding deduction)
		// Lvalues: named variables, array subscripts, member access, dereferences, string literals
		// Rvalues: numeric/bool literals, temporaries, function calls returning non-reference
		if (i < args.size() && args[i].is<ExpressionNode>()) {
			const ExpressionNode& expr = args[i].as<ExpressionNode>();
			bool is_lvalue = std::visit([](const auto& inner) -> bool {
				using T = std::decay_t<decltype(inner)>;
				if constexpr (std::is_same_v<T, IdentifierNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
					return inner.op() == "*" || inner.op() == "++" || inner.op() == "--";
				} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
					return true;
				} else {
					return false;
				}
			}, expr);
			
			if (is_lvalue) {
				arg_type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);
			}
		}
		
		result.push_back(arg_type_node);
	}
	
	return result;
}

// Consume leading specifiers (constexpr, consteval, inline, explicit, virtual) before a member declaration.
// Handles explicit(condition) syntax. Returns a bitmask of MemberLeadingSpecifiers flags.
FlashCpp::MemberLeadingSpecifiers Parser::parse_member_leading_specifiers() {
	using enum FlashCpp::MemberLeadingSpecifiers;
	FlashCpp::MemberLeadingSpecifiers specs = MLS_None;
	while (true) {
		auto k = peek();
		if (k == "constexpr"_tok) {
			specs |= MLS_Constexpr;
			advance();
		} else if (k == "consteval"_tok) {
			specs |= MLS_Consteval;
			advance();
		} else if (k == "inline"_tok) {
			specs |= MLS_Inline;
			advance();
		} else if (k == "explicit"_tok) {
			advance();
			if (peek() == "("_tok) {
				// explicit(condition) - parse and evaluate the condition using constexpr evaluator
				advance(); // consume '('
				
				// Parse the condition expression
				ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				bool explicit_value = true;  // Default to true if evaluation fails
				
				if (!condition_result.is_error() && condition_result.node().has_value()) {
					// Evaluate the constant expression using ConstExprEvaluator
					ConstExpr::EvaluationContext ctx(gSymbolTable);
					ctx.parser = this;  // Enable template function instantiation if needed
					
					auto eval_result = ConstExpr::Evaluator::evaluate(*condition_result.node(), ctx);
					
					if (eval_result.success()) {
						// Convert result to bool - any non-zero value is true
						explicit_value = eval_result.as_bool();
					} else {
						// If evaluation fails (e.g., template-dependent expression),
						// default to true for safety (explicit is the safer default)
						FLASH_LOG(Parser, Debug, "explicit(condition) evaluation failed: ", 
							eval_result.error_message, " - defaulting to explicit=true");
						explicit_value = true;
					}
				}
				
				if (!consume(")"_tok)) {
					// Error: expected closing paren
				}
				
				// Only set MLS_Explicit if the condition is true
				if (explicit_value) {
					specs |= MLS_Explicit;
				}
			} else {
				// Plain explicit (no condition) - always true
				specs |= MLS_Explicit;
			}
		} else if (k == "virtual"_tok) {
			specs |= MLS_Virtual;
			advance();
		} else {
			break;
		}
	}
	return specs;
}

// Phase 2: Unified trailing specifiers parsing
// This method handles all common trailing specifiers after function parameters:
// - CV qualifiers: const, volatile
// - Ref qualifiers: &, &&
// - noexcept specifier: noexcept, noexcept(expr)
// - Virtual specifiers: override, final
// - Special definitions: = 0 (pure virtual), = default, = delete
// - Attributes: __attribute__((...))
ParseResult Parser::parse_function_trailing_specifiers(
	FlashCpp::MemberQualifiers& out_quals,
	FlashCpp::FunctionSpecifiers& out_specs
) {
	// Initialize output structures
	out_quals = FlashCpp::MemberQualifiers{};
	out_specs = FlashCpp::FunctionSpecifiers{};

	while (!peek().is_eof()) {
		const Token& token = peek_info();

		// Parse CV qualifiers (const, volatile)
		if (token.kind() == "const"_tok) {
			out_quals.cv |= CVQualifier::Const;
			advance();
			continue;
		}
		if (token.kind() == "volatile"_tok) {
			out_quals.cv |= CVQualifier::Volatile;
			advance();
			continue;
		}

		// Parse ref qualifiers (& and &&)
		if (token.kind() == "&"_tok) {
			advance();
			out_quals.ref_qualifier = ReferenceQualifier::LValueReference;
			continue;
		}
		if (token.kind() == "&&"_tok) {
			advance();
			out_quals.ref_qualifier = ReferenceQualifier::RValueReference;
			continue;
		}

		// Parse noexcept specifier
		if (token.kind() == "noexcept"_tok) {
			advance(); // consume 'noexcept'
			out_specs.is_noexcept = true;

			// Check for noexcept(expr) form
			if (peek() == "("_tok) {
				advance(); // consume '('

				// Parse the constant expression
				auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (expr_result.is_error()) {
					return expr_result;
				}

				if (expr_result.node().has_value()) {
					out_specs.noexcept_expr = *expr_result.node();
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after noexcept expression", current_token_);
				}
			}
			continue;
		}

		// Parse throw() (old-style exception specification) - just skip it
		if (token.kind() == "throw"_tok) {
			advance(); // consume 'throw'
			if (peek() == "("_tok) {
				advance(); // consume '('
				int paren_depth = 1;
				while (!peek().is_eof() && paren_depth > 0) {
					if (peek() == "("_tok) paren_depth++;
					else if (peek() == ")"_tok) paren_depth--;
					advance();
				}
			}
			continue;
		}

		// Parse requires clause - skip the constraint expression
		// Pattern: func() noexcept requires constraint { }
		// Also handles: requires requires { expr } (nested requires expression)
		if (token.kind() == "requires"_tok) {
			advance(); // consume 'requires'
			
			// Skip the constraint expression by counting balanced brackets/parens
			// The constraint expression ends before '{', ';', '= default', '= delete', or '= 0'
			// BUT: If the constraint is a requires-expression, its body uses { } which shouldn't end the clause
			int paren_depth = 0;
			int angle_depth = 0;
			int brace_depth = 0;
			while (!peek().is_eof()) {
				auto tk = peek();
				
				// Special handling for 'requires' keyword inside the constraint
				// This indicates a requires-expression like: requires requires { ... }
				// The { } after a nested 'requires' is the requires-expression body, not the function body
				if (tk == "requires"_tok) {
					advance(); // consume nested 'requires'
					// Skip optional parameter list: requires(const T t) { ... }
					if (peek() == "("_tok) {
						advance(); // consume '('
						int param_paren_depth = 1;
						while (!peek().is_eof() && param_paren_depth > 0) {
							if (peek() == "("_tok) param_paren_depth++;
							else if (peek() == ")"_tok) param_paren_depth--;
							advance();
						}
					}
					// Expect the requires-expression body
					if (peek() == "{"_tok) {
						advance(); // consume '{'
						brace_depth++;
					}
					continue;
				}
				
				// At top level, check for end of constraint BEFORE updating depth tracking
				// This ensures we break on the function body '{' instead of consuming it
				if (paren_depth == 0 && angle_depth == 0 && brace_depth == 0) {
					// Body start or end of declaration
					if (tk == "{"_tok || tk == ";"_tok) {
						break;
					}
					// Check for = default, = delete, = 0
					if (tk == "="_tok) {
						break;
					}
				}
				
				// Track nested brackets (after checking for end of constraint)
				if (tk == "("_tok) paren_depth++;
				else if (tk == ")"_tok) paren_depth--;
				else if (tk == "{"_tok) brace_depth++;
				else if (tk == "}"_tok) brace_depth--;
				else update_angle_depth(tk, angle_depth);
				
				advance();
			}
			continue;
		}

		// Parse override/final
		// Note: 'override' and 'final' are contextual keywords in C++11+
		// They may be tokenized as either Keyword or Identifier depending on context
		// We accept both to be safe
		if (token.kind() == "override"_tok || 
		    (token.type() == Token::Type::Identifier && token.value() == "override")) {
			out_specs.is_override = true;
			advance();
			continue;
		}
		if (token.kind() == "final"_tok ||
		    (token.type() == Token::Type::Identifier && token.value() == "final")) {
			out_specs.is_final = true;
			advance();
			continue;
		}

		// Parse = 0 (pure virtual), = default, = delete
		if (token.kind() == "="_tok) {
			auto next_kind = peek(1);
			if (next_kind.is_literal()) {
				// Check for "= 0" (pure virtual) — need string check for "0"
				if (peek_info(1).value() == "0") {
					advance(); // consume '='
					advance(); // consume '0'
					out_specs.definition = FlashCpp::DefinitionSpecifier::PureVirtual;
					continue;
				}
			}
			if (next_kind == "default"_tok) {
				advance(); // consume '='
				advance(); // consume 'default'
				out_specs.definition = FlashCpp::DefinitionSpecifier::Defaulted;
				continue;
			}
			if (next_kind == "delete"_tok) {
				advance(); // consume '='
				advance(); // consume 'delete'
				out_specs.definition = FlashCpp::DefinitionSpecifier::Deleted;
				continue;
			}
			// '=' followed by something else - not a trailing specifier
			break;
		}

		// Parse __attribute__((...))
		// Note: __attribute__ is an identifier, not a keyword — string compare required
		if (token.type() == Token::Type::Identifier && token.value() == "__attribute__") {
			skip_gcc_attributes();
			continue;
		}

		// Not a trailing specifier, stop
		break;
	}

	return ParseResult::success();
}

// Phase 4: Unified function header parsing
// This method parses the complete function header (return type, name, parameters, trailing specifiers)
// in a unified way across all function types (free functions, member functions, constructors, etc.)
ParseResult Parser::parse_function_header(
	const FlashCpp::FunctionParsingContext& ctx,
	FlashCpp::ParsedFunctionHeader& out_header
) {
	// Initialize output header
	out_header = FlashCpp::ParsedFunctionHeader{};

	// Parse return type (if not constructor/destructor)
	if (ctx.kind != FlashCpp::FunctionKind::Constructor && 
	    ctx.kind != FlashCpp::FunctionKind::Destructor) {
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		if (type_result.node().has_value() && type_result.node()->is<TypeSpecifierNode>()) {
			// Store pointer to the type node
			out_header.return_type = &type_result.node()->as<TypeSpecifierNode>();
		}
	}

	// Parse function name
	// Note: For operators, we need special handling
	if (ctx.kind == FlashCpp::FunctionKind::Operator || ctx.kind == FlashCpp::FunctionKind::Conversion) {
		// Operator parsing is complex - for now, just check for 'operator' keyword
		if (peek() == "operator"_tok) {
			out_header.name_token = peek_info();
			advance();
			// Operator symbol parsing would continue here in full implementation
		} else {
			return ParseResult::error("Expected 'operator' keyword", current_token_);
		}
	} else if (ctx.kind == FlashCpp::FunctionKind::Constructor) {
		// Constructor name must match the parent struct name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected constructor name", current_token_);
		}
		if (peek_info().value() != ctx.parent_struct_name) {
			return ParseResult::error("Constructor name must match class name", peek_info());
		}
		out_header.name_token = peek_info();
		advance();
	} else if (ctx.kind == FlashCpp::FunctionKind::Destructor) {
		// Destructor must start with '~'
		if (peek() != "~"_tok) {
			return ParseResult::error("Expected '~' for destructor", current_token_);
		}
		advance();  // consume '~'
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected destructor name", current_token_);
		}
		if (peek_info().value() != ctx.parent_struct_name) {
			return ParseResult::error("Destructor name must match class name", peek_info());
		}
		out_header.name_token = peek_info();
		advance();
	} else {
		// Regular function name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected function name", current_token_);
		}
		out_header.name_token = peek_info();
		advance();
	}

	// Parse parameter list using Phase 1 unified method
	auto params_result = parse_parameter_list(out_header.params, out_header.storage.calling_convention);
	if (params_result.is_error()) {
		return params_result;
	}

	// Parse trailing specifiers using Phase 2 unified method
	auto specs_result = parse_function_trailing_specifiers(out_header.member_quals, out_header.specifiers);
	if (specs_result.is_error()) {
		return specs_result;
	}

	// Validate specifiers for function kind
	if (ctx.kind == FlashCpp::FunctionKind::Free) {
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Free functions cannot be virtual", out_header.name_token);
		}
		if (out_header.specifiers.is_override || out_header.specifiers.is_final) {
			return ParseResult::error("Free functions cannot use override/final", out_header.name_token);
		}
		if (out_header.specifiers.is_pure_virtual()) {
			return ParseResult::error("Free functions cannot be pure virtual", out_header.name_token);
		}
		// CV qualifiers don't apply to free functions
		if (out_header.member_quals.is_const() || out_header.member_quals.is_volatile()) {
			return ParseResult::error("Free functions cannot have const/volatile qualifiers", out_header.name_token);
		}
	}

	if (ctx.kind == FlashCpp::FunctionKind::StaticMember) {
		// Static member functions can't be virtual or have CV qualifiers
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Static member functions cannot be virtual", out_header.name_token);
		}
		if (out_header.member_quals.is_const() || out_header.member_quals.is_volatile()) {
			return ParseResult::error("Static member functions cannot have const/volatile qualifiers", out_header.name_token);
		}
	}

	if (ctx.kind == FlashCpp::FunctionKind::Constructor) {
		// Constructors can't be virtual, override, final, or have return type
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Constructors cannot be virtual", out_header.name_token);
		}
		if (out_header.specifiers.is_override || out_header.specifiers.is_final) {
			return ParseResult::error("Constructors cannot use override/final", out_header.name_token);
		}
	}

	// Parse trailing return type if present (for auto return type)
	if (peek() == "->"_tok) {
		advance();  // consume '->'
		auto trailing_result = parse_type_specifier();
		if (trailing_result.is_error()) {
			return trailing_result;
		}
		
		// Apply pointer and reference qualifiers (e.g., T*, T&, T&&)
		if (trailing_result.node().has_value() && trailing_result.node()->is<TypeSpecifierNode>()) {
			TypeSpecifierNode& type_spec = trailing_result.node()->as<TypeSpecifierNode>();
			
			consume_pointer_ref_modifiers(type_spec);
		}
		
		out_header.trailing_return_type = trailing_result.node();
	}

	return ParseResult::success();
}

// Phase 4: Create a FunctionDeclarationNode from a ParsedFunctionHeader
// This bridges the unified header parsing with the existing AST node creation
ParseResult Parser::create_function_from_header(
	const FlashCpp::ParsedFunctionHeader& header,
	[[maybe_unused]] const FlashCpp::FunctionParsingContext& ctx
) {
	// Create the type specifier node for the return type
	ASTNode type_node;
	if (header.return_type != nullptr) {
		type_node = ASTNode::emplace_node<TypeSpecifierNode>(*header.return_type);
	} else {
		// For constructors/destructors, create a void return type
		type_node = ASTNode::emplace_node<TypeSpecifierNode>(Type::Void, 0, 0, Token());
	}

	// Create the declaration node with type and name
	auto [decl_node, decl_ref] = emplace_node_ref<DeclarationNode>(type_node, header.name_token);

	// Create the function declaration node using the DeclarationNode reference
	auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_ref);

	// Set calling convention
	func_ref.set_calling_convention(header.storage.calling_convention);

	// Set linkage
	if (header.storage.linkage != Linkage::None) {
		func_ref.set_linkage(header.storage.linkage);
	} else if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	} else {
		// Check if there's a forward declaration with linkage and inherit it
		// Use lookup_all to check all overloads in case there are multiple
		auto all_overloads = gSymbolTable.lookup_all(header.name_token.value());
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const auto& forward_decl = overload.as<FunctionDeclarationNode>();
				if (forward_decl.linkage() != Linkage::None) {
					func_ref.set_linkage(forward_decl.linkage());
					break;  // Found a forward declaration with linkage, use it
				}
			}
		}
	}

	// Add parameters
	for (const auto& param : header.params.parameters) {
		func_ref.add_parameter_node(param);
	}
	func_ref.set_is_variadic(header.params.is_variadic);

	// Set noexcept if specified
	if (header.specifiers.is_noexcept) {
		func_ref.set_noexcept(true);
		if (header.specifiers.noexcept_expr.has_value()) {
			func_ref.set_noexcept_expression(*header.specifiers.noexcept_expr);
		}
	}

	// Set constexpr/consteval
	func_ref.set_is_constexpr(header.storage.is_constexpr());
	func_ref.set_is_consteval(header.storage.is_consteval());

	return func_node;
}

