#include "Parser.h"
#include "OverloadResolution.h"
#include "TemplateRegistry.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "TemplateProfilingStats.h"
#include "ExpressionSubstitutor.h"
#include "TypeTraitEvaluator.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"
#include <atomic> // Include atomic for constrained partial specialization counter
#include <string_view> // Include string_view header
#include <unordered_set> // Include unordered_set header
#include <ranges> // Include ranges for std::ranges::find
#include <array> // Include array for std::array
#include <charconv> // Include charconv for std::from_chars
#include <cctype>
#include "ChunkedString.h"
#include "Log.h"

static const TypeInfo* lookupTypeInCurrentContext(StringHandle type_handle);

// Break into the debugger only on Windows
#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX  // Prevent Windows.h from defining min/max macros
    #endif
    #include <windows.h>
    #define DEBUG_BREAK() if (IsDebuggerPresent()) { DebugBreak(); }
#else
    // On non-Windows platforms, define as a no-op (does nothing)
    #define DEBUG_BREAK() ((void)0)
#endif

// Maximum number of elements allowed in a parameter pack for fold expressions
// This prevents infinite loops and excessive memory usage in case of bugs
static constexpr size_t MAX_PACK_ELEMENTS = 1000;

// Define the global symbol table (declared as extern in SymbolTable.h)
SymbolTable gSymbolTable;
ChunkedStringAllocator gChunkedStringAllocator;
// Global registries
TemplateRegistry gTemplateRegistry;
ConceptRegistry gConceptRegistry;

// Type keywords set - used for if-statement initializer detection
static const std::unordered_set<std::string_view> type_keywords = {
	"int"sv, "float"sv, "double"sv, "char"sv, "bool"sv, "void"sv,
	"short"sv, "long"sv, "signed"sv, "unsigned"sv, "const"sv, "volatile"sv, "alignas"sv,
	"auto"sv, "wchar_t"sv, "char8_t"sv, "char16_t"sv, "char32_t"sv, "decltype"sv,
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
	{"_cdecl"sv, CallingConvention::Cdecl},
	{"__stdcall"sv, CallingConvention::Stdcall},
	{"_stdcall"sv, CallingConvention::Stdcall},
	{"__fastcall"sv, CallingConvention::Fastcall},
	{"_fastcall"sv, CallingConvention::Fastcall},
	{"__vectorcall"sv, CallingConvention::Vectorcall},
	{"__thiscall"sv, CallingConvention::Thiscall},
	{"__clrcall"sv, CallingConvention::Clrcall}
};

// Helper function to calculate member size and alignment
// Handles pointers, references, and basic types correctly
struct MemberSizeAndAlignment {
	size_t size;
	size_t alignment;
};

static MemberSizeAndAlignment calculateMemberSizeAndAlignment(const TypeSpecifierNode& type_spec) {
	MemberSizeAndAlignment result;
	
	// For pointers, references, and function pointers, size and alignment are always sizeof(void*)
	if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_function_pointer()) {
		result.size = sizeof(void*);
		result.alignment = sizeof(void*);
	} else {
		result.size = get_type_size_bits(type_spec.type()) / 8;
		result.alignment = get_type_alignment(type_spec.type(), result.size);
	}
	
	return result;
}

// Helper function to safely get type size from TemplateArgument
static int getTypeSizeFromTemplateArgument(const TemplateArgument& arg) {
	// Check if this is a basic type that get_type_size_bits can handle
	// Basic types range from Void to MemberObjectPointer in the Type enum
	if (arg.type_value >= Type::Void && arg.type_value <= Type::MemberObjectPointer) {
		return static_cast<size_t>(get_type_size_bits(arg.type_value));
	}
	// For UserDefined and other types (Template, etc), try to extract size from type_specifier
	if (arg.type_specifier.has_value()) {
		const auto& type_spec = arg.type_specifier.value();
		// Use type_index for direct O(1) lookup - no name-based lookup needed
		size_t type_index = type_spec.type_index();
		if (type_index > 0 && type_index < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_index];
			if (type_info.type_size_ > 0) {
				return type_info.type_size_;
			}
		}
	}
	return 0;  // Will be resolved during member access
}

