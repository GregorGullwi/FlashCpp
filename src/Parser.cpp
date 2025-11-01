#include "Parser.h"
#ifdef USE_LLVM
#include "LibClangIRGenerator.h"
#endif
#include "OverloadResolution.h"
#include <string_view> // Include string_view header
#include <unordered_set> // Include unordered_set header
#include "ChunkedString.h"

// Break into the debugger only on Windows
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define DEBUG_BREAK() if (IsDebuggerPresent()) { DebugBreak(); }
#else
    // On non-Windows platforms, define as a no-op (does nothing)
    #define DEBUG_BREAK() ((void)0)
#endif

// Define the global symbol table (declared as extern in SymbolTable.h)
SymbolTable gSymbolTable;
ChunkedStringAllocator gChunkedStringAllocator;

// Type keywords set - used for if-statement initializer detection
static const std::unordered_set<std::string_view> type_keywords = {
	"int"sv, "float"sv, "double"sv, "char"sv, "bool"sv, "void"sv,
	"short"sv, "long"sv, "signed"sv, "unsigned"sv, "const"sv, "volatile"sv, "alignas"sv
};

// Helper function to find all local variable declarations in an AST node
static void findLocalVariableDeclarations(const ASTNode& node, std::unordered_set<std::string>& var_names) {
	if (node.is<VariableDeclarationNode>()) {
		const auto& var_decl = node.as<VariableDeclarationNode>();
		const auto& decl = var_decl.declaration();
		var_names.insert(std::string(decl.identifier_token().value()));
	} else if (node.is<BlockNode>()) {
		const auto& block = node.as<BlockNode>();
		const auto& stmts = block.get_statements();
		for (size_t i = 0; i < stmts.size(); ++i) {
			findLocalVariableDeclarations(stmts[i], var_names);
		}
	} else if (node.is<IfStatementNode>()) {
		const auto& if_stmt = node.as<IfStatementNode>();
		if (if_stmt.get_init_statement().has_value()) {
			findLocalVariableDeclarations(*if_stmt.get_init_statement(), var_names);
		}
		findLocalVariableDeclarations(if_stmt.get_then_statement(), var_names);
		if (if_stmt.get_else_statement().has_value()) {
			findLocalVariableDeclarations(*if_stmt.get_else_statement(), var_names);
		}
	} else if (node.is<WhileStatementNode>()) {
		const auto& while_stmt = node.as<WhileStatementNode>();
		findLocalVariableDeclarations(while_stmt.get_body_statement(), var_names);
	} else if (node.is<DoWhileStatementNode>()) {
		const auto& do_while = node.as<DoWhileStatementNode>();
		findLocalVariableDeclarations(do_while.get_body_statement(), var_names);
	} else if (node.is<ForStatementNode>()) {
		const auto& for_stmt = node.as<ForStatementNode>();
		if (for_stmt.get_init_statement().has_value()) {
			findLocalVariableDeclarations(*for_stmt.get_init_statement(), var_names);
		}
		findLocalVariableDeclarations(for_stmt.get_body_statement(), var_names);
	}
}

// Helper function to find all identifiers referenced in an AST node
static void findReferencedIdentifiers(const ASTNode& node, std::unordered_set<std::string>& identifiers) {
	if (node.is<IdentifierNode>()) {
		identifiers.insert(std::string(node.as<IdentifierNode>().name()));
	} else if (node.is<ExpressionNode>()) {
		// ExpressionNode is a variant, so we need to check each alternative
		const auto& expr = node.as<ExpressionNode>();
		std::visit([&](const auto& inner_node) {
			using T = std::decay_t<decltype(inner_node)>;
			if constexpr (std::is_same_v<T, IdentifierNode>) {
				identifiers.insert(std::string(inner_node.name()));
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<BinaryOperatorNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<UnaryOperatorNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<FunctionCallNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<MemberAccessNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, MemberFunctionCallNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<MemberFunctionCallNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<ArraySubscriptNode*>(&inner_node)), identifiers);
			}
			// Add more types as needed
		}, expr);
	} else if (node.is<BinaryOperatorNode>()) {
		const auto& binop = node.as<BinaryOperatorNode>();
		findReferencedIdentifiers(binop.get_lhs(), identifiers);
		findReferencedIdentifiers(binop.get_rhs(), identifiers);
	} else if (node.is<UnaryOperatorNode>()) {
		const auto& unop = node.as<UnaryOperatorNode>();
		findReferencedIdentifiers(unop.get_operand(), identifiers);
	} else if (node.is<FunctionCallNode>()) {
		const auto& call = node.as<FunctionCallNode>();
		// Don't add the function name itself, just the arguments
		const auto& args = call.arguments();
		for (size_t i = 0; i < args.size(); ++i) {
			findReferencedIdentifiers(args[i], identifiers);
		}
	} else if (node.is<ReturnStatementNode>()) {
		const auto& ret = node.as<ReturnStatementNode>();
		if (ret.expression().has_value()) {
			findReferencedIdentifiers(*ret.expression(), identifiers);
		}
	} else if (node.is<BlockNode>()) {
		const auto& block = node.as<BlockNode>();
		const auto& stmts = block.get_statements();
		for (size_t i = 0; i < stmts.size(); ++i) {
			findReferencedIdentifiers(stmts[i], identifiers);
		}
	} else if (node.is<IfStatementNode>()) {
		const auto& if_stmt = node.as<IfStatementNode>();
		findReferencedIdentifiers(if_stmt.get_condition(), identifiers);
		findReferencedIdentifiers(if_stmt.get_then_statement(), identifiers);
		if (if_stmt.get_else_statement().has_value()) {
			findReferencedIdentifiers(*if_stmt.get_else_statement(), identifiers);
		}
	} else if (node.is<WhileStatementNode>()) {
		const auto& while_stmt = node.as<WhileStatementNode>();
		findReferencedIdentifiers(while_stmt.get_condition(), identifiers);
		findReferencedIdentifiers(while_stmt.get_body_statement(), identifiers);
	} else if (node.is<DoWhileStatementNode>()) {
		const auto& do_while = node.as<DoWhileStatementNode>();
		findReferencedIdentifiers(do_while.get_body_statement(), identifiers);
		findReferencedIdentifiers(do_while.get_condition(), identifiers);
	} else if (node.is<ForStatementNode>()) {
		const auto& for_stmt = node.as<ForStatementNode>();
		if (for_stmt.get_init_statement().has_value()) {
			findReferencedIdentifiers(*for_stmt.get_init_statement(), identifiers);
		}
		if (for_stmt.get_condition().has_value()) {
			findReferencedIdentifiers(*for_stmt.get_condition(), identifiers);
		}
		if (for_stmt.get_update_expression().has_value()) {
			findReferencedIdentifiers(*for_stmt.get_update_expression(), identifiers);
		}
		findReferencedIdentifiers(for_stmt.get_body_statement(), identifiers);
	} else if (node.is<MemberAccessNode>()) {
		const auto& member = node.as<MemberAccessNode>();
		findReferencedIdentifiers(member.object(), identifiers);
		// Don't add the member name itself
	} else if (node.is<MemberFunctionCallNode>()) {
		const auto& member_call = node.as<MemberFunctionCallNode>();
		findReferencedIdentifiers(member_call.object(), identifiers);
		const auto& args = member_call.arguments();
		for (size_t i = 0; i < args.size(); ++i) {
			findReferencedIdentifiers(args[i], identifiers);
		}
	} else if (node.is<ArraySubscriptNode>()) {
		const auto& subscript = node.as<ArraySubscriptNode>();
		findReferencedIdentifiers(subscript.array_expr(), identifiers);
		findReferencedIdentifiers(subscript.index_expr(), identifiers);
	} else if (node.is<VariableDeclarationNode>()) {
		const auto& var_decl = node.as<VariableDeclarationNode>();
		if (var_decl.initializer().has_value()) {
			findReferencedIdentifiers(*var_decl.initializer(), identifiers);
		}
	}
	// Add more node types as needed
}

bool Parser::generate_coff(const std::string& outputFilename) {
#ifdef USE_LLVM
    return FlashCpp::GenerateCOFF(ast_nodes_, outputFilename);
#else
    return false; // Not implemented in this configuration
#endif
}

Parser::Parser(Lexer& lexer, CompileContext& context)
    : lexer_(lexer), context_(context), current_token_(lexer_.next_token()) {
    initialize_native_types();
    ast_nodes_.reserve(default_ast_tree_size_);
}

Parser::ScopedTokenPosition::ScopedTokenPosition(class Parser& parser)
    : parser_(parser), saved_position_(parser.save_token_position()) {}

Parser::ScopedTokenPosition::~ScopedTokenPosition() {
    if (!discarded_) {
        parser_.restore_token_position(saved_position_);
    }
}

ParseResult Parser::ScopedTokenPosition::success(ASTNode node) {
    discarded_ = true;
    parser_.discard_saved_token(saved_position_);
    return ParseResult::success(node);
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
    saved_tokens_[cur_pos.cursor_] = { current_token_, ast_nodes_.size() };
    return cur_pos;
}

void Parser::restore_token_position(const TokenPosition& saved_token_pos) {
    lexer_.restore_token_position(saved_token_pos);
    SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
    current_token_ = saved_token.current_token_;
    ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_,
        ast_nodes_.end());
    //saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::discard_saved_token(const TokenPosition& saved_token_pos) {
	saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::skip_balanced_braces() {
	int brace_depth = 0;
	consume_token();  // consume opening '{'
	brace_depth = 1;

	while (brace_depth > 0 && peek_token().has_value()) {
		auto token = peek_token();
		if (token->value() == "{") {
			brace_depth++;
		} else if (token->value() == "}") {
			brace_depth--;
		}
		consume_token();
	}
}

ParseResult Parser::parse_top_level_node()
{
	// Save the current token's position to restore later in case of a parsing
	// error
	ScopedTokenPosition saved_position(*this);

#if WITH_DEBUG_INFO
	if (break_at_line_.has_value() && peek_token()->line() == break_at_line_)
	{
		DEBUG_BREAK();
	}
#endif

	// Check for #pragma pack directives
	if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "#") {
		consume_token(); // consume '#'
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier &&
		    peek_token()->value() == "pragma") {
			consume_token(); // consume 'pragma'
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier &&
			    peek_token()->value() == "pack") {
				consume_token(); // consume 'pack'

				if (!consume_punctuator("(")) {
					return ParseResult::error("Expected '(' after '#pragma pack'", *current_token_);
				}

				// Check if it's empty: #pragma pack()
				if (consume_punctuator(")")) {
					context_.setPackAlignment(0); // Reset to default
					return saved_position.success();
				}

				// Try to parse a number
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Literal) {
					std::string_view value_str = peek_token()->value();
					try {
						size_t alignment = std::stoull(std::string(value_str));
						if (alignment == 0 || alignment == 1 || alignment == 2 ||
						    alignment == 4 || alignment == 8 || alignment == 16) {
							context_.setPackAlignment(alignment);
							consume_token(); // consume the number
							if (!consume_punctuator(")")) {
								return ParseResult::error("Expected ')' after pack alignment value", *current_token_);
							}
							return saved_position.success();
						}
					} catch (...) {
						// Invalid number
					}
				}

				// If we get here, it's an unsupported pragma pack format
				return ParseResult::error("Unsupported #pragma pack format", *current_token_);
			}
		}
	}

	// Check if it's a using directive, using declaration, or namespace alias
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == "using") {
		auto result = parse_using_directive_or_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return result;
	}

	// Check if it's a namespace declaration
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == "namespace") {
		auto result = parse_namespace();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return result;
	}

	// Check if it's a class or struct declaration
	// Note: alignas can appear before struct, but we handle that in parse_struct_declaration
	// If alignas appears before a variable declaration, it will be handled by parse_declaration_or_function_definition
	if (peek_token()->type() == Token::Type::Keyword &&
		(peek_token()->value() == "class" || peek_token()->value() == "struct")) {
		auto result = parse_struct_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return result;
	}

	// Check if it's an enum declaration
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "enum") {
		auto result = parse_enum_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return result;
	}

	// Check if it's a typedef declaration
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "typedef") {
		auto result = parse_typedef_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return result;
	}

	// Check for extern "C" linkage specification
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "extern") {
		// Save position in case this is just a regular extern declaration
		TokenPosition extern_saved_pos = save_token_position();
		consume_token(); // consume 'extern'

		// Check if this is extern "C" or extern "C++"
		if (peek_token().has_value() && peek_token()->type() == Token::Type::StringLiteral) {
			std::string_view linkage_str = peek_token()->value();

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
				return ParseResult::error("Unknown linkage specification: " + std::string(linkage_str), *current_token_);
			}

			consume_token(); // consume linkage string

			// Discard the extern_saved_pos since we're handling extern "C"
			discard_saved_token(extern_saved_pos);

			// Check for block form: extern "C" { ... }
			if (peek_token().has_value() && peek_token()->value() == "{") {
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
				return result;
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
		} else {
			// Regular extern without linkage specification, restore and continue
			restore_token_position(extern_saved_pos);
		}
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
    // Check for alignas specifier before the type
    std::optional<size_t> custom_alignment = parse_alignas_specifier();

    // Parse the type specifier
    auto type_specifier_result = parse_type_specifier();
    if (type_specifier_result.is_error()) {
        return type_specifier_result;
    }

    // Get the type specifier node to modify it with pointer levels
    TypeSpecifierNode& type_spec = type_specifier_result.node()->as<TypeSpecifierNode>();

    // Check if this might be a function pointer declaration
    // Function pointers have the pattern: type (*identifier)(params)
    // We need to check for '(' followed by '*' to detect this
    if (peek_token().has_value() && peek_token()->value() == "(") {
        // Save position in case this isn't a function pointer
        TokenPosition saved_pos = save_token_position();
        consume_token(); // consume '('

        // Check if next token is '*' (function pointer pattern)
        if (peek_token().has_value() && peek_token()->value() == "*") {
            // This looks like a function pointer! Use parse_declarator
            restore_token_position(saved_pos);
            auto result = parse_declarator(type_spec, Linkage::None);
            if (!result.is_error()) {
                if (auto decl_node = result.node()) {
                    if (custom_alignment.has_value()) {
                        decl_node->as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
                    }
                }
                return result;
            }
            // If parse_declarator fails, fall through to regular parsing
            restore_token_position(saved_pos);
        } else {
            // Not a function pointer, restore and continue with regular parsing
            restore_token_position(saved_pos);
        }
    }

    // Regular pointer/reference/identifier parsing (existing code)
    // Parse pointer declarators: * [const] [volatile] *...
    // Example: int* const* volatile ptr
    while (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
           peek_token()->value() == "*") {
        consume_token(); // consume '*'

        // Check for CV-qualifiers after the *
        CVQualifier ptr_cv = CVQualifier::None;
        while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
            std::string_view kw = peek_token()->value();
            if (kw == "const") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Const));
                consume_token();
            } else if (kw == "volatile") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Volatile));
                consume_token();
            } else {
                break;
            }
        }

        type_spec.add_pointer_level(ptr_cv);
    }

    // Parse reference declarators: & or &&
    // Example: int& ref or int&& rvalue_ref
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator) {
        std::string_view op_value = peek_token()->value();

        if (op_value == "&&") {
            // Rvalue reference (lexer tokenizes && as a single token)
            consume_token();
            type_spec.set_reference(true);  // true = rvalue reference
        } else if (op_value == "&") {
            // Lvalue reference
            consume_token();
            type_spec.set_reference(false);  // false = lvalue reference
        }
    }

    // Check for alignas specifier before the identifier (if not already specified)
    if (!custom_alignment.has_value()) {
        custom_alignment = parse_alignas_specifier();
    }

    // Parse the identifier (name) or operator overload
    Token identifier_token;

    // Check for operator overload (e.g., operator=, operator())
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
        peek_token()->value() == "operator") {

        Token operator_keyword_token = *peek_token();
        consume_token(); // consume 'operator'

        std::string_view operator_name;

        // Check for operator()
        if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
            peek_token()->value() == "(") {
            consume_token(); // consume '('
            if (!peek_token().has_value() || peek_token()->value() != ")") {
                return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
            }
            consume_token(); // consume ')'
            static const std::string operator_call_name = "operator()";
            operator_name = operator_call_name;
        }
        // Check for other operators
        else if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator) {
            Token operator_symbol_token = *peek_token();
            std::string_view operator_symbol = operator_symbol_token.value();
            consume_token(); // consume operator symbol

            // For now, we support operator= and operator()
            if (operator_symbol != "=") {
                return ParseResult::error("Only operator= and operator() are currently supported", operator_symbol_token);
            }
            static const std::string operator_eq_name = "operator=";
            operator_name = operator_eq_name;
        }
        else {
            return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
        }

        // Create a synthetic identifier token for the operator
        identifier_token = Token(Token::Type::Identifier, operator_name,
                                operator_keyword_token.line(), operator_keyword_token.column(),
                                operator_keyword_token.file_index());
    } else {
        // Regular identifier
        auto id_token = consume_token();
        if (!id_token) {
            return ParseResult::error("Expected identifier token", Token());
        }
        if (id_token->type() != Token::Type::Identifier) {
            return ParseResult::error("Expected identifier token", *id_token);
        }
        identifier_token = *id_token;
    }

    // Check for array declaration: identifier[size]
    std::optional<ASTNode> array_size;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
        peek_token()->value() == "[") {
        consume_token(); // consume '['

        // Parse the array size expression
        ParseResult size_result = parse_expression();
        if (size_result.is_error()) {
            return size_result;
        }
        array_size = size_result.node();

        // Expect closing ']'
        if (!peek_token().has_value() || peek_token()->type() != Token::Type::Punctuator ||
            peek_token()->value() != "]") {
            return ParseResult::error("Expected ']' after array size", *current_token_);
        }
        consume_token(); // consume ']'
    }

    // Unwrap the optional ASTNode before passing it to emplace_node
    if (auto node = type_specifier_result.node()) {
        ASTNode decl_node;
        if (array_size.has_value()) {
            decl_node = emplace_node<DeclarationNode>(*node, identifier_token, array_size);
        } else {
            decl_node = emplace_node<DeclarationNode>(*node, identifier_token);
        }

        // Apply custom alignment if specified
        if (custom_alignment.has_value()) {
            decl_node.as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
        }

        return ParseResult::success(decl_node);
    }
    return ParseResult::error("Invalid type specifier node", identifier_token);
}

// NEW: Parse declarators (handles function pointers, arrays, etc.)
ParseResult Parser::parse_declarator(TypeSpecifierNode& base_type, Linkage linkage) {
    // Check for parenthesized declarator: '(' '*' identifier ')'
    // This is the pattern for function pointers: int (*fp)(int, int)
    if (peek_token().has_value() && peek_token()->value() == "(") {
        consume_token(); // consume '('

        // Expect '*' for function pointer
        if (!peek_token().has_value() || peek_token()->value() != "*") {
            return ParseResult::error("Expected '*' in function pointer declarator", *current_token_);
        }
        consume_token(); // consume '*'

        // Parse CV-qualifiers after the * (if any)
        CVQualifier ptr_cv = CVQualifier::None;
        while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
            std::string_view kw = peek_token()->value();
            if (kw == "const") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) |
                    static_cast<uint8_t>(CVQualifier::Const));
                consume_token();
            } else if (kw == "volatile") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) |
                    static_cast<uint8_t>(CVQualifier::Volatile));
                consume_token();
            } else {
                break;
            }
        }

        // Parse identifier
        if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
            return ParseResult::error("Expected identifier in function pointer declarator", *current_token_);
        }
        Token identifier_token = *peek_token();
        consume_token();

        // Expect closing ')'
        if (!peek_token().has_value() || peek_token()->value() != ")") {
            return ParseResult::error("Expected ')' after function pointer identifier", *current_token_);
        }
        consume_token(); // consume ')'

        // Now parse the function parameters: '(' params ')'
        return parse_postfix_declarator(base_type, identifier_token);
    }

    // Handle pointer prefix: * [const] [volatile] *...
    while (peek_token().has_value() &&
           peek_token()->type() == Token::Type::Operator &&
           peek_token()->value() == "*") {
        consume_token(); // consume '*'

        // Parse CV-qualifiers after the *
        CVQualifier ptr_cv = CVQualifier::None;
        while (peek_token().has_value() &&
               peek_token()->type() == Token::Type::Keyword) {
            std::string_view kw = peek_token()->value();
            if (kw == "const") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) |
                    static_cast<uint8_t>(CVQualifier::Const));
                consume_token();
            } else if (kw == "volatile") {
                ptr_cv = static_cast<CVQualifier>(
                    static_cast<uint8_t>(ptr_cv) |
                    static_cast<uint8_t>(CVQualifier::Volatile));
                consume_token();
            } else {
                break;
            }
        }

        base_type.add_pointer_level(ptr_cv);
    }

    // Parse direct declarator (identifier, function, array)
    Token identifier_token;
    return parse_direct_declarator(base_type, identifier_token, linkage);
}

