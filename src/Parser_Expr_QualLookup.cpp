ParseResult Parser::parse_qualified_identifier() {
	// This method parses qualified identifiers like std::print or ns1::ns2::func
	// It should be called when we've already seen an identifier followed by ::

	std::vector<StringType<>> namespaces;
	Token final_identifier;

	// We should already be at an identifier
	auto first_token = peek_info();
	if (first_token.type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier in qualified name", first_token);
	}

	// Collect namespace parts
	while (true) {
		auto identifier_token = advance();
		if (identifier_token.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier", identifier_token);
		}

		// Check if followed by ::
		if (peek() == "::"_tok) {
			// This is a namespace part
			namespaces.emplace_back(StringType<>(identifier_token.value()));
			advance(); // consume ::
		} else {
			// This is the final identifier
			final_identifier = identifier_token;
			break;
		}
	}

	// Create a QualifiedIdentifierNode
	NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
	auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
	return ParseResult::success(qualified_node);
}

// Helper: Parse template brace initialization: Template<Args>{}
// Parses the brace initializer, looks up the instantiated type, and creates a ConstructorCallNode
ParseResult Parser::parse_template_brace_initialization(
        const std::vector<TemplateTypeArg>& template_args,
        std::string_view template_name,
        const Token& identifier_token) {
	
	// Build the instantiated type name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	
	// Look up the instantiated type
	auto type_handle = StringTable::getOrInternStringHandle(instantiated_name);
	auto type_it = gTypesByName.find(type_handle);
	if (type_it == gTypesByName.end()) {
		// Type not found with provided args - try filling in default template arguments
		auto template_lookup = gTemplateRegistry.lookupTemplate(template_name);
		if (template_lookup.has_value() && template_lookup->is<TemplateClassDeclarationNode>()) {
			const auto& template_class = template_lookup->as<TemplateClassDeclarationNode>();
			const auto& template_params = template_class.template_parameters();
			if (template_args.size() < template_params.size()) {
				std::vector<TemplateTypeArg> filled_args = template_args;
				for (size_t i = filled_args.size(); i < template_params.size(); ++i) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
						const ASTNode& default_node = param.default_value();
						if (default_node.is<TypeSpecifierNode>()) {
							filled_args.push_back(TemplateTypeArg(default_node.as<TypeSpecifierNode>()));
						}
					}
				}
				if (filled_args.size() > template_args.size()) {
					instantiated_name = get_instantiated_class_name(template_name, filled_args);
					type_handle = StringTable::getOrInternStringHandle(instantiated_name);
					type_it = gTypesByName.find(type_handle);
				}
			}
		}
		if (type_it == gTypesByName.end()) {
			// Type not found - instantiation may have failed
			return ParseResult::error("Template instantiation failed or type not found", identifier_token);
		}
	}
	
	// Determine which token checking method to use based on what token is '{'
	// If current_token_ is '{', we use current_token_ style checking
	// Otherwise, we use peek_token() style checking
	bool use_current_token = current_token_.value() == "{";
	
	// Consume the opening '{'
	if (use_current_token) {
		advance(); // consume '{'
	} else if (peek() == "{"_tok) {
		advance(); // consume '{'
	} else {
		return ParseResult::error("Expected '{' for brace initialization", identifier_token);
	}
	
	// Parse arguments inside braces
	ChunkedVector<ASTNode> args;
	while (true) {
		// Check for closing brace
		bool at_close = use_current_token 
			? (current_token_.value() == "}")
			: (peek() == "}"_tok);
		
		if (at_close) {
			break;
		}
		
		// Parse argument expression
		auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (argResult.is_error()) {
			return argResult;
		}
		if (auto node = argResult.node()) {
			args.push_back(*node);
		}
		
		// Check for comma or closing brace
		bool has_comma = use_current_token
			? (current_token_.value() == ",")
			: (peek() == ","_tok);
		
		bool has_close = use_current_token
			? (current_token_.value() == "}")
			: (peek() == "}"_tok);
		
		if (has_comma) {
			advance(); // consume ','
		} else if (!has_close) {
			return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
		}
	}
	
	// Consume the closing '}'
	if (use_current_token) {
		if (current_token_.kind().is_eof() || current_token_.value() != "}") {
			return ParseResult::error("Expected '}' after brace initializer", current_token_);
		}
		advance();
	} else {
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after brace initializer", current_token_);
		}
	}
	
	// Create TypeSpecifierNode for the instantiated class
	const TypeInfo& type_info = *type_it->second;
	TypeIndex type_index = type_info.type_index_;
	int type_size = 0;
	if (type_info.struct_info_) {
		type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
	}
	Token type_token(Token::Type::Identifier, instantiated_name, 
	                identifier_token.line(), identifier_token.column(), identifier_token.file_index());
	auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, type_token);
	
	// Create ConstructorCallNode
	std::optional<ASTNode> result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), type_token));
	return ParseResult::success(*result);
}

// Helper: Parse qualified identifier path after template arguments (Template<T>::member)
// Assumes we're positioned right after template arguments and next token is ::
// Returns a QualifiedIdentifierNode wrapped in ExpressionNode if successful
ParseResult Parser::parse_qualified_identifier_after_template(const Token& template_base_token, bool* had_template_keyword) {
	std::vector<StringType<32>> namespaces;
	Token final_identifier = template_base_token;  // Start with the template name
	bool encountered_template_keyword = false;
	
	// Collect the qualified path after ::
	while (peek() == "::"_tok) {
		// Current identifier becomes a namespace part
		namespaces.emplace_back(StringType<32>(final_identifier.value()));
		advance(); // consume ::
		
		// Handle optional 'template' keyword in dependent contexts
		// e.g., typename Base<T>::template member<U>
		if (peek() == "template"_tok) {
			advance(); // consume 'template'
			encountered_template_keyword = true;  // Track that we saw 'template' keyword
		}
		
		// Get next identifier
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected identifier after '::'", peek_info());
		}
		final_identifier = peek_info();
		advance(); // consume the identifier
	}
	
	// Report whether we encountered a 'template' keyword
	if (had_template_keyword) {
		*had_template_keyword = encountered_template_keyword;
	}
	
	// Create a QualifiedIdentifierNode
	NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
	auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
	return ParseResult::success(qualified_node);
}

