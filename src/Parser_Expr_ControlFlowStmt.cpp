ParseResult Parser::parse_for_loop() {
    if (!consume("for"_tok)) {
        return ParseResult::error("Expected 'for' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'for'", current_token_);
    }

    // Enter a new scope for the for loop (C++ standard: for-init-statement creates a scope)
    FlashCpp::SymbolTableScope for_scope(ScopeType::Block);

    // Parse initialization (optional: can be empty, declaration, or expression)
    std::optional<ASTNode> init_statement;

    // Check if init is empty (starts with semicolon)
    if (!consume(";"_tok)) {
        // Not empty, parse init statement
        bool try_as_declaration = false;
        
        if (!peek().is_eof()) {
            if (peek().is_keyword()) {
                // Check if it's a type keyword or CV-qualifier (variable declaration)
                if (type_keywords.find(peek_info().value()) != type_keywords.end()) {
                    try_as_declaration = true;
                }
            } else if (peek().is_identifier()) {
                // Check if it's a known type name (e.g., size_t, string, etc.) or a qualified type (std::size_t)
                StringHandle type_handle = peek_info().handle();
                if (lookupTypeInCurrentContext(type_handle)) {
                    try_as_declaration = true;
                } else if (peek(1) == "::"_tok) {
                    // Treat Identifier followed by :: as a potential qualified type name
                    try_as_declaration = true;
                }
            }
        }
        
        if (try_as_declaration) {
            // Handle variable declaration
            SaveHandle decl_saved = save_token_position();
            ParseResult init = parse_variable_declaration();
            if (init.is_error()) {
                // Not a declaration, backtrack and try as expression instead
                restore_token_position(decl_saved);
                ParseResult expr_init = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                if (expr_init.is_error()) {
                    return expr_init;
                }
                init_statement = expr_init.node();
            } else {
                init_statement = init.node();
            }
        } else {
            // Try parsing as expression
            ParseResult init = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (init.is_error()) {
                return init;
            }
            init_statement = init.node();
        }

        // Check for ranged-for syntax: for (declaration : range_expression)
        if (consume(":"_tok)) {
            // This is a ranged for loop (without init-statement)
            if (!init_statement.has_value()) {
                return ParseResult::error("Ranged for loop requires a loop variable declaration", current_token_);
            }

            // Parse the range expression
            ParseResult range_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (range_result.is_error()) {
                return range_result;
            }

            auto range_expr = range_result.node();
            if (!range_expr.has_value()) {
                return ParseResult::error("Expected range expression in ranged for loop", current_token_);
            }

            if (!consume(")"_tok)) {
                return ParseResult::error("Expected ')' after ranged for loop range expression", current_token_);
            }

            // Parse body (can be a block or a single statement)
            ParseResult body_result;
            if (peek() == "{"_tok) {
                body_result = parse_block();
            } else {
                body_result = parse_statement_or_declaration();
            }

            if (body_result.is_error()) {
                return body_result;
            }

            auto body_node = body_result.node();
            if (!body_node.has_value()) {
                return ParseResult::error("Invalid ranged for loop body", current_token_);
            }

            return ParseResult::success(emplace_node<RangedForStatementNode>(
                *init_statement, *range_expr, *body_node
            ));
        }

        if (!consume(";"_tok)) {
            return ParseResult::error("Expected ';' after for loop initialization", current_token_);
        }
    }

    // At this point, we've parsed the init statement (or it was empty) and consumed the first semicolon
    // Now check for C++20 range-based for with init-statement: for (init; decl : range)
    // This requires checking if the next part looks like a range declaration
    
    // Save position to potentially backtrack
    SaveHandle range_check_pos = save_token_position();
    
    // Check if this could be a C++20 range-based for with init-statement
    bool is_range_for_with_init = false;
    std::optional<ASTNode> range_decl;
    
    if (peek().is_keyword() &&
        type_keywords.find(peek_info().value()) != type_keywords.end()) {
        // Try to parse as a range declaration
        ParseResult decl_result = parse_variable_declaration();
        if (!decl_result.is_error() && decl_result.node().has_value()) {
            // Check if followed by ':'
            if (peek() == ":"_tok) {
                is_range_for_with_init = true;
                range_decl = decl_result.node();
            }
        }
    }
    
    if (is_range_for_with_init) {
        // This is a C++20 range-based for with init-statement
        consume(":"_tok);  // consume the ':'
        
        // Parse the range expression
        ParseResult range_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (range_result.is_error()) {
            return range_result;
        }

        auto range_expr = range_result.node();
        if (!range_expr.has_value()) {
            return ParseResult::error("Expected range expression in ranged for loop", current_token_);
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after ranged for loop range expression", current_token_);
        }

        // Parse body (can be a block or a single statement)
        ParseResult body_result;
        if (peek() == "{"_tok) {
            body_result = parse_block();
        } else {
            body_result = parse_statement_or_declaration();
        }

        if (body_result.is_error()) {
            return body_result;
        }

        auto body_node = body_result.node();
        if (!body_node.has_value()) {
            return ParseResult::error("Invalid ranged for loop body", current_token_);
        }

        // Create ranged for statement with init-statement
        return ParseResult::success(emplace_node<RangedForStatementNode>(
            *range_decl, *range_expr, *body_node, init_statement
        ));
    }
    
    // Not a range-based for with init - restore position and continue with regular for loop
    restore_token_position(range_check_pos);

    // Parse condition (optional: can be empty, defaults to true)
    std::optional<ASTNode> condition;

    // Check if condition is empty (next token is semicolon)
    if (!consume(";"_tok)) {
        // Not empty, parse condition expression
        ParseResult cond_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (cond_result.is_error()) {
            return cond_result;
        }
        condition = cond_result.node();

        if (!consume(";"_tok)) {
            return ParseResult::error("Expected ';' after for loop condition", current_token_);
        }
    }

    // Parse increment/update expression (optional: can be empty)
    std::optional<ASTNode> update_expression;

    // Check if increment is empty (next token is closing paren)
    if (!consume(")"_tok)) {
        // Not empty, parse increment expression (allow comma operator)
        ParseResult inc_result = parse_expression(MIN_PRECEDENCE, ExpressionContext::Normal);
        if (inc_result.is_error()) {
            return inc_result;
        }
        update_expression = inc_result.node();

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after for loop increment", current_token_);
        }
    }

    // Parse body (can be a block or a single statement)
    ParseResult body_result;
    if (peek() == "{"_tok) {
        body_result = parse_block();
    } else {
        body_result = parse_statement_or_declaration();
    }

    if (body_result.is_error()) {
        return body_result;
    }

    // Create for statement node with optional components
    auto body_node = body_result.node();
    if (!body_node.has_value()) {
        return ParseResult::error("Invalid for loop body", current_token_);
    }

    return ParseResult::success(emplace_node<ForStatementNode>(
        init_statement, condition, update_expression, *body_node
    ));
}

