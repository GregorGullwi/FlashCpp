// Phase 5: Unified function body parsing
// This method handles all the common body parsing logic including:
// - = default handling
// - = delete handling
// - Declaration-only (no body)
// - Scope setup with RAII guards
// - 'this' pointer injection for member functions
// - Parameter registration
// - Block parsing
ParseResult Parser::parse_function_body_with_context(
	const FlashCpp::FunctionParsingContext& ctx,
	const FlashCpp::ParsedFunctionHeader& header,
	std::optional<ASTNode>& out_body
) {
	// Initialize output
	out_body = std::nullopt;

	// Handle = default
	if (header.specifiers.is_defaulted()) {
		auto [block_node, block_ref] = create_node_ref(BlockNode());
		out_body = block_node;
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Handle = delete
	if (header.specifiers.is_deleted()) {
		// No body for deleted functions
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Handle pure virtual (= 0)
	if (header.specifiers.is_pure_virtual()) {
		// No body for pure virtual functions
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Check for declaration only (no body) - semicolon
	if (peek() == ";"_tok) {
		advance();  // consume ';'
		return ParseResult::success();  // Declaration only, no body
	}

	// Expect function body with '{'
	if (peek() != "{"_tok) {
		return ParseResult::error("Expected '{' or ';' after function declaration", current_token_);
	}

	// Set up function scope using RAII guard (Phase 3)
	FlashCpp::SymbolTableScope func_scope(ScopeType::Function);

	// Inject 'this' pointer for member functions, constructors, and destructors
	if (ctx.kind == FlashCpp::FunctionKind::Member ||
	    ctx.kind == FlashCpp::FunctionKind::Constructor ||
	    ctx.kind == FlashCpp::FunctionKind::Destructor) {
		// Find the parent struct type
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(ctx.parent_struct_name));
		if (type_it != gTypesByName.end()) {
			// Create 'this' pointer type: StructName*
			auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
				Type::Struct, type_it->second->type_index_,
				64,  // Pointer size in bits
				Token()
			);
			this_type_ref.add_pointer_level();

			// Create a declaration node for 'this'
			Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
			auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);

			// Insert 'this' into the symbol table
			gSymbolTable.insert("this"sv, this_decl_node);
		}
	}

	// Register parameters in the symbol table
	for (const auto& param : header.params.parameters) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl_node = param.as<DeclarationNode>();
			const Token& param_token = param_decl_node.identifier_token();
			gSymbolTable.insert(param_token.value(), param);
		}
	}

	// Parse the block
	auto block_result = parse_block();
	if (block_result.is_error()) {
		return block_result;
	}

	if (block_result.node().has_value()) {
		out_body = *block_result.node();
	}

	// func_scope automatically exits scope when destroyed

	return ParseResult::success();
}