// Helper to parse member template function calls: Template<T>::member<U>()
// This consolidates the logic for parsing member template arguments and function calls
// that appears in multiple places when handling qualified identifiers after template instantiation.
std::optional<ParseResult> Parser::try_parse_member_template_function_call(
	std::string_view instantiated_class_name,
	std::string_view member_name,
	const Token& member_token) {
	
	FLASH_LOG(Templates, Debug, "try_parse_member_template_function_call called for: ", instantiated_class_name, "::", member_name);
	
	// Check for member template arguments: Template<T>::member<U>
	std::optional<std::vector<TemplateTypeArg>> member_template_args;
	if (peek() == "<"_tok) {
		// Before parsing < as template arguments, check if the member is actually a template
		// This prevents misinterpreting patterns like R1<T>::num < R2<T>::num> where < is comparison
		
		// Check if the member is a known template (class or variable template)
		auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
		auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
		
		// Also check with the qualified name (instantiated_class_name::member_name)
		StringBuilder qualified_member_builder;
		qualified_member_builder.append(instantiated_class_name).append("::").append(member_name);
		std::string_view qualified_member_name = qualified_member_builder.preview();
		
		auto qual_template_opt = gTemplateRegistry.lookupTemplate(qualified_member_name);
		auto qual_var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_member_name);
		
		bool is_known_template = member_template_opt.has_value() || member_var_template_opt.has_value() ||
		                         qual_template_opt.has_value() || qual_var_template_opt.has_value();
		
		qualified_member_builder.reset();
		
		if (is_known_template) {
			member_template_args = parse_explicit_template_arguments();
			// If parsing failed, it might be a less-than operator, but that's rare for member access
		} else {
			// Member is NOT a known template - don't parse < as template arguments
			// This handles patterns like integral_constant<bool, R1::num < R2::num>
			FLASH_LOG_FORMAT(Parser, Debug, 
			    "Member '{}' is not a known template - not parsing '<' as template arguments",
			    member_name);
		}
	}
	
	// Check for function call: Template<T>::member() or Template<T>::member<U>()
	if (peek() != "("_tok) {
		return std::nullopt;  // Not a function call
	}
	
	advance(); // consume '('
	
	// Parse function arguments
	ChunkedVector<ASTNode> args;
	while (!peek().is_eof() && peek() != ")"_tok) {
		ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (argResult.is_error()) {
			return argResult;
		}
		
		if (argResult.node().has_value()) {
			args.push_back(*argResult.node());
		}
		
		// Check for comma between arguments
		if (peek() == ","_tok) {
			advance(); // consume ','
		} else if (!peek().is_eof() && peek() != ")"_tok) {
			return ParseResult::error("Expected ',' or ')' in function arguments", peek_info());
		}
	}
	
	// Expect closing parenthesis
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after function arguments", current_token_);
	}
	
	// Try to instantiate the member template function if we have explicit template args
	std::optional<ASTNode> instantiated_func;
	if (member_template_args.has_value() && !member_template_args->empty()) {
		instantiated_func = try_instantiate_member_function_template_explicit(
			instantiated_class_name,
			member_name,
			*member_template_args
		);
	}
	
	// Trigger lazy member function instantiation if needed
	if (!instantiated_func.has_value()) {
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_class_name);
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		FLASH_LOG(Templates, Debug, "Checking lazy instantiation for: ", instantiated_class_name, "::", member_name);
		if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
			FLASH_LOG(Templates, Debug, "Lazy instantiation triggered for qualified call: ", instantiated_class_name, "::", member_name);
			auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
			if (lazy_info_opt.has_value()) {
				instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
				LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
			}
		}
		// If the hash-based name didn't match (dependent vs concrete hash mismatch),
		// try to find the correct instantiation by looking up gTypesByName for a matching
		// template instantiation with the same base template name.
		if (!instantiated_func.has_value()) {
			std::string_view base_tmpl = extractBaseTemplateName(instantiated_class_name);
			if (!base_tmpl.empty()) {
				// Search all types to find a matching template instantiation
				for (const auto& [name_handle, type_info_ptr] : gTypesByName) {
					if (type_info_ptr->isTemplateInstantiation() &&
					    StringTable::getStringView(type_info_ptr->baseTemplateName()) == base_tmpl &&
					    StringTable::getStringView(name_handle) != instantiated_class_name) {
						StringHandle alt_class_handle = name_handle;
						if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(alt_class_handle, member_name_handle)) {
							FLASH_LOG(Templates, Debug, "Lazy instantiation triggered via base template match: ", 
							          StringTable::getStringView(alt_class_handle), "::", member_name);
							auto lazy_info_opt2 = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(alt_class_handle, member_name_handle);
							if (lazy_info_opt2.has_value()) {
								instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt2);
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(alt_class_handle, member_name_handle);
								// Update instantiated_class_name to the correct one for mangling
								instantiated_class_name = StringTable::getStringView(alt_class_handle);
								break;
							}
						}
					}
				}
			}
		}
	}
	
	// Build qualified function name including template args
	StringBuilder func_name_builder;
	func_name_builder.append(instantiated_class_name);
	func_name_builder.append("::");
	func_name_builder.append(member_name);
	
	// If member has template args, append them using hash-based naming
	if (member_template_args.has_value() && !member_template_args->empty()) {
		// Generate hash suffix for template args
		auto key = FlashCpp::makeInstantiationKey(StringTable::getOrInternStringHandle(member_name), *member_template_args);
		func_name_builder.append("$");
		auto hash_val = FlashCpp::TemplateInstantiationKeyHash{}(key);
		char hex[17];
		std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash_val));
		func_name_builder.append(std::string_view(hex, 16));
	}
	std::string_view func_name = func_name_builder.commit();
	
	// Create function call token
	Token func_token(Token::Type::Identifier, func_name, 
	                member_token.line(), 
	                member_token.column(),
	                member_token.file_index());
	
	// If we successfully instantiated the function, use its declaration
	const DeclarationNode* decl_ptr = nullptr;
	const FunctionDeclarationNode* func_decl_ptr = nullptr;
	if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
		func_decl_ptr = &instantiated_func->as<FunctionDeclarationNode>();
		decl_ptr = &func_decl_ptr->decl_node();
	} else {
		// For non-template member functions (e.g. Template<T>::allocate()),
		// resolve directly from the instantiated class before creating a fallback decl.
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_class_name);
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		auto type_it = gTypesByName.find(class_name_handle);
		if (type_it != gTypesByName.end() && type_it->second) {
			const StructTypeInfo* struct_info = type_it->second->getStructInfo();
			if (struct_info) {
				const FunctionDeclarationNode* first_name_match = nullptr;
				size_t call_arg_count = args.size();
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.getName() == member_name_handle && member_func.function_decl.is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
						if (!first_name_match) {
							first_name_match = &candidate;
						}
						if (candidate.parameter_nodes().size() == call_arg_count) {
							func_decl_ptr = &candidate;
							decl_ptr = &func_decl_ptr->decl_node();
							break;
						}
					}
				}
				if (!decl_ptr && first_name_match) {
					func_decl_ptr = first_name_match;
					decl_ptr = &func_decl_ptr->decl_node();
				}
			}
		}

		// Fall back to forward declaration only if we still couldn't resolve.
		if (!decl_ptr) {
			auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, func_token);
			auto forward_decl = emplace_node<DeclarationNode>(type_node, func_token);
			decl_ptr = &forward_decl.as<DeclarationNode>();
		}
	}
	
	auto result = emplace_node<ExpressionNode>(FunctionCallNode(*decl_ptr, std::move(args), func_token));
	
	// Set the mangled name on the function call if we have the function declaration
	if (func_decl_ptr && func_decl_ptr->has_mangled_name()) {
		std::get<FunctionCallNode>(result.as<ExpressionNode>()).set_mangled_name(func_decl_ptr->mangled_name());
	}
	
	return ParseResult::success(result);
}

std::string Parser::buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const {
	StringBuilder result;

	// Get return type from the function's declaration node
	const DeclarationNode& decl = func_node.decl_node();
	const TypeSpecifierNode& ret_type = decl.type_node().as<TypeSpecifierNode>();
	result.append(ret_type.getReadableString()).append(" ");

	// Add namespace prefix if we're in a namespace
	NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
	std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
	if (!qualified_namespace.empty()) {
		result.append(qualified_namespace).append("::");
	}

	// Add class/struct prefix if this is a member function
	if (func_node.is_member_function()) {
		result.append(func_node.parent_struct_name()).append("::");
	}

	// Add function name
	result.append(decl.identifier_token().value());

	// Add parameters
	result.append("(");
	const auto& params = func_node.parameter_nodes();
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) result.append(", ");
		const auto& param_decl = params[i].as<DeclarationNode>();
		const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		result.append(param_type.getReadableString());
	}

	// Add variadic ellipsis if this is a variadic function
	if (func_node.is_variadic()) {
		if (!params.empty()) result.append(", ");
		result.append("...");
	}

	result.append(")");

	return std::string(result.commit());
}

// Check if an identifier name is a template parameter in current scope
bool Parser::is_template_parameter(std::string_view name) const {
    bool result = std::find(template_param_names_.begin(), template_param_names_.end(), name) != template_param_names_.end();
    return result;
}

// Helper: Check if a base class name is a template parameter
// Returns true if the name matches any template parameter in the current template scope
bool Parser::is_base_class_template_parameter(std::string_view base_class_name) const {
	for (const auto& param_name : current_template_param_names_) {
		if (StringTable::getStringView(param_name) == base_class_name) {
			FLASH_LOG_FORMAT(Templates, Debug, 
				"Base class '{}' is a template parameter - deferring resolution", 
				base_class_name);
			return true;
		}
	}
	return false;
}