ParseResult Parser::parse_while_loop() {
    if (!consume("while"_tok)) {
        return ParseResult::error("Expected 'while' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'while'", current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after while condition", current_token_);
    }

    // Parse body (can be a block or a single statement)
    // Always use parse_statement_or_declaration to ensure proper scope management
    ParseResult body_result = parse_statement_or_declaration();
    if (body_result.is_error()) {
        return body_result;
    }

    // Create while statement node
    auto condition_node = condition_result.node();
    auto body_node = body_result.node();
    if (!condition_node.has_value() || !body_node.has_value()) {
        return ParseResult::error("Invalid while loop construction", current_token_);
    }

    return ParseResult::success(emplace_node<WhileStatementNode>(
        *condition_node, *body_node
    ));
}

ParseResult Parser::parse_do_while_loop() {
    if (!consume("do"_tok)) {
        return ParseResult::error("Expected 'do' keyword", current_token_);
    }

    // Parse body (can be a block or a single statement)
    // Always use parse_statement_or_declaration to ensure proper scope management
    ParseResult body_result = parse_statement_or_declaration();
    if (body_result.is_error()) {
        return body_result;
    }

    // For non-block body statements, consume the trailing semicolon
    // (parse_block handles this internally, but single statements don't)
    if (body_result.node().has_value() && !body_result.node()->is<BlockNode>()) {
        consume(";"_tok);
    }

    if (!consume("while"_tok)) {
        return ParseResult::error("Expected 'while' after do-while body", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'while'", current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after do-while condition", current_token_);
    }

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after do-while statement", current_token_);
    }

    // Create do-while statement node
    auto body_node = body_result.node();
    auto condition_node = condition_result.node();
    if (!body_node.has_value() || !condition_node.has_value()) {
        return ParseResult::error("Invalid do-while loop construction", current_token_);
    }

    return ParseResult::success(emplace_node<DoWhileStatementNode>(
        *body_node, *condition_node
    ));
}

ParseResult Parser::parse_break_statement() {
    auto break_token_opt = peek_info();
    if (break_token_opt.value() != "break"sv) {
        return ParseResult::error("Expected 'break' keyword", current_token_);
    }

    Token break_token = break_token_opt;
    advance(); // Consume the 'break' keyword

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after break statement", current_token_);
    }

    return ParseResult::success(emplace_node<BreakStatementNode>(break_token));
}

ParseResult Parser::parse_continue_statement() {
    auto continue_token_opt = peek_info();
    if (continue_token_opt.value() != "continue"sv) {
        return ParseResult::error("Expected 'continue' keyword", current_token_);
    }

    Token continue_token = continue_token_opt;
    advance(); // Consume the 'continue' keyword

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after continue statement", current_token_);
    }

    return ParseResult::success(emplace_node<ContinueStatementNode>(continue_token));
}

ParseResult Parser::parse_goto_statement() {
    auto goto_token_opt = peek_info();
    if (goto_token_opt.value() != "goto"sv) {
        return ParseResult::error("Expected 'goto' keyword", current_token_);
    }

    Token goto_token = goto_token_opt;
    advance(); // Consume the 'goto' keyword

    // Parse the label identifier
    auto label_token_opt = peek_info();
    if (label_token_opt.type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier after 'goto'", current_token_);
    }

    Token label_token = label_token_opt;
    advance(); // Consume the label identifier

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after goto statement", current_token_);
    }

    return ParseResult::success(emplace_node<GotoStatementNode>(label_token, goto_token));
}

ParseResult Parser::parse_label_statement() {
    // This is called when we've detected identifier followed by ':'
    // The identifier token should be the current token
    auto label_token_opt = peek_info();
    if (label_token_opt.type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier", current_token_);
    }

    Token label_token = label_token_opt;
    advance(); // Consume the label identifier

    if (!consume(":"_tok)) {
        return ParseResult::error("Expected ':' after label", current_token_);
    }

    return ParseResult::success(emplace_node<LabelStatementNode>(label_token));
}

ParseResult Parser::parse_try_statement() {
    // Parse: try { block } catch (type identifier) { block } [catch (...) { block }]
    auto try_token_opt = peek_info();
    if (try_token_opt.value() != "try"sv) {
        return ParseResult::error("Expected 'try' keyword", current_token_);
    }

    Token try_token = try_token_opt;
    advance(); // Consume the 'try' keyword

    // Parse the try block
    auto try_block_result = parse_block();
    if (try_block_result.is_error()) {
        return try_block_result;
    }

    ASTNode try_block = *try_block_result.node();

    // Parse catch clauses (at least one required)
    std::vector<ASTNode> catch_clauses;

    while (peek() == "catch"_tok) {
        Token catch_token = peek_info();
        advance(); // Consume the 'catch' keyword

        if (!consume("("_tok)) {
            return ParseResult::error("Expected '(' after 'catch'", current_token_);
        }

        std::optional<ASTNode> exception_declaration;
        bool is_catch_all = false;

        // Check for catch(...)
        if (peek() == "..."_tok) {
            advance(); // Consume '...'
            is_catch_all = true;
        } else {
            // Parse exception type and optional identifier
            auto type_result = parse_type_and_name();
            if (type_result.is_error()) {
                return type_result;
            }
            exception_declaration = type_result.node();
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after catch declaration", current_token_);
        }

        // Enter a new scope for the catch block and add the exception parameter to the symbol table
        gSymbolTable.enter_scope(ScopeType::Block);
        
        // Add exception parameter to symbol table (if it's not catch(...))
        if (!is_catch_all && exception_declaration.has_value()) {
            const auto& decl = exception_declaration->as<DeclarationNode>();
            if (!decl.identifier_token().value().empty()) {
                gSymbolTable.insert(decl.identifier_token().value(), *exception_declaration);
            }
        }

        // Parse the catch block
        auto catch_block_result = parse_block();
        
        // Exit the catch block scope
        gSymbolTable.exit_scope();
        
        if (catch_block_result.is_error()) {
            return catch_block_result;
        }

        ASTNode catch_block = *catch_block_result.node();

        // Create the catch clause node
        if (is_catch_all) {
            catch_clauses.push_back(emplace_node<CatchClauseNode>(catch_block, catch_token, true));
        } else {
            catch_clauses.push_back(emplace_node<CatchClauseNode>(exception_declaration, catch_block, catch_token));
        }
    }

    if (catch_clauses.empty()) {
        return ParseResult::error("Expected at least one 'catch' clause after 'try' block", current_token_);
    }

    return ParseResult::success(emplace_node<TryStatementNode>(try_block, std::move(catch_clauses), try_token));
}

ParseResult Parser::parse_throw_statement() {
    // Parse: throw; or throw expression;
    auto throw_token_opt = peek_info();
    if (throw_token_opt.value() != "throw"sv) {
        return ParseResult::error("Expected 'throw' keyword", current_token_);
    }

    Token throw_token = throw_token_opt;
    advance(); // Consume the 'throw' keyword

    // Check for rethrow (throw;)
    if (peek() == ";"_tok) {
        advance(); // Consume ';'
        return ParseResult::success(emplace_node<ThrowStatementNode>(throw_token));
    }

    // Parse the expression to throw
    auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (expr_result.is_error()) {
        return expr_result;
    }

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after throw expression", current_token_);
    }

    return ParseResult::success(emplace_node<ThrowStatementNode>(*expr_result.node(), throw_token));
}