// NEW: Parse direct declarator (identifier, function, array)
ParseResult Parser::parse_direct_declarator(TypeSpecifierNode& base_type,
                                             Token& out_identifier,
                                             Linkage linkage) {
    // For now, we'll handle the simple case: identifier followed by optional function params
    // TODO: Handle parenthesized declarators like (*fp)(params) for function pointers

    // Parse identifier
    if (!peek_token().has_value() ||
        peek_token()->type() != Token::Type::Identifier) {
        return ParseResult::error("Expected identifier in declarator",
                                 *current_token_);
    }

    out_identifier = *peek_token();
    consume_token();

    // Parse postfix operators (function, array)
    return parse_postfix_declarator(base_type, out_identifier);
}

// NEW: Parse postfix declarators (function, array)
ParseResult Parser::parse_postfix_declarator(TypeSpecifierNode& base_type,
                                              const Token& identifier) {
    // Check for function declarator: '(' params ')'
    if (peek_token().has_value() && peek_token()->value() == "(") {
        consume_token(); // consume '('

        // Parse parameter list
        std::vector<Type> param_types;

        if (!peek_token().has_value() || peek_token()->value() != ")") {
            while (true) {
                // Parse parameter type
                auto param_type_result = parse_type_specifier();
                if (param_type_result.is_error()) {
                    return param_type_result;
                }

                TypeSpecifierNode& param_type =
                    param_type_result.node()->as<TypeSpecifierNode>();
                param_types.push_back(param_type.type());

                // Optional parameter name (we can ignore it for function pointers)
                if (peek_token().has_value() &&
                    peek_token()->type() == Token::Type::Identifier) {
                    consume_token();
                }

                // Check for comma or closing paren
                if (peek_token().has_value() && peek_token()->value() == ",") {
                    consume_token();
                } else {
                    break;
                }
            }
        }

        if (!consume_punctuator(")")) {
            return ParseResult::error("Expected ')' after function parameters",
                                     *current_token_);
        }

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
		// Insert the FunctionDeclarationNode (which contains parameter info for overload resolution)
		// instead of just the DeclarationNode
		if (auto func_node = function_definition_result.node()) {
			if (!gSymbolTable.insert(identifier_token.value(), *func_node)) {
				// Note: With overloading support, insert() now allows multiple functions with same name
				// It only returns false for non-function duplicate symbols
				return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
			}
		}

		// Is only function declaration
		if (consume_punctuator(";")) {
			return ParseResult::success();
		}

		// Add function parameters to the symbol table within a function scope
		gSymbolTable.enter_scope(ScopeType::Function);

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

			// Parse function body
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				gSymbolTable.exit_scope();
				return block_result;
			}

			current_function_ = nullptr;
			gSymbolTable.exit_scope();

			if (auto node = function_definition_result.node()) {
				if (auto block = block_result.node()) {
					node->as<FunctionDeclarationNode>().set_definition(block->as<BlockNode>());
					return ParseResult::success(*node);
				}
			}
		}
	}
	// Attempt to parse a simple declaration (global variable or typedef)
	// Check for initialization
	std::optional<ASTNode> initializer;
	if (peek_token()->value() == "=") {
		consume_token(); // consume '='
		auto init_expr = parse_expression();
		if (init_expr.is_error()) {
			discard_saved_token(saved_position);
			return init_expr;
		}
		initializer = init_expr.node();
	}

	if (!consume_punctuator(";")) {
		discard_saved_token(saved_position);
		return ParseResult::error("Expected ;", *current_token_);
	}

	// Create a global variable declaration node
	// Reuse the existing decl_node from type_and_name_result
	auto global_var_node = emplace_node<VariableDeclarationNode>(
		type_and_name_result.node().value(),  // Use the existing DeclarationNode
		initializer
	);

	// Add to symbol table
	const Token& identifier_token = decl_node.identifier_token();
	if (!gSymbolTable.insert(identifier_token.value(), global_var_node)) {
		discard_saved_token(saved_position);
		return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
	}

	discard_saved_token(saved_position);
	return ParseResult::success(global_var_node);
}

ParseResult Parser::parse_struct_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Check for alignas specifier before struct/class keyword
	std::optional<size_t> custom_alignment = parse_alignas_specifier();

	// Consume 'struct' or 'class' keyword
	auto struct_keyword = consume_token();
	if (!struct_keyword.has_value() ||
	    (struct_keyword->value() != "struct" && struct_keyword->value() != "class")) {
		return ParseResult::error("Expected 'struct' or 'class' keyword",
		                          struct_keyword.value_or(Token()));
	}

	bool is_class = (struct_keyword->value() == "class");

	// Check for alignas specifier after struct/class keyword (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Parse struct name
	auto name_token = consume_token();
	if (!name_token.has_value() || name_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected struct/class name", name_token.value_or(Token()));
	}

	std::string_view struct_name = name_token->value();

	// Register the struct type in the global type system EARLY
	// This allows member functions (like constructors) to reference the struct type
	// We'll fill in the struct info later after parsing all members
	// For nested classes, we register with the qualified name to avoid conflicts
	std::string type_name = std::string(struct_name);
	bool is_nested_class = !struct_parsing_context_stack_.empty();
	if (is_nested_class) {
		// We're inside a struct, so this is a nested class
		// Use the qualified name (e.g., "Outer::Inner") for the TypeInfo entry
		const auto& context = struct_parsing_context_stack_.back();
		type_name = std::string(context.struct_name) + "::" + type_name;
	}

	TypeInfo& struct_type_info = add_struct_type(type_name);

	// For nested classes, also register with the simple name so it can be referenced
	// from within the nested class itself (e.g., in constructors)
	if (is_nested_class) {
		gTypesByName.emplace(std::string(struct_name), &struct_type_info);
	}

	// Check for alignas specifier after struct name (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Create struct declaration node - string_view points directly into source text
	auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name, is_class);

	// Push struct parsing context for nested class support
	struct_parsing_context_stack_.push_back({struct_name, &struct_ref});

	// Create StructTypeInfo early so we can add base classes to it
	auto struct_info = std::make_unique<StructTypeInfo>(std::string(struct_name), struct_ref.default_access());

	// Apply pack alignment from #pragma pack BEFORE adding members
	size_t pack_alignment = context_.getCurrentPackAlignment();
	if (pack_alignment > 0) {
		struct_info->set_pack_alignment(pack_alignment);
	}

	// Parse base class list (if present): : public Base1, private Base2
	if (peek_token().has_value() && peek_token()->value() == ":") {
		consume_token();  // consume ':'

		do {
			// Parse virtual keyword (optional, can appear before or after access specifier)
			bool is_virtual_base = false;
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "virtual") {
				is_virtual_base = true;
				consume_token();
			}

			// Parse access specifier (optional, defaults to public for struct, private for class)
			AccessSpecifier base_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;

			if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view keyword = peek_token()->value();
				if (keyword == "public") {
					base_access = AccessSpecifier::Public;
					consume_token();
				} else if (keyword == "protected") {
					base_access = AccessSpecifier::Protected;
					consume_token();
				} else if (keyword == "private") {
					base_access = AccessSpecifier::Private;
					consume_token();
				}
			}

			// Check for virtual keyword after access specifier (e.g., "public virtual Base")
			if (!is_virtual_base && peek_token().has_value() && peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "virtual") {
				is_virtual_base = true;
				consume_token();
			}

			// Parse base class name
			auto base_name_token = consume_token();
			if (!base_name_token.has_value() || base_name_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected base class name", base_name_token.value_or(Token()));
			}

			std::string_view base_class_name = base_name_token->value();

			// Look up base class type
			auto base_type_it = gTypesByName.find(base_class_name);
			if (base_type_it == gTypesByName.end()) {
				return ParseResult::error("Base class '" + std::string(base_class_name) + "' not found", *base_name_token);
			}

			const TypeInfo* base_type_info = base_type_it->second;
			if (base_type_info->type_ != Type::Struct) {
				return ParseResult::error("Base class '" + std::string(base_class_name) + "' is not a struct/class", *base_name_token);
			}

			// Add base class to struct node and type info
			struct_ref.add_base_class(base_class_name, base_type_info->type_index_, base_access, is_virtual_base);
			struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base_access, is_virtual_base);

		} while (peek_token().has_value() && peek_token()->value() == "," && consume_token());
	}

	// Expect opening brace
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' after struct/class name or base class list", *peek_token());
	}

	// Default access specifier (public for struct, private for class)
	AccessSpecifier current_access = struct_ref.default_access();

	// Parse members
	while (peek_token().has_value() && peek_token()->value() != "}") {
		// Check for access specifier
		if (peek_token()->type() == Token::Type::Keyword) {
			std::string_view keyword = peek_token()->value();
			if (keyword == "public" || keyword == "protected" || keyword == "private") {
				consume_token();
				if (!consume_punctuator(":")) {
					return ParseResult::error("Expected ':' after access specifier", *peek_token());
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

			// Check for 'friend' keyword
			if (keyword == "friend") {
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
						struct_info->addFriendClass(std::string(friend_decl.name()));
					} else if (friend_decl.kind() == FriendKind::Function) {
						struct_info->addFriendFunction(std::string(friend_decl.name()));
					} else if (friend_decl.kind() == FriendKind::MemberFunction) {
						struct_info->addFriendMemberFunction(
							std::string(friend_decl.class_name()),
							std::string(friend_decl.name()));
					}
				}

				continue;  // Skip to next member
			}

			// Check for nested class/struct declaration
			if (keyword == "class" || keyword == "struct") {
				// Parse nested class
				auto nested_result = parse_struct_declaration();
				if (nested_result.is_error()) {
					return nested_result;
				}

				if (auto nested_node = nested_result.node()) {
					// Set enclosing class relationship
					auto& nested_struct = nested_node->as<StructDeclarationNode>();
					nested_struct.set_enclosing_class(&struct_ref);

					// Add to outer class
					struct_ref.add_nested_class(*nested_node);

					// Update type info
					auto nested_type_it = gTypesByName.find(nested_struct.name());
					if (nested_type_it != gTypesByName.end()) {
						const StructTypeInfo* nested_info_const = nested_type_it->second->getStructInfo();
						if (nested_info_const) {
							// Cast away const for adding to nested classes
							StructTypeInfo* nested_info = const_cast<StructTypeInfo*>(nested_info_const);
							struct_info->addNestedClass(nested_info);
						}

						// Now that enclosing class is set, register the qualified name
						std::string qualified_name = nested_struct.qualified_name();
						if (gTypesByName.find(qualified_name) == gTypesByName.end()) {
							gTypesByName.emplace(qualified_name, nested_type_it->second);
						}
					}
				}

				continue;  // Skip to next member
			}
		}

		// Check for constructor (identifier matching struct name followed by '(')
		// Save position BEFORE checking to allow restoration if not a constructor
		TokenPosition saved_pos = save_token_position();
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier &&
		    peek_token()->value() == struct_name) {
			// Look ahead to see if this is a constructor (next token is '(')
			// We need to consume the struct name token and check the next token
			auto name_token_opt = consume_token();
			if (!name_token_opt.has_value()) {
				return ParseResult::error("Expected constructor name", Token());
			}
			Token name_token = name_token_opt.value();  // Copy the token to keep it alive
			std::string_view ctor_name = name_token.value();  // Get the string_view from the token

			if (peek_token().has_value() && peek_token()->value() == "(") {
				// Discard saved position since we're using this as a constructor
				discard_saved_token(saved_pos);
				// This is a constructor
				auto [ctor_node, ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(struct_name, ctor_name);

				// Parse parameters
				if (!consume_punctuator("(")) {
					return ParseResult::error("Expected '(' for constructor parameter list", *peek_token());
				}

				while (!consume_punctuator(")")) {
					// Parse parameter type and name
					ParseResult type_and_name_result = parse_type_and_name();
					if (type_and_name_result.is_error()) {
						return type_and_name_result;
					}

					if (auto node = type_and_name_result.node()) {
						ctor_ref.add_parameter_node(*node);
					}

					// Check if next token is comma (more parameters) or closing paren (done)
					if (!consume_punctuator(",")) {
						// No comma, so we expect a closing paren on the next iteration
						// Don't break here - let the while loop condition consume the ')'
					}
				}

				// Enter a temporary scope for parsing the initializer list
				// This allows the initializer expressions to reference the constructor parameters
				gSymbolTable.enter_scope(ScopeType::Function);

				// Add parameters to symbol table so they can be referenced in the initializer list
				for (const auto& param : ctor_ref.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl_node = param.as<DeclarationNode>();
						const Token& param_token = param_decl_node.identifier_token();
						gSymbolTable.insert(param_token.value(), param);
					}
				}

				// Parse member initializer list if present (: Base(args), member(value), ...)
				if (peek_token().has_value() && peek_token()->value() == ":") {
					consume_token();  // consume ':'

					// Parse initializers until we hit '{' or ';'
					while (peek_token().has_value() &&
					       peek_token()->value() != "{" &&
					       peek_token()->value() != ";") {
						// Parse initializer name (could be base class or member)
						auto init_name_token = consume_token();
						if (!init_name_token.has_value() || init_name_token->type() != Token::Type::Identifier) {
							return ParseResult::error("Expected member or base class name in initializer list", init_name_token.value_or(Token()));
						}

						std::string_view init_name = init_name_token->value();

						// Expect '(' or '{'
						bool is_paren = peek_token().has_value() && peek_token()->value() == "(";
						bool is_brace = peek_token().has_value() && peek_token()->value() == "{";

						if (!is_paren && !is_brace) {
							return ParseResult::error("Expected '(' or '{' after initializer name", *peek_token());
						}

						consume_token();  // consume '(' or '{'
						std::string_view close_delim = is_paren ? ")" : "}";

						// Parse initializer arguments
						std::vector<ASTNode> init_args;
						if (!peek_token().has_value() || peek_token()->value() != close_delim) {
							do {
								ParseResult arg_result = parse_expression();
								if (arg_result.is_error()) {
									return arg_result;
								}
								if (auto arg_node = arg_result.node()) {
									init_args.push_back(*arg_node);
								}
							} while (peek_token().has_value() && peek_token()->value() == "," && consume_token());
						}

						// Expect closing delimiter
						if (!consume_punctuator(close_delim)) {
							return ParseResult::error(std::string("Expected '") + std::string(close_delim) +
							                         "' after initializer arguments", *peek_token());
						}

						// Determine if this is a base class or member initializer
						bool is_base_init = false;
						for (const auto& base : struct_ref.base_classes()) {
							if (base.name == init_name) {
								is_base_init = true;
								ctor_ref.add_base_initializer(std::string(init_name), std::move(init_args));
								break;
							}
						}

						if (!is_base_init) {
							// It's a member initializer
							// For simplicity, we'll use the first argument as the initializer expression
							if (!init_args.empty()) {
								ctor_ref.add_member_initializer(init_name, init_args[0]);
							}
						}

						// Check for comma (more initializers) or '{'/';' (end of initializer list)
						if (!consume_punctuator(",")) {
							// No comma, so we expect '{' or ';' next
							break;
						}
					}
				}

				// Check for = default or = delete
				bool is_defaulted = false;
				bool is_deleted = false;
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
				    peek_token()->value() == "=") {
					consume_token(); // consume '='

					if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
						if (peek_token()->value() == "default") {
							consume_token(); // consume 'default'
							is_defaulted = true;

							// Expect ';'
							if (!consume_punctuator(";")) {
								gSymbolTable.exit_scope();
								return ParseResult::error("Expected ';' after '= default'", *peek_token());
							}

							// Mark as implicit (same behavior as compiler-generated)
							ctor_ref.set_is_implicit(true);

							// Create an empty block for the constructor body
							auto [block_node, block_ref] = create_node_ref(BlockNode());
							ctor_ref.set_definition(block_ref);

							gSymbolTable.exit_scope();
						} else if (peek_token()->value() == "delete") {
							consume_token(); // consume 'delete'
							is_deleted = true;

							// Expect ';'
							if (!consume_punctuator(";")) {
								gSymbolTable.exit_scope();
								return ParseResult::error("Expected ';' after '= delete'", *peek_token());
							}

							// For now, we'll just skip deleted constructors
							// TODO: Track deleted constructors to prevent their use
							gSymbolTable.exit_scope();
							continue; // Don't add deleted constructor to struct
						} else {
							gSymbolTable.exit_scope();
							return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
						}
					} else {
						gSymbolTable.exit_scope();
						return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
					}
				}

				// Parse constructor body if present (and not defaulted/deleted)
				if (!is_defaulted && !is_deleted && peek_token().has_value() && peek_token()->value() == "{") {
					// DELAYED PARSING: Save the current position (start of '{')
					TokenPosition body_start = save_token_position();

					// Look up the struct type
					auto type_it = gTypesByName.find(struct_name);
					size_t struct_type_index = 0;
					if (type_it != gTypesByName.end()) {
						struct_type_index = type_it->second->type_index_;
					}

					// Skip over the constructor body by counting braces
					skip_balanced_braces();

					// Exit the scope we entered for the initializer list
					// We'll re-enter it when we parse the delayed body
					gSymbolTable.exit_scope();

					// Record this for delayed parsing
					delayed_function_bodies_.push_back({
						nullptr,  // func_node (not used for constructors)
						body_start,
						struct_name,
						struct_type_index,
						&struct_ref,
						true,  // is_constructor
						false,  // is_destructor
						&ctor_ref,  // ctor_node
						nullptr   // dtor_node
					});
				} else if (!is_defaulted && !is_deleted && !consume_punctuator(";")) {
					// No constructor body, just exit the scope we entered for the initializer list
					gSymbolTable.exit_scope();
					return ParseResult::error("Expected '{', ';', '= default', or '= delete' after constructor declaration", *peek_token());
				} else if (!is_defaulted && !is_deleted) {
					// Constructor declaration only (no body), exit the scope
					gSymbolTable.exit_scope();
				}

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
		bool is_virtual = false;
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
		    peek_token()->value() == "virtual") {
			is_virtual = true;
			consume_token();  // consume 'virtual'
		}

		// Check for destructor (~StructName followed by '(')
		if (peek_token().has_value() && peek_token()->value() == "~") {
			consume_token();  // consume '~'

			auto name_token_opt = consume_token();
			if (!name_token_opt.has_value() || name_token_opt->type() != Token::Type::Identifier ||
			    name_token_opt->value() != struct_name) {
				return ParseResult::error("Expected struct name after '~' in destructor", name_token_opt.value_or(Token()));
			}
			Token name_token = name_token_opt.value();  // Copy the token to keep it alive
			std::string_view dtor_name = name_token.value();  // Get the string_view from the token

			if (!consume_punctuator("(")) {
				return ParseResult::error("Expected '(' after destructor name", *peek_token());
			}

			if (!consume_punctuator(")")) {
				return ParseResult::error("Destructor cannot have parameters", *peek_token());
			}

			auto [dtor_node, dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(struct_name, dtor_name);

			// Parse override/final specifiers for destructors
			bool is_override = false;
			bool is_final = false;
			while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view keyword = peek_token()->value();
				if (keyword == "override") {
					is_override = true;
					consume_token();
				} else if (keyword == "final") {
					is_final = true;
					consume_token();
				} else {
					break;  // Not a destructor specifier
				}
			}

			// In C++, 'override' or 'final' on destructor implies 'virtual'
			if (is_override || is_final) {
				is_virtual = true;
			}

			// Check for = default or = delete
			bool is_defaulted = false;
			bool is_deleted = false;
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
			    peek_token()->value() == "=") {
				consume_token(); // consume '='

				if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
					if (peek_token()->value() == "default") {
						consume_token(); // consume 'default'
						is_defaulted = true;

						// Expect ';'
						if (!consume_punctuator(";")) {
							return ParseResult::error("Expected ';' after '= default'", *peek_token());
						}

						// Create an empty block for the destructor body
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						dtor_ref.set_definition(block_ref);
					} else if (peek_token()->value() == "delete") {
						consume_token(); // consume 'delete'
						is_deleted = true;

						// Expect ';'
						if (!consume_punctuator(";")) {
							return ParseResult::error("Expected ';' after '= delete'", *peek_token());
						}

						// For now, we'll just skip deleted destructors
						// TODO: Track deleted destructors to prevent their use
						continue; // Don't add deleted destructor to struct
					} else {
						return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
					}
				} else {
					return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
				}
			}

			// Parse destructor body if present (and not defaulted/deleted)
			if (!is_defaulted && !is_deleted && peek_token().has_value() && peek_token()->value() == "{") {
				// DELAYED PARSING: Save the current position (start of '{')
				TokenPosition body_start = save_token_position();

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
					struct_name,
					struct_type_index,
					&struct_ref,
					false,  // is_constructor
					true,   // is_destructor
					nullptr,  // ctor_node
					&dtor_ref   // dtor_node
				});
			} else if (!is_defaulted && !is_deleted && !consume_punctuator(";")) {
				return ParseResult::error("Expected '{', ';', '= default', or '= delete' after destructor declaration", *peek_token());
			}

			// Add destructor to struct (unless deleted)
			if (!is_deleted) {
				struct_ref.add_destructor(dtor_node, current_access, is_virtual);
			}
			continue;
		}

		// Parse member declaration (could be data member or member function)
		// Note: is_virtual was already checked above (line 794)
		auto member_result = parse_type_and_name();
		if (member_result.is_error()) {
			return member_result;
		}

		// Get the member node - we need to check this exists before proceeding
		if (!member_result.node().has_value()) {
			return ParseResult::error("Expected member declaration", *peek_token());
		}

		// Check if this is a member function (has '(') or data member (has ';')
		if (peek_token().has_value() && peek_token()->value() == "(") {
			// This is a member function declaration
			if (!member_result.node()->is<DeclarationNode>()) {
				return ParseResult::error("Expected declaration node for member function", *peek_token());
			}

			DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();

			// Parse function declaration with parameters
			auto func_result = parse_function_declaration(decl_node);
			if (func_result.is_error()) {
				return func_result;
			}

			// Mark this as a member function
			if (!func_result.node().has_value()) {
				return ParseResult::error("Failed to create function declaration node", *peek_token());
			}

			FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();

			// Create a new FunctionDeclarationNode with member function info
			// Pass string_view directly - FunctionDeclarationNode stores it as string_view
			auto [member_func_node, member_func_ref] =
				emplace_node_ref<FunctionDeclarationNode>(decl_node, struct_name);

			// Copy parameters from the parsed function
			for (const auto& param : func_decl.parameter_nodes()) {
				member_func_ref.add_parameter_node(param);
			}

			// Check for override, final, = 0, = default, or = delete after parameters
			bool is_override = false;
			bool is_final = false;
			bool is_pure_virtual = false;
			bool is_defaulted = false;
			bool is_deleted = false;

			// Parse override/final specifiers
			while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view keyword = peek_token()->value();
				if (keyword == "override") {
					is_override = true;
					consume_token();
				} else if (keyword == "final") {
					is_final = true;
					consume_token();
				} else {
					break;  // Not a function specifier
				}
			}

			// Check for = 0 (pure virtual), = default, or = delete
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
			    peek_token()->value() == "=") {
				consume_token(); // consume '='

				if (peek_token().has_value()) {
					if (peek_token()->type() == Token::Type::Literal && peek_token()->value() == "0") {
						// Pure virtual function (= 0)
						consume_token();  // consume '0'
						is_pure_virtual = true;
						if (!is_virtual) {
							return ParseResult::error("Pure virtual function must be declared with 'virtual' keyword", *peek_token());
						}
					} else if (peek_token()->type() == Token::Type::Keyword) {
						if (peek_token()->value() == "default") {
							consume_token(); // consume 'default'
							is_defaulted = true;

							// Expect ';'
							if (!consume_punctuator(";")) {
								return ParseResult::error("Expected ';' after '= default'", *peek_token());
							}

							// Mark as implicit (same behavior as compiler-generated)
							member_func_ref.set_is_implicit(true);

							// Create an empty block for the function body
							auto [block_node, block_ref] = create_node_ref(BlockNode());
							member_func_ref.set_definition(block_ref);
						} else if (peek_token()->value() == "delete") {
							consume_token(); // consume 'delete'
							is_deleted = true;

							// Expect ';'
							if (!consume_punctuator(";")) {
								return ParseResult::error("Expected ';' after '= delete'", *peek_token());
							}

							// For now, we'll just skip deleted functions
							// TODO: Track deleted functions to prevent their use
							continue; // Don't add deleted function to struct
						} else {
							return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
						}
					} else {
						return ParseResult::error("Expected '0', 'default', or 'delete' after '='", *peek_token());
					}
				}
			}

			// Parse function body if present (and not defaulted/deleted)
			if (!is_defaulted && !is_deleted && peek_token().has_value() && peek_token()->value() == "{") {
				// DELAYED PARSING: Save the current position (start of '{')
				TokenPosition body_start = save_token_position();

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
					struct_name,
					struct_type_index,
					&struct_ref,
					false,  // is_constructor
					false,  // is_destructor
					nullptr,  // ctor_node
					nullptr   // dtor_node
				});
				// Inline function body consumed, no semicolon needed
			} else if (!is_defaulted && !is_deleted) {
				// Function declaration without body - expect semicolon
				if (!consume_punctuator(";")) {
					return ParseResult::error("Expected '{', ';', '= default', or '= delete' after member function declaration", *peek_token());
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
				                                 is_virtual, is_pure_virtual, is_override, is_final);
			} else {
				// Add regular member function to struct
				struct_ref.add_member_function(member_func_node, current_access,
				                               is_virtual, is_pure_virtual, is_override, is_final);
			}
		} else {
			// This is a data member
			std::optional<ASTNode> default_initializer;

			// Check for member initialization (C++11 feature)
			if (peek_token().has_value() && peek_token()->value() == "=") {
				consume_token(); // consume '='

				// Parse the initializer expression
				auto init_result = parse_expression();
				if (init_result.is_error()) {
					return init_result;
				}

				// Store the initializer for use in constructor generation
				if (init_result.node().has_value()) {
					default_initializer = *init_result.node();
				}
			}

			// Expect semicolon after member declaration
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after struct member declaration", *peek_token());
			}

			// Add member to struct with current access level and default initializer
			struct_ref.add_member(*member_result.node(), current_access, default_initializer);
		}
	}

	// Expect closing brace
	if (!consume_punctuator("}")) {
		return ParseResult::error("Expected '}' at end of struct/class definition", *peek_token());
	}

	// Expect semicolon after struct definition
	if (!consume_punctuator(";")) {
		return ParseResult::error("Expected ';' after struct/class definition", *peek_token());
	}

	// struct_type_info was already registered early (before parsing members)
	// struct_info was created early (before parsing base classes and members)
	// Now process data members and calculate layout
	for (const auto& member_decl : struct_ref.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

		// Get member size and alignment
		size_t member_size = type_spec.size_in_bits() / 8;
		size_t member_alignment = get_type_alignment(type_spec.type(), member_size);

		// For struct types, get size and alignment from the struct type info
		if (type_spec.type() == Type::Struct) {
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
				member_alignment = member_type_info->getStructInfo()->alignment;
			}
		}

		// Add member to struct layout with default initializer
		struct_info->addMember(
			std::string(decl.identifier_token().value()),
			type_spec.type(),
			type_spec.type_index(),
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer
		);
	}

	// Process member functions, constructors, and destructors
	bool has_user_defined_constructor = false;
	bool has_user_defined_copy_constructor = false;
	bool has_user_defined_move_constructor = false;
	bool has_user_defined_copy_assignment = false;
	bool has_user_defined_move_assignment = false;
	bool has_user_defined_destructor = false;

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
			// Regular member function
			const FunctionDeclarationNode& func = func_decl.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func.decl_node();

			// Add member function to struct type info
			struct_info->addMemberFunction(
				std::string(decl.identifier_token().value()),
				func_decl.function_declaration,
				func_decl.access,
				func_decl.is_virtual,
				func_decl.is_pure_virtual,
				func_decl.is_override,
				func_decl.is_final
			);
		}
	}

	// Generate default constructor if no user-defined constructor exists
	if (!has_user_defined_constructor) {
		// Create a default constructor node
		auto [default_ctor_node, default_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			struct_name,
			struct_name
		);

		// Create an empty block for the constructor body
		auto [block_node, block_ref] = create_node_ref(BlockNode());
		default_ctor_ref.set_definition(block_ref);

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
	if (!has_user_defined_copy_constructor && !has_user_defined_move_constructor) {
		// Create a copy constructor node: Type(const Type& other)
		auto [copy_ctor_node, copy_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			struct_name,
			struct_name
		);

		// Create parameter: const Type& other
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::Const  // const qualifier
		);

		// Make it a reference type
		param_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create parameter declaration
		// Use a static string to ensure the string_view in the token remains valid
		static const std::string param_name = "other";
		Token param_token(Token::Type::Identifier, param_name, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to constructor
		copy_ctor_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the constructor body
		auto [copy_block_node, copy_block_ref] = create_node_ref(BlockNode());
		copy_ctor_ref.set_definition(copy_block_ref);

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
	if (!has_user_defined_copy_assignment && !has_user_defined_move_assignment) {
		// Create a copy assignment operator node: Type& operator=(const Type& other)

		// Create return type: Type& (reference to struct type)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto return_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::None
		);
		return_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create declaration node for operator=
		static const std::string operator_eq_name = "operator=";
		Token operator_name_token(Token::Type::Identifier, operator_eq_name,
		                          name_token->line(), name_token->column(),
		                          name_token->file_index());

		auto operator_decl_node = emplace_node<DeclarationNode>(return_type_node, operator_name_token);

		// Create function declaration node
		auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(
			operator_decl_node.as<DeclarationNode>(), struct_name);

		// Create parameter: const Type& other
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::Const  // const qualifier
		);
		param_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create parameter declaration
		static const std::string param_name_assign = "other";
		Token param_token(Token::Type::Identifier, param_name_assign, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to function
		func_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the operator= body
		auto [op_block_node, op_block_ref] = create_node_ref(BlockNode());
		func_ref.set_definition(op_block_ref);

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
	if (!has_user_defined_copy_constructor && !has_user_defined_copy_assignment &&
	    !has_user_defined_move_assignment && !has_user_defined_destructor) {
		// Create a move constructor node: Type(Type&& other)
		auto [move_ctor_node, move_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			struct_name,
			struct_name
		);

		// Create parameter: Type&& other (rvalue reference)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::None
		);

		// Make it an rvalue reference type
		param_type_node.as<TypeSpecifierNode>().set_reference(true);  // true = rvalue reference

		// Create parameter declaration
		static const std::string param_name_move = "other";
		Token param_token(Token::Type::Identifier, param_name_move, 0, 0, 0);
		auto param_decl_node = emplace_node<DeclarationNode>(param_type_node, param_token);

		// Add parameter to constructor
		move_ctor_ref.add_parameter_node(param_decl_node);

		// Create an empty block for the constructor body
		auto [move_block_node, move_block_ref] = create_node_ref(BlockNode());
		move_ctor_ref.set_definition(move_block_ref);

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
	if (!has_user_defined_copy_constructor && !has_user_defined_copy_assignment &&
	    !has_user_defined_move_constructor && !has_user_defined_destructor) {
		// Create a move assignment operator node: Type& operator=(Type&& other)

		// Create return type: Type& (reference to struct type)
		TypeIndex struct_type_index = struct_type_info.type_index_;
		auto return_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::None
		);
		return_type_node.as<TypeSpecifierNode>().set_reference(false);  // lvalue reference

		// Create declaration node for operator=
		static const std::string move_operator_eq_name = "operator=";
		Token move_operator_name_token(Token::Type::Identifier, move_operator_eq_name,
		                          name_token->line(), name_token->column(),
		                          name_token->file_index());

		auto move_operator_decl_node = emplace_node<DeclarationNode>(return_type_node, move_operator_name_token);

		// Create function declaration node
		auto [move_func_node, move_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
			move_operator_decl_node.as<DeclarationNode>(), struct_name);

		// Create parameter: Type&& other (rvalue reference)
		auto move_param_type_node = emplace_node<TypeSpecifierNode>(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_info->total_size * 8),  // size in bits
			*name_token,
			CVQualifier::None
		);
		move_param_type_node.as<TypeSpecifierNode>().set_reference(true);  // true = rvalue reference

		// Create parameter declaration
		static const std::string param_name_move_assign = "other";
		Token move_param_token(Token::Type::Identifier, param_name_move_assign, 0, 0, 0);
		auto move_param_decl_node = emplace_node<DeclarationNode>(move_param_type_node, move_param_token);

		// Add parameter to function
		move_func_ref.add_parameter_node(move_param_decl_node);

		// Create an empty block for the operator= body
		auto [move_op_block_node, move_op_block_ref] = create_node_ref(BlockNode());
		move_func_ref.set_definition(move_op_block_ref);

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

	// Apply custom alignment if specified
	if (custom_alignment.has_value()) {
		struct_info->set_custom_alignment(custom_alignment.value());
	}

	// Finalize struct layout (add padding)
	// Use finalizeWithBases() if there are base classes, otherwise use finalize()
	if (!struct_info->base_classes.empty()) {
		struct_info->finalizeWithBases();
	} else {
		struct_info->finalize();
	}

	// Store struct info in type info
	struct_type_info.setStructInfo(std::move(struct_info));

	// If this is a nested class, also register it with its qualified name
	if (struct_ref.is_nested()) {
		std::string qualified_name = struct_ref.qualified_name();
		// Register the qualified name as an alias in gTypesByName
		// It points to the same TypeInfo as the simple name
		if (gTypesByName.find(qualified_name) == gTypesByName.end()) {
			gTypesByName.emplace(qualified_name, &struct_type_info);
		}
	}

	// Now parse all delayed inline function bodies
	// At this point, all members are visible in the complete-class context

	// Save the current token position (right after the struct definition)
	// We'll restore this after parsing all delayed bodies
	TokenPosition position_after_struct = save_token_position();

	for (auto& delayed : delayed_function_bodies_) {
		// Restore token position to the start of the function body
		restore_token_position(delayed.body_start);

		if (delayed.is_constructor) {
			// Parse constructor body
			// Re-enter the scope for the constructor (we exited it when we delayed parsing)
			gSymbolTable.enter_scope(ScopeType::Function);

			// Set up member function context
			current_function_ = nullptr;  // Constructors don't have a return type
			member_function_context_stack_.push_back({
				delayed.struct_name,
				delayed.struct_type_index,
				delayed.struct_node
			});

			// Add parameters to symbol table
			for (const auto& param : delayed.ctor_node->parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl = param.as<DeclarationNode>();
					gSymbolTable.insert(param_decl.identifier_token().value(), param);
				}
			}

			// Parse the constructor body
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
				struct_parsing_context_stack_.pop_back();
				return block_result;
			}

			// Attach body to constructor node
			if (auto block = block_result.node()) {
				delayed.ctor_node->set_definition(block->as<BlockNode>());
			}

			// Clean up context
			current_function_ = nullptr;
			member_function_context_stack_.pop_back();
			gSymbolTable.exit_scope();

		} else if (delayed.is_destructor) {
			// Parse destructor body
			gSymbolTable.enter_scope(ScopeType::Function);
			current_function_ = nullptr;

			// Set up member function context
			member_function_context_stack_.push_back({
				delayed.struct_name,
				delayed.struct_type_index,
				delayed.struct_node
			});

			// Parse the destructor body
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
				struct_parsing_context_stack_.pop_back();
				return block_result;
			}

			// Attach body to destructor node
			if (auto block = block_result.node()) {
				delayed.dtor_node->set_definition(block->as<BlockNode>());
			}

			// Clean up context
			current_function_ = nullptr;
			member_function_context_stack_.pop_back();
			gSymbolTable.exit_scope();

		} else {
			// Parse regular member function body
			gSymbolTable.enter_scope(ScopeType::Function);

			// Set current function pointer for __func__, __PRETTY_FUNCTION__
			current_function_ = delayed.func_node;

			// Set up member function context
			member_function_context_stack_.push_back({
				delayed.struct_name,
				delayed.struct_type_index,
				delayed.struct_node
			});

			// Add parameters to symbol table
			for (const auto& param : delayed.func_node->parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl = param.as<DeclarationNode>();
					gSymbolTable.insert(param_decl.identifier_token().value(), param);
				}
			}

			// Parse the function body
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
				struct_parsing_context_stack_.pop_back();
				return block_result;
			}

			// Attach body to function node
			if (auto block = block_result.node()) {
				delayed.func_node->set_definition(block->as<BlockNode>());
			}

			// Clean up context
			current_function_ = nullptr;
			member_function_context_stack_.pop_back();
			gSymbolTable.exit_scope();
		}
	}

	// Clear the delayed bodies list for the next struct
	delayed_function_bodies_.clear();

	// Restore token position to right after the struct definition
	// This ensures the parser continues from the correct position
	restore_token_position(position_after_struct);

	// Pop struct parsing context
	struct_parsing_context_stack_.pop_back();

	return saved_position.success(struct_node);
}

