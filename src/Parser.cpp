#include "Parser.h"
#ifdef USE_LLVM
#include "LibClangIRGenerator.h"
#endif
#include "OverloadResolution.h"
#include "TemplateRegistry.h"
#include "ConstExprEvaluator.h"
#include <string_view> // Include string_view header
#include <unordered_set> // Include unordered_set header
#include <ranges> // Include ranges for std::ranges::find
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
TemplateRegistry gTemplateRegistry;

// Type keywords set - used for if-statement initializer detection
static const std::unordered_set<std::string_view> type_keywords = {
	"int"sv, "float"sv, "double"sv, "char"sv, "bool"sv, "void"sv,
	"short"sv, "long"sv, "signed"sv, "unsigned"sv, "const"sv, "volatile"sv, "alignas"sv,
	// Microsoft-specific type keywords
	"__int8"sv, "__int16"sv, "__int32"sv, "__int64"sv
};

// Calling convention keyword mapping - Microsoft-specific
struct CallingConventionMapping {
	std::string_view keyword;
	CallingConvention convention;
};

static constexpr CallingConventionMapping calling_convention_map[] = {
	{"__cdecl"sv, CallingConvention::Cdecl},
	{"__stdcall"sv, CallingConvention::Stdcall},
	{"__fastcall"sv, CallingConvention::Fastcall},
	{"__vectorcall"sv, CallingConvention::Vectorcall},
	{"__thiscall"sv, CallingConvention::Thiscall},
	{"__clrcall"sv, CallingConvention::Clrcall}
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

Parser::ScopedTokenPosition::ScopedTokenPosition(class Parser& parser, const std::source_location location)
    : parser_(parser), saved_position_(parser.save_token_position()), location_(location) {}

Parser::ScopedTokenPosition::~ScopedTokenPosition() {
    if (!discarded_) {
        std::cerr << "DEBUG ~ScopedTokenPosition: Calling restore from "
                  << location_.file_name() << ":" << location_.line() 
                  << " in function " << location_.function_name() << "\n";
        parser_.restore_token_position(saved_position_);
    }
}

ParseResult Parser::ScopedTokenPosition::success(ASTNode node) {
    std::cerr << "DEBUG ScopedTokenPosition::success() called, discarding saved position\n";
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

ParseResult Parser::ScopedTokenPosition::propagate(ParseResult&& result) {
    // Sub-parser already handled position restoration (if needed)
    // Just discard our saved position and forward the result
    discarded_ = true;
    parser_.discard_saved_token(saved_position_);
    return std::move(result);
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

std::optional<Token> Parser::peek_token(size_t lookahead) {
    if (lookahead == 0) {
        return peek_token();  // Peek at current token
    }
    
    // Save current position
    TokenPosition saved_pos = save_token_position();
    
    // Consume tokens to reach the lookahead position
    std::optional<Token> result;
    for (size_t i = 0; i < lookahead; ++i) {
        consume_token();
    }
    
    // Peek at the token at lookahead position
    result = peek_token();
    
    // Restore original position
    restore_lexer_position_only(saved_pos);
    
    return result;
}

TokenPosition Parser::save_token_position() {
    TokenPosition cur_pos = lexer_.save_token_position();
    saved_tokens_[cur_pos.cursor_] = { current_token_, ast_nodes_.size() };
    return cur_pos;
}

void Parser::restore_token_position(const TokenPosition& saved_token_pos, const std::source_location location) {
    lexer_.restore_token_position(saved_token_pos);
    SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
    current_token_ = saved_token.current_token_;
	
	if (context_.isVerboseMode()) {
		size_t old_size = ast_nodes_.size();
		ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_,
			ast_nodes_.end());
	}
    ast_nodes_.erase(ast_nodes_.begin() + saved_token.ast_nodes_size_,
        ast_nodes_.end());
    //saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::restore_lexer_position_only(const TokenPosition& saved_token_pos) {
    // Restore lexer position and current token, but keep AST nodes
    lexer_.restore_token_position(saved_token_pos);
    SavedToken saved_token = saved_tokens_.at(saved_token_pos.cursor_);
    current_token_ = saved_token.current_token_;
    // Don't erase AST nodes - they were intentionally created during re-parsing
}

void Parser::discard_saved_token(const TokenPosition& saved_token_pos) {
	saved_tokens_.erase(saved_token_pos.cursor_);
}

void Parser::skip_balanced_braces() {
	// Expect the current token to be '{'
	if (!peek_token().has_value() || peek_token()->value() != "{") {
		return;
	}
	
	int brace_depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;  // Safety limit to prevent infinite loops

	while (peek_token().has_value() && token_count < MAX_TOKENS) {
		auto token = peek_token();
		if (token->value() == "{") {
			brace_depth++;
		} else if (token->value() == "}") {
			brace_depth--;
			if (brace_depth == 0) {
				// Consume the closing '}' and exit
				consume_token();
				break;
			}
		}
		consume_token();
		token_count++;
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

	// Check for #pragma directives
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

				// Check for push/pop/show: #pragma pack(push) or #pragma pack(pop) or #pragma pack(show)
				// Full syntax:
				//   #pragma pack(push [, identifier] [, n])
				//   #pragma pack(pop [, {identifier | n}])
				//   #pragma pack(show)
				// Note: Identifiers are parsed but NOT currently stored/restored (uses simple stack only)
				// This means #pragma pack(pop, label) just pops once from stack, ignoring the label name
				// True named label support would require a map/stack combo in CompileContext
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
					std::string_view pack_action = peek_token()->value();
					
					// Handle #pragma pack(show)
					if (pack_action == "show") {
						consume_token(); // consume 'show'
						if (!consume_punctuator(")")) {
							return ParseResult::error("Expected ')' after pragma pack show", *current_token_);
						}
						// Emit a warning showing the current pack alignment
						size_t current_align = context_.getCurrentPackAlignment();
						if (current_align == 0) {
							std::cerr << "warning: current pack alignment is default (natural alignment)\n";
						} else {
							std::cerr << "warning: current pack alignment is " << current_align << "\n";
						}
						return saved_position.success();
					}
					
					if (pack_action == "push" || pack_action == "pop") {
						consume_token(); // consume 'push' or 'pop'
						
						// Check for optional parameters
						if (peek_token().has_value() && peek_token()->value() == ",") {
							consume_token(); // consume ','
							
							// First parameter could be identifier or number
							// #pragma pack(push, identifier)
							// #pragma pack(push, identifier, n)
							// #pragma pack(push, n)
							// #pragma pack(pop, identifier)
							// #pragma pack(pop, n)
							
							if (peek_token().has_value()) {
								// Check if it's an identifier (label name)
								if (peek_token()->type() == Token::Type::Identifier) {
									std::string_view identifier = peek_token()->value();
									consume_token(); // consume the identifier
									
									// Check for second comma and alignment value
									// Pattern: pack(push/pop, identifier, n)
									if (peek_token().has_value() && peek_token()->value() == ",") {
										consume_token(); // consume second ','
										
										if (peek_token().has_value()) {
											if (peek_token()->type() == Token::Type::Literal) {
												std::string_view value_str = peek_token()->value();
												size_t alignment = 0;
												auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
												if (result.ec == std::errc()) {
													if (pack_action == "push") {
														context_.pushPackAlignment(identifier, alignment);
													}
													// pop with identifier and number doesn't make sense in MSVC
													consume_token(); // consume the number
												} else {
													consume_token(); // consume invalid number
													if (pack_action == "push") {
														context_.pushPackAlignment(identifier);
													} else {
														context_.popPackAlignment(identifier);
													}
												}
											} else if (peek_token()->type() == Token::Type::Identifier) {
												// Another identifier (macro) - treat as no alignment specified
												consume_token();
												if (pack_action == "push") {
													context_.pushPackAlignment(identifier);
												} else {
													context_.popPackAlignment(identifier);
												}
											}
										}
									} else {
										// Just identifier, no alignment
										// pack(push, identifier) or pack(pop, identifier)
										if (pack_action == "push") {
											context_.pushPackAlignment(identifier);
										} else {
											context_.popPackAlignment(identifier);
										}
									}
								}
								// Check if it's a number directly (no identifier)
								else if (peek_token()->type() == Token::Type::Literal) {
									std::string_view value_str = peek_token()->value();
									size_t alignment = 0;
									auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
									if (result.ec == std::errc()) {
										if (pack_action == "push") {
											context_.pushPackAlignment(alignment);
										}
										// pop with just a number is uncommon but valid
										consume_token(); // consume the number
									} else {
										consume_token(); // consume invalid number
										if (pack_action == "push") {
											context_.pushPackAlignment();
										} else {
											context_.popPackAlignment();
										}
									}
								}
							}
						} else {
							// No parameters - simple push/pop
							if (pack_action == "push") {
								context_.pushPackAlignment();
							} else {
								context_.popPackAlignment();
							}
						}
						
						if (!consume_punctuator(")")) {
							return ParseResult::error("Expected ')' after pragma pack push/pop", *current_token_);
						}
						return saved_position.success();
					}
				}

				// Try to parse a number: #pragma pack(N)
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Literal) {
					std::string_view value_str = peek_token()->value();
					size_t alignment = 0;
					auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
					if (result.ec == std::errc() &&
					    (alignment == 0 || alignment == 1 || alignment == 2 ||
					     alignment == 4 || alignment == 8 || alignment == 16)) {
						context_.setPackAlignment(alignment);
						consume_token(); // consume the number
						if (!consume_punctuator(")")) {
							return ParseResult::error("Expected ')' after pack alignment value", *current_token_);
						}
						return saved_position.success();
					}
				}

				// If we get here, it's an unsupported pragma pack format
				return ParseResult::error("Unsupported #pragma pack format", *current_token_);
			} else {
				// Unknown pragma - skip until end of line or until we hit a token that looks like the start of a new construct
				// Pragmas can span multiple lines with parentheses, so we need to be careful
				std::cerr << "DEBUG: Skipping unknown pragma: " << (peek_token().has_value() ? std::string(peek_token()->value()) : "EOF") << std::endl;
				int paren_depth = 0;
				while (peek_token().has_value()) {
					std::cerr << "  pragma skip loop: token='" << peek_token()->value() << "' type=" << static_cast<int>(peek_token()->type()) << " paren_depth=" << paren_depth << std::endl;
					if (peek_token()->value() == "(") {
						paren_depth++;
						consume_token();
					} else if (peek_token()->value() == ")") {
						paren_depth--;
						consume_token();
						if (paren_depth == 0) {
							// End of pragma
							break;
						}
					} else if (paren_depth == 0 && peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "#") {
						// Start of a new preprocessor directive
						break;
					} else if (paren_depth == 0 && peek_token()->type() == Token::Type::Keyword) {
						// Start of a new declaration (namespace, class, extern, etc.)
						break;
					} else {
						consume_token();
					}
				}
				std::cerr << "DEBUG: Finished skipping pragma, next token: " 
				          << (peek_token().has_value() ? std::string(peek_token()->value()) : "EOF") 
				          << " type=" << (peek_token().has_value() ? static_cast<int>(peek_token()->type()) : -1)
				          << std::endl;
				return saved_position.success();
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
		return saved_position.propagate(std::move(result));
	}

	// Check if it's a static_assert declaration
	if (peek_token()->type() == Token::Type::Keyword &&
		peek_token()->value() == "static_assert") {
		auto result = parse_static_assert();
		if (!result.is_error()) {
			// static_assert doesn't produce an AST node (compile-time only)
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
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
		return saved_position.propagate(std::move(result));
	}

	// Check if it's a template declaration (must come before struct/class check)
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "template") {
		std::cerr << "DEBUG: Top-level found 'template', calling parse_template_declaration\n";
		auto result = parse_template_declaration();
		std::cerr << "DEBUG: parse_template_declaration returned, is_error=" << result.is_error() << "\n";
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			std::cerr << "DEBUG: Template parsed successfully, next token: ";
			if (peek_token().has_value()) {
				std::cerr << "'" << peek_token()->value() << "' at line " << peek_token()->line() << "\n";
			} else {
				std::cerr << "<EOF>\n";
			}
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
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
	if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "enum") {
		auto result = parse_enum_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
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
		return saved_position.propagate(std::move(result));
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

    // Extract calling convention specifiers that can appear after the type
    // Example: void __cdecl func(); or int __stdcall* func();
    // We consume them here and save to last_calling_convention_ for the caller to retrieve
    last_calling_convention_ = CallingConvention::Default;
    while (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
        std::string_view token_val = peek_token()->value();
        
        // Look up calling convention in the mapping table using ranges
        auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
        if (it != std::end(calling_convention_map)) {
            last_calling_convention_ = it->convention;
            consume_token();  // Consume calling convention token
        } else {
            break;  // Not a calling convention keyword, stop scanning
        }
    }

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

    // Check for parameter pack: Type... identifier
    // This is used in variadic function templates like: template<typename... Args> void func(Args... args)
    bool is_parameter_pack = false;
    if (peek_token().has_value() && 
        (peek_token()->type() == Token::Type::Operator || peek_token()->type() == Token::Type::Punctuator) &&
        peek_token()->value() == "...") {
        consume_token(); // consume '...'
        is_parameter_pack = true;
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
        // Regular identifier (or unnamed parameter)
        // Check if this might be an unnamed parameter (next token is ',', ')', '=', or '[')
        if (peek_token().has_value()) {
            auto next = peek_token()->value();
            if (next == "," || next == ")" || next == "=" || next == "[") {
                // This is an unnamed parameter - create a synthetic empty identifier
                identifier_token = Token(Token::Type::Identifier, "",
                                        current_token_->line(), current_token_->column(),
                                        current_token_->file_index());
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
        } else {
            return ParseResult::error("Expected identifier or end of parameter", Token());
        }
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

        // Apply parameter pack flag if this is a parameter pack
        if (is_parameter_pack) {
            decl_node.as<DeclarationNode>().set_parameter_pack(true);
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
	ScopedTokenPosition saved_position(*this);
	
	// Parse any attributes before the declaration ([[nodiscard]], __declspec(dllimport), __cdecl, etc.)
	AttributeInfo attr_info = parse_attributes();
	
	std::cerr << "DEBUG: After parse_attributes, current token: ";
	if (peek_token().has_value()) {
		std::cerr << "'" << peek_token()->value() << "'\n";
	} else {
		std::cerr << "<EOF>\n";
	}

	// Check for constexpr/constinit/consteval keywords
	bool is_constexpr = false;
	bool is_constinit = false;
	bool is_consteval = false;

	while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view kw = peek_token()->value();
		if (kw == "constexpr") {
			is_constexpr = true;
			consume_token();
		} else if (kw == "constinit") {
			is_constinit = true;
			consume_token();
		} else if (kw == "consteval") {
			is_consteval = true;
			consume_token();
		} else {
			break;
		}
	}
	
	if (attr_info.calling_convention == CallingConvention::Default && 
	    last_calling_convention_ != CallingConvention::Default) {
		attr_info.calling_convention = last_calling_convention_;
	}

	// Parse the type specifier and identifier (name)
	// This will also extract any calling convention that appears after the type
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		return type_and_name_result;
	}

	// First, try to parse as a function definition
	DeclarationNode& decl_node = as<DeclarationNode>(type_and_name_result);
	ParseResult function_definition_result = parse_function_declaration(decl_node, attr_info.calling_convention);
	if (!function_definition_result.is_error()) {
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
			// Return the function declaration node (needed for templates)
			if (auto func_node = function_definition_result.node()) {
				return saved_position.success(*func_node);
			}
			return saved_position.success();
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
					node->as<FunctionDeclarationNode>().set_definition(*block);
					return saved_position.success(*node);
				}
			}
			// If we get here, something went wrong but we should still commit
			// because we successfully parsed a function
			return saved_position.success();
		}
	} else {
		// Function parsing failed, the position is restored
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
		if (peek_token()->value() == "=") {
			consume_token(); // consume '='
			auto init_expr = parse_expression();
			if (init_expr.is_error()) {
				return init_expr;
			}
			initializer = init_expr.node();
		}

		if (!consume_punctuator(";")) {
			return ParseResult::error("Expected ;", *current_token_);
		}

		// Create a global variable declaration node
		// Reuse the existing decl_node from type_and_name_result
		auto [global_var_node, global_decl_node] = emplace_node_ref<VariableDeclarationNode>(
			type_and_name_result.node().value(),  // Use the existing DeclarationNode
			initializer
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
			
			// Evaluate the initializer to ensure it's a constant expression
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
			eval_ctx.storage_duration = ConstExpr::StorageDuration::Global;
			eval_ctx.is_constinit = is_constinit;
			
			auto eval_result = ConstExpr::Evaluator::evaluate(initializer.value(), eval_ctx);
			if (!eval_result.success) {
				return ParseResult::error(
					std::string(keyword_name) + " variable initializer must be a constant expression: " + eval_result.error_message,
					identifier_token
				);
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

		return saved_position.success(global_var_node);
	}

	// This should not be reached
	return ParseResult::error("Unexpected parsing state", *current_token_);
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

	// Check for template specialization arguments after struct name
	// e.g., struct MyStruct<int>, struct MyStruct<T&>
	if (peek_token().has_value() && peek_token()->value() == "<") {
		// This is a template specialization - skip the template arguments
		// Full implementation would parse and store these properly
		int angle_bracket_depth = 0;
		consume_token(); // consume '<'
		angle_bracket_depth = 1;
		
		while (peek_token().has_value() && angle_bracket_depth > 0) {
			if (peek_token()->value() == "<") {
				angle_bracket_depth++;
			} else if (peek_token()->value() == ">") {
				angle_bracket_depth--;
			}
			consume_token();
		}
	}

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
		
		// Check if this is a template base class (e.g., Base<T>)
		std::string instantiated_base_name;
		if (peek_token().has_value() && peek_token()->value() == "<") {
			// Parse template arguments
			auto template_args_opt = parse_explicit_template_arguments();
			if (!template_args_opt.has_value()) {
				return ParseResult::error("Failed to parse template arguments for base class", *peek_token());
			}
			
			std::vector<TemplateTypeArg> template_args = *template_args_opt;
			
			// Check if the base class is a template
			auto template_entry = gTemplateRegistry.lookupTemplate(base_class_name);
			if (template_entry) {
				// Try to instantiate the base template
				// Note: try_instantiate_class_template returns nullopt on success 
				// (type is registered in gTypesByName)
				try_instantiate_class_template(base_class_name, template_args);
				
				// Use the instantiated name as the base class
				instantiated_base_name = get_instantiated_class_name(base_class_name, template_args);
				base_class_name = instantiated_base_name;
			}
		}

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
		struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base_access, is_virtual_base);		} while (peek_token().has_value() && peek_token()->value() == "," && consume_token());
	}

	// Check for forward declaration (struct Name;)
	if (peek_token().has_value()) {
		if (peek_token()->value() == ";") {
			// Forward declaration - just register the type and return
			consume_token(); // consume ';'
			return saved_position.success(struct_node);
		}
	}

	// Expect opening brace for full definition
	if (!consume_punctuator("{")) {
		return ParseResult::error("Expected '{' or ';' after struct/class name or base class list", *peek_token());
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

			// Check for 'template' keyword - member function template
			if (keyword == "template") {
				auto template_result = parse_member_function_template(struct_ref, current_access);
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

			// Check for 'enum' keyword - nested enum
			if (keyword == "enum") {
				auto enum_result = parse_enum_declaration();
				if (enum_result.is_error()) {
					return enum_result;
				}

				// Add enum as a nested type
				if (auto enum_node = enum_result.node()) {
					struct_ref.add_nested_class(*enum_node);  // Enums are stored as nested types
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

			// Check for 'using' keyword - type alias
			if (keyword == "using") {
				consume_token(); // consume 'using'
				
				auto alias_token = peek_token();
				if (!alias_token.has_value() || alias_token->type() != Token::Type::Identifier) {
					return ParseResult::error("Expected alias name after 'using'", peek_token().value_or(Token()));
				}
				
				std::string_view alias_name = alias_token->value();
				consume_token(); // consume alias name
				
				// Check for '='
				if (!peek_token().has_value() || peek_token()->value() != "=") {
					return ParseResult::error("Expected '=' after alias name", *current_token_);
				}
				consume_token(); // consume '='
				
				// Parse the type
				auto type_result = parse_type_specifier();
				if (type_result.is_error()) {
					return type_result;
				}
				
				if (!type_result.node().has_value()) {
					return ParseResult::error("Expected type after '=' in type alias", *current_token_);
				}
				
				// Consume semicolon
				if (!consume_punctuator(";")) {
					return ParseResult::error("Expected ';' after type alias", *current_token_);
				}
				
				// Store the alias in the struct
				struct_ref.add_type_alias(alias_name, *type_result.node(), current_access);
				
				// Also register it globally (for non-template or immediate use)
				const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				auto& alias_type_info = gTypeInfo.emplace_back(std::string(alias_name), type_spec.type(), gTypeInfo.size());
				alias_type_info.type_index_ = type_spec.type_index();
				alias_type_info.type_size_ = type_spec.size_in_bits();
				gTypesByName.emplace(alias_type_info.name_, &alias_type_info);
				
				continue;
			}

			// Check for 'static' keyword - static member
			if (keyword == "static") {
				// For now, just parse and skip static members
				// Full implementation would handle static data member initialization
				consume_token(); // consume 'static'
				
				// Check if it's const
				bool is_const = false;
				if (peek_token().has_value() && peek_token()->value() == "const") {
					is_const = true;
					consume_token(); // consume 'const'
				}
				
				// Parse type and name
				auto type_and_name_result = parse_type_and_name();
				if (type_and_name_result.is_error()) {
					return type_and_name_result;
				}
				
				// Check for initialization
				std::optional<ASTNode> init_expr_opt;
				if (peek_token().has_value() && peek_token()->value() == "=") {
					consume_token(); // consume '='
					// Parse the initializer expression
					auto init_result = parse_expression();
					if (init_result.is_error()) {
						return init_result;
					}
					init_expr_opt = init_result.node();
				}

				// Expect semicolon
				if (!consume_punctuator(";")) {
					return ParseResult::error("Expected ';' after static member declaration", *current_token_);
				}

				// Get the declaration and type specifier
				if (!type_and_name_result.node().has_value()) {
					return ParseResult::error("Expected static member declaration", *current_token_);
				}
				const DeclarationNode& decl = type_and_name_result.node()->as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

				// Register static member in struct info
				// Calculate size and alignment for the static member
				size_t static_member_size = get_type_size_bits(type_spec.type()) / 8;
				size_t static_member_alignment = get_type_alignment(type_spec.type(), static_member_size);

				// Add to struct's static members
				struct_info->addStaticMember(
					std::string(decl.identifier_token().value()),
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

						// Determine if this is a delegating, base class, or member initializer
						bool is_delegating = (init_name == struct_ref.name());
						bool is_base_init = false;
						
						if (is_delegating) {
							// Delegating constructor: Point() : Point(0, 0) {}
							// In C++11, if a constructor delegates, it CANNOT have other initializers
							if (!ctor_ref.member_initializers().empty() || !ctor_ref.base_initializers().empty()) {
								return ParseResult::error("Delegating constructor cannot have other member or base initializers", *init_name_token);
							}
							ctor_ref.set_delegating_initializer(std::move(init_args));
						} else {
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
							ctor_ref.set_definition(block_node);

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
						dtor_ref.set_definition(block_node);
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
					&dtor_ref,   // dtor_node
					current_template_param_names_  // template parameter names
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
							member_func_ref.set_definition(block_node);
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
					nullptr,  // dtor_node
					current_template_param_names_  // template parameter names
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

			// Get the type from the member declaration
			if (!member_result.node()->is<DeclarationNode>()) {
				return ParseResult::error("Expected declaration node for member", *peek_token());
			}
			const DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
			const TypeSpecifierNode& type_spec = decl_node.type_node().as<TypeSpecifierNode>();

			// Check for direct brace initialization: C c1{ 1 };
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
			    peek_token()->value() == "{") {
				auto init_result = parse_brace_initializer(type_spec);
				if (init_result.is_error()) {
					return init_result;
				}
				if (init_result.node().has_value()) {
					default_initializer = *init_result.node();
				}
			}
			// Check for member initialization with '=' (C++11 feature)
			else if (peek_token().has_value() && peek_token()->value() == "=") {
				consume_token(); // consume '='

				// Check if this is a brace initializer: B b = { .a = 1 }
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
				    peek_token()->value() == "{") {
					auto init_result = parse_brace_initializer(type_spec);
					if (init_result.is_error()) {
						return init_result;
					}
					if (init_result.node().has_value()) {
						default_initializer = *init_result.node();
					}
				}
				// Check if this is a type name followed by brace initializer: B b = B{ .a = 2 }
				else if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
					// Save position in case this isn't a type name
					TokenPosition saved_pos = save_token_position();

					// Try to parse as type specifier
					ParseResult type_result = parse_type_specifier();
					if (!type_result.is_error() && type_result.node().has_value() &&
					    peek_token().has_value() && peek_token()->type() == Token::Type::Punctuator &&
					    (peek_token()->value() == "{" || peek_token()->value() == "(")) {
						// This is a type name followed by initializer: B{...} or B(...)
						const TypeSpecifierNode& init_type_spec = type_result.node()->as<TypeSpecifierNode>();

						if (peek_token()->value() == "{") {
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
							consume_token(); // consume '('
							std::vector<ASTNode> init_args;
							if (!peek_token().has_value() || peek_token()->value() != ")") {
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
							if (!consume_punctuator(")")) {
								return ParseResult::error("Expected ')' after initializer arguments", *current_token_);
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
						restore_token_position(saved_pos);
						auto init_result = parse_expression();
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							default_initializer = *init_result.node();
						}
					}
				} else {
					// Parse regular expression initializer
					auto init_result = parse_expression();
					if (init_result.is_error()) {
						return init_result;
					}
					if (init_result.node().has_value()) {
						default_initializer = *init_result.node();
					}
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

	// Check for variable declarations after struct definition: struct Point { ... } p, q;
	std::vector<ASTNode> struct_variables;
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
		// Parse variable declarators
		do {
			auto var_name_token = consume_token();
			if (!var_name_token.has_value()) {
				return ParseResult::error("Expected variable name after struct definition", *current_token_);
			}

			// Create a variable declaration node for this variable
			auto var_type_spec = emplace_node<TypeSpecifierNode>(
				Type::Struct,
				struct_type_info.type_index_,
				static_cast<unsigned char>(0),  // Size will be set later
				Token(Token::Type::Identifier, struct_name, var_name_token->line(), var_name_token->column(), var_name_token->file_index())
			);

			auto var_decl = emplace_node<DeclarationNode>(var_type_spec, *var_name_token);

			// Add to symbol table so it can be referenced later in the code
			gSymbolTable.insert(var_name_token->value(), var_decl);

			// Wrap in VariableDeclarationNode so it gets processed properly by code generator
			auto var_decl_node = emplace_node<VariableDeclarationNode>(var_decl, std::nullopt);

			struct_variables.push_back(var_decl_node);

		} while (peek_token().has_value() && peek_token()->value() == "," && consume_token());
	}

	// Expect semicolon after struct definition (and optional variable declarations)
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
		size_t referenced_size_bits = type_spec.size_in_bits();
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
				referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
				member_alignment = member_type_info->getStructInfo()->alignment;
			}
		}

		// For array members, multiply element size by array count
		if (decl.is_array() && decl.array_size().has_value()) {
			// Evaluate the array size expression to get the count
			ConstExpr::EvaluationContext ctx(gSymbolTable);
			auto eval_result = ConstExpr::Evaluator::evaluate(*decl.array_size(), ctx);
			
			if (eval_result.success) {
				size_t array_count = static_cast<size_t>(eval_result.as_int());
				member_size *= array_count;
				referenced_size_bits *= array_count;
			}
		}

		// Add member to struct layout with default initializer
		bool is_ref_member = type_spec.is_reference();
		bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
		if (is_ref_member) {
			member_size = sizeof(void*);
			referenced_size_bits = referenced_size_bits ? referenced_size_bits : (type_spec.size_in_bits());
			member_alignment = sizeof(void*);
		}
		struct_info->addMember(
			std::string(decl.identifier_token().value()),
			type_spec.type(),
			type_spec.type_index(),
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer,
			is_ref_member,
			is_rvalue_ref_member,
			referenced_size_bits
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
	// Skip implicit function generation for template classes (they'll be generated during instantiation)
	if (!has_user_defined_constructor && !parsing_template_class_) {
		// Create a default constructor node
		auto [default_ctor_node, default_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			struct_name,
			struct_name
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

	// If we're parsing a template class, parse the bodies now so they can be stored
	// with the template for later instantiation with parameter substitution
	if (parsing_template_class_) {
		// Save the current token position (right after the struct definition)
		TokenPosition position_after_struct = save_token_position();

		// Parse all delayed function bodies and attach them to the function nodes
		for (auto& delayed : delayed_function_bodies_) {
			// Restore template parameter context for this delayed body
			current_template_param_names_ = delayed.template_param_names;
			
			// Restore token position to the start of the function body
			restore_token_position(delayed.body_start);

			if (delayed.is_constructor && delayed.ctor_node) {
				gSymbolTable.enter_scope(ScopeType::Function);
				current_function_ = nullptr;
				member_function_context_stack_.push_back({
					delayed.struct_name,
					delayed.struct_type_index,
					delayed.struct_node
				});

				for (const auto& param : delayed.ctor_node->parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl = param.as<DeclarationNode>();
						gSymbolTable.insert(param_decl.identifier_token().value(), param);
					}
				}

				auto block_result = parse_block();
				if (block_result.is_error()) {
					current_template_param_names_.clear();
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					struct_parsing_context_stack_.pop_back();
					return block_result;
				}

				if (auto block = block_result.node()) {
					delayed.ctor_node->set_definition(*block);
				}

				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			} else if (delayed.is_destructor && delayed.dtor_node) {
				gSymbolTable.enter_scope(ScopeType::Function);
				current_function_ = nullptr;
				member_function_context_stack_.push_back({
					delayed.struct_name,
					delayed.struct_type_index,
					delayed.struct_node
				});

				auto block_result = parse_block();
				if (block_result.is_error()) {
					current_template_param_names_.clear();
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					struct_parsing_context_stack_.pop_back();
					return block_result;
				}

				if (auto block = block_result.node()) {
					delayed.dtor_node->set_definition(*block);
				}

				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			} else if (delayed.func_node) {
				// Parse regular member function body
				gSymbolTable.enter_scope(ScopeType::Function);

				// Set current function pointer
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
					std::cerr << "ERROR: Failed to parse template member function body: " 
					          << block_result.error_message() << "\n";
					// Clean up context
					current_function_ = nullptr;
					current_template_param_names_.clear();
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					struct_parsing_context_stack_.pop_back();
					return block_result;
				}
				
				if (block_result.node().has_value()) {
					// Attach body to function node - this will be copied during instantiation
					delayed.func_node->set_definition(*block_result.node());
				} else {
					std::cerr << "WARNING: parse_block returned success but no node for template member function\n";
				}

				// Clean up context
				current_function_ = nullptr;
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			}

			current_template_param_names_.clear();
		}

		// Restore position after struct
		restore_token_position(position_after_struct);

		// Clear the delayed bodies list
		delayed_function_bodies_.clear();
		return saved_position.success(struct_node);
	}


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
				delayed.ctor_node->set_definition(*block);
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
				delayed.dtor_node->set_definition(*block);
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
				delayed.func_node->set_definition(*block);
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

	// Store variable declarations for later processing
	// They will be added to the AST by the caller
	pending_struct_variables_ = std::move(struct_variables);

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

	// Parse enum name (optional for anonymous enums)
	std::string_view enum_name;
	//bool is_anonymous = false;
	
	// Check if next token is an identifier (name) or : or { (anonymous enum)
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
		auto name_token = consume_token();
		enum_name = name_token->value();
	} else if (peek_token().has_value() && 
	           (peek_token()->value() == ":" || peek_token()->value() == "{")) {
		// Anonymous enum - generate a unique name
		static int anonymous_enum_counter = 0;
		static std::string temp_string;
		enum_name = temp_string = "__anonymous_enum_" + std::to_string(anonymous_enum_counter++);
		//is_anonymous = true;
	} else {
		return ParseResult::error("Expected enum name, ':', or '{'", *peek_token());
	}

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

ParseResult Parser::parse_static_assert()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'static_assert' keyword
	auto static_assert_keyword = consume_token();
	if (!static_assert_keyword.has_value() || static_assert_keyword->value() != "static_assert") {
		return ParseResult::error("Expected 'static_assert' keyword", static_assert_keyword.value_or(Token()));
	}

	// Expect opening parenthesis
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' after 'static_assert'", *current_token_);
	}

	// Parse the condition expression
	ParseResult condition_result = parse_expression();
	if (condition_result.is_error()) {
		return condition_result;
	}

	// Check for optional comma and message
	std::string message;
	if (consume_punctuator(",")) {
		// Parse the message string literal
		if (peek_token().has_value() && peek_token()->type() == Token::Type::StringLiteral) {
			auto message_token = consume_token();
			if (message_token->value().size() >= 2 && 
			    message_token->value().front() == '"' && 
			    message_token->value().back() == '"') {
				// Extract the message content (remove quotes)
				message = std::string(message_token->value().substr(1, message_token->value().size() - 2));
			}
		} else {
			return ParseResult::error("Expected string literal for static_assert message", *current_token_);
		}
	}

	// Expect closing parenthesis
	if (!consume_punctuator(")")) {
		return ParseResult::error("Expected ')' after static_assert", *current_token_);
	}

	// Expect semicolon
	if (!consume_punctuator(";")) {
		return ParseResult::error("Expected ';' after static_assert", *current_token_);
	}

	// Evaluate the constant expression using ConstExprEvaluator
	ConstExpr::EvaluationContext ctx(gSymbolTable);
	auto eval_result = ConstExpr::Evaluator::evaluate(*condition_result.node(), ctx);
	
	if (!eval_result.success) {
		return ParseResult::error(
			"static_assert condition is not a constant expression: " + eval_result.error_message,
			*static_assert_keyword
		);
	}

	// Check if the assertion failed
	if (!eval_result.as_bool()) {
		std::string error_msg = "static_assert failed";
		if (!message.empty()) {
			error_msg += ": " + message;
		}
		return ParseResult::error(error_msg, *static_assert_keyword);
	}

	// static_assert passed - just skip it
	return saved_position.success();
}

ParseResult Parser::parse_typedef_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'typedef' keyword
	auto typedef_keyword = consume_token();
	if (!typedef_keyword.has_value() || typedef_keyword->value() != "typedef") {
		return ParseResult::error("Expected 'typedef' keyword", typedef_keyword.value_or(Token()));
	}

	// Check if this is an inline struct/class definition: typedef struct { ... } alias;
	// or typedef struct Name { ... } alias;
	bool is_inline_struct = false;
	std::string struct_name_for_typedef_storage;  // Storage for generated names
	std::string_view struct_name_for_typedef;
	TypeIndex struct_type_index = 0;

	if (peek_token().has_value() &&
	    (peek_token()->value() == "struct" || peek_token()->value() == "class")) {
		// Look ahead to see if this is an inline definition
		// Pattern 1: typedef struct { ... } alias;
		// Pattern 2: typedef struct Name { ... } alias;
		auto next_pos = current_token_;
		consume_token(); // consume 'struct' or 'class'

		// Check if next token is '{' (anonymous struct) or identifier followed by '{'
		if (peek_token().has_value() && peek_token()->value() == "{") {
			// Pattern 1: typedef struct { ... } alias;
			is_inline_struct = true;
			// Use a unique temporary name for the struct (will be replaced by typedef alias)
			// Use the current AST size to make it unique
			struct_name_for_typedef_storage = "__anonymous_typedef_struct_" + std::to_string(ast_nodes_.size());
			struct_name_for_typedef = struct_name_for_typedef_storage;
		} else if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
			auto struct_name_token = peek_token();
			consume_token(); // consume struct name

			if (peek_token().has_value() && peek_token()->value() == "{") {
				// Pattern 2: typedef struct Name { ... } alias;
				is_inline_struct = true;
				struct_name_for_typedef = struct_name_token->value();
			} else {
				// Not an inline definition, restore position and parse normally
				current_token_ = next_pos;
				is_inline_struct = false;
			}
		} else {
			// Not an inline definition, restore position and parse normally
			current_token_ = next_pos;
			is_inline_struct = false;
		}
	}

	ASTNode type_node;
	TypeSpecifierNode type_spec;

	if (is_inline_struct) {
		// Parse the inline struct definition
		// We need to manually parse the struct body since we already consumed the keyword and name

		// Register the struct type early
		TypeInfo& struct_type_info = add_struct_type(std::string(struct_name_for_typedef));
		struct_type_index = struct_type_info.type_index_;

		// Create struct declaration node
		auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name_for_typedef, false);

		// Push struct parsing context
		struct_parsing_context_stack_.push_back({struct_name_for_typedef, &struct_ref});

		// Create StructTypeInfo
		auto struct_info = std::make_unique<StructTypeInfo>(std::string(struct_name_for_typedef), AccessSpecifier::Public);

		// Apply pack alignment from #pragma pack
		size_t pack_alignment = context_.getCurrentPackAlignment();
		if (pack_alignment > 0) {
			struct_info->set_pack_alignment(pack_alignment);
		}

		// Expect opening brace
		if (!consume_punctuator("{")) {
			return ParseResult::error("Expected '{' in struct definition", *peek_token());
		}

		// Parse struct members (simplified version - no inheritance, no member functions for now)
		std::vector<StructMemberDecl> members;
		AccessSpecifier current_access = AccessSpecifier::Public;

		while (peek_token().has_value() && peek_token()->value() != "}") {
			// Parse member declaration
			auto member_type_result = parse_type_specifier();
			if (member_type_result.is_error()) {
				return member_type_result;
			}

			if (!member_type_result.node().has_value()) {
				return ParseResult::error("Expected type specifier in struct member", *current_token_);
			}

			// Parse member name
			auto member_name_token = consume_token();
			if (!member_name_token.has_value() || member_name_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected member name in struct", member_name_token.value_or(Token()));
			}

			// Create member declaration
			auto member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), *member_name_token);
			members.push_back({member_decl_node, current_access, std::nullopt});
			struct_ref.add_member(member_decl_node, current_access, std::nullopt);

			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after struct member", *current_token_);
			}
		}

		// Expect closing brace
		if (!consume_punctuator("}")) {
			return ParseResult::error("Expected '}' after struct members", *peek_token());
		}

		// Pop struct parsing context
		struct_parsing_context_stack_.pop_back();

		// Calculate struct layout
		for (const auto& member_decl : members) {
			const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
			const TypeSpecifierNode& member_type_spec = decl.type_node().as<TypeSpecifierNode>();

			size_t member_size = get_type_size_bits(member_type_spec.type()) / 8;
			size_t referenced_size_bits = member_type_spec.size_in_bits();
			size_t member_alignment = get_type_alignment(member_type_spec.type(), member_size);

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
				member_size = sizeof(void*);
				referenced_size_bits = referenced_size_bits ? referenced_size_bits : member_type_spec.size_in_bits();
				member_alignment = sizeof(void*);
			}
			struct_info->addMember(
				std::string(decl.identifier_token().value()),
				member_type_spec.type(),
				member_type_spec.type_index(),
				member_size,
				member_alignment,
				member_decl.access,
				member_decl.default_initializer,
				is_ref_member,
				is_rvalue_ref_member,
				referenced_size_bits
			);
		}

		// Finalize struct layout (add padding)
		struct_info->finalize();

		// Store struct info
		struct_type_info.setStructInfo(std::move(struct_info));

		// Create type specifier for the struct
		type_spec = TypeSpecifierNode(
			Type::Struct,
			struct_type_index,
			static_cast<unsigned char>(struct_type_info.getStructInfo()->total_size * 8),
			Token(Token::Type::Identifier, struct_name_for_typedef, 0, 0, 0)
		);
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else {
		// Parse the underlying type normally
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type specifier after 'typedef'", *current_token_);
		}

		type_node = *type_result.node();
		type_spec = type_node.as<TypeSpecifierNode>();

		// Handle pointer declarators (e.g., typedef int* IntPtr;)
		while (peek_token().has_value() && peek_token()->value() == "*") {
			consume_token(); // consume '*'
			type_spec.add_pointer_level();

			// Skip const/volatile and Microsoft-specific modifiers after *
			// const/volatile: CV-qualifiers on the pointer itself (e.g., int * const p)
			// Microsoft modifiers: See explanation in parse_type_specifier()
			while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
				std::string_view kw = peek_token()->value();
				if (kw == "const" || kw == "volatile" ||
				    kw == "__ptr32" || kw == "__ptr64" || kw == "__w64" ||
				    kw == "__unaligned" || kw == "__uptr" || kw == "__sptr") {
					consume_token();
				} else {
					break;
				}
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

		// Check if it's a using directive, using declaration, or namespace alias
		if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "using") {
			decl_result = parse_using_directive_or_declaration();
		}
		// Check if it's a nested namespace
		else if (peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "namespace") {
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

	// Check if this is a type alias or namespace alias: using identifier = ...;
	// We need to look ahead to see if there's an '=' after the first identifier
	TokenPosition lookahead_pos = save_token_position();
	auto first_token = peek_token();
	if (first_token.has_value() && first_token->type() == Token::Type::Identifier) {
		consume_token(); // consume identifier
		auto next_token = peek_token();
		if (next_token.has_value() && next_token->value() == "=") {
			// This is either a type alias or namespace alias: using alias = type/namespace;
			restore_token_position(lookahead_pos);

			// Parse alias name
			auto alias_token = consume_token();
			if (!alias_token.has_value() || alias_token->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected alias name after 'using'", *current_token_);
			}

			// Consume '='
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Operator || peek_token()->value() != "=") {
				return ParseResult::error("Expected '=' after alias name", *current_token_);
			}
			consume_token(); // consume '='

			// Try to parse as a type specifier (for type aliases like: using value_type = T;)
			ParseResult type_result = parse_type_specifier();
			if (!type_result.is_error()) {
				// This is a type alias
				if (!consume_punctuator(";")) {
					return ParseResult::error("Expected ';' after type alias", *current_token_);
				}

				// Register the type alias in gTypesByName
				if (type_result.node().has_value()) {
					const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
					
					// Create a TypeInfo for the alias that points to the underlying type
					std::string alias_name(alias_token->value());
					auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), gTypeInfo.size());
					alias_type_info.type_index_ = type_spec.type_index();
					alias_type_info.type_size_ = type_spec.size_in_bits();
					gTypesByName.emplace(alias_type_info.name_, &alias_type_info);
				}

				// Return success (no AST node needed for type aliases)
				return saved_position.success();
			}

			// Not a type alias, try parsing as namespace path for namespace alias
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = consume_token();
				if (!ns_token.has_value() || ns_token->type() != Token::Type::Identifier) {
					return ParseResult::error("Expected type or namespace name", ns_token.value_or(Token()));
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

	// Otherwise, this is a using declaration: using std::vector; or using ::name;
	std::vector<StringType<>> namespace_path;
	Token identifier_token;

	// Check if this starts with :: (global namespace scope)
	if (peek_token().has_value() && peek_token()->value() == "::") {
		consume_token(); // consume ::
		// For global namespace, we use an empty namespace_path
		// The identifier follows immediately
		auto token = consume_token();
		if (!token.has_value() || token->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier after :: in using declaration", token.value_or(Token()));
		}
		identifier_token = *token;
	} else {
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
		std::cerr << "DEBUG: parse_type_specifier returning early - invalid token\n";
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
		// Microsoft-specific type modifiers - consume and ignore
		// __ptr32/__ptr64: Pointer size modifiers (32-bit vs 64-bit) - not needed for x64-only target
		// __sptr/__uptr: Signed/unsigned pointer extension - only relevant for 32/64-bit mixing
		// __w64: Deprecated 64-bit portability warning marker
		// __unaligned: Alignment hint - doesn't affect type parsing
		else if (token_value == "__ptr32" || token_value == "__ptr64" ||
		         token_value == "__w64" || token_value == "__unaligned" ||
		         token_value == "__uptr" || token_value == "__sptr") {
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
				// Microsoft-specific type keywords
				{"__int8", {Type::Char, 8}},
				{"__int16", {Type::Short, 16}},
				{"__int32", {Type::Int, 32}},
				{"__int64", {Type::LongLong, 64}},
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
	else if (qualifier != TypeQualifier::None || long_count > 0) {
		// Handle cases like "unsigned", "signed", "long" without explicit type (defaults to int)
		// But NOT "const" alone - that could be "const Widget" which needs to continue
		// Examples: "unsigned" -> unsigned int, "signed" -> signed int, "long" -> long int

		if (long_count == 1) {
			// "long" or "const long" -> long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLong : Type::Long;
			type_size = sizeof(long) * 8;
		} else if (long_count == 2) {
			// "long long" or "const long long" -> long long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLongLong : Type::LongLong;
			type_size = 64;
		} else {
			// "unsigned", "signed" without type -> int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedInt : Type::Int;
			type_size = 32;
		}

		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, Token(), cv_qualifier));
	}
	// If we only have CV-qualifiers (const/volatile) without unsigned/signed/long,
	// continue parsing - could be "const Widget&", "const int*", etc.
	// The following checks handle struct/class keywords and identifiers
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

			// If this is a typedef to a struct (no struct_info but has type_index pointing to the actual struct),
			// follow the type_index to get the actual struct TypeInfo
			if (!struct_info && struct_type_info->type_index_ < gTypeInfo.size()) {
				const TypeInfo& actual_struct = gTypeInfo[struct_type_info->type_index_];
				if (actual_struct.isStruct() && actual_struct.getStructInfo()) {
					struct_type_info = &actual_struct;
					struct_info = actual_struct.getStructInfo();
				}
			}

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

		// Check for template arguments: Container<int>
		std::optional<std::vector<TemplateTypeArg>> template_args;
		auto next_token = peek_token();
		if (next_token.has_value() && next_token->value() == "<") {
			template_args = parse_explicit_template_arguments();
			// If parsing succeeded, check if this is an alias template first
			if (template_args.has_value()) {
				// Check if this is an alias template
				auto alias_opt = gTemplateRegistry.lookup_alias_template(type_name);
				if (alias_opt.has_value()) {
					const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
					
					// Substitute template arguments into the target type
					TypeSpecifierNode instantiated_type = alias_node.target_type_node();
					const auto& template_params = alias_node.template_parameters();
					const auto& param_names = alias_node.template_param_names();
					
					// Perform substitution for template parameters in the target type
					for (size_t i = 0; i < template_args->size() && i < param_names.size(); ++i) {
						const auto& arg = (*template_args)[i];
						std::string_view param_name = param_names[i];
						
						// Check if the target type refers to this template parameter
						// The target type will have Type::UserDefined and a type_index pointing to
						// the TypeInfo we created for the template parameter
						bool is_template_param = false;
						if (instantiated_type.type() == Type::UserDefined && instantiated_type.type_index() < gTypeInfo.size()) {
							const TypeInfo& ti = gTypeInfo[instantiated_type.type_index()];
							if (ti.name_ == param_name) {
								is_template_param = true;
							}
						}
						
						if (is_template_param) {
							// The target type is using this template parameter
							if (arg.is_value) {
								std::cerr << "ERROR: Non-type template arguments not supported in alias templates yet\n";
								return ParseResult::error("Non-type template arguments not supported in alias templates", type_name_token);
							}
							
							// Save pointer/reference modifiers from target type
							size_t ptr_depth = instantiated_type.pointer_depth();
							bool is_ref = instantiated_type.is_reference();
							bool is_rval_ref = instantiated_type.is_rvalue_reference();
							CVQualifier cv = instantiated_type.cv_qualifier();
							
							// Get the size in bits for the argument type
							unsigned char size_bits = 0;
							if (arg.base_type == Type::Struct || arg.base_type == Type::UserDefined) {
								// Look up the struct size from type_index
								if (arg.type_index < gTypeInfo.size()) {
									const TypeInfo& ti = gTypeInfo[arg.type_index];
									size_bits = static_cast<unsigned char>(ti.type_size_);
								}
							} else {
								// Use standard type sizes
								size_bits = static_cast<unsigned char>(get_type_size_bits(arg.base_type));
							}
							
							// Create new type with substituted base type
							instantiated_type = TypeSpecifierNode(
								arg.base_type,
								arg.type_index,
								size_bits,
								Token(),  // No token for instantiated type
								cv
							);
							
							// Reapply pointer/reference modifiers from target type
							// e.g., if target is T* and we substitute int for T, we get int*
							for (size_t p = 0; p < ptr_depth; ++p) {
								instantiated_type.add_pointer_level(CVQualifier::None);
							}
							if (is_rval_ref) {
								instantiated_type.set_reference(true);  // rvalue ref
							} else if (is_ref) {
								instantiated_type.set_lvalue_reference(true);  // lvalue ref
							}
						}
					}
					
					return ParseResult::success(emplace_node<TypeSpecifierNode>(instantiated_type));
				}
				
				auto instantiated_class = try_instantiate_class_template(type_name, *template_args);
				
				// If instantiation returned a struct node, add it to the AST so it gets visited during codegen
				if (instantiated_class.has_value() && instantiated_class->is<StructDeclarationNode>()) {
					ast_nodes_.push_back(*instantiated_class);
				}
				
				// Fill in default template arguments to get the actual instantiated name
				// (try_instantiate_class_template fills them internally, we need to do the same here)
				std::vector<TemplateTypeArg> filled_template_args = *template_args;
				auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
				if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					const auto& template_class = template_opt->as<TemplateClassDeclarationNode>();
					const auto& template_params = template_class.template_parameters();
					
					// Fill in defaults for missing parameters
					for (size_t i = filled_template_args.size(); i < template_params.size(); ++i) {
						const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
						if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
							const ASTNode& default_node = param.default_value();
							if (default_node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
								filled_template_args.push_back(TemplateTypeArg(default_type));
							}
						}
					}
				}
				
				// Whether instantiation succeeded or returned nullopt (for specializations),
				// the type should now be registered. Look it up using filled args.
				std::string_view instantiated_name = get_instantiated_class_name(type_name, filled_template_args);
				
				// Check for qualified name after template arguments: Template<T>::nested
				if (peek_token().has_value() && peek_token()->value() == "::") {
					// Parse the qualified identifier path (e.g., Template<int>::Inner)
					auto qualified_result = parse_qualified_identifier_after_template(type_name_token);
					if (qualified_result.is_error()) {
						std::cerr << "DEBUG: parse_qualified_identifier_after_template failed\n";
						return qualified_result;
					}
					
					// Build fully qualified type name using instantiated template name
					const auto& qualified_node = qualified_result.node()->as<QualifiedIdentifierNode>();
					std::string qualified_type_name(instantiated_name);
					for (const auto& ns_part : qualified_node.namespaces()) {
						// Skip the first part (template name itself) as it's already in instantiated_name
#if USE_OLD_STRING_APPROACH
						std::string_view ns_view = ns_part;
#else
						std::string_view ns_view = ns_part.view();
#endif
						if (ns_view != type_name) {
							qualified_type_name += "::" + std::string(ns_view);
						}
					}
					qualified_type_name += "::" + std::string(qualified_node.identifier_token().value());
					
					// Look up the fully qualified type (e.g., "Traits_int::nested")
					auto qual_type_it = gTypesByName.find(qualified_type_name);
					std::cerr << "DEBUG: Looking up qualified type '" << qualified_type_name << "': " 
					          << (qual_type_it != gTypesByName.end() ? "FOUND" : "NOT FOUND") << "\n";
					if (qual_type_it != gTypesByName.end()) {
						const TypeInfo* type_info = qual_type_it->second;
						
						// Handle both struct types and type aliases
						if (type_info->isStruct()) {
							const StructTypeInfo* struct_info = type_info->getStructInfo();

							if (struct_info) {
								type_size = static_cast<unsigned char>(struct_info->total_size * 8);
							} else {
								type_size = 0;
							}
							return ParseResult::success(emplace_node<TypeSpecifierNode>(
								Type::Struct, type_info->type_index_, type_size, type_name_token, cv_qualifier));
						} else {
							// This is a type alias - return the aliased type
							type_size = static_cast<unsigned char>(type_info->type_size_);
							return ParseResult::success(emplace_node<TypeSpecifierNode>(
								type_info->type_, type_info->type_index_, type_size, type_name_token, cv_qualifier));
						}
					}
					return ParseResult::error("Unknown nested type: " + qualified_type_name, type_name_token);
				}
				
				auto inst_type_it = gTypesByName.find(instantiated_name);
				if (inst_type_it != gTypesByName.end() && inst_type_it->second->isStruct()) {
					const TypeInfo* struct_type_info = inst_type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						type_size = static_cast<unsigned char>(struct_info->total_size * 8);
					} else {
						type_size = 0;
					}
					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
				}
				// If type not found, fall through to error handling below
			}
		}

		// Check if this is a template with all default parameters (e.g., Container instead of Container<>)
		auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
		if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
			const auto& template_class = template_opt->as<TemplateClassDeclarationNode>();
			const auto& template_params = template_class.template_parameters();
			
			// Check if all parameters have defaults
			bool all_have_defaults = true;
			for (const auto& param_node : template_params) {
				if (param_node.is<TemplateParameterNode>()) {
					const auto& param = param_node.as<TemplateParameterNode>();
					if (!param.has_default()) {
						all_have_defaults = false;
						break;
					}
				}
			}
			
			if (all_have_defaults) {
				// Instantiate with empty args - defaults will be filled in
				std::vector<TemplateTypeArg> empty_args;
				auto instantiated_class = try_instantiate_class_template(type_name, empty_args);
				
				// Fill in default template arguments to get the actual instantiated name
				std::vector<TemplateTypeArg> filled_template_args;
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
						const ASTNode& default_node = param.default_value();
						if (default_node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
							filled_template_args.push_back(TemplateTypeArg(default_type));
						}
					}
				}
				
				std::string_view instantiated_name = get_instantiated_class_name(type_name, filled_template_args);
				
				auto inst_type_it = gTypesByName.find(instantiated_name);
				if (inst_type_it != gTypesByName.end() && inst_type_it->second->isStruct()) {
					const TypeInfo* struct_type_info = inst_type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						type_size = static_cast<unsigned char>(struct_info->total_size * 8);
					} else {
						type_size = 0;
					}
					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
				}
			}
		}

		// Check if this is a registered struct type
		auto type_it = gTypesByName.find(type_name);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			// This is a struct type (or a typedef to a struct type)
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			// If this is a typedef to a struct (no struct_info but has type_index pointing to the actual struct),
			// follow the type_index to get the actual struct TypeInfo
			if (!struct_info && struct_type_info->type_index_ < gTypeInfo.size()) {
				const TypeInfo& actual_struct = gTypeInfo[struct_type_info->type_index_];
				if (actual_struct.isStruct() && actual_struct.getStructInfo()) {
					struct_type_info = &actual_struct;
					struct_info = actual_struct.getStructInfo();
				}
			}

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
			} else if (user_type_index < gTypeInfo.size()) {
				// Not a typedef - might be a struct type without size set in TypeInfo
				// Look up actual size from struct info if available
				const TypeInfo& actual_type_info = gTypeInfo[user_type_index];
				if (actual_type_info.isStruct()) {
					const StructTypeInfo* struct_info = actual_type_info.getStructInfo();
					if (struct_info) {
						type_size = static_cast<unsigned char>(struct_info->total_size * 8);
					}
				}
			}
		}
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			resolved_type, user_type_index, type_size, type_name_token, cv_qualifier));
	}

	std::string error_msg = "Unexpected token in type specifier";
	if (current_token_opt.has_value()) {
		error_msg += ": '";
		error_msg += current_token_opt->value();
		error_msg += "'";
	}
	return ParseResult::error(error_msg, current_token_opt.value_or(Token()));
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