// ============================================================================
// Windows SEH (Structured Exception Handling) Parsers
// ============================================================================

ParseResult Parser::parse_seh_try_statement() {
    // Parse: __try { block } __except(filter) { block }
    //    or: __try { block } __finally { block }
    if (peek() != "__try"_tok) {
        return ParseResult::error("Expected '__try' keyword", current_token_);
    }

    Token try_token = peek_info();
    advance(); // Consume the '__try' keyword

    // Parse the try block
    auto try_block_result = parse_block();
    if (try_block_result.is_error()) {
        return try_block_result;
    }

    ASTNode try_block = *try_block_result.node();

    // Check what follows: __except or __finally
    if (!peek().is_keyword()) {
        return ParseResult::error("Expected '__except' or '__finally' after '__try' block", current_token_);
    }

    if (peek() == "__except"_tok) {
        // Parse __except clause
        Token except_token = peek_info();
        advance(); // Consume '__except'

        if (!consume("("_tok)) {
            return ParseResult::error("Expected '(' after '__except'", current_token_);
        }

        // Parse the filter expression
        auto filter_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (filter_expr_result.is_error()) {
            return filter_expr_result;
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after __except filter expression", current_token_);
        }

        // Create filter expression node
        ASTNode filter_node = emplace_node<SehFilterExpressionNode>(*filter_expr_result.node(), except_token);

        // Parse the except block
        auto except_block_result = parse_block();
        if (except_block_result.is_error()) {
            return except_block_result;
        }

        // Create except clause node
        ASTNode except_clause = emplace_node<SehExceptClauseNode>(filter_node, *except_block_result.node(), except_token);

        // Create and return try-except statement node
        return ParseResult::success(emplace_node<SehTryExceptStatementNode>(try_block, except_clause, try_token));

    } else if (peek() == "__finally"_tok) {
        // Parse __finally clause
        Token finally_token = peek_info();
        advance(); // Consume '__finally'

        // Parse the finally block
        auto finally_block_result = parse_block();
        if (finally_block_result.is_error()) {
            return finally_block_result;
        }

        // Create finally clause node
        ASTNode finally_clause = emplace_node<SehFinallyClauseNode>(*finally_block_result.node(), finally_token);

        // Create and return try-finally statement node
        return ParseResult::success(emplace_node<SehTryFinallyStatementNode>(try_block, finally_clause, try_token));

    } else {
        return ParseResult::error("Expected '__except' or '__finally' after '__try' block", current_token_);
    }
}

ParseResult Parser::parse_seh_leave_statement() {
    // Parse: __leave;
    if (peek() != "__leave"_tok) {
        return ParseResult::error("Expected '__leave' keyword", current_token_);
    }

    Token leave_token = peek_info();
    advance(); // Consume the '__leave' keyword

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after '__leave'", current_token_);
    }

    return ParseResult::success(emplace_node<SehLeaveStatementNode>(leave_token));
}