ParseResult Parser::parse_enum_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'enum' keyword
	auto enum_keyword = consume_token();
	if (!enum_keyword.has_value() || enum_keyword->value() != "enum") {
		return ParseResult::error("Expected 'enum' keyword", enum_keyword.value_or(Token()));
	}

	// Check for 'class' or 'struct' keyword (enum class / enum struct)
	bool is_scoped = false;
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
	    (peek_token()->value() == "class" || peek_token()->value() == "struct")) {
		is_scoped = true;
		consume_token(); // consume 'class' or 'struct'
	}

	// Parse enum name
	auto name_token = consume_token();
	if (!name_token.has_value() || name_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected enum name", name_token.value_or(Token()));
	}

	std::string_view enum_name = name_token->value();

	// Register the enum type in the global type system EARLY
	TypeInfo& enum_type_info = add_enum_type(std::string(enum_name));

	// Create enum declaration node
	auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name, is_scoped);

	// Check for underlying type specification (: type)
	if (peek_token().has_value() && peek_token()->value() == ":") {
		consume_token(); // consume ':'

		// Parse the underlying type
		auto underlying_type_result = parse_type_specifier();
		if (underlying_type_result.is_error()) {
			return underlying_type_result;
		}

		if (auto type_node = underlying_type_result.node()) {
			enum_ref.set_underlying_type(*type_node);
		}
	}

	// Expect opening brace
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' after enum name", *peek_token());
	}

	// Create enum type info
	auto enum_info = std::make_unique<EnumTypeInfo>(std::string(enum_name), is_scoped);

	// Determine underlying type (default is int)
	Type underlying_type = Type::Int;
	unsigned char underlying_size = 32;
	if (enum_ref.has_underlying_type()) {
		const auto& type_spec = enum_ref.underlying_type()->as<TypeSpecifierNode>();
		underlying_type = type_spec.type();
		underlying_size = type_spec.size_in_bits();
	}
	enum_info->underlying_type = underlying_type;
	enum_info->underlying_size = underlying_size;

	// Parse enumerators
	long long next_value = 0;
	while (peek_token().has_value() && peek_token()->value() != "}") {
		// Parse enumerator name
		auto enumerator_name_token = consume_token();
		if (!enumerator_name_token.has_value() || enumerator_name_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected enumerator name", enumerator_name_token.value_or(Token()));
		}

		std::string_view enumerator_name = enumerator_name_token->value();
		std::optional<ASTNode> enumerator_value;
		long long value = next_value;

		// Check for explicit value (= expression)
		if (peek_token().has_value() && peek_token()->value() == "=") {
			consume_token(); // consume '='

			// Parse the value expression
			auto value_result = parse_expression();
			if (value_result.is_error()) {
				return value_result;
			}

			if (auto value_node = value_result.node()) {
				enumerator_value = *value_node;

				// Try to evaluate constant expression
				// For now, we only handle numeric literals
				if (value_node->is<ExpressionNode>()) {
					const auto& expr = value_node->as<ExpressionNode>();
					if (std::holds_alternative<NumericLiteralNode>(expr)) {
						const auto& literal = std::get<NumericLiteralNode>(expr);
						const auto& literal_value = literal.value();
						if (std::holds_alternative<unsigned long long>(literal_value)) {
							value = static_cast<long long>(std::get<unsigned long long>(literal_value));
						} else if (std::holds_alternative<double>(literal_value)) {
							value = static_cast<long long>(std::get<double>(literal_value));
						}
					}
				}
			}
		}

		// Create enumerator node
		auto enumerator_node = emplace_node<EnumeratorNode>(*enumerator_name_token, enumerator_value);
		enum_ref.add_enumerator(enumerator_node);

		// Add enumerator to enum type info
		enum_info->addEnumerator(std::string(enumerator_name), value);

		// For unscoped enums, add enumerator to current scope as a constant
		// This allows unscoped enum values to be used without qualification
		if (!is_scoped) {
			auto enum_type_node = emplace_node<TypeSpecifierNode>(
				Type::Enum, enum_type_info.type_index_, underlying_size, *enumerator_name_token);
			auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, *enumerator_name_token);
			gSymbolTable.insert(enumerator_name, enumerator_decl);
		}

		next_value = value + 1;

		// Check for comma or closing brace
		if (peek_token().has_value() && peek_token()->value() == ",") {
			consume_token(); // consume ','
			// Allow trailing comma before '}'
			if (peek_token().has_value() && peek_token()->value() == "}") {
				break;
			}
		} else if (peek_token().has_value() && peek_token()->value() == "}") {
			break;
		} else {
			return ParseResult::error("Expected ',' or '}' after enumerator", *peek_token());
		}
	}

	// Expect closing brace
	if (!consume_punctuator("}")) {
		return ParseResult::error("Expected '}' after enum body", *peek_token());
	}

	// Optional semicolon
	consume_punctuator(";");

	// Store enum info in type info
	enum_type_info.setEnumInfo(std::move(enum_info));

	return saved_position.success(enum_node);
}

