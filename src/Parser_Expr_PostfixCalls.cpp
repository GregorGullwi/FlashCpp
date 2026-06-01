#include "Parser.h"
#include "CallNodeHelpers.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"
#include <limits>

// Helper: resolve object type from expression and try to instantiate member function template.
// Uses get_expression_type() to handle any expression (identifiers, function calls, member access, etc.).
// Returns the instantiated function node if successful, nullopt otherwise.
std::optional<ASTNode> Parser::tryResolveMemberFunctionTemplate(
	const std::optional<ASTNode>& object_expr, std::string_view member_name,
	const std::optional<InlineVector<TemplateTypeArg, 4>>& explicit_template_args,
	std::span<const TypeSpecifierNode> arg_types) {
	if (!object_expr.has_value())
		return std::nullopt;
	auto type_opt = get_expression_type(*object_expr);
	if (!type_opt.has_value())
		return std::nullopt;
	const auto& type_spec = *type_opt;
	if (!is_struct_type(type_spec.category()))
		return std::nullopt;
	TypeIndex type_idx = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_idx);
	if (!type_info)
		return std::nullopt;
	if (type_info->is_incomplete_instantiation_) {
		instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
		type_info = findTypeByName(type_info->name());
		if (!type_info || type_info->is_incomplete_instantiation_) {
			return std::nullopt;
		}
	}
	auto struct_name = StringTable::getStringView(type_info->name());
	instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
	if (explicit_template_args.has_value()) {
		// Propagate call argument types so overload resolution can reject mismatched
		// overloads (e.g. value vs pointer).  Without this, the first successful
		// overload candidate is always returned regardless of argument compatibility.
		FlashCpp::ScopedState guard_explicit_call_arg_types(current_explicit_call_arg_types_);
		if (!arg_types.empty()) {
			current_explicit_call_arg_types_ = &arg_types;
		}
		return try_instantiate_member_function_template_explicit(struct_name, member_name, *explicit_template_args);
	} else if (!arg_types.empty()) {
		return try_instantiate_member_function_template(struct_name, member_name, arg_types);
	}
	return std::nullopt;
}

const FunctionDeclarationNode* Parser::tryResolveConcreteMemberFunction(
	const std::optional<ASTNode>& object_expr, std::string_view member_name) {
	if (!object_expr.has_value())
		return nullptr;
	auto type_opt = get_expression_type(*object_expr);
	if (!type_opt.has_value())
		return nullptr;
	const auto& type_spec = *type_opt;
	if (!is_struct_type(type_spec.category()))
		return nullptr;
	TypeIndex type_idx = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_idx);
	if (!type_info)
		return nullptr;
	if (type_info->is_incomplete_instantiation_) {
		instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
		type_info = findTypeByName(type_info->name());
		if (!type_info || type_info->is_incomplete_instantiation_) {
			return nullptr;
		}
	}

	StringHandle type_name = type_info->name();
	instantiateLazyClassToPhase(type_name, ClassInstantiationPhase::Full);
	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info)
		return nullptr;

	StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
	const FunctionDeclarationNode* match = nullptr;
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.is_constructor || member_func.is_destructor)
			continue;
		if (member_func.getName() != member_name_handle)
			continue;
		if (!member_func.function_decl.is<FunctionDeclarationNode>())
			continue;

		const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
		// Keep deferred-body concrete members viable for overload selection.
		// Their bodies are materialized lazily via LazyMemberInstantiationRegistry.
		if (candidate.failed_substitution()) {
			continue;
		}
		if (match) {
			return nullptr;
		}
		match = &candidate;
	}

	if (!match) {
		auto [base_member_func, owner_struct] = struct_info->findMemberFunctionRecursive(member_name_handle);
		if (base_member_func != nullptr && owner_struct != nullptr && owner_struct != struct_info &&
			!base_member_func->is_constructor && !base_member_func->is_destructor &&
			base_member_func->function_decl.is<FunctionDeclarationNode>()) {
			const auto& candidate = base_member_func->function_decl.as<FunctionDeclarationNode>();
			if (!candidate.failed_substitution()) {
				match = &candidate;
			}
		}
	}

	return match;
}

const FunctionDeclarationNode* Parser::tryResolveConcreteCallOperator(
	const std::optional<ASTNode>& object_expr,
	std::span<const TypeSpecifierNode> arg_types,
	size_t argument_count,
	bool all_arg_types_known) {
	if (!object_expr.has_value())
		return nullptr;
	auto type_opt = get_expression_type(*object_expr);
	if (!type_opt.has_value())
		return nullptr;
	const auto& type_spec = *type_opt;
	if (!is_struct_type(type_spec.category()))
		return nullptr;
	TypeIndex type_idx = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_idx);
	if (!type_info)
		return nullptr;
	if (type_info->is_incomplete_instantiation_) {
		instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
		type_info = findTypeByName(type_info->name());
		if (!type_info || type_info->is_incomplete_instantiation_) {
			return nullptr;
		}
	}

	instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info)
		return nullptr;

	std::vector<ASTNode> call_candidates;
	std::unordered_set<const StructTypeInfo*> visited;
	auto collect_visible_call_operators = [&](const StructTypeInfo* current_struct, const auto& self) -> void {
		if (!current_struct || !visited.insert(current_struct).second) {
			return;
		}

		bool has_local_call_operator = false;
		for (const auto& member_func : current_struct->member_functions) {
			if (member_func.operator_kind != OverloadableOperator::Call ||
				!member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}
			const auto& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
			if (candidate.failed_substitution()) {
				continue;
			}
			has_local_call_operator = true;
			call_candidates.push_back(member_func.function_decl);
		}
		if (has_local_call_operator) {
			return;
		}

		for (const auto& base_spec : current_struct->base_classes) {
			if (const StructTypeInfo* base_struct = tryGetStructTypeInfo(base_spec.type_index)) {
				self(base_struct, self);
			}
		}
	};
	collect_visible_call_operators(struct_info, collect_visible_call_operators);

	if (call_candidates.empty()) {
		return nullptr;
	}

	if (all_arg_types_known) {
		const OverloadResolutionResult op_result = resolve_overload(call_candidates, arg_types);
		if (op_result.has_match && !op_result.is_ambiguous && op_result.selected_overload != nullptr) {
			return &op_result.selected_overload->as<FunctionDeclarationNode>();
		}
		if (op_result.is_ambiguous) {
			return nullptr;
		}
	}

	const FunctionDeclarationNode* same_arity_candidate = nullptr;
	bool ambiguous_same_arity = false;
	for (const ASTNode& candidate_node : call_candidates) {
		const auto& candidate = candidate_node.as<FunctionDeclarationNode>();
		const size_t min_required = countMinRequiredArgs(candidate);
		const size_t max_accepted = candidate.is_variadic()
			? std::numeric_limits<size_t>::max()
			: candidate.parameter_nodes().size();
		if (argument_count < min_required || argument_count > max_accepted) {
			continue;
		}
		if (same_arity_candidate != nullptr) {
			ambiguous_same_arity = true;
			break;
		}
		same_arity_candidate = &candidate;
	}
	if (same_arity_candidate != nullptr && !ambiguous_same_arity) {
		return same_arity_candidate;
	}
	if (call_candidates.size() == 1) {
		return &call_candidates.front().as<FunctionDeclarationNode>();
	}

	if (all_arg_types_known) {
		if (std::optional<ASTNode> instantiated_operator = tryResolveMemberFunctionTemplate(
				object_expr,
				"operator()"sv,
				std::nullopt,
				arg_types);
			instantiated_operator.has_value() &&
			instantiated_operator->is<FunctionDeclarationNode>()) {
			return &instantiated_operator->as<FunctionDeclarationNode>();
		}
	}

	return nullptr;
}