// Helper: Look up a type alias including inherited ones from base classes
// Searches struct_name::member_name first, then recursively searches base classes
// Uses depth limit to prevent infinite recursion in case of malformed input
const TypeInfo* Parser::lookup_inherited_type_alias(StringHandle struct_name, StringHandle member_name, int depth) {
	// Prevent infinite recursion with a reasonable depth limit
	constexpr int kMaxInheritanceDepth = 100;
	if (depth > kMaxInheritanceDepth) {
		FLASH_LOG_FORMAT(Templates, Warning, "lookup_inherited_type_alias: max depth exceeded for '{}::{}'", 
		                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name));
		return nullptr;
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "lookup_inherited_type_alias: looking for '{}::{}' ", 
	                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name));
	
	// First try direct lookup with qualified name
	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(struct_name))
	                     .append("::")
	                     .append(StringTable::getStringView(member_name));
	std::string_view qualified_name = qualified_name_builder.commit();
	
	auto direct_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name));
	if (direct_it != gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Found direct type alias '{}'", qualified_name);
		return direct_it->second;
	}
	
	// Not found directly, look up the struct and search its base classes
	auto struct_it = gTypesByName.find(struct_name);
	if (struct_it == gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' not found in gTypesByName", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	const TypeInfo* struct_type_info = struct_it->second;
	
	// If this is a type alias (no struct_info_), resolve the underlying type
	if (!struct_type_info->struct_info_) {
		// This might be a type alias - try to find the actual struct type
		// Type aliases have a type_index that points to the underlying type
		// Check if type_index_ is valid and points to a different TypeInfo entry
		if (struct_type_info->type_index_ < gTypeInfo.size()) {
			const TypeInfo& underlying_type = gTypeInfo[struct_type_info->type_index_];
			// Check if this is actually an alias (points to a different TypeInfo)
			// by comparing the pointer addresses
			if (&underlying_type != struct_type_info && underlying_type.struct_info_) {
				StringHandle underlying_name = underlying_type.name();
				FLASH_LOG_FORMAT(Templates, Debug, "Type '{}' is an alias for '{}', following alias", 
				                 StringTable::getStringView(struct_name), StringTable::getStringView(underlying_name));
				return lookup_inherited_type_alias(underlying_name, member_name, depth + 1);
			}
		}
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has no struct_info_ and couldn't resolve alias", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	// Search base classes recursively
	const StructTypeInfo* struct_info = struct_type_info->struct_info_.get();
	FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has {} base classes", StringTable::getStringView(struct_name), struct_info->base_classes.size());
	for (const auto& base_class : struct_info->base_classes) {
		// Skip deferred base classes (they haven't been resolved yet)
		if (base_class.is_deferred) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping deferred base class '{}'", base_class.name);
			continue;
		}
		
		FLASH_LOG_FORMAT(Templates, Debug, "Checking base class '{}'", base_class.name);
		// Recursively look up in base class - convert base_class.name to StringHandle for performance
		StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class.name);
		const TypeInfo* base_result = lookup_inherited_type_alias(base_name_handle, member_name, depth + 1);
		if (base_result != nullptr) {
			FLASH_LOG_FORMAT(Templates, Debug, "Found inherited type alias '{}::{}' via base class '{}'",
			                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name), base_class.name);
			return base_result;
		}
	}
	
	return nullptr;
}

// Helper: Look up a template function including inherited ones from base classes
const std::vector<ASTNode>* Parser::lookup_inherited_template(StringHandle struct_name, std::string_view template_name, int depth) {
	// Prevent infinite recursion with a reasonable depth limit
	constexpr int kMaxInheritanceDepth = 100;
	if (depth > kMaxInheritanceDepth) {
		FLASH_LOG_FORMAT(Templates, Warning, "lookup_inherited_template: max depth exceeded for '{}::{}'", 
		                 StringTable::getStringView(struct_name), template_name);
		return nullptr;
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "lookup_inherited_template: looking for '{}::{}' ", 
	                 StringTable::getStringView(struct_name), template_name);
	
	// First try direct lookup with qualified name (ClassName::functionName)
	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(struct_name))
	                     .append("::")
	                     .append(template_name);
	std::string_view qualified_name = qualified_name_builder.commit();
	
	const std::vector<ASTNode>* direct_templates = gTemplateRegistry.lookupAllTemplates(qualified_name);
	if (direct_templates != nullptr && !direct_templates->empty()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Found direct template function '{}'", qualified_name);
		return direct_templates;
	}
	
	// Not found directly, look up the struct and search its base classes
	auto struct_it = gTypesByName.find(struct_name);
	if (struct_it == gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' not found in gTypesByName", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	const TypeInfo* struct_type_info = struct_it->second;
	
	// If this is a type alias (no struct_info_), resolve the underlying type
	if (!struct_type_info->struct_info_) {
		// This might be a type alias - try to find the actual struct type
		// Type aliases have a type_index that points to the underlying type
		// Check if type_index_ is valid and points to a different TypeInfo entry
		if (struct_type_info->type_index_ < gTypeInfo.size()) {
			const TypeInfo& underlying_type = gTypeInfo[struct_type_info->type_index_];
			// Check if this is actually an alias (points to a different TypeInfo)
			// by comparing the pointer addresses
			if (&underlying_type != struct_type_info && underlying_type.struct_info_) {
				StringHandle underlying_name = underlying_type.name();
				FLASH_LOG_FORMAT(Templates, Debug, "Type '{}' is an alias for '{}', following alias", 
				                 StringTable::getStringView(struct_name), StringTable::getStringView(underlying_name));
				return lookup_inherited_template(underlying_name, template_name, depth + 1);
			}
		}
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has no struct_info_ and couldn't resolve alias", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	// Search base classes recursively
	const StructTypeInfo* struct_info = struct_type_info->struct_info_.get();
	FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has {} base classes", StringTable::getStringView(struct_name), struct_info->base_classes.size());
	for (const auto& base_class : struct_info->base_classes) {
		// Skip deferred base classes (they haven't been resolved yet)
		if (base_class.is_deferred) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping deferred base class '{}'", base_class.name);
			continue;
		}
		
		FLASH_LOG_FORMAT(Templates, Debug, "Checking base class '{}'", base_class.name);
		// Recursively look up in base class - convert base_class.name to StringHandle for performance
		StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class.name);
		const std::vector<ASTNode>* base_result = lookup_inherited_template(base_name_handle, template_name, depth + 1);
		if (base_result != nullptr && !base_result->empty()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Found inherited template function '{}::{}' via base class '{}'",
			                 StringTable::getStringView(struct_name), template_name, base_class.name);
			return base_result;
		}
	}
	
	return nullptr;
}