ParseResult Parser::parse_typedef_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'typedef' keyword
	auto typedef_keyword = consume_token();
	if (!typedef_keyword.has_value() || typedef_keyword->value() != "typedef") {
		return ParseResult::error("Expected 'typedef' keyword", typedef_keyword.value_or(Token()));
	}

	// Parse the underlying type
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}

	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type specifier after 'typedef'", *current_token_);
	}

	ASTNode type_node = *type_result.node();
	TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();

	// Handle pointer declarators (e.g., typedef int* IntPtr;)
	while (peek_token().has_value() && peek_token()->value() == "*") {
		consume_token(); // consume '*'
		type_spec.add_pointer_level();

		// Skip const/volatile after * for now (TODO: handle properly)
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
			if (peek_token()->value() == "const" || peek_token()->value() == "volatile") {
				consume_token();
			} else {
				break;
			}
		}
	}

	// Parse the alias name (identifier)
	auto alias_token = consume_token();
	if (!alias_token.has_value() || alias_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier after type in typedef", alias_token.value_or(Token()));
	}

	std::string_view alias_name = alias_token->value();

	// Expect semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error("Expected ';' after typedef declaration", *current_token_);
	}

	// Register the typedef alias in the type system
	// The typedef should resolve to the underlying type, not be a new UserDefined type
	// We create a TypeInfo entry that mirrors the underlying type
	auto& alias_type_info = gTypeInfo.emplace_back(std::string(alias_name), type_spec.type(), gTypeInfo.size());
	alias_type_info.type_index_ = type_spec.type_index();
	alias_type_info.type_size_ = type_spec.size_in_bits();
	gTypesByName.emplace(alias_type_info.name_, &alias_type_info);

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
	auto friend_keyword = consume_token();
	if (!friend_keyword.has_value() || friend_keyword->value() != "friend") {
		return ParseResult::error("Expected 'friend' keyword", friend_keyword.value_or(Token()));
	}

	// Check for 'class' keyword (friend class declaration)
	if (peek_token().has_value() && peek_token()->value() == "class") {
		consume_token();  // consume 'class'

		// Parse class name
		auto class_name_token = consume_token();
		if (!class_name_token.has_value() || class_name_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected class name after 'friend class'", *current_token_);
		}

		// Expect semicolon
		if (!consume_punctuator(";")) {
			return ParseResult::error("Expected ';' after friend class declaration", *current_token_);
		}

		auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Class, class_name_token->value());
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

	// Parse function name (may be qualified: ClassName::functionName)
	// We only need to track the last qualifier (the class name) for friend member functions
	std::string_view last_qualifier;
	std::string_view function_name;

	while (peek_token().has_value()) {
		auto name_token = consume_token();
		if (!name_token.has_value() || name_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected function name in friend declaration", *current_token_);
		}

		// Check for :: (qualified name)
		if (peek_token().has_value() && peek_token()->value() == "::") {
			consume_token();  // consume '::'
			last_qualifier = name_token->value();
		} else {
			function_name = name_token->value();
			break;
		}
	}

	// Parse function parameters
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' after friend function name", *current_token_);
	}

	// Parse parameter list (simplified - just skip to closing paren)
	int paren_depth = 1;
	while (paren_depth > 0 && peek_token().has_value()) {
		auto token = consume_token();
		if (token->value() == "(") {
			paren_depth++;
		} else if (token->value() == ")") {
			paren_depth--;
		}
	}

	// Expect semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error("Expected ';' after friend function declaration", *current_token_);
	}

	// Create friend declaration node
	ASTNode friend_node;
	if (last_qualifier.empty()) {
		// Friend function
		friend_node = emplace_node<FriendDeclarationNode>(FriendKind::Function, function_name);
	} else {
		// Friend member function
		friend_node = emplace_node<FriendDeclarationNode>(FriendKind::MemberFunction, function_name, std::string(last_qualifier));
	}

	return saved_position.success(friend_node);
}

ParseResult Parser::parse_namespace() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'namespace' keyword
	if (!consume_keyword("namespace")) {
		return ParseResult::error("Expected 'namespace' keyword", *peek_token());
	}

	// Check if this is an anonymous namespace (namespace { ... })
	std::string_view namespace_name = "";
	bool is_anonymous = false;

	if (peek_token().has_value() && peek_token()->value() == "{") {
		// Anonymous namespace
		is_anonymous = true;
		// For anonymous namespaces, we'll use an empty name
		// The symbol table will handle them specially
		namespace_name = "";
	} else {
		// Named namespace - parse namespace name
		auto name_token = consume_token();
		if (!name_token.has_value() || name_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected namespace name or '{'", name_token.value_or(Token()));
		}
		namespace_name = name_token->value();

		// Check if this is a namespace alias: namespace alias = target;
		if (peek_token().has_value() && peek_token()->value() == "=") {
			// This is a namespace alias, not a namespace declaration
			// Restore position and parse as namespace alias
			Token alias_token = *name_token;
			consume_token(); // consume '='

			// Parse target namespace path
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = consume_token();
				if (!ns_token.has_value() || ns_token->type() != Token::Type::Identifier) {
					return ParseResult::error("Expected namespace name", ns_token.value_or(Token()));
				}
				target_namespace.emplace_back(StringType<>(ns_token->value()));

				// Check for ::
				if (peek_token().has_value() && peek_token()->value() == "::") {
					consume_token(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after namespace alias", *current_token_);
			}

			auto alias_node = emplace_node<NamespaceAliasNode>(alias_token, std::move(target_namespace));
			return saved_position.success(alias_node);
		}
	}

	// Expect opening brace
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' after namespace name", *peek_token());
	}

	// Create namespace declaration node - string_view points directly into source text
	// For anonymous namespaces, use empty string_view
	auto [namespace_node, namespace_ref] = emplace_node_ref<NamespaceDeclarationNode>(is_anonymous ? "" : namespace_name);

	// Enter namespace scope
	// For anonymous namespaces, we still need to enter a scope, but we use a generated unique name
	// This ensures symbols are properly scoped but accessible from the parent scope
	if (is_anonymous) {
		// Generate a unique name for internal use
		static size_t anon_counter = 0;
		std::string anon_name = "__anon_ns_" + std::to_string(anon_counter++);
		// For now, just enter the global scope again (symbols will be global)
		// TODO: Implement proper anonymous namespace semantics
		// Anonymous namespace symbols should be accessible without qualification
		// but have internal linkage
	} else {
		gSymbolTable.enter_namespace(namespace_name);
	}

	// Parse declarations within the namespace
	while (peek_token().has_value() && peek_token()->value() != "}") {
		ParseResult decl_result;

		// Check if it's a nested namespace
		if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "namespace") {
			decl_result = parse_namespace();
		}
		// Check if it's a struct/class declaration
		else if (peek_token()->type() == Token::Type::Keyword &&
			(peek_token()->value() == "class" || peek_token()->value() == "struct")) {
			decl_result = parse_struct_declaration();
		}
		// Check if it's an enum declaration
		else if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "enum") {
			decl_result = parse_enum_declaration();
		}
		// Otherwise, parse as function or variable declaration
		else {
			decl_result = parse_declaration_or_function_definition();
		}

		if (decl_result.is_error()) {
			if (!is_anonymous) {
				gSymbolTable.exit_scope();
			}
			return decl_result;
		}

		if (auto node = decl_result.node()) {
			namespace_ref.add_declaration(*node);
		}
	}

	// Expect closing brace
	if (!consume_punctuator("}")) {
		if (!is_anonymous) {
			gSymbolTable.exit_scope();
		}
		return ParseResult::error("Expected '}' after namespace body", *peek_token());
	}

	// Exit namespace scope (only if not anonymous)
	if (!is_anonymous) {
		gSymbolTable.exit_scope();
	}

	return saved_position.success(namespace_node);
}

ParseResult Parser::parse_using_directive_or_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'using' keyword
	auto using_token_opt = peek_token();
	if (!using_token_opt.has_value() || using_token_opt->value() != "using") {
		return ParseResult::error("Expected 'using' keyword", using_token_opt.value_or(Token()));
	}
	Token using_token = using_token_opt.value();
	consume_token(); // consume 'using'

	// Check if this is a namespace alias: using identifier = namespace::path;
	// We need to look ahead to see if there's an '=' after the first identifier
	TokenPosition lookahead_pos = save_token_position();
	auto first_token = peek_token();
	if (first_token.has_value() && first_token->type() == Token::Type::Identifier) {
		consume_token(); // consume identifier
		auto next_token = peek_token();
		if (next_token.has_value() && next_token->value() == "=") {
			// This is a namespace alias: using alias = target::namespace;
			restore_token_position(lookahead_pos);

			// Parse alias name
			auto alias_token = consume_token();
			if (!alias_token.has_value() || alias_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected alias name after 'using'", *current_token_);
			}

			// Consume '='
			if (!consume_punctuator("=")) {
				return ParseResult::error("Expected '=' after alias name", *current_token_);
			}

			// Parse target namespace path
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = consume_token();
				if (!ns_token.has_value() || ns_token->type() != Token::Type::Identifier) {
					return ParseResult::error("Expected namespace name", ns_token.value_or(Token()));
				}
				target_namespace.emplace_back(StringType<>(ns_token->value()));

				// Check for ::
				if (peek_token().has_value() && peek_token()->value() == "::") {
					consume_token(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after namespace alias", *current_token_);
			}

			auto alias_node = emplace_node<NamespaceAliasNode>(*alias_token, std::move(target_namespace));
			return saved_position.success(alias_node);
		}
	}
	// Not a namespace alias, restore position
	restore_token_position(lookahead_pos);

	// Check if this is "using namespace" directive
	if (peek_token().has_value() && peek_token()->value() == "namespace") {
		consume_token(); // consume 'namespace'

		// Parse namespace path (e.g., std::filesystem)
		std::vector<StringType<>> namespace_path;
		while (true) {
			auto ns_token = consume_token();
			if (!ns_token.has_value() || ns_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected namespace name", ns_token.value_or(Token()));
			}
			namespace_path.emplace_back(StringType<>(ns_token->value()));

			// Check for ::
			if (peek_token().has_value() && peek_token()->value() == "::") {
				consume_token(); // consume ::
			} else {
				break;
			}
		}

		// Expect semicolon
		if (!consume_punctuator(";")) {
			return ParseResult::error("Expected ';' after using directive", *current_token_);
		}

		auto directive_node = emplace_node<UsingDirectiveNode>(std::move(namespace_path), using_token);
		return saved_position.success(directive_node);
	}

	// Otherwise, this is a using declaration: using std::vector;
	std::vector<StringType<>> namespace_path;
	Token identifier_token;

	// Parse qualified name (namespace::...::identifier)
	while (true) {
		auto token = consume_token();
		if (!token.has_value() || token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier in using declaration", token.value_or(Token()));
		}

		// Check if followed by ::
		if (peek_token().has_value() && peek_token()->value() == "::") {
			// This is a namespace part
			namespace_path.emplace_back(StringType<>(token->value()));
			consume_token(); // consume ::
		} else {
			// This is the final identifier
			identifier_token = *token;
			break;
		}
	}

	// Expect semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error("Expected ';' after using declaration", *current_token_);
	}

	auto decl_node = emplace_node<UsingDeclarationNode>(std::move(namespace_path), identifier_token, using_token);
	return saved_position.success(decl_node);
}

ParseResult Parser::parse_type_specifier()
{
	auto current_token_opt = peek_token();

	// Check for decltype FIRST, before any other checks
	if (current_token_opt.has_value() && current_token_opt->value() == "decltype") {
		return parse_decltype_specifier();
	}

	if (!current_token_opt.has_value() ||
		(current_token_opt->type() != Token::Type::Keyword &&
			current_token_opt->type() != Token::Type::Identifier)) {
		return ParseResult::error("Expected type specifier",
			current_token_opt.value_or(Token()));
	}

	size_t long_count = 0;
	TypeQualifier qualifier = TypeQualifier::None;
	CVQualifier cv_qualifier = CVQualifier::None;

	// Parse CV-qualifiers and type qualifiers in any order
	// e.g., "const int", "int const", "const unsigned int", "unsigned const int"
	bool parsing_qualifiers = true;
	while (parsing_qualifiers && current_token_opt.has_value()) {
		std::string_view token_value = current_token_opt->value();
		if (token_value == "const") {
			cv_qualifier = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Const));
			consume_token();
			current_token_opt = peek_token();
		}
		else if (token_value == "volatile") {
			cv_qualifier = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Volatile));
			consume_token();
			current_token_opt = peek_token();
		}
		else if (token_value == "long") {
			long_count++;
			consume_token();
			current_token_opt = peek_token();
		}
		else if (token_value == "signed") {
			qualifier = TypeQualifier::Signed;
			consume_token();
			current_token_opt = peek_token();
		}
		else if (token_value == "unsigned") {
			qualifier = TypeQualifier::Unsigned;
			consume_token();
			current_token_opt = peek_token();
		}
		else {
			parsing_qualifiers = false;
		}
	}

	static const std::unordered_map<std::string_view, std::tuple<Type, size_t>>
		type_map = {
				{"void", {Type::Void, 0}},
				{"bool", {Type::Bool, 1}},
				{"char", {Type::Char, 8}},
				{"short", {Type::Short, 16}},
				{"int", {Type::Int, 32}},
				{"long", {Type::Long, sizeof(long) * 8}},
				{"float", {Type::Float, 32}},
				{"double", {Type::Double, 64}},
				{"auto", {Type::Auto, 0}},
	};

	Type type = Type::UserDefined;
	unsigned char type_size = 0;

	// Check if we have a type keyword, or if we only have qualifiers (e.g., "long", "unsigned")
	bool has_explicit_type = false;
	if (current_token_opt.has_value()) {
		const auto& it = type_map.find(current_token_opt->value());
		if (it != type_map.end()) {
			type = std::get<0>(it->second);
			type_size = static_cast<unsigned char>(std::get<1>(it->second));
			has_explicit_type = true;
		}
	}

	// If we have an explicit type keyword, process it
	if (has_explicit_type) {

		// Apply signed/unsigned qualifier to integer types
		if (qualifier == TypeQualifier::Unsigned) {
			switch (type) {
				case Type::Char:
					type = Type::UnsignedChar;
					type_size = 8;
					break;
				case Type::Short:
					type = Type::UnsignedShort;
					type_size = 16;
					break;
				case Type::Int:
					type = Type::UnsignedInt;
					type_size = 32;
					break;
				case Type::Long:
					type = Type::UnsignedLong;
					type_size = sizeof(unsigned long) * 8;
					break;
				default:
					break;
			}
		} else if (qualifier == TypeQualifier::Signed) {
			// Explicitly signed types keep their current type but ensure correct size
			switch (type) {
				case Type::Char:
					type_size = 8;
					break;
				case Type::Short:
					type_size = 16;
					break;
				case Type::Int:
					type_size = 32;
					break;
				case Type::Long:
					type_size = sizeof(long) * 8;
					break;
				default:
					break;
			}
		}

		if (long_count == 1) {
			if (type == Type::Float) {
				type_size = sizeof(long double);
			}
			else if (type == Type::Long) {
				type = Type::LongLong;
				type_size = 64;
			}
			else if (type == Type::UnsignedLong) {
				type = Type::UnsignedLongLong;
				type_size = 64;
			}
		}

		consume_token();

		// Check for trailing CV-qualifiers (e.g., "int const", "float volatile")
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
			std::string_view next_token = peek_token()->value();
			if (next_token == "const") {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Const));
				consume_token();
			}
			else if (next_token == "volatile") {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Volatile));
				consume_token();
			}
			else {
				break;
			}
		}

		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, current_token_opt.value(), cv_qualifier));
	}
	else if (qualifier != TypeQualifier::None || cv_qualifier != CVQualifier::None || long_count > 0) {
		// Handle cases like "unsigned", "signed", "const", "long" without explicit type (defaults to int)
		// Examples: "unsigned" -> unsigned int, "const" -> const int, "long" -> long int

		if (long_count == 1) {
			// "long" or "const long" -> long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLong : Type::Long;
			type_size = sizeof(long) * 8;
		} else if (long_count == 2) {
			// "long long" or "const long long" -> long long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLongLong : Type::LongLong;
			type_size = 64;
		} else {
			// "unsigned", "signed", or "const" without type -> int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedInt : Type::Int;
			type_size = 32;
		}

		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, Token(), cv_qualifier));
	}
	else if (current_token_opt.has_value() && current_token_opt->type() == Token::Type::Keyword &&
	         (current_token_opt->value() == "struct" || current_token_opt->value() == "class")) {
		// Handle "struct TypeName" or "class TypeName"
		consume_token(); // consume 'struct' or 'class'

		// Get the type name
		current_token_opt = peek_token();
		if (!current_token_opt.has_value() || current_token_opt->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected type name after 'struct' or 'class'",
			                          current_token_opt.value_or(Token()));
		}

		std::string type_name(current_token_opt->value());
		Token type_name_token = *current_token_opt;
		consume_token();

		// Look up the struct type
		auto type_it = gTypesByName.find(type_name);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				type_size = static_cast<unsigned char>(struct_info->total_size * 8);  // Convert bytes to bits
			} else {
				// Struct is being defined but not yet finalized (e.g., in member function parameters)
				// Use a placeholder size of 0 - it will be updated when the struct is finalized
				type_size = 0;
			}
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
		}

		return ParseResult::error("Unknown struct/class type: " + type_name, type_name_token);
	}
	else if (current_token_opt.has_value() && current_token_opt->type() == Token::Type::Identifier) {
		// Handle user-defined type (struct, class, or other user-defined types)
		std::string type_name(current_token_opt->value());
		Token type_name_token = *current_token_opt;  // Save the token before consuming it
		consume_token();

		// Check for qualified name (e.g., Outer::Inner for nested classes)
		while (peek_token().has_value() && peek_token()->value() == "::") {
			consume_token();  // consume '::'

			auto next_token = peek_token();
			if (!next_token.has_value() || next_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after '::'", next_token.value_or(Token()));
			}

			type_name += "::" + std::string(next_token->value());
			consume_token();
		}

		// Check if this is a registered struct type
		auto type_it = gTypesByName.find(type_name);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			// This is a struct type
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				type_size = static_cast<unsigned char>(struct_info->total_size * 8);  // Convert bytes to bits
			} else {
				// Struct is being defined but not yet finalized (e.g., in member function parameters)
				// Use a placeholder size of 0 - it will be updated when the struct is finalized
				type_size = 0;
			}
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
		}

		// Check if this is a registered enum type
		if (type_it != gTypesByName.end() && type_it->second->isEnum()) {
			// This is an enum type
			const TypeInfo* enum_type_info = type_it->second;
			const EnumTypeInfo* enum_info = enum_type_info->getEnumInfo();

			if (enum_info) {
				type_size = enum_info->underlying_size;
			} else {
				// Enum is being defined but not yet finalized
				type_size = 32;  // Default to int size
			}
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::Enum, enum_type_info->type_index_, type_size, type_name_token, cv_qualifier));
		}

		// Otherwise, treat as generic user-defined type or typedef
		// Look up the type_index if it's a registered type
		TypeIndex user_type_index = 0;
		Type resolved_type = Type::UserDefined;
		if (type_it != gTypesByName.end()) {
			user_type_index = type_it->second->type_index_;
			// If this is a typedef (has a stored type and size, but is not a struct/enum), use the underlying type
			bool is_typedef = (type_it->second->type_size_ > 0 && !type_it->second->isStruct() && !type_it->second->isEnum());
			if (is_typedef) {
				resolved_type = type_it->second->type_;
				type_size = type_it->second->type_size_;
			}
		}
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			resolved_type, user_type_index, type_size, type_name_token, cv_qualifier));
	}

	return ParseResult::error("Unexpected token in type specifier",
		current_token_opt.value_or(Token()));
}

