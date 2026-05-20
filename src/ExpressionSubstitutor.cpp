#include "ExpressionSubstitutor.h"
#include "CallNodeHelpers.h"
#include "Parser.h"
#include "TemplateInstantiationHelper.h"
#include "AstTraversal.h"
#include "Log.h"
#include <charconv>
#include <limits>

namespace {

// Bound recursive placeholder unwrapping so malformed alias cycles cannot recurse indefinitely.
static constexpr int kMaxDependentMemberTypeResolutionDepth = 16;
static constexpr int kInitialDependentMemberTypeResolutionDepth = 0;

bool parseUnsignedDecimal(std::string_view text, uint64_t& value) {
	auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	return ec == std::errc{} && ptr == text.data() + text.size();
}

bool parseSignedDecimal(std::string_view text, int64_t& value) {
	auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
	return ec == std::errc{} && ptr == text.data() + text.size();
}

bool decodeTTPPlaceholderArg(std::string_view encoded_arg, TemplateTypeArg& decoded_arg) {
	if (encoded_arg.empty()) {
		return false;
	}

	if (encoded_arg.front() == 'p') {
		uint64_t raw_handle = 0;
		if (!parseUnsignedDecimal(encoded_arg.substr(1), raw_handle) ||
			raw_handle > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		StringHandle handle;
		handle.handle = static_cast<uint32_t>(raw_handle);
		if (!handle.isValid()) {
			return false;
		}
		decoded_arg = TemplateTypeArg::makeTemplate(handle);
		return true;
	}

	if (encoded_arg.front() != 't' && encoded_arg.front() != 'v') {
		return false;
	}

	size_t colon = encoded_arg.find(':', 1);
	if (colon == std::string_view::npos) {
		return false;
	}

	uint64_t raw_category = 0;
	if (!parseUnsignedDecimal(encoded_arg.substr(1, colon - 1), raw_category) ||
		raw_category > std::numeric_limits<uint8_t>::max()) {
		return false;
	}
	TypeCategory category = static_cast<TypeCategory>(static_cast<uint8_t>(raw_category));

	if (encoded_arg.front() == 'v') {
		int64_t value = 0;
		if (!parseSignedDecimal(encoded_arg.substr(colon + 1), value)) {
			return false;
		}
		decoded_arg = TemplateTypeArg(value, category);
		return true;
	}

	uint64_t raw_index = 0;
	if (!parseUnsignedDecimal(encoded_arg.substr(colon + 1), raw_index) ||
		raw_index > std::numeric_limits<uint32_t>::max()) {
		return false;
	}

	if (is_builtin_type(category)) {
		TypeIndex builtin_index = nativeTypeIndex(category);
		// Builtin categories are encoded with their canonical native TypeIndex slot so
		// placeholder names remain unambiguous across parse/substitution. Reject
		// mismatches here to avoid silently materializing the wrong builtin type.
		if (!builtin_index.is_valid() || builtin_index.index() != raw_index) {
			return false;
		}
		decoded_arg.type_index = builtin_index;
		decoded_arg.setCategory(category);
		return true;
	}

	decoded_arg.type_index = TypeIndex{static_cast<uint32_t>(raw_index), category};
	decoded_arg.setCategory(category);
	return true;
}

int computeTypeSizeInBits(TypeIndex type_index) {
	int size_in_bits = get_type_size_bits(type_index.category());
	if (size_in_bits != 0) {
		return size_in_bits;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		return 0;
	}

	size_in_bits = type_info->sizeInBits().value;
	if (size_in_bits != 0 || !type_info->isStruct()) {
		return size_in_bits;
	}

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	return struct_info ? struct_info->sizeInBits().value : 0;
}

std::vector<ASTNode> materializeTemplateArgumentNodesForQualifiedId(
	std::span<const TemplateTypeArg> template_args,
	const Token& source_token) {
	std::vector<ASTNode> result;
	result.reserve(template_args.size());

	for (const TemplateTypeArg& arg : template_args) {
		if (arg.is_dependent || arg.dependent_name.isValid()) {
			Token dep_token(
				Token::Type::Identifier,
				arg.dependent_name.view(),
				source_token.line(),
				source_token.column(),
				source_token.file_index());
			ExpressionNode& dep_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
				TemplateParameterReferenceNode(arg.dependent_name, dep_token));
			result.push_back(ASTNode(&dep_expr));
			continue;
		}

		if (arg.is_value) {
			if (arg.typeEnum() == TypeCategory::Bool) {
				Token bool_token(
					Token::Type::Keyword,
					arg.value != 0 ? "true"sv : "false"sv,
					source_token.line(),
					source_token.column(),
					source_token.file_index());
				ExpressionNode& bool_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
					BoolLiteralNode(bool_token, arg.value != 0));
				result.push_back(ASTNode(&bool_expr));
			} else {
				StringBuilder text_builder;
				std::string_view literal_text = text_builder.append(arg.value).commit();
				TypeCategory literal_type = arg.typeEnum() == TypeCategory::Invalid
					? TypeCategory::Int
					: arg.typeEnum();
				Token literal_token(
					Token::Type::Literal,
					literal_text,
					source_token.line(),
					source_token.column(),
					source_token.file_index());
				ExpressionNode& literal_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
					NumericLiteralNode(
						literal_token,
						static_cast<unsigned long long>(arg.value),
						literal_type,
						TypeQualifier::None,
						get_type_size_bits(literal_type)));
				result.push_back(ASTNode(&literal_expr));
			}
			continue;
		}

		TypeIndex arg_type_index = arg.type_index;
		if (!arg_type_index.is_valid()) {
			if (arg.typeEnum() == TypeCategory::Invalid) {
				continue;
			}
			arg_type_index = TypeIndex{}.withCategory(arg.typeEnum());
		}

		int size_in_bits = computeTypeSizeInBits(arg_type_index);
		TypeSpecifierNode& type_node = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			arg_type_index,
			size_in_bits,
			source_token,
			CVQualifier::None,
			ReferenceQualifier::None);
		result.push_back(ASTNode(&type_node));
	}

	return result;
}

std::optional<std::string_view> tryResolveCanonicalTypeName(
	const TypeSpecifierNode& type,
	std::string_view type_name,
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map) {
	if (!type.type_index().is_valid()) {
		return std::nullopt;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type.type_index());
	if (!type_info) {
		return std::nullopt;
	}

	std::string_view canonical_type_name = StringTable::getStringView(type_info->name());
	if (canonical_type_name.empty() || canonical_type_name == type_name || !param_map.contains(canonical_type_name)) {
		return std::nullopt;
	}

	return canonical_type_name;
}

TypeSpecifierNode buildTerminalTypeFromResolvedAlias(const ResolvedAliasTypeInfo& resolved_alias, const Token& token) {
	return TypeSpecifierNode(
		resolved_alias.type_index,
		computeTypeSizeInBits(resolved_alias.type_index),
		token,
		CVQualifier::None,
		ReferenceQualifier::None);
}

int checkedPointerDepthToInt(size_t pointer_depth) {
	if (pointer_depth > static_cast<size_t>(std::numeric_limits<int>::max())) {
		throw InternalError("Pointer depth overflow while rebuilding resolved alias type");
	}
	return static_cast<int>(pointer_depth);
}

std::vector<size_t> concatenateArrayDimensions(std::vector<size_t> prefix_dimensions, std::span<const size_t> suffix_dimensions) {
	prefix_dimensions.reserve(prefix_dimensions.size() + suffix_dimensions.size());
	prefix_dimensions.insert(prefix_dimensions.end(), suffix_dimensions.begin(), suffix_dimensions.end());
	return prefix_dimensions;
}

static std::vector<QualifiedTypeMemberAccess> buildQualifiedMemberAccessChain(std::string_view qualified_name) {
	std::vector<QualifiedTypeMemberAccess> member_chain;
	for (std::string_view component : splitQualifiedNamespace(qualified_name)) {
		if (component.empty()) {
			continue;
		}
		QualifiedTypeMemberAccess member_access;
		member_access.member_name =
			StringTable::getOrInternStringHandle(component);
		member_chain.push_back(std::move(member_access));
	}
	return member_chain;
}

void appendArrayDimensions(TypeSpecifierNode& target, std::span<const size_t> prefix_dimensions) {
	if (prefix_dimensions.empty()) {
		return;
	}
	target.set_array_dimensions(concatenateArrayDimensions(
		std::vector<size_t>(prefix_dimensions.begin(), prefix_dimensions.end()),
		target.array_dimensions()));
}

void applyResolvedAliasModifiers(TypeSpecifierNode& target, const ResolvedAliasTypeInfo& resolved_alias) {
	if (resolved_alias.cv_qualifier != CVQualifier::None) {
		target.add_cv_qualifier(resolved_alias.cv_qualifier);
	}
	target.add_pointer_levels(checkedPointerDepthToInt(resolved_alias.pointer_depth));
	// Alias-added lvalue references must win during reference collapsing, while
	// alias-added rvalue references only apply if the substituted target was not
	// already a reference.
	if (resolved_alias.reference_qualifier == ReferenceQualifier::LValueReference) {
		target.set_reference_qualifier(ReferenceQualifier::LValueReference);
	} else if (resolved_alias.reference_qualifier == ReferenceQualifier::RValueReference &&
			   target.reference_qualifier() == ReferenceQualifier::None) {
		target.set_reference_qualifier(ReferenceQualifier::RValueReference);
	}
	appendArrayDimensions(target, resolved_alias.array_dimensions);
	if (resolved_alias.function_signature.has_value() && !target.has_function_signature()) {
		target.set_function_signature(*resolved_alias.function_signature);
	}
}

// Reapply modifiers that were written on the outer alias spelling after the alias target is substituted.
void applyOuterTypeModifiers(TypeSpecifierNode& target, const TypeSpecifierNode& source) {
	target.add_pointer_levels(static_cast<int>(source.pointer_depth()));
	target.add_cv_qualifier(source.cv_qualifier());
	if (source.reference_qualifier() != ReferenceQualifier::None) {
		target.set_reference_qualifier(source.reference_qualifier());
	}
	appendArrayDimensions(target, source.array_dimensions());
	if (source.has_function_signature() && !target.has_function_signature()) {
		target.set_function_signature(source.function_signature());
	}
}

} // namespace

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	Parser& parser)
	: param_map_(param_map), parser_(parser) {
	rebuildEnvironmentFromCurrentBindings();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	Parser& parser,
	std::span<const std::string_view> template_param_order)
	: param_map_(param_map), parser_(parser), template_param_order_(template_param_order.begin(), template_param_order.end()) {
	rebuildEnvironmentFromCurrentBindings();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
	Parser& parser)
	: param_map_(param_map), pack_map_(pack_map), parser_(parser) {
	rebuildEnvironmentFromCurrentBindings();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
	Parser& parser,
	std::span<const std::string_view> template_param_order)
	: param_map_(param_map), pack_map_(pack_map), parser_(parser), template_param_order_(template_param_order.begin(), template_param_order.end()) {
	rebuildEnvironmentFromCurrentBindings();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const TemplateEnvironment& environment,
	Parser& parser)
	: parser_(parser) {
	auto append_environment = [&](auto&& self, const TemplateEnvironment* current) -> void {
		if (current == nullptr) {
			return;
		}
		self(self, current->parent);
		for (const TemplateBinding& binding : current->bindings) {
			if (!binding.name.isValid()) {
				continue;
			}
			if (!binding.is_pack && binding.args.empty()) {
				continue;
			}
			std::string_view binding_name = StringTable::getStringView(binding.name);
			if (binding_name.empty()) {
				continue;
			}
			if (binding.is_pack) {
				std::vector<TemplateTypeArg>& pack_args = pack_map_[binding.name];
				pack_args.assign(binding.args.begin(), binding.args.end());
			} else {
				TemplateTypeArg normalized_arg = binding.args.front();
				if (binding.kind == TemplateParameterKind::Template) {
					normalized_arg.is_template_template_arg = true;
					if (!normalized_arg.template_name_handle.isValid()) {
						if (normalized_arg.type_index.is_valid()) {
							if (const TypeInfo* type_info = tryGetTypeInfo(normalized_arg.type_index)) {
								normalized_arg.template_name_handle = type_info->name();
							}
						}
						if (!normalized_arg.template_name_handle.isValid() &&
							normalized_arg.dependent_name.isValid()) {
							normalized_arg.template_name_handle = normalized_arg.dependent_name;
						}
					}
				}
				param_map_[binding_name] = normalized_arg;
			}
			template_param_order_.push_back(binding_name);
		}
	};
	append_environment(append_environment, &environment);
	rebuildEnvironmentFromCurrentBindings();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const TemplateInstantiationContext& context,
	Parser& parser)
	: ExpressionSubstitutor(context.environment, parser) {
}

void ExpressionSubstitutor::rebuildEnvironmentFromCurrentBindings() {
	environment_ = {};
	environment_.bindings.reserve(param_map_.size() + pack_map_.size());
	const auto infer_binding_kind = [](const TemplateTypeArg& arg) {
		if (arg.is_template_template_arg) {
			return TemplateParameterKind::Template;
		}
		return arg.is_value ? TemplateParameterKind::NonType : TemplateParameterKind::Type;
	};
	const auto is_in_order = [&](std::string_view name) {
		for (std::string_view ordered_name : template_param_order_) {
			if (ordered_name == name) {
				return true;
			}
		}
		return false;
	};

	if (!template_param_order_.empty()) {
		for (std::string_view param_name : template_param_order_) {
			auto scalar_it = param_map_.find(param_name);
			auto pack_it = pack_map_.find(StringTable::getOrInternStringHandle(param_name));
			if (scalar_it != param_map_.end() && pack_it != pack_map_.end()) {
				// Some legacy substitution map producers keep a scalar alias for a pack
				// parameter. Prefer the actual pack binding and ignore the scalar alias.
				scalar_it = param_map_.end();
			}
			if (scalar_it != param_map_.end()) {
				TemplateBinding binding;
				binding.name = StringTable::getOrInternStringHandle(param_name);
				binding.kind = infer_binding_kind(scalar_it->second);
				binding.is_pack = false;
				binding.args.push_back(scalar_it->second);
				environment_.bindings.push_back(std::move(binding));
				continue;
			}
			if (pack_it != pack_map_.end()) {
				TemplateBinding binding;
				binding.name = pack_it->first;
				binding.kind = TemplateParameterKind::Type;
				binding.is_pack = true;
				binding.args.reserve(pack_it->second.size());
				for (const TemplateTypeArg& pack_arg : pack_it->second) {
					binding.args.push_back(pack_arg);
				}
				environment_.bindings.push_back(std::move(binding));
			}
		}
	}

	for (const auto& [param_name, bound_arg] : param_map_) {
		if (is_in_order(param_name)) {
			continue;
		}
		TemplateBinding binding;
		binding.name = StringTable::getOrInternStringHandle(param_name);
		binding.kind = infer_binding_kind(bound_arg);
		binding.is_pack = false;
		binding.args.push_back(bound_arg);
		environment_.bindings.push_back(std::move(binding));
	}

	for (const auto& [pack_name, pack_args] : pack_map_) {
		if (is_in_order(StringTable::getStringView(pack_name))) {
			continue;
		}
		TemplateBinding binding;
		binding.name = pack_name;
		binding.kind = TemplateParameterKind::Type;
		binding.is_pack = true;
		binding.args.reserve(pack_args.size());
		for (const TemplateTypeArg& pack_arg : pack_args) {
			binding.args.push_back(pack_arg);
		}
		environment_.bindings.push_back(std::move(binding));
	}
}

ASTNode ExpressionSubstitutor::substitute(const ASTNode& expr) {
	if (!expr.has_value()) {
		return expr;
	}

	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor::substitute: checking node type: ", expr.type_name());

	// If this is an ExpressionNode variant, peel it off and recurse so all
	// dispatch logic lives in one place below (handles ExpressionNode<ExpressionNode<...>>).
	if (expr.is<ExpressionNode>()) {
		auto& substitutor = *this;
		return std::visit([&substitutor](auto&& node) -> ASTNode {
			ASTNode result = substitutor.substitute(ASTNode(&node));
			// If the recursive call returned an unwrapped result (unhandled variant type
			// that fell through to the else branch), re-wrap the original node in
			// ExpressionNode to preserve invariants: downstream callers require
			// is<ExpressionNode>() to return true on substituted expression results.
			if (!result.is<ExpressionNode>()) {
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(node);
				return ASTNode(&new_expr);
			}
			return result;
		},
						  expr.as<ExpressionNode>());
	}

	// Dispatch on concrete node types (reached directly or after ExpressionNode unwrap above).
	if (expr.is<ConstructorCallNode>()) {
		return substituteConstructorCall(expr.as<ConstructorCallNode>());
	} else if (expr.is<CallExprNode>()) {
		return substituteCallExpr(expr.as<CallExprNode>());
	} else if (expr.is<BinaryOperatorNode>()) {
		return substituteBinaryOp(expr.as<BinaryOperatorNode>());
	} else if (expr.is<UnaryOperatorNode>()) {
		return substituteUnaryOp(expr.as<UnaryOperatorNode>());
	} else if (expr.is<TernaryOperatorNode>()) {
		return substituteTernaryOp(expr.as<TernaryOperatorNode>());
	} else if (expr.is<IdentifierNode>()) {
		return substituteIdentifier(expr.as<IdentifierNode>());
	} else if (expr.is<QualifiedIdentifierNode>()) {
		return substituteQualifiedIdentifier(expr.as<QualifiedIdentifierNode>());
	} else if (expr.is<MemberAccessNode>()) {
		return substituteMemberAccess(expr.as<MemberAccessNode>());
	} else if (expr.is<SizeofExprNode>()) {
		return substituteSizeofExpr(expr.as<SizeofExprNode>());
	} else if (expr.is<TypeTraitExprNode>()) {
		return substituteTypeTraitExpr(expr.as<TypeTraitExprNode>());
	} else if (expr.is<StaticCastNode>()) {
		return substituteStaticCast(expr.as<StaticCastNode>());
	} else if (expr.is<NumericLiteralNode>()) {
		// Literals don't need content substitution, but always return wrapped in ExpressionNode
		// so downstream evaluators (which require is<ExpressionNode>()) see a consistent result.
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(expr.as<NumericLiteralNode>());
		return ASTNode(&new_expr);
	} else if (expr.is<BoolLiteralNode>()) {
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(expr.as<BoolLiteralNode>());
		return ASTNode(&new_expr);
	} else if (expr.is<StringLiteralNode>()) {
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(expr.as<StringLiteralNode>());
		return ASTNode(&new_expr);
	} else {
		// For any other node type, return as-is
		FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Unknown expression type: ", expr.type_name());
		return expr;
	}
}

