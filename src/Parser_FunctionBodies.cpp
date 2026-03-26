#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"


// Phase 5: Helper method to register member functions in the symbol table
// This implements C++20's complete-class context for inline member function bodies
void Parser::register_member_functions_in_scope(StructDeclarationNode* struct_node, TypeIndex struct_type_index) {
	// Add member functions from the struct itself
	if (struct_node) {
		for (const auto& member_func : struct_node->member_functions()) {
			if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
				const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
				gSymbolTable.insert(func_decl.decl_node().identifier_token().value(), member_func.function_declaration);
			}
		}
	}

	// Also add inherited member functions from base classes
	if (struct_type_index.index() < getTypeInfoCount()) {
		const TypeInfo& type_info = getTypeInfo(struct_type_index);
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (struct_info) {
			std::vector<TypeIndex> base_classes_to_search;
			for (const auto& base : struct_info->base_classes) {
				base_classes_to_search.push_back(base.type_index);
			}
			for (size_t i = 0; i < base_classes_to_search.size(); ++i) {
				TypeIndex base_idx = base_classes_to_search[i];
				if (base_idx.index() >= getTypeInfoCount()) continue;
				const TypeInfo& base_type_info = getTypeInfo(base_idx);
				const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
				if (!base_struct_info) continue;
				for (const auto& member_func : base_struct_info->member_functions) {
					if (member_func.function_decl.is<FunctionDeclarationNode>()) {
						gSymbolTable.insert(StringTable::getStringView(member_func.getName()), member_func.function_decl);
					}
				}
				for (const auto& nested_base : base_struct_info->base_classes) {
					bool already_in_list = false;
					for (TypeIndex existing : base_classes_to_search) {
						if (existing == nested_base.type_index) { already_in_list = true; break; }
					}
					if (!already_in_list) base_classes_to_search.push_back(nested_base.type_index);
				}
			}
		}
	}
}

// Phase 5: Helper method to set up member function context and scope.
// Handles all member-function entry work in one place:
//  1. Push member_function_context_stack_
//  2. Register member functions in symbol table (complete-class context)
//  3. Inject 'this' pointer into the symbol table
// Both immediate (parse_function_body_with_context) and delayed
// (parse_delayed_function_body) paths call this so they share identical setup.
void Parser::setup_member_function_context(StructDeclarationNode* struct_node, StringHandle struct_name, TypeIndex struct_type_index) {
	// Push member function context
	member_function_context_stack_.push_back({
		struct_name,
		struct_type_index,
		struct_node,
		nullptr  // local_struct_info - not needed here since TypeInfo should be available
	});

	// Register member functions in symbol table for complete-class context
	register_member_functions_in_scope(struct_node, struct_type_index);

	// Inject 'this' pointer into the symbol table.
	// Every member function, constructor, and destructor has an implicit 'this'
	// parameter of type StructName* (C++20 [class.this]).
	if (struct_type_index.index() < getTypeInfoCount()) {
		auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
			TypeCategory::Struct, struct_type_index,
			64,  // Pointer size in bits
			Token(),
			CVQualifier::None,
			ReferenceQualifier::None
		);
		this_type_ref.add_pointer_level();

		Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
		auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
		gSymbolTable.insert("this"sv, this_decl_node);
	}
}

// Phase 5: Helper to register function parameters in the symbol table
void Parser::register_parameters_in_scope(const std::vector<ASTNode>& params) {
	for (const auto& param : params) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		} else if (param.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = param.as<VariableDeclarationNode>();
			const DeclarationNode& param_decl = var_decl.declaration();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		}
	}
}