ParseResult Parser::parse_decltype_specifier()
{
	// Parse decltype(expr) type specifier
	// Example: decltype(x + y) result = x + y;

	ScopedTokenPosition saved_position(*this);

	// Consume 'decltype' keyword
	auto decltype_token_opt = consume_token();
	if (!decltype_token_opt.has_value()) {
		return ParseResult::error("Expected 'decltype' keyword", *current_token_);
	}
	Token decltype_token = *decltype_token_opt;

	// Expect '('
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' after 'decltype'", *current_token_);
	}

	// Parse the expression
	ParseResult expr_result = parse_expression();
	if (expr_result.is_error()) {
		return expr_result;
	}

	// Expect ')'
	if (!consume_punctuator(")")) {
		return ParseResult::error("Expected ')' after decltype expression", *current_token_);
	}

	// Deduce the type from the expression
	auto type_spec_opt = get_expression_type(*expr_result.node());
	if (!type_spec_opt.has_value()) {
		return ParseResult::error("Could not deduce type from decltype expression", decltype_token);
	}

	// Return the deduced type specifier
	return saved_position.success(emplace_node<TypeSpecifierNode>(*type_spec_opt));
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

	// Set linkage from current context (for extern "C" blocks)
	if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	}

	while (!consume_punctuator(")"sv)) {
		// Check for variadic parameter (...)
		if (peek_token().has_value() && peek_token()->value() == "...") {
			consume_token(); // consume '...'
			// Variadic parameter - just skip it for now
			// The function is marked as variadic, but we don't need to store the ... parameter
			if (!consume_punctuator(")"sv)) {
				return ParseResult::error("Expected ')' after variadic parameter '...'", *current_token_);
			}
			break;
		}

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
			auto default_value = parse_expression();
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

	// Handle nested blocks
	if (current_token.type() == Token::Type::Punctuator && current_token.value() == "{") {
		return parse_block();
	}

	if (current_token.type() == Token::Type::Keyword) {
		// Keyword parsing function map - initialized once on first call
		static const std::unordered_map<std::string_view, ParsingFunction> keyword_parsing_functions = {
			{"if", &Parser::parse_if_statement},
			{"for", &Parser::parse_for_loop},
			{"while", &Parser::parse_while_loop},
			{"do", &Parser::parse_do_while_loop},
			{"switch", &Parser::parse_switch_statement},
			{"return", &Parser::parse_return_statement},
			{"break", &Parser::parse_break_statement},
			{"continue", &Parser::parse_continue_statement},
			{"goto", &Parser::parse_goto_statement},
			{"using", &Parser::parse_using_directive_or_declaration},
			{"namespace", &Parser::parse_namespace},
			{"typedef", &Parser::parse_typedef_declaration},
			{"static", &Parser::parse_variable_declaration},
			{"extern", &Parser::parse_variable_declaration},
			{"register", &Parser::parse_variable_declaration},
			{"mutable", &Parser::parse_variable_declaration},
			{"int", &Parser::parse_variable_declaration},
			{"float", &Parser::parse_variable_declaration},
			{"double", &Parser::parse_variable_declaration},
			{"char", &Parser::parse_variable_declaration},
			{"bool", &Parser::parse_variable_declaration},
			{"void", &Parser::parse_variable_declaration},
			{"short", &Parser::parse_variable_declaration},
			{"long", &Parser::parse_variable_declaration},
			{"signed", &Parser::parse_variable_declaration},
			{"unsigned", &Parser::parse_variable_declaration},
			{"const", &Parser::parse_variable_declaration},
			{"volatile", &Parser::parse_variable_declaration},
			{"alignas", &Parser::parse_variable_declaration},
			{"auto", &Parser::parse_variable_declaration},
			{"decltype", &Parser::parse_variable_declaration},  // C++11 decltype type specifier
			{"new", &Parser::parse_expression_statement},
			{"delete", &Parser::parse_expression_statement},
			{"static_cast", &Parser::parse_expression_statement},
			{"dynamic_cast", &Parser::parse_expression_statement},
			{"const_cast", &Parser::parse_expression_statement},
			{"reinterpret_cast", &Parser::parse_expression_statement},
			{"typeid", &Parser::parse_expression_statement},
		};

		auto keyword_iter = keyword_parsing_functions.find(current_token.value());
		if (keyword_iter != keyword_parsing_functions.end()) {
			// Call the appropriate parsing function
			return (this->*(keyword_iter->second))();
		}

		// Unknown keyword - consume token to avoid infinite loop and return error
		consume_token();
		return ParseResult::error("Unknown keyword: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Identifier) {
		// Check if this is a label (identifier followed by ':')
		// We need to look ahead to see if there's a colon
		TokenPosition saved_pos = save_token_position();
		consume_token(); // consume the identifier
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		    peek_token()->value() == ":") {
			// This is a label statement
			restore_token_position(saved_pos);
			return parse_label_statement();
		}
		// Not a label, restore position and continue
		restore_token_position(saved_pos);

		// Check if this identifier is a registered struct/class/enum/typedef type
		std::string type_name(current_token.value());
		auto type_it = gTypesByName.find(type_name);
		if (type_it != gTypesByName.end()) {
			// Check if it's a struct, enum, or typedef (but not a struct/enum that happens to have type_size_ set)
			bool is_typedef = (type_it->second->type_size_ > 0 && !type_it->second->isStruct() && !type_it->second->isEnum());
			if (type_it->second->isStruct() || type_it->second->isEnum() || is_typedef) {
				// This is a struct/enum/typedef type declaration
				return parse_variable_declaration();
			}
		}

		// If it starts with an identifier, it could be an assignment, expression,
		// or function call statement
		return parse_expression();
	}
	else if (current_token.type() == Token::Type::Operator) {
		// Handle prefix operators as expression statements
		// e.g., ++i; or --i; or *p = 42;
		std::string_view op = current_token.value();
		if (op == "++" || op == "--" || op == "*" || op == "&") {
			return parse_expression();
		}
		// Unknown operator - consume token to avoid infinite loop and return error
		consume_token();
		return ParseResult::error("Unexpected operator: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Punctuator) {
		// Handle lambda expressions and other expression statements starting with punctuators
		std::string_view punct = current_token.value();
		if (punct == "[") {
			// Lambda expression
			return parse_expression();
		}
		else if (punct == "(") {
			// Parenthesized expression
			return parse_expression();
		}
		// Unknown punctuator - consume token to avoid infinite loop and return error
		consume_token();
		return ParseResult::error("Unexpected punctuator: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Literal) {
		// Handle literal expression statements (e.g., "42;")
		return parse_expression();
	}
	else {
		// Unknown token type - consume token to avoid infinite loop and return error
		consume_token();
		return ParseResult::error("Expected a statement or declaration",
			current_token);
	}
}

ParseResult Parser::parse_variable_declaration()
{
	// Check for storage class specifier (static, extern, etc.)
	StorageClass storage_class = StorageClass::None;
	Linkage linkage = Linkage::None;

	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view keyword = peek_token()->value();
		if (keyword == "static") {
			storage_class = StorageClass::Static;
			consume_token();
		} else if (keyword == "extern") {
			storage_class = StorageClass::Extern;
			consume_token();
		} else if (keyword == "register") {
			storage_class = StorageClass::Register;
			consume_token();
		} else if (keyword == "mutable") {
			storage_class = StorageClass::Mutable;
			consume_token();
		}
	}

	// Parse the type specifier and identifier (name)
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		return type_and_name_result;
	}

	// Get the type specifier for potential additional declarations
	DeclarationNode& first_decl = type_and_name_result.node()->as<DeclarationNode>();
	TypeSpecifierNode& type_specifier = first_decl.type_node().as<TypeSpecifierNode>();

	// Helper lambda to create a single variable declaration
	auto create_var_decl = [&](DeclarationNode& decl, std::optional<ASTNode> init_expr) -> ASTNode {

		// Add the variable to the symbol table
		const Token& identifier_token = decl.identifier_token();
		gSymbolTable.insert(identifier_token.value(), emplace_node<DeclarationNode>(decl));

		// Create and return a VariableDeclarationNode with storage class
		ASTNode var_decl_node = emplace_node<VariableDeclarationNode>(
			emplace_node<DeclarationNode>(decl),
			init_expr,
			storage_class
		);

		const VariableDeclarationNode& var_decl = var_decl_node.as<VariableDeclarationNode>();

		return var_decl_node;
	};

	// Process the first declaration
	std::optional<ASTNode> first_init_expr;

	// Check for direct initialization with parentheses: Type var(args)
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "(") {
		// Direct initialization with parentheses
		consume_token(); // consume '('

		// Create an InitializerListNode to hold the arguments
		auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());

		// Parse argument list
		while (true) {
			// Check if we've reached the end
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ")") {
				break;
			}

			// Parse the argument expression
			ParseResult arg_result = parse_expression();
			if (arg_result.is_error()) {
				return arg_result;
			}

			if (auto arg_node = arg_result.node()) {
				init_list_ref.add_initializer(*arg_node);
			}

			// Check for comma (more arguments) or closing paren
			if (!consume_punctuator(",")) {
				// No comma, so we expect a closing paren on the next iteration
				break;
			}
		}

		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after direct initialization arguments", *current_token_);
		}

		first_init_expr = init_list_node;
	}
	else if (peek_token()->type() == Token::Type::Operator && peek_token()->value() == "=") {
		consume_token(); // consume the '=' operator

		// Check if this is a brace initializer (e.g., Point p = {10, 20})
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
			// Parse brace initializer list
			ParseResult init_list_result = parse_brace_initializer(type_specifier);
			if (init_list_result.is_error()) {
				return init_list_result;
			}
			first_init_expr = init_list_result.node();
		} else {
			// Regular expression initializer
			ParseResult init_expr_result = parse_expression();
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}
			first_init_expr = init_expr_result.node();

			// If the type is auto, deduce the type from the initializer
			if (type_specifier.type() == Type::Auto && first_init_expr.has_value()) {
				// Get the full type specifier from the initializer expression
				auto deduced_type_spec_opt = get_expression_type(*first_init_expr);
				if (deduced_type_spec_opt.has_value()) {
					// Use the full deduced type specifier (preserves struct type index, etc.)
					type_specifier = *deduced_type_spec_opt;
				} else {
					// Fallback: deduce basic type
					Type deduced_type = deduce_type_from_expression(*first_init_expr);
					unsigned char deduced_size = get_type_size_bits(deduced_type);
					type_specifier = TypeSpecifierNode(deduced_type, TypeQualifier::None, deduced_size, first_decl.identifier_token(), type_specifier.cv_qualifier());
				}
			}
		}
	}
	else if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
		// Direct list initialization: Type var{args}
		ParseResult init_list_result = parse_brace_initializer(type_specifier);
		if (init_list_result.is_error()) {
			return init_list_result;
		}
		first_init_expr = init_list_result.node();
	}

	// Check if there are more declarations (comma-separated)
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ",") {
		// Create a block to hold multiple declarations
		auto [block_node, block_ref] = create_node_ref(BlockNode());

		// Add the first declaration to the block
		block_ref.add_statement_node(create_var_decl(first_decl, first_init_expr));

		// Parse additional declarations
		while (consume_punctuator(",")) {
			// Parse the identifier (name) - reuse the same type
			auto identifier_token = consume_token();
			if (!identifier_token || identifier_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after comma in declaration list", *identifier_token);
			}

			// Create a new DeclarationNode with the same type
			DeclarationNode& new_decl = emplace_node<DeclarationNode>(
				emplace_node<TypeSpecifierNode>(type_specifier),
				*identifier_token
			).as<DeclarationNode>();

			// Check for initialization
			std::optional<ASTNode> init_expr;
			if (peek_token()->type() == Token::Type::Operator && peek_token()->value() == "=") {
				consume_token(); // consume the '=' operator

				// Check if this is a brace initializer
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
					ParseResult init_list_result = parse_brace_initializer(type_specifier);
					if (init_list_result.is_error()) {
						return init_list_result;
					}
					init_expr = init_list_result.node();
				} else {
					ParseResult init_expr_result = parse_expression();
					if (init_expr_result.is_error()) {
						return init_expr_result;
					}
					init_expr = init_expr_result.node();
				}
			}

			// Add this declaration to the block
			block_ref.add_statement_node(create_var_decl(new_decl, init_expr));
		}

		// Return the block containing all declarations
		return ParseResult::success(block_node);
	}
	else {
		// Single declaration - return it directly
		return ParseResult::success(create_var_decl(first_decl, first_init_expr));
	}
}

ParseResult Parser::parse_brace_initializer(const TypeSpecifierNode& type_specifier)
{
	// Parse brace initializer list: { expr1, expr2, ... }
	// This is used for struct initialization like: Point p = {10, 20};

	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' for brace initializer", *current_token_);
	}

	// Create an InitializerListNode to hold the initializer expressions
	auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());

	// Get the struct type information for validation
	if (type_specifier.type() != Type::Struct) {
		return ParseResult::error("Brace initializers are currently only supported for struct types", *current_token_);
	}

	TypeIndex type_index = type_specifier.type_index();
	if (type_index >= gTypeInfo.size()) {
		return ParseResult::error("Invalid struct type index", *current_token_);
	}

	const TypeInfo& type_info = gTypeInfo[type_index];
	if (!type_info.struct_info_) {
		return ParseResult::error("Type is not a struct", *current_token_);
	}

	const StructTypeInfo& struct_info = *type_info.struct_info_;

	// Parse comma-separated initializer expressions (positional or designated)
	size_t member_index = 0;
	bool has_designated = false;  // Track if we've seen any designated initializers
	std::unordered_set<std::string_view> used_members;  // Track which members have been initialized

	while (true) {
		// Check if we've reached the end of the initializer list
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "}") {
			break;
		}

		// Check for designated initializer syntax: .member = value
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ".") {
			has_designated = true;
			consume_token();  // consume '.'

			// Parse member name
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected member name after '.' in designated initializer", *current_token_);
			}
			std::string_view member_name = peek_token()->value();
			consume_token();

			// Validate member name exists in struct
			bool member_found = false;
			for (const auto& member : struct_info.members) {
				if (member.name == member_name) {
					member_found = true;
					break;
				}
			}
			if (!member_found) {
				return ParseResult::error("Unknown member '" + std::string(member_name) + "' in designated initializer", *current_token_);
			}

			// Check for duplicate member initialization
			if (used_members.count(member_name)) {
				return ParseResult::error("Member '" + std::string(member_name) + "' already initialized", *current_token_);
			}
			used_members.insert(member_name);

			// Expect '='
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator || peek_token()->value() != "=") {
				return ParseResult::error("Expected '=' after member name in designated initializer", *current_token_);
			}
			consume_token();  // consume '='

			// Parse the initializer expression
			ParseResult init_expr_result = parse_expression();
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}

			// Add the designated initializer to the list
			if (init_expr_result.node().has_value()) {
				init_list_ref.add_designated_initializer(std::string(member_name), *init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", *current_token_);
			}
		} else {
			// Positional initializer
			if (has_designated) {
				return ParseResult::error("Positional initializers cannot follow designated initializers", *current_token_);
			}

			// Check if we have too many initializers
			if (member_index >= struct_info.members.size()) {
				return ParseResult::error("Too many initializers for struct", *current_token_);
			}

			// Parse the initializer expression
			ParseResult init_expr_result = parse_expression();
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}

			// Add the initializer to the list
			if (init_expr_result.node().has_value()) {
				init_list_ref.add_initializer(*init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", *current_token_);
			}

			member_index++;
		}

		// Check for comma or end of list
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ",") {
			consume_token(); // consume the comma

			// Allow trailing comma before '}'
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "}") {
				break;
			}
		} else {
			// No comma, so we should be at the end
			break;
		}
	}

	if (!consume_punctuator("}")) {
		return ParseResult::error("Expected '}' to close brace initializer", *current_token_);
	}

	// Check if we have too few initializers
	if (member_index < struct_info.members.size()) {
		// This is allowed in C++ - remaining members are zero-initialized
		// For now, we'll just accept it
	}

	return ParseResult::success(init_list_node);
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
	Token return_token = current_token_opt.value();
	consume_token(); // Consume the 'return' keyword

	// Parse the return expression (if any)
	ParseResult return_expr_result;
	auto next_token_opt = peek_token();
	if (!next_token_opt.has_value() ||
		(next_token_opt.value().type() != Token::Type::Punctuator ||
			next_token_opt.value().value() != ";")) {
		return_expr_result = parse_expression();
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
			emplace_node<ReturnStatementNode>(return_expr_result.node(), return_token));
	}
	else {
		return ParseResult::success(emplace_node<ReturnStatementNode>(std::nullopt, return_token));
	}
}

ParseResult Parser::parse_unary_expression()
{
	// Check for 'static_cast' keyword
	if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "static_cast") {
		Token cast_token = *current_token_;
		consume_token(); // consume 'static_cast'

		// Expect '<'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator ||
		    peek_token()->value() != "<") {
			return ParseResult::error("Expected '<' after 'static_cast'", *current_token_);
		}
		consume_token(); // consume '<'

		// Parse the target type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected type in static_cast", *current_token_);
		}

		// Parse pointer declarators: * [const] [volatile] *...
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
		       peek_token()->value() == "*") {
			consume_token(); // consume '*'

			// Check for CV-qualifiers after the *
			CVQualifier ptr_cv = CVQualifier::None;
			while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view kw = peek_token()->value();
				if (kw == "const") {
					ptr_cv = static_cast<CVQualifier>(
						static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Const));
					consume_token();
				} else if (kw == "volatile") {
					ptr_cv = static_cast<CVQualifier>(
						static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Volatile));
					consume_token();
				} else {
					break;
				}
			}

			type_spec.add_pointer_level(ptr_cv);
		}

		// Expect '>'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator ||
		    peek_token()->value() != ">") {
			return ParseResult::error("Expected '>' after type in static_cast", *current_token_);
		}
		consume_token(); // consume '>'

		// Expect '('
		if (!consume_punctuator("(")) {
			return ParseResult::error("Expected '(' after static_cast<Type>", *current_token_);
		}

		// Parse the expression to cast
		ParseResult expr_result = parse_expression();
		if (expr_result.is_error() || !expr_result.node().has_value()) {
			return ParseResult::error("Expected expression in static_cast", *current_token_);
		}

		// Expect ')'
		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after static_cast expression", *current_token_);
		}

		auto cast_expr = emplace_node<ExpressionNode>(
			StaticCastNode(*type_result.node(), *expr_result.node(), cast_token));
		return ParseResult::success(cast_expr);
	}

	// Check for 'dynamic_cast' keyword
	if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "dynamic_cast") {
		Token cast_token = *current_token_;
		consume_token(); // consume 'dynamic_cast'

		// Expect '<'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator ||
		    peek_token()->value() != "<") {
			return ParseResult::error("Expected '<' after 'dynamic_cast'", *current_token_);
		}
		consume_token(); // consume '<'

		// Parse the target type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected type in dynamic_cast", *current_token_);
		}

		// Parse pointer declarators: * [const] [volatile] *...
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
		       peek_token()->value() == "*") {
			consume_token(); // consume '*'

			// Check for CV-qualifiers after the *
			CVQualifier ptr_cv = CVQualifier::None;
			while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view kw = peek_token()->value();
				if (kw == "const") {
					ptr_cv = static_cast<CVQualifier>(
						static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Const));
					consume_token();
				} else if (kw == "volatile") {
					ptr_cv = static_cast<CVQualifier>(
						static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Volatile));
					consume_token();
				} else {
					break;
				}
			}

			type_spec.add_pointer_level(ptr_cv);
		}

		// Expect '>'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator ||
		    peek_token()->value() != ">") {
			return ParseResult::error("Expected '>' after type in dynamic_cast", *current_token_);
		}
		consume_token(); // consume '>'

		// Expect '('
		if (!consume_punctuator("(")) {
			return ParseResult::error("Expected '(' after dynamic_cast<Type>", *current_token_);
		}

		// Parse the expression to cast
		ParseResult expr_result = parse_expression();
		if (expr_result.is_error() || !expr_result.node().has_value()) {
			return ParseResult::error("Expected expression in dynamic_cast", *current_token_);
		}

		// Expect ')'
		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after dynamic_cast expression", *current_token_);
		}

		auto cast_expr = emplace_node<ExpressionNode>(
			DynamicCastNode(*type_result.node(), *expr_result.node(), cast_token));
		return ParseResult::success(cast_expr);
	}

	// Check for 'new' keyword
	if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "new") {
		consume_token(); // consume 'new'

		// Check for placement new: new (address) Type
		std::optional<ASTNode> placement_address;
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		    peek_token()->value() == "(") {
			// This could be placement new or constructor call
			// We need to look ahead to distinguish:
			// - new (expr) Type      -> placement new
			// - new Type(args)       -> constructor call
			//
			// Strategy: Try to parse as placement new first
			// If we see ") Type", it's placement new
			// Otherwise, backtrack and parse as constructor call later

			ScopedTokenPosition saved_position(*this);
			consume_token(); // consume '('

			// Try to parse placement address expression
			ParseResult placement_result = parse_expression();
			if (!placement_result.is_error() &&
			    peek_token().has_value() && peek_token()->value() == ")") {
				consume_token(); // consume ')'

				// Check if next token looks like a type (not end of expression)
				if (peek_token().has_value() &&
				    (peek_token()->type() == Token::Type::Keyword ||
				     peek_token()->type() == Token::Type::Identifier)) {
					// This is placement new - commit the parse
					placement_address = placement_result.node();
					saved_position.success();  // Discard saved position

					// Emit warning if <new> header was not included
					if (!context_.hasIncludedHeader("new")) {
						std::cerr << "Warning: placement new used without '#include <new>'. "
						          << "This is a compiler extension. "
						          << "Standard C++ requires: void* operator new(std::size_t, void*);\n";
					}
				}
				// If not a type, the destructor will restore the position
			}
			// If failed to parse, the destructor will restore the position
		}

		// Parse the type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		auto type_node = type_result.node();
		if (!type_node.has_value()) {
			return ParseResult::error("Expected type after 'new'", *current_token_);
		}

		// Check for array allocation: new Type[size]
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		    peek_token()->value() == "[") {
			consume_token(); // consume '['

			// Parse the size expression
			ParseResult size_result = parse_expression();
			if (size_result.is_error()) {
				return size_result;
			}

			if (!consume_punctuator("]")) {
				return ParseResult::error("Expected ']' after array size", *current_token_);
			}

			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, true, size_result.node(), {}, placement_address));
			return ParseResult::success(new_expr);
		}
		// Check for constructor call: new Type(args)
		else if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		         peek_token()->value() == "(") {
			consume_token(); // consume '('

			ChunkedVector<ASTNode, 128, 256> args;

			// Parse constructor arguments
			if (!peek_token().has_value() || peek_token()->value() != ")") {
				while (true) {
					ParseResult arg_result = parse_expression();
					if (arg_result.is_error()) {
						return arg_result;
					}

					if (auto arg_node = arg_result.node()) {
						args.push_back(*arg_node);
					}

					if (peek_token().has_value() && peek_token()->value() == ",") {
						consume_token(); // consume ','
					} else {
						break;
					}
				}
			}

			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after constructor arguments", *current_token_);
			}

			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, false, std::nullopt, std::move(args), placement_address));
			return ParseResult::success(new_expr);
		}
		// Simple new: new Type
		else {
			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, false, std::nullopt, {}, placement_address));
			return ParseResult::success(new_expr);
		}
	}

	// Check for 'delete' keyword
	if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "delete") {
		consume_token(); // consume 'delete'

		// Check for array delete: delete[]
		bool is_array = false;
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		    peek_token()->value() == "[") {
			consume_token(); // consume '['
			if (!consume_punctuator("]")) {
				return ParseResult::error("Expected ']' after 'delete['", *current_token_);
			}
			is_array = true;
		}

		// Parse the expression to delete
		ParseResult expr_result = parse_unary_expression();
		if (expr_result.is_error()) {
			return expr_result;
		}

		if (auto expr_node = expr_result.node()) {
			auto delete_expr = emplace_node<ExpressionNode>(
				DeleteExpressionNode(*expr_node, is_array));
			return ParseResult::success(delete_expr);
		}
	}

	// Check if the current token is a unary operator
	if (current_token_->type() == Token::Type::Operator) {
		std::string_view op = current_token_->value();

		// Check for unary operators: !, ~, +, -, ++, --, * (dereference), & (address-of)
		if (op == "!" || op == "~" || op == "+" || op == "-" || op == "++" || op == "--" ||
		    op == "*" || op == "&") {
			Token operator_token = *current_token_;
			consume_token();

			// Parse the operand (recursively handle unary expressions)
			ParseResult operand_result = parse_unary_expression();
			if (operand_result.is_error()) {
				return operand_result;
			}

			if (auto operand_node = operand_result.node()) {
				// Special handling for unary + on lambda: decay to function pointer
				if (op == "+" && operand_node->is<LambdaExpressionNode>()) {
					const auto& lambda = operand_node->as<LambdaExpressionNode>();

					// Only captureless lambdas can decay to function pointers
					if (!lambda.captures().empty()) {
						return ParseResult::error("Cannot convert lambda with captures to function pointer", operator_token);
					}

					// For now, just return the lambda itself
					// The code generator will handle the conversion to function pointer
					// TODO: Create a proper function pointer type node
					return ParseResult::success(*operand_node);
				}

				auto unary_op = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *operand_node, true));
				return ParseResult::success(unary_op);
			}

			// If operand_node is empty, return error
			return ParseResult::error("Expected operand after unary operator", operator_token);
		}
	}

	// Not a unary operator, parse as primary expression
	return parse_primary_expression();
}

