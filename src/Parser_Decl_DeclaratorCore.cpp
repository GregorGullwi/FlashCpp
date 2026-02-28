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
            type_spec.set_reference_qualifier(ref_qualifier);
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
                        DeclarationNode& inner_decl = decl_node->as<FunctionDeclarationNode>().decl_node();
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
                                type_spec.set_reference_qualifier(ReferenceQualifier::RValueReference);  // rvalue reference
                            } else {
                                type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
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

    // Function pointer check after reference declarators have been consumed.
    // This handles patterns like: int& (*fp)(int) or ostream& (*__pf)(ostream&)
    // After consuming '&', we now see '(' which starts a function pointer declarator.
    if ((type_spec.is_reference() || type_spec.is_rvalue_reference()) && peek() == "("_tok) {
        SaveHandle saved_pos = save_token_position();
        advance(); // consume '('

        parse_calling_convention();

        if (peek() == "*"_tok) {
            // Looks like a function pointer with reference return type: type& (*name)(params)
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
                
                // Parse pointer and reference declarators: * [const] [volatile] *... & &&
                // Example: void* or const int* const* or int&
                consume_pointer_ref_modifiers(param_type);
                
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
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Constexpr;
			advance();
		} else if (kw == "constinit") {
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Constinit;
			advance();
		} else if (kw == "consteval") {
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Consteval;
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

