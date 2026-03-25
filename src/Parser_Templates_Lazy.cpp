#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"
#include "ExpressionSubstitutor.h"
#include "ParserTemplateClassShared.h"


std::optional<ASTNode> Parser::instantiateLazyMemberFunction(const LazyMemberFunctionInfo& lazy_info) {
	FLASH_LOG(Templates, Debug, "instantiateLazyMemberFunction: ", 
	          lazy_info.identity.instantiated_owner_name, "::", effectiveLookupName(lazy_info.identity));
	
	// Constructors/destructors for nested template types are also materialized lazily.
	if (lazy_info.identity.kind == DeferredMemberIdentity::Kind::Constructor && lazy_info.identity.original_member_node.is<ConstructorDeclarationNode>()) {
		const ConstructorDeclarationNode& ctor_decl = lazy_info.identity.original_member_node.as<ConstructorDeclarationNode>();
		if (!ctor_decl.get_definition().has_value() && !ctor_decl.has_template_body_position()) {
			FLASH_LOG(Templates, Error, "Lazy constructor has no definition and no deferred body position");
			return std::nullopt;
		}

		StringHandle ctor_name_handle = effectiveLookupName(lazy_info.identity);
		std::string_view ctor_name_view = StringTable::getStringView(ctor_name_handle);
		if (ctor_name_view.empty() || ctor_name_view.find("::") != std::string_view::npos) {
			std::string_view struct_name = StringTable::getStringView(lazy_info.identity.instantiated_owner_name);
			if (size_t pos = struct_name.rfind("::"); pos != std::string_view::npos) {
				ctor_name_handle = StringTable::getOrInternStringHandle(struct_name.substr(pos + 2));
			} else {
				ctor_name_handle = lazy_info.identity.instantiated_owner_name;
			}
		}
		auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
			lazy_info.identity.instantiated_owner_name, ctor_name_handle
		);

		// Build parameter list, expanding variadic pack parameters into N individual
		// parameters (args_0, args_1, ...) and populating pack_param_info_ so that
		// pack expansions in initializers and the body resolve correctly.
		size_t saved_ctor_pack_info = pack_param_info_.size();
		for (const auto& param : ctor_decl.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param.as<DeclarationNode>();
				const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

				bool handled_as_pack = false;
				if (param_decl.is_parameter_pack() && param_type_spec.type() == Type::UserDefined) {
					std::string_view type_name = param_type_spec.token().value();
					size_t non_variadic = 0;
					size_t pack_size = 0;
					bool found_pack = false;
					for (size_t i = 0; i < lazy_info.template_params.size(); ++i) {
						if (!lazy_info.template_params[i].is<TemplateParameterNode>()) continue;
						const TemplateParameterNode& tparam = lazy_info.template_params[i].as<TemplateParameterNode>();
						if (!tparam.is_variadic()) { non_variadic++; continue; }
						if (tparam.name() == type_name) {
							pack_size = lazy_info.template_args.size() > non_variadic
								? lazy_info.template_args.size() - non_variadic : 0;
							found_pack = true;
							break;
						}
					}
					if (found_pack) {
						if (pack_size == 0) { handled_as_pack = true; }
						else {
							std::string_view orig_name = param_decl.identifier_token().value();
							for (size_t pi = 0; pi < pack_size; ++pi) {
								const TemplateTypeArg& elem = lazy_info.template_args[non_variadic + pi];
								Type elem_type = elem.base_type;
								TypeIndex elem_type_index = elem.type_index;
								TypeSpecifierNode sub_type(
									elem_type, param_type_spec.qualifier(),
									get_type_size_bits(elem_type),
									param_decl.identifier_token(), param_type_spec.cv_qualifier());
								sub_type.set_type_index(elem_type_index);
								for (const auto& pl : param_type_spec.pointer_levels())
									sub_type.add_pointer_level(pl.cv_qualifier);
								sub_type.set_reference_qualifier(param_type_spec.reference_qualifier());
								StringBuilder name_builder;
								name_builder.append(orig_name).append('_').append(pi);
								Token elem_token(Token::Type::Identifier, name_builder.commit(),
									param_decl.identifier_token().line(),
									param_decl.identifier_token().column(),
									param_decl.identifier_token().file_index());
								new_ctor_ref.add_parameter_node(emplace_node<DeclarationNode>(
									emplace_node<TypeSpecifierNode>(sub_type), elem_token));
							}
							pack_param_info_.push_back({orig_name, 0, pack_size});
							handled_as_pack = true;
						}
					}
				}
				if (handled_as_pack) continue;

				auto [param_type, param_type_index] = substitute_template_parameter(
					param_type_spec, lazy_info.template_params, lazy_info.template_args
				);

				TypeSpecifierNode substituted_param_type(
					param_type,
					param_type_spec.qualifier(),
					get_type_size_bits(param_type),
					param_decl.identifier_token(),
					param_type_spec.cv_qualifier()
				);
				substituted_param_type.set_type_index(param_type_index);
				for (const auto& ptr_level : param_type_spec.pointer_levels()) {
					substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
				}
				substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

				auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
				auto substituted_param_decl = emplace_node<DeclarationNode>(substituted_param_type_node, param_decl.identifier_token());
				if (param_decl.has_default_value()) {
					ASTNode substituted_default = substituteTemplateParameters(
						param_decl.default_value(), lazy_info.template_params, lazy_info.template_args);
					substituted_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
				}
				new_ctor_ref.add_parameter_node(substituted_param_decl);
			} else {
				new_ctor_ref.add_parameter_node(param);
			}
		}

		// Build converted_template_args early so member/base initializer expressions
		// can be substituted (they may contain PackExpansionExprNode from patterns like
		// _M_value(std::forward<_Args>(__args)...) that must be expanded now).
		std::vector<TemplateTypeArg> converted_template_args;
		converted_template_args.reserve(lazy_info.template_args.size());
		for (const auto& ttype_arg : lazy_info.template_args) {
			if (ttype_arg.is_value) {
				converted_template_args.push_back(TemplateTypeArg::makeValue(ttype_arg.value, ttype_arg.base_type));
			} else {
				converted_template_args.push_back(TemplateTypeArg::makeType(ttype_arg.base_type, ttype_arg.type_index));
			}
		}

		auto substituteInitExpr = [&](const ASTNode& expr) -> ASTNode {
			return substituteTemplateParameters(expr, lazy_info.template_params, converted_template_args);
		};

		for (const auto& init : ctor_decl.member_initializers()) {
			new_ctor_ref.add_member_initializer(init.member_name, substituteInitExpr(init.initializer_expr));
		}
		// Helper: substitute a single initializer argument, expanding PackExpansionExprNode
		// into multiple arguments when present.  Mirrors the eager path's substituteInitArg.
		auto substituteInitArg = [&](const ASTNode& arg, std::vector<ASTNode>& out) {
			if (arg.is<ExpressionNode>()) {
				const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
				if (const auto* pack_exp = std::get_if<PackExpansionExprNode>(&arg_expr)) {
					ChunkedVector<ASTNode> expanded;
					if (expandPackExpansionArgs(*pack_exp, lazy_info.template_params, converted_template_args, expanded)) {
						for (size_t ei = 0; ei < expanded.size(); ++ei) {
							out.push_back(expanded[ei]);
						}
						return;
					}
				}
			}
			out.push_back(substituteInitExpr(arg));
		};

		for (const auto& init : ctor_decl.base_initializers()) {
			std::vector<ASTNode> substituted_args;
			substituted_args.reserve(init.arguments.size());
			for (const auto& arg : init.arguments) {
				substituteInitArg(arg, substituted_args);
			}
			new_ctor_ref.add_base_initializer(init.getBaseClassName(), std::move(substituted_args));
		}
		if (ctor_decl.delegating_initializer().has_value()) {
			std::vector<ASTNode> substituted_del_args;
			substituted_del_args.reserve(ctor_decl.delegating_initializer()->arguments.size());
			for (const auto& arg : ctor_decl.delegating_initializer()->arguments) {
				substituteInitArg(arg, substituted_del_args);
			}
			new_ctor_ref.set_delegating_initializer(std::move(substituted_del_args));
		}
		new_ctor_ref.set_is_implicit(ctor_decl.is_implicit());
		new_ctor_ref.set_noexcept(ctor_decl.is_noexcept());

		std::optional<ASTNode> body_to_substitute;
		if (ctor_decl.get_definition().has_value()) {
			body_to_substitute = ctor_decl.get_definition();
		} else if (ctor_decl.has_template_body_position()) {
			FlashCpp::TemplateParameterScope template_scope;
			InlineVector<StringHandle, 4> param_names;
			param_names.reserve(lazy_info.template_params.size());
			for (const auto& tparam_node : lazy_info.template_params) {
				if (tparam_node.is<TemplateParameterNode>()) {
					param_names.push_back(tparam_node.as<TemplateParameterNode>().nameHandle());
				}
			}
			registerTypeParamsInScope(param_names, lazy_info.template_args, template_scope, true);

			SaveHandle current_pos = save_token_position();
			restore_lexer_position_only(ctor_decl.template_body_position());
			gSymbolTable.enter_scope(ScopeType::Function);
			for (const auto& param : new_ctor_ref.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl = param.as<DeclarationNode>();
					gSymbolTable.insert(param_decl.identifier_token().value(), param);
				}
			}

			{
				FlashCpp::ScopedState guard_subs(template_param_substitutions_);
				populateTemplateParamSubstitutions(template_param_substitutions_, param_names, lazy_info.template_args);
				auto block_result = parse_function_body(true /* is_ctor_or_dtor: constructor */);  // handles function-try-blocks
				if (!block_result.is_error() && block_result.node().has_value()) {
					body_to_substitute = block_result.node();
				}
			}

			gSymbolTable.exit_scope();
			restore_lexer_position_only(current_pos);
			discard_saved_token(current_pos);
		}

		if (!body_to_substitute.has_value()) {
			FLASH_LOG(Templates, Error, "Failed to obtain constructor body for lazy instantiation");
			return std::nullopt;
		}

		ASTNode substituted_body = substituteTemplateParameters(
			*body_to_substitute,
			lazy_info.template_params,
			converted_template_args
		);
		new_ctor_ref.set_definition(substituted_body);
		pack_param_info_.resize(saved_ctor_pack_info);

		ast_nodes_.push_back(new_ctor_node);
		return new_ctor_node;
	}

	if (lazy_info.identity.kind == DeferredMemberIdentity::Kind::Destructor && lazy_info.identity.original_member_node.is<DestructorDeclarationNode>()) {
		const DestructorDeclarationNode& dtor_decl = lazy_info.identity.original_member_node.as<DestructorDeclarationNode>();
		if (!dtor_decl.get_definition().has_value()) {
			FLASH_LOG(Templates, Error, "Lazy destructor has no definition");
			return std::nullopt;
		}

		StringHandle dtor_name_handle = effectiveLookupName(lazy_info.identity);
		std::string_view dtor_name_view = StringTable::getStringView(dtor_name_handle);
		// Normalize destructor name for nested template instantiations:
		// - empty: missing in lazy registry entry
		// - qualified: need unqualified "~Class" form
		// - missing '~': malformed name that must be reconstructed
		if (dtor_name_view.empty() || dtor_name_view.find("::") != std::string_view::npos || dtor_name_view[0] != '~') {
			std::string_view struct_name = StringTable::getStringView(lazy_info.identity.instantiated_owner_name);
			std::string_view simple_name = struct_name;
			constexpr size_t kScopeResolutionLen = 2; // "::"
			if (size_t pos = struct_name.rfind("::"); pos != std::string_view::npos) {
				simple_name = struct_name.substr(pos + kScopeResolutionLen);
			}
			dtor_name_handle = StringTable::getOrInternStringHandle(StringBuilder().append("~").append(simple_name).commit());
		}

		auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
			lazy_info.identity.instantiated_owner_name, dtor_name_handle
		);
		new_dtor_ref.set_noexcept(dtor_decl.is_noexcept());
		new_dtor_ref.set_has_noexcept_specifier(dtor_decl.has_noexcept_specifier());
		if (dtor_decl.has_noexcept_expression()) {
			new_dtor_ref.set_noexcept_expression(*dtor_decl.noexcept_expression());
		}

		std::vector<TemplateTypeArg> converted_template_args;
		converted_template_args.reserve(lazy_info.template_args.size());
		for (const auto& ttype_arg : lazy_info.template_args) {
			if (ttype_arg.is_value) {
				converted_template_args.push_back(TemplateTypeArg::makeValue(ttype_arg.value, ttype_arg.base_type));
			} else {
				converted_template_args.push_back(TemplateTypeArg::makeType(ttype_arg.base_type, ttype_arg.type_index));
			}
		}

		ASTNode substituted_body = substituteTemplateParameters(
			*dtor_decl.get_definition(),
			lazy_info.template_params,
			converted_template_args
		);
		new_dtor_ref.set_definition(substituted_body);

		ast_nodes_.push_back(new_dtor_node);
		return new_dtor_node;
	}

	// Get the original function declaration
	if (!lazy_info.identity.original_member_node.is<FunctionDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Lazy member function node is not a FunctionDeclarationNode");
		return std::nullopt;
	}
	
	const FunctionDeclarationNode& func_decl = lazy_info.identity.original_member_node.as<FunctionDeclarationNode>();
	const DeclarationNode& decl = func_decl.decl_node();
	
	if (!func_decl.get_definition().has_value() && !func_decl.has_template_body_position()) {
		FLASH_LOG(Templates, Error, "Lazy member function has no definition and no deferred body position");
		return std::nullopt;
	}
	
	// Perform template parameter substitution (same as eager path)
	// Substitute return type
	const TypeSpecifierNode& return_type_spec = decl.type_node().as<TypeSpecifierNode>();
	auto [return_type, return_type_index] = substitute_template_parameter(
		return_type_spec, lazy_info.template_params, lazy_info.template_args
	);

	// Resolve self-referential types: when a member function's return type or parameter type
	// refers to the template class itself (e.g., W& in W<T>::operator+=), the type_index
	// still points to the uninstantiated template base (e.g., W with size=0). We need to
	// resolve it to the instantiated class (e.g., W<int> with correct size).
	auto resolve_self_type = [&lazy_info](Type& type, TypeIndex& type_index) {
		if (type == Type::Struct && type_index.is_valid() && type_index.value < gTypeInfo.size()) {
			if (gTypeInfo[type_index.value].name() == lazy_info.identity.template_owner_name) {
				// This type refers to the template base class — resolve to the instantiated class
				auto it = gTypesByName.find(lazy_info.identity.instantiated_owner_name);
				if (it != gTypesByName.end()) {
					type_index = it->second->type_index_;
				}
			}
		}
	};

	resolve_self_type(return_type, return_type_index);

	// Create substituted return type node (use the return type's token, not the function identifier)
	TypeSpecifierNode substituted_return_type(
		return_type,
		return_type_spec.qualifier(),
		get_type_size_bits(return_type),
		return_type_spec.token()
	);
	substituted_return_type.set_type_index(return_type_index);

	// Copy pointer levels and reference qualifiers from original
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type.add_pointer_level(ptr_level.cv_qualifier);
	}
	substituted_return_type.set_reference_qualifier(return_type_spec.reference_qualifier());

	auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);

	// Invariant: fn_identifier_token.handle() == effectiveLookupName(lazy_info.identity).
	// This is asserted below after finalization (Slice 5).
	// Slice 4: use the canonical instantiated lookup name (from identity) as the
	// function identifier token so the emitted body matches the stub's registered name.
	StringHandle fn_name_handle = effectiveLookupName(lazy_info.identity);
	Token fn_identifier_token(Token::Type::Identifier,
		StringTable::getStringView(fn_name_handle),
		decl.identifier_token().line(), decl.identifier_token().column(),
		decl.identifier_token().file_index());
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
		substituted_return_node, fn_identifier_token
	);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
		new_func_decl_ref, lazy_info.identity.instantiated_owner_name
	);
	setOuterTemplateBindingsFromParams(new_func_ref, lazy_info.template_params, lazy_info.template_args);

	// Substitute and copy parameters, expanding variadic pack parameters into N individual
	// parameters (args_0, args_1, ...) and populating pack_param_info_ for body expansion.
	size_t saved_pack_info = pack_param_info_.size();
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			// Expand variadic pack parameters (e.g. "Args... args") into N params.
			bool handled_as_pack = false;
			if (param_decl.is_parameter_pack() && param_type_spec.type() == Type::UserDefined) {
				std::string_view type_name = param_type_spec.token().value();
				size_t non_variadic = 0;
				size_t pack_size = 0;
				bool found_pack = false;
				for (size_t i = 0; i < lazy_info.template_params.size(); ++i) {
					if (!lazy_info.template_params[i].is<TemplateParameterNode>()) continue;
					const TemplateParameterNode& tparam = lazy_info.template_params[i].as<TemplateParameterNode>();
					if (!tparam.is_variadic()) { non_variadic++; continue; }
					if (tparam.name() == type_name) {
						pack_size = lazy_info.template_args.size() > non_variadic
							? lazy_info.template_args.size() - non_variadic : 0;
						found_pack = true;
						break;
					}
				}
				if (found_pack) {
					if (pack_size == 0) { handled_as_pack = true; } // empty pack, skip
					else {
						std::string_view orig_name = param_decl.identifier_token().value();
						for (size_t pi = 0; pi < pack_size; ++pi) {
							const TemplateTypeArg& elem = lazy_info.template_args[non_variadic + pi];
							Type elem_type = elem.base_type;
							TypeIndex elem_type_index = elem.type_index;
							TypeSpecifierNode sub_type(
								elem_type, param_type_spec.qualifier(),
								get_type_size_bits(elem_type),
								param_decl.identifier_token(), param_type_spec.cv_qualifier());
							sub_type.set_type_index(elem_type_index);
							for (const auto& pl : param_type_spec.pointer_levels())
								sub_type.add_pointer_level(pl.cv_qualifier);
							sub_type.set_reference_qualifier(param_type_spec.reference_qualifier());
							StringBuilder name_builder;
							name_builder.append(orig_name).append('_').append(pi);
							Token elem_token(Token::Type::Identifier, name_builder.commit(),
								param_decl.identifier_token().line(),
								param_decl.identifier_token().column(),
								param_decl.identifier_token().file_index());
							new_func_ref.add_parameter_node(emplace_node<DeclarationNode>(
								emplace_node<TypeSpecifierNode>(sub_type), elem_token));
						}
						pack_param_info_.push_back({orig_name, 0, pack_size});
						handled_as_pack = true;
					}
				}
			}
			if (handled_as_pack) continue;

			// Substitute parameter type
			auto [param_type, param_type_index] = substitute_template_parameter(
				param_type_spec, lazy_info.template_params, lazy_info.template_args
			);

			// Resolve self-referential class types (same as return type)
			resolve_self_type(param_type, param_type_index);

			// Create substituted parameter type
			TypeSpecifierNode substituted_param_type(
				param_type,
				param_type_spec.qualifier(),
				get_type_size_bits(param_type),
				param_decl.identifier_token(),
				param_type_spec.cv_qualifier()
			);
			substituted_param_type.set_type_index(param_type_index);

			// Copy pointer levels and reference qualifiers
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
			}
			substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

			auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
			auto substituted_param_decl = emplace_node<DeclarationNode>(
				substituted_param_type_node, param_decl.identifier_token()
			);
			// Copy default value if present
			if (param_decl.has_default_value()) {
				ASTNode substituted_default = substituteTemplateParameters(
					param_decl.default_value(), lazy_info.template_params, lazy_info.template_args);
				substituted_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
			}
			new_func_ref.add_parameter_node(substituted_param_decl);
		} else {
			// Non-declaration parameter, copy as-is
			new_func_ref.add_parameter_node(param);
		}
	}

	// Get the function body - either from definition or by re-parsing from saved position
	std::optional<ASTNode> body_to_substitute;
	
	if (func_decl.get_definition().has_value()) {
		// Use the already-parsed definition
		body_to_substitute = func_decl.get_definition();
	} else if (func_decl.has_template_body_position()) {
		FLASH_LOG(Templates, Debug, "Lazy member function body: re-parsing from saved position");
		// Re-parse the function body from saved position
		// This is needed for member struct templates where body parsing is deferred
		
		// Set up template parameter types in the type system for body parsing
		FlashCpp::TemplateParameterScope template_scope;
		InlineVector<StringHandle, 4> param_names;
		param_names.reserve(lazy_info.template_params.size());
		for (const auto& tparam_node : lazy_info.template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().nameHandle());
			}
		}
		
		registerTypeParamsInScope(param_names, lazy_info.template_args, template_scope, true);

		// Save current position and parsing context
		SaveHandle current_pos = save_token_position();
		const FunctionDeclarationNode* saved_current_function = current_function_;
		
		// When re-parsing a lazy member function body with concrete types,
		// we're no longer in a dependent template context. Set parsing_template_depth_
		// to false so that constant expressions like sizeof(int) are evaluated.
		FlashCpp::ScopedState guard_ptb(parsing_template_depth_);  // saves depth, restores on exit
		parsing_template_depth_ = 0;  // suppress template body context during lazy instantiation

		// Restore to the function body start
		restore_lexer_position_only(func_decl.template_body_position());

		// Set up parsing context for the function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}

		// Set up template parameter substitutions so non-type params (e.g., int N)
		// are resolved during parse_block() just as in try_instantiate_single_template.
		{
			FlashCpp::ScopedState guard_subs(template_param_substitutions_);
			populateTemplateParamSubstitutions(template_param_substitutions_, param_names, lazy_info.template_args);

			// Parse the function body
			{
				FlashCpp::ScopedState guard_param_names(current_template_param_names_);
				for (const auto& pn : param_names) {
					current_template_param_names_.push_back(pn);
				}

				auto block_result = parse_function_body();  // handles function-try-blocks

				if (!block_result.is_error() && block_result.node().has_value()) {
					body_to_substitute = block_result.node();
				}
			} // current_template_param_names_ restored here
		} // template_param_substitutions_ restored here
		
		// Clean up context
		current_function_ = saved_current_function;
		gSymbolTable.exit_scope();

		// Restore original position
		restore_lexer_position_only(current_pos);
		discard_saved_token(current_pos);
	}

	// Substitute template parameters in the function body
	if (body_to_substitute.has_value()) {
		// Build template argument vector for registration
		std::vector<TemplateTypeArg> converted_template_args;
		for (const auto& ttype_arg : lazy_info.template_args) {
			if (ttype_arg.is_value) {
				converted_template_args.push_back(TemplateTypeArg::makeValue(ttype_arg.value, ttype_arg.base_type));
			} else {
				converted_template_args.push_back(TemplateTypeArg::makeType(ttype_arg.base_type, ttype_arg.type_index));
			}
		}

		// Push struct parsing context so that get_class_template_pack_size can find pack info in the registry
		// This is needed for sizeof...(Pack) to work in lazy member function bodies
		StructParsingContext struct_ctx;
		struct_ctx.struct_name = StringTable::getStringView(lazy_info.identity.instantiated_owner_name);
		struct_ctx.struct_node = nullptr;
		struct_ctx.local_struct_info = nullptr;
		struct_parsing_context_stack_.push_back(struct_ctx);
		auto pop_struct_ctx = [this](void*) {
			if (!struct_parsing_context_stack_.empty())
				struct_parsing_context_stack_.pop_back();
		};
		std::unique_ptr<void, decltype(pop_struct_ctx)> struct_ctx_scope(reinterpret_cast<void*>(1), pop_struct_ctx);

		ASTNode substituted_body = substituteTemplateParameters(
			*body_to_substitute,
			lazy_info.template_params,
			converted_template_args
		);
		new_func_ref.set_definition(substituted_body);
	}

	copy_function_properties(new_func_ref, func_decl);
	pack_param_info_.resize(saved_pack_info);
	// Carry the const-method qualifier so mangling emits 'K' (Itanium) / 'QEBA' (MSVC).
	new_func_ref.set_is_const_member_function(lazy_info.identity.is_const_method);
	new_func_ref.set_is_volatile_member_function(hasCVQualifier(lazy_info.identity.cv_qualifier, CVQualifier::Volatile));

	if (new_func_ref.get_definition().has_value()) {
		finalize_function_after_definition(new_func_ref);
	} else {
		// This is essential so that FunctionCallNode can carry the correct mangled name
		// and codegen can resolve the correct function for each template instantiation
		compute_and_set_mangled_name(new_func_ref);
	}

	// Slice 5: assert that the emitted body's name matches the canonical identity name.
	{
		StringHandle expected = effectiveLookupName(lazy_info.identity);
		StringHandle actual   = new_func_ref.decl_node().identifier_token().handle();
		if (actual != expected) {
			FLASH_LOG_FORMAT(Templates, Warning,
				"Slice 5 identity assertion: emitted body name '{}' != expected '{}' for {}::{}",
				StringTable::getStringView(actual),
				StringTable::getStringView(expected),
				StringTable::getStringView(lazy_info.identity.instantiated_owner_name),
				StringTable::getStringView(expected));
		}
	}

	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(lazy_info.identity.instantiated_owner_name))
		.append("::")
		.append(effectiveLookupName(lazy_info.identity));
	StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
	OuterTemplateBinding outer_binding;
	for (const auto& tp : lazy_info.template_params) {
		if (tp.is<TemplateParameterNode>()) {
			outer_binding.param_names.push_back(tp.as<TemplateParameterNode>().nameHandle());
		}
	}
	for (const auto& arg : lazy_info.template_args) {
		outer_binding.param_args.push_back(arg);
	}
	gTemplateRegistry.registerOuterTemplateBinding(qualified_name_handle, std::move(outer_binding));

	// Add the instantiated function to the AST so it gets visited during codegen
	// This is safe now that the StringBuilder bug is fixed
	ast_nodes_.push_back(new_func_node);
	
	// Also update the StructTypeInfo to replace the signature-only function with the full definition
	// Find the struct in gTypesByName
	auto struct_it = gTypesByName.find(lazy_info.identity.instantiated_owner_name);
	if (struct_it != gTypesByName.end()) {
		TypeInfo* struct_type_info = struct_it->second;
		StructTypeInfo* struct_info = struct_type_info->getStructInfo();
		if (struct_info) {
			// Find and update the member function
			for (auto& member_func : struct_info->member_functions) {
				if (member_func.getName() == effectiveLookupName(lazy_info.identity) &&
				    member_func.is_const() == lazy_info.identity.is_const_method) {
					// Replace with the instantiated function
					member_func.function_decl = new_func_node;
					FLASH_LOG(Templates, Debug, "Updated StructTypeInfo with instantiated function body");
					break;
				}
			}
		}
	}
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy member function: ", 
	          lazy_info.identity.instantiated_owner_name, "::", effectiveLookupName(lazy_info.identity));
	
	return new_func_node;
}