ParseResult Parser::parse_expression(int precedence)
{
	ParseResult result = parse_unary_expression();
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
				// Create the binary operation and update the result
				auto binary_op = emplace_node<ExpressionNode>(
					BinaryOperatorNode(operator_token, *leftNode, *rightNode));
				result = ParseResult::success(binary_op);
			}
		}
	}

	return result;
}

std::optional<TypedNumeric> get_numeric_literal_type(std::string_view text)
{
	// Convert the text to lowercase for case-insensitive parsing
	std::string lowerText(text);
	std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);

	TypedNumeric typeInfo;
	char* end_ptr = nullptr;

	// Check if this is a floating-point literal (contains '.', 'e', or 'E', or has 'f'/'l' suffix)
	bool has_decimal_point = lowerText.find('.') != std::string::npos;
	bool has_exponent = lowerText.find('e') != std::string::npos;
	bool has_float_suffix = lowerText.find('f') != std::string::npos;
	bool is_floating_point = has_decimal_point || has_exponent || has_float_suffix;

	if (is_floating_point) {
		// Parse as floating-point literal
		double float_value = std::strtod(lowerText.c_str(), &end_ptr);
		typeInfo.value = float_value;

		// Check suffix to determine float vs double
		std::string_view suffix = end_ptr;

		// Branchless suffix detection using bit manipulation
		// Check for 'f' or 'F' suffix
		bool is_float = (suffix.find('f') != std::string_view::npos);
		// Check for 'l' or 'L' suffix (long double)
		bool is_long_double = (suffix.find('l') != std::string_view::npos) && !is_float;

		// Branchless type selection
		// If is_float: Type::Float (12), else if is_long_double: Type::LongDouble (14), else Type::Double (13)
		typeInfo.type = static_cast<Type>(
			static_cast<int>(Type::Float) * is_float +
			static_cast<int>(Type::LongDouble) * is_long_double * (!is_float) +
			static_cast<int>(Type::Double) * (!is_float) * (!is_long_double)
		);

		// Branchless size selection: float=32, double=64, long double=80
		typeInfo.sizeInBits = static_cast<unsigned char>(
			32 * is_float +
			80 * is_long_double * (!is_float) +
			64 * (!is_float) * (!is_long_double)
		);

		typeInfo.typeQualifier = TypeQualifier::None;
		return typeInfo;
	}

	// Integer literal parsing
	if (lowerText.find("0x") == 0) {
		// Hexadecimal literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 4.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), &end_ptr, 16);
	}
	else if (lowerText.find("0b") == 0) {
		// Binary literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 1.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.substr(2).c_str(), &end_ptr, 2);
	}
	else if (lowerText.find("0") == 0 && lowerText.length() > 1 && lowerText[1] != '.') {
		// Octal literal (but not "0." which is a float)
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 1) * 3.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.substr(1).c_str(), &end_ptr, 8);
	}
	else {
		// Decimal integer literal
		typeInfo.sizeInBits = static_cast<unsigned char>(sizeof(int) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str(), &end_ptr, 10);
	}

	// Check for integer suffixes
	static constexpr std::string_view suffixCharacters = "ul";
	std::string_view suffix = end_ptr;
	if (!suffix.empty() && suffix.find_first_not_of(suffixCharacters) == std::string_view::npos) {
		bool hasUnsigned = suffix.find('u') != std::string_view::npos;
		typeInfo.typeQualifier = hasUnsigned ? TypeQualifier::Unsigned : TypeQualifier::Signed;
		typeInfo.type = hasUnsigned ? Type::UnsignedInt : Type::Int;

		// Count the number of 'l' characters
		auto l_count = std::count(suffix.begin(), suffix.end(), 'l');
		if (l_count > 0) {
			typeInfo.sizeInBits = sizeof(long) * static_cast<size_t>(8 + (l_count & 2) * 8);
		}
	} else {
		// Default for literals without suffix: signed int
		typeInfo.typeQualifier = TypeQualifier::Signed;
		typeInfo.type = Type::Int;
	}

	return typeInfo;
}


int Parser::get_operator_precedence(const std::string_view& op)
{
	// C++ operator precedence (higher number = higher precedence)
	static const std::unordered_map<std::string_view, int> precedence_map = {
			// Multiplicative (precedence 16)
			{"*", 16},  {"/", 16},  {"%", 16},
			// Additive (precedence 15)
			{"+", 15},  {"-", 15},
			// Shift (precedence 14)
			{"<<", 14}, {">>", 14},
			// Relational (precedence 13)
			{"<", 13},  {"<=", 13}, {">", 13},  {">=", 13},
			// Equality (precedence 12)
			{"==", 12}, {"!=", 12},
			// Bitwise AND (precedence 11)
			{"&", 11},
			// Bitwise XOR (precedence 10)
			{"^", 10},
			// Bitwise OR (precedence 9)
			{"|", 9},
			// Logical AND (precedence 8)
			{"&&", 8},
			// Logical OR (precedence 7)
			{"||", 7},
			// Assignment operators (precedence 3, right-associative, lowest precedence)
			{"=", 3}, {"+=", 3}, {"-=", 3}, {"*=", 3}, {"/=", 3},
			{"%=", 3}, {"&=", 3}, {"|=", 3}, {"^=", 3},
			{"<<=", 3}, {">>=", 3},
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
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == value) {
		consume_token(); // consume keyword
		return true;
	}
	return false;
}

bool Parser::consume_punctuator(const std::string_view& value)
{
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
		peek_token()->value() == value) {
		consume_token(); // consume punctuator
		return true;
	}
	return false;
}

std::optional<size_t> Parser::parse_alignas_specifier()
{
	// Parse: alignas(constant-expression)
	// For now, we only support integer literals

	// Check if next token is alignas keyword
	if (!peek_token().has_value() ||
	    peek_token()->type() != Token::Type::Keyword ||
	    peek_token()->value() != "alignas") {
		return std::nullopt;
	}

	// Save position in case parsing fails
	TokenPosition saved_pos = save_token_position();

	consume_token(); // consume "alignas"

	if (!consume_punctuator("(")) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Parse the alignment value (must be a constant expression, we support literals for now)
	auto token = peek_token();
	if (!token.has_value() || token->type() != Token::Type::Literal) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Parse the numeric literal
	std::string_view value_str = token->value();
	size_t alignment = 0;

	// Try to parse as integer
	auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
	if (result.ec != std::errc()) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	consume_token(); // consume the literal

	if (!consume_punctuator(")")) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Validate alignment (must be power of 2)
	if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Success - discard saved position
	discard_saved_token(saved_pos);
	return alignment;
}

ParseResult Parser::parse_primary_expression()
{
	std::optional<ASTNode> result;

	// Check for lambda expression first (starts with '[')
	if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == "[") {
		ParseResult lambda_result = parse_lambda_expression();
		if (lambda_result.is_error()) {
			return lambda_result;
		}
		result = lambda_result.node();
		// Don't return here - continue to postfix operator handling
		// This allows immediately invoked lambdas: []() { ... }()
	}
	// Check for offsetof builtin first (before general identifier handling)
	else if (current_token_->type() == Token::Type::Identifier && current_token_->value() == "offsetof") {
		// Handle offsetof builtin: offsetof(struct_type, member)
		Token offsetof_token = *current_token_;
		consume_token(); // consume 'offsetof'

		if (!consume_punctuator("(")) {
			return ParseResult::error("Expected '(' after 'offsetof'", *current_token_);
		}

		// Parse the struct type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected struct type in offsetof", *current_token_);
		}

		if (!consume_punctuator(",")) {
			return ParseResult::error("Expected ',' after struct type in offsetof", *current_token_);
		}

		// Parse the member name
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected member name in offsetof", *current_token_);
		}
		Token member_name = *peek_token();
		consume_token(); // consume member name

		if (!consume_punctuator(")")) {
			return ParseResult::error("Expected ')' after offsetof arguments", *current_token_);
		}

		result = emplace_node<ExpressionNode>(
			OffsetofExprNode(*type_result.node(), member_name, offsetof_token));
	}
	else if (current_token_->type() == Token::Type::Identifier) {
		Token idenfifier_token = *current_token_;

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
			consume_token();

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Check if this is a qualified identifier (namespace::identifier)
		// Helper to get DeclarationNode from either DeclarationNode or FunctionDeclarationNode
		auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
			if (node.is<DeclarationNode>()) {
				return &node.as<DeclarationNode>();
			} else if (node.is<FunctionDeclarationNode>()) {
				return &node.as<FunctionDeclarationNode>().decl_node();
			}
			return nullptr;
		};

		// We need to consume the identifier first to check what comes after it
		consume_token();
		if (current_token_.has_value() && current_token_->value() == "::"sv) {
			// Build the qualified identifier manually
			std::vector<StringType<32>> namespaces;
			Token final_identifier = idenfifier_token;

			// Collect namespace parts
			while (current_token_.has_value() && current_token_->value() == "::"sv) {
				// Current identifier is a namespace part
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				consume_token(); // consume ::

				// Get next identifier
				if (!current_token_.has_value() || current_token_->type() != Token::Type::Identifier) {
					return ParseResult::error("Expected identifier after '::'", current_token_.value_or(Token()));
				}
				final_identifier = *current_token_;
				consume_token(); // consume the identifier to check for the next ::
			}

			// current_token_ is now the token after the final identifier

			// Create a QualifiedIdentifierNode
			auto qualified_node = emplace_node<QualifiedIdentifierNode>(namespaces, final_identifier);
			const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

			// Try to look up the qualified identifier
			auto identifierType = gSymbolTable.lookup_qualified(qual_id.namespaces(), qual_id.name());

			// Check if followed by '(' for function call
			if (current_token_.has_value() && current_token_->value() == "(") {
				consume_token(); // consume '('

				// If not found, create a forward declaration
				if (!identifierType) {
					auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
					auto forward_decl = emplace_node<DeclarationNode>(type_node, qual_id.identifier_token());
					identifierType = forward_decl;
				}

				// Parse function arguments
				ChunkedVector<ASTNode> args;
				if (!peek_token().has_value() || peek_token()->value() != ")"sv) {
					while (true) {
						auto arg_result = parse_expression();
						if (arg_result.is_error()) {
							return arg_result;
						}
						if (auto arg = arg_result.node()) {
							args.push_back(*arg);
						}

						if (!peek_token().has_value()) {
							return ParseResult::error("Expected ',' or ')' in function call", *current_token_);
						}

						if (peek_token()->value() == ")") {
							break;
						}

						if (!consume_punctuator(",")) {
							return ParseResult::error("Expected ',' between function arguments", *current_token_);
						}
					}
				}

				if (!consume_punctuator(")")) {
					return ParseResult::error("Expected ')' after function call arguments", *current_token_);
				}

				// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
				const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration", qual_id.identifier_token());
				}

				// Create function call node with the qualified identifier
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), qual_id.identifier_token()));
			} else {
				// Just a qualified identifier reference
				result = emplace_node<ExpressionNode>(qual_id);
			}

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Get the identifier's type information from the symbol table
		auto identifierType = gSymbolTable.lookup(idenfifier_token.value());

		if (!identifierType) {
			// If we're inside a member function, check if this is a member variable
			if (!member_function_context_stack_.empty()) {
				const auto& context = member_function_context_stack_.back();
				const StructDeclarationNode* struct_node = context.struct_node;

				// Check if this identifier matches any data member in the struct (including inherited members)
				if (struct_node) {
					// First check direct members
					for (const auto& member_decl : struct_node->members()) {
						const ASTNode& member_node = member_decl.declaration;
						if (member_node.is<DeclarationNode>()) {
							const DeclarationNode& decl = member_node.as<DeclarationNode>();
							if (decl.identifier_token().value() == idenfifier_token.value()) {
								// This is a member variable! Transform it into this->member
								// Create a "this" token with the correct value
								Token this_token(Token::Type::Keyword, "this",
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Identifier already consumed at line 1621
								return ParseResult::success(*result);
							}
						}
					}

					// Also check base class members
					for (const auto& base : struct_node->base_classes()) {
						// Look up the base class type
						auto base_type_it = gTypesByName.find(base.name);
						if (base_type_it != gTypesByName.end()) {
							const TypeInfo* base_type_info = base_type_it->second;
							const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();

							if (base_struct_info) {
								// Check if the identifier is a member of the base class (recursively)
								const StructMember* member = base_struct_info->findMemberRecursive(std::string(idenfifier_token.value()));
								if (member) {
									// This is an inherited member variable! Transform it into this->member
									Token this_token(Token::Type::Keyword, "this",
									                 idenfifier_token.line(), idenfifier_token.column(),
									                 idenfifier_token.file_index());
									auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

									// Create member access node: this->member
									result = emplace_node<ExpressionNode>(
										MemberAccessNode(this_ident, idenfifier_token));

									// Identifier already consumed at line 1621
									return ParseResult::success(*result);
								}
							}
						}
					}
				}
			}

			// Check if this is a function call (forward reference)
			// Identifier already consumed at line 1621
			if (consume_punctuator("("sv)) {
				// Create a forward declaration for the function
				// We'll assume it returns int for now (this is a simplification)
				auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
				auto forward_decl = emplace_node<DeclarationNode>(type_node, idenfifier_token);

				// Add to symbol table as a forward declaration
				gSymbolTable.insert(idenfifier_token.value(), forward_decl);
				identifierType = forward_decl;

				if (!peek_token().has_value())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")") {
					ParseResult argResult = parse_expression();
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

				// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
				const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration", idenfifier_token);
				}

				result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
			}
			else {
				// Not a function call, but identifier not found - this is an error
				std::cerr << "DEBUG: Missing identifier: " << idenfifier_token.value() << "\n";
				return ParseResult::error("Missing identifier", idenfifier_token);
			}
		}
		else if (!identifierType->is<DeclarationNode>() && !identifierType->is<FunctionDeclarationNode>() && !identifierType->is<VariableDeclarationNode>()) {
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, *current_token_);
		}
		else {
			// Identifier already consumed at line 1621

			// Check if this looks like a function call
			// Only consume '(' if the identifier is actually a function OR a function pointer OR has operator()
			bool is_function_decl = identifierType->is<FunctionDeclarationNode>();
			bool is_function_pointer = false;
			bool has_operator_call = false;
			if (identifierType->is<DeclarationNode>()) {
				const auto& decl = identifierType->as<DeclarationNode>();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
				is_function_pointer = type_node.is_function_pointer();

				// Check if this is a struct with operator()
				if (type_node.type() == Type::Struct) {
					TypeIndex type_index = type_node.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_) {
							// Check if struct has operator()
							for (const auto& member_func : type_info.struct_info_->member_functions) {
								if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
									has_operator_call = true;
									break;
								}
							}
						}
					}
				}
			}
			bool is_function_call = peek_token().has_value() && peek_token()->value() == "(" &&
			                        (is_function_decl || is_function_pointer || has_operator_call);

			if (is_function_call && consume_punctuator("("sv)) {
				if (!peek_token().has_value())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")"sv) {
					ParseResult argResult = parse_expression();
					if (argResult.is_error()) {
						return argResult;
					}

					if (auto node = argResult.node()) {
						args.push_back(*node);
					}

					if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == ","sv) {
						consume_token(); // Consume comma
					}
					else if (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")"sv) {
						return ParseResult::error("Expected ',' or ')' after function argument", *current_token_);
					}

					if (!peek_token().has_value())
						return ParseResult::error(ParserError::NotImplemented, Token());
				}

				if (!consume_punctuator(")")) {
					return ParseResult::error("Expected ')' after function call arguments", *current_token_);
				}

				// For operator() calls, create a member function call
				if (has_operator_call) {
					// Create a member function call: object.operator()(args)
					auto object_expr = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));

					// Find the operator() function declaration in the struct
					const auto& decl = identifierType->as<DeclarationNode>();
					const auto& type_node = decl.type_node().as<TypeSpecifierNode>();
					TypeIndex type_index = type_node.type_index();
					const TypeInfo& type_info = gTypeInfo[type_index];

					// Find operator() in member functions
					FunctionDeclarationNode* operator_call_func = nullptr;
					for (const auto& member_func : type_info.struct_info_->member_functions) {
						if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
							operator_call_func = &const_cast<FunctionDeclarationNode&>(member_func.function_decl.as<FunctionDeclarationNode>());
							break;
						}
					}

					if (!operator_call_func) {
						return ParseResult::error("operator() not found in struct", idenfifier_token);
					}

					Token operator_token(Token::Type::Identifier, "operator()", idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					result = emplace_node<ExpressionNode>(MemberFunctionCallNode(object_expr, *operator_call_func, std::move(args), operator_token));
				}
				// For function pointers, skip overload resolution and create FunctionCallNode directly
				else if (is_function_pointer) {
					const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
					if (!decl_ptr) {
						return ParseResult::error("Invalid function pointer declaration", idenfifier_token);
					}
					result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
				}
				else {
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
							result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
							break;
						}
						arg_types.push_back(*arg_type);
					}

					// If we successfully extracted all argument types, perform overload resolution
					if (!result.has_value() && arg_types.size() == args.size()) {
						auto resolution_result = resolve_overload(all_overloads, arg_types);

						if (!resolution_result.has_match) {
							return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
						}

						if (resolution_result.is_ambiguous) {
							return ParseResult::error("Ambiguous call to overloaded function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
						}

						// Get the selected overload
						const DeclarationNode* decl_ptr = getDeclarationNode(*resolution_result.selected_overload);
						if (!decl_ptr) {
							return ParseResult::error("Invalid function declaration", idenfifier_token);
						}

						result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
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
	else if (current_token_->type() == Token::Type::Literal) {
		auto literal_type = get_numeric_literal_type(current_token_->value());
		if (!literal_type) {
			return ParseResult::error("Expected numeric literal", *current_token_);
		}
		result = emplace_node<ExpressionNode>(NumericLiteralNode(*current_token_, literal_type->value, literal_type->type, literal_type->typeQualifier, literal_type->sizeInBits));
		consume_token();
	}
	else if (current_token_->type() == Token::Type::StringLiteral) {
		// Handle adjacent string literal concatenation
		// C++ allows "Hello " "World" to be concatenated into "Hello World"
		Token first_string = *current_token_;
		std::string concatenated_value(first_string.value());
		consume_token();

		// Check for adjacent string literals
		while (peek_token().has_value() && peek_token()->type() == Token::Type::StringLiteral) {
			Token next_string = *peek_token();
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
			consume_token();
		}

		// Store the concatenated string in CompileContext so it persists
		std::string_view persistent_string = context_.storeFunctionNameLiteral(concatenated_value);
		Token concatenated_token(Token::Type::StringLiteral,
		                         persistent_string,
		                         first_string.line(),
		                         first_string.column(),
		                         first_string.file_index());

		result = emplace_node<ExpressionNode>(StringLiteralNode(concatenated_token));
	}
	else if (current_token_->type() == Token::Type::CharacterLiteral) {
		// Parse character literal and convert to numeric value
		std::string_view value = current_token_->value();

		// Character literal format: 'x' or '\x'
		// Remove the surrounding quotes
		if (value.size() < 3) {
			return ParseResult::error("Invalid character literal", *current_token_);
		}

		char char_value = 0;
		if (value[1] == '\\') {
			// Escape sequence
			if (value.size() < 4) {
				return ParseResult::error("Invalid escape sequence in character literal", *current_token_);
			}
			char escape_char = value[2];
			switch (escape_char) {
				case 'n': char_value = '\n'; break;
				case 't': char_value = '\t'; break;
				case 'r': char_value = '\r'; break;
				case '0': char_value = '\0'; break;
				case '\\': char_value = '\\'; break;
				case '\'': char_value = '\''; break;
				case '"': char_value = '"'; break;
				default:
					return ParseResult::error("Unknown escape sequence in character literal", *current_token_);
			}
		}
		else {
			// Single character
			char_value = value[1];
		}

		// Create a numeric literal node with the character's value
		result = emplace_node<ExpressionNode>(NumericLiteralNode(*current_token_,
			static_cast<unsigned long long>(static_cast<unsigned char>(char_value)),
			Type::Char, TypeQualifier::None, 8));
		consume_token();
	}
	else if (current_token_->type() == Token::Type::Keyword &&
			 (current_token_->value() == "true"sv || current_token_->value() == "false"sv)) {
		// Handle bool literals
		bool value = (current_token_->value() == "true");
		result = emplace_node<ExpressionNode>(NumericLiteralNode(*current_token_,
			static_cast<unsigned long long>(value), Type::Bool, TypeQualifier::None, 1));
		consume_token();
	}
	else if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "this"sv) {
		// Handle 'this' keyword - represents a pointer to the current object
		// Only valid inside member functions
		if (member_function_context_stack_.empty()) {
			return ParseResult::error("'this' can only be used inside a member function", *current_token_);
		}

		Token this_token = *current_token_;
		consume_token();

		// Create an identifier node for 'this'
		result = emplace_node<ExpressionNode>(IdentifierNode(this_token));
	}
	else if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "sizeof"sv) {
		// Handle sizeof operator: sizeof(type) or sizeof(expression)
		Token sizeof_token = *current_token_;
		consume_token(); // consume 'sizeof'

		if (!consume_punctuator("("sv)) {
			return ParseResult::error("Expected '(' after 'sizeof'", *current_token_);
		}

		// Try to parse as a type first
		TokenPosition saved_pos = save_token_position();
		ParseResult type_result = parse_type_specifier();

		if (!type_result.is_error() && type_result.node().has_value()) {
			// Successfully parsed as type
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after sizeof type", *current_token_);
			}
			discard_saved_token(saved_pos);
			result = emplace_node<ExpressionNode>(SizeofExprNode(*type_result.node(), sizeof_token));
		}
		else {
			// Not a type, try parsing as expression
			restore_token_position(saved_pos);
			ParseResult expr_result = parse_expression();
			if (expr_result.is_error()) {
				return ParseResult::error("Expected type or expression after 'sizeof('", *current_token_);
			}
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after sizeof expression", *current_token_);
			}
			result = emplace_node<ExpressionNode>(
				SizeofExprNode::from_expression(*expr_result.node(), sizeof_token));
		}
	}
	else if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "typeid"sv) {
		// Handle typeid operator: typeid(type) or typeid(expression)
		Token typeid_token = *current_token_;
		consume_token(); // consume 'typeid'

		if (!consume_punctuator("("sv)) {
			return ParseResult::error("Expected '(' after 'typeid'", *current_token_);
		}

		// Try to parse as a type first
		TokenPosition saved_pos = save_token_position();
		ParseResult type_result = parse_type_specifier();

		if (!type_result.is_error() && type_result.node().has_value()) {
			// Successfully parsed as type
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after typeid type", *current_token_);
			}
			discard_saved_token(saved_pos);
			result = emplace_node<ExpressionNode>(TypeidNode(*type_result.node(), true, typeid_token));
		}
		else {
			// Not a type, try parsing as expression
			restore_token_position(saved_pos);
			ParseResult expr_result = parse_expression();
			if (expr_result.is_error()) {
				return ParseResult::error("Expected type or expression after 'typeid('", *current_token_);
			}
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after typeid expression", *current_token_);
			}
			result = emplace_node<ExpressionNode>(TypeidNode(*expr_result.node(), false, typeid_token));
		}
	}
	else if (consume_punctuator("(")) {
		// Could be either:
		// 1. C-style cast: (Type)expression
		// 2. Parenthesized expression: (expression)
		// Try to parse as type first
		TokenPosition saved_pos = save_token_position();
		ParseResult type_result = parse_type_specifier();

		if (!type_result.is_error() && type_result.node().has_value()) {
			// Successfully parsed as type, check if followed by ')'
			if (consume_punctuator(")")) {
				// This is a C-style cast: (Type)expression
				// Save the cast token for error reporting
				Token cast_token = Token(Token::Type::Punctuator, "cast",
				                        current_token_->line(), current_token_->column(),
				                        current_token_->file_index());

				// Parse the expression to cast
				ParseResult expr_result = parse_unary_expression();
				if (expr_result.is_error() || !expr_result.node().has_value()) {
					return ParseResult::error("Expected expression after C-style cast", *current_token_);
				}

				discard_saved_token(saved_pos);
				// Create a StaticCastNode (C-style casts behave like static_cast in most cases)
				auto cast_expr = emplace_node<ExpressionNode>(
					StaticCastNode(*type_result.node(), *expr_result.node(), cast_token));
				result = cast_expr;
			} else {
				// Not a cast, restore and parse as parenthesized expression
				restore_token_position(saved_pos);
				ParseResult paren_result = parse_expression();
				if (paren_result.is_error()) {
					return paren_result;
				}
				if (!consume_punctuator(")")) {
					return ParseResult::error("Expected ')' after parenthesized expression",
						*current_token_);
				}
				result = paren_result.node();
			}
		} else {
			// Not a type, parse as parenthesized expression
			restore_token_position(saved_pos);
			ParseResult paren_result = parse_expression();
			if (paren_result.is_error()) {
				return paren_result;
			}
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after parenthesized expression",
					*current_token_);
			}
			result = paren_result.node();
		}
	}
	else {
		return ParseResult::error("Expected primary expression", *current_token_);
	}

	// Check for postfix operators (++, --, and array subscript [])
	while (result.has_value() && peek_token().has_value()) {
		if (peek_token()->type() == Token::Type::Operator) {
			std::string_view op = peek_token()->value();
			if (op == "++" || op == "--") {
				Token operator_token = *current_token_;
				consume_token(); // consume the postfix operator

				// Create a postfix unary operator node (is_prefix = false)
				result = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *result, false));
				continue;  // Check for more postfix operators
			}
		}

		// Check for function call operator () - for operator() overload
		if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "(") {
			// This could be operator() call on an object
			// We need to check if the result is an object type (not a function)

			Token paren_token = *peek_token();
			consume_token(); // consume '('

			// Parse function arguments
			ChunkedVector<ASTNode> args;
			if (!peek_token().has_value() || peek_token()->value() != ")") {
				while (true) {
					auto arg_result = parse_expression();
					if (arg_result.is_error()) {
						return arg_result;
					}
					if (auto arg = arg_result.node()) {
						args.push_back(*arg);
					}

					if (!peek_token().has_value()) {
						return ParseResult::error("Expected ',' or ')' in function call", *current_token_);
					}

					if (peek_token()->value() == ")") {
						break;
					}

					if (!consume_punctuator(",")) {
						return ParseResult::error("Expected ',' between function arguments", *current_token_);
					}
				}
			}

			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after function call arguments", *current_token_);
			}

			// Create operator() call as a member function call
			// The member function name is "operator()"
			static const std::string operator_call_name = "operator()";
			Token operator_token(Token::Type::Identifier, operator_call_name,
			                     paren_token.line(), paren_token.column(), paren_token.file_index());

			// Create a temporary function declaration for operator()
			// This will be resolved during code generation
			auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, operator_token);
			auto temp_decl = emplace_node<DeclarationNode>(temp_type, operator_token);
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());

			// Create member function call node for operator()
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, func_ref, std::move(args), operator_token));
			continue;
		}

		// Check for array subscript operator []
		if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "[") {
			Token bracket_token = *peek_token();
			consume_token(); // consume '['

			// Parse the index expression
			ParseResult index_result = parse_expression();
			if (index_result.is_error()) {
				return index_result;
			}

			// Expect closing ']'
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Punctuator ||
			    peek_token()->value() != "]") {
				return ParseResult::error("Expected ']' after array index", *current_token_);
			}
			consume_token(); // consume ']'

			// Create array subscript node
			if (auto index_node = index_result.node()) {
				result = emplace_node<ExpressionNode>(
					ArraySubscriptNode(*result, *index_node, bracket_token));
				continue;  // Check for more postfix operators (e.g., arr[i][j])
			} else {
				return ParseResult::error("Invalid array index expression", bracket_token);
			}
		}

		// Check for member access operator . or ->
		if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "."sv) {
			consume_token(); // consume '.'
		} else if (peek_token()->type() == Token::Type::Operator && peek_token()->value() == "->"sv) {
			Token arrow_token = *peek_token();
			consume_token(); // consume '->'
			// Transform ptr->member to (*ptr).member
			// Create a dereference node
			Token deref_token(Token::Type::Operator, "*", arrow_token.line(), arrow_token.column(), arrow_token.file_index());
			result = emplace_node<ExpressionNode>(
				UnaryOperatorNode(deref_token, *result, true));
		} else {
			break;  // No more postfix operators
		}

		// Expect an identifier (member name)
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected member name after '.' or '->'", *current_token_);
		}

		Token member_name_token = *peek_token();
		consume_token(); // consume member name

		// Check if this is a member function call (followed by '(')
		if (peek_token().has_value() && peek_token()->value() == "("sv) {
			// This is a member function call: obj.method(args)
			// We need to find the member function in the struct type info

			// For now, create a placeholder - we'll need to look up the function from struct type info
			// The actual lookup will happen during code generation when we have type information

			consume_token(); // consume '('

			// Parse function arguments
			ChunkedVector<ASTNode> args;
			if (!peek_token().has_value() || peek_token()->value() != ")"sv) {
				while (true) {
					auto arg_result = parse_expression();
					if (arg_result.is_error()) {
						return arg_result;
					}
					if (auto arg = arg_result.node()) {
						args.push_back(*arg);
					}

					if (!peek_token().has_value()) {
						return ParseResult::error("Expected ',' or ')' in function call", *current_token_);
					}

					if (peek_token()->value() == ")") {
						break;
					}

					if (!consume_punctuator(",")) {
						return ParseResult::error("Expected ',' between function arguments", *current_token_);
					}
				}
			}

			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after function call arguments", *current_token_);
			}

			// Create a temporary function declaration node for the member function
			// We'll resolve the actual function during code generation
			auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, member_name_token);
			auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_name_token);
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());

			// Create member function call node
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, func_ref, std::move(args), member_name_token));
			continue;
		}

		// Regular member access (not a function call)
		result = emplace_node<ExpressionNode>(
			MemberAccessNode(*result, member_name_token));
		continue;  // Check for more postfix operators (e.g., obj.member1.member2)
	}

	if (result.has_value())
		return ParseResult::success(*result);

	return ParseResult::success();
}