ParseResult Parser::parse_lambda_expression() {
    // Expect '['
    if (!consume("["_tok)) {
        return ParseResult::error("Expected '[' to start lambda expression", current_token_);
    }

    Token lambda_token = current_token_;

    // Parse captures
    std::vector<LambdaCaptureNode> captures;

    // Check for empty capture list
    if (peek() != "]"_tok) {
        // Parse capture list
        while (true) {
            auto token = peek_info();
            if (peek().is_eof()) {
                return ParseResult::error("Unexpected end of file in lambda capture list", current_token_);
            }

            // Check for capture-all
            if (token.value() == "=") {
                advance();
                captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByValue));
            } else if (token.value() == "&") {
                advance();
                // Check if this is capture-all by reference or a specific reference capture
                auto next_token = peek_info();
                if (next_token.type() == Token::Type::Identifier) {
                    // Could be [&x] or [&x = expr]
                    Token id_token = next_token;
                    advance();
                    
                    // Check for init-capture: [&x = expr]
                    if (peek() == "="_tok) {
                        advance(); // consume '='
                        auto init_expr = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                        if (init_expr.is_error()) {
                            return init_expr;
                        }
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByReference, id_token, *init_expr.node()));
                    } else {
                        // Simple reference capture: [&x]
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByReference, id_token));
                    }
                } else {
                    // Capture-all by reference: [&]
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByReference));
                }
            } else if (token.type() == Token::Type::Operator && token.value() == "*") {
                // Check for [*this] capture (C++17)
                advance(); // consume '*'
                auto next_token = peek_info();
                if (next_token.value() == "this") {
                    Token this_token = next_token;
                    advance(); // consume 'this'
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::CopyThis, this_token));
                } else {
                    return ParseResult::error("Expected 'this' after '*' in lambda capture", current_token_);
                }
            } else if (token.type() == Token::Type::Identifier || token.type() == Token::Type::Keyword) {
                // Check for 'this' keyword first
                if (token.value() == "this") {
                    Token this_token = token;
                    advance();
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::This, this_token));
                } else if (token.type() == Token::Type::Identifier) {
                    // Could be [x] or [x = expr]
                    Token id_token = token;
                    advance();
                    
                    
                    // Check for init-capture: [x = expr]
                    if (peek() == "="_tok) {
                        advance(); // consume '='
                        auto init_expr = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                        if (init_expr.is_error()) {
                            return init_expr;
                        }
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByValue, id_token, *init_expr.node()));
                    } else {
                        // Simple value capture: [x]
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByValue, id_token));
                    }
                } else {
                    return ParseResult::error("Expected capture specifier in lambda", token);
                }
            } else {
                return ParseResult::error("Expected capture specifier in lambda", token);
            }

            // Check for comma (more captures) or closing bracket
            if (peek() == ","_tok) {
                advance(); // consume comma
            } else {
                break;
            }
        }
    }

    // Expect ']'
    if (!consume("]"_tok)) {
        return ParseResult::error("Expected ']' after lambda captures", current_token_);
    }

    // Parse optional template parameter list (C++20): []<typename T>(...) 
    std::vector<std::string_view> template_param_names;
    if (peek() == "<"_tok) {
        advance(); // consume '<'
        
        // Parse template parameters
        while (true) {
            // Expect 'typename' or 'class' keyword
            if (peek().is_eof()) {
                return ParseResult::error("Expected template parameter", current_token_);
            }
            
            auto keyword_token = peek_info();
            if (keyword_token.value() != "typename" && keyword_token.value() != "class") {
                return ParseResult::error("Expected 'typename' or 'class' in template parameter", keyword_token);
            }
            advance(); // consume 'typename' or 'class'
            
            // Expect identifier (template parameter name)
            if (!peek().is_identifier()) {
                return ParseResult::error("Expected template parameter name", current_token_);
            }
            
            Token param_name_token = peek_info();
            template_param_names.push_back(param_name_token.value());
            advance(); // consume parameter name
            
            // Check for comma (more parameters) or closing '>'
            if (peek() == ","_tok) {
                advance(); // consume comma
            } else if (peek() == ">"_tok) {
                advance(); // consume '>'
                break;
            } else {
                return ParseResult::error("Expected ',' or '>' in template parameter list", current_token_);
            }
        }
    }

    // Parse parameter list (optional) using unified parse_parameter_list (Phase 1)
    std::vector<ASTNode> parameters;
    if (peek() == "("_tok) {
        FlashCpp::ParsedParameterList params;
        auto param_result = parse_parameter_list(params);
        if (param_result.is_error()) {
            return param_result;
        }
        parameters = std::move(params.parameters);
        // Note: params.is_variadic could be used for variadic lambdas (C++14+)
    }

    // Parse optional lambda specifiers (C++20 lambda-specifier-seq)
    // Accepts mutable, constexpr, consteval in any order
    bool is_mutable = false;
    bool lambda_is_constexpr = false;
    bool lambda_is_consteval = false;
    bool parsing_specifiers = true;
    while (parsing_specifiers) {
        if (!is_mutable && peek() == "mutable"_tok) {
            advance();
            is_mutable = true;
        } else if (!lambda_is_constexpr && !lambda_is_consteval && peek() == "constexpr"_tok) {
            advance();
            lambda_is_constexpr = true;
        } else if (!lambda_is_consteval && !lambda_is_constexpr && peek() == "consteval"_tok) {
            advance();
            lambda_is_consteval = true;
        } else {
            parsing_specifiers = false;
        }
    }

    // Parse optional noexcept specifier (C++20)
    bool lambda_is_noexcept = false;
    if (peek() == "noexcept"_tok) {
        advance(); // consume 'noexcept'
        lambda_is_noexcept = true;
        // Handle noexcept(expr) form - evaluate the expression
        if (peek() == "("_tok) {
            advance(); // consume '('
            auto noexcept_expr = parse_expression(MIN_PRECEDENCE, ExpressionContext::Normal);
            if (noexcept_expr.node().has_value()) {
                ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
                eval_ctx.parser = this;
                auto eval_result = ConstExpr::Evaluator::evaluate(*noexcept_expr.node(), eval_ctx);
                if (eval_result.success()) {
                    lambda_is_noexcept = eval_result.as_int() != 0;
                }
            }
            consume(")"_tok);
        }
    }

    // Skip optional requires clause (C++20)
    if (peek() == "requires"_tok) {
        advance(); // consume 'requires'
        // Skip the requires expression/clause
        if (peek() == "("_tok) {
            // requires(expr) form
            advance(); // consume '('
            int paren_depth = 1;
            while (!peek().is_eof() && paren_depth > 0) {
                if (peek() == "("_tok) paren_depth++;
                else if (peek() == ")"_tok) paren_depth--;
                if (paren_depth > 0) advance();
            }
            consume(")"_tok);
        } else {
            // Simple requires constraint expression (e.g., requires SomeConcept<T>)
            // Skip tokens until we reach '->' or '{'
            while (!peek().is_eof() && peek() != "->"_tok && peek() != "{"_tok) {
                advance();
            }
        }
    }

    // Skip C++20 attributes on lambda (e.g., [[nodiscard]])
    skip_cpp_attributes();

    // Parse optional return type (-> type)
    std::optional<ASTNode> return_type;
    if (peek() == "->"_tok) {
        advance(); // consume '->'
        ParseResult type_result = parse_type_specifier();
        if (type_result.is_error()) {
            return type_result;
        }
        return_type = type_result.node();
    }

    // Parse body (must be a compound statement)
    if (peek() != "{"_tok) {
        return ParseResult::error("Expected '{' for lambda body", current_token_);
    }

    // Add parameters and captures to symbol table before parsing body
    gSymbolTable.enter_scope(ScopeType::Block);
    
    // Add captures to symbol table
    for (const auto& capture : captures) {
        if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
            capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
            // Skip 'this' and '*this' captures - they're handled differently
            continue;
        }
        if (capture.kind() == LambdaCaptureNode::CaptureKind::AllByValue ||
            capture.kind() == LambdaCaptureNode::CaptureKind::AllByReference) {
            // Capture-all will be expanded later, skip for now
            continue;
        }
        
        // For regular captures (by value or by reference), add them to the symbol table
        // so they can be referenced in the lambda body
        Token id_token = capture.identifier_token();
        
        // Determine the type for the capture variable
        // For init-captures, we need to get the type from the initializer
        // For regular captures, we look up the original variable
        TypeSpecifierNode capture_type_node(Type::Auto, TypeQualifier::None, 0, id_token);
        
        if (capture.has_initializer()) {
            // Init-capture: [x = expr]
            // Try to deduce the type from the initializer expression
            auto deduced_type_opt = get_expression_type(*capture.initializer());
            if (deduced_type_opt.has_value()) {
                capture_type_node = *deduced_type_opt;
            }
        } else {
            // Regular capture: [x] or [&x]
            // Look up the original variable to get its type
            auto var_symbol = lookup_symbol(id_token.handle());
            if (var_symbol.has_value()) {
                const DeclarationNode* decl = get_decl_from_symbol(*var_symbol);
                if (decl) {
                    capture_type_node = decl->type_node().as<TypeSpecifierNode>();
                }
            }
        }
        
        // Create a DeclarationNode for the capture variable
        auto capture_decl = emplace_node<DeclarationNode>(
            emplace_node<TypeSpecifierNode>(capture_type_node),
            id_token
        );
        
        // Add to symbol table
        gSymbolTable.insert(id_token.value(), capture_decl);
    }
    
    // Add parameters to symbol table
    for (const auto& param : parameters) {
        if (param.is<DeclarationNode>()) {
            const auto& decl = param.as<DeclarationNode>();
            gSymbolTable.insert(decl.identifier_token().value(), param);
        }
    }

    ParseResult body_result = parse_block();

    // Remove parameters from symbol table after parsing body
    gSymbolTable.exit_scope();

    if (body_result.is_error()) {
        return body_result;
    }

    // Deduce lambda return type if not explicitly specified or if it's auto
    // Now with proper guard against circular dependencies in get_expression_type
    // AND validation that all return paths return the same type
    if (!return_type.has_value() || 
        (return_type->is<TypeSpecifierNode>() && return_type->as<TypeSpecifierNode>().type() == Type::Auto)) {
        // Search lambda body for return statements to deduce return type
        [[maybe_unused]] const BlockNode& body = body_result.node()->as<BlockNode>();
        std::optional<TypeSpecifierNode> deduced_type;
        std::vector<std::pair<TypeSpecifierNode, Token>> all_return_types;  // Track all return types for validation
        
        // Recursive lambda to search for return statements in lambda body
        std::function<void(const ASTNode&)> find_return_in_lambda = [&](const ASTNode& node) {
            if (node.is<ReturnStatementNode>()) {
                const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
                if (ret.expression().has_value()) {
                    // Try to get the type using get_expression_type
                    // The guard in get_expression_type will prevent infinite recursion
                    auto expr_type_opt = get_expression_type(*ret.expression());
                    if (expr_type_opt.has_value()) {
                        // Store this return type for validation
                        all_return_types.emplace_back(*expr_type_opt, lambda_token);
                        
                        FLASH_LOG(Parser, Debug, "Lambda found return statement #", all_return_types.size(), 
                                 " with type=", (int)expr_type_opt->type(), " size=", (int)expr_type_opt->size_in_bits());
                        
                        // Set the deduced type from the first return statement
                        if (!deduced_type.has_value()) {
                            deduced_type = *expr_type_opt;
                            FLASH_LOG(Parser, Debug, "Lambda return type deduced from expression: type=", 
                                     (int)deduced_type->type(), " size=", (int)deduced_type->size_in_bits());
                        }
                    } else {
                        // If we couldn't deduce (possibly due to circular dependency guard),
                        // default to int as a safe fallback
                        if (!deduced_type.has_value()) {
                            deduced_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                            all_return_types.emplace_back(*deduced_type, lambda_token);
                            FLASH_LOG(Parser, Debug, "Lambda return type defaulted to int (type resolution failed)");
                        }
                    }
                }
            } else if (node.is<BlockNode>()) {
                // Recursively search nested blocks
                const BlockNode& block = node.as<BlockNode>();
                const auto& stmts = block.get_statements();
                for (size_t i = 0; i < stmts.size(); ++i) {
                    find_return_in_lambda(stmts[i]);
                }
            } else if (node.is<IfStatementNode>()) {
                const IfStatementNode& if_stmt = node.as<IfStatementNode>();
                find_return_in_lambda(if_stmt.get_then_statement());
                if (if_stmt.has_else()) {
                    find_return_in_lambda(*if_stmt.get_else_statement());
                }
            } else if (node.is<WhileStatementNode>()) {
                const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
                find_return_in_lambda(while_stmt.get_body_statement());
            } else if (node.is<ForStatementNode>()) {
                const ForStatementNode& for_stmt = node.as<ForStatementNode>();
                find_return_in_lambda(for_stmt.get_body_statement());
            } else if (node.is<DoWhileStatementNode>()) {
                const DoWhileStatementNode& do_while = node.as<DoWhileStatementNode>();
                if (do_while.get_body_statement().has_value()) {
                    find_return_in_lambda(do_while.get_body_statement());
                }
            } else if (node.is<SwitchStatementNode>()) {
                const SwitchStatementNode& switch_stmt = node.as<SwitchStatementNode>();
                if (switch_stmt.get_body().has_value()) {
                    find_return_in_lambda(switch_stmt.get_body());
                }
            }
        };
        
        // Search the lambda body
        find_return_in_lambda(*body_result.node());
        
        // Validate that all return statements have compatible types
        if (all_return_types.size() > 1) {
            const TypeSpecifierNode& first_type = all_return_types[0].first;
            for (size_t i = 1; i < all_return_types.size(); ++i) {
                const TypeSpecifierNode& current_type = all_return_types[i].first;
                if (!are_types_compatible(first_type, current_type)) {
                    // Build error message showing the conflicting types
                    std::string error_msg = "Lambda has inconsistent return types: ";
                    error_msg += "first return has type '";
                    error_msg += type_to_string(first_type);
                    error_msg += "', but another return has type '";
                    error_msg += type_to_string(current_type);
                    error_msg += "'";
                    
                    FLASH_LOG(Parser, Error, error_msg);
                    return ParseResult::error(error_msg, all_return_types[i].second);
                }
            }
        }
        
        // If we found a deduced type, use it; otherwise default to void
        if (deduced_type.has_value()) {
            return_type = emplace_node<TypeSpecifierNode>(*deduced_type);
            FLASH_LOG(Parser, Debug, "Lambda auto return type deduced: type=", (int)deduced_type->type());
        } else {
            // No return statement found or return with no value - lambda returns void
            return_type = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0);
            FLASH_LOG(Parser, Debug, "Lambda has no return or returns void");
        }
    }

    // Expand capture-all before creating the lambda node
    std::vector<LambdaCaptureNode> expanded_captures;
    std::vector<ASTNode> captured_var_decls_for_all;  // Store declarations for capture-all
    bool has_capture_all = false;
    LambdaCaptureNode::CaptureKind capture_all_kind = LambdaCaptureNode::CaptureKind::ByValue;

    for (const auto& capture : captures) {
        if (capture.is_capture_all()) {
            has_capture_all = true;
            capture_all_kind = capture.kind();
        } else {
            expanded_captures.push_back(capture);
        }
    }

    if (has_capture_all) {
        // Find all identifiers referenced in the lambda body
        std::unordered_set<StringHandle> referenced_vars;
        findReferencedIdentifiers(*body_result.node(), referenced_vars);

        // Build a set of parameter names to exclude from captures
        std::unordered_set<StringHandle> param_names;
        for (const auto& param : parameters) {
            if (param.is<DeclarationNode>()) {
                param_names.insert(param.as<DeclarationNode>().identifier_token().handle());
            }
        }

        // Build a set of local variable names declared inside the lambda body
        std::unordered_set<StringHandle> local_vars;
        findLocalVariableDeclarations(*body_result.node(), local_vars);

        // Convert capture-all kind to specific capture kind
        LambdaCaptureNode::CaptureKind specific_kind =
            (capture_all_kind == LambdaCaptureNode::CaptureKind::AllByValue)
            ? LambdaCaptureNode::CaptureKind::ByValue
            : LambdaCaptureNode::CaptureKind::ByReference;

        // For each referenced variable, check if it's a non-local variable
        for (const auto& var_name : referenced_vars) {
            // Skip empty names or placeholders
			if (!var_name.isValid() || var_name.view() == "_"sv) {
                continue;
            }

            // Skip if it's a parameter
            if (param_names.find(var_name) != param_names.end()) {
                continue;
            }

            // Skip if it's a local variable declared inside the lambda
            if (local_vars.find(var_name) != local_vars.end()) {
                continue;
            }

            // Look up the variable in the symbol table
            // At this point, we're after the lambda body scope was exited,
            // so any variable found in the symbol table is from an outer scope
            std::optional<ASTNode> var_symbol = lookup_symbol(var_name);
            if (var_symbol.has_value()) {
                // Check if this is a variable (not a function or type)
                // Variables are stored as DeclarationNode or VariableDeclarationNode in the symbol table
                if (const DeclarationNode* decl = get_decl_from_symbol(*var_symbol)) {
                    // Check if this variable is already explicitly captured
                    bool already_captured = false;
                    for (const auto& existing_capture : expanded_captures) {
                        if (existing_capture.identifier_name() == var_name) {
                            already_captured = true;
                            break;
                        }
                    }

                    if (!already_captured) {
                        // Create a capture node for this variable with SPECIFIC kind (not AllByValue/AllByReference)
                        // Use the identifier token from the declaration to ensure stable string_view
                        Token var_token = decl->identifier_token();
                        expanded_captures.emplace_back(specific_kind, var_token);  // Use ByValue or ByReference, not AllByValue/AllByReference
                        // Store the declaration for later use
                        captured_var_decls_for_all.push_back(*var_symbol);
                    }
                }
            }
        }
    }

    auto lambda_node = emplace_node<LambdaExpressionNode>(
        std::move(expanded_captures),
        std::move(parameters),
        *body_result.node(),
        return_type,
        lambda_token,
        is_mutable,
        std::move(template_param_names),
        lambda_is_noexcept,
        lambda_is_constexpr,
        lambda_is_consteval
    );

    // Register the lambda closure type in the type system immediately
    // This allows auto type deduction to work
    const auto& lambda = lambda_node.as<LambdaExpressionNode>();
    auto closure_name = lambda.generate_lambda_name();

    // Get captures from the lambda node (since we moved them above)
    const auto& lambda_captures = lambda.captures();

    TypeInfo& closure_type = add_struct_type(closure_name);
    auto closure_struct_info = std::make_unique<StructTypeInfo>(closure_name, AccessSpecifier::Public);

    // For non-capturing lambdas, create a 1-byte struct (like Clang does)
    if (lambda_captures.empty()) {
        closure_struct_info->total_size = 1;
        closure_struct_info->alignment = 1;
    } else {
        // Add captured variables as members to the closure struct
        for (const auto& capture : lambda_captures) {
            if (capture.is_capture_all()) {
                // Capture-all should have been expanded before this point
                continue;
            }
            
            // Handle [this] capture
            if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
                // [this] capture: store a pointer to the enclosing object (8 bytes on x64)
                // We'll store it with a special member name so it can be accessed later
                TypeSpecifierNode ptr_type(Type::Void, TypeQualifier::None, 64);
                ptr_type.add_pointer_level();  // Make it a void*
                
                // Phase 7B: Intern special member name and use StringHandle overload
                StringHandle this_member_handle = StringTable::getOrInternStringHandle("__this");
                closure_struct_info->addMember(
                    this_member_handle,  // Special member name for captured this
                    Type::Void,         // Base type (will be treated as pointer)
                    0,                  // No type index
                    8,                  // Pointer size on x64
                    8,                  // Alignment
                    AccessSpecifier::Public,
                    std::nullopt,       // No initializer
                    false,              // Not a reference
                    false,              // Not rvalue reference
                    64                  // Size in bits
                );
                continue;  // Skip the rest of processing for this capture
            }
            
            // Handle [*this] capture (C++17)
            if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
                // [*this] capture: store a copy of the entire enclosing object
                // We need to determine the size of the enclosing struct
                if (!member_function_context_stack_.empty()) {
                    const auto& context = member_function_context_stack_.back();
                    StringHandle struct_name = context.struct_name;
                    auto type_it = gTypesByName.find(struct_name);
                    if (type_it != gTypesByName.end()) {
                        const TypeInfo* enclosing_type = type_it->second;
                        const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo();
                        if (enclosing_struct) {
                            StringHandle copy_this_member_handle = StringTable::getOrInternStringHandle("__copy_this");
                            closure_struct_info->addMember(
                                copy_this_member_handle,            // Special member name for copied this
                                Type::Struct,                       // Struct type
                                enclosing_type->type_index_,        // Type index of enclosing struct
                                enclosing_struct->total_size,       // Size of the entire struct
                                enclosing_struct->alignment,        // Alignment from enclosing struct
                                AccessSpecifier::Public,
                                std::nullopt,                       // No initializer
                                false,                              // Not a reference
                                false,                              // Not rvalue reference
                                enclosing_struct->total_size * 8    // Size in bits
                            );
                        }
                    }
                }
                continue;  // Skip the rest of processing for this capture
            }

            auto var_name = StringTable::getOrInternStringHandle(capture.identifier_name());
            TypeSpecifierNode var_type(Type::Int, TypeQualifier::None, 32);  // Default type
            
            if (capture.has_initializer()) {
                // Init-capture: type is inferred from the initializer
                // For now, use simple type inference based on the initializer
                const auto& init_expr = *capture.initializer();
                
                // Try to infer type from the initializer expression
                if (init_expr.is<NumericLiteralNode>()) {
                    var_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                } else if (init_expr.is<IdentifierNode>()) {
                    // Look up the identifier's type
                    auto init_id = init_expr.as<IdentifierNode>().nameHandle();
                    auto init_symbol = lookup_symbol(init_id);
                    if (init_symbol.has_value()) {
                        const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
                        if (init_decl) {
                            var_type = init_decl->type_node().as<TypeSpecifierNode>();
                        }
                    }
                } else if (init_expr.is<ExpressionNode>()) {
                    // For expressions, try to get the type from a binary operation or other expr
                    const auto& expr_node = init_expr.as<ExpressionNode>();
                    if (std::holds_alternative<BinaryOperatorNode>(expr_node)) {
                        // For binary operations, assume int type for arithmetic
                        var_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                    } else if (std::holds_alternative<IdentifierNode>(expr_node)) {
                        auto init_id = std::get<IdentifierNode>(expr_node).nameHandle();
                        auto init_symbol = lookup_symbol(init_id);
                        if (init_symbol.has_value()) {
                            const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
                            if (init_decl) {
                                var_type = init_decl->type_node().as<TypeSpecifierNode>();
                            }
                        }
                    }
                }
                // For other expression types, we'll use the default int type
            } else {
                // Regular capture: look up the variable in the current scope
                std::optional<ASTNode> var_symbol = lookup_symbol(var_name);
                
                if (!var_symbol.has_value()) {
                    continue;
                }
                
                const DeclarationNode* var_decl = get_decl_from_symbol(*var_symbol);
                if (!var_decl) {
                    continue;
                }
                
                var_type = var_decl->type_node().as<TypeSpecifierNode>();
            }

            // Determine size and alignment based on capture kind
            size_t member_size;
            size_t member_alignment;
            Type member_type;
            TypeIndex type_index = 0;

			if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
				// By-reference capture: store a pointer (8 bytes on x64)
				// We store the base type (e.g., Int) but the member will be accessed as a pointer
				member_size = 8;
				member_alignment = 8;
				member_type = var_type.type();
				if (var_type.type() == Type::Struct) {
					type_index = var_type.type_index();
				}
			} else {
                // By-value capture: store the actual value
                member_size = var_type.size_in_bits() / 8;
                member_alignment = member_size;  // Simple alignment = size
                member_type = var_type.type();
                if (var_type.type() == Type::Struct) {
                    type_index = var_type.type_index();
                }
            }

			size_t referenced_size_bits = member_size * 8;
			bool is_ref_capture = (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference);
			if (is_ref_capture) {
				referenced_size_bits = var_type.size_in_bits();
				if (referenced_size_bits == 0 && var_type.type() == Type::Struct) {
					const TypeInfo* member_type_info = nullptr;
					for (const auto& ti : gTypeInfo) {
						if (ti.type_index_ == var_type.type_index()) {
							member_type_info = &ti;
							break;
						}
					}
					if (member_type_info && member_type_info->getStructInfo()) {
						referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
					}
				}
			}

			closure_struct_info->addMember(
				var_name,
				member_type,
				type_index,
				member_size,
				member_alignment,
				AccessSpecifier::Public,
				std::nullopt,
				is_ref_capture,
				false,
				referenced_size_bits
			);
        }

        // addMember() already updates total_size and alignment, but ensure minimum size of 1
        if (closure_struct_info->total_size == 0) {
            closure_struct_info->total_size = 1;
        }
    }

    // Generate operator() member function for the lambda
    // This allows lambda() calls to work
    // Determine return type
    TypeSpecifierNode return_type_spec(Type::Int, TypeQualifier::None, 32);
    if (return_type.has_value()) {
        return_type_spec = return_type->as<TypeSpecifierNode>();
    }

    // Create operator() declaration
    DeclarationNode& operator_call_decl = emplace_node<DeclarationNode>(
        emplace_node<TypeSpecifierNode>(return_type_spec),
        Token(Token::Type::Identifier, "operator()"sv, lambda_token.line(), lambda_token.column(), lambda_token.file_index())
    ).as<DeclarationNode>();

    // Create FunctionDeclarationNode for operator()
    ASTNode operator_call_func_node = emplace_node<FunctionDeclarationNode>(
        operator_call_decl,
        closure_name
    );
    FunctionDeclarationNode& operator_call_func = operator_call_func_node.as<FunctionDeclarationNode>();

    // Add parameters from lambda to operator()
    for (const auto& param : lambda.parameters()) {
        operator_call_func.add_parameter_node(param);
    }

    // Add operator() as a member function
    StructMemberFunction operator_call_member(
        StringTable::getOrInternStringHandle("operator()"),
        operator_call_func_node,  // Use the original ASTNode, not a copy
        AccessSpecifier::Public,
        false,  // not constructor
        false,  // not destructor
        true,   // is operator overload
        "()"    // operator symbol
    );

    closure_struct_info->member_functions.push_back(operator_call_member);

    closure_type.struct_info_ = std::move(closure_struct_info);

    // Wrap the lambda in an ExpressionNode before returning
    ExpressionNode expr_node = lambda_node.as<LambdaExpressionNode>();
    return ParseResult::success(emplace_node<ExpressionNode>(std::move(expr_node)));
}

