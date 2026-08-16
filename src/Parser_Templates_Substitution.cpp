#include "Parser.h"
#include "AstTraversal.h"
#include "CallNodeHelpers.h"
#include "ConstExprEvaluator.h"
#include "ExpressionSubstitutor.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TemplateArgumentMaterialization.h"
#include "TypeTraitEvaluator.h"
#include <unordered_map>

namespace {

struct NamedPackBinding {
	bool found = false;
	size_t count = 0;
	std::optional<std::vector<TemplateTypeArg>> template_args;
};

template <typename ParamContainer, typename ArgContainer, typename ExactPackSizeLookup>
std::optional<std::vector<TemplateTypeArg>> extractNamedTemplateArgumentPack(
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	std::string_view pack_name,
	ExactPackSizeLookup&& exact_pack_size_lookup) {
	size_t total_non_variadic = 0;
	size_t total_variadic = 0;
	for (const auto& param_node : template_params) {
		const TemplateParameterNode* tparam = tryGetTemplateParameterNode(param_node);
		if (tparam == nullptr) {
			continue;
		}
		if (tparam->is_variadic()) {
			++total_variadic;
		} else {
			++total_non_variadic;
		}
	}

	size_t arg_index = 0;
	for (const auto& param_node : template_params) {
		const TemplateParameterNode* tparam = tryGetTemplateParameterNode(param_node);
		if (tparam == nullptr) {
			continue;
		}
		if (!tparam->is_variadic()) {
			if (arg_index >= template_args.size()) {
				return std::nullopt;
			}
			++arg_index;
			continue;
		}

		size_t pack_size = 0;
		if (auto exact_size = exact_pack_size_lookup(tparam->name())) {
			pack_size = *exact_size;
		} else if (total_variadic == 1 && template_args.size() >= total_non_variadic) {
			pack_size = template_args.size() - total_non_variadic;
		} else {
			return std::nullopt;
		}

		if (tparam->name() == pack_name) {
			if (arg_index + pack_size > template_args.size()) {
				return std::nullopt;
			}
			std::vector<TemplateTypeArg> pack_args;
			pack_args.reserve(pack_size);
			for (size_t i = 0; i < pack_size; ++i) {
				pack_args.push_back(template_args[arg_index + i]);
			}
			return pack_args;
		}

		arg_index += pack_size;
	}

	return std::nullopt;
}

// Return true if the expression tree contains an IdentifierNode whose name equals pack_name.
// Uses AstTraversal::visitExpressionNode which calls std::visit on the ExpressionNode variant,
// so the static_assert below gives a compile error if a new ExpressionNode variant is added
// without being handled here.
template <typename T>
inline constexpr bool AlwaysFalseExprContains = false;

bool exprContainsIdentifier(const ASTNode& expr, std::string_view pack_name) {
	if (!expr.has_value() || pack_name.empty()) {
		return false;
	}
	if (expr.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = expr.as<TypeSpecifierNode>();
		return type_spec.token().value() == pack_name;
	}

	return AstTraversal::visitExpressionNode(expr, [&](const auto& node) -> bool {
		using T = std::decay_t<decltype(node)>;
		if constexpr (std::is_same_v<T, IdentifierNode>) {
			return node.name() == pack_name;
		} else if constexpr (std::is_same_v<T, TemplateParameterReferenceNode>) {
			return StringTable::getStringView(node.param_name()) == pack_name;
		} else if constexpr (std::is_same_v<T, CallExprNode>) {
			if (node.has_receiver() && exprContainsIdentifier(node.receiver(), pack_name))
				return true;
			for (const auto& arg : node.arguments())
				if (exprContainsIdentifier(arg, pack_name)) return true;
			if (node.has_template_arguments()) {
				for (const auto& ta : node.template_arguments())
					if (exprContainsIdentifier(ta, pack_name)) return true;
			}
			return false;
		} else if constexpr (std::is_same_v<T, ConstructorCallNode>) {
			for (const auto& arg : node.arguments())
				if (exprContainsIdentifier(arg, pack_name)) return true;
			return false;
		} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
			return exprContainsIdentifier(node.get_lhs(), pack_name) ||
				   exprContainsIdentifier(node.get_rhs(), pack_name);
		} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
			return exprContainsIdentifier(node.get_operand(), pack_name);
		} else if constexpr (std::is_same_v<T, TernaryOperatorNode>) {
			return exprContainsIdentifier(node.condition(), pack_name) ||
				   exprContainsIdentifier(node.true_expr(), pack_name) ||
				   exprContainsIdentifier(node.false_expr(), pack_name);
		} else if constexpr (std::is_same_v<T, StaticCastNode> ||
							 std::is_same_v<T, DynamicCastNode> ||
							 std::is_same_v<T, ConstCastNode> ||
							 std::is_same_v<T, ReinterpretCastNode>) {
			return exprContainsIdentifier(node.expr(), pack_name);
		} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
			return !node.is_type() && exprContainsIdentifier(node.type_or_expr(), pack_name);
		} else if constexpr (std::is_same_v<T, AlignofExprNode>) {
			return !node.is_type() && exprContainsIdentifier(node.type_or_expr(), pack_name);
		} else if constexpr (std::is_same_v<T, TypeidNode>) {
			return !node.is_type() && exprContainsIdentifier(node.operand(), pack_name);
		} else if constexpr (std::is_same_v<T, NoexceptExprNode>) {
			return exprContainsIdentifier(node.expr(), pack_name);
		} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
			return exprContainsIdentifier(node.object(), pack_name);
		} else if constexpr (std::is_same_v<T, PointerToMemberAccessNode>) {
			return exprContainsIdentifier(node.object(), pack_name) ||
				   exprContainsIdentifier(node.member_pointer(), pack_name);
		} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
			return exprContainsIdentifier(node.array_expr(), pack_name) ||
				   exprContainsIdentifier(node.index_expr(), pack_name);
		} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode> ||
							 std::is_same_v<T, StringLiteralNode> ||
							 std::is_same_v<T, NumericLiteralNode> ||
							 std::is_same_v<T, BoolLiteralNode> ||
							 std::is_same_v<T, SizeofPackNode> ||
							 std::is_same_v<T, OffsetofExprNode> ||
							 std::is_same_v<T, TypeTraitExprNode> ||
							 std::is_same_v<T, NewExpressionNode> ||
							 std::is_same_v<T, DeleteExpressionNode> ||
							 std::is_same_v<T, LambdaExpressionNode> ||
							 std::is_same_v<T, FoldExpressionNode> ||
							 std::is_same_v<T, PackExpansionExprNode> ||
							 std::is_same_v<T, PseudoDestructorCallNode> ||
							 std::is_same_v<T, InitializerListConstructionNode> ||
							 std::is_same_v<T, ThrowExpressionNode>) {
			return false;
		} else {
			static_assert(AlwaysFalseExprContains<T>, "Unhandled ExpressionNode type in exprContainsIdentifier — add it here");
		}
	});
}

}