// Instantiate a lazy static member on-demand
// This is called when a static member is accessed for the first time
// Returns true if instantiation was performed, false if not needed or failed
bool Parser::instantiateLazyStaticMember(StringHandle instantiated_class_name, StringHandle member_name) {
	// Check if this member needs lazy instantiation
	if (!LazyStaticMemberRegistry::getInstance().needsInstantiation(instantiated_class_name, member_name)) {
		return false;  // Not registered for lazy instantiation
	}
	
	FLASH_LOG(Templates, Debug, "Lazy instantiation triggered for static member: ", 
	          instantiated_class_name, "::", member_name);
	
	// Get the lazy member info (returns a pointer to avoid copying)
	const LazyStaticMemberInfo* lazy_info_ptr = LazyStaticMemberRegistry::getInstance().getLazyStaticMemberInfo(
		instantiated_class_name, member_name);
	if (!lazy_info_ptr) {
		FLASH_LOG(Templates, Error, "Failed to get lazy static member info for: ", 
		          instantiated_class_name, "::", member_name);
		return false;
	}
	
	const LazyStaticMemberInfo& lazy_info = *lazy_info_ptr;
	
	// Find the struct_info to add the member to
	auto type_it = gTypesByName.find(instantiated_class_name);
	if (type_it == gTypesByName.end()) {
		FLASH_LOG(Templates, Error, "Failed to find struct info for: ", instantiated_class_name);
		return false;
	}
	
	StructTypeInfo* struct_info = type_it->second->getStructInfo();
	if (!struct_info) {
		FLASH_LOG(Templates, Error, "Type is not a struct: ", instantiated_class_name);
		return false;
	}
	
	// Perform initializer substitution if needed
	std::optional<ASTNode> substituted_initializer = lazy_info.initializer;
	
	if (lazy_info.needs_substitution && lazy_info.initializer.has_value() && 
	    lazy_info.initializer->is<ExpressionNode>()) {
		const ExpressionNode& expr = lazy_info.initializer->as<ExpressionNode>();
		const auto& template_params = lazy_info.template_params;
		const auto& template_args = lazy_info.template_args;
		
		// Helper to calculate pack size for substitution
		auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (!template_params[i].is<TemplateParameterNode>()) continue;
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == pack_name && tparam.is_variadic()) {
					size_t non_variadic_count = 0;
					for (const auto& param : template_params) {
						if (param.is<TemplateParameterNode>() && !param.as<TemplateParameterNode>().is_variadic()) {
							non_variadic_count++;
						}
					}
					return template_args.size() - non_variadic_count;
				}
			}
			return std::nullopt;
		};
		
		// Helper to create a numeric literal from pack size
		auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
			std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
			Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
			);
		};
		
		// Handle SizeofPackNode
		if (const auto* sizeof_pack_ptr = std::get_if<SizeofPackNode>(&expr)) {
			const SizeofPackNode& sizeof_pack = *sizeof_pack_ptr;
			if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
				substituted_initializer = make_pack_size_literal(*pack_size);
			}
		}
		// Handle FoldExpressionNode
		else if (std::holds_alternative<FoldExpressionNode>(expr)) {
			const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
			std::string_view pack_name = fold.pack_name();
			std::string_view op = fold.op();
			
			// Find the parameter pack
			std::optional<size_t> pack_param_idx;
			for (size_t p = 0; p < template_params.size(); ++p) {
				if (!template_params[p].is<TemplateParameterNode>()) continue;
				const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
				if (tparam.name() == pack_name && tparam.is_variadic()) {
					pack_param_idx = p;
					break;
				}
			}
			
			if (pack_param_idx.has_value()) {
				// Collect pack values
				std::vector<int64_t> pack_values;
				bool all_values_found = true;
				
				size_t non_variadic_count = 0;
				for (const auto& param : template_params) {
					if (param.is<TemplateParameterNode>() && !param.as<TemplateParameterNode>().is_variadic()) {
						non_variadic_count++;
					}
				}
				
				for (size_t i = non_variadic_count; i < template_args.size() && all_values_found; ++i) {
					if (template_args[i].is_value) {
						pack_values.push_back(template_args[i].value);
					} else {
						all_values_found = false;
					}
				}
				
				if (all_values_found && !pack_values.empty()) {
					auto fold_result = ConstExpr::evaluate_fold_expression(op, pack_values);
					if (fold_result.has_value()) {
						// Create a bool literal for && and ||, numeric for others
						if (op == "&&" || op == "||") {
							Token bool_token(Token::Type::Keyword, *fold_result ? "true"sv : "false"sv, 0, 0, 0);
							substituted_initializer = emplace_node<ExpressionNode>(
								BoolLiteralNode(bool_token, *fold_result != 0)
							);
						} else {
							std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(*fold_result)).commit();
							Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
							substituted_initializer = emplace_node<ExpressionNode>(
								NumericLiteralNode(num_token, static_cast<unsigned long long>(*fold_result), Type::Int, TypeQualifier::None, 64)
							);
						}
					}
				}
			}
		}
		// Handle TemplateParameterReferenceNode
		else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
			if (auto subst = substitute_nontype_template_param(
			        tparam_ref.param_name().view(), template_args, template_params)) {
				substituted_initializer = subst;
			}
		}
		// Handle IdentifierNode that might be a template parameter
		else if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
			if (auto subst = substitute_nontype_template_param(
			        id_node.name(), template_args, template_params)) {
				substituted_initializer = subst;
			}
		}
		
		// General fallback: Use ExpressionSubstitutor for any remaining template-dependent expressions
		// This handles expressions like __v<T> (variable template invocations with template parameters)
		// Check if we still have the original initializer (i.e., no specific handler above modified it)
		bool was_substituted = false;
		if (std::holds_alternative<FoldExpressionNode>(expr)) was_substituted = true;
		if (std::holds_alternative<SizeofPackNode>(expr)) was_substituted = true;
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) was_substituted = true;
		// IdentifierNode only gets substituted if it matches a template parameter
		
		if (!was_substituted) {
			// Use ExpressionSubstitutor for general template parameter substitution
			std::unordered_map<std::string_view, TemplateTypeArg> param_map;
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					param_map[param.name()] = template_args[i];
				}
			}
			
			if (!param_map.empty()) {
				ExpressionSubstitutor substitutor(param_map, *this);
				substituted_initializer = substitutor.substitute(lazy_info.initializer.value());
				FLASH_LOG(Templates, Debug, "Applied general template parameter substitution to lazy static member initializer");
			}

			if (substituted_initializer.has_value()) {
				substituted_initializer = rebindStaticMemberInitializerFunctionCalls(
					substituted_initializer.value(),
					struct_info);
			}

			// Try to evaluate the substituted expression to a constant value.
			// This turns expressions like "1 * __static_sign$hash::value / __static_gcd$hash::value"
			// into a single NumericLiteralNode, enabling downstream constexpr evaluation.
			if (substituted_initializer.has_value() && substituted_initializer->is<ExpressionNode>()) {
				ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
				eval_ctx.parser = this;
				// Provide struct context so the evaluator prefers same-struct member functions over globals.
				eval_ctx.struct_info = struct_info;
				// Provide template args so sizeof(T) etc. resolve correctly.
				for (size_t i = 0; i < lazy_info.template_params.size() && i < lazy_info.template_args.size(); ++i) {
					if (lazy_info.template_params[i].is<TemplateParameterNode>()) {
						eval_ctx.template_param_names.push_back(lazy_info.template_params[i].as<TemplateParameterNode>().name());
						eval_ctx.template_args.push_back(lazy_info.template_args[i]);
					}
				}
				auto eval_result = ConstExpr::Evaluator::evaluate(*substituted_initializer, eval_ctx);
				if (eval_result.success()) {
					int64_t val = eval_result.as_int();
					if (val < 0) {
						// For negative values, create UnaryOperator('-', NumericLiteral(abs(val)))
						// to avoid uint64_t cast producing wrong string/value
						std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(-static_cast<uint64_t>(val))).commit();
						Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
						auto& literal_node = emplace_node_ref<ExpressionNode>(
							NumericLiteralNode(num_token, static_cast<unsigned long long>(-static_cast<uint64_t>(val)), Type::Int, TypeQualifier::None, 64)).second;
						Token minus_token(Token::Type::Operator, "-"sv, 0, 0, 0);
						substituted_initializer = emplace_node<ExpressionNode>(
							UnaryOperatorNode(minus_token, ASTNode(&literal_node), true, false));
					} else {
						std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(val)).commit();
						Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
						substituted_initializer = emplace_node<ExpressionNode>(
							NumericLiteralNode(num_token, static_cast<unsigned long long>(val), Type::Int, TypeQualifier::None, 64));
					}
					FLASH_LOG(Templates, Debug, "Evaluated lazy static member initializer to constant: ", val);
				}
			}
		}
	}
	
	// Perform type substitution
	TypeSpecifierNode original_type_spec(lazy_info.type, TypeQualifier::None, lazy_info.size * 8);
	original_type_spec.set_type_index(lazy_info.type_index);
	
	auto [substituted_type, substituted_type_index] = substitute_template_parameter(
		original_type_spec, lazy_info.template_params, lazy_info.template_args);
	
	size_t substituted_size = get_type_size_bits(substituted_type) / 8;
	
	// Update the existing static member with the computed initializer
	// (The member was already added during template instantiation with std::nullopt initializer)
	if (!struct_info->updateStaticMemberInitializer(lazy_info.member_name, substituted_initializer)) {
		// Member doesn't exist yet - add it (shouldn't normally happen with lazy instantiation)
		struct_info->addStaticMember(
			lazy_info.member_name,
			substituted_type,
			substituted_type_index,
			substituted_size,
			lazy_info.alignment,
			lazy_info.access,
			substituted_initializer,
			lazy_info.cv_qualifier,
			lazy_info.reference_qualifier,
			lazy_info.pointer_depth
		);
	}
	
	// Mark as instantiated (remove from lazy registry)
	LazyStaticMemberRegistry::getInstance().markInstantiated(instantiated_class_name, member_name);
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy static member: ", 
	          instantiated_class_name, "::", member_name);
	
	return true;
}