// Helper: Validate and add a base class (consolidates lookup, validation, and registration)
ParseResult Parser::validate_and_add_base_class(
	std::string_view base_class_name,
	StructDeclarationNode& struct_ref,
	StructTypeInfo* struct_info,
	AccessSpecifier base_access,
	bool is_virtual_base,
	const Token& error_token)
{
	// Look up base class type
	auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base_class_name));
	
	// If not found directly, try with current namespace prefix
	// This handles cases like: struct Derived : public inner::Base { }
	// where inner::Base is actually ns::inner::Base and we're inside ns
	if (base_type_it == gTypesByName.end()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
		if (!qualified_namespace.empty()) {
			// Try the full namespace qualification first (e.g., ns::outer::inner::Base).
			StringBuilder qualified_name;
			qualified_name.append(qualified_namespace).append("::").append(base_class_name);
			std::string_view qualified_name_view = qualified_name.commit();
			base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name_view));
			if (base_type_it != gTypesByName.end()) {
				FLASH_LOG(Parser, Debug, "Found base class '", base_class_name, 
				          "' as '", qualified_name_view, "' in current namespace context");
			}
			
			// Try suffixes like inner::Base, deep::Base for sibling namespace access.
			for (size_t pos = qualified_namespace.find("::");
			     pos != std::string_view::npos && base_type_it == gTypesByName.end();
			     pos = qualified_namespace.find("::", pos + 2)) {
				std::string_view suffix = qualified_namespace.substr(pos + 2);
				StringBuilder suffix_builder;
				suffix_builder.append(suffix).append("::").append(base_class_name);
				qualified_name_view = suffix_builder.commit();
				base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name_view));
				
				if (base_type_it != gTypesByName.end()) {
					FLASH_LOG(Parser, Debug, "Found base class '", base_class_name, 
					          "' as '", qualified_name_view, "' in current namespace context");
				}
			}
		}
	}
	
	if (base_type_it == gTypesByName.end()) {
		return ParseResult::error("Base class '" + std::string(base_class_name) + "' not found", error_token);
	}

	const TypeInfo* base_type_info = base_type_it->second;
	
	FLASH_LOG_FORMAT(Parser, Debug, "process_base_class: initial base_type_info for '{}': type={}, type_index={}", 
	                 base_class_name, static_cast<int>(base_type_info->type_), base_type_info->type_index_);
	
	// Resolve type aliases: if base_type_info points to another type (type alias),
	// follow the chain to find the actual struct type
	size_t max_alias_depth = 10;  // Prevent infinite loops
	while (base_type_info->type_ != Type::Struct && base_type_info->type_index_ < gTypeInfo.size() && max_alias_depth-- > 0) {
		const TypeInfo& underlying = gTypeInfo[base_type_info->type_index_];
		// Stop if we're pointing to ourselves (not a valid alias)
		if (&underlying == base_type_info) break;
		FLASH_LOG_FORMAT(Parser, Debug, "Resolving type alias '{}' -> type_index {}, underlying type={}", 
		                 base_class_name, base_type_info->type_index_, static_cast<int>(underlying.type_));
		base_type_info = &underlying;
	}
	
	FLASH_LOG_FORMAT(Parser, Debug, "process_base_class: final base_type_info: type={}, type_index={}", 
	                 static_cast<int>(base_type_info->type_), base_type_info->type_index_);
	
	// Check if base class is a template parameter
	bool is_template_param = is_base_class_template_parameter(base_class_name);
	
	// Check if base class is a dependent template placeholder (e.g., integral_constant$hash)
	auto [is_dependent_placeholder, template_base] = isDependentTemplatePlaceholder(base_class_name);
	
	// In template bodies, a UserDefined type alias (e.g., _Tp_alloc_type) may resolve to a struct
	// at instantiation time. Treat it as a deferred base class.
	bool is_dependent_type_alias = false;
	if (!is_template_param && !is_dependent_placeholder && base_type_info->type_ == Type::UserDefined &&
		(parsing_template_body_ || !struct_parsing_context_stack_.empty())) {
		is_dependent_type_alias = true;
	}
	
	// Allow Type::Struct for concrete types OR template parameters OR dependent placeholders OR dependent type aliases
	if (!is_template_param && !is_dependent_placeholder && !is_dependent_type_alias && base_type_info->type_ != Type::Struct) {
		return ParseResult::error("Base class '" + std::string(base_class_name) + "' is not a struct/class", error_token);
	}

	// For template parameters, dependent placeholders, or dependent type aliases, skip 'final' check
	if (!is_template_param && !is_dependent_placeholder && !is_dependent_type_alias) {
		// Check if base class is final
		if (base_type_info->struct_info_ && base_type_info->struct_info_->is_final) {
			return ParseResult::error("Cannot inherit from final class '" + std::string(base_class_name) + "'", error_token);
		}
	}

	// Add base class to struct node and type info
	bool is_deferred = is_template_param || is_dependent_type_alias;
	struct_ref.add_base_class(base_class_name, base_type_info->type_index_, base_access, is_virtual_base, is_deferred);
	struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base_access, is_virtual_base, is_deferred);
	
	return ParseResult::success();
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
		// First try to get the type name from the token (useful for type aliases parsed inside templates
		// where the type_index might be 0/placeholder because the alias wasn't fully registered yet)
		std::string_view type_name;
		if (original_type.token().type() != Token::Type::Uninitialized && !original_type.token().value().empty()) {
			type_name = original_type.token().value();
		}
		
		// If we have a valid type_index, prefer the name from gTypeInfo
		if (result_type_index < gTypeInfo.size() && result_type_index > 0) {
			const TypeInfo& type_info = gTypeInfo[result_type_index];
			type_name = StringTable::getStringView(type_info.name());
			
			FLASH_LOG(Templates, Debug, "substitute_template_parameter: type_index=", result_type_index, 
				", type_name='", type_name, "', underlying_type=", static_cast<int>(type_info.type_), 
				", underlying_type_index=", type_info.type_index_);
		} else if (!type_name.empty()) {
			FLASH_LOG(Templates, Debug, "substitute_template_parameter: using token name '", type_name, "' (type_index=", result_type_index, " is placeholder)");
		}

		// Try to find which template parameter this is
		bool found_match = false;
		if (!type_name.empty()) {
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
						
						found_match = true;
						break;
					}
				}
			}

			// Try to resolve dependent qualified member types (e.g., Helper_T::type)
			if (!found_match && type_name.find("::") != std::string_view::npos) {
				auto sep_pos = type_name.find("::");
				std::string base_part(type_name.substr(0, sep_pos));
				std::string_view member_part = type_name.substr(sep_pos + 2);
				auto build_resolved_handle = [](std::string_view base, std::string_view member) {
					StringBuilder sb;
					return StringTable::getOrInternStringHandle(sb.append(base).append("::").append(member).commit());
				};
				
				bool replaced = false;
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>()) continue;
					const auto& tparam = template_params[i].as<TemplateParameterNode>();
					std::string_view tname = tparam.name();
					auto pos = base_part.find(tname);
					if (pos != std::string::npos) {
						base_part.replace(pos, tname.size(), template_args[i].toString());
						replaced = true;
					}
				}
				
				if (replaced) {
					StringHandle resolved_handle = build_resolved_handle(base_part, member_part);
					auto type_it = gTypesByName.find(resolved_handle);
					FLASH_LOG(Templates, Debug, "Dependent member type lookup for '",
					          StringTable::getStringView(resolved_handle), "' found=", (type_it != gTypesByName.end()));
					
					// If not found, try instantiating the base template
					// The base_part contains a mangled name like "enable_if_void_int"
					// We need to find the actual template name, which could be "enable_if" not just "enable"
					if (type_it == gTypesByName.end()) {
						std::string_view base_template_name = extract_base_template_name(base_part);
					
						// Only try to instantiate if we found a class template (not a function template)
						if (!base_template_name.empty()) {
							auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
							if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
								try_instantiate_class_template(base_template_name, template_args);
								
								std::string_view instantiated_base = get_instantiated_class_name(base_template_name, template_args);
								resolved_handle = build_resolved_handle(instantiated_base, member_part);
								type_it = gTypesByName.find(resolved_handle);
								FLASH_LOG(Templates, Debug, "After instantiating base template '", base_template_name, "', lookup for '",
								          StringTable::getStringView(resolved_handle), "' found=", (type_it != gTypesByName.end()));
							}
						}
					}
					
					if (type_it != gTypesByName.end()) {
						const TypeInfo* resolved_info = type_it->second;
						result_type = resolved_info->type_;
						result_type_index = resolved_info->type_index_;
						found_match = true;
					}
				}
			}

			// Handle hash-based dependent qualified types like "Wrapper$hash::Nested"
			// These come from parsing "typename Wrapper<T>::Nested" during template definition.
			// The hash represents a dependent instantiation (Wrapper<T> with T not yet resolved).
			// We need to extract the template name ("Wrapper"), re-instantiate with concrete args,
			// and look up the nested type in the new instantiation.
			if (!found_match && type_name.find("::") != std::string_view::npos) {
				auto sep_pos = type_name.find("::");
				std::string_view base_part_sv = type_name.substr(0, sep_pos);
				std::string_view member_part = type_name.substr(sep_pos + 2);
				// Hash-based mangled template name in base part
				// (e.g., "Wrapper$a1b2c3d4" for dependent Wrapper<T>)
				std::string_view base_template_name = extractBaseTemplateName(base_part_sv);
				
				if (!base_template_name.empty()) {
					
					auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
					if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
						// Re-instantiate with concrete args
						try_instantiate_class_template(base_template_name, template_args);
						std::string_view instantiated_base = get_instantiated_class_name(base_template_name, template_args);
						
						StringBuilder sb;
						StringHandle resolved_handle = StringTable::getOrInternStringHandle(
							sb.append(instantiated_base).append("::").append(member_part).commit());
						auto type_it = gTypesByName.find(resolved_handle);
						
						FLASH_LOG(Templates, Debug, "Dependent hash-qualified type: '", type_name,
						          "' -> '", StringTable::getStringView(resolved_handle),
						          "' found=", (type_it != gTypesByName.end()));
						
						if (type_it != gTypesByName.end()) {
							const TypeInfo* resolved_info = type_it->second;
							result_type = resolved_info->type_;
							result_type_index = resolved_info->type_index_;
							found_match = true;
						}
					}
				}
			}

			// Handle dependent placeholder types like "TC_T" - template instantiations that
			// contain template parameters in their mangled name. Extract the template base
			// name and instantiate with the substituted arguments.
			if (!found_match && type_name.find('_') != std::string_view::npos) {
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>()) continue;
					const auto& tparam = template_params[i].as<TemplateParameterNode>();
					std::string_view param_name = tparam.name();

					// Check if the type name ends with "_<param>" pattern (like "TC_T" for param "T")
					size_t pos = type_name.rfind(param_name);
					if (pos != std::string_view::npos && pos > 0 && type_name[pos - 1] == '_' &&
					    pos + param_name.size() == type_name.size()) {
						// Extract the template base name by finding the template in registry
						std::string_view base_sv = type_name.substr(0, pos - 1);
						auto template_opt = gTemplateRegistry.lookupTemplate(base_sv);
						if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
							// Found the template! Instantiate it with the concrete arguments
							FLASH_LOG(Templates, Debug, "substitute_template_parameter: '", type_name,
							          "' is a dependent placeholder for template '", base_sv, "' - instantiating with concrete args");

							try_instantiate_class_template(base_sv, template_args);
							std::string_view instantiated_name = get_instantiated_class_name(base_sv, template_args);

							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
							if (type_it != gTypesByName.end()) {
								const TypeInfo* resolved_info = type_it->second;
								result_type = resolved_info->type_;
								result_type_index = resolved_info->type_index_;
								found_match = true;
								FLASH_LOG(Templates, Debug, "  Resolved to '", instantiated_name, "' (type_index=", result_type_index, ")");
							}
							break;
						}
					}
				}
			}

			// If not found as a direct template parameter, check if this is a type alias
			// that resolves to a template parameter (e.g., "using value_type = T;")
			// This requires a valid type_index to look up the alias info
			if (!found_match && result_type_index > 0 && result_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[result_type_index];
				if (type_info.type_ == Type::UserDefined && type_info.type_index_ != result_type_index) {
					// This is a type alias - recursively check what it resolves to
					if (type_info.type_index_ < gTypeInfo.size()) {
						const TypeInfo& alias_target_info = gTypeInfo[type_info.type_index_];
						std::string_view alias_target_name = StringTable::getStringView(alias_target_info.name());
						
						// Check if the alias target is a template parameter
						for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
							if (template_params[i].is<TemplateParameterNode>()) {
								const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
								if (tparam.name() == alias_target_name) {
									// The type alias resolves to a template parameter - substitute!
									const TemplateTypeArg& arg = template_args[i];
									result_type = arg.base_type;
									result_type_index = arg.type_index;
									FLASH_LOG(Templates, Debug, "Substituted type alias '", type_name, 
										"' (which refers to template param '", alias_target_name, "') with type=", static_cast<int>(result_type));
									found_match = true;
									break;
								}
							}
						}
					}
				}
			}

			// Handle dependent template-template parameter instantiation placeholders.
			// When TT<int> or TTT<Inner> is used in a template body, a dependent placeholder
			// with isTemplateInstantiation() is created (baseTemplateName()=TT, templateArgs()=[int]).
			// Here we substitute: find a Template param whose name matches baseTemplateName(),
			// then instantiate the corresponding concrete template with the preserved args.
			if (!found_match && result_type_index < gTypeInfo.size() && result_type_index > 0) {
				const TypeInfo& placeholder_info = gTypeInfo[result_type_index];
				if (placeholder_info.isTemplateInstantiation()) {
					std::string_view base_tpl_name = StringTable::getStringView(placeholder_info.baseTemplateName());
					for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
						if (!template_params[i].is<TemplateParameterNode>()) continue;
						const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
						if (tparam.kind() == TemplateParameterKind::Template && tparam.name() == base_tpl_name) {
							const TemplateTypeArg& concrete_arg = template_args[i];
							if (concrete_arg.type_index < gTypeInfo.size()) {
								std::string_view concrete_tpl_name = StringTable::getStringView(gTypeInfo[concrete_arg.type_index].name());
								// Convert the preserved args from the placeholder to TemplateTypeArg
								std::vector<TemplateTypeArg> concrete_args;
								for (const auto& arg_info : placeholder_info.templateArgs()) {
									TemplateTypeArg ta;
									ta.base_type = arg_info.base_type;
									ta.type_index = arg_info.type_index;
									ta.is_value = arg_info.is_value;
									ta.value = arg_info.intValue();
									concrete_args.push_back(ta);
								}
								// Instantiate the concrete template with the preserved args
								try_instantiate_class_template(concrete_tpl_name, concrete_args);
								std::string_view inst_name = get_instantiated_class_name(concrete_tpl_name, concrete_args);
								auto inst_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
								if (inst_it != gTypesByName.end()) {
									result_type = inst_it->second->type_;
									result_type_index = inst_it->second->type_index_;
									found_match = true;
									FLASH_LOG_FORMAT(Templates, Debug, "Resolved template-template placeholder '{}' → '{}' via concrete template '{}'",
									                 base_tpl_name, inst_name, concrete_tpl_name);
								}
							}
							break;
						}
					}
				}
			}
		}
	}

	return {result_type, result_type_index};
}