void Parser::finalizePostfixCallExpression(
	std::optional<ASTNode>& result,
	const Token& paren_token,
	ChunkedVector<ASTNode>&& args,
	std::vector<TypeSpecifierNode>&& arg_types) {
	if (result->is<ExpressionNode>()) {
		const ExpressionNode& expr = result->as<ExpressionNode>();
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			const auto& member_access = std::get<MemberAccessNode>(expr);
			bool is_function_pointer_call = false;
			if (!member_function_context_stack_.empty()) {
				const auto& member_ctx = member_function_context_stack_.back();
				if (const TypeInfo* struct_type_info = tryGetTypeInfo(member_ctx.struct_type_index)) {
					if (const StructTypeInfo* struct_info = struct_type_info->getStructInfo()) {
						std::string_view member_name = member_access.member_name();
						for (const auto& member : struct_info->members) {
							if (member.getName() == StringTable::getOrInternStringHandle(member_name)) {
								is_function_pointer_call = member.type_index.category() == TypeCategory::FunctionPointer;
								break;
							}
						}
					}
				}
			}

			if (is_function_pointer_call) {
				Token member_token = member_access.member_token();
				auto temp_type = emplace_node<TypeSpecifierNode>(TypeCategory::Int, TypeQualifier::None, 32, member_token, CVQualifier::None);
				auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_token);
				[[maybe_unused]] auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());
				result = emplace_node<ExpressionNode>(
					makeResolvedMemberCallExpr(*result, func_ref, std::move(args), member_token));
				return;
			}
		}

		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			if (unary_op.op() == "&" && unary_op.is_prefix()) {
				const ASTNode& operand = unary_op.get_operand();
				if (operand.is<ExpressionNode>()) {
					const ExpressionNode& op_expr = operand.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(op_expr)) {
						const auto& id = std::get<IdentifierNode>(op_expr);
						auto sym = gSymbolTable.lookup(id.name());
						if (sym.has_value() && sym->is<FunctionDeclarationNode>()) {
							const auto& func_decl = sym->as<FunctionDeclarationNode>();
							result = emplace_node<ExpressionNode>(
								makeResolvedCallExpr(func_decl, std::move(args), paren_token));
							if (func_decl.has_mangled_name()) {
								setCallMangledName(result->as<ExpressionNode>(), func_decl.mangled_name());
							}
							return;
						}
					}
				}
			}
		}
	}

	Token operator_token(
		Token::Type::Identifier,
		"operator()"sv,
		paren_token.line(),
		paren_token.column(),
		paren_token.file_index());
	const bool all_arg_types_known = arg_types.size() == args.size();
	const FunctionDeclarationNode* func_ref = tryResolveConcreteCallOperator(
		result,
		arg_types,
		args.size(),
		all_arg_types_known);
	if (!func_ref) {
		// Keep the deferred member-call representation for unresolved/dependent
		// callable objects so sema can handle the remaining compatibility cases.
		auto temp_type = emplace_node<TypeSpecifierNode>(TypeCategory::Int, TypeQualifier::None, 32, operator_token, CVQualifier::None);
		auto temp_decl = emplace_node<DeclarationNode>(temp_type, operator_token);
		func_ref = &emplace_node<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>()).as<FunctionDeclarationNode>();
	}

	result = emplace_node<ExpressionNode>(
		makeResolvedMemberCallExpr(*result, *func_ref, std::move(args), operator_token));
}

std::optional<ASTNode> Parser::tryInstantiateMemberFunctionTemplateCall(
	std::string_view struct_name,
	std::string_view member_name,
	const std::optional<InlineVector<TemplateTypeArg, 4>>& explicit_template_args,
	std::span<const TypeSpecifierNode> call_arg_types,
	bool has_call_args,
	bool has_dependent_call_args) {
	// `has_call_args` tracks whether the parser saw any call arguments syntactically,
	// while `call_arg_types` tracks only the arguments whose types are already known
	// at this parse point and may therefore be only a partial subset.  If a
	// syntactically non-empty call still has no collected types here, preserving the
	// old behavior by deferring deduction is safer than forcing a hard failure.
	if (explicit_template_args.has_value()) {
		// Propagate call argument types so that explicit member template calls
		// (e.g. outer.pick<char>(&nine)) can discriminate between overloads that
		// differ only in pointer depth.  Without this, the value overload would
		// always be chosen over the pointer overload because the overload loop
		// returns on the first successfully-instantiated candidate.
		FlashCpp::ScopedState guard_explicit_call_arg_types(current_explicit_call_arg_types_);
		if (!call_arg_types.empty()) {
			current_explicit_call_arg_types_ = &call_arg_types;
		}
		return try_instantiate_member_function_template_explicit(
			struct_name,
			member_name,
			*explicit_template_args);
	}
	if (!has_call_args) {
		return try_instantiate_member_function_template_explicit(
			struct_name,
			member_name,
			{});
	}
	if (has_dependent_call_args) {
		return std::nullopt;
	}
	if (call_arg_types.empty()) {
		std::string_view failure_reason = StringBuilder()
			.append("cannot instantiate member function template '")
			.append(struct_name)
			.append("::")
			.append(member_name)
			.append("' because no concrete call argument types were collected")
			.commit();
		FLASH_LOG(Templates, Debug, failure_reason);
		return failTemplateInstantiation(failure_reason, nullptr, std::nullopt);
	}
	return try_instantiate_member_function_template(
		struct_name,
		member_name,
		call_arg_types);
}