// Phase 2: Instantiate a lazy class to the specified phase
// Returns true if instantiation was performed or already at/past target phase, false on failure
bool Parser::instantiateLazyClassToPhase(StringHandle instantiated_name, ClassInstantiationPhase target_phase) {
	auto& registry = LazyClassInstantiationRegistry::getInstance();
	
	// Check if the class is registered for lazy instantiation
	if (!registry.isRegistered(instantiated_name)) {
		// Not a lazily instantiated class - might be already fully instantiated or not a template
		return true;
	}
	
	// Check if already at or past target phase
	ClassInstantiationPhase current_phase = registry.getCurrentPhase(instantiated_name);
	if (static_cast<uint8_t>(current_phase) >= static_cast<uint8_t>(target_phase)) {
		return true;  // Already done
	}
	
	const LazyClassInstantiationInfo* lazy_info = registry.getLazyClassInfo(instantiated_name);
	if (!lazy_info) {
		FLASH_LOG(Templates, Error, "Failed to get lazy class info for: ", instantiated_name);
		return false;
	}
	
	FLASH_LOG(Templates, Debug, "Instantiating lazy class '", instantiated_name, 
	          "' from phase ", static_cast<int>(current_phase),
	          " to phase ", static_cast<int>(target_phase));
	
	// Phase A -> B transition: Compute size and alignment
	if (current_phase < ClassInstantiationPhase::Layout && 
	    target_phase >= ClassInstantiationPhase::Layout) {
		
		// Look up the type info
		auto type_it = gTypesByName.find(instantiated_name);
		if (type_it == gTypesByName.end()) {
			FLASH_LOG(Templates, Error, "Type not found in gTypesByName: ", instantiated_name);
			return false;
		}
		
		// Get the StructTypeInfo and ensure layout is computed
		// Note: Layout computation happens during try_instantiate_class_template 
		// when the struct_info is created, so this phase is mostly about
		// ensuring members have been processed for size computation
		const TypeInfo* type_info = type_it->second;
		if (type_info->isStruct()) {
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info) {
				// Layout is already computed during minimal instantiation
				// Just verify it's valid
				if (struct_info->total_size == 0 && !struct_info->members.empty()) {
					FLASH_LOG(Templates, Warning, "Struct has members but zero size: ", instantiated_name);
				}
			}
		}
		
		registry.updatePhase(instantiated_name, ClassInstantiationPhase::Layout);
		current_phase = ClassInstantiationPhase::Layout;
		
		FLASH_LOG(Templates, Debug, "Completed Layout phase for: ", instantiated_name);
	}
	
	// Phase B -> C transition: Instantiate all members and base classes
	if (current_phase < ClassInstantiationPhase::Full && 
	    target_phase >= ClassInstantiationPhase::Full) {
		
		// Force instantiate all static members
		auto type_it = gTypesByName.find(instantiated_name);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			const StructTypeInfo* struct_info = type_it->second->getStructInfo();
			if (struct_info) {
				// Trigger lazy instantiation of all static members
				for (const auto& static_member : struct_info->static_members) {
					if (!static_member.initializer.has_value()) {
						// May need lazy instantiation
						instantiateLazyStaticMember(instantiated_name, static_member.name);
					}
				}
			}
		}
		
		// Mark as fully instantiated
		registry.markFullyInstantiated(instantiated_name);
		
		FLASH_LOG(Templates, Debug, "Completed Full phase for: ", instantiated_name);
	}
	
	return true;
}