// Phase 5 + Priority 2: Unified delayed function body parsing.
// Uses FunctionParsingScopeGuard to handle all per-function entry/exit work:
//   - Symbol table function scope (enter on construction, exit on destruction)
//   - current_function_ save/restore
//   - setup_member_function_context() — context stack + member-func registration + 'this'
//   - register_parameters_in_scope()  — parameter symbol-table insertion
// The guard replaces the former SymbolTableScope + MemberContextCleanup combo,
// eliminating scattered manual cleanup blocks on every early return.
ParseResult Parser::parse_delayed_function_body(DelayedFunctionBody& delayed, std::optional<ASTNode>& out_body) {
	out_body = std::nullopt;

	const bool has_member_ctx = !delayed.is_free_function;

	// Determine function node and parameters before constructing the guard,
	// so we can pass current_function to the guard constructor.
	FunctionDeclarationNode* func_node = nullptr;
	const std::vector<ASTNode>* params_ptr = nullptr;
	if (delayed.is_constructor && delayed.ctor_node) {
		params_ptr = &delayed.ctor_node->parameter_nodes();
	} else if (delayed.func_node && !delayed.is_destructor) {
		func_node = delayed.func_node;
		params_ptr = &func_node->parameter_nodes();
	}

	// FunctionParsingScopeGuard owns scope, current_function_ save/restore,
	// member-context push/pop, 'this' injection, and parameter registration.
	// s_empty_params is used for destructors (no parameters) and as a fallback.
	static const std::vector<ASTNode> s_empty_params;
	FlashCpp::FunctionParsingScopeGuard func_guard(*this, has_member_ctx,
		delayed.struct_node, delayed.struct_name, delayed.struct_type_index,
		params_ptr ? *params_ptr : s_empty_params, func_node);

	// Parse constructor initializer list if present (for constructors with delayed parsing)
	if (delayed.is_constructor && delayed.has_initializer_list && delayed.ctor_node) {
		// Restore to the position of the initializer list (':')
		restore_token_position(delayed.initializer_list_start);

		// Parse the initializer list now that all class members are visible
		if (peek() == ":"_tok) {
			advance();  // consume ':'

			// Parse initializers until we hit '{' or ';'
			while (peek() != "{"_tok &&
			       peek() != ";"_tok) {
				// Parse initializer name (could be base class or member)
				auto init_name_token = advance();
				if (init_name_token.type() != Token::Type::Identifier) {
					return ParseResult::error("Expected member or base class name in initializer list", init_name_token);
				}

				std::string_view init_name = init_name_token.value();

				// Handle namespace-qualified base class names: std::optional<_Tp>{...}
				init_name = consume_qualified_name_suffix(init_name);

				// Check for template arguments: Base<T>(...) in base class initializer
				if (peek() == "<"_tok) {
					skip_template_arguments();
				}

				// Expect '(' or '{'
				bool is_paren = peek() == "("_tok;
				bool is_brace = peek() == "{"_tok;

				if (!is_paren && !is_brace) {
					return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
				}

				advance();  // consume '(' or '{'
				TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();

				// Parse initializer arguments
				std::vector<ASTNode> init_args;
				if (peek().is_eof() || peek() != close_kind) {
					do {
						ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (arg_result.is_error()) {
							return arg_result;
						}
						if (auto arg_node = arg_result.node()) {
							// Check for pack expansion: expr...
							if (peek() == "..."_tok) {
								Token ellipsis_token = peek_info();
								advance(); // consume '...'
								ExpressionNode& pack_expansion = gChunkedAnyStorage.emplace_back<ExpressionNode>(
									PackExpansionExprNode(*arg_node, ellipsis_token));
								init_args.push_back(ASTNode(&pack_expansion));
								continue;
							}
							init_args.push_back(*arg_node);
						}
					} while (consume(","_tok));
				}

				// Expect closing delimiter
				if (!consume(close_kind)) {
					return ParseResult::error(is_paren ? "Expected ')' after initializer arguments"
					                                   : "Expected '}' after initializer arguments", peek_info());
				}

				// Determine if this is a delegating, base class, or member initializer
				bool is_delegating = (init_name == delayed.struct_name);
				bool is_base_init = false;

				if (is_delegating) {
					// Delegating constructor: Point() : Point(0, 0) {}
					// In C++11, if a constructor delegates, it CANNOT have other initializers
					if (!delayed.ctor_node->member_initializers().empty() || !delayed.ctor_node->base_initializers().empty()) {
						return ParseResult::error("Delegating constructor cannot have other member or base initializers", init_name_token);
					}
					delayed.ctor_node->set_delegating_initializer(std::move(init_args));
				} else {
					// Check if it's a base class initializer
					if (delayed.struct_node) {
						for (const auto& base : delayed.struct_node->base_classes()) {
							if (base.name == init_name) {
								is_base_init = true;
								// Phase 7B: Intern base class name and use StringHandle overload
								StringHandle base_name_handle = StringTable::getOrInternStringHandle(init_name);
								delayed.ctor_node->add_base_initializer(base_name_handle, std::move(init_args));
								break;
							}
						}
						// Also check deferred template base classes (e.g., Base<T> in template<T> struct Derived : Base<T>)
						if (!is_base_init) {
							StringHandle init_name_handle = StringTable::getOrInternStringHandle(init_name);
							for (const auto& deferred_base : delayed.struct_node->deferred_template_base_classes()) {
								if (deferred_base.base_template_name == init_name_handle) {
									is_base_init = true;
									delayed.ctor_node->add_base_initializer(init_name_handle, std::move(init_args));
									break;
								}
							}
						}
					}

					if (!is_base_init) {
						// It's a member initializer
						if (is_brace && init_args.empty()) {
							// Empty brace-init (e.g., arr{}): C++ requires value-initialization
							// (zero-init for scalars/arrays). Store an empty InitializerListNode so
							// the constexpr evaluator and IR generator can zero-fill correctly.
							auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());
							delayed.ctor_node->add_member_initializer(init_name, init_list_node);
						} else if (is_brace && init_args.size() > 1) {
							// Multiple brace-init args (e.g., arr{a, b, c}): wrap in InitializerListNode
							auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());
							for (auto& arg : init_args) {
								init_list_ref.add_initializer(arg);
							}
							delayed.ctor_node->add_member_initializer(init_name, init_list_node);
						} else if (!init_args.empty()) {
							delayed.ctor_node->add_member_initializer(init_name, init_args[0]);
						}
					}
				}

				// Check for comma (more initializers) or '{'/';' (end of initializer list)
				if (!consume(","_tok)) {
					// No comma, so we expect '{' or ';' next
					break;
				}
			}
		}

		// After parsing initializer list, restore to the body position
		restore_token_position(delayed.body_start);
	}

	// Parse the function body.  For normal functions or member functions with body_start at 'try',
	// parse_function_body() handles everything.  For constructors/destructors with has_function_try
	// set, the 'try' was already consumed during the first pass so body_start is at '{'; in that
	// case we parse the block then parse the catch clauses ourselves and wrap everything in a try.
	const bool is_ctor_or_dtor = delayed.is_constructor || delayed.is_destructor;
	ParseResult block_result;
	if (delayed.has_function_try) {
		// 'try' already consumed; body_start is at '{'
		block_result = parse_block();
	} else {
		block_result = parse_function_body(is_ctor_or_dtor);
	}
	if (block_result.is_error()) {
		return block_result;
	}

	// When has_function_try: parse catch clauses and wrap the body in a TryStatementNode
	if (delayed.has_function_try && block_result.node().has_value()) {
		// The 'try' keyword was already consumed during the first (skip) pass, so no token
		// is available here.  Token() (the default) is fine because the token is only used
		// for error-reporting inside TryStatementNode and the 'try' location is already past.
		Token try_token;
		ASTNode try_body = *block_result.node();
		std::vector<ASTNode> catch_clauses;

		while (peek() == "catch"_tok) {
			auto clause_result = parse_one_catch_clause(catch_clauses);
			if (clause_result.is_error()) {
				return clause_result;
			}
		}

		if (catch_clauses.empty()) {
			return ParseResult::error("Expected at least one 'catch' clause after function-try-block", current_token_);
		}

		ASTNode try_stmt_block = make_try_block_body(try_body, std::move(catch_clauses), try_token, is_ctor_or_dtor);
		block_result = ParseResult::success(try_stmt_block);
	}

	// Set the body on the appropriate node
	if (block_result.node().has_value()) {
		out_body = *block_result.node();
		if (delayed.is_constructor && delayed.ctor_node) {
			delayed.ctor_node->set_definition(*block_result.node());
		} else if (delayed.is_destructor && delayed.dtor_node) {
			delayed.dtor_node->set_definition(*block_result.node());
		} else if (delayed.func_node) {
			delayed.func_node->set_definition(*block_result.node());
			finalize_function_after_definition(*delayed.func_node, true);
		}
	}

	// All cleanup handled by FunctionParsingScopeGuard (func_guard):
	// exits symbol-table scope, restores current_function_, pops member context.
	return ParseResult::success();
}

