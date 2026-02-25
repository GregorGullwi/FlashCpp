ParseResult Parser::parse_top_level_node()
{
	// Save the current token's position to restore later in case of a parsing
	// error
	ScopedTokenPosition saved_position(*this);

#if WITH_DEBUG_INFO
	if (break_at_line_.has_value() && peek_info().line() == break_at_line_)
	{
		DEBUG_BREAK();
	}
#endif

	// Skip empty declarations (lone semicolons) - valid in C++ (empty-declaration)
	if (peek() == ";"_tok) {
		advance();
		return saved_position.success();
	}

	// Check for __pragma() - Microsoft's inline pragma syntax
	// e.g., __pragma(pack(push, 8))
	if (peek_info().type() == Token::Type::Identifier && peek_info().value() == "__pragma") {
		advance(); // consume '__pragma'
		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '__pragma'", current_token_);
		}
		
		// Now parse what's inside - it could be pack(...) or something else
		if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
		    peek_info().value() == "pack") {
			advance(); // consume 'pack'
			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after '__pragma(pack'", current_token_);
			}
			
			// Reuse the pack parsing logic by calling parse_pragma_pack_inner
			auto pack_result = parse_pragma_pack_inner();
			if (pack_result.is_error()) {
				return pack_result;
			}
			
			// Consume the outer closing ')'
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after '__pragma(...)'", current_token_);
			}
			return saved_position.success();
		} else {
			// Unknown __pragma content - skip until balanced parens
			int paren_depth = 1;
			while (!peek().is_eof() && paren_depth > 0) {
				if (peek() == "("_tok) {
					paren_depth++;
				} else if (peek() == ")"_tok) {
					paren_depth--;
				}
				advance();
			}
			return saved_position.success();
		}
	}

	// Check for #pragma directives
	if (peek() == "#"_tok) {
		advance(); // consume '#'
		if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
		    peek_info().value() == "pragma") {
			advance(); // consume 'pragma'
			if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
			    peek_info().value() == "pack") {
				advance(); // consume 'pack'

				if (!consume("("_tok)) {
					return ParseResult::error("Expected '(' after '#pragma pack'", current_token_);
				}

				// Use the shared helper function to parse the pack contents
				auto pack_result = parse_pragma_pack_inner();
				if (pack_result.is_error()) {
					return saved_position.propagate(std::move(pack_result));
				}
				return saved_position.success();
			} else {
				// Unknown pragma - skip until end of line or until we hit a token that looks like the start of a new construct
				// Pragmas can span multiple lines with parentheses, so we need to be careful
				FLASH_LOG(Parser, Warning, "Skipping unknown pragma: ", (!peek().is_eof() ? std::string(peek_info().value()) : "EOF"));
				int paren_depth = 0;
				while (!peek().is_eof()) {
					FLASH_LOG(Parser, Debug, "  pragma skip loop: token='", peek_info().value(), "' type=", static_cast<int>(peek_info().type()), " paren_depth=", paren_depth);
					if (peek() == "("_tok) {
						paren_depth++;
						advance();
					} else if (peek() == ")"_tok) {
						paren_depth--;
						advance();
						if (paren_depth == 0) {
							// End of pragma
							break;
						}
					} else if (paren_depth == 0 && peek() == "#"_tok) {
						// Start of a new preprocessor directive
						break;
					} else if (paren_depth == 0 && peek().is_keyword()) {
						// Start of a new declaration (namespace, class, extern, etc.)
						break;
					} else {
						advance();
					}
				}
				return saved_position.success();
			}
		}
	}

	// Helper: parse, push resulting node to AST, return success/propagate.
	auto try_parse_and_push = [&](ParseResult result) -> ParseResult {
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	};

	// Check if it's a using directive, using declaration, or namespace alias
	if (peek() == "using"_tok)
		return try_parse_and_push(parse_using_directive_or_declaration());

	// Check if it's a static_assert declaration
	if (peek() == "static_assert"_tok) {
		auto result = parse_static_assert();
		if (!result.is_error()) {
			// static_assert doesn't produce an AST node (compile-time only)
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	}

	// Check for inline namespace declaration (inline namespace foo { ... })
	if (peek() == "inline"_tok) {
		auto next = peek_info(1);
		if (next.kind() == "namespace"_tok) {
			pending_inline_namespace_ = true;
			advance(); // consume 'inline'
			return try_parse_and_push(parse_namespace());
		}
	}

	// Check if it's a namespace declaration
	if (peek() == "namespace"_tok)
		return try_parse_and_push(parse_namespace());

	// Check if it's a template declaration (must come before struct/class check)
	if (peek() == "template"_tok)
		return try_parse_and_push(parse_template_declaration());

	// Check if it's a concept declaration (C++20)
	if (peek() == "concept"_tok)
		return try_parse_and_push(parse_concept_declaration());

	// Check if it's a class or struct declaration
	// Note: alignas can appear before struct, but we handle that in parse_struct_declaration
	// If alignas appears before a variable declaration, it will be handled by parse_declaration_or_function_definition
	if ((peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok)) {
		auto result = parse_struct_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			// Add any pending variable declarations from the struct definition
			for (auto& var_node : pending_struct_variables_) {
				ast_nodes_.push_back(var_node);
			}
			pending_struct_variables_.clear();
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	}

	// Check if it's an enum declaration
	if (peek() == "enum"_tok)
		return try_parse_and_push(parse_enum_declaration());

	// Check if it's a typedef declaration
	if (peek() == "typedef"_tok)
		return try_parse_and_push(parse_typedef_declaration());

	// Check for extern "C" linkage specification
	if (peek() == "extern"_tok) {
		// Save position in case this is just a regular extern declaration
		SaveHandle extern_saved_pos = save_token_position();
		advance(); // consume 'extern'

		// Check if this is extern "C" or extern "C++"
		if (peek().is_string_literal()) {
			std::string_view linkage_str = peek_info().value();

			// Remove quotes from string literal
			if (linkage_str.size() >= 2 && linkage_str.front() == '"' && linkage_str.back() == '"') {
				linkage_str = linkage_str.substr(1, linkage_str.size() - 2);
			}

			Linkage linkage = Linkage::None;
			if (linkage_str == "C") {
				linkage = Linkage::C;
			} else if (linkage_str == "C++") {
				linkage = Linkage::CPlusPlus;
			} else {
				return ParseResult::error("Unknown linkage specification: " + std::string(linkage_str), current_token_);
			}

			advance(); // consume linkage string

			// Discard the extern_saved_pos since we're handling extern "C"
			discard_saved_token(extern_saved_pos);

			// Check for block form: extern "C" { ... }
			if (peek() == "{"_tok) {
				auto result = parse_extern_block(linkage);
				if (!result.is_error()) {
					if (auto node = result.node()) {
						// The block contains multiple declarations, add them all
						if (node->is<BlockNode>()) {
							const BlockNode& block = node->as<BlockNode>();
							block.get_statements().visit([&](const ASTNode& stmt) {
								ast_nodes_.push_back(stmt);
							});
						}
					}
					return saved_position.success();
				}
				return saved_position.propagate(std::move(result));
			}

			// Single declaration form: extern "C" int func();
			// Set the current linkage and parse the declaration/function
			Linkage saved_linkage = current_linkage_;
			current_linkage_ = linkage;

			ParseResult decl_result = parse_declaration_or_function_definition();

			// Restore the previous linkage
			current_linkage_ = saved_linkage;

			if (decl_result.is_error()) {
				return decl_result;
			}

			// Add the node to the AST if it exists
			if (auto decl_node = decl_result.node()) {
				ast_nodes_.push_back(*decl_node);
			}

			return saved_position.success();
		} else if (peek() == "template"_tok) {
			// extern template class allocator<char>; — explicit instantiation declaration
			// Don't restore, we've already consumed 'extern', and parse_template_declaration
			// will consume 'template'. Discard the saved position.
			discard_saved_token(extern_saved_pos);
			auto template_result = parse_template_declaration();
			if (!template_result.is_error()) {
				return saved_position.success();
			}
			return saved_position.propagate(std::move(template_result));
		} else {
			// Regular extern without linkage specification, restore and continue
			restore_token_position(extern_saved_pos);
		}
	}

	// Attempt to parse a function definition, variable declaration, or typedef
	FLASH_LOG(Parser, Debug, "parse_top_level_node: About to call parse_declaration_or_function_definition, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	auto result = parse_declaration_or_function_definition();
	if (!result.is_error()) {
		if (auto node = result.node()) {
			ast_nodes_.push_back(*node);
		}
		return saved_position.success();
	}

	// If we failed to parse any top-level construct, restore the token position
	// and report an error
	FLASH_LOG(Parser, Debug, "parse_top_level_node: parse_declaration_or_function_definition failed, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A", ", error: ", result.error_message());
	
	// Preserve the original error token instead of creating a new error with the saved token
	// This ensures error messages point to the actual error location, not the start of the construct
	return saved_position.propagate(std::move(result));
}

ParseResult Parser::parse_type_and_name() {
    // Add parsing depth check to prevent infinite loops
    if (++parsing_depth_ > MAX_PARSING_DEPTH) {
        --parsing_depth_;
        FLASH_LOG(Parser, Error, "Maximum parsing depth (", MAX_PARSING_DEPTH, ") exceeded in parse_type_and_name()");
        FLASH_LOG(Parser, Error, "This indicates an infinite loop in type parsing");
        return ParseResult::error("Maximum parsing depth exceeded - possible infinite loop", current_token_);
    }
    
    // RAII guard to decrement depth on all exit paths
    struct DepthGuard {
        size_t& depth;
        ~DepthGuard() { --depth; }
    } depth_guard{parsing_depth_};
    
    FLASH_LOG(Parser, Debug, "parse_type_and_name: Starting, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
    
    // Check for alignas specifier before the type
    std::optional<size_t> custom_alignment = parse_alignas_specifier();

    // Parse the type specifier
    FLASH_LOG(Parser, Debug, "parse_type_and_name: About to parse type_specifier, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
    auto type_specifier_result = parse_type_specifier();
    if (type_specifier_result.is_error()) {
        FLASH_LOG(Parser, Debug, "parse_type_and_name: parse_type_specifier failed: ", type_specifier_result.error_message());
        return type_specifier_result;
    }

    if (!type_specifier_result.node().has_value()) {
        return ParseResult::error("Expected type specifier", current_token_);
    }

    // Get the type specifier node to modify it with pointer levels
    TypeSpecifierNode& type_spec = type_specifier_result.node()->as<TypeSpecifierNode>();

    // Check for structured binding: auto [a, b, c] = expr;
    // Also support: auto& [a, b] = pair; and auto&& [x, y] = temp;
    // This must be checked after parsing the type specifier (auto) but before parsing pointer/reference/identifier
    if (type_spec.type() == Type::Auto) {
        // Check for optional reference qualifiers
        ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
        
        if (peek() == "&"_tok) {
            advance(); // consume '&'
            
            // Check if it's && (rvalue reference) or just & (lvalue reference)
            if (peek() == "&"_tok) {
                advance(); // consume second '&'
                ref_qualifier = ReferenceQualifier::RValueReference;
            } else {
                ref_qualifier = ReferenceQualifier::LValueReference;
            }
        }
        
        // Now check for '[' to confirm structured binding
        if (peek() == "["_tok) {
            FLASH_LOG(Parser, Debug, "parse_type_and_name: Detected structured binding pattern: auto [");
            
            // Parse structured binding with reference qualifier
            return parse_structured_binding(type_spec.cv_qualifier(), ref_qualifier);
        }
        
        // If we consumed reference qualifiers but it's not a structured binding,
        // apply them to the type_spec (e.g., auto& ref = x; or auto&& rvalue = temp;)
        if (ref_qualifier != ReferenceQualifier::None) {
            if (ref_qualifier == ReferenceQualifier::RValueReference) {
                type_spec.set_reference(true);  // true = rvalue reference
            } else if (ref_qualifier == ReferenceQualifier::LValueReference) {
                type_spec.set_reference(false);  // false = lvalue reference
            }
        }
    }

    // C++20 constrained auto parameters: ConceptName auto param or Concept<T> auto param
    // The concept constraint was parsed as a UserDefined type by parse_type_specifier().
    // If followed by 'auto', this is an abbreviated function template parameter.
    // Store the concept name on the TypeSpecifierNode for requires clause generation.
    if (type_spec.type() == Type::UserDefined && peek() == "auto"_tok) {
        // Capture the concept name before converting the type to Auto
        std::string_view concept_name = type_spec.token().value();
        FLASH_LOG(Parser, Debug, "parse_type_and_name: Constrained auto parameter detected (concept='", concept_name, "'), consuming 'auto'");
        advance(); // consume 'auto'
        type_spec.set_type(Type::Auto);
        // Store the concept constraint so abbreviated template generation can build a requires clause
        type_spec.set_concept_constraint(concept_name);
    }

    // Extract calling convention specifiers that can appear after the type
    // Example: void __cdecl func(); or int __stdcall* func();
    // We consume them here and save to last_calling_convention_ for the caller to retrieve
    last_calling_convention_ = CallingConvention::Default;
    while (peek().is_identifier()) {
        std::string_view token_val = peek_info().value();
        
        // Look up calling convention in the mapping table using ranges
        auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
        if (it != std::end(calling_convention_map)) {
            last_calling_convention_ = it->convention;
            advance();  // Consume calling convention token
        } else {
            break;  // Not a calling convention keyword, stop scanning
        }
    }

    // Check if this might be a function pointer declaration
    // Function pointers have the pattern: type (*identifier)(params)
    // We need to check for '(' followed by '*' to detect this
    // Also handle calling convention: type (__cdecl *identifier)(params)
    if (peek() == "("_tok) {
        FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Found '(' - checking for function pointer. current_token={}",
            std::string(current_token_.value()));
        // Save position in case this isn't a function pointer or reference declarator
        SaveHandle saved_pos = save_token_position();
        advance(); // consume '('
        FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: After consuming '(', current_token={}, peek={}",
            std::string(current_token_.value()),
            !peek().is_eof() ? std::string(peek_info().value()) : "N/A");

        parse_calling_convention();

        // Check if next token is '*' (function pointer pattern) or '&' (reference to array pattern)
        if (peek() == "*"_tok) {
            // This looks like a function pointer! Use parse_declarator
            restore_token_position(saved_pos);
            auto result = parse_declarator(type_spec, Linkage::None);
            if (!result.is_error()) {
                if (auto decl_node = result.node()) {
                    // Check if result is a DeclarationNode (for function pointers) or FunctionDeclarationNode
                    // For DeclarationNode, apply custom alignment directly
                    // For FunctionDeclarationNode, alignment would apply to the underlying declaration
                    if (decl_node->is<DeclarationNode>() && custom_alignment.has_value()) {
                        decl_node->as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
                    } else if (decl_node->is<FunctionDeclarationNode>() && custom_alignment.has_value()) {
                        // For function declarations, alignment applies to the underlying declaration node
                        DeclarationNode& inner_decl = const_cast<DeclarationNode&>(
                            decl_node->as<FunctionDeclarationNode>().decl_node());
                        inner_decl.set_custom_alignment(custom_alignment.value());
                    }
                }
                discard_saved_token(saved_pos);
                return result;
            }
            // If parse_declarator fails, fall through to regular parsing
            restore_token_position(saved_pos);;
        } else if (!peek().is_eof() && (peek() == "&"_tok || peek() == "&&"_tok)) {
            // This is a reference to array pattern: T (&arr)[N] or T (&&arr)[N]
            // Also handles unnamed variant: T (&)[N] (used in function parameters)
            // Pattern: type (&identifier)[array_size] or type (&&identifier)[array_size]
            bool is_rvalue_ref = (peek() == "&&"_tok);
            advance(); // consume '&' or '&&'
            
            // Parse optional identifier (may be unnamed for function parameters)
            Token ref_identifier;
            bool has_name = false;
            if (peek().is_identifier()) {
                ref_identifier = peek_info();
                has_name = true;
                advance();
            }
            
            // Expect closing ')'
            if (peek() != ")"_tok) {
                // Not a valid reference-to-array pattern, restore and continue
                restore_token_position(saved_pos);
            } else {
                advance(); // consume ')'
                
                // Expect array size: '[' size ']'
                if (peek() != "["_tok) {
                    // Not a reference-to-array pattern, restore and continue
                    restore_token_position(saved_pos);
                } else {
                    advance(); // consume '['
                    
                    // Parse array size expression
                    auto size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                    if (size_result.is_error()) {
                        restore_token_position(saved_pos);
                    } else {
                        std::optional<ASTNode> array_size_expr = size_result.node();
                        
                        // Expect closing ']'
                        if (!consume("]"_tok)) {
                            restore_token_position(saved_pos);
                        } else {
                            // Successfully parsed reference-to-array pattern
                            // Set the type_spec to be a reference
                            if (is_rvalue_ref) {
                                type_spec.set_reference(true);  // rvalue reference
                            } else {
                                type_spec.set_lvalue_reference(true);  // lvalue reference
                            }
                            type_spec.set_array(true);
                            
                            // Use a synthetic unnamed token if no name was provided
                            if (!has_name) {
                                ref_identifier = Token(Token::Type::Identifier, ""sv,
                                    type_spec.token().line(), type_spec.token().column(),
                                    type_spec.token().file_index());
                            }
                            
                            // Create declaration node
                            auto decl_node = emplace_node<DeclarationNode>(
                                emplace_node<TypeSpecifierNode>(type_spec),
                                ref_identifier,
                                array_size_expr
                            );
                            
                            if (custom_alignment.has_value()) {
                                decl_node.as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
                            }
                            
                            discard_saved_token(saved_pos);
                            return ParseResult::success(decl_node);
                        }
                    }
                }
            }
        } else if (peek().is_identifier()) {
            // Check for pointer-to-member-function pattern: type (ClassName::*identifier)(params)
            // After '(' we see an identifier, check if it's followed by ::*
            SaveHandle ptrmf_check_pos = save_token_position();
            Token class_name_token = peek_info();
            advance(); // consume class name
            
            if (peek() == "::"_tok) {
                advance(); // consume '::'
                
                if (peek() == "*"_tok) {
                    advance(); // consume '*'
                    
                    // Parse CV-qualifiers after * if any
                    [[maybe_unused]] CVQualifier ptr_cv = parse_cv_qualifiers();
                    
                    // Check for identifier (parameter name) or ')' (unnamed parameter)
                    Token identifier_token;
                    if (peek().is_identifier()) {
                        identifier_token = peek_info();
                        advance(); // consume identifier
                    } else {
                        // Unnamed pointer-to-member-function parameter
                        identifier_token = Token(Token::Type::Identifier, ""sv,
                            current_token_.line(), current_token_.column(),
                            current_token_.file_index());
                    }
                    
                    // Expect ')'
                    if (peek() == ")"_tok) {
                        advance(); // consume ')'
                        
                        // Expect '(' for function parameters
                        if (peek() == "("_tok) {
                            // This is a pointer-to-member-function declaration!
                            FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Detected pointer-to-member-function: {} ({}::*{})()", 
                                type_spec.token().value(), class_name_token.value(), identifier_token.value());
                            
                            // Skip the function parameter list by counting parentheses
                            advance(); // consume '('
                            int paren_depth = 1;
                            while (paren_depth > 0 && !peek().is_eof()) {
                                if (peek() == "("_tok) {
                                    paren_depth++;
                                } else if (peek() == ")"_tok) {
                                    paren_depth--;
                                }
                                advance();
                            }
                            
                            // Skip any cv-qualifiers after the function parameters
                            // e.g., _Ret (_Tp::*__pf)() const
                            while (!peek().is_eof()) {
                                std::string_view tok = peek_info().value();
                                if (tok == "const" || tok == "volatile" || tok == "noexcept") {
                                    advance();
                                } else {
                                    break;
                                }
                            }
                            
                            // Set up the type as a pointer-to-member-function
                            type_spec.set_member_class_name(class_name_token.handle());
                            type_spec.add_pointer_level(CVQualifier::None);
                            
                            // Create declaration node
                            auto decl_node = emplace_node<DeclarationNode>(
                                emplace_node<TypeSpecifierNode>(type_spec),
                                identifier_token
                            );
                            
                            if (custom_alignment.has_value()) {
                                decl_node.as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
                            }
                            
                            discard_saved_token(saved_pos);
                            discard_saved_token(ptrmf_check_pos);
                            return ParseResult::success(decl_node);
                        }
                    }
                }
            }
            // Not a pointer-to-member-function pattern, restore inner position
            restore_token_position(ptrmf_check_pos);
            // Continue to restore outer position below
            restore_token_position(saved_pos);
        } else {
            // Not a function pointer or reference declarator, restore and continue with regular parsing
            FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Not a function pointer, restoring. Before restore: current_token={}", 
                std::string(current_token_.value()));
            restore_token_position(saved_pos);
            FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: After restore: current_token={}, peek={}", 
                std::string(current_token_.value()),
                !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
        }
    }

    // Regular pointer/reference/identifier parsing (existing code)
    // Check for pointer-to-member syntax: ClassName::*
    // Pattern: int Point::*ptr_to_x
    if (peek().is_identifier()) {
        // Save position in case this isn't a pointer-to-member
        SaveHandle saved_pos = save_token_position();
        Token class_name_token = peek_info();
        advance(); // consume class name
        
        // Check for ::
        if (peek() == "::"_tok) {
            advance(); // consume '::'
            
            // Check for *
            if (peek() == "*"_tok) {
                advance(); // consume '*'
                
                // This is a pointer-to-member declaration!
                FLASH_LOG(Parser, Debug, "parse_type_and_name: Detected pointer-to-member: ", 
                          class_name_token.value(), "::*");
                
                // Set the member class name
                type_spec.set_member_class_name(class_name_token.handle());
                
                // Add a pointer level to indicate this is a pointer
                type_spec.add_pointer_level(CVQualifier::None);
                
                // Discard the saved position and continue parsing
                discard_saved_token(saved_pos);
            } else {
                // Not a pointer-to-member, restore position
                restore_token_position(saved_pos);
            }
        } else {
            // Not followed by ::, restore position
            restore_token_position(saved_pos);
        }
    }
    
    // Parse pointer declarators: * [const] [volatile] *...
    // Example: int* const* volatile ptr
    while (peek() == "*"_tok) {
        advance(); // consume '*'

        // Check for CV-qualifiers after the *
        CVQualifier ptr_cv = parse_cv_qualifiers();

        type_spec.add_pointer_level(ptr_cv);
    }

    // Second function pointer check: after pointer levels have been consumed.
    // This handles patterns like: void *(*callback)(void *)
    // After parsing 'void' as type and '*' as pointer level (making void*),
    // we now see '(' which starts a function pointer declarator.
    if (type_spec.pointer_depth() > 0 && peek() == "("_tok) {
        SaveHandle saved_pos = save_token_position();
        advance(); // consume '('

        parse_calling_convention();

        if (peek() == "*"_tok) {
            // Looks like a function pointer with pointer return type: type* (*name)(params)
            restore_token_position(saved_pos);
            auto result = parse_declarator(type_spec, Linkage::None);
            if (!result.is_error()) {
                if (auto decl_node = result.node()) {
                    if (decl_node->is<DeclarationNode>() && custom_alignment.has_value()) {
                        decl_node->as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
                    }
                }
                discard_saved_token(saved_pos);
                return result;
            }
        }
        restore_token_position(saved_pos);
    }

    // Parse postfix cv-qualifiers before pointers/references: Type const* or Type const&
    // This handles C++ postfix const/volatile syntax like: typename _Str::value_type const*
    CVQualifier postfix_cv = parse_cv_qualifiers();
    type_spec.add_cv_qualifier(postfix_cv);

    // After postfix cv-qualifiers, parse pointer and reference declarators.
    // This handles patterns like: typename _Str::value_type const* __lhs
    // where const appears between the dependent type and the pointer.
    consume_pointer_ref_modifiers(type_spec);

    // Check for calling convention AFTER pointer/reference declarators
    // Example: void* __cdecl func(); or int& __stdcall func();
    // This handles the case where calling convention appears after * or &
    while (peek().is_identifier()) {
        std::string_view token_val = peek_info().value();

        // Look up calling convention in the mapping table using ranges
        auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
        if (it != std::end(calling_convention_map)) {
            last_calling_convention_ = it->convention;
            advance();  // Consume calling convention token
        } else {
            break;  // Not a calling convention keyword, stop scanning
        }
    }

    // Check for parameter pack: Type... identifier
    // This is used in variadic function templates like: template<typename... Args> void func(Args... args)
    bool is_parameter_pack = false;
    if (!peek().is_eof() && 
        (peek_info().type() == Token::Type::Operator || peek_info().type() == Token::Type::Punctuator) &&
        peek() == "..."_tok) {
        advance(); // consume '...'
        is_parameter_pack = true;
    }

    // Check for alignas specifier before the identifier (if not already specified)
    if (!custom_alignment.has_value()) {
        custom_alignment = parse_alignas_specifier();
    }

    // Parse the identifier (name) or operator overload
    Token identifier_token;

    // Check for operator overload (e.g., operator=, operator())
    if (peek() == "operator"_tok) {

        Token operator_keyword_token = peek_info();
        advance(); // consume 'operator'

        std::string_view operator_name;

        // Check for operator()
        if (peek() == "("_tok) {
            advance(); // consume '('
            if (peek() != ")"_tok) {
                return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
            }
            advance(); // consume ')'
            static const std::string operator_call_name = "operator()";
            operator_name = operator_call_name;
        }
        // Check for other operators
        else if (!peek().is_eof() && peek_info().type() == Token::Type::Operator) {
            Token operator_symbol_token = peek_info();
            std::string_view operator_symbol = operator_symbol_token.value();
            advance(); // consume operator symbol

            // Build operator name like "operator=" or "operator<<"
            static std::unordered_map<std::string_view, std::string> operator_names = {
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
                operator_name = it->second;
            } else {
                return ParseResult::error("Unsupported operator overload: operator" + std::string(operator_symbol), operator_symbol_token);
            }
        }
        // Check for subscript operator: operator[]
        // Note: '[' is a Punctuator, not an Operator, so it needs separate handling
        else if (peek() == "["_tok) {
            advance(); // consume '['
            if (peek() != "]"_tok) {
                return ParseResult::error("Expected ']' after 'operator['", operator_keyword_token);
            }
            advance(); // consume ']'
            static const std::string operator_subscript_name = "operator[]";
            operator_name = operator_subscript_name;
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
                static const std::string op_new = "operator new";
                static const std::string op_new_array = "operator new[]";
                operator_name = is_array ? op_new_array : op_new;
            } else {
                static const std::string op_delete = "operator delete";
                static const std::string op_delete_array = "operator delete[]";
                operator_name = is_array ? op_delete_array : op_delete;
            }
        }
        // Check for user-defined literal operator: operator""suffix or operator "" suffix
        else if (peek().is_string_literal()) {
            Token string_token = peek_info();
            advance(); // consume ""
            
            // Parse the suffix identifier (e.g., 'sv' in operator""sv)
            if (peek().is_identifier()) {
                std::string_view suffix = peek_info().value();
                advance(); // consume suffix
                
                StringBuilder op_name_builder;
                operator_name = op_name_builder.append("operator\"\"").append(suffix).commit();
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

            // Now expect "("
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
            operator_name = op_name_builder.commit();
        }

        // Create a synthetic identifier token for the operator
        identifier_token = Token(Token::Type::Identifier, operator_name,
                                operator_keyword_token.line(), operator_keyword_token.column(),
                                operator_keyword_token.file_index());
        
        // Skip any C++ attributes like [[nodiscard]] that may appear after the operator name
        // but before the parameter list (e.g., operator() [[nodiscard]] (args))
        skip_cpp_attributes();
    } else {
        // Regular identifier (or unnamed parameter)
        // First, skip any specifiers that may appear after the return type but before the identifier
        // This handles non-standard (but valid in GCC/libstdc++) patterns like: void constexpr operator=()
        while (peek().is_keyword()) {
            std::string_view kw = peek_info().value();
            if (kw == "constexpr" || kw == "consteval" || kw == "inline") {
                advance(); // skip the specifier
            } else {
                break;
            }
        }
        
        // Skip GCC __attribute__((...)) that may appear between return type and function name
        // e.g., inline _Atomic_word __attribute__((__always_inline__)) __exchange_and_add(...)
        skip_gcc_attributes();
        
        // After skipping specifiers, check if this is now an operator (e.g., void constexpr operator=())
        if (peek() == "operator"_tok) {
            Token operator_keyword_token = peek_info();
            advance(); // consume 'operator'
            
            std::string_view operator_name;
            
            // Check for operator()
            if (peek() == "("_tok) {
                advance(); // consume '('
                if (peek() != ")"_tok) {
                    return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
                }
                advance(); // consume ')'
                static const std::string operator_call_name = "operator()";
                operator_name = operator_call_name;
            }
            // Check for other operators
            else if (!peek().is_eof() && peek_info().type() == Token::Type::Operator) {
                Token operator_symbol_token = peek_info();
                std::string_view operator_symbol = operator_symbol_token.value();
                advance(); // consume operator symbol
                
                // Build operator name - use the same map as the primary operator handling
                static std::unordered_map<std::string_view, std::string> operator_names_late = {
                    {"=", "operator="}, {"<=>", "operator<=>"},
                    {"<<", "operator<<"}, {">>", "operator>>"},
                    {"+", "operator+"}, {"-", "operator-"},
                    {"*", "operator*"}, {"/", "operator/"},
                    {"%", "operator%"}, {"&", "operator&"},
                    {"|", "operator|"}, {"^", "operator^"},
                    {"~", "operator~"}, {"!", "operator!"},
                    {"<", "operator<"}, {">", "operator>"},
                    {"<=", "operator<="}, {">=", "operator>="},
                    {"==", "operator=="}, {"!=", "operator!="},
                    {"&&", "operator&&"}, {"||", "operator||"},
                    {"++", "operator++"}, {"--", "operator--"},
                    {"->", "operator->"}, {"->*", "operator->*"},
                    {"[]", "operator[]"}, {",", "operator,"},
                    // Compound assignment operators
                    {"+=", "operator+="}, {"-=", "operator-="},
                    {"*=", "operator*="}, {"/=", "operator/="},
                    {"%=", "operator%="}, {"&=", "operator&="},
                    {"|=", "operator|="}, {"^=", "operator^="},
                    {"<<=", "operator<<="}, {">>=", "operator>>="}
                };
                
                auto it = operator_names_late.find(operator_symbol);
                if (it != operator_names_late.end()) {
                    operator_name = it->second;
                } else {
                    return ParseResult::error("Unknown operator symbol", operator_symbol_token);
                }
            }
            // Check for subscript operator
            else if (peek() == "["_tok) {
                advance(); // consume '['
                if (peek() != "]"_tok) {
                    return ParseResult::error("Expected ']' after 'operator['", operator_keyword_token);
                }
                advance(); // consume ']'
                static const std::string operator_subscript_name = "operator[]";
                operator_name = operator_subscript_name;
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
                    static const std::string op_new = "operator new";
                    static const std::string op_new_array = "operator new[]";
                    operator_name = is_array ? op_new_array : op_new;
                } else {
                    static const std::string op_delete = "operator delete";
                    static const std::string op_delete_array = "operator delete[]";
                    operator_name = is_array ? op_delete_array : op_delete;
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
                    operator_name = op_name_builder.append("operator\"\"").append(suffix).commit();
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

                // Now expect "("
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
                operator_name = op_name_builder.commit();
            }
            
            // Create a synthetic identifier token for the operator
            identifier_token = Token(Token::Type::Identifier, operator_name,
                                    operator_keyword_token.line(), operator_keyword_token.column(),
                                    operator_keyword_token.file_index());
        } else {
            // Check if this might be an unnamed parameter (next token is ',', ')', '=', or '[')
            FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Parsing identifier. current_token={}, peek={}", 
                std::string(current_token_.value()),
                !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
            if (!peek().is_eof()) {
                auto next = peek_info().value();
                if (next == "," || next == ")" || next == "=" || next == "[" ||
                    next == ":" || next == ";") {
                    // This is an unnamed parameter/member - create a synthetic empty identifier
                    // ':' handles unnamed bitfields (e.g., int :32;) and ';' handles unnamed members
                    FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Unnamed parameter detected, next={}", std::string(next));
                    identifier_token = Token(Token::Type::Identifier, ""sv,
                                            current_token_.line(), current_token_.column(),
                                            current_token_.file_index());
                } else {
                    // Regular identifier
                    FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Consuming token as identifier, peek={}", std::string(next));
                    auto id_token = advance();
                    if (id_token.type() != Token::Type::Identifier) {
                        return ParseResult::error("Expected identifier token", id_token);
                    }
                    identifier_token = id_token;
                    FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Consumed identifier={}, now current_token={}, peek={}", 
                        std::string(identifier_token.value()),
                        std::string(current_token_.value()),
                        !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
                }
            } else {
                return ParseResult::error("Expected identifier or end of parameter", Token());
            }
        }
    }

    // Skip C++ attributes after identifier (e.g., func_name [[nodiscard]] (params))
    skip_cpp_attributes();

    // Check for array declaration: identifier[size] or identifier[size1][size2]...
    std::vector<ASTNode> array_dimensions;
    bool is_unsized_array = false;
    while (peek() == "["_tok) {
        advance(); // consume '['

        // Check for empty brackets (unsized array, size inferred from initializer)
        // Only the first dimension can be unsized
        if (peek() == "]"_tok) {
            if (array_dimensions.empty()) {
                // Empty brackets - array size will be inferred from initializer
                is_unsized_array = true;
            } else {
                return ParseResult::error("Only the first array dimension can be unsized", current_token_);
            }
        } else {
            // Parse the array size expression
            ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (size_result.is_error()) {
                return size_result;
            }
            array_dimensions.push_back(*size_result.node());
        }

        // Expect closing ']'
        if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
            peek() != "]"_tok) {
            return ParseResult::error("Expected ']' after array size", current_token_);
        }
        advance(); // consume ']'
    }

    // Unwrap the optional ASTNode before passing it to emplace_node
    if (auto node = type_specifier_result.node()) {
        ASTNode decl_node;
        if (!array_dimensions.empty()) {
            decl_node = emplace_node<DeclarationNode>(*node, identifier_token, std::move(array_dimensions));
        } else if (is_unsized_array) {
            // Mark as an unsized array - size will be inferred from initializer
            decl_node = emplace_node<DeclarationNode>(*node, identifier_token);
            decl_node.as<DeclarationNode>().set_unsized_array(true);
        } else {
            decl_node = emplace_node<DeclarationNode>(*node, identifier_token);
        }

        // Apply custom alignment if specified
        if (custom_alignment.has_value()) {
            decl_node.as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
        }

        // Apply parameter pack flag if this is a parameter pack
        if (is_parameter_pack) {
            decl_node.as<DeclarationNode>().set_parameter_pack(true);
        }

        return ParseResult::success(decl_node);
    }
    return ParseResult::error("Invalid type specifier node", identifier_token);
}

// Parse structured binding: auto [a, b, c] = expr;
// Also supports: auto& [a, b] = pair; and auto&& [x, y] = temp;
ParseResult Parser::parse_structured_binding(CVQualifier cv_qualifiers, ReferenceQualifier ref_qualifier) {
    FLASH_LOG(Parser, Debug, "parse_structured_binding: Starting");
    
    // At this point, we've already parsed 'auto' (and optional &/&&) and confirmed the next token is '['
    // Consume the '['
    if (peek() != "["_tok) {
        return ParseResult::error("Expected '[' for structured binding", current_token_);
    }
    advance(); // consume '['
    
    // Parse the identifier list: a, b, c
    std::vector<StringHandle> identifiers;
    
    while (true) {
        // Expect an identifier
        if (!peek().is_identifier()) {
            return ParseResult::error("Expected identifier in structured binding", current_token_);
        }
        
        Token id_token = peek_info();
        StringHandle id_handle = StringTable::createStringHandle(id_token.value());
        identifiers.push_back(id_handle);
        advance(); // consume identifier
        
        FLASH_LOG(Parser, Debug, "parse_structured_binding: Parsed identifier: ", StringTable::getStringView(id_handle));
        
        // Check for comma (more identifiers) or closing bracket
        if (peek() == ","_tok) {
            advance(); // consume ','
            // Continue to parse next identifier
        } else if (peek() == "]"_tok) {
            // End of identifier list
            break;
        } else {
            return ParseResult::error("Expected ',' or ']' in structured binding identifier list", current_token_);
        }
    }
    
    // Consume the ']'
    if (peek() != "]"_tok) {
        return ParseResult::error("Expected ']' after structured binding identifiers", current_token_);
    }
    advance(); // consume ']'
    
    FLASH_LOG(Parser, Debug, "parse_structured_binding: Parsed ", identifiers.size(), " identifiers");
    
    // Now expect the initializer: '=' expr or '{' expr '}' or '(' expr ')'
    // For C++17, we support '=' and '{}' 
    // C++20 adds '()' but let's keep it simple for now
    if (peek().is_eof()) {
        return ParseResult::error("Expected initializer after structured binding identifiers", current_token_);
    }
    
    std::optional<ASTNode> initializer;
    
    if (peek() == "="_tok) {
        advance(); // consume '='
        
        // Parse the initializer expression
        auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (init_result.is_error()) {
            return init_result;
        }
        initializer = init_result.node();
        
    } else if (peek() == "{"_tok) {
        // Brace initializer: auto [a, b] {expr};
        // For now, we'll parse this as an expression that starts with '{'
        auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (init_result.is_error()) {
            return init_result;
        }
        initializer = init_result.node();
        
    } else {
        return ParseResult::error("Expected '=' or '{' after structured binding identifiers", current_token_);
    }
    
    if (!initializer.has_value()) {
        return ParseResult::error("Failed to parse structured binding initializer", current_token_);
    }
    
    FLASH_LOG(Parser, Debug, "parse_structured_binding: Successfully parsed initializer");
    
    // Create the StructuredBindingNode
    ASTNode binding_node = emplace_node<StructuredBindingNode>(
        std::move(identifiers),
        *initializer,
        cv_qualifiers,
        ref_qualifier
    );
    
    FLASH_LOG(Parser, Debug, "parse_structured_binding: Created StructuredBindingNode");

    
    // IMPORTANT: We need to add placeholder declarations to the symbol table for each identifier
    // so that the parser can find them when they're used later in the same scope.
    // The actual types will be determined during code generation, but we need placeholders for parsing.
    const StructuredBindingNode& sb_node = binding_node.as<StructuredBindingNode>();
    for (const auto& id_handle : sb_node.identifiers()) {
        std::string_view id_name = StringTable::getStringView(id_handle);
        
        // Create a placeholder TypeSpecifierNode (we'll use Auto type as a placeholder)
        TypeSpecifierNode placeholder_type(Type::Auto, TypeQualifier::None, 0, Token());
        
        // Create a placeholder DeclarationNode
        Token placeholder_token(Token::Type::Identifier, id_name, 0, 0, 0);
        ASTNode placeholder_decl = emplace_node<DeclarationNode>(
            emplace_node<TypeSpecifierNode>(placeholder_type),
            placeholder_token
        );
        
        // Add to symbol table
        if (!gSymbolTable.insert(id_name, placeholder_decl)) {
            FLASH_LOG(Parser, Warning, "Structured binding identifier '", id_name, "' already exists in scope");
        } else {
            FLASH_LOG(Parser, Debug, "parse_structured_binding: Added placeholder for '", id_name, "' to symbol table");
        }
    }
    
    return ParseResult::success(binding_node);
}


// NEW: Parse declarators (handles function pointers, arrays, etc.)
ParseResult Parser::parse_declarator(TypeSpecifierNode& base_type, Linkage linkage) {
    // Check for parenthesized declarator: '(' '*' identifier ')'
    // This is the pattern for function pointers: int (*fp)(int, int)
    if (peek() == "("_tok) {
        advance(); // consume '('

        parse_calling_convention();

        // Expect '*' for function pointer
        if (peek() != "*"_tok) {
            return ParseResult::error("Expected '*' in function pointer declarator", current_token_);
        }
        advance(); // consume '*'

        // Parse CV-qualifiers after the * (if any)
        CVQualifier ptr_cv = parse_cv_qualifiers();
        skip_cpp_attributes();   // Handle [[...]] / __attribute__((...)) on the pointer declarator

        // Check for unnamed function pointer parameter: type (*)(params)
        // In this case, after '*' we immediately see ')' instead of an identifier
        if (peek() == ")"_tok) {
            // This is an unnamed function pointer parameter
            advance(); // consume ')'
            
            // Now parse the function parameters: '(' params ')'
            // Create a dummy identifier token for the unnamed parameter
            Token dummy_identifier(Token::Type::Identifier, ""sv, 0, 0, 0);
            
            return parse_postfix_declarator(base_type, dummy_identifier);
        }

        // Parse identifier
        if (!peek().is_identifier()) {
            return ParseResult::error("Expected identifier in function pointer declarator", current_token_);
        }
        Token identifier_token = peek_info();
        advance();

        // Check what comes after the identifier:
        // Case 1: ')' followed by '(' -> function pointer variable: int (*fp)(params)
        // Case 2: '(' -> function returning pointer: int (*func(params))[array_size] or int (*func(params))
        if (peek() == "("_tok) {
            // Case 2: This is a function returning pointer (or pointer-to-array)
            // Pattern: type (*func_name(params))[array_size] or type (*func_name(params))
            // Parse function parameters using unified parse_parameter_list (Phase 1)
            FlashCpp::ParsedParameterList params;
            auto param_result = parse_parameter_list(params);
            if (param_result.is_error()) {
                return param_result;
            }

            // Now expect closing ')' for the (*func(...)) part
            if (!consume(")"_tok)) {
                return ParseResult::error("Expected ')' after function declarator", current_token_);
            }

            // Check for array declarator: '[' size ']' after the function declarator
            // Pattern: type (*func(params))[array_size]
            std::optional<ASTNode> array_size_expr;
            if (peek() == "["_tok) {
                advance(); // consume '['

                // Parse array size expression
                auto size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                if (size_result.is_error()) {
                    return size_result;
                }
                array_size_expr = size_result.node();

                if (!consume("]"_tok)) {
                    return ParseResult::error("Expected ']' after array size", current_token_);
                }

                // The return type is: base_type (*)[array_size] = pointer to array of base_type
                // Set the base_type to indicate it's a pointer to array
                base_type.add_pointer_level(ptr_cv);
                base_type.set_array(true);
            } else {
                // The return type is: base_type (*) = pointer to base_type
                base_type.add_pointer_level(ptr_cv);
            }

            // Create declaration node for the function with the computed return type
            auto decl_node = emplace_node<DeclarationNode>(
                emplace_node<TypeSpecifierNode>(base_type),
                identifier_token,
                array_size_expr
            );

            // Create function declaration node
            auto func_decl_node = emplace_node<FunctionDeclarationNode>(
                decl_node.as<DeclarationNode>()
            );

            // Add parameters
            FunctionDeclarationNode& func_ref = func_decl_node.as<FunctionDeclarationNode>();
            for (const auto& param : params.parameters) {
                func_ref.add_parameter_node(param);
            }
            func_ref.set_is_variadic(params.is_variadic);

            return ParseResult::success(func_decl_node);
        }

        // Case 1: Expect closing ')' for function pointer variable pattern
        if (peek() != ")"_tok) {
            return ParseResult::error("Expected ')' after function pointer identifier", current_token_);
        }
        advance(); // consume ')'

        // Now parse the function parameters: '(' params ')'
        return parse_postfix_declarator(base_type, identifier_token);
    }

    // Handle pointer prefix: * [const] [volatile] *...
    while (peek() == "*"_tok) {
        advance(); // consume '*'

        // Parse CV-qualifiers after the *
        CVQualifier ptr_cv = parse_cv_qualifiers();

        base_type.add_pointer_level(ptr_cv);
    }

    // Parse direct declarator (identifier, function, array)
    Token identifier_token;
    return parse_direct_declarator(base_type, identifier_token, linkage);
}

// NEW: Parse direct declarator (identifier, function, array)
ParseResult Parser::parse_direct_declarator(TypeSpecifierNode& base_type,
                                             Token& out_identifier,
                                             [[maybe_unused]] Linkage linkage) {
    // For now, we'll handle the simple case: identifier followed by optional function params
    // TODO: Handle parenthesized declarators like (*fp)(params) for function pointers

    // Parse identifier
    if (!peek().is_identifier()) {
        return ParseResult::error("Expected identifier in declarator",
                                 current_token_);
    }

    out_identifier = peek_info();
    advance();

    // Parse postfix operators (function, array)
    return parse_postfix_declarator(base_type, out_identifier);
}

// NEW: Parse postfix declarators (function, array)
ParseResult Parser::parse_postfix_declarator(TypeSpecifierNode& base_type,
                                              const Token& identifier) {
    // Check for function declarator: '(' params ')'
    if (peek() == "("_tok) {
        advance(); // consume '('

        // Parse parameter list
        std::vector<Type> param_types;

        if (peek() != ")"_tok) {
            while (true) {
                // Parse parameter type
                auto param_type_result = parse_type_specifier();
                if (param_type_result.is_error()) {
                    return param_type_result;
                }

                TypeSpecifierNode& param_type =
                    param_type_result.node()->as<TypeSpecifierNode>();
                
                // Parse pointer declarators: * [const] [volatile] *...
                // Example: void* or const int* const*
                while (peek() == "*"_tok) {
                    advance(); // consume '*'
                    
                    // Check for CV-qualifiers after the *
                    CVQualifier ptr_cv = parse_cv_qualifiers();
                    
                    param_type.add_pointer_level(ptr_cv);
                }
                
                param_types.push_back(param_type.type());

                // Check for pack expansion '...' after the type (e.g., Args...)
                // This handles function pointer parameters like void (*)(Args...)
                if (peek() == "..."_tok) {
                    advance(); // consume '...'
                    // The pack expansion will be resolved during template instantiation
                    // For now, we just consume the ... token to allow parsing to continue
                    // Mark the function signature as having a pack expansion
                    param_type.set_pack_expansion(true);
                    
                    // Check for additional '...' for C-style variadic after pack expansion
                    // Pattern: Args...... (6 dots = pack expansion + C variadic)
                    if (peek() == "..."_tok) {
                        advance(); // consume second '...'
                        // This marks the function as C-style variadic as well
                    }
                }

                // Optional parameter name (we can ignore it for function pointers)
                if (peek().is_identifier()) {
                    advance();
                }

                // Check for comma or closing paren
                if (peek() == ","_tok) {
                    advance();
                } else {
                    break;
                }
            }
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after function parameters",
                                     current_token_);
        }

        // Check for noexcept specifier on function pointer type
        // Pattern: void (*)(Args...) noexcept or void (*)(Args...) noexcept(expr)
        skip_noexcept_specifier();

        // This is a function pointer!
        // The base_type is the return type
        // We need to create a function pointer type

        Type return_type = base_type.type();

        // Create a new TypeSpecifierNode for the function pointer
        // Function pointers are 64 bits (8 bytes) on x64
        TypeSpecifierNode fp_type(Type::FunctionPointer, TypeQualifier::None, 64);

        FunctionSignature sig;
        sig.return_type = return_type;
        sig.parameter_types = param_types;
        sig.linkage = Linkage::None;  // TODO: Use the linkage parameter
        fp_type.set_function_signature(sig);

        // Replace base_type with the function pointer type
        base_type = fp_type;
    }

    // Check for array declarator: '[' size ']'
    // TODO: Implement array support

    // Create and return declaration node
    return ParseResult::success(
        emplace_node<DeclarationNode>(
            emplace_node<TypeSpecifierNode>(base_type),
            identifier
        )
    );
}

// Phase 1 Consolidation: Parse declaration specifiers shared between
// parse_declaration_or_function_definition() and parse_variable_declaration()
// Handles: attributes ([[nodiscard]], __declspec), storage class (static, extern),
// constexpr/constinit/consteval, inline specifiers
FlashCpp::DeclarationSpecifiers Parser::parse_declaration_specifiers()
{
	FlashCpp::DeclarationSpecifiers specs;
	
	// Parse any attributes before the declaration ([[nodiscard]], __declspec(dllimport), __cdecl, etc.)
	AttributeInfo attr_info = parse_attributes();
	specs.linkage = attr_info.linkage;
	specs.calling_convention = attr_info.calling_convention;
	
	// Parse storage class specifiers and constexpr/constinit/consteval keywords
	// These can appear in any order: "static constexpr", "constexpr static", etc.
	bool done = false;
	while (!done && peek().is_keyword()) {
		std::string_view kw = peek_info().value();
		if (kw == "constexpr") {
			specs.is_constexpr = true;
			advance();
		} else if (kw == "constinit") {
			specs.is_constinit = true;
			advance();
		} else if (kw == "consteval") {
			specs.is_consteval = true;
			advance();
		} else if (kw == "inline" || kw == "__inline" || kw == "__forceinline") {
			specs.is_inline = true;
			advance();
		} else if (kw == "static") {
			specs.storage_class = StorageClass::Static;
			advance();
		} else if (kw == "extern") {
			specs.storage_class = StorageClass::Extern;
			advance();
		} else if (kw == "register") {
			specs.storage_class = StorageClass::Register;
			advance();
		} else if (kw == "mutable") {
			specs.storage_class = StorageClass::Mutable;
			advance();
		} else {
			done = true;
		}
	}
	
	// Also skip any GCC attributes that might appear after storage class specifiers
	skip_gcc_attributes();
	
	// Apply last calling convention if none was explicitly specified
	if (specs.calling_convention == CallingConvention::Default && 
	    last_calling_convention_ != CallingConvention::Default) {
		specs.calling_convention = last_calling_convention_;
	}
	
	return specs;
}

// Phase 2 Consolidation: Lookahead to detect if '(' starts function parameters
// vs direct initialization (e.g., `int x(10)` vs `int func(int y)`)
// 
// Returns true if current position is at '(' and it looks like function parameters:
// - `int x()` - empty = function (prefer function over value-init variable)
// - `int x(int y)` - starts with type = function params
// - `int x(10)` - starts with literal/expression = direct init (return false)
// - `int x(a)` where `a` is a type = function params (return true)
// - `int x(a)` where `a` is a variable = direct init (return false)
//
// This uses lookahead without consuming tokens.
bool Parser::looks_like_function_parameters()
{
	// Must be at '('
	if (peek() != "("_tok) {
		return false;
	}
	
	// Save current position for restoration
	SaveHandle saved = save_token_position();
	
	advance();  // consume '('
	
	// Empty parens: `int x()` - prefer function
	if (peek() == ")"_tok) {
		restore_token_position(saved);
		return true;
	}
	
	// Look at what's after '('
	// If it starts with a type keyword, it's likely function parameters
	// If it starts with a literal or identifier that's not a type, it's likely direct init
	
	if (!peek().is_eof()) {
		Token::Type token_type = peek_info().type();
		std::string_view token_value = peek_info().value();
		
		// Literals (numbers, strings) = direct initialization
		if (token_type == Token::Type::Literal) {
			restore_token_position(saved);
			return false;
		}
		
		// Type keywords = function parameters
		static const std::unordered_set<std::string_view> param_type_keywords = {
			"int", "float", "double", "char", "bool", "void",
			"short", "long", "signed", "unsigned", "const", "volatile",
			"auto", "decltype", "struct", "class", "enum", "union",
			"wchar_t", "char8_t", "char16_t", "char32_t",
			"__int8", "__int16", "__int32", "__int64"
		};
		
		if (token_type == Token::Type::Keyword && param_type_keywords.count(token_value)) {
			restore_token_position(saved);
			return true;
		}
		
		// For identifiers, we need to check if it's a known type
		if (token_type == Token::Type::Identifier) {
			StringHandle id_handle = StringTable::getOrInternStringHandle(token_value);
			
			// Check if this identifier is a known type in the type registry
			auto type_iter = gTypesByName.find(id_handle);
			if (type_iter != gTypesByName.end()) {
				// It's a known type = function parameters
				restore_token_position(saved);
				return true;
			}
			
			// Check if it's in current scope as a variable
			auto symbol_lookup = gSymbolTable.lookup(token_value);
			if (symbol_lookup.has_value()) {
				// It's a variable = direct initialization
				restore_token_position(saved);
				return false;
			}
			
			// Unknown identifier - check if next token gives us more context
			// e.g., `int x(MyType y)` where 'y' is identifier = function param
			// e.g., `int x(a + b)` where '+' follows = expression = direct init
			advance();  // consume the identifier
			
			if (!peek().is_eof()) {
				std::string_view next_val = peek_info().value();
				// If followed by an identifier, it's likely `type name` = function param
				if (peek().is_identifier()) {
					restore_token_position(saved);
					return true;
				}
				// If followed by `)` or `,` it could be either - check if identifier is uppercase (likely type)
				if (next_val == ")" || next_val == ",") {
					// Heuristic: if the identifier starts with uppercase, assume it's a type
					// This handles common cases like `int x(MyClass)`
					if (!token_value.empty() && std::isupper(static_cast<unsigned char>(token_value[0]))) {
						restore_token_position(saved);
						return true;
					}
				}
				// If followed by operators (+, -, *, etc.) = expression = direct init
				if (peek_info().type() == Token::Type::Operator) {
					restore_token_position(saved);
					return false;
				}
				// If followed by pointer/reference declarators (* or &) = function param with pointer type
				if (next_val == "*" || next_val == "&") {
					restore_token_position(saved);
					return true;
				}
			}
			
			restore_token_position(saved);
			// Default to false for unknown identifiers (direct initialization)
			return false;
		}
		
		// Pointer/reference operators at start
		// Could be function with complex return type, OR dereference expression like *this
		if (token_value == "*" || token_value == "&") {
			// Peek ahead to see what follows the operator
			advance();  // consume the '*' or '&'
			
			if (!peek().is_eof()) {
				std::string_view after_op = peek_info().value();
				Token::Type after_op_type = peek_info().type();
				
				// If followed by 'this', it's *this or &this = direct initialization
				if (after_op == "this") {
					restore_token_position(saved);
					return false;
				}
				
				// If followed by a variable (identifier in symbol table), it's *var or &var = expression
				if (after_op_type == Token::Type::Identifier) {
					auto symbol_lookup = gSymbolTable.lookup(after_op);
					if (symbol_lookup.has_value()) {
						restore_token_position(saved);
						return false;
					}
				}
				
				// If followed by a literal, it's an expression (though unusual)
				if (after_op_type == Token::Type::Literal) {
					restore_token_position(saved);
					return false;
				}
				
				// If followed by an open paren, it could be (*expr) = expression
				if (after_op == "(") {
					restore_token_position(saved);
					return false;
				}
			}
			
			restore_token_position(saved);
			// Otherwise, assume function with pointer parameter
			return true;
		}
	}
	
	restore_token_position(saved);
	// Default: unknown, assume not function params
	return false;
}

// Phase 4: Unified declaration parsing
// This is the single entry point for parsing all declarations (variables and functions)
// It delegates to the appropriate specialized parsing function based on context
ParseResult Parser::parse_declaration(FlashCpp::DeclarationContext context)
{
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration: Starting, context={}, current token: {}", 
		static_cast<int>(context),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	
	// Determine actual context if Auto
	FlashCpp::DeclarationContext effective_context = context;
	if (context == FlashCpp::DeclarationContext::Auto) {
		// Infer from current scope type
		ScopeType current_scope = gSymbolTable.get_current_scope_type();
		switch (current_scope) {
			case ScopeType::Global:
			case ScopeType::Namespace:
				effective_context = FlashCpp::DeclarationContext::TopLevel;
				break;
			case ScopeType::Function:
			case ScopeType::Block:
				effective_context = FlashCpp::DeclarationContext::BlockScope;
				break;
			default:
				effective_context = FlashCpp::DeclarationContext::BlockScope;
				break;
		}
	}
	
	// Delegate to appropriate specialized parser based on context
	switch (effective_context) {
		case FlashCpp::DeclarationContext::TopLevel:
			// Top-level uses parse_declaration_or_function_definition
			// It handles: function definitions, global variables, out-of-line member functions
			return parse_declaration_or_function_definition();
			
		case FlashCpp::DeclarationContext::BlockScope:
		case FlashCpp::DeclarationContext::ForInit:
		case FlashCpp::DeclarationContext::IfInit:
		case FlashCpp::DeclarationContext::SwitchInit:
			// Block scope uses parse_variable_declaration
			// It handles: local variables, direct init, structured bindings, and now also
			// function declarations (delegated via looks_like_function_parameters)
			return parse_variable_declaration();
			
		case FlashCpp::DeclarationContext::ClassMember:
			// Class member declarations are handled by parse_struct_declaration
			// This shouldn't be called directly for class members
			return ParseResult::error("Class member declarations should use parse_struct_declaration", current_token_);
			
		default:
			return ParseResult::error("Unknown declaration context", current_token_);
	}
}

ParseResult Parser::parse_declaration_or_function_definition()
{
	ScopedTokenPosition saved_position(*this);
	
	FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: Starting, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	
	// Phase 1 Consolidation: Use shared specifier parsing helper
	FlashCpp::DeclarationSpecifiers specs = parse_declaration_specifiers();
	
	// Extract values for backward compatibility (will be removed in later phases)
	bool is_constexpr = specs.is_constexpr;
	bool is_constinit = specs.is_constinit;
	bool is_consteval = specs.is_consteval;
	[[maybe_unused]] bool is_inline = specs.is_inline;
	
	// Create AttributeInfo for backward compatibility with existing code paths
	AttributeInfo attr_info;
	attr_info.linkage = specs.linkage;
	attr_info.calling_convention = specs.calling_convention;

	// Check for inline/constexpr struct/class definition pattern:
	// inline constexpr struct Name { ... } variable = {};
	// This is a struct definition combined with a variable declaration
	if (peek().is_keyword() &&
	    (peek() == "struct"_tok || peek() == "class"_tok)) {
		// Delegate to struct parsing which will handle the full definition
		// and any trailing variable declarations
		// TODO: Pass specs (is_constexpr, is_inline, etc.) to parse_struct_declaration()
		// so they can be applied to trailing variable declarations after the struct body.
		// Currently, these specifiers are parsed but not propagated.
		auto result = parse_struct_declaration();
		if (!result.is_error()) {
			// Successfully parsed struct, propagate the result
			return saved_position.propagate(std::move(result));
		}
		// If struct parsing fails, fall through to normal parsing
	}

	// Check for out-of-line constructor/destructor pattern: ClassName::ClassName(...) or ClassName::~ClassName()
	// These have no return type, so we need to detect them before parse_type_and_name()
	if (peek().is_identifier()) {
		std::string_view first_id = peek_info().value();
		
		// Build fully qualified name using current namespace and buildQualifiedNameFromHandle
		NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_class_name = current_namespace_handle.isGlobal() 
			? first_id 
			: buildQualifiedNameFromHandle(current_namespace_handle, first_id);
		
		// Try to find the class, first with qualified name, then unqualified
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_class_name));
		if (type_it == gTypesByName.end()) {
			// Try unqualified name
			type_it = gTypesByName.find(StringTable::getOrInternStringHandle(first_id));
		}
		
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			// Save position to look ahead
			SaveHandle lookahead_pos = save_token_position();
			advance();  // consume first identifier
			
			// Check for :: followed by same identifier (constructor) or ~identifier (destructor)
			if (peek() == "::"_tok) {
				advance();  // consume ::
				
				bool is_destructor = false;
				if (peek() == "~"_tok) {
					is_destructor = true;
					advance();  // consume ~
				}
				
				// Check if next identifier matches the class name (constructor/destructor pattern)
				if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
				    peek_info().value() == first_id) {
					// This is an out-of-line constructor or destructor definition!
					// Restore and parse it specially
					restore_token_position(lookahead_pos);
					// Pass the qualified name so the function can find the struct
					// Use propagate() to avoid restoring position when ScopedTokenPosition destructor runs
					return saved_position.propagate(parse_out_of_line_constructor_or_destructor(qualified_class_name, is_destructor, specs));
				}
			}
			// Not a constructor/destructor pattern, restore and continue normal parsing
			restore_token_position(lookahead_pos);
		}
	}

	// Parse the type specifier and identifier (name)
	// This will also extract any calling convention that appears after the type
	FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: About to parse type_and_name, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: parse_type_and_name failed: ", type_and_name_result.error_message());
		return type_and_name_result;
	}

	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_type_and_name succeeded. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Handle structured bindings (e.g., auto [a, b] = expr;)
	// parse_type_and_name may return a StructuredBindingNode instead of a DeclarationNode
	if (type_and_name_result.node().has_value() && type_and_name_result.node()->is<StructuredBindingNode>()) {
		// Validate: structured bindings cannot have storage class specifiers
		if (specs.storage_class != StorageClass::None) {
			return ParseResult::error("Structured bindings cannot have storage class specifiers (static, extern, etc.)", current_token_);
		}
		if (is_constexpr) {
			return ParseResult::error("Structured bindings cannot be constexpr", current_token_);
		}
		if (is_constinit) {
			return ParseResult::error("Structured bindings cannot be constinit", current_token_);
		}
		return saved_position.success(type_and_name_result.node().value());
	}

	// Check for out-of-line member function definition: ClassName::functionName(...)
	// Pattern: ReturnType ClassName::functionName(...) { ... }
	DeclarationNode& decl_node = as<DeclarationNode>(type_and_name_result);
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: Got decl_node, identifier={}. About to check for '::', current_token={}, peek={}", 
		std::string(decl_node.identifier_token().value()),
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	if (peek() == "::"_tok) {
		// This is an out-of-line member function definition
		advance();  // consume '::'
		
		// The class name is in decl_node.identifier_token()
		StringHandle class_name = decl_node.identifier_token().handle();
		
		// Parse the actual function name - this can be an identifier or 'operator' keyword
		Token function_name_token;
		[[maybe_unused]] bool is_operator = false;
		
		if (peek() == "operator"_tok) {
			// Out-of-line operator definition: ClassName::operator=(...) etc.
			is_operator = true;
			function_name_token = peek_info();
			advance();  // consume 'operator'
			
			// Consume the operator symbol (=, ==, !=, <<, >>, etc.)
			if (peek().is_eof()) {
				FLASH_LOG(Parser, Error, "Expected operator symbol after 'operator'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Build the full operator name using StringBuilder
			StringBuilder operator_builder;
			operator_builder.append("operator");
			std::string_view op = peek_info().value();
			operator_builder.append(op);
			advance();
			
			// Handle multi-character operators like >>=, <<=, etc.
			while (!peek().is_eof()) {
				std::string_view next = peek_info().value();
				if (next == "=" || next == ">" || next == "<") {
					// Could be part of >>=, <<=, etc.
					if (op == ">" && (next == ">" || next == "=")) {
						operator_builder.append(next);
						advance();
						op = next;
					} else if (op == "<" && (next == "<" || next == "=")) {
						operator_builder.append(next);
						advance();
						op = next;
					} else if ((op == ">" || op == "<" || op == "!" || op == "=") && next == "=") {
						operator_builder.append(next);
						advance();
						break;
					} else {
						break;
					}
				} else {
					break;
				}
			}
			
			// Create a token with the full operator name
			std::string_view operator_symbol = operator_builder.commit();
			function_name_token = Token(Token::Type::Identifier, operator_symbol, 
				function_name_token.line(), function_name_token.column(), function_name_token.file_index());
		} else if (peek().is_identifier()) {
			function_name_token = peek_info();
			advance();
		} else {
			FLASH_LOG(Parser, Error, "Expected function name or 'operator' after '::'");
			return ParseResult::error(ParserError::UnexpectedToken, peek_info());
		}
		
		// Find the struct in the type registry
		auto struct_iter = gTypesByName.find(class_name);
		if (struct_iter == gTypesByName.end()) {
			FLASH_LOG(Parser, Error, "Unknown class '", class_name.view(), "' in out-of-line member function definition");
			return ParseResult::error(ParserError::UnexpectedToken, decl_node.identifier_token());
		}
		
		const TypeInfo* type_info = struct_iter->second;
		StructTypeInfo* struct_info = const_cast<StructTypeInfo*>(type_info->getStructInfo());
		if (!struct_info) {
			// Type alias resolution: follow type_index_ to find the actual struct type
			// e.g., using Alias = SomeStruct; then Alias::member() needs to resolve to SomeStruct
			if (type_info->type_index_ < gTypeInfo.size() && &gTypeInfo[type_info->type_index_] != type_info) {
				const TypeInfo& resolved_type = gTypeInfo[type_info->type_index_];
				struct_info = const_cast<StructTypeInfo*>(resolved_type.getStructInfo());
			}
		}
		if (!struct_info) {
			FLASH_LOG(Parser, Error, "'", class_name.view(), "' is not a struct/class type");
			return ParseResult::error(ParserError::UnexpectedToken, decl_node.identifier_token());
		}
		
		// Check if this is an out-of-line static member variable definition with parenthesized initializer
		// Pattern: inline constexpr Type ClassName::member_name(initializer);
		// This must be checked BEFORE assuming it's a function definition
		StringHandle member_name_handle = function_name_token.handle();
		const StructStaticMember* static_member = struct_info->findStaticMember(member_name_handle);
		if (static_member != nullptr && peek() == "("_tok) {
			// This is a static member variable definition with parenthesized initializer
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '('
			
			// Parse the initializer expression
			auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (init_result.is_error() || !init_result.node().has_value()) {
				FLASH_LOG(Parser, Error, "Failed to parse initializer for static member variable '",
				          class_name.view(), "::", function_name_token.value(), "'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Expect closing parenthesis
			if (!consume(")"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ')' after static member variable initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable definition");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			return finalize_static_member_init(static_member, *init_result.node(), decl_node, function_name_token, saved_position);
		}
		
		// Check if this is an out-of-line static member variable definition with brace initializer
		// Pattern: inline Type ClassName::member_name{};  or  ClassName::member_name{value};
		if (static_member != nullptr && peek() == "{"_tok) {
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition with brace init: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '{'
			
			// Parse the initializer expression if present (empty braces = default init)
			std::optional<ASTNode> init_expr;
			if (peek() != "}"_tok) {
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (init_result.is_error() || !init_result.node().has_value()) {
					FLASH_LOG(Parser, Error, "Failed to parse brace initializer for static member variable '",
					          class_name.view(), "::", function_name_token.value(), "'");
					return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
				}
				init_expr = *init_result.node();
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				FLASH_LOG(Parser, Error, "Expected '}' after static member variable brace initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable brace initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Finalize the static member initializer (handling empty brace-init) and return the variable node
			return finalize_static_member_init(static_member, init_expr, decl_node, function_name_token, saved_position);
		}
		
		// Check if this is an out-of-line static member variable definition with copy initializer
		// Pattern: Type ClassName::member_name = expr;
		if (static_member != nullptr && peek() == "="_tok) {
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition with = init: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '='
			
			// Parse the initializer expression
			auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (init_result.is_error() || !init_result.node().has_value()) {
				FLASH_LOG(Parser, Error, "Failed to parse initializer for static member variable '",
				          class_name.view(), "::", function_name_token.value(), "'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable definition");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			return finalize_static_member_init(static_member, *init_result.node(), decl_node, function_name_token, saved_position);
		}
		
		// Create a new declaration node with the function name
		ASTNode return_type_node = decl_node.type_node();
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, function_name_token);
		
		// Create the FunctionDeclarationNode with parent struct name (marks it as member function)
		auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref, class_name);
		
		// Parse the function parameters using unified parameter list parsing (Phase 1)
		FlashCpp::ParsedParameterList params;
		auto param_result = parse_parameter_list(params, attr_info.calling_convention);
		if (param_result.is_error()) {
			FLASH_LOG(Parser, Error, "Error parsing parameter list");
			return param_result;
		}

		// Skip optional qualifiers after parameter list using existing helper
		// Note: skip_function_trailing_specifiers() doesn't skip override/final as they have semantic meaning
		// For out-of-line definitions, we also skip override/final as they're already recorded in the declaration
		FlashCpp::MemberQualifiers member_quals;
		skip_function_trailing_specifiers(member_quals);
		
		// Also skip override/final for out-of-line definitions
		while (!peek().is_eof()) {
			auto next_val = peek_info().value();
			if (next_val == "override" || next_val == "final") {
				advance();
			} else {
				break;
			}
		}
		
		// Skip trailing requires clause for out-of-line definitions
		// (the constraint was already recorded during the in-class declaration)
		skip_trailing_requires_clause();

		// Apply parsed parameters to the function
		for (const auto& param : params.parameters) {
			func_ref.add_parameter_node(param);
		}
		func_ref.set_is_variadic(params.is_variadic);
		
		// Apply attributes
		func_ref.set_calling_convention(attr_info.calling_convention);
		if (attr_info.linkage == Linkage::DllImport || attr_info.linkage == Linkage::DllExport) {
			func_ref.set_linkage(attr_info.linkage);
		}
		func_ref.set_is_constexpr(is_constexpr);
		func_ref.set_is_constinit(is_constinit);
		func_ref.set_is_consteval(is_consteval);
		
		// Search for existing member function declaration with the same name and const qualification
		StructMemberFunction* existing_member = nullptr;
		for (auto& member : struct_info->member_functions) {
			if (member.getName() == function_name_token.handle() &&
				member.is_const == member_quals.is_const &&
				member.is_volatile == member_quals.is_volatile) {
				existing_member = &member;
				break;
			}
		}
		
		if (!existing_member) {
			FLASH_LOG(Parser, Error, "Out-of-line definition of '", class_name.view(), "::", function_name_token.value(), 
			          "' does not match any declaration in the class");
			return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
		}
		
		// Validate that the existing declaration is a FunctionDeclarationNode
		if (!existing_member->function_decl.is<FunctionDeclarationNode>()) {
			FLASH_LOG(Parser, Error, "Member '", function_name_token.value(), "' is not a function");
			return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
		}
	
		FunctionDeclarationNode& existing_func_ref = const_cast<FunctionDeclarationNode&>(
			existing_member->function_decl.as<FunctionDeclarationNode>());
	
		// Phase 7: Use unified signature validation
		auto validation_result = validate_signature_match(existing_func_ref, func_ref);
		if (!validation_result.is_match()) {
			FLASH_LOG(Parser, Error, validation_result.error_message, " in out-of-line definition of '", 
					  class_name.view(), "::", function_name_token.value(), "'");
			return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
		}
		
		// Check for declaration only (;) or function definition ({)
		if (consume(";"_tok)) {
			// Declaration only
			return saved_position.success(func_node);
		}
		
		// Parse function body
		if (peek() != "{"_tok) {
			FLASH_LOG(Parser, Error, "Expected '{' or ';' after function declaration, got: '",
				(!peek().is_eof() ? std::string(peek_info().value()) : "<EOF>"), "'");
			return ParseResult::error(ParserError::UnexpectedToken, peek_info());
		}
		
		// Enter function scope with RAII guard (Phase 3)
		FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
		
		// Push member function context so that member variables are resolved correctly
		member_function_context_stack_.push_back({
			class_name,
			type_info->type_index_,
			nullptr,  // struct_node - we don't have access to it here, but struct_type_index should be enough
			nullptr   // local_struct_info - not needed here since TypeInfo is already populated
		});
		
		// Add 'this' pointer to symbol table
		auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
			Type::Struct, type_info->type_index_, 
			static_cast<int>(struct_info->total_size * 8), Token()
		);
		this_type_ref.add_pointer_level();  // Make it a pointer
		
		Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
		auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
		gSymbolTable.insert("this"sv, this_decl_node);
		
		// Add function parameters to symbol table using the DEFINITION's parameter names
		// (not the declaration's names - C++ allows different names between declaration and definition)
		// The types have already been validated to match via validate_signature_match()
		for (const ASTNode& param_node : func_ref.parameter_nodes()) {
			if (param_node.is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
				const DeclarationNode& param_decl = var_decl.declaration();
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			} else if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		}
		
		// Parse function body
		ParseResult body_result = parse_block();
		
		if (body_result.is_error()) {
			member_function_context_stack_.pop_back();
			// func_scope automatically exits scope on return
			return body_result;
		}
	
		// existing_func_ref is already defined earlier after validation
		if (body_result.node().has_value()) {
			// Generate mangled name before setting definition (Phase 6 mangling)
			compute_and_set_mangled_name(existing_func_ref);
			if (!existing_func_ref.set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Function '", class_name.view(), "::", function_name_token.value(), 
						  "' already has a definition");
				member_function_context_stack_.pop_back();
				// func_scope automatically exits scope on return
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			// Update parameter nodes to use definition's parameter names
			// C++ allows declaration and definition to have different parameter names
			existing_func_ref.update_parameter_nodes_from_definition(func_ref.parameter_nodes());
			// Deduce auto return types from function body
			deduce_and_update_auto_return_type(existing_func_ref);
		}

		member_function_context_stack_.pop_back();
		// func_scope automatically exits scope at end of block
	
		// Return success without a node - the existing declaration already has the definition attached
		// Don't return the node because it's already in the AST from the struct declaration
		return saved_position.success();
	}

	// First, try to parse as a function definition
	// Save position before attempting function parse so we can backtrack if it's actually a variable
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to try parse_function_declaration. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	SaveHandle before_function_parse = save_token_position();
	ParseResult function_definition_result = parse_function_declaration(decl_node, attr_info.calling_convention);
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_function_declaration returned. is_error={}, current_token={}, peek={}", 
		function_definition_result.is_error(),
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	if (!function_definition_result.is_error()) {
		// Successfully parsed as function - discard saved position
		discard_saved_token(before_function_parse);
		// It was successfully parsed as a function definition
		// Apply attribute linkage if present (calling convention already set in parse_function_declaration)
		if (auto func_node_ptr = function_definition_result.node()) {
			FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
			if (attr_info.linkage == Linkage::DllImport || attr_info.linkage == Linkage::DllExport) {
				func_node.set_linkage(attr_info.linkage);
			}
			func_node.set_is_constexpr(is_constexpr);
			func_node.set_is_constinit(is_constinit);
			func_node.set_is_consteval(is_consteval);
		}
		
		// Continue with function-specific logic
		TypeSpecifierNode& type_specifier = decl_node.type_node().as<TypeSpecifierNode>();
		
		// Parse trailing specifiers using Phase 2 unified method (instead of just skipping them)
		// For free functions: noexcept is applied, const/volatile/&/&&/override/final are ignored
		FlashCpp::MemberQualifiers member_quals;
		FlashCpp::FunctionSpecifiers func_specs;
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to parse_function_trailing_specifiers. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_function_trailing_specifiers returned. is_error={}, current_token={}, peek={}", 
			specs_result.is_error(),
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		if (specs_result.is_error()) {
			return specs_result;
		}
		
		// Apply noexcept specifier to free functions
		if (func_specs.is_noexcept) {
			if (auto func_node_ptr = function_definition_result.node()) {
				FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
				func_node.set_noexcept(true);
				if (func_specs.noexcept_expr.has_value()) {
					func_node.set_noexcept_expression(*func_specs.noexcept_expr);
				}
			}
		}
		
		if (type_specifier.type() == Type::Auto) {
			const bool is_trailing_return_type = (peek() == "->"_tok);
			if (is_trailing_return_type) {
				advance();

				ParseResult trailing_type_specifier = parse_type_specifier();
				if (trailing_type_specifier.is_error())
					return trailing_type_specifier;

				// Apply pointer and reference qualifiers (ptr-operator in C++20 grammar)
				if (trailing_type_specifier.node().has_value() && 
				    trailing_type_specifier.node()->is<TypeSpecifierNode>()) {
					TypeSpecifierNode& trailing_ts = trailing_type_specifier.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(trailing_ts);
				}

				type_specifier = as<TypeSpecifierNode>(trailing_type_specifier);
			}
		}

		const Token& identifier_token = decl_node.identifier_token();
		StringHandle func_name = identifier_token.handle();
		
		// C++20 Abbreviated Function Templates: Check if any parameter has auto type
		// If so, convert this function to an implicit function template
		if (auto func_node_ptr = function_definition_result.node()) {
			FunctionDeclarationNode& func_decl = func_node_ptr->as<FunctionDeclarationNode>();
			
			// Count auto parameters and collect their info including concept constraints
			struct AutoParamInfo {
				size_t index;
				Token token;
				std::string_view concept_name;  // Empty if unconstrained
			};
			std::vector<AutoParamInfo> auto_params;
			const auto& params = func_decl.parameter_nodes();
			for (size_t i = 0; i < params.size(); ++i) {
				if (params[i].is<DeclarationNode>()) {
					const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					if (param_type.type() == Type::Auto) {
						std::string_view concept_constraint = param_type.has_concept_constraint() ? param_type.concept_constraint() : std::string_view{};
						auto_params.push_back({i, param_decl.identifier_token(), concept_constraint});
					}
				}
			}
			
			// If we have auto parameters, convert to abbreviated function template
			if (!auto_params.empty()) {
				// Create synthetic template parameters for each auto parameter
				// Each auto becomes a unique template type parameter: _T0, _T1, etc.
				std::vector<ASTNode> template_params;
				std::vector<StringHandle> template_param_names;
				
				for (size_t i = 0; i < auto_params.size(); ++i) {
					// Generate synthetic parameter name like "_T0", "_T1", etc.
					// Using underscore prefix to avoid conflicts with user-defined names
					// StringBuilder.commit() returns a persistent string_view
					StringHandle param_name = StringTable::getOrInternStringHandle(StringBuilder().append("_T"sv).append(static_cast<int64_t>(i)));
					
					// Use the auto parameter's token for position/error reporting
					Token param_token = auto_params[i].token;
					
					// Create a type template parameter node
					auto param_node = emplace_node<TemplateParameterNode>(param_name, param_token);
					
					// Set concept constraint if the auto parameter was constrained (e.g., IsInt auto x)
					if (!auto_params[i].concept_name.empty()) {
						param_node.as<TemplateParameterNode>().set_concept_constraint(auto_params[i].concept_name);
					}
					
					template_params.push_back(param_node);
					template_param_names.push_back(param_name);
				}
				
				// Create the TemplateFunctionDeclarationNode wrapping the function
				auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
					std::move(template_params),
					*func_node_ptr,
					std::nullopt  // No requires clause for abbreviated templates
				);
				
				// Register the template in the template registry
				gTemplateRegistry.registerTemplate(func_name, template_func_node);
				
				// Also register the template parameter names for lookup
				gTemplateRegistry.registerTemplateParameters(func_name, template_param_names);
				
				// Add the template function to the symbol table
				gSymbolTable.insert(func_name, template_func_node);
				
				// Set template param names for parsing body (for template parameter recognition)
				current_template_param_names_ = template_param_names;
				
				// Check if this is just a declaration (no body)
				if (peek() == ";"_tok) {
					advance();  // consume ';'
					current_template_param_names_.clear();
					return saved_position.success(template_func_node);
				}
				
				// Has a body - save position at the '{' for delayed parsing during instantiation
				// Note: set_template_body_position is on FunctionDeclarationNode, which is the
				// underlying node inside TemplateFunctionDeclarationNode - this is consistent
				// with how regular template functions store their body position
				if (peek() == "{"_tok) {
					SaveHandle body_start = save_token_position();
					func_decl.set_template_body_position(body_start);
					skip_balanced_braces();
				}
				
				current_template_param_names_.clear();
				return saved_position.success(template_func_node);
			}
		}
		
		// Insert the FunctionDeclarationNode (which contains parameter info for overload resolution)
		// instead of just the DeclarationNode
		if (auto func_node = function_definition_result.node()) {
			if (!gSymbolTable.insert(func_name, *func_node)) {
				// Note: With overloading support, insert() now allows multiple functions with same name
				// It only returns false for non-function duplicate symbols
				return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
			}
		}

		// Is only function declaration
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: Checking for ';' vs function body. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		if (consume(";"_tok)) {
			// Return the function declaration node (needed for templates)
			if (auto func_node = function_definition_result.node()) {
				return saved_position.success(*func_node);
			}
			return saved_position.success();
		}

		// Add function parameters to the symbol table within a function scope (Phase 3: RAII)
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to parse function body. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		FlashCpp::SymbolTableScope func_scope(ScopeType::Function);

		// Set current function pointer for __func__, __PRETTY_FUNCTION__
		// The FunctionDeclarationNode persists in the AST, so the pointer is safe
		if (auto funcNode = function_definition_result.node()) {
			const auto& func_decl = funcNode->as<FunctionDeclarationNode>();
			current_function_ = &func_decl;

			for (const auto& param : func_decl.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl_node = param.as<DeclarationNode>();
					const Token& param_token = param_decl_node.identifier_token();
					gSymbolTable.insert(param_token.value(), param);
				}
			}

			// Note: trailing specifiers were already skipped after parse_function_declaration()

			// Parse function body
			FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to call parse_block. current_token={}, peek={}", 
				std::string(current_token_.value()),
				!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				// func_scope automatically exits scope
				return block_result;
			}

			current_function_ = nullptr;
			// func_scope automatically exits scope

			if (auto node = function_definition_result.node()) {
				if (auto block = block_result.node()) {
					FunctionDeclarationNode& final_func_decl = node->as<FunctionDeclarationNode>();
					// Generate mangled name before finalizing (Phase 6 mangling)
					compute_and_set_mangled_name(final_func_decl);
					final_func_decl.set_definition(*block);
					// Deduce auto return types from function body
					deduce_and_update_auto_return_type(final_func_decl);
					return saved_position.success(*node);
				}
			}
			// If we get here, something went wrong but we should still commit
			// because we successfully parsed a function
			return saved_position.success();
		}
	} else {
		// Function parsing failed - restore position to try variable declaration
		restore_token_position(before_function_parse);
		
		// If the error is a semantic error (not a syntax error about expecting '('),
		// return it directly instead of trying variable declaration parsing
		std::string error_msg = function_definition_result.error_message();
		if (error_msg.find("Variadic") != std::string::npos ||
		    error_msg.find("calling convention") != std::string::npos) {
			return function_definition_result;
		}
		
		// Otherwise, try parsing as a variable declaration
		// Attempt to parse a simple declaration (global variable or typedef)
		// Check for initialization
		std::optional<ASTNode> initializer;
		
		// Get the type specifier for brace initializer parsing and constexpr checks
		// This is always safe since decl_node is a DeclarationNode with a TypeSpecifierNode
		TypeSpecifierNode& type_specifier = decl_node.type_node().as<TypeSpecifierNode>();
		
		// Phase 3 Consolidation: Use shared copy initialization helper for = and = {} forms
		if (peek() == "="_tok) {
			auto init_result = parse_copy_initialization(decl_node, type_specifier);
			if (init_result.has_value()) {
				initializer = init_result;
			} else {
				return ParseResult::error("Failed to parse initializer expression", current_token_);
			}
		} else if (peek() == "{"_tok) {
			// Direct list initialization: Type var{args}
			ParseResult init_list_result = parse_brace_initializer(type_specifier);
			if (init_list_result.is_error()) {
				return init_list_result;
			}
			initializer = init_list_result.node();
		} else if (peek() == "("_tok) {
			// Direct initialization: Type var(args)
			// At global scope with struct types, use ConstructorCallNode for constexpr evaluation.
			// At block scope, use InitializerListNode consistent with parse_variable_declaration.
			bool is_global_scope = (gSymbolTable.get_current_scope_type() == ScopeType::Global);
			if (is_global_scope && type_specifier.type() == Type::Struct) {
				Token paren_token = peek_info();
				advance(); // consume '('
				ChunkedVector<ASTNode> arguments;
				while (!peek().is_eof() && peek() != ")"_tok) {
					auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						return arg_result;
					}
					if (auto arg_node = arg_result.node()) {
						arguments.push_back(*arg_node);
					}
					if (peek() == ","_tok) {
						advance();
					} else {
						break;
					}
				}
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after constructor arguments", current_token_);
				}
				ASTNode type_node_copy = decl_node.type_node();
				initializer = ASTNode::emplace_node<ConstructorCallNode>(
					type_node_copy, std::move(arguments), paren_token);
			} else {
				auto init_result = parse_direct_initialization();
				if (init_result.has_value()) {
					initializer = init_result;
				} else {
					return ParseResult::error("Expected ')' after direct initialization arguments", current_token_);
				}
			}
		}

		// Create a variable declaration node for the first variable
		// Reuse the existing decl_node from type_and_name_result
		auto [global_var_node, global_decl_node] = emplace_node_ref<VariableDeclarationNode>(
			type_and_name_result.node().value(),  // Use the existing DeclarationNode
			initializer,
			specs.storage_class
		);
		global_decl_node.set_is_constexpr(is_constexpr);
		global_decl_node.set_is_constinit(is_constinit);

		// Get identifier token for error reporting
		const Token& identifier_token = decl_node.identifier_token();

		// Semantic checks for constexpr/constinit - only enforce for global/static variables
		// For local constexpr variables, they can fall back to runtime initialization (like const)
		bool is_global_scope = (gSymbolTable.get_current_scope_type() == ScopeType::Global);
		
		if ((is_constexpr || is_constinit) && is_global_scope) {
			const char* keyword_name = is_constexpr ? "constexpr" : "constinit";
			
			// Both constexpr and constinit require an initializer
			if (!initializer.has_value()) {
				return ParseResult::error(
					std::string(keyword_name) + " variable must have an initializer", 
					identifier_token
				);
			}
			
			// Skip constexpr evaluation for struct types with initializer lists
			// The constexpr evaluator doesn't currently support InitializerListNode
			// Also skip for expressions that contain casts or other unsupported operations
			// TODO: Implement full constexpr evaluation
			bool is_struct_init_list = (type_specifier.type() == Type::Struct && 
			                            initializer->is<InitializerListNode>());
			
			if (!is_struct_init_list) {
				// Evaluate the initializer to ensure it's a constant expression
				ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
				eval_ctx.storage_duration = ConstExpr::StorageDuration::Global;
				eval_ctx.is_constinit = is_constinit;
				
				auto eval_result = ConstExpr::Evaluator::evaluate(initializer.value(), eval_ctx);
				// C++ semantics distinction between constexpr and constinit:
				// - constexpr: variable CAN be used in constant expressions if initialized with a 
				//   constant expression, but it's not required at parse time. If evaluation fails,
				//   the variable is treated as a regular const variable.
				// - constinit: variable MUST be initialized with a constant expression (C++20).
				//   Failure to evaluate at compile time is always an error.
				if (!eval_result.success() && is_constinit) {
					return ParseResult::error(
						std::string(keyword_name) + " variable initializer must be a constant expression: " + eval_result.error_message,
						identifier_token
					);
				}
			}
			
			// Note: The evaluated value could be stored in the VariableDeclarationNode for later use
			// For now, we just validate that it can be evaluated
		}
		
		// For local constexpr variables, treat them like const - no validation, just runtime initialization
		// This follows C++ standard: constexpr means "can be used in constant expressions"
		// but doesn't require compile-time evaluation for local variables


		// Add to symbol table
		if (!gSymbolTable.insert(identifier_token.value(), global_var_node)) {
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
		}

		// Handle comma-separated declarations (e.g., int x, y, z;)
		// When there are additional variables, collect them all in a BlockNode
		if (peek() == ","_tok) {
			// Create a block to hold all declarations
			auto [block_node, block_ref] = emplace_node_ref<BlockNode>();
			
			// Add the first declaration to the block
			block_ref.add_statement_node(global_var_node);
			
			while (peek() == ","_tok) {
				advance(); // consume ','

				// Parse the next variable name
				auto next_identifier_token = advance();
				if (!next_identifier_token.kind().is_identifier()) {
					return ParseResult::error("Expected identifier after comma in declaration list", current_token_);
				}

				// Create a new DeclarationNode with the same type
				DeclarationNode& next_decl = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(type_specifier),
					next_identifier_token
				).as<DeclarationNode>();
				TypeSpecifierNode& next_type_spec = next_decl.type_node().as<TypeSpecifierNode>();

				// Phase 3 Consolidation: Use shared copy initialization helper
				std::optional<ASTNode> next_initializer;
				if (peek() == "="_tok) {
					auto init_result = parse_copy_initialization(next_decl, next_type_spec);
					if (init_result.has_value()) {
						next_initializer = init_result;
					} else {
						return ParseResult::error("Failed to parse initializer expression", current_token_);
					}
				} else if (peek() == "("_tok) {
					// Direct initialization for comma-separated declaration: Type var1, var2(args)
					auto init_result = parse_direct_initialization();
					if (init_result.has_value()) {
						next_initializer = init_result;
					} else {
						return ParseResult::error("Expected ')' after direct initialization arguments", current_token_);
					}
				} else if (peek() == "{"_tok) {
					// Direct list initialization for comma-separated declaration: Type var1, var2{args}
					ParseResult init_list_result = parse_brace_initializer(type_specifier);
					if (init_list_result.is_error()) {
						return init_list_result;
					}
					next_initializer = init_list_result.node();
				}

				// Create a variable declaration node for this additional variable
				auto [next_var_node, next_var_decl] = emplace_node_ref<VariableDeclarationNode>(
					emplace_node<DeclarationNode>(next_decl),
					next_initializer,
					specs.storage_class
				);
				next_var_decl.set_is_constexpr(is_constexpr);
				next_var_decl.set_is_constinit(is_constinit);

				// Add to symbol table
				if (!gSymbolTable.insert(next_identifier_token.value(), next_var_node)) {
					return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, next_identifier_token);
				}
				
				// Add to block
				block_ref.add_statement_node(next_var_node);
			}

			// Expect semicolon after all declarations
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after declaration", current_token_);
			}

			return saved_position.success(block_node);
		}

		// Single declaration - expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after declaration", current_token_);
		}

		return saved_position.success(global_var_node);
	}

	// This should not be reached
	return ParseResult::error("Unexpected parsing state", current_token_);
}

// Parse out-of-line constructor or destructor definition
// Pattern: ClassName::ClassName(...) { ... } or ClassName::~ClassName() { ... }
ParseResult Parser::parse_out_of_line_constructor_or_destructor(std::string_view class_name, bool is_destructor, const FlashCpp::DeclarationSpecifiers& specs)
{
	ScopedTokenPosition saved_position(*this);
	
	FLASH_LOG_FORMAT(Parser, Debug, "parse_out_of_line_constructor_or_destructor: class={}, is_destructor={}", 
		std::string(class_name), is_destructor);
	
	// Consume ClassName::~?ClassName
	Token class_name_token = peek_info();
	advance();  // consume first class name
	
	if (!consume("::"_tok)) {
		return ParseResult::error("Expected '::' in out-of-line constructor/destructor definition", current_token_);
	}
	
	if (is_destructor) {
		// Check for ~ (might be operator type, not punctuator)
		if (peek() != "~"_tok) {
			return ParseResult::error("Expected '~' for destructor definition", current_token_);
		}
		advance();  // consume ~
	}
	
	// Consume the second class name (constructor/destructor name)
	Token func_name_token = peek_info();
	advance();
	
	// Find the struct in the type registry
	StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_name);
	auto struct_iter = gTypesByName.find(class_name_handle);
	if (struct_iter == gTypesByName.end()) {
		FLASH_LOG(Parser, Error, "Unknown class '", class_name, "' in out-of-line constructor/destructor definition");
		return ParseResult::error("Unknown class in out-of-line constructor/destructor", class_name_token);
	}
	
	const TypeInfo* type_info = struct_iter->second;
	StructTypeInfo* struct_info = const_cast<StructTypeInfo*>(type_info->getStructInfo());
	if (!struct_info) {
		FLASH_LOG(Parser, Error, "'", class_name, "' is not a struct/class type");
		return ParseResult::error("Not a struct/class type", class_name_token);
	}
	
	// Parse parameter list
	FlashCpp::ParsedParameterList params;
	auto param_result = parse_parameter_list(params, specs.calling_convention);
	if (param_result.is_error()) {
		return param_result;
	}
	
	// Skip optional qualifiers (noexcept, const, etc.) using existing helper
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);
	
	// Skip trailing requires clause for out-of-line constructor/destructor definitions
	skip_trailing_requires_clause();
	
	// Find the matching constructor/destructor declaration in the struct
	StructMemberFunction* existing_member = nullptr;
	size_t param_count = params.parameters.size();
	
	for (auto& member : struct_info->member_functions) {
		if (is_destructor && member.is_destructor) {
			// Destructors have no parameters to match
			if (member.function_decl.is<DestructorDeclarationNode>()) {
				const DestructorDeclarationNode& dtor = member.function_decl.as<DestructorDeclarationNode>();
				// Skip if already has definition
				if (dtor.get_definition().has_value()) {
					continue;
				}
			}
			existing_member = &member;
			break;
		} else if (!is_destructor && member.is_constructor) {
			// For constructors, match by parameter count and types
			if (member.function_decl.is<ConstructorDeclarationNode>()) {
				const ConstructorDeclarationNode& ctor = member.function_decl.as<ConstructorDeclarationNode>();
				
				// Skip if already has definition
				if (ctor.get_definition().has_value()) {
					continue;
				}
				
				// Check parameter count first
				if (ctor.parameter_nodes().size() != param_count) {
					continue;
				}
				
				// Match parameter types
				bool params_match = true;
				for (size_t i = 0; i < param_count && params_match; ++i) {
					const ASTNode& decl_param = ctor.parameter_nodes()[i];
					const ASTNode& def_param = params.parameters[i];
					
					// Get type info from both parameters
					const TypeSpecifierNode* decl_type = nullptr;
					const TypeSpecifierNode* def_type = nullptr;
					
					if (decl_param.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var = decl_param.as<VariableDeclarationNode>();
						if (var.declaration().type_node().is<TypeSpecifierNode>()) {
							decl_type = &var.declaration().type_node().as<TypeSpecifierNode>();
						}
					} else if (decl_param.is<DeclarationNode>()) {
						const DeclarationNode& decl = decl_param.as<DeclarationNode>();
						if (decl.type_node().is<TypeSpecifierNode>()) {
							decl_type = &decl.type_node().as<TypeSpecifierNode>();
						}
					}
					
					if (def_param.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var = def_param.as<VariableDeclarationNode>();
						if (var.declaration().type_node().is<TypeSpecifierNode>()) {
							def_type = &var.declaration().type_node().as<TypeSpecifierNode>();
						}
					} else if (def_param.is<DeclarationNode>()) {
						const DeclarationNode& decl = def_param.as<DeclarationNode>();
						if (decl.type_node().is<TypeSpecifierNode>()) {
							def_type = &decl.type_node().as<TypeSpecifierNode>();
						}
					}
					
					if (!decl_type || !def_type) {
						params_match = false;
						continue;
					}
					
					// Compare types
					if (decl_type->type() != def_type->type()) {
						params_match = false;
					} else if (decl_type->pointer_depth() != def_type->pointer_depth()) {
						params_match = false;
					} else if (decl_type->is_reference() != def_type->is_reference()) {
						params_match = false;
					} else if (decl_type->type_index() != def_type->type_index()) {
						// For user-defined types, check type_index
						params_match = false;
					}
				}
				
				if (params_match) {
					existing_member = &member;
					break;
				}
			}
		}
	}
	
	if (!existing_member) {
		FLASH_LOG(Parser, Error, "Out-of-line definition of '", class_name, is_destructor ? "::~" : "::", class_name, 
		          "' does not match any declaration in the class");
		return ParseResult::error("No matching declaration found", func_name_token);
	}
	
	// Get mutable reference to constructor for adding member initializers
	ConstructorDeclarationNode* ctor_ref = nullptr;
	if (!is_destructor && existing_member->function_decl.is<ConstructorDeclarationNode>()) {
		ctor_ref = &const_cast<ConstructorDeclarationNode&>(
			existing_member->function_decl.as<ConstructorDeclarationNode>());
	}
	
	// Enter function scope with RAII guard - need to do this before parsing initializer list
	// so that expressions in the initializer can reference parameters
	FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
	
	// Push member function context so that member variables are resolved correctly
	member_function_context_stack_.push_back({
		class_name_handle,
		type_info->type_index_,
		nullptr,  // struct_node - we don't have access to it here
		nullptr   // local_struct_info - not needed here
	});
	
	// Add 'this' pointer to symbol table
	auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
		Type::Struct, type_info->type_index_, 
		static_cast<int>(struct_info->total_size * 8), Token()
	);
	this_type_ref.add_pointer_level();  // Make it a pointer
	
	Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
	auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
	gSymbolTable.insert("this"sv, this_decl_node);
	
	// Add function parameters to symbol table - use the DEFINITION's parameters (params.parameters)
	// not the declaration's parameters, since they may have different names
	for (const ASTNode& param_node : params.parameters) {
		if (param_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
			const DeclarationNode& param_decl = var_decl.declaration();
			if (!param_decl.identifier_token().value().empty()) {
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		} else if (param_node.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			if (!param_decl.identifier_token().value().empty()) {
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		}
	}
	
	// For constructors, parse member initializer list
	if (!is_destructor && peek() == ":"_tok) {
		advance();  // consume ':'
		
		while (!peek().is_eof() &&
		       peek() != "{"_tok &&
		       peek() != ";"_tok) {
			auto init_name_token = advance();
			if (!init_name_token.kind().is_identifier()) {
				member_function_context_stack_.pop_back();
				return ParseResult::error("Expected member name in initializer list", init_name_token);
			}
			
			std::string_view init_name = init_name_token.value();
			
			// Check for template arguments: Base<T>(...) in base class initializer
			if (peek() == "<"_tok) {
				skip_template_arguments();
			}
			
			bool is_paren = peek() == "("_tok;
			bool is_brace = peek() == "{"_tok;
			
			if (!is_paren && !is_brace) {
				member_function_context_stack_.pop_back();
				return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
			}
			
			advance();  // consume '(' or '{'
			TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();
			
			std::vector<ASTNode> init_args;
			if (peek() != close_kind) {
				do {
					ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						member_function_context_stack_.pop_back();
						return arg_result;
					}
					if (auto arg_node = arg_result.node()) {
						init_args.push_back(*arg_node);
					}
				} while (peek() == ","_tok && (advance(), true));
			}
			
			if (!consume(close_kind)) {
				member_function_context_stack_.pop_back();
				return ParseResult::error(is_paren ?
				    "Expected ')' after initializer arguments" :
				    "Expected '}' after initializer arguments", peek_info());
			}
			
			// Add member initializer to constructor
			if (ctor_ref && !init_args.empty()) {
				ctor_ref->add_member_initializer(init_name, init_args[0]);
			}
			
			if (!consume(","_tok)) {
				break;
			}
		}
	}
	
	// Parse function body
	if (peek() != "{"_tok) {
		member_function_context_stack_.pop_back();
		return ParseResult::error("Expected '{' in constructor/destructor definition", current_token_);
	}
	
	// Parse function body
	ParseResult body_result = parse_block();
	
	if (body_result.is_error()) {
		member_function_context_stack_.pop_back();
		return body_result;
	}
	
	// Set the definition on the existing declaration
	if (body_result.node().has_value()) {
		if (is_destructor && existing_member->function_decl.is<DestructorDeclarationNode>()) {
			DestructorDeclarationNode& dtor = const_cast<DestructorDeclarationNode&>(
				existing_member->function_decl.as<DestructorDeclarationNode>());
			if (!dtor.set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Destructor '", class_name, "::~", class_name, "' already has a definition");
				member_function_context_stack_.pop_back();
				return ParseResult::error("Destructor already has definition", func_name_token);
			}
			// Note: Destructors have no parameters, so no need to update parameter nodes
		} else if (ctor_ref) {
			if (!ctor_ref->set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Constructor '", class_name, "::", class_name, "' already has a definition");
				member_function_context_stack_.pop_back();
				return ParseResult::error("Constructor already has definition", func_name_token);
			}
			// Update parameter nodes to use definition's parameter names
			// C++ allows declaration and definition to have different parameter names
			ctor_ref->update_parameter_nodes_from_definition(params.parameters);
		}
	}
	
	member_function_context_stack_.pop_back();
	
	FLASH_LOG_FORMAT(Parser, Debug, "parse_out_of_line_constructor_or_destructor: Successfully parsed {}::{}{}()", 
		std::string(class_name), is_destructor ? "~" : "", std::string(class_name));
	
	// Return success - the existing declaration already has the definition attached
	return saved_position.success();
}

// Helper function to parse and register a type alias (typedef or using) inside a struct/template
// Handles both "typedef Type Alias;" and "using Alias = Type;" syntax
// Also handles inline definitions: "typedef struct { ... } Alias;"
// Also handles using-declarations: "using namespace::name;" (member access import)
// Returns ParseResult with no node on success, error on failure
ParseResult Parser::parse_member_type_alias(std::string_view keyword, StructDeclarationNode* struct_ref, AccessSpecifier current_access)
{
	advance(); // consume 'typedef' or 'using'
	
	// For 'using', check if it's an alias or a using-declaration
	if (keyword == "using") {
		auto alias_token = peek_info();
		if (!alias_token.kind().is_identifier()) {
			return ParseResult::error("Expected alias name after 'using'", peek_info());
		}
		
		// Look ahead to see if this is:
		// 1. Type alias: using Alias = Type;  (identifier followed by '=')
		// 2. Using-declaration: using namespace::member;  (identifier followed by '::')
		// 3. Inheriting constructor: using Base<T>::Base;  (identifier<template args> followed by '::')
		SaveHandle lookahead_pos = save_token_position();
		advance(); // consume first identifier
		
		// Skip template arguments if present
		if (peek() == "<"_tok) {
			skip_template_arguments();
		}
		
		auto next_token = peek_info();
		
		if (next_token.kind() == "::"_tok) {
			// This is a using-declaration like: using std::__is_integer<_Tp>::__value;
			// Or an inheriting constructor like: using Base<T>::Base;
			// Parse and extract the member name to register it in the current scope
			std::string_view base_class_name = alias_token.value();  // Remember the first identifier (base class name)
			std::string_view member_name;
			
			while (peek() == "::"_tok) {
				advance(); // consume '::'
				
				// Consume the next identifier, operator, or template
				if (!peek().is_eof()) {
					if (peek().is_identifier()) {
						member_name = peek_info().value();  // Track last identifier as potential member name
						advance(); // consume identifier
						
						// Check for template arguments
						if (peek() == "<"_tok) {
							skip_template_arguments();
							// After template args, the member name is whatever comes next
							member_name = "";  // Reset - next identifier after :: will be the member
						}
					} else if (peek() == "operator"_tok) {
						// using Base::operator Type; (conversion operator)
						// using Base::operator=; (assignment operator)
						advance(); // consume 'operator'
						// Build the full operator name: "operator=", "operator<<", "operator __integral_type", etc.
						StringBuilder op_name_builder;
						op_name_builder.append("operator");
						while (!peek().is_eof() && peek() != ";"_tok && peek() != "..."_tok) {
							// Add space before type names but not before operator symbols
							if (peek().is_identifier() || peek().is_keyword()) {
								op_name_builder.append(" ");
							}
							op_name_builder.append(peek_info().value());
							advance();
						}
						member_name = op_name_builder.commit();
						break;
					} else {
						break;
					}
				}
			}
			
			// Check if this is an inheriting constructor: using Base::Base;
			// Per C++ standard, inheriting constructors specifically require the member name
			// to match the base class name. General using-declarations can import any member.
			// Example: using Base<T>::Base;  // Inherits all Base constructors
			//          using Base::member;     // Imports a specific member
			bool is_inheriting_constructor = (member_name == base_class_name);
			
			// Register the imported member name in the struct parsing context
			// This makes the member accessible by its simple name even when the
			// base class is a dependent type (template) that can't be resolved yet
			if (!member_name.empty()) {
				if (!struct_parsing_context_stack_.empty()) {
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					struct_parsing_context_stack_.back().imported_members.push_back(member_handle);
					
					if (is_inheriting_constructor) {
						FLASH_LOG(Parser, Debug, "Inheriting constructors from '", base_class_name, "' into struct parsing context");
						// For inheriting constructors, we import the constructors from the base class
						// Mark that constructors are inherited
						struct_parsing_context_stack_.back().has_inherited_constructors = true;
					} else {
						FLASH_LOG(Parser, Debug, "Using-declaration imports member '", member_name, "' into struct parsing context");
					}
				}
			}
			
			// Consume pack expansion '...' if present (C++17 using-declaration with pack expansion)
			// e.g., using Base<Args>::member...;
			if (peek() == "..."_tok) {
				advance(); // consume '...'
			}
			
			// Consume trailing semicolon
			if (peek() == ";"_tok) {
				advance(); // consume ';'
			}
			
			// Discard the saved position - we successfully parsed the using-declaration
			discard_saved_token(lookahead_pos);
			return ParseResult::success();
		}
		
		// Restore position - this is a type alias
		restore_token_position(lookahead_pos);
		
		StringHandle alias_name = alias_token.handle();
		advance(); // consume alias name
		
		// Skip C++ [[...]] and GCC __attribute__((...)) between alias name and '='
		// e.g., using is_always_equal __attribute__((__deprecated__("..."))) = true_type;
		// e.g., using result_type [[__deprecated__]] = size_t;
		skip_cpp_attributes();
		
		// Check for '='
		if (peek() != "="_tok) {
			return ParseResult::error("Expected '=' after alias name", current_token_);
		}
		advance(); // consume '='
		
		// Parse the type
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type after '=' in type alias", current_token_);
		}

		// Parse pointer/reference modifiers after the base type
		// For example: using type = _Tp&; or using RvalueRef = T&&;
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Parse postfix cv-qualifiers: _Tp const, _Tp volatile, _Tp const volatile
		// This is the C++ postfix const/volatile syntax used in standard library headers
		CVQualifier cv_qualifier = parse_cv_qualifiers();
		type_spec.add_cv_qualifier(cv_qualifier);
		
		// Check for pointer-to-member type syntax: Type Class::*
		// This is used in <type_traits> for result_of patterns
		// Pattern: using _MemPtr = _Res _Class::*;
		if (peek().is_identifier()) {
			// Look ahead to see if this is Class::* pattern
			auto saved_pos = save_token_position();
			Token class_token = peek_info();
			advance(); // consume potential class name
			
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					// This is a pointer-to-member type: Type Class::*
					// Mark the type as a pointer-to-member
					type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
					type_spec.set_member_class_name(class_token.handle());
					FLASH_LOG(Parser, Debug, "Parsed pointer-to-member type: ", type_spec.token().value(), " ", class_token.value(), "::*");
					discard_saved_token(saved_pos);
				} else {
					// Not a pointer-to-member, restore position
					restore_token_position(saved_pos);
				}
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		}
		
		// Parse pointer declarators: * [const] [volatile] *...
		while (peek() == "*"_tok) {
			advance(); // consume '*'
			
			// Parse cv-qualifiers after pointer
			CVQualifier ptr_cv = parse_cv_qualifiers();
			type_spec.add_pointer_level(ptr_cv);
		}
		
		// Check for function pointer/reference type syntax: ReturnType (&)(...) or ReturnType (*)(...) 
		// Pattern: Type (&)() = lvalue reference to function returning Type
		// Pattern: Type (&&)() = rvalue reference to function returning Type
		// Pattern: Type (*)() = pointer to function returning Type
		// This handles types like: int (&)(), _Xp (&)(), etc.
		if (peek() == "("_tok) {
			auto func_type_saved_pos = save_token_position();
			advance(); // consume '('
			
			// Check what's inside the parentheses: &, &&, or *
			bool is_function_ref = false;
			bool is_rvalue_function_ref = false;
			bool is_function_ptr = false;
			
			if (!peek().is_eof()) {
				if (peek() == "&&"_tok) {
					is_rvalue_function_ref = true;
					advance(); // consume '&&'
				} else if (peek() == "&"_tok) {
					is_function_ref = true;
					advance(); // consume '&'
					// Check for second & (in case lexer didn't combine them)
					if (peek() == "&"_tok) {
						is_rvalue_function_ref = true;
						is_function_ref = false;
						advance(); // consume second '&'
					}
				} else if (peek() == "*"_tok) {
					is_function_ptr = true;
					advance(); // consume '*'
				}
			}
			
			// After &, &&, or *, expect ')'
			if ((is_function_ref || is_rvalue_function_ref || is_function_ptr) &&
			    peek() == ")"_tok) {
				advance(); // consume ')'
				
				// Now expect '(' for the parameter list
				if (peek() == "("_tok) {
					advance(); // consume '('
					
					// Parse parameter list (can be empty or have parameters)
					// For now, we'll skip the parameter list - we just need to recognize the syntax
					// and accept it for type traits purposes
					std::vector<Type> param_types;
					while (!peek().is_eof() && peek() != ")"_tok) {
						// Skip parameter - can be complex types
						auto param_type_result = parse_type_specifier();
						if (!param_type_result.is_error() && param_type_result.node().has_value()) {
							const TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
							param_types.push_back(param_type.type());
						}
						
						// Handle pointer/reference/cv-qualifier modifiers after type
						while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok ||
							   peek() == "const"_tok || peek() == "volatile"_tok) {
							advance();
						}
						
						// Handle pack expansion '...' (e.g., _Args...)
						if (peek() == "..."_tok) {
							advance(); // consume '...'
						}
						
						// Check for comma
						if (peek() == ","_tok) {
							advance(); // consume ','
						} else {
							break;
						}
					}
					
					if (peek() == ")"_tok) {
						advance(); // consume ')'
						
						// Successfully parsed function reference/pointer type!
						// Mark the type accordingly
						FunctionSignature func_sig;
						func_sig.return_type = type_spec.type();
						func_sig.parameter_types = std::move(param_types);
						
						if (is_function_ptr) {
							type_spec.add_pointer_level(CVQualifier::None);
						}
						type_spec.set_function_signature(func_sig);
						
						if (is_function_ref) {
							type_spec.set_reference(false);  // lvalue reference
						} else if (is_rvalue_function_ref) {
							type_spec.set_reference(true);   // rvalue reference
						}
						
						FLASH_LOG(Parser, Debug, "Parsed function reference/pointer type: ", 
						          is_function_ptr ? "pointer" : (is_rvalue_function_ref ? "rvalue ref" : "lvalue ref"),
						          " to function");
						
						// Discard saved position - we successfully parsed
						discard_saved_token(func_type_saved_pos);
					} else {
						// Parsing failed - restore position
						restore_token_position(func_type_saved_pos);
					}
				} else {
					// No parameter list follows - restore position
					restore_token_position(func_type_saved_pos);
				}
			} else if (!is_function_ref && !is_rvalue_function_ref && !is_function_ptr) {
				// Could be a bare function type: ReturnType(Args...)
				// e.g., using type = _Res(_Args...);
				// The '(' was already consumed, we're looking at the first parameter type or ')'
				std::vector<Type> param_types;
				bool parsed_bare_function_type = false;
				
				while (!peek().is_eof() && peek() != ")"_tok) {
					auto param_type_result = parse_type_specifier();
					if (param_type_result.is_error() || !param_type_result.node().has_value()) {
						break;
					}
					TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
					
					// Handle pointer/reference/cv-qualifier modifiers after type
					consume_pointer_ref_modifiers(param_type);
					
					// Handle pack expansion '...' (e.g., _Args...)
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						param_type.set_pack_expansion(true);
					}
					
					param_types.push_back(param_type.type());
					
					if (peek() == ","_tok) {
						advance(); // consume ','
					} else {
						break;
					}
				}
				
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					parsed_bare_function_type = true;
					
					FunctionSignature func_sig;
					func_sig.return_type = type_spec.type();
					func_sig.parameter_types = std::move(param_types);
					type_spec.set_function_signature(func_sig);
					
					FLASH_LOG(Parser, Debug, "Parsed bare function type in type alias");
					
					discard_saved_token(func_type_saved_pos);
				}
				
				if (!parsed_bare_function_type) {
					restore_token_position(func_type_saved_pos);
				}
			} else {
				// Not a function type syntax - restore position
				restore_token_position(func_type_saved_pos);
			}
		}
		
		// Parse reference modifiers: & or &&
		ReferenceQualifier ref_qual = parse_reference_qualifier();
		FLASH_LOG_FORMAT(Parser, Debug, "Type alias '{}': ref_qual={} (0=None, 1=LValue, 2=RValue)", 
			StringTable::getStringView(alias_name), static_cast<int>(ref_qual));
		type_spec.set_reference_qualifier(ref_qual);
		
		// Parse array dimensions: using _Type = _Tp[_Nm]; or using _Type = _Tp[2][3];
		while (peek() == "["_tok) {
			advance(); // consume '['
			if (peek() == "]"_tok) {
				type_spec.set_array(true);
				advance(); // consume ']'
			} else {
				auto dim_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (dim_result.is_error()) {
					return dim_result;
				}
				auto dim_val = try_evaluate_constant_expression(*dim_result.node());
				size_t dim_size = dim_val.has_value() ? static_cast<size_t>(dim_val->value) : 0;
				type_spec.add_array_dimension(dim_size);
				if (!consume("]"_tok)) {
					return ParseResult::error("Expected ']' after array dimension in type alias", current_token_);
				}
			}
		}
		
		// Consume semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after type alias", current_token_);
		}
		
		// Store the alias in the struct (if struct_ref provided)
		if (struct_ref) {
			struct_ref->add_type_alias(alias_name, *type_result.node(), current_access);
		}
		
		// Also register it globally with qualified name (e.g., WithType::type)
		// (re-get type_spec since we modified it above)
		const TypeSpecifierNode& final_type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Build qualified name if we're inside a struct
		StringHandle qualified_alias_name = alias_name;
		if (struct_ref) {
			StringBuilder qualified_builder;
			qualified_builder.append(struct_ref->name());
			qualified_builder.append("::");
			qualified_builder.append(alias_name);
			qualified_alias_name = StringTable::getOrInternStringHandle(qualified_builder.commit());
		}
		
		auto& alias_type_info = gTypeInfo.emplace_back(qualified_alias_name, final_type_spec.type(), final_type_spec.type_index(), final_type_spec.size_in_bits());
		alias_type_info.is_reference_ = final_type_spec.is_reference();
		alias_type_info.is_rvalue_reference_ = final_type_spec.is_rvalue_reference();
		alias_type_info.pointer_depth_ = final_type_spec.pointer_depth();
		gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
		
		return ParseResult::success();
	}
	
	// For 'typedef', check if this is an inline struct/enum definition
	// Pattern: typedef struct { ... } Alias;
	// Pattern: typedef enum { ... } Alias;
	if (!peek().is_eof() && 
	    (peek() == "struct"_tok || peek() == "class"_tok || peek() == "enum"_tok)) {
		// This is potentially an inline definition - use the full parse_typedef_declaration logic
		// We already consumed 'typedef', so we need to restore it
		// Actually, we can't restore easily, so let's handle it inline here
		
		bool is_enum = peek() == "enum"_tok;
		bool is_struct = peek() == "struct"_tok || peek() == "class"_tok;
		
		// Look ahead to check if it's really an inline definition
		auto saved_pos = save_token_position();
		advance(); // consume struct/class/enum
		
		bool is_inline_definition = false;
		if (!peek().is_eof()) {
			// If next token is '{', it's definitely inline: typedef struct { ... } Alias;
			if (peek() == "{"_tok) {
				is_inline_definition = true;
			} else if (peek().is_identifier()) {
				// Could be: typedef struct Name { ... } Alias; (inline)
				// or:       typedef struct Name Alias; (forward reference)
				advance(); // consume name
				if (!peek().is_eof() && (peek() == "{"_tok || peek() == ":"_tok)) {
					is_inline_definition = true;
				}
			}
		}
		
		restore_token_position(saved_pos);
		
		if (is_inline_definition && is_struct) {
			// Parse inline struct: typedef struct { ... } Alias; or typedef struct Name { ... } Alias;
			bool is_class = peek() == "class"_tok;
			advance(); // consume 'struct' or 'class'
			
			// Check if there's a struct name or if it's anonymous
			std::string_view struct_name_view;
			
			if (peek().is_identifier()) {
				struct_name_view = peek_info().value();
				advance(); // consume struct name
			} else {
				// Anonymous struct - generate a unique name using StringBuilder for persistent storage
				struct_name_view = StringBuilder()
					.append("__anonymous_typedef_struct_")
					.append(ast_nodes_.size())
					.commit();
			}
			
			// Register the struct type early
			StringHandle struct_name = StringTable::getOrInternStringHandle(struct_name_view);
			TypeInfo& struct_type_info = add_struct_type(struct_name);
			TypeIndex struct_type_index = struct_type_info.type_index_;
			// Create struct declaration node
			auto [struct_node, struct_ref_inner] = emplace_node_ref<StructDeclarationNode>(struct_name, is_class);
			
			// Create StructTypeInfo
			auto struct_info = std::make_unique<StructTypeInfo>(struct_name, is_class ? AccessSpecifier::Private : AccessSpecifier::Public);
			
			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' in struct definition", peek_info());
			}
			
			// Parse struct members (simplified - just type and name)
			AccessSpecifier member_access = struct_info->default_access;
			size_t member_count = 0;
			const size_t MAX_MEMBERS = 10000; // Safety limit
			
			while (!peek().is_eof() && peek() != "}"_tok && member_count < MAX_MEMBERS) {
				member_count++;
				
				// Parse member type
				auto member_type_result = parse_type_specifier();
				if (member_type_result.is_error()) {
					return member_type_result;
				}
				
				if (!member_type_result.node().has_value()) {
					return ParseResult::error("Expected type specifier in struct member", current_token_);
				}
				
				// Handle pointer declarators with CV-qualifiers (e.g., "unsigned short const* _locale_pctype")
				// Parse pointer declarators: * [const] [volatile] *...
				TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(member_type_spec);

				// Parse member name
				auto member_name_token = peek_info();
				if (!member_name_token.kind().is_identifier()) {
					FLASH_LOG(Parser, Debug, "Expected member name but got: type=",
						!member_name_token.kind().is_eof() ? static_cast<int>(member_name_token.type()) : -1,
						" value='", !member_name_token.kind().is_eof() ? member_name_token.value() : "NONE", "'");
					return ParseResult::error("Expected member name in struct", member_name_token);
				}
				advance(); // consume the member name

				std::optional<size_t> bitfield_width;
				
				// Handle bitfield declarations: unsigned int field:8;
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
					if (width_result.is_error()) {
						return width_result;
					}
					if (width_result.node().has_value()) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
						if (!eval_result.success() || eval_result.as_int() < 0) {
							return ParseResult::error("Bitfield width must be a non-negative integral constant expression", current_token_);
						}
						bitfield_width = static_cast<size_t>(eval_result.as_int());
					}
				}

				// Create member declaration
				auto member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token);
				struct_ref_inner.add_member(member_decl_node, member_access, std::nullopt, bitfield_width);
				
				// Handle comma-separated declarations
				while (peek() == ","_tok) {
					advance(); // consume ','
					auto next_name = advance();
					if (!next_name.kind().is_identifier()) {
						return ParseResult::error("Expected member name after comma", current_token_);
					}
					std::optional<size_t> next_bitfield_width;
					if (peek() == ":"_tok) {
						advance(); // consume ':'
						auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
						if (width_result.is_error()) {
							return width_result;
						}
						if (width_result.node().has_value()) {
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
							if (!eval_result.success() || eval_result.as_int() < 0) {
								return ParseResult::error("Bitfield width must be a non-negative integral constant expression", current_token_);
							}
							next_bitfield_width = static_cast<size_t>(eval_result.as_int());
						}
					}
					auto next_decl = emplace_node<DeclarationNode>(
						emplace_node<TypeSpecifierNode>(member_type_spec),
						next_name
					);
					struct_ref_inner.add_member(next_decl, member_access, std::nullopt, next_bitfield_width);
				}
				
				// Expect semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after struct member", current_token_);
				}
			}
			
			if (member_count >= MAX_MEMBERS) {
				return ParseResult::error("Struct has too many members (possible infinite loop detected)", current_token_);
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after struct members", peek_info());
			}
			
			// Calculate struct layout
			for (const auto& member_decl : struct_ref_inner.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& member_type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				// Calculate member size and alignment
				auto [member_size_in_bits, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
				
				// For struct types, get the actual size from TypeInfo
				if (member_type_spec.type() == Type::Struct) {
					TypeInfo* member_type_info = nullptr;
					for (auto& ti : gTypeInfo) {
						if (ti.type_index_ == member_type_spec.type_index()) {
							member_type_info = &ti;
							break;
						}
					}
					if (member_type_info && member_type_info->getStructInfo()) {
						member_size_in_bits = member_type_info->getStructInfo()->total_size;
						member_alignment = member_type_info->getStructInfo()->alignment;
					}
				}
				
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				struct_info->addMember(
					member_name_handle,
					member_type_spec.type(),
					member_type_spec.type_index(),
					member_size_in_bits,
					member_alignment,
					member_access,
					std::nullopt,
					member_type_spec.is_reference(),
					member_type_spec.is_rvalue_reference(),
					member_type_spec.size_in_bits(),
					false,
					{},
					static_cast<int>(member_type_spec.pointer_depth()),
					member_decl.bitfield_width
				);
			}
			
			// Finalize struct layout
			if (!struct_info->finalize()) {
				return ParseResult::error(struct_info->getFinalizationError(), Token());
			}
			
			// Store struct info
			struct_type_info.setStructInfo(std::move(struct_info));
			// Update type_size_ from the finalized struct's total size
			if (struct_type_info.getStructInfo()) {
				struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
			}
			
			// Parse the typedef alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after struct definition", current_token_);
			}
			auto alias_name = alias_token.handle();
			
			// Consume semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after typedef", current_token_);
			}
			
			// Create type specifier for the typedef
			int struct_size_bits = 0;
			if (const StructTypeInfo* finalized_struct_info = struct_type_info.getStructInfo()) {
				struct_size_bits = static_cast<int>(finalized_struct_info->total_size * 8);
			}
			TypeSpecifierNode type_spec(
				Type::Struct,
				struct_type_index,
				struct_size_bits,
				alias_token
			);
			ASTNode type_node = emplace_node<TypeSpecifierNode>(type_spec);
			
			// Store the alias in the struct (if struct_ref provided)
			if (struct_ref) {
				struct_ref->add_type_alias(alias_name, type_node, current_access);
			}
			
			// Register the alias globally
			auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), gTypeInfo.size(), type_spec.size_in_bits());
			alias_type_info.type_index_ = type_spec.type_index();
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			return ParseResult::success();
		}
		
		if (is_inline_definition && is_enum) {
			// Parse inline enum: typedef enum { ... } Alias;
			advance(); // consume 'enum'
			
			// Check if there's an enum name or if it's anonymous
			StringHandle enum_name;
			
			if (peek().is_identifier()) {
				enum_name = peek_info().handle();
				advance(); // consume enum name
			} else {
				// Anonymous enum - generate a unique name using StringBuilder for persistent storage
				enum_name = StringTable::getOrInternStringHandle(StringBuilder()
					.append("__anonymous_typedef_enum_")
					.append(ast_nodes_.size()));
			}
			
			// Register the enum type early
			TypeInfo& enum_type_info = add_enum_type(enum_name);
			TypeIndex enum_type_index = enum_type_info.type_index_;
			
			// Create enum declaration node
			bool is_scoped = false;
			auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name, is_scoped);
			
			// Check for underlying type specification (: type)
			if (peek() == ":"_tok) {
				advance(); // consume ':'
				auto underlying_type_result = parse_type_specifier();
				if (underlying_type_result.is_error()) {
					return underlying_type_result;
				}
				if (auto underlying_type_node = underlying_type_result.node()) {
					enum_ref.set_underlying_type(*underlying_type_node);
				}
			}
			
			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' in enum definition", peek_info());
			}
			
			// Create enum type info
			auto enum_info = std::make_unique<EnumTypeInfo>(enum_name, is_scoped);
			
			// Determine underlying type
			int underlying_size = 32;
			if (enum_ref.has_underlying_type()) {
				const auto& type_spec_node = enum_ref.underlying_type()->as<TypeSpecifierNode>();
				underlying_size = type_spec_node.size_in_bits();
			}
			
			// Parse enumerators
			int64_t next_value = 0;
			size_t enumerator_count = 0;
			const size_t MAX_ENUMERATORS = 10000; // Safety limit
			
			// Store enum info early so ConstExprEvaluator can look up values during parsing
			enum_type_info.setEnumInfo(std::move(enum_info));
			auto* live_enum_info = enum_type_info.getEnumInfo();
			
			// For scoped enums, push a temporary scope so that enumerator names
			// are visible to subsequent value expressions (C++ §9.7.1/2)
			if (is_scoped) {
				gSymbolTable.enter_scope(ScopeType::Block);
			}
			
			while (!peek().is_eof() && peek() != "}"_tok && enumerator_count < MAX_ENUMERATORS) {
				enumerator_count++;
				
				auto enumerator_name_token = advance();
				if (!enumerator_name_token.kind().is_identifier()) {
					if (is_scoped) gSymbolTable.exit_scope();
					return ParseResult::error("Expected enumerator name in enum", enumerator_name_token);
				}
				
				int64_t value = next_value;
				std::optional<ASTNode> enumerator_value;
				
				if (peek() == "="_tok) {
					advance(); // consume '='
					auto value_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (value_expr_result.is_error()) {
						if (is_scoped) gSymbolTable.exit_scope();
						return value_expr_result;
					}
					if (auto value_node = value_expr_result.node()) {
						enumerator_value = *value_node;
						// Extract numeric value if possible
						bool value_extracted = false;
						if (value_node->is<ExpressionNode>()) {
							const auto& expr = value_node->as<ExpressionNode>();
							if (std::holds_alternative<NumericLiteralNode>(expr)) {
								const auto& lit = std::get<NumericLiteralNode>(expr);
								const auto& val = lit.value();
								if (std::holds_alternative<unsigned long long>(val)) {
									value = static_cast<int64_t>(std::get<unsigned long long>(val));
									value_extracted = true;
								}
							}
						}
						// Fallback: use ConstExprEvaluator for complex expressions
						if (!value_extracted) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*value_node, eval_ctx);
							if (eval_result.success()) {
								value = eval_result.as_int();
							}
						}
					}
				}
				
				auto enumerator_node = emplace_node<EnumeratorNode>(enumerator_name_token, enumerator_value);
				enum_ref.add_enumerator(enumerator_node);
				// Phase 7B: Intern enumerator name and use StringHandle overload
				StringHandle enumerator_name_handle = enumerator_name_token.handle();
				live_enum_info->addEnumerator(enumerator_name_handle, value);
				
				// Add enumerator to current scope as DeclarationNode so codegen and
				// ConstExprEvaluator (via gTypeInfo enum lookup) can both find it
				{
					auto enum_type_node = emplace_node<TypeSpecifierNode>(
						Type::Enum, enum_type_index, underlying_size, enumerator_name_token);
					auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_name_token);
					gSymbolTable.insert(enumerator_name_token.value(), enumerator_decl);
				}
				
				next_value = value + 1;
				
				if (peek() == ","_tok) {
					advance();
					if (peek() == "}"_tok) {
						break;
					}
				} else {
					break;
				}
			}
			
			if (enumerator_count >= MAX_ENUMERATORS) {
				if (is_scoped) gSymbolTable.exit_scope();
				return ParseResult::error("Enum has too many enumerators (possible infinite loop detected)", current_token_);
			}
			
			// Pop temporary scope for scoped enums
			if (is_scoped) {
				gSymbolTable.exit_scope();
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after enum enumerators", peek_info());
			}
			
			// enum_info was already stored in gTypeInfo before the loop
			
			// Parse the typedef alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after enum definition", current_token_);
			}
			auto alias_name = alias_token.handle();
			
			// Consume semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after typedef", current_token_);
			}
			
			// Create type specifier for the typedef
			TypeSpecifierNode type_spec(Type::Enum, TypeQualifier::None, underlying_size, alias_token);
			type_spec.set_type_index(enum_type_index);
			ASTNode type_node = emplace_node<TypeSpecifierNode>(type_spec);
			
			// Store the alias in the struct (if struct_ref provided)
			if (struct_ref) {
				struct_ref->add_type_alias(alias_name, type_node, current_access);
			}
			
			// Register the alias globally
			auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			return ParseResult::success();
		}
	}
	
	// Simple typedef: typedef Type Alias;
	// Parse the type
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}
	
	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type after 'typedef'", current_token_);
	}
	
	ASTNode type_node = *type_result.node();
	TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();
	consume_pointer_ref_modifiers(type_spec);

	// Check for pointer-to-member type syntax: typedef Type Class::* alias;
	// This is used in <type_traits> for result_of patterns
	// Pattern: typedef _Res _Class::* _MemPtr;
	if (peek().is_identifier()) {
		// Look ahead to see if this is Class::* pattern
		SaveHandle saved_pos = save_token_position();
		Token class_token = peek_info();
		advance(); // consume potential class name
		
		if (peek() == "::"_tok) {
			advance(); // consume '::'
			if (peek() == "*"_tok) {
				advance(); // consume '*'
				// This is a pointer-to-member type: Type Class::*
				// Mark the type as a pointer-to-member
				type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
				type_spec.set_member_class_name(class_token.handle());
				FLASH_LOG(Parser, Debug, "Parsed pointer-to-member typedef in member_type_alias: ", type_spec.token().value(), " ", class_token.value(), "::*");
				discard_saved_token(saved_pos);
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		} else {
			// Not a pointer-to-member, restore position
			restore_token_position(saved_pos);
		}
	}
	
	// Check for function pointer typedef: typedef ReturnType (*Name)(Params);
	// Pattern: typedef void (*event_callback)(event e, ios_base& b, int i);
	if (peek() == "("_tok) {
		SaveHandle fnptr_check = save_token_position();
		advance(); // consume '('
		if (peek() == "*"_tok) {
			advance(); // consume '*'
			if (peek().is_identifier()) {
				Token fnptr_name_token = peek_info();
				advance(); // consume alias name
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					// Skip the parameter list
					if (peek() == "("_tok) {
						skip_balanced_parens();
					}
					discard_saved_token(fnptr_check);

					auto alias_name = fnptr_name_token.handle();

					// Register as a function pointer type (treat as void* for now)
					type_spec.add_pointer_level(CVQualifier::None);
					type_node = emplace_node<TypeSpecifierNode>(type_spec);

					// Store the alias in the struct (if struct_ref provided)
					if (struct_ref) {
						struct_ref->add_type_alias(alias_name, type_node, current_access);
					}

					// Register the alias globally
					auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
					gTypesByName.emplace(alias_type_info.name(), &alias_type_info);

					// Consume semicolon
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after typedef", current_token_);
					}

					return ParseResult::success();
				}
			}
		}
		restore_token_position(fnptr_check);
	}

	// Parse the typedef alias name
	auto alias_token = peek_info();
	if (!alias_token.kind().is_identifier()) {
		return ParseResult::error("Expected alias name in typedef", peek_info());
	}
	
	auto alias_name = alias_token.handle();
	advance(); // consume alias name
	
	// Skip C++11 attributes that may follow the alias name (e.g., typedef T name [[__deprecated__]];)
	// This is a GNU extension where attributes can appear on the declarator in a typedef
	skip_cpp_attributes();
	
	// Consume semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after typedef", current_token_);
	}
	
	// Update type_node with modified type_spec (with pointers)
	type_node = emplace_node<TypeSpecifierNode>(type_spec);
	
	// Store the alias in the struct (if struct_ref provided)
	if (struct_ref) {
		struct_ref->add_type_alias(alias_name, type_node, current_access);
	}
	
	// Also register it globally
	auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
	alias_type_info.is_rvalue_reference_ = type_spec.is_rvalue_reference();
	gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
	
	return ParseResult::success();
}

ParseResult Parser::parse_struct_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Check for alignas specifier before struct/class keyword
	std::optional<size_t> custom_alignment = parse_alignas_specifier();

	// Consume 'struct', 'class', or 'union' keyword
	auto struct_keyword = advance();
	if (struct_keyword.kind() != "struct"_tok &&
	    struct_keyword.kind() != "class"_tok && struct_keyword.kind() != "union"_tok) {
		return ParseResult::error("Expected 'struct', 'class', or 'union' keyword",
		                          struct_keyword);
	}

	bool is_class = (struct_keyword.kind() == "class"_tok);
	bool is_union = (struct_keyword.kind() == "union"_tok);

	// Check for alignas specifier after struct/class keyword (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Skip C++11 attributes like [[deprecated]], [[nodiscard]], etc.
	// These can appear between struct/class keyword and the name
	// e.g., struct [[__deprecated__]] is_literal_type
	// Also skips GCC attributes like __attribute__((__aligned__))
	// e.g., struct __attribute__((__aligned__)) { }
	skip_cpp_attributes();

	// Parse struct name (optional for anonymous structs)
	auto name_token = advance();
	if (!name_token.kind().is_identifier()) {
		return ParseResult::error("Expected struct/class name", name_token);
	}

	auto struct_name = name_token.handle();

	// Handle out-of-line nested class definitions: class Outer::Inner { ... }
	// The parser consumes the qualified name and uses the last identifier as the struct name
	while (peek() == "::"_tok) {
		advance(); // consume '::'
		if (peek().is_identifier()) {
			name_token = advance();
			struct_name = name_token.handle();
		} else {
			break;
		}
	}

	// Check for template specialization arguments after struct name
	// e.g., struct MyStruct<int>, struct MyStruct<T&>
	if (peek() == "<"_tok) {
		// This is a template specialization - skip the template arguments
		// Full implementation would parse and store these properly
		int angle_bracket_depth = 0;
		advance(); // consume '<'
		angle_bracket_depth = 1;
		
		while (!peek().is_eof() && angle_bracket_depth > 0) {
			if (peek() == "<"_tok) {
				angle_bracket_depth++;
			} else if (peek() == ">"_tok) {
				angle_bracket_depth--;
			}
			advance();
		}
	}

	// Register the struct type in the global type system EARLY
	// This allows member functions (like constructors) to reference the struct type
	// We'll fill in the struct info later after parsing all members
	// For nested classes, we register with the qualified name to avoid conflicts
	bool is_nested_class = !struct_parsing_context_stack_.empty();
	
	// Create a persistent qualified name for nested classes (e.g., "Outer::Inner")
	// This is used when creating member functions so they reference the correct struct type
	// For top-level classes, qualified_struct_name equals struct_name
	StringHandle qualified_struct_name = struct_name;
	StringHandle type_name = struct_name;
	
	// Get namespace handle and qualified name early so we can use it for both TypeInfo and StructTypeInfo
	NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
	std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_namespace_handle);
	
	// Build the full qualified name for use in mangling
	// - For nested classes: Parent::Child
	// - For namespace classes: ns::Class  
	// - For top-level classes: just the simple name
	StringHandle full_qualified_name;
	
	if (is_nested_class) {
		// We're inside a struct, so this is a nested class
		// Use the qualified name (e.g., "Outer::Inner") for the TypeInfo entry
		const auto& context = struct_parsing_context_stack_.back();
		// Build the qualified name using StringBuilder for a persistent allocation
		qualified_struct_name = StringTable::getOrInternStringHandle(StringBuilder()
			.append(context.struct_name)
			.append("::")
			.append(struct_name));
		type_name = qualified_struct_name;
		full_qualified_name = qualified_struct_name;
	} else if (!qualified_namespace.empty()) {
		// Top-level class in a namespace - use namespace-qualified name for proper mangling
		full_qualified_name = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, struct_name);
		qualified_struct_name = full_qualified_name;  // Also update qualified_struct_name for implicit constructors
		type_name = full_qualified_name;  // TypeInfo should also use fully qualified name
	}

	TypeInfo& struct_type_info = add_struct_type(type_name);

	// For nested classes, also register with the simple name so it can be referenced
	// from within the nested class itself (e.g., in constructors)
	if (is_nested_class) {
		gTypesByName.emplace(struct_name, &struct_type_info);
	}
	
	// For namespace classes, also register with the simple name for 'this' pointer lookup
	// during member function code generation. The TypeInfo's name is fully qualified (ns::Test)
	// but parent_struct_name is just "Test", so we need this alias for lookups.
	if (!is_nested_class && !qualified_namespace.empty()) {
		if (gTypesByName.find(struct_name) == gTypesByName.end()) {
			gTypesByName.emplace(struct_name, &struct_type_info);
		}
	}

	// If inside an inline namespace, register the parent-qualified name (e.g., outer::Foo)
	if (!qualified_namespace.empty() && !inline_namespace_stack_.empty() && inline_namespace_stack_.back() && !parsing_template_class_) {
		NamespaceHandle parent_namespace_handle = gNamespaceRegistry.getParent(current_namespace_handle);
		StringHandle parent_handle = gNamespaceRegistry.buildQualifiedIdentifier(parent_namespace_handle, struct_name);
		if (gTypesByName.find(parent_handle) == gTypesByName.end()) {
			gTypesByName.emplace(parent_handle, &struct_type_info);
		}
	}
	
	// Register with namespace-qualified names for all levels of the namespace path
	// This allows lookups like "inner::Base" when we're in namespace "ns" to find "ns::inner::Base"
	if (!qualified_namespace.empty() && !is_nested_class) {
		// full_qualified_name already computed above, just log if needed
		FLASH_LOG(Parser, Debug, "Registered struct '", StringTable::getStringView(struct_name), 
		          "' with namespace-qualified name '", StringTable::getStringView(full_qualified_name), "'");
		
		// Also register intermediate names (e.g., "inner::Base" for "ns::inner::Base")
		// This allows sibling namespace access patterns like:
		// namespace ns { namespace inner { struct Base {}; } struct Derived : public inner::Base {}; }
		for (size_t pos = qualified_namespace.find("::"); pos != std::string_view::npos; pos = qualified_namespace.find("::", pos + 2)) {
			std::string_view suffix = qualified_namespace.substr(pos + 2);
			StringBuilder partial_qualified;
			partial_qualified.append(suffix).append("::").append(struct_name);
			std::string_view partial_view = partial_qualified.commit();
			auto partial_handle = StringTable::getOrInternStringHandle(partial_view);
			if (gTypesByName.find(partial_handle) == gTypesByName.end()) {
				gTypesByName.emplace(partial_handle, &struct_type_info);
				FLASH_LOG(Parser, Debug, "Registered struct '", StringTable::getStringView(struct_name), 
				          "' with partial qualified name '", partial_view, "'");
			}
		}
	}

	// Check for alignas specifier after struct name (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Create struct declaration node - string_view points directly into source text
	auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name, is_class);

	// Push struct parsing context for nested class support
	struct_parsing_context_stack_.push_back({
		StringTable::getStringView(struct_name),
		&struct_ref,
		nullptr,
		gSymbolTable.get_current_namespace_handle(),
		{}
	});
	
	// RAII guard to ensure stack is always popped, even on early returns
	auto pop_stack_guard = [this](void*) { 
		if (!struct_parsing_context_stack_.empty()) {
			struct_parsing_context_stack_.pop_back(); 
		}
	};
	std::unique_ptr<void, decltype(pop_stack_guard)> stack_guard(reinterpret_cast<void*>(1), pop_stack_guard);

	// Create StructTypeInfo early so we can add base classes to it
	// For nested classes, use the qualified name so getName() returns the full name for mangling
	// For top-level classes in a namespace, use full_qualified_name for correct mangling
	// For top-level classes not in a namespace, use the simple name
	StringHandle struct_info_name;
	if (is_nested_class) {
		struct_info_name = qualified_struct_name;
	} else if (full_qualified_name.isValid()) {
		// Top-level class in a namespace - use namespace-qualified name for proper mangling
		struct_info_name = full_qualified_name;
	} else {
		struct_info_name = struct_name;
	}
	auto struct_info = std::make_unique<StructTypeInfo>(struct_info_name, struct_ref.default_access());
	struct_info->is_union = is_union;
	
	// Update the struct parsing context with the local_struct_info for static member lookup
	if (!struct_parsing_context_stack_.empty()) {
		struct_parsing_context_stack_.back().local_struct_info = struct_info.get();
	}

	// Apply pack alignment from #pragma pack BEFORE adding members
	size_t pack_alignment = context_.getCurrentPackAlignment();
	if (pack_alignment > 0) {
		struct_info->set_pack_alignment(pack_alignment);
	}

	// Parse base class list (if present): : public Base1, private Base2
	if (peek() == ":"_tok) {
		advance();  // consume ':'

		do {
			// Parse virtual keyword (optional, can appear before or after access specifier)
			bool is_virtual_base = false;
			if (peek() == "virtual"_tok) {
				is_virtual_base = true;
				advance();
			}

			// Parse access specifier (optional, defaults to public for struct, private for class)
			AccessSpecifier base_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;

			if (peek().is_keyword()) {
				std::string_view keyword = peek_info().value();
				if (keyword == "public") {
					base_access = AccessSpecifier::Public;
					advance();
				} else if (keyword == "protected") {
					base_access = AccessSpecifier::Protected;
					advance();
				} else if (keyword == "private") {
					base_access = AccessSpecifier::Private;
					advance();
				}
			}

			// Check for virtual keyword after access specifier (e.g., "public virtual Base")
			if (!is_virtual_base && peek() == "virtual"_tok) {
				is_virtual_base = true;
				advance();
			}

		// Parse base class name (or decltype expression)
		// Check if this is a decltype base class (e.g., : decltype(expr))
		std::string_view base_class_name;
		TypeSpecifierNode base_type_spec;
		[[maybe_unused]] bool is_decltype_base = false;
		Token base_name_token;  // For error reporting
		
		if (peek() == "decltype"_tok) {
			// Parse decltype(expr) as base class
			base_name_token = peek_info();  // Save for error reporting
			
			// For decltype base classes, we need to parse and try to evaluate the expression
			advance();  // consume 'decltype'
			
			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after 'decltype'", peek_info());
			}
			
			// Parse the expression inside decltype
			ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Decltype);
			if (expr_result.is_error()) {
				return expr_result;
			}
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after decltype expression", peek_info());
			}
			
			// Try to evaluate the expression to get the base class type
			auto type_spec_opt = get_expression_type(*expr_result.node());
			
			if (type_spec_opt.has_value() && 
			    type_spec_opt->type() == Type::Struct && 
			    type_spec_opt->type_index() > 0 &&
			    type_spec_opt->type_index() < gTypeInfo.size()) {
				// Successfully evaluated - add as regular base class
				const TypeInfo& base_type_info = gTypeInfo[type_spec_opt->type_index()];
				std::string_view resolved_base_class_name = StringTable::getStringView(base_type_info.name());
				
				FLASH_LOG(Templates, Debug, "Resolved decltype base class immediately: ", resolved_base_class_name);
				
				// Check if base class is final
				if (base_type_info.struct_info_ && base_type_info.struct_info_->is_final) {
					return ParseResult::error("Cannot inherit from final class '" + std::string(resolved_base_class_name) + "'", base_name_token);
				}
				
				// Add base class to struct node and type info
				struct_ref.add_base_class(resolved_base_class_name, base_type_info.type_index_, base_access, is_virtual_base);
				struct_info->addBaseClass(resolved_base_class_name, base_type_info.type_index_, base_access, is_virtual_base);
				
				// Continue to next base class - skip the rest of the loop body
				continue;
			} else {
				// Could not evaluate now - must be template-dependent, so defer it
				FLASH_LOG(Templates, Debug, "Deferring decltype base class - will be resolved during template instantiation");
				is_decltype_base = true;
				
				// Add deferred base class to struct node with the unevaluated expression
				struct_ref.add_deferred_base_class(*expr_result.node(), base_access, is_virtual_base);
				
				// Continue to next base class - skip the rest of the loop body
				continue;
			}
			
			// Note: code never reaches here due to continue statements above
		} else {
			// Try to parse as qualified identifier (e.g., ns::class, ns::template<Args>::type)
			// Save position in case this is just a simple identifier
			auto saved_pos = save_token_position();
			auto qualified_result = parse_qualified_identifier_with_templates();
			
			if (qualified_result.has_value()) {
				// Qualified identifier like ns::class or ns::template<Args>
				discard_saved_token(saved_pos);
				base_name_token = qualified_result->final_identifier;
				
				// Build the full qualified name using StringBuilder
				StringBuilder full_name_builder;
				for (const auto& ns_handle : qualified_result->namespaces) {
					if (full_name_builder.preview().size() > 0) full_name_builder += "::";
					full_name_builder.append(ns_handle);
				}
				if (full_name_builder.preview().size() > 0) full_name_builder += "::";
				full_name_builder += qualified_result->final_identifier.value();
				std::string_view full_name = full_name_builder.commit();
				
				// Check if there are template arguments
				if (qualified_result->has_template_arguments) {
					// We have template arguments - instantiate the template
					std::vector<TemplateTypeArg> template_args = *qualified_result->template_args;
					
					// Check if any template arguments are dependent
					bool has_dependent_args = false;
					for (const auto& arg : template_args) {
						if (arg.is_dependent || arg.is_pack) {
							has_dependent_args = true;
							break;
						}
					}
					
					// Check for member type access (e.g., ::type) BEFORE deciding to defer
					// We need to consume this even if deferring
					std::optional<StringHandle> member_type_name;
					if (current_token_.value() == "::") {
						advance(); // consume ::
						if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
							return ParseResult::error("Expected member name after ::", current_token_);
						}
						StringHandle member_name = current_token_.handle();
						advance(); // consume member name
						
						member_type_name = member_name;
						
						// Build the fully qualified member type name for logging
						StringBuilder qualified_builder;
						qualified_builder += full_name;
						qualified_builder += "::";
						qualified_builder.append(member_name);
						std::string_view full_member_name = qualified_builder.commit();
						
						FLASH_LOG_FORMAT(Templates, Debug, "Found member type access: {}", full_member_name);
					}
					
					// If template arguments are dependent, defer resolution
					if (has_dependent_args) {
						FLASH_LOG_FORMAT(Templates, Debug, "Base class {} has dependent template arguments - deferring resolution", full_name);
						
						std::vector<TemplateArgumentNodeInfo> arg_infos;
						arg_infos.reserve(template_args.size());
						
						for (size_t arg_idx = 0; arg_idx < template_args.size(); ++arg_idx) {
							const auto& targ = template_args[arg_idx];
							TemplateArgumentNodeInfo info;
							info.is_pack = targ.is_pack;
							info.is_dependent = targ.is_dependent;
							
							StringHandle dep_name = targ.dependent_name;
							if (!dep_name.isValid() && targ.type_index < gTypeInfo.size()) {
								dep_name = gTypeInfo[targ.type_index].name_;
							}
							if (!dep_name.isValid() && arg_idx < current_template_param_names_.size()) {
								dep_name = current_template_param_names_[arg_idx];
							}
							
							if ((targ.is_pack || targ.is_dependent) && dep_name.isValid()) {
								TemplateParameterReferenceNode tparam_ref(dep_name, Token());
								info.node = emplace_node<ExpressionNode>(tparam_ref);
							} else {
								TypeSpecifierNode type_node(
									targ.base_type,
									targ.type_index,
									64,
									Token{},
									targ.cv_qualifier
								);
								
								for (size_t i = 0; i < targ.pointer_depth; ++i) {
									type_node.add_pointer_level();
								}
								if (targ.is_rvalue_reference) {
									type_node.set_reference(true);
								} else if (targ.is_reference) {
									type_node.set_reference(false);
								}
								if (targ.is_array) {
									type_node.set_array(true, targ.array_size);
								}
								
								info.node = emplace_node<TypeSpecifierNode>(type_node);
							}
							
							arg_infos.push_back(std::move(info));
						}
						
						StringHandle template_name_handle = StringTable::getOrInternStringHandle(full_name);
						struct_ref.add_deferred_template_base_class(template_name_handle, std::move(arg_infos), member_type_name, base_access, is_virtual_base);
						
						continue;  // Skip to next base class or exit loop
					}
					
					// Instantiate the template using the qualified name
					// This handles namespace-qualified templates correctly
					auto instantiated_node = try_instantiate_class_template(full_name, template_args, true);
					if (instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
						const StructDeclarationNode& class_decl = instantiated_node->as<StructDeclarationNode>();
						full_name = StringTable::getStringView(class_decl.name());
						FLASH_LOG_FORMAT(Templates, Debug, "Instantiated base class template: {}", full_name);
					}
				}
				
				base_class_name = full_name;
			} else {
				// Simple identifier - restore position and parse it
				restore_token_position(saved_pos);
				auto base_name_token_opt = advance();
				if (!base_name_token_opt.kind().is_identifier()) {
					return ParseResult::error("Expected base class name", base_name_token_opt);
				}
				base_name_token = base_name_token_opt;
				base_class_name = base_name_token.value();
			}
		}
		
		// Regular (non-decltype) base class processing
		// Check if this is a template base class (e.g., Base<T>) and not already handled
		std::string_view instantiated_base_name;
		if (peek() == "<"_tok) {
			// Parse template arguments
			std::vector<ASTNode> template_arg_nodes;
			auto template_args_opt = parse_explicit_template_arguments(&template_arg_nodes);
			if (!template_args_opt.has_value()) {
				return ParseResult::error("Failed to parse template arguments for base class", peek_info());
			}
			
			std::vector<TemplateTypeArg> template_args = *template_args_opt;
			std::optional<StringHandle> member_type_name;
			std::optional<Token> member_name_token;
			
			// Check for member type access (e.g., ::type) after template arguments
			// This handles patterns like: __not_<T>::type
			auto next_token = peek_info();
			if (next_token.kind() == "::"_tok) {
				advance(); // consume ::
				next_token = peek_info();
				if (!next_token.kind().is_identifier()) {
					return ParseResult::error("Expected member name after ::", next_token);
				}
				StringHandle member_name = next_token.handle();
				advance(); // consume member name
				
				member_type_name = member_name;
				member_name_token = next_token;
				FLASH_LOG_FORMAT(Templates, Debug, "Found member type access after template args: {}::{}", base_class_name, next_token.value());
			}
			
			// Check if any template arguments are dependent
			// This includes both explicit dependent flags AND types whose names contain template parameters
			bool has_dependent_args = false;
			auto contains_template_param = [this](StringHandle type_name_handle) -> bool {
				std::string_view type_name = StringTable::getStringView(type_name_handle);
				// Check if this looks like a mangled template name (contains underscores as separators)
				// Mangled names like "is_integral__Tp" use underscore as separator
				bool is_mangled_name = type_name.find('_') != std::string_view::npos;

				for (const auto& param_name : current_template_param_names_) {
					std::string_view param_sv = StringTable::getStringView(param_name);
					// Check if type_name contains param_name as an identifier
					// (not just substring, to avoid false positives like "T" in "Template")
					size_t pos = type_name.find(param_sv);
					while (pos != std::string_view::npos) {
						bool start_ok = (pos == 0) || (!std::isalnum(static_cast<unsigned char>(type_name[pos - 1])) && type_name[pos - 1] != '_');
						bool end_ok = (pos + param_sv.size() >= type_name.size()) || (!std::isalnum(static_cast<unsigned char>(type_name[pos + param_sv.size()])) && type_name[pos + param_sv.size()] != '_');
						if (start_ok && end_ok) {
							return true;
						}
						// For mangled template names (like "is_integral__Tp"), underscore is a valid separator
						// Allow matching when the param starts with _ and is preceded by another _
						// e.g., "__Tp" in "is_integral__Tp" where param is "_Tp"
						if (is_mangled_name && pos > 0 && type_name[pos - 1] == '_' && param_sv[0] == '_') {
							// Check end boundary (must be end of string or followed by underscore/non-alnum)
							bool relaxed_end_ok = (pos + param_sv.size() >= type_name.size()) ||
							                      (type_name[pos + param_sv.size()] == '_') ||
							                      (!std::isalnum(static_cast<unsigned char>(type_name[pos + param_sv.size()])));
							if (relaxed_end_ok) {
								return true;
							}
						}
						pos = type_name.find(param_sv, pos + 1);
					}
				}
				return false;
			};
			
			for (const auto& arg : template_args) {
				if (arg.is_dependent) {
					has_dependent_args = true;
					break;
				}
				// Also check if the type name contains any template parameter names
				// This catches cases like is_integral<T> where is_dependent might not be set
				// but the type name contains "T"
				if (arg.base_type == Type::Struct || arg.base_type == Type::UserDefined) {
					if (arg.type_index < gTypeInfo.size()) {
						StringHandle type_name_handle = gTypeInfo[arg.type_index].name();
						FLASH_LOG_FORMAT(Templates, Debug, "Checking base class arg: type={}, type_index={}, name='{}'", 
						                 static_cast<int>(arg.base_type), arg.type_index, StringTable::getStringView(type_name_handle));
						if (contains_template_param(type_name_handle)) {
							FLASH_LOG_FORMAT(Templates, Debug, "Base class arg '{}' contains template parameter - marking as dependent", StringTable::getStringView(type_name_handle));
							has_dependent_args = true;
							break;
						}
					}
				}
			}
			
			// Also check the AST nodes for template arguments - they may contain
			// TemplateParameterReferenceNode which indicates dependent types
			if (!has_dependent_args && parsing_template_body_) {
				for (const auto& arg_node : template_arg_nodes) {
					if (arg_node.is<TypeSpecifierNode>()) {
						const auto& type_spec = arg_node.as<TypeSpecifierNode>();
						// Check if the type name contains template parameters
						if (type_spec.type_index() < gTypeInfo.size()) {
							StringHandle type_name_handle = gTypeInfo[type_spec.type_index()].name();
							// Check if this type is a template (has nested template args)
							// If it's a template class and we're inside a template body, 
							// and it was registered with the same name as the primary template,
							// it might be a dependent instantiation that was skipped
							auto template_entry = gTemplateRegistry.lookupTemplate(type_name_handle);
							if (template_entry.has_value()) {
								FLASH_LOG_FORMAT(Templates, Debug, "Base class arg '{}' is a template class in template body - marking as dependent", StringTable::getStringView(type_name_handle));
								has_dependent_args = true;
								break;
							}
						}
					}
				}
			}
			
			// If template arguments are dependent, we're inside a template declaration
			// Don't try to instantiate or resolve the base class yet
			if (has_dependent_args) {
				FLASH_LOG_FORMAT(Templates, Debug, "Base class {} has dependent template arguments - deferring resolution", base_class_name);
				
				std::vector<TemplateArgumentNodeInfo> arg_infos;
				arg_infos.reserve(template_args.size());
				for (size_t i = 0; i < template_args.size(); ++i) {
					TemplateArgumentNodeInfo info;
					info.is_pack = template_args[i].is_pack;
					info.is_dependent = template_args[i].is_dependent;
					if (i < template_arg_nodes.size()) {
						info.node = template_arg_nodes[i];
					}
					arg_infos.push_back(std::move(info));
				}
				
				StringHandle template_name_handle = StringTable::getOrInternStringHandle(base_class_name);
				struct_ref.add_deferred_template_base_class(template_name_handle, std::move(arg_infos), member_type_name, base_access, is_virtual_base);
				
				continue;  // Skip to next base class or exit loop
			}
			
			// Instantiate base class template if needed and register in AST
			// Note: try_instantiate_class_template returns nullopt on success 
			// (type is registered in gTypesByName)
			instantiated_base_name = instantiate_and_register_base_template(base_class_name, template_args);
			
			// Resolve member type alias if present (e.g., Base<T>::type)
			if (member_type_name.has_value()) {
				std::string_view member_name = StringTable::getStringView(*member_type_name);
				
				// First try direct lookup
				StringBuilder qualified_builder;
				qualified_builder.append(base_class_name);
				qualified_builder.append("::"sv);
				qualified_builder.append(member_name);
				std::string_view alias_name = qualified_builder.commit();
				
				const TypeInfo* alias_type_info = nullptr;
				auto alias_it = gTypesByName.find(StringTable::getOrInternStringHandle(alias_name));
				if (alias_it == gTypesByName.end()) {
					// Try looking up through inheritance (e.g., wrapper<true_type>::type where type is inherited)
					alias_type_info = lookup_inherited_type_alias(base_class_name, member_name);
					if (alias_type_info == nullptr) {
						return ParseResult::error("Base class '" + std::string(alias_name) + "' not found", *member_name_token);
					}
					FLASH_LOG_FORMAT(Templates, Debug, "Found inherited member alias: {}", StringTable::getStringView(alias_type_info->name()));
				} else {
					alias_type_info = alias_it->second;
					FLASH_LOG_FORMAT(Templates, Debug, "Found direct member alias: {}", alias_name);
				}
				
				// Resolve the type alias to its underlying type
				// Type aliases have a type_index that points to the actual struct/class
				const TypeInfo* resolved_type = alias_type_info;
				size_t max_alias_depth = 10;  // Prevent infinite loops
				while (resolved_type->type_index_ < gTypeInfo.size() && max_alias_depth-- > 0) {
					const TypeInfo& underlying = gTypeInfo[resolved_type->type_index_];
					// Stop if we're pointing to ourselves (not a valid alias)
					if (&underlying == resolved_type) break;
					
					FLASH_LOG_FORMAT(Templates, Debug, "Resolving type alias '{}' -> underlying type_index={}, type={}", 
					                 StringTable::getStringView(resolved_type->name()), 
					                 resolved_type->type_index_, 
					                 static_cast<int>(underlying.type_));
					
					resolved_type = &underlying;
					// If we've reached a concrete struct type, we're done
					if (underlying.type_ == Type::Struct) break;
				}
				
				// Use the resolved underlying type name as the base class
				base_class_name = StringTable::getStringView(resolved_type->name());
				FLASH_LOG_FORMAT(Templates, Debug, "Resolved member alias base to underlying type: {}", base_class_name);
				
				if (member_name_token.has_value()) {
					base_name_token = *member_name_token;
				}
			}
		}

		// Validate and add the base class
		ParseResult result = validate_and_add_base_class(base_class_name, struct_ref, struct_info.get(), base_access, is_virtual_base, base_name_token);
		if (result.is_error()) {
			return result;
		}
	} while (peek() == ","_tok && (advance(), true));
	}

	// Check for 'final' keyword (after class/struct name or base class list)
	if (peek() == "final"_tok) {
		advance();  // consume 'final'
		struct_ref.set_is_final(true);
		struct_info->is_final = true;
	}

	// Check for forward declaration (struct Name;)
	if (!peek().is_eof()) {
		if (peek() == ";"_tok) {
			// Forward declaration - just register the type and return
			advance(); // consume ';'
			struct_ref.set_is_forward_declaration(true);
			return saved_position.success(struct_node);
		}
	}

	// Expect opening brace for full definition
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' or ';' after struct/class name or base class list", peek_info());
	}

	// Default access specifier (public for struct, private for class)
	AccessSpecifier current_access = struct_ref.default_access();

	// Parse members
	while (!peek().is_eof() && peek() != "}"_tok) {
		// Skip empty declarations (bare ';' tokens) - valid in C++
		if (peek() == ";"_tok) {
			advance();
			continue;
		}
		
		// Skip C++ attributes like [[nodiscard]], [[maybe_unused]], etc.
		// These can appear on member declarations, conversion operators, etc.
		skip_cpp_attributes();
		
		// Check for access specifier
		if (peek().is_keyword()) {
			std::string_view keyword = peek_info().value();
			if (keyword == "public" || keyword == "protected" || keyword == "private") {
				advance();
				if (!consume(":"_tok)) {
					return ParseResult::error("Expected ':' after access specifier", peek_info());
				}

				// Update current access level
				if (keyword == "public") {
					current_access = AccessSpecifier::Public;
				} else if (keyword == "protected") {
					current_access = AccessSpecifier::Protected;
				} else if (keyword == "private") {
					current_access = AccessSpecifier::Private;
				}
				continue;
			}

			// Check for 'template' keyword - could be member function template or member template alias
			if (keyword == "template") {
				auto template_result = parse_member_template_or_function(struct_ref, current_access);
				if (template_result.is_error()) {
					return template_result;
				}
				continue;
			}

			// Check for 'static_assert' keyword
			if (keyword == "static_assert") {
				auto static_assert_result = parse_static_assert();
				if (static_assert_result.is_error()) {
					return static_assert_result;
				}
				continue;
			}

			// Check for 'enum' keyword - nested enum declaration
			if (keyword == "enum") {
				auto enum_result = parse_enum_declaration();
				if (enum_result.is_error()) {
					return enum_result;
				}
				// Track the enum's TypeIndex in the struct for nested enum enumerator lookup during codegen
				if (auto enum_node = enum_result.node(); enum_node.has_value() && enum_node->is<EnumDeclarationNode>()) {
					const auto& enum_decl = enum_node->as<EnumDeclarationNode>();
					auto enum_it = gTypesByName.find(StringTable::getOrInternStringHandle(enum_decl.name()));
					if (enum_it != gTypesByName.end()) {
						struct_info->addNestedEnumIndex(enum_it->second->type_index_);
					}
				}
				// The semicolon is already consumed by parse_enum_declaration
				continue;
			}

			// Check for 'using' keyword - type alias
			if (keyword == "using") {
				auto alias_result = parse_member_type_alias("using", &struct_ref, current_access);
				if (alias_result.is_error()) {
					return alias_result;
				}
				continue;
			}

			// Check for 'typedef' keyword - type alias (C-style)
			if (keyword == "typedef") {
				auto alias_result = parse_member_type_alias("typedef", &struct_ref, current_access);
				if (alias_result.is_error()) {
					return alias_result;
				}
				continue;
			}

			// Check for nested class/struct/union declaration or anonymous union
			if (keyword == "class" || keyword == "struct" || keyword == "union") {
				// Peek ahead to determine if this is:
				// 1. Anonymous struct/union: struct { ... };
				// 2. Nested struct declaration: struct Name { ... };
				// 3. Member with struct type: struct Name member; or struct Name *ptr;
				SaveHandle saved_pos = save_token_position();
				auto union_or_struct_keyword = advance(); // consume 'struct', 'class', or 'union'
				bool is_union_keyword = (union_or_struct_keyword.value() == "union");
				
				// Skip attributes between struct/union keyword and opening brace (for anonymous structs)
				// e.g., struct __attribute__((__aligned__)) { } member;
				skip_cpp_attributes();
				
				if (peek() == "{"_tok) {
					// Pattern 1: Anonymous union/struct or named anonymous union/struct as a member
					
					// Save the position before the opening brace
					SaveHandle brace_start_pos = save_token_position();
					
					// Peek ahead to check if this is:
					// - True anonymous union/struct: struct { ... };
					// - Named anonymous union/struct: struct { ... } member_name;
					// Skip to the closing brace and check what follows
					skip_balanced_braces();
					bool is_named_anonymous = false;
					if (peek().is_identifier()) {
						is_named_anonymous = true;
					}
					
					// Restore position to the opening brace to parse the members
					restore_token_position(brace_start_pos);
					
					// Now consume the opening brace
					advance(); // consume '{'
					
					if (is_named_anonymous) {
						// Named anonymous struct/union: struct { int x; } member_name;
						// Create an anonymous type and parse members into it
						
						// Generate a unique name for the anonymous struct/union type
						static int anonymous_type_counter = 0;
						std::string_view anon_type_name = StringBuilder()
							.append("__anonymous_")
							.append(is_union_keyword ? "union_" : "struct_")
							.append(static_cast<int64_t>(anonymous_type_counter++))
							.commit();
						StringHandle anon_type_name_handle = StringTable::getOrInternStringHandle(anon_type_name);
						
						// Create the anonymous struct/union type
						TypeInfo& anon_type_info = add_struct_type(anon_type_name_handle);
						
						// Create StructTypeInfo
						auto anon_struct_info_ptr = std::make_unique<StructTypeInfo>(anon_type_name_handle, AccessSpecifier::Public);
						StructTypeInfo* anon_struct_info = anon_struct_info_ptr.get();
						
						// Set the union flag if this is a union
						if (is_union_keyword) {
							anon_struct_info->is_union = true;
						}
						
						// Parse all members of the anonymous struct/union and add them to the anonymous type
						while (!peek().is_eof() && peek() != "}"_tok) {
							// Parse member type
							auto member_type_result = parse_type_specifier();
							if (member_type_result.is_error()) {
								return member_type_result;
							}
							
							if (!member_type_result.node().has_value()) {
								return ParseResult::error("Expected type specifier in named anonymous struct/union", current_token_);
							}
							
							// Handle pointer declarators
							TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
							while (peek() == "*"_tok) {
								advance(); // consume '*'
								CVQualifier ptr_cv = parse_cv_qualifiers();
								member_type_spec.add_pointer_level(ptr_cv);
							}
							
							// Check for function pointer member pattern: type (*name)(params);
							// This handles patterns like: void (*sa_sigaction)(int, siginfo_t *, void *);
							if (auto funcptr_member = try_parse_function_pointer_member()) {
								anon_struct_info->members.push_back(*funcptr_member);
								continue;  // Continue with next member
							}
							
							// Parse member name
							auto member_name_token = peek_info();
							if (!member_name_token.kind().is_identifier()) {
								return ParseResult::error("Expected member name in named anonymous struct/union", member_name_token);
							}
							advance(); // consume the member name
							
							// Calculate member size and alignment
							auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
							
							// Add member to the anonymous type
							StringHandle member_name_handle = member_name_token.handle();
							anon_struct_info->members.push_back(StructMember{
								member_name_handle,
								member_type_spec.type(),
								member_type_spec.type_index(),
								0,  // offset will be calculated below
								member_size,
								member_alignment,
								AccessSpecifier::Public,
								std::nullopt,  // no default initializer
								false,  // is_reference
								false,  // is_rvalue_reference
								0,      // referenced_size_bits
								false,  // is_array
								{},     // array_dimensions
								0,      // pointer_depth
								std::nullopt // bitfield_width
							});
							
							// Expect semicolon
							if (!consume(";"_tok)) {
								return ParseResult::error("Expected ';' after member in named anonymous struct/union", current_token_);
							}
						}
						
						// Expect closing brace
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after named anonymous struct/union members", peek_info());
						}
						
						// Calculate the layout for the anonymous type
						if (is_union_keyword) {
							// Union layout: all members at offset 0, size is max of all member sizes
							size_t max_size = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								member.offset = 0;  // All union members at offset 0
								if (member.size > max_size) {
									max_size = member.size;
								}
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							anon_struct_info->total_size = max_size;
							anon_struct_info->alignment = max_alignment;
						} else {
							// Struct layout: members are laid out sequentially
							size_t offset = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								// Align the offset
								if (member.alignment > 0) {
									offset = (offset + member.alignment - 1) / member.alignment * member.alignment;
								}
								member.offset = offset;
								offset += member.size;
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							// Add padding to align the struct size
							if (max_alignment > 0) {
								offset = (offset + max_alignment - 1) / max_alignment * max_alignment;
							}
							anon_struct_info->total_size = offset;
							anon_struct_info->alignment = max_alignment;
						}
						
						// Set the StructTypeInfo for the anonymous type
						anon_type_info.setStructInfo(std::move(anon_struct_info_ptr));
						
						// Now parse the member declarators (one or more identifiers separated by commas)
						do {
							// Parse variable name
							auto var_name_token = advance();
							if (!var_name_token.kind().is_identifier()) {
								return ParseResult::error("Expected identifier for named anonymous struct/union member", current_token_);
							}
							
							// Create a TypeSpecifierNode for the anonymous type
							TypeSpecifierNode anon_type_spec(
								Type::Struct,
								anon_type_info.type_index_,
								static_cast<unsigned char>(anon_struct_info->total_size),
								Token(Token::Type::Identifier, StringTable::getStringView(anon_type_name_handle), 0, 0, 0)
							);
							
							// Create a member with the anonymous type
							auto anon_type_spec_node = emplace_node<TypeSpecifierNode>(anon_type_spec);
							auto member_decl = emplace_node<DeclarationNode>(anon_type_spec_node, var_name_token);
							
							// Add the member to the struct
							struct_ref.add_member(member_decl, current_access, std::nullopt);
							
						} while (peek() == ","_tok && (advance(), true));
						
						// Expect semicolon after the member declarations
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after named anonymous struct/union member", current_token_);
						}
						
						discard_saved_token(saved_pos);
						discard_saved_token(brace_start_pos);
						continue;  // Skip to next member
					}
					
					// True anonymous union/struct: struct { ... };
					// Store the union info for processing during layout phase
					
					// Mark the position where this anonymous union appears in the member list
					size_t union_marker_index = struct_ref.members().size();
					struct_ref.add_anonymous_union_marker(union_marker_index, is_union_keyword);
					
					// Parse all members of the anonymous union and store their info
					while (!peek().is_eof() && peek() != "}"_tok) {
						// Check for nested anonymous union
						if (peek().is_keyword() &&
						    (peek() == "union"_tok || peek() == "struct"_tok)) {
							SaveHandle nested_saved_pos = save_token_position();
							advance(); // consume 'union' or 'struct'
							
							if (peek() == "{"_tok) {
								// Nested anonymous union - parse recursively
								advance(); // consume '{'
								
								// Parse nested anonymous union members
								while (!peek().is_eof() && peek() != "}"_tok) {
									// Parse member type
									auto nested_member_type_result = parse_type_specifier();
									if (nested_member_type_result.is_error()) {
										return nested_member_type_result;
									}
									
									if (!nested_member_type_result.node().has_value()) {
										return ParseResult::error("Expected type specifier in nested anonymous union", current_token_);
									}
									
									// Handle pointer declarators
									TypeSpecifierNode& nested_member_type_spec = nested_member_type_result.node()->as<TypeSpecifierNode>();
									while (peek() == "*"_tok) {
										advance(); // consume '*'
										CVQualifier ptr_cv = parse_cv_qualifiers();
										nested_member_type_spec.add_pointer_level(ptr_cv);
									}
									
									// Parse member name
									auto nested_member_name_token = peek_info();
									if (!nested_member_name_token.kind().is_identifier()) {
										return ParseResult::error("Expected member name in nested anonymous union", nested_member_name_token);
									}
									advance(); // consume the member name
									
									// Check for array declarator
									std::vector<ASTNode> nested_array_dimensions;
									while (peek() == "["_tok) {
										advance(); // consume '['
										
										// Parse the array size expression
										ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (size_result.is_error()) {
											return size_result;
										}
										nested_array_dimensions.push_back(*size_result.node());
										
										// Expect closing ']'
										if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
										    peek() != "]"_tok) {
											return ParseResult::error("Expected ']' after array size", current_token_);
										}
										advance(); // consume ']'
									}
									
									// Create member declaration for nested union member
									ASTNode nested_member_decl_node;
									if (!nested_array_dimensions.empty()) {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token, std::move(nested_array_dimensions));
									} else {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token);
									}
									// Flatten nested union members into outer union/struct
									struct_ref.add_member(nested_member_decl_node, current_access, std::nullopt);
									
									// Expect semicolon
									if (!consume(";"_tok)) {
										return ParseResult::error("Expected ';' after nested anonymous union member", current_token_);
									}
								}
								
								// Expect closing brace for nested union
								if (!consume("}"_tok)) {
									return ParseResult::error("Expected '}' after nested anonymous union members", peek_info());
								}
								
								// Expect semicolon after nested anonymous union
								if (!consume(";"_tok)) {
									return ParseResult::error("Expected ';' after nested anonymous union", current_token_);
								}
								
								discard_saved_token(nested_saved_pos);
								continue; // Continue with next member of outer union
							} else {
								// Named union/struct - restore position and parse normally
								restore_token_position(nested_saved_pos);
							}
						}
						
						// Parse member type
						auto anon_member_type_result = parse_type_specifier();
						if (anon_member_type_result.is_error()) {
							return anon_member_type_result;
						}
						
						if (!anon_member_type_result.node().has_value()) {
							return ParseResult::error("Expected type specifier in anonymous union", current_token_);
						}
						
						// Handle pointer declarators
						TypeSpecifierNode& anon_member_type_spec = anon_member_type_result.node()->as<TypeSpecifierNode>();
						while (peek() == "*"_tok) {
							advance(); // consume '*'
							CVQualifier ptr_cv = parse_cv_qualifiers();
							anon_member_type_spec.add_pointer_level(ptr_cv);
						}
						
						// Parse member name (allow unnamed bitfields: int : 0;)
						auto anon_member_name_token = peek_info();
						if (anon_member_name_token.kind().is_identifier()) {
							advance(); // consume the member name
						} else if (peek() == ":"_tok) {
							anon_member_name_token = Token(
								Token::Type::Identifier,
								""sv,
								current_token_.line(),
								current_token_.column(),
								current_token_.file_index()
							);
						} else {
							return ParseResult::error("Expected member name in anonymous union", anon_member_name_token);
						}
						
						// Check for array declarator
						std::vector<ASTNode> anon_array_dimensions;
						while (peek() == "["_tok) {
							advance(); // consume '['
							
							// Parse the array size expression
							ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (size_result.is_error()) {
								return size_result;
							}
							anon_array_dimensions.push_back(*size_result.node());
							
							// Expect closing ']'
							if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
							    peek() != "]"_tok) {
								return ParseResult::error("Expected ']' after array size", current_token_);
							}
							advance(); // consume ']'
						}
						
						std::optional<size_t> bitfield_width;
						if (peek() == ":"_tok) {
							advance(); // consume ':'
							auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
							if (width_result.is_error()) {
								return width_result;
							}
							if (width_result.node().has_value()) {
								ConstExpr::EvaluationContext ctx(gSymbolTable);
								auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
								if (!eval_result.success() || eval_result.as_int() < 0) {
									return ParseResult::error("Bitfield width must be a non-negative integral constant expression", current_token_);
								}
								bitfield_width = static_cast<size_t>(eval_result.as_int());
							}
						}

						// Calculate member size and alignment
						auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(anon_member_type_spec);
						size_t referenced_size_bits = anon_member_type_spec.size_in_bits();
						if (bitfield_width.has_value() && *bitfield_width == 0) {
							// Zero-width bitfields in anonymous unions are layout directives:
							// they don't contribute storage and should not raise union alignment.
							member_size = 0;
							member_alignment = 1;
						}
						
						// For struct types, get size and alignment from the struct type info
						if (anon_member_type_spec.type() == Type::Struct && !anon_member_type_spec.is_pointer() && !anon_member_type_spec.is_reference()) {
							const TypeInfo* member_type_info = nullptr;
							for (const auto& ti : gTypeInfo) {
								if (ti.type_index_ == anon_member_type_spec.type_index()) {
									member_type_info = &ti;
									break;
								}
							}
							if (member_type_info && member_type_info->getStructInfo()) {
								member_size = member_type_info->getStructInfo()->total_size;
								referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
								member_alignment = member_type_info->getStructInfo()->alignment;
							}
						}
						
						// For array members, multiply element size by array count and collect dimensions
						bool is_array = false;
						std::vector<size_t> array_dimensions;
						if (!anon_array_dimensions.empty()) {
							is_array = true;
							for (const auto& dim_expr : anon_array_dimensions) {
								ConstExpr::EvaluationContext ctx(gSymbolTable);
								auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
								if (eval_result.success() && eval_result.as_int() > 0) {
									size_t dim_size = static_cast<size_t>(eval_result.as_int());
									array_dimensions.push_back(dim_size);
									member_size *= dim_size;
									referenced_size_bits *= dim_size;
								}
							}
						}
						
						// Store the anonymous union member info for later processing during layout
						bool is_ref_member = anon_member_type_spec.is_reference();
						bool is_rvalue_ref_member = anon_member_type_spec.is_rvalue_reference();
						if (is_ref_member) {
							referenced_size_bits = referenced_size_bits ? referenced_size_bits : (anon_member_type_spec.size_in_bits());
						}
						
						StringHandle member_name_handle = anon_member_name_token.handle();
						struct_ref.add_anonymous_union_member(
							member_name_handle,
							anon_member_type_spec.type(),
							anon_member_type_spec.type_index(),
							member_size,
							member_alignment,
							bitfield_width,
							referenced_size_bits,
							is_ref_member,
							is_rvalue_ref_member,
							is_array,
							static_cast<int>(anon_member_type_spec.pointer_depth()),
							std::move(array_dimensions)
						);
						
						// Add DeclarationNode to struct_ref for symbol table and AST purposes
						// During layout phase, these will be skipped (already processed as union members)
						ASTNode anon_member_decl_node;
						if (!anon_array_dimensions.empty()) {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token, std::move(anon_array_dimensions));
						} else {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token);
						}
						struct_ref.add_member(anon_member_decl_node, AccessSpecifier::Public, std::nullopt, bitfield_width);
						
						// Expect semicolon
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after anonymous union member", current_token_);
						}
					}
					
					// Expect closing brace
					if (!consume("}"_tok)) {
						return ParseResult::error("Expected '}' after anonymous union members", peek_info());
					}
					
					// Expect semicolon after true anonymous union
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after anonymous union", current_token_);
					}
					
					discard_saved_token(saved_pos);
					continue;  // Skip to next member
				} else if (peek().is_identifier()) {
					// Could be pattern 2 or 3
					advance(); // consume the identifier (struct name)
					
					// Pattern 2: Nested struct declaration
					// Check for '{' (body), ';' (forward declaration), or ':' (base class)
					if (!peek().is_eof() && (peek() == "{"_tok || 
					                                  peek() == ";"_tok ||
					                                  peek() == ":"_tok)) {
						// Pattern 2: Nested struct declaration (with or without base class)
						restore_token_position(saved_pos);
						
						// Save the parent's delayed function bodies before parsing nested struct
						// This prevents the nested struct's parse_struct_declaration() from trying
						// to parse the parent's delayed bodies
						auto saved_delayed_bodies = std::move(delayed_function_bodies_);
						delayed_function_bodies_.clear();
						
						auto nested_result = parse_struct_declaration();
						
						// Restore the parent's delayed function bodies after nested struct is complete
						// Any delayed bodies from the nested struct have already been parsed
						delayed_function_bodies_ = std::move(saved_delayed_bodies);
						
						if (nested_result.is_error()) {
							return nested_result;
						}

						if (auto nested_node = nested_result.node()) {
							// Set enclosing class relationship
							auto& nested_struct = nested_node->as<StructDeclarationNode>();
							nested_struct.set_enclosing_class(&struct_ref);

							// Add to outer class
							struct_ref.add_nested_class(*nested_node);

							// Update type info - use qualified name to avoid ambiguity
							std::string_view qualified_nested_name = StringBuilder()
								.append(qualified_struct_name)
								.append("::")
								.append(nested_struct.name())
								.commit();
							auto nested_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_nested_name));
							if (nested_type_it != gTypesByName.end()) {
								const StructTypeInfo* nested_info_const = nested_type_it->second->getStructInfo();
								if (nested_info_const) {
									StructTypeInfo* nested_info = const_cast<StructTypeInfo*>(nested_info_const);
									struct_info->addNestedClass(nested_info);
								}

								auto qualified_name = StringTable::getOrInternStringHandle(qualified_nested_name);
								if (gTypesByName.find(qualified_name) == gTypesByName.end()) {
									gTypesByName.emplace(qualified_name, nested_type_it->second);
								}
							}
							
							// Handle any variable declarators parsed after the nested declaration
							// e.g., "union Data { ... } data;" - the "data" member should be added
							for (auto& var_node : pending_struct_variables_) {
								// Extract the declaration node from the VariableDeclarationNode wrapper
								auto& var_decl_node = var_node.as<VariableDeclarationNode>();
								auto decl_node = var_decl_node.declaration_node();
								
								// Add as a member of the outer struct
								struct_ref.add_member(decl_node, current_access, std::nullopt);
							}
							pending_struct_variables_.clear();
						}

						continue;  // Skip to next member
					} else {
						// Pattern 3: Member with struct type (struct Name member; or struct Name *ptr;)
						// Restore and let normal member parsing handle it
						restore_token_position(saved_pos);
					}
				} else {
					// Not a nested declaration, restore and let normal parsing handle it
					restore_token_position(saved_pos);
				}
			}
		}

		// Check for constexpr, consteval, inline, explicit specifiers (can appear on constructors and member functions)
		// This also handles cases where specifiers precede 'static' or 'friend' in any order,
		// e.g., "constexpr static int x = 42;" or "inline friend void foo() {}"
		auto member_specs = parse_member_leading_specifiers();

		// Check for 'friend' keyword - may appear after specifiers like constexpr/inline
		if (peek() == "friend"_tok) {
			auto friend_result = parse_friend_declaration();
			if (friend_result.is_error()) {
				return friend_result;
			}

			// Add friend declaration to struct
			if (auto friend_node = friend_result.node()) {
				struct_ref.add_friend(*friend_node);

				// Add to StructTypeInfo
				const auto& friend_decl = friend_node->as<FriendDeclarationNode>();
				if (friend_decl.kind() == FriendKind::Class) {
					StringHandle friend_class_name_handle = friend_decl.name();
					struct_info->addFriendClass(friend_class_name_handle);
				} else if (friend_decl.kind() == FriendKind::Function) {
					StringHandle friend_func_name_handle = friend_decl.name();
					struct_info->addFriendFunction(friend_func_name_handle);
				} else if (friend_decl.kind() == FriendKind::MemberFunction) {
					StringHandle friend_class_name_handle = friend_decl.class_name();
					StringHandle friend_func_name_handle = friend_decl.name();
					struct_info->addFriendMemberFunction(
						friend_class_name_handle,
						friend_func_name_handle);
				}
			}

			continue;  // Skip to next member
		}

		// Check for 'static' keyword - may appear after specifiers like constexpr/inline
		if (peek() == "static"_tok) {
			advance(); // consume 'static'
			
			// Check if it's const or constexpr (some may already be consumed by parse_member_leading_specifiers)
			bool is_const = false;
			bool is_static_constexpr = !!(member_specs & FlashCpp::MLS_Constexpr);
			while (peek().is_keyword()) {
				std::string_view kw = peek_info().value();
				if (kw == "const") {
					is_const = true;
					advance();
				} else if (kw == "constexpr") {
					is_static_constexpr = true;
					advance();
				} else if (kw == "inline") {
					advance(); // consume 'inline'
				} else {
					break;
				}
			}
			
			// Parse type and name
			auto type_and_name_result = parse_type_and_name();
			if (type_and_name_result.is_error()) {
				return type_and_name_result;
			}
			
			// Check if this is a static member function (has '(')
			if (parse_static_member_function(type_and_name_result, is_static_constexpr,
			                                   qualified_struct_name, struct_ref, struct_info.get(),
			                                   current_access, current_template_param_names_)) {
				// Function was handled (or error occurred)
				if (type_and_name_result.is_error()) {
					return type_and_name_result;
				}
				continue;
			}
			
			// Check for initialization (static data member)
			std::optional<ASTNode> init_expr_opt;
			if (peek() == "="_tok) {
				advance(); // consume '='
				
				// Push struct context so static member references can be resolved
				// This enables expressions like `!is_signed` to find `is_signed` as a static member
				size_t struct_type_index = 0;
				auto type_it = gTypesByName.find(qualified_struct_name);
				if (type_it != gTypesByName.end()) {
					struct_type_index = type_it->second->type_index_;
				}
				member_function_context_stack_.push_back({qualified_struct_name, struct_type_index, &struct_ref, struct_info.get()});
				
				// Parse the initializer expression
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				
				// Pop context after parsing
				member_function_context_stack_.pop_back();
				
				if (init_result.is_error()) {
					return init_result;
				}
				init_expr_opt = init_result.node();
			} else if (peek() == "{"_tok) {
				// Brace initialization: static constexpr int x{42};
				advance(); // consume '{'
				
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (init_result.is_error()) {
					return init_result;
				}
				init_expr_opt = init_result.node();
				
				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after brace initializer", current_token_);
				}
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after static member declaration", current_token_);
			}

			// Get the declaration and type specifier
			if (!type_and_name_result.node().has_value()) {
				return ParseResult::error("Expected static member declaration", current_token_);
			}
			const DeclarationNode& decl = type_and_name_result.node()->as<DeclarationNode>();
			const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

			// Register static member in struct info
			// Calculate size and alignment for the static member
			size_t static_member_size = get_type_size_bits(type_spec.type()) / 8;
			size_t static_member_alignment = get_type_alignment(type_spec.type(), static_member_size);

			// Add to struct's static members
			StringHandle static_member_name_handle = decl.identifier_token().handle();
			struct_info->addStaticMember(
				static_member_name_handle,
				type_spec.type(),
				type_spec.type_index(),
				static_member_size,
				static_member_alignment,
				current_access,
				init_expr_opt,  // initializer
				is_const
			);

			continue;
		}

		// Check for constructor (identifier matching struct name followed by '(')
		// Save position BEFORE checking to allow restoration if not a constructor
		SaveHandle saved_pos = save_token_position();
		if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
		    peek_info().value() == struct_name) {
			// Look ahead to see if this is a constructor (next token is '(')
			// We need to consume the struct name token and check the next token
			auto name_token_opt = advance();
			Token ctor_name_token = name_token_opt;  // Copy the token to keep it alive
			std::string_view ctor_name = ctor_name_token.value();  // Get the string_view from the token

			if (peek() == "("_tok) {
				// Discard saved position since we're using this as a constructor
				discard_saved_token(saved_pos);
				// This is a constructor
				// Use qualified_struct_name for nested classes so the member function references the correct type
				auto [ctor_node, ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(qualified_struct_name, StringTable::getOrInternStringHandle(ctor_name));

				// Parse parameters using unified parameter list parsing (Phase 1)
				FlashCpp::ParsedParameterList params;
				auto param_result = parse_parameter_list(params);
				if (param_result.is_error()) {
					return param_result;
				}

				// Apply parsed parameters to the constructor
				for (const auto& param : params.parameters) {
					ctor_ref.add_parameter_node(param);
				}
				// Note: Variadic constructors are extremely rare in C++ and not commonly used.
				// The AST node type doesn't currently track variadic status for constructors.
				// For now, we silently accept them without storing the variadic flag.
				// If variadic constructor support becomes needed, ConstructorDeclarationNode
				// can be extended with set_is_variadic() similar to FunctionDeclarationNode.

				// Apply specifiers from member_specs
				ctor_ref.set_explicit(member_specs & FlashCpp::MLS_Explicit);
				ctor_ref.set_constexpr(member_specs & FlashCpp::MLS_Constexpr);

				// Enter a temporary scope for parsing the initializer list (Phase 3: RAII)
				// This allows the initializer expressions to reference the constructor parameters
				FlashCpp::SymbolTableScope ctor_scope(ScopeType::Function);

				// Add parameters to symbol table so they can be referenced in the initializer list
				for (const auto& param : ctor_ref.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl_node = param.as<DeclarationNode>();
						const Token& param_token = param_decl_node.identifier_token();
						gSymbolTable.insert(param_token.value(), param);
					}
				}

				// Parse exception specifier (noexcept or throw()) before initializer list
				if (parse_constructor_exception_specifier()) {
					ctor_ref.set_noexcept(true);
				}

				// Handle trailing requires clause: pair() requires constraint : first(), second() { }
				// Skip the constraint expression (we don't enforce constraints yet, but need to parse them)
				if (peek() == "requires"_tok) {
					advance(); // consume 'requires'
					
					// Skip the constraint expression by counting balanced brackets/parens
					// The constraint expression ends before ':', '{', or ';'
					int paren_depth = 0;
					int angle_depth = 0;
					while (!peek().is_eof()) {
						std::string_view tok_val = peek_info().value();
						
						// Track nested brackets
						if (tok_val == "(") paren_depth++;
						else if (tok_val == ")") paren_depth--;
						else update_angle_depth(tok_val, angle_depth);
						
						// At top level, check for end of constraint
						if (paren_depth == 0 && angle_depth == 0) {
							// Initializer list, body, or declaration end
							if (tok_val == ":" || tok_val == "{" || tok_val == ";") {
								break;
							}
						}
						
						advance();
					}
				}

				// Skip GCC __attribute__ between exception specifier and initializer list
				// e.g. polymorphic_allocator(memory_resource* __r) noexcept __attribute__((__nonnull__)) : _M_resource(__r) { }
				skip_gcc_attributes();

				// Check for member initializer list (: Base(args), member(value), ...)
				// For delayed parsing, save the position and skip it
				SaveHandle initializer_list_start;
				bool has_initializer_list = false;
				if (peek() == ":"_tok) {
					// Save position before consuming ':'
					initializer_list_start = save_token_position();
					has_initializer_list = true;
					
					advance();  // consume ':'

					// Skip initializers until we hit '{' or ';' by counting parentheses/braces
					while (!peek().is_eof() &&
					       peek() != "{"_tok &&
					       peek() != ";"_tok) {
						// Skip initializer name
						advance();
						
						// Skip template arguments if present: Base<T>(...)
						if (peek() == "<"_tok) {
							skip_template_arguments();
						}
						
						// Expect '(' or '{'
						if (peek() == "("_tok) {
							skip_balanced_parens();
						} else if (peek() == "{"_tok) {
							skip_balanced_braces();
						} else {
							return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
						}
						
						// Check for comma (more initializers) or '{'/';' (end of initializer list)
						if (peek() == ","_tok) {
							advance();  // consume ','
						} else {
							// No comma, so we expect '{' or ';' next
							break;
						}
					}
				}

				// Check for = default or = delete
				bool is_defaulted = false;
				bool is_deleted = false;
				if (peek() == "="_tok) {
					advance(); // consume '='

					if (peek().is_keyword()) {
						if (peek() == "default"_tok) {
							advance(); // consume 'default'
							is_defaulted = true;

							// Expect ';'
							if (!consume(";"_tok)) {
								// ctor_scope automatically exits scope on return
								return ParseResult::error("Expected ';' after '= default'", peek_info());
							}

							// Mark as implicit (same behavior as compiler-generated)
							ctor_ref.set_is_implicit(true);

							// Create an empty block for the constructor body
							auto [block_node, block_ref] = create_node_ref(BlockNode());
							// Generate mangled name for the constructor
							NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(ctor_ref);
							ctor_ref.set_mangled_name(mangled.view());
							ctor_ref.set_definition(block_node);

							// ctor_scope automatically exits scope when leaving this branch
						} else if (peek() == "delete"_tok) {
							advance(); // consume 'delete'
							is_deleted = true;

							// Expect ';'
							if (!consume(";"_tok)) {
								// ctor_scope automatically exits scope on return
								return ParseResult::error("Expected ';' after '= delete'", peek_info());
							}

							// Track deleted constructors to prevent their use
							// Determine what kind of constructor this is based on parameters:
							// - No params = default constructor
							// - 1 param of lvalue reference to same type = copy constructor
							// - 1 param of rvalue reference to same type = move constructor
							if (struct_info) {
								size_t num_params = params.parameters.size();
								bool is_copy_ctor = false;
								bool is_move_ctor = false;
								
								if (num_params == 1) {
									// Check if the parameter is a reference to this type
									const auto& param = params.parameters[0];
									if (param.is<DeclarationNode>()) {
										const auto& param_decl = param.as<DeclarationNode>();
										// Check if the type specifier matches the struct name
										const auto& type_node = param_decl.type_node();
										if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
											const auto& type_spec = type_node.as<TypeSpecifierNode>();
											std::string_view param_type_name = type_spec.token().value();
											if (param_type_name == struct_name || 
											    param_type_name == qualified_struct_name.view()) {
												// It's a reference to this type
												if (type_spec.is_rvalue_reference()) {
													is_move_ctor = true;
												} else if (type_spec.is_reference()) {
													is_copy_ctor = true;
												}
											}
										}
									}
								}
								
								struct_info->markConstructorDeleted(is_copy_ctor, is_move_ctor);
							}

							// ctor_scope automatically exits scope on continue
							continue; // Don't add deleted constructor to struct
						} else {
							// ctor_scope automatically exits scope on return
							return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
						}
					} else {
						// ctor_scope automatically exits scope on return
						return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
					}
				}

				// Parse constructor body if present (and not defaulted/deleted)
				if (!is_defaulted && !is_deleted && peek() == "{"_tok) {
					// DELAYED PARSING: Save the current position (start of '{')
					SaveHandle body_start = save_token_position();

					// Look up the struct type
					auto type_it = gTypesByName.find(struct_name);
					size_t struct_type_index = 0;
					if (type_it != gTypesByName.end()) {
						struct_type_index = type_it->second->type_index_;
					}

					// Skip over the constructor body by counting braces
					skip_balanced_braces();

					// Dismiss the RAII scope guard - we'll re-enter when parsing the delayed body
					ctor_scope.dismiss();
					gSymbolTable.exit_scope();

					// Record this for delayed parsing
					delayed_function_bodies_.push_back({
						nullptr,  // func_node (not used for constructors)
						body_start,
						initializer_list_start,  // Save position of initializer list
						struct_name,
						struct_type_index,
						&struct_ref,
						has_initializer_list,     // Flag if initializer list exists
						true,  // is_constructor
						false,  // is_destructor
						&ctor_ref,  // ctor_node
						nullptr,   // dtor_node
						{}  // template_param_names (empty for non-template constructors)
					});
				} else if (!is_defaulted && !is_deleted && !consume(";"_tok)) {
					// No constructor body - ctor_scope automatically exits scope on return
					return ParseResult::error("Expected '{', ';', '= default', or '= delete' after constructor declaration", peek_info());
				}
				// For all other cases, ctor_scope automatically exits scope at end of block

				// Add constructor to struct
				struct_ref.add_constructor(ctor_node, current_access);
				continue;
			} else {
				// Not a constructor, restore position and parse as normal member
				restore_token_position(saved_pos);
			}
		} else {
			// Token doesn't match struct name, discard saved position
			discard_saved_token(saved_pos);
		}

		// Check for 'virtual' keyword (for virtual destructors and virtual member functions)
		// parse_member_leading_specifiers() already consumed 'virtual' if present
		bool is_virtual = !!(member_specs & FlashCpp::MLS_Virtual);

		// Check for destructor (~StructName followed by '(')
		if (peek() == "~"_tok) {
			advance();  // consume '~'

			auto name_token_opt = advance();
			if (!name_token_opt.kind().is_identifier() ||
			    name_token_opt.value() != struct_name) {
				return ParseResult::error("Expected struct name after '~' in destructor", name_token_opt);
			}
			Token dtor_name_token = name_token_opt;  // Copy the token to keep it alive
			std::string_view dtor_name = dtor_name_token.value();  // Get the string_view from the token

			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after destructor name", peek_info());
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Destructor cannot have parameters", peek_info());
			}

			// Use qualified_struct_name for nested classes so the member function references the correct type
			auto [dtor_node, dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(qualified_struct_name, StringTable::getOrInternStringHandle(dtor_name));

			// Parse trailing specifiers (noexcept, override, final, __attribute__, etc.)
			// Destructor trailing specifiers are similar to member function specifiers
			FlashCpp::MemberQualifiers dtor_member_quals;
			FlashCpp::FunctionSpecifiers dtor_func_specs;
			auto dtor_specs_result = parse_function_trailing_specifiers(dtor_member_quals, dtor_func_specs);
			if (dtor_specs_result.is_error()) {
				return dtor_specs_result;
			}
			
			// Apply specifiers
			bool is_override = dtor_func_specs.is_override;
			bool is_final = dtor_func_specs.is_final;
			if (dtor_func_specs.is_noexcept) {
				dtor_ref.set_noexcept(true);
			}

			// In C++, 'override' or 'final' on destructor implies 'virtual'
			if (is_override || is_final) {
				is_virtual = true;
			}

			// Check for = default or = delete
			bool is_defaulted = false;
			bool is_deleted = false;
			if (peek() == "="_tok) {
				advance(); // consume '='

				if (peek().is_keyword()) {
					if (peek() == "default"_tok) {
						advance(); // consume 'default'
						is_defaulted = true;

						// Expect ';'
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= default'", peek_info());
						}

						// Create an empty block for the destructor body
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						// Generate mangled name for the destructor
						NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(dtor_ref);
						dtor_ref.set_mangled_name(mangled);
						dtor_ref.set_definition(block_node);
					} else if (peek() == "delete"_tok) {
						advance(); // consume 'delete'
						is_deleted = true;

						// Expect ';'
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= delete'", peek_info());
						}

						// Track deleted destructors to prevent their use
						if (struct_info) {
							struct_info->markDestructorDeleted();
						}
						continue; // Don't add deleted destructor to struct
					} else {
						return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
					}
				} else {
					return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
				}
			}

			// Parse destructor body if present (and not defaulted/deleted)
			if (!is_defaulted && !is_deleted && peek() == "{"_tok) {
				// DELAYED PARSING: Save the current position (start of '{')
				SaveHandle body_start = save_token_position();

				// Look up the struct type
				auto type_it = gTypesByName.find(struct_name);
				size_t struct_type_index = 0;
				if (type_it != gTypesByName.end()) {
					struct_type_index = type_it->second->type_index_;
				}

				// Skip over the destructor body by counting braces
				skip_balanced_braces();

				// Record this for delayed parsing
				delayed_function_bodies_.push_back({
					nullptr,  // func_node (not used for destructors)
					body_start,
					{},       // initializer_list_start (not used)
					struct_name,
					struct_type_index,
					&struct_ref,
					false,    // has_initializer_list
					false,  // is_constructor
					true,   // is_destructor
					nullptr,  // ctor_node
					&dtor_ref,   // dtor_node
					current_template_param_names_  // template parameter names
				});
			} else if (!is_defaulted && !is_deleted && !consume(";"_tok)) {
				return ParseResult::error("Expected '{', ';', '= default', or '= delete' after destructor declaration", peek_info());
			}

			// Add destructor to struct (unless deleted)
			if (!is_deleted) {
				struct_ref.add_destructor(dtor_node, current_access, is_virtual);
			}
			continue;
		}

		// Parse member declaration (could be data member or member function)
		// Note: is_virtual was already checked above (line 794)
		
		// Special handling for conversion operators: operator type()
		// Conversion operators don't have a return type, so we need to detect them early
		ParseResult member_result;
		if (peek() == "operator"_tok) {
			// This is a conversion operator - parse it specially
			Token operator_keyword_token = peek_info();
			advance(); // consume 'operator'
			
			// Parse the target type
			auto type_result = parse_type_specifier();
			if (type_result.is_error()) {
				return type_result;
			}
			if (!type_result.node().has_value()) {
				return ParseResult::error("Expected type specifier after 'operator' keyword in conversion operator", operator_keyword_token);
			}
			
			// Consume pointer/reference modifiers: operator _Tp&(), operator _Tp*(), etc.
			TypeSpecifierNode& target_type_mut = type_result.node()->as<TypeSpecifierNode>();
			consume_conversion_operator_target_modifiers(target_type_mut);
			
			// Create operator name like "operator int" using StringBuilder
			const TypeSpecifierNode& target_type = type_result.node()->as<TypeSpecifierNode>();
			StringBuilder op_name_builder;
			op_name_builder.append("operator ");
			op_name_builder.append(target_type.getReadableString());
			std::string_view operator_name = op_name_builder.commit();
			
			// Create a synthetic identifier token for the operator
			Token identifier_token = Token(Token::Type::Identifier, operator_name,
			                              operator_keyword_token.line(), operator_keyword_token.column(),
			                              operator_keyword_token.file_index());
			
			// Conversion operators implicitly return the target type
			// Use the parsed target type as the return type
			// Create declaration node with target type as return type and operator name
			ASTNode decl_node = emplace_node<DeclarationNode>(
				type_result.node().value(),
				identifier_token
			);
			
			member_result = ParseResult::success(decl_node);
		} else {
			// Regular member (data or function)
			member_result = parse_type_and_name();
			if (member_result.is_error()) {
				// In template body, recover from member parse errors by skipping to next ';' or '}'
				if (parsing_template_body_ || !struct_parsing_context_stack_.empty()) {
					FLASH_LOG(Parser, Warning, "Template struct body (", StringTable::getStringView(struct_name), "): skipping unparseable member declaration at ", peek_info().value(), " line=", peek_info().line());
					while (!peek().is_eof() && peek() != "}"_tok) {
						if (peek() == ";"_tok) {
							advance(); // consume ';'
							break;
						}
						if (peek() == "{"_tok) {
							skip_balanced_braces();
							if (peek() == ";"_tok) advance();
							break;
						}
						advance();
					}
					continue;
				}
				return member_result;
			}
		}

		// Get the member node - we need to check this exists before proceeding
		if (!member_result.node().has_value()) {
			// In template body, recover from missing member declaration
			if (parsing_template_body_ || !struct_parsing_context_stack_.empty()) {
				FLASH_LOG(Parser, Warning, "Template struct body: skipping unparseable member declaration at ", peek_info().value());
				while (!peek().is_eof() && peek() != "}"_tok) {
					if (peek() == ";"_tok) {
						advance();
						break;
					}
					if (peek() == "{"_tok) {
						skip_balanced_braces();
						if (peek() == ";"_tok) advance();
						break;
					}
					advance();
				}
				continue;
			}
			return ParseResult::error("Expected member declaration", peek_info());
		}

		// Check if this is a member function (has '(') or data member (has ';')
		if (peek() == "("_tok) {
			// This is a member function declaration
			if (!member_result.node()->is<DeclarationNode>()) {
				return ParseResult::error("Expected declaration node for member function", peek_info());
			}

			DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();

			// Parse function declaration with parameters
			auto func_result = parse_function_declaration(decl_node);
			if (func_result.is_error()) {
				return func_result;
			}

			// Mark this as a member function
			if (!func_result.node().has_value()) {
				return ParseResult::error("Failed to create function declaration node", peek_info());
			}

			FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();

			// Create a new FunctionDeclarationNode with member function info
			// Pass string_view directly - FunctionDeclarationNode stores it as string_view
			// Use qualified_struct_name for nested classes so the member function references the correct type
			auto [member_func_node, member_func_ref] =
				emplace_node_ref<FunctionDeclarationNode>(decl_node, qualified_struct_name);

			// Copy parameters from the parsed function
			for (const auto& param : func_decl.parameter_nodes()) {
				member_func_ref.add_parameter_node(param);
			}

			// Mark as constexpr if the constexpr keyword was present
			member_func_ref.set_is_constexpr(member_specs & FlashCpp::MLS_Constexpr);

			// Use unified trailing specifiers parsing (Phase 2)
			// This handles: const, volatile, &, &&, noexcept, override, final, = 0, = default, = delete
			FlashCpp::MemberQualifiers member_quals;
			FlashCpp::FunctionSpecifiers func_specs;
			auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
			if (specs_result.is_error()) {
				return specs_result;
			}

			// Extract parsed specifiers for use in member function registration
			bool is_const_member = member_quals.is_const;
			bool is_volatile_member = member_quals.is_volatile;
			bool is_override = func_specs.is_override;
			bool is_final = func_specs.is_final;
			bool is_pure_virtual = func_specs.is_pure_virtual;
			bool is_defaulted = func_specs.is_defaulted;
			bool is_deleted = func_specs.is_deleted;

			// Handle defaulted functions: set implicit flag and create empty body
			if (is_defaulted) {
				// Expect ';'
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after '= default'", peek_info());
				}

				// Mark as implicit (same behavior as compiler-generated)
				member_func_ref.set_is_implicit(true);

				// Create a simple block for the function body
				auto [block_node, block_ref] = create_node_ref(BlockNode());

				// Special-case defaulted spaceship operator: emit a safe return value
				if (decl_node.identifier_token().value() == "operator<=>") {
					Token zero_token(Token::Type::Literal, "0"sv,
						decl_node.identifier_token().line(),
						decl_node.identifier_token().column(),
						decl_node.identifier_token().file_index());
					auto zero_expr = emplace_node<ExpressionNode>(
						NumericLiteralNode(zero_token, 0ULL, Type::Int, TypeQualifier::None, 32));
					auto return_stmt = emplace_node<ReturnStatementNode>(
						std::optional<ASTNode>(zero_expr), zero_token);
					block_ref.add_statement_node(return_stmt);
				}

				// Generate mangled name before setting definition (Phase 6 mangling)
				compute_and_set_mangled_name(member_func_ref);
				member_func_ref.set_definition(block_node);
			}
			
			// Handle deleted functions: skip adding to struct (they cannot be called)
			if (is_deleted) {
				// Expect ';'
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after '= delete'", peek_info());
				}
				
				// Track deleted assignment operators to prevent their implicit use
				if (struct_info && decl_node.identifier_token().value() == "operator=") {
					// Check if it's a move or copy assignment operator based on parameter type
					bool is_move_assign = false;
					const auto& params = member_func_ref.parameter_nodes();
					if (params.size() == 1) {
						const auto& param = params[0];
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							// Check if the type specifier matches the struct name
							const auto& type_node = param_decl.type_node();
							if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
								const auto& type_spec = type_node.as<TypeSpecifierNode>();
								std::string_view param_type_name = type_spec.token().value();
								if (param_type_name == struct_name || 
								    param_type_name == qualified_struct_name.view()) {
									// It's a reference to this type
									if (type_spec.is_rvalue_reference()) {
										is_move_assign = true;
									}
								}
							}
						}
					}
					struct_info->markAssignmentDeleted(is_move_assign);
				}
				
				// Deleted functions are not added to the struct - they exist only to prevent
				// implicit generation of special member functions or to disable certain overloads
				continue;
			}

			// Validate pure virtual functions must be declared with 'virtual'
			if (is_pure_virtual && !is_virtual) {
				return ParseResult::error("Pure virtual function must be declared with 'virtual' keyword", peek_info());
			}

			// Parse function body if present (and not defaulted/deleted)
			if (!is_defaulted && !is_deleted && peek() == "{"_tok) {
				// DELAYED PARSING: Save the current position (start of '{')
				SaveHandle body_start = save_token_position();

				// Look up the struct type to get its type index
				auto type_it = gTypesByName.find(struct_name);
				size_t struct_type_index = 0;
				if (type_it != gTypesByName.end()) {
					struct_type_index = type_it->second->type_index_;
				}

				// Skip over the function body by counting braces
				skip_balanced_braces();

				// Record this for delayed parsing
				delayed_function_bodies_.push_back({
					&member_func_ref,
					body_start,
					{},       // initializer_list_start (not used)
					struct_name,
					struct_type_index,
					&struct_ref,
					false,    // has_initializer_list
					false,  // is_constructor
					false,  // is_destructor
					nullptr,  // ctor_node
					nullptr,  // dtor_node
					current_template_param_names_  // template parameter names
				});
				// Inline function body consumed, no semicolon needed
			} else if (!is_defaulted && !is_deleted) {
				// Function declaration without body - expect semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected '{', ';', '= default', or '= delete' after member function declaration", peek_info());
				}
			}

			// In C++, 'override' implies 'virtual'
			if (is_override || is_final) {
				is_virtual = true;
			}

			// Check if this is an operator overload
			std::string_view func_name = decl_node.identifier_token().value();
			if (func_name.starts_with("operator")) {
				// Extract the operator symbol (e.g., "operator=" -> "=")
				std::string_view operator_symbol = func_name.substr(8);  // Skip "operator"
				struct_ref.add_operator_overload(operator_symbol, member_func_node, current_access,
				                                 is_virtual, is_pure_virtual, is_override, is_final,
				                                 is_const_member, is_volatile_member);
			} else {
				// Add regular member function to struct
				struct_ref.add_member_function(member_func_node, current_access,
				                               is_virtual, is_pure_virtual, is_override, is_final,
				                               is_const_member, is_volatile_member);
			}
		} else {
			// This is a data member
			std::optional<ASTNode> default_initializer;

			// Get the type from the member declaration
			if (!member_result.node()->is<DeclarationNode>()) {
				return ParseResult::error("Expected declaration node for member", peek_info());
			}
			const DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
			const TypeSpecifierNode& type_spec = decl_node.type_node().as<TypeSpecifierNode>();

			std::optional<size_t> bitfield_width;
			std::optional<ASTNode> bitfield_width_expr;
			// Handle bitfield declarations: int x : 5; or unnamed: int : 32;
			// Bitfields specify a width in bits after ':' and before ';'
			if (peek() == ":"_tok) {
				advance(); // consume ':'
				// Parse the bitfield width expression (usually a numeric literal)
				auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
				if (width_result.is_error()) {
					return width_result;
				}
				if (width_result.node().has_value()) {
					ConstExpr::EvaluationContext ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
					if (!eval_result.success() || eval_result.as_int() < 0) {
						// Defer evaluation for template non-type parameters
						bitfield_width_expr = *width_result.node();
					} else {
						bitfield_width = static_cast<size_t>(eval_result.as_int());
					}
				}
			}

			// Check for direct brace initialization: C c1{ 1 };
			if (peek() == "{"_tok) {
				auto init_result = parse_brace_initializer(type_spec);
				if (init_result.is_error()) {
					return init_result;
				}
				if (init_result.node().has_value()) {
					default_initializer = *init_result.node();
				}
			}
			// Check for member initialization with '=' (C++11 feature)
			else if (peek() == "="_tok) {
				advance(); // consume '='

				// Check if this is a brace initializer: B b = { .a = 1 }
				if (peek() == "{"_tok) {
					auto init_result = parse_brace_initializer(type_spec);
					if (init_result.is_error()) {
						return init_result;
					}
					if (init_result.node().has_value()) {
						default_initializer = *init_result.node();
					}
				}
				// Check if this is a type name followed by brace initializer: B b = B{ .a = 2 }
				else if (peek().is_identifier()) {
					// Save position in case this isn't a type name
					SaveHandle member_init_saved_pos = save_token_position();

					// Try to parse as type specifier
					ParseResult type_result = parse_type_specifier();
					if (!type_result.is_error() && type_result.node().has_value() &&
					    !peek().is_eof() && (peek() == "{"_tok || peek() == "("_tok)) {
						// This is a type name followed by initializer: B{...} or B(...)
						const TypeSpecifierNode& init_type_spec = type_result.node()->as<TypeSpecifierNode>();

						if (peek() == "{"_tok) {
							// Parse brace initializer
							auto init_result = parse_brace_initializer(init_type_spec);
							if (init_result.is_error()) {
								return init_result;
							}
							if (init_result.node().has_value()) {
								default_initializer = *init_result.node();
							}
						} else {
							// Parse parenthesized initializer: B(args)
							advance(); // consume '('
							std::vector<ASTNode> init_args;
							if (peek() != ")"_tok) {
								do {
									ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
									if (arg_result.is_error()) {
										return arg_result;
									}
									if (auto arg_node = arg_result.node()) {
										init_args.push_back(*arg_node);
									}
								} while (peek() == ","_tok && (advance(), true));
							}
							if (!consume(")"_tok)) {
								return ParseResult::error("Expected ')' after initializer arguments", current_token_);
							}

							// Create an InitializerListNode with the arguments
							auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());
							for (auto& arg : init_args) {
								init_list_ref.add_initializer(arg);
							}
							default_initializer = init_list_node;
						}
						discard_saved_token(saved_pos);
					} else {
						// Not a type name, restore and parse as expression
						restore_token_position(member_init_saved_pos);
						auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							default_initializer = *init_result.node();
						}
					}
				} else {
					// Parse regular expression initializer
					auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (init_result.is_error()) {
						return init_result;
					}
					if (init_result.node().has_value()) {
						default_initializer = *init_result.node();
					}
				}
			}

			// Validate that parameter packs cannot be data members
			// Only function and template parameters can be parameter packs
			if (member_result.node()->is<DeclarationNode>()) {
				const DeclarationNode& member_decl = member_result.node()->as<DeclarationNode>();
				if (member_decl.is_parameter_pack()) {
					return ParseResult::error("Only function and template parameters can be parameter packs", member_decl.identifier_token());
				}
			}

			// Add the first member to struct with current access level and default initializer
			struct_ref.add_member(*member_result.node(), current_access, default_initializer, bitfield_width, bitfield_width_expr);

			// Check for comma-separated additional declarations (e.g., int x, y, z;)
			while (peek() == ","_tok) {
				advance(); // consume ','

				// Parse the identifier (name) - reuse the same type
				auto identifier_token = advance();
				if (!identifier_token.kind().is_identifier()) {
					return ParseResult::error("Expected identifier after comma in member declaration list", current_token_);
				}

				// Create a new DeclarationNode with the same type
				ASTNode new_decl = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(type_spec),
					identifier_token
				);

				std::optional<size_t> additional_bitfield_width;
				std::optional<ASTNode> additional_bitfield_width_expr;
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
					if (width_result.is_error()) {
						return width_result;
					}
					if (width_result.node().has_value()) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
						if (!eval_result.success() || eval_result.as_int() < 0) {
							// Defer evaluation for template non-type parameters
							additional_bitfield_width_expr = *width_result.node();
						} else {
							additional_bitfield_width = static_cast<size_t>(eval_result.as_int());
						}
					}
				}

				// Check for optional initialization for this member
				std::optional<ASTNode> additional_init;
				if (peek() == "{"_tok) {
					auto init_result = parse_brace_initializer(type_spec);
					if (init_result.is_error()) {
						return init_result;
					}
					if (init_result.node().has_value()) {
						additional_init = *init_result.node();
					}
				}
				else if (peek() == "="_tok) {
					advance(); // consume '='
					if (peek() == "{"_tok) {
						auto init_result = parse_brace_initializer(type_spec);
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							additional_init = *init_result.node();
						}
					} else {
						// Parse expression with precedence > comma operator (precedence 1)
						auto init_result = parse_expression(2, ExpressionContext::Normal);
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							additional_init = *init_result.node();
						}
					}
				}

				// Add this member to the struct
				struct_ref.add_member(new_decl, current_access, additional_init, additional_bitfield_width, additional_bitfield_width_expr);
			}

			// Expect semicolon after member declaration(s)
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after struct member declaration", peek_info());
			}
		}
	}

	// Expect closing brace
	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' at end of struct/class definition", peek_info());
	}

	// Skip any attributes after struct/class closing brace (e.g., __attribute__((__deprecated__)))
	// These must be skipped before trying to parse variable declarations
	skip_cpp_attributes();

	// Check for variable declarations after struct definition: struct Point { ... } p, q;
	// Also handles: inline constexpr struct Name { ... } variable = {};
	// And: struct S { ... } inline constexpr s{};  (C++17 inline variables)
	std::vector<ASTNode> struct_variables;
	
	// First, skip any storage class specifiers before the variable name
	// Valid specifiers: inline, constexpr, static, extern, thread_local
	bool has_inline = false;
	bool has_constexpr = false;
	[[maybe_unused]] bool has_static = false;
	while (peek().is_keyword()) {
		std::string_view kw = peek_info().value();
		if (kw == "inline") {
			has_inline = true;
			advance();
		} else if (kw == "constexpr") {
			has_constexpr = true;
			advance();
		} else if (kw == "static") {
			has_static = true;
			advance();
		} else if (kw == "const") {
			advance();
		} else {
			break;
		}
	}
	
	(void)has_inline;  // Mark as used
	(void)has_constexpr;  // Mark as used
	
	if (!peek().is_eof() && 
	    (peek().is_identifier() || 
	     (peek() == "*"_tok))) {
		// Parse variable declarators
		do {
			// Handle pointer declarators
			TypeSpecifierNode var_type_spec(
				Type::Struct,
				struct_type_info.type_index_,
				static_cast<unsigned char>(0),  // Size will be set later
				Token(Token::Type::Identifier, StringTable::getStringView(struct_name), 0, 0, 0)
			);
			
			// Parse any pointer levels
			while (peek() == "*"_tok) {
				advance(); // consume '*'
				CVQualifier ptr_cv = parse_cv_qualifiers();
				var_type_spec.add_pointer_level(ptr_cv);
			}
			
			auto var_name_token = advance();

			// Create a variable declaration node for this variable
			auto var_type_spec_node = emplace_node<TypeSpecifierNode>(var_type_spec);
			auto var_decl = emplace_node<DeclarationNode>(var_type_spec_node, var_name_token);

			// Add to symbol table so it can be referenced later in the code
			gSymbolTable.insert(var_name_token.value(), var_decl);

			// Check for initializer: struct S {} s = {};
			std::optional<ASTNode> init_expr;
			if (peek() == "="_tok) {
				advance(); // consume '='
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (init_result.is_error()) {
					return init_result;
				}
				init_expr = init_result.node();
			} else if (peek() == "{"_tok) {
				// C++11 brace initialization: struct S { } s{};
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (init_result.is_error()) {
					return init_result;
				}
				init_expr = init_result.node();
			}

			// Wrap in VariableDeclarationNode so it gets processed properly by code generator
			auto var_decl_node = emplace_node<VariableDeclarationNode>(var_decl, init_expr);

			struct_variables.push_back(var_decl_node);

		} while (peek() == ","_tok && (advance(), true));
	}

	// Expect semicolon after struct definition (and optional variable declarations)
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after struct/class definition", peek_info());
	}

	// struct_type_info was already registered early (before parsing members)
	// struct_info was created early (before parsing base classes and members)
	// Now process data members and calculate layout
	
	// Build a set of member indices that are part of anonymous unions (to skip during normal processing)
	std::unordered_set<size_t> anonymous_union_member_indices;
	for (const auto& anon_union : struct_ref.anonymous_unions()) {
		// Mark all member indices that are union members (they're already in the anon_union.union_members list)
		for (size_t i = 0; i < anon_union.union_members.size(); ++i) {
			anonymous_union_member_indices.insert(anon_union.member_index_in_ast + i);
		}
	}
	
	size_t member_index = 0;
	size_t next_union_idx = 0;
	const std::vector<AnonymousUnionInfo>& anon_unions = struct_ref.anonymous_unions();
	
	for (const auto& member_decl : struct_ref.members()) {
		// Check if we should process an anonymous union before this member
		while (next_union_idx < anon_unions.size() && anon_unions[next_union_idx].member_index_in_ast == member_index) {
			const AnonymousUnionInfo& union_info = anon_unions[next_union_idx];
			
			// Process all anonymous union members at the same offset
			size_t union_start_offset = struct_info->total_size;
			size_t union_max_size = 0;
			size_t union_max_alignment = 1;
			
			// First pass: determine union alignment and size
			for (const auto& union_member : union_info.union_members) {
				size_t effective_alignment = union_member.member_alignment;
				if (struct_info->pack_alignment > 0 && struct_info->pack_alignment < union_member.member_alignment) {
					effective_alignment = struct_info->pack_alignment;
				}
				union_max_size = std::max(union_max_size, union_member.member_size);
				union_max_alignment = std::max(union_max_alignment, effective_alignment);
			}
			
			// Align the union start offset
			size_t aligned_union_start = ((union_start_offset + union_max_alignment - 1) & ~(union_max_alignment - 1));
			
			// Second pass: add all union members at the same aligned offset
			for (const auto& union_member : union_info.union_members) {
				size_t effective_alignment = union_member.member_alignment;
				if (struct_info->pack_alignment > 0 && struct_info->pack_alignment < union_member.member_alignment) {
					effective_alignment = struct_info->pack_alignment;
				}
				
				// Manually add member to struct_info at the aligned offset
				struct_info->members.emplace_back(
					union_member.member_name,
					union_member.member_type,
					union_member.type_index,
					aligned_union_start,  // Same offset for all union members
					union_member.member_size,
					effective_alignment,
					AccessSpecifier::Public,  // Anonymous union members are always public
					std::nullopt,  // No default initializer
					union_member.is_reference,
					union_member.is_rvalue_reference,
					union_member.referenced_size_bits,
					union_member.is_array,
					union_member.array_dimensions,
					union_member.pointer_depth,
					union_member.bitfield_width
				);
				
				// Update struct alignment
				struct_info->alignment = std::max(struct_info->alignment, effective_alignment);
			}
			
			// Update total_size to account for the union (largest member)
			struct_info->total_size = aligned_union_start + union_max_size;
			struct_info->active_bitfield_unit_size = 0;
			struct_info->active_bitfield_bits_used = 0;
			struct_info->active_bitfield_unit_alignment = 0;
			struct_info->active_bitfield_type = Type::Invalid;
			
			next_union_idx++;
		}
		
		// Skip individual anonymous union member nodes (they're already processed above)
		if (anonymous_union_member_indices.count(member_index) > 0) {
			member_index++;
			continue;
		}
		
		// Process regular (non-union) member
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

		// Get member size and alignment
		// Calculate member size and alignment
		auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(type_spec);
		size_t referenced_size_bits = type_spec.size_in_bits();

		// For struct types, get size and alignment from the struct type info
		if (type_spec.type() == Type::Struct && !type_spec.is_pointer() && !type_spec.is_reference()) {
			// Look up the struct type by type_index
			const TypeInfo* member_type_info = nullptr;
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == type_spec.type_index()) {
					member_type_info = &ti;
					break;
				}
			}

			if (member_type_info && member_type_info->getStructInfo()) {
				member_size = member_type_info->getStructInfo()->total_size;
				referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
				member_alignment = member_type_info->getStructInfo()->alignment;
			}
		}

		// For array members, multiply element size by array count and collect dimensions
		bool is_array = false;
		std::vector<size_t> array_dimensions;
		if (decl.is_array()) {
			is_array = true;
			// Collect all array dimensions
			const auto& dims = decl.array_dimensions();
			for (const auto& dim_expr : dims) {
				ConstExpr::EvaluationContext ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
				if (eval_result.success() && eval_result.as_int() > 0) {
					size_t dim_size = static_cast<size_t>(eval_result.as_int());
					array_dimensions.push_back(dim_size);
					member_size *= dim_size;
					referenced_size_bits *= dim_size;
				}
			}
		}

		// Add member to struct layout with default initializer
		bool is_ref_member = type_spec.is_reference();
		bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
		// Reference size and alignment were already set above
		if (is_ref_member) {
			// Update referenced_size_bits if not already set
			referenced_size_bits = referenced_size_bits ? referenced_size_bits : (type_spec.size_in_bits());
		}
		// Phase 7B: Intern member name and use StringHandle overload
		StringHandle member_name_handle = decl.identifier_token().handle();
		struct_info->addMember(
			member_name_handle,
			type_spec.type(),
			type_spec.type_index(),
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer,
			is_ref_member,
			is_rvalue_ref_member,
			referenced_size_bits,
			is_array,
			array_dimensions,
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width
		);
		
		member_index++;
	}

	// Process member functions, constructors, and destructors
	bool has_user_defined_constructor = false;
	bool has_user_defined_copy_constructor = false;
	bool has_user_defined_move_constructor = false;
	bool has_user_defined_copy_assignment = false;
	bool has_user_defined_move_assignment = false;
	bool has_user_defined_destructor = false;
	bool has_user_defined_spaceship = false;  // Track if operator<=> is defined

	for (const auto& func_decl : struct_ref.member_functions()) {
		if (func_decl.is_constructor) {
			// Add constructor to struct type info
			struct_info->addConstructor(
				func_decl.function_declaration,
				func_decl.access
			);
			has_user_defined_constructor = true;

			// Check if this is a copy or move constructor
			const auto& ctor_node = func_decl.function_declaration.as<ConstructorDeclarationNode>();
			const auto& params = ctor_node.parameter_nodes();
			if (params.size() == 1) {
				const auto& param_decl = params[0].as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

				if (param_type.is_reference() && param_type.type() == Type::Struct) {
					has_user_defined_copy_constructor = true;
				} else if (param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
					has_user_defined_move_constructor = true;
				}
			}
		} else if (func_decl.is_destructor) {
			// Add destructor to struct type info
			struct_info->addDestructor(
				func_decl.function_declaration,
				func_decl.access,
				func_decl.is_virtual
			);
			has_user_defined_destructor = true;
		} else if (func_decl.is_operator_overload) {
			// Operator overload
			struct_info->addOperatorOverload(
				func_decl.operator_symbol,
				func_decl.function_declaration,
				func_decl.access,
				func_decl.is_virtual,
				func_decl.is_pure_virtual,
				func_decl.is_override,
				func_decl.is_final
			);

			// Check if this is a spaceship operator
			if (func_decl.operator_symbol == "<=>") {
				has_user_defined_spaceship = true;
			}

			// Check if this is a copy or move assignment operator
			if (func_decl.operator_symbol == "=") {
				const auto& func_node = func_decl.function_declaration.as<FunctionDeclarationNode>();
				const auto& params = func_node.parameter_nodes();
				if (params.size() == 1) {
					const auto& param_decl = params[0].as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

					if (param_type.is_reference() && !param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
						has_user_defined_copy_assignment = true;
					} else if (param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
						has_user_defined_move_assignment = true;
					}
				}
			}
		} else {
			// Regular member function or template member function
			StringHandle func_name_handle;
			
			// Handle both regular functions and template functions
			if (func_decl.function_declaration.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func = func_decl.function_declaration.as<FunctionDeclarationNode>();
				const DeclarationNode& decl = func.decl_node();
				func_name_handle = decl.identifier_token().handle();
			} else if (func_decl.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				// Template member function - extract name from the inner function declaration
				const TemplateFunctionDeclarationNode& tmpl_func = func_decl.function_declaration.as<TemplateFunctionDeclarationNode>();
				const FunctionDeclarationNode& func = tmpl_func.function_decl_node();
				const DeclarationNode& decl = func.decl_node();
				func_name_handle = decl.identifier_token().handle();
			} else {
				// Unknown function type - skip
				continue;
			}

			// Add member function to struct type info
			// Phase 7B: Intern function name and use StringHandle overload
			struct_info->addMemberFunction(
				func_name_handle,
				func_decl.function_declaration,
				func_decl.access,
				func_decl.is_virtual,
				func_decl.is_pure_virtual,
				func_decl.is_override,
				func_decl.is_final
			);
			// Propagate const/volatile qualifiers from the AST node to StructTypeInfo
			auto& registered_func = struct_info->member_functions.back();
			registered_func.is_const = func_decl.is_const;
			registered_func.is_volatile = func_decl.is_volatile;
	}
}

	// Generate inherited constructors if "using Base::Base;" was encountered
	// This must happen before implicit constructor generation
	if (!struct_parsing_context_stack_.empty() && 
	    struct_parsing_context_stack_.back().has_inherited_constructors && 
	    !parsing_template_class_) {
		// Iterate through base classes and generate forwarding constructors
		for (const auto& base_class : struct_info->base_classes) {
			if (base_class.type_index >= gTypeInfo.size()) {
				continue;
			}
			
			const TypeInfo& base_type_info = gTypeInfo[base_class.type_index];
			const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
			
			if (!base_struct_info) {
				continue;
			}
			
			// Generate a forwarding constructor for each base class constructor
			for (const auto& base_ctor_info : base_struct_info->member_functions) {
				if (!base_ctor_info.is_constructor) {
					continue;
				}
				
				const ConstructorDeclarationNode& base_ctor = 
					base_ctor_info.function_decl.as<ConstructorDeclarationNode>();
				
				// Skip copy and move constructors (they are not inherited)
				const auto& base_params = base_ctor.parameter_nodes();
				if (base_params.size() == 1) {
					const auto& param_decl = base_params[0].as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					
					if (param_type.is_reference() && param_type.type() == Type::Struct) {
						// This is a copy or move constructor - skip it
						continue;
					}
				}
				
				// Create a forwarding constructor for the derived class
				auto [derived_ctor_node, derived_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
					qualified_struct_name,
					qualified_struct_name
				);
				
				// Copy parameters from base constructor to derived constructor
				for (const auto& base_param : base_params) {
					const DeclarationNode& base_param_decl = base_param.as<DeclarationNode>();
					const TypeSpecifierNode& base_param_type = base_param_decl.type_node().as<TypeSpecifierNode>();
					
					// Create a copy of the parameter for the derived constructor
					auto param_type_node = emplace_node<TypeSpecifierNode>(
						base_param_type.type(),
						base_param_type.type_index(),
						base_param_type.size_in_bits(),
						base_param_decl.identifier_token(),
						base_param_type.cv_qualifier()
					);
					
					// Copy reference qualifiers
					if (base_param_type.is_rvalue_reference()) {
						param_type_node.as<TypeSpecifierNode>().set_reference(true);
					} else if (base_param_type.is_reference()) {
						param_type_node.as<TypeSpecifierNode>().set_lvalue_reference(true);
					}
					
					auto param_decl_node = emplace_node<DeclarationNode>(
						param_type_node,
						base_param_decl.identifier_token()
					);
					
					derived_ctor_ref.add_parameter_node(param_decl_node);
				}
				
				// Create base initializer to forward to base constructor
				// This will call Base::Base(args...) where args are the parameters
				std::vector<ASTNode> base_init_args;
				for (const auto& param : base_params) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					// Create an identifier node for the parameter and wrap it in an ExpressionNode
					IdentifierNode id_node(param_decl.identifier_token());
					auto expr_node = emplace_node<ExpressionNode>(id_node);
					base_init_args.push_back(expr_node);
				}
				
				// Add base initializer to constructor
				derived_ctor_ref.add_base_initializer(
					StringTable::getOrInternStringHandle(base_class.name),
					std::move(base_init_args)
				);
				
				// Create an empty block for the constructor body
				auto [block_node, block_ref] = create_node_ref(BlockNode());
				derived_ctor_ref.set_definition(block_node);
				
				// Mark this as an implicit constructor (even though it's inherited)
				derived_ctor_ref.set_is_implicit(false);
				
				// Add the inherited constructor to the struct type info
				struct_info->addConstructor(
					derived_ctor_node,
					AccessSpecifier::Public
				);
				
				// Add the inherited constructor to the struct node
				struct_ref.add_constructor(derived_ctor_node, AccessSpecifier::Public);
				
				// Mark that we now have a user-defined constructor (the inherited one)
				has_user_defined_constructor = true;
				
				FLASH_LOG(Parser, Debug, "Generated inherited constructor for '", 
						  StringTable::getStringView(qualified_struct_name), "' with ", 
						  base_params.size(), " parameter(s)");
			}
		}
	}

	// Generate default constructor if no user-defined constructor exists
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_constructor && !parsing_template_class_) {
		// Create a default constructor node
		// Use qualified_struct_name to include namespace for proper mangling
		auto [default_ctor_node, default_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			qualified_struct_name,
			qualified_struct_name
		);

		// Create an empty block for the constructor body
		auto [block_node, block_ref] = create_node_ref(BlockNode());
		default_ctor_ref.set_definition(block_node);

		// Mark this as an implicit default constructor
		default_ctor_ref.set_is_implicit(true);

		// Add the default constructor to the struct type info
		struct_info->addConstructor(
			default_ctor_node,
			AccessSpecifier::Public  // Default constructors are always public
		);

		// Add the default constructor to the struct node
		struct_ref.add_constructor(default_ctor_node, AccessSpecifier::Public);
	}

	// Generate copy constructor if no user-defined copy constructor exists
	// According to C++ rules, copy constructor is implicitly generated unless:
	// - User declared a move constructor or move assignment operator
	// - User declared a copy constructor
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_copy_constructor && !has_user_defined_move_constructor && !parsing_template_class_) {
		// Create a copy constructor node: Type(const Type& other)
		// Use qualified_struct_name to include namespace for proper mangling
		auto [copy_ctor_node, copy_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			qualified_struct_name,
			qualified_struct_name
		);

		// Create parameter: const Type& other
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::Const  // const qualifier
		);

		// Make it a reference type
		param_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create parameter declaration
		Token param_token(Token::Type::Identifier, "other"sv, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to constructor
		copy_ctor_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the constructor body
		auto [copy_block_node, copy_block_ref] = create_node_ref(BlockNode());
		copy_ctor_ref.set_definition(copy_block_node);

		// Mark this as an implicit copy constructor
		copy_ctor_ref.set_is_implicit(true);

		// Add the copy constructor to the struct type info
		struct_info->addConstructor(
			copy_ctor_node,
			AccessSpecifier::Public
		);

		// Add the copy constructor to the struct node
		struct_ref.add_constructor(copy_ctor_node, AccessSpecifier::Public);
	}

	// Generate copy assignment operator if no user-defined copy assignment operator exists
	// According to C++ rules, copy assignment operator is implicitly generated unless:
	// - User declared a move constructor or move assignment operator
	// - User declared a copy assignment operator
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_copy_assignment && !has_user_defined_move_assignment && !parsing_template_class_) {
		// Create a copy assignment operator node: Type& operator=(const Type& other)

		// Create return type: Type& (reference to struct type)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto return_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::None
		);
		return_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create declaration node for operator=
		Token operator_name_token(Token::Type::Identifier, "operator="sv,
		                          name_token.line(), name_token.column(),
		                          name_token.file_index());

		auto operator_decl_node = emplace_node<DeclarationNode>(return_type_node, operator_name_token);

		// Create function declaration node
		// Use qualified_struct_name for nested classes so the member function references the correct type
		auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(
			operator_decl_node.as<DeclarationNode>(), qualified_struct_name);

		// Create parameter: const Type& other
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::Const  // const qualifier
		);
		param_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create parameter declaration
		Token param_token(Token::Type::Identifier, "other"sv, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to function
		func_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the operator= body
		auto [op_block_node, op_block_ref] = create_node_ref(BlockNode());
		// Generate mangled name before setting definition (Phase 6 mangling)
		compute_and_set_mangled_name(func_ref);
		func_ref.set_definition(op_block_node);

		// Mark this as an implicit operator=
		func_ref.set_is_implicit(true);

		// Add the operator= to the struct type info
		struct_info->addOperatorOverload(
			"=",
			func_node,
			AccessSpecifier::Public
		);

		// Add the operator= to the struct node
		static const std::string_view operator_symbol_eq = "=";
		struct_ref.add_operator_overload(operator_symbol_eq, func_node, AccessSpecifier::Public);
	}

	// Generate move constructor if no user-defined special member functions exist
	// According to C++ rules, move constructor is implicitly generated unless:
	// - User declared a copy constructor, copy assignment, move assignment, or destructor
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_copy_constructor && !has_user_defined_copy_assignment &&
	    !has_user_defined_move_assignment && !has_user_defined_destructor && !parsing_template_class_) {
		// Create a move constructor node: Type(Type&& other)
		// Use qualified_struct_name to include namespace for proper mangling
		auto [move_ctor_node, move_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			qualified_struct_name,
			qualified_struct_name
		);

		// Create parameter: Type&& other (rvalue reference)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::None
		);

		// Make it an rvalue reference type
		param_type_node.as<TypeSpecifierNode>().set_reference(true);  // true = rvalue reference

		// Create parameter declaration
		Token param_token(Token::Type::Identifier, "other"sv, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to constructor
		move_ctor_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the constructor body
		auto [move_block_node, move_block_ref] = create_node_ref(BlockNode());
		move_ctor_ref.set_definition(move_block_node);

		// Mark this as an implicit move constructor
		move_ctor_ref.set_is_implicit(true);

		// Add the move constructor to the struct type info
		struct_info->addConstructor(move_ctor_node, AccessSpecifier::Public);

		// Add the move constructor to the struct node
		struct_ref.add_constructor(move_ctor_node, AccessSpecifier::Public);
	}

	// Generate move assignment operator if no user-defined special member functions exist
	// According to C++ rules, move assignment operator is implicitly generated unless:
	// - User declared a copy constructor, copy assignment, move constructor, or destructor
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_copy_constructor && !has_user_defined_copy_assignment &&
	    !has_user_defined_move_constructor && !has_user_defined_destructor && !parsing_template_class_) {
		// Create a move assignment operator node: Type& operator=(Type&& other)

		// Create return type: Type& (reference to struct type)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto return_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::None
		);
		return_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create declaration node for operator=
		Token move_operator_name_token(Token::Type::Identifier, "operator="sv,
		                          name_token.line(), name_token.column(),
		                          name_token.file_index());

		auto move_operator_decl_node = emplace_node<DeclarationNode>(return_type_node, move_operator_name_token);

		// Create function declaration node
		// Use qualified_struct_name for nested classes so the member function references the correct type
		auto [move_func_node, move_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
			move_operator_decl_node.as<DeclarationNode>(), qualified_struct_name);

		// Create parameter: Type&& other (rvalue reference)
		auto move_param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_info->total_size * 8),  // size in bits
			name_token,
			CVQualifier::None
		);
		move_param_type_node.as<TypeSpecifierNode>().set_reference(true);  // true = rvalue reference

		// Create parameter declaration
		Token move_param_token(Token::Type::Identifier, "other"sv, 0, 0, 0);
		auto move_param_decl_node = emplace_node<DeclarationNode>(move_param_type_node, move_param_token);

		// Add parameter to function
		move_func_ref.add_parameter_node(move_param_decl_node);

		// Create an empty block for the operator= body
		auto [move_op_block_node, move_op_block_ref] = create_node_ref(BlockNode());
		// Generate mangled name before setting definition (Phase 6 mangling)
		compute_and_set_mangled_name(move_func_ref);
		move_func_ref.set_definition(move_op_block_node);

		// Mark this as an implicit operator=
		move_func_ref.set_is_implicit(true);

		// Add the move assignment operator to the struct type info
		struct_info->addOperatorOverload(
			"=",
			move_func_node,
			AccessSpecifier::Public
		);

		// Add the move assignment operator to the struct node
		static const std::string_view move_operator_symbol_eq = "=";
		struct_ref.add_operator_overload(move_operator_symbol_eq, move_func_node, AccessSpecifier::Public);
	}

	// Generate comparison operators from operator<=> if defined
	// According to C++20, when operator<=> is defined, the compiler automatically synthesizes
	// the six comparison operators: ==, !=, <, >, <=, >=
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (has_user_defined_spaceship && !parsing_template_class_) {
		TypeIndex struct_type_index = struct_type_info.type_index_;
		
		// Array of comparison operators to synthesize
		static const std::array<std::pair<std::string_view, std::string_view>, 6> comparison_ops = {{
			{"==", "operator=="},
			{"!=", "operator!="},
			{"<", "operator<"},
			{">", "operator>"},
			{"<=", "operator<="},
			{">=", "operator>="}
		}};
		
		for (const auto& [op_symbol, op_name] : comparison_ops) {
			// Create return type: bool
			auto return_type_node = emplace_node<TypeSpecifierNode>(
				Type::Bool,
				0,  // type_index for bool
				8,  // size in bits
				name_token,
				CVQualifier::None
			);
			
			// Create declaration node for the operator
			Token operator_name_token(Token::Type::Identifier, op_name,
			                          name_token.line(), name_token.column(),
			                          name_token.file_index());
			
			auto operator_decl_node = emplace_node<DeclarationNode>(return_type_node, operator_name_token);
			
			// Create function declaration node
			// Use qualified_struct_name for nested classes so the member function references the correct type
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(
				operator_decl_node.as<DeclarationNode>(), qualified_struct_name);
			
			// Create parameter: const Type& other
			auto param_type_node = emplace_node<TypeSpecifierNode>(
				Type::Struct,
				struct_type_index,
				static_cast<int>(struct_info->total_size * 8),  // size in bits
				name_token,
				CVQualifier::Const  // const qualifier
			);
			param_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference
			
			// Create parameter declaration
			Token param_token(Token::Type::Identifier, "other"sv, 0, 0, 0);
			auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);
			
			// Add parameter to function
			func_ref.add_parameter_node(param_decl_node);
			
			// Generate function body that calls operator<=> and compares result
			// The body should be equivalent to:
			//   return (this->operator<=>(other)) <op> 0;
			// where <op> is the appropriate comparison operator
			
			// First, find the spaceship operator function in the struct
			const FunctionDeclarationNode* spaceship_func = nullptr;
			for (const auto& member_func : struct_ref.member_functions()) {
				if (member_func.is_operator_overload && member_func.operator_symbol == "<=>") {
					spaceship_func = &(member_func.function_declaration.as<FunctionDeclarationNode>());
					break;
				}
			}
			
			if (!spaceship_func) {
				// This shouldn't happen since we only get here if has_user_defined_spaceship is true
				return ParseResult::error("Internal error: spaceship operator not found", name_token);
			}
			
			// Create the function body
			auto [op_block_node, op_block_ref] = create_node_ref(BlockNode());
			
			// Create "this" identifier
			Token this_token(Token::Type::Keyword, "this"sv,
			                name_token.line(), name_token.column(),
			                name_token.file_index());
			auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));
			
			// Create "other" identifier reference
			Token other_token(Token::Type::Identifier, "other"sv,
			                 name_token.line(), name_token.column(),
			                 name_token.file_index());
			auto other_node = emplace_node<ExpressionNode>(IdentifierNode(other_token));
			
			// Create arguments vector for the spaceship operator call
			ChunkedVector<ASTNode> spaceship_args;
			spaceship_args.push_back(other_node);
			
			// Create member function call: this->operator<=>(other)
			auto spaceship_call = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(this_node, const_cast<FunctionDeclarationNode&>(*spaceship_func), std::move(spaceship_args), operator_name_token));
			
			// Create numeric literal for 0
			Token zero_token(Token::Type::Literal, "0"sv,
			                name_token.line(), name_token.column(),
			                name_token.file_index());
			auto zero_node = emplace_node<ExpressionNode>(
				NumericLiteralNode(zero_token, 0ULL, Type::Int, TypeQualifier::None, 32));
			
			// Create comparison operator token for comparing result with 0
			Token comparison_token(Token::Type::Operator, op_symbol,
			                      name_token.line(), name_token.column(),
			                      name_token.file_index());
			
			// Create binary operator node: (spaceship_call) <op> 0
			auto comparison_expr = emplace_node<ExpressionNode>(
				BinaryOperatorNode(comparison_token, spaceship_call, zero_node));
			
			// Create return statement
			auto return_stmt = emplace_node<ReturnStatementNode>(
				std::optional<ASTNode>(comparison_expr), operator_name_token);
			
			// Add return statement to block
			op_block_ref.add_statement_node(return_stmt);
			
			// Generate mangled name before setting definition (Phase 6 mangling)
			compute_and_set_mangled_name(func_ref);
			func_ref.set_definition(op_block_node);
			// Mark as implicit to allow codegen to handle synthesized comparisons safely
			func_ref.set_is_implicit(true);
			
			// Add the operator to the struct type info
			struct_info->addOperatorOverload(
				op_symbol,
				func_node,
				AccessSpecifier::Public
			);
			
			// Add the operator to the struct node
			struct_ref.add_operator_overload(op_symbol, func_node, AccessSpecifier::Public);
		}
	}

	// Apply custom alignment if specified
	if (custom_alignment.has_value()) {
		struct_info->set_custom_alignment(custom_alignment.value());
	}

	// Finalize struct layout (add padding)
	// Use finalizeWithBases() if there are base classes, otherwise use finalize()
	bool finalize_success;
	if (!struct_info->base_classes.empty()) {
		finalize_success = struct_info->finalizeWithBases();
	} else {
		finalize_success = struct_info->finalize();
	}
	
	// Check for semantic errors during finalization (e.g., overriding final function)
	if (!finalize_success) {
		return ParseResult::error(struct_info->getFinalizationError(), Token());
	}

	// Check if template class has static members before moving struct_info
	bool has_static_members = false;
	if (parsing_template_class_ && struct_info) {
		has_static_members = !struct_info->static_members.empty();
	}

	// Store struct info in type info
	struct_type_info.setStructInfo(std::move(struct_info));
	// Update type_size_ from the finalized struct's total size
	if (struct_type_info.getStructInfo()) {
		struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
	}

	// If this is a nested class, also register it with its qualified name
	if (struct_ref.is_nested()) {
		auto qualified_name = struct_ref.qualified_name();
		// Register the qualified name as an alias in gTypesByName
		// It points to the same TypeInfo as the simple name
		if (gTypesByName.find(qualified_name) == gTypesByName.end()) {
			gTypesByName.emplace(qualified_name, &struct_type_info);
		}
	}

	// Now parse all delayed inline function bodies
	// At this point, all members are visible in the complete-class context

	// If we're parsing a template class that has static members, DON'T parse the bodies now
	// Instead, store them for parsing during template instantiation (two-phase lookup)
	// This allows static member lookups to work because TypeInfo will exist at instantiation time
	// For templates without static members, parse bodies normally to preserve template parameter access
	if (parsing_template_class_ && has_static_members) {
		// Convert DelayedFunctionBody to DeferredTemplateMemberBody for storage
		pending_template_deferred_bodies_.clear();
		for (const auto& delayed : delayed_function_bodies_) {
			DeferredTemplateMemberBody deferred;
			
			// Get function name for matching during instantiation
			StringHandle func_name;
			bool is_const_method = false;
			if (delayed.is_constructor && delayed.ctor_node) {
				func_name = delayed.ctor_node->name();
			} else if (delayed.is_destructor && delayed.dtor_node) {
				func_name = delayed.dtor_node->name();
			} else if (delayed.func_node) {
				const auto& decl = delayed.func_node->decl_node();
				func_name = decl.identifier_token().handle();
				// is_const is stored in StructMemberFunctionDecl, not in FunctionDeclarationNode
				// We'll match by name only for now
			}
			
			deferred.function_name = func_name;
			deferred.body_start = delayed.body_start;
			deferred.initializer_list_start = delayed.initializer_list_start;
			deferred.has_initializer_list = delayed.has_initializer_list;
			deferred.struct_name = delayed.struct_name;  // string_view from token (persistent)
			deferred.struct_type_index = delayed.struct_type_index;
			deferred.is_constructor = delayed.is_constructor;
			deferred.is_destructor = delayed.is_destructor;
			deferred.is_const_method = is_const_method;
			deferred.template_param_names = delayed.template_param_names;
			pending_template_deferred_bodies_.push_back(std::move(deferred));
		}
		
		// Clear the delayed bodies list - they're now in pending_template_deferred_bodies_
		delayed_function_bodies_.clear();
		
		// Return without parsing the bodies - they'll be parsed during instantiation
		return saved_position.success(struct_node);
	}


	// Save the current token position (right after the struct definition)
	// We'll restore this after parsing all delayed bodies
	SaveHandle position_after_struct = save_token_position();

	// Parse all delayed function bodies using unified helper (Phase 5)
	for (auto& delayed : delayed_function_bodies_) {
		// Restore token position to the start of the function body
		restore_token_position(delayed.body_start);

		// Use Phase 5 unified delayed body parsing
		std::optional<ASTNode> body;
		auto result = parse_delayed_function_body(delayed, body);
		if (result.is_error()) {
			// Stack will be popped by the RAII guard
			return result;
		}
	}

	// Clear the delayed bodies list for the next struct
	delayed_function_bodies_.clear();

	// Restore token position to right after the struct definition
	// This ensures the parser continues from the correct position
	restore_token_position(position_after_struct);

	// Stack will be popped by the RAII guard

	// Store variable declarations for later processing
	// They will be added to the AST by the caller
	pending_struct_variables_ = std::move(struct_variables);

	return saved_position.success(struct_node);
}

ParseResult Parser::parse_enum_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'enum' keyword
	auto enum_keyword = advance();
	if (enum_keyword.kind() != "enum"_tok) {
		return ParseResult::error("Expected 'enum' keyword", enum_keyword);
	}

	// Check for 'class' or 'struct' keyword (enum class / enum struct)
	bool is_scoped = false;
	if (peek().is_keyword() &&
	    (peek() == "class"_tok || peek() == "struct"_tok)) {
		is_scoped = true;
		advance(); // consume 'class' or 'struct'
	}

	// Parse enum name (optional for anonymous enums)
	StringHandle enum_name;
	//bool is_anonymous = false;
	
	// Check if next token is an identifier (name) or : or { (anonymous enum)
	if (peek().is_identifier()) {
		auto name_token = advance();
		enum_name = name_token.handle();
	} else if (!peek().is_eof() && 
	           (peek() == ":"_tok || peek() == "{"_tok)) {
		// Anonymous enum - generate a unique name
		static int anonymous_enum_counter = 0;
		enum_name = StringTable::getOrInternStringHandle(StringBuilder().append("__anonymous_enum_").append(std::to_string(anonymous_enum_counter++)));
		//is_anonymous = true;
	} else {
		return ParseResult::error("Expected enum name, ':', or '{'", peek_info());
	}

	// Register the enum type in the global type system EARLY
	TypeInfo& enum_type_info = add_enum_type(enum_name);

	// Create enum declaration node
	auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name, is_scoped);

	// Check for underlying type specification (: type)
	if (peek() == ":"_tok) {
		advance(); // consume ':'

		// Parse the underlying type
		auto underlying_type_result = parse_type_specifier();
		if (underlying_type_result.is_error()) {
			return underlying_type_result;
		}

		if (auto type_node = underlying_type_result.node()) {
			enum_ref.set_underlying_type(*type_node);
		}
	}

	// Check for forward declaration (semicolon without body)
	// C++11: enum class Name : underlying_type;
	// This is a forward declaration, not a definition
	FLASH_LOG(Parser, Debug, "Checking for enum forward declaration, peek_token has_value=", !peek().is_eof(),
	          !peek().is_eof() ? (std::string(" value='") + std::string(peek_info().value()) + "'") : "");
	if (peek() == ";"_tok) {
		// This is a forward declaration
		advance(); // Consume the semicolon
		
		// For scoped enums with underlying type, forward declarations are valid C++11
		// We mark this as a forward declaration
		enum_ref.set_is_forward_declaration(true);
		
		// Set size on TypeInfo for forward-declared enum (use type_size_)
		if (enum_ref.has_underlying_type()) {
			const auto& type_spec = enum_ref.underlying_type()->as<TypeSpecifierNode>();
			enum_type_info.type_size_ = type_spec.size_in_bits();
		} else if (is_scoped) {
			// Scoped enums without underlying type default to int (32 bits)
			enum_type_info.type_size_ = 32;
		}
		
		FLASH_LOG(Parser, Debug, "Parsed enum forward declaration: ", std::string(StringTable::getStringView(enum_name)));
		return saved_position.success(enum_node);
	}

	// Expect opening brace for full definition
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' after enum name", peek_info());
	}

	// Create enum type info
	auto enum_info = std::make_unique<EnumTypeInfo>(enum_name, is_scoped);

	// Determine underlying type (default is int)
	Type underlying_type = Type::Int;
	int underlying_size = 32;
	if (enum_ref.has_underlying_type()) {
		const auto& type_spec = enum_ref.underlying_type()->as<TypeSpecifierNode>();
		underlying_type = type_spec.type();
		underlying_size = type_spec.size_in_bits();
	}
	enum_info->underlying_type = underlying_type;
	enum_info->underlying_size = underlying_size;

	// Store enum info early so ConstExprEvaluator can look up values during parsing
	enum_type_info.setEnumInfo(std::move(enum_info));
	auto* live_enum_info = enum_type_info.getEnumInfo();

	// Parse enumerators
	long long next_value = 0;
	// For scoped enums, push a temporary scope so that enumerator names
	// are visible to subsequent value expressions (C++ §9.7.1/2)
	if (is_scoped) {
		gSymbolTable.enter_scope(ScopeType::Block);
	}
	while (!peek().is_eof() && peek() != "}"_tok) {
		// Parse enumerator name
		auto enumerator_name_token = advance();
		if (!enumerator_name_token.kind().is_identifier()) {
			if (is_scoped) gSymbolTable.exit_scope();
			return ParseResult::error("Expected enumerator name", enumerator_name_token);
		}

		std::string_view enumerator_name = enumerator_name_token.value();
		std::optional<ASTNode> enumerator_value;
		long long value = next_value;

		// Check for explicit value (= expression)
		if (peek() == "="_tok) {
			advance(); // consume '='

			auto value_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (value_result.is_error()) {
				if (is_scoped) gSymbolTable.exit_scope();
				return value_result;
			}

			if (auto value_node = value_result.node()) {
				enumerator_value = *value_node;

				// Try to evaluate constant expression
				bool value_extracted = false;
				if (value_node->is<ExpressionNode>()) {
					const auto& expr = value_node->as<ExpressionNode>();
					if (std::holds_alternative<NumericLiteralNode>(expr)) {
						const auto& literal = std::get<NumericLiteralNode>(expr);
						const auto& literal_value = literal.value();
						if (std::holds_alternative<unsigned long long>(literal_value)) {
							value = static_cast<long long>(std::get<unsigned long long>(literal_value));
							value_extracted = true;
						} else if (std::holds_alternative<double>(literal_value)) {
							value = static_cast<long long>(std::get<double>(literal_value));
							value_extracted = true;
						}
					}
				}
				// Fallback: use ConstExprEvaluator for complex expressions
				if (!value_extracted) {
					ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(*value_node, eval_ctx);
					if (eval_result.success()) {
						value = eval_result.as_int();
					}
				}
			}
		}

		// Create enumerator node
		auto enumerator_node = emplace_node<EnumeratorNode>(enumerator_name_token, enumerator_value);
		enum_ref.add_enumerator(enumerator_node);

		// Add enumerator to enum type info
		// Phase 7B: Intern enumerator name and use StringHandle overload
		StringHandle enumerator_name_handle = StringTable::getOrInternStringHandle(enumerator_name);
		live_enum_info->addEnumerator(enumerator_name_handle, value);

		// Add enumerator to current scope as DeclarationNode so codegen and
		// ConstExprEvaluator (via gTypeInfo enum lookup) can both find it
		{
			auto enum_type_node = emplace_node<TypeSpecifierNode>(
				Type::Enum, enum_type_info.type_index_, underlying_size, enumerator_name_token);
			auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_name_token);
			gSymbolTable.insert(enumerator_name, enumerator_decl);
		}

		next_value = value + 1;

		// Check for comma or closing brace
		if (peek() == ","_tok) {
			advance(); // consume ','
			// Allow trailing comma before '}'
			if (peek() == "}"_tok) {
				break;
			}
		} else if (peek() == "}"_tok) {
			break;
		} else {
			if (is_scoped) gSymbolTable.exit_scope();
			return ParseResult::error("Expected ',' or '}' after enumerator", peek_info());
		}
	}

	// Pop temporary scope for scoped enums
	if (is_scoped) {
		gSymbolTable.exit_scope();
	}

	// Expect closing brace
	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' after enum body", peek_info());
	}

	// Optional semicolon
	consume(";"_tok);

	// enum_info was already stored in gTypeInfo before the loop

	return saved_position.success(enum_node);
}

ParseResult Parser::parse_static_assert()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'static_assert' keyword
	auto static_assert_keyword = advance();
	if (static_assert_keyword.kind() != "static_assert"_tok) {
		return ParseResult::error("Expected 'static_assert' keyword", static_assert_keyword);
	}

	// Expect opening parenthesis
	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' after 'static_assert'", current_token_);
	}

	// Parse the condition expression
	ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (condition_result.is_error()) {
		return condition_result;
	}

	// Check for optional comma and message
	std::string message;
	if (consume(","_tok)) {
		// Parse the message string literal(s) - C++ allows adjacent string literals to be concatenated
		while (peek().is_string_literal()) {
			auto message_token = advance();
			if (message_token.value().size() >= 2 && 
			    message_token.value().front() == '"' && 
			    message_token.value().back() == '"') {
				// Extract the message content (remove quotes) and append
				message += std::string(message_token.value().substr(1, message_token.value().size() - 2));
			}
		}
		if (message.empty()) {
			return ParseResult::error("Expected string literal for static_assert message", current_token_);
		}
	}

	// Expect closing parenthesis
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after static_assert", current_token_);
	}

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after static_assert", current_token_);
	}

	// If we're inside a template body during template DEFINITION (not instantiation),
	// defer static_assert evaluation until instantiation.
	// However, if we can evaluate it now (non-dependent expression), we should do so to catch errors early.
	// The expression may depend on template parameters that are not yet known
	bool is_in_template_definition = parsing_template_body_ && !current_template_param_names_.empty();
	
	// Also consider struct parsing context - if we're inside a template struct body,
	// member function bodies may be parsed later but still contain template-dependent expressions
	bool is_in_template_struct = !struct_parsing_context_stack_.empty() && 
		(parsing_template_body_ || !current_template_param_names_.empty());
	
	// Try to evaluate the constant expression using ConstExprEvaluator
	ConstExpr::EvaluationContext ctx(gSymbolTable);
	ctx.parser = this;  // Enable template function instantiation
	
	// Pass struct context for static member lookup in static_assert within struct body
	if (!struct_parsing_context_stack_.empty()) {
		const auto& struct_ctx = struct_parsing_context_stack_.back();
		ctx.struct_node = struct_ctx.struct_node;
		ctx.struct_info = struct_ctx.local_struct_info;
	}
	
	auto eval_result = ConstExpr::Evaluator::evaluate(*condition_result.node(), ctx);
	
	// If evaluation failed with a template-dependent expression error, defer only in template context.
	// In non-template code, fall through to error handling (e.g. sizeof returning 0 for incomplete types).
	if (!eval_result.success() && eval_result.error_type == ConstExpr::EvalErrorType::TemplateDependentExpression) {
		if (is_in_template_definition || is_in_template_struct) {
			FLASH_LOG(Templates, Debug, "Deferring static_assert with template-dependent expression: ", eval_result.error_message);
			
			// Store the deferred static_assert in the current struct/class for later evaluation
			if (!struct_parsing_context_stack_.empty()) {
				const auto& struct_ctx = struct_parsing_context_stack_.back();
				if (struct_ctx.struct_node) {
					StringHandle message_handle = StringTable::getOrInternStringHandle(message);
					struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
					FLASH_LOG(Templates, Debug, "Stored deferred static_assert in struct '", 
					          struct_ctx.struct_node->name(), "' for later evaluation");
				}
			}
			
			return saved_position.success();
		}
		// Not in template context - fall through to error handling below
	}
	
	// If we're in a template definition and evaluation failed for other reasons,
	// that's okay - skip it and it will be checked during instantiation
	if ((is_in_template_definition || is_in_template_struct) && !eval_result.success()) {
		FLASH_LOG(Templates, Debug, "static_assert evaluation failed in template body: ", eval_result.error_message);
		
		// Store the deferred static_assert in the current struct/class for later evaluation
		if (!struct_parsing_context_stack_.empty()) {
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			if (struct_ctx.struct_node) {
				StringHandle message_handle = StringTable::getOrInternStringHandle(message);
				struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
			}
		}
		
		return saved_position.success();
	}
	
	if (!eval_result.success()) {
		// If we're inside a struct body, defer - our constexpr evaluator is incomplete
		// and many standard library static_asserts use complex constexpr functions
		if (!struct_parsing_context_stack_.empty()) {
			FLASH_LOG(Parser, Debug, "Deferring static_assert with unevaluable condition in struct body: ", eval_result.error_message);
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			if (struct_ctx.struct_node) {
				StringHandle message_handle = StringTable::getOrInternStringHandle(message);
				struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
			}
			return saved_position.success();
		}
		return ParseResult::error(
			"static_assert condition is not a constant expression: " + eval_result.error_message,
			static_assert_keyword
		);
	}

	// Check if the assertion failed
	if (!eval_result.as_bool()) {
		// In template contexts, static_assert may evaluate to false because
		// type traits like is_constructible<_Tp, _Args...> return false_type
		// for unknown/dependent types. Defer instead of failing.
		if (is_in_template_definition || is_in_template_struct) {
			FLASH_LOG(Templates, Debug, "Deferring static_assert that evaluated to false in template context");
			if (!struct_parsing_context_stack_.empty()) {
				const auto& struct_ctx = struct_parsing_context_stack_.back();
				if (struct_ctx.struct_node) {
					StringHandle message_handle = StringTable::getOrInternStringHandle(message);
					struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
				}
			}
			return saved_position.success();
		}
		std::string error_msg = "static_assert failed";
		if (!message.empty()) {
			error_msg += ": " + message;
		}
		return ParseResult::error(error_msg, static_assert_keyword);
	}

	// static_assert passed - just skip it
	return saved_position.success();
}

// Helper function to try parsing a function pointer member in struct/union context
// Pattern: type (*name)(params);
// This assumes parse_type_specifier has already been called and the next token is '('
// Returns std::optional<StructMember> - empty if not a function pointer pattern
std::optional<StructMember> Parser::try_parse_function_pointer_member()
{
	// Check for function pointer pattern: '(' followed by '*'
	if (peek() != "("_tok) {
		return std::nullopt;
	}
	
	SaveHandle funcptr_saved_pos = save_token_position();
	advance(); // consume '('
	
	if (peek() != "*"_tok) {
		restore_token_position(funcptr_saved_pos);
		return std::nullopt;
	}
	advance(); // consume '*'
	
	// Parse optional CV-qualifiers after *
	parse_cv_qualifiers();
	
	// Parse function pointer name
	if (!peek().is_identifier()) {
		restore_token_position(funcptr_saved_pos);
		return std::nullopt;
	}
	Token funcptr_name_token = peek_info();
	advance(); // consume the name
	
	// Expect closing ')' after the name
	if (peek() != ")"_tok) {
		restore_token_position(funcptr_saved_pos);
		return std::nullopt;
	}
	advance(); // consume ')'
	
	// Expect '(' for function parameters
	if (peek() != "("_tok) {
		restore_token_position(funcptr_saved_pos);
		return std::nullopt;
	}
	
	// Parse function parameters - skip through until matching ')'
	advance(); // consume '('
	int paren_depth = 1;
	while (!peek().is_eof() && paren_depth > 0) {
		if (peek() == "("_tok) {
			paren_depth++;
		} else if (peek() == ")"_tok) {
			paren_depth--;
		}
		advance();
	}
	
	// Expect semicolon after function pointer declaration
	if (peek() != ";"_tok) {
		restore_token_position(funcptr_saved_pos);
		return std::nullopt;
	}
	advance(); // consume ';'
	
	// Create StructMember for the function pointer
	// Use pointer size from target architecture (defaulting to 64-bit)
	constexpr size_t pointer_size = sizeof(void*);
	constexpr size_t pointer_alignment = alignof(void*);
	
	StringHandle funcptr_name_handle = funcptr_name_token.handle();
	
	discard_saved_token(funcptr_saved_pos);
	
	return StructMember{
		funcptr_name_handle,
		Type::FunctionPointer,
		0,  // type_index for function pointers
		0,  // offset will be calculated later
		pointer_size,
		pointer_alignment,
		AccessSpecifier::Public,
		std::nullopt,  // no default initializer
		false,  // is_reference
		false,  // is_rvalue_reference
		0,      // referenced_size_bits
		false,  // is_array
		{},     // array_dimensions
		0,      // pointer_depth
		std::nullopt // bitfield_width
	};
}

// Helper function to parse members of anonymous struct/union (handles recursive nesting)
// This is used when parsing anonymous structs/unions inside typedef declarations
// Example: typedef struct { union { struct { int a; } inner; } outer; } MyStruct;
ParseResult Parser::parse_anonymous_struct_union_members(StructTypeInfo* out_struct_info, std::string_view parent_name_prefix)
{
	static int recursive_anonymous_counter = 0;
	
	while (!peek().is_eof() && peek() != "}"_tok) {
		// Check for nested named anonymous struct/union: struct { ... } member_name;
		if (peek().is_keyword() &&
		    (peek() == "union"_tok || peek() == "struct"_tok)) {
			SaveHandle nested_saved_pos = save_token_position();
			bool nested_is_union = (peek() == "union"_tok);
			advance(); // consume 'union' or 'struct'
			
			if (peek() == "{"_tok) {
				// Nested anonymous struct/union pattern - consume body and member name
				advance(); // consume '{'
				
				// Generate a unique name for the nested anonymous type
				std::string_view nested_anon_type_name = StringBuilder()
					.append(parent_name_prefix)
					.append("_")
					.append(nested_is_union ? "union_" : "struct_")
					.append(static_cast<int64_t>(recursive_anonymous_counter++))
					.commit();
				StringHandle nested_anon_type_name_handle = StringTable::getOrInternStringHandle(nested_anon_type_name);
				
				// Create the nested anonymous struct/union type
				TypeInfo& nested_anon_type_info = add_struct_type(nested_anon_type_name_handle);
				
				// Create StructTypeInfo
				auto nested_anon_struct_info_ptr = std::make_unique<StructTypeInfo>(nested_anon_type_name_handle, AccessSpecifier::Public);
				StructTypeInfo* nested_anon_struct_info = nested_anon_struct_info_ptr.get();
				
				// Set the union flag if this is a union
				if (nested_is_union) {
					nested_anon_struct_info->is_union = true;
				}
				
				// Recursively parse members of the nested anonymous struct/union
				ParseResult nested_result = parse_anonymous_struct_union_members(nested_anon_struct_info, nested_anon_type_name);
				if (nested_result.is_error()) {
					return nested_result;
				}
				
				// Expect closing brace
				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after nested anonymous struct/union members", peek_info());
				}
				
				// Calculate the layout for the nested anonymous type
				if (nested_is_union) {
					// Union layout: all members at offset 0, size is max of all member sizes
					size_t max_size = 0;
					size_t max_alignment = 1;
					for (auto& nested_member : nested_anon_struct_info->members) {
						nested_member.offset = 0;  // All union members at offset 0
						if (nested_member.size > max_size) {
							max_size = nested_member.size;
						}
						if (nested_member.alignment > max_alignment) {
							max_alignment = nested_member.alignment;
						}
					}
					nested_anon_struct_info->total_size = max_size;
					nested_anon_struct_info->alignment = max_alignment;
				} else {
					// Struct layout: sequential members with alignment
					size_t current_offset = 0;
					size_t max_alignment = 1;
					for (auto& nested_member : nested_anon_struct_info->members) {
						// Align current offset
						if (nested_member.alignment > 0) {
							current_offset = (current_offset + nested_member.alignment - 1) & ~(nested_member.alignment - 1);
						}
						nested_member.offset = current_offset;
						current_offset += nested_member.size;
						if (nested_member.alignment > max_alignment) {
							max_alignment = nested_member.alignment;
						}
					}
					// Final alignment padding
					if (max_alignment > 0) {
						current_offset = (current_offset + max_alignment - 1) & ~(max_alignment - 1);
					}
					nested_anon_struct_info->total_size = current_offset;
					nested_anon_struct_info->alignment = max_alignment;
				}
				
				// Set the struct info on the type info
				nested_anon_type_info.setStructInfo(std::move(nested_anon_struct_info_ptr));
				
				// Now parse the member name for the enclosing anonymous struct/union
				auto outer_member_name_token = peek_info();
				if (!outer_member_name_token.kind().is_identifier()) {
					return ParseResult::error("Expected member name after nested anonymous struct/union", outer_member_name_token);
				}
				advance(); // consume the member name
				
				// Calculate size for the nested anonymous type
				size_t nested_type_size = nested_anon_type_info.getStructInfo()->total_size;
				size_t nested_type_alignment = nested_anon_type_info.getStructInfo()->alignment;
				
				// Add member to the outer anonymous type
				StringHandle outer_member_name_handle = outer_member_name_token.handle();
				out_struct_info->members.push_back(StructMember{
					outer_member_name_handle,
					Type::Struct,
					nested_anon_type_info.type_index_,
					0,  // offset will be calculated later
					nested_type_size,
					nested_type_alignment,
					AccessSpecifier::Public,
					std::nullopt,  // no default initializer
					false,  // is_reference
					false,  // is_rvalue_reference
					0,      // referenced_size_bits
					false,  // is_array
					{},     // array_dimensions
					0,      // pointer_depth
					std::nullopt // bitfield_width
				});
				
				// Expect semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after nested anonymous struct/union member", current_token_);
				}
				
				discard_saved_token(nested_saved_pos);
				continue;  // Continue with next member
			} else {
				// Not an anonymous struct/union - restore position and parse normally
				restore_token_position(nested_saved_pos);
			}
		}
		
		// Parse member type normally
		auto member_type_result = parse_type_specifier();
		if (member_type_result.is_error()) {
			return member_type_result;
		}
		
		if (!member_type_result.node().has_value()) {
			return ParseResult::error("Expected type specifier in anonymous struct/union", current_token_);
		}
		
		// Handle pointer declarators
		TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
		while (peek() == "*"_tok) {
			advance(); // consume '*'
			CVQualifier ptr_cv = parse_cv_qualifiers();
			member_type_spec.add_pointer_level(ptr_cv);
		}
		
		// Check for function pointer member pattern: type (*name)(params);
		// This handles patterns like: void (*_function)(__sigval_t);
		if (auto funcptr_member = try_parse_function_pointer_member()) {
			out_struct_info->members.push_back(*funcptr_member);
			continue;  // Continue with next member
		}
		
		// Parse member name
		auto member_name_token = peek_info();
		if (!member_name_token.kind().is_identifier()) {
			return ParseResult::error("Expected member name in anonymous struct/union", member_name_token);
		}
		advance(); // consume the member name
		
		// Check for array declarator
		std::vector<ASTNode> array_dimensions;
		while (peek() == "["_tok) {
			advance(); // consume '['
			
			// Parse the array size expression
			ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}
			array_dimensions.push_back(*size_result.node());
			
			// Expect closing ']'
			if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
			    peek() != "]"_tok) {
				return ParseResult::error("Expected ']' after array size", current_token_);
			}
			advance(); // consume ']'
		}
		
		// Calculate member size and alignment
		auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
		size_t referenced_size_bits = member_size * 8;
		std::vector<size_t> resolved_array_dimensions;
		for (const auto& dim_expr : array_dimensions) {
			ConstExpr::EvaluationContext ctx(gSymbolTable);
			auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
			if (eval_result.success() && eval_result.as_int() > 0) {
				size_t dim_size = static_cast<size_t>(eval_result.as_int());
				resolved_array_dimensions.push_back(dim_size);
				member_size *= dim_size;
				referenced_size_bits *= dim_size;
			}
		}
		
		// Add member to the anonymous type
		StringHandle member_name_handle = member_name_token.handle();
		out_struct_info->members.push_back(StructMember{
			member_name_handle,
			member_type_spec.type(),
			member_type_spec.type_index(),
			0,  // offset will be calculated later
			member_size,
			member_alignment,
			AccessSpecifier::Public,
			std::nullopt,  // no default initializer
			false,  // is_reference
			false,  // is_rvalue_reference
			referenced_size_bits,
			!resolved_array_dimensions.empty(),  // is_array
			std::move(resolved_array_dimensions), // array_dimensions
			0,      // pointer_depth
			std::nullopt // bitfield_width
		});
		
		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after member in anonymous struct/union", current_token_);
		}
	}
	
	return ParseResult::success();
}

ParseResult Parser::parse_typedef_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'typedef' keyword
	auto typedef_keyword = advance();
	if (typedef_keyword.kind() != "typedef"_tok) {
		return ParseResult::error("Expected 'typedef' keyword", typedef_keyword);
	}

	// Check if this is an inline struct/class definition: typedef struct { ... } alias;
	// or typedef struct Name { ... } alias;
	bool is_inline_struct = false;
	StringHandle struct_name_for_typedef;
	TypeIndex struct_type_index = 0;

	// Check if this is an inline enum definition: typedef enum { ... } alias;
	// or typedef enum _Name { ... } alias;
	bool is_inline_enum = false;
	StringHandle enum_name_for_typedef;
	TypeIndex enum_type_index = 0;

	if (peek() == "enum"_tok) {
		// Look ahead to see if this is an inline definition
		// Pattern 1: typedef enum { ... } alias;
		// Pattern 2: typedef enum _Name { ... } alias;
		// Pattern 3: typedef enum class Name { ... } alias;
		auto next_pos = current_token_;
		advance(); // consume 'enum'

		// Check for 'class' or 'struct' keyword (enum class / enum struct)
		[[maybe_unused]] bool has_class_keyword = false;
		if (peek().is_keyword() &&
		    (peek() == "class"_tok || peek() == "struct"_tok)) {
			has_class_keyword = true;
			advance(); // consume 'class' or 'struct'
		}

		// Check if next token is '{' (anonymous enum) or identifier followed by ':' or '{'
		if (peek() == "{"_tok) {
			// Pattern 1: typedef enum { ... } alias;
			is_inline_enum = true;
			enum_name_for_typedef = StringTable::getOrInternStringHandle(StringBuilder().append("__anonymous_typedef_enum_"sv).append(ast_nodes_.size()));
		} else if (peek().is_identifier()) {
			auto enum_name_token = peek_info();
			advance(); // consume enum name

			if (!peek().is_eof() && 
			    (peek() == "{"_tok || peek() == ":"_tok)) {
				// Pattern 2: typedef enum _Name { ... } alias;
				// or typedef enum _Name : type { ... } alias;
				is_inline_enum = true;
				enum_name_for_typedef = enum_name_token.handle();
			} else {
				// Not an inline definition, restore position and parse normally
				current_token_ = next_pos;
				is_inline_enum = false;
			}
		} else {
			// Not an inline definition, restore position and parse normally
			current_token_ = next_pos;
			is_inline_enum = false;
		}
	} else if (!peek().is_eof() &&
	    (peek() == "struct"_tok || peek() == "class"_tok || peek() == "union"_tok)) {
		// Look ahead to see if this is an inline definition
		// Pattern 1: typedef struct { ... } alias;
		// Pattern 2: typedef struct Name { ... } alias;
		// Pattern 3: typedef union { ... } alias;
		// Pattern 4: typedef union Name { ... } alias;
		SaveHandle next_pos = save_token_position();
		advance(); // consume 'struct', 'class', or 'union'

		// Check if next token is '{' (anonymous struct/union) or identifier followed by '{'
		if (peek() == "{"_tok) {
			// Pattern 1/3: typedef struct/union { ... } alias;
			is_inline_struct = true;
			// Use a unique temporary name for the struct/union (will be replaced by typedef alias)
			// Use the current AST size to make it unique
			struct_name_for_typedef = StringTable::getOrInternStringHandle(StringBuilder().append("__anonymous_typedef_struct_"sv).append(ast_nodes_.size()));
			discard_saved_token(next_pos);
		} else if (peek().is_identifier()) {
			auto struct_name_token = peek_info();
			advance(); // consume struct/union name

			if (peek() == "{"_tok) {
				// Pattern 2/4: typedef struct/union Name { ... } alias;
				is_inline_struct = true;
				struct_name_for_typedef = struct_name_token.handle();
				discard_saved_token(next_pos);
			} else {
				// Not an inline definition, restore position and parse normally
				restore_token_position(next_pos);
				is_inline_struct = false;
			}
		} else {
			// Not an inline definition, restore position and parse normally
			restore_token_position(next_pos);
			is_inline_struct = false;
		}
	}

	ASTNode type_node;
	TypeSpecifierNode type_spec;

	if (is_inline_enum) {
		// Parse the inline enum definition
		// We need to manually parse the enum body since we already consumed the keyword and name

		// Register the enum type early
		TypeInfo& enum_type_info = add_enum_type(enum_name_for_typedef);
		enum_type_index = enum_type_info.type_index_;

		// Create enum declaration node
		// Note: We don't know if it's scoped yet - we'll determine from the parsing context
		bool is_scoped = false; // C-style typedef enum is typically not scoped
		auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name_for_typedef, is_scoped);

		// Check for underlying type specification (: type)
		if (peek() == ":"_tok) {
			advance(); // consume ':'

			// Parse the underlying type
			auto underlying_type_result = parse_type_specifier();
			if (underlying_type_result.is_error()) {
				return underlying_type_result;
			}

			if (auto underlying_type_node = underlying_type_result.node()) {
				enum_ref.set_underlying_type(*underlying_type_node);
			}
		}

		// Expect opening brace
		if (!consume("{"_tok)) {
			return ParseResult::error("Expected '{' in enum definition", peek_info());
		}

		// Create enum type info
		auto enum_info = std::make_unique<EnumTypeInfo>(enum_name_for_typedef, is_scoped);

		// Determine underlying type (default is int)
		int underlying_size = 32;
		if (enum_ref.has_underlying_type()) {
			const auto& type_spec_node = enum_ref.underlying_type()->as<TypeSpecifierNode>();
			underlying_size = type_spec_node.size_in_bits();
		}

		// Store enum info early so ConstExprEvaluator can look up values during parsing
		auto& enum_type_info_ref = gTypeInfo[enum_type_index];
		enum_type_info_ref.setEnumInfo(std::move(enum_info));
		auto* live_enum_info = enum_type_info_ref.getEnumInfo();

		// Parse enumerators
		int64_t next_value = 0;
		// For scoped enums, push a temporary scope so that enumerator names
		// are visible to subsequent value expressions (C++ §9.7.1/2)
		if (is_scoped) {
			gSymbolTable.enter_scope(ScopeType::Block);
		}
		while (!peek().is_eof() && peek() != "}"_tok) {
			// Parse enumerator name
			auto enumerator_name_token = advance();
			if (!enumerator_name_token.kind().is_identifier()) {
				if (is_scoped) gSymbolTable.exit_scope();
				return ParseResult::error("Expected enumerator name in enum", enumerator_name_token);
			}

			int64_t value = next_value;
			std::optional<ASTNode> enumerator_value;

			// Check for explicit value
			if (peek() == "="_tok) {
				advance(); // consume '='

				// Parse constant expression
				auto value_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (value_expr_result.is_error()) {
					if (is_scoped) gSymbolTable.exit_scope();
					return value_expr_result;
				}

				// Extract value from expression
				if (auto value_node = value_expr_result.node()) {
					enumerator_value = *value_node;
					
					bool value_extracted = false;
					if (value_node->is<ExpressionNode>()) {
						const auto& expr = value_node->as<ExpressionNode>();
						if (std::holds_alternative<NumericLiteralNode>(expr)) {
							const auto& lit = std::get<NumericLiteralNode>(expr);
							const auto& val = lit.value();
							if (std::holds_alternative<unsigned long long>(val)) {
								value = static_cast<int64_t>(std::get<unsigned long long>(val));
								value_extracted = true;
							} else if (std::holds_alternative<double>(val)) {
								value = static_cast<int64_t>(std::get<double>(val));
								value_extracted = true;
							}
						}
					}
					// Fallback: use ConstExprEvaluator for complex expressions
					if (!value_extracted) {
						ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*value_node, eval_ctx);
						if (eval_result.success()) {
							value = eval_result.as_int();
						}
					}
				}
			}

			// Add enumerator
			auto enumerator_node = emplace_node<EnumeratorNode>(enumerator_name_token, enumerator_value);
			enum_ref.add_enumerator(enumerator_node);
			// Phase 7B: Intern enumerator name and use StringHandle overload
			StringHandle enumerator_name_handle = enumerator_name_token.handle();
			live_enum_info->addEnumerator(enumerator_name_handle, value);

			// Add enumerator to current scope as DeclarationNode so codegen and
			// ConstExprEvaluator (via gTypeInfo enum lookup) can both find it
			{
				auto enum_type_node = emplace_node<TypeSpecifierNode>(
					Type::Enum, enum_type_index, underlying_size, enumerator_name_token);
				auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_name_token);
				gSymbolTable.insert(enumerator_name_token.value(), enumerator_decl);
			}

			next_value = value + 1;

			// Check for comma (more enumerators) or closing brace
			if (peek() == ","_tok) {
				advance(); // consume ','
				// Allow trailing comma before '}'
				if (peek() == "}"_tok) {
					break;
				}
			} else {
				break;
			}
		}

		// Pop temporary scope for scoped enums
		if (is_scoped) {
			gSymbolTable.exit_scope();
		}

		// Expect closing brace
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after enum enumerators", peek_info());
		}

		// enum_info was already stored in gTypeInfo before the loop

		// Add enum declaration to AST
		gSymbolTable.insert(enum_name_for_typedef, enum_node);
		ast_nodes_.push_back(enum_node);

		// Create type specifier for the typedef
		type_spec = TypeSpecifierNode(Type::Enum, TypeQualifier::None, underlying_size, typedef_keyword);
		type_spec.set_type_index(enum_type_index);
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else if (is_inline_struct) {
		// Parse the inline struct definition
		// We need to manually parse the struct body since we already consumed the keyword and name

		// Register the struct type early
		TypeInfo& struct_type_info = add_struct_type(struct_name_for_typedef);
		struct_type_index = struct_type_info.type_index_;

		// Create struct declaration node
		auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name_for_typedef, false);

		// Push struct parsing context
		struct_parsing_context_stack_.push_back({
			StringTable::getStringView(struct_name_for_typedef),
			&struct_ref,
			nullptr,
			gSymbolTable.get_current_namespace_handle(),
			{}
		});

		// Create StructTypeInfo
		auto struct_info = std::make_unique<StructTypeInfo>(struct_name_for_typedef, AccessSpecifier::Public);
		
		// Update the struct parsing context with the local_struct_info for static member lookup
		if (!struct_parsing_context_stack_.empty()) {
			struct_parsing_context_stack_.back().local_struct_info = struct_info.get();
		}

		// Apply pack alignment from #pragma pack
		size_t pack_alignment = context_.getCurrentPackAlignment();
		if (pack_alignment > 0) {
			struct_info->set_pack_alignment(pack_alignment);
		}

		// Expect opening brace
		if (!consume("{"_tok)) {
			return ParseResult::error("Expected '{' in struct definition", peek_info());
		}

		// Parse struct members (simplified version - no inheritance, no member functions for now)
		std::vector<StructMemberDecl> members;
		AccessSpecifier current_access = AccessSpecifier::Public;

		while (!peek().is_eof() && peek() != "}"_tok) {
			// Check for anonymous union/struct (union { ... };)
			if (peek().is_keyword() &&
			    (peek() == "union"_tok || peek() == "struct"_tok)) {
				// Peek ahead to see if this is anonymous (followed by '{')
				SaveHandle saved_pos = save_token_position();
				auto union_or_struct_keyword = advance(); // consume 'union' or 'struct'
				bool is_union = (union_or_struct_keyword.value() == "union");
				
				if (peek() == "{"_tok) {
					// Could be true anonymous union (union { ... };) or named anonymous (union { ... } name;)
					// Peek ahead to determine which pattern
					SaveHandle brace_start_pos = save_token_position();
					skip_balanced_braces();
					bool is_named_anonymous = false;
					if (peek().is_identifier()) {
						is_named_anonymous = true;
					}
					// Restore position to the opening brace to parse the members
					restore_token_position(brace_start_pos);
					
					// Now consume the opening brace
					advance(); // consume '{'
					
					if (is_named_anonymous) {
						// Named anonymous union/struct: union { ... } member_name;
						// Create an anonymous type and parse members into it
						
						// Generate a unique name for the anonymous union/struct type
						static int typedef_anonymous_type_counter = 0;
						std::string_view anon_type_name = StringBuilder()
							.append("__typedef_anonymous_")
							.append(is_union ? "union_" : "struct_")
							.append(static_cast<int64_t>(typedef_anonymous_type_counter++))
							.commit();
						StringHandle anon_type_name_handle = StringTable::getOrInternStringHandle(anon_type_name);
						
						// Create the anonymous struct/union type
						TypeInfo& anon_type_info = add_struct_type(anon_type_name_handle);
						
						// Create StructTypeInfo
						auto anon_struct_info_ptr = std::make_unique<StructTypeInfo>(anon_type_name_handle, AccessSpecifier::Public);
						StructTypeInfo* anon_struct_info = anon_struct_info_ptr.get();
						
						// Set the union flag if this is a union
						if (is_union) {
							anon_struct_info->is_union = true;
						}
						
						// Parse all members using the recursive helper
						ParseResult members_result = parse_anonymous_struct_union_members(anon_struct_info, anon_type_name);
						if (members_result.is_error()) {
							return members_result;
						}
						
						// Expect closing brace
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after named anonymous union/struct members in typedef", peek_info());
						}
						
						// Calculate the layout for the anonymous type
						if (is_union) {
							// Union layout: all members at offset 0, size is max of all member sizes
							size_t max_size = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								member.offset = 0;  // All union members at offset 0
								if (member.size > max_size) {
									max_size = member.size;
								}
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							anon_struct_info->total_size = max_size;
							anon_struct_info->alignment = max_alignment;
						} else {
							// Struct layout: sequential members with alignment
							size_t current_offset = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								// Align current offset
								if (member.alignment > 0) {
									current_offset = (current_offset + member.alignment - 1) & ~(member.alignment - 1);
								}
								member.offset = current_offset;
								current_offset += member.size;
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							// Final alignment padding
							if (max_alignment > 0) {
								current_offset = (current_offset + max_alignment - 1) & ~(max_alignment - 1);
							}
							anon_struct_info->total_size = current_offset;
							anon_struct_info->alignment = max_alignment;
						}
						
						// Set the struct info on the type info
						anon_type_info.setStructInfo(std::move(anon_struct_info_ptr));
						
						// Now parse the member name(s) - handle comma-separated declarators
						do {
							// Parse declarator name and pointer levels
							int ptr_levels = 0;
							while (peek() == "*"_tok) {
								advance(); // consume '*'
								ptr_levels++;
							}
							
							auto member_name_token = peek_info();
							if (!member_name_token.kind().is_identifier()) {
								return ParseResult::error("Expected member name after named anonymous union/struct in typedef", member_name_token);
							}
							advance(); // consume the member name
							
							// Create type specifier for the anonymous type
							TypeSpecifierNode anon_type_spec(Type::Struct, TypeQualifier::None, 
								static_cast<int>(anon_type_info.getStructInfo()->total_size * 8), union_or_struct_keyword);
							anon_type_spec.set_type_index(anon_type_info.type_index_);
							for (int i = 0; i < ptr_levels; i++) {
								anon_type_spec.add_pointer_level(CVQualifier::None);
							}
							
							// Create declaration node
							ASTNode type_node_for_member = emplace_node<TypeSpecifierNode>(anon_type_spec);
							ASTNode member_decl_node = emplace_node<DeclarationNode>(type_node_for_member, member_name_token);
							
							// Add as member of enclosing struct
							members.push_back({member_decl_node, current_access, std::nullopt});
							struct_ref.add_member(member_decl_node, current_access, std::nullopt);
							
						} while (peek() == ","_tok && (advance(), true));
						
						// Expect semicolon after the member declarations
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after named anonymous union/struct member in typedef", current_token_);
						}
						
						discard_saved_token(saved_pos);
						discard_saved_token(brace_start_pos);
						continue;  // Skip to next member
					}
					
					// True anonymous union/struct - parse and flatten members (original code path)
					// Parse all members of the anonymous union
					std::vector<StructMemberDecl> anon_members;
					while (!peek().is_eof() && peek() != "}"_tok) {
						// Check for nested anonymous union
						if (peek().is_keyword() &&
						    (peek() == "union"_tok || peek() == "struct"_tok)) {
							SaveHandle nested_saved_pos = save_token_position();
							advance(); // consume 'union' or 'struct'
							
							if (peek() == "{"_tok) {
								// Nested anonymous union - parse recursively
								advance(); // consume '{'
								
								// Parse nested anonymous union members
								while (!peek().is_eof() && peek() != "}"_tok) {
									// Parse member type
									auto nested_member_type_result = parse_type_specifier();
									if (nested_member_type_result.is_error()) {
										return nested_member_type_result;
									}
									
									if (!nested_member_type_result.node().has_value()) {
										return ParseResult::error("Expected type specifier in nested anonymous union", current_token_);
									}
									
									// Handle pointer declarators
									TypeSpecifierNode& nested_member_type_spec = nested_member_type_result.node()->as<TypeSpecifierNode>();
									while (peek() == "*"_tok) {
										advance(); // consume '*'
										CVQualifier ptr_cv = parse_cv_qualifiers();
										nested_member_type_spec.add_pointer_level(ptr_cv);
									}
									
									// Parse member name
									auto nested_member_name_token = peek_info();
									if (!nested_member_name_token.kind().is_identifier()) {
										return ParseResult::error("Expected member name in nested anonymous union", nested_member_name_token);
									}
									advance(); // consume the member name
									
									// Check for array declarator
									std::vector<ASTNode> nested_array_dimensions;
									while (peek() == "["_tok) {
										advance(); // consume '['
										
										// Parse the array size expression
										ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (size_result.is_error()) {
											return size_result;
										}
										nested_array_dimensions.push_back(*size_result.node());
										
										// Expect closing ']'
										if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
										    peek() != "]"_tok) {
											return ParseResult::error("Expected ']' after array size", current_token_);
										}
										advance(); // consume ']'
									}
									
									// Create member declaration for nested union member
									ASTNode nested_member_decl_node;
									if (!nested_array_dimensions.empty()) {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token, std::move(nested_array_dimensions));
									} else {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token);
									}
									// Flatten nested union members into outer union
									anon_members.push_back({nested_member_decl_node, current_access, std::nullopt});
									
									// Expect semicolon
									if (!consume(";"_tok)) {
										return ParseResult::error("Expected ';' after nested anonymous union member", current_token_);
									}
								}
								
								// Expect closing brace for nested union
								if (!consume("}"_tok)) {
									return ParseResult::error("Expected '}' after nested anonymous union members", peek_info());
								}
								
								// Expect semicolon after nested anonymous union
								if (!consume(";"_tok)) {
									return ParseResult::error("Expected ';' after nested anonymous union", current_token_);
								}
								
								discard_saved_token(nested_saved_pos);
								continue; // Continue with next member of outer union
							} else {
								// Named union/struct - restore position and parse normally
								restore_token_position(nested_saved_pos);
							}
						}
						
						// Parse member type
						auto anon_member_type_result = parse_type_specifier();
						if (anon_member_type_result.is_error()) {
							return anon_member_type_result;
						}
						
						if (!anon_member_type_result.node().has_value()) {
							return ParseResult::error("Expected type specifier in anonymous union", current_token_);
						}
						
						// Handle pointer declarators
						TypeSpecifierNode& anon_member_type_spec = anon_member_type_result.node()->as<TypeSpecifierNode>();
						while (peek() == "*"_tok) {
							advance(); // consume '*'
							CVQualifier ptr_cv = parse_cv_qualifiers();
							anon_member_type_spec.add_pointer_level(ptr_cv);
						}
						
						// Parse member name
						auto anon_member_name_token = peek_info();
						if (!anon_member_name_token.kind().is_identifier()) {
							return ParseResult::error("Expected member name in anonymous union", anon_member_name_token);
						}
						advance(); // consume the member name
						
						// Check for array declarator
						std::vector<ASTNode> anon_array_dimensions;
						while (peek() == "["_tok) {
							advance(); // consume '['
							
							// Parse the array size expression
							ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (size_result.is_error()) {
								return size_result;
							}
							anon_array_dimensions.push_back(*size_result.node());
							
							// Expect closing ']'
							if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
							    peek() != "]"_tok) {
								return ParseResult::error("Expected ']' after array size", current_token_);
							}
							advance(); // consume ']'
						}
						
						// Create member declaration
						ASTNode anon_member_decl_node;
						if (!anon_array_dimensions.empty()) {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token, std::move(anon_array_dimensions));
						} else {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token);
						}
						anon_members.push_back({anon_member_decl_node, current_access, std::nullopt});
						
						// Expect semicolon
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after anonymous union member", current_token_);
						}
					}
					
					// Expect closing brace
					if (!consume("}"_tok)) {
						return ParseResult::error("Expected '}' after anonymous union members", peek_info());
					}
					
					// Expect semicolon after anonymous union
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after anonymous union", current_token_);
					}
					
					// Flatten anonymous union members into parent struct
					// All members of an anonymous union share the same offset
					for (const auto& anon_member : anon_members) {
						members.push_back(anon_member);
						struct_ref.add_member(anon_member.declaration, anon_member.access, anon_member.default_initializer);
					}
					
					discard_saved_token(saved_pos);
					continue;
				} else {
					// Named union/struct - restore and parse as type
					restore_token_position(saved_pos);
				}
			}
			
			// Parse member declaration
			auto member_type_result = parse_type_specifier();
			if (member_type_result.is_error()) {
				return member_type_result;
			}

			if (!member_type_result.node().has_value()) {
				return ParseResult::error("Expected type specifier in struct member", current_token_);
			}

			// Handle pointer declarators with CV-qualifiers (e.g., "unsigned short const* _locale_pctype")
			// Parse pointer declarators: * [const] [volatile] *...
			TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
			while (peek() == "*"_tok) {
				advance(); // consume '*'

				// Check for CV-qualifiers after the *
				CVQualifier ptr_cv = parse_cv_qualifiers();

				// Add pointer level to the type specifier
				member_type_spec.add_pointer_level(ptr_cv);
			}

			// Parse member name
			auto member_name_token = peek_info();
			if (!member_name_token.kind().is_identifier()) {
				return ParseResult::error("Expected member name in struct", member_name_token);
			}
			advance(); // consume the member name

			// Check for array declarator: '[' size ']' or multidimensional '[' size1 '][' size2 ']'...
			std::vector<ASTNode> array_dimensions;
			while (peek() == "["_tok) {
				advance(); // consume '['

				// Parse the array size expression
				ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (size_result.is_error()) {
					return size_result;
				}
				array_dimensions.push_back(*size_result.node());

				// Expect closing ']'
				if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
				    peek() != "]"_tok) {
					return ParseResult::error("Expected ']' after array size", current_token_);
				}
				advance(); // consume ']'
			}

			std::optional<size_t> bitfield_width;
			std::optional<ASTNode> bitfield_width_expr;
			// Handle bitfield declarations: unsigned int field:8;
			if (peek() == ":"_tok) {
				advance(); // consume ':'
				auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
				if (width_result.is_error()) {
					return width_result;
				}
				if (width_result.node().has_value()) {
					ConstExpr::EvaluationContext ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
					if (!eval_result.success() || eval_result.as_int() < 0) {
						// Defer evaluation for template non-type parameters
						bitfield_width_expr = *width_result.node();
					} else {
						bitfield_width = static_cast<size_t>(eval_result.as_int());
					}
				}
			}

			// Create member declaration
			ASTNode member_decl_node;
			if (!array_dimensions.empty()) {
				member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token, std::move(array_dimensions));
			} else {
				member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token);
			}
			members.push_back({member_decl_node, current_access, std::nullopt, bitfield_width});
			members.back().bitfield_width_expr = bitfield_width_expr;
			struct_ref.add_member(member_decl_node, current_access, std::nullopt, bitfield_width, bitfield_width_expr);

			// Handle comma-separated declarations (e.g., int x, y, z;)
			while (peek() == ","_tok) {
				advance(); // consume ','

				// Parse the next member name
				auto next_member_name = advance();
				if (!next_member_name.kind().is_identifier()) {
					return ParseResult::error("Expected member name after comma", current_token_);
				}

				std::optional<size_t> additional_bitfield_width;
				std::optional<ASTNode> additional_bitfield_width_expr;
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
					if (width_result.is_error()) {
						return width_result;
					}
					if (width_result.node().has_value()) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
						if (!eval_result.success() || eval_result.as_int() < 0) {
							// Defer evaluation for template non-type parameters
							additional_bitfield_width_expr = *width_result.node();
						} else {
							additional_bitfield_width = static_cast<size_t>(eval_result.as_int());
						}
					}
				}

				// Create declaration with same type
				auto next_member_decl = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(member_type_spec),
					next_member_name
				);
				members.push_back({next_member_decl, current_access, std::nullopt, additional_bitfield_width});
				members.back().bitfield_width_expr = additional_bitfield_width_expr;
				struct_ref.add_member(next_member_decl, current_access, std::nullopt, additional_bitfield_width, additional_bitfield_width_expr);
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after struct member", current_token_);
			}
		}

		// Expect closing brace
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after struct members", peek_info());
		}

		// Pop struct parsing context
		struct_parsing_context_stack_.pop_back();

		// Calculate struct layout
		for (const auto& member_decl : members) {
			const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
			const TypeSpecifierNode& member_type_spec = decl.type_node().as<TypeSpecifierNode>();

			// Calculate member size and alignment
			auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
			size_t referenced_size_bits = member_type_spec.size_in_bits();

			if (member_type_spec.type() == Type::Struct) {
				const TypeInfo* member_type_info = nullptr;
				for (const auto& ti : gTypeInfo) {
					if (ti.type_index_ == member_type_spec.type_index()) {
						member_type_info = &ti;
						break;
					}
				}

				if (member_type_info && member_type_info->getStructInfo()) {
					member_size = member_type_info->getStructInfo()->total_size;
					referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
					member_alignment = member_type_info->getStructInfo()->alignment;
				}
			}

			bool is_ref_member = member_type_spec.is_reference();
			bool is_rvalue_ref_member = member_type_spec.is_rvalue_reference();
			if (is_ref_member) {
				// Size and alignment were already set correctly above for references
				referenced_size_bits = referenced_size_bits ? referenced_size_bits : member_type_spec.size_in_bits();
			}
			// Phase 7B: Intern member name and use StringHandle overload
			StringHandle member_name_handle = decl.identifier_token().handle();
			struct_info->addMember(
				member_name_handle,
				member_type_spec.type(),
				member_type_spec.type_index(),
				member_size,
				member_alignment,
				member_decl.access,
				member_decl.default_initializer,
				is_ref_member,
				is_rvalue_ref_member,
				referenced_size_bits,
				false,
				{},
				static_cast<int>(member_type_spec.pointer_depth()),
				member_decl.bitfield_width
			);
		}

		// Finalize struct layout (add padding)
		if (!struct_info->finalize()) {
			return ParseResult::error(struct_info->getFinalizationError(), Token());
		}

		// Store struct info
		struct_type_info.setStructInfo(std::move(struct_info));
		// Update type_size_ from the finalized struct's total size
		if (struct_type_info.getStructInfo()) {
			struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
		}

		// Create type specifier for the struct
		// Note: Use struct_type_info.getStructInfo() since struct_info was moved above
		type_spec = TypeSpecifierNode(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_type_info.getStructInfo()->total_size * 8),
			Token(Token::Type::Identifier, StringTable::getStringView(struct_name_for_typedef), 0, 0, 0)
		);
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else {
		// Parse the underlying type normally
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type specifier after 'typedef'", current_token_);
		}

		type_node = *type_result.node();
		type_spec = type_node.as<TypeSpecifierNode>();

		// Handle pointer/reference declarators (ptr-operator in C++20 grammar)
		// consume_pointer_ref_modifiers handles *, cv-qualifiers, MSVC modifiers, &, &&
		consume_pointer_ref_modifiers(type_spec);
		
		// Check for pointer-to-member type syntax: typedef Type Class::* alias;
		// This is used in <type_traits> for result_of patterns
		// Pattern: typedef _Res _Class::* _MemPtr;
		if (peek().is_identifier()) {
			// Look ahead to see if this is Class::* pattern
			SaveHandle saved_pos = save_token_position();
			Token class_token = peek_info();
			advance(); // consume potential class name
			
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					// This is a pointer-to-member type: Type Class::*
					// Mark the type as a pointer-to-member
					type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
					type_spec.set_member_class_name(class_token.handle());
					FLASH_LOG(Parser, Debug, "Parsed pointer-to-member typedef: ", type_spec.token().value(), " ", class_token.value(), "::*");
					discard_saved_token(saved_pos);
				} else {
					// Not a pointer-to-member, restore position
					restore_token_position(saved_pos);
				}
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		}
	}

	// Check for function pointer typedef: typedef return_type (*alias_name)(params);
	// Pattern: '(' '*' identifier ')' '(' params ')'
	bool is_function_pointer_typedef = false;
	std::string_view function_pointer_alias_name;
	if (peek() == "("_tok) {
		// Peek ahead to check if this is a function pointer pattern
		SaveHandle paren_saved = save_token_position();
		advance(); // consume '('
		
		parse_calling_convention();
		
		if (peek() == "*"_tok) {
			advance(); // consume '*'
			
			// Now expect the alias name identifier
			skip_cpp_attributes();
			skip_gcc_attributes();
			if (peek().is_identifier()) {
				function_pointer_alias_name = peek_info().value();
				advance(); // consume alias name
				
				// Expect closing ')'
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					
					// Now expect '(' for the parameter list
					if (peek() == "("_tok) {
						// This is a function pointer typedef!
						is_function_pointer_typedef = true;
						discard_saved_token(paren_saved);
						
						// Parse the parameter list
						advance(); // consume '('
						
						// Skip the parameter list by counting parentheses
						int paren_depth = 1;
						while (paren_depth > 0 && !peek().is_eof()) {
							auto token = peek_info();
							if (token.value() == "(") {
								paren_depth++;
							} else if (token.value() == ")") {
								paren_depth--;
							}
							advance();
						}
						
						// We've consumed through the closing ')' of the parameter list
					}
				}
			}
		}
		
		// If not a function pointer typedef, restore position
		if (!is_function_pointer_typedef) {
			restore_token_position(paren_saved);
		}
	}

	std::string_view alias_name;
	std::optional<Token> alias_token;
	
	if (is_function_pointer_typedef) {
		alias_name = function_pointer_alias_name;
		// Create a synthetic token for the alias name (use file index 0 since it's synthetic)
		alias_token = Token(Token::Type::Identifier, function_pointer_alias_name, 0, 0, 0);
		
		// For function pointer typedefs, create a proper FunctionPointer type
		// The return type is in type_spec, create a function pointer type with it
		Type return_type = type_spec.type();
		
		// Create a new TypeSpecifierNode for the function pointer (64-bit pointer)
		TypeSpecifierNode fp_type(Type::FunctionPointer, TypeQualifier::None, 64);
		
		// Create a basic function signature with the return type
		// Note: We don't have full parameter info here since we just skipped the param list
		// This is a simplified implementation that handles the common case
		FunctionSignature sig;
		sig.return_type = return_type;
		sig.linkage = Linkage::None;
		fp_type.set_function_signature(sig);
		
		// Replace type_spec with the function pointer type
		type_spec = fp_type;
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else {
		// Parse the alias name (identifier)
		alias_token = advance();
		if (!alias_token->kind().is_identifier()) {
			return ParseResult::error("Expected identifier after type in typedef", *alias_token);
		}
		alias_name = alias_token->value();
	}

	// Check for function type typedef: typedef return_type name(params);
	// This is different from function pointer typedef: typedef return_type (*name)(params);
	if (peek() == "("_tok) {
		// This is a function type typedef
		// Parse the parameter list by skipping to the closing ')'
		advance(); // consume '('
		
		int paren_depth = 1;
		while (paren_depth > 0 && !peek().is_eof()) {
			auto token = peek_info();
			if (token.value() == "(") {
				paren_depth++;
			} else if (token.value() == ")") {
				paren_depth--;
			}
			advance();
		}
		
		// After consuming the closing ')', we should be at the semicolon
		// (or potentially attribute specifiers, which we'll skip in the semicolon check)
	}

	// Check for array typedef: typedef type name[size];
	// This creates a type alias for an array type
	if (peek() == "["_tok) {
		// Parse array dimensions
		while (peek() == "["_tok) {
			advance(); // consume '['
			
			// Parse the array size expression
			ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}
			
			// Try to evaluate the array size using constexpr evaluator
			size_t array_size = 0;
			if (size_result.node().has_value()) {
				ConstExpr::EvaluationContext ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), ctx);
				if (eval_result.success() && eval_result.as_int() > 0) {
					array_size = static_cast<size_t>(eval_result.as_int());
				}
			}
			
			// Add array dimension to the type specifier
			type_spec.add_array_dimension(array_size);
			
			// Expect closing ']'
			if (!consume("]"_tok)) {
				return ParseResult::error("Expected ']' after array size in typedef", current_token_);
			}
		}
		
		// Update type_node with the array type
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	}

	// Skip any GCC attributes that might appear before the semicolon
	// e.g., typedef _Complex float __cfloat128 __attribute__ ((__mode__ (__TC__)));
	skip_cpp_attributes();

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after typedef declaration", current_token_);
	}

	// Build the qualified name for the typedef if we're in a namespace
	std::string_view qualified_alias_name;
	NamespaceHandle namespace_handle = gSymbolTable.get_current_namespace_handle();
	if (!namespace_handle.isGlobal()) {
		StringHandle alias_handle = StringTable::getOrInternStringHandle(alias_name);
		StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, alias_handle);
		qualified_alias_name = StringTable::getStringView(qualified_handle);
	} else {
		qualified_alias_name = alias_name;
	}

	// Register the typedef alias in the type system
	// The typedef should resolve to the underlying type, not be a new UserDefined type
	// We create a TypeInfo entry that mirrors the underlying type
	auto& alias_type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(qualified_alias_name), type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
	alias_type_info.pointer_depth_ = type_spec.pointer_depth();
	alias_type_info.is_reference_ = type_spec.is_reference();
	alias_type_info.is_rvalue_reference_ = type_spec.is_rvalue_reference();
	gTypesByName.emplace(alias_type_info.name(), &alias_type_info);

	// Update the type_node with the modified type_spec (with pointers)
	type_node = emplace_node<TypeSpecifierNode>(type_spec);

	// Create and return typedef declaration node
	auto typedef_node = emplace_node<TypedefDeclarationNode>(type_node, *alias_token);
	return saved_position.success(typedef_node);
}

ParseResult Parser::parse_friend_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'friend' keyword
	auto friend_keyword = advance();
	if (friend_keyword.kind() != "friend"_tok) {
		return ParseResult::error("Expected 'friend' keyword", friend_keyword);
	}

	// Check for 'class' keyword (friend class declaration)
	if (peek() == "class"_tok || peek() == "struct"_tok) {
		advance();  // consume 'class'/'struct'

		// Parse class name (may be qualified: Outer::Inner)
		auto class_name_token = advance();
		if (!class_name_token.kind().is_identifier()) {
			return ParseResult::error("Expected class name after 'friend class'", current_token_);
		}

		// Handle qualified names: friend class locale::_Impl;
		// Build full qualified name for proper friend resolution
		std::string qualified_friend_name(class_name_token.value());
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			if (peek().is_identifier()) {
				qualified_friend_name += "::";
				class_name_token = advance();
				qualified_friend_name += class_name_token.value();
			} else {
				break;
			}
		}

		// Skip template arguments if present: friend class SomeTemplate<T>;
		if (peek() == "<"_tok) {
			skip_template_arguments();
		}

		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after friend class declaration", current_token_);
		}

		auto friend_name_handle = StringTable::getOrInternStringHandle(qualified_friend_name);
		auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Class, friend_name_handle);
		return saved_position.success(friend_node);
	}

	// Otherwise, parse as friend function or friend member function
	// For now, we'll parse a simplified version that just captures the name
	// Full function signature parsing would require more complex logic

	// Parse return type (simplified - just consume tokens until we find identifier or ::)
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}

	// Skip pointer/reference qualifiers that may appear after the base type
	// Patterns like: friend int* func(); or friend int& func(); or friend int const* func();
	while (!peek().is_eof()) {
		auto k = peek();
		if (k == "*"_tok || k == "&"_tok || k == "&&"_tok ||
		    k == "const"_tok || k == "volatile"_tok) {
			advance();
		} else {
			break;
		}
	}

	// Check if this is a friend class/struct declaration without 'class' keyword
	// Pattern: friend std::numeric_limits<__max_size_type>;
	// After parsing the type specifier (which includes template args), if ';' follows, it's a friend class
	if (peek() == ";"_tok) {
		advance(); // consume ';'
		const auto& type_spec = type_result.node()->as<TypeSpecifierNode>();
		// Use the type_index to look up the full qualified name from gTypeInfo,
		// since token() only holds a single identifier segment (e.g., 'std' not 'std::numeric_limits')
		StringHandle friend_name = (type_spec.type_index() < gTypeInfo.size())
			? gTypeInfo[type_spec.type_index()].name()
			: type_spec.token().handle();
		auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Class, friend_name);
		return saved_position.success(friend_node);
	}

	// Parse function name (may be qualified: ClassName::functionName, or an operator)
	// We only need to track the last qualifier (the class name) for friend member functions
	std::string_view last_qualifier;
	std::string_view function_name;

	// Check for operator keyword (friend operator function)
	if (peek() == "operator"_tok) {
		advance();  // consume 'operator'
		// The operator can be followed by various things: ==, !=, (), [], etc.
		// Just skip tokens until we find '('
		while (!peek().is_eof() && peek() != "("_tok) {
			advance();
		}
		function_name = "operator";
	} else {
		while (!peek().is_eof()) {
			auto name_token = advance();
			if (!name_token.kind().is_identifier()) {
				return ParseResult::error("Expected function name in friend declaration", current_token_);
			}

			// Skip template arguments on qualified name components (e.g., Class<int>::func)
			if (peek() == "<"_tok) {
				skip_template_arguments();
			}

			// Check for :: (qualified name)
			if (peek() == "::"_tok) {
				advance();  // consume '::'
				last_qualifier = name_token.value();
				// After ::, check for operator keyword (like std::operator==)
				if (peek() == "operator"_tok) {
					advance();  // consume 'operator'
					// Skip tokens until we find '('
					while (!peek().is_eof() && peek() != "("_tok) {
						advance();
					}
					function_name = "operator";
					break;
				}
			} else {
				function_name = name_token.value();
				break;
			}
		}
	}

	// Skip template arguments for explicit specialization friends (e.g., friend func<>(args...))
	if (peek() == "<"_tok) {
		skip_template_arguments();
	}

	// Parse function parameters
	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' after friend function name", current_token_);
	}

	// Parse parameter list (simplified - just skip to closing paren)
	int paren_depth = 1;
	while (paren_depth > 0 && !peek().is_eof()) {
		auto token = advance();
		if (token.value() == "(") {
			paren_depth++;
		} else if (token.value() == ")") {
			paren_depth--;
		}
	}

	// Skip optional qualifiers after parameter list using existing helper
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);

	// Skip trailing requires clause on friend functions
	skip_trailing_requires_clause();

	// Handle friend function body (inline definition), = default, = delete, or semicolon (declaration only)
	if (peek() == "{"_tok) {
		// Friend function with inline body - skip the body using existing helper
		skip_balanced_braces();
	} else if (peek() == "="_tok) {
		// Handle = default or = delete
		advance(); // consume '='
		if (!peek().is_eof() && (peek() == "default"_tok || peek() == "delete"_tok)) {
			advance(); // consume 'default' or 'delete'
		}
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after friend function declaration", current_token_);
		}
	} else if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after friend function declaration", current_token_);
	}

	// Create friend declaration node
	ASTNode friend_node;
	if (last_qualifier.empty()) {
		// Friend function
		friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Function, StringTable::getOrInternStringHandle(function_name));
	} else {
		// Friend member function
		friend_node = emplace_node<FriendDeclarationNode>(FriendKind::MemberFunction, StringTable::getOrInternStringHandle(function_name), StringTable::getOrInternStringHandle(std::string(last_qualifier)));
	}

	return saved_position.success(friend_node);
}

// Parse template friend declarations
// Pattern: template<typename T1, typename T2> friend struct pair;
ParseResult Parser::parse_template_friend_declaration(StructDeclarationNode& struct_node) {
	ScopedTokenPosition saved_position(*this);

	// Consume 'template' keyword
	if (!consume("template"_tok)) {
		return ParseResult::error("Expected 'template' keyword", peek_info());
	}

	// Consume '<' 
	// Note: '<' is tokenized as Token::Type::Operator by the lexer, so we check
	// the value only (matching how other template parsing code handles '<')
	if (peek() != "<"_tok) {
		return ParseResult::error("Expected '<' after 'template'", peek_info());
	}
	advance(); // consume '<'

	// Skip template parameters - we don't need to parse them in detail for friend declarations
	// Just consume everything until we find the matching '>'
	int angle_bracket_depth = 1;
	while (angle_bracket_depth > 0 && !peek().is_eof()) {
		if (peek() == "<"_tok) {
			angle_bracket_depth++;
		} else if (peek() == ">"_tok) {
			angle_bracket_depth--;
		}
		advance();
	}

	// Parse optional requires clause between template parameters and 'friend'
	// e.g., template<typename _It2, sentinel_for<_It> _Sent2>
	//         requires sentinel_for<_Sent, _It2>
	//         friend constexpr bool operator==(...) { ... }
	if (peek() == "requires"_tok) {
		advance(); // consume 'requires'
		// Parse the constraint expression properly for compile-time evaluation
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			FLASH_LOG(Parser, Warning, "Failed to parse requires clause in friend template: ", constraint_result.error_message());
		} else {
			FLASH_LOG(Parser, Debug, "Parsed requires clause in friend template for compile-time evaluation");
		}
	}

	// Now we should see 'friend'
	if (!consume("friend"_tok)) {
		return ParseResult::error("Expected 'friend' keyword after template parameters", peek_info());
	}

	// Check for 'struct' or 'class' keyword
	[[maybe_unused]] bool is_struct = false;
	if (peek() == "struct"_tok) {
		is_struct = true;
		advance(); // consume 'struct'
	} else if (peek() == "class"_tok) {
		advance(); // consume 'class'
	} else {
		// Not a template friend class/struct declaration - might be a friend function template
		// We skip the declaration since friend function templates don't affect accessibility
		// and are primarily for ADL (Argument-Dependent Lookup) purposes.
		// The empty name is acceptable because we only need to record that a friend 
		// declaration exists; the actual function resolution happens at call sites.
		
		// Skip until ';' or '{' (for friend function templates with inline definitions)
		while (!peek().is_eof() && peek() != ";"_tok && peek() != "{"_tok) {
			advance();
		}
		
		// Handle inline friend function template body: { ... }
		if (peek() == "{"_tok) {
			skip_balanced_braces();
		}
		
		// Skip trailing semicolon if present (for declarations without body)
		if (peek() == ";"_tok) {
			advance();
		}
		
		// Create a minimal friend declaration node - name is empty since we skipped parsing
		auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Function, StringHandle{});
		struct_node.add_friend(friend_node);
		return saved_position.success(friend_node);
	}

	// Parse the class/struct name (may be namespace-qualified: std::ClassName)
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected class/struct name after 'friend struct/class'", peek_info());
	}

	// Build the full qualified name: ns1::ns2::ClassName
	StringBuilder qualified_name_builder;
	qualified_name_builder.append(advance().value());

	// Handle namespace-qualified names: std::_Rb_tree_merge_helper
	while (peek() == "::"_tok) {
		advance(); // consume '::'
		if (peek().is_identifier()) {
			qualified_name_builder.append("::");
			qualified_name_builder.append(advance().value());
		} else {
			break;
		}
	}
	std::string_view qualified_name = qualified_name_builder.commit();

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after template friend class declaration", peek_info());
	}

	// Create friend declaration node with TemplateClass kind, storing the full qualified name
	auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::TemplateClass, StringTable::getOrInternStringHandle(qualified_name));
	struct_node.add_friend(friend_node);

	return saved_position.success(friend_node);
}

ParseResult Parser::parse_namespace() {
	ScopedTokenPosition saved_position(*this);

	// Detect if this namespace was prefixed with 'inline'
	bool is_inline_namespace = pending_inline_namespace_;
	pending_inline_namespace_ = false;

	// Consume 'namespace' keyword
	if (!consume("namespace"_tok)) {
		return ParseResult::error("Expected 'namespace' keyword", peek_info());
	}

	// Check if this is an anonymous namespace (namespace { ... })
	std::string_view namespace_name = "";
	bool is_anonymous = false;
	
	// C++17 nested namespace declarations: namespace A::B::C { }
	// This vector holds all namespace names for nested declarations
	std::vector<std::string_view> nested_names;
	// Track which nested namespaces are inline (parallel to nested_names)
	std::vector<bool> nested_inline_flags;

	if (peek() == "{"_tok) {
		// Anonymous namespace
		is_anonymous = true;
		// For anonymous namespaces, we'll use an empty name
		// The symbol table will handle them specially
		namespace_name = "";
	} else {
		// Named namespace - parse namespace name
		auto name_token = advance();
		if (!name_token.kind().is_identifier()) {
			return ParseResult::error("Expected namespace name or '{'", name_token);
		}
		namespace_name = name_token.value();
		
		// Collect all namespace names (including the first one for nested namespaces)
		// The first namespace gets the is_inline_namespace flag from 'inline namespace' prefix
		nested_names.push_back(namespace_name);
		nested_inline_flags.push_back(is_inline_namespace);
		
		// C++17 nested namespace declarations: namespace A::B::C { }
		// Also supports C++20: namespace A::inline B::C { }
		// Continue collecting nested namespace names if present
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			
			// Check for inline keyword in nested namespace: namespace A::inline B { }
			bool nested_is_inline = false;
			if (peek() == "inline"_tok) {
				advance(); // consume 'inline'
				nested_is_inline = true;
			}
			
			auto nested_name_token = advance();
			if (!nested_name_token.kind().is_identifier()) {
				return ParseResult::error("Expected namespace name after '::'", nested_name_token);
			}
			nested_names.push_back(nested_name_token.value());
			nested_inline_flags.push_back(nested_is_inline);
		}

		// Skip any attributes after the namespace name (e.g., __attribute__((__abi_tag__ ("cxx11"))))
		skip_gcc_attributes();

		// Check if this is a namespace alias: namespace alias = target;
		if (peek() == "="_tok) {
			// This is a namespace alias, not a namespace declaration
			// Restore position and parse as namespace alias
			Token alias_token = name_token;
			advance(); // consume '='

			// Parse target namespace path
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = advance();
				if (!ns_token.kind().is_identifier()) {
					return ParseResult::error("Expected namespace name", ns_token);
				}
				target_namespace.emplace_back(StringType<>(ns_token.value()));

				// Check for ::
				if (peek() == "::"_tok) {
					advance(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after namespace alias", current_token_);
			}

			// Convert namespace path to handle and add the alias to the symbol table
			NamespaceHandle target_handle = gSymbolTable.resolve_namespace_handle(target_namespace);
			gSymbolTable.add_namespace_alias(alias_token.value(), target_handle);

			auto alias_node = emplace_node<NamespaceAliasNode>(alias_token, target_handle);
			return saved_position.success(alias_node);
		}
	}

	// Inline namespaces inject their members into the enclosing namespace scope
	// For nested declarations like namespace A::inline B, B is inline within A
	// We now handle this per-namespace in the enter loop below, not just for the first namespace

	// Expect opening brace
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' after namespace name", peek_info());
	}

	// Create namespace declaration node - string_view points directly into source text
	// For anonymous namespaces, use empty string_view
	// For nested namespaces (A::B::C), we use the innermost name for the AST node
	// but enter all scopes in the symbol table
	std::string_view innermost_name = nested_names.empty() ? namespace_name : nested_names.back();
	auto [namespace_node, namespace_ref] = emplace_node_ref<NamespaceDeclarationNode>(is_anonymous ? "" : innermost_name);

	// Enter namespace scope(s) and handle inline namespaces
	// For anonymous namespaces, we DON'T enter a new scope in the symbol table
	// Instead, symbols are added to the current scope but tracked separately for mangling
	// This allows them to be accessed without qualification (per C++ standard)
	// while still getting unique linkage names
	// For nested namespaces (A::B::C), enter each scope in order
	if (!is_anonymous) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		
		for (size_t i = 0; i < nested_names.size(); ++i) {
			const auto& ns_name = nested_names[i];
			bool this_ns_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
			StringHandle name_handle = StringTable::getOrInternStringHandle(ns_name);
			NamespaceHandle next_handle = gNamespaceRegistry.getOrCreateNamespace(current_handle, name_handle);
			
			// If this namespace is inline, add a using directive BEFORE entering
			// This makes members visible in the current (parent) scope
			if (this_ns_is_inline && next_handle.isValid()) {
				gSymbolTable.add_using_directive(next_handle);
			}
			
			if (next_handle.isValid()) {
				gSymbolTable.enter_namespace(next_handle);
				current_handle = next_handle;
			} else {
				gSymbolTable.enter_namespace(ns_name);
				current_handle = gSymbolTable.get_current_namespace_handle();
			}
		}
	}

	// Track inline namespace nesting (one entry per nested level for proper cleanup)
	for (size_t i = 0; i < (nested_names.empty() ? 1 : nested_names.size()); ++i) {
		bool this_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
		inline_namespace_stack_.push_back(this_is_inline);
	}
	// For anonymous namespaces, track the namespace in the AST but not in symbol lookup
	// Symbols will be added to current scope during declaration parsing

	// Parse declarations within the namespace
	while (!peek().is_eof() && peek() != "}"_tok) {
		ParseResult decl_result;

		// Skip empty declarations (lone semicolons) - valid in C++ (e.g., namespace foo { }; )
		if (peek() == ";"_tok) {
			advance();
			continue;
		}

		// Check if it's a using directive, using declaration, or namespace alias
		if (peek() == "using"_tok) {
			decl_result = parse_using_directive_or_declaration();
		}
		// Check if it's a nested namespace (or inline namespace)
		else if (peek() == "namespace"_tok) {
			decl_result = parse_namespace();
		}
		// Check if it's an inline namespace (inline namespace __cxx11 { ... })
		else if (peek() == "inline"_tok) {
			auto next = peek_info(1);
			if (next.kind() == "namespace"_tok) {
				advance(); // consume 'inline'
				pending_inline_namespace_ = true;
				decl_result = parse_namespace(); // parse_namespace handles the rest
			} else {
				// Just a regular inline declaration
				decl_result = parse_declaration_or_function_definition();
			}
		}
		// Check if it's a struct/class/union declaration
		else if ((peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok)) {
			decl_result = parse_struct_declaration();
		}
		// Check if it's an enum declaration
		else if (peek() == "enum"_tok) {
			decl_result = parse_enum_declaration();
		}
		// Check if it's a typedef declaration
		else if (peek() == "typedef"_tok) {
			decl_result = parse_typedef_declaration();
		}
		// Check if it's a template declaration
		else if (peek() == "template"_tok) {
			decl_result = parse_template_declaration();
		}
		// Check if it's an extern declaration (extern "C" or extern "C++")
		else if (peek() == "extern"_tok) {
			// Save position in case this is just a regular extern declaration
			SaveHandle extern_saved_pos = save_token_position();
			advance(); // consume 'extern'

			// Check if this is extern "C" or extern "C++"
			if (peek().is_string_literal()) {
				std::string_view linkage_str = peek_info().value();

				// Remove quotes from string literal
				if (linkage_str.size() >= 2 && linkage_str.front() == '"' && linkage_str.back() == '"') {
					linkage_str = linkage_str.substr(1, linkage_str.size() - 2);
				}

				Linkage linkage = Linkage::None;
				if (linkage_str == "C") {
					linkage = Linkage::C;
				} else if (linkage_str == "C++") {
					linkage = Linkage::CPlusPlus;
				} else {
					if (!is_anonymous) {
						gSymbolTable.exit_scope();
					}
					return ParseResult::error("Unknown linkage specification: " + std::string(linkage_str), current_token_);
				}

				advance(); // consume linkage string
				discard_saved_token(extern_saved_pos);

				// Check for block form: extern "C" { ... }
				if (peek() == "{"_tok) {
					decl_result = parse_extern_block(linkage);
				} else {
					// Single declaration form: extern "C++" int func();
					Linkage saved_linkage = current_linkage_;
					current_linkage_ = linkage;
					decl_result = parse_declaration_or_function_definition();
					current_linkage_ = saved_linkage;
				}
			} else if (peek() == "template"_tok) {
				// extern template class allocator<char>; — explicit instantiation declaration
				discard_saved_token(extern_saved_pos);
				decl_result = parse_template_declaration();
			} else {
				// Regular extern declaration (not extern "C")
				restore_token_position(extern_saved_pos);
				decl_result = parse_declaration_or_function_definition();
			}
		}
		// Otherwise, parse as function or variable declaration
		else {
			decl_result = parse_declaration_or_function_definition();
		}

		if (decl_result.is_error()) {
			// Exit all nested namespace scopes on error
			if (!is_anonymous) {
				size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
				for (size_t i = 0; i < nesting_depth; ++i) {
					gSymbolTable.exit_scope();
				}
			}
			return decl_result;
		}

		if (auto node = decl_result.node()) {
			namespace_ref.add_declaration(*node);
		}
	}

	// Expect closing brace
	if (!consume("}"_tok)) {
		// Exit all nested namespace scopes on error
		if (!is_anonymous) {
			size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
			for (size_t i = 0; i < nesting_depth; ++i) {
				gSymbolTable.exit_scope();
				inline_namespace_stack_.pop_back();
			}
		} else {
			inline_namespace_stack_.pop_back();
		}
		return ParseResult::error("Expected '}' after namespace body", peek_info());
	}

	// Exit namespace scope(s) (only for named namespaces, not anonymous)
	// For nested namespaces (A::B::C), exit each scope in reverse order
	if (!is_anonymous) {
		size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
		for (size_t i = 0; i < nesting_depth; ++i) {
			gSymbolTable.exit_scope();
			inline_namespace_stack_.pop_back();
		}
	} else {
		inline_namespace_stack_.pop_back();
	}

	// Merge inline namespace symbols into parent namespace for qualified lookup
	// We need to do this for each inline namespace in the chain
	// Capture the path AFTER exiting scopes (we're back to original scope)
	if (!is_anonymous && !nested_inline_flags.empty()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		for (size_t i = 0; i < nested_names.size(); ++i) {
			bool this_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
			StringHandle name_handle = StringTable::getOrInternStringHandle(nested_names[i]);
			NamespaceHandle inline_handle = gNamespaceRegistry.getOrCreateNamespace(current_handle, name_handle);
			if (this_is_inline) {
				gSymbolTable.merge_inline_namespace(inline_handle, current_handle);
			}
			current_handle = inline_handle;
		}
	}

	return saved_position.success(namespace_node);
}

ParseResult Parser::parse_using_directive_or_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'using' keyword
	auto using_token_opt = peek_info();
	if (using_token_opt.kind() != "using"_tok) {
		return ParseResult::error("Expected 'using' keyword", using_token_opt);
	}
	Token using_token = using_token_opt;
	advance(); // consume 'using'

	// Check if this is a type alias or namespace alias: using identifier = ...;
	// We need to look ahead to see if there's an '=' after the first identifier
	// (possibly with [[attributes]] in between)
	SaveHandle lookahead_pos = save_token_position();
	auto first_token = peek_info();
	if (first_token.kind().is_identifier()) {
		advance(); // consume identifier
		// Skip attributes in lookahead: using name [[deprecated]] = type;
		skip_cpp_attributes();
		auto next_token = peek_info();
		if (next_token.kind() == "="_tok) {
			// This is either a type alias or namespace alias: using alias = type/namespace;
			restore_token_position(lookahead_pos);

			// Parse alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after 'using'", current_token_);
			}

			// Skip C++ attributes like [[__deprecated__]] between name and '='
			skip_cpp_attributes();

			// Consume '='
			if (peek().is_eof() || peek_info().type() != Token::Type::Operator || peek() != "="_tok) {
				return ParseResult::error("Expected '=' after alias name", current_token_);
			}
			advance(); // consume '='

			// Try to parse as a type specifier (for type aliases like: using value_type = T;)
			ParseResult type_result = parse_type_specifier();
			if (!type_result.is_error()) {
				// Parse any pointer/reference modifiers after the type
				// For example: using RvalueRef = typename T::type&&;
				if (type_result.node().has_value()) {
					TypeSpecifierNode type_spec = type_result.node()->as<TypeSpecifierNode>();
					
					// Check for pointer-to-member type syntax: Type Class::*
					// This is used in <type_traits> for result_of patterns
					// Pattern: using _MemPtr = _Res _Class::*;
					if (peek().is_identifier()) {
						// Look ahead to see if this is Class::* pattern
						auto saved_pos = save_token_position();
						Token class_token = peek_info();
						advance(); // consume potential class name
						
						if (peek() == "::"_tok) {
							advance(); // consume '::'
							if (peek() == "*"_tok) {
								advance(); // consume '*'
								// This is a pointer-to-member type: Type Class::*
								// Mark the type as a pointer-to-member
								type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
								type_spec.set_member_class_name(class_token.handle());
								FLASH_LOG(Parser, Debug, "Parsed pointer-to-member type: ", type_spec.token().value(), " ", class_token.value(), "::*");
								discard_saved_token(saved_pos);
							} else {
								// Not a pointer-to-member, restore position
								restore_token_position(saved_pos);
							}
						} else {
							// Not a pointer-to-member, restore position
							restore_token_position(saved_pos);
						}
					}
					
					// Parse pointer declarators: * [const] [volatile] *...
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						
						// Check for CV-qualifiers after the *
						CVQualifier ptr_cv = parse_cv_qualifiers();
						
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Check for function pointer/reference type syntax: ReturnType (&)(...) or ReturnType (*)(...) 
					// Pattern: Type (&)() = lvalue reference to function returning Type
					// Pattern: Type (&&)() = rvalue reference to function returning Type
					// Pattern: Type (*)() = pointer to function returning Type
					// This handles types like: int (&)(), _Xp (&)(), etc.
					if (peek() == "("_tok) {
						auto func_type_saved_pos = save_token_position();
						advance(); // consume '('
						
						// Check what's inside the parentheses: &, &&, or *
						bool is_function_ref = false;
						bool is_rvalue_function_ref = false;
						bool is_function_ptr = false;
						
						if (!peek().is_eof()) {
							if (peek() == "&&"_tok) {
								is_rvalue_function_ref = true;
								advance(); // consume '&&'
							} else if (peek() == "&"_tok) {
								is_function_ref = true;
								advance(); // consume '&'
							} else if (peek() == "*"_tok) {
								is_function_ptr = true;
								advance(); // consume '*'
							}
						}
						
						// After &, &&, or *, expect ')'
						if ((is_function_ref || is_rvalue_function_ref || is_function_ptr) &&
						    peek() == ")"_tok) {
							advance(); // consume ')'
							
							// Now expect '(' for the parameter list
							if (peek() == "("_tok) {
								advance(); // consume '('
								
								// Parse parameter list (can be empty or have parameters)
								std::vector<Type> param_types;
								while (!peek().is_eof() && peek() != ")"_tok) {
									// Skip parameter - can be complex types
									auto param_type_result = parse_type_specifier();
									if (!param_type_result.is_error() && param_type_result.node().has_value()) {
										const TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
										param_types.push_back(param_type.type());
									}
									
									// Check for comma
									if (peek() == ","_tok) {
										advance(); // consume ','
									} else {
										break;
									}
								}
								
								if (peek() == ")"_tok) {
									advance(); // consume ')'
									
									// Successfully parsed function reference/pointer type!
									FunctionSignature func_sig;
									func_sig.return_type = type_spec.type();
									func_sig.parameter_types = std::move(param_types);
									
									if (is_function_ptr) {
										type_spec.add_pointer_level(CVQualifier::None);
									}
									type_spec.set_function_signature(func_sig);
									
									if (is_function_ref) {
										type_spec.set_reference(false);  // lvalue reference
									} else if (is_rvalue_function_ref) {
										type_spec.set_reference(true);   // rvalue reference
									}
									
									FLASH_LOG(Parser, Debug, "Parsed function reference/pointer type in global alias: ", 
									          is_function_ptr ? "pointer" : (is_rvalue_function_ref ? "rvalue ref" : "lvalue ref"),
									          " to function");
									
									// Discard saved position - we successfully parsed
									discard_saved_token(func_type_saved_pos);
								} else {
									// Parsing failed - restore position
									restore_token_position(func_type_saved_pos);
								}
							} else {
								// No parameter list follows - restore position
								restore_token_position(func_type_saved_pos);
							}
						} else {
							// Not a function type syntax - restore position
							restore_token_position(func_type_saved_pos);
						}
					}
					
					// Parse reference declarators: & or &&
					ReferenceQualifier ref_qual = parse_reference_qualifier();
					if (ref_qual == ReferenceQualifier::RValueReference) {
						type_spec.set_reference(true);  // true = rvalue reference
					} else if (ref_qual == ReferenceQualifier::LValueReference) {
						type_spec.set_reference(false);  // false = lvalue reference
					}
					
					// Parse array dimensions: using _Type = _Tp[_Nm];
					while (peek() == "["_tok) {
						advance(); // consume '['
						if (peek() == "]"_tok) {
							type_spec.set_array(true);
							advance(); // consume ']'
						} else {
							auto dim_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (dim_result.is_error()) {
								return dim_result;
							}
							auto dim_val = try_evaluate_constant_expression(*dim_result.node());
							size_t dim_size = dim_val.has_value() ? static_cast<size_t>(dim_val->value) : 0;
							type_spec.add_array_dimension(dim_size);
							if (!consume("]"_tok)) {
								return ParseResult::error("Expected ']' after array dimension in type alias", current_token_);
							}
						}
					}
					
					// This is a type alias
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after type alias", current_token_);
					}

					// Register the type alias in gTypesByName
					// Create a TypeInfo for the alias that points to the underlying type
					auto& alias_type_info = gTypeInfo.emplace_back(alias_token.handle(), type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
					alias_type_info.pointer_depth_ = type_spec.pointer_depth();
					alias_type_info.is_reference_ = type_spec.is_reference();
					alias_type_info.is_rvalue_reference_ = type_spec.is_rvalue_reference();
					// Copy function signature for function pointer/reference type aliases
					if (type_spec.has_function_signature()) {
						alias_type_info.function_signature_ = type_spec.function_signature();
					}
					
					gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
					
					// Also register with namespace-qualified name for type aliases defined in namespaces
					NamespaceHandle namespace_handle = gSymbolTable.get_current_namespace_handle();
					if (!namespace_handle.isGlobal()) {
						StringHandle alias_handle = alias_token.handle();
						auto full_qualified_name = gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, alias_handle);
						if (gTypesByName.find(full_qualified_name) == gTypesByName.end()) {
							gTypesByName.emplace(full_qualified_name, &alias_type_info);
							FLASH_LOG_FORMAT(Parser, Debug, "Registered type alias '{}' with namespace-qualified name '{}'",
							                 alias_token.value(), StringTable::getStringView(full_qualified_name));
						}
					}

					// Return success (no AST node needed for type aliases)
					return saved_position.success();
				}
				
				// If we didn't get a node from parse_type_specifier, just check for semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after type alias", current_token_);
				}
				return saved_position.success();
			} else if (parsing_template_body_ || gSymbolTable.get_current_scope_type() == ScopeType::Function) {
				// If we're in a template body OR function body and type parsing failed, it's likely a template-dependent type
				// or a complex type expression during template instantiation.
				// Skip to semicolon and continue (template aliases with dependent types can't be fully resolved now).
				// For function-local type aliases (like in template instantiations), they're often not needed
				// for code generation as the actual type is already known from the return type.
				FLASH_LOG(Parser, Debug, "Skipping unparseable using declaration in ", 
				          parsing_template_body_ ? "template body" : "function body");
				while (!peek().is_eof() && peek() != ";"_tok) {
					advance();
				}
				if (consume(";"_tok)) {
					// Successfully skipped the template-dependent using declaration
					return saved_position.success();
				}
				return ParseResult::error("Expected ';' after using declaration", current_token_);
			}

			// Not a type alias, try parsing as namespace path for namespace alias
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = advance();
				if (!ns_token.kind().is_identifier()) {
					return ParseResult::error("Expected type or namespace name", ns_token);
				}
				target_namespace.emplace_back(StringType<>(ns_token.value()));

				// Check for ::
				if (peek() == "::"_tok) {
					advance(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after namespace alias", current_token_);
			}

			// Convert namespace path to handle and add the alias to the symbol table
			NamespaceHandle target_handle = gSymbolTable.resolve_namespace_handle(target_namespace);
			gSymbolTable.add_namespace_alias(alias_token.value(), target_handle);

			auto alias_node = emplace_node<NamespaceAliasNode>(alias_token, target_handle);
			return saved_position.success(alias_node);
		}
	}
	// Not a namespace alias, restore position
	restore_token_position(lookahead_pos);

	// Check if this is "using namespace" directive
	if (peek() == "namespace"_tok) {
		advance(); // consume 'namespace'

		// Parse namespace path (e.g., std::filesystem)
		std::vector<StringType<>> namespace_path;
		while (true) {
			auto ns_token = advance();
			if (!ns_token.kind().is_identifier()) {
				return ParseResult::error("Expected namespace name", ns_token);
			}
			namespace_path.emplace_back(StringType<>(ns_token.value()));

			// Check for ::
			if (peek() == "::"_tok) {
				advance(); // consume ::
			} else {
				break;
			}
		}

		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after using directive", current_token_);
		}

		// Convert namespace path to handle and add the using directive to the symbol table
		NamespaceHandle namespace_handle = gSymbolTable.resolve_namespace_handle(namespace_path);
		gSymbolTable.add_using_directive(namespace_handle);

		auto directive_node = emplace_node<UsingDirectiveNode>(namespace_handle, using_token);
		return saved_position.success(directive_node);
	}

	// Check if this is C++20 "using enum" declaration
	if (peek() == "enum"_tok) {
		advance(); // consume 'enum'

		// Parse enum type name (can be qualified: namespace::EnumType or just EnumType)
		std::vector<StringType<>> namespace_path;
		Token enum_type_token;

		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected enum type name after 'using enum'", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final enum type name
				enum_type_token = token;
				break;
			}
		}

		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after 'using enum' declaration", current_token_);
		}

		// Create the using enum node - CodeGen will also handle this for its local scope
		StringHandle enum_name_handle = enum_type_token.handle();
		auto using_enum_node = emplace_node<UsingEnumNode>(enum_name_handle, using_token);
		
		// Add enumerators to gSymbolTable NOW so they're available during parsing
		// This is needed because the parser needs to resolve identifiers like 'Red' when
		// parsing subsequent expressions (e.g., static_cast<int>(Red))
		auto type_it = gTypesByName.find(enum_name_handle);
		if (type_it != gTypesByName.end() && type_it->second->getEnumInfo()) {
			const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
			
			for (const auto& enumerator : enum_info->enumerators) {
				// Create a type node for the enum type
				auto enum_type_node = emplace_node<TypeSpecifierNode>(
					Type::Enum, type_it->second->type_index_, enum_info->underlying_size, enum_type_token);
				
				// Create a declaration node for the enumerator
				Token enumerator_token(Token::Type::Identifier, 
					StringTable::getStringView(enumerator.getName()), 0, 0, 0);
				auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_token);
				
				// Insert into gSymbolTable so it's available during parsing
				gSymbolTable.insert(StringTable::getStringView(enumerator.getName()), enumerator_decl);
			}
			
			FLASH_LOG(Parser, Debug, "Using enum '", enum_type_token.value(), 
				"' - added ", enum_info->enumerators.size(), " enumerators to parser scope");
		} else {
			FLASH_LOG(General, Error, "Enum type '", enum_type_token.value(), 
				"' not found for 'using enum' declaration");
		}
		
		return saved_position.success(using_enum_node);
	}

	// Otherwise, this is a using declaration: using std::vector; or using ::name;
	std::vector<StringType<>> namespace_path;
	Token identifier_token;

	// Check if this starts with :: (global namespace scope)
	if (peek() == "::"_tok) {
		advance(); // consume leading ::
		// After the leading ::, we need to parse the qualified name
		// This could be ::name or ::namespace::name
		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected identifier after :: in using declaration", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final identifier
				identifier_token = token;
				break;
			}
		}
	} else {
		// Parse qualified name (namespace::...::identifier)
		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected identifier in using declaration", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final identifier
				identifier_token = token;
				break;
			}
		}
	}

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after using declaration", current_token_);
	}

	// Convert namespace path to handle and add the using declaration to the symbol table
	NamespaceHandle namespace_handle = gSymbolTable.resolve_namespace_handle(namespace_path);
	gSymbolTable.add_using_declaration(
		std::string_view(identifier_token.value()),
		namespace_handle,
		std::string_view(identifier_token.value())
	);

	// Check if the identifier refers to an existing type in the source namespace
	// Build the source type name (either global or qualified with namespace_path)
	StringHandle source_type_name;
	if (namespace_path.empty()) {
		// using ::identifier; - refers to global namespace
		source_type_name = identifier_token.handle();
	} else {
		// using ns1::ns2::identifier; - build qualified name
		StringHandle identifier_handle = identifier_token.handle();
		source_type_name = namespace_handle.isValid()
			? gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, identifier_handle)
			: identifier_handle;
	}
	
	// Look up the type in gTypesByName
	auto existing_type_it = gTypesByName.find(source_type_name);
	
	// If not found with qualified name, try the unqualified name
	// This handles cases like: using ::__gnu_cxx::lldiv_t; where __gnu_cxx::lldiv_t
	// might itself be an alias to ::lldiv_t
	if (existing_type_it == gTypesByName.end() && !namespace_path.empty()) {
		StringHandle qualified_source = source_type_name;  // Save the qualified name for logging
		StringHandle unqualified_source = identifier_token.handle();
		auto unqualified_it = gTypesByName.find(unqualified_source);
		if (unqualified_it != gTypesByName.end()) {
			existing_type_it = unqualified_it;
			source_type_name = unqualified_source;  // Update to use the unqualified name that was found
			FLASH_LOG_FORMAT(Parser, Debug, "Using declaration: qualified name {} not found, using unqualified name {}", 
			                 StringTable::getStringView(qualified_source), StringTable::getStringView(unqualified_source));
		}
	}
	
	// If we're inside a namespace, we need to register the type with a qualified name
	// so that "std::lldiv_t" can be recognized as a type
	NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
	if (!current_namespace_handle.isGlobal()) {
		// Build qualified name for the target: std::identifier
		StringHandle identifier_handle = identifier_token.handle();
		StringHandle target_type_name = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, identifier_handle);
		
		// Check if target name is already registered (avoid duplicates)
		if (gTypesByName.find(target_type_name) == gTypesByName.end()) {
			if (existing_type_it != gTypesByName.end()) {
				// Found existing type - create alias pointing to it
				const TypeInfo* source_type = existing_type_it->second;
				auto& alias_type_info = gTypeInfo.emplace_back(target_type_name, source_type->type_, source_type->type_index_, source_type->type_size_);
				alias_type_info.pointer_depth_ = source_type->pointer_depth_;
				
				// If the source type has StructInfo, we don't copy it - we rely on type_index_ to point to it
				// This is the same pattern used for typedef resolution
				
				gTypesByName.emplace(target_type_name, &alias_type_info);
				FLASH_LOG_FORMAT(Parser, Debug, "Registered type alias from using declaration: {} -> {}", 
				                 StringTable::getStringView(target_type_name), StringTable::getStringView(source_type_name));
				
				// Also register with the unqualified name within the current namespace scope
				// This allows code inside the namespace to use the type without qualification
				// e.g., inside namespace std, both "std::lldiv_t" and "lldiv_t" should work
				StringHandle unqualified_name = identifier_token.handle();
				if (gTypesByName.find(unqualified_name) == gTypesByName.end()) {
					gTypesByName.emplace(unqualified_name, &alias_type_info);
					FLASH_LOG_FORMAT(Parser, Debug, "Also registered unqualified type name: {}", 
					                 StringTable::getStringView(unqualified_name));
				}
			}
		}
	}

	auto decl_node = emplace_node<UsingDeclarationNode>(namespace_handle, identifier_token, using_token);
	return saved_position.success(decl_node);
}

ParseResult Parser::finalize_static_member_init(const StructStaticMember* static_member,
                                                 std::optional<ASTNode> init_expr,
                                                 DeclarationNode& decl_node,
                                                 const Token& name_token,
                                                 ScopedTokenPosition& saved_position) {
	StructStaticMember* mutable_member = const_cast<StructStaticMember*>(static_member);
	ASTNode return_type_node = decl_node.type_node();
	auto [var_decl_node, var_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, name_token);

	if (init_expr.has_value()) {
		mutable_member->initializer = *init_expr;
		auto [var_node, var_ref] = emplace_node_ref<VariableDeclarationNode>(var_decl_node, *init_expr);
		return saved_position.success(var_node);
	}

	// Empty brace init - create a zero literal matching the static member's type
	Type member_type = static_member->type;
	unsigned char member_size_bits = static_cast<unsigned char>(static_member->size * 8);
	if (member_size_bits == 0) {
		member_size_bits = 32;
	}
	NumericLiteralValue zero_value;
	std::string_view zero_str;
	if (member_type == Type::Float || member_type == Type::Double || member_type == Type::LongDouble) {
		zero_value = 0.0;
		zero_str = "0.0"sv;
	} else {
		zero_value = 0ULL;
		zero_str = "0"sv;
	}
	Token zero_token(Token::Type::Literal, zero_str, 0, 0, 0);
	auto literal = emplace_node<ExpressionNode>(NumericLiteralNode(zero_token, zero_value, member_type, TypeQualifier::None, member_size_bits));
	auto [var_node, var_ref] = emplace_node_ref<VariableDeclarationNode>(var_decl_node, literal);
	return saved_position.success(var_node);
}