ParseResult Parser::parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention)
{
	// Parse parameters
	if (!consume_punctuator("(")) {
		return ParseResult::error("Expected '(' for function parameter list",
			*current_token_);
	}

	// Create the function declaration
	auto [func_node, func_ref] =
		create_node_ref<FunctionDeclarationNode>(declaration_node);
	
	// Set calling convention immediately so it's available during parameter parsing
	func_ref.set_calling_convention(calling_convention);

	// Set linkage from current context (for extern "C" blocks)
	if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	}

	while (!consume_punctuator(")"sv)) {
		// Check for variadic parameter (...)
		if (peek_token().has_value() && peek_token()->value() == "...") {
			consume_token(); // consume '...'
			// Mark the function as variadic
			func_ref.set_is_variadic(true);
			
			// Validate calling convention for variadic functions
			// Only __cdecl and __vectorcall support variadic parameters (caller cleanup)
			CallingConvention cc = func_ref.calling_convention();
			if (cc != CallingConvention::Default && 
			    cc != CallingConvention::Cdecl && 
			    cc != CallingConvention::Vectorcall) {
				return ParseResult::error(
					"Variadic functions must use __cdecl or __vectorcall calling convention "
					"(other conventions use callee cleanup which is incompatible with variadic arguments)", 
					*current_token_);
			}
			
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

			// Parse the default value expression
			auto default_value = parse_expression();
			// Set the default value
		}

		if (consume_punctuator(","sv)) {
			// After a comma, check if the next token is '...' for variadic parameters
			if (peek_token().has_value() && peek_token()->value() == "...") {
				consume_token(); // consume '...'
				func_ref.set_is_variadic(true);
				
				// Validate calling convention for variadic functions
				// Only __cdecl and __vectorcall support variadic parameters (caller cleanup)
				CallingConvention cc = func_ref.calling_convention();
				if (cc != CallingConvention::Default && 
				    cc != CallingConvention::Cdecl && 
				    cc != CallingConvention::Vectorcall) {
					return ParseResult::error(
						"Variadic functions must use __cdecl or __vectorcall calling convention "
						"(other conventions use callee cleanup which is incompatible with variadic arguments)", 
						*current_token_);
				}
				
				if (!consume_punctuator(")"sv)) {
					return ParseResult::error("Expected ')' after variadic parameter '...'", *current_token_);
				}
				break;
			}
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

		// Add any pending variable declarations from struct definitions
		for (auto& var_node : pending_struct_variables_) {
			block_ref.add_statement_node(var_node);
		}
		pending_struct_variables_.clear();

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
			{"template", &Parser::parse_template_declaration},
			{"struct", &Parser::parse_struct_declaration},
			{"class", &Parser::parse_struct_declaration},
			{"static", &Parser::parse_variable_declaration},
			{"extern", &Parser::parse_variable_declaration},
			{"register", &Parser::parse_variable_declaration},
			{"mutable", &Parser::parse_variable_declaration},
			{"constexpr", &Parser::parse_variable_declaration},
			{"constinit", &Parser::parse_variable_declaration},
			{"consteval", &Parser::parse_variable_declaration},
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
			// Microsoft-specific type keywords
			{"__int8", &Parser::parse_variable_declaration},
			{"__int16", &Parser::parse_variable_declaration},
			{"__int32", &Parser::parse_variable_declaration},
			{"__int64", &Parser::parse_variable_declaration},
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
		
		// Check if this is a template identifier (e.g., Container<int>::Iterator)
		// Templates need to be parsed as variable declarations
		// UNLESS the next token is '(' (which indicates a function template call)
		bool is_template = gTemplateRegistry.lookupTemplate(type_name).has_value();
		bool is_alias_template = gTemplateRegistry.lookup_alias_template(type_name).has_value();
		
		if (is_template || is_alias_template) {
			// We need to consume the identifier to peek at what comes after
			consume_token(); // consume the identifier
			// Peek ahead to see if this is a function call (template_name(...))
			// or a variable declaration (template_name<...> var)
			if (peek_token().has_value()) {
				if (peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == "(") {
					// Restore position before the identifier so parse_expression can handle it
					restore_token_position(saved_pos);
					// This is a function call, parse as expression
					return parse_expression();
				}
			}
			std::cerr << "DEBUG: Parsing as variable declaration\n";
			// Restore position before the identifier so parse_variable_declaration can handle it
			restore_token_position(saved_pos);
			// Otherwise, it's a variable declaration with a template type
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
	// Check for constexpr/constinit keywords (C++11/C++20)
	bool is_constexpr = false;
	bool is_constinit = false;
	
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view keyword = peek_token()->value();
		if (keyword == "constexpr") {
			is_constexpr = true;
			consume_token();
		} else if (keyword == "constinit") {
			is_constinit = true;
			consume_token();
		}
	}
	
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

	// Check again for constexpr/constinit after storage class (handles "static constinit" order)
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view keyword = peek_token()->value();
		if (keyword == "constexpr") {
			is_constexpr = true;
			consume_token();
		} else if (keyword == "constinit") {
			is_constinit = true;
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

	if (first_init_expr.has_value() && first_init_expr->is<InitializerListNode>()) {
		try_apply_deduction_guides(type_specifier, first_init_expr->as<InitializerListNode>());
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
					// Parse expression with precedence > comma operator (precedence 1)
					// This prevents comma from being treated as an operator in declaration lists
					ParseResult init_expr_result = parse_expression(2);
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

			// Parse the initializer expression with precedence > comma operator (precedence 1)
			// This prevents comma from being treated as an operator in initializer lists
			ParseResult init_expr_result = parse_expression(2);
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

			// Parse the initializer expression with precedence > comma operator (precedence 1)
			// This prevents comma from being treated as an operator in initializer lists
			ParseResult init_expr_result = parse_expression(2);
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

bool Parser::try_apply_deduction_guides(TypeSpecifierNode& type_specifier, const InitializerListNode& init_list)
{
	if (init_list.has_any_designated()) {
		return false;
	}

	if (type_specifier.type() != Type::UserDefined && type_specifier.type() != Type::Struct) {
		return false;
	}

	std::string_view class_name = type_specifier.token().value();
	if (class_name.empty()) {
		return false;
	}

	if (!gTemplateRegistry.lookupTemplate(class_name).has_value()) {
		return false;
	}

	auto guide_nodes = gTemplateRegistry.lookup_deduction_guides(class_name);
	if (guide_nodes.empty()) {
		return false;
	}

	std::vector<TypeSpecifierNode> argument_types;
	argument_types.reserve(init_list.initializers().size());
	for (const auto& arg_expr : init_list.initializers()) {
		auto arg_type_opt = get_expression_type(arg_expr);
		if (!arg_type_opt.has_value()) {
			return false;
		}
		argument_types.push_back(*arg_type_opt);
	}

	std::vector<TemplateTypeArg> deduced_args;
	for (const auto& guide_node : guide_nodes) {
		if (!guide_node.is<DeductionGuideNode>()) {
			continue;
		}
		const auto& guide = guide_node.as<DeductionGuideNode>();
		if (deduce_template_arguments_from_guide(guide, argument_types, deduced_args)) {
			if (instantiate_deduced_template(class_name, deduced_args, type_specifier)) {
				return true;
			}
		}
	}

	return false;
}

bool Parser::deduce_template_arguments_from_guide(const DeductionGuideNode& guide,
	const std::vector<TypeSpecifierNode>& argument_types,
	std::vector<TemplateTypeArg>& out_template_args) const
{
	if (guide.guide_parameters().size() != argument_types.size()) {
		return false;
	}

	std::unordered_map<std::string_view, const TemplateParameterNode*> template_params;
	for (const auto& param_node : guide.template_parameters()) {
		if (!param_node.is<TemplateParameterNode>()) {
			continue;
		}
		const auto& tparam = param_node.as<TemplateParameterNode>();
		if (tparam.kind() == TemplateParameterKind::Type) {
			template_params.emplace(tparam.name(), &tparam);
		}
	}

	std::unordered_map<std::string_view, TypeSpecifierNode> bindings;
	for (size_t i = 0; i < guide.guide_parameters().size(); ++i) {
		if (!guide.guide_parameters()[i].is<TypeSpecifierNode>()) {
			return false;
		}
		const auto& param_type = guide.guide_parameters()[i].as<TypeSpecifierNode>();
		const auto& arg_type = argument_types[i];
		if (!match_template_parameter_type(param_type, arg_type, template_params, bindings)) {
			return false;
		}
	}

	out_template_args.clear();
	out_template_args.reserve(guide.deduced_template_args_nodes().size());
	for (const auto& rhs_node : guide.deduced_template_args_nodes()) {
		if (!rhs_node.is<TypeSpecifierNode>()) {
			return false;
		}
		const auto& rhs_type = rhs_node.as<TypeSpecifierNode>();
		auto placeholder = extract_template_param_name(rhs_type, template_params);
		if (placeholder.has_value()) {
			auto binding_it = bindings.find(*placeholder);
			if (binding_it == bindings.end()) {
				return false;
			}
			out_template_args.emplace_back(binding_it->second);
			continue;
		}

		out_template_args.emplace_back(rhs_type);
	}

	return !out_template_args.empty();
}

bool Parser::match_template_parameter_type(TypeSpecifierNode param_type,
	TypeSpecifierNode argument_type,
	const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params,
	std::unordered_map<std::string_view, TypeSpecifierNode>& bindings) const
{
	auto bind_placeholder = [&](std::string_view name, const TypeSpecifierNode& deduced_type) {
		auto [it, inserted] = bindings.emplace(name, deduced_type);
		if (!inserted && !types_equivalent(it->second, deduced_type)) {
			return false;
		}
		return true;
	};

	if (param_type.is_reference()) {
		bool requires_rvalue = param_type.is_rvalue_reference();
		if (requires_rvalue && argument_type.is_reference() && !argument_type.is_rvalue_reference()) {
			return false;
		}
		param_type.set_lvalue_reference(false);
		if (argument_type.is_reference()) {
			argument_type.set_lvalue_reference(false);
		}
	}

	while (param_type.pointer_depth() > 0) {
		if (argument_type.pointer_depth() == 0) {
			return false;
		}
		const auto& param_level = param_type.pointer_levels().back();
		const auto& arg_level = argument_type.pointer_levels().back();
		if (param_level.cv_qualifier != arg_level.cv_qualifier) {
			return false;
		}
		param_type.remove_pointer_level();
		argument_type.remove_pointer_level();
	}

	auto placeholder = extract_template_param_name(param_type, template_params);
	if (placeholder.has_value()) {
		return bind_placeholder(*placeholder, argument_type);
	}

	return types_equivalent(param_type, argument_type);
}

std::optional<std::string_view> Parser::extract_template_param_name(const TypeSpecifierNode& type_spec,
	const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params) const
{
	if (!template_params.empty()) {
		std::string_view token_name = type_spec.token().value();
		if (!token_name.empty()) {
			auto it = template_params.find(token_name);
			if (it != template_params.end()) {
				return it->first;
			}
		}
	}

	if (type_spec.type_index() < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
		auto it = template_params.find(type_info.name_);
		if (it != template_params.end()) {
			return it->first;
		}
	}

	return std::nullopt;
}

bool Parser::types_equivalent(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) const
{
	if (lhs.type() != rhs.type()) return false;
	if (lhs.type_index() != rhs.type_index()) return false;
	if (lhs.cv_qualifier() != rhs.cv_qualifier()) return false;
	if (lhs.pointer_depth() != rhs.pointer_depth()) return false;
	if (lhs.is_reference() != rhs.is_reference()) return false;
	if (lhs.is_rvalue_reference() != rhs.is_rvalue_reference()) return false;

	const auto& lhs_levels = lhs.pointer_levels();
	const auto& rhs_levels = rhs.pointer_levels();
	for (size_t i = 0; i < lhs_levels.size(); ++i) {
		if (lhs_levels[i].cv_qualifier != rhs_levels[i].cv_qualifier) {
			return false;
		}
	}

	return true;
}

bool Parser::instantiate_deduced_template(std::string_view class_name,
	const std::vector<TemplateTypeArg>& template_args,
	TypeSpecifierNode& type_specifier)
{
	if (template_args.empty()) {
		return false;
	}

	auto instantiated_class = try_instantiate_class_template(class_name, template_args);
	if (instantiated_class.has_value() && instantiated_class->is<StructDeclarationNode>()) {
		ast_nodes_.push_back(*instantiated_class);
	}

	std::string_view instantiated_name = get_instantiated_class_name(class_name, template_args);
	auto type_it = gTypesByName.find(instantiated_name);
	if (type_it == gTypesByName.end() || !type_it->second->isStruct()) {
		return false;
	}

	const TypeInfo* struct_type_info = type_it->second;
	unsigned char size_bits = 0;
	if (const StructTypeInfo* struct_info = struct_type_info->getStructInfo()) {
		size_bits = static_cast<unsigned char>(struct_info->total_size * 8);
	}

	TypeSpecifierNode resolved(Type::Struct, struct_type_info->type_index_, size_bits, type_specifier.token(), type_specifier.cv_qualifier());
	resolved.copy_indirection_from(type_specifier);
	type_specifier = resolved;
	return true;
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
		// Check if the current token is a binary operator or comma (which can be an operator)
		bool is_operator = peek_token()->type() == Token::Type::Operator;
		bool is_comma = peek_token()->type() == Token::Type::Punctuator && peek_token()->value() == ",";

		if (!is_operator && !is_comma) {
			break;
		}

		// Skip pack expansion operator '...' - it should be handled by the caller (e.g., function call argument parsing)
		if (peek_token()->value() == "...") {
			break;
		}

		// Skip ternary operator '?' - it's handled separately below
		if (is_operator && peek_token()->value() == "?") {
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

	// Check for ternary operator (condition ? true_expr : false_expr)
	// Ternary has precedence 5 (between assignment=3 and logical-or=7)
	// Only parse ternary if we're at a precedence level that allows it
	if (precedence <= 5 && peek_token().has_value() && 
	    peek_token()->type() == Token::Type::Operator && peek_token()->value() == "?") {
		consume_token();  // Consume '?'
		Token question_token = *current_token_;  // Save the '?' token

		// Parse the true expression (allow lower precedence on the right)
		ParseResult true_result = parse_expression(0);
		if (true_result.is_error()) {
			return true_result;
		}

		// Expect ':'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Punctuator || peek_token()->value() != ":") {
			return ParseResult::error("Expected ':' in ternary operator", *current_token_);
		}
		consume_token();  // Consume ':'

		// Parse the false expression (use precedence 5 for right-associativity)
		ParseResult false_result = parse_expression(5);
		if (false_result.is_error()) {
			return false_result;
		}

		if (auto condition_node = result.node()) {
			if (auto true_node = true_result.node()) {
				if (auto false_node = false_result.node()) {
					// Create the ternary operator node
					auto ternary_op = emplace_node<ExpressionNode>(
						TernaryOperatorNode(*condition_node, *true_node, *false_node, question_token));
					result = ParseResult::success(ternary_op);
				}
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
			// Ternary conditional (precedence 5, handled specially in parse_expression)
			{"?", 5},
			// Assignment operators (precedence 3, right-associative, lowest precedence)
			{"=", 3}, {"+=", 3}, {"-=", 3}, {"*=", 3}, {"/=", 3},
			{"%=", 3}, {"&=", 3}, {"|=", 3}, {"^=", 3},
			{"<<=", 3}, {">>=", 3},
			// Comma operator (precedence 1, lowest precedence)
			{",", 1},
	};

	auto it = precedence_map.find(op);
	if (it != precedence_map.end()) {
		return it->second;
	}
	else {
		// Log warning for unknown operators to help debugging
		std::cerr << "WARNING: Unknown operator '" << op << "' in get_operator_precedence, returning 0\n";
		return 0;
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

// Skip C++ standard attributes like [[nodiscard]], [[maybe_unused]], etc.
void Parser::skip_cpp_attributes()
{
	while (peek_token().has_value() && peek_token()->value() == "[") {
		auto next = peek_token(1);
		if (next.has_value() && next->value() == "[") {
			// Found [[
			consume_token(); // consume first [
			consume_token(); // consume second [
			
			// Skip everything until ]]
			int bracket_depth = 2;
			while (peek_token().has_value() && bracket_depth > 0) {
				if (peek_token()->value() == "[") {
					bracket_depth++;
				} else if (peek_token()->value() == "]") {
					bracket_depth--;
				}
				consume_token();
			}
		} else {
			break; // Not [[, stop
		}
	}
}

// Parse Microsoft __declspec(...) attributes and return linkage
Linkage Parser::parse_declspec_attributes()
{
	Linkage linkage = Linkage::None;
	
	while (peek_token().has_value() && peek_token()->value() == "__declspec") {
		consume_token(); // consume "__declspec"
		
		if (!consume_punctuator("(")) {
			return linkage; // Invalid __declspec, return what we have
		}
		
		// Parse the declspec specifier(s)
		while (peek_token().has_value() && peek_token()->value() != ")") {
			if (peek_token()->type() == Token::Type::Identifier) {
				std::string_view spec = peek_token()->value();
				if (spec == "dllimport") {
					linkage = Linkage::DllImport;
				} else if (spec == "dllexport") {
					linkage = Linkage::DllExport;
				}
				// else: ignore other declspec attributes like align, deprecated, etc.
				consume_token();
			} else if (peek_token()->value() == "(") {
				// Skip nested parens like __declspec(align(16))
				int paren_depth = 1;
				consume_token();
				while (peek_token().has_value() && paren_depth > 0) {
					if (peek_token()->value() == "(") {
						paren_depth++;
					} else if (peek_token()->value() == ")") {
						paren_depth--;
					}
					consume_token();
				}
			} else {
				consume_token(); // Skip other tokens
			}
		}
		
		if (!consume_punctuator(")")) {
			return linkage; // Missing closing paren
		}
	}
	
	return linkage;
}

// Parse calling convention keywords and return the calling convention
CallingConvention Parser::parse_calling_convention()
{
	CallingConvention calling_conv = CallingConvention::Default;
	
	while (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
		std::string_view token_val = peek_token()->value();
		
		// Look up calling convention in the mapping table using ranges
		auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
		if (it != std::end(calling_convention_map)) {
			calling_conv = it->convention;  // Last one wins if multiple specified
			consume_token();
		} else {
			break;  // Not a calling convention keyword, stop scanning
		}
	}
	
	return calling_conv;
}

// Parse all types of attributes (both C++ standard and Microsoft-specific)
Parser::AttributeInfo Parser::parse_attributes()
{
	AttributeInfo info;
	
	skip_cpp_attributes();  // C++ attributes don't affect linkage
	info.linkage = parse_declspec_attributes();
	info.calling_convention = parse_calling_convention();
	
	// Handle potential interleaved attributes (e.g., __declspec(...) [[nodiscard]] __declspec(...))
	if (peek_token().has_value() && peek_token()->value() == "[") {
		// Recurse to handle more attributes (prefer more specific linkage)
		AttributeInfo more_info = parse_attributes();
		if (more_info.linkage != Linkage::None) {
			info.linkage = more_info.linkage;
		}
		if (more_info.calling_convention != CallingConvention::Default) {
			info.calling_convention = more_info.calling_convention;
		}
	}
	
	return info;
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
	// Check for global namespace scope operator :: at the beginning
	else if (current_token_->type() == Token::Type::Punctuator && current_token_->value() == "::") {
		consume_token(); // consume ::

		// Expect an identifier after ::
		if (!current_token_.has_value() || current_token_->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier after '::'", current_token_.value_or(Token()));
		}

		Token first_identifier = *current_token_;
		consume_token(); // consume identifier

		// Helper to get DeclarationNode from either DeclarationNode or FunctionDeclarationNode
		auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
			if (node.is<DeclarationNode>()) {
				return &node.as<DeclarationNode>();
			} else if (node.is<FunctionDeclarationNode>()) {
				return &node.as<FunctionDeclarationNode>().decl_node();
			}
			return nullptr;
		};

		// Check if there are more :: following (e.g., ::ns::func)
		std::vector<StringType<32>> namespaces;
		Token final_identifier = first_identifier;

		while (current_token_.has_value() && current_token_->value() == "::"sv) {
			// Current identifier is a namespace part
			namespaces.emplace_back(StringType<32>(final_identifier.value()));
			consume_token(); // consume ::

			// Get next identifier
			if (!current_token_.has_value() || current_token_->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after '::'", current_token_.value_or(Token()));
			}
			final_identifier = *current_token_;
			consume_token(); // consume the identifier
		}

		// Create a QualifiedIdentifierNode with empty namespace path for global scope
		// If namespaces is empty, it means ::identifier (global namespace)
		// If namespaces is not empty, it means ::ns::identifier
		auto qualified_node = emplace_node<QualifiedIdentifierNode>(namespaces, final_identifier);
		const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

		// Try to look up the qualified identifier
		// For global namespace (empty namespaces), we need to look in the global scope
		std::optional<ASTNode> identifierType;
		if (namespaces.empty()) {
			// Global namespace - look up in global symbol table
			identifierType = lookup_symbol(qual_id.name());
		} else {
			// Qualified with namespace - use lookup_qualified
			identifierType = lookup_symbol_qualified(qual_id.namespaces(), qual_id.name());
		}

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
			// Just a qualified identifier reference (e.g., ::globalValue)
			result = emplace_node<ExpressionNode>(qual_id);
		}

		if (result.has_value())
			return ParseResult::success(*result);
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

		// Check for std::forward intrinsic
		// std::forward<T>(arg) is a compiler intrinsic for perfect forwarding
		if (qual_id.namespaces().size() == 1 && 
		    qual_id.namespaces()[0] == "std" && 
		    qual_id.name() == "forward") {
			
			// Handle std::forward<T>(arg)
			// For now, we'll treat it as an identity function that preserves references
			// Skip template arguments if present
			if (current_token_.has_value() && current_token_->value() == "<") {
				// Skip template arguments: <T>
				int angle_bracket_depth = 1;
				consume_token(); // consume <
				
				while (angle_bracket_depth > 0 && current_token_.has_value()) {
					if (current_token_->value() == "<") angle_bracket_depth++;
					else if (current_token_->value() == ">") angle_bracket_depth--;
					consume_token();
				}
			}
			
			// Now expect (arg)
			if (!current_token_.has_value() || current_token_->value() != "(") {
				return ParseResult::error("Expected '(' after std::forward", final_identifier);
			}
			consume_token(); // consume '('
			
			// Parse the single argument
			auto arg_result = parse_expression();
			if (arg_result.is_error()) {
				return arg_result;
			}
			
			if (!current_token_.has_value() || current_token_->value() != ")") {
				return ParseResult::error("Expected ')' after std::forward argument", *current_token_);
			}
			consume_token(); // consume ')'
			
			// std::forward<T>(arg) is essentially an identity function
			// Just return the argument expression itself
			// The type system already preserves the reference type
			result = arg_result.node();
			return ParseResult::success(*result);
		}

		// Try to look up the qualified identifier
		auto identifierType = gSymbolTable.lookup_qualified(qual_id.namespaces(), qual_id.name());			// Check if followed by '(' for function call
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
						
						// Check for pack expansion: expr...
						if (peek_token().has_value() && peek_token()->value() == "...") {
							consume_token(); // consume '...'
							
							// Pack expansion: need to expand the expression for each pack element
							// Strategy: Try to find expanded pack elements in the symbol table
							
							if (auto arg_node = arg_result.node()) {
								// Simple case: if the expression is just a single identifier that looks
								// like a pack parameter, try to expand it
								if (arg_node->is<IdentifierNode>()) {
									std::string_view pack_name = arg_node->as<IdentifierNode>().name();
									
									// Try to find pack_name_0, pack_name_1, etc. in the symbol table
									size_t pack_size = 0;
									std::string base_name(pack_name);
									
									for (size_t i = 0; i < 100; ++i) {  // reasonable limit
										// Use StringBuilder to create a persistent string
										std::string_view element_name = StringBuilder()
											.append(base_name)
											.append("_")
											.append(static_cast<int>(i))
											.commit();
										
										if (gSymbolTable.lookup(element_name).has_value()) {
											++pack_size;
										} else {
											break;
										}
									}
									
									if (pack_size > 0) {
										// Add each pack element as a separate argument
										for (size_t i = 0; i < pack_size; ++i) {
											// Use StringBuilder to create a persistent string for the token
											std::string_view element_name = StringBuilder()
												.append(base_name)
												.append("_")
												.append(static_cast<int>(i))
												.commit();
											
											Token elem_token(Token::Type::Identifier, element_name, 0, 0, 0);
											auto elem_node = emplace_node<ExpressionNode>(IdentifierNode(elem_token));
											args.push_back(elem_node);
										}
									} else {
										args.push_back(*arg_node);
									}
								} else {
									// Complex expression: need full rewriting (not implemented yet)
									args.push_back(*arg_node);
								}
							}
						} else {
							// Regular argument
							if (auto arg = arg_result.node()) {
								args.push_back(*arg);
							}
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
		// Use template-aware lookup if we're parsing a template body
		std::optional<ASTNode> identifierType;
		if (parsing_template_body_ && !current_template_param_names_.empty()) {
			identifierType = gSymbolTable.lookup(idenfifier_token.value(), gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
		} else {
			identifierType = lookup_symbol(idenfifier_token.value());
		}
		
		// If the identifier is a template parameter reference, wrap it in ExpressionNode and return immediately
		if (identifierType.has_value() && identifierType->is<TemplateParameterReferenceNode>()) {
			const auto& tparam_ref = identifierType->as<TemplateParameterReferenceNode>();
			
			// Check if it's followed by anything that would make it part of a larger expression
			// For now, just wrap it and return
			result = emplace_node<ExpressionNode>(tparam_ref);
			return ParseResult::success(*result);
		}
		
		// Special case: if the identifier is not found but is followed by '...', 
		// it might be a pack parameter that was expanded (e.g., "args" -> "args_0", "args_1", etc.)
		// Allow it to proceed so pack expansion can handle it
		bool is_pack_expansion = false;
		if (!identifierType.has_value() && peek_token().has_value() && peek_token()->value() == "...") {
			is_pack_expansion = true;
		}

		// Check if this is a template function call
		if (identifierType && identifierType->is<TemplateFunctionDeclarationNode>() &&
		    consume_punctuator("("sv)) {
			
			// Parse arguments to deduce template parameters
			if (!peek_token().has_value())
				return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

			ChunkedVector<ASTNode> args;
			std::vector<TypeSpecifierNode> arg_types;
			
			while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")") {
				ParseResult argResult = parse_expression();
				if (argResult.is_error()) {
					return argResult;
				}

				if (auto node = argResult.node()) {
					args.push_back(*node);
					
					// Try to deduce the type of this argument
					if (node->is<ExpressionNode>()) {
						const auto& expr = node->as<ExpressionNode>();
						Type arg_type = Type::Int;  // Default assumption
						bool is_lvalue = false;  // Track if this is an lvalue for perfect forwarding
					
						std::visit([&](const auto& inner) {
							using T = std::decay_t<decltype(inner)>;
							if constexpr (std::is_same_v<T, NumericLiteralNode>) {
								arg_type = inner.type();
								// Literals are rvalues
							} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
								arg_type = Type::Char;  // const char*
								// String literals are lvalues (but typically decay to pointers)
							} else if constexpr (std::is_same_v<T, IdentifierNode>) {
								// Look up the identifier's type
								auto id_type = lookup_symbol(inner.name());
								if (id_type.has_value()) {
									if (id_type->template is<DeclarationNode>()) {
										const auto& decl = id_type->template as<DeclarationNode>();
										if (decl.type_node().template is<TypeSpecifierNode>()) {
											arg_type = decl.type_node().template as<TypeSpecifierNode>().type();
											// Named variables are lvalues
											is_lvalue = true;
										}
									}
								}
							}
						}, expr);
					
						TypeSpecifierNode arg_type_node(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token());
						if (is_lvalue) {
							// Mark as lvalue reference for perfect forwarding template deduction
							arg_type_node.set_lvalue_reference(true);
						}
						arg_types.push_back(arg_type_node);
					}
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

			// Try to instantiate the template function
			auto template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
			
			if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
				const auto& func = template_func_inst->as<FunctionDeclarationNode>();
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(const_cast<DeclarationNode&>(func.decl_node()), std::move(args), idenfifier_token));
				return ParseResult::success(*result);
			} else {
				std::cerr << "DEBUG: Template instantiation failed\n";
				return ParseResult::error("Failed to instantiate template function", idenfifier_token);
			}
		}

		if (!identifierType) {
			// Check if this is a template function before treating it as missing
			if (current_token_.has_value() && current_token_->value() == "(" &&
			    gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
				// Don't set identifierType - fall through to the function call handling below
				// which will trigger template instantiation
			}
			// If we're inside a member function, check if this is a member variable
			else if (!member_function_context_stack_.empty()) {
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
				} else if (context.struct_type_index != 0) {
					// struct_node is null, but we have struct_type_index
					// This happens during template body parsing where we don't have access to struct_node
					// Look up the struct type from the type system
					if (context.struct_type_index < gTypeInfo.size()) {
						const TypeInfo& struct_type_info = gTypeInfo[context.struct_type_index];
						const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
						
						if (struct_info) {
							// Check if the identifier is a member of this struct
							for (const auto& member : struct_info->members) {
								if (member.name == idenfifier_token.value()) {
									// This is a member variable! Transform it into this->member
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
							
							// Also check base class members
							const StructMember* member = struct_info->findMemberRecursive(std::string(idenfifier_token.value()));
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

			// Check if this is a function call or constructor call (forward reference)
			// Identifier already consumed at line 1621
			if (consume_punctuator("("sv)) {
				// First, check if this is a type name (constructor call)
				auto type_it = gTypesByName.find(std::string(idenfifier_token.value()));
				if (type_it != gTypesByName.end()) {
					// This is a constructor call: TypeName(args)
					// Parse constructor arguments
					ChunkedVector<ASTNode> args;
					while (current_token_.has_value() && 
					       (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")")) {
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
							return ParseResult::error("Expected ',' or ')' after constructor argument", *current_token_);
						}
					}
					
					if (!consume_punctuator(")")) {
						std::cerr << "DEBUG: Failed to consume ')' after constructor arguments, current token: " 
						          << current_token_->value() << "\n";
						return ParseResult::error("Expected ')' after constructor arguments", *current_token_);
					}
					
					// Create TypeSpecifierNode for the constructor call
					TypeIndex type_index = type_it->second->type_index_;
					auto type_spec_node = emplace_node<TypeSpecifierNode>(
						Type::UserDefined, TypeQualifier::None, type_index, idenfifier_token);
					
					result = emplace_node<ExpressionNode>(
						ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
					
					return ParseResult::success(*result);
				}
				
				// Not a constructor - check if this is a template function that needs instantiation
				std::optional<ASTNode> template_func_inst;
				if (gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
					// Parse arguments to deduce template parameters
					if (!peek_token().has_value())
						return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

					ChunkedVector<ASTNode> args;
					std::vector<TypeSpecifierNode> arg_types;
					
					while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")") {
						ParseResult argResult = parse_expression();
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
									if constexpr (std::is_same_v<T, NumericLiteralNode>) {
										arg_type = inner.type();
									} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
										arg_type = Type::Char;  // const char*
									} else if constexpr (std::is_same_v<T, IdentifierNode>) {
										// Look up the identifier's type
										auto id_type = lookup_symbol(inner.name());
										if (id_type.has_value()) {
											if (id_type->template is<DeclarationNode>()) {
												const auto& decl = id_type->template as<DeclarationNode>();
												if (decl.type_node().template is<TypeSpecifierNode>()) {
													arg_type = decl.type_node().template as<TypeSpecifierNode>().type();
												}
											}
										}
									}
								}, expr);
								
								arg_types.emplace_back(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token());
							}
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

					// Try to instantiate the template function
					template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
					
					if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
						const auto& func = template_func_inst->as<FunctionDeclarationNode>();
						result = emplace_node<ExpressionNode>(
							FunctionCallNode(const_cast<DeclarationNode&>(func.decl_node()), std::move(args), idenfifier_token));
						return ParseResult::success(*result);
					} else {
						std::cerr << "DEBUG: Template instantiation failed or didn't return FunctionDeclarationNode\n";
						// Fall through to forward declaration
					}
				}
				
				// Not a template function, or instantiation failed
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

					// Check for pack expansion: expr...
					if (peek_token().has_value() && peek_token()->value() == "...") {
						consume_token(); // consume '...'
						
						// Pack expansion: need to expand the expression for each pack element
						if (auto arg_node = argResult.node()) {
							// Simple case: if the expression is just a single identifier that looks
							// like a pack parameter, try to expand it
							if (arg_node->is<IdentifierNode>()) {
								std::string_view pack_name = arg_node->as<IdentifierNode>().name();
								
								// Try to find pack_name_0, pack_name_1, etc. in the symbol table
								size_t pack_size = 0;
								std::string base_name(pack_name);
								
								for (size_t i = 0; i < 100; ++i) {  // reasonable limit
									// Use StringBuilder to create a persistent string
									std::string_view element_name = StringBuilder()
										.append(base_name)
										.append("_")
										.append(static_cast<int>(i))
										.commit();
									
									if (gSymbolTable.lookup(element_name).has_value()) {
										++pack_size;
									} else {
										break;
									}
								}
								
								if (pack_size > 0) {
									// Add each pack element as a separate argument
									for (size_t i = 0; i < pack_size; ++i) {
										// Use StringBuilder to create a persistent string for the token
										std::string_view element_name = StringBuilder()
											.append(base_name)
											.append("_")
											.append(static_cast<int>(i))
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
								// Complex expression: need full rewriting (not implemented yet)
								std::cerr << "DEBUG: Complex pack expansion not yet implemented\n";
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
				// Not a function call - could be a template with `<` or just missing identifier
				// Check if this might be a template: identifier<...>
				if (peek_token().has_value() && peek_token()->value() == "<") {
					// Try to parse as template instantiation with member access
					auto explicit_template_args = parse_explicit_template_arguments();
					
					if (explicit_template_args.has_value()) {
						// Successfully parsed template arguments
						// Now check for :: to handle Template<T>::member syntax
						if (peek_token().has_value() && peek_token()->value() == "::") {
							// Instantiate the template to get the actual instantiated name
							std::string_view template_name = idenfifier_token.value();
							std::string_view instantiated_name = get_instantiated_class_name(template_name, *explicit_template_args);
							try_instantiate_class_template(template_name, *explicit_template_args);
							
							// Parse qualified identifier after template, using the instantiated name
							// We need to collect the :: path ourselves since we have the instantiated name
							std::vector<StringType<32>> namespaces;
							Token final_identifier = idenfifier_token;
							
							// Collect the qualified path after ::
							while (peek_token().has_value() && peek_token()->value() == "::") {
								// Current identifier becomes a namespace part (but use instantiated name for first part)
								if (namespaces.empty()) {
									namespaces.emplace_back(StringType<32>(instantiated_name));
								} else {
									namespaces.emplace_back(StringType<32>(final_identifier.value()));
								}
								consume_token(); // consume ::
								
								// Get next identifier
								if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
									return ParseResult::error("Expected identifier after '::'", peek_token().value_or(Token()));
								}
								final_identifier = *peek_token();
								consume_token(); // consume the identifier
							}
							
							// Create a QualifiedIdentifierNode with the instantiated type name
							auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(namespaces, final_identifier);
							const auto& qualified_node = qualified_node_ast.as<QualifiedIdentifierNode>();
							result = emplace_node<ExpressionNode>(qualified_node);
							return ParseResult::success(*result);
						}
					}
				}
				
				// Check if we're parsing a template and this identifier is a template parameter
				if (parsing_template_class_ || !current_template_param_names_.empty()) {
					// Check if this identifier matches any template parameter name
					for (const auto& param_name : current_template_param_names_) {
						if (param_name == idenfifier_token.value()) {
							// This is a template parameter reference
							result = emplace_node<ExpressionNode>(TemplateParameterReferenceNode(param_name, idenfifier_token));
							return ParseResult::success(*result);
						}
					}
				}

				// Not a function call, template member access, or template parameter reference
				// But allow pack expansion (identifier...)
				if (is_pack_expansion) {
					// Create a simple identifier node - the pack expansion will be handled by the caller
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Not a function call, template member access, template parameter reference, or pack expansion - this is an error
				std::cerr << "DEBUG: Missing identifier: " << idenfifier_token.value() << "\n";
				return ParseResult::error("Missing identifier", idenfifier_token);
			}
		}
		
		if (identifierType && (!identifierType->is<DeclarationNode>() && 
		         !identifierType->is<FunctionDeclarationNode>() && 
		         !identifierType->is<VariableDeclarationNode>() &&
		         !identifierType->is<TemplateFunctionDeclarationNode>() &&
		         !identifierType->is<TemplateVariableDeclarationNode>() &&
		         !identifierType->is<TemplateParameterReferenceNode>())) {
			std::cerr << "DEBUG: Identifier type check failed, type_name=" << identifierType->type_name() << "\n";
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, *current_token_);
		}
		else {
			// Identifier already consumed at line 1621

			// Check for explicit template arguments: identifier<type1, type2>(args)
			std::optional<std::vector<TemplateTypeArg>> explicit_template_args;
			if (peek_token().has_value() && peek_token()->value() == "<") {
				explicit_template_args = parse_explicit_template_arguments();
				// If parsing failed, it might be a less-than operator, so continue normally
				
				// After template arguments, check for :: to handle Template<T>::member syntax
				if (explicit_template_args.has_value() && peek_token().has_value() && peek_token()->value() == "::") {
					// Parse qualified identifier after template
					auto qualified_result = parse_qualified_identifier_after_template(idenfifier_token);
					if (!qualified_result.is_error() && qualified_result.node().has_value()) {
						auto qualified_node = qualified_result.node()->as<QualifiedIdentifierNode>();
						result = emplace_node<ExpressionNode>(qualified_node);
						return ParseResult::success(*result);
					}
				}
				
				// Check if this is a variable template usage (identifier<args> without following '(')
				if (explicit_template_args.has_value() && 
				    (!peek_token().has_value() || peek_token()->value() != "(")) {
					// Try to instantiate as variable template
					auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
					if (var_template_opt.has_value()) {
						auto instantiated_var = try_instantiate_variable_template(idenfifier_token.value(), *explicit_template_args);
						if (instantiated_var.has_value() && instantiated_var->is<VariableDeclarationNode>()) {
							const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
							const auto& decl = var_decl.declaration();
							// Return identifier reference to the instantiated variable
							Token inst_token(Token::Type::Identifier, decl.identifier_token().value(), 
							                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
							return ParseResult::success(*result);
						}
					}
				}
			}

			// Initially set result to a simple identifier - will be upgraded to FunctionCallNode if it's a function call
			if (!result.has_value()) {
				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}

			// Check if this looks like a function call
			// Only consume '(' if the identifier is actually a function OR a function pointer OR has operator()
			bool is_function_decl = identifierType->is<FunctionDeclarationNode>() || identifierType->is<TemplateFunctionDeclarationNode>();
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
			// Check if this is a template parameter (for constructor calls like T(...))
			bool is_template_parameter = identifierType->is<TemplateParameterReferenceNode>();
			
			bool is_function_call = peek_token().has_value() && peek_token()->value() == "(" &&
			                        (is_function_decl || is_function_pointer || has_operator_call || explicit_template_args.has_value() || is_template_parameter);

			if (is_function_call && consume_punctuator("("sv)) {
				if (!peek_token().has_value())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_->type() != Token::Type::Punctuator || current_token_->value() != ")"sv) {
					ParseResult argResult = parse_expression();
					if (argResult.is_error()) {
						return argResult;
					}

					// Check for pack expansion: expr...
					if (peek_token().has_value() && peek_token()->value() == "...") {
						consume_token(); // consume '...'
						
						// Pack expansion: need to expand the expression for each pack element
						if (auto arg_node = argResult.node()) {
							// Check if this is an ExpressionNode containing an IdentifierNode
							if (arg_node->is<ExpressionNode>()) {
								const auto& expr = arg_node->as<ExpressionNode>();
								if (std::holds_alternative<IdentifierNode>(expr)) {
									const auto& ident_node = std::get<IdentifierNode>(expr);
									std::string_view pack_name = ident_node.name();
									// Try to find pack_name_0, pack_name_1, etc. in the symbol table
									size_t pack_size = 0;
									std::string base_name(pack_name);
									
									for (size_t i = 0; i < 100; ++i) {  // reasonable limit
										// Use StringBuilder to create a persistent string
										std::string_view element_name = StringBuilder()
											.append(base_name)
											.append("_")
											.append(static_cast<int>(i))
											.commit();
										
										if (gSymbolTable.lookup(element_name).has_value()) {
											++pack_size;
										} else {
											break;
										}
									}
									
									if (pack_size > 0) {
										// Add each pack element as a separate argument
										for (size_t i = 0; i < pack_size; ++i) {
											// Use StringBuilder to create a persistent string for the token
											std::string_view element_name = StringBuilder()
												.append(base_name)
												.append("_")
												.append(static_cast<int>(i))
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
									// Complex expression: need full rewriting (not implemented yet)
									if (auto node = argResult.node()) {
										args.push_back(*node);
									}
								}
							} else {
								// Not an ExpressionNode
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
				// For template parameter constructor calls, create ConstructorCallNode
				else if (is_template_parameter) {
					// This is a constructor call: T(args)
					const auto& template_param = identifierType->as<TemplateParameterReferenceNode>();
					// Create a TypeSpecifierNode for the template parameter
					Token param_token(Token::Type::Identifier, template_param.param_name(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
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
					// Check if this is a constructor call on a template parameter
					if (result.has_value() && result->is<ExpressionNode>()) {
						const ExpressionNode& expr = result->as<ExpressionNode>();
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							// This is a constructor call: T(args)
							const auto& template_param = std::get<TemplateParameterReferenceNode>(expr);
							// Create a TypeSpecifierNode for the template parameter
							Token param_token(Token::Type::Identifier, template_param.param_name(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
						} else {
							// Perform overload resolution for regular functions
							// First, get all overloads of this function
							auto all_overloads = gSymbolTable.lookup_all(idenfifier_token.value());

							std::cerr << "DEBUG [function call]: lookup_all for '" << idenfifier_token.value() << "' returned " << all_overloads.size() << " overloads" << std::endl;
							for (size_t i = 0; i < all_overloads.size(); ++i) {
								std::cerr << "  overload " << i << ": ";
								if (all_overloads[i].is<DeclarationNode>()) {
									const auto& decl = all_overloads[i].as<DeclarationNode>();
									std::cerr << "DeclarationNode - " << decl.identifier_token().value();
								} else if (all_overloads[i].is<FunctionDeclarationNode>()) {
									const auto& func = all_overloads[i].as<FunctionDeclarationNode>();
									std::cerr << "FunctionDeclarationNode - " << func.decl_node().identifier_token().value();
								} else {
									std::cerr << "Other node type";
								}
								std::cerr << std::endl;
							}

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
							
								TypeSpecifierNode arg_type_node = *arg_type;
							
								// For perfect forwarding: check if argument is an lvalue (named variable)
								// If so, mark it as an lvalue reference for template deduction
								if (args[i].is<ExpressionNode>()) {
									const ExpressionNode& expr = args[i].as<ExpressionNode>();
									if (std::holds_alternative<IdentifierNode>(expr)) {
										// This is a named variable (lvalue) - mark as lvalue reference
										// For forwarding reference deduction: Args&& deduces to T& for lvalues
										arg_type_node.set_lvalue_reference(true);
									}
									// TODO: Handle other lvalue cases (array subscript, member access, dereference, etc.)
									// Literals and temporaries remain as-is (treated as rvalues)
								}
							
								arg_types.push_back(arg_type_node);
							}
							
							// If we successfully extracted all argument types, perform overload resolution
							if (arg_types.size() == args.size()) {
								// If explicit template arguments were provided, use them directly
								if (explicit_template_args.has_value()) {
									auto instantiated_func = try_instantiate_template_explicit(idenfifier_token.value(), *explicit_template_args);
									if (instantiated_func.has_value()) {
										// Successfully instantiated template
										const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
										if (!decl_ptr) {
											return ParseResult::error("Invalid template instantiation", idenfifier_token);
										}
										result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
									} else {
										return ParseResult::error("No matching template for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
									}
								} else {
									// No explicit template arguments - try overload resolution first
									if (all_overloads.empty()) {
										// No overloads found - try template instantiation
										auto instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
										if (instantiated_func.has_value()) {
											// Successfully instantiated template
											const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
											if (!decl_ptr) {
												return ParseResult::error("Invalid template instantiation", idenfifier_token);
											}
											result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
										} else {
											return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										}
									} else {
										// Have overloads - do overload resolution
										auto resolution_result = resolve_overload(all_overloads, arg_types);

										if (!resolution_result.has_match) {
											// No matching regular function found - try template instantiation with deduction
											auto instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
											if (instantiated_func.has_value()) {
												// Successfully instantiated template
												const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
												if (!decl_ptr) {
													return ParseResult::error("Invalid template instantiation", idenfifier_token);
												}
												result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
											} else {
												return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
											}
										} else if (resolution_result.is_ambiguous) {
											return ParseResult::error("Ambiguous call to overloaded function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										} else {
											// Get the selected overload
											const DeclarationNode* decl_ptr = getDeclarationNode(*resolution_result.selected_overload);
											if (!decl_ptr) {
												return ParseResult::error("Invalid function declaration", idenfifier_token);
											}

											result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
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
	else if (current_token_->type() == Token::Type::Keyword && current_token_->value() == "nullptr"sv) {
		// Handle nullptr literal - represented as null pointer constant (0)
		// The actual type will be determined by context (can convert to any pointer type)
		result = emplace_node<ExpressionNode>(NumericLiteralNode(*current_token_,
			0ULL, Type::Int, TypeQualifier::None, 64));
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
		// Also handle sizeof... operator: sizeof...(pack_name)
		Token sizeof_token = *current_token_;
		consume_token(); // consume 'sizeof'

		// Check for ellipsis to determine if this is sizeof... (parameter pack)
		bool is_sizeof_pack = false;
		if (peek_token().has_value() && 
		    (peek_token()->type() == Token::Type::Operator || peek_token()->type() == Token::Type::Punctuator) &&
		    peek_token()->value() == "...") {
			consume_token(); // consume '...'
			is_sizeof_pack = true;
		}

		if (!consume_punctuator("("sv)) {
			return ParseResult::error("Expected '(' after 'sizeof'", *current_token_);
		}

		if (is_sizeof_pack) {
			// Parse sizeof...(pack_name)
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected parameter pack name after 'sizeof...('", *current_token_);
			}
			
			Token pack_name_token = *peek_token();
			std::string_view pack_name = pack_name_token.value();
			consume_token(); // consume pack name
			
			if (!consume_punctuator(")")) {
				return ParseResult::error("Expected ')' after sizeof... pack name", *current_token_);
			}
			
			result = emplace_node<ExpressionNode>(SizeofPackNode(pack_name, sizeof_token));
		}
		else {
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
		// 3. C++17 Fold expression: (...op pack), (pack op...), (init op...op pack), (pack op...op init)
		
		// Check for fold expression patterns
		TokenPosition fold_check_pos = save_token_position();
		bool is_fold = false;
		
		// Pattern 1: Unary left fold: (... op pack)
		if (peek_token().has_value() && peek_token()->value() == "...") {
			consume_token(); // consume ...
			
			// Next should be an operator
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator) {
				std::string_view fold_op = peek_token()->value();
				Token op_token = *peek_token();
				consume_token(); // consume operator
				
				// Next should be the pack identifier
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
					std::string_view pack_name = peek_token()->value();
					Token pack_token = *peek_token();
					consume_token(); // consume pack name
					
					if (consume_punctuator(")")) {
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
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
				std::string_view first_id = peek_token()->value();
				Token first_id_token = *peek_token();
				consume_token(); // consume identifier
				
				// Check what follows
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator) {
					std::string_view fold_op = peek_token()->value();
					Token op_token = *peek_token();
					consume_token(); // consume operator
					
					// Check for ... (fold expression)
					if (peek_token().has_value() && peek_token()->value() == "...") {
						consume_token(); // consume ...
						
						// Check if binary fold or unary right fold
						if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
							peek_token()->value() == fold_op) {
							// Binary right fold: (pack op ... op init)
							consume_token(); // consume second operator
							
							ParseResult init_result = parse_expression();
							if (!init_result.is_error() && init_result.node().has_value() &&
								consume_punctuator(")")) {
								discard_saved_token(fold_check_pos);
								result = emplace_node<ExpressionNode>(
									FoldExpressionNode(first_id, fold_op,
										FoldExpressionNode::Direction::Right, *init_result.node(), op_token));
								is_fold = true;
							}
						} else if (consume_punctuator(")")) {
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
			TokenPosition init_pos = save_token_position();
			ParseResult init_result = parse_primary_expression();
			
			if (!init_result.is_error() && init_result.node().has_value()) {
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator) {
					std::string_view fold_op = peek_token()->value();
					Token op_token = *peek_token();
					consume_token(); // consume operator
					
					if (peek_token().has_value() && peek_token()->value() == "...") {
						consume_token(); // consume ...
						
						if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
							peek_token()->value() == fold_op) {
							consume_token(); // consume second operator
							
							if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
								std::string_view pack_name = peek_token()->value();
								consume_token(); // consume pack name
								
								if (consume_punctuator(")")) {
									// Valid binary left fold: (init op ... op pack)
									discard_saved_token(fold_check_pos);
									discard_saved_token(init_pos);
									result = emplace_node<ExpressionNode>(
										FoldExpressionNode(pack_name, fold_op,
											FoldExpressionNode::Direction::Left, *init_result.node(), op_token));
									is_fold = true;
								}
							}
						}
					}
				}
			}
			
			if (!is_fold) {
				restore_token_position(init_pos);
			}
		}
		
		// If not a fold expression, parse as regular parenthesized expression or cast
		if (!is_fold) {
			restore_token_position(fold_check_pos);
		
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
					// Allow comma operator in parenthesized expressions
					ParseResult paren_result = parse_expression(MIN_PRECEDENCE);
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
				// Allow comma operator in parenthesized expressions
				ParseResult paren_result = parse_expression(MIN_PRECEDENCE);
				if (paren_result.is_error()) {
					return paren_result;
				}
				if (!consume_punctuator(")")) {
					return ParseResult::error("Expected ')' after parenthesized expression",
						*current_token_);
				}
				result = paren_result.node();
			}
		}  // End of fold expression check
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
		std::cerr << ">>>>> Checking member: " << member_name_token.value() 
		          << " peek=" << (peek_token().has_value() ? peek_token()->value() : "NONE") << "\n";
		if (peek_token().has_value() && peek_token()->value() == "("sv) {
			// This is a member function call: obj.method(args)
			std::cerr << ">>>>> IS MEMBER FUNCTION CALL: " << member_name_token.value() << "\n";

			consume_token(); // consume '('

			// Parse function arguments
			ChunkedVector<ASTNode> args;
			std::vector<TypeSpecifierNode> arg_types;  // Collect argument types for template deduction
			
			std::cerr << ">>>>> About to parse arguments\n";
			if (!peek_token().has_value() || peek_token()->value() != ")"sv) {
				while (true) {
					auto arg_result = parse_expression();
					if (arg_result.is_error()) {
						return arg_result;
					}
					if (auto arg = arg_result.node()) {
						args.push_back(*arg);
						
						// Try to deduce the argument type for template instantiation
						// For now, we'll deduce from literals and identifiers
						Type arg_type = Type::Int;  // Default
						if (arg->is<ExpressionNode>()) {
							const ExpressionNode& expr = arg->as<ExpressionNode>();
							if (std::holds_alternative<NumericLiteralNode>(expr)) {
								const auto& lit = std::get<NumericLiteralNode>(expr);
								arg_type = lit.type();
							} else if (std::holds_alternative<IdentifierNode>(expr)) {
								// Look up identifier type from symbol table
								const auto& ident = std::get<IdentifierNode>(expr);
								auto symbol = lookup_symbol(ident.name());
								if (symbol.has_value() && symbol->is<DeclarationNode>()) {
									const auto& decl = symbol->as<DeclarationNode>();
									arg_type = decl.type_node().as<TypeSpecifierNode>().type();
								}
							}
						}
						arg_types.emplace_back(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token());
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

			// Try to get the object's type to check for member function templates
			std::optional<std::string_view> object_struct_name;
			
			// Try to deduce the object type from the result expression
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr)) {
					const auto& ident = std::get<IdentifierNode>(expr);
					auto symbol = lookup_symbol(ident.name());
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl = symbol->as<DeclarationNode>();
						const auto& type_spec = decl.type_node().as<TypeSpecifierNode>();
						if (type_spec.type() == Type::UserDefined || type_spec.type() == Type::Struct) {
							TypeIndex type_idx = type_spec.type_index();
							if (type_idx < gTypeInfo.size()) {
								object_struct_name = gTypeInfo[type_idx].name_;
							}
						}
					}
				}
			}

			// Try to instantiate member function template if applicable
			std::optional<ASTNode> instantiated_func;
			std::cerr << "DEBUG: object_struct_name.has_value()=" << object_struct_name.has_value()
			          << " arg_types.empty()=" << arg_types.empty() << " arg_types.size()=" << arg_types.size() << "\n";
			if (object_struct_name.has_value() && !arg_types.empty()) {
				std::cerr << "DEBUG: Calling try_instantiate_member_function_template("
				          << *object_struct_name << ", " << member_name_token.value()
				          << ", " << arg_types.size() << " arg_types)\n";
				instantiated_func = try_instantiate_member_function_template(
					*object_struct_name,
					member_name_token.value(),
					arg_types
				);
				std::cerr << "DEBUG: try_instantiate returned, has_value=" << instantiated_func.has_value() << "\n";
			}

			// Use the instantiated function if available, otherwise create temporary placeholder
			FunctionDeclarationNode* func_ref_ptr = nullptr;
			if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
				func_ref_ptr = &instantiated_func->as<FunctionDeclarationNode>();
				std::cerr << ">>>>> Using instantiated function, has_definition=" 
				          << func_ref_ptr->get_definition().has_value() << "\n";
			} else {
				// Create a temporary function declaration node for the member function
				auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, member_name_token);
				auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_name_token);
				auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());
				func_ref_ptr = &func_ref;
			}

			// Create member function call node
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, *func_ref_ptr, std::move(args), member_name_token));
			continue;
		}

		// Regular member access (not a function call)
		result = emplace_node<ExpressionNode>(
			MemberAccessNode(*result, member_name_token));
		continue;  // Check for more postfix operators (e.g., obj.member1.member2)
	}

	if (result.has_value())
		return ParseResult::success(*result);

	// No result was produced - this should not happen in a well-formed expression
	return ParseResult();  // Return monostate instead of empty success
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
            std::optional<ASTNode> var_symbol = lookup_symbol(var_name);
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
            std::optional<ASTNode> var_symbol = lookup_symbol(var_name);

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
				std::string(var_name),
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
    func_ref.set_definition(body_copy);

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

    // Check for C++17 'if constexpr'
    bool is_constexpr = false;
    if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword &&
        peek_token()->value() == "constexpr") {
        consume_keyword("constexpr"sv);
        is_constexpr = true;
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
                *cond_node, *then_node, else_stmt, init_statement, is_constexpr
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

// Helper: Parse qualified identifier path after template arguments (Template<T>::member)
// Assumes we're positioned right after template arguments and next token is ::
// Returns a QualifiedIdentifierNode wrapped in ExpressionNode if successful
ParseResult Parser::parse_qualified_identifier_after_template(const Token& template_base_token) {
	std::vector<StringType<32>> namespaces;
	Token final_identifier = template_base_token;  // Start with the template name
	
	// Collect the qualified path after ::
	while (peek_token().has_value() && peek_token()->value() == "::") {
		// Current identifier becomes a namespace part
		namespaces.emplace_back(StringType<32>(final_identifier.value()));
		consume_token(); // consume ::
		
		// Get next identifier
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier after '::'", peek_token().value_or(Token()));
		}
		final_identifier = *peek_token();
		consume_token(); // consume the identifier
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

	// Add variadic ellipsis if this is a variadic function
	if (func_node.is_variadic()) {
		if (!params.empty()) result += ", ";
		result += "...";
	}

	result += ")";

	return result;
}

// Check if an identifier name is a template parameter in current scope
bool Parser::is_template_parameter(std::string_view name) const {
    bool result = std::find(template_param_names_.begin(), template_param_names_.end(), name) != template_param_names_.end();
    if (parsing_template_body_) {
        std::cerr << "DEBUG: is_template_parameter('" << name << "') = " << result
                  << ", parsing_template_body_ = " << parsing_template_body_
                  << ", template_param_names size = " << template_param_names_.size() << "\n";
    }
    return result;
}

// Substitute template parameter in a type specification
// Handles complex transformations like const T& -> const int&, T* -> int*, etc.
std::pair<Type, TypeIndex> Parser::substitute_template_parameter(
	const TypeSpecifierNode& original_type,
	const std::vector<ASTNode>& template_params,
	const std::vector<TemplateTypeArg>& template_args
) {
	Type result_type = original_type.type();
	TypeIndex result_type_index = original_type.type_index();

	// Only substitute UserDefined types (which might be template parameters)
	if (result_type == Type::UserDefined) {
		// Look up the type name using type_index
		if (result_type_index < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[result_type_index];
			std::string_view type_name = type_info.name_;

			// Try to find which template parameter this is
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name) {
						// Found a match! Substitute with the concrete type
						const TemplateTypeArg& arg = template_args[i];
						
						// The template argument already contains the full type info including:
						// - base_type, type_index
						// - pointer_depth, is_reference, is_rvalue_reference
						// - cv_qualifier (const/volatile)
						
						// We need to apply the qualifiers from BOTH:
						// 1. The original type (e.g., const T& has const and reference)
						// 2. The template argument (e.g., T=int* has pointer_depth=1)
						
						result_type = arg.base_type;
						result_type_index = arg.type_index;
						
						// Note: The qualifiers (pointer_depth, references, const/volatile) are NOT
						// combined here because they are already fully specified in the TypeSpecifierNode
						// that will be created using this base type. The caller is responsible for
						// constructing a new TypeSpecifierNode with the appropriate qualifiers.
						
						break;
					}
				}
			}
		}
	}

	return {result_type, result_type_index};
}

// Lookup symbol with template parameter checking
std::optional<ASTNode> Parser::lookup_symbol_with_template_check(std::string_view identifier) {
    // First check if it's a template parameter using the new method
    if (parsing_template_body_ && !current_template_param_names_.empty()) {
        std::cerr << "DEBUG: Creating TemplateParameterReferenceNode for '" << identifier << "' using new method\n";
        return gSymbolTable.lookup(identifier, gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
    }

    // Otherwise, do normal symbol lookup
    return gSymbolTable.lookup(identifier);
}

// Helper to extract type from an expression for overload resolution
std::optional<TypeSpecifierNode> Parser::get_expression_type(const ASTNode& expr_node) const {
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
		auto symbol = this->lookup_symbol(ident.name());
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

	// Save the current linkage and set the new one
	Linkage saved_linkage = current_linkage_;
	current_linkage_ = linkage;

	// Save the current AST size to know which nodes were added by this block
	size_t ast_size_before = ast_nodes_.size();

	// Parse declarations until '}' by calling parse_top_level_node() repeatedly
	// This ensures extern "C" blocks support exactly the same constructs as file scope
	while (peek_token().has_value() && peek_token()->value() != "}") {
		
		ParseResult result = parse_top_level_node();
		
		if (result.is_error()) {
			current_linkage_ = saved_linkage;  // Restore linkage before returning error
			return result;
		}
		
		// parse_top_level_node() already adds nodes to ast_nodes_, so we don't need to do it here
	}

	// Restore the previous linkage
	current_linkage_ = saved_linkage;

	if (!consume_punctuator("}")) {
		return ParseResult::error("Expected '}' after extern block", *current_token_);
	}

	// Create a block node containing all declarations parsed in this extern block
	auto [block_node, block_ref] = create_node_ref(BlockNode());
	
	// Move all nodes added during this block into the BlockNode
	for (size_t i = ast_size_before; i < ast_nodes_.size(); ++i) {
		block_ref.add_statement_node(ast_nodes_[i]);
	}
	
	// Remove those nodes from ast_nodes_ since they're now in the BlockNode
	ast_nodes_.resize(ast_size_before);

	return ParseResult::success(block_node);
}

// Helper function to get the size of a type in bits
unsigned char Parser::get_type_size_bits(Type type) {
	switch (type) {
		case Type::Void:
			return 0;
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

// Parse template declaration: template<typename T> ...
ParseResult Parser::parse_template_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'template' keyword
	if (!consume_keyword("template")) {
		return ParseResult::error("Expected 'template' keyword", *peek_token());
	}

	// Expect '<' to start template parameter list
	// Note: '<' is an operator, not a punctuator
	if (!peek_token().has_value() || peek_token()->value() != "<") {
		return ParseResult::error("Expected '<' after 'template' keyword", *current_token_);
	}
	consume_token(); // consume '<'

	// Check if this is a template specialization (template<>)
	bool is_specialization = false;
	if (peek_token().has_value() && peek_token()->value() == ">") {
		is_specialization = true;
		consume_token(); // consume '>'
	}

	// Parse template parameter list (unless it's a specialization)
	std::vector<ASTNode> template_params;
	if (!is_specialization) {
		auto param_list_result = parse_template_parameter_list(template_params);
		if (param_list_result.is_error()) {
			return param_list_result;
		}

		// Expect '>' to end template parameter list
		// Note: '>' is an operator, not a punctuator
		if (!peek_token().has_value() || peek_token()->value() != ">") {
			return ParseResult::error("Expected '>' after template parameter list", *current_token_);
		}
		consume_token(); // consume '>'
	}

	// Now parse what comes after the template parameter list
	// We support function templates and class templates

	// Add template parameters to the type system temporarily
	// This allows them to be used in the function body or class members
	std::vector<TypeInfo*> template_type_infos;
	std::vector<std::string_view> template_param_names;  // string_view from Token storage
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			// Add ALL template parameters to the name list (Type, NonType, and Template)
			// This allows them to be recognized when referenced in the template body
			template_param_names.push_back(tparam.name());  // string_view from Token
			
			// Only Type parameters need TypeInfo registration
			if (tparam.kind() == TemplateParameterKind::Type) {
				// Register the template parameter as a user-defined type temporarily
				// Create a TypeInfo entry for the template parameter
				auto& type_info = gTypeInfo.emplace_back(std::string(tparam.name()), Type::UserDefined, gTypeInfo.size());
				gTypesByName.emplace(type_info.name_, &type_info);
				template_type_infos.push_back(&type_info);
			}
		}
	}

	// RAII cleanup for template parameters registered in gTypesByName
	// This ensures cleanup happens on ALL return paths (success or error)
	struct TemplateParamCleanup {
		std::vector<TypeInfo*>& type_infos;
		~TemplateParamCleanup() {
			std::cerr << "DEBUG: TemplateParamCleanup destructor called, cleaning up " << type_infos.size() << " entries\n";
			for (const auto* type_info : type_infos) {
				gTypesByName.erase(type_info->name_);
				// Note: We don't remove from gTypeInfo because it's a deque and removing would invalidate pointers
			}
		}
	} cleanup_guard{template_type_infos};

	// Check if it's an alias template: template<typename T> using Ptr = T*;
	bool is_alias_template = peek_token().has_value() &&
	                         peek_token()->type() == Token::Type::Keyword &&
	                         peek_token()->value() == "using";

	// Check if it's a class/struct template
	bool is_class_template = peek_token().has_value() &&
	                         peek_token()->type() == Token::Type::Keyword &&
	                         (peek_token()->value() == "class" || peek_token()->value() == "struct");

	// Check if it's a variable template (constexpr, inline, etc. + type + identifier)
	bool is_variable_template = false;
	if (!is_alias_template && !is_class_template && peek_token().has_value()) {
		// Variable templates usually start with constexpr, inline, or a type directly
		// Save position to check
		auto var_check_pos = save_token_position();
		
		// Skip storage class specifiers (constexpr, inline, static, etc.)
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
			std::string_view kw = peek_token()->value();
			if (kw == "constexpr" || kw == "inline" || kw == "static" || 
			    kw == "const" || kw == "volatile" || kw == "extern") {
				consume_token();
			} else {
				break;
			}
		}
		
		// Try to parse type specifier
		auto var_type_result = parse_type_specifier();
		if (!var_type_result.is_error()) {
			// After type, expect identifier (variable name)
			if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
				std::string_view var_name = peek_token()->value();
				consume_token();
				
				// After identifier, expect '=' for variable template
				// (function would have '(' here)
				if (peek_token().has_value() && peek_token()->value() == "=") {
					is_variable_template = true;
				}
			}
		}
		
		// Restore position for actual parsing
		restore_token_position(var_check_pos);
	}

	std::cerr << "DEBUG: is_alias_template=" << is_alias_template << std::endl;
	std::cerr << "DEBUG: is_class_template=" << is_class_template << std::endl;
	std::cerr << "DEBUG: is_variable_template=" << is_variable_template << std::endl;
	if (peek_token().has_value()) {
		std::cerr << "DEBUG: Next token after template params: '" << peek_token()->value() << "' (type=" << static_cast<int>(peek_token()->type()) << ")" << std::endl;
	}

	ParseResult decl_result;
	if (is_alias_template) {
		// Consume 'using' keyword
		consume_token();
		
		// Parse alias name
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected alias name after 'using' in template", *current_token_);
		}
		Token alias_name_token = *peek_token();
		std::string_view alias_name = alias_name_token.value();
		consume_token();
		
		// Expect '='
		if (!peek_token().has_value() || peek_token()->value() != "=") {
			return ParseResult::error("Expected '=' after alias name in template", *current_token_);
		}
		consume_token(); // consume '='
		
		// Parse the target type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		// Get the TypeSpecifierNode and check for pointer/reference modifiers
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Handle pointer depth (*, **, etc.)
		while (peek_token().has_value() && peek_token()->value() == "*") {
			consume_token(); // consume '*'
			
			// Parse CV-qualifiers after the * (const, volatile)
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
			
			type_spec.add_pointer_level(ptr_cv);
		}
		
		// Handle reference modifiers (&, &&)
		if (peek_token().has_value() && peek_token()->value() == "&") {
			consume_token(); // consume first '&'
			
			// Check for rvalue reference (&&)
			if (peek_token().has_value() && peek_token()->value() == "&") {
				consume_token(); // consume second '&'
				type_spec.set_reference(true);  // true = rvalue reference
			} else {
				type_spec.set_lvalue_reference(true);  // lvalue reference
			}
		}
		
		// Expect semicolon
		if (!consume_punctuator(";")) {
			return ParseResult::error("Expected ';' after alias template declaration", *current_token_);
		}
		
		// Create TemplateAliasNode
		auto alias_node = emplace_node<TemplateAliasNode>(
			std::move(template_params),
			std::move(template_param_names),
			alias_name,
			type_result.node().value()
		);
		
		// Register the alias template in the template registry
		// We'll handle instantiation later when the alias is used
		gTemplateRegistry.register_alias_template(std::string(alias_name), alias_node);
		
		return saved_position.success(alias_node);
	}
	else if (is_variable_template) {
		std::cerr << "DEBUG: Parsing variable template" << std::endl;
		
		// Parse storage class specifiers manually (constexpr, inline, static, etc.)
		bool is_constexpr = false;
		StorageClass storage_class = StorageClass::None;
		
		while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
			std::string_view kw = peek_token()->value();
			if (kw == "constexpr") {
				is_constexpr = true;
				consume_token();
			} else if (kw == "inline") {
				consume_token(); // consume but don't store for now
			} else if (kw == "static") {
				storage_class = StorageClass::Static;
				consume_token();
			} else {
				break; // Not a storage class specifier
			}
		}
		
		// Now parse the variable declaration: Type name = initializer;
		// We need to manually parse type, name, and initializer
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		// Parse variable name
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			return ParseResult::error("Expected variable name in variable template", *current_token_);
		}
		Token var_name_token = *peek_token();
		consume_token();
		
		// Create DeclarationNode
		auto decl_node = emplace_node<DeclarationNode>(
			type_result.node().value(),
			var_name_token
		);
		
		// Parse initializer
		std::optional<ASTNode> init_expr;
		if (peek_token().has_value() && peek_token()->value() == "=") {
			consume_token(); // consume '='
			
			// Parse the initializer expression
			auto init_result = parse_expression();
			if (init_result.is_error()) {
				return init_result;
			}
			init_expr = init_result.node();
		}
		
		// Expect semicolon
		if (!consume_punctuator(";")) {
			return ParseResult::error("Expected ';' after variable template declaration", *current_token_);
		}
		
		// Create VariableDeclarationNode
		auto var_decl_node = emplace_node<VariableDeclarationNode>(
			decl_node,
			init_expr,
			storage_class
		);
		
		// Set constexpr flag if present
		var_decl_node.as<VariableDeclarationNode>().set_is_constexpr(is_constexpr);
		
		// Create TemplateVariableDeclarationNode
		auto template_var_node = emplace_node<TemplateVariableDeclarationNode>(
			std::move(template_params),
			var_decl_node
		);
		
		// Register in template registry
		std::string_view var_name = var_name_token.value();
		gTemplateRegistry.registerVariableTemplate(var_name, template_var_node);
		
		// Also add to symbol table so identifier lookup works
		gSymbolTable.insert(var_name, template_var_node);
		
		return saved_position.success(template_var_node);
	}
	else if (is_class_template) {
		std::cerr << "DEBUG: Parsing class template" << std::endl;
		
		// Check if this is a partial specialization by peeking ahead
		// Pattern: template<typename T> struct Name<T&> { ... }
		// After struct/class keyword and name, if we see '<', it's a specialization
		bool is_partial_specialization = false;
		if (!is_specialization && !template_params.empty()) {
			// Save position to peek ahead
			auto peek_pos = save_token_position();
			
			// Try to consume struct/class keyword
			if (consume_keyword("struct") || consume_keyword("class")) {
				// Try to get class name
				if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
					std::string_view class_name = peek_token()->value();
					consume_token();
					
					// Check if template arguments follow
					if (peek_token().has_value() && peek_token()->value() == "<") {
						// This is a partial specialization!
						is_partial_specialization = true;
					}
				}
			}
			
			// Restore position
			restore_token_position(peek_pos);
		}
		
		// Handle full template specialization (template<>)
		if (is_specialization) {
			// Parse: class ClassName<TemplateArgs> { ... }
			// We need to parse the class keyword, name, template arguments, and body separately

			// Set parsing context flags
			parsing_template_class_ = true;
			parsing_template_body_ = true;

			bool is_class = consume_keyword("class");
			if (!is_class) {
				consume_keyword("struct");  // Try struct instead
			}

			// Parse class name
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected class name after 'class' keyword", *current_token_);
			}

			Token class_name_token = *peek_token();
			std::string_view template_name = class_name_token.value();
			consume_token();

			// Parse template arguments: <int>, <float>, etc.
			auto template_args_opt = parse_explicit_template_arguments();
			if (!template_args_opt.has_value()) {
				return ParseResult::error("Expected template arguments in specialization", *current_token_);
			}

			std::vector<TemplateTypeArg> template_args = *template_args_opt;

			// Now parse the class body as a regular struct
			// But we need to give it a unique name that includes the template arguments
			std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);

			// Create a struct node with the instantiated name
			auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
				instantiated_name,
				is_class
			);

			// Parse base classes if any
			if (peek_token().has_value() && peek_token()->value() == ":") {
				// TODO: Handle base classes in specializations
				// For now, skip this
			}

			// Expect opening brace
			if (!consume_punctuator("{")) {
				return ParseResult::error("Expected '{' after class name in specialization", *peek_token());
			}

			// Create struct type info first so we can reference it
			TypeInfo& struct_type_info = add_struct_type(std::string(instantiated_name));

			// Parse class members (simplified - reuse struct parsing logic)
			// For now, we'll parse a simple class body
			AccessSpecifier current_access = struct_ref.default_access();

			// Set up member function context so functions know they're in a class
			member_function_context_stack_.push_back({
				instantiated_name,
				struct_type_info.type_index_,
				&struct_ref
			});

			while (peek_token().has_value() && peek_token()->value() != "}") {
				// Check for access specifiers
				if (peek_token()->type() == Token::Type::Keyword) {
					if (peek_token()->value() == "public") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'public'", *peek_token());
						}
						current_access = AccessSpecifier::Public;
						continue;
					} else if (peek_token()->value() == "private") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'private'", *peek_token());
						}
						current_access = AccessSpecifier::Private;
						continue;
					} else if (peek_token()->value() == "protected") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'protected'", *peek_token());
						}
						current_access = AccessSpecifier::Protected;
						continue;
					} else if (peek_token()->value() == "static_assert") {
						// Handle static_assert inside class body
						auto static_assert_result = parse_static_assert();
						if (static_assert_result.is_error()) {
							return static_assert_result;
						}
						continue;
					} else if (peek_token()->value() == "using") {
						// Handle type alias inside class body: using value_type = T;
						auto using_result = parse_using_directive_or_declaration();
						if (using_result.is_error()) {
							return using_result;
						}
						// Type aliases are registered during parsing
						// For template specializations, they're already in gTypesByName
						continue;
					} else if (peek_token()->value() == "static") {
						// Handle static members: static const int size = 10;
						consume_token(); // consume "static"
						
						// Handle optional const
						bool is_const = false;
						if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword 
							&& peek_token()->value() == "const") {
							is_const = true;
							consume_token();
						}
						
						// Parse type and name
						auto type_and_name = parse_type_and_name();
						if (type_and_name.is_error()) {
							return type_and_name;
						}

						// Optional initializer
						std::optional<ASTNode> init_expr_opt;
						if (peek_token().has_value() && peek_token()->type() == Token::Type::Operator
							&& peek_token()->value() == "=") {
							consume_token(); // consume "="

							// Parse initializer expression
							auto init_result = parse_expression();
							if (init_result.is_error()) {
								return init_result;
							}
							init_expr_opt = init_result.node();
						}

						// Consume semicolon
						if (!consume_punctuator(";")) {
							return ParseResult::error("Expected ';' after static member declaration", *peek_token());
						}

						// Get the declaration and type specifier
						if (!type_and_name.node().has_value()) {
							return ParseResult::error("Expected static member declaration", *peek_token());
						}
						const DeclarationNode& decl = type_and_name.node()->as<DeclarationNode>();
						const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

						// Register static member in struct info
						// Calculate size and alignment for the static member
						size_t member_size = get_type_size_bits(type_spec.type()) / 8;
						size_t member_alignment = get_type_alignment(type_spec.type(), member_size);

						// Register the static member
						struct_type_info.getStructInfo()->addStaticMember(
							std::string(decl.identifier_token().value()),
							type_spec.type(),
							type_spec.type_index(),
							member_size,
							member_alignment,
							AccessSpecifier::Public,
							init_expr_opt,
							is_const
						);

						continue;
					}
				}

					// Parse member declaration (use same logic as regular struct parsing)
				auto member_result = parse_type_and_name();
				if (member_result.is_error()) {
					return member_result;
				}

				if (!member_result.node().has_value()) {
					return ParseResult::error("Expected member declaration", *peek_token());
				}

				// Check if this is a member function (has '(') or data member
				if (peek_token().has_value() && peek_token()->value() == "(") {
					// This is a member function
					if (!member_result.node()->is<DeclarationNode>()) {
						return ParseResult::error("Expected declaration node for member function", *peek_token());
					}

					DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();

					// Parse function declaration with parameters
					auto func_result = parse_function_declaration(decl_node);
					if (func_result.is_error()) {
						return func_result;
					}

					if (!func_result.node().has_value()) {
						return ParseResult::error("Failed to create function declaration node", *peek_token());
					}

					FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();
					DeclarationNode& func_decl_node = const_cast<DeclarationNode&>(func_decl.decl_node());

					// Create a new FunctionDeclarationNode with member function info
					auto [member_func_node, member_func_ref] =
						emplace_node_ref<FunctionDeclarationNode>(func_decl_node, instantiated_name);

					// Copy parameters from the parsed function
					for (const auto& param : func_decl.parameter_nodes()) {
						member_func_ref.add_parameter_node(param);
					}

					// Copy function body if it exists
					auto definition_opt = func_decl.get_definition();
					if (definition_opt.has_value()) {
						member_func_ref.set_definition(definition_opt.value());
					}

					// Check for function body and use delayed parsing
					if (peek_token().has_value() && peek_token()->value() == "{") {
						// Save position at start of body
						TokenPosition body_start = save_token_position();

						// Skip over the function body by counting braces
						skip_balanced_braces();

						// Record for delayed parsing
						delayed_function_bodies_.push_back({
							&member_func_ref,
							body_start,
							instantiated_name,
							struct_type_info.type_index_,
							&struct_ref,
							false,  // is_constructor
							false,  // is_destructor
							nullptr,  // ctor_node
							nullptr,  // dtor_node
							{}  // no template parameter names for specializations
						});
					} else {
						// No body - expect semicolon
						if (!consume_punctuator(";")) {
							return ParseResult::error("Expected '{' or ';' after member function declaration", *peek_token());
						}
					}

					// Add to struct
					struct_ref.add_member_function(
						member_func_node,
						current_access,
						false,  // is_virtual
						false,  // is_pure_virtual
						false,  // is_override
						false   // is_final
					);
				} else {
					// This is a data member
					std::optional<ASTNode> default_initializer;

					// Get the type from the member declaration
					if (!member_result.node()->is<DeclarationNode>()) {
						return ParseResult::error("Expected declaration node for member", *peek_token());
					}
					const DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
					const TypeSpecifierNode& type_spec = decl_node.type_node().as<TypeSpecifierNode>();

					// Check for member initialization with '=' (C++11 feature)
					if (peek_token().has_value() && peek_token()->value() == "=") {
						consume_token(); // consume '='

						// Parse the initializer expression
						auto init_result = parse_expression();
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							default_initializer = *init_result.node();
						}
					}

					struct_ref.add_member(*member_result.node(), current_access, default_initializer);

					// Consume semicolon
					if (!consume_punctuator(";")) {
						return ParseResult::error("Expected ';' after member declaration", *peek_token());
					}
				}

				// Consumed semicolon above in each branch
			}

			// Expect closing brace
			if (!consume_punctuator("}")) {
				return ParseResult::error("Expected '}' after class body", *peek_token());
			}

			// Pop member function context
			member_function_context_stack_.pop_back();

			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after class declaration", *peek_token());
			}

			// struct_type_info was already created above
			// Now create the StructTypeInfo with member information
			auto struct_info = std::make_unique<StructTypeInfo>(std::string(instantiated_name), struct_ref.default_access());

			// Add members to struct info
			for (const auto& member_decl : struct_ref.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

				// Calculate member size and alignment
				size_t member_size = get_type_size_bits(type_spec.type()) / 8;
				size_t referenced_size_bits = type_spec.size_in_bits();
				size_t member_alignment = get_type_alignment(type_spec.type(), member_size);

				if (type_spec.type() == Type::Struct) {
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

				bool is_ref_member = type_spec.is_reference();
				bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
				if (is_ref_member) {
					member_size = sizeof(void*);
					referenced_size_bits = referenced_size_bits ? referenced_size_bits : type_spec.size_in_bits();
					member_alignment = sizeof(void*);
				}
				struct_info->addMember(
					std::string(decl.identifier_token().value()),
					type_spec.type(),
					type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					is_ref_member,
					is_rvalue_ref_member,
					referenced_size_bits
				);
			}

			// Add member functions to struct info
			for (const auto& member_func_decl : struct_ref.member_functions()) {
				const FunctionDeclarationNode& func_decl = member_func_decl.function_declaration.as<FunctionDeclarationNode>();
				const DeclarationNode& decl = func_decl.decl_node();

				struct_info->addMemberFunction(
					std::string(decl.identifier_token().value()),
					member_func_decl.function_declaration,
					member_func_decl.access,
					member_func_decl.is_virtual,
					member_func_decl.is_pure_virtual,
					member_func_decl.is_override,
					member_func_decl.is_final
				);
			}

			// Finalize the struct layout
			struct_info->finalize();

			// Store struct info in type info
			struct_type_info.setStructInfo(std::move(struct_info));

			// Parse delayed function bodies for specialization member functions
			TokenPosition position_after_struct = save_token_position();
			for (auto& delayed : delayed_function_bodies_) {
				// Restore token position to the start of the function body
				restore_token_position(delayed.body_start);

				// Set up function context
				gSymbolTable.enter_scope(ScopeType::Function);
				member_function_context_stack_.push_back({
					delayed.struct_name,
					delayed.struct_type_index,
					delayed.struct_node
				});

				// Add function parameters to scope
				for (const auto& param : delayed.func_node->parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl = param.as<DeclarationNode>();
						gSymbolTable.insert(param_decl.identifier_token().value(), param);
					}
				}

				// Parse the function body
				auto block_result = parse_block();
				if (block_result.is_error()) {
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return block_result;
				}

				if (auto block = block_result.node()) {
					delayed.func_node->set_definition(*block);
				}

				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			}

			// Clear delayed function bodies
			delayed_function_bodies_.clear();

			// Restore position after struct
			restore_token_position(position_after_struct);

			// Register the specialization
			std::cerr << "DEBUG: Registering specialization for " << template_name << " with " << template_args.size() << " args\n";
			for (size_t i = 0; i < template_args.size(); ++i) {
				std::cerr << "  Arg " << i << ": base_type=" << static_cast<int>(template_args[i].base_type)
						  << " is_ref=" << template_args[i].is_reference << "\n";
			}
			// NOTE:
			// At this point we have parsed a specialization of the primary template.
			// Two forms are supported:
			//  - Full/Exact specialization: template<> struct Container<bool> { ... };
			//  - Partial specialization   : template<typename T> struct Container<T*> { ... };
			//
			// Full specializations:
			//   - template_params is empty (template<>)
			//   - template_args holds fully concrete TemplateTypeArg values (e.g., bool)
			//   - We must register an exact specialization that will be preferred for a
			//     matching instantiation (e.g., Container<bool>).
			//
			// Partial specializations:
			//   - template_params is non-empty (e.g., <typename T>)
			//   - template_args/pattern_args use TemplateTypeArg to encode the pattern
			//     (T*, T&, const T, etc.) and are handled via pattern matching.
			//
			// Implementation:
			//   - If template_params is empty, treat as full specialization and register
			//     via registerSpecialization().
			//   - Otherwise, treat as partial specialization pattern and register via
			//     registerSpecializationPattern().
			if (template_params.empty()) {
				// Full specialization: exact match on concrete arguments
				gTemplateRegistry.registerSpecialization(template_name, template_args, struct_node);
			} else {
				// Partial specialization: register as a pattern for matching
				gTemplateRegistry.registerSpecializationPattern(template_name, template_params, template_args, struct_node);
			}
		
			// Reset parsing context flags
			parsing_template_class_ = false;
			parsing_template_body_ = false;
		
			// Don't add specialization to AST - it's stored in the template registry
			// and will be used when Container<int> is instantiated
			std::cerr << "DEBUG: Specialization registered, returning no node\n";
			return saved_position.success();
		}
		
		// Handle partial specialization (template<typename T> struct X<T&>)
		if (is_partial_specialization) {
			std::cerr << "DEBUG: Parsing partial specialization\n";
			
			// Parse the struct/class keyword
			bool is_class = consume_keyword("class");
			if (!is_class) {
				consume_keyword("struct");
			}
			
			// Parse class name
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected class name", *current_token_);
			}
			
			Token class_name_token = *peek_token();
			std::string_view template_name = class_name_token.value();
			consume_token();
			
			// Parse the specialization pattern: <T&>, <T*, U>, etc.
			auto pattern_args_opt = parse_explicit_template_arguments();
			if (!pattern_args_opt.has_value()) {
				return ParseResult::error("Expected template argument pattern in partial specialization", *current_token_);
			}
			
			std::vector<TemplateTypeArg> pattern_args = *pattern_args_opt;
			
			// Generate a unique name for the pattern template
			// We use the template parameter names + modifiers to create unique pattern names
			// E.g., Container<T*> -> Container_pattern_TP
			//       Container<T**> -> Container_pattern_TPP
			//       Container<T&> -> Container_pattern_TR
			std::string pattern_name = std::string(template_name) + "_pattern";
			for (const auto& arg : pattern_args) {
				// Add modifiers to make pattern unique
				pattern_name += "_";
				// Add pointer markers
				for (size_t i = 0; i < arg.pointer_depth; ++i) {
					pattern_name += "P";
				}
				// Add reference markers
				if (arg.is_rvalue_reference) {
					pattern_name += "RR";
				} else if (arg.is_reference) {
					pattern_name += "R";
				}
				// Add const/volatile markers
				if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
					pattern_name += "C";
				}
				if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
					pattern_name += "V";
				}
			}
			std::string_view instantiated_name = StringBuilder().append(pattern_name).commit();
			
			std::cerr << "DEBUG: Partial specialization pattern generates name: " << instantiated_name << "\n";
			
			// Create a struct node for this specialization
			auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
				instantiated_name,
				is_class
			);
			
			// Expect opening brace
			if (!consume_punctuator("{")) {
				return ParseResult::error("Expected '{' after partial specialization header", *peek_token());
			}
			
			// Create struct type info
			TypeInfo& struct_type_info = add_struct_type(std::string(instantiated_name));
			
			AccessSpecifier current_access = struct_ref.default_access();
			
			// Set up member function context
			member_function_context_stack_.push_back({
				instantiated_name,
				struct_type_info.type_index_,
				&struct_ref
			});
			
			// Parse class body (same as full specialization)
			while (peek_token().has_value() && peek_token()->value() != "}") {
				// Check for access specifiers
				if (peek_token()->type() == Token::Type::Keyword) {
					if (peek_token()->value() == "public") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'public'", *peek_token());
						}
						current_access = AccessSpecifier::Public;
						continue;
					} else if (peek_token()->value() == "private") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'private'", *peek_token());
						}
						current_access = AccessSpecifier::Private;
						continue;
					} else if (peek_token()->value() == "protected") {
						consume_token();
						if (!consume_punctuator(":")) {
							return ParseResult::error("Expected ':' after 'protected'", *peek_token());
						}
						current_access = AccessSpecifier::Protected;
						continue;
					}
				}
				
				// Parse member declaration
				auto member_result = parse_declaration_or_function_definition();
				if (member_result.is_error()) {
					return member_result;
				}
				
				if (member_result.node().has_value()) {
					ASTNode member_node = *member_result.node();
					if (member_node.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_node = member_node.as<VariableDeclarationNode>();
						struct_ref.add_member(var_node.declaration_node(), current_access);
					} else if (member_node.is<FunctionDeclarationNode>()) {
						// Handle member function
						struct_ref.add_member_function(member_node, current_access);
					}
				}
				
				// Consume optional semicolon
				consume_punctuator(";");
			}
			
			// Expect closing brace
			if (!consume_punctuator("}")) {
				return ParseResult::error("Expected '}' after class body", *peek_token());
			}
			
			// Pop member function context
			member_function_context_stack_.pop_back();
			
			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after class declaration", *peek_token());
			}
			
			// Create StructTypeInfo with member information
			auto struct_info = std::make_unique<StructTypeInfo>(std::string(instantiated_name), struct_ref.default_access());
			
			// Add members to struct info
			for (const auto& member_decl : struct_ref.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				size_t member_size = get_type_size_bits(type_spec.type()) / 8;
				size_t member_alignment = get_type_alignment(type_spec.type(), member_size);
				
				bool is_ref_member = type_spec.is_reference();
				bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
				struct_info->addMember(
					std::string(decl.identifier_token().value()),
					type_spec.type(),
					type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					is_ref_member,
					is_rvalue_ref_member,
					(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(type_spec.type()) : 0
				);
			}
			
			// Add member functions to struct info
			for (const auto& member_func_decl : struct_ref.member_functions()) {
				const FunctionDeclarationNode& func_decl = member_func_decl.function_declaration.as<FunctionDeclarationNode>();
				const DeclarationNode& decl = func_decl.decl_node();
				
				struct_info->addMemberFunction(
					std::string(decl.identifier_token().value()),
					member_func_decl.function_declaration,
					member_func_decl.access,
					member_func_decl.is_virtual,
					member_func_decl.is_pure_virtual,
					member_func_decl.is_override,
					member_func_decl.is_final
				);
			}
			
			// Finalize the struct layout
			struct_info->finalize();
			
			// Store struct info
			struct_type_info.setStructInfo(std::move(struct_info));
			
			// Register the specialization PATTERN (not exact match)
			// This allows pattern matching during instantiation
			std::cerr << "DEBUG: Registering partial specialization PATTERN for " << template_name << "\n";
			for (size_t i = 0; i < pattern_args.size(); ++i) {
				std::cerr << "  Pattern arg " << i << ": base_type=" << static_cast<int>(pattern_args[i].base_type)
				          << " is_ref=" << pattern_args[i].is_reference
				          << " is_rvalue_ref=" << pattern_args[i].is_rvalue_reference
				          << " ptr_depth=" << pattern_args[i].pointer_depth << "\n";
			}
			gTemplateRegistry.registerSpecializationPattern(template_name, template_params, pattern_args, struct_node);
			
			std::cerr << "DEBUG: Returning partial specialization struct node\n";
			return saved_position.success(struct_node);
		}

		// Set flag to indicate we're parsing a template class
		// This will prevent delayed function bodies from being parsed immediately
		std::cerr << "DEBUG: About to set parsing_template_class_ = true\n";
		std::cerr << "DEBUG: Setting template parameter context for class template\n";
		std::cerr << "DEBUG: template_params.size() = " << template_params.size() << "\n";
		for (size_t i = 0; i < template_params.size(); ++i) {
			std::cerr << "DEBUG: template_params[" << i << "] type_name: " << template_params[i].type_name() << "\n";
			std::cerr << "DEBUG: template_params[" << i << "] is<TemplateParameterNode>: " << template_params[i].is<TemplateParameterNode>() << "\n";
			if (template_params[i].is<TemplateParameterNode>()) {
				const auto& tparam = template_params[i].as<TemplateParameterNode>();
				std::cerr << "DEBUG:   name='" << tparam.name() << "' kind=" << static_cast<int>(tparam.kind()) << "\n";
			}
		}
		parsing_template_class_ = true;
		parsing_template_body_ = true;
		template_param_names_.clear();
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
				template_param_names_.push_back(tparam.name());
			}
		}
		std::cerr << "DEBUG: Template parameter context set, parsing_template_body_ = " << parsing_template_body_ << "\n";
		
		// Set template parameter context for current_template_param_names_
		std::vector<std::string_view> template_param_names_for_body;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
				template_param_names_for_body.push_back(tparam.name());
			}
		}
		current_template_param_names_ = std::move(template_param_names_for_body);

		// Parse class template
		decl_result = parse_struct_declaration();

		// Clear template parameter context
		current_template_param_names_.clear();

		// Reset flag
		parsing_template_class_ = false;
		parsing_template_body_ = false;
		template_param_names_.clear();
		current_template_param_names_.clear();
	} else {
		std::cerr << "DEBUG: Parsing function template (not a class template)" << std::endl;
		// Could be:
		// 1. Deduction guide: template<typename T> ClassName(T) -> ClassName<T>;
		// 2. Function template: template<typename T> T max(T a, T b) { ... }
		// 3. Out-of-line member function: template<typename T> void Vector<T>::push_back(T v) { ... }

		// Check for deduction guide by looking for ClassName(...) -> pattern
		// Save position to peek ahead
		std::cerr << "DEBUG: Checking if this is a deduction guide..." << std::endl;
		auto deduction_guide_check_pos = save_token_position();
		bool is_deduction_guide = false;
		std::string_view guide_class_name;
		
		// Try to peek: if we see Identifier ( ... ) ->, it's likely a deduction guide
		if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
			guide_class_name = peek_token()->value();
			std::cerr << "DEBUG: Found identifier '" << guide_class_name << "'" << std::endl;
			consume_token();
			if (peek_token().has_value() && peek_token()->value() == "(") {
				std::cerr << "DEBUG: Found '(', skipping parameter list..." << std::endl;
				consume_token(); // consume '('
				// Skip parameter list
				int paren_depth = 1; // Start at 1 since we already consumed '('
				while (peek_token().has_value() && paren_depth > 0) {
					if (peek_token()->value() == "(") paren_depth++;
					else if (peek_token()->value() == ")") paren_depth--;
					consume_token();
				}
				std::cerr << "DEBUG: After params, next token: " << (peek_token().has_value() ? peek_token()->value() : "<EOF>") << std::endl;
				// Check for ->
				if (peek_token().has_value() && peek_token()->value() == "->") {
					is_deduction_guide = true;
					std::cerr << "DEBUG: Detected deduction guide pattern for " << guide_class_name << std::endl;
				}
			}
		}
		restore_token_position(deduction_guide_check_pos);
		std::cerr << "DEBUG: is_deduction_guide=" << is_deduction_guide << std::endl;
		
		if (is_deduction_guide) {
			std::cerr << "DEBUG: Detected deduction guide for " << guide_class_name << std::endl;
			
			// Parse: ClassName(params) -> ClassName<args>;
			// class name
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected class name in deduction guide", *current_token_);
			}
			std::string_view class_name = peek_token()->value();
			consume_token();
			
			// Parse parameter list
			if (!peek_token().has_value() || peek_token()->value() != "(") {
				return ParseResult::error("Expected '(' in deduction guide", *current_token_);
			}
			consume_token(); // consume '('
			
			std::vector<ASTNode> guide_params;
			if (peek_token().has_value() && peek_token()->value() != ")") {
				// Parse parameters
				while (true) {
					auto param_type_result = parse_type_specifier();
					if (param_type_result.is_error()) {
						return param_type_result;
					}
					guide_params.push_back(*param_type_result.node());

					// Allow pointer/reference declarators directly in guide parameters (e.g., T*, const T&, etc.)
					if (!guide_params.empty() && guide_params.back().is<TypeSpecifierNode>()) {
						TypeSpecifierNode& param_type = guide_params.back().as<TypeSpecifierNode>();

						// Parse pointer levels with optional CV-qualifiers
						while (peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
						       peek_token()->value() == "*") {
							consume_token(); // consume '*'

							CVQualifier ptr_cv = CVQualifier::None;
							while (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
								std::string_view kw = peek_token()->value();
								if (kw == "const") {
									ptr_cv = static_cast<CVQualifier>(
										static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Const));
									consume_token();
								}
								else if (kw == "volatile") {
									ptr_cv = static_cast<CVQualifier>(
										static_cast<uint8_t>(ptr_cv) | static_cast<uint8_t>(CVQualifier::Volatile));
									consume_token();
								}
								else {
									break;
								}
							}

							param_type.add_pointer_level(ptr_cv);
						}

						// Parse references (& or &&)
						if (peek_token().has_value() && peek_token()->value() == "&&") {
							param_type.set_reference(true);
							consume_token();
						}
						else if (peek_token().has_value() && peek_token()->value() == "&") {
							param_type.set_lvalue_reference(true);
							consume_token();
						}
					}
					
					// Optional parameter name (ignored)
					if (peek_token().has_value() && peek_token()->type() == Token::Type::Identifier) {
						consume_token();
					}
					
					if (peek_token().has_value() && peek_token()->value() == ",") {
						consume_token();
						continue;
					}
					break;
				}
			}
			
			if (!peek_token().has_value() || peek_token()->value() != ")") {
				return ParseResult::error("Expected ')' in deduction guide", *current_token_);
			}
			consume_token(); // consume ')'
			
			// Expect ->
			if (!peek_token().has_value() || peek_token()->value() != "->") {
				return ParseResult::error("Expected '->' in deduction guide", *current_token_);
			}
			consume_token(); // consume '->'
			
			// Parse deduced type: ClassName<args>
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected class name after '->' in deduction guide", *current_token_);
			}
			consume_token(); // consume class name (should match)
			
			// Parse template arguments
			std::vector<ASTNode> deduced_type_nodes;
			auto deduced_args_opt = parse_explicit_template_arguments(&deduced_type_nodes);
			if (!deduced_args_opt.has_value()) {
				return ParseResult::error("Expected template arguments in deduction guide", *current_token_);
			}
			if (deduced_type_nodes.size() != deduced_args_opt->size()) {
				return ParseResult::error("Unsupported deduction guide arguments", *current_token_);
			}
			
			// Expect semicolon
			if (!consume_punctuator(";")) {
				return ParseResult::error("Expected ';' after deduction guide", *current_token_);
			}
			
			// Create DeductionGuideNode
			auto guide_node = emplace_node<DeductionGuideNode>(
				std::move(template_params),
				class_name,
				std::move(guide_params),
				std::move(deduced_type_nodes)
			);
			
			// Register the deduction guide
			gTemplateRegistry.register_deduction_guide(class_name, guide_node);
			
			std::cerr << "DEBUG: Registered deduction guide for " << class_name << std::endl;
			return saved_position.success();
		}

		// Try to detect out-of-line member function definition
		// Pattern: ReturnType ClassName<TemplateArgs>::FunctionName(...)
		std::cerr << "DEBUG: Checking for out-of-line member function" << std::endl;
		auto out_of_line_result = try_parse_out_of_line_template_member(template_params, template_param_names);
		if (out_of_line_result.has_value()) {
			std::cerr << "DEBUG: Successfully parsed as out-of-line member function" << std::endl;
			return saved_position.success();  // Successfully parsed out-of-line definition
		}
		std::cerr << "DEBUG: Not an out-of-line member function, parsing as regular function template" << std::endl;

		// Otherwise, parse as function template
		// For function templates, we need to use delayed parsing for the body
		// instead of parsing it immediately like regular functions
		
		std::cerr << "DEBUG: Parsing function declaration for template" << std::endl;
		// Parse the function declaration manually to handle delayed body parsing
		auto type_and_name_result = parse_type_and_name();
		if (type_and_name_result.is_error()) {
			std::cerr << "DEBUG: parse_type_and_name failed: " << type_and_name_result.error_message() << std::endl;
			return type_and_name_result;
		}
		std::cerr << "DEBUG: parse_type_and_name succeeded" << std::endl;

		if (!type_and_name_result.node().has_value() || !type_and_name_result.node()->is<DeclarationNode>()) {
			std::cerr << "DEBUG: type_and_name_result has no DeclarationNode" << std::endl;
			return ParseResult::error("Expected function declaration after template parameter list", *current_token_);
		}
		std::cerr << "DEBUG: Got DeclarationNode from type_and_name" << std::endl;

		DeclarationNode& decl_node = type_and_name_result.node()->as<DeclarationNode>();

		std::cerr << "DEBUG: Parsing function parameters" << std::endl;
		// Parse function declaration with parameters
		auto func_result = parse_function_declaration(decl_node);
		if (func_result.is_error()) {
			std::cerr << "DEBUG: parse_function_declaration failed: " << func_result.error_message() << std::endl;
			return func_result;
		}
		std::cerr << "DEBUG: parse_function_declaration succeeded" << std::endl;

		if (!func_result.node().has_value()) {
			std::cerr << "DEBUG: func_result has no node" << std::endl;
			return ParseResult::error("Failed to create function declaration node", *current_token_);
		}
		std::cerr << "DEBUG: Got function declaration node" << std::endl;

		FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();

		std::cerr << "DEBUG: Checking for function body or semicolon" << std::endl;
		if (peek_token().has_value()) {
			std::cerr << "DEBUG: Next token: '" << peek_token()->value() << "'" << std::endl;
		}
		
		// Check if there's a function body or just a semicolon
		if (peek_token().has_value() && peek_token()->value() == ";") {
			// Just a declaration, consume the semicolon
			consume_token();
		} else if (peek_token().has_value() && peek_token()->value() == "{") {
			// Has a body - save position at the '{' 
			// This way when we restore, current_token_ will be '{' and we can parse normally
			TokenPosition body_start = save_token_position();
			
			// Store the body position in the function declaration so we can re-parse it later
			func_decl.set_template_body_position(body_start);
			
			// Skip over the body (skip_balanced_braces will consume the '{' and everything up to the matching '}')
			skip_balanced_braces();
		}

		decl_result = ParseResult::success(*func_result.node());
	}

	if (decl_result.is_error()) {
		return decl_result;
	}

	if (!decl_result.node().has_value()) {
		return ParseResult::error("Expected function or class declaration after template parameter list", *current_token_);
	}

	ASTNode decl_node = *decl_result.node();

	// Create appropriate template node based on what was parsed
	if (decl_node.is<FunctionDeclarationNode>()) {
		// Create a TemplateFunctionDeclarationNode
		auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
			std::move(template_params),
			decl_node
		);

		// Register the template in the template registry
		const FunctionDeclarationNode& func_decl = decl_node.as<FunctionDeclarationNode>();
		const DeclarationNode& func_decl_node = func_decl.decl_node();
		gTemplateRegistry.registerTemplate(func_decl_node.identifier_token().value(), template_func_node);

		// Add the template function to the symbol table so it can be found during overload resolution
		gSymbolTable.insert(func_decl_node.identifier_token().value(), template_func_node);

		return saved_position.success(template_func_node);
	} else if (decl_node.is<StructDeclarationNode>()) {
		// Create a TemplateClassDeclarationNode with parameter names for lookup
		std::vector<std::string_view> param_names;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				param_names.push_back(param.as<TemplateParameterNode>().name());
			}
		}
		
		auto template_class_node = emplace_node<TemplateClassDeclarationNode>(
			std::move(template_params),
			std::move(param_names),
			decl_node
		);

		// Register the template in the template registry
		const StructDeclarationNode& struct_decl = decl_node.as<StructDeclarationNode>();
		gTemplateRegistry.registerTemplate(struct_decl.name(), template_class_node);

		// Primary templates shouldn't be added to AST - only instantiations and specializations
		// Return success with no node so the caller doesn't add it to ast_nodes_
		std::cerr << "DEBUG: Registered primary template " << struct_decl.name() << ", returning no node\n";
		return saved_position.success();
	} else {
		return ParseResult::error("Unsupported template declaration type", *current_token_);
	}
}