ASTNode ExpressionSubstitutor::substituteConstructorCall(const ConstructorCallNode& ctor) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing constructor call");

	// Get the type being constructed
	const TypeSpecifierNode& type_spec = ctor.type_node();

	// Substitute template parameters in the type
	TypeSpecifierNode substituted_type = substituteInType(type_spec);

	// Create new ConstructorCallNode with substituted type
	// First, we need to copy the arguments
	ChunkedVector<ASTNode> substituted_args;
	for (size_t i = 0; i < ctor.arguments().size(); ++i) {
		substituted_args.push_back(substitute(ctor.arguments()[i]));
	}

	// Create new TypeSpecifierNode and ConstructorCallNode
	TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
	ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
		new_type,
		std::move(substituted_args),
		ctor.called_from());

	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
	return ASTNode(&new_expr);
}

std::vector<TemplateTypeArg> ExpressionSubstitutor::collectCurrentBoundTemplateArgs(std::string_view use_site) const {
	std::vector<TemplateTypeArg> bound_args;
	if (!template_param_order_.empty()) {
		size_t scalar_bindings_consumed = 0;
		size_t pack_bindings_consumed = 0;
		bound_args.reserve(param_map_.size());
		std::unordered_set<std::string_view> consumed_ordered_names;
		for (std::string_view param_name : template_param_order_) {
			if (!consumed_ordered_names.insert(param_name).second) {
				continue;
			}
			auto scalar_it = param_map_.find(param_name);
			auto pack_it = pack_map_.find(StringTable::getOrInternStringHandle(param_name));
			if (scalar_it != param_map_.end() && pack_it != pack_map_.end()) {
				// Prefer pack bindings when both are present.
				scalar_it = param_map_.end();
			}
			if (scalar_it != param_map_.end()) {
				bound_args.push_back(scalar_it->second);
				++scalar_bindings_consumed;
				continue;
			}
			if (pack_it != pack_map_.end()) {
				bound_args.insert(bound_args.end(), pack_it->second.begin(), pack_it->second.end());
				++pack_bindings_consumed;
				continue;
			}
			throw InternalError(
				"ExpressionSubstitutor missing binding for ordered template parameter '" +
				std::string(param_name) +
				"' while collecting bound args for " +
				std::string(use_site));
		}
		if (scalar_bindings_consumed != param_map_.size() || pack_bindings_consumed != pack_map_.size()) {
			throw InternalError("ExpressionSubstitutor missing template parameter order entries while collecting bound args for " + std::string(use_site));
		}
		return bound_args;
	}

	const size_t binding_count = param_map_.size() + pack_map_.size();
	if (binding_count <= 1) {
		if (!param_map_.empty()) {
			bound_args.push_back(param_map_.begin()->second);
		} else if (!pack_map_.empty()) {
			bound_args.insert(bound_args.end(), pack_map_.begin()->second.begin(), pack_map_.begin()->second.end());
		}
		return bound_args;
	}

	throw InternalError("ExpressionSubstitutor requires template parameter declaration order when collecting bound args for " + std::string(use_site));
}

ExpressionSubstitutor::MaterializedStoredTemplateArgs ExpressionSubstitutor::materializeStoredTemplateArgs(
	const TypeInfo& template_instantiation_info,
	bool evaluate_dependent_member_values,
	int depth) {
	// Cycle-detection: prevent infinite mutual recursion between
	// materializeStoredTemplateArgs and substituteQualifiedIdentifier.
	// If we are already materializing this TypeInfo on the call stack,
	// return the raw (unsubstituted) args to break the cycle.
	if (!materializing_type_infos_.insert(&template_instantiation_info).second) {
		MaterializedStoredTemplateArgs cycle_result;
		const auto& raw_args = template_instantiation_info.templateArgs();
		cycle_result.args.reserve(raw_args.size());
		for (const auto& arg : raw_args) {
			cycle_result.args.push_back(toTemplateTypeArg(arg));
		}
		return cycle_result;
	}
	ScopeGuard guard([&]() { materializing_type_infos_.erase(&template_instantiation_info); });
	MaterializedStoredTemplateArgs result;
	const auto& stored_args = template_instantiation_info.templateArgs();
	result.args.reserve(stored_args.size());
	TemplateEnvironment context_environment;
	if (const TypeInfo::InstantiationContext* context = template_instantiation_info.instantiationContext();
		context != nullptr) {
		context_environment = buildTemplateEnvironment(*context);
	}

	TemplateEnvironment active_environment = environment_;
	active_environment.parent = context_environment.bindings.empty() ? nullptr : &context_environment;
	const TemplateEnvironment& lookup_environment =
		active_environment.bindings.empty() && active_environment.parent != nullptr
		? *active_environment.parent
		: active_environment;
	auto expressionReferencesPackBinding = [&](const ASTNode& expr) {
		bool references_pack = false;
		AstTraversal::visitAST(expr, [&](const ASTNode& node) {
			if (references_pack) {
				return;
			}
			StringHandle candidate_name;
			if (node.is<TemplateParameterReferenceNode>()) {
				candidate_name = node.as<TemplateParameterReferenceNode>().param_name();
			} else if (node.is<IdentifierNode>()) {
				candidate_name = node.as<IdentifierNode>().getOrInternNameHandle();
			} else if (node.is<QualifiedIdentifierNode>()) {
				std::string_view ns_name =
					gNamespaceRegistry.getQualifiedName(node.as<QualifiedIdentifierNode>().namespace_handle());
				if (!ns_name.empty()) {
					candidate_name = StringTable::getOrInternStringHandle(ns_name);
				}
			}
			if (candidate_name.isValid() && pack_map_.find(candidate_name) != pack_map_.end()) {
				references_pack = true;
			}
		});
		return references_pack;
	};
	auto expandDependentValuePack =
		[&](const TemplateTypeArg& pack_arg) -> std::optional<std::vector<TemplateTypeArg>> {
		if (!pack_arg.is_value || !pack_arg.dependent_expr.has_value()) {
			return std::nullopt;
		}
		if (!pack_arg.is_pack &&
			!expressionReferencesPackBinding(*pack_arg.dependent_expr)) {
			return std::nullopt;
		}

		std::optional<size_t> expansion_count;
		for (const auto& [pack_name, pack_args] : pack_map_) {
			(void)pack_name;
			if (!expansion_count.has_value()) {
				expansion_count = pack_args.size();
			} else if (*expansion_count != pack_args.size()) {
				return std::nullopt;
			}
		}
		if (!expansion_count.has_value()) {
			return std::nullopt;
		}

		std::vector<TemplateTypeArg> expanded_args;
		expanded_args.reserve(*expansion_count);
		for (size_t expansion_index = 0; expansion_index < *expansion_count; ++expansion_index) {
			std::unordered_map<std::string_view, TemplateTypeArg> scalar_bindings = param_map_;
			for (const auto& [pack_name, pack_args] : pack_map_) {
				if (expansion_index >= pack_args.size()) {
					return std::nullopt;
				}
				scalar_bindings[StringTable::getStringView(pack_name)] = pack_args[expansion_index];
			}

			ExpressionSubstitutor element_substitutor(scalar_bindings, parser_, template_param_order_);
			element_substitutor.setCurrentOwnerTypeName(current_owner_type_name_);
			ASTNode substituted_expr = element_substitutor.substitute(*pack_arg.dependent_expr);
			if (auto eval_result = parser_.try_evaluate_constant_expression(substituted_expr)) {
				expanded_args.emplace_back(eval_result->value, eval_result->type);
				continue;
			}

			TemplateTypeArg unresolved_arg = pack_arg;
			unresolved_arg.is_pack = false;
			unresolved_arg.is_dependent = true;
			unresolved_arg.dependent_expr = std::move(substituted_expr);
			expanded_args.push_back(std::move(unresolved_arg));
		}

		return expanded_args;
	};

	for (const auto& arg : stored_args) {
		TemplateTypeArg materialized_arg = toTemplateTypeArg(arg);
		bool substituted = false;
		auto applyEvaluatedValue = [&](const Parser::ConstantValue& value) {
			materialized_arg.is_value = true;
			materialized_arg.value = value.value;
			materialized_arg.type_index = nativeTypeIndex(value.type);
			materialized_arg.dependent_name = {};
			materialized_arg.is_dependent = false;
			result.had_substitution = true;
			substituted = true;
		};

		if (std::optional<std::vector<TemplateTypeArg>> expanded_pack_args =
				expandDependentValuePack(materialized_arg);
			expanded_pack_args.has_value()) {
			result.had_substitution = true;
			for (TemplateTypeArg& expanded_arg : *expanded_pack_args) {
				result.args.push_back(std::move(expanded_arg));
			}
			continue;
		}

		if (materialized_arg.is_value && materialized_arg.dependent_expr.has_value()) {
			const ASTNode& stored_expr = *materialized_arg.dependent_expr;
			ASTNode substituted_expr = substitute(stored_expr);
			if (auto eval_result = parser_.try_evaluate_constant_expression(substituted_expr)) {
				applyEvaluatedValue(*eval_result);
				FLASH_LOG(Templates, Debug, "materializeStoredTemplateArgs: evaluated dependent value expr -> ", eval_result->value);
			} else {
				materialized_arg.dependent_expr = std::move(substituted_expr);
				materialized_arg.is_dependent = true;
				result.had_substitution = true;
			}
		}

		if (materialized_arg.dependent_name.isValid()) {
			std::string_view dependent_name = StringTable::getStringView(materialized_arg.dependent_name);
			auto dep_subst_it = param_map_.find(dependent_name);
			if (dep_subst_it != param_map_.end()) {
				if (!substituted) {
					materialized_arg = rebindDependentTemplateTypeArg(dep_subst_it->second, materialized_arg);
					result.had_substitution = true;
					substituted = true;
				}
			} else if (!materialized_arg.is_value) {
				if (auto context_binding = resolveContextBinding(
						materialized_arg.dependent_name,
						lookup_environment);
					context_binding.has_value()) {
					materialized_arg = rebindDependentTemplateTypeArg(*context_binding, materialized_arg);
					result.had_substitution = true;
					substituted = true;
				}
			} else if (evaluate_dependent_member_values && materialized_arg.is_value) {
				size_t scope_pos = dependent_name.rfind("::");
				if (scope_pos != std::string_view::npos) {
					std::string_view dependent_base_name = dependent_name.substr(0, scope_pos);
					std::string_view dependent_member_name = dependent_name.substr(scope_pos + 2);
					auto base_subst_it = param_map_.find(dependent_base_name);
					if (base_subst_it != param_map_.end()) {
						const TemplateTypeArg& concrete_base = base_subst_it->second;
						if (is_struct_type(concrete_base.category())) {
							if (const TypeInfo* concrete_base_info = tryGetTypeInfo(concrete_base.type_index)) {
								StringHandle concrete_type_name = concrete_base_info->name();
								Token member_token(Token::Type::Identifier, dependent_member_name, 0, 0, 0);
								NamespaceHandle member_ns = gNamespaceRegistry.getOrCreateNamespace(
									NamespaceRegistry::GLOBAL_NAMESPACE,
									concrete_type_name);
								QualifiedIdentifierNode& member_qual_id =
									gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
										member_ns,
										member_token);
								ExpressionNode& member_expr =
									gChunkedAnyStorage.emplace_back<ExpressionNode>(member_qual_id);
								if (auto value = parser_.try_evaluate_constant_expression(ASTNode(&member_expr))) {
									applyEvaluatedValue(*value);
								}
							}
						}
					}
				}
			}
		}

		if (!substituted && !arg.is_value && arg.type_index.is_valid()) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				if (const TypeInfo* recursively_resolved_type = resolveDependentMemberType(
						*arg_type_info,
						depth)) {
					materialized_arg.type_index =
						recursively_resolved_type->registeredTypeIndex().withCategory(recursively_resolved_type->typeEnum());
					materialized_arg.setCategory(recursively_resolved_type->typeEnum());
					materialized_arg.dependent_name = {};
					materialized_arg.is_dependent = false;
					result.had_substitution = true;
					substituted = true;
					result.args.push_back(materialized_arg);
					continue;
				}

				std::string_view arg_type_name = StringTable::getStringView(arg_type_info->name());
				auto type_subst_it = param_map_.find(arg_type_name);
				if (type_subst_it != param_map_.end()) {
					materialized_arg = rebindDependentTemplateTypeArg(type_subst_it->second, materialized_arg);
					result.had_substitution = true;
					substituted = true;
				} else if (arg_type_info->name().isValid()) {
					// Alias-target stored template args may carry intermediate
					// dependent parameter names (e.g. Type -> Head). If direct
					// param_map_ lookup misses, resolve via the full environment so
					// deferred trait/default evaluation sees the concrete outer type.
					if (auto context_binding = resolveContextBinding(arg_type_info->name(), lookup_environment);
						context_binding.has_value()) {
						materialized_arg = rebindDependentTemplateTypeArg(*context_binding, materialized_arg);
						result.had_substitution = true;
						substituted = true;
					}
				}

				if (!substituted &&
					arg_type_info->isTemplateInstantiation() &&
					depth < kMaxDependentMemberTypeResolutionDepth) {
					MaterializedStoredTemplateArgs nested_materialized_args =
						materializeStoredTemplateArgs(
							*arg_type_info,
							evaluate_dependent_member_values,
							depth + 1);
					if (!nested_materialized_args.args.empty() &&
						!templateArgsStillDependent(nested_materialized_args.args)) {
						StringHandle qualified_base_template_name =
							gNamespaceRegistry.buildQualifiedIdentifier(
								arg_type_info->sourceNamespace(),
								arg_type_info->baseTemplateName());
						std::string_view base_template_name =
							StringTable::getStringView(qualified_base_template_name);
						if (base_template_name.empty()) {
							base_template_name =
								StringTable::getStringView(arg_type_info->baseTemplateName());
						}
						if (!base_template_name.empty()) {
							Parser::AliasTemplateMaterializationResult materialized_nested =
								parser_.materializeTemplateInstantiationForLookup(
									base_template_name,
									nested_materialized_args.args);
							const TypeInfo* resolved_nested_type =
								materialized_nested.resolved_type_info;
							if (resolved_nested_type == nullptr &&
								qualified_base_template_name != arg_type_info->baseTemplateName()) {
								materialized_nested =
									parser_.materializeTemplateInstantiationForLookup(
										StringTable::getStringView(arg_type_info->baseTemplateName()),
										nested_materialized_args.args);
								resolved_nested_type = materialized_nested.resolved_type_info;
							}
							if (resolved_nested_type == nullptr) {
								StringHandle canonical_name_handle =
									materialized_nested.canonicalNameHandle();
								if (canonical_name_handle.isValid()) {
									resolved_nested_type = findTypeByName(canonical_name_handle);
								}
							}
							if (resolved_nested_type != nullptr) {
								TypeIndex resolved_nested_index =
									resolved_nested_type->registeredTypeIndex().withCategory(
										resolved_nested_type->typeEnum());
								TemplateTypeArg resolved_nested_arg =
									makeTemplateTypeArgFromResolvedAlias(
										resolveAliasTypeInfo(resolved_nested_index),
										resolved_nested_index);
								materialized_arg = rebindDependentTemplateTypeArg(
									resolved_nested_arg,
									materialized_arg);
								result.had_substitution = true;
								substituted = true;
							}
						}
					}
				}
			}
		}

		result.args.push_back(materialized_arg);
	}

	return result;
}

InlineVector<TemplateTypeArg, 4> ExpressionSubstitutor::materializeDependentRecordTemplateArgs(
	std::span<const TypeInfo::TemplateArgInfo> stored_args,
	int depth) {
	InlineVector<TemplateTypeArg, 4> materialized_args;
	materialized_args.reserve(stored_args.size());
	for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
		TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
		if (arg.is_value && arg.dependent_expr.has_value()) {
			ASTNode substituted_expr = substitute(*arg.dependent_expr);
			if (auto eval_result = parser_.try_evaluate_constant_expression(substituted_expr)) {
				TemplateTypeArg concrete_arg = TemplateTypeArg::makeValueIdentity(eval_result->identity);
				concrete_arg.is_pack = arg.is_pack;
				arg = concrete_arg;
			} else {
				arg.dependent_expr = std::move(substituted_expr);
				arg.is_dependent = true;
			}
		}
		if (arg.dependent_name.isValid()) {
			std::string_view dependent_arg_name = StringTable::getStringView(arg.dependent_name);
			if (auto subst_it = param_map_.find(dependent_arg_name);
				subst_it != param_map_.end()) {
				arg = rebindDependentTemplateTypeArg(subst_it->second, arg);
			} else if (auto context_binding = resolveContextBinding(arg.dependent_name, environment_);
				context_binding.has_value()) {
				arg = rebindDependentTemplateTypeArg(*context_binding, arg);
			}
		} else if (!arg.is_value && arg.type_index.is_valid()) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				if (arg_type_info->isDependentMemberType()) {
					if (const TypeInfo* resolved_member_type =
							resolveDependentMemberType(*arg_type_info, depth + 1);
						resolved_member_type != nullptr) {
						arg.type_index =
							resolved_member_type->registeredTypeIndex().withCategory(resolved_member_type->typeEnum());
						arg.setCategory(resolved_member_type->typeEnum());
						arg.dependent_name = {};
						arg.is_dependent = false;
						materialized_args.push_back(std::move(arg));
						continue;
					}
				}
				std::string_view arg_type_name = StringTable::getStringView(arg_type_info->name());
				if (auto subst_it = param_map_.find(arg_type_name);
					subst_it != param_map_.end()) {
					arg = rebindDependentTemplateTypeArg(subst_it->second, arg);
				}
			}
		}
		materialized_args.push_back(std::move(arg));
	}
	return materialized_args;
}

bool ExpressionSubstitutor::templateArgsStillDependent(std::span<const TemplateTypeArg> args) const {
	for (const TemplateTypeArg& arg : args) {
		if (arg.is_dependent) {
			return true;
		}
		if (arg.dependent_name.isValid()) {
			return true;
		}
		if (!arg.is_value && arg.type_index.is_valid()) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				if (param_map_.find(StringTable::getStringView(arg_type_info->name())) !=
					param_map_.end()) {
					return true;
				}
			}
		}
	}
	return false;
}