std::optional<TypeSpecifierNode>
Parser::tryMaterializeDependentAliasTypeSpecifier(
	const TypeSpecifierNode& type_spec,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	const TypeInfo* dependent_alias_info = tryGetTypeInfo(type_spec.type_index());
	if (dependent_alias_info == nullptr ||
		!dependent_alias_info->isTemplateInstantiation()) {
		return std::nullopt;
	}

	StringHandle qualified_alias_name =
		gNamespaceRegistry.buildQualifiedIdentifier(
			dependent_alias_info->sourceNamespace(),
			dependent_alias_info->baseTemplateName());
	std::optional<ASTNode> alias_entry =
		gTemplateRegistry.lookup_alias_template(
			StringTable::getStringView(qualified_alias_name));
	if (!alias_entry.has_value()) {
		alias_entry = gTemplateRegistry.lookup_alias_template(
			StringTable::getStringView(dependent_alias_info->baseTemplateName()));
	}
	if (!alias_entry.has_value() || !alias_entry->is<TemplateAliasNode>()) {
		return std::nullopt;
	}

	TemplateEnvironment substitution_environment =
		buildTemplateEnvironment(template_params, template_args, nullptr);
	ExpressionSubstitutor substitutor(substitution_environment, *this);
	TypeSpecifierNode substituted_type =
		substitutor.substituteTypeSpecifier(type_spec);
	if (!substituted_type.type_index().is_valid() &&
		is_builtin_type(substituted_type.type())) {
		TypeIndex native_type_index = nativeTypeIndex(substituted_type.type());
		if (native_type_index.is_valid()) {
			substituted_type.set_type_index(native_type_index);
			substituted_type.set_size_in_bits(
				get_type_size_bits(substituted_type.type()));
		}
	}
	if (typeSpecStillUsesDependentPlaceholder(substituted_type) ||
		substituted_type.type() == TypeCategory::Template ||
		(substituted_type.type() == type_spec.type() &&
		 substituted_type.type_index() == type_spec.type_index())) {
		return std::nullopt;
	}
	return substituted_type;
}

ASTNode Parser::substituteTemplateParameters(
	const ASTNode& node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	return substituteTemplateParameters(
		node,
		template_params,
		template_args,
		TypeIndex{},
		false);
}

ASTNode Parser::substituteTemplateParameters(
	const ASTNode& node,
	const TemplateInstantiationContext& context) {
	TypeIndex owner_type_index = context.current_instantiation_type;
	const bool has_implicit_this = !member_function_context_stack_.empty() &&
		member_function_context_stack_.back().has_implicit_this;
	if (!owner_type_index.is_valid() && !member_function_context_stack_.empty()) {
		owner_type_index = member_function_context_stack_.back().struct_type_index;
	}
	TemplateBodySubstitutionState state = makeTemplateBodySubstitutionState(
		owner_type_index,
		has_implicit_this);
	state.environment = context.environment;
	return substituteTemplateParametersWithState(
		node,
		context.template_parameters,
		context.template_arguments,
		state);
}

ASTNode Parser::substituteTemplateParameters(
	const ASTNode& node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	TypeIndex current_owner_type_index,
	bool has_implicit_this) {
	TemplateBodySubstitutionState state = makeTemplateBodySubstitutionState(
		current_owner_type_index,
		has_implicit_this);
	state.environment = buildTemplateEnvironment(template_params, template_args, nullptr);
	return substituteTemplateParametersWithState(
		node,
		template_params,
		template_args,
		state);
}

Parser::TemplateBodySubstitutionState Parser::makeTemplateBodySubstitutionState(
	TypeIndex owner_type_index,
	bool has_implicit_this) const {
	TemplateBodySubstitutionState state;
	state.owner_type_index = owner_type_index;
	state.has_implicit_this = has_implicit_this;

	if (state.owner_type_index.is_valid()) {
		const TypeInfo* owner_type_info = tryGetTypeInfo(state.owner_type_index);
		if (owner_type_info == nullptr) {
			throw CompileError(std::string(StringBuilder()
				.append("Unable to resolve concrete template substitution owner TypeIndex ")
				.append(static_cast<uint64_t>(state.owner_type_index.index()))
				.commit()));
		}
		state.owner_type_name = owner_type_info->name();
	} else if (has_implicit_this) {
		throw CompileError(
			"Implicit-object template substitution requires a concrete owner TypeIndex");
	}
	return state;
}