// Parse a function body.  This handles both the normal case:
//   '{' statement* '}'
// and function-try-blocks (C++11 [dcl.fct.def.general]):
//   'try' '{' statement* '}' catch-clause+
// In the latter case the result is a BlockNode containing a single TryStatementNode,
// which is semantically equivalent for non-constructor functions.
ParseResult Parser::parse_function_body(bool is_ctor_or_dtor) {
	// Normal block
	if (peek() == "{"_tok) {
		return parse_block();
	}

	// Function-try-block
	if (peek() == "try"_tok) {
		Token try_token = peek_info();
		advance();  // consume 'try'

		// Parse the try body
		auto try_block_result = parse_block();
		if (try_block_result.is_error()) {
			return try_block_result;
		}
		ASTNode try_block = *try_block_result.node();

		// Parse catch clauses (at least one required)
		std::vector<ASTNode> catch_clauses;
		while (peek() == "catch"_tok) {
			auto clause_result = parse_one_catch_clause(catch_clauses);
			if (clause_result.is_error()) {
				return clause_result;
			}
		}

		if (catch_clauses.empty()) {
			return ParseResult::error("Expected at least one 'catch' clause after function-try-block", current_token_);
		}

		return ParseResult::success(make_try_block_body(try_block, std::move(catch_clauses), try_token, is_ctor_or_dtor));
	}

	return ParseResult::error("Expected '{' or 'try' for function body", current_token_);
}