// Lookup symbol with template parameter checking
std::optional<ASTNode> Parser::lookup_symbol_with_template_check(StringHandle identifier) {
    // First check if it's a template parameter using the new method
    if (parsing_template_body_ && !current_template_param_names_.empty()) {
        return gSymbolTable.lookup(identifier, gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
    }

    // Otherwise, do normal symbol lookup
    return gSymbolTable.lookup(identifier);
}

// Helper to extract type from an expression for overload resolution
std::optional<TypeSpecifierNode> Parser::get_expression_type(const ASTNode& expr_node) {
	// Guard against infinite recursion by tracking the call stack
	// Use the address of the expr_node as a unique identifier
	const void* expr_ptr = &expr_node;
	
	// Check if we're already resolving this expression's type
	if (expression_type_resolution_stack_.count(expr_ptr) > 0) {
		FLASH_LOG(Parser, Debug, "get_expression_type: Circular dependency detected, returning nullopt");
		return std::nullopt;  // Prevent infinite recursion
	}
	
	// Add to stack and use RAII to ensure removal
	expression_type_resolution_stack_.insert(expr_ptr);
	auto guard = ScopeGuard([this, expr_ptr]() {
		expression_type_resolution_stack_.erase(expr_ptr);
	});
	
	// Handle lambda expressions directly (not wrapped in ExpressionNode)
	if (expr_node.is<LambdaExpressionNode>()) {
		const auto& lambda = expr_node.as<LambdaExpressionNode>();
		auto closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			// Get closure size in bits from struct info
			int closure_size_bits = 64; // Default to pointer size
			if (closure_type->getStructInfo()) {
				closure_size_bits = closure_type->getStructInfo()->total_size * 8;
			}
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, closure_size_bits, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 64, lambda.lambda_token());
	}

	if (!expr_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();

	// Handle different expression types
	if (std::holds_alternative<BoolLiteralNode>(expr)) {
		return TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8);
	}
	else if (std::holds_alternative<NumericLiteralNode>(expr)) {
		const auto& literal = std::get<NumericLiteralNode>(expr);
		return TypeSpecifierNode(literal.type(), literal.qualifier(), literal.sizeInBits());
	}
	else if (std::holds_alternative<StringLiteralNode>(expr)) {
		// String literals have type "const char*" (pointer to const char)
		TypeSpecifierNode char_type(Type::Char, TypeQualifier::None, 8, {}, CVQualifier::Const);
		char_type.add_pointer_level();
		return char_type;
	}
	else if (std::holds_alternative<IdentifierNode>(expr)) {
		const auto& ident = std::get<IdentifierNode>(expr);
		auto symbol = this->lookup_symbol(ident.nameHandle());
		if (symbol.has_value()) {
			if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
				TypeSpecifierNode type = decl->type_node().as<TypeSpecifierNode>();

				// Handle array-to-pointer decay
				// When an array is used in an expression (except with sizeof, &, etc.),
				// it decays to a pointer to its first element
				// Use is_array() which handles both sized arrays (int arr[5]) and
				// unsized arrays (int arr[] = {...}) where is_unsized_array_ is true
				if (decl->is_array()) {
					// This is an array declaration - decay to pointer
					// Create a new TypeSpecifierNode with one level of pointer
					TypeSpecifierNode pointer_type = type;
					pointer_type.add_pointer_level();
					return pointer_type;
				}

				return type;
			}
			// Handle function identifiers: __typeof(func) / decltype(func) should
			// return the function's return type. GCC's __typeof on a function name
			// yields the function type, but for practical purposes (libstdc++ usage
			// like 'extern "C" __typeof(uselocale) __uselocale;'), returning the
			// return type allows parsing to continue past these declarations.
			if (symbol->is<FunctionDeclarationNode>()) {
				const auto& func = symbol->as<FunctionDeclarationNode>();
				const TypeSpecifierNode& ret_type = func.decl_node().type_node().as<TypeSpecifierNode>();
				return ret_type;
			}
		}
	}
	else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& binary = std::get<BinaryOperatorNode>(expr);
		TokenKind op_kind = binary.get_token().kind();

		// Comparison and logical operators always return bool
		if (op_kind == tok::Equal || op_kind == tok::NotEqual ||
		    op_kind == tok::Less || op_kind == tok::Greater ||
		    op_kind == tok::LessEq || op_kind == tok::GreaterEq ||
		    op_kind == tok::LogicalAnd || op_kind == tok::LogicalOr) {
			return TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8);
		}

		// For bitwise/arithmetic operators, check the LHS type
		// If LHS is an enum, check for free function operator overloads
		auto lhs_type_opt = get_expression_type(binary.get_lhs());
		if (lhs_type_opt.has_value() && lhs_type_opt->type() == Type::Enum) {
			// Look for a free function operator overload (e.g., operator&(EnumA, EnumB) -> EnumA)
			StringBuilder op_name_builder;
			op_name_builder.append("operator"sv);
			op_name_builder.append(binary.op());
			auto op_name = op_name_builder.commit();
			auto overloads = gSymbolTable.lookup_all(op_name);
			for (const auto& overload : overloads) {
				if (overload.is<FunctionDeclarationNode>()) {
					const auto& func = overload.as<FunctionDeclarationNode>();
					const ASTNode& type_node = func.decl_node().type_node();
					if (type_node.is<TypeSpecifierNode>()) {
						return type_node.as<TypeSpecifierNode>();
					}
				}
			}
		}

		// For same-type operands, return the LHS type
		if (lhs_type_opt.has_value()) {
			auto rhs_type_opt = get_expression_type(binary.get_rhs());
			if (rhs_type_opt.has_value() && lhs_type_opt->type() == rhs_type_opt->type()) {
				return *lhs_type_opt;
			}
		}

		// Default: return int for arithmetic/bitwise operations
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
				result.set_reference_qualifier(ReferenceQualifier::LValueReference);
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
		TypeSpecifierNode return_type = decl.type_node().as<TypeSpecifierNode>();
		
		FLASH_LOG(Parser, Debug, "get_expression_type for function '", decl.identifier_token().value(), "': return_type=", (int)return_type.type(), ", is_ref=", return_type.is_reference(), ", is_rvalue_ref=", return_type.is_rvalue_reference());
		
		// If the return type is still auto, the function should have been deduced already
		// during parsing. The TypeSpecifierNode in the declaration should have been updated.
		// If it's still auto, it means deduction failed or wasn't performed.
		return return_type;
	}
	else if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
		// For member function calls (including lambda operator() calls), get the return type
		const auto& member_call = std::get<MemberFunctionCallNode>(expr);
		const auto& decl = member_call.function_declaration();
		TypeSpecifierNode return_type = decl.decl_node().type_node().as<TypeSpecifierNode>();
		
		// Try to get the actual function declaration from the struct info
		// The placeholder function declaration may have wrong return type
		const ASTNode& object_node = member_call.object();
		if (object_node.is<ExpressionNode>()) {
			auto object_type_opt = get_expression_type(object_node);
			if (object_type_opt.has_value() && object_type_opt->type() == Type::Struct) {
				size_t struct_type_index = object_type_opt->type_index();
				if (struct_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[struct_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Look up the member function
						std::string_view func_name = decl.decl_node().identifier_token().value();
						for (const auto& member_func : struct_info->member_functions) {
							if (member_func.getName() == StringTable::getOrInternStringHandle(func_name) && 
								member_func.function_decl.is<FunctionDeclarationNode>()) {
								// Found the real function - use its return type
								const FunctionDeclarationNode& real_func = 
									member_func.function_decl.as<FunctionDeclarationNode>();
								return_type = real_func.decl_node().type_node().as<TypeSpecifierNode>();
								break;
							}
						}
					}
				}
			}
		}
		
		FLASH_LOG(Parser, Debug, "get_expression_type for member function call: ", 
				  decl.decl_node().identifier_token().value(), 
				  " return_type=", (int)return_type.type(), " size=", (int)return_type.size_in_bits());
		
		// If the return type is still auto, it should have been deduced during parsing
		return return_type;
	}
	else if (std::holds_alternative<LambdaExpressionNode>(expr)) {
		// For lambda expressions, return the closure struct type
		const auto& lambda = std::get<LambdaExpressionNode>(expr);
		auto closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			// Get closure size in bits from struct info
			int closure_size_bits = 64; // Default to pointer size
			if (closure_type->getStructInfo()) {
				closure_size_bits = closure_type->getStructInfo()->total_size * 8;
			}
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, closure_size_bits, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 64, lambda.lambda_token());
	}
	else if (std::holds_alternative<ConstructorCallNode>(expr)) {
		// For constructor calls like Widget(42), return the type being constructed
		const auto& ctor_call = std::get<ConstructorCallNode>(expr);
		const ASTNode& type_node = ctor_call.type_node();
		if (type_node.is<TypeSpecifierNode>()) {
			return type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<StaticCastNode>(expr)) {
		// For cast expressions like (Type)expr or static_cast<Type>(expr), return the target type
		const auto& cast = std::get<StaticCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<DynamicCastNode>(expr)) {
		// For dynamic_cast<Type>(expr), return the target type
		const auto& cast = std::get<DynamicCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<ConstCastNode>(expr)) {
		// For const_cast<Type>(expr), return the target type
		const auto& cast = std::get<ConstCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<ReinterpretCastNode>(expr)) {
		// For reinterpret_cast<Type>(expr), return the target type
		const auto& cast = std::get<ReinterpretCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<MemberAccessNode>(expr)) {
		// For member access expressions like obj.member or (*ptr).member
		const auto& member_access = std::get<MemberAccessNode>(expr);
		const ASTNode& object_node = member_access.object();
		std::string_view member_name = member_access.member_name();
		
		// Get the type of the object
		auto object_type_opt = get_expression_type(object_node);
		if (!object_type_opt.has_value()) {
			return std::nullopt;
		}
		
		const TypeSpecifierNode& object_type = *object_type_opt;
		
		// Handle struct/class member access
		if (object_type.type() == Type::Struct || object_type.type() == Type::UserDefined) {
			size_t struct_type_index = object_type.type_index();
			if (struct_type_index < gTypeInfo.size()) {
				// Look up the member
				auto member_result = FlashCpp::gLazyMemberResolver.resolve(static_cast<TypeIndex>(struct_type_index), StringTable::getOrInternStringHandle(std::string(member_name)));
				if (member_result) {
					// Return the member's type
					// member->size is in bytes, TypeSpecifierNode expects bits
					TypeSpecifierNode member_type(member_result.member->type, TypeQualifier::None, member_result.member->size * 8);
					member_type.set_type_index(member_result.member->type_index);
					return member_type;
				}
			}
		}
	}
	else if (std::holds_alternative<PointerToMemberAccessNode>(expr)) {
		// For pointer-to-member access expressions like obj.*ptr_to_member or obj->*ptr_to_member
		// The type depends on the pointer-to-member type, which is complex to determine
		// For now, return nullopt as this is primarily used in decltype contexts where
		// the actual type isn't needed during parsing
		return std::nullopt;
	}
	else if (std::holds_alternative<PseudoDestructorCallNode>(expr)) {
		// Pseudo-destructor call (obj.~Type()) always returns void
		const auto& dtor_call = std::get<PseudoDestructorCallNode>(expr);
		return TypeSpecifierNode(Type::Void, TypeQualifier::None, 0, dtor_call.type_name_token());
	}
	else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		// For ternary expressions (cond ? true_expr : false_expr), determine the common type
		// This is important for decltype(true ? expr1 : expr2) patterns used in <type_traits>
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		
		// Get types of both branches
		auto true_type_opt = get_expression_type(ternary.true_expr());
		auto false_type_opt = get_expression_type(ternary.false_expr());
		
		// If both types are available, determine the common type
		if (true_type_opt.has_value() && false_type_opt.has_value()) {
			const TypeSpecifierNode& true_type = *true_type_opt;
			const TypeSpecifierNode& false_type = *false_type_opt;
			
			// If both types are the same, return that type
			if (true_type.type() == false_type.type() && 
				true_type.type_index() == false_type.type_index() &&
				true_type.pointer_levels().size() == false_type.pointer_levels().size()) {
				// Return the common type (prefer the true branch for reference/const qualifiers)
				return true_type;
			}
			
			// Handle common type conversions for arithmetic types
			if (true_type.type() != Type::Struct && true_type.type() != Type::UserDefined &&
				false_type.type() != Type::Struct && false_type.type() != Type::UserDefined) {
				// For arithmetic types, use usual arithmetic conversions
				// Return the larger type (in terms of bit width)
				if (true_type.size_in_bits() >= false_type.size_in_bits()) {
					return true_type;
				} else {
					return false_type;
				}
			}
			
			// For mixed struct types, we can't easily determine the common type
			// In template context, this might be a dependent type
			// Return the true branch type as fallback
			return true_type;
		}
		
		// If only one type is available, return that
		if (true_type_opt.has_value()) {
			return true_type_opt;
		}
		if (false_type_opt.has_value()) {
			return false_type_opt;
		}
		
		// Both types unavailable - return nullopt
		return std::nullopt;
	}
	else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
		// For qualified identifiers like MakeUnsigned::List<int, char>::size
		// We need to look up the type of the static member
		const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
		NamespaceHandle ns_handle = qual_id.namespace_handle();
		std::string_view member_name = qual_id.name();
		
		if (!ns_handle.isGlobal()) {
			// Get the struct name (the namespace handle's name is the last component)
			std::string_view struct_name = gNamespaceRegistry.getName(ns_handle);
			
			// Try to find the struct in gTypesByName
			auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			
			// If not found directly, try building full qualified name
			if (struct_type_it == gTypesByName.end() && gNamespaceRegistry.getDepth(ns_handle) > 1) {
				std::string_view full_qualified_name = gNamespaceRegistry.getQualifiedName(ns_handle);
				struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(full_qualified_name));
			}
			
			if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					// Trigger lazy static member instantiation if needed
					StringHandle member_name_handle = StringTable::getOrInternStringHandle(std::string(member_name));
					instantiateLazyStaticMember(struct_info->name, member_name_handle);
					
					// Look for static member
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
					if (static_member && owner_struct) {
						// Found the static member - return its type
						TypeSpecifierNode member_type(static_member->type, TypeQualifier::None, static_member->size * 8);
						member_type.set_type_index(static_member->type_index);
						if (static_member->is_const()) {
							member_type.set_cv_qualifier(CVQualifier::Const);
						}
						if (static_member->pointer_depth > 0) {
							member_type.add_pointer_levels(static_member->pointer_depth);
						}
						if (static_member->reference_qualifier != ReferenceQualifier::None) {
							member_type.set_reference_qualifier(static_member->reference_qualifier);
						}
						return member_type;
					}
				}
			}
		}
	}
	// Add more cases as needed

	return std::nullopt;
}