// Parse template parameter list: typename T, int N, ...
ParseResult Parser::parse_template_parameter_list(std::vector<ASTNode>& out_params) {
	// Parse first parameter
	auto param_result = parse_template_parameter();
	if (param_result.is_error()) {
		return param_result;
	}

	if (param_result.node().has_value()) {
		out_params.push_back(*param_result.node());
	}

	// Parse additional parameters separated by commas
	while (peek_token().has_value() && peek_token()->value() == ",") {
		consume_token(); // consume ','

		param_result = parse_template_parameter();
		if (param_result.is_error()) {
			return param_result;
		}

		if (param_result.node().has_value()) {
			out_params.push_back(*param_result.node());
		}
	}

	return ParseResult::success();
}

// Parse a single template parameter: typename T, class T, int N, etc.
ParseResult Parser::parse_template_parameter() {
	ScopedTokenPosition saved_position(*this);

	std::cerr << "DEBUG: parse_template_parameter() called, current token: ";
	if (peek_token().has_value()) {
		std::cerr << "'" << peek_token()->value() << "' (type=" << static_cast<int>(peek_token()->type()) << ")";
	} else {
		std::cerr << "<EOF>";
	}
	std::cerr << std::endl;

	// Check for template template parameter: template<template<typename> class Container>
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword && peek_token()->value() == "template") {
		std::cerr << "DEBUG: Found 'template' keyword, parsing template template parameter" << std::endl;
		Token template_keyword = *peek_token();
		consume_token(); // consume 'template'

		// Expect '<' to start nested template parameter list
		if (!peek_token().has_value() || peek_token()->value() != "<") {
			std::cerr << "DEBUG: Expected '<' after 'template', got: ";
			if (peek_token().has_value()) std::cerr << "'" << peek_token()->value() << "'";
			else std::cerr << "<EOF>";
			std::cerr << std::endl;
			return ParseResult::error("Expected '<' after 'template' keyword in template template parameter", *current_token_);
		}
		consume_token(); // consume '<'

		// Parse nested template parameter forms (just type specifiers, no names)
		std::vector<ASTNode> nested_params;
		auto param_list_result = parse_template_template_parameter_forms(nested_params);
		if (param_list_result.is_error()) {
			std::cerr << "DEBUG: parse_template_template_parameter_forms failed" << std::endl;
			return param_list_result;
		}

		// Expect '>' to close nested template parameter list
		if (!peek_token().has_value() || peek_token()->value() != ">") {
			std::cerr << "DEBUG: Expected '>' after nested template parameter list, got: ";
			if (peek_token().has_value()) std::cerr << "'" << peek_token()->value() << "'";
			else std::cerr << "<EOF>";
			std::cerr << std::endl;
			return ParseResult::error("Expected '>' after nested template parameter list", *current_token_);
		}
		consume_token(); // consume '>'

		// Expect 'class' or 'typename'
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Keyword ||
		    (peek_token()->value() != "class" && peek_token()->value() != "typename")) {
			std::cerr << "DEBUG: Expected 'class' or 'typename' after template parameter list, got: ";
			if (peek_token().has_value()) std::cerr << "'" << peek_token()->value() << "'";
			else std::cerr << "<EOF>";
			std::cerr << std::endl;
			return ParseResult::error("Expected 'class' or 'typename' after template parameter list in template template parameter", *current_token_);
		}
		consume_token(); // consume 'class' or 'typename'

		// Expect identifier (parameter name)
		if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
			std::cerr << "DEBUG: Expected identifier for template template parameter name, got: ";
			if (peek_token().has_value()) std::cerr << "'" << peek_token()->value() << "'";
			else std::cerr << "<EOF>";
			std::cerr << std::endl;
			return ParseResult::error("Expected identifier for template template parameter name", *current_token_);
		}

		Token param_name_token = *peek_token();
		std::string_view param_name = param_name_token.value();
		consume_token(); // consume parameter name

		// Create template template parameter node
		auto param_node = emplace_node<TemplateParameterNode>(param_name, std::move(nested_params), param_name_token);

		std::cerr << "DEBUG: Successfully created template template parameter node for '" << param_name << "'" << std::endl;

		// TODO: Handle default arguments (e.g., template<typename> class Container = std::vector)

		return saved_position.success(param_node);
	}

	// Check for type parameter: typename or class
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view keyword = peek_token()->value();

		if (keyword == "typename" || keyword == "class") {
			Token keyword_token = *peek_token();
			consume_token(); // consume 'typename' or 'class'

			// Check for ellipsis (parameter pack): typename... Args
			bool is_variadic = false;
			if (peek_token().has_value() && 
			    (peek_token()->type() == Token::Type::Operator || peek_token()->type() == Token::Type::Punctuator) &&
			    peek_token()->value() == "...") {
				consume_token(); // consume '...'
				is_variadic = true;
			}

			// Expect identifier (parameter name)
			if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after 'typename' or 'class'", *current_token_);
			}

			Token param_name_token = *peek_token();
			std::string_view param_name = param_name_token.value();
			consume_token(); // consume parameter name

			// Create type parameter node
			auto param_node = emplace_node<TemplateParameterNode>(param_name, param_name_token);
			
			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}

			// Handle default arguments (e.g., typename T = int)
			// Note: Parameter packs cannot have default arguments
			if (!is_variadic && peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
			    peek_token()->value() == "=") {
				consume_token(); // consume '='
				
				// Parse the default type
				auto default_type_result = parse_type_specifier();
				if (default_type_result.is_error()) {
					return ParseResult::error("Expected type after '=' in template parameter default", *current_token_);
				}
				
				if (default_type_result.node().has_value()) {
					param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
				}
			}

			return saved_position.success(param_node);
		}
	}

	// Check for non-type parameter: int N, bool B, etc.
	// Parse type specifier
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}

	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type specifier for non-type template parameter", *current_token_);
	}

	// Check for ellipsis (parameter pack): int... Ns
	bool is_variadic = false;
	if (peek_token().has_value() && 
	    (peek_token()->type() == Token::Type::Operator || peek_token()->type() == Token::Type::Punctuator) &&
	    peek_token()->value() == "...") {
		consume_token(); // consume '...'
		is_variadic = true;
	}	// Expect identifier (parameter name)
	if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier for non-type template parameter", *current_token_);
	}

	Token param_name_token = *peek_token();
	std::string_view param_name = param_name_token.value();
	consume_token(); // consume parameter name

	// Create non-type parameter node
	auto param_node = emplace_node<TemplateParameterNode>(param_name, *type_result.node(), param_name_token);
	
	// Set variadic flag if this is a parameter pack
	if (is_variadic) {
		param_node.as<TemplateParameterNode>().set_variadic(true);
	}

	// Handle default arguments (e.g., int N = 10)
	// Note: Parameter packs cannot have default arguments
	if (!is_variadic && peek_token().has_value() && peek_token()->type() == Token::Type::Operator &&
	    peek_token()->value() == "=") {
		consume_token(); // consume '='
		
		// Parse the default value expression
		auto default_value_result = parse_expression();
		if (default_value_result.is_error()) {
			return ParseResult::error("Expected expression after '=' in template parameter default", *current_token_);
		}
		
		if (default_value_result.node().has_value()) {
			param_node.as<TemplateParameterNode>().set_default_value(*default_value_result.node());
		}
	}

	return saved_position.success(param_node);
}

