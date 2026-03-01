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
	// Also skips Microsoft __declspec attributes
	// e.g., class __declspec(dllimport) _Lockit { }
	skip_cpp_attributes();
	parse_declspec_attributes();

	// Parse struct name (optional for anonymous structs)
	auto name_token = advance();
	if (!name_token.kind().is_identifier()) {
		return ParseResult::error("Expected struct/class name", name_token);
	}

	auto struct_name = name_token.handle();

	// Handle out-of-line nested class definitions and template specializations.
	// Patterns: class Outer::Inner { ... }
	//           class Wrapper<T>::Nested { ... }  (template out-of-line nested class)
	//           struct MyStruct<int> { ... }       (template specialization)
	// Loop handles interleaved <Args> and ::Name components in any order.
	for (;;) {
		if (peek() == "<"_tok) {
			// Skip template specialization arguments: <T>, <int, float>, <pair<int,int>>, etc.
			// Uses skip_template_arguments() which properly handles >> tokens for nested templates
			skip_template_arguments();
		} else if (peek() == "::"_tok) {
			// Scope resolution — consume :: and the following identifier as the actual struct name
			advance(); // consume '::'
			if (peek().is_identifier()) {
				name_token = advance();
				struct_name = name_token.handle();
			} else {
				break;
			}
		} else {
			break;
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

	// Check for 'final' keyword before base class list (C++ standard: class-key identifier final(opt) base-clause(opt))
	if (peek() == "final"_tok) {
		advance();  // consume 'final'
		struct_ref.set_is_final(true);
		struct_info->is_final = true;
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
								type_node.set_reference_qualifier(targ.ref_qualifier);
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
			
			// Consume optional ::member type access and ... pack expansion
			auto post_info = consume_base_class_qualifiers_after_template_args();
			if (post_info.member_type_name.has_value()) {
				FLASH_LOG_FORMAT(Templates, Debug, "Found member type access after template args: {}::{}", base_class_name, StringTable::getStringView(*post_info.member_type_name));
			}
			
			// Check if any template arguments are dependent
			// This includes both explicit dependent flags AND types whose names contain template parameters
			bool has_dependent_args = post_info.is_pack_expansion;
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
				
				auto arg_infos = build_template_arg_infos(template_args, template_arg_nodes);
				
				StringHandle template_name_handle = StringTable::getOrInternStringHandle(base_class_name);
				struct_ref.add_deferred_template_base_class(template_name_handle, std::move(arg_infos), post_info.member_type_name, base_access, is_virtual_base, post_info.is_pack_expansion);
				
				continue;  // Skip to next base class or exit loop
			}
			
			// Instantiate base class template if needed and register in AST
			// Note: try_instantiate_class_template returns nullopt on success 
			// (type is registered in gTypesByName)
			instantiated_base_name = instantiate_and_register_base_template(base_class_name, template_args);
			
			// Resolve member type alias if present (e.g., Base<T>::type)
			if (post_info.member_type_name.has_value()) {
				std::string_view member_name = StringTable::getStringView(*post_info.member_type_name);
				
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
						return ParseResult::error("Base class '" + std::string(alias_name) + "' not found", *post_info.member_name_token);
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
				
				if (post_info.member_name_token.has_value()) {
					base_name_token = *post_info.member_name_token;
				}
			}
		}

		// Handle pack expansion '...' for variadic template parameters (e.g., struct C : Bases...)
		if (peek() == "..."_tok) {
			advance(); // consume '...'
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
								ReferenceQualifier::None,
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
						ReferenceQualifier ref_qual = anon_member_type_spec.reference_qualifier();
						if (ref_qual != ReferenceQualifier::None) {
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
							ref_qual,
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
								StructTypeInfo* nested_info = nested_type_it->second->getStructInfo();
								if (nested_info) {
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
			CVQualifier cv_qual = CVQualifier::None;
			bool is_static_constexpr = !!(member_specs & FlashCpp::MLS_Constexpr);
			while (peek().is_keyword()) {
				std::string_view kw = peek_info().value();
				if (kw == "const") {
					cv_qual |= CVQualifier::Const;
					advance();
				} else if (kw == "constexpr") {
					is_static_constexpr = true;
					cv_qual |= CVQualifier::Const; // constexpr implies const
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
			// Calculate size and alignment for the static member (handles pointers/references correctly)
			auto [static_member_size, static_member_alignment] = calculateMemberSizeAndAlignment(type_spec);
			ReferenceQualifier ref_qual = type_spec.reference_qualifier();
			int ptr_depth = static_cast<int>(type_spec.pointer_depth());

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
				cv_qual,
				ref_qual,
				ptr_depth
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
							// Initializer list, body, declaration end, or = default/delete
							if (tok_val == ":" || tok_val == "{" || tok_val == ";" || tok_val == "=") {
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
						// Skip initializer name (may be namespace-qualified: std::optional<_Tp>{...})
						advance();
						
						// Handle namespace-qualified base class names: ns::Class or std::optional
						skip_qualified_name_parts();
						
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
			bool is_const_member = member_quals.is_const();
			bool is_volatile_member = member_quals.is_volatile();
			bool is_override = func_specs.is_override;
			bool is_final = func_specs.is_final;
			bool is_pure_virtual = func_specs.is_pure_virtual();
			bool is_defaulted = func_specs.is_defaulted();
			bool is_deleted = func_specs.is_deleted();

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
					union_member.reference_qualifier,
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
		ReferenceQualifier ref_qual = type_spec.reference_qualifier();
		// Reference size and alignment were already set above
		if (ref_qual != ReferenceQualifier::None) {
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
			ref_qual,
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
					param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(base_param_type.reference_qualifier());
					
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
		param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference

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
		return_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference

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
		param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference

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
		param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // true = rvalue reference

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
		return_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference

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
		move_param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // true = rvalue reference

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
			param_type_node.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
			
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
				MemberFunctionCallNode(this_node, *spaceship_func, std::move(spaceship_args), operator_name_token));
			
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
	struct_info->has_deferred_base_classes = !struct_ref.deferred_template_base_classes().empty();
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
		// Member function templates (e.g., template<typename U> ClassName(U arg) {...})
		// inside non-template classes must NOT have their bodies parsed now.
		// Per C++ §13.9.2 (two-phase lookup), a member function template is only
		// instantiated when referenced in a context that requires a definition.
		// Save the body position on the declaration node for later instantiation.
		//
		// For template classes, member function template bodies ARE parsed here because
		// they form part of the class template definition and will be re-parsed during
		// each class template instantiation.
		if (delayed.is_member_function_template && !parsing_template_class_) {
			if (delayed.is_constructor && delayed.ctor_node) {
				delayed.ctor_node->set_template_body_position(delayed.body_start);
			} else if (delayed.func_node) {
				delayed.func_node->set_template_body_position(delayed.body_start);
			}
			continue;  // body deferred to instantiation time
		}

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
		ReferenceQualifier::None,
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
					ReferenceQualifier::None,
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
			ReferenceQualifier::None,
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
		std::string_view qualified_friend_name = consume_qualified_name_suffix(class_name_token.value());

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
	// Handle namespace-qualified names: std::_Rb_tree_merge_helper
	std::string_view qualified_name = consume_qualified_name_suffix(advance().value());

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after template friend class declaration", peek_info());
	}

	// Create friend declaration node with TemplateClass kind, storing the full qualified name
	auto friend_node = emplace_node<FriendDeclarationNode>(FriendKind::TemplateClass, StringTable::getOrInternStringHandle(qualified_name));
	struct_node.add_friend(friend_node);

	return saved_position.success(friend_node);
}