ExpressionSubstitutor::MaterializedDependentMemberLookup
ExpressionSubstitutor::lookupMaterializedDependentMember(const TypeInfo& type_info, int depth) {
	MaterializedDependentMemberLookup lookup;
	if (!type_info.isDependentMemberType() || depth >= kMaxDependentMemberTypeResolutionDepth) {
		return lookup;
	}

	if (const TypeInfo::DependentQualifiedNameRecord* dependent_name =
			type_info.dependentQualifiedName()) {
		std::string_view owner_name =
			StringTable::getStringView(dependent_name->owner_name);
		std::string_view materialized_owner_name;
		switch (dependent_name->owner_kind) {
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::CurrentInstantiation:
			if (current_owner_type_name_.isValid()) {
				materialized_owner_name =
					StringTable::getStringView(current_owner_type_name_);
				break;
			}
			if (dependent_name->owner_type.is_valid()) {
				if (const TypeInfo* owner_type_info =
						tryGetTypeInfo(dependent_name->owner_type)) {
					materialized_owner_name =
						StringTable::getStringView(owner_type_info->name());
				}
			}
			break;
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::TemplateParameter:
			if (dependent_name->owner_template_arguments.empty()) {
				if (auto owner_subst_it = param_map_.find(owner_name);
					owner_subst_it != param_map_.end()) {
					const TypeInfo* owner_type_info =
						tryGetTypeInfo(owner_subst_it->second.type_index);
					if (owner_type_info != nullptr) {
						materialized_owner_name =
							StringTable::getStringView(owner_type_info->name());
					}
				}
				break;
			}
			[[fallthrough]];
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::DependentInstantiation:
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::UnknownSpecialization: {
			InlineVector<TemplateTypeArg, 4> owner_args =
				materializeDependentRecordTemplateArgs(dependent_name->owner_template_arguments, depth);
			if (!owner_args.empty() && !templateArgsStillDependent(owner_args)) {
				if (auto owner_template_subst_it = param_map_.find(owner_name);
					owner_template_subst_it != param_map_.end() &&
					owner_template_subst_it->second.is_template_template_arg) {
					owner_name = StringTable::getStringView(
						owner_template_subst_it->second.template_name_handle);
				}
				Parser::AliasTemplateMaterializationResult materialized_owner =
					parser_.materializeTemplateInstantiationForLookup(
						owner_name,
						owner_args);
				materialized_owner_name = materialized_owner.canonicalName();
			}
			break;
		}
		}

		if (!materialized_owner_name.empty()) {
			std::vector<QualifiedTypeMemberAccess> member_chain;
			member_chain.reserve(dependent_name->member_chain.size());
			for (const auto& member_record : dependent_name->member_chain) {
				QualifiedTypeMemberAccess member_access;
				member_access.member_name = member_record.name;
				if (member_record.has_template_arguments) {
					InlineVector<TemplateTypeArg, 4> member_args =
						materializeDependentRecordTemplateArgs(member_record.template_arguments, depth);
					if (templateArgsStillDependent(member_args)) {
						return lookup;
					}
					member_access.has_template_arguments = true;
					member_access.template_arguments =
						&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(member_args.toVector());
				}
				member_chain.push_back(std::move(member_access));
			}

			const TypeInfo* resolved_type =
				parser_.resolveBaseClassMemberTypeChain(
					materialized_owner_name,
					member_chain);
			if (resolved_type != nullptr) {
				if (!dependent_name->member_chain.empty()) {
					StringBuilder full_path_builder;
					full_path_builder.append(materialized_owner_name);
					for (const auto& member_record : dependent_name->member_chain) {
						full_path_builder.append("::");
						full_path_builder.append(
							StringTable::getStringView(member_record.name));
					}
					lookup.materialized_member_handle =
						StringTable::getOrInternStringHandle(
							full_path_builder.commit());
					lookup.terminal_member_name =
						dependent_name->member_chain.back().name;
				}
				const TypeInfo* next_dependent_type = nullptr;
				if (const TypeInfo* alias_target =
						tryGetTypeInfo(resolved_type->type_index_);
					alias_target != nullptr &&
					alias_target != resolved_type &&
					alias_target->isDependentMemberType()) {
					next_dependent_type = alias_target;
				} else if (resolved_type->isDependentMemberType()) {
					next_dependent_type = resolved_type;
				}
				if (next_dependent_type != nullptr) {
					MaterializedDependentMemberLookup nested_lookup =
						lookupMaterializedDependentMember(*next_dependent_type, depth + 1);
					if (nested_lookup.resolved_type != nullptr) {
						return nested_lookup;
					}
					return lookup;
				}
				lookup.resolved_type = resolved_type;
				return lookup;
			}
		}
	}

	std::string_view type_name = StringTable::getStringView(type_info.name());
	size_t member_sep = type_name.rfind("::");
	if (member_sep == std::string_view::npos) {
		return lookup;
	}

	std::string_view dependent_base_name = type_name.substr(0, member_sep);
	constexpr size_t kScopeResolutionLength = 2;
	std::string_view dependent_member_name = type_name.substr(member_sep + kScopeResolutionLength);
	std::string_view base_template_name = extractBaseTemplateName(dependent_base_name);
	if (base_template_name.empty()) {
		return lookup;
	}

	auto dependent_base_it = getTypesByNameMap().find(
		StringTable::getOrInternStringHandle(dependent_base_name));
	if (dependent_base_it == getTypesByNameMap().end() ||
		dependent_base_it->second == nullptr ||
		!dependent_base_it->second->isTemplateInstantiation()) {
		return lookup;
	}

	MaterializedStoredTemplateArgs concrete_base_args =
		materializeStoredTemplateArgs(
			*dependent_base_it->second,
			/*evaluate_dependent_member_values=*/true,
			depth);
	Parser::AliasTemplateMaterializationResult materialized_base =
		parser_.materializeTemplateInstantiationForLookup(
			base_template_name,
			concrete_base_args.args);
	std::string_view materialized_base_name = materialized_base.canonicalName();
	if (materialized_base_name.empty()) {
		return lookup;
	}

	QualifiedTypeMemberAccess member_access;
	member_access.member_name = StringTable::getOrInternStringHandle(dependent_member_name);
	std::vector<QualifiedTypeMemberAccess> member_chain;
	member_chain.push_back(std::move(member_access));
	const TypeInfo* resolved_type =
		parser_.resolveBaseClassMemberTypeChain(
			materialized_base_name,
			member_chain);
	if (resolved_type == nullptr) {
		return lookup;
	}
	lookup.materialized_member_handle =
		StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(materialized_base_name)
				.append("::")
				.append(dependent_member_name)
				.commit());
	lookup.terminal_member_name = member_access.member_name;

	const TypeInfo* next_dependent_type = nullptr;
	if (const TypeInfo* alias_target = tryGetTypeInfo(resolved_type->type_index_);
		alias_target != nullptr && alias_target != resolved_type && alias_target->isDependentMemberType()) {
		next_dependent_type = alias_target;
	} else if (resolved_type->isDependentMemberType()) {
		next_dependent_type = resolved_type;
	}
	if (next_dependent_type != nullptr) {
		MaterializedDependentMemberLookup nested_lookup =
			lookupMaterializedDependentMember(*next_dependent_type, depth + 1);
		if (nested_lookup.resolved_type != nullptr) {
			return nested_lookup;
		}
		return lookup;
	}

	lookup.resolved_type = resolved_type;
	return lookup;
}

const TypeInfo* ExpressionSubstitutor::resolveMaterializedMemberAliasLookup(
	const MaterializedDependentMemberLookup& lookup) {
	if (lookup.resolved_type == nullptr) {
		return nullptr;
	}
	if (!lookup.materialized_member_handle.isValid()) {
		return lookup.resolved_type;
	}

	auto concrete_member_it = getTypesByNameMap().find(lookup.materialized_member_handle);
	if (concrete_member_it != getTypesByNameMap().end() &&
		concrete_member_it->second != nullptr) {
		return concrete_member_it->second;
	}

	if (!lookup.terminal_member_name.isValid() ||
		lookup.resolved_type->name() != lookup.terminal_member_name) {
		return lookup.resolved_type;
	}

	TypeIndex resolved_member_index =
		lookup.resolved_type->registeredTypeIndex().withCategory(
			lookup.resolved_type->typeEnum());
	TypeSpecifierNode concrete_member_spec(
		resolved_member_index,
		lookup.resolved_type->sizeInBits(),
		Token(),
		CVQualifier::None,
		ReferenceQualifier::None);
	if (const TypeSpecifierNode* existing_alias_spec =
			lookup.resolved_type->aliasTypeSpecifier()) {
		concrete_member_spec = *existing_alias_spec;
	}
	TypeInfo& concrete_member_info = add_type_alias_copy(
		lookup.materialized_member_handle,
		resolved_member_index,
		lookup.resolved_type->sizeInBits().value,
		concrete_member_spec,
		*lookup.resolved_type);
	getTypesByNameMap().insert_or_assign(
		lookup.materialized_member_handle,
		&concrete_member_info);
	return &concrete_member_info;
}

const TypeInfo* ExpressionSubstitutor::resolveQualifiedMemberNamespaceChain(
	std::string_view owner_name,
	std::span<QualifiedTypeMemberAccess> member_chain) {
	if (owner_name.empty()) {
		return nullptr;
	}

	const TypeInfo* resolved_member =
		parser_.resolveBaseClassMemberTypeChain(owner_name, member_chain);
	if (resolved_member == nullptr) {
		return nullptr;
	}

	bool had_dependent_member_lookup = false;
	if (const TypeInfo* alias_target = tryGetTypeInfo(resolved_member->type_index_);
		alias_target != nullptr &&
		alias_target != resolved_member &&
		alias_target->isDependentMemberType()) {
		had_dependent_member_lookup = true;
		MaterializedDependentMemberLookup lookup =
			lookupMaterializedDependentMember(
				*alias_target,
				kInitialDependentMemberTypeResolutionDepth);
		if (const TypeInfo* materialized_member =
				resolveMaterializedMemberAliasLookup(lookup);
			materialized_member != nullptr) {
			return materialized_member;
		}
	}
	if (resolved_member->isDependentMemberType()) {
		had_dependent_member_lookup = true;
		MaterializedDependentMemberLookup lookup =
			lookupMaterializedDependentMember(
				*resolved_member,
				kInitialDependentMemberTypeResolutionDepth);
		if (const TypeInfo* materialized_member =
				resolveMaterializedMemberAliasLookup(lookup);
			materialized_member != nullptr) {
			return materialized_member;
		}
	}

	ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
		resolved_member->registeredTypeIndex().withCategory(
			resolved_member->typeEnum()));
	const TypeInfo* resolved_type_info = resolved_alias.terminal_type_info;
	if (resolved_type_info == nullptr && resolved_alias.type_index.is_valid()) {
		resolved_type_info = tryGetTypeInfo(resolved_alias.type_index);
	}
	if (resolved_type_info != nullptr &&
		!resolved_type_info->isDependentMemberType()) {
		return resolved_type_info;
	}
	return had_dependent_member_lookup ? nullptr : resolved_member;
}

const TypeInfo* ExpressionSubstitutor::resolveCurrentOwnerQualifiedNamespaceMember(
	StringHandle member_name_handle) {
	if (!current_owner_type_name_.isValid() || !member_name_handle.isValid()) {
		return nullptr;
	}

	std::vector<QualifiedTypeMemberAccess> member_chain =
		buildQualifiedMemberAccessChain(
			StringTable::getStringView(member_name_handle));
	if (member_chain.empty()) {
		return nullptr;
	}
	std::string_view owner_name = StringTable::getStringView(current_owner_type_name_);
	return resolveQualifiedMemberNamespaceChain(owner_name, member_chain);
}

const TypeInfo* ExpressionSubstitutor::resolveDependentMemberType(const TypeInfo& type_info, int depth) {
	return lookupMaterializedDependentMember(type_info, depth).resolved_type;
}

const TypeInfo* ExpressionSubstitutor::resolveDependentMemberTypeForSubstitution(const TypeInfo& type_info) {
	return resolveDependentMemberType(type_info, kInitialDependentMemberTypeResolutionDepth);
}