// Helper to convert TemplateTypeArg vector to TypeInfo::TemplateArgInfo vector
// This enables storing template instantiation metadata in TypeInfo for O(1) lookup
static InlineVector<TypeInfo::TemplateArgInfo, 4> convertToTemplateArgInfo(const std::vector<TemplateTypeArg>& template_args) {
	InlineVector<TypeInfo::TemplateArgInfo, 4> result;
	for (const auto& arg : template_args) {
		TypeInfo::TemplateArgInfo info;
		info.base_type = arg.base_type;
		info.type_index = arg.type_index;
		info.pointer_depth = arg.pointer_depth;
		info.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
		info.ref_qualifier = arg.is_rvalue_reference ? ReferenceQualifier::RValueReference
		                                             : (arg.is_reference ? ReferenceQualifier::LValueReference : ReferenceQualifier::None);
		info.cv_qualifier = arg.cv_qualifier;
		info.is_array = arg.is_array;
		info.array_size = arg.array_size;
		info.value = arg.value;
		info.is_value = arg.is_value;
		result.push_back(info);
	}
	return result;
}

// Helper to check if a type name is a dependent template placeholder
// Uses TypeInfo metadata first (O(1)), falls back to string parsing for backward compatibility
// Returns: {is_dependent, base_template_name}
static std::pair<bool, std::string_view> isDependentTemplatePlaceholder(std::string_view type_name) {
	// First try TypeInfo-based detection (O(1), preferred)
	auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(type_name));
	if (type_it != gTypesByName.end()) {
		const TypeInfo* type_info = type_it->second;
		if (type_info->isTemplateInstantiation()) {
			return {true, StringTable::getStringView(type_info->baseTemplateName())};
		}
	}
	
	// Fallback: check for hash-based naming (template$hash pattern)
	size_t dollar_pos = type_name.find('$');
	if (dollar_pos != std::string_view::npos) {
		return {true, type_name.substr(0, dollar_pos)};
	}
	
	// Fallback: check for old-style _void suffix
	if (type_name.ends_with("_void")) {
		size_t underscore_pos = type_name.find('_');
		if (underscore_pos != std::string_view::npos) {
			return {true, type_name.substr(0, underscore_pos)};
		}
	}
	
	return {false, {}};
}

// Split a qualified namespace string ("a::b::c") into components for mangling.
static std::vector<std::string_view> splitQualifiedNamespace(std::string_view qualified_namespace) {
	std::vector<std::string_view> components;
	if (qualified_namespace.empty()) {
		return components;
	}
	size_t start = 0;
	while (true) {
		size_t pos = qualified_namespace.find("::", start);
		if (pos == std::string_view::npos) {
			components.push_back(qualified_namespace.substr(start));
			break;
		}
		components.push_back(qualified_namespace.substr(start, pos - start));
		start = pos + 2;
	}
	return components;
}