ASTNode Parser::substituteTemplateParametersWithState(
	const ASTNode& node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	TemplateBodySubstitutionState& state) {
	const bool is_root_substitution =
		active_template_body_substitution_ != &state;
	TemplateBodySubstitutionState* previous_active =
		active_template_body_substitution_;
	bool pushed_member_context = false;
	if (is_root_substitution) {
		active_template_body_substitution_ = &state;
		if (state.owner_type_name.isValid() &&
			state.owner_type_index.is_valid() &&
			(member_function_context_stack_.empty() ||
			 member_function_context_stack_.back().struct_type_index !=
				 state.owner_type_index ||
			 member_function_context_stack_.back().has_implicit_this !=
				 state.has_implicit_this)) {
			member_function_context_stack_.push_back({
				state.owner_type_name,
				state.owner_type_index,
				nullptr,
				nullptr,
				state.has_implicit_this
			});
			pushed_member_context = true;
		}
	}
	auto substitution_scope = ScopeGuard(
		[this, is_root_substitution, previous_active, pushed_member_context]() {
			if (pushed_member_context) {
				member_function_context_stack_.pop_back();
			}
			if (is_root_substitution) {
				active_template_body_substitution_ = previous_active;
			}
		});

	const auto substitute_nested = [&](const ASTNode& child) -> ASTNode {
		return substituteTemplateParametersWithState(
			child,
			template_params,
			template_args,
			state);
	};
	const bool is_expression_surface = node.is<ExpressionNode>() ||
		ExpressionStructure::visitExpressionChildren(
			node,
			[](ExpressionStructure::ExpressionChildRole, const ASTNode&) {});
	if (is_expression_surface) {
		TemplateEnvironment expression_environment = state.environment;
		std::optional<TemplateEnvironment> owner_environment;
		if (state.owner_type_index.is_valid()) {
			if (const TypeInfo* owner_type_info = tryGetTypeInfo(state.owner_type_index);
				owner_type_info != nullptr && owner_type_info->hasInstantiationContext()) {
				owner_environment = buildTemplateEnvironment(
					*owner_type_info->instantiationContext());
				owner_environment->parent = expression_environment.parent;
				expression_environment.parent = &*owner_environment;
			}
		}
		for (const TemplateParamSubstitution& substitution : template_param_substitutions_) {
			bool already_bound = false;
			for (const TemplateBinding& binding : expression_environment.bindings) {
				if (binding.name == substitution.param_name) {
					already_bound = true;
					break;
				}
			}
			if (already_bound) {
				continue;
			}
			TemplateBinding binding;
			binding.name = substitution.param_name;
			binding.is_pack = substitution.is_pack;
			if (substitution.is_pack) {
				binding.kind = substitution.pack_args.empty() ||
					substitution.pack_args.front().is_value
					? TemplateParameterKind::NonType
					: TemplateParameterKind::Type;
				binding.args.assign(
					substitution.pack_args.begin(),
					substitution.pack_args.end());
			} else if (substitution.is_type_param) {
				binding.kind = TemplateParameterKind::Type;
				binding.args.push_back(substitution.substituted_type);
			} else if (substitution.is_value_param) {
				binding.kind = TemplateParameterKind::NonType;
				binding.args.emplace_back(substitution.value, substitution.value_type);
			} else {
				binding.kind = TemplateParameterKind::Template;
			}
			expression_environment.bindings.push_back(std::move(binding));
		}
		for (TemplateBinding& binding : expression_environment.bindings) {
			for (const TemplateParameterNode& parameter : template_params) {
				if (parameter.nameHandle() != binding.name) {
					continue;
				}
				for (TemplateTypeArg& argument : binding.args) {
					argument = enrichTemplateArgForParameter(parameter, std::move(argument));
				}
				break;
			}
		}
		ExpressionSubstitutor substitutor(
			expression_environment,
			*this,
			template_params,
			template_args);
		if (state.owner_type_name.isValid()) {
			substitutor.setCurrentOwnerTypeName(state.owner_type_name);
		}
		ASTNode substituted = substitutor.substitute(node);
		if (!node.is<ExpressionNode>() && substituted.is<ExpressionNode>()) {
			return std::visit(
				[](const auto& expression_node) -> ASTNode {
					return ASTNode(&expression_node);
				},
				substituted.as<ExpressionNode>());
		}
		return substituted;
	}
	auto bind_constexpr_locals = [&]() {
		std::unordered_map<std::string_view, ConstExpr::EvalResult> bindings;
		bindings.reserve(state.constexpr_locals.size());
		for (const auto& local : state.constexpr_locals) {
			bindings.emplace(local.name, ConstExpr::EvalResult::from_int(local.value));
		}
		return bindings;
	};
	const StringHandle current_owner_type_name = state.owner_type_name;
	// Helper function to get type name as string
	auto get_type_name = [](TypeCategory type) -> std::string_view {
		switch (type) {
		case TypeCategory::Void:
			return "void";
		case TypeCategory::Bool:
			return "bool";
		case TypeCategory::Char:
			return "char";
		case TypeCategory::UnsignedChar:
			return "unsigned char";
		case TypeCategory::Short:
			return "short";
		case TypeCategory::UnsignedShort:
			return "unsigned short";
		case TypeCategory::Int:
			return "int";
		case TypeCategory::UnsignedInt:
			return "unsigned int";
		case TypeCategory::Long:
			return "long";
		case TypeCategory::UnsignedLong:
			return "unsigned long";
		case TypeCategory::LongLong:
			return "long long";
		case TypeCategory::UnsignedLongLong:
			return "unsigned long long";
		case TypeCategory::Float:
			return "float";
		case TypeCategory::Double:
			return "double";
		case TypeCategory::LongDouble:
			return "long double";
		case TypeCategory::UserDefined:
			return "user_defined";  // This should be handled specially
		default:
			return "unknown";
		}
	};
	// Non-expression AST nodes retain their existing parser-owned substitution paths.
	if (node.is<DeclarationNode>()) {
		// Handle declarations that might have template parameter types
		const DeclarationNode& decl = node.as<DeclarationNode>();

		// Substitute the type specifier
		ASTNode substituted_type = substitute_nested(decl.type_node());

		// Create new declaration with substituted type while preserving declaration metadata
		ASTNode new_decl_node = emplace_node<DeclarationNode>(substituted_type, decl.identifier_token());
		DeclarationNode& new_decl = new_decl_node.as<DeclarationNode>();
		new_decl.set_custom_alignment(decl.custom_alignment());
		new_decl.set_parameter_pack(decl.is_parameter_pack());
		new_decl.set_unsized_array(decl.is_unsized_array());
		if (!decl.array_dimensions().empty()) {
			std::vector<ASTNode> substituted_dims;
			substituted_dims.reserve(decl.array_dimensions().size());
			for (const auto& dim : decl.array_dimensions()) {
				substituted_dims.push_back(substitute_nested(dim));
			}
			new_decl.set_array_dimensions(std::move(substituted_dims));
		}
		if (decl.has_default_value()) {
			new_decl.set_default_value(substitute_nested(decl.default_value()));
		}
		return new_decl_node;

	} else if (node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();
		FLASH_LOG(Templates, Trace, "  substituteTemplateParameters TypeSpecifierNode: cat=", static_cast<int>(type_spec.category()),
			" token='", type_spec.token().value(), "' pointer_depth=", static_cast<int>(type_spec.pointer_depth()));
		const auto makeTypeSpecifierFromTemplateArg = [&](const TemplateTypeArg& arg) -> ASTNode {
			Token substituted_token = type_spec.token();
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				substituted_token = Token(
					is_struct_type(arg.category()) ? Token::Type::Identifier : Token::Type::Keyword,
					is_struct_type(arg.category()) ? StringTable::getStringView(arg_type_info->name()) : get_type_name(arg.category()),
					type_spec.token().line(),
					type_spec.token().column(),
					type_spec.token().file_index());
			} else if (std::string_view substituted_type_name = get_type_name(arg.category());
					   !substituted_type_name.empty() && substituted_type_name != "unknown"sv) {
				substituted_token = Token(
					Token::Type::Keyword,
					substituted_type_name,
					type_spec.token().line(),
					type_spec.token().column(),
					type_spec.token().file_index());
			}
			TypeSpecifierNode substituted_spec =
				makeTypeSpecifierFromTemplateTypeArg(arg, substituted_token);
			for (const PointerLevel& pointer_level : type_spec.pointer_levels()) {
				substituted_spec.add_pointer_level(pointer_level.cv_qualifier);
			}
			substituted_spec.add_cv_qualifier(type_spec.cv_qualifier());
			substituted_spec.set_reference_qualifier(collapseReferenceQualifiers(
				arg.ref_qualifier,
				type_spec.reference_qualifier()));
			return emplace_node<TypeSpecifierNode>(substituted_spec);
		};
		const auto makeTypeSpecifier = [&](const TypeInfo& target_type_info) -> ASTNode {
			const bool is_struct_like = target_type_info.type_index_.category() == TypeCategory::Struct ||
										target_type_info.getStructInfo() != nullptr;
			int size_bits = 0;
			if (is_struct_like) {
				size_bits = static_cast<int>(target_type_info.sizeInBits().value);
			} else {
				size_bits = target_type_info.hasStoredSize()
					? static_cast<int>(target_type_info.sizeInBits().value)
					: get_type_size_bits(target_type_info.type_index_.category());
			}
			Token substituted_token = type_spec.token();
			if (target_type_info.type_index_.category() == TypeCategory::Struct ||
				target_type_info.type_index_.category() == TypeCategory::UserDefined) {
				substituted_token = Token(
					Token::Type::Identifier,
					StringTable::getStringView(target_type_info.name()),
					type_spec.token().line(),
					type_spec.token().column(),
					type_spec.token().file_index());
			} else {
				std::string_view substituted_type_name = get_type_name(target_type_info.type_index_.category());
				if (!substituted_type_name.empty() && substituted_type_name != "unknown"sv) {
					substituted_token = Token(
						Token::Type::Keyword,
						substituted_type_name,
						type_spec.token().line(),
						type_spec.token().column(),
						type_spec.token().file_index());
				}
			}
			TypeSpecifierNode substituted_spec(
				target_type_info.type_index_,
				size_bits,
				substituted_token,
				type_spec.cv_qualifier(),
				type_spec.reference_qualifier());
			substituted_spec.copy_indirection_from(type_spec);
			substituted_spec.set_reference_qualifier(type_spec.reference_qualifier());
			return emplace_node<TypeSpecifierNode>(substituted_spec);
		};

		if (current_owner_type_name.isValid() && type_spec.type_index().is_valid()) {
			if (const TypeInfo* current_owner_type_info =
					findTypeByName(current_owner_type_name)) {
				const StructTypeInfo* current_owner_struct_info =
					current_owner_type_info->getStructInfo();
				if (current_owner_struct_info != nullptr &&
					current_owner_struct_info->isOwnTypeIndex(type_spec.type_index())) {
					return makeTypeSpecifier(*current_owner_type_info);
				}
			}
		}

		if (type_spec.type_index().is_valid()) {
			if (const TypeInfo* dependent_type_info = tryGetTypeInfo(type_spec.type_index());
				dependent_type_info != nullptr &&
				dependent_type_info->isDependentMemberType() &&
				dependent_type_info->hasDependentQualifiedName()) {
				auto sub_map = buildSubstitutionParamMap(template_params, template_args);
				ExpressionSubstitutor substitutor(sub_map.param_map, *this, sub_map.param_order);
				if (current_owner_type_name.isValid()) {
					substitutor.setCurrentOwnerTypeName(current_owner_type_name);
				}
				if (const TypeInfo* resolved_dependent_type =
						substitutor.resolveDependentMemberTypeForSubstitution(*dependent_type_info)) {
					TypeSpecifierNode substituted_spec =
						makeTypeSpecifierFromTemplateTypeArg(
							resolveTypeInfoToTemplateArg(*resolved_dependent_type, type_spec),
							type_spec.token());
					return emplace_node<TypeSpecifierNode>(substituted_spec);
				}
			}
		}

		if (std::optional<TypeSpecifierNode> materialized_alias_type =
				tryMaterializeDependentAliasTypeSpecifier(
					type_spec,
					template_params,
					template_args)) {
			return emplace_node<TypeSpecifierNode>(*materialized_alias_type);
		}

		// Check if this is a user-defined type that matches a template parameter,
		// or a dependent placeholder type (built-in category with placeholder type_index).
		if (type_spec.category() == TypeCategory::UserDefined ||
			type_spec.category() == TypeCategory::Struct ||
			type_spec.category() == TypeCategory::TypeAlias ||
			type_spec.category() == TypeCategory::Template ||
			type_spec.category() == TypeCategory::Auto ||
			typeSpecStillUsesDependentPlaceholder(type_spec)) {
			const TemplateTypeArg* matched_direct_template_arg = nullptr;
			bool exact_template_param_substituted = false;
			std::optional<ASTNode> exact_template_param_node;
			const StringHandle direct_type_name = getStructuredTypeName(type_spec);
			forEachNonPackTemplateParamArgBinding(
				template_params,
				template_args,
				[&](const TemplateParameterNode& template_param, const TemplateTypeArg& template_arg, size_t) {
					if (exact_template_param_substituted ||
						template_param.kind() != TemplateParameterKind::Type ||
						template_param.nameHandle() != direct_type_name ||
						!template_arg.isTypeArgument()) {
						return;
					}
					matched_direct_template_arg = &template_arg;
					exact_template_param_node = makeTypeSpecifierFromTemplateArg(template_arg);
					exact_template_param_substituted = true;
				});
			if (exact_template_param_substituted) {
				return *exact_template_param_node;
			}

			TypeIndex substituted_type_index = substitute_template_parameter(
				type_spec,
				template_params,
				template_args);
			if (substituted_type_index.category() != type_spec.type() || substituted_type_index != type_spec.type_index()) {
				if (const TypeInfo* substituted_type_info = tryGetTypeInfo(substituted_type_index)) {
					return makeTypeSpecifier(*substituted_type_info);
				}
				int substituted_size_bits = get_type_size_bits(substituted_type_index.category());
				Token substituted_token = type_spec.token();
				std::string_view substituted_type_name = get_type_name(substituted_type_index.category());
				if (!substituted_type_name.empty() && substituted_type_name != "unknown"sv) {
					substituted_token = Token(
						Token::Type::Keyword,
						substituted_type_name,
						type_spec.token().line(),
						type_spec.token().column(),
						type_spec.token().file_index());
				}
				TypeSpecifierNode substituted_spec(
					substituted_type_index,
					substituted_size_bits,
					substituted_token,
					type_spec.cv_qualifier(),
					type_spec.reference_qualifier());
				substituted_spec.copy_indirection_from(type_spec);
				substituted_spec.set_reference_qualifier(type_spec.reference_qualifier());
				if (matched_direct_template_arg != nullptr) {
					for (size_t i = 0; i < matched_direct_template_arg->pointer_depth; ++i) {
						CVQualifier cv = i < matched_direct_template_arg->pointer_cv_qualifiers.size()
							? matched_direct_template_arg->pointer_cv_qualifiers[i]
							: CVQualifier::None;
						substituted_spec.add_pointer_level(cv);
					}
					substituted_spec.set_reference_qualifier(collapseReferenceQualifiers(
						matched_direct_template_arg->ref_qualifier,
						substituted_spec.reference_qualifier()));
					if (!substituted_spec.has_function_signature() &&
						matched_direct_template_arg->function_signature.has_value()) {
						substituted_spec.set_function_signature(*matched_direct_template_arg->function_signature);
					}
					const int resolved_size_bits = getTypeSpecSizeBits(substituted_spec);
					if (resolved_size_bits > 0) {
						substituted_spec.set_size_in_bits(resolved_size_bits);
					}
				}
				return emplace_node<TypeSpecifierNode>(substituted_spec);
			}
		}

		// Remap nested struct types from template pattern to instantiated version.
		// When a member function body references a nested type like Inner{}, the
		// TypeSpecifierNode still carries the pattern's type_index (e.g., "Outer::Inner").
		// We need to remap it to the instantiated version (e.g., "Outer$hash::Inner").
		if ((type_spec.category() == TypeCategory::Struct ||
			 type_spec.category() == TypeCategory::UserDefined ||
			 type_spec.category() == TypeCategory::TypeAlias) &&
			type_spec.type_index().is_valid()) {
			if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
				std::string_view type_name = StringTable::getStringView(type_info->name());
				// Use the rightmost scope separator so namespaced patterns like
				// "ns::Outer::Inner" split into parent="ns::Outer", nested="Inner".
				auto sep_pos = type_name.rfind("::");
				if (sep_pos != std::string_view::npos) {
					std::string_view parent_name = type_name.substr(0, sep_pos);
					std::string_view nested_name = type_name.substr(sep_pos + 2);
					// Try both the fully-qualified template name ("ns::Outer"), its short
					// name ("Outer"), and the base template name extracted from hashed
					// placeholders ("Outer" from "Outer$hash").
					std::vector<std::string_view> template_owner_candidates;
					template_owner_candidates.push_back(parent_name);
					if (std::string_view base_parent = extractBaseTemplateName(parent_name); !base_parent.empty()) {
						template_owner_candidates.push_back(base_parent);
					}
					size_t parent_sep_pos = parent_name.rfind("::");
					if (parent_sep_pos != std::string_view::npos) {
						template_owner_candidates.push_back(parent_name.substr(parent_sep_pos + 2));
					}
					std::vector<TemplateTypeArg> args_vec(template_args.begin(), template_args.end());
					for (const std::string_view& parent_candidate : template_owner_candidates) {
						auto template_opt = gTemplateRegistry.lookupTemplate(parent_candidate);
						if (!template_opt.has_value()) {
							continue;
						}
						std::string_view inst_parent = FlashCpp::generateInstantiatedNameFromArgs(parent_candidate, args_vec);
						StringBuilder sb;
						StringHandle inst_nested_handle = StringTable::getOrInternStringHandle(
							sb.append(inst_parent).append("::"sv).append(nested_name).commit());
						auto it = getTypesByNameMap().find(inst_nested_handle);
						if (it == getTypesByNameMap().end()) {
							continue;
						}
						const TypeInfo* inst_type_info = it->second;
						FLASH_LOG(Templates, Trace,
							"Remapped nested struct type '", type_name,
							"' -> '", StringTable::getStringView(inst_type_info->name()), "'");
						return makeTypeSpecifier(*inst_type_info);
					}
				}
			}
		}

		return node;

	} else if (node.is<InitializerListNode>()) {
		const InitializerListNode& init_list = node.as<InitializerListNode>();
		auto new_init_list = emplace_node<InitializerListNode>();
		InitializerListNode& new_init_list_ref = new_init_list.as<InitializerListNode>();

		for (size_t i = 0; i < init_list.initializers().size(); ++i) {
			const ASTNode& element = init_list.initializers()[i];
			// Expand PackExpansionExprNode elements (e.g. {args...}) into multiple
			// substituted initializers. Designated initializers cannot be pack expansions
			// per C++20 [dcl.init.aggr], so only apply to non-designated slots.
			if (!init_list.is_designated(i) && element.is<ExpressionNode>()) {
				const ExpressionNode& elem_expr = element.as<ExpressionNode>();
				if (const auto* pack_expansion_expr = std::get_if<PackExpansionExprNode>(&elem_expr)) {
					ChunkedVector<ASTNode> expanded;
					if (expandPackExpansionArgs(*pack_expansion_expr, template_params, template_args, expanded)) {
						for (size_t k = 0; k < expanded.size(); ++k) {
							new_init_list_ref.add_initializer(expanded[k]);
						}
						continue;
					}
				}
			}
			ASTNode substituted_init = substitute_nested(element);
			if (init_list.is_designated(i)) {
				new_init_list_ref.add_designated_initializer(init_list.member_name(i), substituted_init);
			} else {
				new_init_list_ref.add_initializer(substituted_init);
			}
		}

		return new_init_list;

	} else if (node.is<BlockNode>()) {
		// Handle block nodes by substituting in all statements
		const BlockNode& block = node.as<BlockNode>();
		const bool introduces_scope = !block.is_synthetic_decl_list();
		const size_t saved_binding_count = state.local_bindings.size();
		const size_t saved_constexpr_count = state.constexpr_locals.size();

		auto new_block = emplace_node<BlockNode>();
		BlockNode& new_block_ref = new_block.as<BlockNode>();
		new_block_ref.set_synthetic_decl_list(block.is_synthetic_decl_list());

		for (size_t i = 0; i < block.get_statements().size(); ++i) {
			new_block_ref.add_statement_node(substitute_nested(block.get_statements()[i]));
		}

		if (introduces_scope) {
			state.local_bindings.resize(saved_binding_count);
			state.constexpr_locals.resize(saved_constexpr_count);
		}

		return new_block;

	} else if (node.is<ForStatementNode>()) {
		// Handle for statements — init is in scope for condition/update/body.
		const ForStatementNode& for_stmt = node.as<ForStatementNode>();
		const size_t saved_binding_count = state.local_bindings.size();
		const size_t saved_constexpr_count = state.constexpr_locals.size();

		auto init_stmt = for_stmt.get_init_statement().has_value()
			? std::optional<ASTNode>(substitute_nested(*for_stmt.get_init_statement()))
			: std::nullopt;
		auto condition = for_stmt.get_condition().has_value()
			? std::optional<ASTNode>(substitute_nested(*for_stmt.get_condition()))
			: std::nullopt;
		auto update_expr = for_stmt.get_update_expression().has_value()
			? std::optional<ASTNode>(substitute_nested(*for_stmt.get_update_expression()))
			: std::nullopt;
		auto body_stmt = substitute_nested(for_stmt.get_body_statement());

		state.local_bindings.resize(saved_binding_count);
		state.constexpr_locals.resize(saved_constexpr_count);
		return emplace_node<ForStatementNode>(init_stmt, condition, update_expr, body_stmt);

	} else if (node.is<UnaryOperatorNode>()) {
		// Handle unary operators
		const UnaryOperatorNode& unary_op = node.as<UnaryOperatorNode>();

		ASTNode substituted_operand = substitute_nested(unary_op.get_operand());

		return emplace_node<UnaryOperatorNode>(
			unary_op.get_token(),
			substituted_operand,
			unary_op.is_prefix(),
			unary_op.is_builtin_addressof());

	} else if (node.is<VariableDeclarationNode>()) {
		// Handle variable declarations
		const VariableDeclarationNode& var_decl = node.as<VariableDeclarationNode>();
		ASTNode substituted_decl = substitute_nested(var_decl.declaration_node());
		if (substituted_decl.is<DeclarationNode>()) {
			DeclarationNode& local_decl = substituted_decl.as<DeclarationNode>();
			if (local_decl.identifier_token().handle().isValid()) {
				state.local_bindings.push_back({
					local_decl.identifier_token().handle(),
					&local_decl
				});
			}
		}

		auto initializer = var_decl.initializer().has_value() ? std::optional<ASTNode>(substitute_nested(*var_decl.initializer())) : std::nullopt;

		// Phase 6: for unsized arrays with pack-expanded initializers (e.g.
		//   template<typename... Ts> f(Ts... args) { int arr[] = {args...}; }
		// the parser-time inference set the outer dimension to 1 (the single
		// PackExpansionExprNode).  After substitution the initializer list has
		// been expanded into N concrete elements, so re-run the unsized-array
		// size inference against the substituted initializer.  Emplace a fresh
		// TypeSpecifierNode copy so we don't mutate the template pattern's
		// shared TypeSpecifierNode (which is returned unchanged for
		// non-template types like `int`).
		if (substituted_decl.is<DeclarationNode>() && initializer.has_value() &&
			initializer->is<InitializerListNode>()) {
			DeclarationNode& sub_decl_ref = substituted_decl.as<DeclarationNode>();
			if (sub_decl_ref.is_unsized_array()) {
				const TypeSpecifierNode& current_spec =
					sub_decl_ref.type_specifier_node();
				TypeSpecifierNode fresh_type_node = current_spec;
				inferUnsizedArraySizeFromInitializer(sub_decl_ref,
					fresh_type_node,
					initializer);
				sub_decl_ref.set_type_node(fresh_type_node);
			}
		}
		if (substituted_decl.is<DeclarationNode>() && initializer.has_value()) {
			DeclarationNode& sub_decl_ref = substituted_decl.as<DeclarationNode>();
			const TypeCategory placeholder_cat =
				sub_decl_ref.type_specifier_node().type();
			if (isPlaceholderAutoType(placeholder_cat)) {
				std::optional<TypeSpecifierNode> deduced_type =
					get_expression_type(*initializer);
				// When typing still treats the call as dependent-unqualified but
				// the callee declaration is known, recover its return type and
				// rebind through the active substitution (view_interface::_Cast).
				if (!deduced_type.has_value()) {
					const CallExprNode* call_expr = nullptr;
					if (initializer->is<CallExprNode>()) {
						call_expr = &initializer->as<CallExprNode>();
					} else if (initializer->is<ExpressionNode>()) {
						call_expr = std::get_if<CallExprNode>(
							&initializer->as<ExpressionNode>());
					}
					if (call_expr != nullptr) {
						if (const FunctionDeclarationNode* callee_func =
								getBestEffortDirectCallTarget(*call_expr);
							callee_func != nullptr) {
							deduced_type = callee_func->decl_node().type_specifier_node();
						} else if (call_expr->has_parser_return_type_hint()) {
							deduced_type = call_expr->parser_return_type_hint();
						}
					}
				}
				if (deduced_type.has_value()) {
					// Call return types may still spell template parameters
					// (CRTP Derived& / _Derived&) until run through the active
					// substitution map. Rebind before deciding to defer.
					if (typeSpecStillUsesDependentPlaceholder(*deduced_type)) {
						ASTNode rebound_type_node = substitute_nested(
							emplace_node<TypeSpecifierNode>(*deduced_type));
						if (rebound_type_node.is<TypeSpecifierNode>()) {
							deduced_type = rebound_type_node.as<TypeSpecifierNode>();
						}
					}
					if (typeSpecStillUsesDependentPlaceholder(*deduced_type)) {
						FLASH_LOG(Parser, Debug,
								  "Deferred substituted auto local; initializer type still dependent");
					} else {
						sub_decl_ref.set_type_node(
							applyPlaceholderDeclaratorDeduction(
								placeholder_cat,
								sub_decl_ref.type_specifier_node(),
								*deduced_type));
					}
				} else if (!isDependentTemplateContext()) {
					throw CompileError(std::string(StringBuilder()
						.append("Unable to deduce 'auto' type for instantiated local variable '")
						.append(sub_decl_ref.identifier_token().value())
						.append("' from initializer expression")
						.commit()));
				}
			}
		}

		ASTNode new_var_node = emplace_node<VariableDeclarationNode>(substituted_decl, initializer, var_decl.storage_class());
		VariableDeclarationNode& new_var = new_var_node.as<VariableDeclarationNode>();
		setOuterTemplateBindingsFromParams(new_var, template_params, template_args);

		// Preserve constexpr/constinit flags
		new_var.set_is_thread_local(var_decl.is_thread_local());
		if (var_decl.is_constexpr())
			new_var.set_is_constexpr(true);
		if (var_decl.is_constinit())
			new_var.set_is_constinit(true);

		// For constexpr variables with substituted initializers, update the symbol table
		// so that subsequent if constexpr conditions can look up the concrete value
		if (var_decl.is_constexpr() && initializer.has_value()) {
			std::string_view var_name = var_decl.declaration().identifier_token().value();
			gSymbolTable.insert(var_name, new_var_node);
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
			auto constexpr_bindings = bind_constexpr_locals();
			eval_ctx.local_bindings = &constexpr_bindings;
			ConstExpr::EvalResult init_result =
				ConstExpr::Evaluator::evaluate(*initializer, eval_ctx);
			if (init_result.success()) {
				state.constexpr_locals.push_back({var_name, init_result.as_int()});
			}
		}

		return new_var_node;

	} else if (node.is<ReturnStatementNode>()) {
		// Handle return statements
		const ReturnStatementNode& ret_stmt = node.as<ReturnStatementNode>();

		auto expr = ret_stmt.expression().has_value() ? std::optional<ASTNode>(substitute_nested(*ret_stmt.expression())) : std::nullopt;

		return emplace_node<ReturnStatementNode>(expr, ret_stmt.return_token());

	} else if (node.is<IfStatementNode>()) {
		// Handle if statements — init is substituted before condition/branches
		// and introduces a scope spanning the whole if ([stmt.if]/2).
		const IfStatementNode& if_stmt = node.as<IfStatementNode>();
		const size_t saved_binding_count = state.local_bindings.size();
		const size_t saved_constexpr_count = state.constexpr_locals.size();

		auto substituted_init = if_stmt.get_init_statement().has_value()
			? std::optional<ASTNode>(substitute_nested(*if_stmt.get_init_statement()))
			: std::nullopt;
		ASTNode substituted_condition = substitute_nested(if_stmt.get_condition());

		// For if constexpr, evaluate the condition at compile time and eliminate the dead branch
		if (if_stmt.is_constexpr()) {
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
			auto constexpr_bindings = bind_constexpr_locals();
			eval_ctx.local_bindings = &constexpr_bindings;
			auto eval_result = ConstExpr::Evaluator::evaluate(substituted_condition, eval_ctx);
			if (eval_result.success()) {
				bool condition_value = eval_result.as_bool();
				FLASH_LOG(Templates, Trace, "if constexpr condition evaluated to ", condition_value ? "true" : "false");
				ASTNode selected;
				if (condition_value) {
					selected = substitute_nested(if_stmt.get_then_statement());
				} else if (if_stmt.has_else()) {
					selected = substitute_nested(*if_stmt.get_else_statement());
				} else {
					selected = emplace_node<BlockNode>();
				}
				state.local_bindings.resize(saved_binding_count);
				state.constexpr_locals.resize(saved_constexpr_count);
				return selected;
			}
		}

		ASTNode substituted_then = substitute_nested(if_stmt.get_then_statement());
		auto substituted_else = if_stmt.get_else_statement().has_value()
			? std::optional<ASTNode>(substitute_nested(*if_stmt.get_else_statement()))
			: std::nullopt;

		state.local_bindings.resize(saved_binding_count);
		state.constexpr_locals.resize(saved_constexpr_count);
		return emplace_node<IfStatementNode>(
			substituted_condition,
			substituted_then,
			substituted_else,
			substituted_init,
			if_stmt.is_constexpr());

	} else if (node.is<WhileStatementNode>()) {
		// Handle while statements
		const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();

		ASTNode substituted_condition = substitute_nested(while_stmt.get_condition());
		ASTNode substituted_body = substitute_nested(while_stmt.get_body_statement());

		return emplace_node<WhileStatementNode>(substituted_condition, substituted_body);
	}

	// For other node types, return as-is (simplified implementation)
	return node;
}