ASTNode ExpressionSubstitutor::substituteFunctionCallImpl(const CallExprNode& call) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing function call");
	FLASH_LOG(Templates, Debug, "  has_mangled_name: ", call.has_mangled_name());
	FLASH_LOG(Templates, Debug, "  has_template_arguments: ", call.has_template_arguments());
	FLASH_LOG(Templates, Debug, "  has_dependent_qualified_lookup_record: ", call.has_dependent_qualified_lookup_record());

	const DeclarationNode& decl_node = call.callee().declaration();
	std::string_view func_name = call.called_from().value();
	if (func_name.empty()) {
		func_name = decl_node.identifier_token().value();
	}
	const std::string_view qualified_name = call.has_qualified_name()
		? call.qualified_name()
		: std::string_view{};
	auto wrapOriginalCall = [&]() -> ASTNode {
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(call);
		return ASTNode(&new_expr);
	};
	auto copyMetadataToExpr = [&](ExpressionNode& expr, const CallMetadataCopyOptions& options = {}) {
		if (auto* call_expr = std::get_if<CallExprNode>(&expr)) {
			copyCallMetadata(*call_expr, call, options);
		}
	};
	auto emplaceDirectCallExpr = [&](const DeclarationNode& target_decl, const FunctionDeclarationNode* target_func, ChunkedVector<ASTNode>&& args, const Token& called_from) -> ExpressionNode& {
		CallExprNode new_call = target_func
			? makeResolvedCallExpr(*target_func, std::move(args), called_from)
			: makeDirectCallExpr(target_decl, std::move(args), called_from);
		return gChunkedAnyStorage.emplace_back<ExpressionNode>(std::move(new_call));
	};
	auto normalizePendingSemanticRoots = [&]() {
		parser_.normalizePendingSemanticRoots();
	};
	auto materializeSubstitutedUnresolvedCall = [&](ChunkedVector<ASTNode>&& substituted_args) -> ASTNode {
		CallExprNode substituted_call(
			call.callee(),
			std::move(substituted_args),
			call.called_from());
		copyCallMetadataWithTransformedTemplateArguments(
			substituted_call,
			call,
			[this](const ASTNode& template_arg) {
				return substitute(template_arg);
			},
			CallMetadataCopyOptions{});
		ExpressionNode& new_expr =
			gChunkedAnyStorage.emplace_back<ExpressionNode>(std::move(substituted_call));
		return ASTNode(&new_expr);
	};
	std::vector<ASTNode> explicit_template_arg_nodes;

	// Check if this function call has explicit template arguments (e.g., base_trait<T>())
	if (call.has_template_arguments()) {
		const std::span<const ASTNode> template_arguments = call.template_arguments();
		explicit_template_arg_nodes.assign(template_arguments.begin(), template_arguments.end());
		FLASH_LOG(Templates, Debug, "  Found ", explicit_template_arg_nodes.size(), " template argument nodes");

		FLASH_LOG(Templates, Debug, "  Function name: ", func_name);

		// Check if any arguments are pack expansions
		bool has_pack_expansion = false;
		for (const ASTNode& arg_node : explicit_template_arg_nodes) {
			std::string_view pack_name;
			if (isPackExpansion(arg_node, pack_name)) {
				has_pack_expansion = true;
				break;
			}
		}

		std::vector<TemplateTypeArg> substituted_template_args;
		bool failed_value_extraction = false;

		if (has_pack_expansion) {
			// Use pack expansion logic
			FLASH_LOG(Templates, Debug, "  Template arguments contain pack expansion, expanding...");
			substituted_template_args = expandPacksInArguments(explicit_template_arg_nodes);
			FLASH_LOG(Templates, Debug, "  After pack expansion: ", substituted_template_args.size(), " arguments");
		} else {
			// Original logic for non-pack arguments
			// Substitute template parameters in the template arguments
			auto append_resolved_explicit_type_arg = [&](const TypeSpecifierNode& explicit_type) {
				TypeSpecifierNode resolved_explicit_type = substituteInType(explicit_type);
				substituted_template_args.emplace_back(resolved_explicit_type);
			};
			for (const ASTNode& arg_node : explicit_template_arg_nodes) {
				FLASH_LOG(Templates, Debug, "  Checking template argument node, has_value: ", arg_node.has_value(), " type: ", arg_node.type_name());

				// Template arguments can be stored as TypeSpecifierNode for type arguments
				if (arg_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
					FLASH_LOG(Templates, Debug, "    Template argument is TypeSpecifierNode: type=", (int)type_spec.type(), " type_index=", type_spec.type_index());

					// First, check if type_index points to a template parameter in gTypeInfo
					std::string_view type_name = "";
					if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
						type_name = StringTable::getStringView(type_info->name());
						FLASH_LOG(Templates, Debug, "    Type name from gTypeInfo: ", type_name);
					}
					if (type_name.empty()) {
						type_name = type_spec.token().value();
						if (!type_name.empty()) {
							FLASH_LOG(Templates, Debug, "    Type name from token: ", type_name);
						}
					}

					auto pack_it = pack_map_.find(type_name);
					if (type_spec.is_pack_expansion() && pack_it != pack_map_.end()) {
						FLASH_LOG(Templates, Debug, "    Expanding template parameter pack: ", type_name, " with ", pack_it->second.size(), " arguments");
						substituted_template_args.insert(
							substituted_template_args.end(),
							pack_it->second.begin(),
							pack_it->second.end());
						continue;
					}

					// Check if this type_name is in our substitution map (indicating it's a template parameter)
					auto it = param_map_.find(type_name);
					if (it != param_map_.end()) {
					// This is a template parameter - substitute it
						FLASH_LOG(Templates, Debug, "    Substituting template parameter: ", type_name, " -> type_index=", it->second.type_index);
						substituted_template_args.push_back(it->second);
					}
					// Check if this is a template parameter type (Type::Template)
					else if (type_spec.category() == TypeCategory::Template) {
						// This is a template parameter - we need to substitute it
						// The type_index should point to a template parameter
						FLASH_LOG(Templates, Debug, "    Type is Template, looking up in substitution map");

						// For template parameters, we need to look them up by name
						// The type_spec.type_index() should tell us which parameter it is
						// But we need the name to do the substitution
						// Let's check if we can find it in gTypeInfo
						if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
							std::string_view param_name = StringTable::getStringView(type_info->name());
							FLASH_LOG(Templates, Debug, "    Template parameter name: ", param_name);

							auto it2 = param_map_.find(param_name);
							if (it2 != param_map_.end()) {
								FLASH_LOG(Templates, Debug, "    Substituting: ", param_name, " -> type_index=", it2->second.type_index);
								substituted_template_args.push_back(it2->second);
							} else {
								// Not every TypeCategory::Template spelling is an active template
								// parameter in this substitution context (e.g. dependent aliases
								// like size_type inside a class template instantiation). Preserve
								// it as a concrete explicit type argument instead of bailing out.
								FLASH_LOG(Templates, Debug, "    Template-like type not present in substitution map, preserving explicit argument: ", param_name);
								append_resolved_explicit_type_arg(type_spec);
							}
						} else {
							// Keep unresolved template-category spellings as explicit type args.
							append_resolved_explicit_type_arg(type_spec);
						}
					} else {
						// Non-template type argument - use it directly
						FLASH_LOG(Templates, Debug, "    Template argument is concrete type, using directly");
						append_resolved_explicit_type_arg(type_spec);
					}
				}
				// Check if this is a TemplateParameterReferenceNode that needs substitution
				else if (arg_node.is<ExpressionNode>()) {
					const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
					bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
						using T = std::decay_t<decltype(inner)>;
						return std::is_same_v<T, TemplateParameterReferenceNode>;
					},
															expr_variant);

					if (is_template_param_ref) {
						// This is a template parameter reference - substitute it
						const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
						std::string_view param_name = tparam_ref.param_name().view();
						FLASH_LOG(Templates, Debug, "    Template argument is parameter reference: ", param_name);

						auto it = param_map_.find(param_name);
						if (it != param_map_.end()) {
							FLASH_LOG(Templates, Debug, "    Substituting: ", param_name, " -> type_index=", it->second.type_index);
							substituted_template_args.push_back(it->second);
						} else {
							FLASH_LOG(Templates, Warning, "    Template parameter not found in substitution map: ", param_name);
							// Return as-is if we can't substitute
							return wrapOriginalCall();
						}
					} else {
						// Non-dependent template argument - substitute/evaluate to a value if possible
						FLASH_LOG(Templates, Debug, "    Template argument is non-dependent expression");
						ASTNode substituted_arg_node = substitute(arg_node);
						if (substituted_arg_node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& type_spec = substituted_arg_node.as<TypeSpecifierNode>();
							substituted_template_args.emplace_back(type_spec);
							continue;
						}
						if (!substituted_arg_node.is<ExpressionNode>()) {
							FLASH_LOG(Templates, Debug, "    Substituted template argument is not an ExpressionNode");
							failed_value_extraction = true;
							break;
						}
						const ExpressionNode& substituted_expr = substituted_arg_node.as<ExpressionNode>();
						if (const auto* identifier = std::get_if<IdentifierNode>(&substituted_expr)) {
							StringHandle type_name = StringTable::getOrInternStringHandle(identifier->name());
							// Direct registry convenience wrappers are intentionally used here.
							// ExpressionSubstitutor stores parser_ (a Parser&) but not a
							// TemplateLookupContext with instantiation-time namespace/timing data.
							// The convenience wrappers use PointOfDefinition/Ordinary timing,
							// which is appropriate for this simple "is this name a template?"
							// existence check during substitution.
							if (gTemplateRegistry.lookup_alias_template(type_name).has_value() ||
								gTemplateRegistry.lookupTemplate(type_name).has_value() ||
								gTemplateRegistry.isClassTemplate(type_name)) {
								substituted_template_args.push_back(TemplateTypeArg::makeTemplate(type_name));
								continue;
							}
							if (const TypeInfo* type_info = findTypeByName(type_name)) {
								substituted_template_args.push_back(TemplateTypeArg::makeType(type_info->type_index_));
								continue;
							}
							if (auto builtin_type = parser_.get_builtin_type_info(identifier->name())) {
								substituted_template_args.push_back(
									TemplateTypeArg::makeType(nativeTypeIndex(builtin_type->first)));
								continue;
							}
							FLASH_LOG(Templates, Debug, "    Substituted identifier template argument is not a known type: ", identifier->name());
							failed_value_extraction = true;
							break;
						}
						if (std::holds_alternative<NumericLiteralNode>(substituted_expr)) {
							const NumericLiteralNode& lit = std::get<NumericLiteralNode>(substituted_expr);
							const NumericLiteralValue& raw_value = lit.value();
							int64_t value = 0;
							if (const auto* ull_val = std::get_if<unsigned long long>(&raw_value)) {
								value = static_cast<int64_t>(*ull_val);
							} else if (const auto* d_val = std::get_if<double>(&raw_value)) {
								value = static_cast<int64_t>(*d_val);
							} else {
								FLASH_LOG(Templates, Debug, "    Numeric literal value variant type not handled for template arg extraction");
								failed_value_extraction = true;
								break;
							}
							substituted_template_args.emplace_back(value, lit.type());
						} else if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&substituted_expr)) {
							const BoolLiteralNode& lit = *bool_literal;
							substituted_template_args.emplace_back(lit.value() ? 1 : 0, TypeCategory::Bool);
						} else {
							FLASH_LOG(Templates, Debug, "    Substituted template argument expression type not handled for value extraction");
							failed_value_extraction = true;
							break;
						}
					}
				} else {
					FLASH_LOG(Templates, Debug, "    Template argument is unknown type");
				}
			}
		} // End of else block for non-pack arguments
		if (failed_value_extraction) {
			FLASH_LOG(Templates, Debug, "  Could not safely extract all substituted template argument values; keeping original call");
			return wrapOriginalCall();
		}

		// If no arguments were collected but we have pack substitutions available, use them
		if (substituted_template_args.empty() && !pack_map_.empty() && !call.has_template_arguments()) {
			FLASH_LOG(Templates, Debug, "  Using pack substitution map to recover template arguments for function call");
			for (const auto& pack_entry : pack_map_) {
				substituted_template_args.insert(substituted_template_args.end(), pack_entry.second.begin(), pack_entry.second.end());
				break; // Use the first available pack substitution
			}
		}

		// Now we have substituted template arguments - instantiate the template
		if (!substituted_template_args.empty()) {
			FLASH_LOG(Templates, Debug, "  Attempting to instantiate template: ", func_name, " with ", substituted_template_args.size(), " arguments");

			ChunkedVector<ASTNode> substituted_args_nodes;
			std::vector<TypeSpecifierNode> substituted_arg_types;
			for (size_t i = 0; i < call.arguments().size(); ++i) {
				ASTNode substituted_arg = substitute(call.arguments()[i]);
				substituted_args_nodes.push_back(substituted_arg);
			}
			bool have_complete_substituted_arg_types =
				parser_.tryCollectFunctionCallArgTypes(substituted_args_nodes, substituted_arg_types);

			auto tryInstantiateExplicitFunctionTemplate = [&](std::string_view template_name) -> std::optional<ASTNode> {
				if (template_name.empty()) {
					return std::nullopt;
				}
				if (have_complete_substituted_arg_types) {
					std::optional<ASTNode> instantiated = parser_.try_instantiate_template_explicit(
						template_name,
						substituted_template_args,
						substituted_arg_types);
					if (instantiated.has_value()) {
						normalizePendingSemanticRoots();
					}
					return instantiated;
				}
				std::optional<ASTNode> instantiated = parser_.try_instantiate_template_explicit(template_name, substituted_template_args);
				if (instantiated.has_value()) {
					normalizePendingSemanticRoots();
				}
				return instantiated;
			};
			auto tryInstantiateExplicitMemberTemplateForCurrentOwner =
				[&](std::string_view member_name) -> std::optional<ASTNode> {
				if (!current_owner_type_name_.isValid() || member_name.empty()) {
					return std::nullopt;
				}
				std::vector<TemplateTypeArg> current_inst_args;
				if (auto owner_type_it = getTypesByNameMap().find(current_owner_type_name_);
					owner_type_it != getTypesByNameMap().end() && owner_type_it->second != nullptr) {
					const TypeInfo* owner_type_info = owner_type_it->second;
					current_inst_args.reserve(owner_type_info->templateArgs().size());
					for (const auto& stored_arg : owner_type_info->templateArgs()) {
						current_inst_args.push_back(toTemplateTypeArg(stored_arg));
					}
				}
				if (current_inst_args.empty()) {
					current_inst_args =
						collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteFunctionCallImpl");
				}
				FLASH_LOG(Templates, Debug,
					"Trying member template lookup for owner ",
					StringTable::getStringView(current_owner_type_name_),
					" with ", current_inst_args.size(), " bound args");
				Parser::AliasTemplateMaterializationResult canonical_owner =
					parser_.resolveCanonicalInstantiatedOwnerForLookup(
						StringTable::getStringView(current_owner_type_name_),
						std::span<const TemplateTypeArg>(
							current_inst_args.data(),
							current_inst_args.size()));
				std::string_view member_owner_name = canonical_owner.instantiated_name.empty()
					? StringTable::getStringView(current_owner_type_name_)
					: canonical_owner.instantiated_name;
				FLASH_LOG(Templates, Debug,
					"Member template canonical owner resolved to ", member_owner_name);
				std::optional<ASTNode> instantiated =
					parser_.try_instantiate_member_function_template_explicit(
						member_owner_name,
						member_name,
						substituted_template_args);
				if (instantiated.has_value()) {
					normalizePendingSemanticRoots();
				} else {
					FLASH_LOG(Templates, Debug,
						"Member template instantiation failed for ", member_owner_name,
						"::", member_name);
				}
				return instantiated;
			};
			auto templateArgsStillDependent = [&](std::span<const TemplateTypeArg> args) {
				for (const TemplateTypeArg& arg : args) {
					if (arg.is_dependent || arg.dependent_name.isValid() || arg.dependent_expr.has_value()) {
						return true;
					}
				}
				return false;
			};
			auto makeSubstitutedCallArguments = [&]() {
				ChunkedVector<ASTNode> substituted_args;
				for (size_t i = 0; i < call.arguments().size(); ++i) {
					substituted_args.push_back(substitute(call.arguments()[i]));
				}
				return substituted_args;
			};
			// First try function template instantiation to obtain accurate return type
			std::optional<ASTNode> instantiated_template = std::nullopt;
			if (!qualified_name.empty()) {
				if (size_t scope_pos = qualified_name.rfind("::"); scope_pos != std::string_view::npos) {
					std::string_view owner_name = qualified_name.substr(0, scope_pos);
					std::string_view member_name = qualified_name.substr(scope_pos + 2);
					if (current_owner_type_name_.isValid()) {
						bool source_owner_matches_current_instantiation = false;
						std::string_view current_owner_name = StringTable::getStringView(current_owner_type_name_);
						if (owner_name == current_owner_name) {
							source_owner_matches_current_instantiation = true;
						} else if (auto owner_type_it = getTypesByNameMap().find(current_owner_type_name_);
							owner_type_it != getTypesByNameMap().end() && owner_type_it->second != nullptr) {
							const TypeInfo* owner_type_info = owner_type_it->second;
							std::string_view base_template_name =
								StringTable::getStringView(owner_type_info->baseTemplateName());
							if (owner_name == base_template_name) {
								source_owner_matches_current_instantiation = true;
							} else {
								std::string_view qualified_base_template_name =
									buildQualifiedNameFromHandle(
										owner_type_info->sourceNamespace(),
										StringTable::getStringView(owner_type_info->baseTemplateName()));
								source_owner_matches_current_instantiation =
									!qualified_base_template_name.empty() &&
									owner_name == qualified_base_template_name;
							}
						}
						if (source_owner_matches_current_instantiation) {
							instantiated_template =
								tryInstantiateExplicitMemberTemplateForCurrentOwner(member_name);
						}
					}
					if (instantiated_template.has_value()) {
						// Current-instantiation member lookup already found the correct owner.
					} else {
						std::vector<TemplateTypeArg> current_inst_args =
							collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteFunctionCallImpl");
						Parser::AliasTemplateMaterializationResult canonical_owner =
							parser_.resolveCanonicalInstantiatedOwnerForLookup(
								owner_name,
								std::span<const TemplateTypeArg>(
									current_inst_args.data(),
									current_inst_args.size()));
						if (!canonical_owner.instantiated_name.empty()) {
							owner_name = canonical_owner.instantiated_name;
						}
						if (canonical_owner.resolved_type_info != nullptr) {
							// Only try member-template recovery when the owner resolved to an actual type.
							instantiated_template = parser_.try_instantiate_member_function_template_explicit(
								owner_name,
								member_name,
								substituted_template_args);
							if (instantiated_template.has_value()) {
								normalizePendingSemanticRoots();
							}
						}
					}
				}
				if (!instantiated_template.has_value()) {
					instantiated_template = tryInstantiateExplicitFunctionTemplate(qualified_name);
				}
			}
			if (!instantiated_template.has_value()) {
				instantiated_template =
					tryInstantiateExplicitMemberTemplateForCurrentOwner(func_name);
			}
			if (!instantiated_template.has_value()) {
				instantiated_template = tryInstantiateExplicitFunctionTemplate(func_name);
			}
			if (instantiated_template.has_value()) {
				if (instantiated_template->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = instantiated_template->as<FunctionDeclarationNode>();

					substituted_args_nodes.clear();
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args_nodes.push_back(substitute(call.arguments()[i]));
					}

					ExpressionNode& new_expr = emplaceDirectCallExpr(
						func_decl.decl_node(),
						&func_decl,
						std::move(substituted_args_nodes),
						call.called_from());
					CallMetadataCopyOptions copy_options;
					copy_options.copy_template_arguments = false;
					copyMetadataToExpr(new_expr, copy_options);
					if (func_decl.has_mangled_name()) {
						setCallMangledName(new_expr, func_decl.mangled_name());
					}
					std::vector<ASTNode> substituted_template_arg_nodes;
					substituted_template_arg_nodes.reserve(explicit_template_arg_nodes.size());
					for (const auto& arg_node : explicit_template_arg_nodes) {
						substituted_template_arg_nodes.push_back(substitute(arg_node));
					}
					setCallTemplateArguments(new_expr, std::move(substituted_template_arg_nodes));
					return ASTNode(&new_expr);
				}
			}

			// Try variable template instantiation before class template
			const bool had_dependent_template_args =
				templateArgsStillDependent(std::span<const TemplateTypeArg>(substituted_template_args.data(), substituted_template_args.size()));
			const bool has_variable_template_candidate =
				gTemplateRegistry.lookupVariableTemplate(func_name).has_value() ||
				(call.has_qualified_name() && gTemplateRegistry.lookupVariableTemplate(call.qualified_name()).has_value());
			auto var_template_node = parser_.try_instantiate_variable_template(func_name, substituted_template_args);
			if (var_template_node.has_value()) {
				normalizePendingSemanticRoots();
			}
			// If not found by simple name, try qualified name if available
			if (!var_template_node.has_value() && call.has_qualified_name()) {
				var_template_node = parser_.try_instantiate_variable_template(call.qualified_name(), substituted_template_args);
				if (var_template_node.has_value()) {
					normalizePendingSemanticRoots();
				}
			}
			if (var_template_node.has_value()) {
				FLASH_LOG(Templates, Debug, "  Successfully instantiated variable template: ", func_name);
				// Variable template instantiation returns the variable declaration node
				// We want to return the initializer expression
				if (var_template_node->is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var_decl = var_template_node->as<VariableDeclarationNode>();
					if (var_decl.initializer().has_value()) {
						return var_decl.initializer().value();
					}
				}
				// If not a variable declaration or no initializer, return as-is
				return *var_template_node;
			}
			if (had_dependent_template_args) {
				FLASH_LOG(Templates, Debug,
					"  Preserving unresolved dependent call after explicit template/variable-template instantiation miss: ",
					!qualified_name.empty() ? qualified_name : func_name);
				return materializeSubstitutedUnresolvedCall(makeSubstitutedCallArguments());
			}

			Parser::AliasTemplateMaterializationResult materialized_type =
				parser_.materializeTemplateInstantiationForLookup(
					!qualified_name.empty() ? qualified_name : func_name,
					substituted_template_args);
			if (materialized_type.resolved_type_info == nullptr && !qualified_name.empty()) {
				materialized_type =
					parser_.materializeTemplateInstantiationForLookup(
						func_name,
						substituted_template_args);
			}
			if (const TypeInfo* resolved_type_info = materialized_type.resolved_type_info) {
				TypeIndex new_type_index = resolved_type_info->registeredTypeIndex();
				int type_size_bits =
					resolved_type_info->hasStoredSize()
						? static_cast<int>(resolved_type_info->sizeInBits().value)
						: 64;

				FLASH_LOG(Templates, Debug, "  Successfully instantiated template with type_index=", new_type_index);

				// Create a TypeSpecifierNode for the instantiated type
				TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
					new_type_index.withCategory(resolved_type_info->typeEnum()),
					type_size_bits,
					Token{},
					CVQualifier::None,
					ReferenceQualifier::None);

				// Create a ConstructorCallNode instead of an ordinary CallExprNode
				substituted_args_nodes.clear();
				for (size_t i = 0; i < call.arguments().size(); ++i) {
					substituted_args_nodes.push_back(substitute(call.arguments()[i]));
				}
				ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
					ASTNode(&new_type),
					std::move(substituted_args_nodes),
					call.called_from());

				// Wrap in ExpressionNode
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
				return ASTNode(&new_expr);
			} else {
				FLASH_LOG(Templates, Warning, "  Failed to instantiate template: ", func_name);
				if (has_variable_template_candidate) {
					FLASH_LOG(Templates, Debug,
						"  Variable-template candidate instantiation failed; preserving explicit unresolved call");
					return materializeSubstitutedUnresolvedCall(makeSubstitutedCallArguments());
				}
			}
		}
	}

	// Check if this is actually a template constructor call like base_trait<T>()
	// In this case, the function_declaration might have template information
	FLASH_LOG(Templates, Debug, "  DeclarationNode identifier: ", decl_node.identifier_token().value());

	// Check the type_node - it might contain template information
	ASTNode type_node = decl_node.type_node();
	if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		FLASH_LOG(Templates, Debug, "  TypeSpecifierNode: type=", (int)type_spec.type(), " type_index=", type_spec.type_index());

		// If this is a struct type, it might be a template instantiation
		if (type_spec.category() == TypeCategory::Struct) {
			if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
				std::string_view type_name = StringTable::getStringView(type_info->name());
				FLASH_LOG(Templates, Debug, "  Type name: ", type_name);

				// Try to substitute template arguments in this type
				TypeSpecifierNode substituted_type = substituteInType(type_spec);

				// If substitution happened, create a new constructor call
				// Check if the type_index changed
				if (substituted_type.type_index() != type_spec.type_index()) {
					FLASH_LOG(Templates, Debug, "  Type was substituted, creating ConstructorCallNode");

					// Create a ConstructorCallNode instead of an ordinary CallExprNode
					ChunkedVector<ASTNode> substituted_args_nodes;
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args_nodes.push_back(substitute(call.arguments()[i]));
					}

					TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
					ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
						ASTNode(&new_type),
						std::move(substituted_args_nodes),
						call.called_from());

					// Wrap in ExpressionNode
					ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
					return ASTNode(&new_expr);
				}
			}
		}
	}
	// If not a template constructor call or no substitution needed, check if the function itself is a template
	// Look up the function in the symbol table
	std::string_view template_func_name = decl_node.identifier_token().value();
	auto symbol_opt = gSymbolTable.lookup(template_func_name);

	if (symbol_opt.has_value() && symbol_opt->is<TemplateFunctionDeclarationNode>()) {
		FLASH_LOG(Templates, Debug, "  Function is a template: ", template_func_name);

		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < call.arguments().size(); ++i) {
			ASTNode substituted_arg = substitute(call.arguments()[i]);
			substituted_args.push_back(substituted_arg);
		}

		std::optional<ASTNode> instantiated_opt = parser_.tryInstantiateTemplateFromCallArguments(
			call.has_qualified_name() ? call.qualified_name() : std::string_view(),
			template_func_name,
			substituted_args);
		if (instantiated_opt.has_value()) {
			normalizePendingSemanticRoots();
		}

		if (instantiated_opt.has_value() && instantiated_opt->is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& instantiated_func = instantiated_opt->as<FunctionDeclarationNode>();
			FLASH_LOG(Templates, Debug, "  Successfully instantiated template function");

			// Create a new direct CallExprNode with the instantiated function
			ExpressionNode& new_expr = emplaceDirectCallExpr(
				instantiated_func.decl_node(),
				&instantiated_func,
				std::move(substituted_args),
				call.called_from());
			copyMetadataToExpr(new_expr);
			if (instantiated_func.has_mangled_name()) {
				setCallMangledName(new_expr, instantiated_func.mangled_name());
			}
			return ASTNode(&new_expr);
		} else {
			FLASH_LOG(Templates, Warning, "  Failed to instantiate template function: ", template_func_name);
		}
	}

	if (call.has_dependent_unqualified_lookup_record()) {
		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < call.arguments().size(); ++i) {
			substituted_args.push_back(substitute(call.arguments()[i]));
		}

		std::vector<TypeSpecifierNode> substituted_arg_types;
		if (parser_.tryCollectFunctionCallArgTypes(substituted_args, substituted_arg_types)) {
			if (std::optional<ASTNode> resolved_target =
					parser_.resolveDependentUnqualifiedCallAtPointOfInstantiation(
						*call.dependent_unqualified_lookup_record(),
						substituted_args,
						substituted_arg_types);
				resolved_target.has_value()) {
				if (const FunctionDeclarationNode* target_func =
						get_function_decl_node(*resolved_target);
					target_func != nullptr) {
					ExpressionNode& new_expr = emplaceDirectCallExpr(
						target_func->decl_node(),
						target_func,
						std::move(substituted_args),
						call.called_from());
					CallMetadataCopyOptions copy_options;
					copy_options.copy_dependent_unqualified_lookup_record = false;
					copyMetadataToExpr(new_expr, copy_options);
					if (target_func->has_mangled_name()) {
						setCallMangledName(new_expr, target_func->mangled_name());
					}
					if (!target_func->parent_struct_name().empty()) {
						setCallQualifiedName(
							new_expr,
							StringBuilder()
								.append(target_func->parent_struct_name())
								.append("::")
								.append(target_func->decl_node().identifier_token().value())
								.commit());
					}
					return ASTNode(&new_expr);
				}
			}
		}
		return materializeSubstitutedUnresolvedCall(std::move(substituted_args));
	}

	if (call.has_dependent_qualified_lookup_record()) {
		const TypeInfo::DependentQualifiedNameRecord& dependent_record =
			*call.dependent_qualified_lookup_record();
		if (dependent_record.member_chain.size() == 1) {
			Token synthetic_member_token(
				Token::Type::Identifier,
				StringTable::getStringView(dependent_record.member_chain.back().name),
				call.called_from().line(),
				call.called_from().column(),
				call.called_from().file_index());
			QualifiedIdentifierNode synthetic_qual_id(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				synthetic_member_token);
			synthetic_qual_id.setDependentQualifiedName(dependent_record);
			ASTNode substituted_callee_node =
				substituteQualifiedIdentifier(synthetic_qual_id);
			if (substituted_callee_node.is<ExpressionNode>()) {
				const ExpressionNode& substituted_callee_expr =
					substituted_callee_node.as<ExpressionNode>();
				if (const auto* resolved_qual_id =
						std::get_if<QualifiedIdentifierNode>(
							&substituted_callee_expr);
					resolved_qual_id != nullptr &&
					!resolved_qual_id->hasDependentQualifiedName()) {
					const std::string_view resolved_owner_name =
						gNamespaceRegistry.getQualifiedName(
							resolved_qual_id->namespace_handle());
					const std::string_view resolved_member_name =
						resolved_qual_id->name();
					if (!resolved_owner_name.empty()) {
						ChunkedVector<ASTNode> substituted_args;
						for (size_t i = 0; i < call.arguments().size(); ++i) {
							substituted_args.push_back(substitute(call.arguments()[i]));
						}

						const FunctionDeclarationNode* target_func = nullptr;
						std::string_view mutable_resolved_owner_name =
							resolved_owner_name;
						if (std::optional<ASTNode> instantiated_member =
								parser_.instantiateLazyMemberForCanonicalOwner(
									mutable_resolved_owner_name,
									resolved_member_name,
									std::span<const TemplateTypeArg>{});
							instantiated_member.has_value() &&
							instantiated_member->is<FunctionDeclarationNode>()) {
							target_func = &instantiated_member->as<FunctionDeclarationNode>();
						}
						if (target_func == nullptr) {
							auto qualified_symbol =
								gSymbolTable.lookup_qualified(
									resolved_qual_id->namespace_handle(),
									resolved_member_name);
							if (qualified_symbol.has_value()) {
								target_func = get_function_decl_node(*qualified_symbol);
							}
						}
						if (target_func != nullptr) {
							Token called_from_token(
								Token::Type::Identifier,
								StringBuilder()
									.append(resolved_owner_name)
									.append("::")
									.append(resolved_member_name)
									.commit(),
								call.called_from().line(),
								call.called_from().column(),
								call.called_from().file_index());
							ExpressionNode& new_expr = emplaceDirectCallExpr(
								target_func->decl_node(),
								target_func,
								std::move(substituted_args),
								called_from_token);
							copyMetadataToExpr(new_expr);
							setCallQualifiedName(
								new_expr,
								StringBuilder()
									.append(resolved_owner_name)
									.append("::")
									.append(resolved_member_name)
									.commit());
							if (target_func->has_mangled_name()) {
								setCallMangledName(new_expr, target_func->mangled_name());
							}
							return ASTNode(&new_expr);
						}
					}
				}
			}
		}
		auto materializeRecordArgs =
			[&](std::span<const TypeInfo::TemplateArgInfo> stored_args) {
			std::vector<TemplateTypeArg> materialized_args;
			materialized_args.reserve(stored_args.size());
			for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
				TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
				if (arg.dependent_name.isValid()) {
					auto param_it =
						param_map_.find(StringTable::getStringView(arg.dependent_name));
					if (param_it != param_map_.end()) {
						arg = rebindDependentTemplateTypeArg(param_it->second, arg);
					}
				}
				materialized_args.push_back(std::move(arg));
			}
			return materialized_args;
		};
		auto areTemplateArgsConcrete = [&](std::span<const TemplateTypeArg> args) {
			for (const TemplateTypeArg& arg : args) {
				if (arg.is_dependent) {
					return false;
				}
				if (arg.is_value || !arg.type_index.is_valid()) {
					continue;
				}
				const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index);
				if (arg_type_info &&
					(arg_type_info->is_incomplete_instantiation_ ||
					 (arg_type_info->isTemplateInstantiation() &&
					  arg_type_info->getStructInfo() == nullptr))) {
					return false;
				}
			}
			return true;
		};

		std::string_view materialized_owner_name;
		std::string_view recorded_owner_name =
			StringTable::getStringView(dependent_record.owner_name);
		switch (dependent_record.owner_kind) {
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::CurrentInstantiation:
			if (dependent_record.owner_type.is_valid()) {
				if (const TypeInfo* owner_type_info =
						tryGetTypeInfo(dependent_record.owner_type)) {
					materialized_owner_name =
						StringTable::getStringView(owner_type_info->name());
				}
			}
			break;
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::TemplateParameter:
			if (dependent_record.owner_template_arguments.empty()) {
				auto owner_param_it = param_map_.find(recorded_owner_name);
				if (owner_param_it != param_map_.end()) {
					if (const TypeInfo* owner_type_info =
							tryGetTypeInfo(owner_param_it->second.type_index)) {
						materialized_owner_name =
							StringTable::getStringView(owner_type_info->name());
					}
				}
				break;
			}
			[[fallthrough]];
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::DependentInstantiation:
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::UnknownSpecialization: {
			std::vector<TemplateTypeArg> owner_args =
				materializeRecordArgs(dependent_record.owner_template_arguments);
			if (areTemplateArgsConcrete(owner_args)) {
				Parser::AliasTemplateMaterializationResult canonical_owner =
					parser_.resolveCanonicalInstantiatedOwnerForLookup(
						recorded_owner_name,
						std::span<const TemplateTypeArg>(
							owner_args.data(),
							owner_args.size()));
				if (!canonical_owner.instantiated_name.empty()) {
					materialized_owner_name = canonical_owner.instantiated_name;
				} else if (canonical_owner.resolved_type_info != nullptr) {
					materialized_owner_name =
						StringTable::getStringView(canonical_owner.resolved_type_info->name());
				}
			}
			break;
		}
		}

		if (!materialized_owner_name.empty() &&
			!dependent_record.member_chain.empty()) {
			const std::string_view materialized_record_owner_name =
				materialized_owner_name;
			std::string_view member_name =
				StringTable::getStringView(
					dependent_record.member_chain.back().name);
			std::vector<QualifiedTypeMemberAccess> owner_member_chain;
			owner_member_chain.reserve(
				dependent_record.member_chain.size() > 0
					? dependent_record.member_chain.size() - 1
					: 0);
			bool owner_chain_concrete = true;
			for (size_t i = 0; i + 1 < dependent_record.member_chain.size(); ++i) {
				const auto& member_record = dependent_record.member_chain[i];
				QualifiedTypeMemberAccess member_access;
				member_access.member_name = member_record.name;
				if (member_record.has_template_arguments) {
					std::vector<TemplateTypeArg> member_args =
						materializeRecordArgs(member_record.template_arguments);
					if (!areTemplateArgsConcrete(member_args)) {
						owner_chain_concrete = false;
						break;
					}
					member_access.has_template_arguments = true;
					member_access.template_arguments =
						&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(
							std::move(member_args));
				}
				owner_member_chain.push_back(std::move(member_access));
			}

			if (owner_chain_concrete) {
				if (!owner_member_chain.empty()) {
					std::string_view chained_owner_name = materialized_owner_name;
					bool owner_chain_materialized = true;
					for (size_t i = 0; i + 1 < dependent_record.member_chain.size(); ++i) {
						const auto& member_record = dependent_record.member_chain[i];
						const std::string_view member_name_for_owner =
							StringTable::getStringView(member_record.name);
						if (member_record.has_template_arguments) {
							std::vector<TemplateTypeArg> member_args =
								materializeRecordArgs(member_record.template_arguments);
							if (!areTemplateArgsConcrete(member_args)) {
								owner_chain_materialized = false;
								break;
							}
							const StringHandle chained_owner_handle =
								StringTable::getOrInternStringHandle(chained_owner_name);
							const StringHandle member_name_handle =
								StringTable::getOrInternStringHandle(member_name_for_owner);
							std::string_view qualified_owner_name;
							if (i == 0 &&
								!recorded_owner_name.empty()) {
								std::string_view pattern_owner_name =
									StringBuilder()
										.append(recorded_owner_name)
										.append("::")
										.append(member_name_for_owner)
										.commit();
								if (gTemplateRegistry.lookupTemplate(pattern_owner_name).has_value()) {
									qualified_owner_name = pattern_owner_name;
								}
							}
							if (qualified_owner_name.empty()) {
								qualified_owner_name =
									parser_.lookup_inherited_member_template_name(
										chained_owner_handle,
										member_name_handle,
										0);
							}
							if (qualified_owner_name.empty()) {
								qualified_owner_name =
									StringBuilder()
										.append(chained_owner_name)
										.append("::")
										.append(member_name_for_owner)
										.commit();
							}
							if (auto owner_it = getTypesByNameMap().find(chained_owner_handle);
								owner_it != getTypesByNameMap().end() &&
								owner_it->second != nullptr &&
								owner_it->second->hasInstantiationContext()) {
								const TypeInfo::InstantiationContext* parent_context =
									owner_it->second->instantiationContext();
								OuterTemplateBinding outer_binding;
								for (size_t binding_i = 0;
									 binding_i < parent_context->param_names.size() &&
									 binding_i < parent_context->param_args.size();
									 ++binding_i) {
									outer_binding.param_names.push_back(
										parent_context->param_names[binding_i]);
									outer_binding.param_args.push_back(
										toTemplateTypeArg(parent_context->param_args[binding_i]));
								}
								if (!outer_binding.param_names.empty()) {
									gTemplateRegistry.registerOuterTemplateBinding(
										StringTable::getOrInternStringHandle(qualified_owner_name),
										std::move(outer_binding));
								}
							}
							if (std::optional<ASTNode> instantiated_member_template =
									parser_.try_instantiate_class_template(
										qualified_owner_name,
										member_args,
										false);
								instantiated_member_template.has_value() &&
								instantiated_member_template->is<StructDeclarationNode>()) {
								parser_.registerAndNormalizeLateMaterializedTopLevelNode(
									*instantiated_member_template);
							}
							Parser::AliasTemplateMaterializationResult materialized_member_owner =
								parser_.resolveCanonicalInstantiatedOwnerForLookup(
									qualified_owner_name,
									std::span<const TemplateTypeArg>(
										member_args.data(),
										member_args.size()));
							chained_owner_name = materialized_member_owner.canonicalName();
							if (chained_owner_name.empty()) {
								chained_owner_name =
									parser_.get_instantiated_class_name(
										qualified_owner_name,
										member_args);
							} else {
								const StringHandle chained_owner_lookup_handle =
									StringTable::getOrInternStringHandle(
										chained_owner_name);
								auto chained_owner_it =
									getTypesByNameMap().find(chained_owner_lookup_handle);
								if (chained_owner_it == getTypesByNameMap().end() ||
									chained_owner_it->second == nullptr) {
									std::string_view instantiated_owner_name =
										parser_.get_instantiated_class_name(
											qualified_owner_name,
											member_args);
									if (!instantiated_owner_name.empty()) {
										chained_owner_name = instantiated_owner_name;
									}
								}
							}
							if (chained_owner_name.empty()) {
								owner_chain_materialized = false;
								break;
							}
							continue;
						}
						if (const TypeInfo* resolved_owner =
								parser_.resolveBaseClassMemberTypeChain(
									chained_owner_name,
									std::span<QualifiedTypeMemberAccess>(
										&owner_member_chain[i],
										1))) {
							chained_owner_name =
								StringTable::getStringView(resolved_owner->name());
						} else {
							owner_chain_materialized = false;
							break;
						}
					}
					if (owner_chain_materialized) {
						materialized_owner_name = chained_owner_name;
					} else {
						materialized_owner_name = {};
					}
				}

				if (!materialized_owner_name.empty()) {
					ChunkedVector<ASTNode> substituted_args;
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args.push_back(substitute(call.arguments()[i]));
					}
					const FunctionDeclarationNode* target_func = nullptr;

					std::optional<ASTNode> instantiated_member =
						parser_.instantiateLazyMemberForCanonicalOwner(
							materialized_owner_name,
							member_name,
							std::span<const TemplateTypeArg>{});
					if (instantiated_member.has_value() &&
						instantiated_member->is<FunctionDeclarationNode>()) {
						target_func =
							&instantiated_member->as<FunctionDeclarationNode>();
					}
					parser_.instantiateLazyClassToPhase(
						StringTable::getOrInternStringHandle(materialized_owner_name),
						ClassInstantiationPhase::Full);

					const FunctionDeclarationNode* resolved_struct_member = nullptr;
					StringHandle owner_handle =
						StringTable::getOrInternStringHandle(materialized_owner_name);
					StringHandle member_handle =
						StringTable::getOrInternStringHandle(member_name);
					auto owner_it = getTypesByNameMap().find(owner_handle);
					if (owner_it != getTypesByNameMap().end() &&
						owner_it->second != nullptr) {
						if (const StructTypeInfo* struct_info =
								owner_it->second->getStructInfo()) {
							auto member_result =
								struct_info->findMemberFunctionRecursive(member_handle);
							if (member_result.first != nullptr &&
								member_result.first->function_decl.is<FunctionDeclarationNode>()) {
								resolved_struct_member =
									&member_result.first->function_decl.as<FunctionDeclarationNode>();
							}
						}
					}
					if (target_func == nullptr) {
						target_func = resolved_struct_member;
					}
					if (target_func == nullptr) {
						std::vector<ASTNode> member_candidates =
							gSymbolTable.lookup_all(member_name);
						for (const ASTNode& candidate_node : member_candidates) {
							const FunctionDeclarationNode* candidate_func =
								get_function_decl_node(candidate_node);
							if (candidate_func == nullptr) {
								continue;
							}
							const std::string_view parent_name =
								candidate_func->parent_struct_name();
							if (parent_name == materialized_owner_name ||
								(!parent_name.empty() &&
								 parent_name.ends_with(materialized_owner_name))) {
								target_func = candidate_func;
								break;
							}
						}
					}
					if (target_func == nullptr &&
						!materialized_record_owner_name.empty() &&
						materialized_owner_name != materialized_record_owner_name) {
						std::string_view qualified_owner_name =
							StringBuilder()
								.append(materialized_record_owner_name)
								.append("::")
								.append(materialized_owner_name)
								.commit();
						std::string_view mutable_qualified_owner_name =
							qualified_owner_name;
						if (std::optional<ASTNode> qualified_instantiated_member =
								parser_.instantiateLazyMemberForCanonicalOwner(
									mutable_qualified_owner_name,
									member_name,
									std::span<const TemplateTypeArg>{});
							qualified_instantiated_member.has_value() &&
							qualified_instantiated_member->is<FunctionDeclarationNode>()) {
							target_func =
								&qualified_instantiated_member->as<FunctionDeclarationNode>();
							materialized_owner_name = mutable_qualified_owner_name;
						}
					}

					if (target_func != nullptr) {
						Token called_from_token(
							Token::Type::Identifier,
							StringBuilder()
								.append(materialized_owner_name)
								.append("::")
								.append(member_name)
								.commit(),
							call.called_from().line(),
							call.called_from().column(),
							call.called_from().file_index());
						ExpressionNode& new_expr = emplaceDirectCallExpr(
							target_func->decl_node(),
							target_func,
							std::move(substituted_args),
							called_from_token);
						copyMetadataToExpr(new_expr);
						setCallQualifiedName(
							new_expr,
							StringBuilder()
								.append(materialized_owner_name)
								.append("::")
								.append(member_name)
								.commit());
						if (target_func->has_mangled_name()) {
							setCallMangledName(new_expr, target_func->mangled_name());
						}
						return ASTNode(&new_expr);
					}
				}
			}
		}
	}

	// If not a template function call or instantiation failed, check for qualified owners
	// that became concrete during substitution and need canonical owner identity rebound.
	size_t scope_pos = func_name.rfind("::");
	if (scope_pos != std::string_view::npos) {
		std::string_view owner_name = func_name.substr(0, scope_pos);
		std::string_view member_name = func_name.substr(scope_pos + 2);

		// Collect concrete template arguments from param_map_
		std::vector<TemplateTypeArg> inst_args =
			collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteFunctionCallImpl");

		if (!inst_args.empty()) {
			Parser::AliasTemplateMaterializationResult canonical_owner =
				parser_.resolveCanonicalInstantiatedOwnerForLookup(
					owner_name,
					std::span<const TemplateTypeArg>(inst_args.data(), inst_args.size()));
			const bool owner_is_template_instantiation =
				canonical_owner.resolved_type_info != nullptr &&
				canonical_owner.resolved_type_info->isTemplateInstantiation();
			const bool owner_has_template_hash = !extractBaseTemplateName(owner_name).empty();
			if (owner_is_template_instantiation || owner_has_template_hash) {
				std::string_view instantiated_name = canonical_owner.instantiated_name.empty()
					? owner_name
					: canonical_owner.instantiated_name;

				// Build the corrected qualified function name
				StringBuilder new_func_name_builder;
				new_func_name_builder.append(instantiated_name).append("::").append(member_name);
				std::string_view new_func_name = new_func_name_builder.commit();

				FLASH_LOG(Templates, Debug, "  Substituted qualified function name: ", func_name, " -> ", new_func_name);

				// Create a new token with the corrected name
				Token new_func_token(Token::Type::Identifier, new_func_name,
									 call.called_from().line(), call.called_from().column(), call.called_from().file_index());

				// Substitute arguments
				ChunkedVector<ASTNode> substituted_args;
				for (size_t i = 0; i < call.arguments().size(); ++i) {
					substituted_args.push_back(substitute(call.arguments()[i]));
				}

				// Try to trigger lazy member function instantiation for the target
				parser_.instantiateLazyMemberForCanonicalOwner(
					instantiated_name,
					member_name,
					std::span<const TemplateTypeArg>{});

				StringHandle inst_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
				StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);

				const FunctionDeclarationNode* target_func = nullptr;
				auto type_it = getTypesByNameMap().find(inst_name_handle);
				if (type_it != getTypesByNameMap().end()) {
					if (const StructTypeInfo* struct_info = type_it->second->getStructInfo()) {
						for (const auto& member_func : struct_info->member_functions) {
							if (member_func.getName() == member_handle &&
								member_func.function_decl.is<FunctionDeclarationNode>()) {
								target_func = &member_func.function_decl.as<FunctionDeclarationNode>();
								break;
							}
						}
					}
				}

				if (target_func == nullptr) {
					FLASH_LOG(Templates, Debug, "  Canonical owner member not materialized yet, keeping original call");
					return wrapOriginalCall();
				}

				ExpressionNode& new_expr = emplaceDirectCallExpr(
					target_func->decl_node(),
					target_func,
					std::move(substituted_args),
					new_func_token);
				copyMetadataToExpr(new_expr);
				setCallQualifiedName(new_expr, new_func_name);
				if (target_func && target_func->has_mangled_name()) {
					setCallMangledName(new_expr, target_func->mangled_name());
				}
				return ASTNode(&new_expr);
			}
		}
	}

	// Return with substituted children preserved.
	FLASH_LOG(Templates, Debug, "  Returning function call as-is");
	ChunkedVector<ASTNode> substituted_fallback_args;
	for (size_t i = 0; i < call.arguments().size(); ++i) {
		substituted_fallback_args.push_back(substitute(call.arguments()[i]));
	}
	return materializeSubstitutedUnresolvedCall(std::move(substituted_fallback_args));
}