// Wrap try_body + catch_clauses into a BlockNode containing a single TryStatementNode.
// Both parse_function_body() and parse_delayed_function_body() use this common helper.
// Set is_ctor_or_dtor=true for constructor/destructor function-try-blocks so the IR generator
// can emit the C++20 [except.handle]/15 implicit rethrow at the end of each catch handler.
ASTNode Parser::make_try_block_body(ASTNode try_body, std::vector<ASTNode> catch_clauses, Token try_token, bool is_ctor_or_dtor) {
	ASTNode try_stmt = emplace_node<TryStatementNode>(try_body, std::move(catch_clauses), try_token);
	if (is_ctor_or_dtor) {
		try_stmt.as<TryStatementNode>().set_is_ctor_dtor_function_try();
	}
	auto [block_node, block_ref] = create_node_ref(BlockNode());
	block_ref.add_statement_node(try_stmt);
	return block_node;
}

// Parse one catch clause at the current token position and append it to catch_clauses.
// Expects to be called when peek() == "catch"_tok.
// Returns an error ParseResult on failure, or an empty success otherwise.
ParseResult Parser::parse_one_catch_clause(std::vector<ASTNode>& catch_clauses) {
	Token catch_token = peek_info();
	advance();  // consume 'catch'

	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' after 'catch'", current_token_);
	}

	std::optional<ASTNode> exception_declaration;
	bool is_catch_all = false;

	if (peek() == "..."_tok) {
		advance();
		is_catch_all = true;
	} else {
		auto type_result = parse_type_and_name();
		if (type_result.is_error()) {
			return type_result;
		}
		exception_declaration = type_result.node();
	}

	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after catch declaration", current_token_);
	}

	gSymbolTable.enter_scope(ScopeType::Block);
	if (!is_catch_all && exception_declaration.has_value()) {
		const auto& decl = exception_declaration->as<DeclarationNode>();
		if (!decl.identifier_token().value().empty()) {
			gSymbolTable.insert(decl.identifier_token().value(), *exception_declaration);
		}
	}
	auto catch_block_result = parse_block();
	gSymbolTable.exit_scope();

	if (catch_block_result.is_error()) {
		return catch_block_result;
	}

	ASTNode catch_block = *catch_block_result.node();
	if (is_catch_all) {
		catch_clauses.push_back(emplace_node<CatchClauseNode>(catch_block, catch_token, true));
	} else {
		catch_clauses.push_back(emplace_node<CatchClauseNode>(exception_declaration, catch_block, catch_token));
	}
	return ParseResult::success();
}
FlashCpp::SignatureValidationResult Parser::validate_signature_match(
	const FunctionDeclarationNode& declaration,
	const FunctionDeclarationNode& definition)
{
	using namespace FlashCpp;
	
	// Helper lambda to extract TypeSpecifierNode from a parameter
	auto extract_param_type = [](const ASTNode& param) -> const TypeSpecifierNode* {
		if (param.is<DeclarationNode>()) {
			return &param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
		} else if (param.is<VariableDeclarationNode>()) {
			return &param.as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
		}
		return nullptr;
	};
	
	// Validate parameter count
	const auto& decl_params = declaration.parameter_nodes();
	const auto& def_params = definition.parameter_nodes();
	
	if (decl_params.size() != def_params.size()) {
		std::string msg = "Declaration has " + std::to_string(decl_params.size()) +
		                  " parameters, definition has " + std::to_string(def_params.size());
		return SignatureValidationResult::error(SignatureMismatch::ParameterCount, 0, std::move(msg));
	}
	
	// Validate each parameter type
	for (size_t i = 0; i < decl_params.size(); ++i) {
		const TypeSpecifierNode* decl_type = extract_param_type(decl_params[i]);
		const TypeSpecifierNode* def_type = extract_param_type(def_params[i]);
		
		if (!decl_type || !def_type) {
			return SignatureValidationResult::error(SignatureMismatch::InternalError, i + 1,
				"Unable to extract parameter type information");
		}
		
		// Compare basic type properties (ignore top-level cv-qualifiers on parameters - they don't affect signature)
		if (def_type->type() != decl_type->type() ||
			def_type->type_index() != decl_type->type_index() ||
			def_type->pointer_depth() != decl_type->pointer_depth() ||
			def_type->is_reference() != decl_type->is_reference()) {
			std::string msg = "Parameter " + std::to_string(i + 1) + " type mismatch";
			return SignatureValidationResult::error(SignatureMismatch::ParameterType, i + 1, std::move(msg));
		}
		
		// For pointers, compare cv-qualifiers on pointed-to type (int* vs const int*)
		if (def_type->pointer_depth() > 0) {
			if (def_type->cv_qualifier() != decl_type->cv_qualifier()) {
				std::string msg = "Parameter " + std::to_string(i + 1) + " pointer cv-qualifier mismatch";
				return SignatureValidationResult::error(SignatureMismatch::ParameterCVQualifier, i + 1, std::move(msg));
			}
			
			// cv-qualifiers on pointer levels also matter: int* const vs int*
			const auto& def_levels = def_type->pointer_levels();
			const auto& decl_levels = decl_type->pointer_levels();
			for (size_t p = 0; p < def_levels.size(); ++p) {
				if (def_levels[p].cv_qualifier != decl_levels[p].cv_qualifier) {
					std::string msg = "Parameter " + std::to_string(i + 1) + " pointer level cv-qualifier mismatch";
					return SignatureValidationResult::error(SignatureMismatch::ParameterPointerLevel, i + 1, std::move(msg));
				}
			}
		}
		
		// For references, compare cv-qualifiers on the base type (const T& vs T&)
		if (def_type->is_reference()) {
			if (def_type->cv_qualifier() != decl_type->cv_qualifier()) {
				std::string msg = "Parameter " + std::to_string(i + 1) + " reference cv-qualifier mismatch";
				return SignatureValidationResult::error(SignatureMismatch::ParameterCVQualifier, i + 1, std::move(msg));
			}
		}
	}
	
	// Validate return type
	const DeclarationNode& decl_decl = declaration.decl_node();
	const DeclarationNode& def_decl = definition.decl_node();
	const TypeSpecifierNode& decl_return_type = decl_decl.type_node().as<TypeSpecifierNode>();
	const TypeSpecifierNode& def_return_type = def_decl.type_node().as<TypeSpecifierNode>();
	
	if (def_return_type.type() != decl_return_type.type() ||
		def_return_type.type_index() != decl_return_type.type_index() ||
		def_return_type.pointer_depth() != decl_return_type.pointer_depth() ||
		def_return_type.is_reference() != decl_return_type.is_reference()) {
		return SignatureValidationResult::error(SignatureMismatch::ReturnType, 0, "Return type mismatch");
	}
	
	return SignatureValidationResult::success();
}