ParseResult Parser::parse_if_statement() {
    if (!consume("if"_tok)) {
        return ParseResult::error("Expected 'if' keyword", current_token_);
    }

    // Check for C++17 'if constexpr'
    bool is_constexpr = false;
    if (peek() == "constexpr"_tok) {
        consume("constexpr"_tok);
        is_constexpr = true;
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'if'", current_token_);
    }

    // Unified declaration handling for if-statements:
    // 1. C++17 if-with-initializer: if (Type var = expr; condition)
    // 2. C++ declaration-as-condition: if (Type var = expr)
    // Both start with a type followed by a variable declaration.
    // We try parse_variable_declaration() once and check the delimiter:
    //   ';' → init-statement, then parse the condition expression separately
    //   ')' → declaration-as-condition
    //   otherwise → not a declaration, fall back to expression parsing
    std::optional<ASTNode> init_statement;
    std::optional<FlashCpp::SymbolTableScope> if_scope;
    ParseResult condition;
    bool condition_parsed = false;

    // Determine if the next tokens could be a declaration (keyword type or identifier type)
    bool try_declaration = false;
    if (peek().is_keyword() && type_keywords.find(peek_info().value()) != type_keywords.end()) {
        try_declaration = true;
    } else if (peek().is_identifier()) {
        // Lookahead: check for "Type name =" pattern where Type can be qualified (ns::Type)
        // This avoids misinterpreting simple "if (x)" as a declaration
        auto lookahead = save_token_position();
        advance(); // skip potential type name
        // Skip qualified name components: ns::inner::Type
        while (peek() == "::"_tok) {
            advance(); // skip '::'
            if (peek().is_identifier()) {
                advance(); // skip next component
            }
        }
        if (peek() == "<"_tok) {
            skip_template_arguments();
        }
        while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok) {
            advance();
        }
        if (peek().is_identifier()) {
            advance(); // skip potential variable name
            if (peek() == "="_tok || peek() == "{"_tok) {
                try_declaration = true;
            }
        }
        restore_token_position(lookahead);
    }

    if (try_declaration) {
        auto checkpoint = save_token_position();
        if_scope.emplace(ScopeType::Block);

        ParseResult potential_decl = parse_variable_declaration();

        if (!potential_decl.is_error() && peek() == ";"_tok) {
            // Init-statement: if (Type var = expr; condition)
            discard_saved_token(checkpoint);
            init_statement = potential_decl.node();
            if (!consume(";"_tok)) {
                return ParseResult::error("Expected ';' after if initializer", current_token_);
            }
        } else if (!potential_decl.is_error() && peek() == ")"_tok) {
            // Declaration-as-condition: if (Type var = expr)
            discard_saved_token(checkpoint);
            condition = potential_decl;
            condition_parsed = true;
        } else {
            // Not a declaration - undo scope (reset calls exit_scope) and restore tokens
            if_scope.reset();
            restore_token_position(checkpoint);
        }
    }

    // Parse condition as expression if not already set by declaration-as-condition
    if (!condition_parsed) {
        condition = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    }
    if (condition.is_error()) {
        return condition;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after if condition", current_token_);
    }

    // Skip C++20 [[likely]]/[[unlikely]] attributes on if branches
    skip_cpp_attributes();

    // For if constexpr during template body re-parsing with parameter packs,
    // evaluate the condition at compile time and skip the dead branch
    // (which may contain ill-formed code like unexpanded parameter packs)
    if (is_constexpr && has_parameter_packs_ && condition.node().has_value()) {
        ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
        eval_ctx.parser = this;
        auto eval_result = ConstExpr::Evaluator::evaluate(*condition.node(), eval_ctx);
        if (eval_result.success()) {
            bool condition_value = eval_result.as_int() != 0;
            FLASH_LOG(Templates, Debug, "if constexpr condition evaluated to ", condition_value ? "true" : "false", " during template body re-parse");
            
            if (condition_value) {
                // Parse the then-branch normally
                ParseResult then_stmt_result;
                if (peek() == "{"_tok) {
                    then_stmt_result = parse_block();
                } else {
                    then_stmt_result = parse_statement_or_declaration();
                    consume(";"_tok);
                }
                // Skip the else-branch if present
                if (peek() == "else"_tok) {
                    advance(); // consume 'else'
                    skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after else
                    // Recursively skip the else branch, which may be:
                    // 1. A block: else { ... }
                    // 2. An else-if chain: else if (...) { ... } else ...
                    // 3. A single statement: else return x;
                    while (true) {
                        if (peek() == "{"_tok) {
                            skip_balanced_braces();
                            break;
                        } else if (peek() == "if"_tok) {
                            advance(); // consume 'if'
                            if (peek() == "constexpr"_tok) advance();
                            skip_balanced_parens(); // skip condition
                            skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after if condition
                            // Skip then-branch (block or statement)
                            if (peek() == "{"_tok) {
                                skip_balanced_braces();
                            } else {
                                while (!peek().is_eof() && peek() != ";"_tok) advance();
                                consume(";"_tok);
                            }
                            // Continue loop to handle else/else-if after this branch
                            if (peek() == "else"_tok) {
                                advance(); // consume 'else'
                                skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after inner else
                                continue; // loop handles next branch
                            }
                            break;
                        } else {
                            // Single statement else - skip to semicolon
                            while (!peek().is_eof() && peek() != ";"_tok) advance();
                            consume(";"_tok);
                            break;
                        }
                    }
                }
                // Return just the then-branch content
                return then_stmt_result;
            } else {
                // Skip the then-branch
                if (peek() == "{"_tok) {
                    skip_balanced_braces();
                } else {
                    while (!peek().is_eof() && peek() != ";"_tok) advance();
                    consume(";"_tok);
                }
                // Parse the else-branch if present
                if (peek() == "else"_tok) {
                    consume("else"_tok);
                    skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after else
                    ParseResult else_result;
                    if (peek() == "{"_tok) {
                        else_result = parse_block();
                    } else if (peek() == "if"_tok) {
                        else_result = parse_if_statement();
                    } else {
                        else_result = parse_statement_or_declaration();
                        consume(";"_tok);
                    }
                    if (!else_result.is_error() && else_result.node().has_value()) {
                        return else_result;
                    }
                    return else_result;  // Propagate the error
                }
                // No else branch and condition is false - return empty block
                return ParseResult::success(emplace_node<BlockNode>());
            }
        }
    }

    // Parse then-statement (can be a block or a single statement)
    ParseResult then_stmt;
    if (peek() == "{"_tok) {
        then_stmt = parse_block();
    } else {
        then_stmt = parse_statement_or_declaration();
        // Consume trailing semicolon if present (expression statements don't consume their ';')
        consume(";"_tok);
    }

    if (then_stmt.is_error()) {
        return then_stmt;
    }

    // Check for else clause
    std::optional<ASTNode> else_stmt;
    if (peek() == "else"_tok) {
        consume("else"_tok);

        // Skip C++20 [[likely]]/[[unlikely]] attributes on else branches
        skip_cpp_attributes();

        // Parse else-statement (can be a block, another if, or a single statement)
        ParseResult else_result;
        if (peek() == "{"_tok) {
            else_result = parse_block();
        } else if (peek() == "if"_tok) {
            // Handle else-if chain
            else_result = parse_if_statement();
        } else {
            else_result = parse_statement_or_declaration();
            // Consume trailing semicolon if present
            consume(";"_tok);
        }

        if (else_result.is_error()) {
            return else_result;
        }
        else_stmt = else_result.node();
    }

    // Create if statement node
    if (auto cond_node = condition.node()) {
        if (auto then_node = then_stmt.node()) {
            return ParseResult::success(emplace_node<IfStatementNode>(
                *cond_node, *then_node, else_stmt, init_statement, is_constexpr
            ));
        }
    }

    return ParseResult::error("Invalid if statement construction", current_token_);
}