ASTNode ExpressionSubstitutor::substituteCallExpr(const CallExprNode& call) {
	if (!call.has_receiver()) {
		return substituteFunctionCallImpl(call);
	}

	ASTNode substituted_receiver = substitute(call.receiver());
	ChunkedVector<ASTNode> substituted_args;
	for (size_t i = 0; i < call.arguments().size(); ++i) {
		substituted_args.push_back(substitute(call.arguments()[i]));
	}

	if ((call.callee().is_member() || call.callee().is_static_member()) &&
		call.called_from().kind().is_identifier()) {
		if (const FunctionDeclarationNode* rebound_member =
				parser_.tryResolveConcreteMemberFunction(substituted_receiver, call.called_from().value());
			rebound_member != nullptr) {
			if (auto receiver_type = parser_.get_expression_type(substituted_receiver);
				receiver_type.has_value() && is_struct_type(receiver_type->category())) {
				if (const TypeInfo* receiver_type_info = tryGetTypeInfo(receiver_type->type_index());
					receiver_type_info != nullptr) {
					std::string_view class_name =
						StringTable::getStringView(receiver_type_info->name());
					if (parser_.instantiateLazyMemberForCanonicalOwner(
							class_name,
							call.called_from().value(),
							std::span<const TemplateTypeArg>{})
							.has_value()) {
						rebound_member =
							parser_.tryResolveConcreteMemberFunction(substituted_receiver, call.called_from().value());
					}
				}
			}

			if (rebound_member != nullptr) {
				CallExprNode rebound_call = makeResolvedMemberCallExpr(
					substituted_receiver,
					*rebound_member,
					std::move(substituted_args),
					call.called_from());
				copyCallMetadataWithTransformedTemplateArguments(
					rebound_call,
					call,
					[this](const ASTNode& template_arg) {
						return substitute(template_arg);
					},
					CallMetadataCopyOptions{});
				if (rebound_member->has_mangled_name()) {
					rebound_call.set_mangled_name(rebound_member->mangled_name());
				}
				ExpressionNode& rebound_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(rebound_call);
				return ASTNode(&rebound_expr);
			}
		}
	}

	CallExprNode substituted_call(call.callee(), substituted_receiver, std::move(substituted_args), call.called_from());
	copyCallMetadataWithTransformedTemplateArguments(
		substituted_call,
		call,
		[this](const ASTNode& template_arg) {
			return substitute(template_arg);
		},
		CallMetadataCopyOptions{});

	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(substituted_call);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteBinaryOp(const BinaryOperatorNode& binop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing binary operator");

	// Recursively substitute in left and right operands
	ASTNode substituted_lhs = substitute(binop.get_lhs());
	ASTNode substituted_rhs = substitute(binop.get_rhs());

	// Create new BinaryOperatorNode with substituted operands
	BinaryOperatorNode new_binop_value(
		binop.get_token(),
		substituted_lhs,
		substituted_rhs);
	parser_.annotateConcreteBinaryOperatorOverload(new_binop_value);

	// Wrap in ExpressionNode so it can be evaluated by try_evaluate_constant_expression
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_binop_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteUnaryOp(const UnaryOperatorNode& unop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing unary operator");

	// Recursively substitute in operand
	ASTNode substituted_operand = substitute(unop.get_operand());

	// Create new UnaryOperatorNode with substituted operand
	UnaryOperatorNode new_unop_value(
		unop.get_token(),
		substituted_operand,
		unop.is_prefix(),
		unop.is_builtin_addressof());

	// Wrap in ExpressionNode so it can be evaluated by try_evaluate_constant_expression
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_unop_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteTernaryOp(const TernaryOperatorNode& ternary) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing ternary operator");

	// Recursively substitute in condition, true_expr, and false_expr
	ASTNode substituted_condition = substitute(ternary.condition());
	ASTNode substituted_true = substitute(ternary.true_expr());
	ASTNode substituted_false = substitute(ternary.false_expr());

	// Create new TernaryOperatorNode with substituted operands
	TernaryOperatorNode new_ternary(
		substituted_condition,
		substituted_true,
		substituted_false,
		ternary.get_token());

	// Wrap in ExpressionNode so it can be evaluated by try_evaluate_constant_expression
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ternary);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteIdentifier(const IdentifierNode& id) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing identifier: ", id.name());

	// Check if this identifier is a template parameter that needs substitution
	auto it = param_map_.find(id.name());
	if (it != param_map_.end()) {
		const TemplateTypeArg& arg = it->second;
		FLASH_LOG(Templates, Debug, "  Found template parameter substitution: ", id.name(),
				  " -> type=", (int)arg.typeEnum(), ", type_index=", arg.type_index, ", is_value=", arg.is_value,
				  ", is_template_template_arg=", arg.is_template_template_arg);

		// Handle template-template parameters
		// When W is a TTP bound to "box", we need to return an identifier for "box"
		// so that later processing (e.g., W<int>::member) can resolve correctly
		if (arg.is_template_template_arg && arg.template_name_handle.isValid()) {
			std::string_view concrete_template_name = StringTable::getStringView(arg.template_name_handle);
			FLASH_LOG(Templates, Debug, "  Template-template parameter, substituting with template name: ", concrete_template_name);

			// Create an identifier for the concrete template
			Token concrete_token(Token::Type::Identifier, concrete_template_name, 0, 0, 0);
			IdentifierNode& concrete_id = gChunkedAnyStorage.emplace_back<IdentifierNode>(concrete_token);
			ExpressionNode& expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(concrete_id);
			return ASTNode(&expr);
		}

		// Handle non-type template parameters (values)
		if (arg.is_value) {
			FLASH_LOG(Templates, Debug, "  Non-type template parameter, creating literal with value: ", arg.value);

			// Determine the type based on the template argument's base_type
			TypeCategory literal_cat = arg.category();
			if (literal_cat == TypeCategory::Template || literal_cat == TypeCategory::UserDefined) {
				// For template parameters, default to int
				literal_cat = TypeCategory::Int;
			}

			// Handle bool types specially with BoolLiteralNode
			if (literal_cat == TypeCategory::Bool) {
				std::string_view bool_str = (arg.value != 0) ? "true" : "false";
				Token bool_token(Token::Type::Keyword, bool_str, 0, 0, 0);
				BoolLiteralNode& bool_literal = gChunkedAnyStorage.emplace_back<BoolLiteralNode>(
					bool_token,
					arg.value != 0);
				ExpressionNode& expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(bool_literal);
				return ASTNode(&expr);
			}

			// Create a numeric literal from the value
			std::string_view value_str = StringBuilder().append(static_cast<uint64_t>(arg.value)).commit();
			Token num_token(Token::Type::Literal, value_str, 0, 0, 0);

			NumericLiteralNode& literal = gChunkedAnyStorage.emplace_back<NumericLiteralNode>(
				num_token,
				static_cast<unsigned long long>(arg.value),
				literal_cat,
				TypeQualifier::None,
				64);

			ExpressionNode& expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(literal);
			return ASTNode(&expr);
		}

		// Handle type template parameters
		// Create a TypeSpecifierNode from the template argument
		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			arg.type_index.withCategory(arg.typeEnum()),
			64,	// Default size, will be adjusted by type system
			Token{},
			arg.cv_qualifier,
			ReferenceQualifier::None);

		// Add pointer levels if needed
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			new_type.add_pointer_level();
		}

		// Set reference qualifiers
		new_type.set_reference_qualifier(arg.ref_qualifier);

		return ASTNode(&new_type);
	}

	// Not a template parameter, return as-is (wrapped in ExpressionNode so the
	// evaluator's is<ExpressionNode>() check passes)
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(id);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteQualifiedIdentifier(const QualifiedIdentifierNode& qual_id) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing qualified identifier: ", qual_id.full_name());

	// Cycle-detection guard: prevent re-entrant substitution of the same
	// qualified identifier (namespace × member-name), which can arise when
	// a dependent template arg's expression refers back to the same qualified
	// identifier currently being resolved.
	const uint64_t qual_id_key =
		(static_cast<uint64_t>(qual_id.namespace_handle().index) << 32) |
		static_cast<uint64_t>(qual_id.nameHandle().handle);
	if (!substituting_qual_ids_.insert(qual_id_key).second) {
		ExpressionNode& deferred_expr =
			gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&deferred_expr);
	}
	ScopeGuard qual_id_guard([&]() { substituting_qual_ids_.erase(qual_id_key); });

	// Qualified identifiers like R1_T::num need template parameter substitution in the namespace part
	// The namespace is stored as a mangled template name like "R1_T" (template R1 with parameter T)
	// We need to substitute T with its concrete type to get "R1_long"

	// Get the namespace name (e.g., "R1_T")
	std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
	constexpr std::string_view kNestedTypeAliasName = "type";
	auto canonicalizeLookupOwnerForMember =
		[&](const Parser::AliasTemplateMaterializationResult& materialized_owner,
			std::string_view member_name) -> std::string_view {
		std::string_view owner_name = materialized_owner.canonicalName();
		if (owner_name.empty() || member_name == kNestedTypeAliasName) {
			return owner_name;
		}

		const TypeInfo* owner_type_info = materialized_owner.resolved_type_info;
		if (owner_type_info == nullptr) {
			owner_type_info = findTypeByName(
				StringTable::getOrInternStringHandle(owner_name));
		}
		if (owner_type_info != nullptr &&
			owner_type_info->isStruct()) {
			if (const StructTypeInfo* owner_struct_info =
					owner_type_info->getStructInfo();
				owner_struct_info != nullptr) {
				StringHandle member_handle =
					StringTable::getOrInternStringHandle(member_name);
				auto [static_member, owner_struct] =
					owner_struct_info->findStaticMemberRecursive(member_handle);
				if (static_member != nullptr || owner_struct != nullptr) {
					return owner_name;
				}
			}
		}

		StringHandle nested_alias_handle = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(owner_name)
				.append("::")
				.append(kNestedTypeAliasName)
				.commit());
		auto nested_alias_it = getTypesByNameMap().find(nested_alias_handle);
		if (nested_alias_it == getTypesByNameMap().end() ||
			nested_alias_it->second == nullptr) {
			return owner_name;
		}

		ResolvedAliasTypeInfo resolved_nested_alias = resolveAliasTypeInfo(
			nested_alias_it->second->registeredTypeIndex().withCategory(
				nested_alias_it->second->typeEnum()));
		const TypeInfo* nested_target_info =
			resolved_nested_alias.terminal_type_info;
		if (nested_target_info == nullptr &&
			resolved_nested_alias.type_index.is_valid()) {
			nested_target_info =
				tryGetTypeInfo(resolved_nested_alias.type_index);
		}
		return nested_target_info != nullptr &&
				nested_target_info->name().isValid()
			? StringTable::getStringView(nested_target_info->name())
			: owner_name;
	};

	if (const TypeInfo::DependentQualifiedNameRecord* dependent_name =
			qual_id.dependentQualifiedName()) {
		std::string_view owner_name =
			StringTable::getStringView(dependent_name->owner_name);
		std::string_view materialized_owner_name;
		switch (dependent_name->owner_kind) {
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::CurrentInstantiation:
			if (current_owner_type_name_.isValid()) {
				materialized_owner_name =
					StringTable::getStringView(current_owner_type_name_);
			} else if (dependent_name->owner_type.is_valid()) {
				if (const TypeInfo* owner_type_info =
						tryGetTypeInfo(dependent_name->owner_type)) {
					materialized_owner_name =
						StringTable::getStringView(owner_type_info->name());
				}
			}
			break;
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::TemplateParameter:
			if (dependent_name->owner_template_arguments.empty()) {
				if (auto owner_subst_it = param_map_.find(owner_name);
					owner_subst_it != param_map_.end()) {
					if (const TypeInfo* owner_type_info =
							tryGetTypeInfo(owner_subst_it->second.type_index)) {
						materialized_owner_name =
							StringTable::getStringView(owner_type_info->name());
					}
				}
				break;
			}
			[[fallthrough]];
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::DependentInstantiation:
		case TypeInfo::DependentQualifiedNameRecord::OwnerKind::UnknownSpecialization: {
			InlineVector<TemplateTypeArg, 4> owner_args =
				materializeDependentRecordTemplateArgs(
					dependent_name->owner_template_arguments,
					kInitialDependentMemberTypeResolutionDepth);
			if (!owner_args.empty() && !templateArgsStillDependent(owner_args)) {
				if (auto owner_template_subst_it = param_map_.find(owner_name);
					owner_template_subst_it != param_map_.end() &&
					owner_template_subst_it->second.is_template_template_arg) {
					owner_name = StringTable::getStringView(
						owner_template_subst_it->second.template_name_handle);
				}
				Parser::AliasTemplateMaterializationResult materialized_owner =
					parser_.materializeTemplateInstantiationForLookup(owner_name, owner_args);
				materialized_owner_name = materialized_owner.canonicalName();
				if (materialized_owner_name.empty()) {
					materialized_owner_name =
						parser_.get_instantiated_class_name(owner_name, owner_args);
				}
			}
			break;
		}
		}

		if (!materialized_owner_name.empty() && !dependent_name->member_chain.empty()) {
			std::string_view materialized_namespace = materialized_owner_name;
			if (dependent_name->member_chain.size() > 1) {
				std::vector<QualifiedTypeMemberAccess> namespace_member_chain;
				namespace_member_chain.reserve(
					dependent_name->member_chain.size() - 1);
				for (size_t member_index = 0;
					 member_index + 1 < dependent_name->member_chain.size();
					 ++member_index) {
					const auto& member_record =
						dependent_name->member_chain[member_index];
					QualifiedTypeMemberAccess member_access;
					member_access.member_name = member_record.name;
					if (member_record.has_template_arguments) {
						InlineVector<TemplateTypeArg, 4> member_args =
							materializeDependentRecordTemplateArgs(
								member_record.template_arguments,
								kInitialDependentMemberTypeResolutionDepth);
						if (templateArgsStillDependent(member_args)) {
							ExpressionNode& deferred_expr =
								gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
							return ASTNode(&deferred_expr);
						}
						member_access.has_template_arguments = true;
						member_access.template_arguments =
							&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(
								member_args.toVector());
					}
					namespace_member_chain.push_back(std::move(member_access));
				}
				const TypeInfo* resolved_namespace =
					resolveQualifiedMemberNamespaceChain(
						materialized_owner_name,
						namespace_member_chain);
				if (resolved_namespace == nullptr ||
					!resolved_namespace->name().isValid()) {
					ExpressionNode& deferred_expr =
						gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
					return ASTNode(&deferred_expr);
				}
				materialized_namespace =
					StringTable::getStringView(resolved_namespace->name());
			}

			const auto& final_member = dependent_name->member_chain.back();
			std::string_view final_member_name =
				StringTable::getStringView(final_member.name);
			Token final_token = qual_id.identifier_token();
			if (final_member.name != qual_id.nameHandle()) {
				final_token = Token(
					Token::Type::Identifier,
					final_member_name,
					qual_id.identifier_token().line(),
					qual_id.identifier_token().column(),
					qual_id.identifier_token().file_index());
			}
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				StringTable::getOrInternStringHandle(materialized_namespace));
			QualifiedIdentifierNode& new_qual_id =
				gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
					new_ns_handle,
					final_token);
			auto substituteQualIdTemplateArgs = [&]() -> std::vector<ASTNode> {
				std::vector<ASTNode> result;
				result.reserve(qual_id.template_arguments().size());
				for (const ASTNode& template_arg_node : qual_id.template_arguments()) {
					result.push_back(substitute(template_arg_node));
				}
				return result;
			};
			if (final_member.has_template_arguments) {
				InlineVector<TemplateTypeArg, 4> final_member_args =
					materializeDependentRecordTemplateArgs(
						final_member.template_arguments,
						kInitialDependentMemberTypeResolutionDepth);
				if (templateArgsStillDependent(final_member_args)) {
					ExpressionNode& deferred_expr =
						gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
					return ASTNode(&deferred_expr);
				}
				std::vector<ASTNode> explicit_template_arg_nodes =
					materializeTemplateArgumentNodesForQualifiedId(
						std::span<const TemplateTypeArg>(
							final_member_args.data(),
							final_member_args.size()),
						final_token);
				if (explicit_template_arg_nodes.empty() &&
					qual_id.has_template_arguments()) {
					explicit_template_arg_nodes = substituteQualIdTemplateArgs();
				}
				if (!explicit_template_arg_nodes.empty()) {
					new_qual_id.set_template_arguments(
						std::move(explicit_template_arg_nodes));
				}
			} else if (qual_id.has_template_arguments()) {
				std::vector<ASTNode> substituted = substituteQualIdTemplateArgs();
				if (!substituted.empty()) {
					new_qual_id.set_template_arguments(std::move(substituted));
				}
			}
			FLASH_LOG(Templates, Debug, "  Record-substituted qualified-id: ",
					  qual_id.full_name(), " -> ", materialized_namespace, "::",
					  final_member_name);
			ExpressionNode& new_expr =
				gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
			return ASTNode(&new_expr);
		}
	}

	// Check for TTP placeholder names (e.g., "W$0" where W is a template-template parameter
	// and $0 encodes the template arguments). These are created when parsing expressions
	// like W<int>::id where W is a TTP. The placeholder name encodes: TTP_name$type_index
	{
		size_t dollar_pos = ns_name.find('$');
		if (dollar_pos != std::string_view::npos && dollar_pos > 0) {
			std::string_view ttp_name = ns_name.substr(0, dollar_pos);
			std::string_view encoded_args = ns_name.substr(dollar_pos + 1);

			// Check if ttp_name is a template-template parameter in our substitution map
			auto ttp_it = param_map_.find(ttp_name);
			if (ttp_it != param_map_.end() && ttp_it->second.is_template_template_arg) {
				const TemplateTypeArg& ttp_arg = ttp_it->second;
				FLASH_LOG(Templates, Debug, "  Detected TTP placeholder '", ns_name,
						  "' - TTP '", ttp_name, "' maps to template '",
						  StringTable::getStringView(ttp_arg.template_name_handle), "'");

				// Get the concrete template name
				std::string_view concrete_template_name = StringTable::getStringView(ttp_arg.template_name_handle);

				// Parse the encoded args - format uses ';' separators and explicit value/type markers.
				std::vector<TemplateTypeArg> instantiation_args;
				std::string_view remaining = encoded_args;
				while (!remaining.empty()) {
					size_t separator = remaining.find(';');
					std::string_view arg_str = (separator != std::string_view::npos)
						? remaining.substr(0, separator)
						: remaining;
					TemplateTypeArg decoded_arg;
					if (!decodeTTPPlaceholderArg(arg_str, decoded_arg)) {
						FLASH_LOG(Templates, Warning, "  Failed to parse TTP arg encoding: '", arg_str, "'");
						break;
					}

					instantiation_args.push_back(decoded_arg);

					if (separator != std::string_view::npos) {
						remaining = remaining.substr(separator + 1);
					} else {
						break;
					}
				}

				// Now instantiate the concrete template with the parsed args
				if (!instantiation_args.empty()) {
					Parser::AliasTemplateMaterializationResult materialized_type =
						parser_.materializeTemplateInstantiationForLookup(concrete_template_name, instantiation_args);
					if (!materialized_type.instantiated_name.empty()) {
						StringHandle instantiated_name_handle =
							StringTable::getOrInternStringHandle(materialized_type.instantiated_name);

						// Create a new QualifiedIdentifierNode with the instantiated type as namespace
						NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
							NamespaceRegistry::GLOBAL_NAMESPACE,
							instantiated_name_handle);

						QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
							new_ns_handle,
							qual_id.identifier_token());
						FLASH_LOG(Templates, Debug, "  Substituted TTP: ", ns_name, "::", qual_id.name(), " -> ",
								  materialized_type.instantiated_name, "::", qual_id.name());
						ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
						return ASTNode(&new_expr);
					}
				}
			}
		}
	}

	// Check if the entire namespace name is a template parameter (e.g., _R1::num where _R1 is typename _R1)
	// This handles patterns like _R1::num where _R1 is substituted with a concrete struct type
	{
		auto param_it = param_map_.find(ns_name);
		if (param_it != param_map_.end()) {
			const TemplateTypeArg& concrete_type = param_it->second;
			FLASH_LOG(Templates, Debug, "  Namespace '", ns_name, "' is a template parameter, substituting with concrete type");

			// The concrete type should be a class type - get its instantiated name.
			//
			// Do not gate this solely on TemplateTypeArg::category().  Several dependent
			// substitution paths preserve the correct TypeIndex slot but leave the
			// embedded category as Invalid/UserDefined until the TypeInfo is consulted.
			// For a qualified-id such as A::value in a default NTTP, failing to look
			// through the TypeIndex keeps the name as the dependent spelling "A::value"
			// and the constexpr evaluator quite correctly diagnoses it as undefined.
			// This order ensures qualified-id expressions like A::value resolve through
			// alias chains to the concrete struct/class owner type.
			Parser::AliasTemplateMaterializationResult canonical_owner =
				parser_.materializeCanonicalOwnerTypeForLookup(concrete_type);
			const TypeInfo* type_info = canonical_owner.resolved_type_info;
			if (type_info != nullptr && (type_info->isStruct() || type_info->getStructInfo() != nullptr)) {
				StringHandle type_name_handle =
					canonical_owner.canonicalNameHandle().isValid()
						? canonical_owner.canonicalNameHandle()
						: type_info->name();

				// Create a new QualifiedIdentifierNode with the concrete struct as namespace
				NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
					NamespaceRegistry::GLOBAL_NAMESPACE,
					type_name_handle);

				QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
					new_ns_handle,
					qual_id.identifier_token());
				FLASH_LOG(Templates, Debug, "  Substituted: ", ns_name, "::", qual_id.name(), " -> ",
						  StringTable::getStringView(type_name_handle), "::", qual_id.name());
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
				return ASTNode(&new_expr);
			}

			// Template parameter was found but the concrete type is not a struct/class
			// (e.g., !type_index.is_valid() due to incomplete resolution, or base_type is Template).
			// Return as-is rather than falling through to $/_-based separator logic which
			// would incorrectly parse the namespace name as a mangled template instantiation.
			FLASH_LOG(Templates, Debug, "  Template parameter '", ns_name,
					  "' found but concrete type is not a resolvable struct (base_type=",
					  static_cast<int>(concrete_type.typeEnum()), ", type_index=", concrete_type.type_index,
					  "), returning as-is");
			{
				ExpressionNode& fallback_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
				return ASTNode(&fallback_expr);
			}
		}
	}

	if (current_owner_type_name_.isValid() && !ns_name.empty()) {
		if (const TypeInfo* resolved_member_info =
				resolveCurrentOwnerQualifiedNamespaceMember(
					StringTable::getOrInternStringHandle(ns_name));
			resolved_member_info != nullptr) {
			StringHandle resolved_name_handle = resolved_member_info->name();
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				resolved_name_handle);
			QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
				new_ns_handle,
				qual_id.identifier_token());
			FLASH_LOG(Templates, Debug, "  Resolved struct-local alias namespace '", ns_name,
					  "' -> ", StringTable::getStringView(resolved_name_handle),
					  " in owner ", StringTable::getStringView(current_owner_type_name_));
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
			return ASTNode(&new_expr);
		}
	}

	// Check if the namespace name contains template parameters (hash-based naming)
	std::string_view base_template_name = extractBaseTemplateName(ns_name);
	if (base_template_name.empty()) {
		// Not a template instantiation, return as-is
		FLASH_LOG(Templates, Debug, "  No template parameters in namespace, returning as-is");
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&new_expr);
	}

	FLASH_LOG(Templates, Debug, "  Base template: ", base_template_name);

	// Build concrete template arguments by looking up the dependent placeholder's stored args
	// and substituting template parameters from param_map_.
	// This is critical: we must use the placeholder's OWN template args (e.g., __static_sign<_Pn>
	// has 1 arg), not the outer template's full param_map_ (e.g., ratio<_Num, _Den> has 2 params).
	std::vector<TemplateTypeArg> inst_args;
	bool source_instantiation_is_dependent = false;
	bool materialized_had_substitution = false;

	auto ns_name_handle = StringTable::getOrInternStringHandle(ns_name);
	auto type_it = getTypesByNameMap().find(ns_name_handle);
	auto template_args_still_dependent = [&](std::span<const TemplateTypeArg> args) {
		for (const TemplateTypeArg& arg : args) {
			if (arg.is_dependent) {
				return true;
			}
			if (arg.is_value || !arg.type_index.is_valid()) {
				continue;
			}
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				std::string_view arg_type_name = StringTable::getStringView(arg_type_info->name());
				if (param_map_.find(arg_type_name) != param_map_.end()) {
					return true;
				}
			}
		}
		return false;
	};
	if (type_it != getTypesByNameMap().end() &&
		type_it->second != nullptr &&
		(type_it->second->isTemplateInstantiation() || !type_it->second->templateArgs().empty())) {
		for (const auto& source_arg : type_it->second->templateArgs()) {
			if (source_arg.dependent_name.isValid()) {
				source_instantiation_is_dependent = true;
				break;
			}
		}
		MaterializedStoredTemplateArgs materialized_args =
			materializeStoredTemplateArgs(*type_it->second, /*evaluate_dependent_member_values=*/true, kInitialDependentMemberTypeResolutionDepth);
		materialized_had_substitution = materialized_args.had_substitution;
		inst_args = std::move(materialized_args.args);
	}

	// Fallback: if TypeInfo lookup found no stored args (e.g., the placeholder wasn't registered
	// in getTypesByNameMap(), or it's a non-template namespace), fall back to using the full param_map_.
	// This handles cases like pack expansion bases where no TypeInfo exists.
	if (inst_args.empty() && (type_it == getTypesByNameMap().end() || type_it->second == nullptr)) {
		inst_args = collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteQualifiedIdentifier");
	}
	for (TemplateTypeArg& arg : inst_args) {
		if (!arg.is_value && !arg.is_template_template_arg && is_builtin_type(arg.category())) {
			TypeIndex canonical_builtin_index = nativeTypeIndex(arg.category());
			if (canonical_builtin_index.is_valid()) {
				arg.type_index = canonical_builtin_index;
				arg.setCategory(arg.category());
			}
		}
	}
	for (TemplateTypeArg& arg : inst_args) {
		if (!arg.is_value && arg.type_index.is_valid()) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				std::string_view arg_type_name = StringTable::getStringView(arg_type_info->name());
				auto param_it = param_map_.find(arg_type_name);
				if (param_it != param_map_.end()) {
					arg = rebindDependentTemplateTypeArg(param_it->second, arg);
				} else if (auto context_binding =
							   resolveContextBinding(arg_type_info->name(), environment_);
						   context_binding.has_value()) {
					arg = rebindDependentTemplateTypeArg(*context_binding, arg);
				}
			}
		}
		if (!arg.is_dependent) {
			continue;
		}

		for (size_t concretize_attempt = 0; concretize_attempt < 8 && arg.is_dependent; ++concretize_attempt) {
			if (arg.dependent_name.isValid()) {
				std::string_view dependent_name = StringTable::getStringView(arg.dependent_name);
				auto param_it = param_map_.find(dependent_name);
				if (param_it != param_map_.end()) {
					arg = rebindDependentTemplateTypeArg(param_it->second, arg);
					continue;
				}
			}

			if (arg.is_value && arg.dependent_expr.has_value()) {
				ASTNode substituted_expr = substitute(*arg.dependent_expr);
				if (auto value = parser_.try_evaluate_constant_expression(substituted_expr)) {
					TemplateTypeArg concrete_arg(value->value, value->type);
					concrete_arg.is_pack = arg.is_pack;
					arg = concrete_arg;
					break;
				}
				arg.dependent_expr = std::move(substituted_expr);
			}

			break;
		}
	}
	if (!inst_args.empty() && template_args_still_dependent(inst_args)) {
		// Keep unresolved qualified-ids dependent until a later instantiation phase.
		// This is standard-conforming for dependent names and avoids selecting an
		// unrelated concrete specialization from global type state.
		FLASH_LOG(Templates, Debug, "  substituteQualifiedIdentifier: inst_args still dependent, returning as-is for ns='", ns_name, "'");
		ExpressionNode& deferred_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&deferred_expr);
	}
	if (!inst_args.empty() &&
		type_it != getTypesByNameMap().end() &&
		type_it->second != nullptr &&
		!source_instantiation_is_dependent &&
		!materialized_had_substitution) {
		// Namespace already names a concrete instantiation; keep it stable instead
		// of rebuilding from reconstructed args, which can lose canonical type indices.
		ExpressionNode& concrete_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&concrete_expr);
	}

	if (!inst_args.empty()) {
		FLASH_LOG(Templates, Debug, "  Triggering instantiation of template '", base_template_name,
				  "' with ", inst_args.size(), " arguments");
		Parser::AliasTemplateMaterializationResult materialized_namespace =
			parser_.materializeTemplateInstantiationForLookup(base_template_name, inst_args);
		std::string_view instantiated_name = canonicalizeLookupOwnerForMember(
			materialized_namespace,
			qual_id.name());

		FLASH_LOG(Templates, Debug, "  Substituted namespace: ", ns_name, " -> ", instantiated_name);

		// Create a new QualifiedIdentifierNode with the correct instantiated namespace
		StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
		NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
			NamespaceRegistry::GLOBAL_NAMESPACE,
			instantiated_name_handle);

		QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
			new_ns_handle,
			qual_id.identifier_token());
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
		return ASTNode(&new_expr);
	}

	// Empty pack expansion case: pack expanded to 0 elements, instantiate base template with 0 args
	if (materialized_had_substitution && inst_args.empty() && !base_template_name.empty()) {
		FLASH_LOG(Templates, Debug, "  Empty pack expansion for '", base_template_name, "', instantiating with 0 args");
		Parser::AliasTemplateMaterializationResult materialized_namespace =
			parser_.materializeTemplateInstantiationForLookup(base_template_name, {});
		std::string_view instantiated_name = canonicalizeLookupOwnerForMember(
			materialized_namespace,
			qual_id.name());
		if (!instantiated_name.empty()) {
			FLASH_LOG(Templates, Debug, "  Empty-pack substituted namespace: ", ns_name, " -> ", instantiated_name);
			StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				instantiated_name_handle);
			QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
				new_ns_handle,
				qual_id.identifier_token());
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
			return ASTNode(&new_expr);
		}
	}

	// No template arguments - just return as-is
	FLASH_LOG(Templates, Debug, "  No template arguments to substitute");
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteMemberAccess(const MemberAccessNode& member_access) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing member access on member: ", member_access.member_name());

	// Recursively substitute in the object expression
	// For expressions like "R1<T>::num", the object might be a template instantiation
	ASTNode substituted_object = substitute(member_access.object());

	// Create new MemberAccessNode with substituted object
	MemberAccessNode new_member_value(
		substituted_object,
		member_access.member_token(),
		member_access.is_arrow());

	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_member_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteSizeofExpr(const SizeofExprNode& sizeof_expr) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing sizeof expression");

	// Get the type or expression from the sizeof
	ASTNode type_or_expr = sizeof_expr.type_or_expr();

	if (sizeof_expr.is_type() && type_or_expr.is<TypeSpecifierNode>()) {
		// This is sizeof(type) - substitute the type
		const TypeSpecifierNode& type_spec = type_or_expr.as<TypeSpecifierNode>();
		TypeSpecifierNode substituted_type = substituteInType(type_spec);

		// Create new TypeSpecifierNode
		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);

		// Create new SizeofExprNode with substituted type
		SizeofExprNode& new_sizeof = gChunkedAnyStorage.emplace_back<SizeofExprNode>(
			ASTNode(&new_type),
			sizeof_expr.sizeof_token());

		// Wrap in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_sizeof);
		return ASTNode(&new_expr);
	} else {
		// This is sizeof(expression) - substitute the expression
		ASTNode substituted_expr = substitute(type_or_expr);
		if (substituted_expr.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& substituted_type = substituted_expr.as<TypeSpecifierNode>();
			const int size_bits = getTypeSpecSizeBits(substituted_type);
			if (size_bits > 0) {
				StringBuilder size_builder;
				std::string_view size_str = size_builder.append(static_cast<size_t>(size_bits / 8)).commit();
				Token literal_token(
					Token::Type::Literal,
					size_str,
					sizeof_expr.sizeof_token().line(),
					sizeof_expr.sizeof_token().column(),
					sizeof_expr.sizeof_token().file_index());
				return ASTNode::emplace_node<ExpressionNode>(NumericLiteralNode(
					literal_token,
					static_cast<unsigned long long>(size_bits / 8),
					TypeCategory::UnsignedLongLong,
					TypeQualifier::None,
					64));
			}
		}
		if (type_or_expr.is<ExpressionNode>()) {
			const ExpressionNode& original_expr = type_or_expr.as<ExpressionNode>();
			std::string_view dependent_name;
			if (const auto* ident = std::get_if<IdentifierNode>(&original_expr)) {
				dependent_name = ident->name();
			} else if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&original_expr)) {
				dependent_name = tparam_ref->param_name().view();
			}
			if (!dependent_name.empty()) {
				auto it = param_map_.find(dependent_name);
				if (it != param_map_.end() && it->second.isTypeArgument()) {
					TypeSpecifierNode bound_type = makeTypeSpecifierFromTemplateTypeArg(
						it->second,
						sizeof_expr.sizeof_token());
					int size_bits = getTypeSpecSizeBits(bound_type);
					if (size_bits > 0) {
						StringBuilder size_builder;
						std::string_view size_str = size_builder.append(static_cast<size_t>(size_bits / 8)).commit();
						Token literal_token(
							Token::Type::Literal,
							size_str,
							sizeof_expr.sizeof_token().line(),
							sizeof_expr.sizeof_token().column(),
							sizeof_expr.sizeof_token().file_index());
						return ASTNode::emplace_node<ExpressionNode>(NumericLiteralNode(
							literal_token,
							static_cast<unsigned long long>(size_bits / 8),
							TypeCategory::UnsignedLongLong,
							TypeQualifier::None,
							64));
					}
					return ASTNode::emplace_node<ExpressionNode>(
						SizeofExprNode(ASTNode::emplace_node<TypeSpecifierNode>(bound_type), sizeof_expr.sizeof_token()));
				}
			}
		}

		// Create new SizeofExprNode with substituted expression
		SizeofExprNode new_sizeof = SizeofExprNode::from_expression(substituted_expr, sizeof_expr.sizeof_token());
		SizeofExprNode& new_sizeof_ref = gChunkedAnyStorage.emplace_back<SizeofExprNode>(new_sizeof);

		// Wrap in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_sizeof_ref);
		return ASTNode(&new_expr);
	}
}