void Parser::copy_function_properties(FunctionDeclarationNode& dest, const FunctionDeclarationNode& src)
{
	dest.set_namespace_handle(src.namespace_handle());
	dest.set_is_constexpr(src.is_constexpr());
	dest.set_is_consteval(src.is_consteval());
	dest.set_is_constinit(src.is_constinit());
	dest.set_noexcept(src.is_noexcept());
	if (src.has_noexcept_expression()) {
		dest.set_noexcept_expression(*src.noexcept_expression());
	}
	dest.set_is_variadic(src.is_variadic());
	dest.set_is_deleted(src.is_deleted());
	dest.set_is_static(src.is_static());
	dest.set_is_const_member_function(src.is_const_member_function());
	dest.set_is_volatile_member_function(src.is_volatile_member_function());
	dest.set_is_implicit(src.is_implicit());
	dest.set_inline_always(src.is_inline_always());
	dest.set_linkage(src.linkage());
	dest.set_calling_convention(src.calling_convention());
}

ASTNode Parser::create_defaulted_member_function_body(const FunctionDeclarationNode& func_node)
{
	auto [block_node, block_ref] = create_node_ref(BlockNode());
	const DeclarationNode& decl_node = func_node.decl_node();

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

	return block_node;
}

void Parser::finalize_function_signature_after_definition(FunctionDeclarationNode& func_node)
{
	deduce_and_update_auto_return_type(func_node);
}

