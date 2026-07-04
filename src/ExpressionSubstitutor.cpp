#include "ExpressionSubstitutor.h"
#include "CallNodeHelpers.h"
#include "MemberFunctionLookupShared.h"
#include "Parser.h"
#include "TemplateInstantiationHelper.h"
#include "TemplateArgumentMaterialization.h"
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
		computeTemplateArgumentTypeSizeBits(resolved_alias.type_index),
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
	captureParserPackState();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	Parser& parser,
	std::span<const std::string_view> template_param_order)
	: param_map_(param_map), parser_(parser), template_param_order_(template_param_order.begin(), template_param_order.end()) {
	rebuildEnvironmentFromCurrentBindings();
	captureParserPackState();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
	Parser& parser)
	: param_map_(param_map), pack_map_(pack_map), parser_(parser) {
	rebuildEnvironmentFromCurrentBindings();
	captureParserPackState();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const std::unordered_map<std::string_view, TemplateTypeArg>& param_map,
	const std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>& pack_map,
	Parser& parser,
	std::span<const std::string_view> template_param_order)
	: param_map_(param_map), pack_map_(pack_map), parser_(parser), template_param_order_(template_param_order.begin(), template_param_order.end()) {
	rebuildEnvironmentFromCurrentBindings();
	captureParserPackState();
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
	captureParserPackState();
}

ExpressionSubstitutor::ExpressionSubstitutor(
	const TemplateInstantiationContext& context,
	Parser& parser)
	: ExpressionSubstitutor(context.environment, parser) {
}

void ExpressionSubstitutor::captureParserPackState() {
	captured_pack_param_info_ = parser_.pack_param_info_;
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
	} else if (expr.is<TypeSpecifierNode>()) {
		TypeSpecifierNode substituted_type =
			substituteInType(expr.as<TypeSpecifierNode>());
		TypeSpecifierNode& stored_type =
			gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
				std::move(substituted_type));
		return ASTNode(&stored_type);
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
	ChunkedVector<ASTNode> substituted_args =
		substituteCallArgumentsPreservingPackExpansion(ctor.arguments());

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

void ExpressionSubstitutor::substituteCallArgumentPreservingPackExpansion(
	const ASTNode& arg,
	ChunkedVector<ASTNode>& out) {
	if (arg.is<ExpressionNode>()) {
		const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
		if (const auto* pack_expansion_expr = std::get_if<PackExpansionExprNode>(&arg_expr)) {
			InlineVector<TemplateParameterNode, 4> template_params;
			InlineVector<TemplateTypeArg, 4> template_args;
			template_params.reserve(environment_.bindings.size());
			for (const TemplateBinding& binding : environment_.bindings) {
				if (!binding.name.isValid()) {
					continue;
				}
				Token param_token(
					Token::Type::Identifier,
					StringTable::getStringView(binding.name),
					0,
					0,
					0);
				TemplateParameterNode template_param;
				switch (binding.kind) {
				case TemplateParameterKind::Type:
					template_param = TemplateParameterNode(binding.name, param_token);
					break;
				case TemplateParameterKind::NonType: {
					TypeSpecifierNode parameter_type;
					if (!binding.args.empty()) {
						const TemplateTypeArg& bound_arg = binding.args.front();
						if (bound_arg.type_index.is_valid()) {
							parameter_type = TypeSpecifierNode(
								bound_arg.type_index,
								computeTemplateArgumentTypeSizeBits(bound_arg.type_index),
								param_token,
								CVQualifier::None,
								ReferenceQualifier::None);
						} else if (bound_arg.typeEnum() != TypeCategory::Invalid) {
							parameter_type = TypeSpecifierNode(
								bound_arg.typeEnum(),
								TypeQualifier::None,
								get_type_size_bits(bound_arg.typeEnum()),
								param_token,
								CVQualifier::None);
						}
					}
					template_param = TemplateParameterNode(binding.name, parameter_type, param_token);
					break;
				}
				case TemplateParameterKind::Template:
					template_param = TemplateParameterNode(
						binding.name,
						std::span<const TemplateParameterNode>{},
						param_token);
					break;
				default:
					throw InternalError("Unsupported template parameter kind while preserving pack expansion during call substitution");
				}
				template_param.set_variadic(binding.is_pack);
				template_params.push_back(std::move(template_param));
				if (binding.is_pack) {
					for (const TemplateTypeArg& pack_arg : binding.args) {
						template_args.push_back(pack_arg);
					}
				} else if (!binding.args.empty()) {
					template_args.push_back(binding.args.front());
				}
			}

			if (!template_params.empty() &&
				parser_.expandPackExpansionArgs(
					*pack_expansion_expr,
					std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
					std::span<const TemplateTypeArg>(template_args.data(), template_args.size()),
					std::span<const Parser::PackParamInfo>(
						captured_pack_param_info_.data(),
						captured_pack_param_info_.size()),
					out)) {
				return;
			}

			InlineVector<StringHandle, 4> matched_pack_names;
			AstTraversal::visitAST(pack_expansion_expr->pattern(), [&](const ASTNode& node) {
				StringHandle candidate_name;
				if (node.is<TemplateParameterReferenceNode>()) {
					candidate_name = node.as<TemplateParameterReferenceNode>().param_name();
				} else if (node.is<IdentifierNode>()) {
					candidate_name = node.as<IdentifierNode>().getOrInternNameHandle();
				} else if (node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();
					if (!type_spec.token().value().empty()) {
						candidate_name = StringTable::getOrInternStringHandle(
							type_spec.token().value());
					} else if (type_spec.type_index().is_valid()) {
						if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
							candidate_name = type_info->name();
						}
					}
				}
				if (!candidate_name.isValid() ||
					pack_map_.find(candidate_name) == pack_map_.end()) {
					return;
				}
				for (StringHandle matched_name : matched_pack_names) {
					if (matched_name == candidate_name) {
						return;
					}
				}
				matched_pack_names.push_back(candidate_name);
			});

			if (!matched_pack_names.empty()) {
				size_t pack_size = pack_map_.find(matched_pack_names[0])->second.size();
				for (size_t pack_index = 1; pack_index < matched_pack_names.size(); ++pack_index) {
					size_t current_pack_size =
						pack_map_.find(matched_pack_names[pack_index])->second.size();
					if (current_pack_size != pack_size) {
						throw InternalError(
							"Mismatched pack sizes while preserving pack expansion during call substitution");
					}
				}
				if (pack_size == 0) {
					return;
				}

				InlineVector<std::string_view, 8> ordered_param_names;
				if (!template_param_order_.empty()) {
					for (std::string_view param_name : template_param_order_) {
						ordered_param_names.push_back(param_name);
					}
				} else {
					for (const TemplateBinding& binding : environment_.bindings) {
						if (!binding.name.isValid()) {
							continue;
						}
						ordered_param_names.push_back(
							StringTable::getStringView(binding.name));
					}
				}

				std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>> remaining_pack_bindings =
					pack_map_;
				for (StringHandle matched_pack_name : matched_pack_names) {
					remaining_pack_bindings.erase(matched_pack_name);
				}

				for (size_t expansion_index = 0; expansion_index < pack_size; ++expansion_index) {
					std::unordered_map<std::string_view, TemplateTypeArg> scalar_bindings =
						param_map_;
					for (StringHandle matched_pack_name : matched_pack_names) {
						auto pack_it = pack_map_.find(matched_pack_name);
						if (pack_it == pack_map_.end() ||
							expansion_index >= pack_it->second.size()) {
							throw InternalError(
								"Missing pack element while preserving pack expansion during call substitution");
						}
						scalar_bindings[StringTable::getStringView(matched_pack_name)] =
							pack_it->second[expansion_index];
					}

					ExpressionSubstitutor element_substitutor(
						scalar_bindings,
						remaining_pack_bindings,
						parser_,
						std::span<const std::string_view>(
							ordered_param_names.data(),
							ordered_param_names.size()));
					element_substitutor.setCurrentOwnerTypeName(current_owner_type_name_);
					out.push_back(element_substitutor.substitute(
						pack_expansion_expr->pattern()));
				}
				return;
			}
		}
	}

	out.push_back(substitute(arg));
}

