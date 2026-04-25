#include "ExpressionSubstitutor.h"
#include "CallNodeHelpers.h"
#include "Parser.h"
#include "TemplateInstantiationHelper.h"
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

std::vector<size_t> concatenateArrayDimensions(std::vector<size_t> prefix_dimensions, const std::vector<size_t>& suffix_dimensions) {
	prefix_dimensions.reserve(prefix_dimensions.size() + suffix_dimensions.size());
	prefix_dimensions.insert(prefix_dimensions.end(), suffix_dimensions.begin(), suffix_dimensions.end());
	return prefix_dimensions;
}

void appendArrayDimensions(TypeSpecifierNode& target, std::vector<size_t> prefix_dimensions) {
	if (prefix_dimensions.empty()) {
		return;
	}
	target.set_array_dimensions(concatenateArrayDimensions(std::move(prefix_dimensions), target.array_dimensions()));
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
		for (std::string_view param_name : template_param_order_) {
			auto scalar_it = param_map_.find(param_name);
			auto pack_it = pack_map_.find(StringTable::getOrInternStringHandle(param_name));
			if (scalar_it != param_map_.end() && pack_it != pack_map_.end()) {
				throw InternalError("ExpressionSubstitutor found both scalar and pack bindings for template parameter '" + std::string(param_name) + "'");
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
	bool evaluate_dependent_member_values) {
	MaterializedStoredTemplateArgs result;
	const auto& stored_args = template_instantiation_info.templateArgs();
	result.args.reserve(stored_args.size());

	for (const auto& arg : stored_args) {
		TemplateTypeArg materialized_arg = toTemplateTypeArg(arg);
		bool substituted = false;

		if (materialized_arg.dependent_name.isValid()) {
			std::string_view dependent_name = StringTable::getStringView(materialized_arg.dependent_name);
			auto dep_subst_it = param_map_.find(dependent_name);
			if (dep_subst_it != param_map_.end()) {
				materialized_arg = rebindDependentTemplateTypeArg(dep_subst_it->second, materialized_arg);
				result.had_substitution = true;
				substituted = true;
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
									materialized_arg.is_value = true;
									materialized_arg.value = value->value;
									materialized_arg.type_index = nativeTypeIndex(value->type);
									materialized_arg.dependent_name = {};
									materialized_arg.is_dependent = false;
									result.had_substitution = true;
									substituted = true;
								}
							}
						}
					}
				}
			}
		}

		if (!substituted && !arg.is_value && is_struct_type(arg.category())) {
			if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg.type_index)) {
				if (const TypeInfo* recursively_resolved_type = resolveDependentMemberType(
						*arg_type_info,
						kInitialDependentMemberTypeResolutionDepth)) {
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
				}
			}
		}

		result.args.push_back(materialized_arg);
	}

	return result;
}

const TypeInfo* ExpressionSubstitutor::resolveDependentMemberType(const TypeInfo& type_info, int depth) {
	if (!type_info.isDependentMemberType() || depth >= kMaxDependentMemberTypeResolutionDepth) {
		return nullptr;
	}

	std::string_view type_name = StringTable::getStringView(type_info.name());
	size_t member_sep = type_name.rfind("::");
	if (member_sep == std::string_view::npos) {
		return nullptr;
	}

	std::string_view dependent_base_name = type_name.substr(0, member_sep);
	constexpr size_t kScopeResolutionLength = 2;
	std::string_view dependent_member_name = type_name.substr(member_sep + kScopeResolutionLength);
	std::string_view base_template_name = extractBaseTemplateName(dependent_base_name);
	if (base_template_name.empty()) {
		return nullptr;
	}

	auto dependent_base_it = getTypesByNameMap().find(
		StringTable::getOrInternStringHandle(dependent_base_name));
	if (dependent_base_it == getTypesByNameMap().end() ||
		dependent_base_it->second == nullptr ||
		!dependent_base_it->second->isTemplateInstantiation()) {
		return nullptr;
	}

	MaterializedStoredTemplateArgs concrete_base_args =
		materializeStoredTemplateArgs(
			*dependent_base_it->second,
			/*evaluate_dependent_member_values=*/true);
	Parser::AliasTemplateMaterializationResult materialized_base =
		parser_.materializeTemplateInstantiationForLookup(
			base_template_name,
			concrete_base_args.args);
	if (materialized_base.instantiated_name.empty()) {
		return nullptr;
	}

	QualifiedTypeMemberAccess member_access;
	member_access.member_name = StringTable::getOrInternStringHandle(dependent_member_name);
	std::vector<QualifiedTypeMemberAccess> member_chain;
	member_chain.push_back(std::move(member_access));
	const TypeInfo* resolved_type =
		parser_.resolveBaseClassMemberTypeChain(
			materialized_base.instantiated_name,
			member_chain);
	if (resolved_type == nullptr) {
		return nullptr;
	}

	const TypeInfo* next_dependent_type = nullptr;
	if (const TypeInfo* alias_target = tryGetTypeInfo(resolved_type->type_index_);
		alias_target != nullptr && alias_target != resolved_type && alias_target->isDependentMemberType()) {
		next_dependent_type = alias_target;
	} else if (resolved_type->isDependentMemberType()) {
		next_dependent_type = resolved_type;
	}
	if (next_dependent_type != nullptr) {
		return resolveDependentMemberType(*next_dependent_type, depth + 1);
	}

	return resolved_type;
}