ParseResult Parser::parse_switch_statement() {
    if (!consume("switch"_tok)) {
        return ParseResult::error("Expected 'switch' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'switch'", current_token_);
    }

    // Parse the switch condition expression
    auto condition = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition.is_error()) {
        return condition;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after switch condition", current_token_);
    }

    // Parse the switch body (must be a compound statement with braces)
    if (!consume("{"_tok)) {
        return ParseResult::error("Expected '{' for switch body", current_token_);
    }

    // Create a block to hold case/default labels and their statements
    auto [block_node, block_ref] = create_node_ref(BlockNode());

    // Parse case and default labels
    while (!peek().is_eof() && peek() != "}"_tok) {
        auto current = peek_info();

        if (current.type() == Token::Type::Keyword && current.value() == "case") {
            // Parse case label
            advance(); // consume 'case'

            // Parse case value (must be a constant expression)
            auto case_value = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (case_value.is_error()) {
                return case_value;
            }

            if (!consume(":"_tok)) {
                return ParseResult::error("Expected ':' after case value", current_token_);
            }

            // Skip C++20 [[likely]]/[[unlikely]] attributes after case label
            skip_cpp_attributes();

            // Parse statements until next case/default/closing brace
            // We collect all statements for this case into a sub-block
            auto [case_block_node, case_block_ref] = create_node_ref(BlockNode());

            while (!peek().is_eof() &&
                   peek() != "}"_tok &&
                   !(peek().is_keyword() &&
                     (peek() == "case"_tok || peek() == "default"_tok))) {
                // Skip stray semicolons (empty statements)
                if (peek().is_punctuator() && peek() == ";"_tok) {
                    advance();
                    continue;
                }

                auto stmt = parse_statement_or_declaration();
                if (stmt.is_error()) {
                    return stmt;
                }
                if (auto stmt_node = stmt.node()) {
                    case_block_ref.add_statement_node(*stmt_node);
                }
            }

            // Create case label node with the block of statements
            auto case_label = emplace_node<CaseLabelNode>(*case_value.node(), case_block_node);
            block_ref.add_statement_node(case_label);

        } else if (current.type() == Token::Type::Keyword && current.value() == "default") {
            // Parse default label
            advance(); // consume 'default'

            if (!consume(":"_tok)) {
                return ParseResult::error("Expected ':' after 'default'", current_token_);
            }

            // Skip C++20 [[likely]]/[[unlikely]] attributes after default label
            skip_cpp_attributes();

            // Parse statements until next case/default/closing brace
            auto [default_block_node, default_block_ref] = create_node_ref(BlockNode());

            while (!peek().is_eof() &&
                   peek() != "}"_tok &&
                   !(peek().is_keyword() &&
                     (peek() == "case"_tok || peek() == "default"_tok))) {
                // Skip stray semicolons (empty statements)
                if (peek().is_punctuator() && peek() == ";"_tok) {
                    advance();
                    continue;
                }

                auto stmt = parse_statement_or_declaration();
                if (stmt.is_error()) {
                    return stmt;
                }
                if (auto stmt_node = stmt.node()) {
                    default_block_ref.add_statement_node(*stmt_node);
                }
            }

            // Create default label node with the block of statements
            auto default_label = emplace_node<DefaultLabelNode>(default_block_node);
            block_ref.add_statement_node(default_label);

        } else {
            // If we're here, we have an unexpected token at the switch body level
            std::string error_msg = "Expected 'case' or 'default' in switch body, but found: ";
            if (current.type() == Token::Type::Keyword) {
                error_msg += "keyword '" + std::string(current.value()) + "'";
            } else if (current.type() == Token::Type::Identifier) {
                error_msg += "identifier '" + std::string(current.value()) + "'";
            } else {
                error_msg += "'" + std::string(current.value()) + "'";
            }
            return ParseResult::error(error_msg, current_token_);
        }
    }

    if (!consume("}"_tok)) {
        return ParseResult::error("Expected '}' to close switch body", current_token_);
    }

    // Create switch statement node
    if (auto cond_node = condition.node()) {
        return ParseResult::success(emplace_node<SwitchStatementNode>(*cond_node, block_node));
    }

    return ParseResult::error("Invalid switch statement construction", current_token_);
}