void Parser::finalize_function_after_definition(FunctionDeclarationNode& func_node, bool force_recompute_mangled_name)
{
	finalize_function_signature_after_definition(func_node);
	compute_and_set_mangled_name(func_node, force_recompute_mangled_name);
}

namespace {
bool functionSignatureHasUnresolvedPlaceholder(const FunctionDeclarationNode& func_node) {
	const DeclarationNode& decl_node = func_node.decl_node();
	if (decl_node.type_node().is<TypeSpecifierNode>() &&
		isPlaceholderAutoType(decl_node.type_node().as<TypeSpecifierNode>().type())) {
		return true;
	}

	for (const auto& param_node : func_node.parameter_nodes()) {
		const DeclarationNode* param_decl = nullptr;
		if (param_node.is<DeclarationNode>()) {
			param_decl = &param_node.as<DeclarationNode>();
		} else if (param_node.is<VariableDeclarationNode>()) {
			param_decl = &param_node.as<VariableDeclarationNode>().declaration();
		}

		if (!param_decl || !param_decl->type_node().is<TypeSpecifierNode>()) {
			continue;
		}

		if (isPlaceholderAutoType(param_decl->type_node().as<TypeSpecifierNode>().type())) {
			return true;
		}
	}

	return false;
}
}

// Phase 6 (mangling): Generate and set mangled name on a FunctionDeclarationNode
// This should be called after all function properties are set (parameters, variadic flag, etc.)
// Note: The mangled name is stored as a string_view pointing to ChunkedStringAllocator storage
// which remains valid for the lifetime of the compilation.
void Parser::compute_and_set_mangled_name(FunctionDeclarationNode& func_node, bool force_recompute)
{
	// Skip if already has a mangled name
	if (!force_recompute && func_node.has_mangled_name()) {
		return;
	}

	if (functionSignatureHasUnresolvedPlaceholder(func_node)) {
		// Placeholder auto normalization now completes after parsing on some paths
		// (for example, function-try-block bodies and generic-lambda instantiations).
		// Defer mangling until the full signature is concrete.
		return;
	}

	const DeclarationNode& decl_node = func_node.decl_node();
	
	// C linkage functions don't get mangled - just use the function name as-is
	if (func_node.linkage() == Linkage::C) {
		std::string_view func_name = decl_node.identifier_token().value();
		func_node.set_mangled_name(func_name);
		return;
	}

	// Build namespace path from current symbol table state as string_view vector
	// For member functions, only build namespace path if parent_struct_name doesn't already contain namespace
	// (to avoid double-encoding the namespace in the mangled name)
	std::vector<std::string_view> ns_path;
	bool should_get_namespace = true;
	
	if (func_node.is_member_function()) {
		std::string_view parent_name = func_node.parent_struct_name();
		// If parent_struct_name already contains "::", namespace is embedded in struct name
		// so we don't need to pass it separately
		if (parent_name.find("::") != std::string_view::npos) {
			should_get_namespace = false;
		}
	}
	
	if (should_get_namespace) {
		bool struct_found = false;
		if (func_node.is_member_function()) {
			// For member functions, always derive namespace from the struct's
			// declaration-site NamespaceHandle rather than the current symbol table
			// state. This ensures correctness when instantiating from a different
			// namespace (e.g., instantiating calc::Holder<int> from main()).
			std::string_view parent_name = func_node.parent_struct_name();
			auto struct_name_handle = StringTable::getOrInternStringHandle(parent_name);
			auto type_it = getTypesByNameMap().find(struct_name_handle);
			if (type_it != getTypesByNameMap().end()) {
				struct_found = true;
				ns_path = buildNamespacePathFromHandle(type_it->second->namespaceHandle());
			}
		}
		if (!struct_found && ns_path.empty()) {
			// Free functions, or member functions whose struct wasn't found: fall back
			// to the current symbol table namespace. Do NOT fall back when the struct
			// was found at global scope — an empty ns_path is correct in that case,
			// and overwriting it with the instantiation-site namespace would produce
			// wrong mangled names.
			NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
			std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
			ns_path = splitQualifiedNamespace(qualified_namespace);
		}
	}
	
	// Generate the mangled name using the NameMangling helper
	NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(func_node, ns_path);
	
	// Set the mangled name on the node
	func_node.set_mangled_name(mangled.view());
}