// Helper function to deduce the type of an expression for auto type deduction
Type Parser::deduce_type_from_expression(const ASTNode& expr) {
	// For now, use a simple approach: use the existing get_expression_type function
	// which returns TypeSpecifierNode, and extract the type from it
	auto type_spec_opt = get_expression_type(expr);
	if (type_spec_opt.has_value()) {
		return type_spec_opt->type();
	}

	// Default to int if we can't determine the type
	return Type::Int;
}

// Helper function to deduce and update auto return type from function body
void Parser::deduce_and_update_auto_return_type(FunctionDeclarationNode& func_decl) {
	// Check if the return type is auto
	DeclarationNode& decl_node = func_decl.decl_node();
	const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
	
	FLASH_LOG(Parser, Debug, "deduce_and_update_auto_return_type called for function: ", 
			  decl_node.identifier_token().value(), " return_type=", (int)return_type.type());
	
	if (return_type.type() != Type::Auto) {
		return;  // Not an auto return type, nothing to do
	}
	
	// Prevent infinite recursion: check if we're already deducing this function's type
	if (functions_being_deduced_.count(&func_decl) > 0) {
		FLASH_LOG(Parser, Debug, "  Already deducing this function, skipping to prevent recursion");
		return;
	}
	
	// Add this function to the set of functions being deduced
	functions_being_deduced_.insert(&func_decl);
	
	// RAII guard to remove the function from the set when we exit
	auto guard = ScopeGuard([this, &func_decl]() {
		functions_being_deduced_.erase(&func_decl);
	});
	
	// Get the function body
	const std::optional<ASTNode>& body_opt = func_decl.get_definition();
	if (!body_opt.has_value() || !body_opt->is<BlockNode>()) {
		FLASH_LOG(Parser, Debug, "  No body or invalid body");
		return;  // No body or invalid body
	}
	
	// Walk through the function body to find return statements
	const BlockNode& body = body_opt->as<BlockNode>();
	std::optional<TypeSpecifierNode> deduced_type;
	std::vector<std::pair<TypeSpecifierNode, Token>> all_return_types;  // Track all return types for validation
	
	// Recursive lambda to search for return statements
	std::function<void(const ASTNode&)> find_return_statements = [&](const ASTNode& node) {
		if (node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
			if (ret.expression().has_value()) {
				auto expr_type_opt = get_expression_type(*ret.expression());
				if (expr_type_opt.has_value()) {
					// Store this return type for validation
					all_return_types.emplace_back(*expr_type_opt, decl_node.identifier_token());
					
					// Set deduced type from first return
					if (!deduced_type.has_value()) {
						deduced_type = *expr_type_opt;
						FLASH_LOG(Parser, Debug, "  Found return statement, deduced type: ", 
								  (int)deduced_type->type(), " size: ", (int)deduced_type->size_in_bits());
					}
				}
			}
		} else if (node.is<BlockNode>()) {
			// Recursively search nested blocks
			const BlockNode& block = node.as<BlockNode>();
			block.get_statements().visit([&](const ASTNode& stmt) {
				find_return_statements(stmt);
			});
		} else if (node.is<IfStatementNode>()) {
			const IfStatementNode& if_stmt = node.as<IfStatementNode>();
			if (if_stmt.get_then_statement().has_value()) {
				find_return_statements(if_stmt.get_then_statement());
			}
			if (if_stmt.get_else_statement().has_value()) {
				find_return_statements(*if_stmt.get_else_statement());
			}
		} else if (node.is<ForStatementNode>()) {
			const ForStatementNode& for_stmt = node.as<ForStatementNode>();
			if (for_stmt.get_body_statement().has_value()) {
				find_return_statements(for_stmt.get_body_statement());
			}
		} else if (node.is<WhileStatementNode>()) {
			const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
			if (while_stmt.get_body_statement().has_value()) {
				find_return_statements(while_stmt.get_body_statement());
			}
		} else if (node.is<DoWhileStatementNode>()) {
			const DoWhileStatementNode& do_while = node.as<DoWhileStatementNode>();
			if (do_while.get_body_statement().has_value()) {
				find_return_statements(do_while.get_body_statement());
			}
		} else if (node.is<SwitchStatementNode>()) {
			const SwitchStatementNode& switch_stmt = node.as<SwitchStatementNode>();
			if (switch_stmt.get_body().has_value()) {
				find_return_statements(switch_stmt.get_body());
			}
		}
		// Add more statement types as needed
	};
	
	// Search the function body
	body.get_statements().visit([&](const ASTNode& stmt) {
		find_return_statements(stmt);
	});
	
	// Validate that all return statements have compatible types
	if (all_return_types.size() > 1) {
		const TypeSpecifierNode& first_type = all_return_types[0].first;
		for (size_t i = 1; i < all_return_types.size(); ++i) {
			const TypeSpecifierNode& current_type = all_return_types[i].first;
			if (!are_types_compatible(first_type, current_type)) {
				// Log error but don't fail compilation (just log warning)
				// We could make this a hard error, but for now just warn
				FLASH_LOG(Parser, Warning, "Function '", decl_node.identifier_token().value(),
						  "' has inconsistent return types: first return has type '",
						  type_to_string(first_type), "', but another return has type '",
						  type_to_string(current_type), "'");
			}
		}
	}
	
	// If we found a deduced type, update the function declaration's return type
	if (deduced_type.has_value()) {
		// Create a new ASTNode with the deduced type and update the declaration
		// Note: new_type_ref is a reference to the newly created node, not the moved-from deduced_type
		auto [new_type_node, new_type_ref] = create_node_ref<TypeSpecifierNode>(std::move(*deduced_type));
		decl_node.set_type_node(new_type_node);
		
		FLASH_LOG(Parser, Debug, "  Updated return type to: ", (int)new_type_ref.type(), 
				  " size: ", (int)new_type_ref.size_in_bits());
		
		// Log deduction for debugging
		FLASH_LOG(Parser, Debug, "Deduced auto return type for function '", decl_node.identifier_token().value(), 
				  "': type=", (int)new_type_ref.type(), " size=", (int)new_type_ref.size_in_bits());
	}
}