ParseResult Parser::parse_member_postfix(std::optional<ASTNode>& result, const Token& operator_start_token) {
	// Expect an identifier (member name) OR ~ for pseudo-destructor call
	// Pseudo-destructor pattern: obj.~Type() or ptr->~Type()
	bool is_arrow_access = (operator_start_token.kind() == "->"_tok);

	if (peek() == "~"_tok) {
		advance(); // consume '~'

		if (!peek().is_identifier()) {
			return ParseResult::error("Expected type name after '~' in pseudo-destructor call", current_token_);
		}

		Token destructor_type_token = peek_info();
		advance(); // consume type name

		std::string qualified_type_name(destructor_type_token.value());
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected identifier after '::' in pseudo-destructor type", current_token_);
			}
			qualified_type_name += "::";
			qualified_type_name += peek_info().value();
			advance(); // consume identifier
		}

		if (peek() == "<"_tok) {
			skip_template_arguments();
		}

		if (peek() != "("_tok) {
			return ParseResult::error("Expected '(' after destructor name", current_token_);
		}
		advance(); // consume '('

		if (peek() != ")"_tok) {
			return ParseResult::error("Expected ')' - pseudo-destructor takes no arguments", current_token_);
		}
		advance(); // consume ')'

		FLASH_LOG(Parser, Debug, "Parsed pseudo-destructor call: ~", qualified_type_name);
		result = emplace_node<ExpressionNode>(
			PseudoDestructorCallNode(*result, qualified_type_name, destructor_type_token, is_arrow_access));
		return ParseResult::success(*result);
	}

	// Handle member operator call syntax: obj.operator<=>(...) or ptr->operator++(...)
	if (peek() == "operator"_tok) {
		Token operator_keyword_token = peek_info();
		advance(); // consume 'operator'

		std::string_view operator_name;
		if (auto err = parse_operator_name(operator_keyword_token, operator_name)) {
			return std::move(*err);
		}

		Token member_operator_name_token(
			Token::Type::Identifier,
			operator_name,
			operator_keyword_token.line(),
			operator_keyword_token.column(),
			operator_keyword_token.file_index());

		if (peek() != "("_tok) {
			return ParseResult::error("Expected '(' after operator name in member operator call", current_token_);
		}
		advance(); // consume '('

		auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
			.handle_pack_expansion = true,
			.collect_types = true,
			.expand_simple_packs = false});
		if (!args_result.success) {
			return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
		}
		ChunkedVector<ASTNode> args = std::move(args_result.args);

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after member operator call arguments", current_token_);
		}

		const FunctionDeclarationNode* known_member_func =
			tryResolveConcreteMemberFunction(result, operator_name);
		FunctionDeclarationNode* func_ref_ptr = nullptr;
		if (known_member_func) {
			const size_t min_required = countMinRequiredArgs(*known_member_func);
			const size_t max_accepted = known_member_func->is_variadic()
				? std::numeric_limits<size_t>::max()
				: known_member_func->parameter_nodes().size();
			if (args.size() < min_required || args.size() > max_accepted) {
				return ParseResult::error(
					"No matching member function for call to '" + std::string(operator_name) + "'",
					member_operator_name_token);
			}
			func_ref_ptr = const_cast<FunctionDeclarationNode*>(known_member_func);
		} else {
			if (isHardUseLikeInstantiationMode()) {
				// Reuse existing concrete-member resolution signal: in hard-use contexts,
				// when parser knows the concrete receiver type but cannot resolve a
				// matching member operator call here, report a user-facing error instead
				// of creating a placeholder callee node.
				if (auto object_type_opt = get_expression_type(*result);
					object_type_opt.has_value() &&
					is_struct_type(object_type_opt->category())) {
					const TypeInfo* object_type_info = tryGetTypeInfo(object_type_opt->type_index());
					if (object_type_info &&
						!object_type_info->is_incomplete_instantiation_ &&
						!object_type_info->isDependentPlaceholder()) {
						if (const StructTypeInfo* object_struct_info = object_type_info->getStructInfo()) {
							const StringHandle operator_name_handle =
								StringTable::getOrInternStringHandle(operator_name);
							auto [known_operator, owner_struct] =
								object_struct_info->findMemberFunctionRecursive(operator_name_handle);
							if (!known_operator || !owner_struct) {
								return ParseResult::error(
									"No matching member function for call to '" + std::string(operator_name) + "'",
									member_operator_name_token);
							}
						}
					}
				}
			}
			auto type_spec = emplace_node<TypeSpecifierNode>(
				TypeIndex{}.withCategory(TypeCategory::Auto),
				0,
				member_operator_name_token,
				CVQualifier::None,
				ReferenceQualifier::None);
			auto& operator_decl =
				emplace_node<DeclarationNode>(type_spec, member_operator_name_token)
					.as<DeclarationNode>();
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(operator_decl);
			func_ref_ptr = &func_ref;
		}

		result = emplace_node<ExpressionNode>(
			makeResolvedMemberCallExpr(*result, *func_ref_ptr, std::move(args), member_operator_name_token));
		if (func_ref_ptr->has_mangled_name()) {
			setCallMangledName(result->as<ExpressionNode>(), func_ref_ptr->mangled_name());
		}
		if (func_ref_ptr->is_member_function() &&
			!func_ref_ptr->parent_struct_name().empty()) {
			StringBuilder qualified_name_builder;
			qualified_name_builder
				.append(func_ref_ptr->parent_struct_name())
				.append("::")
				.append(func_ref_ptr->decl_node().identifier_token().value());
			setCallQualifiedName(result->as<ExpressionNode>(), qualified_name_builder.commit());
		}
		return ParseResult::success(*result);
	}

	if (peek() == "template"_tok)
		advance();

	if (!peek().is_identifier()) {
		return ParseResult::error(
			std::string(StringBuilder().append("Expected member name after '").append(operator_start_token.value()).append("'").commit()),
			operator_start_token);
	}

	Token member_name_token = peek_info();
	advance(); // consume member name

	TemplateTypeArgParsingResult explicit_template_args = parse_explicit_template_arguments_as_result(TokenDestroyPattern::Restore);

	if (peek() == "("_tok) {
		explicit_template_args.destroy_pattern_ = TokenDestroyPattern::Discard;

		const FunctionDeclarationNode* known_member_func = nullptr;
		if (!explicit_template_args) {
			known_member_func = tryResolveConcreteMemberFunction(result, member_name_token.value());
		}
		// Note: when explicit_template_args is set, we do NOT do an early instantiation
		// here (before parsing arguments), because the result would be discarded and —
		// more importantly — it would cache the FIRST overload candidate, preventing the
		// later call (tryInstantiateMemberFunctionTemplateCall below, which has the actual
		// call-argument types) from selecting the correct overload.

		advance(); // consume '('

		auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
			.handle_pack_expansion = true,
			.collect_types = true,
			.expand_simple_packs = false,
			.callee_decl = known_member_func});
		if (!args_result.success) {
			return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
		}
		ChunkedVector<ASTNode> args = std::move(args_result.args);
		std::vector<TypeSpecifierNode> arg_types = std::move(args_result.arg_types);

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after function call arguments", current_token_);
		}

		std::optional<std::string_view> object_struct_name;
		if (auto type_opt = get_expression_type(*result); type_opt.has_value() &&
													  is_struct_type(type_opt->category())) {
			TypeIndex type_idx = type_opt->type_index();
			if (type_idx.is_valid()) {
				if (const TypeInfo* type_info = tryGetTypeInfo(type_idx)) {
					if (!type_info->is_incomplete_instantiation_) {
						object_struct_name = StringTable::getStringView(type_info->name());
						instantiateLazyClassToPhase(type_info->name(), ClassInstantiationPhase::Full);
					}
				}
			}
		}

		if (template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe &&
			object_struct_name.has_value() && !sfinae_type_map_.empty()) {
			StringHandle obj_name_handle = StringTable::getOrInternStringHandle(*object_struct_name);
			auto subst_it = sfinae_type_map_.find(obj_name_handle);
			if (subst_it != sfinae_type_map_.end()) {
				TypeIndex concrete_idx = subst_it->second;
				if (const TypeInfo* concrete_type = tryGetTypeInfo(concrete_idx)) {
					object_struct_name = StringTable::getStringView(concrete_type->name());
				}
			}
			bool member_found = false;
			for (auto& node : ast_nodes_) {
				if (node.is<StructDeclarationNode>()) {
					auto& sn = node.as<StructDeclarationNode>();
					if (sn.name() == *object_struct_name) {
						for (const auto& member : sn.members()) {
							if (member.declaration.is<DeclarationNode>()) {
								if (member.declaration.as<DeclarationNode>().identifier_token().value() == member_name_token.value()) {
									member_found = true;
									break;
								}
							}
						}
						if (!member_found) {
							for (const auto& mf : sn.member_functions()) {
								if (mf.is_constructor || mf.is_destructor)
									continue;
								if (mf.function_declaration.is<FunctionDeclarationNode>()) {
									const auto& func = mf.function_declaration.as<FunctionDeclarationNode>();
									if (func.decl_node().identifier_token().value() == member_name_token.value()) {
										member_found = true;
										break;
									}
								}
							}
						}
						break;
					}
				}
			}
			if (!member_found) {
				return ParseResult::error("SFINAE: member not found on concrete type", member_name_token);
			}
		}

		std::optional<ASTNode> instantiated_func;
		if (object_struct_name.has_value()) {
			std::optional<InlineVector<TemplateTypeArg, 4>> member_template_args;
			if (explicit_template_args) {
				member_template_args = explicit_template_args.read_template_type_args();
			}
			instantiated_func = tryInstantiateMemberFunctionTemplateCall(
				*object_struct_name,
				member_name_token.value(),
				member_template_args,
				arg_types,
				!args.empty(),
				// parse_function_arguments already produced the concrete arg-type list used
				// by this postfix path; keep the existing "no extra dependent deferral" behavior.
				false);
		}

		if (object_struct_name.has_value() && !instantiating_lazy_member_ &&
			isHardUseLikeInstantiationMode()) {
			std::string_view func_name = member_name_token.value();
			if (!func_name.empty()) {
				if (!instantiated_func.has_value()) {
					instantiating_lazy_member_ = true;
					auto restore_lazy_instantiation = ScopeGuard([&]() {
						instantiating_lazy_member_ = false;
					});
					instantiated_func =
						instantiateLazyMemberForCanonicalOwner(
							*object_struct_name,
							func_name,
							std::span<const TemplateTypeArg>{});
				}
				if (instantiated_func.has_value()) {
					FLASH_LOG(Templates, Debug, "Lazy instantiation completed for: ", *object_struct_name, "::", func_name);
				}
			}
		}

		if (object_struct_name.has_value() &&
			isHardUseLikeInstantiationMode() &&
			!known_member_func && !instantiated_func.has_value()) {
			auto checkHasMemberTemplate = [&]() {
				auto hasFunctionTemplateForOwner = [&](StringHandle owner_name_handle) {
					TemplateNameLookupResult lookup_result = gTemplateRegistry.lookupTemplateName(
						buildMemberFunctionTemplateLookupRequest(
							owner_name_handle,
							member_name_token.handle(),
							false));
					return lookup_result.hasFunctionTemplate();
				};

				const StringHandle owner_name_handle =
					StringTable::getOrInternStringHandle(*object_struct_name);
				if (hasFunctionTemplateForOwner(owner_name_handle)) {
					return true;
				}

				std::string_view base_name = extractBaseTemplateName(*object_struct_name);
				if (base_name.empty()) {
					return false;
				}

				return hasFunctionTemplateForOwner(
					StringTable::getOrInternStringHandle(base_name));
			};

			if (checkHasMemberTemplate()) {
				return ParseResult::error(
					"No matching member function for call to '" + std::string(member_name_token.value()) + "'",
					member_name_token);
			}
		}
		if (explicit_template_args &&
			object_struct_name.has_value() &&
			isHardUseLikeInstantiationMode() &&
			!instantiated_func.has_value()) {
			return ParseResult::error(
				"No matching member function template for call to '" + std::string(member_name_token.value()) + "'",
				member_name_token);
		}
		if (known_member_func && !instantiated_func.has_value()) {
			const size_t min_required = countMinRequiredArgs(*known_member_func);
			const size_t max_accepted = known_member_func->is_variadic()
				? std::numeric_limits<size_t>::max()
				: known_member_func->parameter_nodes().size();
			if (args.size() < min_required || args.size() > max_accepted) {
				return ParseResult::error(
					"No matching member function for call to '" + std::string(member_name_token.value()) + "'",
					member_name_token);
			}
		}

		// C++ [class.member.lookup]/1: when the derived class declares ANY overload
		// of the same name, all base-class overloads are hidden.  The single-match
		// arity check above handles the case where tryResolveConcreteMemberFunction
		// found exactly one concrete overload.  This block handles the multi-overload
		// case (2+ local overloads cause tryResolveConcreteMemberFunction to return
		// nullptr).  If the derived class owns the name but no local overload accepts
		// the argument count, the call is ill-formed.
		if (!known_member_func && !instantiated_func.has_value() &&
			!explicit_template_args && object_struct_name.has_value() &&
			isHardUseLikeInstantiationMode()) {
			if (auto type_opt = get_expression_type(*result); type_opt.has_value() &&
														  is_struct_type(type_opt->category())) {
				TypeIndex type_idx = type_opt->type_index();
				const TypeInfo* type_info = tryGetTypeInfo(type_idx);
				const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
				if (struct_info) {
					StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name_token.value());
					bool has_any_local_overload = false;
					bool has_arity_match = false;
					for (const auto& mf : struct_info->member_functions) {
						if (mf.is_constructor || mf.is_destructor)
							continue;
						if (mf.getName() != member_name_handle)
							continue;
						has_any_local_overload = true;
						// Check if this overload accepts the argument count
						if (const auto* candidate = get_function_decl_node(mf.function_decl)) {
							const size_t min_req = countMinRequiredArgs(*candidate);
							const size_t max_acc = candidate->is_variadic()
								? std::numeric_limits<size_t>::max()
								: candidate->parameter_nodes().size();
							if (args.size() >= min_req && args.size() <= max_acc) {
								has_arity_match = true;
								break;
							}
						}
					}
					if (has_any_local_overload && !has_arity_match) {
						return ParseResult::error(
							"No matching member function for call to '" + std::string(member_name_token.value()) + "'",
							member_name_token);
					}
				}
			}
		}

		FunctionDeclarationNode* func_ref_ptr = nullptr;
		if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
			func_ref_ptr = &instantiated_func->as<FunctionDeclarationNode>();
		} else if (known_member_func) {
			func_ref_ptr = const_cast<FunctionDeclarationNode*>(known_member_func);
		} else {
			auto temp_type = emplace_node<TypeSpecifierNode>(TypeCategory::Int, TypeQualifier::None, 32, member_name_token, CVQualifier::None);
			auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_name_token);
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());
			func_ref_ptr = &func_ref;
		}

		result = emplace_node<ExpressionNode>(
			makeResolvedMemberCallExpr(*result, *func_ref_ptr, std::move(args), member_name_token));
		if (func_ref_ptr->has_mangled_name()) {
			setCallMangledName(result->as<ExpressionNode>(), func_ref_ptr->mangled_name());
		}
		if (func_ref_ptr->is_member_function() &&
			!func_ref_ptr->parent_struct_name().empty()) {
			StringBuilder qualified_name_builder;
			qualified_name_builder
				.append(func_ref_ptr->parent_struct_name())
				.append("::")
				.append(func_ref_ptr->decl_node().identifier_token().value());
			setCallQualifiedName(
				result->as<ExpressionNode>(),
				qualified_name_builder.commit());
		}
		return ParseResult::success(*result);
	}

	result = emplace_node<ExpressionNode>(
		MemberAccessNode(*result, member_name_token, is_arrow_access));
	return ParseResult::success(*result);
}