// Parse template template parameter forms (just type specifiers without names)
// Used for template<template<typename> class Container> syntax
ParseResult Parser::parse_template_template_parameter_forms(std::vector<ASTNode>& out_params) {
	// Parse first parameter form
	auto param_result = parse_template_template_parameter_form();
	if (param_result.is_error()) {
		return param_result;
	}

	if (param_result.node().has_value()) {
		out_params.push_back(*param_result.node());
	}

	// Parse additional parameter forms separated by commas
	while (peek_token().has_value() && peek_token()->value() == ",") {
		consume_token(); // consume ','

		param_result = parse_template_template_parameter_form();
		if (param_result.is_error()) {
			return param_result;
		}

		if (param_result.node().has_value()) {
			out_params.push_back(*param_result.node());
		}
	}

	return ParseResult::success();
}

// Parse a single template template parameter form (just type specifier, no name)
// For template<template<typename> class Container>, this parses "typename"
ParseResult Parser::parse_template_template_parameter_form() {
	ScopedTokenPosition saved_position(*this);

	// Only support typename and class for now (no non-type parameters in template template parameters)
	if (peek_token().has_value() && peek_token()->type() == Token::Type::Keyword) {
		std::string_view keyword = peek_token()->value();

		if (keyword == "typename" || keyword == "class") {
			Token keyword_token = *peek_token();
			consume_token(); // consume 'typename' or 'class'

			// For template template parameters, we don't expect an identifier name
			// Just create a type parameter node with an empty name
			auto param_node = emplace_node<TemplateParameterNode>("", keyword_token);

			return saved_position.success(param_node);
		}
	}

	return ParseResult::error("Expected 'typename' or 'class' in template template parameter form", *current_token_);
}