// Helper function to count pack elements in template parameter packs
// Counts by looking up pack_name_0, pack_name_1, etc. in the symbol table
size_t Parser::count_pack_elements(std::string_view pack_name) const {
	size_t num_pack_elements = 0;
	StringBuilder param_name_builder;
	
	while (true) {
		// Build the parameter name: pack_name + "_" + index
		param_name_builder.append(pack_name);
		param_name_builder.append('_');
		param_name_builder.append(num_pack_elements);
		std::string_view param_name = param_name_builder.preview();
		
		// Check if this parameter exists in the symbol table
		auto lookup_result = gSymbolTable.lookup(param_name);
		param_name_builder.reset();  // Reset for next iteration
		
		if (!lookup_result.has_value()) {
			break;  // No more pack elements
		}
		num_pack_elements++;
		
		// Safety limit to prevent infinite loops
		if (num_pack_elements > MAX_PACK_ELEMENTS) {
			FLASH_LOG(Templates, Error, "Pack '", pack_name, "' expansion exceeded MAX_PACK_ELEMENTS (", MAX_PACK_ELEMENTS, ")");
			break;
		}
	}
	
	return num_pack_elements;
}

// Parse extern "C" { ... } block
ParseResult Parser::parse_extern_block(Linkage linkage) {
	// Expect '{'
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' after extern linkage specification", current_token_);
	}

	// Save the current linkage and set the new one
	Linkage saved_linkage = current_linkage_;
	current_linkage_ = linkage;

	// Save the current AST size to know which nodes were added by this block
	size_t ast_size_before = ast_nodes_.size();

	// Parse declarations until '}' by calling parse_top_level_node() repeatedly
	// This ensures extern "C" blocks support exactly the same constructs as file scope
	while (!peek().is_eof() && peek() != "}"_tok) {
		
		ParseResult result = parse_top_level_node();
		
		if (result.is_error()) {
			current_linkage_ = saved_linkage;  // Restore linkage before returning error
			return result;
		}
		
		// parse_top_level_node() already adds nodes to ast_nodes_, so we don't need to do it here
	}

	// Restore the previous linkage
	current_linkage_ = saved_linkage;

	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' after extern block", current_token_);
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
// Helper function to check if two types are compatible (same type, ignoring qualifiers)
bool Parser::are_types_compatible(const TypeSpecifierNode& type1, const TypeSpecifierNode& type2) const {
	// Check basic type
	if (type1.type() != type2.type()) {
		return false;
	}
	
	// For user-defined types (Struct, Enum), check type index
	if (type1.type() == Type::Struct || type1.type() == Type::Enum) {
		if (type1.type_index() != type2.type_index()) {
			return false;
		}
	}
	
	// Check pointer levels
	if (type1.pointer_levels().size() != type2.pointer_levels().size()) {
		return false;
	}
	
	// Check if reference
	if (type1.is_reference() != type2.is_reference()) {
		return false;
	}
	
	// Types are compatible (we ignore const/volatile qualifiers for this check)
	return true;
}