ParseResult Parser::parse_for_loop() {
    if (!consume_keyword("for"sv)) {
        return ParseResult::error("Expected 'for' keyword", *current_token_);
    }

    if (!consume_punctuator("("sv)) {
        return ParseResult::error("Expected '(' after 'for'", *current_token_);
    }

    // Parse initialization (optional: can be empty, declaration, or expression)
    std::optional<ASTNode> init_statement;

    // Check if init is empty (starts with semicolon)
    if (!consume_punctuator(";"sv)) {
        // Not empty, parse init statement
        if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
            // Check if it's a type keyword or CV-qualifier (variable declaration)
            if (type_keywords.find(peek_token()->value()) != type_keywords.end()) {
                // Handle variable declaration
                ParseResult init = parse_variable_declaration();
                if (init.is_error()) {
                    return init;
                }
                init_statement = init.node();
            } else {
                // Not a type keyword, try parsing as expression
                ParseResult init = parse_expression();
                if (init.is_error()) {
                    return init;
                }
                init_statement = init.node();
            }
        } else {
            // Handle expression
            ParseResult init = parse_expression();
            if (init.is_error()) {
                return init;
            }
            init_statement = init.node();
        }

        // Check for ranged-for syntax: for (declaration : range_expression)
        if (consume_punctuator(":"sv)) {
            // This is a ranged for loop
            if (!init_statement.has_value()) {
                return ParseResult::error("Ranged for loop requires a loop variable declaration", *current_token_);
            }

            // Parse the range expression
            ParseResult range_result = parse_expression();
            if (range_result.is_error()) {
                return range_result;
            }

            auto range_expr = range_result.node();
            if (!range_expr.has_value()) {
                return ParseResult::error("Expected range expression in ranged for loop", *current_token_);
            }

            if (!consume_punctuator(")"sv)) {
                return ParseResult::error("Expected ')' after ranged for loop range expression", *current_token_);
            }

            // Parse body (can be a block or a single statement)
            ParseResult body_result;
            if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
                body_result = parse_block();
            } else {
                body_result = parse_statement_or_declaration();
            }

            if (body_result.is_error()) {
                return body_result;
            }

            auto body_node = body_result.node();
            if (!body_node.has_value()) {
                return ParseResult::error("Invalid ranged for loop body", *current_token_);
            }

            return ParseResult::success(emplace_node<RangedForStatementNode>(
                *init_statement, *range_expr, *body_node
            ));
        }

        if (!consume_punctuator(";"sv)) {
            return ParseResult::error("Expected ';' after for loop initialization", *current_token_);
        }
    }

    // Parse condition (optional: can be empty, defaults to true)
    std::optional<ASTNode> condition;

    // Check if condition is empty (next token is semicolon)
    if (!consume_punctuator(";"sv)) {
        // Not empty, parse condition expression
        ParseResult cond_result = parse_expression();
        if (cond_result.is_error()) {
            return cond_result;
        }
        condition = cond_result.node();

        if (!consume_punctuator(";"sv)) {
            return ParseResult::error("Expected ';' after for loop condition", *current_token_);
        }
    }

    // Parse increment/update expression (optional: can be empty)
    std::optional<ASTNode> update_expression;

    // Check if increment is empty (next token is closing paren)
    if (!consume_punctuator(")"sv)) {
        // Not empty, parse increment expression
        ParseResult inc_result = parse_expression();
        if (inc_result.is_error()) {
            return inc_result;
        }
        update_expression = inc_result.node();

        if (!consume_punctuator(")")) {
            return ParseResult::error("Expected ')' after for loop increment", *current_token_);
        }
    }

    // Parse body (can be a block or a single statement)
    ParseResult body_result;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
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
        return ParseResult::error("Invalid for loop body", *current_token_);
    }

    return ParseResult::success(emplace_node<ForStatementNode>(
        init_statement, condition, update_expression, *body_node
    ));
}