// Parse member function template inside a class
// Pattern: template<typename U> ReturnType functionName(U param) { ... }
ParseResult Parser::parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access) {
	ScopedTokenPosition saved_position(*this);

	// Consume 'template' keyword
	if (!consume_keyword("template")) {
		return ParseResult::error("Expected 'template' keyword", *peek_token());
	}

	// Expect '<' to start template parameter list
	if (!peek_token().has_value() || peek_token()->value() != "<") {
		return ParseResult::error("Expected '<' after 'template' keyword", *current_token_);
	}
	consume_token(); // consume '<'

	// Parse template parameter list
	std::vector<ASTNode> template_params;
	std::vector<std::string_view> template_param_names;

	auto param_list_result = parse_template_parameter_list(template_params);
	if (param_list_result.is_error()) {
		return param_list_result;
	}

	// Expect '>' to close template parameter list
	if (!peek_token().has_value() || peek_token()->value() != ">") {
		return ParseResult::error("Expected '>' after template parameter list", *current_token_);
	}
	consume_token(); // consume '>'

	// Extract template parameter names
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			template_param_names.push_back(tparam.name());
		}
	}

	// Temporarily add template parameters to type system
	std::vector<TypeInfo*> template_type_infos;
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::Type) {
				auto& type_info = gTypeInfo.emplace_back(std::string(tparam.name()), Type::UserDefined, gTypeInfo.size());
				gTypesByName.emplace(type_info.name_, &type_info);
				template_type_infos.push_back(&type_info);
			}
		}
	}

	// Parse the member function declaration
	auto member_result = parse_type_and_name();
	if (member_result.is_error()) {
		// Clean up template parameters
		for (const auto* type_info : template_type_infos) {
			gTypesByName.erase(type_info->name_);
		}
		return member_result;
	}

	if (!member_result.node().has_value() || !member_result.node()->is<DeclarationNode>()) {
		// Clean up template parameters
		for (const auto* type_info : template_type_infos) {
			gTypesByName.erase(type_info->name_);
		}
		return ParseResult::error("Expected declaration node for member function template", *peek_token());
	}

	DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();

	// Parse function declaration with parameters
	auto func_result = parse_function_declaration(decl_node);
	if (func_result.is_error()) {
		// Clean up template parameters
		for (const auto* type_info : template_type_infos) {
			gTypesByName.erase(type_info->name_);
		}
		return func_result;
	}

	if (!func_result.node().has_value()) {
		// Clean up template parameters
		for (const auto* type_info : template_type_infos) {
			gTypesByName.erase(type_info->name_);
		}
		return ParseResult::error("Failed to create function declaration node", *peek_token());
	}

	FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();

	// Create a template function declaration node
	auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
		std::move(template_params),
		*func_result.node()
	);

	// Check if there's a function body or just a semicolon
	if (peek_token().has_value() && peek_token()->value() == ";") {
		// Just a declaration, consume the semicolon
		consume_token();
	} else if (peek_token().has_value() && peek_token()->value() == "{") {
		// Has a body - save position AT the '{'
		TokenPosition body_start = save_token_position();
		
		// Store the body position in the function declaration so we can re-parse it later
		func_decl.set_template_body_position(body_start);
		
		// Skip over the body (including the opening and closing braces)
		skip_balanced_braces();
	}

	// Add to struct as a member function template
	// Register the template in the global registry with qualified name (ClassName::functionName)
	std::string qualified_name = std::string(struct_node.name()) + "::" + std::string(decl_node.identifier_token().value());
	gTemplateRegistry.registerTemplate(qualified_name, template_func_node);

	// Clean up template parameters
	for (const auto* type_info : template_type_infos) {
		gTypesByName.erase(type_info->name_);
	}

	return saved_position.success();
}