// Phase 5: Helper method to register member functions in the symbol table
// This implements C++20's complete-class context for inline member function bodies
void Parser::register_member_functions_in_scope(StructDeclarationNode* struct_node, size_t struct_type_index) {
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
	if (struct_type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[struct_type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (struct_info) {
			std::vector<TypeIndex> base_classes_to_search;
			for (const auto& base : struct_info->base_classes) {
				base_classes_to_search.push_back(base.type_index);
			}
			for (size_t i = 0; i < base_classes_to_search.size(); ++i) {
				TypeIndex base_idx = base_classes_to_search[i];
				if (base_idx >= gTypeInfo.size()) continue;
				const TypeInfo& base_type_info = gTypeInfo[base_idx];
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

// Phase 5: Helper method to set up member function context and scope
void Parser::setup_member_function_context(StructDeclarationNode* struct_node, StringHandle struct_name, size_t struct_type_index) {
	// Push member function context
	member_function_context_stack_.push_back({
		struct_name,
		struct_type_index,
		struct_node,
		nullptr  // local_struct_info - not needed here since TypeInfo should be available
	});

	// Register member functions in symbol table for complete-class context
	register_member_functions_in_scope(struct_node, struct_type_index);
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

// Phase 5: Unified delayed function body parsing
ParseResult Parser::parse_delayed_function_body(DelayedFunctionBody& delayed, std::optional<ASTNode>& out_body) {
	out_body = std::nullopt;
	
	// Enter function scope
	gSymbolTable.enter_scope(ScopeType::Function);
	
	// Set up member function context
	setup_member_function_context(delayed.struct_node, delayed.struct_name, delayed.struct_type_index);
	
	// Get the appropriate function node and parameters
	FunctionDeclarationNode* func_node = nullptr;
	const std::vector<ASTNode>* params = nullptr;
	
	if (delayed.is_constructor && delayed.ctor_node) {
		current_function_ = nullptr;  // Constructors don't have return type
		params = &delayed.ctor_node->parameter_nodes();
	} else if (delayed.is_destructor && delayed.dtor_node) {
		current_function_ = nullptr;  // Destructors don't have return type
		// Destructors have no parameters
	} else if (delayed.func_node) {
		func_node = delayed.func_node;
		current_function_ = func_node;
		params = &func_node->parameter_nodes();
	}
	
	// Register parameters in symbol table
	if (params) {
		register_parameters_in_scope(*params);
	}
	
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
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return ParseResult::error("Expected member or base class name in initializer list", init_name_token);
				}

				std::string_view init_name = init_name_token.value();

				// Check for template arguments: Base<T>(...) in base class initializer
				if (peek() == "<"_tok) {
					skip_template_arguments();
				}

				// Expect '(' or '{'
				bool is_paren = peek() == "("_tok;
				bool is_brace = peek() == "{"_tok;

				if (!is_paren && !is_brace) {
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
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
							// Clean up
							current_function_ = nullptr;
							member_function_context_stack_.pop_back();
							gSymbolTable.exit_scope();
							return arg_result;
						}
						if (auto arg_node = arg_result.node()) {
							// Check for pack expansion: expr...
							if (peek() == "..."_tok) {
								advance(); // consume '...'
								// Mark this as a pack expansion - actual expansion happens at instantiation
							}
							init_args.push_back(*arg_node);
						}
					} while (consume(","_tok));
				}

				// Expect closing delimiter
				if (!consume(close_kind)) {
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
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
						// Clean up
						current_function_ = nullptr;
						member_function_context_stack_.pop_back();
						gSymbolTable.exit_scope();
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
						// For simplicity, we'll use the first argument as the initializer expression
						if (!init_args.empty()) {
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
	
	// Parse the function body
	auto block_result = parse_block();
	if (block_result.is_error()) {
		// Clean up
		current_function_ = nullptr;
		member_function_context_stack_.pop_back();
		gSymbolTable.exit_scope();
		return block_result;
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
			// Deduce auto return types from function body (only if return type is auto)
			TypeSpecifierNode return_type = delayed.func_node->decl_node().type_node().as<TypeSpecifierNode>();
			if (return_type.type() == Type::Auto) {
				deduce_and_update_auto_return_type(*delayed.func_node);
			}
		}
	}
	
	// Clean up context
	current_function_ = nullptr;
	member_function_context_stack_.pop_back();
	gSymbolTable.exit_scope();
	
	return ParseResult::success();
}

// Phase 7: Unified signature validation for out-of-line definitions
// Compares a declaration's signature with a definition's signature and returns detailed mismatch information
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

// Phase 6 (mangling): Generate and set mangled name on a FunctionDeclarationNode
// This should be called after all function properties are set (parameters, variadic flag, etc.)
// Note: The mangled name is stored as a string_view pointing to ChunkedStringAllocator storage
// which remains valid for the lifetime of the compilation.
void Parser::compute_and_set_mangled_name(FunctionDeclarationNode& func_node)
{
	// Skip if already has a mangled name
	if (func_node.has_mangled_name()) {
		return;
	}
	
	// C linkage functions don't get mangled - just use the function name as-is
	if (func_node.linkage() == Linkage::C) {
		const DeclarationNode& decl_node = func_node.decl_node();
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
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
		ns_path = splitQualifiedNamespace(qualified_namespace);
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