ParseResult Parser::parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention)
{
	// Create the function declaration first
	auto [func_node, func_ref] =
		create_node_ref<FunctionDeclarationNode>(declaration_node);
	
	// Set calling convention immediately so it's available during parameter parsing
	func_ref.set_calling_convention(calling_convention);

	// Set linkage from current context (for extern "C" blocks)
	if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	}

	// Use unified parameter list parsing (Phase 1)
	FlashCpp::ParsedParameterList params;
	auto param_result = parse_parameter_list(params, calling_convention);
	if (param_result.is_error()) {
		return param_result;
	}

	// Apply the parsed parameters to the function
	for (const auto& param : params.parameters) {
		func_ref.add_parameter_node(param);
	}
	func_ref.set_is_variadic(params.is_variadic);

	// If linkage wasn't set from current context, check if there's a forward declaration with linkage
	if (func_ref.linkage() == Linkage::None) {
		// Use lookup_all to check all overloads in case there are multiple
		auto all_overloads = gSymbolTable.lookup_all(declaration_node.identifier_token().value());
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

	// Note: Trailing specifiers (const, volatile, &, &&, noexcept, override, final, 
	// = 0, = default, = delete, __attribute__) are NOT handled here.
	// Each call site is responsible for handling trailing specifiers as appropriate:
	// - Free functions: call skip_function_trailing_specifiers() or parse_function_trailing_specifiers()
	// - Member functions: the struct member parsing handles these with full semantic information

	return func_node;
}