ASTNode ExpressionSubstitutor::substituteTypeTraitExpr(const TypeTraitExprNode& trait_expr) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing type trait expression");

	auto substitute_trait_type = [this](const ASTNode& type_node) {
		if (!type_node.is<TypeSpecifierNode>()) {
			return type_node;
		}

		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			substituteInType(type_node.as<TypeSpecifierNode>()));
		return ASTNode(&new_type);
	};

	if (trait_expr.is_no_arg_trait()) {
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
			TypeTraitExprNode(trait_expr.kind(), trait_expr.trait_token()));
		return ASTNode(&new_expr);
	}

	ASTNode substituted_type = substitute_trait_type(trait_expr.type_node());

	if (trait_expr.has_second_type()) {
		ASTNode substituted_second_type = substitute_trait_type(trait_expr.second_type_node());

		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
			TypeTraitExprNode(trait_expr.kind(), substituted_type, substituted_second_type, trait_expr.trait_token()));
		return ASTNode(&new_expr);
	}

	if (trait_expr.is_variadic_trait()) {
		std::vector<ASTNode> substituted_additional_types;
		substituted_additional_types.reserve(trait_expr.additional_type_nodes().size());
		for (const ASTNode& additional_type : trait_expr.additional_type_nodes()) {
			substituted_additional_types.push_back(substitute_trait_type(additional_type));
		}

		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
			TypeTraitExprNode(trait_expr.kind(), substituted_type, std::move(substituted_additional_types), trait_expr.trait_token()));
		return ASTNode(&new_expr);
	}

	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(
		TypeTraitExprNode(trait_expr.kind(), substituted_type, trait_expr.trait_token()));
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteStaticCast(const StaticCastNode& cast_node) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing static_cast/functional cast expression");

	// Recursively substitute in the inner expression
	ASTNode substituted_expr = substitute(cast_node.expr());

	// The target type typically doesn't need substitution for built-in functional casts
	// like bool(x), int(x), but handle it for completeness
	TypeSpecifierNode substituted_type = substituteInType(cast_node.target_type());

	// Create new StaticCastNode with substituted expression
	StaticCastNode& new_cast = gChunkedAnyStorage.emplace_back<StaticCastNode>(
		substituted_type, substituted_expr, cast_node.cast_token());

	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_cast);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteLiteral(const ASTNode& literal) {
	// Literals don't need substitution
	return literal;
}