ChunkedVector<ASTNode> ExpressionSubstitutor::substituteCallArgumentsPreservingPackExpansion(
	const ChunkedVector<ASTNode>& args) {
	ChunkedVector<ASTNode> substituted_args;
	for (const ASTNode& arg : args) {
		substituteCallArgumentPreservingPackExpansion(arg, substituted_args);
	}
	return substituted_args;
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

Parser::AliasTemplateMaterializationResult
ExpressionSubstitutor::materializeDependentQualifiedRecordOwner(
	const TypeInfo::DependentQualifiedNameRecord& dependent_name,
	int depth,
	bool prefer_current_owner_type_name,
	bool allow_current_context_dependent_owner_materialization) {
	Parser::AliasTemplateMaterializationResult materialized_owner;
	std::string_view owner_name =
		StringTable::getStringView(dependent_name.owner_name);
	auto assign_owner_type =
		[&](const TypeInfo* owner_type_info) {
		if (owner_type_info == nullptr) {
			return;
		}
		materialized_owner.resolved_type_info = owner_type_info;
		if (owner_type_info->name().isValid()) {
			materialized_owner.instantiated_name =
				StringTable::getStringView(owner_type_info->name());
		}
	};
	auto try_materialize_current_context_owner =
		[&](std::span<const TemplateTypeArg> owner_args) -> bool {
			auto try_owner_name =
				[&](std::string_view candidate_owner_name) -> bool {
					if (candidate_owner_name.empty()) {
						return false;
					}
					Parser::ResolvedQualifiedOwner current_context_owner =
						parser_.resolveQualifiedOwnerForLookup(
							candidate_owner_name);
					if (current_context_owner.resolved_from_current_context) {
						if (current_context_owner.type_info != nullptr) {
							materialized_owner =
								parser_.materializeCanonicalOwnerTypeForLookup(
									*current_context_owner.type_info,
									owner_args);
						} else if (!current_context_owner.lookup_name.empty()) {
							materialized_owner =
								parser_.resolveCanonicalInstantiatedOwnerForLookup(
									current_context_owner.lookup_name,
									owner_args);
						}
						if (!materialized_owner.instantiated_name.empty() ||
							materialized_owner.resolved_type_info != nullptr) {
							return true;
						}
					}
					return false;
				};
			if (std::string_view base_template_name =
					extractBaseTemplateName(owner_name);
				!base_template_name.empty()) {
				if (current_owner_type_name_.isValid()) {
					std::string_view current_owner_name =
						StringTable::getStringView(current_owner_type_name_);
					std::string_view current_owner_pattern_name;
					if (std::optional<StringHandle> owner_pattern_handle =
							gTemplateRegistry.get_instantiation_pattern(
								current_owner_type_name_);
						owner_pattern_handle.has_value()) {
						current_owner_pattern_name =
							StringTable::getStringView(*owner_pattern_handle);
					} else {
						current_owner_pattern_name =
							extractBaseTemplateName(current_owner_name);
					}

					auto try_prefixed_owner_name =
						[&](std::string_view owner_prefix) -> bool {
							if (owner_prefix.empty()) {
								return false;
							}
							materialized_owner =
								parser_.materializeTemplateInstantiationForLookup(
									StringBuilder()
										.append(owner_prefix)
										.append("::")
										.append(base_template_name)
										.commit(),
									owner_args);
							return !materialized_owner.instantiated_name.empty() ||
								materialized_owner.resolved_type_info != nullptr;
						};

					if (try_prefixed_owner_name(current_owner_pattern_name)) {
						return true;
					}
					if (!current_owner_name.empty() &&
						current_owner_name != current_owner_pattern_name &&
						try_prefixed_owner_name(current_owner_name)) {
						return true;
					}
				}
				if (base_template_name != owner_name &&
					try_owner_name(base_template_name)) {
					return true;
				}
			}
			if (try_owner_name(owner_name)) {
				return true;
			}
			return false;
		};
	auto owner_refers_to_current_owner_type =
		[&]() -> bool {
			if (!current_owner_type_name_.isValid()) {
				return false;
			}
			std::string_view current_owner_name =
				StringTable::getStringView(current_owner_type_name_);
			if (owner_name.empty() || owner_name == current_owner_name) {
				return true;
			}

			const TypeInfo* current_owner_type_info =
				findTypeByName(current_owner_type_name_);
			if (current_owner_type_info == nullptr) {
				return false;
			}

			const std::string_view current_base_template_name =
				StringTable::getStringView(
					current_owner_type_info->baseTemplateName());
			if (!current_base_template_name.empty() &&
				(owner_name == current_base_template_name ||
				 extractBaseTemplateName(owner_name) ==
					 current_base_template_name)) {
				return true;
			}

			StringHandle qualified_base_template_handle =
				gNamespaceRegistry.buildQualifiedIdentifier(
					current_owner_type_info->sourceNamespace(),
					current_owner_type_info->baseTemplateName());
			const std::string_view qualified_base_template_name =
				StringTable::getStringView(qualified_base_template_handle);
			return !qualified_base_template_name.empty() &&
				owner_name == qualified_base_template_name;
		};
	switch (dependent_name.owner_kind) {
	case TypeInfo::DependentQualifiedNameRecord::OwnerKind::CurrentInstantiation:
		if (prefer_current_owner_type_name &&
			owner_refers_to_current_owner_type()) {
			materialized_owner.instantiated_name =
				StringTable::getStringView(current_owner_type_name_);
			materialized_owner.resolved_type_info =
				findTypeByName(current_owner_type_name_);
			break;
		}
		if (!dependent_name.owner_template_arguments.empty()) {
			InlineVector<TemplateTypeArg, 4> owner_args =
				materializeDependentRecordTemplateArgs(
					dependent_name.owner_template_arguments,
					depth);
			if (!templateArgsStillDependent(owner_args)) {
				if (try_materialize_current_context_owner(
						std::span<const TemplateTypeArg>(
							owner_args.data(),
							owner_args.size()))) {
					break;
				}
				materialized_owner =
					parser_.resolveCanonicalInstantiatedOwnerForLookup(
						owner_name,
						std::span<const TemplateTypeArg>(
							owner_args.data(),
							owner_args.size()));
				if (materialized_owner.instantiated_name.empty() &&
					materialized_owner.resolved_type_info == nullptr &&
					!owner_args.empty()) {
					materialized_owner.instantiated_name =
						parser_.get_instantiated_class_name(owner_name, owner_args);
					if (!materialized_owner.instantiated_name.empty()) {
						materialized_owner.resolved_type_info = findTypeByName(
							StringTable::getOrInternStringHandle(
								materialized_owner.instantiated_name));
					}
				}
				if (materialized_owner.instantiated_name.empty() &&
					materialized_owner.resolved_type_info == nullptr &&
					dependent_name.owner_type.is_valid()) {
					assign_owner_type(tryGetTypeInfo(dependent_name.owner_type));
				}
				break;
			}
		}
		if (dependent_name.owner_type.is_valid()) {
			assign_owner_type(tryGetTypeInfo(dependent_name.owner_type));
		}
		break;
	case TypeInfo::DependentQualifiedNameRecord::OwnerKind::TemplateParameter:
		if (dependent_name.owner_template_arguments.empty()) {
			if (auto owner_subst_it = param_map_.find(owner_name);
				owner_subst_it != param_map_.end()) {
				if (const TypeInfo* owner_type_info =
						tryGetTypeInfo(owner_subst_it->second.type_index)) {
					materialized_owner =
						parser_.materializeCanonicalOwnerTypeForLookup(*owner_type_info, {});
					if (materialized_owner.instantiated_name.empty() &&
						materialized_owner.resolved_type_info == nullptr) {
						assign_owner_type(owner_type_info);
					}
				}
			}
			break;
		}
		[[fallthrough]];
	case TypeInfo::DependentQualifiedNameRecord::OwnerKind::DependentInstantiation:
	case TypeInfo::DependentQualifiedNameRecord::OwnerKind::UnknownSpecialization: {
		InlineVector<TemplateTypeArg, 4> owner_args =
			materializeDependentRecordTemplateArgs(
				dependent_name.owner_template_arguments,
				depth);
		if (owner_args.empty() && !dependent_name.owner_template_arguments.empty()) {
			break;
		}
		if (templateArgsStillDependent(owner_args)) {
			break;
		}
		if (allow_current_context_dependent_owner_materialization &&
			try_materialize_current_context_owner(
				std::span<const TemplateTypeArg>(
					owner_args.data(),
					owner_args.size()))) {
			break;
		}
		if (auto owner_template_subst_it = param_map_.find(owner_name);
			owner_template_subst_it != param_map_.end() &&
			owner_template_subst_it->second.is_template_template_arg &&
			owner_template_subst_it->second.template_name_handle.isValid()) {
			owner_name = StringTable::getStringView(
				owner_template_subst_it->second.template_name_handle);
		}
		materialized_owner =
			parser_.resolveCanonicalInstantiatedOwnerForLookup(
				owner_name,
				std::span<const TemplateTypeArg>(
					owner_args.data(),
					owner_args.size()));
		if (materialized_owner.instantiated_name.empty() &&
			materialized_owner.resolved_type_info == nullptr &&
			!owner_args.empty()) {
			materialized_owner.instantiated_name =
				parser_.get_instantiated_class_name(owner_name, owner_args);
			if (!materialized_owner.instantiated_name.empty()) {
				materialized_owner.resolved_type_info = findTypeByName(
					StringTable::getOrInternStringHandle(
						materialized_owner.instantiated_name));
			}
		}
		break;
	}
	}

	return materialized_owner;
}

ExpressionSubstitutor::MaterializedQualifiedLookupOwner
ExpressionSubstitutor::materializeDependentQualifiedMemberPrefixOwner(
	StringHandle owner_name_handle,
	std::string_view recorded_owner_name,
	std::span<const TypeInfo::DependentQualifiedNameRecord::Member> prefix_chain,
	int depth) {
	MaterializedQualifiedLookupOwner result;
	if (!owner_name_handle.isValid()) {
		return result;
	}

	result.owner_name = owner_name_handle;
	result.owner_type = findTypeByName(owner_name_handle);
	StringHandle recorded_owner_name_handle;
	if (!recorded_owner_name.empty()) {
		recorded_owner_name_handle =
			StringTable::getOrInternStringHandle(recorded_owner_name);
	}
	result.outer_binding = parser_.buildAccumulatedOuterTemplateBinding(
		result.owner_type,
		nullptr,
		recorded_owner_name_handle);
	std::string_view root_owner_name = StringTable::getStringView(owner_name_handle);
	std::vector<QualifiedTypeMemberAccess> materialized_prefix_chain;
	materialized_prefix_chain.reserve(prefix_chain.size());
	auto resolve_materialized_prefix_chain = [&]() -> const TypeInfo* {
		if (root_owner_name.empty() || materialized_prefix_chain.empty()) {
			return nullptr;
		}
		return resolveQualifiedMemberNamespaceChain(root_owner_name, materialized_prefix_chain);
	};

	for (size_t i = 0; i < prefix_chain.size(); ++i) {
		const auto& member_record = prefix_chain[i];
		std::string_view chained_owner_name =
			StringTable::getStringView(result.owner_name);
		if (chained_owner_name.empty()) {
			result = {};
			return result;
		}

		if (member_record.has_template_arguments) {
			InlineVector<TemplateTypeArg, 4> member_args =
				materializeDependentRecordTemplateArgs(
					member_record.template_arguments,
					depth);
			if (templateArgsStillDependent(member_args)) {
				result = {};
				return result;
			}

			std::string_view member_name =
				StringTable::getStringView(member_record.name);
			std::string_view qualified_owner_name;
			if (i == 0 &&
				!recorded_owner_name.empty()) {
				std::string_view pattern_owner_name =
					StringBuilder()
						.append(recorded_owner_name)
						.append("::")
						.append(member_name)
						.commit();
				if (gTemplateRegistry.lookupTemplate(pattern_owner_name).has_value()) {
					qualified_owner_name = pattern_owner_name;
				}
			}
			if (qualified_owner_name.empty()) {
				qualified_owner_name =
					parser_.lookup_inherited_member_template_name(
						result.owner_name,
						member_record.name,
						0);
			}
			if (qualified_owner_name.empty()) {
				qualified_owner_name =
					StringBuilder()
						.append(chained_owner_name)
						.append("::")
						.append(member_name)
						.commit();
			}

			OuterTemplateBinding outer_binding =
				parser_.buildAccumulatedOuterTemplateBinding(
					result.owner_type,
					&result.outer_binding,
					StringHandle{});
			if (!outer_binding.param_names.empty()) {
				gTemplateRegistry.registerOuterTemplateBinding(
					StringTable::getOrInternStringHandle(qualified_owner_name),
					std::move(outer_binding));
			}

			// Prefer resolving the dependent member-template segment via qualified
			// owner-chain lookup first. This path preserves current-instantiation
			// and unknown-specialization outer bindings for nested member templates
			// such as Traits<T>::template Box<U>::type::value. The outer binding
			// must be registered before this lookup because the lookup can lazily
			// instantiate the member template and cache the first result.
			QualifiedTypeMemberAccess templated_member_access;
			templated_member_access.member_name = member_record.name;
			templated_member_access.has_template_arguments = true;
			templated_member_access.template_arguments =
				&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(
					member_args.toVector());
			if (const TypeInfo* resolved_owner =
					resolveQualifiedMemberNamespaceChain(
						chained_owner_name,
						std::span<QualifiedTypeMemberAccess>(
							&templated_member_access,
							1));
				resolved_owner != nullptr &&
				resolved_owner->name().isValid()) {
				(void)resolved_owner;
			}
			materialized_prefix_chain.push_back(templated_member_access);

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
			StringHandle next_owner_handle =
				materialized_member_owner.canonicalNameHandle();
			if (!next_owner_handle.isValid()) {
				std::string_view instantiated_owner_name =
					parser_.get_instantiated_class_name(
						qualified_owner_name,
						member_args);
				if (!instantiated_owner_name.empty()) {
					next_owner_handle =
						StringTable::getOrInternStringHandle(instantiated_owner_name);
				}
			}
			if (!next_owner_handle.isValid()) {
				result = {};
				return result;
			}

			result.owner_name = next_owner_handle;
			result.owner_type = materialized_member_owner.resolved_type_info;
			if (result.owner_type == nullptr) {
				result.owner_type = findTypeByName(next_owner_handle);
			}
			if (const TypeInfo* chain_owner = resolve_materialized_prefix_chain();
				chain_owner != nullptr &&
				chain_owner->name().isValid()) {
				result.owner_type = chain_owner;
				result.owner_name = chain_owner->name();
			}
			result.outer_binding =
				parser_.buildAccumulatedOuterTemplateBinding(
					result.owner_type,
					&result.outer_binding,
					StringHandle{});
			continue;
		}

		QualifiedTypeMemberAccess member_access;
		member_access.member_name = member_record.name;
		materialized_prefix_chain.push_back(member_access);
		const TypeInfo* resolved_owner = resolve_materialized_prefix_chain();
		if (resolved_owner == nullptr) {
			resolved_owner =
				resolveQualifiedMemberNamespaceChain(
					chained_owner_name,
					std::span<QualifiedTypeMemberAccess>(&member_access, 1));
		}
		if (resolved_owner == nullptr ||
			!resolved_owner->name().isValid()) {
			result = {};
			return result;
		}

		result.owner_type = resolved_owner;
		result.owner_name = result.owner_type->name();
	}

	return result;
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
		auto resolveConcreteAliasChain = [&](const TypeInfo* candidate_type_info) -> const TypeInfo* {
			const TypeInfo* current_type_info = candidate_type_info;
			for (size_t alias_depth = 0; current_type_info != nullptr && current_type_info->isTypeAlias() && alias_depth < 32; ++alias_depth) {
				// A member alias like `using type = T;` resolved with `T = int*`
				// keeps its pointer/reference/array/cv indirection in the alias
				// specifier while its TypeIndex points at the terminal type. Stop
				// before collapsing such an alias so those surface modifiers survive.
				if (typeAliasPreservesSurfaceModifiers(*current_type_info)) {
					break;
				}
				ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
					current_type_info->registeredTypeIndex().withCategory(current_type_info->typeEnum()));
				if (resolved_alias.terminal_type_info == nullptr ||
					resolved_alias.terminal_type_info == current_type_info) {
					break;
				}
				current_type_info = resolved_alias.terminal_type_info;
			}
			return current_type_info;
		};
		Parser::AliasTemplateMaterializationResult materialized_owner =
			materializeDependentQualifiedRecordOwner(
				*dependent_name,
				depth,
				/*prefer_current_owner_type_name=*/true,
				/*allow_current_context_dependent_owner_materialization=*/false);
		std::string_view materialized_owner_name =
			materialized_owner.canonicalName();
		FLASH_LOG(Templates, Debug, "lookupMaterializedDependentMember owner ",
				  StringTable::getStringView(dependent_name->owner_name), " -> ", materialized_owner_name,
				  " members=", dependent_name->member_chain.size());
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
			resolved_type = resolveConcreteAliasChain(resolved_type);
			FLASH_LOG(Templates, Debug, "lookupMaterializedDependentMember chain result ",
					  resolved_type != nullptr ? StringTable::getStringView(resolved_type->name()) : std::string_view{"<null>"});
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

std::optional<ASTNode> ExpressionSubstitutor::tryEvaluateConcreteConceptCall(const CallExprNode& call) const {
	const std::string_view concept_name = call.has_qualified_name()
		? call.qualified_name()
		: call.called_from().value();
	auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
	if (!concept_opt.has_value() && call.has_qualified_name()) {
		concept_opt = gConceptRegistry.lookupConcept(call.called_from().value());
	}
	if (!concept_opt.has_value() || !concept_opt->is<ConceptDeclarationNode>() || !call.has_template_arguments()) {
		return std::nullopt;
	}

	const auto try_materialize_bound_template_arg = [&](const ASTNode& template_arg)
		-> std::optional<std::vector<ASTNode>> {
		if (template_arg.is<TypeSpecifierNode>()) {
			return std::vector<ASTNode>{template_arg};
		}
		if (!template_arg.is<ExpressionNode>()) {
			return std::nullopt;
		}

		const ExpressionNode& expr = template_arg.as<ExpressionNode>();
		std::string_view param_name;
		Token source_token = call.called_from();
		auto try_materialize_concrete_type = [&](std::string_view type_name, const Token& type_token)
			-> std::optional<std::vector<ASTNode>> {
			if (!type_name.empty()) {
				if (const TypeInfo* type_info =
						findTypeByName(StringTable::getOrInternStringHandle(type_name));
					type_info != nullptr) {
					TypeSpecifierNode type_spec = makeTypeSpecifierFromTemplateTypeArg(
						TemplateTypeArg::makeType(
							type_info->registeredTypeIndex().withCategory(
								type_info->typeEnum())),
						type_token);
					return std::vector<ASTNode>{
						ASTNode::emplace_node<TypeSpecifierNode>(std::move(type_spec))};
				}

				if (TypeCategory builtin_category =
						getTemplateArgumentBuiltinCategoryFromTokenText(type_name);
					builtin_category != TypeCategory::Invalid) {
					TypeSpecifierNode type_spec = makeTypeSpecifierFromTemplateTypeArg(
						TemplateTypeArg::makeType(nativeTypeIndex(builtin_category)),
						type_token);
					return std::vector<ASTNode>{
						ASTNode::emplace_node<TypeSpecifierNode>(std::move(type_spec))};
				}
			}
			return std::nullopt;
		};
		if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
			param_name = identifier->name();
			source_token = identifier->identifier_token();
			if (std::optional<std::vector<ASTNode>> concrete_type =
					try_materialize_concrete_type(param_name, source_token);
				concrete_type.has_value()) {
				return concrete_type;
			}
		} else if (const auto* qualified = std::get_if<QualifiedIdentifierNode>(&expr)) {
			source_token = qualified->identifier_token();
			if (std::optional<std::vector<ASTNode>> concrete_type =
					try_materialize_concrete_type(
						qualified->full_name(),
						source_token);
				concrete_type.has_value()) {
				return concrete_type;
			}
			if (std::optional<std::vector<ASTNode>> concrete_type =
					try_materialize_concrete_type(
						qualified->name(),
						source_token);
				concrete_type.has_value()) {
				return concrete_type;
			}
			return std::nullopt;
		} else if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&expr)) {
			param_name = tparam_ref->param_name().view();
			source_token = tparam_ref->token();
		} else {
			return std::nullopt;
		}

		auto scalar_it = param_map_.find(param_name);
		if (scalar_it != param_map_.end()) {
			const TemplateTypeArg bound_arg = scalar_it->second;
			return materializeTemplateArgumentNodes(
				std::span<const TemplateTypeArg>(&bound_arg, 1),
				source_token);
		}

		auto pack_it = pack_map_.find(StringTable::getOrInternStringHandle(param_name));
		if (pack_it != pack_map_.end()) {
			return materializeTemplateArgumentNodes(
				std::span<const TemplateTypeArg>(pack_it->second.data(), pack_it->second.size()),
				source_token);
		}

		return std::nullopt;
	};

	std::optional<InlineVector<TemplateTypeArg, 4>> concrete_args =
		parser_.materializeConcreteCallTemplateArguments(call.template_arguments());
	if (!concrete_args.has_value()) {
		std::vector<ASTNode> rebound_template_args;
		rebound_template_args.reserve(call.template_arguments().size());
		bool rebound_any_template_arg = false;

		for (const ASTNode& template_arg : call.template_arguments()) {
			if (std::optional<std::vector<ASTNode>> rebound_nodes =
					try_materialize_bound_template_arg(template_arg);
				rebound_nodes.has_value()) {
				rebound_any_template_arg = true;
				rebound_template_args.insert(
					rebound_template_args.end(),
					rebound_nodes->begin(),
					rebound_nodes->end());
				continue;
			}

			rebound_template_args.push_back(template_arg);
		}

		if (!rebound_any_template_arg) {
			return std::nullopt;
		}

		concrete_args = parser_.materializeConcreteCallTemplateArguments(rebound_template_args);
		if (!concrete_args.has_value()) {
			return std::nullopt;
		}
	}
	if (templateArgsStillDependent(*concrete_args)) {
		return std::nullopt;
	}

	const ConstraintEvaluationResult constraint_result =
		evaluateConstraint(concept_opt->as<ConceptDeclarationNode>(), *concrete_args, &parser_);
	const Token bool_token(
		Token::Type::Keyword,
		constraint_result.satisfied ? "true"sv : "false"sv,
		call.called_from().line(),
		call.called_from().column(),
		call.called_from().file_index());
	return ASTNode::emplace_node<ExpressionNode>(
		BoolLiteralNode(bool_token, constraint_result.satisfied));
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
	auto materializeResolvedCallExpr = [&](
		const FunctionDeclarationNode& target_func,
		ChunkedVector<ASTNode>&& args,
		const Token& called_from_token,
		std::optional<std::string_view> qualified_call_name,
		bool copy_template_arguments,
		bool infer_qualified_name_from_parent) -> ASTNode {
		ExpressionNode& new_expr = emplaceDirectCallExpr(
			target_func.decl_node(),
			&target_func,
			std::move(args),
			called_from_token);
		CallMetadataCopyOptions copy_options;
		copy_options.copy_template_arguments = copy_template_arguments;
		copy_options.copy_dependent_unqualified_lookup_record = false;
		copy_options.copy_dependent_qualified_lookup_record = false;
		copyMetadataToExpr(new_expr, copy_options);
		if (call.has_dependent_unqualified_lookup_record()) {
			DependentUnqualifiedCallLookupRecord completed_record =
				*call.dependent_unqualified_lookup_record();
			completed_record.point_of_instantiation_function = &target_func;
			completed_record.point_of_instantiation_mangled_name = StringHandle{};
			if (target_func.has_mangled_name()) {
				completed_record.point_of_instantiation_mangled_name =
					StringTable::getOrInternStringHandle(
						target_func.mangled_name());
			}
			setCallDependentUnqualifiedLookupRecord(new_expr, completed_record);
		}
		if (qualified_call_name.has_value() && !qualified_call_name->empty()) {
			setCallQualifiedName(new_expr, *qualified_call_name);
		} else if (infer_qualified_name_from_parent) {
			if (std::optional<std::string_view> inferred_qualified_name =
					tryBuildQualifiedCallNameOverride(target_func);
				inferred_qualified_name.has_value()) {
				setCallQualifiedName(new_expr, *inferred_qualified_name);
			}
		}
		if (target_func.has_mangled_name()) {
			setCallMangledName(new_expr, target_func.mangled_name());
		}
		const bool needs_structured_qualified_record =
			call.has_qualified_name() ||
			(qualified_call_name.has_value() && !qualified_call_name->empty()) ||
			infer_qualified_name_from_parent;
		if (needs_structured_qualified_record &&
			!call.has_definition_lookup_record() &&
			!call.has_dependent_unqualified_lookup_record()) {
			CallExprNode* materialized_call = std::get_if<CallExprNode>(&new_expr);
			if (materialized_call != nullptr) {
				std::vector<TypeSpecifierNode> substituted_arg_types;
				if (parser_.tryCollectFunctionCallArgTypes(
						materialized_call->arguments(),
						substituted_arg_types)) {
					if (std::optional<FunctionCallDefinitionLookupRecord> record =
							parser_.tryBuildCurrentFunctionCallDefinitionLookupRecord(
								target_func.decl_node().identifier_token(),
								substituted_arg_types,
								false,
								target_func,
								true,
								false);
						record.has_value()) {
						setCallDefinitionLookupRecord(*materialized_call, *record);
					}
				}
			}
		}
		return ASTNode(&new_expr);
	};
	auto normalizePendingSemanticRoots = [&]() {
		parser_.normalizePendingSemanticRoots();
	};
	auto resolveUniqueFunctionOverload = [&](
		std::span<const ASTNode> candidates,
		std::span<const TypeSpecifierNode> substituted_arg_types) -> const FunctionDeclarationNode* {
		if (candidates.empty()) {
			return nullptr;
		}
		OverloadResolutionResult resolution =
			resolve_overload_with_argument_nodes(candidates, substituted_arg_types, std::span<const ASTNode>{});
		if (resolution.has_match &&
			!resolution.is_ambiguous &&
			resolution.selected_overload != nullptr &&
			resolution.selected_overload->is<FunctionDeclarationNode>()) {
			return &resolution.selected_overload->as<FunctionDeclarationNode>();
		}
		return nullptr;
	};
	auto resolveQualifiedOwnerOverload = [&](
		std::string_view owner_name,
		std::string_view member_name,
		const ChunkedVector<ASTNode>& substituted_args) -> const FunctionDeclarationNode* {
		if (owner_name.empty() || member_name.empty()) {
			return nullptr;
		}
		std::vector<TypeSpecifierNode> substituted_arg_types;
		if (!parser_.tryCollectFunctionCallArgTypes(substituted_args, substituted_arg_types)) {
			return nullptr;
		}

		DefinitionPreferredMemberOverloadSet owner_overloads =
			collectOwnerNamedMemberFunctionOverloads(
			owner_name,
			member_name,
			[this](StringHandle owner_handle) {
				parser_.instantiateLazyClassToPhase(
					owner_handle,
					ClassInstantiationPhase::Full);
			},
			[](const StructMemberFunction& member_func,
			   const FunctionDeclarationNode&) {
				return !member_func.is_constructor &&
					!member_func.is_destructor;
			});
		if (const FunctionDeclarationNode* definition_target =
				resolveUniqueFunctionOverload(
					owner_overloads.definition_preferred,
					substituted_arg_types);
			definition_target != nullptr) {
			return definition_target;
		}
		return resolveUniqueFunctionOverload(
			owner_overloads.all,
			substituted_arg_types);
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

	if (call.has_template_arguments()) {
		CallExprNode substituted_concept_call(
			call.callee(),
			ChunkedVector<ASTNode>{},
			call.called_from());
		copyCallMetadataWithTransformedTemplateArguments(
			substituted_concept_call,
			call,
			[this](const ASTNode& template_arg) {
				return substitute(template_arg);
			},
			CallMetadataCopyOptions{});
		if (std::optional<ASTNode> concept_result =
				tryEvaluateConcreteConceptCall(substituted_concept_call);
			concept_result.has_value()) {
			return *concept_result;
		}
	}

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
							if (TypeCategory builtin_category =
									getTemplateArgumentBuiltinCategoryFromTokenText(identifier->name());
								builtin_category != TypeCategory::Invalid) {
								substituted_template_args.push_back(
									TemplateTypeArg::makeType(nativeTypeIndex(builtin_category)));
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
					if (call.has_definition_lookup_record() &&
						call.definition_lookup_record()
							->definition_bound_template_declaration != nullptr &&
						call.has_qualified_name() &&
						template_name == call.qualified_name()) {
						std::vector<ASTNode> substituted_template_arg_nodes;
						substituted_template_arg_nodes.reserve(
							explicit_template_arg_nodes.size());
						for (const auto& arg_node :
							 explicit_template_arg_nodes) {
							substituted_template_arg_nodes.push_back(
								substitute(arg_node));
						}
						std::optional<ASTNode> instantiated =
							parser_.resolveDefinitionBoundQualifiedTemplateCall(
								*call.definition_lookup_record(),
								template_name,
								std::span<const ASTNode>(
									substituted_template_arg_nodes.data(),
									substituted_template_arg_nodes.size()),
								substituted_args_nodes,
								substituted_arg_types);
						if (instantiated.has_value()) {
							normalizePendingSemanticRoots();
						}
						return instantiated;
					}
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
			auto tryInstantiateExplicitQualifiedMemberTemplateFromRecord =
				[&]() -> std::optional<ASTNode> {
				if (!call.has_dependent_qualified_lookup_record()) {
					return std::nullopt;
				}
				const TypeInfo::DependentQualifiedNameRecord& dependent_record =
					*call.dependent_qualified_lookup_record();
				if (dependent_record.member_chain.empty()) {
					return std::nullopt;
				}
				const TypeInfo::DependentQualifiedNameRecord::Member&
					final_member_record =
						dependent_record.member_chain.back();
				InlineVector<TemplateTypeArg, 4> member_template_args;
				if (final_member_record.has_template_arguments) {
					member_template_args =
						materializeDependentRecordTemplateArgs(
							final_member_record.template_arguments,
							kInitialDependentMemberTypeResolutionDepth);
				} else if (!substituted_template_args.empty()) {
					member_template_args.reserve(
						substituted_template_args.size());
					for (const TemplateTypeArg& arg :
						 substituted_template_args) {
						member_template_args.push_back(arg);
					}
				} else {
					return std::nullopt;
				}
				if (templateArgsStillDependent(member_template_args)) {
					return std::nullopt;
				}
				const bool prefer_current_owner_type_name =
					dependent_record.owner_kind ==
					TypeInfo::DependentQualifiedNameRecord::OwnerKind::
						CurrentInstantiation;
				const std::string_view recorded_owner_name =
					StringTable::getStringView(dependent_record.owner_name);
				Parser::AliasTemplateMaterializationResult materialized_owner =
					materializeDependentQualifiedRecordOwner(
						dependent_record,
						kInitialDependentMemberTypeResolutionDepth,
						prefer_current_owner_type_name,
						/*allow_current_context_dependent_owner_materialization=*/true);
				std::string_view materialized_owner_name =
					materialized_owner.canonicalName();
				if (!materialized_owner_name.empty() &&
					dependent_record.member_chain.size() > 1) {
					MaterializedQualifiedLookupOwner prefix_owner =
						materializeDependentQualifiedMemberPrefixOwner(
							StringTable::getOrInternStringHandle(
								materialized_owner_name),
							recorded_owner_name,
							std::span<
								const TypeInfo::DependentQualifiedNameRecord::Member>(
								dependent_record.member_chain.data(),
								dependent_record.member_chain.size() - 1),
							kInitialDependentMemberTypeResolutionDepth);
					materialized_owner_name =
						prefix_owner.owner_name.isValid()
							? StringTable::getStringView(
								  prefix_owner.owner_name)
							: std::string_view{};
				}
				if (materialized_owner_name.empty()) {
					return std::nullopt;
				}
				const std::string_view member_name =
					StringTable::getStringView(final_member_record.name);
				if (member_name.empty()) {
					return std::nullopt;
				}
				std::optional<ASTNode> instantiated =
					parser_.try_instantiate_member_function_template_explicit(
						materialized_owner_name,
						member_name,
						std::span<const TemplateTypeArg>(
							member_template_args.data(),
							member_template_args.size()));
				if (instantiated.has_value()) {
					normalizePendingSemanticRoots();
				}
				return instantiated;
			};
			// First try function template instantiation to obtain accurate return type
			std::optional<ASTNode> instantiated_template = std::nullopt;
			if (call.has_dependent_qualified_lookup_record()) {
				instantiated_template =
					tryInstantiateExplicitQualifiedMemberTemplateFromRecord();
			}
			if (!instantiated_template.has_value() &&
				!call.has_dependent_qualified_lookup_record() &&
				!qualified_name.empty()) {
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
			}
			if (!instantiated_template.has_value() && !qualified_name.empty()) {
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

					substituted_args_nodes =
						substituteCallArgumentsPreservingPackExpansion(call.arguments());
					ASTNode resolved_call = materializeResolvedCallExpr(
						func_decl,
						std::move(substituted_args_nodes),
						call.called_from(),
						std::nullopt,
						false, // copy_template_arguments
						false  // infer_qualified_name_from_parent
					);
					std::vector<ASTNode> substituted_template_arg_nodes;
					substituted_template_arg_nodes.reserve(explicit_template_arg_nodes.size());
					for (const auto& arg_node : explicit_template_arg_nodes) {
						substituted_template_arg_nodes.push_back(substitute(arg_node));
					}
					setCallTemplateArguments(
						resolved_call.as<ExpressionNode>(),
						std::move(substituted_template_arg_nodes));
					return resolved_call;
				}
			}

			// Try variable template instantiation before class template
			const bool had_dependent_template_args =
				templateArgsStillDependent(std::span<const TemplateTypeArg>(substituted_template_args.data(), substituted_template_args.size()));
			const bool has_variable_template_candidate =
				gTemplateRegistry.lookupVariableTemplate(func_name).has_value() ||
				(call.has_qualified_name() &&
					gTemplateRegistry.lookupVariableTemplate(call.qualified_name()).has_value());
			std::optional<ASTNode> var_template_node;
			if (gTemplateRegistry.lookupVariableTemplate(func_name).has_value()) {
				var_template_node =
					parser_.try_instantiate_variable_template(
						func_name,
						substituted_template_args,
						nullptr);
			}
			if (var_template_node.has_value()) {
				normalizePendingSemanticRoots();
			}
			// If not found by simple name, try qualified name if available
			if (!var_template_node.has_value() &&
				call.has_qualified_name() &&
				gTemplateRegistry.lookupVariableTemplate(call.qualified_name()).has_value()) {
				var_template_node =
					parser_.try_instantiate_variable_template(
						call.qualified_name(),
						substituted_template_args,
						nullptr);
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
				return materializeSubstitutedUnresolvedCall(
					substituteCallArgumentsPreservingPackExpansion(call.arguments()));
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
				substituted_args_nodes =
					substituteCallArgumentsPreservingPackExpansion(call.arguments());
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
					return materializeSubstitutedUnresolvedCall(
						substituteCallArgumentsPreservingPackExpansion(call.arguments()));
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
					ChunkedVector<ASTNode> substituted_args_nodes =
						substituteCallArgumentsPreservingPackExpansion(call.arguments());

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

		ChunkedVector<ASTNode> substituted_args =
			substituteCallArgumentsPreservingPackExpansion(call.arguments());

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
			return materializeResolvedCallExpr(
				instantiated_func,
				std::move(substituted_args),
				call.called_from(),
				std::nullopt,
				true,  // copy_template_arguments
				false  // infer_qualified_name_from_parent
			);
		} else {
			FLASH_LOG(Templates, Warning, "  Failed to instantiate template function: ", template_func_name);
		}
	}

	if (call.has_dependent_unqualified_lookup_record()) {
		ChunkedVector<ASTNode> substituted_args =
			substituteCallArgumentsPreservingPackExpansion(call.arguments());

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
					return materializeResolvedCallExpr(
						*target_func,
						std::move(substituted_args),
						call.called_from(),
						std::nullopt,
						true, // copy_template_arguments
						true  // infer_qualified_name_from_parent
					);
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
						ChunkedVector<ASTNode> substituted_args =
							substituteCallArgumentsPreservingPackExpansion(call.arguments());

						const FunctionDeclarationNode* target_func =
							resolveQualifiedOwnerOverload(
								resolved_owner_name,
								resolved_member_name,
								substituted_args);
						std::string_view mutable_resolved_owner_name =
							resolved_owner_name;
						if (target_func == nullptr) {
							std::optional<ASTNode> instantiated_member =
								parser_.instantiateLazyMemberForCanonicalOwner(
									mutable_resolved_owner_name,
									resolved_member_name,
									std::span<const TemplateTypeArg>{});
							if (instantiated_member.has_value() &&
								instantiated_member->is<FunctionDeclarationNode>()) {
								target_func = &instantiated_member->as<FunctionDeclarationNode>();
							}
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
						if (const FunctionDeclarationNode* resolved_target =
								resolveQualifiedOwnerOverload(
									resolved_owner_name,
									resolved_member_name,
									substituted_args);
							resolved_target != nullptr &&
							(target_func == nullptr ||
							 (!target_func->get_definition().has_value() &&
							  resolved_target->get_definition().has_value()))) {
							target_func = resolved_target;
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
							const std::string_view resolved_qualified_name =
								StringBuilder()
									.append(resolved_owner_name)
									.append("::")
									.append(resolved_member_name)
									.commit();
							return materializeResolvedCallExpr(
								*target_func,
								std::move(substituted_args),
								called_from_token,
								resolved_qualified_name,
								true,  // copy_template_arguments
								false  // infer_qualified_name_from_parent
							);
						}
					}
				}
			}
		}
		const std::string_view recorded_owner_name =
			StringTable::getStringView(dependent_record.owner_name);
		Parser::AliasTemplateMaterializationResult materialized_owner =
			materializeDependentQualifiedRecordOwner(
				dependent_record,
				kInitialDependentMemberTypeResolutionDepth,
				/*prefer_current_owner_type_name=*/false,
				/*allow_current_context_dependent_owner_materialization=*/false);
		std::string_view materialized_owner_name =
			materialized_owner.canonicalName();

		if (!materialized_owner_name.empty() &&
			!dependent_record.member_chain.empty()) {
			const std::string_view materialized_record_owner_name =
				materialized_owner_name;
			std::string_view member_name =
				StringTable::getStringView(
					dependent_record.member_chain.back().name);
			if (dependent_record.member_chain.size() > 1) {
				MaterializedQualifiedLookupOwner materialized_prefix_owner =
					materializeDependentQualifiedMemberPrefixOwner(
						StringTable::getOrInternStringHandle(materialized_owner_name),
						recorded_owner_name,
						std::span<const TypeInfo::DependentQualifiedNameRecord::Member>(
							dependent_record.member_chain.data(),
							dependent_record.member_chain.size() - 1),
						kInitialDependentMemberTypeResolutionDepth);
				materialized_owner_name =
					materialized_prefix_owner.owner_name.isValid()
						? StringTable::getStringView(materialized_prefix_owner.owner_name)
						: std::string_view{};
			}

			if (!materialized_owner_name.empty()) {
					ChunkedVector<ASTNode> substituted_args =
						substituteCallArgumentsPreservingPackExpansion(call.arguments());
					const FunctionDeclarationNode* target_func =
						resolveQualifiedOwnerOverload(
							materialized_owner_name,
							member_name,
							substituted_args);

					if (target_func == nullptr) {
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
					if (!materialized_record_owner_name.empty() &&
						materialized_owner_name != materialized_record_owner_name &&
						(target_func == nullptr ||
						 !target_func->get_definition().has_value())) {
						std::string_view qualified_owner_name =
							StringBuilder()
								.append(materialized_record_owner_name)
								.append("::")
								.append(materialized_owner_name)
								.commit();
						std::string_view mutable_qualified_owner_name =
							qualified_owner_name;
						if (const FunctionDeclarationNode* qualified_target =
								resolveQualifiedOwnerOverload(
									qualified_owner_name,
									member_name,
									substituted_args);
							qualified_target != nullptr &&
							(target_func == nullptr ||
							 (!target_func->get_definition().has_value() &&
							  qualified_target->get_definition().has_value()))) {
							target_func = qualified_target;
							materialized_owner_name = qualified_owner_name;
						} else if (std::optional<ASTNode> qualified_instantiated_member =
								parser_.instantiateLazyMemberForCanonicalOwner(
									mutable_qualified_owner_name,
									member_name,
									std::span<const TemplateTypeArg>{});
							qualified_instantiated_member.has_value() &&
							qualified_instantiated_member->is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& instantiated_qualified_target =
								qualified_instantiated_member->as<FunctionDeclarationNode>();
							if (target_func == nullptr ||
								(!target_func->get_definition().has_value() &&
								 instantiated_qualified_target.get_definition().has_value())) {
								target_func = &instantiated_qualified_target;
								materialized_owner_name = mutable_qualified_owner_name;
							}
						}
					}
					if (const FunctionDeclarationNode* resolved_target =
							resolveQualifiedOwnerOverload(
								materialized_owner_name,
								member_name,
								substituted_args);
						resolved_target != nullptr &&
						(target_func == nullptr ||
						 (!target_func->get_definition().has_value() &&
						  resolved_target->get_definition().has_value()))) {
						target_func = resolved_target;
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
						const std::string_view materialized_qualified_name =
							StringBuilder()
								.append(materialized_owner_name)
								.append("::")
								.append(member_name)
								.commit();
						return materializeResolvedCallExpr(
							*target_func,
							std::move(substituted_args),
							called_from_token,
							materialized_qualified_name,
							true,  // copy_template_arguments
							false  // infer_qualified_name_from_parent
						);
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
				ChunkedVector<ASTNode> substituted_args =
					substituteCallArgumentsPreservingPackExpansion(call.arguments());

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
				if (const FunctionDeclarationNode* resolved_target =
						resolveQualifiedOwnerOverload(
							instantiated_name,
							member_name,
							substituted_args);
					resolved_target != nullptr &&
					(target_func == nullptr ||
					 (!target_func->get_definition().has_value() &&
					  resolved_target->get_definition().has_value()))) {
					target_func = resolved_target;
				}

				if (target_func == nullptr) {
					FLASH_LOG(Templates, Debug, "  Canonical owner member not materialized yet, keeping original call");
					return wrapOriginalCall();
				}

				return materializeResolvedCallExpr(
					*target_func,
					std::move(substituted_args),
					new_func_token,
					new_func_name,
					true,  // copy_template_arguments
					false  // infer_qualified_name_from_parent
				);
			}
		}
	}

	// Return with substituted children preserved.
	FLASH_LOG(Templates, Debug, "  Returning function call as-is");
	ChunkedVector<ASTNode> substituted_fallback_args =
		substituteCallArgumentsPreservingPackExpansion(call.arguments());
	return materializeSubstitutedUnresolvedCall(std::move(substituted_fallback_args));
}