// Extract base template name from a mangled template instantiation name
// Supports underscore-based naming: "enable_if_void_int" -> "enable_if"
// Future: Will support hash-based naming: "enable_if$abc123" -> "enable_if"
//
// Tries progressively longer prefixes by searching for '_' separators
// until a registered template or alias template is found.
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name(std::string_view mangled_name) {
	// Try progressively longer prefixes until we find a registered template
	size_t underscore_pos = 0;

	while ((underscore_pos = mangled_name.find('_', underscore_pos)) != std::string_view::npos) {
		std::string_view candidate = mangled_name.substr(0, underscore_pos);

		// Check if this is a registered class template
		auto candidate_opt = gTemplateRegistry.lookupTemplate(candidate);
		if (candidate_opt.has_value()) {
			FLASH_LOG(Templates, Trace, "extract_base_template_name: found template '",
					  candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}

		// Also check alias templates
		auto alias_candidate = gTemplateRegistry.lookup_alias_template(candidate);
		if (alias_candidate.has_value()) {
			FLASH_LOG(Templates, Trace, "extract_base_template_name: found alias template '",
					  candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}

		underscore_pos++; // Move past this underscore
	}

	return {};  // Not found
}

// Extract base template name by stripping suffixes from right to left
// Used when we have an instantiated name like "Container_int_float"
// and need to find "Container"
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name_by_stripping(std::string_view instantiated_name) {
	std::string_view base_template_name = instantiated_name;

	// Try progressively stripping '_suffix' patterns until we find a registered template
	while (!base_template_name.empty()) {
		// Check if current name is a registered template
		auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
		if (template_opt.has_value()) {
			FLASH_LOG(Templates, Trace, "extract_base_template_name_by_stripping: found template '",
					  base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}

		// Also check alias templates
		auto alias_opt = gTemplateRegistry.lookup_alias_template(base_template_name);
		if (alias_opt.has_value()) {
			FLASH_LOG(Templates, Trace, "extract_base_template_name_by_stripping: found alias template '",
					  base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}

		// Strip last suffix
		size_t underscore_pos = base_template_name.find_last_of('_');
		if (underscore_pos == std::string_view::npos) {
			break;  // No more underscores to strip
		}

		base_template_name = base_template_name.substr(0, underscore_pos);
	}

	return {};  // Not found
}
// Helper: resolve a type name within the current namespace context (including using directives)
const TypeInfo* lookupTypeInCurrentContext(StringHandle type_handle) {
	const std::string_view type_name = StringTable::getStringView(type_handle);

	// Prefer declarations from the active symbol scope for unqualified type names.
	// This keeps block/function-local types authoritative even when
	// getTypesByNameMap() still holds an older outer-scope type under the same key.
	if (type_name.find("::") == std::string_view::npos) {
		if (std::optional<ASTNode> symbol = gSymbolTable.lookup(type_name);
			symbol.has_value()) {
			TypeIndex scoped_type_index;
			if (symbol->is<EnumDeclarationNode>()) {
				scoped_type_index = symbol->as<EnumDeclarationNode>().type_index();
			} else if (symbol->is<TypedefDeclarationNode>()) {
				scoped_type_index = symbol->as<TypedefDeclarationNode>()
					.type_specifier_node()
					.type_index();
			}
			if (const TypeInfo* scoped_type_info = tryGetTypeInfo(scoped_type_index)) {
				return scoped_type_info;
			}
		}
	}

	auto isDirectlyVisibleUnqualified = [&](const TypeInfo* type_info) {
		if (!type_info) {
			return false;
		}

		if (type_name.find("::") != std::string_view::npos) {
			return true;
		}

		NamespaceHandle decl_ns = type_info->namespaceHandle();
		if (!decl_ns.isValid() || decl_ns.isGlobal()) {
			return true;
		}

		// Namespace members may be registered under a short alias in gTypesByName for
		// internal convenience, but ordinary unqualified lookup must not treat that as
		// visible outside the proper namespace / using context.
		// Accept only exact canonical-name matches for bare names so `X` does not
		// become visible merely because `ns::X` was also inserted under a short alias.
		return type_info->name() == type_handle;
	};

	// Walk current namespace chain outward first (e.g., physics::Vector, ::Vector)
	// This must come before the unqualified lookup so that namespace-local types
	// are preferred over short aliases registered by other namespaces.
	NamespaceHandle ns_handle = gSymbolTable.get_current_namespace_handle();
	while (ns_handle.isValid() && !ns_handle.isGlobal()) {
		StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(ns_handle, type_handle);
		auto q_it = getTypesByNameMap().find(qualified);
		if (q_it != getTypesByNameMap().end()) {
			return q_it->second;
		}
		ns_handle = gNamespaceRegistry.getParent(ns_handle);
	}

	// Then try direct unqualified lookup, subject to namespace visibility rules.
	// Note: local type shadowing is already handled by the symbol-table lookup above,
	// which is scope-aware and always returns the innermost declaration.
	auto it = getTypesByNameMap().find(type_handle);
	if (it != getTypesByNameMap().end() && isDirectlyVisibleUnqualified(it->second)) {
		return it->second;
	}

	// Exact using declarations in block/enclosing scopes.
	// C++ unqualified lookup can find a type introduced by `using ns::Type;`,
	// but it must not guess unrelated `other::Type` declarations by suffix.
	auto using_declarations = gSymbolTable.get_current_using_declaration_handles();
	auto using_decl_it = using_declarations.find(type_name);
	if (using_decl_it != using_declarations.end()) {
		const auto& [using_namespace, original_name] = using_decl_it->second;
		StringHandle original_handle = StringTable::getOrInternStringHandle(original_name);
		StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(using_namespace, original_handle);
		auto imported_it = getTypesByNameMap().find(qualified);
		if (imported_it != getTypesByNameMap().end()) {
			return imported_it->second;
		}
	}

	// Exact using directives
	for (NamespaceHandle using_ns : gSymbolTable.get_current_using_directive_handles()) {
		if (!using_ns.isValid())
			continue;
		StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(using_ns, type_handle);
		auto u_it = getTypesByNameMap().find(qualified);
		if (u_it != getTypesByNameMap().end()) {
			return u_it->second;
		}
	}

	return nullptr;
}

// Expand a PackExpansionExprNode into multiple substituted arguments for function calls.
// For each pack element, the pattern expression is cloned with the pack identifier replaced,
// then template parameters are substituted.
bool Parser::expandPackExpansionArgs(
	const PackExpansionExprNode& pack_expansion,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	ChunkedVector<ASTNode>& out_args) {
	return expandPackExpansionArgs(
		pack_expansion,
		template_params,
		template_args,
		std::span<const PackParamInfo>(pack_param_info_.data(), pack_param_info_.size()),
		out_args);
}

bool Parser::expandPackExpansionArgs(
	const PackExpansionExprNode& pack_expansion,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	std::span<const PackParamInfo> function_pack_infos,
	ChunkedVector<ASTNode>& out_args) {
	const ASTNode& pattern = pack_expansion.pattern();
	InlineVector<std::pair<size_t, size_t>, 4> template_param_arg_ranges;
	template_param_arg_ranges.reserve(template_params.size());

	// Keep the argument boundary recorded by deduction. In particular, a
	// deduced pack followed by a defaulted template parameter is represented as
	// one flat argument sequence, so counting all remaining arguments here can
	// incorrectly consume the default as another pack element.
	size_t arg_cursor = 0;
	for (size_t param_index = 0; param_index < template_params.size(); ++param_index) {
		const TemplateParameterNode& param = template_params[param_index];
		if (!param.is_variadic()) {
			template_param_arg_ranges.push_back({
				arg_cursor,
				arg_cursor < template_args.size() ? 1u : 0u});
			if (arg_cursor < template_args.size()) {
				++arg_cursor;
			}
			continue;
		}

		const size_t pack_size = countTemplatePackArguments(
			template_params,
			template_args,
			param_index,
			arg_cursor);
		template_param_arg_ranges.push_back({arg_cursor, pack_size});
		arg_cursor += pack_size;
	}

	std::optional<size_t> num_pack_elements;
	for (size_t p = 0; p < template_params.size(); ++p) {
		const TemplateParameterNode& tparam = template_params[p];
		if (!tparam.is_variadic() || !exprContainsIdentifier(pattern, tparam.name())) {
			continue;
		}
		const size_t pack_size = template_param_arg_ranges[p].second;
		if (!num_pack_elements.has_value()) {
			num_pack_elements = pack_size;
		} else if (*num_pack_elements != pack_size) {
			throw InternalError("Mismatched template parameter pack sizes while expanding call arguments");
		}
	}
	// Also check pack_param_info_ for function parameter packs. Match the pack
	// name against the pattern so empty packs are still recognized and consumed
	// instead of leaking a PackExpansionExprNode across the parser/sema boundary.
	InlineVector<std::string_view, 4> matched_function_pack_names;
	for (const auto& pack_info : function_pack_infos) {
		if (exprContainsIdentifier(pattern, pack_info.original_name)) {
			matched_function_pack_names.push_back(pack_info.original_name);
			if (!num_pack_elements.has_value()) {
				num_pack_elements = pack_info.pack_size;
			} else if (*num_pack_elements != pack_info.pack_size) {
				throw InternalError("Mismatched function parameter pack sizes while expanding call arguments");
			}
		}
	}

	if (!num_pack_elements.has_value())
		return false;
	if (*num_pack_elements == 0) {
		FLASH_LOG(Templates, Trace, "Expanding PackExpansionExprNode in function call args: empty pack");
		return true;
	}

	FLASH_LOG(Templates, Trace, "Expanding PackExpansionExprNode in function call args: ", *num_pack_elements, " elements");
	for (size_t pi = 0; pi < *num_pack_elements; ++pi) {
		// Build substitution params for this single pack element
		InlineVector<TemplateParameterNode, 4> subst_params;
		InlineVector<TemplateTypeArg, 4> subst_args;
		for (size_t p = 0; p < template_params.size(); ++p) {
			const auto& tparam = template_params[p];
			if (tparam.is_variadic()) {
				const auto& [pack_start, pack_size] = template_param_arg_ranges[p];
				if (pi >= pack_size) {
					continue;
				}
				TemplateParameterNode single_tparam = tparam;
				single_tparam.set_variadic(false);
				subst_params.push_back(single_tparam);
				subst_args.push_back(template_args[pack_start + pi]);
			} else if (template_param_arg_ranges[p].second != 0) {
				subst_params.push_back(template_params[p]);
				subst_args.push_back(template_args[template_param_arg_ranges[p].first]);
			}
		}

		// Replace the function parameter pack identifier (e.g., "args") with
		// the expanded element name (e.g., "args_0") in the pattern before substitution
		ASTNode expanded_pattern = pattern;
		for (std::string_view func_pack_name : matched_function_pack_names) {
			expanded_pattern = replacePackIdentifierInExpr(expanded_pattern, func_pack_name, pi);
		}
		ASTNode substituted = substituteTemplateParameters(expanded_pattern, std::span<const TemplateParameterNode>(subst_params.data(), subst_params.size()), std::span<const TemplateTypeArg>(subst_args.data(), subst_args.size()));
		out_args.push_back(substituted);
	}
	return true;
}

// Substitute a single argument, expanding PackExpansionExprNode when present.
// Consolidates the repeated check-expand-or-substitute pattern used in
// ordinary call-expression, constructor-call, and member-call handlers.
void Parser::substituteArgWithPackExpansion(
	const ASTNode& arg,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	TemplateBodySubstitutionState& state,
	ChunkedVector<ASTNode>& out) {
	if (arg.is<ExpressionNode>()) {
		const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
		if (const auto* pack_expansion_expr = std::get_if<PackExpansionExprNode>(&arg_expr)) {
			if (expandPackExpansionArgs(*pack_expansion_expr, template_params, template_args, out)) {
				return;
			}
		}
	}
	out.push_back(substituteTemplateParametersWithState(
		arg,
		template_params,
		template_args,
		state));
}

// Replace a pack parameter identifier in an expression pattern with its expanded element name.
// For example, given pattern `identity(args)` and pack_name="args", element_index=2,
// this returns `identity(args_2)`.
// Recursively walks the expression tree to find and replace IdentifierNodes matching pack_name.
ASTNode Parser::replacePackIdentifierInExpr(const ASTNode& expr, std::string_view pack_name, size_t element_index) {
	if (!expr.has_value() || pack_name.empty()) {
		return expr;
	}
	if (expr.is<TypeSpecifierNode>()) {
		return expr;
	}

	const auto replace_identifier = [&](const IdentifierNode& identifier) -> ASTNode {
		if (identifier.name() != pack_name) {
			return ASTNode();
		}
		StringBuilder expanded_name;
		expanded_name.append(pack_name);
		expanded_name.append('_');
		expanded_name.append(element_index);
		Token new_token(Token::Type::Identifier, expanded_name.commit(), 0, 0, 0);
		return emplace_node<ExpressionNode>(createBoundIdentifier(new_token));
	};

	if (expr.is<IdentifierNode>()) {
		if (ASTNode replacement = replace_identifier(expr.as<IdentifierNode>()); replacement.has_value()) {
			return replacement;
		}
	}
	if (expr.is<ExpressionNode>()) {
		const ExpressionNode& expression = expr.as<ExpressionNode>();
		if (const auto* identifier = std::get_if<IdentifierNode>(&expression)) {
			if (ASTNode replacement = replace_identifier(*identifier); replacement.has_value()) {
				return replacement;
			}
		}
	}

	ExpressionRewriter rewriter;
	const auto rewrite_one_to_one = [&](const ASTNode& child, ExpressionStructure::ExpressionChildRole) {
		return replacePackIdentifierInExpr(child, pack_name, element_index);
	};
	const auto rewrite_zero_to_many = [&](const ASTNode& child, ExpressionStructure::ExpressionChildRole, std::vector<ASTNode>& output) {
		output.push_back(replacePackIdentifierInExpr(child, pack_name, element_index));
	};
	return rewriter.rewrite(expr, rewrite_one_to_one, rewrite_zero_to_many);
}

InlineVector<ASTNode, 4> Parser::expandPackExpressionArgument(const ASTNode& pattern) {
	InlineVector<const PackParamInfo*, 4> packs_in_expr;
	for (const auto& pack_info : pack_param_info_) {
		if (pack_info.pack_size > 0 && exprContainsIdentifier(pattern, pack_info.original_name)) {
			packs_in_expr.push_back(&pack_info);
		}
	}

	if (packs_in_expr.empty()) {
		return {};
	}

	size_t pack_size = packs_in_expr[0]->pack_size;
	for (size_t pack_index = 1; pack_index < packs_in_expr.size(); ++pack_index) {
		if (packs_in_expr[pack_index]->pack_size != pack_size) {
			FLASH_LOG(Parser, Error, "Pack expansion contains parameter packs of different lengths");
			return {pattern};
		}
	}

	InlineVector<ASTNode, 4> expanded_args;
	expanded_args.reserve(pack_size);
	for (size_t i = 0; i < pack_size; ++i) {
		ASTNode expanded_pattern = pattern;
		for (const auto* pack_info : packs_in_expr) {
			expanded_pattern = replacePackIdentifierInExpr(expanded_pattern, pack_info->original_name, i);
		}
		expanded_args.push_back(expanded_pattern);
	}
	return expanded_args;
}