TypeSpecifierNode ExpressionSubstitutor::substituteInType(const TypeSpecifierNode& type) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Substituting in type");
	FLASH_LOG_FORMAT(Templates, Debug, "  Input type: base_type={}, type_index={}", (int)type.type(), type.type_index());

	if (type.type_index().is_valid()) {
		const TypeInfo* type_info = tryGetTypeInfo(type.type_index());
		if (type_info && type_info->isTypeAlias()) {
			ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
				type_info->registeredTypeIndex().withCategory(type_info->typeEnum()));
			if (resolved_alias.type_index.is_valid() && resolved_alias.type_index != type.type_index()) {
				TypeSpecifierNode substituted_alias = substituteInType(buildTerminalTypeFromResolvedAlias(resolved_alias, type.token()));
				applyResolvedAliasModifiers(substituted_alias, resolved_alias);
				applyOuterTypeModifiers(substituted_alias, type);
				return substituted_alias;
			}
		}
	}

	// First, check whether this type spelling names a template parameter that needs substitution.
	std::string_view token_type_name = type.token().value();
	if (token_type_name.empty()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(type.type_index())) {
			token_type_name = StringTable::getStringView(type_info->name());
		}
	}

	if (!token_type_name.empty()) {
		FLASH_LOG(Templates, Debug, "  Type candidate for template substitution: ", token_type_name);

		if (auto builtin_type = parser_.get_builtin_type_info(token_type_name); builtin_type.has_value()) {
			TypeSpecifierNode builtin_spec(
				nativeTypeIndex(builtin_type->first),
				type.size_in_bits(),
				type.token(),
				type.cv_qualifier(),
				type.reference_qualifier());
			for (const PointerLevel& pointer_level : type.pointer_levels()) {
				builtin_spec.add_pointer_level(pointer_level.cv_qualifier);
			}
			if (type.is_pack_expansion()) {
				builtin_spec.set_pack_expansion(true);
			}
			if (type.is_array()) {
				for (size_t dim : type.array_dimensions()) {
					builtin_spec.add_array_dimension(dim);
				}
			}
			if (type.has_function_signature()) {
				builtin_spec.set_function_signature(type.function_signature());
			}
			return builtin_spec;
		}

		// Look up this template parameter in our substitution map.
		auto it = param_map_.find(token_type_name);
		if (it == param_map_.end()) {
			if (std::optional<std::string_view> canonical_type_name = tryResolveCanonicalTypeName(type, token_type_name, param_map_)) {
				FLASH_LOG(Templates, Debug, "  Resolved type token via canonical type info name: ", token_type_name, " -> ", *canonical_type_name);
				token_type_name = *canonical_type_name;
				it = param_map_.find(token_type_name);
			}
		}
		if (it != param_map_.end()) {
			TemplateTypeArg subst = it->second;
			if (!subst.is_value && subst.type_index.is_valid()) {
				if (const TypeInfo* substituted_type_info = tryGetTypeInfo(subst.type_index);
					substituted_type_info != nullptr &&
					!substituted_type_info->isDependentPlaceholder()) {
					ResolvedAliasTypeInfo resolved_subst_alias = resolveAliasTypeInfo(
						subst.type_index.withCategory(substituted_type_info->typeEnum()));
					if (resolved_subst_alias.terminal_type_info != nullptr &&
						resolved_subst_alias.terminal_type_info->typeEnum() != TypeCategory::Invalid) {
						substituted_type_info = resolved_subst_alias.terminal_type_info;
					}
					if (substituted_type_info->typeEnum() != TypeCategory::Invalid) {
						subst.type_index = FlashCpp::canonicalizeTemplateIdentityTypeIndex(
							substituted_type_info->registeredTypeIndex().withCategory(
								substituted_type_info->typeEnum()));
						subst.setCategory(substituted_type_info->typeEnum());
					}
				}
			}
			FLASH_LOG(Templates, Debug, "  Substituting template parameter: ", token_type_name,
					  " -> base_type=", (int)subst.typeEnum(), ", type_index=", subst.type_index);
			if (subst.is_value) {
				throw CompileError("Template argument used in a type position did not resolve to a type");
			}

			if (subst.is_template_template_arg && subst.template_name_handle.isValid()) {
				if (const TypeInfo* original_type_info = tryGetTypeInfo(type.type_index());
					original_type_info != nullptr && original_type_info->isTemplateInstantiation()) {
					MaterializedStoredTemplateArgs substituted_args =
						materializeStoredTemplateArgs(
							*original_type_info,
							/*evaluate_dependent_member_values=*/false,
							kInitialDependentMemberTypeResolutionDepth);
					if (!substituted_args.args.empty()) {
						std::string_view concrete_template_name =
							StringTable::getStringView(subst.template_name_handle);
						Parser::AliasTemplateMaterializationResult materialized_type =
							parser_.materializeTemplateInstantiationForLookup(
								concrete_template_name,
								substituted_args.args);
						if (const TypeInfo* resolved_type_info = materialized_type.resolved_type_info) {
							TypeSpecifierNode resolved_type(
								resolved_type_info->registeredTypeIndex().withCategory(
									resolved_type_info->typeEnum()),
								resolved_type_info->sizeInBits(),
								type.token(),
								type.cv_qualifier(),
								ReferenceQualifier::None);
							applyOuterTypeModifiers(resolved_type, type);
							return resolved_type;
						}
					}
				}
			}

			TypeSpecifierNode substituted_type =
				makeTypeSpecifierFromTemplateTypeArg(subst, type.token());
			applyOuterTypeModifiers(substituted_type, type);
			return substituted_type;
		}

		// Resolve a type name that is a member of the current class template instantiation
		// (e.g. "size_type" used inside a member function of basic_string_view<CharT>).
		// C++20 [temp.dep.type]: a name that refers to a member of the current instantiation
		// is resolved by looking it up in the scope of the concrete instantiation.
		// We use resolveBaseClassMemberTypeChain, the same proper lookup path used by
		// resolveDependentMemberType and deferred base-class resolution.
		if (current_owner_type_name_.isValid()) {
			QualifiedTypeMemberAccess member_access;
			member_access.member_name = StringTable::getOrInternStringHandle(token_type_name);
			InlineVector<QualifiedTypeMemberAccess, 4> member_chain;
			member_chain.push_back(std::move(member_access));
			std::string_view owner_name = StringTable::getStringView(current_owner_type_name_);
			const TypeInfo* resolved_member =
				parser_.resolveBaseClassMemberTypeChain(owner_name, member_chain);
			if (resolved_member != nullptr) {
				FLASH_LOG(Templates, Debug, "  Resolved '", token_type_name,
					"' as member type of '", owner_name, "'");
				ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
					resolved_member->registeredTypeIndex().withCategory(resolved_member->typeEnum()));
				TypeSpecifierNode resolved_type = resolved_alias.type_index.is_valid()
					? buildTerminalTypeFromResolvedAlias(resolved_alias, type.token())
					: TypeSpecifierNode(
						resolved_member->registeredTypeIndex().withCategory(resolved_member->typeEnum()),
						resolved_member->sizeInBits(),
						type.token(),
						CVQualifier::None,
						ReferenceQualifier::None);
				if (resolved_alias.type_index.is_valid()) {
					applyResolvedAliasModifiers(resolved_type, resolved_alias);
				}
				applyOuterTypeModifiers(resolved_type, type);
				return resolved_type;
			}
		}

		FLASH_LOG(Templates, Warning, "  Template parameter not found in substitution map: ", token_type_name);
	}

	// Check if this is a struct/class type that might have template arguments
	if (type.category() == TypeCategory::Struct) {
		if (current_owner_type_name_.isValid()) {
			auto owner_type_it = getTypesByNameMap().find(current_owner_type_name_);
			if (owner_type_it != getTypesByNameMap().end() &&
				owner_type_it->second != nullptr &&
				owner_type_it->second->isTemplateInstantiation()) {
				StringHandle owner_base_name = owner_type_it->second->baseTemplateName();
				StringHandle type_name_handle = type.token().handle();
				if (!type_name_handle.isValid()) {
					if (const TypeInfo* type_info = tryGetTypeInfo(type.type_index())) {
						type_name_handle = type_info->name();
					}
				}

				if (owner_base_name.isValid() && type_name_handle == owner_base_name) {
					std::vector<TemplateTypeArg> current_inst_args =
						collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteInType");
					if (!current_inst_args.empty()) {
						Parser::AliasTemplateMaterializationResult materialized_type =
							parser_.materializeTemplateInstantiationForLookup(
								StringTable::getStringView(owner_base_name),
								current_inst_args);
						if (const TypeInfo* resolved_type_info = materialized_type.resolved_type_info) {
							TypeSpecifierNode substituted_current_instantiation(
								resolved_type_info->registeredTypeIndex().withCategory(
									resolved_type_info->typeEnum()),
								resolved_type_info->sizeInBits(),
								type.token(),
								CVQualifier::None,
								ReferenceQualifier::None);
							applyOuterTypeModifiers(substituted_current_instantiation, type);
							return substituted_current_instantiation;
						}
					}
				}
			}
		}

		if (const TypeInfo* type_info = tryGetTypeInfo(type.type_index())) {
			std::string_view type_name = StringTable::getStringView(type_info->name());
			FLASH_LOG(Templates, Debug, "  Type is struct: ", type_name, " type_index=", type.type_index());

			if (type_info->isTemplateInstantiation()) {
				std::string_view base_name = StringTable::getStringView(type_info->baseTemplateName());
				FLASH_LOG(Templates, Debug, "  Found template type: ", base_name);

				MaterializedStoredTemplateArgs substituted_args =
					materializeStoredTemplateArgs(*type_info, /*evaluate_dependent_member_values=*/false, kInitialDependentMemberTypeResolutionDepth);
				if (substituted_args.had_substitution && !substituted_args.args.empty()) {
					Parser::AliasTemplateMaterializationResult materialized_type =
						parser_.materializeTemplateInstantiationForLookup(base_name, substituted_args.args);
					if (const TypeInfo* resolved_type_info = materialized_type.resolved_type_info) {
						TypeIndex new_type_index = resolved_type_info->registeredTypeIndex();
						FLASH_LOG(Templates, Debug, "  Successfully materialized template: ", base_name, " with type_index=", new_type_index);
						return TypeSpecifierNode(new_type_index.withCategory(resolved_type_info->typeEnum()), 64, Token{}, type.cv_qualifier(), ReferenceQualifier::None);
					}
					FLASH_LOG(Templates, Warning, "  Failed to materialize template: ", base_name);
				}
			}
		}
	}

	// If no substitution needed or failed, return a copy of the original type
	return type;
}