ParseResult Parser::parse_while_loop() {
    if (!consume_keyword("while"sv)) {
        return ParseResult::error("Expected 'while' keyword", *current_token_);
    }

    if (!consume_punctuator("("sv)) {
        return ParseResult::error("Expected '(' after 'while'", *current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression();
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume_punctuator(")"sv)) {
        return ParseResult::error("Expected ')' after while condition", *current_token_);
    }

    // Parse body (can be a block or a single statement)
    ParseResult body_result;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{"sv) {
        body_result = parse_block();
    } else {
        body_result = parse_statement_or_declaration();
    }

    if (body_result.is_error()) {
        return body_result;
    }

    // Create while statement node
    auto condition_node = condition_result.node();
    auto body_node = body_result.node();
    if (!condition_node.has_value() || !body_node.has_value()) {
        return ParseResult::error("Invalid while loop construction", *current_token_);
    }

    return ParseResult::success(emplace_node<WhileStatementNode>(
        *condition_node, *body_node
    ));
}

ParseResult Parser::parse_do_while_loop() {
    if (!consume_keyword("do"sv)) {
        return ParseResult::error("Expected 'do' keyword", *current_token_);
    }

    // Parse body (can be a block or a single statement)
    ParseResult body_result;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
        body_result = parse_block();
    } else {
        body_result = parse_statement_or_declaration();
    }

    if (body_result.is_error()) {
        return body_result;
    }

    if (!consume_keyword("while"sv)) {
        return ParseResult::error("Expected 'while' after do-while body", *current_token_);
    }

    if (!consume_punctuator("("sv)) {
        return ParseResult::error("Expected '(' after 'while'", *current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression();
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume_punctuator(")"sv)) {
        return ParseResult::error("Expected ')' after do-while condition", *current_token_);
    }

    if (!consume_punctuator(";"sv)) {
        return ParseResult::error("Expected ';' after do-while statement", *current_token_);
    }

    // Create do-while statement node
    auto body_node = body_result.node();
    auto condition_node = condition_result.node();
    if (!body_node.has_value() || !condition_node.has_value()) {
        return ParseResult::error("Invalid do-while loop construction", *current_token_);
    }

    return ParseResult::success(emplace_node<DoWhileStatementNode>(
        *body_node, *condition_node
    ));
}

ParseResult Parser::parse_break_statement() {
    auto break_token_opt = peek_token();
    if (!break_token_opt.has_value() || break_token_opt->value() != "break"sv) {
        return ParseResult::error("Expected 'break' keyword", *current_token_);
    }

    Token break_token = break_token_opt.value();
    consume_token(); // Consume the 'break' keyword

    if (!consume_punctuator(";"sv)) {
        return ParseResult::error("Expected ';' after break statement", *current_token_);
    }

    return ParseResult::success(emplace_node<BreakStatementNode>(break_token));
}

ParseResult Parser::parse_continue_statement() {
    auto continue_token_opt = peek_token();
    if (!continue_token_opt.has_value() || continue_token_opt->value() != "continue"sv) {
        return ParseResult::error("Expected 'continue' keyword", *current_token_);
    }

    Token continue_token = continue_token_opt.value();
    consume_token(); // Consume the 'continue' keyword

    if (!consume_punctuator(";"sv)) {
        return ParseResult::error("Expected ';' after continue statement", *current_token_);
    }

    return ParseResult::success(emplace_node<ContinueStatementNode>(continue_token));
}

ParseResult Parser::parse_goto_statement() {
    auto goto_token_opt = peek_token();
    if (!goto_token_opt.has_value() || goto_token_opt->value() != "goto"sv) {
        return ParseResult::error("Expected 'goto' keyword", *current_token_);
    }

    Token goto_token = goto_token_opt.value();
    consume_token(); // Consume the 'goto' keyword

    // Parse the label identifier
    auto label_token_opt = peek_token();
    if (!label_token_opt.has_value() || label_token_opt->type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier after 'goto'", *current_token_);
    }

    Token label_token = label_token_opt.value();
    consume_token(); // Consume the label identifier

    if (!consume_punctuator(";"sv)) {
        return ParseResult::error("Expected ';' after goto statement", *current_token_);
    }

    return ParseResult::success(emplace_node<GotoStatementNode>(label_token, goto_token));
}

ParseResult Parser::parse_label_statement() {
    // This is called when we've detected identifier followed by ':'
    // The identifier token should be the current token
    auto label_token_opt = peek_token();
    if (!label_token_opt.has_value() || label_token_opt->type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier", *current_token_);
    }

    Token label_token = label_token_opt.value();
    consume_token(); // Consume the label identifier

    if (!consume_punctuator(":"sv)) {
        return ParseResult::error("Expected ':' after label", *current_token_);
    }

    return ParseResult::success(emplace_node<LabelStatementNode>(label_token));
}

ParseResult Parser::parse_lambda_expression() {
    // Expect '['
    if (!consume_punctuator("[")) {
        return ParseResult::error("Expected '[' to start lambda expression", *current_token_);
    }

    Token lambda_token = *current_token_;

    // Parse captures
    std::vector<LambdaCaptureNode> captures;

    // Check for empty capture list
    if (!peek_token().has_value() || peek_token()->value() != "]") {
        // Parse capture list
        while (true) {
            auto token = peek_token();
            if (!token.has_value()) {
                return ParseResult::error("Unexpected end of file in lambda capture list", *current_token_);
            }

            // Check for capture-all
            if (token->value() == "=") {
                consume_token();
                captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByValue));
            } else if (token->value() == "&") {
                consume_token();
                // Check if this is capture-all by reference or a specific reference capture
                auto next_token = peek_token();
                if (next_token.has_value() && next_token->type() == Token::Type::Identifier) {
                    // Specific reference capture: [&x]
                    Token id_token = *next_token;
                    consume_token();
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByReference, id_token));
                } else {
                    // Capture-all by reference: [&]
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByReference));
                }
            } else if (token->type() == Token::Type::Identifier) {
                // Capture by value: [x]
                Token id_token = *token;
                consume_token();
                captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByValue, id_token));
            } else {
                return ParseResult::error("Expected capture specifier in lambda", *token);
            }

            // Check for comma (more captures) or closing bracket
            if (peek_token().has_value() && peek_token()->value() == ",") {
                consume_token(); // consume comma
            } else {
                break;
            }
        }
    }

    // Expect ']'
    if (!consume_punctuator("]")) {
        return ParseResult::error("Expected ']' after lambda captures", *current_token_);
    }

    // Parse parameter list (optional)
    std::vector<ASTNode> parameters;
    if (peek_token().has_value() && peek_token()->value() == "(") {
        consume_token(); // consume '('

        // Parse parameters
        if (!peek_token().has_value() || peek_token()->value() != ")") {
            while (true) {
                // Parse parameter declaration (type + name only, no initializer)
                ParseResult param_result = parse_type_and_name();
                if (param_result.is_error()) {
                    return param_result;
                }
                if (param_result.node().has_value()) {
                    parameters.push_back(*param_result.node());
                }

                // Check for comma (more parameters) or closing paren
                if (peek_token().has_value() && peek_token()->value() == ",") {
                    consume_token(); // consume comma
                } else {
                    break;
                }
            }
        }

        // Expect ')'
        if (!consume_punctuator(")")) {
            return ParseResult::error("Expected ')' after lambda parameters", *current_token_);
        }
    }

    // Parse optional return type (-> type)
    std::optional<ASTNode> return_type;
    if (peek_token().has_value() && peek_token()->value() == "->") {
        consume_token(); // consume '->'
        ParseResult type_result = parse_type_specifier();
        if (type_result.is_error()) {
            return type_result;
        }
        return_type = type_result.node();
    }

    // Parse body (must be a compound statement)
    if (!peek_token().has_value() || peek_token()->value() != "{") {
        return ParseResult::error("Expected '{' for lambda body", *current_token_);
    }

    // Add parameters to symbol table before parsing body
    gSymbolTable.enter_scope(ScopeType::Block);
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
        std::unordered_set<std::string> referenced_vars;
        findReferencedIdentifiers(*body_result.node(), referenced_vars);

        // Build a set of parameter names to exclude from captures
        std::unordered_set<std::string> param_names;
        for (const auto& param : parameters) {
            if (param.is<DeclarationNode>()) {
                param_names.insert(std::string(param.as<DeclarationNode>().identifier_token().value()));
            }
        }

        // Build a set of local variable names declared inside the lambda body
        std::unordered_set<std::string> local_vars;
        findLocalVariableDeclarations(*body_result.node(), local_vars);

        // Convert capture-all kind to specific capture kind
        LambdaCaptureNode::CaptureKind specific_kind =
            (capture_all_kind == LambdaCaptureNode::CaptureKind::AllByValue)
            ? LambdaCaptureNode::CaptureKind::ByValue
            : LambdaCaptureNode::CaptureKind::ByReference;

        // For each referenced variable, check if it's a non-local variable
        for (const auto& var_name : referenced_vars) {
            // Skip empty names or placeholders
            if (var_name.empty() || var_name == "_") {
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
            std::optional<ASTNode> var_symbol = gSymbolTable.lookup(var_name);
            if (var_symbol.has_value()) {
                // Check if this is a variable (not a function or type)
                // Variables are stored as DeclarationNode in the symbol table
                if (var_symbol->is<DeclarationNode>()) {
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
                        const auto& decl = var_symbol->as<DeclarationNode>();
                        Token var_token = decl.identifier_token();
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
        lambda_token
    );

    // Register the lambda closure type in the type system immediately
    // This allows auto type deduction to work
    const auto& lambda = lambda_node.as<LambdaExpressionNode>();
    std::string closure_name = lambda.generate_lambda_name();

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

            // Look up the captured variable in the current scope
            std::string_view var_name = capture.identifier_name();
            std::optional<ASTNode> var_symbol = gSymbolTable.lookup(var_name);

            if (!var_symbol.has_value()) {
                continue;
            }

            if (!var_symbol->is<DeclarationNode>()) {
                continue;
            }

            const auto& var_decl = var_symbol->as<DeclarationNode>();
            const auto& var_type = var_decl.type_node().as<TypeSpecifierNode>();

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

            closure_struct_info->addMember(
                std::string(var_name),
                member_type,
                type_index,
                member_size,
                member_alignment,
                AccessSpecifier::Public
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
        Token(Token::Type::Identifier, "operator()", lambda_token.line(), lambda_token.column(), lambda_token.file_index())
    ).as<DeclarationNode>();

    // Create FunctionDeclarationNode for operator()
    FunctionDeclarationNode& operator_call_func = emplace_node<FunctionDeclarationNode>(
        operator_call_decl,
        closure_name
    ).as<FunctionDeclarationNode>();

    // Add parameters from lambda to operator()
    for (const auto& param : parameters) {
        operator_call_func.add_parameter_node(param);
    }

    // Add operator() as a member function
    StructMemberFunction operator_call_member(
        "operator()",
        emplace_node<FunctionDeclarationNode>(operator_call_func),
        AccessSpecifier::Public,
        false,  // not constructor
        false,  // not destructor
        true,   // is operator overload
        "()"    // operator symbol
    );

    closure_struct_info->member_functions.push_back(operator_call_member);

    closure_type.struct_info_ = std::move(closure_struct_info);

    return ParseResult::success(lambda_node);
}

ParseResult Parser::transformLambdaToStruct(const LambdaExpressionNode& lambda) {
    // Transform lambda into a struct with operator() (Clang-style)
    // This is Phase 1: Simple lambdas without captures

    // Generate unique struct name for this lambda
    static int lambda_counter = 0;
    std::string struct_name = "__lambda_" + std::to_string(lambda_counter++);
    Token struct_token = lambda.lambda_token();

    // Create struct type specifier
    auto struct_type = emplace_node<TypeSpecifierNode>(
        Type::Struct,
        TypeQualifier::None,
        0,  // size will be calculated later
        struct_token
    );

    // Create struct type info
    auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
        struct_name,
        false  // is_class = false (it's a struct)
    );

    // Add operator() as a member function
    // Create return type for operator()
    ASTNode return_type_node;
    if (lambda.return_type().has_value()) {
        return_type_node = *lambda.return_type();
    } else {
        // Default to int if no return type specified
        return_type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, struct_token);
    }

    // Create operator() identifier token
    static const std::string operator_call_name = "operator()";
    Token operator_token(Token::Type::Identifier, operator_call_name,
                        struct_token.line(), struct_token.column(), struct_token.file_index());

    // Create declaration for operator()
    auto [operator_decl, operator_decl_ref] = emplace_node_ref<DeclarationNode>(
        return_type_node,
        operator_token
    );

    // Create function declaration for operator() with parameters
    auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(
        operator_decl_ref
    );

    // Add parameters from lambda
    for (const auto& param : lambda.parameters()) {
        func_ref.add_parameter_node(param);
    }

    // Set the body - need to create a mutable copy
    auto body_copy = emplace_node<BlockNode>(lambda.body().as<BlockNode>());
    func_ref.set_definition(body_copy.as<BlockNode>());

    // Add operator() to the struct
    struct_ref.add_operator_overload(
        "()",  // operator symbol
        func_node,
        AccessSpecifier::Public,
        false,  // not virtual
        false,  // not pure virtual
        false,  // not override
        false   // not final
    );

    // Register the struct in the symbol table
    gSymbolTable.insert(struct_name, struct_node);

    // For now, just return the lambda node itself
    // TODO: Return a proper struct construction expression
    auto result_lambda = emplace_node<LambdaExpressionNode>(
        lambda.captures(),
        lambda.parameters(),
        lambda.body(),
        lambda.return_type(),
        lambda.lambda_token()
    );

    return ParseResult::success(result_lambda);
}

ParseResult Parser::parse_if_statement() {
    if (!consume_keyword("if"sv)) {
        return ParseResult::error("Expected 'if' keyword", *current_token_);
    }

    if (!consume_punctuator("("sv)) {
        return ParseResult::error("Expected '(' after 'if'", *current_token_);
    }

    // Check for C++20 if-with-initializer: if (init; condition)
    std::optional<ASTNode> init_statement;

    // Look ahead to see if there's a semicolon (indicating init statement)
    // Only try to parse as initializer if we see a type keyword or CV-qualifier
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
        // Only proceed if this is actually a type keyword or CV-qualifier
        if (type_keywords.find(peek_token()->value()) != type_keywords.end()) {
            // Could be a declaration like: if (int x = 5; x > 0)
            auto checkpoint = save_token_position();
            ParseResult potential_init = parse_variable_declaration();

            if (!potential_init.is_error() && peek_token().has_value() &&
                peek_token()->type() == Token::Type::Punctuator &&
                peek_token()->value() == ";") {
                // We have an initializer
                discard_saved_token(checkpoint);
                init_statement = potential_init.node();
                if (!consume_punctuator(";")) {
                    return ParseResult::error("Expected ';' after if initializer", *current_token_);
                }
            } else {
                // Not an initializer, restore position
                restore_token_position(checkpoint);
            }
        }
    }

    // Parse condition
    auto condition = parse_expression();
    if (condition.is_error()) {
        return condition;
    }

    if (!consume_punctuator(")")) {
        return ParseResult::error("Expected ')' after if condition", *current_token_);
    }

    // Parse then-statement (can be a block or a single statement)
    ParseResult then_stmt;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
        then_stmt = parse_block();
    } else {
        then_stmt = parse_statement_or_declaration();
    }

    if (then_stmt.is_error()) {
        return then_stmt;
    }

    // Check for else clause
    std::optional<ASTNode> else_stmt;
    if (peek_token().has_value() &&
        peek_token()->type() == Token::Type::Keyword &&
        peek_token()->value() == "else") {
        consume_keyword("else");

        // Parse else-statement (can be a block, another if, or a single statement)
        ParseResult else_result;
        if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "{") {
            else_result = parse_block();
        } else if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "if") {
            // Handle else-if chain
            else_result = parse_if_statement();
        } else {
            else_result = parse_statement_or_declaration();
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
                *cond_node, *then_node, else_stmt, init_statement
            ));
        }
    }

    return ParseResult::error("Invalid if statement construction", *current_token_);
}

ParseResult Parser::parse_switch_statement() {
    if (!consume_keyword("switch"sv)) {
        return ParseResult::error("Expected 'switch' keyword", *current_token_);
    }

    if (!consume_punctuator("("sv)) {
        return ParseResult::error("Expected '(' after 'switch'", *current_token_);
    }

    // Parse the switch condition expression
    auto condition = parse_expression();
    if (condition.is_error()) {
        return condition;
    }

    if (!consume_punctuator(")"sv)) {
        return ParseResult::error("Expected ')' after switch condition", *current_token_);
    }

    // Parse the switch body (must be a compound statement with braces)
    if (!consume_punctuator("{"sv)) {
        return ParseResult::error("Expected '{' for switch body", *current_token_);
    }

    // Create a block to hold case/default labels and their statements
    auto [block_node, block_ref] = create_node_ref(BlockNode());

    // Parse case and default labels
    while (peek_token().has_value() && peek_token()->value() != "}") {
        auto current = peek_token();

        if (current->type() == Token::Type::Keyword && current->value() == "case") {
            // Parse case label
            consume_token(); // consume 'case'

            // Parse case value (must be a constant expression)
            auto case_value = parse_expression();
            if (case_value.is_error()) {
                return case_value;
            }

            if (!consume_punctuator(":"sv)) {
                return ParseResult::error("Expected ':' after case value", *current_token_);
            }

            // Parse statements until next case/default/closing brace
            // We collect all statements for this case into a sub-block
            auto [case_block_node, case_block_ref] = create_node_ref(BlockNode());

            while (peek_token().has_value() &&
                   peek_token()->value() != "}" &&
                   !(peek_token()->type() == Token::Type::Keyword &&
                     (peek_token()->value() == "case" || peek_token()->value() == "default"))) {
                // Skip stray semicolons (empty statements)
                if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ";") {
                    consume_token();
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

        } else if (current->type() == Token::Type::Keyword && current->value() == "default") {
            // Parse default label
            consume_token(); // consume 'default'

            if (!consume_punctuator(":"sv)) {
                return ParseResult::error("Expected ':' after 'default'", *current_token_);
            }

            // Parse statements until next case/default/closing brace
            auto [default_block_node, default_block_ref] = create_node_ref(BlockNode());

            while (peek_token().has_value() &&
                   peek_token()->value() != "}" &&
                   !(peek_token()->type() == Token::Type::Keyword &&
                     (peek_token()->value() == "case" || peek_token()->value() == "default"))) {
                // Skip stray semicolons (empty statements)
                if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ";") {
                    consume_token();
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
            if (current->type() == Token::Type::Keyword) {
                error_msg += "keyword '" + std::string(current->value()) + "'";
            } else if (current->type() == Token::Type::Identifier) {
                error_msg += "identifier '" + std::string(current->value()) + "'";
            } else {
                error_msg += "'" + std::string(current->value()) + "'";
            }
            return ParseResult::error(error_msg, *current_token_);
        }
    }

    if (!consume_punctuator("}"sv)) {
        return ParseResult::error("Expected '}' to close switch body", *current_token_);
    }

    // Create switch statement node
    if (auto cond_node = condition.node()) {
        return ParseResult::success(emplace_node<SwitchStatementNode>(*cond_node, block_node));
    }

    return ParseResult::error("Invalid switch statement construction", *current_token_);
}

ParseResult Parser::parse_qualified_identifier() {
	// This method parses qualified identifiers like std::print or ns1::ns2::func
	// It should be called when we've already seen an identifier followed by ::

	std::vector<StringType<>> namespaces;
	Token final_identifier;

	// We should already be at an identifier
	auto first_token = peek_token();
	if (!first_token.has_value() || first_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier in qualified name", first_token.value_or(Token()));
	}

	// Collect namespace parts
	while (true) {
		auto identifier_token = consume_token();
		if (!identifier_token.has_value() || identifier_token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier", identifier_token.value_or(Token()));
		}

		// Check if followed by ::
		if (peek_token().has_value() && peek_token()->value() == "::") {
			// This is a namespace part
			namespaces.emplace_back(StringType<>(identifier_token->value()));
			consume_token(); // consume ::
		} else {
			// This is the final identifier
			final_identifier = *identifier_token;
			break;
		}
	}

	// Create a QualifiedIdentifierNode
	auto qualified_node = emplace_node<QualifiedIdentifierNode>(namespaces, final_identifier);
	return ParseResult::success(qualified_node);
}

std::string Parser::buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const {
	std::string result;

	// Get return type from the function's declaration node
	const DeclarationNode& decl = func_node.decl_node();
	const TypeSpecifierNode& ret_type = decl.type_node().as<TypeSpecifierNode>();
	result += ret_type.getReadableString();
	result += " ";

	// Add namespace prefix if we're in a namespace
	auto namespace_path = gSymbolTable.build_current_namespace_path();
	for (const auto& ns : namespace_path) {
#if USE_OLD_STRING_APPROACH
		result += ns + "::";
#else
		result += std::string(ns.view()) + "::";
#endif
	}

	// Add class/struct prefix if this is a member function
	if (func_node.is_member_function()) {
		result += func_node.parent_struct_name();
		result += "::";
	}

	// Add function name
	result += decl.identifier_token().value();

	// Add parameters
	result += "(";
	const auto& params = func_node.parameter_nodes();
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) result += ", ";
		const auto& param_decl = params[i].as<DeclarationNode>();
		const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		result += param_type.getReadableString();
	}
	result += ")";

	return result;
}

// Helper to extract type from an expression for overload resolution
std::optional<TypeSpecifierNode> Parser::get_expression_type(const ASTNode& expr_node) {
	// Handle lambda expressions directly (not wrapped in ExpressionNode)
	if (expr_node.is<LambdaExpressionNode>()) {
		const auto& lambda = expr_node.as<LambdaExpressionNode>();
		std::string closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, 8, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 8, lambda.lambda_token());
	}

	if (!expr_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();

	// Handle different expression types
	if (std::holds_alternative<NumericLiteralNode>(expr)) {
		const auto& literal = std::get<NumericLiteralNode>(expr);
		return TypeSpecifierNode(literal.type(), literal.qualifier(), literal.sizeInBits());
	}
	else if (std::holds_alternative<IdentifierNode>(expr)) {
		const auto& ident = std::get<IdentifierNode>(expr);
		auto symbol = gSymbolTable.lookup(ident.name());
		if (symbol.has_value() && symbol->is<DeclarationNode>()) {
			const auto& decl = symbol->as<DeclarationNode>();
			TypeSpecifierNode type = decl.type_node().as<TypeSpecifierNode>();

			// Handle array-to-pointer decay
			// When an array is used in an expression (except with sizeof, &, etc.),
			// it decays to a pointer to its first element
			if (decl.array_size().has_value()) {
				// This is an array declaration - decay to pointer
				// Create a new TypeSpecifierNode with one level of pointer
				TypeSpecifierNode pointer_type = type;
				pointer_type.add_pointer_level();
				return pointer_type;
			}

			return type;
		}
	}
	else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		// For binary operators, we'd need to evaluate the result type
		// For now, just return int as a placeholder
		// TODO: Implement proper type inference for binary operators
		return TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
	}
	else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		// For unary operators, handle type transformations
		const auto& unary = std::get<UnaryOperatorNode>(expr);
		std::string_view op = unary.op();

		// Get the operand type
		auto operand_type_opt = get_expression_type(unary.get_operand());
		if (!operand_type_opt.has_value()) {
			return std::nullopt;
		}

		TypeSpecifierNode operand_type = *operand_type_opt;

		// Handle dereference operator: *ptr -> removes one level of pointer/reference
		if (op == "*") {
			if (operand_type.is_reference()) {
				// Dereferencing a reference gives the underlying type
				TypeSpecifierNode result = operand_type;
				result.set_reference(false);
				return result;
			} else if (operand_type.pointer_levels().size() > 0) {
				// Dereferencing a pointer removes one level of pointer
				TypeSpecifierNode result = operand_type;
				result.remove_pointer_level();
				return result;
			}
		}
		// Handle address-of operator: &var -> adds one level of pointer
		else if (op == "&") {
			TypeSpecifierNode result = operand_type;
			result.add_pointer_level();
			return result;
		}

		// For other unary operators (+, -, !, ~, ++, --), return the operand type
		return operand_type;
	}
	else if (std::holds_alternative<FunctionCallNode>(expr)) {
		// For function calls, get the return type
		const auto& func_call = std::get<FunctionCallNode>(expr);
		const auto& decl = func_call.function_declaration();
		return decl.type_node().as<TypeSpecifierNode>();
	}
	else if (std::holds_alternative<LambdaExpressionNode>(expr)) {
		// For lambda expressions, return the closure struct type
		const auto& lambda = std::get<LambdaExpressionNode>(expr);
		std::string closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, 8, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 8, lambda.lambda_token());
	}
	// Add more cases as needed

	return std::nullopt;
}

// Helper function to deduce the type of an expression for auto type deduction
Type Parser::deduce_type_from_expression(const ASTNode& expr) const {
	// For now, use a simple approach: use the existing get_expression_type function
	// which returns TypeSpecifierNode, and extract the type from it
	auto type_spec_opt = get_expression_type(expr);
	if (type_spec_opt.has_value()) {
		return type_spec_opt->type();
	}

	// Default to int if we can't determine the type
	return Type::Int;
}

// Parse extern "C" { ... } block
ParseResult Parser::parse_extern_block(Linkage linkage) {
	// Expect '{'
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' after extern linkage specification", *current_token_);
	}

	// Create a block to hold all declarations
	auto [block_node, block_ref] = create_node_ref(BlockNode());

	// Save the current linkage and set the new one
	Linkage saved_linkage = current_linkage_;
	current_linkage_ = linkage;

	// Parse declarations until '}'
	while (peek_token().has_value() && peek_token()->value() != "}") {
		// Parse a declaration or function definition
		ParseResult decl_result = parse_declaration_or_function_definition();
		if (decl_result.is_error()) {
			current_linkage_ = saved_linkage;  // Restore linkage before returning error
			return decl_result;
		}

		if (auto decl_node = decl_result.node()) {
			block_ref.add_statement_node(*decl_node);
		}
	}

	// Restore the previous linkage
	current_linkage_ = saved_linkage;

	if (!consume_punctuator("}")) {
		return ParseResult::error("Expected '}' after extern block", *current_token_);
	}

	return ParseResult::success(block_node);
}

// Helper function to get the size of a type in bits
unsigned char Parser::get_type_size_bits(Type type) {
	switch (type) {
		case Type::Bool:
		case Type::Char:
		case Type::UnsignedChar:
			return 8;
		case Type::Short:
		case Type::UnsignedShort:
			return 16;
		case Type::Int:
		case Type::UnsignedInt:
		case Type::Float:
			return 32;
		case Type::Long:
		case Type::UnsignedLong:
		case Type::LongLong:
		case Type::UnsignedLongLong:
		case Type::Double:
			return 64;
		case Type::LongDouble:
			return 80;
		default:
			return 32;  // Default to 32 bits
	}
}
