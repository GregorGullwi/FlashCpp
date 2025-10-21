#include "Parser.h"
#ifdef USE_LLVM
#include "LibClangIRGenerator.h"
#endif
#include "OverloadResolution.h"
#include <string_view> // Include string_view header
#include <unordered_set> // Include unordered_set header

// Define the global symbol table (declared as extern in SymbolTable.h)
SymbolTable gSymbolTable;

static const std::unordered_set<std::string_view> type_keywords = {
	"int"sv, "float"sv, "double"sv, "char"sv, "bool"sv, "void"sv,
	"short"sv, "long"sv, "signed"sv, "unsigned"sv, "const"sv, "volatile"sv, "alignas"sv
};

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

    // Check for operator overload (e.g., operator=)
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
        peek_token()->value() == "operator") {

        Token operator_keyword_token = *peek_token();
        consume_token(); // consume 'operator'

        // Parse the operator symbol
        if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator) {
            return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
        }

        Token operator_symbol_token = *peek_token();
        std::string_view operator_symbol = operator_symbol_token.value();
        consume_token(); // consume operator symbol

        // For now, we only support operator=
        if (operator_symbol != "=") {
            return ParseResult::error("Only operator= is currently supported", operator_symbol_token);
        }

        // Create a synthetic identifier token for "operator="
        // Use a static string so the string_view remains valid
        static const std::string operator_eq_name = "operator=";
        identifier_token = Token(Token::Type::Identifier, operator_eq_name,
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
	// Attempt to parse a simple declaration (variable or typedef)
	if (!consume_punctuator(";")) {
		discard_saved_token(saved_position);
		return ParseResult::error("Expected ;", *current_token_);
	}

	discard_saved_token(saved_position);
	return ParseResult::success();
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
	TypeInfo& struct_type_info = add_struct_type(std::string(struct_name));

	// Check for alignas specifier after struct name (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Create struct declaration node - string_view points directly into source text
	auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name, is_class);

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

			// Parse base class name
			auto base_name_token = consume_token();
			if (!base_name_token.has_value() || base_name_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected base class name", base_name_token.value_or(Token()));
			}

			std::string base_class_name(base_name_token->value());

			// Look up base class type
			auto base_type_it = gTypesByName.find(base_class_name);
			if (base_type_it == gTypesByName.end()) {
				return ParseResult::error("Base class '" + base_class_name + "' not found", *base_name_token);
			}

			const TypeInfo* base_type_info = base_type_it->second;
			if (base_type_info->type_ != Type::Struct) {
				return ParseResult::error("Base class '" + base_class_name + "' is not a struct/class", *base_name_token);
			}

			// Add base class to struct node and type info
			struct_ref.add_base_class(base_class_name, base_type_info->type_index_, base_access);
			struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base_access);

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
					// We already entered a scope for the initializer list, so we don't need to enter again
					// Just set up the member function context
					current_function_ = nullptr;  // Constructors don't have a return type

					// Look up the struct type
					auto type_it = gTypesByName.find(struct_name);
					size_t struct_type_index = 0;
					if (type_it != gTypesByName.end()) {
						struct_type_index = type_it->second->type_index_;
					}

					member_function_context_stack_.push_back({struct_name, struct_type_index, &struct_ref});

					// Parameters are already in the symbol table from the initializer list parsing

					auto block_result = parse_block();
					if (block_result.is_error()) {
						current_function_ = nullptr;
						member_function_context_stack_.pop_back();
						gSymbolTable.exit_scope();
						return block_result;
					}

					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();

					if (auto block = block_result.node()) {
						ctor_ref.set_definition(block->as<BlockNode>());
					}
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
				gSymbolTable.enter_scope(ScopeType::Function);
				current_function_ = nullptr;

				// Look up the struct type
				auto type_it = gTypesByName.find(struct_name);
				size_t struct_type_index = 0;
				if (type_it != gTypesByName.end()) {
					struct_type_index = type_it->second->type_index_;
				}

				member_function_context_stack_.push_back({struct_name, struct_type_index, &struct_ref});

				auto block_result = parse_block();
				if (block_result.is_error()) {
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return block_result;
				}

				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();

				if (auto block = block_result.node()) {
					dtor_ref.set_definition(block->as<BlockNode>());
				}
			} else if (!is_defaulted && !is_deleted && !consume_punctuator(";")) {
				return ParseResult::error("Expected '{', ';', '= default', or '= delete' after destructor declaration", *peek_token());
			}

			// Add destructor to struct (unless deleted)
			if (!is_deleted) {
				struct_ref.add_destructor(dtor_node, current_access);
			}
			continue;
		}

		// Parse member declaration (could be data member or member function)
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
					return ParseResult::error("Expected 'default' or 'delete' after '='", *peek_token());
				}
			}

			// Parse function body if present (and not defaulted/deleted)
			if (!is_defaulted && !is_deleted && peek_token().has_value() && peek_token()->value() == "{") {
				// Enter function scope for parsing the body
				gSymbolTable.enter_scope(ScopeType::Function);

				// Set current function pointer for __func__, __PRETTY_FUNCTION__
				// The FunctionDeclarationNode persists in the AST, so the pointer is safe
				current_function_ = &member_func_ref;

				// Look up the struct type to get its type index
				// Use string_view directly - gTypesByName supports heterogeneous lookup
				auto type_it = gTypesByName.find(struct_name);
				size_t struct_type_index = 0;
				if (type_it != gTypesByName.end()) {
					struct_type_index = type_it->second->type_index_;
				}

				// Push member function context so we can resolve member variables
				// Store a pointer to the struct node so we can access members during parsing
				// Pass string_view directly - MemberFunctionContext stores it as string_view
				member_function_context_stack_.push_back({struct_name, struct_type_index, &struct_ref});

				// Add parameters to symbol table
				for (const auto& param : member_func_ref.parameter_nodes()) {
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
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return block_result;
				}

				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();

				if (auto block = block_result.node()) {
					member_func_ref.set_definition(block->as<BlockNode>());
				}
			} else if (!is_defaulted && !is_deleted && !consume_punctuator(";")) {
				return ParseResult::error("Expected '{', ';', '= default', or '= delete' after member function declaration", *peek_token());
			}

			// Check if this is an operator overload
			std::string_view func_name = decl_node.identifier_token().value();
			if (func_name.starts_with("operator")) {
				// Extract the operator symbol (e.g., "operator=" -> "=")
				std::string_view operator_symbol = func_name.substr(8);  // Skip "operator"
				struct_ref.add_operator_overload(operator_symbol, member_func_node, current_access);
			} else {
				// Add regular member function to struct
				struct_ref.add_member_function(member_func_node, current_access);
			}
		} else {
			// This is a data member
			// Expect semicolon after member declaration
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after struct member declaration", *peek_token());
			}

			// Add member to struct with current access level
			struct_ref.add_member(*member_result.node(), current_access);
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

		// Add member to struct layout
		struct_info->addMember(
			std::string(decl.identifier_token().value()),
			type_spec.type(),
			type_spec.type_index(),
			member_size,
			member_alignment,
			member_decl.access
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
				func_decl.access
			);
			has_user_defined_destructor = true;
		} else if (func_decl.is_operator_overload) {
			// Operator overload
			struct_info->addOperatorOverload(
				func_decl.operator_symbol,
				func_decl.function_declaration,
				func_decl.access
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
				func_decl.access
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

ParseResult Parser::parse_namespace() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'namespace' keyword
	if (!consume_keyword("namespace")) {
		return ParseResult::error("Expected 'namespace' keyword", *peek_token());
	}

	// Parse namespace name
	auto name_token = consume_token();
	if (!name_token.has_value() || name_token->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected namespace name", name_token.value_or(Token()));
	}

	std::string_view namespace_name = name_token->value();

	// Expect opening brace
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' after namespace name", *peek_token());
	}

	// Create namespace declaration node - string_view points directly into source text
	auto [namespace_node, namespace_ref] = emplace_node_ref<NamespaceDeclarationNode>(namespace_name);

	// Enter namespace scope
	gSymbolTable.enter_namespace(namespace_name);

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
			gSymbolTable.exit_scope();
			return decl_result;
		}

		if (auto node = decl_result.node()) {
			namespace_ref.add_declaration(*node);
		}
	}

	// Expect closing brace
	if (!consume_punctuator("}")) {
		gSymbolTable.exit_scope();
		return ParseResult::error("Expected '}' after namespace body", *peek_token());
	}

	// Exit namespace scope
	gSymbolTable.exit_scope();

	return saved_position.success(namespace_node);
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

		// Otherwise, treat as generic user-defined type
		// Look up the type_index if it's a registered type
		TypeIndex user_type_index = 0;
		if (type_it != gTypesByName.end()) {
			user_type_index = type_it->second->type_index_;
		}
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			Type::UserDefined, user_type_index, type_size, type_name_token, cv_qualifier));
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
		static const std::unordered_map<std::string_view, ParsingFunction>
			keyword_parsing_functions = {
			{"if", &Parser::parse_if_statement},
			{"for", &Parser::parse_for_loop},
			{"while", &Parser::parse_while_loop},
			{"do", &Parser::parse_do_while_loop},
			{"return", &Parser::parse_return_statement},
			{"break", &Parser::parse_break_statement},
			{"continue", &Parser::parse_continue_statement},
			//{"struct", &Parser::parse_struct_declaration}
		};

		auto keyword_iter = keyword_parsing_functions.find(current_token.value());
		if (keyword_iter != keyword_parsing_functions.end()) {
			// Call the appropriate parsing function
			return (this->*(keyword_iter->second))();
		}
		else {
			// Check if it's a type specifier keyword (int, float, etc.) or CV-qualifier or alignas
			if (type_keywords.find(current_token.value()) != type_keywords.end()) {
				// Parse as variable declaration with optional initialization
				return parse_variable_declaration();
			}
			// Check if it's 'new' or 'delete' - these are expression keywords
			else if (current_token.value() == "new" || current_token.value() == "delete") {
				// Parse as expression statement
				return parse_expression();
			}
			else {
				// Unknown keyword - consume token to avoid infinite loop and return error
				consume_token();
				return ParseResult::error("Unknown keyword: " + std::string(current_token.value()),
					current_token);
			}
		}
	}
	else if (current_token.type() == Token::Type::Identifier) {
		// Check if this identifier is a registered struct/class/enum type
		std::string type_name(current_token.value());
		auto type_it = gTypesByName.find(type_name);
		if (type_it != gTypesByName.end() && (type_it->second->isStruct() || type_it->second->isEnum())) {
			// This is a struct/enum type declaration
			return parse_variable_declaration();
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
	else {
		// Unknown token type - consume token to avoid infinite loop and return error
		consume_token();
		return ParseResult::error("Expected a statement or declaration",
			current_token);
	}
}

ParseResult Parser::parse_variable_declaration()
{
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

		// Create and return a VariableDeclarationNode
		ASTNode var_decl_node = emplace_node<VariableDeclarationNode>(
			emplace_node<DeclarationNode>(decl),
			init_expr
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

	// Parse comma-separated initializer expressions
	size_t member_index = 0;
	while (true) {
		// Check if we've reached the end of the initializer list
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "}") {
			break;
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
				auto unary_op = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *operand_node, true));
				return ParseResult::success(unary_op);
			}
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

	// Check for offsetof builtin first (before general identifier handling)
	if (current_token_->type() == Token::Type::Identifier && current_token_->value() == "offsetof") {
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
				return ParseResult::error("Missing identifier", idenfifier_token);
			}
		}
		else if (!identifierType->is<DeclarationNode>() && !identifierType->is<FunctionDeclarationNode>()) {
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, *current_token_);
		}
		else {
			// Identifier already consumed at line 1621

			if (consume_punctuator("("sv)) {
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

				// Perform overload resolution
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
	else if (consume_punctuator("(")) {
		// Parse parenthesized expression
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

		// Check for member access operator .
		if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "."sv) {
			consume_token(); // consume '.'

			// Expect an identifier (member name)
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected member name after '.'", *current_token_);
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

		// No more postfix operators
		break;
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
std::optional<TypeSpecifierNode> Parser::get_expression_type(const ASTNode& expr_node) const {
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
	// Add more cases as needed

	return std::nullopt;
}