// Helper: Check if a template argument node is a pack expansion
bool ExpressionSubstitutor::isPackExpansion(const ASTNode& arg_node, std::string_view& pack_name) {
	// Check if this is a TemplateParameterReferenceNode or IdentifierNode that refers to a pack
	if (arg_node.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
		bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
			using T = std::decay_t<decltype(inner)>;
			return std::is_same_v<T, TemplateParameterReferenceNode>;
		},
												expr_variant);

		if (is_template_param_ref) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
			pack_name = tparam_ref.param_name().view();

			// Check if this parameter is in our pack map
			if (pack_map_.find(pack_name) != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Detected pack expansion: ", pack_name);
				return true;
			}
		}

		// Also check IdentifierNode - pack parameters may be stored as plain identifiers
		// e.g., or_fn<Bn...>(0) where Bn is a variadic pack
		if (const auto* id_node = std::get_if<IdentifierNode>(&expr_variant)) {
			pack_name = id_node->name();
			if (pack_map_.find(pack_name) != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Detected pack expansion (IdentifierNode): ", pack_name);
				return true;
			}
		}
	}

	// Also check TypeSpecifierNode for pack types
	if (arg_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
		if (!type_spec.is_pack_expansion()) {
			return false;
		}

		if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
			pack_name = StringTable::getStringView(type_info->name());
		}
		if (pack_name.empty()) {
			pack_name = type_spec.token().value();
		}

		if (pack_map_.find(pack_name) != pack_map_.end()) {
			FLASH_LOG(Templates, Debug, "Detected pack expansion (TypeSpecifier): ", pack_name);
			return true;
		}
	}

	return false;
}

// Helper: Expand pack parameters in template arguments
std::vector<TemplateTypeArg> ExpressionSubstitutor::expandPacksInArguments(
	std::span<const ASTNode> template_arg_nodes) {

	std::vector<TemplateTypeArg> expanded_args;

	for (const ASTNode& arg_node : template_arg_nodes) {
		std::string_view pack_name;

		// Check if this argument is a pack expansion
		if (isPackExpansion(arg_node, pack_name)) {
			// Expand the pack
			auto pack_it = pack_map_.find(pack_name);
			if (pack_it != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Expanding pack: ", pack_name, " with ", pack_it->second.size(), " arguments");

				// Add all arguments from the pack
				for (const auto& pack_arg : pack_it->second) {
					expanded_args.push_back(pack_arg);
				}
			}
		} else {
			// Regular argument - substitute if it's a template parameter
			if (arg_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
				std::string_view type_name = "";

				if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
					type_name = StringTable::getStringView(type_info->name());
				}

				// Check if this is a scalar template parameter
				auto it = param_map_.find(type_name);
				if (it != param_map_.end()) {
					expanded_args.push_back(it->second);
				} else {
					// Use as-is
					expanded_args.push_back(TemplateTypeArg(type_spec));
				}
			} else if (arg_node.is<ExpressionNode>()) {
				const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
				bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
					using T = std::decay_t<decltype(inner)>;
					return std::is_same_v<T, TemplateParameterReferenceNode>;
				},
														expr_variant);

				if (is_template_param_ref) {
					const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
					std::string_view param_name = tparam_ref.param_name().view();

					auto it = param_map_.find(param_name);
					if (it != param_map_.end()) {
						expanded_args.push_back(it->second);
						continue;
					}
				} else if (const auto* identifier = std::get_if<IdentifierNode>(&expr_variant)) {
					auto it = param_map_.find(identifier->name());
					if (it != param_map_.end()) {
						expanded_args.push_back(it->second);
						continue;
					}
				}

				ASTNode substituted_arg_node = substitute(arg_node);
				if (substituted_arg_node.is<TypeSpecifierNode>()) {
					expanded_args.emplace_back(substituted_arg_node.as<TypeSpecifierNode>());
					continue;
				}
				if (!substituted_arg_node.is<ExpressionNode>()) {
					continue;
				}

				const ExpressionNode& substituted_expr = substituted_arg_node.as<ExpressionNode>();
				if (const auto* identifier = std::get_if<IdentifierNode>(&substituted_expr)) {
					StringHandle type_name = StringTable::getOrInternStringHandle(identifier->name());
					if (const TypeInfo* type_info = findTypeByName(type_name)) {
						expanded_args.push_back(TemplateTypeArg::makeType(type_info->type_index_));
						continue;
					}
					if (auto builtin_type = parser_.get_builtin_type_info(identifier->name())) {
						expanded_args.push_back(
							TemplateTypeArg::makeType(nativeTypeIndex(builtin_type->first)));
						continue;
					}
				}
				if (std::holds_alternative<NumericLiteralNode>(substituted_expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(substituted_expr);
					const NumericLiteralValue& raw_value = lit.value();
					int64_t value = 0;
					if (const auto* ull_val = std::get_if<unsigned long long>(&raw_value)) {
						value = static_cast<int64_t>(*ull_val);
					} else if (const auto* d_val = std::get_if<double>(&raw_value)) {
						value = static_cast<int64_t>(*d_val);
					} else {
						continue;
					}
					expanded_args.emplace_back(value, lit.type());
					continue;
				}
				if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&substituted_expr)) {
					expanded_args.emplace_back(bool_literal->value() ? 1 : 0, TypeCategory::Bool);
					continue;
				}
				if (const auto* template_param_ref = std::get_if<TemplateParameterReferenceNode>(&substituted_expr)) {
					auto it = param_map_.find(template_param_ref->param_name().view());
					if (it != param_map_.end()) {
						expanded_args.push_back(it->second);
					}
				}
			}
		}
	}

	return expanded_args;
}