// Priority 4: Find a regular (non-constructor, non-destructor) member function in struct_info
// that matches the given name, cv-qualifiers, and parameter count.
// Returns a mutable pointer to the matching StructMemberFunction, or nullptr if not found.
//
// Per C++20 [dcl.fct.def.general]/p2, an out-of-line definition must match a previously
// introduced declaration by name, parameter-type-list, and cv-qualifiers.
// Entries stored as FunctionDeclarationNode are fully verifiable (param count checked).
// Entries stored under a different node type (e.g. some operator representations) cannot
// have their parameter-type-list verified here; they are accepted as matching declarations
// only when no fully-verifiable match exists.
StructMemberFunction* Parser::find_member_function_by_signature(
	StructTypeInfo& struct_info,
	StringHandle name,
	const FlashCpp::MemberQualifiers& quals,
	size_t param_count)
{
	StructMemberFunction* unverified_match = nullptr;
	for (auto& member : struct_info.member_functions) {
		if (member.getName() != name ||
			member.is_const()    != quals.is_const() ||
			member.is_volatile() != quals.is_volatile()) {
			continue;
		}
		if (member.function_decl.is<FunctionDeclarationNode>()) {
			const auto& decl_func = member.function_decl.as<FunctionDeclarationNode>();
			if (decl_func.parameter_nodes().size() == param_count) {
				return &member;  // Fully-verified match — prefer over any unverified one.
			}
		} else if (!unverified_match) {
			// Declaration stored under a non-FunctionDeclarationNode node type:
			// name and qualifiers match but parameter count cannot be verified.
			// Keep as candidate and continue looking for a fully-verified match.
			unverified_match = &member;
		}
	}
	return unverified_match;
}

// Priority 4: Helper to extract the TypeSpecifierNode from a parameter ASTNode.
// Returns nullptr if the parameter is not a DeclarationNode or VariableDeclarationNode
// with a TypeSpecifierNode.
static const TypeSpecifierNode* get_param_type_specifier(const ASTNode& param) {
	if (param.is<VariableDeclarationNode>()) {
		const VariableDeclarationNode& var = param.as<VariableDeclarationNode>();
		if (var.declaration().type_node().is<TypeSpecifierNode>())
			return &var.declaration().type_node().as<TypeSpecifierNode>();
	} else if (param.is<DeclarationNode>()) {
		const DeclarationNode& decl = param.as<DeclarationNode>();
		if (decl.type_node().is<TypeSpecifierNode>())
			return &decl.type_node().as<TypeSpecifierNode>();
	}
	return nullptr;
}

// Priority 4: Find the constructor or destructor in struct_info that matches the given
// parameter list.  Skips entries that already have a definition (avoids duplicate body).
// For destructors, parameter matching is not performed (destructors have no parameters).
// Returns a mutable pointer to the matching StructMemberFunction, or nullptr if not found.
StructMemberFunction* Parser::find_ctor_dtor_for_definition(
	StructTypeInfo& struct_info,
	bool is_destructor,
	const FlashCpp::ParsedParameterList& params)
{
	const size_t param_count = params.parameters.size();

	for (auto& member : struct_info.member_functions) {
		if (is_destructor) {
			if (!member.is_destructor) continue;
			if (member.function_decl.is<DestructorDeclarationNode>()) {
				const DestructorDeclarationNode& dtor = member.function_decl.as<DestructorDeclarationNode>();
				if (dtor.get_definition().has_value()) continue;  // already defined
			}
			return &member;
		}

		// Constructor lookup: match by parameter count and types
		if (!member.is_constructor) continue;
		if (!member.function_decl.is<ConstructorDeclarationNode>()) continue;
		const ConstructorDeclarationNode& ctor = member.function_decl.as<ConstructorDeclarationNode>();
		if (ctor.get_definition().has_value()) continue;  // already defined
		if (ctor.parameter_nodes().size() != param_count) continue;

		bool params_match = true;
		for (size_t i = 0; i < param_count && params_match; ++i) {
			const TypeSpecifierNode* decl_type = get_param_type_specifier(ctor.parameter_nodes()[i]);
			const TypeSpecifierNode* def_type  = get_param_type_specifier(params.parameters[i]);
			if (!decl_type || !def_type) { params_match = false; break; }
			if (decl_type->type()          != def_type->type()          ||
				decl_type->pointer_depth() != def_type->pointer_depth() ||
				decl_type->is_reference()  != def_type->is_reference()  ||
				decl_type->type_index()    != def_type->type_index()) {
				params_match = false;
			}
		}
		if (params_match) return &member;
	}
	return nullptr;
}