// Parse explicit template arguments: <int, float, ...>
// Returns a vector of types if successful, nullopt otherwise
std::optional<std::vector<TemplateTypeArg>> Parser::parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes) {
	// Save position in case this isn't template arguments
	auto saved_pos = save_token_position();

	// Check for '<'
	if (!peek_token().has_value() || peek_token()->value() != "<") {
		return std::nullopt;
	}
	consume_token(); // consume '<'

	std::vector<TemplateTypeArg> template_args;

	// Check for empty template argument list (e.g., Container<>)
	if (peek_token().has_value() && peek_token()->value() == ">") {
		consume_token(); // consume '>'
		// Success - discard saved position
		discard_saved_token(saved_pos);
		return template_args;  // Return empty vector
	}

	// Parse template arguments
	while (true) {
		// Save position in case type parsing fails
		TokenPosition arg_saved_pos = save_token_position();

		// First, try to parse an expression (for non-type template parameters)
		auto expr_result = parse_primary_expression();
		if (!expr_result.is_error() && expr_result.node().has_value()) {
			// Successfully parsed an expression - check if it's a numeric literal
			const ExpressionNode& expr = expr_result.node()->as<ExpressionNode>();
			if (std::holds_alternative<NumericLiteralNode>(expr)) {
				const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
				const auto& val = lit.value();
				if (std::holds_alternative<unsigned long long>(val)) {
					template_args.emplace_back(static_cast<int64_t>(std::get<unsigned long long>(val)));
					std::cerr << "DEBUG: parse_explicit_template_arguments parsed numeric literal: " 
					          << std::get<unsigned long long>(val) << "\n";
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>'
				} else if (std::holds_alternative<double>(val)) {
					template_args.emplace_back(static_cast<int64_t>(std::get<double>(val)));
					std::cerr << "DEBUG: parse_explicit_template_arguments parsed double literal: " 
					          << std::get<double>(val) << "\n";
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>'
				} else {
					std::cerr << "DEBUG: Unsupported numeric literal type\n";
					restore_token_position(saved_pos);
					return std::nullopt;
				}
				
				// Check for ',' or '>' after the numeric literal
				if (!peek_token().has_value()) {
					std::cerr << "DEBUG: parse_explicit_template_arguments unexpected end of tokens after numeric literal\n";
					restore_token_position(saved_pos);
					return std::nullopt;
				}

				if (peek_token()->value() == ">") {
					consume_token(); // consume '>'
					break;
				}

				if (peek_token()->value() == ",") {
					consume_token(); // consume ','
					continue;
				}

				// Unexpected token after numeric literal
				std::cerr << "DEBUG: parse_explicit_template_arguments unexpected token after numeric literal: '" 
				          << peek_token()->value() << "'\n";
				restore_token_position(saved_pos);
				return std::nullopt;
			}

			// Expression is not a numeric literal - fall through to type parsing
		}

		// Expression parsing failed or wasn't a numeric literal - try parsing a type
		restore_token_position(arg_saved_pos);
		auto type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			// Neither type nor expression parsing worked
			std::cerr << "DEBUG: parse_explicit_template_arguments failed to parse type or expression\n";
			restore_token_position(saved_pos);
			return std::nullopt;
		}

		// Successfully parsed a type
		TypeSpecifierNode& type_node = type_result.node()->as<TypeSpecifierNode>();
		
		// Check for pointer modifiers (*) - for patterns like T*, T**, etc.
		while (peek_token().has_value() && peek_token()->value() == "*") {
			consume_token(); // consume '*'
			type_node.add_pointer_level(CVQualifier::None);
			std::cerr << "DEBUG: parse_explicit_template_arguments found pointer level\n";
		}
		
		// Check for reference modifier (&) or rvalue reference (&&)
		if (peek_token().has_value() && peek_token()->value() == "&&") {
			// Rvalue reference - single && token
			consume_token(); // consume '&&'
			type_node.set_reference(true);  // is_rvalue = true
			std::cerr << "DEBUG: parse_explicit_template_arguments found rvalue reference (&&)\n";
		} else if (peek_token().has_value() && peek_token()->value() == "&") {
			consume_token(); // consume '&'
			
			// Check for second & (though lexer usually combines them)
			if (peek_token().has_value() && peek_token()->value() == "&") {
				consume_token(); // consume second '&'
				type_node.set_reference(true);  // is_rvalue = true
				std::cerr << "DEBUG: parse_explicit_template_arguments found rvalue reference (& &)\n";
			} else {
				type_node.set_reference(false); // is_rvalue = false (lvalue reference)
				std::cerr << "DEBUG: parse_explicit_template_arguments found lvalue reference (&)\n";
			}
		}

		// Create TemplateTypeArg from the fully parsed type
		template_args.push_back(TemplateTypeArg(type_node));
		if (out_type_nodes) {
			out_type_nodes->push_back(*type_result.node());
		}

		// Check for ',' or '>'
		if (!peek_token().has_value()) {
			std::cerr << "DEBUG: parse_explicit_template_arguments unexpected end of tokens\n";
			restore_token_position(saved_pos);
			return std::nullopt;
		}

		if (peek_token()->value() == ">") {
			consume_token(); // consume '>'
			break;
		}

		if (peek_token()->value() == ",") {
			consume_token(); // consume ','
			continue;
		}

		// Unexpected token
		std::cerr << "DEBUG: parse_explicit_template_arguments unexpected token: '" << peek_token()->value() << "'\n";
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Success - discard saved position
	discard_saved_token(saved_pos);
	return template_args;
}

// Try to instantiate a template with explicit template arguments
std::optional<ASTNode> Parser::try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types) {
	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
	if (!template_opt.has_value()) {
		return std::nullopt;  // No template with this name
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Verify we have the right number of template arguments
	if (explicit_types.size() != template_params.size()) {
		return std::nullopt;  // Wrong number of template arguments
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		if (param.kind() == TemplateParameterKind::Template) {
			// Template template parameter - the explicit_types[i] should be a template name
			// For now, we'll assume it's passed as a string in the type system
			// TODO: Implement proper template template argument parsing
			template_args.push_back(TemplateArgument::makeTemplate(""));  // Placeholder
		} else {
			// Convert TemplateTypeArg to Type for now (losing reference information in this path)
			// TODO: Extend TemplateArgument to support full type information
			template_args.push_back(TemplateArgument::makeType(explicit_types[i].base_type));
		}
	}

	// Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = std::string(template_name);
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	// Instantiate the template (same logic as try_instantiate_template)
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(template_name, template_args);

	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Create a token for the mangled name
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Create return type with the first template argument
	ASTNode return_type = emplace_node<TypeSpecifierNode>(
		template_args[0].type_value,
		TypeQualifier::None,
		get_type_size_bits(template_args[0].type_value),
		Token()
	);

	auto new_decl = emplace_node<DeclarationNode>(return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_decl.as<DeclarationNode>());

	// Add parameters with concrete types
	for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
		const auto& param = func_decl.parameter_nodes()[i];
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();

			// Use the deduced type (simplified - assumes all params are T)
			ASTNode param_type = emplace_node<TypeSpecifierNode>(
				template_args[0].type_value,
				TypeQualifier::None,
				get_type_size_bits(template_args[0].type_value),
				Token()
			);

			auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Copy the function body if it exists
	auto orig_body = func_decl.get_definition();
	if (orig_body.has_value()) {
		new_func_ref.set_definition(orig_body.value());
	}

	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	// Add to symbol table
	gSymbolTable.insert(mangled_token.value(), new_func_node);

	// Add to top-level AST so it gets visited by the code generator
	std::cerr << "DEBUG [7619 try_instantiate_template]: Adding function: " << mangled_token.value() << "\n";
	ast_nodes_.push_back(new_func_node);

	return new_func_node;
}

// Try to instantiate a function template with the given argument types
// Returns the instantiated function declaration node if successful
std::optional<ASTNode> Parser::try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types) {
	static int recursion_depth = 0;
	recursion_depth++;
	
	if (recursion_depth > 10) {
		std::cerr << "ERROR: try_instantiate_template recursion depth exceeded 10! Possible infinite loop for template '" << template_name << "'" << std::endl;
		recursion_depth--;
		return std::nullopt;
	}
	
	std::cerr << "DEBUG [depth=" << recursion_depth << "]: try_instantiate_template() called for '" << template_name << "' with " << arg_types.size() << " arguments" << std::endl;

	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
	if (!template_opt.has_value()) {
		std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template '" << template_name << "' not found in registry" << std::endl;
		recursion_depth--;
		return std::nullopt;  // No template with this name
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template '" << template_name << "' is not a function template" << std::endl;
		recursion_depth--;
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template has " << template_params.size() << " parameters" << std::endl;

	// Step 1: Deduce template arguments from function call arguments
	// For now, we support simple type parameter deduction
	// We deduce template parameters in order from function arguments
	// TODO: Add support for non-type parameters and more complex deduction

	// Check if we have only variadic parameters - they can be empty
	bool all_variadic = true;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();
		if (!param.is_variadic()) {
			all_variadic = false;
			break;
		}
	}

	if (arg_types.empty() && !all_variadic) {
		recursion_depth--;
		return std::nullopt;  // No arguments to deduce from
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	std::vector<Type> deduced_type_args;  // For types extracted from instantiated names

	// Deduce template parameters in order from function arguments
	// For template<typename T, typename U> T func(T a, U b):
	//   - T is deduced from first argument
	//   - U is deduced from second argument
	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();

		std::cerr << "DEBUG: Processing template parameter '" << param.name() << "' of kind " << static_cast<int>(param.kind()) << std::endl;

		if (param.kind() == TemplateParameterKind::Template) {
			std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template template parameter found, attempting deduction" << std::endl;
			// Template template parameter - deduce from argument type
			if (arg_index < arg_types.size()) {
				const TypeSpecifierNode& arg_type = arg_types[arg_index];
				
				// Template template parameters can only be deduced from struct types
				if (arg_type.type() == Type::Struct) {
					// Get the struct name (e.g., "Vector_int")
					TypeIndex type_index = arg_type.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						std::string_view instantiated_name = type_info.name_;
						
						std::cerr << "DEBUG [depth=" << recursion_depth << "]: Argument is struct type '" << instantiated_name << "'" << std::endl;
						
						// Parse the instantiated name to extract template name and type arguments
						// Format: template_name_type1_type2_...
						size_t first_underscore = instantiated_name.find('_');
						if (first_underscore != std::string_view::npos) {
							std::string_view template_name = instantiated_name.substr(0, first_underscore);
							
							std::cerr << "DEBUG [depth=" << recursion_depth << "]: Extracted template name '" << template_name << "'" << std::endl;
							
							// Check if this template exists
							auto template_check = gTemplateRegistry.lookupTemplate(template_name);
							if (template_check.has_value()) {
								std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template '" << template_name << "' exists, adding to template args" << std::endl;
								template_args.push_back(TemplateArgument::makeTemplate(template_name));
								
								// Extract type arguments from the remaining parts
								std::string_view remaining = instantiated_name.substr(first_underscore + 1);
								size_t pos = 0;
								while (pos < remaining.size()) {
									size_t next_underscore = remaining.find('_', pos);
									std::string_view type_str = (next_underscore == std::string_view::npos) 
										? remaining.substr(pos) 
										: remaining.substr(pos, next_underscore - pos);
									
									// Convert type string to Type
									Type deduced_type = TemplateRegistry::stringToType(type_str);
									if (deduced_type != Type::Invalid) {
										deduced_type_args.push_back(deduced_type);
										std::cerr << "DEBUG [depth=" << recursion_depth << "]: Extracted type argument '" << type_str << "' -> " << static_cast<int>(deduced_type) << std::endl;
									} else {
										std::cerr << "DEBUG [depth=" << recursion_depth << "]: Unknown type string '" << type_str << "' in instantiated name '" << instantiated_name << "'" << std::endl;
										recursion_depth--;
										return std::nullopt;
									}
									
									if (next_underscore == std::string_view::npos) break;
									pos = next_underscore + 1;
								}
								
								arg_index++;
							} else {
								std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template '" << template_name << "' not found" << std::endl;
								recursion_depth--;
								return std::nullopt;
							}
						} else {
							std::cerr << "DEBUG [depth=" << recursion_depth << "]: Could not extract template name from '" << instantiated_name << "'" << std::endl;
							recursion_depth--;
							return std::nullopt;
						}
					} else {
						std::cerr << "DEBUG [depth=" << recursion_depth << "]: Invalid type index " << static_cast<int>(type_index) << std::endl;
						recursion_depth--;
						return std::nullopt;
					}
				} else {
					std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template template parameter requires struct argument, got type " << static_cast<int>(arg_type.type()) << std::endl;
					recursion_depth--;
					return std::nullopt;
				}
			} else {
				std::cerr << "DEBUG [depth=" << recursion_depth << "]: Not enough arguments to deduce template template parameter" << std::endl;
				recursion_depth--;
				return std::nullopt;
			}
		} else if (param.kind() == TemplateParameterKind::Type) {
			// Type parameter - check if it's variadic (parameter pack)
			if (param.is_variadic()) {
				// Deduce all remaining argument types for this parameter pack
				while (arg_index < arg_types.size()) {
					// Store full TypeSpecifierNode to preserve reference info for perfect forwarding
					template_args.push_back(TemplateArgument::makeTypeSpecifier(arg_types[arg_index]));
					arg_index++;
				}
				
				// Note: If no arguments remain, the pack is empty (which is valid)
			} else {
				// Non-variadic type parameter
				if (!deduced_type_args.empty()) {
					Type deduced_type = deduced_type_args[0];
					template_args.push_back(TemplateArgument::makeType(deduced_type));
					deduced_type_args.erase(deduced_type_args.begin());
				} else if (arg_index < arg_types.size()) {
					template_args.push_back(TemplateArgument::makeType(arg_types[arg_index].type()));
					arg_index++;
				} else {
					// Not enough arguments to deduce all template parameters
					// Fall back to first argument for remaining parameters
					template_args.push_back(TemplateArgument::makeType(arg_types[0].type()));
				}
			}
		} else {
			// Non-type parameter - not yet supported in deduction
			// TODO: Implement non-type parameter deduction
			std::cerr << "DEBUG [depth=" << recursion_depth << "]: Non-type parameter not supported in deduction" << std::endl;
			recursion_depth--;
			return std::nullopt;
		}
	}

	// Step 2: Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = std::string(template_name);
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	std::cerr << "DEBUG [depth=" << recursion_depth << "]: Checking for existing instantiation with key: template_name='" << key.template_name << "', " 
	          << key.type_arguments.size() << " type args, " << key.template_arguments.size() << " template args" << std::endl;

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		std::cerr << "DEBUG [depth=" << recursion_depth << "]: Found existing instantiation, returning it" << std::endl;
		recursion_depth--;
		return *existing_inst;  // Return existing instantiation
	}

	std::cerr << "DEBUG [depth=" << recursion_depth << "]: No existing instantiation found, creating new one" << std::endl;

	// Step 3: Instantiate the template
	// For Phase 2, we'll create a simplified instantiation
	// We'll just use the original function with a mangled name
	// Full AST cloning and substitution will be implemented later

	// Generate mangled name for the instantiation
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(template_name, template_args);

	std::cerr << "DEBUG: Instantiating template '" << template_name << "' -> '" << mangled_name << "'" << std::endl;

	// For now, we'll create a simple wrapper that references the original function
	// This is a temporary solution - proper instantiation requires:
	// 1. Cloning the entire AST subtree
	// 2. Substituting all template parameter references
	// 3. Type checking the instantiated code

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Convert template_args to TemplateTypeArg format for substitution
	std::vector<TemplateTypeArg> template_args_as_type_args;
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			TemplateTypeArg type_arg;
			type_arg.base_type = arg.type_value;
			type_arg.type_index = 0;  // Simple types don't have an index
			template_args_as_type_args.push_back(type_arg);
		}
		// Note: Template and value arguments aren't used in type substitution
	}

	// Create a token for the mangled name
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Create return type - substitute template parameters if needed
	const TypeSpecifierNode& orig_return_type = orig_decl.type_node().as<TypeSpecifierNode>();
	
	// Use the new substitution helper to handle complex types (const T&, T*, etc.)
	auto [return_type_enum, return_type_index] = substitute_template_parameter(
		orig_return_type, template_params, template_args_as_type_args
	);

	ASTNode return_type = emplace_node<TypeSpecifierNode>(
		return_type_enum,
		TypeQualifier::None,
		get_type_size_bits(return_type_enum),
		Token()
	);

	auto new_decl = emplace_node<DeclarationNode>(return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_decl.as<DeclarationNode>());

	// Add parameters with substituted types
	size_t arg_type_index = 0;  // Track which argument type we're using
	for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
		const auto& param = func_decl.parameter_nodes()[i];
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			
			// Check if this is a parameter pack
			if (param_decl.is_parameter_pack()) {
				// Track how many elements this pack expands to
				size_t pack_start_index = arg_type_index;
				
				// Check if the original parameter type is an rvalue reference (for perfect forwarding)
				const TypeSpecifierNode& orig_param_type = param_decl.type_node().as<TypeSpecifierNode>();
				bool is_forwarding_reference = orig_param_type.is_rvalue_reference();
				
				// Expand the parameter pack into multiple parameters
				// Use all remaining argument types for this pack
				while (arg_type_index < arg_types.size()) {
					const TypeSpecifierNode& arg_type = arg_types[arg_type_index];
					
					// Create a new parameter with the concrete type
					ASTNode param_type = emplace_node<TypeSpecifierNode>(
						arg_type.type(),
						arg_type.qualifier(),
						arg_type.size_in_bits(),
						Token()
					);
					param_type.as<TypeSpecifierNode>().set_type_index(arg_type.type_index());
				
					// If the original parameter was a forwarding reference (T&&), apply reference collapsing
					// Reference collapsing rules:
					//   T& && → T&    (lvalue reference wins)
					//   T&& && → T&&  (both rvalue → rvalue)
					//   T && → T&&    (non-reference + && → rvalue reference)
					if (is_forwarding_reference) {
						if (arg_type.is_lvalue_reference()) {
							// Deduced type is lvalue reference (e.g., int&)
							// Applying && gives int& && which collapses to int&
							param_type.as<TypeSpecifierNode>().set_lvalue_reference(true);
							std::cerr << "DEBUG [depth=" << recursion_depth << "]: Forwarding ref + lvalue → lvalue reference" << std::endl;
						} else if (arg_type.is_rvalue_reference()) {
							// Deduced type is rvalue reference (e.g., int&&)
							// Applying && gives int&& && which collapses to int&&
							param_type.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
							std::cerr << "DEBUG [depth=" << recursion_depth << "]: Forwarding ref + rvalue → rvalue reference" << std::endl;
						} else {
							// Deduced type is non-reference (e.g., int from literal)
							// Applying && gives int&&
							param_type.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
							std::cerr << "DEBUG [depth=" << recursion_depth << "]: Forwarding ref + non-ref → rvalue reference" << std::endl;
						}
					}
				
					// Copy pointer levels and CV qualifiers
					for (const auto& ptr_level : arg_type.pointer_levels()) {
						param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
					}
					
					// Create parameter name: base_name + index (e.g., args_0, args_1, ...)
					StringBuilder param_name_builder;
					param_name_builder.append(param_decl.identifier_token().value());
					param_name_builder.append('_');
					param_name_builder.append(static_cast<int>(arg_type_index));
					std::string_view param_name = param_name_builder.commit();
					
					Token param_token(Token::Type::Identifier, 
									 param_name,
									 param_decl.identifier_token().line(),
									 param_decl.identifier_token().column(),
									 param_decl.identifier_token().file_index());
				
					auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_token);
					new_func_ref.add_parameter_node(new_param_decl);
					
					arg_type_index++;
				}
				
				// Record the pack expansion size for use during body re-parsing
				//size_t pack_size = arg_type_index - pack_start_index;
				// We'll set this before re-parsing the body below
				
			} else {
				// Regular parameter - substitute template parameters in the parameter type
				if (arg_type_index < arg_types.size()) {
					const TypeSpecifierNode& arg_type = arg_types[arg_type_index];
					
					ASTNode param_type = emplace_node<TypeSpecifierNode>(
						arg_type.type(),
						arg_type.qualifier(),
						arg_type.size_in_bits(),
						Token()
					);
					param_type.as<TypeSpecifierNode>().set_type_index(arg_type.type_index());
					
					auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
					new_func_ref.add_parameter_node(new_param_decl);
					
					arg_type_index++;
				}
			}
		}
	}

	// Handle the function body
	// Check if the template has a body position stored for re-parsing
	if (func_decl.has_template_body_position()) {
		std::cerr << "DEBUG: Template has body position, re-parsing function body" << std::endl;
		// Re-parse the function body with template parameters substituted
		const std::vector<ASTNode>& template_params = template_func.template_parameters();
		
		// Temporarily add the concrete types to the type system with template parameter names
		std::vector<TypeInfo*> temp_type_infos;
		std::vector<std::string_view> param_names;
		for (const auto& tparam_node : template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			Type concrete_type = template_args[i].type_value;

			auto& type_info = gTypeInfo.emplace_back(std::string(param_name), concrete_type, gTypeInfo.size());
			gTypesByName.emplace(type_info.name_, &type_info);
			temp_type_infos.push_back(&type_info);
			std::cerr << "DEBUG: Added temp type info for '" << param_name << "' -> type " << static_cast<int>(concrete_type) << std::endl;
		}

		// Save current position
		TokenPosition current_pos = save_token_position();
		std::cerr << "DEBUG: Saved current position, cursor=" << current_pos.cursor_ << std::endl;

		// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
		restore_lexer_position_only(func_decl.template_body_position());
		std::cerr << "DEBUG: Restored to body position" << std::endl;

		// Set up parsing context for the function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
				std::cerr << "DEBUG: Added parameter '" << param_decl.identifier_token().value() << "' to symbol table" << std::endl;
			}
		}

		std::cerr << "DEBUG: About to call parse_block()" << std::endl;
		// Parse the function body
		auto block_result = parse_block();
		std::cerr << "DEBUG: parse_block() returned, error=" << block_result.is_error() << ", has_value=" << block_result.node().has_value() << std::endl;
		if (!block_result.is_error() && block_result.node().has_value()) {
			// After parsing, we need to substitute template parameters in the body
			// This is essential for features like fold expressions that need AST transformation
			// Convert template_args to TemplateArgument format for substitution
			std::vector<TemplateArgument> converted_template_args;
			for (const auto& arg : template_args) {
				if (arg.kind == TemplateArgument::Kind::Type) {
					converted_template_args.push_back(TemplateArgument::makeType(arg.type_value));
				} else if (arg.kind == TemplateArgument::Kind::Value) {
					converted_template_args.push_back(TemplateArgument::makeValue(arg.int_value));
				}
			}
		
			ASTNode substituted_body = substituteTemplateParameters(
				*block_result.node(),
				template_params,
				converted_template_args
			);
		
			new_func_ref.set_definition(substituted_body);
			std::cerr << "DEBUG: Set function definition with substituted body" << std::endl;
		}
		
		// Clean up context
		current_function_ = nullptr;
		gSymbolTable.exit_scope();

		// Restore original position (lexer only - keep AST nodes we created)
		restore_lexer_position_only(current_pos);
		std::cerr << "DEBUG: Restored original position" << std::endl;

		// Remove temporary type infos
		for (const auto* type_info : temp_type_infos) {
			gTypesByName.erase(type_info->name_);
			std::cerr << "DEBUG: Removed temp type info for '" << type_info->name_ << "'" << std::endl;
		}
	} else {
		// Fallback: copy the function body pointer directly (old behavior)
		auto orig_body = func_decl.get_definition();
		if (orig_body.has_value()) {
			new_func_ref.set_definition(orig_body.value());
			std::cerr << "DEBUG: Copied original function body (fallback)" << std::endl;
		} else {
			std::cerr << "DEBUG: No function body to copy" << std::endl;
		}
	}

	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	// Add to symbol table at GLOBAL scope (not current scope)
	// Template instantiations should be globally visible, not scoped to where they're called
	
	// Save current scope depth
	size_t scopes_to_exit = 0;
	while (gSymbolTable.get_current_scope_type() != ScopeType::Global) {
		gSymbolTable.exit_scope();
		scopes_to_exit++;
	}
	
	// Insert at global scope
	gSymbolTable.insert(mangled_token.value(), new_func_node);
	
	// Restore scopes
	for (size_t i = 0; i < scopes_to_exit; ++i) {
		gSymbolTable.enter_scope(ScopeType::Function);  // Assume function scopes for now
	}

	// Add to top-level AST so it gets visited by the code generator
	ast_nodes_.push_back(new_func_node);

	std::cerr << "DEBUG [depth=" << recursion_depth << "]: Template instantiation completed successfully" << std::endl;
	recursion_depth--;
	return new_func_node;
}

// Get the mangled name for an instantiated class template
// Example: Container<int> -> Container_int
std::string_view Parser::get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	StringBuilder result;
	result.append(template_name);
	result.append("_");

	for (size_t i = 0; i < template_args.size(); ++i) {
		if (i > 0) result.append("_");

		result.append(template_args[i].toString());
	}

	return result.commit();
}