// Phase 3: Evaluate a lazy type alias on-demand
// Returns the evaluated type and type index, or nullopt if not found/failed
std::optional<std::pair<Type, TypeIndex>> Parser::evaluateLazyTypeAlias(
	StringHandle instantiated_class_name, StringHandle member_name) {
	
	auto& registry = LazyTypeAliasRegistry::getInstance();
	
	// Check for cached result first
	auto cached = registry.getCachedResult(instantiated_class_name, member_name);
	if (cached.has_value()) {
		FLASH_LOG(Templates, Debug, "Using cached type alias result for: ", 
		          instantiated_class_name, "::", member_name);
		return cached;
	}
	
	// Get the lazy alias info (nullptr if not registered)
	const LazyTypeAliasInfo* lazy_info = registry.getLazyTypeAliasInfo(instantiated_class_name, member_name);
	if (!lazy_info) {
		return std::nullopt;  // Not registered for lazy evaluation
	}
	
	FLASH_LOG(Templates, Debug, "Evaluating lazy type alias: ", 
	          instantiated_class_name, "::", member_name);
	
	// Evaluate the type alias by substituting template parameters
	if (!lazy_info->unevaluated_target.is<TypeSpecifierNode>()) {
		FLASH_LOG(Templates, Error, "Lazy type alias target is not a TypeSpecifierNode: ", 
		          instantiated_class_name, "::", member_name);
		return std::nullopt;
	}
	
	const TypeSpecifierNode& target_type = lazy_info->unevaluated_target.as<TypeSpecifierNode>();
	
	// Perform template parameter substitution
	auto [substituted_type, substituted_type_index] = substitute_template_parameter(
		target_type, lazy_info->template_params, lazy_info->template_args);
	
	// Cache the result
	registry.markEvaluated(instantiated_class_name, member_name, substituted_type, substituted_type_index);
	
	FLASH_LOG(Templates, Debug, "Successfully evaluated lazy type alias: ", 
	          instantiated_class_name, "::", member_name, 
	          " -> type=", static_cast<int>(substituted_type), ", index=", substituted_type_index);
	
	return std::make_pair(substituted_type, substituted_type_index);
}

