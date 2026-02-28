	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		if (!node.get_definition().has_value() && !node.is_implicit()) {
			return;
		}

		struct NamespaceStackGuard {
			std::vector<std::string>& target;
			std::vector<std::string> saved;
			explicit NamespaceStackGuard(std::vector<std::string>& stack)
				: target(stack), saved(stack) {}
			~NamespaceStackGuard() { target = std::move(saved); }
		} namespace_guard{ current_namespace_stack_ };

		// Deferred or synthesized function generation can lose namespace stack context.
		// Recover it from the declaration registry so unqualified lookup remains standard-compliant.
		if (current_namespace_stack_.empty() && global_symbol_table_) {
			if (auto ns_handle = global_symbol_table_->find_namespace_of_function(node); ns_handle.has_value() && !ns_handle->isGlobal()) {
				std::vector<NamespaceHandle> namespace_path;
				NamespaceHandle current = *ns_handle;
				while (current.isValid() && !current.isGlobal()) {
					namespace_path.push_back(current);
					current = gNamespaceRegistry.getParent(current);
				}
				for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
					current_namespace_stack_.emplace_back(gNamespaceRegistry.getName(*it));
				}
			}
		}

		// Reset the temporary variable counter for each new function
		// For non-static member functions, reserve TempVar(1) for the implicit 'this' parameter
		// Static member functions have no 'this' pointer
		var_counter = (node.is_member_function() && !node.is_static()) ? TempVar(2) : TempVar();

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		const DeclarationNode& func_decl = node.decl_node();
		const std::string_view func_name_view = func_decl.identifier_token().value();
		current_function_name_ = StringTable::getOrInternStringHandle(func_name_view);
		
		// Set current function return type and size for type checking in return statements
		const TypeSpecifierNode& ret_type_spec = func_decl.type_node().as<TypeSpecifierNode>();
		current_function_return_type_ = ret_type_spec.type();
		current_function_returns_reference_ = ret_type_spec.is_reference();
		
		// Get actual return size - for struct types, TypeSpecifierNode.size_in_bits() may be 0
		// so we need to look it up from gTypeInfo using the type_index
		int actual_ret_size = static_cast<int>(ret_type_spec.size_in_bits());
		if (actual_ret_size == 0 && ret_type_spec.type() == Type::Struct && ret_type_spec.type_index() > 0) {
			// Look up struct size from type info
			if (ret_type_spec.type_index() < gTypeInfo.size() && gTypeInfo[ret_type_spec.type_index()].struct_info_) {
				actual_ret_size = static_cast<int>(gTypeInfo[ret_type_spec.type_index()].struct_info_->total_size * 8);
			}
		}
		
		// For pointer return types or reference return types, use 64-bit size (pointer size on x64)
		// References are represented as pointers at the IR level
		current_function_return_size_ = (ret_type_spec.pointer_depth() > 0 || ret_type_spec.is_reference()) 
			? 64 
			: actual_ret_size;

		// Set or clear current_struct_name_ based on whether this is a member function
		// This is critical for member variable lookup in generateIdentifierIr
		if (node.is_member_function()) {
			// For member functions, set current_struct_name_ from parent_struct_name
			// Use the parent_struct_name directly (simple name like "Test") rather than
			// looking up the TypeInfo's name (which may be namespace-qualified like "ns::Test").
			// The namespace will be added during mangling from current_namespace_stack_.
			std::string_view parent_name = node.parent_struct_name();
			// If parent_struct_name is a template pattern but we have a valid struct context
			// from visitStructDeclarationNode, keep the struct context (instantiated name)
			if (!parent_name.empty() && !gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(parent_name))) {
				current_struct_name_ = StringTable::getOrInternStringHandle(parent_name);
			}
			// else: keep current_struct_name_ from visitStructDeclarationNode context
		} else if (!current_struct_name_.isValid()) {
			// Clear current_struct_name_ only if we don't already have a struct context
			// (e.g., from visitStructDeclarationNode visiting this function as a member).
			// Template instantiation may not set is_member_function_ on pattern-derived functions.
			current_struct_name_ = StringHandle();
		}

		if (FLASH_LOG_ENABLED(Codegen, Debug)) {
			const TypeSpecifierNode& debug_ret_type = func_decl.type_node().as<TypeSpecifierNode>();
			FLASH_LOG(Codegen, Debug, "===== CODEGEN visitFunctionDeclarationNode: ", func_decl.identifier_token().value(), " =====");
			FLASH_LOG(Codegen, Debug, "  return_type: ", (int)debug_ret_type.type(), " size: ", (int)debug_ret_type.size_in_bits(), " ptr_depth: ", debug_ret_type.pointer_depth(), " is_ref: ", debug_ret_type.is_reference(), " is_rvalue_ref: ", debug_ret_type.is_rvalue_reference());
			FLASH_LOG(Codegen, Debug, "  is_member_function: ", node.is_member_function());
			if (node.is_member_function()) {
				FLASH_LOG(Codegen, Debug, "  parent_struct_name: ", node.parent_struct_name());
			}
			FLASH_LOG(Codegen, Debug, "  parameter_count: ", node.parameter_nodes().size());
			for (size_t i = 0; i < node.parameter_nodes().size(); ++i) {
				const auto& param = node.parameter_nodes()[i];
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					FLASH_LOG(Codegen, Debug, "  param[", i, "]: name='", param_decl.identifier_token().value()
							  , "' type=", (int)param_type.type() 
							  , " type_index=", param_type.type_index()
							  , " size=", (int)param_type.size_in_bits()
							  , " ptr_depth=", param_type.pointer_depth()
							  , " base_cv=", (int)param_type.cv_qualifier()
							  , " is_ref=", param_type.is_reference()
							  , " is_rvalue_ref=", param_type.is_rvalue_reference());
					for (size_t j = 0; j < param_type.pointer_levels().size(); ++j) {
						FLASH_LOG(Codegen, Debug, " ptr[", j, "]_cv=", (int)param_type.pointer_levels()[j].cv_qualifier);
					}
				}
			}
			FLASH_LOG(Codegen, Debug, "=====");
		}

		// Clear static local names map for new function
		static_local_names_.clear();

		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		// Use FunctionDeclOp to store typed payload
		FunctionDeclOp func_decl_op;
		
		// Return type information
		func_decl_op.return_type = ret_type.type();
		
		// Get actual return size - for struct types, TypeSpecifierNode.size_in_bits() may be 0
		// so we need to look it up from gTypeInfo using the type_index
		int actual_return_size = static_cast<int>(ret_type.size_in_bits());
		if (actual_return_size == 0 && ret_type.type() == Type::Struct && ret_type.type_index() > 0) {
			// Look up struct size from type info
			if (ret_type.type_index() < gTypeInfo.size() && gTypeInfo[ret_type.type_index()].struct_info_) {
				actual_return_size = static_cast<int>(gTypeInfo[ret_type.type_index()].struct_info_->total_size * 8);
			}
		}
		
		// For pointer return types, use 64-bit size (pointer size on x64)
		// For reference return types, keep the base type size (the reference itself is 64-bit at ABI level,
		// but we display it as the base type with a reference qualifier)
		func_decl_op.return_size_in_bits = (ret_type.pointer_depth() > 0) 
			? 64 
			: actual_return_size;
		func_decl_op.return_pointer_depth = ret_type.pointer_depth();
		func_decl_op.return_type_index = ret_type.type_index();
		func_decl_op.returns_reference = ret_type.is_reference();
		func_decl_op.returns_rvalue_reference = ret_type.is_rvalue_reference();
		
		// Detect if function returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this (pointer/reference returns are in RAX like regular pointers)
		bool returns_struct_by_value = returnsStructByValue(ret_type.type(), ret_type.pointer_depth(), ret_type.is_reference());
		bool needs_hidden_return_param = needsHiddenReturnParam(ret_type.type(), ret_type.pointer_depth(), ret_type.is_reference(), actual_return_size, context_->isLLP64());
		func_decl_op.has_hidden_return_param = needs_hidden_return_param;
		
		// Track return type index and hidden parameter flag for current function context
		current_function_return_type_index_ = ret_type.type_index();
		current_function_has_hidden_return_param_ = needs_hidden_return_param;
		
		if (returns_struct_by_value) {
			if (needs_hidden_return_param) {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function {} returns struct by value (size={} bits) - will use hidden return parameter (RVO/NRVO)",
					func_decl.identifier_token().value(), ret_type.size_in_bits());
			} else {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function {} returns small struct by value (size={} bits) - will return in RAX",
					func_decl.identifier_token().value(), ret_type.size_in_bits());
			}
		}
		
		// Function name
		func_decl_op.function_name = func_decl.identifier_token().handle();

		// Add struct/class name for member functions
		// Use current_struct_name_ if set (for instantiated template specializations),
		// otherwise use the function node's parent_struct_name
		// For nested classes, we need to use the fully qualified name from TypeInfo
		std::string_view struct_name_for_function;
		if (current_struct_name_.isValid()) {
			struct_name_for_function = StringTable::getStringView(current_struct_name_);
		} else if (node.is_member_function()) {
			struct_name_for_function = node.parent_struct_name();
		} else {
			struct_name_for_function = ""sv;
		}
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_function);
		
		// Linkage and variadic flag
		func_decl_op.linkage = node.linkage();
		func_decl_op.is_variadic = node.is_variadic();
		func_decl_op.is_static_member = node.is_static();
		
		// Member functions defined inside the class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file to allow duplicate definitions
		// This includes constructors, destructors, and regular member functions defined inline
		// Also mark functions in std namespace as inline to handle standard library functions that
		// are defined in headers (like std::abs) and may be instantiated multiple times
		bool is_in_std_namespace = false;
		if (!current_namespace_stack_.empty()) {
			is_in_std_namespace = (current_namespace_stack_[0] == "std");
		}
		func_decl_op.is_inline = node.is_member_function() || is_in_std_namespace;

		// Use pre-computed mangled name from AST node if available (Phase 6 migration)
		// Fall back to generating it here if not (for backward compatibility during migration)
		std::string_view mangled_name;
		
		// Don't pass namespace_stack when struct_name already includes the namespace
		// (e.g., "std::simple" already has the namespace embedded, so we shouldn't also pass ["std"])
		// This avoids double-encoding the namespace in the mangled name
		std::vector<std::string> namespace_for_mangling;
		if (struct_name_for_function.find("::") == std::string_view::npos) {
			// struct_name doesn't contain namespace, use current_namespace_stack_
			namespace_for_mangling = current_namespace_stack_;
		}
		// else: struct_name already contains namespace prefix, don't add it again
		
		if (node.has_mangled_name()) {
			mangled_name = node.mangled_name();
		} else if (node.has_non_type_template_args()) {
			// Generate mangled name with template arguments for template specializations (e.g., get<0>)
			const TypeSpecifierNode& return_type = func_decl.type_node().as<TypeSpecifierNode>();
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param : node.parameter_nodes()) {
				param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
			}
			auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
				func_decl.identifier_token().value(), return_type, param_types, 
				node.non_type_template_args(), node.is_variadic(), 
				struct_name_for_function, namespace_for_mangling);
			mangled_name = mangled.view();
		} else {
			// Generate mangled name using the FunctionDeclarationNode overload
			mangled_name = generateMangledNameForCall(node, struct_name_for_function, namespace_for_mangling);
		}
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled_name);

		// Skip duplicate function definitions to prevent multiple codegen of the same function
		// This is especially important for inline functions from standard headers (like std::abs)
		// that may be parsed multiple times
		if (generated_function_names_.count(func_decl_op.mangled_name) > 0) {
			FLASH_LOG(Codegen, Debug, "Skipping duplicate function definition: ", func_decl.identifier_token().value(), " (", mangled_name, ")");
			return;
		}
		generated_function_names_.insert(func_decl_op.mangled_name);

		// Add parameters to function declaration
		std::vector<CachedParamInfo> cached_params;
		cached_params.reserve(node.parameter_nodes().size());
		size_t unnamed_param_counter = 0;  // Counter for generating unique names for unnamed parameters
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam param_info;
			param_info.type = param_type.type();
			param_info.size_in_bits = static_cast<int>(param_type.size_in_bits());
			// For struct types, TypeSpecifierNode.size_in_bits() may be 0 (e.g. template-instantiated
			// struct parameters) — resolve from gTypeInfo using the type_index, same as return types.
			if (param_info.size_in_bits == 0 && param_type.type() == Type::Struct && param_type.type_index() > 0) {
				if (param_type.type_index() < gTypeInfo.size() && gTypeInfo[param_type.type_index()].struct_info_) {
					param_info.size_in_bits = static_cast<int>(gTypeInfo[param_type.type_index()].struct_info_->total_size * 8);
				}
			}
			
			// Lvalue references (&) are treated like pointers in the IR (address at the ABI level)
			int pointer_depth = static_cast<int>(param_type.pointer_depth());
			if (param_type.is_lvalue_reference()) {
				pointer_depth += 1;  // Add 1 for lvalue reference (ABI treats it as an additional pointer level)
			}
			// Note: Rvalue references (T&&) are tracked separately via is_rvalue_reference flag.
			// While lvalue references are always implemented as pointers at the ABI level,
			// rvalue references in the context of perfect forwarding can receive values directly
			// when bound to temporaries/literals. The pointer_depth increment is omitted to allow
			// this direct value passing, while the is_rvalue_reference flag enables proper handling
			// in both the caller (materialization + address-taking) and callee (dereferencing).
			param_info.pointer_depth = pointer_depth;
			
			// Handle unnamed parameters (e.g., `operator=(const T&) = default;` without explicit param name)
			// Generate a unique name like "__param_0", "__param_1", etc. for unnamed parameters
			std::string_view param_name = param_decl.identifier_token().value();
			if (param_name.empty()) {
				// For defaulted operators (operator=, operator<=>, and synthesized comparison operators),
				// use "other" as the conventional name for the first parameter.
				std::string_view func_name_for_param = func_decl.identifier_token().value();
				bool is_defaulted_operator = unnamed_param_counter == 0 && (
					func_name_for_param == "operator=" ||
					func_name_for_param == "operator<=>" ||
					func_name_for_param == "operator==" ||
					func_name_for_param == "operator!=" ||
					func_name_for_param == "operator<" ||
					func_name_for_param == "operator>" ||
					func_name_for_param == "operator<=" ||
					func_name_for_param == "operator>=");
				if (is_defaulted_operator) {
					param_info.name = StringTable::getOrInternStringHandle("other");
				} else {
					// Generate unique name for unnamed parameter
					param_info.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(unnamed_param_counter).commit());
				}
				unnamed_param_counter++;
			} else {
				param_info.name = StringTable::getOrInternStringHandle(param_name);
			}
			
			param_info.is_reference = param_type.is_reference();  // Tracks ANY reference (lvalue or rvalue)
			param_info.is_rvalue_reference = param_type.is_rvalue_reference();  // Specific rvalue ref flag
			param_info.cv_qualifier = param_type.cv_qualifier();

			func_decl_op.parameters.push_back(std::move(param_info));
			var_counter.next();

			CachedParamInfo cache_entry;
			cache_entry.is_reference = param_type.is_reference();
			cache_entry.is_rvalue_reference = param_type.is_rvalue_reference();
			cache_entry.is_parameter_pack = param_decl.is_parameter_pack();
			cached_params.push_back(cache_entry);
		}

		// Store cached parameter info keyed by mangled function name
		StringHandle cache_key = func_decl_op.mangled_name.isValid()
			? func_decl_op.mangled_name
			: func_decl.identifier_token().handle();
		function_param_cache_[cache_key] = std::move(cached_params);

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), func_decl.identifier_token()));

		// Generate memberwise three-way comparison for defaulted operator<=>
		if (func_name_view == "operator<=>" && node.is_implicit()) {
			// Set up function scope and 'this' pointer
			symbol_table.enter_scope(ScopeType::Function);
			if (node.is_member_function()) {
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
					if (struct_info) {
						Token this_token = func_decl.identifier_token();
						auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
							Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
						this_type.as<TypeSpecifierNode>().add_pointer_level();
						auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);
						symbol_table.insert("this"sv, this_decl);
					}
				}
			}
			for (const auto& param : node.parameter_nodes()) {
				symbol_table.insert(param.as<DeclarationNode>().identifier_token().value(), param);
			}

			// Look up struct info
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = type_it->second;
				const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
				if (struct_info && !struct_info->members.empty()) {
					StringHandle this_handle = StringTable::getOrInternStringHandle("this");
					StringHandle other_handle;
					if (!node.parameter_nodes().empty()) {
						std::string_view param_name = node.parameter_nodes()[0].as<DeclarationNode>().identifier_token().value();
						if (!param_name.empty()) {
							other_handle = StringTable::getOrInternStringHandle(param_name);
						}
					}
					if (!other_handle.isValid()) {
						other_handle = StringTable::getOrInternStringHandle("other");
					}

					static size_t spaceship_counter = 0;
					size_t current_spaceship = spaceship_counter++;

					for (size_t mi = 0; mi < struct_info->members.size(); ++mi) {
						const auto& member = struct_info->members[mi];
						int member_bits = static_cast<int>(member.size * 8);

						// Labels for this member's comparison
						auto diff_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_diff_").append(current_spaceship).append("_").append(mi));
						auto lt_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_lt_").append(current_spaceship).append("_").append(mi));
						auto gt_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_gt_").append(current_spaceship).append("_").append(mi));
						auto next_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_next_").append(current_spaceship).append("_").append(mi));

						// For struct members, delegate to the member's operator<=>
						if (member.type == Type::Struct && member.type_index > 0 && member.type_index < gTypeInfo.size()) {
							const TypeInfo& member_type_info = gTypeInfo[member.type_index];
							const StructTypeInfo* member_struct_info = member_type_info.getStructInfo();

							// Find operator<=> in the member struct and generate its mangled name
							StringHandle member_spaceship_mangled;
							if (member_struct_info) {
								for (const auto& mf : member_struct_info->member_functions) {
									if (mf.is_operator_overload && mf.operator_symbol == "<=>") {
										if (mf.function_decl.is<FunctionDeclarationNode>()) {
											const auto& spaceship_func = mf.function_decl.as<FunctionDeclarationNode>();
											// Use generateMangledNameForCall for consistent mangling across platforms
											std::string_view member_struct_name = StringTable::getStringView(member_type_info.name());
											member_spaceship_mangled = StringTable::getOrInternStringHandle(
												generateMangledNameForCall(spaceship_func, member_struct_name));
										}
										break;
									}
								}
							}

							if (member_spaceship_mangled.isValid()) {
								// Load addresses of this->member and other.member for the call
								TempVar lhs_val = var_counter.next();
								MemberLoadOp lhs_load;
								lhs_load.result.value = lhs_val;
								lhs_load.result.type = member.type;
								lhs_load.result.size_in_bits = member_bits;
								lhs_load.object = this_handle;
								lhs_load.member_name = member.getName();
								lhs_load.offset = static_cast<int>(member.offset);
								lhs_load.is_reference = member.is_reference();
								lhs_load.is_rvalue_reference = member.is_rvalue_reference();
								lhs_load.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(lhs_load), func_decl.identifier_token()));

								TempVar rhs_val = var_counter.next();
								MemberLoadOp rhs_load;
								rhs_load.result.value = rhs_val;
								rhs_load.result.type = member.type;
								rhs_load.result.size_in_bits = member_bits;
								rhs_load.object = other_handle;
								rhs_load.member_name = member.getName();
								rhs_load.offset = static_cast<int>(member.offset);
								rhs_load.is_reference = member.is_reference();
								rhs_load.is_rvalue_reference = member.is_rvalue_reference();
								rhs_load.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(rhs_load), func_decl.identifier_token()));

								// Call member's operator<=>(this->member, other.member)
								TempVar call_result = var_counter.next();
								CallOp call_op;
								call_op.function_name = member_spaceship_mangled;
								call_op.is_member_function = true;
								call_op.return_type = Type::Int;
								call_op.return_size_in_bits = 32;
								call_op.result = call_result;

								TypedValue lhs_arg;
								lhs_arg.type = Type::Struct;
								lhs_arg.size_in_bits = 64;
								lhs_arg.value = lhs_val;
								lhs_arg.pointer_depth = 1;
								call_op.args.push_back(std::move(lhs_arg));

								TypedValue rhs_arg;
								rhs_arg.type = Type::Struct;
								rhs_arg.size_in_bits = 64;
								rhs_arg.value = rhs_val;
								rhs_arg.ref_qualifier = ReferenceQualifier::LValueReference;
								call_op.args.push_back(std::move(rhs_arg));

								ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), func_decl.identifier_token()));

								// Check if result != 0 (members not equal)
								TempVar ne_result = var_counter.next();
								BinaryOp ne_op{
									.lhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{call_result}, .is_signed = true},
									.rhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{0ULL}, .is_signed = true},
									.result = IrValue{ne_result}
								};
								ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(ne_op), func_decl.identifier_token()));

								// Branch: if not equal, return the result directly
								CondBranchOp ne_branch;
								ne_branch.label_true = diff_label;
								ne_branch.label_false = next_label;
								ne_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{ne_result}};
								ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(ne_branch), func_decl.identifier_token()));

								// Label: diff - return the inner <=> result
								ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = diff_label}, func_decl.identifier_token()));
								{
									emitReturn(IrValue{call_result}, Type::Int, 32, func_decl.identifier_token());
								}

								// Label: next - continue to next member
								ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = next_label}, func_decl.identifier_token()));
								continue;
							}
							// Fall through to primitive comparison if no operator<=> found
						}

						// Primitive member comparison
						TempVar lhs_val = var_counter.next();
						MemberLoadOp lhs_load;
						lhs_load.result.value = lhs_val;
						lhs_load.result.type = member.type;
						lhs_load.result.size_in_bits = member_bits;
						lhs_load.object = this_handle;
						lhs_load.member_name = member.getName();
						lhs_load.offset = static_cast<int>(member.offset);
						lhs_load.is_reference = member.is_reference();
						lhs_load.is_rvalue_reference = member.is_rvalue_reference();
						lhs_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(lhs_load), func_decl.identifier_token()));

						TempVar rhs_val = var_counter.next();
						MemberLoadOp rhs_load;
						rhs_load.result.value = rhs_val;
						rhs_load.result.type = member.type;
						rhs_load.result.size_in_bits = member_bits;
						rhs_load.object = other_handle;
						rhs_load.member_name = member.getName();
						rhs_load.offset = static_cast<int>(member.offset);
						rhs_load.is_reference = member.is_reference();
						rhs_load.is_rvalue_reference = member.is_rvalue_reference();
						rhs_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(rhs_load), func_decl.identifier_token()));

						// Compare: lhs != rhs
						TempVar ne_result = var_counter.next();
						BinaryOp ne_op{
							.lhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{lhs_val}, .is_signed = isSignedType(member.type)},
							.rhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{rhs_val}, .is_signed = isSignedType(member.type)},
							.result = IrValue{ne_result}
						};
						ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(ne_op), func_decl.identifier_token()));

						// Branch: if not equal, go to diff handling
						CondBranchOp ne_branch;
						ne_branch.label_true = diff_label;
						ne_branch.label_false = next_label;
						ne_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{ne_result}};
						ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(ne_branch), func_decl.identifier_token()));

						// Label: diff - members are not equal
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = diff_label}, func_decl.identifier_token()));

						// Compare: lhs < rhs
						TempVar lt_result = var_counter.next();
						BinaryOp lt_op{
							.lhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{lhs_val}, .is_signed = isSignedType(member.type)},
							.rhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{rhs_val}, .is_signed = isSignedType(member.type)},
							.result = IrValue{lt_result}
						};
						ir_.addInstruction(IrInstruction(IrOpcode::LessThan, std::move(lt_op), func_decl.identifier_token()));

						// Branch: if lhs < rhs, return -1, else return 1
						CondBranchOp lt_branch;
						lt_branch.label_true = lt_label;
						lt_branch.label_false = gt_label;
						lt_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{lt_result}};
						ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(lt_branch), func_decl.identifier_token()));

						// Label: lt - return -1 (two's complement: 0xFFFFFFFF in 32-bit)
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = lt_label}, func_decl.identifier_token()));
						{
							emitReturn(IrValue{0xFFFFFFFFULL}, Type::Int, 32, func_decl.identifier_token());
						}

						// Label: gt - return 1
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = gt_label}, func_decl.identifier_token()));
						{
							emitReturn(IrValue{1ULL}, Type::Int, 32, func_decl.identifier_token());
						}

						// Label: next - continue to next member
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = next_label}, func_decl.identifier_token()));
					}
				}
			}

			// All members equal - return 0
			emitReturn(IrValue{0ULL}, Type::Int, 32, func_decl.identifier_token());
			symbol_table.exit_scope();
			return;
		}

		// Synthesized comparison operators from operator<=> - generate memberwise comparison directly
		// Determine comparison opcode once from the operator name
		std::optional<IrOpcode> synthesized_cmp_opcode;
		if (node.is_implicit()) {
			if (func_name_view == "operator==") { synthesized_cmp_opcode = IrOpcode::Equal; }
			else if (func_name_view == "operator!=") { synthesized_cmp_opcode = IrOpcode::NotEqual; }
			else if (func_name_view == "operator<") { synthesized_cmp_opcode = IrOpcode::LessThan; }
			else if (func_name_view == "operator>") { synthesized_cmp_opcode = IrOpcode::GreaterThan; }
			else if (func_name_view == "operator<=") { synthesized_cmp_opcode = IrOpcode::LessEqual; }
			else if (func_name_view == "operator>=") { synthesized_cmp_opcode = IrOpcode::GreaterEqual; }
		}
		if (synthesized_cmp_opcode) {
			// Instead of processing the parser-generated body (which has auto return type issues),
			// generate direct memberwise comparison. This calls operator<=> and compares result with 0.
			symbol_table.enter_scope(ScopeType::Function);
			if (node.is_member_function()) {
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
					if (struct_info) {
						Token this_token = func_decl.identifier_token();
						auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
							Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
						this_type.as<TypeSpecifierNode>().add_pointer_level();
						auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);
						symbol_table.insert("this"sv, this_decl);
					}
				}
			}
			for (const auto& param : node.parameter_nodes()) {
				std::string_view pname = param.as<DeclarationNode>().identifier_token().value();
				if (!pname.empty()) {
					symbol_table.insert(pname, param);
				}
			}

			// Find the operator<=> to call it - generate mangled name from the function signature
			// (AST mangled name may not be set for user-defined operator<=>)
			StringHandle spaceship_mangled;
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					for (const auto& mf : struct_info->member_functions) {
						if (mf.is_operator_overload && mf.operator_symbol == "<=>") {
							if (mf.function_decl.is<FunctionDeclarationNode>()) {
								const auto& spaceship_func = mf.function_decl.as<FunctionDeclarationNode>();
								// Use generateMangledNameForCall for consistent mangling across platforms
								spaceship_mangled = StringTable::getOrInternStringHandle(
									generateMangledNameForCall(spaceship_func, node.parent_struct_name()));
							}
							break;
						}
					}
				}
			}

			if (spaceship_mangled.isValid()) {
				// Generate: call operator<=>(this, other) -> int result
				TempVar call_result = var_counter.next();
				CallOp call_op;
				call_op.function_name = spaceship_mangled;
				call_op.is_member_function = true;
				call_op.return_type = Type::Int;
				call_op.return_size_in_bits = 32;
				call_op.result = call_result;

				// Pass 'this' as first arg
				StringHandle this_handle = StringTable::getOrInternStringHandle("this");
				TypedValue this_arg;
				this_arg.type = Type::Struct;
				this_arg.size_in_bits = 64;
				this_arg.value = this_handle;
				this_arg.pointer_depth = 1;
				call_op.args.push_back(std::move(this_arg));

				// Pass 'other' as second arg (reference = pointer)
				StringHandle other_handle;
				if (!node.parameter_nodes().empty()) {
					std::string_view param_name = node.parameter_nodes()[0].as<DeclarationNode>().identifier_token().value();
					if (!param_name.empty()) {
						other_handle = StringTable::getOrInternStringHandle(param_name);
					}
				}
				if (!other_handle.isValid()) {
					other_handle = StringTable::getOrInternStringHandle("other");
				}
				TypedValue other_arg;
				other_arg.type = Type::Struct;
				other_arg.size_in_bits = 64;
				other_arg.value = other_handle;
				other_arg.ref_qualifier = ReferenceQualifier::LValueReference;
				call_op.args.push_back(std::move(other_arg));

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), func_decl.identifier_token()));

				// Compare result with 0 using the pre-determined comparison opcode
				TempVar cmp_result = var_counter.next();
				BinaryOp cmp_op{
					.lhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{call_result}, .is_signed = true},
					.rhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{0ULL}, .is_signed = true},
					.result = IrValue{cmp_result}
				};
				ir_.addInstruction(IrInstruction(*synthesized_cmp_opcode, std::move(cmp_op), func_decl.identifier_token()));

				// Return the boolean result
				emitReturn(IrValue{cmp_result}, Type::Bool, 8, func_decl.identifier_token());
			} else {
				// Fallback: operator<=> not found, return false for all synthesized operators
				emitReturn(IrValue{0ULL}, Type::Bool, 8, func_decl.identifier_token());
			}

			symbol_table.exit_scope();
			return;
		}

		symbol_table.enter_scope(ScopeType::Function);

		// For non-static member functions, add implicit 'this' pointer to symbol table
		// Static member functions have no 'this' pointer
		if (node.is_member_function() && !node.is_static()) {
			// Look up the struct type to get its type index and size
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = type_it->second;
				const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

				if (struct_info) {
					// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
					Token this_token = func_decl.identifier_token();  // Use function token for location
					auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
					// Mark 'this' as a pointer to struct (not a struct value)
					this_type.as<TypeSpecifierNode>().add_pointer_level();
					auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

					// Add 'this' to symbol table (it's the implicit first parameter)
					symbol_table.insert("this"sv, this_decl);
				}
			}
		}

		// Allocate stack space for local variables and parameters
		// Parameters are already in their registers, we just need to allocate space for them
		//size_t paramIndex = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			//const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			symbol_table.insert(param_decl.identifier_token().value(), param);
			//paramIndex++;
		}

		// Check if this is an implicit operator= that needs code generation
		if (node.is_implicit() && node.is_member_function()) {
			std::string_view func_name = func_decl.identifier_token().value();
			if (func_name == "operator=") {
				// This is an implicit copy or move assignment operator
				// Determine which one by checking the parameter type
// 				bool is_move_assignment = false;
// 				if (node.parameter_nodes().size() == 1) {
// 					const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
// 					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
// 					if (param_type.is_rvalue_reference()) {
// 						is_move_assignment = true;
// 					}
// 				}

				// Generate memberwise assignment from source parameter to 'this'
				// (same code for both copy and move assignment - memberwise copy/move)
				
				// Get the parameter name from the function declaration
				// For defaulted operator= without explicit parameter name (e.g., `operator=(const T&) = default;`),
				// the parameter name might be empty. Use "other" as the default name.
				// This name must match what's in func_decl_op.parameters.
				StringHandle source_param_name_handle;
				if (!node.parameter_nodes().empty()) {
					const auto& param_node = node.parameter_nodes()[0];
					if (param_node.is<DeclarationNode>()) {
						std::string_view param_name = param_node.as<DeclarationNode>().identifier_token().value();
						if (!param_name.empty()) {
							source_param_name_handle = StringTable::getOrInternStringHandle(param_name);
						}
					}
				}
				// Default to "other" if no parameter name found
				if (!source_param_name_handle.isValid()) {
					source_param_name_handle = StringTable::getOrInternStringHandle("other");
				}

				// Look up the struct type
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						// Generate memberwise assignment
						for (const auto& member : struct_info->members) {
							// First, load the member from source parameter
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = source_param_name_handle;  // Load from source parameter
							member_load.member_name = member.getName();
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference();
							member_load.is_rvalue_reference = member.is_rvalue_reference();
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), func_decl.identifier_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, is_ref, is_rvalue_ref, ref_size_bits, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference();
							member_store.is_rvalue_reference = member.is_rvalue_reference();
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), func_decl.identifier_token()));
						}

						// Return *this (the return value is the 'this' pointer dereferenced)
						// Generate: %temp = dereference [Type][Size] %this
						//           return [Type][Size] %temp
						TempVar this_deref = var_counter.next();
						std::vector<IrOperand> deref_operands;
						deref_operands.emplace_back(this_deref);  // result variable
						DereferenceOp deref_op;
						deref_op.result = this_deref;
						deref_op.pointer.type = Type::Struct;
						deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
						deref_op.pointer.value = StringTable::getOrInternStringHandle("this");

						ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), func_decl.identifier_token()));

						// Return the dereferenced value
						emitReturn(this_deref, Type::Struct, static_cast<int>(struct_info->total_size * 8), func_decl.identifier_token());
					}
				}
			}
		} else {
			// User-defined function body
			// Enter a scope for the function body to track destructors
			enterScope();
			const BlockNode& block = node.get_definition().value().as<BlockNode>();
			block.get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		}

		// Exit the function body scope and call destructors before returning
		// Only do this for user-defined function bodies where we called enterScope()
		if (!node.is_implicit() || !node.is_member_function()) {
			exitScope();
		}

		// Add implicit return if needed
		// Check if the last instruction is a return
		bool ends_with_return = false;
		if (!ir_.getInstructions().empty()) {
			const auto& last_instr = ir_.getInstructions().back();
			ends_with_return = (last_instr.getOpcode() == IrOpcode::Return);
		}

		if (!ends_with_return) {
			// Add implicit return for void functions
			if (ret_type.type() == Type::Void) {
				emitVoidReturn(func_decl.identifier_token());
			}
			// Special case: main() implicitly returns 0 if no return statement
			else if (func_decl.identifier_token().value() == "main") {
				emitReturn(0ULL, Type::Int, 32, func_decl.identifier_token());
			}
			// For other non-void functions, this is an error (missing return statement)
			// TODO: This should be a compile error, but for now we'll allow it
			// Full implementation requires control flow analysis to check all paths
		}

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
		// This allows nested contexts (like local struct member functions) to work properly
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system



		// Skip pattern structs - they're templates and shouldn't generate code
		if (gTemplateRegistry.isPatternStructName(node.name())) {
			return;
		}
		
		// Skip structs with incomplete instantiation - they have unresolved template params
		{
			auto incomplete_it = gTypesByName.find(node.name());
			if (incomplete_it != gTypesByName.end() && incomplete_it->second->is_incomplete_instantiation_) {
				FLASH_LOG(Codegen, Debug, "Skipping struct '", StringTable::getStringView(node.name()), "' (incomplete instantiation)");
				return;
			}
		}

		std::string_view struct_name = StringTable::getStringView(node.name());

		// Generate member functions for both global and local structs
		// Save the enclosing function context so member function visits don't clobber it
		StringHandle saved_enclosing_function = current_function_name_;
		StringHandle saved_struct_name = current_struct_name_;
		
		// Check if this is a local struct (declared inside a function)
		bool is_local_struct = current_function_name_.isValid();
		
		// Set struct context so member functions know which struct they belong to
		// NOTE: We don't clear this until the next struct - the string must persist
		// because IrOperands store string_view references to it
		// For nested classes, we need to use the fully qualified name from TypeInfo
		// If current_struct_name_ is valid, this is a nested class, so construct fully qualified name
		StringHandle lookup_name;
		if (current_struct_name_.isValid()) {
			// This is a nested class - construct fully qualified name like "Outer::Inner"
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(current_struct_name_))
			                     .append("::")
			                     .append(struct_name);
			lookup_name = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
		} else {
			// Top-level class - first try simple name, then look for namespace-qualified version
			lookup_name = StringTable::getOrInternStringHandle(struct_name);
		}
		
		auto type_it = gTypesByName.find(lookup_name);
		if (type_it != gTypesByName.end()) {
			current_struct_name_ = type_it->second->name();
		} else {
			// If simple name lookup failed, search for namespace-qualified version
			// e.g., for "simple", look for "std::simple" or other qualified names
			bool found_qualified = false;
			for (const auto& [name_handle, type_info] : gTypesByName) {
				std::string_view qualified_name = StringTable::getStringView(name_handle);
				// Check if this name ends with "::" + struct_name
				if (qualified_name.size() > struct_name.size() + 2) {
					size_t expected_pos = qualified_name.size() - struct_name.size();
					if (qualified_name.substr(expected_pos) == struct_name &&
					    qualified_name.substr(expected_pos - 2, 2) == "::") {
						current_struct_name_ = name_handle;
						found_qualified = true;
						break;
					}
				}
			}
			if (!found_qualified) {
				current_struct_name_ = lookup_name;
			}
		}
		
		// For local structs, collect member functions for deferred generation
		// For global structs, visit them immediately
		if (is_local_struct) {
			for (const auto& member_func : node.member_functions()) {
				LocalStructMemberInfo info;
				info.struct_name = current_struct_name_;
				info.enclosing_function_name = saved_enclosing_function;
				info.member_function_node = member_func.function_declaration;
				collected_local_struct_members_.push_back(std::move(info));
			}
		} else {
			FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - visiting members immediately, count=", node.member_functions().size());
			for (const auto& member_func : node.member_functions()) {
				// Each member function can be a FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
				FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - processing member function, is_constructor=", member_func.is_constructor);
				try {
					// Call the specific visitor directly instead of visit() to avoid clearing current_function_name_
					const ASTNode& func_decl = member_func.function_declaration;
					if (func_decl.is<FunctionDeclarationNode>()) {
						const auto& fn = func_decl.as<FunctionDeclarationNode>();
						// Skip functions with unresolved auto parameters (abbreviated templates)
						// These will be instantiated when called with concrete types
						bool fn_has_auto = false;
						for (const auto& p : fn.parameter_nodes()) {
							if (p.is<DeclarationNode>()) {
								const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								if (pt.type() == Type::Auto) {
									fn_has_auto = true;
									break;
								}
							}
						}
						if (!fn_has_auto) {
							visitFunctionDeclarationNode(fn);
							// If the function was skipped (lazy stub - no body yet), queue it for
							// deferred lazy instantiation so the body gets generated.
							if (!fn.get_definition().has_value() && !fn.is_implicit() && parser_) {
								StringHandle member_handle = fn.decl_node().identifier_token().handle();
								if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(current_struct_name_, member_handle)) {
									DeferredMemberFunctionInfo deferred_info;
									deferred_info.struct_name = current_struct_name_;
									deferred_info.function_node = func_decl;
									deferred_member_functions_.push_back(std::move(deferred_info));
									FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - queued lazy member function '",
									          fn.decl_node().identifier_token().value(), "' for deferred instantiation");
								}
							}
						} else {
							FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping member function with auto params (will be instantiated on call)");
						}
					} else if (func_decl.is<ConstructorDeclarationNode>()) {
						const auto& ctor = func_decl.as<ConstructorDeclarationNode>();
						// Skip constructors with unresolved auto parameters (member function templates)
						// These will be instantiated when called with concrete types
						bool ctor_has_auto = false;
						for (const auto& p : ctor.parameter_nodes()) {
							if (p.is<DeclarationNode>()) {
								const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								if (pt.type() == Type::Auto) {
									ctor_has_auto = true;
									break;
								}
							}
						}
						if (!ctor_has_auto) {
							visitConstructorDeclarationNode(ctor);
						} else {
							FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping template constructor with auto params (will be instantiated on call)");
						}
					} else if (func_decl.is<DestructorDeclarationNode>()) {
						visitDestructorDeclarationNode(func_decl.as<DestructorDeclarationNode>());
					} else if (func_decl.is<TemplateFunctionDeclarationNode>()) {
						// For member functions of class template instantiations that are wrapped in
						// TemplateFunctionDeclarationNode. If the inner function has a definition,
						// check if all parameter types are resolved. If any parameter still has
						// Type::Auto, this is a member function template (e.g., abbreviated template
						// from constrained auto) that should only be instantiated when called.
						const auto& tmpl = func_decl.as<TemplateFunctionDeclarationNode>();
						if (tmpl.function_declaration().is<FunctionDeclarationNode>()) {
							const auto& inner_func = tmpl.function_declaration().as<FunctionDeclarationNode>();
							if (inner_func.get_definition().has_value()) {
								// Check if any parameter has unresolved Auto type
								bool has_auto_param = false;
								for (const auto& p : inner_func.parameter_nodes()) {
									if (p.is<DeclarationNode>()) {
										const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
										if (pt.type() == Type::Auto) {
											has_auto_param = true;
											break;
										}
									}
								}
								if (!has_auto_param) {
									visitFunctionDeclarationNode(inner_func);
								} else {
									FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping member function template with auto params (will be instantiated on call)");
								}
							}
						}
					}
				} catch (const std::exception& ex) {
					FLASH_LOG(Codegen, Error, "Exception while visiting member function in struct ",
					          struct_name, ": ", ex.what());
					throw;
				} catch (...) {
					FLASH_LOG(Codegen, Error, "Unknown exception while visiting member function in struct ",
					          struct_name);
					throw;
				}
			}
		}  // End of if-else for local vs global struct
		
		// Clear current_function_name_ before visiting nested classes
		// Nested classes should not be treated as local structs even if we're inside
		// a member function context (e.g., after visiting constructors which set current_function_name_)
		// Nested classes are always at class scope, not function scope
		current_function_name_ = StringHandle();
		
		// Save current_struct_name_ before visiting nested classes so each nested class
		// gets the correct parent context (important when there are multiple nested classes)
		StringHandle parent_struct_name = current_struct_name_;
			
			// Visit nested classes recursively
			for (const auto& nested_class_node : node.nested_classes()) {
				if (nested_class_node.is<StructDeclarationNode>()) {
					FLASH_LOG(Codegen, Debug, "  Visiting nested class");
					// Restore parent context before each nested class visit
					current_struct_name_ = parent_struct_name;
					visitStructDeclarationNode(nested_class_node.as<StructDeclarationNode>());
				}
			}

			// Generate global storage for static members
			auto static_member_type_it = gTypesByName.find(node.name());
			if (static_member_type_it != gTypesByName.end()) {
				const TypeInfo* type_info = static_member_type_it->second;
				
				// Skip if we've already processed this TypeInfo pointer
				// (same struct can be registered under multiple keys in gTypesByName)
				if (processed_type_infos_.count(type_info) > 0) {
					// Already processed in generateStaticMemberDeclarations() or earlier visit
				} else {
					processed_type_infos_.insert(type_info);
					
					const StructTypeInfo* struct_info = type_info->getStructInfo();
					if (struct_info) {
						for (const auto& static_member : struct_info->static_members) {
							// Build the qualified name for deduplication using type_info->name()
							// This ensures consistency with generateStaticMemberDeclarations() which uses
							// the type name from gTypesByName iterator (important for template instantiations)
							StringBuilder qualified_name_sb;
							qualified_name_sb.append(StringTable::getStringView(type_info->name())).append("::").append(StringTable::getStringView(static_member.getName()));
							std::string_view qualified_name = qualified_name_sb.commit();
							StringHandle name_handle = StringTable::getOrInternStringHandle(qualified_name);
							
							// Skip if already emitted
							if (emitted_static_members_.count(name_handle) > 0) {
								continue;
							}
							emitted_static_members_.insert(name_handle);

							GlobalVariableDeclOp op;
							op.type = static_member.type;
							op.size_in_bits = static_cast<int>(static_member.size * 8);
							op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

							// Check if static member has an initializer
							op.is_initialized = static_member.initializer.has_value();
							if (op.is_initialized) {
								// Evaluate the initializer expression
								auto init_operands = visitExpressionNode(static_member.initializer->as<ExpressionNode>());
								// Convert to raw bytes
								if (init_operands.size() >= 3) {
									unsigned long long value = 0;
									if (std::holds_alternative<unsigned long long>(init_operands[2])) {
										value = std::get<unsigned long long>(init_operands[2]);
									} else if (std::holds_alternative<double>(init_operands[2])) {
										double d = std::get<double>(init_operands[2]);
										std::memcpy(&value, &d, sizeof(double));
									}
									size_t byte_count = op.size_in_bits / 8;
									for (size_t i = 0; i < byte_count; ++i) {
										op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
									}
								}
							}
							ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
						}
					}
				}
			}
			// Clear current_struct_name_ for top-level structs
		
			if (current_struct_name_.isValid()) {
				std::string_view current_name = StringTable::getStringView(current_struct_name_);
				if (current_name.find("::") == std::string_view::npos) {
					current_struct_name_ = StringHandle();
				}
			}
		// Restore the enclosing function and struct context
		current_function_name_ = saved_enclosing_function;
		current_struct_name_ = saved_struct_name;
	}

	void visitEnumDeclarationNode([[maybe_unused]] const EnumDeclarationNode& node) {
		// Enum declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system
		// Enumerators are treated as compile-time constants and don't need runtime code generation
		// For unscoped enums, the enumerators are already added to the symbol table during parsing
	}

	void visitConstructorDeclarationNode(const ConstructorDeclarationNode& node) {
		// If no definition and not explicit, check if implicit
		if (!node.get_definition().has_value()) {
			if (node.is_implicit()) {
				// Implicit constructors might not have a body if trivial, but we must emit the symbol
				// so the linker can find it if referenced.
				// Proceed to generate an empty function body.
			} else {
				return;
			}
		}

		// Reset the temporary variable counter for each new constructor
		// Constructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
		static_local_names_.clear();

		// Create constructor declaration with typed payload
		FunctionDeclOp ctor_decl_op;
		// For nested classes, use current_struct_name_ which contains the fully qualified name
		std::string_view struct_name_for_ctor = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : StringTable::getStringView(node.struct_name());
		
		// Extract just the last component of the class name for the constructor function name
		// For "Outer::Inner", we want "Inner" as the function name
		std::string_view ctor_function_name = struct_name_for_ctor;
		std::string_view parent_class_name;  // For mangling - all components except the last
		size_t last_colon = struct_name_for_ctor.rfind("::");
		if (last_colon != std::string_view::npos) {
			ctor_function_name = struct_name_for_ctor.substr(last_colon + 2);  // "Inner"
			parent_class_name = struct_name_for_ctor.substr(0, last_colon);     // "Outer"
		} else {
			parent_class_name = struct_name_for_ctor;  // Not nested, use as-is
		}
		
		ctor_decl_op.function_name = StringTable::getOrInternStringHandle(ctor_function_name);  // Constructor name (last component)
		ctor_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);  // Struct name for member function (fully qualified)
		ctor_decl_op.return_type = Type::Void;  // Constructors don't have a return type
		ctor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
		ctor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
		ctor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for constructors
		ctor_decl_op.is_variadic = false;  // Constructors are never variadic
		// Constructors defined inside class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file
		ctor_decl_op.is_inline = true;

		// Generate mangled name for constructor
		// For template instantiations, use struct_name_for_ctor which has the correct instantiated name
		// (e.g., "Base_char" instead of "Base")
		{
			std::vector<std::string_view> empty_namespace_path;

			// Use the appropriate mangling based on the style
			if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::MSVC) {
				// MSVC uses dedicated constructor mangling (??0ClassName@@...)
				ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(
					NameMangling::generateMangledNameForConstructor(struct_name_for_ctor, node.parameter_nodes(), empty_namespace_path)
				);
			} else if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
				// Itanium uses regular mangling with class name as function name (produces C1 marker)
				TypeSpecifierNode return_type(Type::Void, TypeQualifier::None, 0);
				ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(NameMangling::generateMangledName(
					ctor_function_name, return_type, node.parameter_nodes(),
					false, struct_name_for_ctor, empty_namespace_path, Linkage::CPlusPlus));
			} else {
				assert(false && "Unhandled name mangling type");
			}
		}
		
		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		// Add parameter types to constructor declaration
		size_t ctor_unnamed_param_counter = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor decl operands");
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam func_param;
			func_param.type = param_type.type();
			func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
			func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
			
			// Handle empty parameter names (e.g., from defaulted constructors)
			std::string_view param_name = param_decl.identifier_token().value();
			if (param_name.empty()) {
				// For copy/move constructors (first parameter is a reference to same struct type),
				// use "other" as the conventional name. This must match the body generation code
				// that references "other" for memberwise copy operations.
				bool is_copy_or_move_param = ctor_unnamed_param_counter == 0 &&
					(param_type.is_reference() || param_type.is_rvalue_reference()) &&
					node.parameter_nodes().size() == 1;
				
				if (is_copy_or_move_param) {
					func_param.name = StringTable::getOrInternStringHandle("other");
				} else {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(ctor_unnamed_param_counter).commit());
				}
				ctor_unnamed_param_counter++;
			} else {
				func_param.name = StringTable::getOrInternStringHandle(param_name);
			}
			
			func_param.is_reference = param_type.is_reference();
			func_param.is_rvalue_reference = param_type.is_rvalue_reference();
			func_param.cv_qualifier = param_type.cv_qualifier();
			ctor_decl_op.parameters.push_back(func_param);
		}

		// Skip duplicate constructor definitions (e.g. when a static member call queues all struct members)
		if (generated_function_names_.count(ctor_decl_op.mangled_name) > 0) {
			FLASH_LOG(Codegen, Debug, "Skipping duplicate constructor definition: ", StringTable::getStringView(ctor_decl_op.mangled_name));
			return;
		}
		generated_function_names_.insert(ctor_decl_op.mangled_name);

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), node.name_token()));
		
		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		// Use struct_name_for_ctor (which is fully qualified) instead of node.struct_name()
		// to handle nested classes correctly (node.struct_name() might be just "Inner" instead of "Outer::Inner")
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use constructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				// Mark 'this' as a pointer to struct (not a struct value)
				this_type.as<TypeSpecifierNode>().add_pointer_level();
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this"sv, this_decl);
			}
		}

		// Add parameters to symbol table
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor symbol table");
			symbol_table.insert(param_decl.identifier_token().value(), param);
		}

		// C++11 Delegating constructor: if present, ONLY call the target constructor
		// No base class or member initialization should happen
		if (node.delegating_initializer().has_value()) {
			const auto& delegating_init = node.delegating_initializer().value();
			
			// Build constructor call: StructName::StructName(this, args...)
			ConstructorCallOp ctor_op;
			ctor_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);
			ctor_op.object = StringTable::getOrInternStringHandle("this");

			// Add constructor arguments from delegating initializer
			for (const auto& arg : delegating_init.arguments) {
				auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
				// arg_operands = [type, size, value]
				if (arg_operands.size() >= 3) {
					TypedValue tv = toTypedValue(arg_operands);
					ctor_op.arguments.push_back(std::move(tv));
				}
			}

			ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
			
			// Delegating constructors don't execute the body or initialize members
			// Just return
			emitVoidReturn(node.name_token());
			return;
		}

		// C++ construction order:
		// 1. Base class constructors (in declaration order)
		// 2. Member variables (in declaration order)
		// 3. Constructor body

		// Look up the struct type to get base class and member information
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Step 1: Call base class constructors (in declaration order)
				for (const auto& base : struct_info->base_classes) {
					// Check if there's an explicit base initializer
					const BaseInitializer* base_init = nullptr;
					for (const auto& init : node.base_initializers()) {
						StringHandle base_name_handle = StringTable::getOrInternStringHandle(base.name);
						if (init.getBaseClassName() == base_name_handle) {
							base_init = &init;
							break;
						}
						// For template instantiations, the base initializer stores the un-substituted
						// name (e.g., "Base") but struct_info has the instantiated name (e.g., "Base$hash").
						// Also match against the base template name.
						if (base.type_index < gTypeInfo.size()) {
							const TypeInfo& base_ti = gTypeInfo[base.type_index];
							if (base_ti.isTemplateInstantiation() && init.getBaseClassName() == base_ti.baseTemplateName()) {
								base_init = &init;
								break;
							}
						}
					}

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build constructor call: Base::Base(this, args...)
					ConstructorCallOp ctor_op;
					ctor_op.struct_name = base_type_info.name();
					ctor_op.object = StringTable::getOrInternStringHandle("this");
					// For multiple inheritance, the 'this' pointer must be adjusted to point to the base subobject
					assert(base.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Base class offset exceeds int range");
					ctor_op.base_class_offset = static_cast<int>(base.offset);

					// Add constructor arguments from base initializer
					if (base_init) {
						for (const auto& arg : base_init->arguments) {
							auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
							// arg_operands = [type, size, value]
							if (arg_operands.size() >= 3) {
								TypedValue tv = toTypedValue(arg_operands);
								ctor_op.arguments.push_back(std::move(tv));
							}
						}
						// If there's an explicit initializer, generate the constructor call
						ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
					}
					// If no explicit initializer and this is NOT an implicit copy/move constructor,
					// call default constructor (no args)
					// For implicit copy/move constructors, the base constructor call is generated
					// in the implicit constructor generation code below
					// Note: implicit DEFAULT constructors (0 params) SHOULD call base default constructors
					else {
						bool is_implicit_default_ctor = node.is_implicit() && node.parameter_nodes().size() == 0;
						if (!node.is_implicit() || is_implicit_default_ctor) {
							// Only call base default constructor if the base class actually has constructors
							// This avoids link errors when inheriting from classes without constructors
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (base_struct_info && base_struct_info->hasAnyConstructor()) {
								// Call default constructor with no arguments
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
							}
						}
					}
				}
				
				// Step 1.5: Initialize vptr if this class has virtual functions
				// This must happen after base constructor calls (which set up base vptr)
				// but before member initialization
				if (struct_info->has_vtable) {
					// Use the pre-generated vtable symbol from struct_info
					// The vtable symbol is generated once during buildVTable()
					auto vtable_symbol = StringTable::getOrInternStringHandle(struct_info->vtable_symbol);
					
					// Create a MemberStore instruction to store vtable address to offset 0 (vptr)
					MemberStoreOp vptr_store;
					vptr_store.object = StringTable::getOrInternStringHandle("this");
					vptr_store.member_name = StringTable::getOrInternStringHandle("__vptr");  // Virtual pointer (synthetic member)
					vptr_store.offset = 0;  // vptr is always at offset 0
					vptr_store.struct_type_info = struct_type_info;  // Use TypeInfo pointer
					vptr_store.is_reference = false;
					vptr_store.is_rvalue_reference = false;
					vptr_store.vtable_symbol = vtable_symbol;  // Store vtable symbol as string_view
					
					// The value is a vtable symbol reference
					// Type is pointer (Type::Void with pointer semantics), size is 64 bits (8 bytes)
					// The actual symbol will be loaded using the vtable_symbol field
					vptr_store.value.type = Type::Void;
					vptr_store.value.size_in_bits = 64;
					vptr_store.value.value = static_cast<unsigned long long>(0);  // Placeholder
					
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(vptr_store), node.name_token()));
				}
			}
		}

		// Step 2: Generate IR for member initializers (executed before constructor body)
		// Look up the struct type to get member information
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// If this is an implicit constructor, generate appropriate initialization
				if (node.is_implicit()) {
					// Check if this is a copy or move constructor (has one parameter that is a reference)
					bool is_copy_constructor = false;
					bool is_move_constructor = false;
					if (node.parameter_nodes().size() == 1) {
						const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						if (param_type.is_reference() && param_type.type() == Type::Struct) {
							if (param_type.is_rvalue_reference()) {
								is_move_constructor = true;
							} else {
								is_copy_constructor = true;
							}
						}
					}

					if (is_copy_constructor || is_move_constructor) {
						// Implicit copy/move constructor: call base class copy/move constructors first, then memberwise copy/move from 'other' to 'this'

						// Step 1: Call base class copy/move constructors (in declaration order)
						for (const auto& base : struct_info->base_classes) {
							// Get base class type info
							if (base.type_index >= gTypeInfo.size()) {
								continue;  // Invalid base type index
							}
							const TypeInfo& base_type_info = gTypeInfo[base.type_index];

							// Only call base copy/move constructor if the base class actually has constructors
							// This avoids link errors when inheriting from classes without constructors
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (!base_struct_info || !base_struct_info->hasAnyConstructor()) {
								continue;  // Skip if base has no constructors
							}

							// Build constructor call: Base::Base(this, other)
							// For copy constructors, pass 'other' as the copy source (cast to base class reference)
							// For move constructors, pass 'other' as the move source
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = base_type_info.name();
							ctor_op.object = StringTable::getOrInternStringHandle("this");
							// For multiple inheritance, the 'this' pointer must be adjusted to point to the base subobject
							assert(base.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Base class offset exceeds int range");
							ctor_op.base_class_offset = static_cast<int>(base.offset);
							// Add 'other' parameter for copy/move constructor
							// IMPORTANT: Use BASE CLASS type_index, not derived class, for proper name mangling
							TypedValue other_arg;
							other_arg.type = Type::Struct;  // Parameter type (struct reference)
							other_arg.size_in_bits = static_cast<int>(base_type_info.struct_info_ ? base_type_info.struct_info_->total_size * 8 : struct_info->total_size * 8);
							other_arg.value = StringTable::getOrInternStringHandle("other");  // Parameter value ('other' object)
							other_arg.type_index = base.type_index;  // Use BASE class type index for proper mangling
							if (is_copy_constructor) {
								other_arg.ref_qualifier = ReferenceQualifier::LValueReference;  // Copy ctor takes lvalue reference
								other_arg.cv_qualifier = CVQualifier::Const;  // Copy ctor takes const reference
							} else if (is_move_constructor) {
								other_arg.ref_qualifier = ReferenceQualifier::RValueReference;  // Move ctor takes rvalue reference
							}
							ctor_op.arguments.push_back(std::move(other_arg));

							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
						}

						// Step 2: Memberwise copy/move from 'other' to 'this'
						for (const auto& member : struct_info->members) {
							// First, load the member from 'other'
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = StringTable::getOrInternStringHandle("other"sv);  // Load from 'other' parameter
							member_load.member_name = member.getName();
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference();
							member_load.is_rvalue_reference = member.is_rvalue_reference();
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), node.name_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this"sv);
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference();
							member_store.is_rvalue_reference = member.is_rvalue_reference();
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
						}
					} else {
						// Implicit default constructor: use default member initializers or zero-initialize

						// Step 1: Handle bitfield members - combine into single per-unit stores
						{
							std::unordered_map<size_t, unsigned long long> combined_bitfield_values;
							std::unordered_set<size_t> bitfield_offsets;
							for (const auto& member : struct_info->members) {
								if (member.bitfield_width.has_value()) {
									bitfield_offsets.insert(member.offset);
									unsigned long long val = 0;
									if (member.default_initializer.has_value()) {
										ConstExpr::EvaluationContext ctx(gSymbolTable);
										auto eval_result = ConstExpr::Evaluator::evaluate(*member.default_initializer, ctx);
										if (eval_result.success()) {
											if (std::holds_alternative<unsigned long long>(eval_result.value)) {
												val = std::get<unsigned long long>(eval_result.value);
											} else if (std::holds_alternative<long long>(eval_result.value)) {
												val = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
											} else if (std::holds_alternative<bool>(eval_result.value)) {
												val = std::get<bool>(eval_result.value) ? 1ULL : 0ULL;
											}
										}
									}
									size_t width = *member.bitfield_width;
									unsigned long long mask = (width < 64) ? ((1ULL << width) - 1) : ~0ULL;
									combined_bitfield_values[member.offset] |= ((val & mask) << member.bitfield_bit_offset);
								}
							}
							for (auto offset : bitfield_offsets) {
								for (const auto& member : struct_info->members) {
									if (member.offset == offset && member.bitfield_width.has_value()) {
										MemberStoreOp combined_store;
										combined_store.value.type = member.type;
										combined_store.value.size_in_bits = static_cast<int>(member.size * 8);
										combined_store.value.value = combined_bitfield_values[offset];
										combined_store.object = StringTable::getOrInternStringHandle("this");
										combined_store.member_name = member.getName();
										combined_store.offset = static_cast<int>(offset);
										combined_store.is_reference = false;
										combined_store.is_rvalue_reference = false;
										combined_store.struct_type_info = nullptr;
										ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(combined_store), node.name_token()));
										break;
									}
								}
							}
						}

						// Step 2: Handle non-bitfield members
						for (const auto& member : struct_info->members) {
							if (member.bitfield_width.has_value()) continue; // handled above
							// Generate MemberStore IR to initialize the member
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							
							// Determine the initial value
							IrValue member_value;
							// Check if member has a default initializer (C++11 feature)
							if (member.default_initializer.has_value()) {
								const ASTNode& init_node = member.default_initializer.value();
								if (init_node.has_value() && init_node.is<ExpressionNode>()) {
									// Use the default member initializer
									auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
									// Extract just the value (third element of init_operands)
									if (std::holds_alternative<TempVar>(init_operands[2])) {
										member_value = std::get<TempVar>(init_operands[2]);
									} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
										member_value = std::get<unsigned long long>(init_operands[2]);
									} else if (std::holds_alternative<double>(init_operands[2])) {
										member_value = std::get<double>(init_operands[2]);
									} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
										member_value = std::get<StringHandle>(init_operands[2]);
									} else {
										member_value = 0ULL;  // fallback
									}
								} else if (init_node.has_value() && init_node.is<InitializerListNode>()) {
									// Handle brace initializers like `B b1 = { .a = 1 };`
									const InitializerListNode& init_list = init_node.as<InitializerListNode>();
									const auto& initializers = init_list.initializers();
									
									// For struct members with brace initializers, we need to handle them specially
									// Get the type info for this member
									TypeIndex member_type_index = member.type_index;
									if (member_type_index < gTypeInfo.size()) {
										const TypeInfo& member_type_info = gTypeInfo[member_type_index];
										
										// If this is a struct type, we need to initialize its members
										if (member_type_info.struct_info_ && !member_type_info.struct_info_->members.empty()) {
											// Build a map of member names to initializer expressions
											std::unordered_map<StringHandle, const ASTNode*> member_values;
											size_t positional_index = 0;
											
											for (size_t i = 0; i < initializers.size(); ++i) {
												if (init_list.is_designated(i)) {
													// Designated initializer - use member name
													StringHandle member_name = init_list.member_name(i);
													member_values[member_name] = &initializers[i];
												} else {
													// Positional initializer - map to member by index
													if (positional_index < member_type_info.struct_info_->members.size()) {
														StringHandle member_name = member_type_info.struct_info_->members[positional_index].getName();
														member_values[member_name] = &initializers[i];
														positional_index++;
													}
												}
											}
											
											// Generate nested member stores for each member of the nested struct
											for (const StructMember& nested_member : member_type_info.struct_info_->members) {
												// Determine initial value for nested member
												std::optional<IrValue> nested_member_value;
												StringHandle nested_member_name_handle = nested_member.getName();
												
												if (member_values.count(nested_member_name_handle)) {
													const ASTNode& init_expr = *member_values[nested_member_name_handle];
														
													// Check if this is a nested braced initializer (two-level nesting)
													if (init_expr.is<InitializerListNode>()) {
														// Handle nested braced initializers using the recursive helper
														const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();
															
														// Get the type info for the nested member
														TypeIndex nested_member_type_index = nested_member.type_index;
														if (nested_member_type_index < gTypeInfo.size()) {
															const TypeInfo& nested_member_type_info = gTypeInfo[nested_member_type_index];
																
															// If this is a struct type, use the recursive helper
															if (nested_member_type_info.struct_info_ && !nested_member_type_info.struct_info_->members.empty()) {
																generateNestedMemberStores(
																	*nested_member_type_info.struct_info_,
																	nested_init_list,
																	StringTable::getOrInternStringHandle("this"),
																	static_cast<int>(member.offset + nested_member.offset),
																	node.name_token()
																);
																continue;  // Skip the nested member store
															} else {
																// For non-struct types with single-element initializer lists
																const auto& nested_initializers = nested_init_list.initializers();
																if (nested_initializers.size() == 1 && nested_initializers[0].is<ExpressionNode>()) {
																	auto nested_init_operands = visitExpressionNode(nested_initializers[0].as<ExpressionNode>());
																	if (std::holds_alternative<TempVar>(nested_init_operands[2])) {
																		nested_member_value = std::get<TempVar>(nested_init_operands[2]);
																	} else if (std::holds_alternative<unsigned long long>(nested_init_operands[2])) {
																		nested_member_value = std::get<unsigned long long>(nested_init_operands[2]);
																	} else if (std::holds_alternative<double>(nested_init_operands[2])) {
																		nested_member_value = std::get<double>(nested_init_operands[2]);
																	} else if (std::holds_alternative<StringHandle>(nested_init_operands[2])) {
																		nested_member_value = std::get<StringHandle>(nested_init_operands[2]);
																	}
																}
															}
														}
													} else if (init_expr.is<ExpressionNode>()) {
														auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
														if (std::holds_alternative<TempVar>(init_operands[2])) {
															nested_member_value = std::get<TempVar>(init_operands[2]);
														} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
															nested_member_value = std::get<unsigned long long>(init_operands[2]);
														} else if (std::holds_alternative<double>(init_operands[2])) {
															nested_member_value = std::get<double>(init_operands[2]);
														} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
															nested_member_value = std::get<StringHandle>(init_operands[2]);
														}
													}
												}
												
												if (nested_member_value.has_value()) {
													// Generate nested member store
													MemberStoreOp nested_member_store;
													nested_member_store.value.type = nested_member.type;
													nested_member_store.value.size_in_bits = static_cast<int>(nested_member.size * 8);
													nested_member_store.value.value = nested_member_value.value();
													nested_member_store.object = StringTable::getOrInternStringHandle("this");
													nested_member_store.member_name = nested_member.getName();
													// Calculate offset: parent member offset + nested member offset
													nested_member_store.offset = static_cast<int>(member.offset + nested_member.offset);
													nested_member_store.is_reference = nested_member.is_reference();
													nested_member_store.is_rvalue_reference = nested_member.is_rvalue_reference();
													nested_member_store.struct_type_info = nullptr;
												
													ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(nested_member_store), node.name_token()));
												}
											}
											
											// Skip the outer member store since we've already generated nested stores
											continue;
										} else {
											// For non-struct types with single-element initializer lists
											if (initializers.size() == 1 && initializers[0].is<ExpressionNode>()) {
												auto init_operands = visitExpressionNode(initializers[0].as<ExpressionNode>());
												if (std::holds_alternative<TempVar>(init_operands[2])) {
													member_value = std::get<TempVar>(init_operands[2]);
												} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
													member_value = std::get<unsigned long long>(init_operands[2]);
												} else if (std::holds_alternative<double>(init_operands[2])) {
													member_value = std::get<double>(init_operands[2]);
												} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
													member_value = std::get<StringHandle>(init_operands[2]);
												} else {
													member_value = 0ULL;
												}
											} else {
												member_value = 0ULL;
											}
										}
									} else {
										member_value = 0ULL;
									}
								} else {
									// Default initializer exists but isn't an expression, zero-initialize
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										member_value = 0ULL;  // Zero for integer types
									} else if (member.type == Type::Float || member.type == Type::Double) {
										member_value = 0.0;  // Zero for floating-point types
									} else if (member.type == Type::Bool) {
										member_value = 0ULL;  // False for bool (0)
									} else {
										member_value = 0ULL;  // Default to zero
									}
								}
							} else {
								// Check if this is a struct type with a constructor
								bool is_struct_with_constructor = false;
								if (member.type == Type::Struct && member.type_index < gTypeInfo.size()) {
									const TypeInfo& member_type_info = gTypeInfo[member.type_index];
									if (member_type_info.struct_info_ && member_type_info.struct_info_->hasAnyConstructor()) {
										is_struct_with_constructor = true;
									}
								}
								
								if (is_struct_with_constructor) {
									// Call the nested struct's default constructor instead of zero-initializing
									const TypeInfo& member_type_info = gTypeInfo[member.type_index];
									ConstructorCallOp ctor_op;
									ctor_op.struct_name = member_type_info.name();
									ctor_op.object = StringTable::getOrInternStringHandle("this");
									// No arguments for default constructor
									// Use base_class_offset to specify the member's offset within the parent struct
									assert(member.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Member offset exceeds int range");
									ctor_op.base_class_offset = static_cast<int>(member.offset);
									ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
									continue;  // Skip the MemberStore since constructor handles initialization
								} else {
									// Zero-initialize based on type
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										member_value = 0ULL;  // Zero for integer types
									} else if (member.type == Type::Float || member.type == Type::Double) {
										member_value = 0.0;  // Zero for floating-point types
									} else if (member.type == Type::Bool) {
										member_value = 0ULL;  // False for bool (0)
									} else {
										member_value = 0ULL;  // Default to zero
									}
								}
							}
	
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference();
							member_store.is_rvalue_reference = member.is_rvalue_reference();
							member_store.struct_type_info = nullptr;
	
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
						}
					}
				} else {
					// User-defined constructor: initialize all members
					// Precedence: explicit initializer > default initializer > zero-initialize

					// Build a map of explicit member initializers for quick lookup
					std::unordered_map<std::string, const MemberInitializer*> explicit_inits;
					for (const auto& initializer : node.member_initializers()) {
						explicit_inits[std::string(initializer.member_name)] = &initializer;
					}

					// Initialize all members
					for (const auto& member : struct_info->members) {
						// Generate MemberStore IR to initialize the member
						
						// Determine the initial value
						IrValue member_value;
						// Check for explicit initializer first (highest precedence)
						auto explicit_it = explicit_inits.find(std::string(StringTable::getStringView(member.getName())));
						if (explicit_it != explicit_inits.end()) {
							// Special handling for reference members initialized with reference variables/parameters
							// When initializing a reference member (int& ref) with a reference parameter (int& r),
							// we need to use the pointer value that the parameter holds, not dereference it
							bool handled_as_reference_init = false;
							if (member.is_reference() || member.is_rvalue_reference()) {
								// Check if the initializer is a simple identifier
								const ASTNode& init_expr = explicit_it->second->initializer_expr;
								if (init_expr.is<ExpressionNode>()) {
									const auto& expr_node = init_expr.as<ExpressionNode>();
									if (std::holds_alternative<IdentifierNode>(expr_node)) {
										const auto& id_node = std::get<IdentifierNode>(expr_node);
										auto init_name = StringTable::getOrInternStringHandle(id_node.name());
										
										// Look up the identifier in the symbol table
										std::optional<ASTNode> init_symbol = symbol_table.lookup(init_name);
										if (init_symbol.has_value() && init_symbol->is<DeclarationNode>()) {
											const auto& init_decl = init_symbol->as<DeclarationNode>();
											const auto& init_type = init_decl.type_node().as<TypeSpecifierNode>();
											
											// If the initializer is a reference, use its value directly (it's already a pointer)
											// Don't dereference it - just use the string_view to refer to the variable
											if (init_type.is_reference() || init_type.is_rvalue_reference()) {
												member_value = init_name;
												handled_as_reference_init = true;
											}
										}
									}
								}
							}
							
							if (!handled_as_reference_init) {
								// Use explicit initializer from constructor initializer list
								auto init_operands = visitExpressionNode(explicit_it->second->initializer_expr.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
								if (std::holds_alternative<TempVar>(init_operands[2])) {
									member_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									member_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									member_value = std::get<double>(init_operands[2]);
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									member_value = std::get<StringHandle>(init_operands[2]);
								} else {
									member_value = 0ULL;  // fallback
								}
							}
						} else if (member.default_initializer.has_value()) {
							const ASTNode& init_node = member.default_initializer.value();
							if (init_node.has_value() && init_node.is<ExpressionNode>()) {
								// Use default member initializer (C++11 feature)
								auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
								if (std::holds_alternative<TempVar>(init_operands[2])) {
									member_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									member_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									member_value = std::get<double>(init_operands[2]);
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									member_value = std::get<StringHandle>(init_operands[2]);
								} else {
									member_value = 0ULL;  // fallback
								}
							} else {
								// Default initializer exists but isn't an expression, zero-initialize
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;
								}
							}
						} else {
							// Check if this is a struct type with a constructor
							bool is_struct_with_constructor = false;
							if (member.type == Type::Struct && member.type_index < gTypeInfo.size()) {
								const TypeInfo& member_type_info = gTypeInfo[member.type_index];
								if (member_type_info.struct_info_ && member_type_info.struct_info_->hasAnyConstructor()) {
									is_struct_with_constructor = true;
								}
							}
							
							if (is_struct_with_constructor) {
								// Call the nested struct's default constructor instead of zero-initializing
								const TypeInfo& member_type_info = gTypeInfo[member.type_index];
								ConstructorCallOp ctor_op;
								ctor_op.struct_name = member_type_info.name();
								ctor_op.object = StringTable::getOrInternStringHandle("this");
								// No arguments for default constructor
								// Use base_class_offset to specify the member's offset within the parent struct
								assert(member.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Member offset exceeds int range");
								ctor_op.base_class_offset = static_cast<int>(member.offset);
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
								continue;  // Skip the MemberStore since constructor handles initialization
							} else {
								// Zero-initialize based on type
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;  // Zero for integer types
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;  // Zero for floating-point types
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;  // Default to zero
								}
							}
						}
	
						MemberStoreOp member_store;
						member_store.value.type = member.type;
						member_store.value.size_in_bits = static_cast<int>(member.size * 8);
						member_store.value.value = member_value;
						member_store.object = StringTable::getOrInternStringHandle("this");
						member_store.member_name = member.getName();
						member_store.offset = static_cast<int>(member.offset);
						member_store.is_reference = member.is_reference();
						member_store.is_rvalue_reference = member.is_rvalue_reference();
						member_store.struct_type_info = nullptr;
						member_store.bitfield_width = member.bitfield_width;
						member_store.bitfield_bit_offset = member.bitfield_bit_offset;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
					}
				}
			}
		}

		// Visit the constructor body
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		block.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Add implicit return for constructor (constructors don't have explicit return statements)
		emitVoidReturn(node.name_token());

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node) {
		if (!node.get_definition().has_value())
			return;

		// Reset the temporary variable counter for each new destructor
		// Destructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
	static_local_names_.clear();

	// Create destructor declaration with typed payload
	FunctionDeclOp dtor_decl_op;
	dtor_decl_op.function_name = StringTable::getOrInternStringHandle(StringBuilder().append("~"sv).append(node.struct_name()));  // Destructor name
	dtor_decl_op.struct_name = node.struct_name();
	dtor_decl_op.return_type = Type::Void;  // Destructors don't have a return type
	dtor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
	dtor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
	dtor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for destructors
	dtor_decl_op.is_variadic = false;  // Destructors are never variadic

	// Generate mangled name for destructor
	// Use the dedicated mangling function for destructors to ensure correct platform-specific mangling
	// (e.g., MSVC uses ??1ClassName@... format)
	dtor_decl_op.mangled_name = NameMangling::generateMangledNameFromNode(node);

	// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
	// We don't add it here to avoid duplication

	ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(dtor_decl_op), node.name_token()));		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		auto type_it = gTypesByName.find(node.struct_name());
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use destructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				// Mark 'this' as a pointer to struct (not a struct value)
				this_type.as<TypeSpecifierNode>().add_pointer_level();
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this"sv, this_decl);
			}
		}

		// C++ destruction order:
		// 1. Destructor body
		// 2. Member variables destroyed (automatic for non-class types)
		// 3. Base class destructors (in REVERSE declaration order)

		// Step 1: Visit the destructor body
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		block.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Step 2: Member destruction is automatic for primitive types (no action needed)

		// Step 3: Call base class destructors in REVERSE order
		auto struct_type_it = gTypesByName.find(node.struct_name());
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info && !struct_info->base_classes.empty()) {
				// Iterate through base classes in reverse order
				for (auto it = struct_info->base_classes.rbegin(); it != struct_info->base_classes.rend(); ++it) {
					const auto& base = *it;

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build destructor call: Base::~Base(this)
					DestructorCallOp dtor_op;
					dtor_op.struct_name = base_type_info.name();
					dtor_op.object = StringTable::getOrInternStringHandle("this");

					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), node.name_token()));
				}
			}
		}

		// Add implicit return for destructor (destructors don't have explicit return statements)
		emitVoidReturn(node.name_token());

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