// Helper function to substitute template parameters in an expression
// This recursively traverses the expression tree and replaces constructor calls with template parameter types
ASTNode Parser::substitute_template_params_in_expression(
	const ASTNode& expr,
	const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map) {
	
	// ASTNode wraps types via std::any, check if it contains an ExpressionNode
	if (!expr.is<ExpressionNode>()) {
		return expr; // Return as-is if not an expression
	}
	
	const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
	
	// Handle constructor call: T(value) -> ConcreteType(value)
	if (std::holds_alternative<ConstructorCallNode>(expr_variant)) {
		const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr_variant);
		const TypeSpecifierNode& ctor_type = ctor.type_node().as<TypeSpecifierNode>();
		
		// Check if this constructor type is in our substitution map
		// For variable templates with cleaned-up template parameters, the constructor
		// might have type_index=0 or some other invalid value. So we check if there's
		// exactly one entry in the map and assume any UserDefined constructor is for that type.
		if (ctor_type.type() == Type::UserDefined) {
			// If we have exactly one type substitution and this is a UserDefined constructor,
			// assume it's for the template parameter
			if (type_substitution_map.size() == 1) {
				const TemplateTypeArg& arg = type_substitution_map.begin()->second;
				
				// Create a new type specifier with the concrete type
				TypeSpecifierNode new_type(
					arg.base_type,
					TypeQualifier::None,
					get_type_size_bits(arg.base_type),
					ctor.called_from()
				);
				
				// Recursively substitute in arguments
				ChunkedVector<ASTNode> new_args;
				for (size_t i = 0; i < ctor.arguments().size(); ++i) {
					new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map));
				}
				
				// Create new constructor call with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				ConstructorCallNode new_ctor(new_type_node, std::move(new_args), ctor.called_from());
				return emplace_node<ExpressionNode>(new_ctor);
			}
		}
		
		// Not a template parameter constructor - recursively substitute in arguments
		ChunkedVector<ASTNode> new_args;
		for (size_t i = 0; i < ctor.arguments().size(); ++i) {
			new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map));
		}
		ConstructorCallNode new_ctor(ctor.type_node(), std::move(new_args), ctor.called_from());
		return emplace_node<ExpressionNode>(new_ctor);
	}
	
	// Handle binary operators - recursively substitute in both operands
	if (std::holds_alternative<BinaryOperatorNode>(expr_variant)) {
		const BinaryOperatorNode& binop = std::get<BinaryOperatorNode>(expr_variant);
		auto new_left = substitute_template_params_in_expression(
			binop.get_lhs(), type_substitution_map);
		auto new_right = substitute_template_params_in_expression(
			binop.get_rhs(), type_substitution_map);
		
		BinaryOperatorNode new_binop(
			binop.get_token(),
			new_left,
			new_right
		);
		return emplace_node<ExpressionNode>(new_binop);
	}
	
	// Handle unary operators - recursively substitute in operand
	if (std::holds_alternative<UnaryOperatorNode>(expr_variant)) {
		const UnaryOperatorNode& unop = std::get<UnaryOperatorNode>(expr_variant);
		auto new_operand = substitute_template_params_in_expression(
			unop.get_operand(), type_substitution_map);
		
		UnaryOperatorNode new_unop(
			unop.get_token(),
			new_operand,
			unop.is_prefix()
		);
		return emplace_node<ExpressionNode>(new_unop);
	}
	
	// For other expression types (literals, identifiers, etc.), return as-is
	return expr;
}

// Try to instantiate a class template with explicit template arguments
// Returns the instantiated StructDeclarationNode if successful
// Try to instantiate a variable template with the given template arguments
// Returns the instantiated variable declaration node or nullopt if already instantiated
std::optional<ASTNode> Parser::try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	// Look up the variable template
	auto template_opt = gTemplateRegistry.lookupVariableTemplate(template_name);
	if (!template_opt.has_value()) {
		std::cerr << "ERROR: Variable template '" << template_name << "' not found\n";
		return std::nullopt;
	}
	
	if (!template_opt->is<TemplateVariableDeclarationNode>()) {
		std::cerr << "ERROR: Expected TemplateVariableDeclarationNode\n";
		return std::nullopt;
	}
	
	const TemplateVariableDeclarationNode& var_template = template_opt->as<TemplateVariableDeclarationNode>();
	
	// Generate unique name for the instantiation
	std::string instantiated_name = std::string(template_name);
	for (const auto& arg : template_args) {
		instantiated_name += "_";
		instantiated_name += std::string(TemplateRegistry::typeToString(arg.base_type));
		if (arg.is_rvalue_reference) instantiated_name += "_rvalref";
		else if (arg.is_reference) instantiated_name += "_ref";
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			instantiated_name += "_ptr";
		}
	}
	std::string_view persistent_name = StringBuilder().append(instantiated_name).commit();
	
	// Check if already instantiated
	if (gSymbolTable.lookup(persistent_name).has_value()) {
		return gSymbolTable.lookup(persistent_name);
	}
	
	// Perform template substitution
	const std::vector<ASTNode>& template_params = var_template.template_parameters();
	if (template_args.size() != template_params.size()) {
		std::cerr << "ERROR: Template argument count mismatch: expected " << template_params.size() 
		          << ", got " << template_args.size() << "\n";
		return std::nullopt;
	}
	
	// Get the original variable declaration
	const VariableDeclarationNode& orig_var_decl = var_template.variable_decl_node();
	const DeclarationNode& orig_decl = orig_var_decl.declaration();
	const TypeSpecifierNode& orig_type = orig_decl.type_node().as<TypeSpecifierNode>();
	
	// Build a map from template parameter type_index to concrete type for substitution
	std::unordered_map<TypeIndex, TemplateTypeArg> type_substitution_map;
	
	// Substitute template parameter with concrete type
	// For now, assume simple case where the type is just the template parameter
	TypeSpecifierNode substituted_type = orig_type;
	
	// Check if the type is a template parameter
	for (size_t i = 0; i < template_params.size(); ++i) {
		if (!template_params[i].is<TemplateParameterNode>()) continue;
		
		const auto& tparam = template_params[i].as<TemplateParameterNode>();
		if (tparam.kind() != TemplateParameterKind::Type) continue;
		
		// For variable templates, if the variable type is UserDefined, it's likely the template parameter
		// We can't look it up in gTypesByName because it was cleaned up after parsing
		// So we'll assume the orig_type IS the template parameter and add it to the map
		if (orig_type.type() == Type::UserDefined) {
			const TemplateTypeArg& arg = template_args[i];
			
			// Add to substitution map
			type_substitution_map[orig_type.type_index()] = arg;
			
			substituted_type = TypeSpecifierNode(
				arg.base_type,
				TypeQualifier::None,
				get_type_size_bits(arg.base_type),
				Token()
			);
			// Apply cv-qualifiers, references, and pointers from template argument
			if (arg.is_rvalue_reference) {
				substituted_type.set_reference(true);
			} else if (arg.is_reference) {
				substituted_type.set_lvalue_reference(true);
			}
			for (size_t p = 0; p < arg.pointer_depth; ++p) {
				substituted_type.add_pointer_level(CVQualifier::None);
			}
			break;
		}
	}
	
	// Create new declaration with substituted type and instantiated name
	Token instantiated_name_token(Token::Type::Identifier, persistent_name, 0, 0, 0);
	auto new_type_node = emplace_node<TypeSpecifierNode>(substituted_type);
	auto new_decl_node = emplace_node<DeclarationNode>(new_type_node, instantiated_name_token);
	
	// Substitute template parameters in initializer expression
	std::optional<ASTNode> new_initializer = std::nullopt;
	if (orig_var_decl.initializer().has_value()) {
		new_initializer = substitute_template_params_in_expression(
			orig_var_decl.initializer().value(),
			type_substitution_map
		);
	}
	
	// Create instantiated variable declaration
	auto instantiated_var_decl = emplace_node<VariableDeclarationNode>(
		new_decl_node,
		new_initializer,
		orig_var_decl.storage_class()
	);
	
	// Register the DeclarationNode in symbol table (not the VariableDeclarationNode)
	// This is how normal variables are registered
	// IMPORTANT: Use insertGlobal because we might be called during function parsing
	// but we need to insert into global scope
	bool insert_result = gSymbolTable.insertGlobal(persistent_name, new_decl_node);
	
	// Verify it's there
	auto verify = gSymbolTable.lookup(persistent_name);
	
	// Add to AST at the beginning so it gets code-generated before functions that use it
	// Insert after other global declarations but before function definitions
	ast_nodes_.insert(ast_nodes_.begin(), instantiated_var_decl);
	
	return instantiated_var_decl;
}

std::optional<ASTNode> Parser::try_instantiate_class_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	std::cerr << "DEBUG: try_instantiate_class_template called with template_name='" << template_name << "' and " << template_args.size() << " args\n";

	// 1) Full/Exact specialization lookup
	// If there is an exact specialization registered for (template_name, template_args),
	// it always wins over partial specializations and the primary template.
	if (!template_args.empty()) {
		auto exact_spec = gTemplateRegistry.lookupExactSpecialization(template_name, template_args);
		if (exact_spec.has_value()) {
			std::cerr << "DEBUG: Found exact (non-pattern) specialization for '" << template_name << "'\n";
			return exact_spec;
		}
	}
	
	// Generate the instantiated class name first
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	std::cerr << "DEBUG: Target instantiated name: '" << instantiated_name << "'\n";

	// Check if we already have this instantiation
	auto existing_type = gTypesByName.find(instantiated_name);
	if (existing_type != gTypesByName.end()) {
		std::cerr << "DEBUG: Type already exists, returning nullopt\n";
		return std::nullopt;
	}
	
	// First, check if there's an exact specialization match
	std::cerr << "DEBUG: Looking up specialization for " << template_name << " with " << template_args.size() << " args\n";
	for (size_t i = 0; i < template_args.size(); ++i) {
		std::cerr << "  Arg " << i << ": base_type=" << static_cast<int>(template_args[i].base_type) 
		          << " is_ref=" << template_args[i].is_reference
		          << " is_rvalue_ref=" << template_args[i].is_rvalue_reference
		          << " ptr_depth=" << template_args[i].pointer_depth << "\n";
	}
	
	// Try to match a specialization pattern
	auto pattern_match_opt = gTemplateRegistry.matchSpecializationPattern(template_name, template_args);
	if (pattern_match_opt.has_value()) {
		std::cerr << "DEBUG: Found matching specialization pattern!\n";
		// Found a matching pattern - we need to instantiate it with concrete types
		const ASTNode& pattern_node = *pattern_match_opt;
		
		if (!pattern_node.is<StructDeclarationNode>()) {
			std::cerr << "DEBUG: Pattern node is not a StructDeclarationNode\n";
			return std::nullopt;
		}
		
		const StructDeclarationNode& pattern_struct = pattern_node.as<StructDeclarationNode>();
		
		// Get template parameters from the primary template
		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (!template_opt.has_value() || !template_opt->is<TemplateClassDeclarationNode>()) {
			std::cerr << "ERROR: Could not find primary template for pattern specialization\n";
			return std::nullopt;
		}
		const TemplateClassDeclarationNode& primary_template = template_opt->as<TemplateClassDeclarationNode>();
		const std::vector<ASTNode>& template_params = primary_template.template_parameters();
		
		// Create a new struct with the instantiated name
		// Copy members from the pattern, substituting template parameters
		// For now, if members use template parameters, we substitute them
		
		// Create struct type info first
		TypeInfo& struct_type_info = add_struct_type(std::string(instantiated_name));
		auto struct_info = std::make_unique<StructTypeInfo>(std::string(instantiated_name), pattern_struct.default_access());
		
		// Copy members from pattern
		for (const auto& member_decl : pattern_struct.members()) {
			const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
			const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
			
			// For pattern specializations, members should already have concrete types
			// since the pattern was parsed with concrete types (e.g., T& was parsed as a reference)
			// We just copy them as-is
			
			// Calculate member size accounting for pointer depth
			size_t member_size;
			if (type_spec.pointer_depth() > 0 || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
				// Pointers and references are always 8 bytes (64-bit)
				member_size = 8;
			} else {
				member_size = get_type_size_bits(type_spec.type()) / 8;
			}
			size_t member_alignment = get_type_alignment(type_spec.type(), member_size);
			
			bool is_ref_member = type_spec.is_reference();
			bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
			struct_info->addMember(
				std::string(decl.identifier_token().value()),
				type_spec.type(),
				type_spec.type_index(),
				member_size,
				member_alignment,
				member_decl.access,
				member_decl.default_initializer,
				is_ref_member,
				is_rvalue_ref_member,
				(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(type_spec.type()) : 0
			);
		}
		
		// Copy member functions from pattern
		for (const StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
			const FunctionDeclarationNode& func_decl = mem_func.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func_decl.decl_node();
			struct_info->addMemberFunction(
				std::string(decl.identifier_token().value()),
				mem_func.function_declaration,
				mem_func.access,
				mem_func.is_virtual,
				mem_func.is_pure_virtual,
				mem_func.is_override,
				mem_func.is_final
			);
		}

		// Copy static members from the pattern
		// Get the pattern's StructTypeInfo
		auto pattern_type_it = gTypesByName.find(std::string(pattern_struct.name()));
		if (pattern_type_it != gTypesByName.end()) {
			const TypeInfo* pattern_type_info = pattern_type_it->second;
			const StructTypeInfo* pattern_struct_info = pattern_type_info->getStructInfo();
			if (pattern_struct_info) {
				std::cerr << "DEBUG: Copying " << pattern_struct_info->static_members.size() << " static members from pattern\n";
				for (const auto& static_member : pattern_struct_info->static_members) {
					std::cerr << "DEBUG: Copying static member: " << static_member.name << "\n";
					struct_info->addStaticMember(
						static_member.name,
						static_member.type,
						static_member.type_index,
						static_member.size,
						static_member.alignment,
						static_member.access,
						static_member.initializer,
						static_member.is_const
					);
				}
			}
		}
		
		struct_info->finalize();
		struct_type_info.setStructInfo(std::move(struct_info));
		
		// Create an AST node for the instantiated struct so member functions can be code-generated
		auto instantiated_struct = emplace_node<StructDeclarationNode>(
			instantiated_name,
			false  // is_class
		);
		StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
		
		// Copy data members
		for (const auto& member_decl : pattern_struct.members()) {
			instantiated_struct_ref.add_member(
				member_decl.declaration,
				member_decl.access,
				member_decl.default_initializer
			);
		}
		
		// Copy member functions
		for (const StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
			instantiated_struct_ref.add_member_function(
				mem_func.function_declaration,
				mem_func.access
			);
		}
		
		std::cerr << "DEBUG: Pattern instantiation complete for " << instantiated_name << "\n";
		return instantiated_struct;  // Return the struct node for code generation
	}
	std::cerr << "DEBUG: No pattern match found, using primary template\n";

	// No specialization found - use the primary template
	auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
	std::cerr << "DEBUG: lookupTemplate('" << template_name << "') returned " << (template_opt.has_value() ? "found" : "not found") << "\n";
	if (!template_opt.has_value()) {
		std::cerr << "DEBUG: No primary template found, returning nullopt\n";
		return std::nullopt;  // No template with this name
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateClassDeclarationNode>()) {
		std::cerr << "DEBUG: Template node is not a TemplateClassDeclarationNode, returning nullopt\n";
		return std::nullopt;  // Not a class template
	}

	const TemplateClassDeclarationNode& template_class = template_node.as<TemplateClassDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_class.template_parameters();
	const StructDeclarationNode& class_decl = template_class.class_decl_node();

	// Count non-variadic parameters
	size_t non_variadic_param_count = 0;
	bool has_parameter_pack = false;
	size_t parameter_pack_index = 0;
	
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		if (param.is_variadic()) {
			has_parameter_pack = true;
			parameter_pack_index = i;
		} else {
			non_variadic_param_count++;
		}
	}
	
	std::cerr << "DEBUG: Template has " << non_variadic_param_count << " non-variadic params, has_pack=" << has_parameter_pack << "\n";

	// Verify we have the right number of template arguments
	// For variadic templates: args.size() >= non_variadic_param_count
	// For non-variadic templates: args.size() <= template_params.size()
	if (has_parameter_pack) {
		// With parameter pack, we need at least the non-variadic parameters
		if (template_args.size() < non_variadic_param_count) {
			std::cerr << "DEBUG: Too few arguments for variadic template (got " << template_args.size() 
			          << ", need at least " << non_variadic_param_count << ")\n";
			return std::nullopt;
		}
		// The rest of the arguments go into the parameter pack
	} else {
		// Non-variadic template: allow fewer arguments if remaining parameters have defaults
		if (template_args.size() > template_params.size()) {
			return std::nullopt;  // Too many template arguments
		}
	}
	
	// Create a mutable copy of template_args to fill in defaults
	std::vector<TemplateTypeArg> filled_template_args(template_args.begin(), template_args.end());
	
	// Fill in default arguments for missing parameters
	for (size_t i = filled_template_args.size(); i < template_params.size(); ++i) {
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		// Skip variadic parameters - they're allowed to be empty
		if (param.is_variadic()) {
			continue;
		}
		
		if (!param.has_default()) {
			std::cerr << "DEBUG: Param " << i << " has no default, returning nullopt\n";
			return std::nullopt;  // Missing required template argument
		}
		
		// Use the default value
		if (param.kind() == TemplateParameterKind::Type) {
			// For type parameters with defaults, extract the type
			const ASTNode& default_node = param.default_value();
			if (default_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
				filled_template_args.push_back(TemplateTypeArg(default_type));
			}
		} else if (param.kind() == TemplateParameterKind::NonType) {
			// For non-type parameters with defaults, evaluate the expression
			const ASTNode& default_node = param.default_value();
			if (default_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = default_node.as<ExpressionNode>();
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
					const auto& val = lit.value();
					if (std::holds_alternative<unsigned long long>(val)) {
						int64_t int_val = static_cast<int64_t>(std::get<unsigned long long>(val));
						filled_template_args.push_back(TemplateTypeArg(int_val));
					} else if (std::holds_alternative<double>(val)) {
						int64_t int_val = static_cast<int64_t>(std::get<double>(val));
						filled_template_args.push_back(TemplateTypeArg(int_val));
					}
				}
			}
		}
	}
	
	// Use the filled template args for the rest of the function
	const std::vector<TemplateTypeArg>& template_args_to_use = filled_template_args;

	// Generate the instantiated class name (again, with filled args)
	instantiated_name = get_instantiated_class_name(template_name, template_args_to_use);
	std::cerr << "DEBUG: Checking if '" << instantiated_name << "' already exists (after default filling)\n";

	// Check if we already have this instantiation (after filling defaults)
	existing_type = gTypesByName.find(instantiated_name);
	if (existing_type != gTypesByName.end()) {
		std::cerr << "DEBUG: Type already exists, returning nullopt\n";
		// Already instantiated, return the existing struct node
		// We need to find the struct node in the AST
		// For now, just return nullopt and let the type lookup handle it
		return std::nullopt;
	}

	// Create a new struct type for the instantiation (but don't create AST node for template instantiations)
	TypeInfo& struct_type_info = add_struct_type(std::string(instantiated_name));
	
	// Create StructTypeInfo
	auto struct_info = std::make_unique<StructTypeInfo>(std::string(instantiated_name), AccessSpecifier::Public);

	// Copy members from the template, substituting template parameters with concrete types
	for (const auto& member_decl : class_decl.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

		// Substitute template parameter if the member type is a template parameter
		auto [member_type, member_type_index] = substitute_template_parameter(
			type_spec, template_params, template_args_to_use
		);

		// Handle array size substitution for non-type template parameters
		std::optional<ASTNode> substituted_array_size;
		if (decl.is_array()) {
			if (decl.array_size().has_value()) {
				const ASTNode& array_size_node = *decl.array_size();
				
				// The array size might be stored directly or wrapped in different node types
				// Try to extract the identifier or value from various possible representations
				std::optional<std::string_view> identifier_name;
				std::optional<int64_t> literal_value;
				
				// Check if it's an ExpressionNode
				if (array_size_node.is<ExpressionNode>()) {
					const ExpressionNode& expr = array_size_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr)) {
						const IdentifierNode& ident = std::get<IdentifierNode>(expr);
						identifier_name = ident.name();
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
						identifier_name = tparam_ref.param_name();
					} else if (std::holds_alternative<NumericLiteralNode>(expr)) {
						const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
						const auto& val = lit.value();
						if (std::holds_alternative<unsigned long long>(val)) {
							literal_value = static_cast<int64_t>(std::get<unsigned long long>(val));
						}
					}
				}
				// Check if it's a direct IdentifierNode (shouldn't happen, but be safe)
				else if (array_size_node.is<IdentifierNode>()) {
					const IdentifierNode& ident = array_size_node.as<IdentifierNode>();
					identifier_name = ident.name();
				}
				
				// If we found an identifier, try to substitute it with a non-type template parameter value
				if (identifier_name.has_value()) {
					// Try to find which non-type template parameter this is
					for (size_t i = 0; i < template_params.size(); ++i) {
						const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
						if (tparam.kind() == TemplateParameterKind::NonType && tparam.name() == *identifier_name) {
							// Found the non-type parameter - substitute with the actual value
							if (i < template_args_to_use.size() && template_args_to_use[i].is_value) {
								// Create a numeric literal node with the substituted value
								int64_t val = template_args_to_use[i].value;
								Token num_token(Token::Type::Literal, std::to_string(val), 0, 0, 0);
								auto num_literal = emplace_node<ExpressionNode>(
									NumericLiteralNode(num_token, static_cast<unsigned long long>(val), Type::Int, TypeQualifier::None, 32)
								);
								substituted_array_size = num_literal;
								break;
							}
						}
					}
				}
			} else {
				std::cerr << "DEBUG: Array does NOT have array_size!\n";
			}
			
			// If we didn't substitute, keep the original array size
			if (!substituted_array_size.has_value()) {
				substituted_array_size = decl.array_size();
			}
		}

		// Create the substituted type specifier
		// IMPORTANT: Preserve the base CV qualifier from the original type!
		// For example: const T* should become const int* when T=int
		auto substituted_type = emplace_node<TypeSpecifierNode>(
			member_type,
			member_type_index,
			get_type_size_bits(member_type),
			Token(),
			type_spec.cv_qualifier()  // Preserve const/volatile qualifier
		);

		// Copy pointer levels from the original type specifier
		auto& substituted_type_spec = substituted_type.as<TypeSpecifierNode>();
		for (const auto& ptr_level : type_spec.pointer_levels()) {
			substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
		}

		// Preserve reference qualifiers from the original type
		if (type_spec.is_rvalue_reference()) {
			substituted_type_spec.set_reference(true);  // true for rvalue reference
		} else if (type_spec.is_reference()) {
			substituted_type_spec.set_reference(false);  // false for lvalue reference
		}
		
		// Add to the instantiated struct
		// new_struct_ref.add_member(new_member_decl, member_decl.access, member_decl.default_initializer);

		// Calculate member size - for arrays, multiply element size by array size
		size_t member_size;
		if (substituted_array_size.has_value()) {
			// Extract the array size value
			size_t array_size = 1;
			const ASTNode& size_node = *substituted_array_size;
			if (size_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = size_node.as<ExpressionNode>();
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
					const auto& val = lit.value();
					if (std::holds_alternative<unsigned long long>(val)) {
						array_size = static_cast<size_t>(std::get<unsigned long long>(val));
					}
				}
			}
			member_size = (get_type_size_bits(member_type) / 8) * array_size;
		} else {
			// Check if the ORIGINAL type is a pointer or reference (use original type_spec, not substituted member_type)
			if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
				member_size = 8;  // Pointers and references are 64-bit on x64
			} else {
				member_size = get_type_size_bits(member_type) / 8;
			}
		}

		size_t member_alignment = get_type_alignment(member_type, member_size);
		bool is_ref_member = type_spec.is_reference();
		bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
	
		// For reference members, we need to pass the size of the referenced type, not the pointer size
		size_t referenced_size_bits = 0;
		if (is_ref_member || is_rvalue_ref_member) {
			referenced_size_bits = get_type_size_bits(member_type);
		}
	
		struct_info->addMember(
			std::string(decl.identifier_token().value()),
			member_type,
			member_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer,
			is_ref_member,
			is_rvalue_ref_member,
			referenced_size_bits
		);
	}

	// Skip member function instantiation - we only need type information for nested classes
	// Member functions will be instantiated on-demand when called

	// Copy nested classes from the template with template parameter substitution
	std::cerr << "DEBUG: Copying " << class_decl.nested_classes().size() << " nested classes\n";
	for (const auto& nested_class : class_decl.nested_classes()) {
		std::cerr << "DEBUG: Processing nested class\n";
		if (nested_class.is<StructDeclarationNode>()) {
			const StructDeclarationNode& nested_struct = nested_class.as<StructDeclarationNode>();
			std::string qualified_name = std::string(instantiated_name) + "::" + std::string(nested_struct.name());
			std::cerr << "DEBUG: Registering nested class " << qualified_name << "\n";
			
			// Create a new StructTypeInfo for the nested class
			auto nested_struct_info = std::make_unique<StructTypeInfo>(qualified_name, nested_struct.default_access());
			
			// Copy and substitute members from the nested class
			for (const auto& member_decl : nested_struct.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				// Create a substituted type specifier
				TypeSpecifierNode substituted_type_spec(
					type_spec.type(),
					type_spec.qualifier(),
					type_spec.size_in_bits(),
					Token()  // Empty token
				);
				
				// Copy pointer levels from the original type specifier
				for (const auto& ptr_level : type_spec.pointer_levels()) {
					substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
				}
				
				// Substitute template parameters in the base type
				if (substituted_type_spec.type() == Type::UserDefined) {
					TypeIndex type_idx = substituted_type_spec.type_index();
					if (type_idx < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_idx];
						std::string_view type_name = type_info.name_;
						
						// Try to find which template parameter this is
						for (size_t i = 0; i < template_params.size(); ++i) {
							const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
							if (tparam.name() == type_name) {
								// This is a template parameter - substitute with concrete type
								substituted_type_spec = TypeSpecifierNode(
									template_args_to_use[i].base_type,
									TypeQualifier::None,
									get_type_size_bits(template_args_to_use[i].base_type),
									Token()
								);
								// Copy pointer levels to the new type specifier
								for (const auto& ptr_level : type_spec.pointer_levels()) {
									substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
								}
								break;
							}
						}
					}
				}
				
				// Add member to the nested struct info
				// For pointers, use pointer size (64 bits on x64), otherwise use type size
				size_t member_size;
				if (substituted_type_spec.is_pointer()) {
					member_size = 8;  // 64-bit pointer
				} else {
					member_size = substituted_type_spec.size_in_bits() / 8;
				}
				size_t member_alignment = get_type_alignment(substituted_type_spec.type(), member_size);
				
				bool is_ref_member = substituted_type_spec.is_reference();
				bool is_rvalue_ref_member = substituted_type_spec.is_rvalue_reference();
				nested_struct_info->addMember(
					std::string(decl.identifier_token().value()),
					substituted_type_spec.type(),
					substituted_type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					is_ref_member,
					is_rvalue_ref_member,
					(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(substituted_type_spec.type()) : 0
				);
			}
			
			// Finalize the nested struct layout
			nested_struct_info->finalize();
			
			// Register the nested class in the type system
			auto& nested_type_info = gTypeInfo.emplace_back(qualified_name, Type::Struct, gTypeInfo.size());
			nested_type_info.setStructInfo(std::move(nested_struct_info));
			gTypesByName.emplace(qualified_name, &nested_type_info);
		}
	}

	// Copy type aliases from the template with template parameter substitution
	for (const auto& type_alias : class_decl.type_aliases()) {
		std::string qualified_alias_name = std::string(instantiated_name) + "::" + std::string(type_alias.alias_name);
		
		// Get the aliased type and substitute template parameters
		const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
		
		// Create a substituted type specifier
		Type substituted_type = alias_type_spec.type();
		TypeIndex substituted_type_index = alias_type_spec.type_index();
		unsigned char substituted_size = alias_type_spec.size_in_bits();
		
		// Substitute template parameters in the alias type
		if (substituted_type == Type::UserDefined) {
			TypeIndex type_idx = alias_type_spec.type_index();
			if (type_idx < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx];
				std::string_view type_name = type_info.name_;
				
				// Try to find which template parameter this is
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name) {
						// This is a template parameter - substitute with concrete type
						substituted_type = template_args_to_use[i].base_type;
						substituted_type_index = template_args_to_use[i].type_index;
						substituted_size = get_type_size_bits(substituted_type);
						break;
					}
				}
			}
		}
		
		// Register the type alias in gTypesByName
		auto& alias_type_info = gTypeInfo.emplace_back(qualified_alias_name, substituted_type, gTypeInfo.size());
		alias_type_info.type_index_ = substituted_type_index;
		alias_type_info.type_size_ = substituted_size;
		gTypesByName.emplace(alias_type_info.name_, &alias_type_info);
	}

	// Finalize the struct layout
	struct_info->finalize();

	// Store struct info in type info
	struct_type_info.setStructInfo(std::move(struct_info));

	// Get a pointer to the moved struct_info for later use
	StructTypeInfo* struct_info_ptr = struct_type_info.getStructInfo();

	// Create an AST node for the instantiated struct so member functions can be code-generated
	auto instantiated_struct = emplace_node<StructDeclarationNode>(
		instantiated_name,
		false  // is_class
	);
	StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
	
	// Copy member functions from the template
	std::cerr << "DEBUG: Copying " << class_decl.member_functions().size() << " member functions from primary template\n";
	for (const StructMemberFunctionDecl& mem_func : class_decl.member_functions()) {
		std::cerr << "DEBUG: Processing member function, is_constructor=" << mem_func.is_constructor 
		          << " is_destructor=" << mem_func.is_destructor << "\n";

		if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = mem_func.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func_decl.decl_node();
			std::cerr << "DEBUG: Copying member function: " << decl.identifier_token().value()
			          << " has_definition=" << func_decl.get_definition().has_value() << "\n";

			// If the function has a definition, we need to substitute template parameters
			if (func_decl.get_definition().has_value()) {
				std::cerr << "DEBUG: Substituting template parameters in member function body\n";

				// Create a new function declaration with substituted body
				auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
					decl.type_node(), decl.identifier_token()
				);
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_func_decl_ref, instantiated_name
				);

				// Copy parameters
				for (const auto& param : func_decl.parameter_nodes()) {
					new_func_ref.add_parameter_node(param);
				}

				// Substitute template parameters in the function body
				// Convert TemplateTypeArg vector to TemplateArgument vector
				std::vector<TemplateArgument> converted_template_args;
				for (const auto& ttype_arg : template_args_to_use) {
					if (ttype_arg.is_value) {
						converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value));
					} else {
						converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
					}
				}

				try {
					std::cerr << "DEBUG: About to call substituteTemplateParameters\n";
					ASTNode substituted_body = substituteTemplateParameters(
						*func_decl.get_definition(),
						template_params,
						converted_template_args
					);
					std::cerr << "DEBUG: substituteTemplateParameters completed successfully\n";
					new_func_ref.set_definition(substituted_body);
				} catch (const std::exception& e) {
					std::cerr << "ERROR: Exception during template parameter substitution for function " 
					          << decl.identifier_token().value() << ": " << e.what() << "\n";
					throw;
				} catch (...) {
					std::cerr << "ERROR: Unknown exception during template parameter substitution for function " 
					          << decl.identifier_token().value() << "\n";
					throw;
				}

				// Add the substituted function to the instantiated struct
				instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
			} else {
				// No definition to substitute, copy directly
				instantiated_struct_ref.add_member_function(
					mem_func.function_declaration,
					mem_func.access
				);
			}
		} else if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& ctor_decl = mem_func.function_declaration.as<ConstructorDeclarationNode>();
			std::cerr << "DEBUG: Copying constructor: " << ctor_decl.name()
			          << " has_definition=" << ctor_decl.get_definition().has_value() << "\n";

			if (ctor_decl.get_definition().has_value()) {
				std::cerr << "DEBUG: Substituting template parameters in constructor body\n";

				// Convert TemplateTypeArg vector to TemplateArgument vector
				std::vector<TemplateArgument> converted_template_args;
				for (const auto& ttype_arg : template_args_to_use) {
					if (ttype_arg.is_value) {
						converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value));
					} else {
						converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
					}
				}

				try {
					std::cerr << "DEBUG: About to call substituteTemplateParameters for constructor\n";
					ASTNode substituted_body = substituteTemplateParameters(
						*ctor_decl.get_definition(),
						template_params,
						converted_template_args
					);
					std::cerr << "DEBUG: substituteTemplateParameters completed for constructor\n";
					
					// Create a new constructor declaration with substituted body
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name,
						instantiated_name
					);
					
					// Copy parameters
					for (const auto& param : ctor_decl.parameter_nodes()) {
						new_ctor_ref.add_parameter_node(param);
					}
					
					// Copy other properties
					for (const auto& init : ctor_decl.member_initializers()) {
						new_ctor_ref.add_member_initializer(init.member_name, init.initializer_expr);
					}
					for (const auto& init : ctor_decl.base_initializers()) {
						new_ctor_ref.add_base_initializer(init.base_class_name, init.arguments);
					}
					if (ctor_decl.delegating_initializer().has_value()) {
						new_ctor_ref.set_delegating_initializer(ctor_decl.delegating_initializer()->arguments);
					}
					new_ctor_ref.set_is_implicit(ctor_decl.is_implicit());
					new_ctor_ref.set_definition(substituted_body);
					
					// Add the substituted constructor to the instantiated struct
					instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);
				} catch (const std::exception& e) {
					std::cerr << "ERROR: Exception during template parameter substitution for constructor " 
					          << ctor_decl.name() << ": " << e.what() << "\n";
					throw;
				} catch (...) {
					std::cerr << "ERROR: Unknown exception during template parameter substitution for constructor " 
					          << ctor_decl.name() << "\n";
					throw;
				}
			} else {
				// No definition to substitute, copy directly
				instantiated_struct_ref.add_constructor(
					mem_func.function_declaration,
					mem_func.access
				);
			}
		} else if (mem_func.function_declaration.is<DestructorDeclarationNode>()) {
			const DestructorDeclarationNode& dtor_decl = mem_func.function_declaration.as<DestructorDeclarationNode>();
			std::cerr << "DEBUG: Copying destructor: " << dtor_decl.name()
			          << " has_definition=" << dtor_decl.get_definition().has_value() << "\n";

			if (dtor_decl.get_definition().has_value()) {
				std::cerr << "DEBUG: Substituting template parameters in destructor body\n";

				// Convert TemplateTypeArg vector to TemplateArgument vector
				std::vector<TemplateArgument> converted_template_args;
				for (const auto& ttype_arg : template_args_to_use) {
					if (ttype_arg.is_value) {
						converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value));
					} else {
						converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
					}
				}

				try {
					std::cerr << "DEBUG: About to call substituteTemplateParameters for destructor\n";
					ASTNode substituted_body = substituteTemplateParameters(
						*dtor_decl.get_definition(),
						template_params,
						converted_template_args
					);
					std::cerr << "DEBUG: substituteTemplateParameters completed for destructor\n";
					
					// Create a new destructor declaration with substituted body
					std::string_view specialized_dtor_name = StringBuilder()
						.append("~")
						.append(instantiated_name)
						.commit();
					auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
						instantiated_name,
						specialized_dtor_name
					);
					
					new_dtor_ref.set_definition(substituted_body);
					
					// Add the substituted destructor to the instantiated struct
					instantiated_struct_ref.add_destructor(new_dtor_node, mem_func.access);
				} catch (const std::exception& e) {
					std::cerr << "ERROR: Exception during template parameter substitution for destructor " 
					          << dtor_decl.name() << ": " << e.what() << "\n";
					throw;
				} catch (...) {
					std::cerr << "ERROR: Unknown exception during template parameter substitution for destructor " 
					          << dtor_decl.name() << "\n";
					throw;
				}
			} else {
				// No definition to substitute, copy directly
				instantiated_struct_ref.add_destructor(
					mem_func.function_declaration,
					mem_func.access
				);
			}
		} else {
			std::cerr << "ERROR: Unknown member function type in template instantiation: " 
			          << mem_func.function_declaration.type_name() << "\n";
			// Copy directly as fallback
			instantiated_struct_ref.add_member_function(
				mem_func.function_declaration,
				mem_func.access
			);
		}
	}

	// Copy static members from the primary template
	// Get the primary template's StructTypeInfo
	auto primary_type_it = gTypesByName.find(std::string(template_name));
	if (primary_type_it != gTypesByName.end()) {
		const TypeInfo* primary_type_info = primary_type_it->second;
		const StructTypeInfo* primary_struct_info = primary_type_info->getStructInfo();
		if (primary_struct_info) {
			std::cerr << "DEBUG: Copying " << primary_struct_info->static_members.size() << " static members from primary template\n";
			for (const auto& static_member : primary_struct_info->static_members) {
				std::cerr << "DEBUG: Copying static member: " << static_member.name << "\n";
				
				// Check if initializer contains sizeof...(pack_name) and substitute with pack size
				std::optional<ASTNode> substituted_initializer = static_member.initializer;
				if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
					const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
					if (std::holds_alternative<SizeofPackNode>(expr)) {
						const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
						std::string_view pack_name = sizeof_pack.pack_name();
						
						// Find which template parameter is the pack
						size_t pack_size = 0;
						for (size_t i = 0; i < template_params.size(); ++i) {
							const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
							if (tparam.name() == pack_name && tparam.is_variadic()) {
								// Calculate pack size: total args - non-variadic params
								size_t non_variadic_count = 0;
								for (const auto& param : template_params) {
									if (!param.as<TemplateParameterNode>().is_variadic()) {
										non_variadic_count++;
									}
								}
								pack_size = template_args_to_use.size() - non_variadic_count;
								std::cerr << "DEBUG: sizeof...(" << pack_name << ") = " << pack_size << "\n";
								
								// Create a constant expression with the pack size
								// Use StringBuilder to create a persistent string_view
								std::string_view pack_size_str = StringBuilder().append(std::to_string(pack_size)).commit();
								Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
								auto num_literal = emplace_node<ExpressionNode>(
									NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
								);
								substituted_initializer = num_literal;
								break;
							}
						}
					}
				}
				
				// Use struct_info_ptr instead of struct_info (which was moved)
				struct_info_ptr->addStaticMember(
					static_member.name,
					static_member.type,
					static_member.type_index,
					static_member.size,
					static_member.alignment,
					static_member.access,
					substituted_initializer,  // Use substituted initializer if sizeof... was replaced
					static_member.is_const
				);
			}
		}
	}

	// Return the instantiated struct node for code generation
	std::cerr << "DEBUG: Primary template instantiation complete for " << instantiated_name << "\n";
	return instantiated_struct;
}