// Helper function to find all local variable declarations in an AST node
static void findLocalVariableDeclarations(const ASTNode& node, std::unordered_set<StringHandle>& var_names) {
	if (node.is<VariableDeclarationNode>()) {
		const auto& var_decl = node.as<VariableDeclarationNode>();
		const auto& decl = var_decl.declaration();
		var_names.insert(StringTable::getOrInternStringHandle(decl.identifier_token().value()));
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
static void findReferencedIdentifiers(const ASTNode& node, std::unordered_set<StringHandle>& identifiers) {
	if (node.is<IdentifierNode>()) {
		identifiers.insert(node.as<IdentifierNode>().nameHandle());
	} else if (node.is<ExpressionNode>()) {
		// ExpressionNode is a variant, so we need to check each alternative
		const auto& expr = node.as<ExpressionNode>();
		std::visit([&](const auto& inner_node) {
			using T = std::decay_t<decltype(inner_node)>;
			if constexpr (std::is_same_v<T, IdentifierNode>) {
				identifiers.insert(inner_node.nameHandle());
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<BinaryOperatorNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<UnaryOperatorNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<FunctionCallNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<MemberAccessNode*>(&inner_node)), identifiers);
			} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
				findReferencedIdentifiers(ASTNode(const_cast<PointerToMemberAccessNode*>(&inner_node)), identifiers);
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
	} else if (node.is<PointerToMemberAccessNode>()) {
		const auto& ptr_member = node.as<PointerToMemberAccessNode>();
		findReferencedIdentifiers(ptr_member.object(), identifiers);
		findReferencedIdentifiers(ptr_member.member_pointer(), identifiers);
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

Parser::Parser(Lexer& lexer, CompileContext& context)
    : lexer_(lexer), context_(context), current_token_(lexer_.next_token()) {
    initialize_native_types();
    ast_nodes_.reserve(default_ast_tree_size_);
}

Parser::ScopedTokenPosition::ScopedTokenPosition(class Parser& parser, const std::source_location location)
    : parser_(parser), saved_handle_(parser.save_token_position()), location_(location) {}

Parser::ScopedTokenPosition::~ScopedTokenPosition() {
    if (!discarded_) {
        parser_.restore_token_position(saved_handle_);
    }
}

ParseResult Parser::ScopedTokenPosition::success(ASTNode node) {
    discarded_ = true;
    parser_.discard_saved_token(saved_handle_);
    return ParseResult::success(node);
}

ParseResult Parser::ScopedTokenPosition::error(std::string_view error_message) {
    discarded_ = true;
    parser_.discard_saved_token(saved_handle_);
    return ParseResult::error(std::string(error_message),
        parser_.peek_info());
}

ParseResult Parser::ScopedTokenPosition::propagate(ParseResult&& result) {
    // Sub-parser already handled position restoration (if needed)
    // Just discard our saved position and forward the result
    discarded_ = true;
    parser_.discard_saved_token(saved_handle_);
    return std::move(result);
}

Token Parser::consume_token() {
    Token token = current_token_;
    
    // Phase 5: Check if we have an injected token (from >> splitting)
    if (injected_token_.type() != Token::Type::Uninitialized) {
        // Use injected token as the next current_token_
        current_token_ = injected_token_;
        injected_token_ = Token{};  // reset to Uninitialized
        FLASH_LOG_FORMAT(Parser, Debug, "consume_token: Consumed token='{}', next token from injected='{}'", 
            std::string(token.value()),
            std::string(current_token_.value()));
    } else {
        // Normal path: get next token from lexer
        Token next = lexer_.next_token();
        FLASH_LOG_FORMAT(Parser, Debug, "consume_token: Consumed token='{}', next token from lexer='{}'", 
            std::string(token.value()),
            std::string(next.value()));
        current_token_ = next;
    }
    return token;
}

Token Parser::peek_token() {
    // Return current token — EndOfFile is a valid token (not nullopt)
    return current_token_;
}

Token Parser::peek_token(size_t lookahead) {
    if (lookahead == 0) {
        return peek_token();  // Peek at current token
    }
    
    // Save current position
    SaveHandle saved_handle = save_token_position();
    
    // Consume tokens to reach the lookahead position
    for (size_t i = 0; i < lookahead; ++i) {
        consume_token();
    }
    
    // Peek at the token at lookahead position
    Token result = peek_token();
    
    // Restore original position
    restore_lexer_position_only(saved_handle);
    
    // Discard the saved position as we're done with it
    discard_saved_token(saved_handle);
    
    return result;
}

// Phase 5: Split >> token into two > tokens for nested templates
// This is needed for C++20 maximal munch rules: Foo<Bar<int>> should parse as Foo<Bar<int> >
void Parser::split_right_shift_token() {
    if (current_token_.kind() != ">>"_tok) {
        FLASH_LOG(Parser, Error, "split_right_shift_token called but current token is not >>");
        return;
    }
    
    FLASH_LOG(Parser, Debug, "Splitting >> token into two > tokens for nested template");
    
    // Create two synthetic > tokens
    // We use static storage for the ">" string since string_view needs valid memory
    static const std::string_view gt_str = ">";
    
    Token first_gt(Token::Type::Operator, gt_str, 
                   current_token_.line(), 
                   current_token_.column(),
                   current_token_.file_index());
    
    Token second_gt(Token::Type::Operator, gt_str, 
                    current_token_.line(), 
                    current_token_.column() + 1,  // Second > is one character after first
                    current_token_.file_index());
    
    // Replace current >> with first >
    current_token_ = first_gt;
    
    // Inject second > to be consumed next
    injected_token_ = second_gt;
}

// ---- New TokenKind-based API (Phase 0) ----

// Static EOF token returned by peek_info() when at end of input
static const Token eof_token_sentinel(Token::Type::EndOfFile, ""sv, 0, 0, 0);

TokenKind Parser::peek() const {
    return current_token_.kind();
}

TokenKind Parser::peek(size_t lookahead) {
    if (lookahead == 0) {
        return peek();
    }
    return peek_token(lookahead).kind();
}

const Token& Parser::peek_info() const {
    return current_token_;
}

Token Parser::peek_info(size_t lookahead) {
    if (lookahead == 0) {
        return peek_info();
    }
    return peek_token(lookahead);
}

Token Parser::advance() {
    Token result = current_token_;
    
    // Phase 5: Check if we have an injected token (from >> splitting)
    if (injected_token_.type() != Token::Type::Uninitialized) {
        current_token_ = injected_token_;
        injected_token_ = Token{};  // reset to Uninitialized
    } else {
        current_token_ = lexer_.next_token();
    }
    return result;
}

bool Parser::consume(TokenKind kind) {
    if (peek() == kind) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenKind kind) {
    if (peek() == kind) {
        return advance();
    }
    // Emit diagnostic — find the spelling for the expected kind
    std::string_view expected_spelling = "?";
    for (const auto& entry : all_fixed_tokens) {
        if (entry.kind == kind) {
            expected_spelling = entry.spelling;
            break;
        }
    }
    const Token& cur = peek_info();
    FLASH_LOG(Parser, Error, "Expected '", expected_spelling, "' but got '", cur.value(), 
              "' at line ", cur.line(), " column ", cur.column());
    return eof_token_sentinel;
}

Parser::SaveHandle Parser::save_token_position() {
    // Generate unique handle using static incrementing counter
    // This prevents collisions even when multiple saves happen at the same cursor position
    SaveHandle handle = next_save_handle_++;
    
    // Save current parser state (including injected token for >> splitting)
    TokenPosition lexer_pos = lexer_.save_token_position();
    saved_tokens_[handle] = { current_token_, injected_token_, ast_nodes_.size(), lexer_pos };
    
    FLASH_LOG_FORMAT(Parser, Debug, "save_token_position: handle={}, token={}", 
        static_cast<unsigned long>(handle), std::string(current_token_.value()));
    
    return handle;
}

void Parser::restore_token_position(SaveHandle handle, [[maybe_unused]] const std::source_location location) {
    auto it = saved_tokens_.find(handle);
    if (it == saved_tokens_.end()) {
        // Handle not found - this shouldn't happen in correct usage
        return;
    }
    
    const SavedToken& saved_token = it->second;
    {
        std::string saved_tok = std::string(saved_token.current_token_.value());
        std::string current_tok = std::string(current_token_.value());
        
        // DEBUGGING: Track if we're restoring to "ns" token
        if (saved_tok == "ns") {
            FLASH_LOG_FORMAT(Parser, Error, "!!! RESTORING TO 'ns' TOKEN !!! handle={}, current={}", 
                static_cast<unsigned long>(handle), current_tok);
        }
        
        FLASH_LOG_FORMAT(Parser, Debug, "restore_token_position: handle={}, saved token={}, current={}", 
            static_cast<unsigned long>(handle), saved_tok, current_tok);
    }
    
    lexer_.restore_token_position(saved_token.lexer_position_);
    current_token_ = saved_token.current_token_;
    
    // Phase 5: Restore injected token state from save point
    // If the save was made before a >> split, injected_token_ will be Uninitialized (clearing it).
    // If the save was made after a >> split, injected_token_ will contain the second >.
    injected_token_ = saved_token.injected_token_;
	
    // Process AST nodes that were added after the saved position.
    // We need to:
    // 1. Keep FunctionDeclarationNode and StructDeclarationNode in ast_nodes_ - they may be 
    //    template instantiations registered in gTemplateRegistry.instantiations_ cache
    // 2. Move other nodes to ast_discarded_nodes_ to keep them alive (prevent memory corruption)
    //    but not pollute the AST tree
    //
    // This can happen when parsing expressions like `(all(1,1,1) ? 1 : 0)`:
    // 1. Parser tries fold expression patterns, saving position
    // 2. Parser parses `all(1,1,1)`, which instantiates the template
    // 3. Parser finds it's not a fold expression, restores position
    // 4. Template instantiation must be kept in ast_nodes_ for code generation
    size_t new_size = saved_token.ast_nodes_size_;
    // Safety check: don't iterate past the current vector size
    if (new_size > ast_nodes_.size()) {
        // This shouldn't happen, but if it does, just skip the cleanup
        return;
    }
    
    // Iterate from the end to avoid invalidating iterators when removing elements
    for (size_t i = ast_nodes_.size(); i > new_size; ) {
        --i;
        ASTNode& node = ast_nodes_[i];
        if (node.is<FunctionDeclarationNode>() || node.is<StructDeclarationNode>()) {
            // Keep function and struct declarations - they may be template instantiations
            // or struct definitions that are already registered in the symbol table
            // Leave this node in place
        } else {
            // Move this node to discarded list to keep it alive, then remove from ast_nodes_
            ast_discarded_nodes_.push_back(std::move(node));
            ast_nodes_.erase(ast_nodes_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
}

void Parser::restore_lexer_position_only(Parser::SaveHandle handle) {
    // Restore lexer position and current token, but keep AST nodes
    auto it = saved_tokens_.find(handle);
    if (it == saved_tokens_.end()) {
        return;
    }
    
    const SavedToken& saved_token = it->second;
    lexer_.restore_token_position(saved_token.lexer_position_);
    current_token_ = saved_token.current_token_;
    injected_token_ = saved_token.injected_token_;
    // Don't erase AST nodes - they were intentionally created during re-parsing
}

void Parser::discard_saved_token(SaveHandle handle) {
    saved_tokens_.erase(handle);
}

void Parser::skip_balanced_braces() {
	// Expect the current token to be '{'
	if (peek() != "{"_tok) {
		return;
	}
	
	int brace_depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;  // Safety limit to prevent infinite loops

	while (!peek().is_eof() && token_count < MAX_TOKENS) {
		auto kind = peek();
		if (kind == "{"_tok) {
			brace_depth++;
		} else if (kind == "}"_tok) {
			brace_depth--;
			if (brace_depth == 0) {
				// Consume the closing '}' and exit
				advance();
				break;
			}
		}
		advance();
		token_count++;
	}
}

void Parser::skip_balanced_parens() {
	// Expect the current token to be '('
	if (peek() != "("_tok) {
		return;
	}
	
	int paren_depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;  // Safety limit to prevent infinite loops

	while (!peek().is_eof() && token_count < MAX_TOKENS) {
		auto kind = peek();
		if (kind == "("_tok) {
			paren_depth++;
		} else if (kind == ")"_tok) {
			paren_depth--;
			if (paren_depth == 0) {
				// Consume the closing ')' and exit
				advance();
				break;
			}
		}
		advance();
		token_count++;
	}
}

void Parser::skip_template_arguments() {
	// Expect the current token to be '<'
	if (peek() != "<"_tok) {
		return;
	}
	
	int angle_depth = 0;
	size_t token_count = 0;
	const size_t MAX_TOKENS = 10000;  // Safety limit to prevent infinite loops

	while (!peek().is_eof() && token_count < MAX_TOKENS) {
		update_angle_depth(peek(), angle_depth);
		advance();
		
		if (angle_depth == 0) {
			// We've consumed the closing '>' or '>>'
			break;
		}
		
		token_count++;
	}
}

void Parser::skip_member_declaration_to_semicolon() {
	// Skip tokens until we reach ';' at top level, or an unmatched '}'
	// Handles nested parentheses, angle brackets, and braces
	int paren_depth = 0;
	int angle_depth = 0;
	int brace_depth = 0;
	
	while (!peek().is_eof()) {
		auto kind = peek();
		
		if (kind == "("_tok) {
			paren_depth++;
			advance();
		} else if (kind == ")"_tok) {
			paren_depth--;
			advance();
		} else if (kind == "<"_tok || kind == ">"_tok || kind == ">>"_tok) {
			update_angle_depth(kind, angle_depth);
			advance();
		} else if (kind == "{"_tok) {
			brace_depth++;
			advance();
		} else if (kind == "}"_tok) {
			if (brace_depth == 0) {
				break;  // Don't consume - this is end of struct
			}
			brace_depth--;
			advance();
		} else if (kind == ";"_tok && paren_depth == 0 && angle_depth == 0 && brace_depth == 0) {
			advance();
			break;
		} else {
			advance();
		}
	}
}

// Helper function to parse the contents of pack(...) after the opening '('
// Returns success and consumes the closing ')' on success
ParseResult Parser::parse_pragma_pack_inner()
{
	// Check if it's empty: pack()
	if (consume(")"_tok)) {
		context_.setPackAlignment(0); // Reset to default
		return ParseResult::success();
	}

	// Check for push/pop/show: pack(push) or pack(pop) or pack(show)
	// Full syntax:
	//   pack(push [, identifier] [, n])
	//   pack(pop [, {identifier | n}])
	//   pack(show)
	if (peek().is_identifier()) {
		std::string_view pack_action = peek_info().value();
		
		// Handle pack(show)
		if (pack_action == "show") {
			advance(); // consume 'show'
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pragma pack show", current_token_);
			}
			// Emit a warning showing the current pack alignment
			size_t current_align = context_.getCurrentPackAlignment();
			if (current_align == 0) {
				FLASH_LOG(Parser, Warning, "current pack alignment is default (natural alignment)");
			} else {
				FLASH_LOG(Parser, Warning, "current pack alignment is ", current_align);
			}
			return ParseResult::success();
		}
		
		if (pack_action == "push" || pack_action == "pop") {
			advance(); // consume 'push' or 'pop'
			
			// Check for optional parameters
			if (peek() == ","_tok) {
				advance(); // consume ','
				
				// First parameter could be identifier or number
				if (!peek().is_eof()) {
					// Check if it's an identifier (label name)
					if (peek().is_identifier()) {
						std::string_view identifier = peek_info().value();
						advance(); // consume the identifier
						
						// Check for second comma and alignment value
						if (peek() == ","_tok) {
							advance(); // consume second ','
							
							if (!peek().is_eof()) {
								if (peek().is_literal()) {
									std::string_view value_str = peek_info().value();
									size_t alignment = 0;
									auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
									if (result.ec == std::errc()) {
										if (pack_action == "push") {
											context_.pushPackAlignment(identifier, alignment);
										}
										advance(); // consume the number
									} else {
										advance(); // consume invalid number
										if (pack_action == "push") {
											context_.pushPackAlignment(identifier);
										} else {
											context_.popPackAlignment(identifier);
										}
									}
								} else if (peek().is_identifier()) {
									// Another identifier (macro) - treat as no alignment specified
									advance();
									if (pack_action == "push") {
										context_.pushPackAlignment(identifier);
									} else {
										context_.popPackAlignment(identifier);
									}
								}
							}
						} else {
							// Just identifier, no alignment
							if (pack_action == "push") {
								context_.pushPackAlignment(identifier);
							} else {
								context_.popPackAlignment(identifier);
							}
						}
					}
					// Check if it's a number directly (no identifier)
					else if (peek().is_literal()) {
						std::string_view value_str = peek_info().value();
						size_t alignment = 0;
						auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
						if (result.ec == std::errc()) {
							if (pack_action == "push") {
								context_.pushPackAlignment(alignment);
							}
							advance(); // consume the number
						} else {
							advance(); // consume invalid number
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
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pragma pack push/pop", current_token_);
			}
			return ParseResult::success();
		}
	}

	// Try to parse a number: pack(N)
	if (peek().is_literal()) {
		std::string_view value_str = peek_info().value();
		size_t alignment = 0;
		auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
		if (result.ec == std::errc() &&
		    (alignment == 0 || alignment == 1 || alignment == 2 ||
		     alignment == 4 || alignment == 8 || alignment == 16)) {
			context_.setPackAlignment(alignment);
			advance(); // consume the number
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after pack alignment value", current_token_);
			}
			return ParseResult::success();
		}
	}

	// If we get here, it's an unsupported pragma pack format
	return ParseResult::error("Unsupported pragma pack format", current_token_);
}

void Parser::register_builtin_functions() {
	// Register compiler builtin functions so they can be recognized as function calls
	// These will be handled as intrinsics in CodeGen
	
	// Create dummy tokens for builtin functions
	Token dummy_token(Token::Type::Identifier, ""sv, 0, 0, 0);
	
	// Helper lambda to register a builtin function with one parameter
	auto register_builtin = [&](std::string_view name, Type return_type, Type param_type) {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token);
		
		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);
		
		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);
		
		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());
		
		// Create parameter
		Token param_token = dummy_token;
		auto param_type_node = emplace_node<TypeSpecifierNode>(param_type, TypeQualifier::None, 64, param_token);
		auto param_decl = emplace_node<DeclarationNode>(param_type_node, param_token);
		func_decl_ref.add_parameter_node(param_decl);
		
		// Set extern "C" linkage
		func_decl_ref.set_linkage(Linkage::C);
		
		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};
	
	// Helper lambda to register a builtin function with two parameters
	auto register_two_param_builtin = [&](std::string_view name, Type return_type, Type param1_type, Type param2_type) {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token);
		
		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);
		
		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);
		
		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());
		
		// Create first parameter
		Token param1_token = dummy_token;
		auto param1_type_node = emplace_node<TypeSpecifierNode>(param1_type, TypeQualifier::None, 64, param1_token);
		auto param1_decl = emplace_node<DeclarationNode>(param1_type_node, param1_token);
		func_decl_ref.add_parameter_node(param1_decl);
		
		// Create second parameter
		Token param2_token = dummy_token;
		auto param2_type_node = emplace_node<TypeSpecifierNode>(param2_type, TypeQualifier::None, 64, param2_token);
		auto param2_decl = emplace_node<DeclarationNode>(param2_type_node, param2_token);
		func_decl_ref.add_parameter_node(param2_decl);
		
		// Set extern "C" linkage
		func_decl_ref.set_linkage(Linkage::C);
		
		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};
	
	// Helper lambda to register a builtin function with no parameters
	auto register_no_param_builtin = [&](std::string_view name, Type return_type, std::string_view mangled_name = "") {
		// Create return type node
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(return_type, TypeQualifier::None, 64, type_token);
		
		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);
		
		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);
		
		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());
		
		// Set pre-computed mangled name if provided
		if (!mangled_name.empty()) {
			func_decl_ref.set_mangled_name(mangled_name);
		}
		
		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};
	
	
	// Register variadic argument intrinsics (support both __va_start and __builtin_va_start)
	// __builtin_va_start(va_list*, last_param) - Clang-style
	// __va_start(va_list*, last_param) - MSVC-style (legacy)
	// Both return void
	register_two_param_builtin("__builtin_va_start", Type::Void, Type::UnsignedLongLong, Type::UnsignedLongLong);
	register_two_param_builtin("__va_start", Type::Void, Type::UnsignedLongLong, Type::UnsignedLongLong);
	
	// __builtin_va_arg(va_list, type) - returns the specified type
	// For registration purposes, we use int as the return type (will be overridden in codegen)
	// The second parameter is the type identifier, but we just register it as int for parsing
	register_two_param_builtin("__builtin_va_arg", Type::Int, Type::UnsignedLongLong, Type::Int);
	
	// Register integer abs builtins
	register_builtin("__builtin_labs", Type::Long, Type::Long);
	register_builtin("__builtin_llabs", Type::LongLong, Type::LongLong);
	
	// Register floating point abs builtins
	register_builtin("__builtin_fabs", Type::Double, Type::Double);
	register_builtin("__builtin_fabsf", Type::Float, Type::Float);
	register_builtin("__builtin_fabsl", Type::LongDouble, Type::LongDouble);
	
	// Register optimization hint intrinsics
	// __builtin_unreachable() - marks unreachable code paths
	register_no_param_builtin("__builtin_unreachable", Type::Void);
	
	// __builtin_assume(condition) - assumes condition is true for optimization
	register_builtin("__builtin_assume", Type::Void, Type::Bool);
	
	// __builtin_expect(expr, expected) - branch prediction hint, returns expr
	// Using LongLong to match typical usage pattern
	register_two_param_builtin("__builtin_expect", Type::LongLong, Type::LongLong, Type::LongLong);
	
	// __builtin_launder(ptr) - optimization barrier for pointers
	// Using UnsignedLongLong (pointer-sized) for the parameter and return type
	register_builtin("__builtin_launder", Type::UnsignedLongLong, Type::UnsignedLongLong);
	
	// Helper lambda to register a builtin function with a const char* parameter
	// Returns size_t (UnsignedLong on 64-bit)
	auto register_strlen_builtin = [&](std::string_view name) {
		// Create return type node (size_t = unsigned long on 64-bit)
		Token type_token = dummy_token;
		auto return_type_node = emplace_node<TypeSpecifierNode>(Type::UnsignedLong, TypeQualifier::None, 64, type_token);
		
		// Create function name token
		Token func_token = dummy_token;
		func_token = Token(Token::Type::Identifier, name, 0, 0, 0);
		
		// Create declaration node for the function
		auto decl_node = emplace_node<DeclarationNode>(return_type_node, func_token);
		
		// Create function declaration node
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());
		
		// Create parameter: const char* 
		Token param_token = dummy_token;
		auto param_type_node_ref = emplace_node_ref<TypeSpecifierNode>(Type::Char, TypeQualifier::None, 8, param_token);
		param_type_node_ref.second.add_pointer_level(CVQualifier::Const);  // Make it const char*
		auto param_decl = emplace_node<DeclarationNode>(param_type_node_ref.first, param_token);
		func_decl_ref.add_parameter_node(param_decl);
		
		// Set extern "C" linkage
		func_decl_ref.set_linkage(Linkage::C);
		
		// Register in global symbol table
		gSymbolTable.insert(name, func_decl_node);
	};
	
	// __builtin_strlen(const char*) - returns length of string
	// Returns size_t (unsigned long on 64-bit platforms)
	register_strlen_builtin("__builtin_strlen");
	
	// Wide memory/character functions are provided by the C library headers.
	// No manual registration here—declarations should come from the standard headers.
	
	// Register std::terminate - no pre-computed mangled name, will be mangled with namespace context
	// Note: Forward declarations inside functions don't capture namespace context,
	// so we register it globally without explicit mangling
	register_no_param_builtin("terminate", Type::Void);
}