ASTNode ExpressionSubstitutor::substituteCallExpr(const CallExprNode& call) {
	if (!call.has_receiver()) {
		return substituteFunctionCallImpl(call);
	}

	ASTNode substituted_receiver = substitute(call.receiver());
	ChunkedVector<ASTNode> substituted_args =
		substituteCallArgumentsPreservingPackExpansion(call.arguments());

	if ((call.callee().is_member() || call.callee().is_static_member()) &&
		call.called_from().kind().is_identifier()) {
		auto resolveUniqueFunctionOverload = [&](
			std::span<const ASTNode> candidates) -> const FunctionDeclarationNode* {
			if (candidates.empty()) {
				return nullptr;
			}
			std::vector<TypeSpecifierNode> substituted_arg_types;
			if (!parser_.tryCollectFunctionCallArgTypes(
					substituted_args,
					substituted_arg_types)) {
				return nullptr;
			}
			OverloadResolutionResult resolution =
				resolve_overload_with_argument_nodes(candidates, substituted_arg_types, std::span<const ASTNode>{});
			if (resolution.has_match &&
				!resolution.is_ambiguous &&
				resolution.selected_overload != nullptr &&
				resolution.selected_overload->is<FunctionDeclarationNode>()) {
				return &resolution.selected_overload->as<FunctionDeclarationNode>();
			}
			return nullptr;
		};
		auto tryResolveQualifiedReceiverMember =
			[&]() -> const FunctionDeclarationNode* {
				if (!call.has_dependent_qualified_lookup_record()) {
					return nullptr;
				}
				const TypeInfo::DependentQualifiedNameRecord& dependent_record =
					*call.dependent_qualified_lookup_record();
				if (dependent_record.member_chain.empty()) {
					return nullptr;
				}
				Parser::AliasTemplateMaterializationResult materialized_owner =
					materializeDependentQualifiedRecordOwner(
						dependent_record,
						kInitialDependentMemberTypeResolutionDepth,
						true,
						/*allow_current_context_dependent_owner_materialization=*/false);
				std::string_view materialized_owner_name =
					materialized_owner.canonicalName();
				if (materialized_owner_name.empty()) {
					return nullptr;
				}
				if (dependent_record.member_chain.size() > 1) {
					MaterializedQualifiedLookupOwner prefix_owner =
						materializeDependentQualifiedMemberPrefixOwner(
							materialized_owner.canonicalNameHandle(),
							materialized_owner_name,
							std::span<
								const TypeInfo::DependentQualifiedNameRecord::
									Member>(
								dependent_record.member_chain.data(),
								dependent_record.member_chain.size() - 1),
							kInitialDependentMemberTypeResolutionDepth);
					if (!prefix_owner.owner_name.isValid()) {
						return nullptr;
					}
					materialized_owner_name =
						StringTable::getStringView(prefix_owner.owner_name);
				}
				const std::string_view member_name =
					StringTable::getStringView(
						dependent_record.member_chain.back().name);
				if (dependent_record.member_chain.back()
						.has_template_arguments) {
					InlineVector<TemplateTypeArg, 4>
						explicit_member_template_args =
							materializeDependentRecordTemplateArgs(
								dependent_record.member_chain.back()
									.template_arguments,
								kInitialDependentMemberTypeResolutionDepth);
					if (!templateArgsStillDependent(
							std::span<const TemplateTypeArg>(
								explicit_member_template_args.data(),
								explicit_member_template_args.size()))) {
						std::vector<TypeSpecifierNode>
							substituted_arg_types;
						if (parser_.tryCollectFunctionCallArgTypes(
								substituted_args,
								substituted_arg_types)) {
							if (std::optional<ASTNode>
									instantiated_member =
										parser_
											.tryInstantiateMemberFunctionTemplateCall(
												materialized_owner_name,
												member_name,
												explicit_member_template_args,
												substituted_arg_types,
												!substituted_args.empty(),
												false);
								instantiated_member.has_value() &&
								instantiated_member->is<
									FunctionDeclarationNode>()) {
								return &instantiated_member
											->as<
												FunctionDeclarationNode>();
							}
						}
					}
				}
				DefinitionPreferredMemberOverloadSet owner_overloads =
					collectOwnerNamedMemberFunctionOverloads(
					materialized_owner_name,
					member_name,
					[this](StringHandle owner_handle) {
						parser_.instantiateLazyClassToPhase(
							owner_handle,
							ClassInstantiationPhase::Full);
					},
					[](const StructMemberFunction& member_func,
					   const FunctionDeclarationNode&) {
						return !member_func.is_constructor &&
							!member_func.is_destructor;
					});
				const FunctionDeclarationNode* target_func = nullptr;
				if (const FunctionDeclarationNode* definition_target =
						resolveUniqueFunctionOverload(
							std::span<const ASTNode>(
								owner_overloads.definition_preferred.data(),
								owner_overloads.definition_preferred.size()));
					definition_target != nullptr) {
					target_func = definition_target;
				} else {
					target_func = resolveUniqueFunctionOverload(
						std::span<const ASTNode>(
							owner_overloads.all.data(),
							owner_overloads.all.size()));
				}
				std::string_view mutable_owner_name =
					materialized_owner_name;
				if (target_func == nullptr || !target_func->is_materialized()) {
					if (std::optional<ASTNode> instantiated_member =
							parser_.instantiateLazyMemberForCanonicalOwner(
								mutable_owner_name,
								member_name,
								std::span<const TemplateTypeArg>{});
						instantiated_member.has_value() &&
						instantiated_member->is<FunctionDeclarationNode>()) {
						return &instantiated_member
									->as<FunctionDeclarationNode>();
					}
				}
				return target_func;
			};
		const FunctionDeclarationNode* rebound_member =
			tryResolveQualifiedReceiverMember();
		const bool used_qualified_receiver_rebind =
			rebound_member != nullptr;
		if (rebound_member == nullptr) {
			rebound_member =
				parser_.tryResolveConcreteMemberFunction(
					substituted_receiver,
					call.called_from().value());
		}
		if (rebound_member != nullptr) {
			if (!used_qualified_receiver_rebind) {
				if (auto receiver_type =
						parser_.get_expression_type(substituted_receiver);
					receiver_type.has_value() &&
					is_struct_type(receiver_type->category())) {
					if (const TypeInfo* receiver_type_info =
							tryGetTypeInfo(receiver_type->type_index());
						receiver_type_info != nullptr) {
						std::string_view class_name =
							StringTable::getStringView(
								receiver_type_info->name());
						if (parser_.instantiateLazyMemberForCanonicalOwner(
								class_name,
								call.called_from().value(),
								std::span<const TemplateTypeArg>{})
								.has_value()) {
							rebound_member =
								parser_.tryResolveConcreteMemberFunction(
									substituted_receiver,
									call.called_from().value());
						}
					}
				}
			}

			if (rebound_member != nullptr) {
				CallExprNode rebound_call = makeResolvedMemberCallExpr(
					substituted_receiver,
					*rebound_member,
					std::move(substituted_args),
					call.called_from());
				CallMetadataCopyOptions copy_options;
				copy_options.copy_dependent_qualified_lookup_record = false;
				copyCallMetadataWithTransformedTemplateArguments(
					rebound_call,
					call,
					[this](const ASTNode& template_arg) {
						return substitute(template_arg);
					},
					copy_options);
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
				  ", is_template_template_arg=", arg.is_template_template_arg,
				  ", is_dependent=", arg.is_dependent,
				  ", dependent_name='", (arg.dependent_name.isValid() ? StringTable::getStringView(arg.dependent_name) : std::string_view()), "'",
				  ", has_typed_identity=", arg.has_typed_value_identity ? 1 : 0);

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
		Token substituted_token = makeTemplateArgumentTypeToken(
			arg,
			id.identifier_token());
		TypeSpecifierNode new_type =
			makeTypeSpecifierFromTemplateTypeArg(arg, substituted_token);
		TypeSpecifierNode& stored_type =
			gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(std::move(new_type));
		return ASTNode(&stored_type);
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
	if (!ns_name.empty()) {
		size_t first_scope = ns_name.find("::");
		if (first_scope != std::string_view::npos &&
			first_scope + 2 < ns_name.size()) {
			std::string_view base_name = ns_name.substr(0, first_scope);
			std::string_view member_chain = ns_name.substr(first_scope + 2);
			std::vector<QualifiedTypeMemberAccess> parsed_chain =
				parser_.buildQualifiedTypeMemberChain(member_chain);
			if (const TypeInfo* resolved_owner =
					parser_.resolveBaseClassMemberTypeChain(base_name, parsed_chain);
				resolved_owner != nullptr &&
				resolved_owner->name().isValid()) {
				NamespaceHandle rebound_ns = gNamespaceRegistry.getOrCreateNamespace(
					NamespaceRegistry::GLOBAL_NAMESPACE,
					resolved_owner->name());
				QualifiedIdentifierNode& rebound_qual_id =
					gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
						rebound_ns,
						qual_id.identifier_token());
				if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
					rebound_qual_id.setDependentQualifiedName(*dependent_record);
				}
				ExpressionNode& rebound_expr =
					gChunkedAnyStorage.emplace_back<ExpressionNode>(rebound_qual_id);
				return ASTNode(&rebound_expr);
			}
		}
	}
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
		if (owner_type_info == nullptr) {
			size_t separator = owner_name.rfind("::");
			if (separator != std::string_view::npos &&
				separator + 2 < owner_name.size()) {
				std::string_view parent_name = owner_name.substr(0, separator);
				std::string_view nested_member_name = owner_name.substr(separator + 2);
				if (const TypeInfo* resolved_nested_type =
						parser_.lookup_inherited_type_alias(parent_name, nested_member_name);
					resolved_nested_type != nullptr) {
					owner_name = StringTable::getStringView(resolved_nested_type->name());
					owner_type_info = resolved_nested_type;
				}
			}
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
		const std::string_view recorded_owner_name =
			StringTable::getStringView(dependent_name->owner_name);
		Parser::AliasTemplateMaterializationResult materialized_owner =
			materializeDependentQualifiedRecordOwner(
				*dependent_name,
				kInitialDependentMemberTypeResolutionDepth,
				/*prefer_current_owner_type_name=*/true,
				/*allow_current_context_dependent_owner_materialization=*/false);
		std::string_view materialized_owner_name =
			materialized_owner.canonicalName();

		if (!materialized_owner_name.empty() && !dependent_name->member_chain.empty()) {
			std::string_view materialized_namespace = materialized_owner_name;
			if (dependent_name->member_chain.size() > 1) {
				MaterializedQualifiedLookupOwner resolved_namespace =
					materializeDependentQualifiedMemberPrefixOwner(
						StringTable::getOrInternStringHandle(materialized_owner_name),
						recorded_owner_name,
						std::span<const TypeInfo::DependentQualifiedNameRecord::Member>(
							dependent_name->member_chain.data(),
							dependent_name->member_chain.size() - 1),
						kInitialDependentMemberTypeResolutionDepth);
				if (!resolved_namespace.owner_name.isValid()) {
					ExpressionNode& deferred_expr =
						gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
					return ASTNode(&deferred_expr);
				}
				materialized_namespace =
					StringTable::getStringView(resolved_namespace.owner_name);
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
			auto substitute_qualified_id_template_args = [&]() {
				std::vector<ASTNode> explicit_template_arg_nodes;
				explicit_template_arg_nodes.reserve(
					qual_id.template_arguments().size());
				for (const ASTNode& template_arg : qual_id.template_arguments()) {
					if (template_arg.is<TypeSpecifierNode>()) {
						TypeSpecifierNode& substituted_type =
							gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
								substituteInType(
									template_arg.as<TypeSpecifierNode>()));
						explicit_template_arg_nodes.push_back(
							ASTNode(&substituted_type));
					} else {
						explicit_template_arg_nodes.push_back(
							substitute(template_arg));
					}
				}
				return explicit_template_arg_nodes;
			};
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				StringTable::getOrInternStringHandle(materialized_namespace));
			QualifiedIdentifierNode& new_qual_id =
				gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
					new_ns_handle,
					final_token);
			bool preserved_dependent_member_template_record = false;
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
					materializeTemplateArgumentNodes(
						std::span<const TemplateTypeArg>(
							final_member_args.data(),
							final_member_args.size()),
						final_token);
				if (explicit_template_arg_nodes.empty() &&
					qual_id.has_template_arguments()) {
					explicit_template_arg_nodes =
						substitute_qualified_id_template_args();
				}
				if (!explicit_template_arg_nodes.empty()) {
					new_qual_id.set_template_arguments(
						std::move(explicit_template_arg_nodes));
				} else {
					new_qual_id.setDependentQualifiedName(*dependent_name);
					preserved_dependent_member_template_record = true;
				}
			} else if (qual_id.has_template_arguments()) {
				std::vector<ASTNode> explicit_template_arg_nodes =
					substitute_qualified_id_template_args();
				if (!explicit_template_arg_nodes.empty()) {
					new_qual_id.set_template_arguments(
						std::move(explicit_template_arg_nodes));
				}
			}
			if (!preserved_dependent_member_template_record) {
				new_qual_id.setDependentQualifiedName(*dependent_name);
			}
			FLASH_LOG(Templates, Debug, "  Record-substituted qualified-id: ",
					  qual_id.full_name(), " -> ", materialized_namespace, "::",
					  final_member_name,
					  preserved_dependent_member_template_record
						  ? " (preserved deferred member-template record)"
						  : "");
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
						if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
							new_qual_id.setDependentQualifiedName(*dependent_record);
						}
						FLASH_LOG(Templates, Debug, "  Substituted TTP: ", ns_name, "::", qual_id.name(), " -> ",
								  materialized_type.instantiated_name, "::", qual_id.name());
						ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
						return ASTNode(&new_expr);
					}
				}
			}
		}
	}

	if (current_owner_type_name_.isValid() && !ns_name.empty()) {
		auto resolveDirectOwnerAlias = [&]() -> const TypeInfo* {
			StringHandle direct_member_handle = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(StringTable::getStringView(current_owner_type_name_))
					.append("::")
					.append(ns_name)
					.commit());
			auto direct_it = getTypesByNameMap().find(direct_member_handle);
			if (direct_it == getTypesByNameMap().end() || direct_it->second == nullptr) {
				return nullptr;
			}
			ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
				direct_it->second->registeredTypeIndex().withCategory(direct_it->second->typeEnum()));
			if (resolved_alias.terminal_type_info != nullptr) {
				return resolved_alias.terminal_type_info;
			}
			if (resolved_alias.type_index.is_valid()) {
				return tryGetTypeInfo(resolved_alias.type_index);
			}
			return direct_it->second;
		};
		const TypeInfo* resolved_member_info = resolveDirectOwnerAlias();
		if (resolved_member_info == nullptr) {
			resolved_member_info = resolveCurrentOwnerQualifiedNamespaceMember(
				StringTable::getOrInternStringHandle(ns_name));
		}
		if (resolved_member_info != nullptr) {
			StringHandle resolved_name_handle = resolved_member_info->name();
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE,
				resolved_name_handle);
			QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
				new_ns_handle,
				qual_id.identifier_token());
			if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
				new_qual_id.setDependentQualifiedName(*dependent_record);
			}
			FLASH_LOG(Templates, Debug, "  Resolved struct-local alias namespace '", ns_name,
					  "' -> ", StringTable::getStringView(resolved_name_handle),
					  " in owner ", StringTable::getStringView(current_owner_type_name_));
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
			return ASTNode(&new_expr);
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
				if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
					new_qual_id.setDependentQualifiedName(*dependent_record);
				}
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

	// Check if the namespace name contains template parameters (hash-based naming)
	std::string_view base_template_name = extractBaseTemplateName(ns_name);
	if (base_template_name.empty()) {
		// Handle nested member-template qualifiers where template arguments belong
		// to the namespace path component (e.g. Owner::Inner<int>::type::value).
		if (qual_id.has_template_arguments()) {
			size_t tail_separator = ns_name.rfind("::");
			if (tail_separator != std::string_view::npos &&
				tail_separator + 2 < ns_name.size()) {
				std::string_view owner_template_name = ns_name.substr(0, tail_separator);
				std::string_view owner_tail = ns_name.substr(tail_separator + 2);
				std::vector<TemplateTypeArg> owner_template_args;
				owner_template_args.reserve(qual_id.template_arguments().size());
				bool owner_args_valid = true;
				for (const ASTNode& template_arg_node : qual_id.template_arguments()) {
					ASTNode substituted_arg = substitute(template_arg_node);
					if (substituted_arg.is<TypeSpecifierNode>()) {
						owner_template_args.push_back(
							TemplateTypeArg(substituted_arg.as<TypeSpecifierNode>()));
						continue;
					}
					if (substituted_arg.is<ExpressionNode>()) {
						if (auto constant_value =
								parser_.try_evaluate_constant_expression(substituted_arg);
							constant_value.has_value()) {
							TemplateTypeArg value_arg = TemplateTypeArg::makeValue(
								constant_value->value,
								constant_value->type_index.is_valid()
									? constant_value->type_index
									: TypeIndex{}.withCategory(constant_value->type));
							value_arg.setValueIdentity(constant_value->identity);
							owner_template_args.push_back(std::move(value_arg));
							continue;
						}
					}
					owner_args_valid = false;
					break;
				}
				if (owner_args_valid && !owner_template_args.empty()) {
					Parser::AliasTemplateMaterializationResult materialized_owner =
						parser_.materializeTemplateInstantiationForLookup(
							owner_template_name,
							owner_template_args);
					std::string_view materialized_owner_name =
						materialized_owner.canonicalName();
					if (!materialized_owner_name.empty()) {
						StringHandle rewritten_namespace_handle =
							StringTable::getOrInternStringHandle(
								StringBuilder()
									.append(materialized_owner_name)
									.append("::")
									.append(owner_tail)
									.commit());
						NamespaceHandle rewritten_namespace =
							gNamespaceRegistry.getOrCreateNamespace(
								NamespaceRegistry::GLOBAL_NAMESPACE,
								rewritten_namespace_handle);
						QualifiedIdentifierNode& rewritten_qual_id =
							gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
								rewritten_namespace,
								qual_id.identifier_token());
						if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
							rewritten_qual_id.setDependentQualifiedName(*dependent_record);
						}
						ExpressionNode& rewritten_expr =
							gChunkedAnyStorage.emplace_back<ExpressionNode>(
								rewritten_qual_id);
						return ASTNode(&rewritten_expr);
					}
				}
			}
		}
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
		if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
			new_qual_id.setDependentQualifiedName(*dependent_record);
		}
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
			if (const auto* dependent_record = qual_id.dependentQualifiedName()) {
				new_qual_id.setDependentQualifiedName(*dependent_record);
			}
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

	if (std::optional<TypeSpecifierNode> canonical_builtin =
			makeCanonicalBuiltinTypeSpecifier(type)) {
		return *canonical_builtin;
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

		if (TypeCategory builtin_category =
				getTemplateArgumentBuiltinCategoryFromTokenText(token_type_name);
			builtin_category != TypeCategory::Invalid) {
			TypeSpecifierNode builtin_spec(
				nativeTypeIndex(builtin_category),
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

		auto try_owner_binding_substitution = [&](std::string_view candidate_owner_name)
			-> std::optional<TemplateTypeArg> {
			std::string_view current_name = candidate_owner_name;
			while (!current_name.empty()) {
				StringHandle owner_name_handle =
					StringTable::getOrInternStringHandle(current_name);
				const TypeInfo* owner_type_info = findTypeByName(owner_name_handle);
				OuterTemplateBinding owner_binding =
					parser_.buildAccumulatedOuterTemplateBinding(
						owner_type_info,
						nullptr,
						owner_name_handle);
				for (size_t i = 0;
					 i < owner_binding.param_names.size() &&
					 i < owner_binding.param_args.size();
					 ++i) {
					if (StringTable::getStringView(owner_binding.param_names[i]) ==
						token_type_name) {
						const TemplateTypeArg& bound_arg =
							owner_binding.param_args[i];
						if (!bound_arg.is_value &&
							bound_arg.type_index.is_valid()) {
							return bound_arg;
						}
					}
				}
				size_t owner_sep = current_name.rfind("::");
				if (owner_sep == std::string_view::npos) {
					break;
				}
				current_name = current_name.substr(0, owner_sep);
			}
			return std::nullopt;
		};
		if (current_owner_type_name_.isValid()) {
			std::string_view owner_name =
				StringTable::getStringView(current_owner_type_name_);
			if (!owner_name.empty()) {
				if (std::optional<TemplateTypeArg> owner_bound_arg =
						try_owner_binding_substitution(owner_name);
					owner_bound_arg.has_value()) {
					TypeSpecifierNode substituted_type =
						makeTypeSpecifierFromTemplateTypeArg(
							*owner_bound_arg,
							type.token());
					applyOuterTypeModifiers(substituted_type, type);
					return substituted_type;
				}
			}
		}
		if (const Parser::ParserInstantiationContext* inst_ctx =
				parser_.currentInstantiationContext();
			inst_ctx != nullptr &&
			inst_ctx->origin_name.isValid()) {
			std::string_view origin_name =
				StringTable::getStringView(inst_ctx->origin_name);
			if (!origin_name.empty()) {
				if (std::optional<TemplateTypeArg> owner_bound_arg =
						try_owner_binding_substitution(origin_name);
					owner_bound_arg.has_value()) {
					TypeSpecifierNode substituted_type =
						makeTypeSpecifierFromTemplateTypeArg(
							*owner_bound_arg,
							type.token());
					applyOuterTypeModifiers(substituted_type, type);
					return substituted_type;
				}
			}
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
					if (TypeCategory builtin_category =
							getTemplateArgumentBuiltinCategoryFromTokenText(identifier->name());
						builtin_category != TypeCategory::Invalid) {
						expanded_args.push_back(
							TemplateTypeArg::makeType(nativeTypeIndex(builtin_category)));
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