// Try to instantiate a member function template during a member function call
// This is called when parsing obj.method(args) where method is a template
std::optional<ASTNode> Parser::try_instantiate_member_function_template(
	std::string_view struct_name,
	std::string_view member_name,
	const std::vector<TypeSpecifierNode>& arg_types) {
	
	// Build the qualified template name
	std::string qualified_name = std::string(struct_name) + "::" + std::string(member_name);
	
	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
	if (!template_opt.has_value()) {
		return std::nullopt;  // Not a template
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Deduce template arguments from function call arguments
	if (arg_types.empty()) {
		return std::nullopt;  // Can't deduce without arguments
	}
	
	std::cerr << "DEBUG: Template found! func_decl has_definition=" << func_decl.get_definition().has_value() << "\n";

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	
	// Deduce template parameters in order from function arguments
	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();

		if (param.kind() == TemplateParameterKind::Template) {
			// Template template parameter - cannot be deduced from function arguments
			// Template template parameters must be explicitly specified
			return std::nullopt;
		} else if (param.kind() == TemplateParameterKind::Type) {
			if (arg_index < arg_types.size()) {
				template_args.push_back(TemplateArgument::makeType(arg_types[arg_index].type()));
				arg_index++;
			} else {
				// Not enough arguments - use first argument type
				template_args.push_back(TemplateArgument::makeType(arg_types[0].type()));
			}
		} else {
			// Non-type parameter - not yet supported
			return std::nullopt;
		}
	}

	// Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = qualified_name;
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	// Generate mangled name for the instantiation
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(member_name, template_args);

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Substitute the return type if it's a template parameter
	const TypeSpecifierNode& return_type_spec = orig_decl.type_node().as<TypeSpecifierNode>();
	Type return_type = return_type_spec.type();
	TypeIndex return_type_index = return_type_spec.type_index();

	if (return_type == Type::UserDefined && return_type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[return_type_index];
		std::string_view type_name = type_info.name_;

		// Try to find which template parameter this is
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.name() == type_name) {
				return_type = template_args[i].type_value;
				return_type_index = 0;
				break;
			}
		}
	}

	// Create mangled token
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Create return type node
	ASTNode substituted_return_type = emplace_node<TypeSpecifierNode>(
		return_type,
		TypeQualifier::None,
		get_type_size_bits(return_type),
		Token()
	);
	
	// Copy pointer levels from the original return type specifier
	auto& substituted_return_type_spec = substituted_return_type.as<TypeSpecifierNode>();
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type_spec.add_pointer_level(ptr_level.cv_qualifier);
	}

	// Create the new function declaration
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(substituted_return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_func_decl_ref, struct_name);

	// Copy and substitute parameters
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			Type param_type = param_type_spec.type();
			TypeIndex param_type_index = param_type_spec.type_index();

			if (param_type == Type::UserDefined && param_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[param_type_index];
				std::string_view type_name = type_info.name_;

				// Try to find which template parameter this is
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name) {
						param_type = template_args[i].type_value;
						param_type_index = 0;
						break;
					}
				}
			}

			// Create the substituted parameter type specifier
			auto substituted_param_type = emplace_node<TypeSpecifierNode>(
				param_type,
				TypeQualifier::None,
				get_type_size_bits(param_type),
				Token()
			);
			
			// Copy pointer levels from the original parameter type specifier
			auto& substituted_param_type_spec = substituted_param_type.as<TypeSpecifierNode>();
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type_spec.add_pointer_level(ptr_level.cv_qualifier);
			}

			// Create the new parameter declaration
			auto new_param_decl = emplace_node<DeclarationNode>(substituted_param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Check if the template has a body position stored
	if (!func_decl.has_template_body_position()) {
		std::cerr << ">>>>> Template has NO body position!\n";
		// No body to parse - return function without definition
		ast_nodes_.push_back(new_func_node);
		gTemplateRegistry.registerInstantiation(key, new_func_node);
		return new_func_node;
	}

	std::cerr << ">>>>> Template HAS body position, proceeding to parse body\n";
	
	// Temporarily add the concrete types to the type system with template parameter names
	std::vector<TypeInfo*> temp_type_infos;
	std::vector<std::string_view> param_names;
	for (const auto& tparam_node : template_params) {
		if (tparam_node.is<TemplateParameterNode>()) {
			param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
		}
	}
	
	for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
		std::string_view param_name = param_names[i];
		Type concrete_type = template_args[i].type_value;

		auto& type_info = gTypeInfo.emplace_back(std::string(param_name), concrete_type, gTypeInfo.size());
		gTypesByName.emplace(type_info.name_, &type_info);
		temp_type_infos.push_back(&type_info);
	}

	// Save current position
	TokenPosition current_pos = save_token_position();

	// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
	restore_lexer_position_only(func_decl.template_body_position());

		// Look up the struct type info
		auto struct_type_it = gTypesByName.find(struct_name);
		if (struct_type_it == gTypesByName.end()) {
			// Clean up and return error
			for (const auto* type_info : temp_type_infos) {
				gTypesByName.erase(type_info->name_);
			}
			restore_token_position(current_pos);
			return std::nullopt;
		}

		const TypeInfo* struct_type_info = struct_type_it->second;
		TypeIndex struct_type_index = struct_type_info->type_index_;

		// Set up parsing context for the member function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;
		
		// Find the struct node
		StructDeclarationNode* struct_node_ptr = nullptr;
		for (auto& node : ast_nodes_) {
			if (node.is<StructDeclarationNode>()) {
				auto& sn = node.as<StructDeclarationNode>();
				if (sn.name() == struct_name) {
					struct_node_ptr = &sn;
					break;
				}
			}
		}
		
		member_function_context_stack_.push_back({
			struct_name,
			struct_type_index,
			struct_node_ptr
		});

		// Add 'this' pointer to symbol table
		ASTNode this_type = emplace_node<TypeSpecifierNode>(
			Type::UserDefined,
			struct_type_index,
			64,  // Pointer size
			Token()
		);

		Token this_token(Token::Type::Keyword, "this", 0, 0, 0);
		auto this_decl = emplace_node<DeclarationNode>(this_type, this_token);
		gSymbolTable.insert("this", this_decl);

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}

		// Parse the function body
		auto block_result = parse_block();
		std::cerr << "DEBUG: parse_block() error=" << block_result.is_error() 
		          << " has_value=" << block_result.node().has_value() << "\n";
		if (!block_result.is_error() && block_result.node().has_value()) {
			new_func_ref.set_definition(*block_result.node());
			std::cerr << "DEBUG: set_definition called for " << mangled_name << "\n";
		}

		// Clean up context
	std::cerr << "DEBUG [8522]: Cleaning up context\n";
	current_function_ = nullptr;
	std::cerr << "DEBUG [8524]: Popping member_function_context_stack_\n";
	member_function_context_stack_.pop_back();
	std::cerr << "DEBUG [8526]: Exiting scope\n";
	gSymbolTable.exit_scope();

	// Restore original position (lexer only - keep AST nodes we created)
	std::cerr << "DEBUG [8529]: Restoring token position\n";
	restore_lexer_position_only(current_pos);

	// Remove temporary type infos
	std::cerr << "DEBUG [8532]: Removing " << temp_type_infos.size() << " temp type infos\n";
	for (const auto* type_info : temp_type_infos) {
		gTypesByName.erase(type_info->name_);
	}

	// Add the instantiated function to the AST
	std::cerr << "DEBUG [8538]: Adding function to ast_nodes_: " << mangled_name << " (current size=" << ast_nodes_.size() << ")\n";
	ast_nodes_.push_back(new_func_node);
	std::cerr << "DEBUG [8540]: After push, ast_nodes_.size()=" << ast_nodes_.size() << "\n";

	// Update the saved position to include this new node so it doesn't get erased
	// when we restore position in the caller
	saved_tokens_[current_pos.cursor_].ast_nodes_size_ = ast_nodes_.size();

	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	return new_func_node;
}

// Try to parse an out-of-line template member function definition
// Pattern: template<typename T> ReturnType ClassName<T>::functionName(...) { ... }
// Returns true if successfully parsed, false if not an out-of-line definition
std::optional<bool> Parser::try_parse_out_of_line_template_member(
	const std::vector<ASTNode>& template_params,
	const std::vector<std::string_view>& template_param_names) {

	// Save position in case this isn't an out-of-line definition
	TokenPosition saved_pos = save_token_position();

	// Try to parse return type
	auto return_type_result = parse_type_specifier();
	if (return_type_result.is_error() || !return_type_result.node().has_value()) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	ASTNode return_type_node = *return_type_result.node();

	// Check for class name (identifier)
	if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	Token class_name_token = *peek_token();
	std::string_view class_name = class_name_token.value();
	consume_token();

	// Check for template arguments: <T>, <K, V>, etc.
	if (!peek_token().has_value() || peek_token()->value() != "<") {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Parse template arguments (these should match the template parameters)
	// For now, we'll just skip over them - we know they're template parameters
	consume_token();  // consume '<'

	// Skip template arguments until we find '>'
	int angle_bracket_depth = 1;
	while (angle_bracket_depth > 0 && peek_token().has_value()) {
		if (peek_token()->value() == "<") {
			angle_bracket_depth++;
		} else if (peek_token()->value() == ">") {
			angle_bracket_depth--;
		}
		consume_token();
	}

	// Check for '::'
	if (!peek_token().has_value() || peek_token()->value() != "::") {
		restore_token_position(saved_pos);
		return std::nullopt;
	}
	consume_token();  // consume '::'

	// This IS an out-of-line template member function definition!
	// Discard the saved position - we're committed to parsing this
	discard_saved_token(saved_pos);

	// Parse function name
	if (!peek_token().has_value() || peek_token()->type() != Token::Type::Identifier) {
		return std::nullopt;  // Error - expected function name
	}

	Token function_name_token = *peek_token();
	consume_token();

	// Parse parameter list
	if (!peek_token().has_value() || peek_token()->value() != "(") {
		return std::nullopt;  // Error - expected '('
	}

	// Create a function declaration node
	auto [func_decl_node, func_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, function_name_token);
	auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref, function_name_token.value());

	// Parse parameters
	consume_token();  // consume '('
	while (peek_token().has_value() && peek_token()->value() != ")") {
		auto param_result = parse_type_and_name();
		if (param_result.is_error()) {
			return std::nullopt;
		}

		if (param_result.node().has_value()) {
			func_ref.add_parameter_node(*param_result.node());
		}

		if (peek_token().has_value() && peek_token()->value() == ",") {
			consume_token();
		}
	}

	if (!peek_token().has_value() || peek_token()->value() != ")") {
		return std::nullopt;  // Error - expected ')'
	}
	consume_token();  // consume ')'

	// Save the position of the function body
	TokenPosition body_start = save_token_position();

	// Skip the function body for now (we'll re-parse it during instantiation)
	if (peek_token().has_value() && peek_token()->value() == "{") {
		skip_balanced_braces();
	}

	// Register this out-of-line member function in the template registry
	OutOfLineMemberFunction out_of_line_member;
	out_of_line_member.template_params = template_params;
	out_of_line_member.function_node = func_node;
	out_of_line_member.body_start = body_start;
	out_of_line_member.template_param_names = template_param_names;

	gTemplateRegistry.registerOutOfLineMember(class_name, std::move(out_of_line_member));

	return true;  // Successfully parsed out-of-line definition
}

// Parse a template function body with concrete type bindings
// This is called during code generation to instantiate member function templates
std::optional<ASTNode> Parser::parseTemplateBody(
	TokenPosition body_pos,
	const std::vector<std::string_view>& template_param_names,
	const std::vector<Type>& concrete_types,
	std::string_view struct_name,
	TypeIndex struct_type_index
) {
	// Save current parser state using save_token_position so we can restore properly
	TokenPosition saved_cursor = save_token_position();

	// Store types we add so we can clean them up
	std::vector<TypeInfo*> temp_type_infos;

	// Bind template parameters to concrete types
	for (size_t i = 0; i < template_param_names.size() && i < concrete_types.size(); ++i) {
		Type concrete_type = concrete_types[i];
		std::string_view param_name = template_param_names[i];

		// Add a TypeInfo for this concrete type with the template parameter name
		auto& type_info = gTypeInfo.emplace_back(
			std::string(param_name),
			concrete_type,
			gTypeInfo.size()
		);

		// Register in global type lookup
		gTypesByName[std::string(param_name)] = &type_info;
		temp_type_infos.push_back(&type_info);
	}

	// If this is a member function, set up member function context
	bool setup_member_context = !struct_name.empty() && struct_type_index != 0;
	ASTNode this_decl_node;  // Need to keep this alive for the duration of parsing
	if (setup_member_context) {
		// Find the struct in the type system
		auto struct_type_it = gTypesByName.find(std::string(struct_name));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* type_info = struct_type_it->second;
			
			// Add 'this' pointer to global symbol table
			// Create a token for 'this'
			Token this_token(Token::Type::Keyword, "this", 0, 0, 0);
			
			// Create type node for 'this' (pointer to struct)
			auto this_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
				Type::UserDefined,
				struct_type_index,
				64,  // Pointer size
				this_token
			);
			this_type_node.as<TypeSpecifierNode>().add_pointer_level(CVQualifier::None);
			
			// Create declaration for 'this'
			this_decl_node = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);
			
			// Add to global symbol table
			gSymbolTable.insert("this", this_decl_node);
			
			// Also push member function context for good measure
			// Try to find the StructDeclarationNode in the symbol table
			auto struct_symbol_opt = lookup_symbol(struct_name);
			StructDeclarationNode* struct_node_ptr = nullptr;
			if (struct_symbol_opt.has_value() && struct_symbol_opt->is<StructDeclarationNode>()) {
				struct_node_ptr = &const_cast<StructDeclarationNode&>(struct_symbol_opt->as<StructDeclarationNode>());
			}
			
			MemberFunctionContext ctx;
			ctx.struct_name = struct_name;
			ctx.struct_type_index = struct_type_index;
			ctx.struct_node = struct_node_ptr;
			member_function_context_stack_.push_back(ctx);
		}
	}

	// Restore to template body position (this sets current_token_ to the saved token)
	restore_lexer_position_only(body_pos);

	// The current token should now be '{' (the token that was saved)
	// parse_block() will consume it, so don't consume it here

	// Parse the block body
	auto block_result = parse_block();

	// Clean up member function context if we set it up
	if (setup_member_context && !member_function_context_stack_.empty()) {
		member_function_context_stack_.pop_back();
		// Remove 'this' from global symbol table
		// Note: gSymbolTable doesn't have a remove method, but since we're about to restore
		// the parser state anyway, the symbol table will revert to its previous state
	}

	// Clean up temporary type bindings
	for (auto* type_info : temp_type_infos) {
		gTypesByName.erase(type_info->name_);
	}

	// Restore original parser state
	restore_lexer_position_only(saved_cursor);

	if (block_result.is_error() || !block_result.node().has_value()) {
		return std::nullopt;
	}

	return block_result.node();
}

// Substitute template parameters in an AST node with concrete types/values
// This recursively traverses the AST and replaces TemplateParameterReferenceNode instances
ASTNode Parser::substituteTemplateParameters(
	const ASTNode& node,
	const std::vector<ASTNode>& template_params,
	const std::vector<TemplateArgument>& template_args
) {
	// Helper function to get type name as string
	auto get_type_name = [](Type type) -> std::string {
		switch (type) {
			case Type::Void: return "void";
			case Type::Bool: return "bool";
			case Type::Char: return "char";
			case Type::UnsignedChar: return "unsigned char";
			case Type::Short: return "short";
			case Type::UnsignedShort: return "unsigned short";
			case Type::Int: return "int";
			case Type::UnsignedInt: return "unsigned int";
			case Type::Long: return "long";
			case Type::UnsignedLong: return "unsigned long";
			case Type::LongLong: return "long long";
			case Type::UnsignedLongLong: return "unsigned long long";
			case Type::Float: return "float";
			case Type::Double: return "double";
			case Type::LongDouble: return "long double";
			case Type::UserDefined: return "user_defined";  // This should be handled specially
			default: return "unknown";
		}
	};

	// Handle different node types
	if (node.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = node.as<ExpressionNode>();
		const auto& expr = expr_node;

		// Check if this is a TemplateParameterReferenceNode
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
			std::string_view param_name = tparam_ref.param_name();

			// Find which template parameter this is
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == param_name) {
					const TemplateArgument& arg = template_args[i];

					if (arg.kind == TemplateArgument::Kind::Type) {
						// Create an identifier node for the concrete type
						Token type_token(Token::Type::Identifier, std::string(get_type_name(arg.type_value)),
						                tparam_ref.token().line(), tparam_ref.token().column(),
						                tparam_ref.token().file_index());
						return emplace_node<ExpressionNode>(IdentifierNode(type_token));
					} else if (arg.kind == TemplateArgument::Kind::Value) {
						// Create a numeric literal node for the value
						Token value_token(Token::Type::Literal, std::to_string(arg.int_value),
						                 tparam_ref.token().line(), tparam_ref.token().column(),
						                 tparam_ref.token().file_index());
						return emplace_node<ExpressionNode>(NumericLiteralNode(value_token, static_cast<unsigned long long>(arg.int_value), Type::Int, TypeQualifier::None, 32));
					}
					// For template template parameters, not yet supported
					break;
				}
			}

			// If we couldn't substitute, return the original node
			return node;
		}

		// For other expression types, recursively substitute in subexpressions
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const BinaryOperatorNode& bin_op = std::get<BinaryOperatorNode>(expr);
			ASTNode substituted_left = substituteTemplateParameters(bin_op.get_lhs(), template_params, template_args);
			ASTNode substituted_right = substituteTemplateParameters(bin_op.get_rhs(), template_params, template_args);
			return emplace_node<ExpressionNode>(BinaryOperatorNode(bin_op.get_token(), substituted_left, substituted_right));
		} else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(expr);
			ASTNode substituted_operand = substituteTemplateParameters(unary_op.get_operand(), template_params, template_args);
			return emplace_node<ExpressionNode>(UnaryOperatorNode(unary_op.get_token(), substituted_operand, unary_op.is_prefix()));
		} else if (std::holds_alternative<FunctionCallNode>(expr)) {
			const FunctionCallNode& func_call = std::get<FunctionCallNode>(expr);
			ChunkedVector<ASTNode> substituted_args;
			for (size_t i = 0; i < func_call.arguments().size(); ++i) {
				substituted_args.push_back(substituteTemplateParameters(func_call.arguments()[i], template_params, template_args));
			}
			return emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(func_call.function_declaration()), std::move(substituted_args), func_call.called_from()));
		} else if (std::holds_alternative<MemberAccessNode>(expr)) {
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
			ASTNode substituted_object = substituteTemplateParameters(member_access.object(), template_params, template_args);
			return emplace_node<ExpressionNode>(MemberAccessNode(substituted_object, member_access.member_token()));
		} else if (std::holds_alternative<ConstructorCallNode>(expr)) {
			const ConstructorCallNode& constructor_call = std::get<ConstructorCallNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(constructor_call.type_node(), template_params, template_args);
			ChunkedVector<ASTNode> substituted_args;
			for (size_t i = 0; i < constructor_call.arguments().size(); ++i) {
				substituted_args.push_back(substituteTemplateParameters(constructor_call.arguments()[i], template_params, template_args));
			}
			return emplace_node<ExpressionNode>(ConstructorCallNode(substituted_type, std::move(substituted_args), constructor_call.called_from()));
		} else if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const ArraySubscriptNode& array_sub = std::get<ArraySubscriptNode>(expr);
			ASTNode substituted_array = substituteTemplateParameters(array_sub.array_expr(), template_params, template_args);
			ASTNode substituted_index = substituteTemplateParameters(array_sub.index_expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(ArraySubscriptNode(substituted_array, substituted_index, array_sub.bracket_token()));
		} else if (std::holds_alternative<FoldExpressionNode>(expr)) {
			// C++17 Fold expressions - expand into nested binary operations
			const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
		
			// The fold pack_name refers to a function parameter pack (like "args")
			// We need to expand it into individual parameter references (like "args_0", "args_1", "args_2")
			// The number of expansions comes from the number of template arguments
			std::vector<ASTNode> pack_values;
		
			// Count how many times the pack was instantiated by counting template args
			// For variadic templates, all args from the first variadic parameter onward are pack elements
			size_t num_pack_elements = 0;
			for (size_t i = 0; i < template_params.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.is_variadic()) {
					// All remaining template arguments belong to this pack
					num_pack_elements = template_args.size() - i;
					break;
				}
			}
		
			if (num_pack_elements == 0) {
				std::cerr << "WARNING: Fold expression pack '" << fold.pack_name() << "' has no elements\n";
				return node;
			}
		
			// Create identifier nodes for each pack element: pack_name_0, pack_name_1, etc.
			for (size_t i = 0; i < num_pack_elements; ++i) {
				StringBuilder param_name_builder;
				param_name_builder.append(fold.pack_name());
				param_name_builder.append('_');
				param_name_builder.append(static_cast<int>(i));
				std::string_view param_name = param_name_builder.commit();
		
				Token param_token(Token::Type::Identifier, param_name,
								 fold.get_token().line(), fold.get_token().column(),
								 fold.get_token().file_index());
				pack_values.push_back(emplace_node<ExpressionNode>(IdentifierNode(param_token)));
			}
		
			if (pack_values.empty()) {
				std::cerr << "WARNING: Fold expression pack '" << fold.pack_name() << "' is empty\n";
				return node;
			}
		
			// Expand the fold expression based on type and direction
			ASTNode result_expr;
			Token op_token = fold.get_token();
			
			if (fold.type() == FoldExpressionNode::Type::Unary) {
				// Unary fold: (... op pack) or (pack op ...)
				if (fold.direction() == FoldExpressionNode::Direction::Left) {
					// Left fold: (... op pack) = ((pack[0] op pack[1]) op pack[2]) ...
					result_expr = pack_values[0];
					for (size_t i = 1; i < pack_values.size(); ++i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, result_expr, pack_values[i]));
					}
				} else {
					// Right fold: (pack op ...) = pack[0] op (pack[1] op (pack[2] op ...))
					result_expr = pack_values[pack_values.size() - 1];
					for (int i = static_cast<int>(pack_values.size()) - 2; i >= 0; --i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, pack_values[i], result_expr));
					}
				}
			} else {
				// Binary fold with init expression
				ASTNode init = substituteTemplateParameters(*fold.init_expr(), template_params, template_args);
				
				if (fold.direction() == FoldExpressionNode::Direction::Left) {
					// Left binary fold: (init op ... op pack) = (((init op pack[0]) op pack[1]) op ...)
					result_expr = init;
					for (size_t i = 0; i < pack_values.size(); ++i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, result_expr, pack_values[i]));
					}
				} else {
					// Right binary fold: (pack op ... op init) = pack[0] op (pack[1] op (... op init))
					result_expr = init;
					for (int i = static_cast<int>(pack_values.size()) - 1; i >= 0; --i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, pack_values[i], result_expr));
					}
				}
			}
			
			return result_expr;
		}

		// For other expression types that don't contain subexpressions, return as-is
		return node;

	} else if (node.is<FunctionCallNode>()) {
		// Handle function calls that might contain template parameter references
		const FunctionCallNode& func_call = node.as<FunctionCallNode>();

		// Substitute arguments
		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < func_call.arguments().size(); ++i) {
			substituted_args.push_back(substituteTemplateParameters(func_call.arguments()[i], template_params, template_args));
		}

		// For now, don't substitute the function declaration itself
		// Create new function call with substituted arguments
		return emplace_node<FunctionCallNode>(const_cast<DeclarationNode&>(func_call.function_declaration()), std::move(substituted_args), func_call.called_from());

	} else if (node.is<BinaryOperatorNode>()) {
		// Handle binary operators
		const BinaryOperatorNode& bin_op = node.as<BinaryOperatorNode>();

		ASTNode substituted_left = substituteTemplateParameters(bin_op.get_lhs(), template_params, template_args);
		ASTNode substituted_right = substituteTemplateParameters(bin_op.get_rhs(), template_params, template_args);

		return emplace_node<BinaryOperatorNode>(bin_op.get_token(), substituted_left, substituted_right);

	} else if (node.is<DeclarationNode>()) {
		// Handle declarations that might have template parameter types
		const DeclarationNode& decl = node.as<DeclarationNode>();

		// Substitute the type specifier
		ASTNode substituted_type = substituteTemplateParameters(decl.type_node(), template_params, template_args);

		// Create new declaration with substituted type
		return emplace_node<DeclarationNode>(substituted_type, decl.identifier_token());

	} else if (node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();

		// Check if this is a user-defined type that matches a template parameter
		if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			std::string_view type_name = type_info.name_;

			// Check if this type name matches a template parameter
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == type_name && template_args[i].kind == TemplateArgument::Kind::Type) {
					// Substitute with concrete type
					return emplace_node<TypeSpecifierNode>(
						template_args[i].type_value,
						TypeQualifier::None,
						get_type_size_bits(template_args[i].type_value),
						Token()
					);
				}
			}
		}

		return node;

	} else if (node.is<BlockNode>()) {
		// Handle block nodes by substituting in all statements
		const BlockNode& block = node.as<BlockNode>();
		
		auto new_block = emplace_node<BlockNode>();
		BlockNode& new_block_ref = new_block.as<BlockNode>();
		
		for (size_t i = 0; i < block.get_statements().size(); ++i) {
			new_block_ref.add_statement_node(substituteTemplateParameters(block.get_statements()[i], template_params, template_args));
		}
		
		return new_block;

	} else if (node.is<ForStatementNode>()) {
		// Handle for statements
		const ForStatementNode& for_stmt = node.as<ForStatementNode>();
		
		auto init_stmt = for_stmt.get_init_statement().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_init_statement(), template_params, template_args)) : 
			std::nullopt;
		auto condition = for_stmt.get_condition().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_condition(), template_params, template_args)) : 
			std::nullopt;
		auto update_expr = for_stmt.get_update_expression().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_update_expression(), template_params, template_args)) : 
			std::nullopt;
		auto body_stmt = substituteTemplateParameters(for_stmt.get_body_statement(), template_params, template_args);
		
		return emplace_node<ForStatementNode>(init_stmt, condition, update_expr, body_stmt);

	} else if (node.is<UnaryOperatorNode>()) {
		// Handle unary operators
		const UnaryOperatorNode& unary_op = node.as<UnaryOperatorNode>();
		
		ASTNode substituted_operand = substituteTemplateParameters(unary_op.get_operand(), template_params, template_args);
		
		return emplace_node<UnaryOperatorNode>(unary_op.get_token(), substituted_operand, unary_op.is_prefix());

	} else if (node.is<VariableDeclarationNode>()) {
		// Handle variable declarations
		const VariableDeclarationNode& var_decl = node.as<VariableDeclarationNode>();
		
		auto initializer = var_decl.initializer().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*var_decl.initializer(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<VariableDeclarationNode>(var_decl.declaration_node(), initializer, var_decl.storage_class());

	} else if (node.is<ReturnStatementNode>()) {
		// Handle return statements
		const ReturnStatementNode& ret_stmt = node.as<ReturnStatementNode>();
		
		auto expr = ret_stmt.expression().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*ret_stmt.expression(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<ReturnStatementNode>(expr, ret_stmt.return_token());

	} else if (node.is<IfStatementNode>()) {
		// Handle if statements
		const IfStatementNode& if_stmt = node.as<IfStatementNode>();
		
		ASTNode substituted_condition = substituteTemplateParameters(if_stmt.get_condition(), template_params, template_args);
		ASTNode substituted_then = substituteTemplateParameters(if_stmt.get_then_statement(), template_params, template_args);
		auto substituted_else = if_stmt.get_else_statement().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*if_stmt.get_else_statement(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<IfStatementNode>(substituted_condition, substituted_then, substituted_else);

	} else if (node.is<WhileStatementNode>()) {
		// Handle while statements
		const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
		
		ASTNode substituted_condition = substituteTemplateParameters(while_stmt.get_condition(), template_params, template_args);
		ASTNode substituted_body = substituteTemplateParameters(while_stmt.get_body_statement(), template_params, template_args);
		
		return emplace_node<WhileStatementNode>(substituted_condition, substituted_body);
	}

	// For other node types, return as-is (simplified implementation)
	return node;
}