// Helper function to convert a type to a string for error messages
std::string Parser::type_to_string(const TypeSpecifierNode& type) const {
	std::string result;
	
	// Add const/volatile qualifiers
	if (static_cast<uint8_t>(type.cv_qualifier()) & static_cast<uint8_t>(CVQualifier::Const)) {
		result += "const ";
	}
	if (static_cast<uint8_t>(type.cv_qualifier()) & static_cast<uint8_t>(CVQualifier::Volatile)) {
		result += "volatile ";
	}
	
	// Add base type name
	switch (type.type()) {
		case Type::Void: result += "void"; break;
		case Type::Bool: result += "bool"; break;
		case Type::Char: result += "char"; break;
		case Type::UnsignedChar: result += "unsigned char"; break;
		case Type::Short: result += "short"; break;
		case Type::UnsignedShort: result += "unsigned short"; break;
		case Type::Int: result += "int"; break;
		case Type::UnsignedInt: result += "unsigned int"; break;
		case Type::Long: result += "long"; break;
		case Type::UnsignedLong: result += "unsigned long"; break;
		case Type::LongLong: result += "long long"; break;
		case Type::UnsignedLongLong: result += "unsigned long long"; break;
		case Type::Float: result += "float"; break;
		case Type::Double: result += "double"; break;
		case Type::LongDouble: result += "long double"; break;
		case Type::Auto: result += "auto"; break;
		case Type::Struct:
			if (type.type_index() < gTypeInfo.size()) {
				result += std::string(StringTable::getStringView(gTypeInfo[type.type_index()].name()));
			} else {
				result += "struct";
			}
			break;
		case Type::Enum:
			if (type.type_index() < gTypeInfo.size()) {
				result += std::string(StringTable::getStringView(gTypeInfo[type.type_index()].name()));
			} else {
				result += "enum";
			}
			break;
		case Type::Function: result += "function"; break;
		case Type::FunctionPointer: result += "function pointer"; break;
		case Type::MemberFunctionPointer: result += "member function pointer"; break;
		case Type::MemberObjectPointer: result += "member object pointer"; break;
		case Type::Nullptr: result += "nullptr_t"; break;
		default: result += "unknown"; break;
	}
	
	// Add pointer levels
	for (size_t i = 0; i < type.pointer_levels().size(); ++i) {
		result += "*";
		const PointerLevel& ptr_level = type.pointer_levels()[i];
		CVQualifier cv = ptr_level.cv_qualifier;
		if (static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Const)) {
			result += " const";
		}
		if (static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Volatile)) {
			result += " volatile";
		}
	}
	
	// Add reference
	if (type.is_reference()) {
		result += type.is_rvalue_reference() ? "&&" : "&";
	}
	
	return result;
}

// Note: Type size lookup is now unified in ::get_type_size_bits() from AstNodeTypes.h
// This ensures consistent handling of target-dependent types like 'long' (LLP64 vs LP64)