// Apply postfix operators (., ->, [], (), ++, --) to an existing expression result
// This allows cast expressions (static_cast, dynamic_cast, etc.) to be followed by member access
// e.g., static_cast<T&&>(t).operator<=>(u)
ParseResult Parser::apply_postfix_operators(ASTNode& start_result) {
	std::optional<ASTNode> result = start_result;

	// Handle postfix operators in a loop
	constexpr int MAX_POSTFIX_ITERATIONS = 100;	// Safety limit to prevent infinite loops
	int postfix_iteration = 0;
	while (result.has_value() && !peek().is_eof() && postfix_iteration < MAX_POSTFIX_ITERATIONS) {
		++postfix_iteration;
		FLASH_LOG_FORMAT(Parser, Debug, "apply_postfix_operators iteration {}: peek token type={}, value='{}'",
						 postfix_iteration, static_cast<int>(peek_info().type()), peek_info().value());

		// Check for ++ and -- postfix operators
		if (peek().is_operator()) {
			std::string_view op = peek_info().value();
			if (op == "++" || op == "--") {
				Token operator_token = current_token_;
				advance(); // consume the postfix operator

				// Create a postfix unary operator node (is_prefix = false)
				result = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *result, false));
				continue;  // Check for more postfix operators
			}
		}

		// Check for member access (. or ->)
		if ((peek().is_punctuator() && peek() == "."_tok) || peek() == "->"_tok) {
			Token member_operator_token = peek_info();
			advance(); // consume '.' or '->'

			ParseResult member_result = parse_member_postfix(result, member_operator_token);
			if (member_result.is_error()) {
				return member_result;
			}
			continue;
		}

		// Check for function call operator () - e.g., static_cast<T&&>(x)(args...)
		if (peek().is_punctuator() && peek() == "("_tok) {
			Token paren_token = peek_info();
			advance(); // consume '('

			// Parse function arguments
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			finalizePostfixCallExpression(result, paren_token, std::move(args), std::move(args_result.arg_types));
			continue;
		}

		// Check for array subscript operator [] - e.g., static_cast<T*>(p)[i]
		if (peek().is_punctuator() && peek() == "["_tok) {
			Token bracket_token = peek_info();
			advance(); // consume '['

			ParseResult index_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (index_result.is_error()) {
				return index_result;
			}

			if (peek() != "]"_tok) {
				return ParseResult::error("Expected ']' after array index", current_token_);
			}
			advance(); // consume ']'

			if (auto index_node = index_result.node()) {
				result = emplace_node<ExpressionNode>(
					ArraySubscriptNode(*result, *index_node, bracket_token));
				continue;
			} else {
				return ParseResult::error("Invalid array index expression", bracket_token);
			}
		}

		// No more postfix operators we handle here - break
		break;
	}

	if (postfix_iteration >= MAX_POSTFIX_ITERATIONS) {
		return ParseResult::error("Parser error: too many postfix operator iterations", current_token_);
	}

	if (result.has_value()) {
		return ParseResult::success(*result);
	}

	return ParseResult();
}