ASTNode ExpressionSubstitutor::substituteFunctionCallImpl(const CallExprNode& call) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing function call");
	FLASH_LOG(Templates, Debug, "  has_mangled_name: ", call.has_mangled_name());
	FLASH_LOG(Templates, Debug, "  has_template_arguments: ", call.has_template_arguments());

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
		parser_.normalizePendingSemanticRootsIfAvailable();
	};
	std::vector<ASTNode> explicit_template_arg_nodes;

	// Check if this function call has explicit template arguments (e.g., base_trait<T>())
	if (call.has_template_arguments()) {
		explicit_template_arg_nodes = call.template_arguments();
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
								FLASH_LOG(Templates, Warning, "    Template parameter not found in substitution map: ", param_name);
							// Return as-is if we can't substitute
								return wrapOriginalCall();
							}
						}
					} else {
					// Non-template type argument - use it directly
						FLASH_LOG(Templates, Debug, "    Template argument is concrete type, using directly");
						TemplateTypeArg arg(type_spec);
						substituted_template_args.push_back(arg);
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
						if (!substituted_arg_node.is<ExpressionNode>()) {
							FLASH_LOG(Templates, Debug, "    Substituted template argument is not an ExpressionNode");
							failed_value_extraction = true;
							break;
						}
						const ExpressionNode& substituted_expr = substituted_arg_node.as<ExpressionNode>();
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
			bool have_complete_substituted_arg_types = true;
			substituted_arg_types.reserve(call.arguments().size());
			for (size_t i = 0; i < call.arguments().size(); ++i) {
				ASTNode substituted_arg = substitute(call.arguments()[i]);
				substituted_args_nodes.push_back(substituted_arg);
				size_t previous_count = substituted_arg_types.size();
				parser_.appendFunctionCallArgType(substituted_arg, &substituted_arg_types);
				if (substituted_arg_types.size() == previous_count ||
					substituted_arg_types.back().category() == TypeCategory::Invalid) {
					have_complete_substituted_arg_types = false;
				}
			}

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
			auto hasResolvedTypeOwner = [&](StringHandle owner_name_handle) {
				const auto& types_by_name = getTypesByNameMap();
				return types_by_name.find(owner_name_handle) != types_by_name.end();
			};

			// First try function template instantiation to obtain accurate return type
			std::optional<ASTNode> instantiated_template = std::nullopt;
			if (!qualified_name.empty()) {
				if (size_t scope_pos = qualified_name.rfind("::"); scope_pos != std::string_view::npos) {
					std::string_view owner_name = qualified_name.substr(0, scope_pos);
					std::string_view member_name = qualified_name.substr(scope_pos + 2);
					std::vector<TemplateTypeArg> current_inst_args =
						collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteFunctionCallImpl");
					if (!current_inst_args.empty() && extractBaseTemplateName(owner_name).empty()) {
						auto owner_template = gTemplateRegistry.lookupTemplate(owner_name);
						if (owner_template.has_value()) {
							// Only class-template owners can be materialized here. Namespace-qualified
							// calls like std::__atomic_impl::fn should skip this path entirely.
							Parser::AliasTemplateMaterializationResult materialized_owner =
								parser_.materializeTemplateInstantiationForLookup(owner_name, current_inst_args);
							if (!materialized_owner.instantiated_name.empty()) {
								owner_name = materialized_owner.instantiated_name;
							}
						}
					}
					StringHandle owner_name_handle = StringTable::getOrInternStringHandle(owner_name);
					if (hasResolvedTypeOwner(owner_name_handle)) {
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
				if (!instantiated_template.has_value()) {
					instantiated_template = tryInstantiateExplicitFunctionTemplate(qualified_name);
				}
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
		std::vector<TypeSpecifierNode> substituted_arg_types;
		bool have_complete_arg_types = true;
		substituted_arg_types.reserve(call.arguments().size());
		for (size_t i = 0; i < call.arguments().size(); ++i) {
			ASTNode substituted_arg = substitute(call.arguments()[i]);
			substituted_args.push_back(substituted_arg);
			size_t previous_count = substituted_arg_types.size();
			parser_.appendFunctionCallArgType(substituted_arg, &substituted_arg_types);
			if (substituted_arg_types.size() == previous_count ||
				substituted_arg_types.back().category() == TypeCategory::Invalid) {
				have_complete_arg_types = false;
			}
		}

		// Prefer the parser's main function-template instantiation path, which can
		// deduce from ordinary argument types (e.g. foo(args...)->foo(int,int)) after
		// pack expansion. The older constructor-wrapper helper remains as fallback for
		// specialized patterns such as __type_identity<T>{}.
		std::optional<ASTNode> instantiated_opt;
		if (have_complete_arg_types) {
			if (call.has_qualified_name()) {
				instantiated_opt = parser_.try_instantiate_template(call.qualified_name(), substituted_arg_types);
			}
			if (!instantiated_opt.has_value()) {
				instantiated_opt = parser_.try_instantiate_template(template_func_name, substituted_arg_types);
			}
			if (instantiated_opt.has_value()) {
				normalizePendingSemanticRoots();
			}
		}

		if (!instantiated_opt.has_value()) {
			std::vector<TemplateTypeArg> deduced_template_args =
				TemplateInstantiationHelper::deduceTemplateArgsFromCall(substituted_args);
			if (!deduced_template_args.empty()) {
				instantiated_opt = TemplateInstantiationHelper::tryInstantiateTemplateFunction(
					parser_, template_func_name, template_func_name, deduced_template_args);
				if (instantiated_opt.has_value()) {
					normalizePendingSemanticRoots();
				}
			}
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

	// If not a template function call or instantiation failed, check for dependent qualified names
	// Pattern: Base$dependentHash::member(args) — needs re-instantiation with concrete types
	// This happens when a template body is re-parsed: Base<T>::member() creates Base$hash1::member
	// during initial template parsing, but the actual instantiation is Base$hash2.
	size_t scope_pos = func_name.find("::");
	std::string_view base_template_name;
	if (scope_pos != std::string_view::npos) {
		base_template_name = extractBaseTemplateName(func_name.substr(0, scope_pos));
	}
	if (!base_template_name.empty() && scope_pos != std::string_view::npos) {
		std::string_view member_name = func_name.substr(scope_pos + 2);

		// Collect concrete template arguments from param_map_
		std::vector<TemplateTypeArg> inst_args =
			collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteFunctionCallImpl");

		if (!inst_args.empty()) {
			Parser::AliasTemplateMaterializationResult materialized_base =
				parser_.materializeTemplateInstantiationForLookup(base_template_name, inst_args);
			std::string_view instantiated_name = materialized_base.instantiated_name;

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

			// Create new forward declaration with corrected name
			auto& fwd_type_node = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(TypeCategory::Int, TypeQualifier::None, 32, Token(), CVQualifier::None);
			ASTNode fwd_type_ast(&fwd_type_node);
			auto& new_decl = gChunkedAnyStorage.emplace_back<DeclarationNode>(fwd_type_ast, new_func_token);

			// Try to trigger lazy member function instantiation for the target
			StringHandle inst_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
			StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
			auto& lazy_registry = LazyMemberInstantiationRegistry::getInstance();
			LazyMemberKey member_key = LazyMemberKey::anyConst(inst_name_handle, member_handle);
			if (lazy_registry.needsInstantiation(member_key)) {
				auto lazy_info = lazy_registry.getLazyMemberInfo(member_key);
				if (lazy_info.has_value()) {
					parser_.instantiateLazyMemberFunction(*lazy_info);
					normalizePendingSemanticRoots();
					lazy_registry.markInstantiated(inst_name_handle, member_handle, lazy_info->identity.is_const_method);
				}
			}

			const DeclarationNode* target_decl = &new_decl;
			const FunctionDeclarationNode* target_func = nullptr;
			auto type_it = getTypesByNameMap().find(inst_name_handle);
			if (type_it != getTypesByNameMap().end()) {
				if (const StructTypeInfo* struct_info = type_it->second->getStructInfo()) {
					for (const auto& member_func : struct_info->member_functions) {
						if (member_func.getName() == member_handle &&
							member_func.function_decl.is<FunctionDeclarationNode>()) {
							target_func = &member_func.function_decl.as<FunctionDeclarationNode>();
							target_decl = &target_func->decl_node();
							break;
						}
					}
				}
			}

			ExpressionNode& new_expr = emplaceDirectCallExpr(
				*target_decl,
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

	// Return as-is
	FLASH_LOG(Templates, Debug, "  Returning function call as-is");
	return wrapOriginalCall();
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

	// Qualified identifiers like R1_T::num need template parameter substitution in the namespace part
	// The namespace is stored as a mangled template name like "R1_T" (template R1 with parameter T)
	// We need to substitute T with its concrete type to get "R1_long"

	// Get the namespace name (e.g., "R1_T")
	std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());

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

			// The concrete type should be a struct type - get its instantiated name
			if (const TypeInfo* type_info = (is_struct_type(concrete_type.category()))
												? tryGetTypeInfo(concrete_type.type_index)
												: nullptr) {
				StringHandle type_name_handle = type_info->name();

				if (type_info->isTemplateInstantiation()) {
					std::string_view base_name = StringTable::getStringView(type_info->baseTemplateName());
					FLASH_LOG(Templates, Debug, "  Concrete type is template instantiation: base='", base_name, "'");
					if (!base_name.empty()) {
						std::vector<TemplateTypeArg> concrete_inst_args;
						for (const auto& stored_arg : type_info->templateArgs()) {
							concrete_inst_args.push_back(toTemplateTypeArg(stored_arg));
						}
						Parser::AliasTemplateMaterializationResult materialized_type =
							parser_.materializeTemplateInstantiationForLookup(base_name, concrete_inst_args);
						if (!materialized_type.instantiated_name.empty()) {
							type_name_handle = StringTable::getOrInternStringHandle(
								materialized_type.instantiated_name);
						}
					}
				}

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
		StringHandle qualified_alias_handle = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(current_owner_type_name_)
				.append("::")
				.append(ns_name)
				.commit());
		auto alias_it = getTypesByNameMap().find(qualified_alias_handle);
		if (alias_it != getTypesByNameMap().end() && alias_it->second != nullptr) {
			const TypeInfo* alias_info = alias_it->second;
			ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
				alias_info->registeredTypeIndex().withCategory(alias_info->typeEnum()));
			const TypeInfo* resolved_type_info = resolved_alias.terminal_type_info;
			if (!resolved_type_info && resolved_alias.type_index.is_valid()) {
				resolved_type_info = tryGetTypeInfo(resolved_alias.type_index);
			}
			if (resolved_type_info != nullptr) {
				StringHandle resolved_name_handle = resolved_type_info->name();
				std::string_view materialization_target_name =
					StringTable::getStringView(resolved_name_handle);
				if (const TypeInfo* direct_alias_target_info =
						tryGetTypeInfo(alias_info->type_index_)) {
					// Phase 4: use explicit placeholder flag instead of string heuristic
					if (direct_alias_target_info->isDependentMemberType()) {
						materialization_target_name =
							StringTable::getStringView(direct_alias_target_info->name());
					}
				}
				size_t member_sep = materialization_target_name.rfind("::");
				if (member_sep != std::string_view::npos) {
					std::string_view dependent_base_name =
						materialization_target_name.substr(0, member_sep);
					std::string_view dependent_member_name =
						materialization_target_name.substr(member_sep + 2);
					std::string_view base_template_name = extractBaseTemplateName(dependent_base_name);
					if (!base_template_name.empty()) {
						std::vector<TemplateTypeArg> current_inst_args =
							collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteQualifiedIdentifier");
						StringHandle dependent_base_handle =
							StringTable::getOrInternStringHandle(dependent_base_name);
						auto dependent_base_it = getTypesByNameMap().find(dependent_base_handle);
						if (dependent_base_it != getTypesByNameMap().end() &&
							dependent_base_it->second != nullptr &&
							dependent_base_it->second->isTemplateInstantiation()) {
							MaterializedStoredTemplateArgs concrete_base_args =
								materializeStoredTemplateArgs(
									*dependent_base_it->second,
									/*evaluate_dependent_member_values=*/true);
							if (!concrete_base_args.args.empty()) {
								current_inst_args = std::move(concrete_base_args.args);
							}
						}
						if (!current_inst_args.empty()) {
							Parser::AliasTemplateMaterializationResult materialized_alias_base =
								parser_.materializeTemplateInstantiationForLookup(
									base_template_name,
									current_inst_args);
							if (!materialized_alias_base.instantiated_name.empty()) {
								StringHandle concrete_member_handle = StringTable::getOrInternStringHandle(
									StringBuilder()
										.append(materialized_alias_base.instantiated_name)
										.append("::")
										.append(dependent_member_name)
										.commit());
								auto concrete_member_it = getTypesByNameMap().find(concrete_member_handle);
								if (concrete_member_it != getTypesByNameMap().end() &&
									concrete_member_it->second != nullptr) {
									resolved_type_info = concrete_member_it->second;
									resolved_name_handle = concrete_member_it->second->name();
								} else if (StringTable::getStringView(resolved_name_handle) ==
										   dependent_member_name) {
									TypeIndex resolved_member_index =
										resolved_type_info->registeredTypeIndex().withCategory(
											resolved_type_info->typeEnum());
									TypeSpecifierNode concrete_member_spec(
										resolved_member_index,
										resolved_type_info->sizeInBits(),
										Token(),
										CVQualifier::None,
										ReferenceQualifier::None);
									if (const TypeSpecifierNode* existing_alias_spec =
											resolved_type_info->aliasTypeSpecifier()) {
										concrete_member_spec = *existing_alias_spec;
									}
									TypeInfo& concrete_member_info = add_type_alias_copy(
										concrete_member_handle,
										resolved_member_index,
										resolved_type_info->sizeInBits().value,
										concrete_member_spec);
									getTypesByNameMap().insert_or_assign(
										concrete_member_handle,
										&concrete_member_info);
									resolved_type_info = &concrete_member_info;
									resolved_name_handle = concrete_member_info.name();
								}
							}
						}
					}
				}
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

	auto ns_name_handle = StringTable::getOrInternStringHandle(ns_name);
	auto type_it = getTypesByNameMap().find(ns_name_handle);
	if (type_it != getTypesByNameMap().end() && type_it->second->isTemplateInstantiation()) {
		MaterializedStoredTemplateArgs materialized_args =
			materializeStoredTemplateArgs(*type_it->second, /*evaluate_dependent_member_values=*/true);
		inst_args = std::move(materialized_args.args);
	}

	// Fallback: if TypeInfo lookup found no stored args (e.g., the placeholder wasn't registered
	// in getTypesByNameMap(), or it's a non-template namespace), fall back to using the full param_map_.
	// This handles cases like pack expansion bases where no TypeInfo exists.
	if (inst_args.empty()) {
		inst_args = collectCurrentBoundTemplateArgs("ExpressionSubstitutor::substituteQualifiedIdentifier");
	}

	if (!inst_args.empty()) {
		FLASH_LOG(Templates, Debug, "  Triggering instantiation of template '", base_template_name,
				  "' with ", inst_args.size(), " arguments");
		Parser::AliasTemplateMaterializationResult materialized_namespace =
			parser_.materializeTemplateInstantiationForLookup(base_template_name, inst_args);
		std::string_view instantiated_name = materialized_namespace.instantiated_name;

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

		// Create new SizeofExprNode with substituted expression
		SizeofExprNode new_sizeof = SizeofExprNode::from_expression(substituted_expr, sizeof_expr.sizeof_token());
		SizeofExprNode& new_sizeof_ref = gChunkedAnyStorage.emplace_back<SizeofExprNode>(new_sizeof);

		// Wrap in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_sizeof_ref);
		return ASTNode(&new_expr);
	}
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

	// First, check if this is a template parameter type that needs substitution
	// Template parameters can show up as Type::Template, Type::Auto, or Type::UserDefined
	if (type.category() == TypeCategory::Template || isPlaceholderAutoType(type.category()) || type.category() == TypeCategory::UserDefined || type.category() == TypeCategory::TypeAlias) {
		std::string_view type_name = type.token().value();
		if (type_name.empty()) {
			if (const TypeInfo* type_info = tryGetTypeInfo(type.type_index())) {
				type_name = StringTable::getStringView(type_info->name());
			}
		}

		FLASH_LOG(Templates, Debug, "  Type is template parameter: ", type_name);

		// Look up this template parameter in our substitution map
		auto it = param_map_.find(type_name);
		if (it == param_map_.end()) {
			if (std::optional<std::string_view> canonical_type_name = tryResolveCanonicalTypeName(type, type_name, param_map_)) {
				FLASH_LOG(Templates, Debug, "  Resolved type token via canonical type info name: ", type_name, " -> ", *canonical_type_name);
				type_name = *canonical_type_name;
				it = param_map_.find(type_name);
			}
		}
		if (it != param_map_.end()) {
			const TemplateTypeArg& subst = it->second;
			FLASH_LOG(Templates, Debug, "  Substituting template parameter: ", type_name,
					  " -> base_type=", (int)subst.typeEnum(), ", type_index=", subst.type_index);

			// Create a TypeSpecifierNode from the substitution
			// Determine the correct size_in_bits based on the type
			int size_in_bits = get_type_size_bits(subst.category());
			if (size_in_bits == 0) {
				switch (subst.category()) {
				case TypeCategory::Struct:
				case TypeCategory::UserDefined:
				case TypeCategory::TypeAlias:
						// For struct types, we need to look up the size from TypeInfo
					if (const TypeInfo* ti = tryGetTypeInfo(subst.type_index)) {
						if (ti->isStruct()) {
							const StructTypeInfo* si = ti->getStructInfo();
							if (si) {
								size_in_bits = si->sizeInBits().value;
							}
						}
					}
					break;
				default:
					size_in_bits = 64; // Default to 64 bits if unknown
					break;
				}
			}

			TypeSpecifierNode substituted_type(
				subst.type_index.withCategory(subst.typeEnum()),
				size_in_bits,
				Token{},
				subst.cv_qualifier,
				subst.reference_qualifier());

			substituted_type.add_pointer_levels(subst.pointer_depth);

			return substituted_type;
		}

		if (!type_name.empty()) {
			FLASH_LOG(Templates, Warning, "  Template parameter not found in substitution map: ", type_name);
		}
	}

	// Check if this is a struct/class type that might have template arguments
	if (type.category() == TypeCategory::Struct) {
		if (current_owner_type_name_.isValid()) {
			auto owner_type_it = getTypesByNameMap().find(current_owner_type_name_);
			if (owner_type_it != getTypesByNameMap().end() &&
				owner_type_it->second != nullptr &&
				owner_type_it->second->isTemplateInstantiation()) {
				StringHandle owner_base_name = owner_type_it->second->baseTemplateName();
				StringHandle type_name = type.token().handle();
				if (!type_name.isValid()) {
					if (const TypeInfo* type_info = tryGetTypeInfo(type.type_index())) {
						type_name = type_info->name();
					}
				}

				if (owner_base_name.isValid() && type_name == owner_base_name) {
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
					materializeStoredTemplateArgs(*type_info, /*evaluate_dependent_member_values=*/false);
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
	const std::vector<ASTNode>& template_arg_nodes) {

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
					}
				}
			}
		}
	}

	return expanded_args;
}