// Phase 4: Instantiate a lazy nested type on-demand
// Returns the type index of the instantiated nested type, or nullopt if not found/failed
std::optional<TypeIndex> Parser::instantiateLazyNestedType(
	StringHandle parent_class_name, StringHandle nested_type_name) {
	
	auto& registry = LazyNestedTypeRegistry::getInstance();
	
	// Get the lazy nested type info (nullptr if not registered or already instantiated)
	const LazyNestedTypeInfo* lazy_info = registry.getLazyNestedTypeInfo(parent_class_name, nested_type_name);
	if (!lazy_info) {
		return std::nullopt;  // Not registered for lazy instantiation (or already instantiated)
	}
	
	FLASH_LOG(Templates, Debug, "Instantiating lazy nested type: ", 
	          parent_class_name, "::", nested_type_name);
	
	// Get the nested type declaration
	if (!lazy_info->nested_type_declaration.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Lazy nested type declaration is not a StructDeclarationNode: ", 
		          parent_class_name, "::", nested_type_name);
		return std::nullopt;
	}
	
	const StructDeclarationNode& nested_struct = lazy_info->nested_type_declaration.as<StructDeclarationNode>();
	
	// Create the qualified name for the nested type
	std::string_view qualified_name = StringTable::getStringView(lazy_info->qualified_name);
	
	// Check if type already exists (may have been instantiated through another path)
	auto existing_type_it = gTypesByName.find(lazy_info->qualified_name);
	if (existing_type_it != gTypesByName.end()) {
		TypeIndex existing_index = existing_type_it->second->type_index_;
		registry.markInstantiated(parent_class_name, nested_type_name);
		return existing_index;
	}
	
	// Derive the declaration-site namespace from the parent class name, not the
	// nested type's qualified_name. The qualified_name (e.g., "ns::Container$hash::Inner")
	// would create a bogus "Container$hash" namespace via fromQualifiedName. Instead,
	// look up the parent class's NamespaceHandle which correctly gives "ns".
	NamespaceHandle decl_ns = gSymbolTable.get_current_namespace_handle();
	{
		auto parent_it = gTypesByName.find(lazy_info->parent_class_name);
		if (parent_it != gTypesByName.end()) {
			NamespaceHandle parent_ns = parent_it->second->namespaceHandle();
			if (parent_ns.isValid()) {
				decl_ns = parent_ns;
			}
		}
	}

	// Create a new struct type for the nested class
	TypeInfo& nested_type_info = add_struct_type(lazy_info->qualified_name, decl_ns);
	TypeIndex type_index = nested_type_info.type_index_;
	
	// Create StructTypeInfo for the nested type
	auto nested_struct_info = std::make_unique<StructTypeInfo>(lazy_info->qualified_name, nested_struct.default_access(), nested_struct.is_union(), decl_ns);
	
	// Process members with template parameter substitution
	for (const auto& member_decl : nested_struct.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		
		// Substitute template parameters using parent's template args
		auto [substituted_type, substituted_type_index] = substitute_template_parameter(
			type_spec, lazy_info->parent_template_params, lazy_info->parent_template_args);
		
		// Get size for the member
		size_t member_size = 0;
		if (substituted_type_index.value < gTypeInfo.size()) {
			const TypeInfo& member_type_info = gTypeInfo[substituted_type_index.value];
			if (member_type_info.getStructInfo()) {
				member_size = member_type_info.getStructInfo()->total_size;
			} else {
				member_size = get_type_size_bits(substituted_type) / 8;
			}
		} else {
			member_size = get_type_size_bits(substituted_type) / 8;
		}
		
		// Get alignment for the member
		size_t member_alignment = member_size > 0 ? member_size : 1;
		if (substituted_type_index.value < gTypeInfo.size()) {
			const TypeInfo& member_type_info = gTypeInfo[substituted_type_index.value];
			if (member_type_info.getStructInfo()) {
				member_alignment = member_type_info.getStructInfo()->alignment;
			}
		}
		
		// Get the name from the identifier token
		StringHandle member_name_handle = decl.identifier_token().handle();
		
		// Add member to nested struct info
		nested_struct_info->addMember(
			member_name_handle,
			substituted_type,
			substituted_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			std::nullopt,  // No default initializer for now
			type_spec.reference_qualifier(),
			member_size * 8,
			false,  // is_array
			{},     // array_dimensions
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width
		);
	}
	
	// Finalize layout
	nested_struct_info->finalize();
	
	// Process member functions: register for lazy instantiation and add signatures to StructTypeInfo.
	// Uses the shared helper defined in Parser_Templates_Inst_ClassTemplate.cpp (included first
	// in the unity build, so it is visible here).
	registerNestedMemberFunctionsForLazy(nested_struct, *nested_struct_info,
		lazy_info->parent_class_name, lazy_info->qualified_name,
		lazy_info->parent_template_params, lazy_info->parent_template_args);
	
	// Set the struct info on the type
	nested_type_info.struct_info_ = std::move(nested_struct_info);
	
	// Mark as instantiated (removes from lazy registry)
	registry.markInstantiated(parent_class_name, nested_type_name);
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy nested type: ", 
	          qualified_name, " (type_index=", type_index, ")");
	
	return type_index;
}

// Try to parse an out-of-line template member function definition
// Pattern: template<typename T> ReturnType ClassName<T>::functionName(...) { ... }
// Returns true if successfully parsed, false if not an out-of-line definition