// Phase 3: New postfix expression layer
// This function handles postfix operators: ++, --, [], (), ::, ., ->
// It calls parse_primary_expression and then handles postfix operators in a loop
ParseResult Parser::parse_postfix_expression(ExpressionContext context) {
#if WITH_PARSER_RUNTIME_STATS
	FLASHCPP_PARSER_RUNTIME_PHASE(PostfixExpression);
#endif
	// First, parse the primary expression
	ParseResult prim_result = parse_primary_expression(context);
	if (prim_result.is_error()) {
		return prim_result;
	}

	// Phase 3: Postfix operator loop moved from parse_primary_expression
	// This handles postfix operators: ++, --, [], (), ::, ., ->
	// The loop continues until we run out of postfix operators
	// Note: result is now an optional<ASTNode> (extracted from ParseResult) for compatibility with the postfix loop

	std::optional<ASTNode> result = prim_result.node();

	// Handle postfix operators in a loop
	constexpr int MAX_POSTFIX_ITERATIONS = 100;	// Safety limit to prevent infinite loops
	int postfix_iteration = 0;
	while (result.has_value() && !peek().is_eof() && postfix_iteration < MAX_POSTFIX_ITERATIONS) {
		++postfix_iteration;
		FLASH_LOG_FORMAT(Parser, Debug, "Postfix operator iteration {}: peek token type={}, value='{}'",
						 postfix_iteration, static_cast<int>(peek_info().type()), peek_info().value());
		if (peek().is_operator()) {
			std::string_view op = peek_info().value();
			if (op == "++" || op == "--") {
				Token operator_token = current_token_;
				advance(); // consume the postfix operator

				// Create a postfix unary operator node (is_prefix = false)
				result = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *result, false));
				continue;  // Check for more postfix operators
			}
		}

		// Check for function call operator () - for operator() overload or function pointer call
		if (peek().is_punctuator() && peek() == "("_tok) {
			Token paren_token = peek_info();
			advance(); // consume '('

			// Parse function arguments using unified helper
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);
			std::vector<TypeSpecifierNode> arg_types = std::move(args_result.arg_types);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			finalizePostfixCallExpression(result, paren_token, std::move(args), std::move(arg_types));
			continue;
		}

		// Check for array subscript operator []
		if (peek().is_punctuator() && peek() == "["_tok) {
			Token bracket_token = peek_info();
			advance(); // consume '['

			// Parse the index expression
			ParseResult index_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (index_result.is_error()) {
				return index_result;
			}

			// Expect closing ']'
			if (peek() != "]"_tok) {
				return ParseResult::error("Expected ']' after array index", current_token_);
			}
			advance(); // consume ']'

			if (auto index_node = index_result.node()) {
				result = emplace_node<ExpressionNode>(
					ArraySubscriptNode(*result, *index_node, bracket_token));
				continue;  // Check for more postfix operators (e.g., arr[i][j])
			} else {
				return ParseResult::error("Invalid array index expression", bracket_token);
			}
		}

		// Check for scope resolution operator :: (namespace/class member access)
		if (peek().is_punctuator() && peek() == "::"_tok) {
			// Handle namespace::member or class::static_member syntax
			// We have an identifier (in result), now parse :: and the member name

			// Special case: obj.Base::member() - qualified member access through base class
			// When result is a MemberAccessNode, the :: is qualifying the member, not
			// the expression. Rewrite as member access with the final qualified name.
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<MemberAccessNode>(expr)) {
					const auto& member_access = std::get<MemberAccessNode>(expr);
					ASTNode object = member_access.object();
					bool is_arrow = member_access.is_arrow();

					// Save position before consuming any tokens so we can restore the
					// entire chain if we hit a non-identifier after any '::' in the chain
					// (e.g., obj.Base::~Base(), obj.Base::Inner::~Inner(), obj.Base::operator==())
					auto saved_pos = save_token_position();
					advance(); // consume '::'

					// Skip 'template' keyword if present (dependent context disambiguator)
					if (peek() == "template"_tok)
						advance();

					// Consume all qualified parts: Base::Inner::member
					// Each iteration consumes one identifier; if followed by :: we loop again
					bool handled = false;
					while (peek().is_identifier()) {
						Token qualified_member_token = peek_info();
						advance();

						if (peek() == "::"_tok) {
							advance(); // consume '::'
							if (peek() == "template"_tok)
								advance();
							continue; // keep consuming qualified parts
						}

						// This is the final member name
						// Check if it's a member function call
						if (peek() == "("_tok) {
							advance(); // consume '('
							auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
								.handle_pack_expansion = true,
								.collect_types = true,
								.expand_simple_packs = false});
							if (!args_result.success) {
								return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
							}
							ChunkedVector<ASTNode> args = std::move(args_result.args);
							if (!consume(")"_tok)) {
								return ParseResult::error("Expected ')' after qualified member function call", current_token_);
							}
							auto type_spec = emplace_node<TypeSpecifierNode>(TypeIndex{}.withCategory(TypeCategory::Auto), 0, qualified_member_token, CVQualifier::None, ReferenceQualifier::None);
							auto& member_decl = emplace_node<DeclarationNode>(type_spec, qualified_member_token).as<DeclarationNode>();
							auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
							result = emplace_node<ExpressionNode>(
								makeResolvedMemberCallExpr(object, func_decl_node, std::move(args), qualified_member_token));
						} else {
							// Simple qualified member access
							result = emplace_node<ExpressionNode>(
								MemberAccessNode(object, qualified_member_token, is_arrow));
						}
						handled = true;
						break;
					}

					// Handle qualified operator call on member: obj.Base::operator=()
					if (!handled && peek() == "operator"_tok) {
						advance(); // consume 'operator'
						Token operator_keyword_token = current_token_;
						std::string_view op_name;
						if (auto err = parse_operator_name(operator_keyword_token, op_name)) {
							discard_saved_token(saved_pos);
							return std::move(*err);
						}
						Token op_token(Token::Type::Identifier, op_name,
									   operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());
						if (peek() == "("_tok) {
							advance(); // consume '('
							auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
								.handle_pack_expansion = true,
								.collect_types = true,
								.expand_simple_packs = false});
							if (!args_result.success) {
								discard_saved_token(saved_pos);
								return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
							}
							if (!consume(")"_tok)) {
								discard_saved_token(saved_pos);
								return ParseResult::error("Expected ')' after qualified operator member call", current_token_);
							}
							auto type_spec = emplace_node<TypeSpecifierNode>(TypeIndex{}.withCategory(TypeCategory::Auto), 0, op_token, CVQualifier::None, ReferenceQualifier::None);
							auto& member_decl = emplace_node<DeclarationNode>(type_spec, op_token).as<DeclarationNode>();
							auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
							result = emplace_node<ExpressionNode>(
								makeResolvedMemberCallExpr(object, func_decl_node, std::move(args_result.args), op_token));
							handled = true;
						}
					}

					if (handled) {
						discard_saved_token(saved_pos);
						continue;
					}

					// Non-identifier after :: (e.g., ~, operator) — restore entire chain
					// and fall through to the normal :: handler
					restore_token_position(saved_pos);
				}
			}

			advance(); // consume '::'

			// Handle qualified operator call: Type::operator=()
			if (peek() == "operator"_tok) {
				// Get the namespace/class name from the current result
				std::string_view namespace_name;
				if (result->is<ExpressionNode>()) {
					const ExpressionNode& expr = result->as<ExpressionNode>();
					if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&expr)) {
						namespace_name = identifier_ptr->name();
					} else {
						return ParseResult::error("Invalid left operand for '::'", current_token_);
					}
				} else {
					return ParseResult::error("Expected identifier before '::'", current_token_);
				}
				advance(); // consume 'operator'
				std::vector<StringType<32>> namespaces;
				namespaces.emplace_back(StringType<32>(namespace_name));
				return parse_qualified_operator_call(current_token_, namespaces);
			}

			// Expect an identifier after ::
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected identifier after '::'", current_token_);
			}

			// Get the namespace/class name from the current result
			std::string_view namespace_name;
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&expr)) {
					namespace_name = identifier_ptr->name();
				} else {
					return ParseResult::error("Invalid left operand for '::'", current_token_);
				}
			} else {
				return ParseResult::error("Expected identifier before '::'", current_token_);
			}

			// Now parse the rest as a qualified identifier
			std::vector<StringType<32>> namespaces;
			namespaces.emplace_back(StringType<32>(namespace_name));

			Token final_identifier = peek_info();
			advance(); // consume the identifier after ::

			// Check if there are more :: following (e.g., A::B::C)
			while (peek() == "::"_tok) {
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::

				// Handle qualified operator call: A::B::operator=()
				if (peek() == "operator"_tok) {
					advance(); // consume 'operator'
					return parse_qualified_operator_call(current_token_, namespaces);
				}

				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier after '::'", current_token_);
				}
				final_identifier = peek_info();
				advance(); // consume identifier
			}

			// Look up the qualified identifier
			auto qualified_symbol = gSymbolTable.lookup_qualified(namespaces, final_identifier.value());

			// Check if this is followed by template arguments: ns::func<Args>
			std::optional<InlineVector<TemplateTypeArg, 4>> template_args;
			if (peek() == "<"_tok) {
				template_args = parse_explicit_template_arguments();
				// If parsing failed, it might be a less-than operator, continue normally
			}

			// Check if this is a brace initialization: ns::Class<Args>{}
			if (template_args.has_value() && peek() == "{"_tok) {
				// Build the qualified name for lookup
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());

				// Parse the brace initialization using the helper
				ParseResult brace_init_result = parse_template_brace_initialization(*template_args, qualified_name, final_identifier);
				if (brace_init_result.is_error()) {
					// If parsing failed, fall through to error handling
					FLASH_LOG_FORMAT(Parser, Debug, "Brace initialization parsing failed: {}", brace_init_result.error_message());
				} else if (brace_init_result.node().has_value()) {
					result = brace_init_result.node();
					continue; // Check for more postfix operators
				}
			}

			// Check if this is a function call
			if (peek() == "("_tok) {
				advance(); // consume '('

				// Parse function arguments using unified helper (collect types for template deduction)
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = true,
					.expand_simple_packs = false});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}

				// Get the DeclarationNode
				auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
					if (node.is<DeclarationNode>()) {
						return &node.as<DeclarationNode>();
					} else if (node.is<FunctionDeclarationNode>()) {
						return &node.as<FunctionDeclarationNode>().decl_node();
					} else if (node.is<VariableDeclarationNode>()) {
						return &node.as<VariableDeclarationNode>().declaration();
					} else if (node.is<TemplateFunctionDeclarationNode>()) {
						// Handle template function declarations - extract the inner function declaration
						return &node.as<TemplateFunctionDeclarationNode>().function_declaration().as<FunctionDeclarationNode>().decl_node();
					}
					return nullptr;
				};

				const DeclarationNode* decl_ptr = qualified_symbol.has_value() ? getDeclarationNode(*qualified_symbol) : nullptr;
				if (!decl_ptr) {
					StringBuilder class_scope_builder;
					for (size_t i = 0; i < namespaces.size(); ++i) {
						if (i > 0) {
							class_scope_builder.append("::");
						}
						class_scope_builder.append(std::string_view(namespaces[i]));
					}
					std::string_view class_scope = class_scope_builder.commit();
					StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_scope);
					const TypeInfo* class_type_info = lookupTypeInCurrentContext(class_name_handle);
					if (class_type_info == nullptr) {
						auto class_type_it = getTypesByNameMap().find(class_name_handle);
						if (class_type_it != getTypesByNameMap().end()) {
							class_type_info = class_type_it->second;
						}
					}
					if (class_type_info != nullptr) {
						if (class_type_info->isTypeAlias() && class_type_info->type_index_.is_valid()) {
							ResolvedAliasTypeInfo resolved_alias =
								resolveAliasTypeInfo(class_type_info->registeredTypeIndex().withCategory(class_type_info->typeEnum()));
							if (resolved_alias.terminal_type_info != nullptr) {
								class_type_info = resolved_alias.terminal_type_info;
							} else if (resolved_alias.type_index.is_valid()) {
								if (const TypeInfo* resolved_type = tryGetTypeInfo(resolved_alias.type_index)) {
									class_type_info = resolved_type;
								}
							}
						}
						const StructTypeInfo* struct_info = class_type_info->getStructInfo();
						if (struct_info == nullptr) {
							struct_info = tryGetStructTypeInfo(class_type_info->registeredTypeIndex());
						}
						if (struct_info != nullptr) {
							auto member_function_result =
								struct_info->findMemberFunctionRecursive(final_identifier.handle());
							if (const StructMemberFunction* member_function = member_function_result.first) {
								if (member_function->function_decl.is<FunctionDeclarationNode>()) {
									qualified_symbol = member_function->function_decl;
									decl_ptr = &qualified_symbol->as<FunctionDeclarationNode>().decl_node();
								}
							}
						}
					}
				}
				if (qualified_symbol.has_value() && qualified_symbol->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = qualified_symbol->as<FunctionDeclarationNode>();
					if (!func_decl.is_materialized()) {
						StringBuilder class_scope_builder;
						for (size_t i = 0; i < namespaces.size(); ++i) {
							if (i > 0) {
								class_scope_builder.append("::");
							}
							class_scope_builder.append(std::string_view(namespaces[i]));
						}
						std::string_view class_scope = class_scope_builder.commit();
						StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_scope);
						auto class_type_it = getTypesByNameMap().find(class_name_handle);
						if (class_type_it != getTypesByNameMap().end() && class_type_it->second->isTemplateInstantiation()) {
							if (!instantiating_lazy_member_) {
								instantiating_lazy_member_ = true;
								auto restore_lazy_instantiation = ScopeGuard([&]() {
									instantiating_lazy_member_ = false;
								});
								auto instantiated_func =
									instantiateLazyMemberForCanonicalOwner(
										class_scope,
										final_identifier.value(),
										std::span<const TemplateTypeArg>{});
								if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
									qualified_symbol = instantiated_func;
									decl_ptr = &instantiated_func->as<FunctionDeclarationNode>().decl_node();
								}
							}
						}
					}
				}

				// If symbol not found and we're not in extern "C", try template instantiation
				if (!decl_ptr && current_linkage_ != Linkage::C) {
					// Build qualified template name (e.g., "std::move")
					std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());

					std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);

					// Try explicit template instantiation first if template arguments were provided
					// (e.g., ns::func<true>(args) should use try_instantiate_template_explicit)
					if (template_args.has_value()) {
						std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args, arg_types);
						if (!template_inst.has_value()) {
							// Also try without namespace prefix
							template_inst = try_instantiate_template_explicit(final_identifier.value(), *template_args, arg_types);
						}
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							decl_ptr = &template_inst->as<FunctionDeclarationNode>().decl_node();
							FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template with explicit args: ", qualified_name);
						}
					}

					// Fall back to argument-type-based deduction
					if (!decl_ptr) {
						// Try to instantiate the qualified template function
						if (!arg_types.empty()) {
							std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
							if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
								decl_ptr = &template_inst->as<FunctionDeclarationNode>().decl_node();
								FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template: ", qualified_name);
							}
						}
					}
				}

				if (!decl_ptr) {
					// Validate that the namespace path actually exists before creating a forward declaration.
					// This catches errors like f2::func() when only namespace f exists.
					NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
					if (!validateQualifiedNamespace(ns_handle, final_identifier, (parsing_template_depth_ > 0))) {
						return ParseResult::error(
							std::string(StringBuilder().append("Use of undeclared identifier '").append(buildQualifiedNameFromStrings(namespaces, final_identifier.value())).append("'").commit()),
							final_identifier);
					}
					// Namespace exists — create forward declaration for external functions (e.g., std::print)
					auto type_node = emplace_node<TypeSpecifierNode>(TypeCategory::Int, TypeQualifier::None, 32, final_identifier, CVQualifier::None);
					auto forward_decl = emplace_node<DeclarationNode>(type_node, final_identifier);
					decl_ptr = &forward_decl.as<DeclarationNode>();
				}

				// Create function call node
				auto function_call_node = emplace_node<ExpressionNode>(
					qualified_symbol.has_value() && qualified_symbol->is<FunctionDeclarationNode>()
						? makeResolvedCallExpr(qualified_symbol->as<FunctionDeclarationNode>(), std::move(args), final_identifier)
						: makeDirectCallExpr(*decl_ptr, std::move(args), final_identifier));

				// If the function has a pre-computed mangled name, set it on the call expression
				if (qualified_symbol.has_value() && qualified_symbol->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = qualified_symbol->as<FunctionDeclarationNode>();
					if (func_decl.has_mangled_name()) {
						setCallMangledName(function_call_node.as<ExpressionNode>(), func_decl.mangled_name());
						FLASH_LOG(Parser, Debug, "Set mangled name on qualified call expression (postfix path): {}", func_decl.mangled_name());
					}
				}

				result = function_call_node;
				continue; // Check for more postfix operators
			}

			// DEBUG: Log what we have at this point
			if (!peek().is_eof()) {
				FLASH_LOG(Templates, Info, "After function call check: template_args.has_value()=", template_args.has_value(),
						  ", peek='", peek_info().value(), "', peek.empty()=", peek_info().value().empty());
			}

			if (template_args.has_value() && !peek_info().value().empty() && peek() != "("_tok) {
				// This might be a variable template usage with qualified name: ns::var_template<Args>
				// Build the qualified name for lookup
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());
				FLASH_LOG(Templates, Info, "Checking for qualified template: ", qualified_name, ", peek='", peek_info().value(), "'");

				TemplateNameLookupResult qualified_var_lookup = gTemplateRegistry.lookupTemplateName(
					buildTemplateNameLookupRequest(
						StringTable::getOrInternStringHandle(qualified_name),
						TemplateNameLookupKind::Qualified,
						false));
				TemplateNameLookupResult unqualified_var_lookup;
				std::string_view variable_template_lookup_name = qualified_name;
				if (!qualified_var_lookup.hasVariableTemplate()) {
					unqualified_var_lookup = gTemplateRegistry.lookupTemplateName(
						buildTemplateNameLookupRequest(
							final_identifier.handle(),
							TemplateNameLookupKind::Ordinary,
							false));
					if (unqualified_var_lookup.hasVariableTemplate()) {
						variable_template_lookup_name = final_identifier.value();
					}
				}

				const bool has_variable_template =
					qualified_var_lookup.hasVariableTemplate() ||
					unqualified_var_lookup.hasVariableTemplate();
				if (has_variable_template) {
					FLASH_LOG(Templates, Info, "Found variable template: ", variable_template_lookup_name);
					auto instantiated_var = try_instantiate_variable_template(variable_template_lookup_name, *template_args, nullptr);
					if (instantiated_var.has_value()) {
						// Get the instantiated variable name
						std::string_view inst_name;
						if (instantiated_var->is<VariableDeclarationNode>()) {
							const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
							const auto& decl = var_decl.declaration();
							inst_name = decl.identifier_token().value();
						} else if (instantiated_var->is<DeclarationNode>()) {
							const auto& decl = instantiated_var->as<DeclarationNode>();
							inst_name = decl.identifier_token().value();
						} else {
							inst_name = qualified_name;	// Fallback
						}

						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name,
										 final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						FLASH_LOG(Templates, Debug, "Successfully instantiated qualified variable template: ", variable_template_lookup_name);
						continue; // Check for more postfix operators
					}
				}

				// Not a variable template - check if it's a class template that needs instantiation
				// If we have template args, try to instantiate the class template
				// This handles patterns like: std::is_integral<int>::value
				if (!has_variable_template) {
					FLASH_LOG(Templates, Info, "Attempting class template instantiation for: ", qualified_name);
					auto instantiation_result = try_instantiate_class_template(qualified_name, *template_args);
					// Update the type_name to use the fully instantiated name (with defaults filled in)
					if (instantiation_result.has_value() && instantiation_result->is<StructDeclarationNode>()) {
						const StructDeclarationNode& inst_struct = instantiation_result->as<StructDeclarationNode>();
						std::string_view instantiated_name = StringTable::getStringView(inst_struct.name());
						// Replace the base template name in namespaces with the instantiated name
						if (!namespaces.empty()) {
							namespaces.back() = StringType<32>(instantiated_name);
							FLASH_LOG(Templates, Debug, "Updated namespace to use instantiated name: ", instantiated_name);
						}
					}
				}

				// Fall through to handle as regular qualified identifier if not a variable template
			}

			// Check if this might be accessing a static member (e.g., MyClass::value)
			// Try this before checking qualified_symbol, as static member access might not be in symbol table
			std::string_view type_name = namespaces.empty() ? std::string_view() : std::string_view(namespaces.back());
			std::string_view member_name = final_identifier.value();

			// Try to resolve the type and trigger lazy static member instantiation if needed
			if (!type_name.empty()) {
				auto type_handle = StringTable::getOrInternStringHandle(type_name);
				auto type_it = getTypesByNameMap().find(type_handle);
				if (type_it != getTypesByNameMap().end()) {
					const TypeInfo* type_info = type_it->second;
					FLASH_LOG(Parser, Debug, "Found type '", type_name, "' with type=", (int)type_info->typeEnum(),
							  " type_index=", type_info->type_index_);

					// For type aliases, resolve to the actual type
					if (type_info->isStruct()) {
						const TypeInfo* actual_type = tryGetTypeInfo(type_info->type_index_);
						const StructTypeInfo* struct_info = actual_type ? actual_type->getStructInfo() : nullptr;
						if (struct_info) {
							StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
							FLASH_LOG(Parser, Debug, "Triggering lazy instantiation for ",
									  StringTable::getStringView(struct_info->name), "::", member_name);
							// Trigger lazy static member instantiation if needed
							instantiateLazyStaticMember(struct_info->name, member_handle);
						}
					} else if (type_info->isStruct()) {
						// Direct struct type (not an alias)
						const StructTypeInfo* struct_info = type_info->getStructInfo();
						if (struct_info) {
							StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
							FLASH_LOG(Parser, Debug, "Triggering lazy instantiation for ",
									  StringTable::getStringView(struct_info->name), "::", member_name);
							// Trigger lazy static member instantiation if needed
							instantiateLazyStaticMember(struct_info->name, member_handle);
						}
					}
				}
			}

			if (qualified_symbol.has_value()) {
				// Just a qualified identifier reference (e.g., Namespace::globalValue or Class::staticMember)
				NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
				QualifiedIdentifierNode qual_node(ns_handle, final_identifier);
				result = emplace_node<ExpressionNode>(qual_node);
				continue; // Check for more postfix operators
			} else {
				return ParseResult::error("Undefined qualified identifier", final_identifier);
			}
		}

		// Check for member access operator . or -> (or pointer-to-member .* or ->*)
		Token operator_start_token;	// Track the operator token for error reporting

		if (peek() == "."_tok) {
			operator_start_token = peek_info();
			advance(); // consume '.'

			// Check for pointer-to-member operator .*
			if (peek() == "*"_tok) {
				advance(); // consume '*'

				// Parse the RHS expression (pointer to member)
				// Pointer-to-member operators have precedence similar to multiplicative operators (17)
				// But we need to stop at lower precedence operators, so use precedence 17
				ParseResult member_ptr_result = parse_expression(17, ExpressionContext::Normal);

				if (member_ptr_result.is_error()) {
					return member_ptr_result;
				}
				if (!member_ptr_result.node().has_value()) {
					return ParseResult::error("Expected expression after '.*' operator", current_token_);
				}

				// Create PointerToMemberAccessNode
				result = emplace_node<ExpressionNode>(
					PointerToMemberAccessNode(*result, *member_ptr_result.node(), operator_start_token, false));
				continue;  // Check for more postfix operators
			}
		} else if (peek() == "->"_tok) {
			operator_start_token = peek_info();
			advance(); // consume '->'

			// Check for pointer-to-member operator ->*
			if (peek() == "*"_tok) {
				advance(); // consume '*'

				// Parse the RHS expression (pointer to member)
				// Pointer-to-member operators have precedence similar to multiplicative operators (17)
				// But we need to stop at lower precedence operators, so use precedence 17
				ParseResult member_ptr_result = parse_expression(17, ExpressionContext::Normal);
				if (member_ptr_result.is_error()) {
					return member_ptr_result;
				}
				if (!member_ptr_result.node().has_value()) {
					return ParseResult::error("Expected expression after '->*' operator", current_token_);
				}

				// Create PointerToMemberAccessNode
				result = emplace_node<ExpressionNode>(
					PointerToMemberAccessNode(*result, *member_ptr_result.node(), operator_start_token, true));
				continue;  // Check for more postfix operators
			}

			// Note: We don't transform ptr->member to (*ptr).member here anymore.
			// Instead, we pass the is_arrow flag to MemberAccessNode, and CodeGen will
			// handle operator-> overload resolution. For raw pointers, it will generate
			// the equivalent of (*ptr).member; for objects with operator->, it will call that.
		} else {
			if (!peek().is_eof()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Postfix loop: breaking, peek token type={}, value='{}'",
								 static_cast<int>(peek_info().type()), peek_info().value());
			} else {
				FLASH_LOG(Parser, Debug, "Postfix loop: breaking, no more tokens");
			}
			break;  // No more postfix operators
		}

		ParseResult member_result = parse_member_postfix(result, operator_start_token);
		if (member_result.is_error()) {
			return member_result;
		}
		continue;  // Check for more postfix operators (e.g., obj.member1.member2)
	}

	// Check if we hit the iteration limit (indicates potential infinite loop)
	if (postfix_iteration >= MAX_POSTFIX_ITERATIONS) {
		FLASH_LOG_FORMAT(Parser, Error, "Hit MAX_POSTFIX_ITERATIONS limit ({}) - possible infinite loop in postfix operator parsing", MAX_POSTFIX_ITERATIONS);
		return ParseResult::error("Parser error: too many postfix operator iterations", current_token_);
	}

	if (result.has_value())
		return ParseResult::success(*result);

	// No result was produced - this should not happen in a well-formed expression
	return ParseResult();  // Return monostate instead of empty success
}
