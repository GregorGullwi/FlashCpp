#include <cctype>
#include <functional>

#include "Parser.h"
#include "CallNodeHelpers.h"
#include "RebindStaticMemberAst.h"
#include "ConstExprEvaluator.h"
#include "ExpressionSubstitutor.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"
#include "InstantiationQueue.h"
#include "ParserTemplateClassShared.h"
#include "AstTraversal.h"
#include "TemplateEnvironment.h"

static constexpr size_t kMaxAliasUnwrapIterations = 64;

// Per-compilation iteration counter for try_instantiate_class_template.
// Lifted to file scope so resetTemplateInstantiationCounters() can clear it
// at the start of each parse() call (i.e. each new compilation unit).
// Using thread_local avoids data races in multi-threaded compiler drivers.
static thread_local int g_template_inst_iteration_count = 0;
static thread_local bool g_template_inst_iteration_limit_tripped = false;
static constexpr int g_template_inst_max_iterations = 10000;

static TemplateParameterNode rebuildOuterTemplateParameter(
	StringHandle param_name,
	const TypeInfo::TemplateArgInfo& arg_info) {
	Token outer_token(
		Token::Type::Identifier,
		StringTable::getStringView(param_name),
		0,
		0,
		0);
	if (arg_info.is_template_template_arg) {
		return TemplateParameterNode(
			param_name,
			std::vector<TemplateParameterNode>{},
			outer_token);
	}
	if (auto outer_type_spec = makeTypeSpecifierFromTemplateArgInfo(arg_info, outer_token);
		outer_type_spec.has_value()) {
		return TemplateParameterNode(param_name, *outer_type_spec, outer_token);
	}
	return TemplateParameterNode(param_name, outer_token);
}

static TemplateParameterNode rebuildOuterTemplateParameter(
	StringHandle param_name,
	const TemplateTypeArg& arg) {
	Token outer_token(
		Token::Type::Identifier,
		StringTable::getStringView(param_name),
		0,
		0,
		0);
	if (arg.is_template_template_arg) {
		return TemplateParameterNode(
			param_name,
			std::vector<TemplateParameterNode>{},
			outer_token);
	}
	if (arg.is_value) {
		return TemplateParameterNode(
			param_name,
			makeTypeSpecifierFromTemplateTypeArg(arg, outer_token),
			outer_token);
	}
	TemplateParameterNode outer_param(param_name, outer_token);
	outer_param.set_registered_type_index(
		arg.type_index.withCategory(arg.typeEnum()));
	return outer_param;
}

static void buildTemplateParameterReplayState(
	std::span<const TemplateParameterNode> template_params,
	InlineVector<StringHandle, 4>& template_param_names,
	InlineVector<TemplateParameterKind, 4>& template_param_kinds,
	InlineVector<TypeCategory, 4>& non_type_categories) {
	template_param_names.clear();
	template_param_kinds.clear();
	non_type_categories.clear();
	template_param_names.reserve(template_params.size());
	template_param_kinds.reserve(template_params.size());
	non_type_categories.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		template_param_names.push_back(template_param.nameHandle());
		template_param_kinds.push_back(template_param.kind());
		non_type_categories.push_back(
			template_param.kind() == TemplateParameterKind::NonType &&
				template_param.has_type()
				? template_param.type_specifier_node().type()
				: TypeCategory::Invalid);
	}
}

template <typename ParamContainer, typename ArgContainer>
static TypeIndex resolveDependentMemberTemplateArtifactsForParam(
	Parser& parser,
	ASTNode* original_param_type_node,
	const TypeSpecifierNode& param_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	TypeIndex substituted_type_index,
	bool resolve_materialized_member_alias) {
	if (auto resolved_param = resolveDependentPlaceholderFromTemplateParams(
			tryGetTypeInfo(param_type_spec.type_index()),
			template_params,
			template_args)) {
		substituted_type_index = *resolved_param;
	}
	return resolveDependentMemberTemplateSubstitutionArtifacts(
		parser,
		original_param_type_node,
		param_type_spec,
		template_params,
		template_args,
		substituted_type_index,
		true,
		true,
		resolve_materialized_member_alias);
}

template <typename ParamContainer, typename ArgContainer>
static ASTNode buildMaterializedParamTypeNode(
	const TypeSpecifierNode& param_type_spec,
	const ParamContainer& template_params,
	const ArgContainer& template_args,
	TypeCategory new_param_type,
	TypeIndex new_param_type_index) {
	ASTNode new_param_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
		new_param_type,
		param_type_spec.qualifier(),
		get_type_size_bits(new_param_type),
		param_type_spec.token(),
		param_type_spec.cv_qualifier());
	auto& new_param_spec = new_param_type_node.as<TypeSpecifierNode>();
	new_param_spec.set_type_index(new_param_type_index);
	for (const auto& pl : param_type_spec.pointer_levels()) {
		new_param_spec.add_pointer_level(pl.cv_qualifier);
	}
	new_param_spec.set_reference_qualifier(param_type_spec.reference_qualifier());
	if (param_type_spec.has_function_signature()) {
		new_param_spec.set_function_signature(param_type_spec.function_signature());
	} else if (new_param_type_index.category() == TypeCategory::FunctionPointer ||
			   new_param_type_index.category() == TypeCategory::MemberFunctionPointer) {
		if (const auto* arg = findTemplateArgByName(
				param_type_spec.token().value(),
				template_params,
				template_args)) {
			if (arg->function_signature.has_value()) {
				new_param_spec.set_function_signature(*arg->function_signature);
			}
		}
	}
	normalizeSubstitutedTypeSpec(new_param_spec);
	return new_param_type_node;
}

struct SourceMemberIdentityMaps {
	std::unordered_map<const void*, ASTNode> by_node;
	std::unordered_map<uint64_t, ASTNode> by_location;
};

enum class ReplaySignatureMatchResult {
	Match,
	Mismatch,
	InsufficientEvidence
};

template <typename InstantiatedDeclNode, typename OutOfLineDeclNode>
static ReplaySignatureMatchResult declarationsMatchAfterTemplateSubstitution(
	Parser& parser,
	const InstantiatedDeclNode& instantiated_decl,
	const OutOfLineDeclNode& out_of_line_decl,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params);
static const TypeSpecifierNode* getDeclarationParamTypeNode(const ASTNode& param);
static bool typeSpecifiersMatchForSignatureValidation(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs);
static ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params);
static ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const ConstructorDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params);

static const void* sourceMemberAstNodeKey(const ASTNode& node) {
	if (!node.has_value())
		return nullptr;
	if (node.is<FunctionDeclarationNode>())
		return &node.as<FunctionDeclarationNode>();
	if (node.is<TemplateFunctionDeclarationNode>())
		return &node.as<TemplateFunctionDeclarationNode>();
	if (node.is<ConstructorDeclarationNode>())
		return &node.as<ConstructorDeclarationNode>();
	if (node.is<DestructorDeclarationNode>())
		return &node.as<DestructorDeclarationNode>();
	return nullptr;
}

static std::optional<uint64_t> sourceMemberLocationKey(const ASTNode& member) {
	const FunctionDeclarationNode* member_func_decl = get_function_decl_node(member);
	if (member_func_decl == nullptr) {
		return std::nullopt;
	}
	const Token& ident = member_func_decl->decl_node().identifier_token();
	uint64_t key = 0xCBF29CE484222325ull;
	auto mix = [&](uint64_t value) {
		key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
	};
	mix(static_cast<uint64_t>(ident.file_index()));
	mix(static_cast<uint64_t>(ident.line()));
	mix(static_cast<uint64_t>(ident.column()));
	mix(static_cast<uint64_t>(ident.handle().handle));
	const auto& params = member_func_decl->parameter_nodes();
	mix(static_cast<uint64_t>(params.size()));
	for (const ASTNode& param : params) {
		if (param.is<DeclarationNode>()) {
			const TypeSpecifierNode& ts = param.as<DeclarationNode>().type_specifier_node();
			mix(static_cast<uint64_t>(ts.token().handle().handle));
			mix(static_cast<uint64_t>(ts.type_index().index()));
			mix(static_cast<uint64_t>(ts.pointer_depth()));
			mix(static_cast<uint64_t>(ts.reference_qualifier()));
		} else {
			mix(static_cast<uint64_t>(std::hash<std::string_view>{}(param.type_name())));
		}
	}
	if (member.is<TemplateFunctionDeclarationNode>()) {
		mix(static_cast<uint64_t>(
			member.as<TemplateFunctionDeclarationNode>().template_parameters().size()) |
			(1ull << 63));
	}
	return key;
}

static void registerSourceMemberStubIdentity(
	SourceMemberIdentityMaps& identity_maps,
	const ASTNode& source_member,
	ASTNode instantiated_stub) {
	auto register_key = [&](const void* key) {
		if (key != nullptr) {
			identity_maps.by_node[key] = instantiated_stub;
		}
	};
	register_key(sourceMemberAstNodeKey(source_member));
	if (source_member.is<TemplateFunctionDeclarationNode>()) {
		register_key(sourceMemberAstNodeKey(
			source_member.as<TemplateFunctionDeclarationNode>().function_declaration()));
	}
	if (std::optional<uint64_t> location_key = sourceMemberLocationKey(source_member);
		location_key.has_value()) {
		identity_maps.by_location[*location_key] = instantiated_stub;
	}
}

static ASTNode* findSourceMemberStubByIdentity(
	SourceMemberIdentityMaps& identity_maps,
	const ASTNode& source_member) {
	auto find_by_key = [&](const void* key) -> ASTNode* {
		if (key == nullptr) {
			return nullptr;
		}
		auto it = identity_maps.by_node.find(key);
		if (it == identity_maps.by_node.end()) {
			return nullptr;
		}
		return &it->second;
	};
	if (ASTNode* direct = find_by_key(sourceMemberAstNodeKey(source_member))) {
		return direct;
	}
	if (source_member.is<TemplateFunctionDeclarationNode>()) {
		if (ASTNode* by_template_fn = find_by_key(sourceMemberAstNodeKey(
				source_member.as<TemplateFunctionDeclarationNode>().function_declaration()))) {
			return by_template_fn;
		}
	}
	if (std::optional<uint64_t> location_key = sourceMemberLocationKey(source_member);
		location_key.has_value()) {
		auto it = identity_maps.by_location.find(*location_key);
		if (it != identity_maps.by_location.end()) {
			return &it->second;
		}
	}
	return nullptr;
}

struct OutOfLineMemberStubResolution {
	FunctionDeclarationNode* func = nullptr;
	bool ambiguous = false;
	bool insufficient_evidence = false;
};

static OutOfLineMemberStubResolution findPlainOutOfLineMemberStubByIdentity(
	Parser& parser,
	SourceMemberIdentityMaps& identity_maps,
	std::span<const StructMemberFunctionDecl> source_members,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name) {
	const std::string_view out_of_line_name =
		out_of_line_decl.decl_node().identifier_token().value();
	OutOfLineMemberStubResolution resolution;
	InlineVector<FunctionDeclarationNode*, 4> resolved_matches;

	for (const StructMemberFunctionDecl& source_member : source_members) {
		if (!source_member.function_declaration.is<FunctionDeclarationNode>()) {
			continue;
		}
		const FunctionDeclarationNode* source_func_decl =
			get_function_decl_node(source_member.function_declaration);
		if (source_func_decl == nullptr ||
			source_func_decl->decl_node().identifier_token().value() != out_of_line_name) {
			continue;
		}

		ASTNode* matched_stub = findSourceMemberStubByIdentity(
			identity_maps,
			source_member.function_declaration);
		if (matched_stub == nullptr || !matched_stub->is<FunctionDeclarationNode>()) {
			continue;
		}

		FunctionDeclarationNode* inst_func_decl =
			get_function_decl_node_mut(*matched_stub);
		if (inst_func_decl == nullptr) {
			continue;
		}

		ReplaySignatureMatchResult signature_match =
			declarationsMatchAfterTemplateSubstitution(
				parser,
				*inst_func_decl,
				out_of_line_decl,
				outer_template_params,
				outer_template_args,
				owner_type_name,
				std::span<const TemplateParameterNode>{},
				std::span<const TemplateParameterNode>{});
		if (signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
			resolution.insufficient_evidence = true;
			continue;
		}
		if (signature_match != ReplaySignatureMatchResult::Match) {
			continue;
		}

		resolved_matches.push_back(inst_func_decl);
	}

	if (resolved_matches.size() == 1) {
		resolution.func = resolved_matches.front();
	} else if (resolved_matches.size() > 1) {
		resolution.ambiguous = true;
	}
	return resolution;
}

struct OutOfLineConstructorStubResolution {
	ConstructorDeclarationNode* ctor = nullptr;
	bool ambiguous = false;
	bool insufficient_evidence = false;
};

static OutOfLineConstructorStubResolution findPlainOutOfLineConstructorStubByIdentity(
	Parser& parser,
	SourceMemberIdentityMaps& identity_maps,
	std::span<const StructMemberFunctionDecl> source_members,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name) {
	OutOfLineConstructorStubResolution resolution;
	InlineVector<ConstructorDeclarationNode*, 4> resolved_matches;
	for (const StructMemberFunctionDecl& source_member : source_members) {
		if (!source_member.function_declaration.is<ConstructorDeclarationNode>()) {
			continue;
		}

		ASTNode* matched_stub = findSourceMemberStubByIdentity(
			identity_maps,
			source_member.function_declaration);
		if (matched_stub == nullptr || !matched_stub->is<ConstructorDeclarationNode>()) {
			continue;
		}

		ConstructorDeclarationNode* inst_ctor_decl =
			&matched_stub->as<ConstructorDeclarationNode>();
		ReplaySignatureMatchResult signature_match =
			declarationsMatchAfterTemplateSubstitution(
				parser,
				*inst_ctor_decl,
				out_of_line_decl,
				outer_template_params,
				outer_template_args,
				owner_type_name,
				std::span<const TemplateParameterNode>{},
				std::span<const TemplateParameterNode>{});
		if (signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
			resolution.insufficient_evidence = true;
			continue;
		}
		const bool matches_candidate =
			signature_match == ReplaySignatureMatchResult::Match;

		if (matches_candidate) {
			resolved_matches.push_back(inst_ctor_decl);
		}
	}

	if (resolved_matches.size() == 1) {
		resolution.ctor = resolved_matches.front();
		return resolution;
	}
	if (resolved_matches.size() > 1) {
		resolution.ambiguous = true;
	}
	return resolution;
}

template <typename OutOfLineDeclNode>
static OutOfLineConstructorStubResolution findOutOfLineConstructorTemplateStubByIdentity(
	Parser& parser,
	SourceMemberIdentityMaps& identity_maps,
	std::span<const StructMemberFunctionDecl> source_members,
	const OutOfLineDeclNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params) {
	OutOfLineConstructorStubResolution resolution;
	InlineVector<ConstructorDeclarationNode*, 4> resolved_matches;
	for (const StructMemberFunctionDecl& source_member : source_members) {
		if (!source_member.function_declaration.is<ConstructorDeclarationNode>()) {
			continue;
		}

		ASTNode* matched_stub = findSourceMemberStubByIdentity(
			identity_maps,
			source_member.function_declaration);
		if (matched_stub == nullptr || !matched_stub->is<ConstructorDeclarationNode>()) {
			continue;
		}

		ConstructorDeclarationNode* inst_ctor_decl =
			&matched_stub->as<ConstructorDeclarationNode>();
		if (inst_ctor_decl->has_template_body_position()) {
			continue;
		}

		if (!inst_ctor_decl->template_parameters().empty() &&
			inst_ctor_decl->template_parameters().size() !=
				out_of_line_inner_template_params.size()) {
			continue;
		}

		ReplaySignatureMatchResult substituted_signature_match =
			outOfLineConstructorTemplateMatchesCandidate(
			parser,
			*inst_ctor_decl,
			out_of_line_decl,
			outer_template_params,
			outer_template_args,
			owner_type_name,
			std::span<const TemplateParameterNode>(
				inst_ctor_decl->template_parameters().data(),
				inst_ctor_decl->template_parameters().size()),
			out_of_line_inner_template_params);
		if (substituted_signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
			resolution.insufficient_evidence = true;
			continue;
		}
		bool matches_candidate =
			substituted_signature_match == ReplaySignatureMatchResult::Match;

		if (!matches_candidate) {
			continue;
		}

		resolved_matches.push_back(inst_ctor_decl);
	}

	if (resolved_matches.size() == 1) {
		resolution.ctor = resolved_matches.front();
		return resolution;
	}
	if (resolved_matches.size() > 1) {
		resolution.ambiguous = true;
	}
	return resolution;
}

static bool constructorDeclarationsHaveEquivalentInstantiatedSignature(
	const ConstructorDeclarationNode& lhs,
	const ConstructorDeclarationNode& rhs) {
	if (lhs.template_parameters().size() != rhs.template_parameters().size() ||
		lhs.parameter_nodes().size() != rhs.parameter_nodes().size()) {
		return false;
	}

	for (size_t i = 0; i < lhs.parameter_nodes().size(); ++i) {
		const TypeSpecifierNode* lhs_param =
			getDeclarationParamTypeNode(lhs.parameter_nodes()[i]);
		const TypeSpecifierNode* rhs_param =
			getDeclarationParamTypeNode(rhs.parameter_nodes()[i]);
		if (lhs_param == nullptr || rhs_param == nullptr) {
			return false;
		}
		if (!typeSpecifiersMatchForSignatureValidation(*lhs_param, *rhs_param)) {
			return false;
		}
	}

	return true;
}

static bool functionDeclarationsHaveEquivalentInstantiatedSignature(
	const FunctionDeclarationNode& lhs,
	const FunctionDeclarationNode& rhs) {
	if (lhs.decl_node().identifier_token().value() !=
			rhs.decl_node().identifier_token().value() ||
		lhs.parameter_nodes().size() != rhs.parameter_nodes().size() ||
		lhs.is_static() != rhs.is_static() ||
		lhs.is_const_member_function() != rhs.is_const_member_function() ||
		lhs.is_volatile_member_function() != rhs.is_volatile_member_function()) {
		return false;
	}

	for (size_t i = 0; i < lhs.parameter_nodes().size(); ++i) {
		const TypeSpecifierNode* lhs_param =
			getDeclarationParamTypeNode(lhs.parameter_nodes()[i]);
		const TypeSpecifierNode* rhs_param =
			getDeclarationParamTypeNode(rhs.parameter_nodes()[i]);
		if (lhs_param == nullptr || rhs_param == nullptr) {
			return false;
		}
		if (!typeSpecifiersMatchForSignatureValidation(*lhs_param, *rhs_param)) {
			return false;
		}
	}

	return true;
}

template <typename EligibleFn>
static OutOfLineConstructorStubResolution findMatchingConstructorInStructInfo(
	StructTypeInfo& struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	EligibleFn&& is_candidate_eligible) {
	OutOfLineConstructorStubResolution resolution;

	// Prefer direct node identity when the StructTypeInfo stores the same
	// constructor node instance as the replay-attached declaration.
	for (auto& info_func : struct_info.member_functions) {
		if (!info_func.is_constructor ||
			!info_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}
		auto& info_ctor = info_func.function_decl.as<ConstructorDeclarationNode>();
		if (&info_ctor == &ctor_decl) {
			if (is_candidate_eligible(info_ctor)) {
				resolution.ctor = &info_ctor;
			}
			return resolution;
		}
	}

	for (auto& info_func : struct_info.member_functions) {
		if (!info_func.is_constructor ||
			!info_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}

		auto& info_ctor = info_func.function_decl.as<ConstructorDeclarationNode>();
		if (!is_candidate_eligible(info_ctor) ||
			!constructorDeclarationsHaveEquivalentInstantiatedSignature(
				info_ctor,
				ctor_decl)) {
			continue;
		}

		if (resolution.ctor != nullptr) {
			resolution.ambiguous = true;
			return resolution;
		}
		resolution.ctor = &info_ctor;
	}
	return resolution;
}

struct OutOfLineFunctionStubResolution {
	FunctionDeclarationNode* func = nullptr;
	bool ambiguous = false;
};

template <typename EligibleFn>
static OutOfLineFunctionStubResolution findMatchingFunctionInStructInfo(
	StructTypeInfo& struct_info,
	const FunctionDeclarationNode& func_decl,
	EligibleFn&& is_candidate_eligible) {
	OutOfLineFunctionStubResolution resolution;

	for (auto& info_func : struct_info.member_functions) {
		if (info_func.is_constructor ||
			!info_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		auto& info_decl = info_func.function_decl.as<FunctionDeclarationNode>();
		if (&info_decl == &func_decl) {
			if (is_candidate_eligible(info_decl)) {
				resolution.func = &info_decl;
			}
			return resolution;
		}
	}

	for (auto& info_func : struct_info.member_functions) {
		if (info_func.is_constructor ||
			!info_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}

		auto& info_decl = info_func.function_decl.as<FunctionDeclarationNode>();
		if (!is_candidate_eligible(info_decl) ||
			!functionDeclarationsHaveEquivalentInstantiatedSignature(
				info_decl,
				func_decl)) {
			continue;
		}

		if (resolution.func != nullptr) {
			resolution.ambiguous = true;
			return resolution;
		}
		resolution.func = &info_decl;
	}

	return resolution;
}

static void setOutOfLineConstructorTemplateReplayMetadata(
	ConstructorDeclarationNode& ctor_decl,
	const OutOfLineMemberFunction& out_of_line_member) {
	ctor_decl.set_template_body_position(out_of_line_member.body_start);
	if (out_of_line_member.has_initializer_list) {
		ctor_decl.set_template_initializer_list_position(
			out_of_line_member.initializer_list_start);
	}
}

static StringHandle registerLazyConstructorStub(
	ConstructorDeclarationNode& ctor_decl,
	ASTNode ctor_node,
	StringHandle template_owner_name,
	StringHandle instantiated_name,
	AccessSpecifier access,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	const TemplateEnvironmentSnapshot* outer_parent_snapshot) {
	LazyMemberFunctionInfo lazy_ctor_info;
	{
		auto& id = lazy_ctor_info.identity;
		id.original_member_node = ctor_node;
		id.template_owner_name = template_owner_name;
		id.instantiated_owner_name = instantiated_name;
		id.original_lookup_name = ctor_decl.name();
		id.kind = DeferredMemberIdentity::Kind::Constructor;
		id.is_const_method = false;
	}
	for (const TemplateParameterNode& template_param : outer_template_params) {
		lazy_ctor_info.template_params.push_back(template_param);
	}
	for (const TemplateTypeArg& template_arg : outer_template_args) {
		lazy_ctor_info.template_args.push_back(template_arg);
	}
	lazy_ctor_info.outer_template_environment_snapshot =
		buildTemplateEnvironmentSnapshotFromBindings(
			outer_template_params,
			outer_template_args,
			outer_parent_snapshot);
	lazy_ctor_info.access = access;
	lazy_ctor_info.registry_key = ctor_decl.has_lazy_member_registry_key()
		? ctor_decl.lazy_member_registry_key()
		: StringHandle{};
	StringHandle lazy_registry_key =
		LazyMemberInstantiationRegistry::getInstance().registerLazyMember(
			std::move(lazy_ctor_info));
	if (lazy_registry_key.isValid()) {
		ctor_decl.set_lazy_member_registry_key(lazy_registry_key);
	}
	return lazy_registry_key;
}

bool Parser::static_initializer_requires_replay_metadata(
	const std::optional<ASTNode>& initializer,
	std::span<const TemplateParameterNode> template_params_for_substitution) {
	if (!initializer.has_value()) {
		return false;
	}

	std::unordered_set<StringHandle, StringHandleHash> template_param_names;
	template_param_names.reserve(template_params_for_substitution.size());
	for (const TemplateParameterNode& param : template_params_for_substitution) {
		template_param_names.insert(param.nameHandle());
	}

	const auto matches_template_param_name = [&template_param_names](StringHandle candidate) -> bool {
		return template_param_names.contains(candidate);
	};

	return RebindStaticMemberAst::visitASTUntil(
		*initializer,
		[&matches_template_param_name](const ASTNode& node) -> bool {
			if (!node.is<ExpressionNode>()) {
				return false;
			}

			const ExpressionNode& expr = node.as<ExpressionNode>();
			if (std::holds_alternative<FoldExpressionNode>(expr) ||
				std::holds_alternative<SizeofPackNode>(expr) ||
				std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
				return true;
			}
			if (const auto* id = std::get_if<IdentifierNode>(&expr);
				id != nullptr && matches_template_param_name(id->nameHandle())) {
				return true;
			}
			if (const auto* qualified = std::get_if<QualifiedIdentifierNode>(&expr);
				qualified != nullptr && matches_template_param_name(qualified->nameHandle())) {
				return true;
			}
			if (const auto* call = std::get_if<CallExprNode>(&expr)) {
				return call->has_dependent_qualified_lookup_record() ||
					call->dependent_unqualified_lookup_record().has_value();
			}
			return false;
		});
}

struct DeferredBaseReplayContextScope {
	template <typename DeferredBaseSpecifier>
	DeferredBaseReplayContextScope(
		Parser& parser,
		const DeferredBaseSpecifier& deferred_base,
		StringHandle fallback_current_instantiation_name,
		std::span<const TemplateParameterNode> template_params,
		std::span<const TemplateTypeArg> template_args)
		: parser_(parser),
		  replay_definition_lookup_context_(
			  buildLookupContext(
				  parser,
				  deferred_base,
				  fallback_current_instantiation_name)),
		  lookup_scope_(
			  parser.current_template_definition_lookup_context_,
			  replay_definition_lookup_context_.is_valid()
				  ? &replay_definition_lookup_context_
				  : nullptr),
		  template_params_guard_(parser.current_template_params_),
		  substitutions_guard_(parser.template_param_substitutions_) {
		setTemplateParameters(
			parser_,
			deferred_base,
			template_params);
		TemplateEnvironment deferred_base_environment = buildTemplateEnvironment(
			template_params,
			template_args,
			nullptr);
		parser_.populateTemplateParamSubstitutions(
			parser_.template_param_substitutions_,
			deferred_base_environment);
	}

private:
	template <typename DeferredBaseSpecifier>
	static TemplateDefinitionLookupContext buildLookupContext(
		Parser& parser,
		const DeferredBaseSpecifier& deferred_base,
		StringHandle fallback_current_instantiation_name) {
		TemplateDefinitionLookupContext replay_definition_lookup_context =
			deferred_base.replay_definition_lookup_context;
		if (!replay_definition_lookup_context.is_valid() &&
			deferred_base.replay_position.has_value()) {
			SaveHandle lookup_saved_position = parser.save_token_position();
			parser.restore_lexer_position_only(*deferred_base.replay_position);
			Token replay_definition_token = parser.peek_info();
			parser.restore_token_position(lookup_saved_position);
			parser.discard_saved_token(lookup_saved_position);
			replay_definition_lookup_context = parser.ensureReplayDefinitionLookupContext(
				replay_definition_lookup_context,
				replay_definition_token,
				gSymbolTable.get_current_namespace_handle(),
				fallback_current_instantiation_name);
		}
		return replay_definition_lookup_context;
	}

	template <typename DeferredBaseSpecifier>
	static void setTemplateParameters(
		Parser& parser,
		const DeferredBaseSpecifier& deferred_base,
		std::span<const TemplateParameterNode> template_params) {
		if (deferred_base.replay_template_parameters.hasParameters()) {
			parser.setCurrentTemplateParameters(
				deferred_base.replay_template_parameters.names,
				deferred_base.replay_template_parameters.kinds,
				deferred_base.replay_template_parameters.non_type_categories);
			return;
		}

		InlineVector<StringHandle, 4> replay_param_names;
		InlineVector<TemplateParameterKind, 4> replay_param_kinds;
		InlineVector<TypeCategory, 4> replay_non_type_categories;
		buildTemplateParameterReplayState(
			template_params,
			replay_param_names,
			replay_param_kinds,
			replay_non_type_categories);
		parser.setCurrentTemplateParameters(
			std::move(replay_param_names),
			std::move(replay_param_kinds),
			std::move(replay_non_type_categories));
	}

	Parser& parser_;
	TemplateDefinitionLookupContext replay_definition_lookup_context_;
	Parser::ScopedDefinitionLookupContext lookup_scope_;
	FlashCpp::ScopedState<Parser::ActiveTemplateParameterState> template_params_guard_;
	FlashCpp::ScopedState<InlineVector<Parser::TemplateParamSubstitution, 4>> substitutions_guard_;
};

static void registerAliasTemplateWithOuterBinding(
	StringHandle alias_name,
	const ASTNode& alias_node,
	const OuterTemplateBinding* outer_binding) {
	gTemplateRegistry.register_alias_template(alias_name, alias_node);
	if (outer_binding != nullptr) {
		gTemplateRegistry.registerOuterTemplateBinding(alias_name, *outer_binding);
	}
}

void resetTemplateInstantiationCounters() {
	g_template_inst_iteration_count = 0;
	g_template_inst_iteration_limit_tripped = false;
}

// The std::string_view keys point at parser-owned/interned template parameter names that outlive
// each transient substitution map built during instantiation; these maps mirror the existing
// name-substitution containers used throughout template instantiation, while packs stay keyed by
// StringHandle because pack lookup already operates on interned handles.
using TemplateArgSubstitutionMap = std::unordered_map<std::string_view, TemplateTypeArg>;
using TemplateArgPackSubstitutionMap =
	std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>>;

struct DeferredBasePackBinding {
	StringHandle name;
	std::span<const TemplateTypeArg> args;
};

struct DeferredBasePackExpansionBindingInfo {
	std::vector<DeferredBasePackBinding> pack_bindings;
	size_t expansion_count = 1;
	bool invalid = false;
};

// Build the ordinary template-parameter substitution maps used by deferred base
// instantiation. `transform_scalar_arg` can enrich scalar arguments before they
// are stored in the name map, while variadic packs remain grouped in the pack
// map so callers can expand them later.
template <typename TemplateParamsContainer, typename TemplateArgsContainer, typename TransformScalarArgFn>
static void buildTemplateArgSubstitutionMaps(
	const TemplateParamsContainer& template_params,
	const TemplateArgsContainer& template_args,
	TransformScalarArgFn&& transform_scalar_arg,
	TemplateArgSubstitutionMap& name_substitution_map,
	TemplateArgPackSubstitutionMap& pack_substitution_map,
	std::vector<std::string_view>* template_param_order = nullptr) {
	size_t arg_index = 0;
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
		if (tparam == nullptr) {
			continue;
		}

		std::string_view param_name = tparam->name();
		if (template_param_order != nullptr) {
			template_param_order->push_back(param_name);
		}

		if (tparam->is_variadic()) {
			std::vector<TemplateTypeArg> pack_args;
			for (size_t j = arg_index; j < template_args.size(); ++j) {
				pack_args.push_back(template_args[j]);
			}
			pack_substitution_map[StringTable::getOrInternStringHandle(param_name)] = std::move(pack_args);
			break;
		}

		if (arg_index < template_args.size()) {
			name_substitution_map[param_name] =
				transform_scalar_arg(*tparam, template_args[arg_index]);
			++arg_index;
		}
	}
}

static std::string buildDeferredStaticAssertInstantiationError(
	std::string_view evaluation_error,
	StringHandle message_handle,
	bool include_evaluation_error) {
	std::string error_msg = "static_assert failed during template instantiation";
	if (include_evaluation_error) {
		error_msg += ": ";
		error_msg += std::string(evaluation_error);
	}
	std::string_view message_view = StringTable::getStringView(message_handle);
	if (!message_view.empty()) {
		error_msg += include_evaluation_error ? " - " : ": ";
		error_msg += std::string(message_view);
	}
	return error_msg;
}

template <typename NameSubstitutionMap>
static void populateDeferredStaticAssertEvalTemplateBindings(
	ConstExpr::EvaluationContext& eval_ctx,
	const TemplateEnvironment& template_environment,
	std::span<const std::string_view> template_param_order,
	const NameSubstitutionMap& name_substitution_map) {
	eval_ctx.template_environment = template_environment;
	eval_ctx.template_param_names.assign(
		template_param_order.begin(),
		template_param_order.end());
	eval_ctx.template_args.clear();
	eval_ctx.template_args.reserve(template_param_order.size());
	for (std::string_view param_name : template_param_order) {
		auto arg_it = name_substitution_map.find(param_name);
		if (arg_it != name_substitution_map.end()) {
			eval_ctx.template_args.push_back(arg_it->second);
		}
	}
}

// Collect pack bindings referenced by a deferred base specifier and verify that
// every participating pack expands to the same number of elements.
static DeferredBasePackExpansionBindingInfo collectDeferredBasePackExpansionBindings(
	const DeferredTemplateBaseClassSpecifier& deferred_base,
	const TemplateArgPackSubstitutionMap& pack_substitution_map) {
	DeferredBasePackExpansionBindingInfo binding_info;
	if (!deferred_base.is_pack_expansion) {
		return binding_info;
	}

	auto register_pack = [&](StringHandle pack_name) {
		auto pack_it = pack_substitution_map.find(pack_name);
		if (pack_it == pack_substitution_map.end()) {
			return;
		}
		for (const DeferredBasePackBinding& existing_binding : binding_info.pack_bindings) {
			if (existing_binding.name == pack_name) {
				if (existing_binding.args.size() != pack_it->second.size()) {
					binding_info.invalid = true;
				}
				return;
			}
		}
		if (!binding_info.pack_bindings.empty() &&
			binding_info.expansion_count != pack_it->second.size()) {
			binding_info.invalid = true;
			return;
		}
		binding_info.expansion_count = pack_it->second.size();
		binding_info.pack_bindings.push_back(
			DeferredBasePackBinding{pack_name, std::span<const TemplateTypeArg>(pack_it->second)});
	};

	for (const auto& arg_info : deferred_base.template_arguments) {
		if (arg_info.is_pack) {
			continue;
		}
		if (arg_info.node.is<ExpressionNode>()) {
			const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
			if (const auto* template_parameter_reference =
					std::get_if<TemplateParameterReferenceNode>(&expr)) {
				register_pack(template_parameter_reference->param_name());
			} else if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
				register_pack(StringTable::getOrInternStringHandle(identifier->name()));
			}
		} else if (arg_info.node.is<TypeSpecifierNode>()) {
			TypeIndex idx = arg_info.node.as<TypeSpecifierNode>().type_index();
			if (!idx.is_valid()) {
				continue;
			}
			if (const TypeInfo* idx_ti = tryGetTypeInfo(idx)) {
				register_pack(idx_ti->name_);
			}
		}
	}

	return binding_info;
}

// Create the per-expansion substitution map for one deferred-base expansion by
// overlaying the concrete pack element for `expansion_index` onto the ordinary
// name substitutions.
static TemplateArgSubstitutionMap makeDeferredBaseExpansionSubstitutionMap(
	const TemplateArgSubstitutionMap& base_name_substitution_map,
	const DeferredBasePackExpansionBindingInfo& binding_info,
	size_t expansion_index) {
	TemplateArgSubstitutionMap subst_map = base_name_substitution_map;
	for (const DeferredBasePackBinding& pack_binding : binding_info.pack_bindings) {
		if (expansion_index >= pack_binding.args.size()) {
			throw InternalError("Deferred base pack expansion index out of range");
		}
		subst_map[StringTable::getStringView(pack_binding.name)] =
			pack_binding.args[expansion_index];
	}
	return subst_map;
}

static TypeIndex makeDeferredBaseValueTypeIndex(TypeCategory category, TypeIndex type_index) {
	TypeCategory resolved_category = category;
	if (type_index.is_valid()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			if (type_info->typeEnum() != TypeCategory::Invalid) {
				resolved_category = type_info->typeEnum();
			}
		}
		return type_index.withCategory(resolved_category);
	}
	if (TypeIndex native_index = nativeTypeIndex(resolved_category); native_index.is_valid()) {
		return native_index.withCategory(resolved_category);
	}
	return TypeIndex{0, resolved_category};
}

static bool isPlaceholderNttpTypeIndex(TypeIndex type_index) {
	// An invalid TypeIndex represents an unevaluated/unresolved type and must be
	// treated as a placeholder so the caller falls back to the concrete evaluated type.
	if (!type_index.is_valid()) {
		return true;
	}
	TypeCategory category = type_index.category();
	if (category == TypeCategory::Invalid ||
		category == TypeCategory::Auto ||
		category == TypeCategory::DeclTypeAuto) {
		return true;
	}
	if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
		return type_info->isDependentPlaceholder();
	}
	return false;
}

static TemplateTypeArg makeDeferredBaseValueArg(int64_t value, TypeIndex type_index) {
	return TemplateTypeArg::makeValue(value, type_index);
}

// Resolve a deferred-base type argument through the ordinary substitution map,
// then apply the use-site cv/ref/pointer modifiers carried by the original type
// specifier. This keeps the pattern/member-chain paths on their historical
// qualifier-replacement semantics while sharing the lookup boilerplate. The
// primary deferred-base current-instantiation path uses
// Parser::tryMaterializeDeferredBaseTypeArg(...) instead because that path may
// need full placeholder materialization and must preserve resolved-type
// qualifiers when the rebinding becomes more concrete. Returns std::nullopt when
// the map has no matching type entry for this argument.
static std::optional<TemplateTypeArg> tryResolveDeferredBaseTypeArgFromMap(
	const TypeSpecifierNode& type_spec,
	const TemplateArgSubstitutionMap& name_substitution_map) {
	if (!is_struct_type(type_spec.category()) || !type_spec.type_index().is_valid()) {
		return std::nullopt;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
	if (type_info == nullptr) {
		return std::nullopt;
	}

	auto subst_it = name_substitution_map.find(StringTable::getStringView(type_info->name()));
	if (subst_it == name_substitution_map.end()) {
		return std::nullopt;
	}

	TemplateTypeArg resolved_type_arg = subst_it->second;
	resolved_type_arg.pointer_depth = type_spec.pointer_depth();
	resolved_type_arg.ref_qualifier = type_spec.reference_qualifier();
	resolved_type_arg.cv_qualifier = type_spec.cv_qualifier();
	return resolved_type_arg;
}

template <typename ParamContainer, typename ArgContainer>
static std::vector<TemplateTypeArg> materializeTemplateArgsExpandingPacks(
	const TypeInfo& type_info,
	const ParamContainer& template_params,
	const ArgContainer& template_args) {
	std::vector<TemplateTypeArg> result;
	result.reserve(type_info.templateArgs().size());
	for (const auto& arg_info : type_info.templateArgs()) {
		bool expanded_pack = false;
		if (arg_info.dependent_name.isValid()) {
			const std::string_view pack_name = StringTable::getStringView(arg_info.dependent_name);
			for (size_t i = 0; i < template_params.size(); ++i) {
				const TemplateParameterNode* template_param = tryGetTemplateParameterNode(template_params[i]);
				if (template_param == nullptr ||
					template_param->name() != pack_name ||
					!template_param->is_variadic()) {
					continue;
				}

				for (size_t arg_index = i; arg_index < template_args.size(); ++arg_index) {
					const TemplateTypeArg& substituted_arg = template_args[arg_index];
					TemplateTypeArg concrete_arg = substituted_arg;
					if (!arg_info.is_value && !substituted_arg.is_value) {
						concrete_arg = rebindDependentTemplateTypeArg(substituted_arg, arg_info);
					}
					concrete_arg.is_pack = false;
					result.push_back(concrete_arg);
				}
				expanded_pack = true;
				break;
			}
		}

		if (!expanded_pack) {
			result.push_back(materializeTemplateArg(arg_info, template_params, template_args));
		}
	}
	return result;
}

static const TypeSpecifierNode* getDeclarationParamTypeNode(const ASTNode& param) {
	if (!param.is<DeclarationNode>()) {
		return nullptr;
	}
	const ASTNode& type_node = param.as<DeclarationNode>().type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return nullptr;
	}
	return &type_node.as<TypeSpecifierNode>();
}

static void clampPartialPatternTemplateParamIndirectionForOwner(
	TypeSpecifierNode& substituted_type,
	const TypeSpecifierNode& pattern_type,
	StringHandle pattern_struct_name,
	std::span<const TemplateParameterNode> template_params) {
	if (!pattern_struct_name.isValid() ||
		StringTable::getStringView(pattern_struct_name).find("$pattern_") == std::string_view::npos ||
		substituted_type.pointer_depth() <= pattern_type.pointer_depth()) {
		return;
	}
	std::string_view pattern_type_name = pattern_type.token().value();
	if (pattern_type_name.empty()) {
		if (const TypeInfo* pattern_type_info = tryGetTypeInfo(pattern_type.type_index())) {
			pattern_type_name = StringTable::getStringView(pattern_type_info->name());
		}
	}
	for (const TemplateParameterNode& template_param_node : template_params) {
		if (template_param_node.name() == pattern_type_name) {
			substituted_type.limit_pointer_depth(pattern_type.pointer_depth());
			return;
		}
	}
}

static const TemplateTypeArg* findResolvedTypeTemplateArg(
	const TypeSpecifierNode& type_spec,
	const InlineVector<TemplateParameterNode, 4>& template_params,
	std::span<const TemplateTypeArg> template_args) {
	if (!type_spec.type_index().is_valid()) {
		return nullptr;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
	if (type_info == nullptr) {
		return nullptr;
	}

	const std::string_view type_name = StringTable::getStringView(type_info->name());
	for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
		const TemplateParameterNode& template_param = template_params[i];
		if (template_param.kind() != TemplateParameterKind::Type) {
			continue;
		}
		if (template_param.name() != type_name || template_args[i].is_value) {
			continue;
		}
		return &template_args[i];
	}

	return nullptr;
}

static void applyResolvedTemplateArgTypeMetadata(TypeSpecifierNode& target, const TemplateTypeArg* resolved_arg) {
	if (resolved_arg == nullptr) {
		return;
	}

	for (size_t i = 0; i < resolved_arg->pointer_depth; ++i) {
		CVQualifier cv = i < resolved_arg->pointer_cv_qualifiers.size()
			? resolved_arg->pointer_cv_qualifiers[i]
			: CVQualifier::None;
		target.add_pointer_level(cv);
	}

	target.add_cv_qualifier(resolved_arg->cv_qualifier);
	target.set_reference_qualifier(collapseReferenceQualifiers(
		resolved_arg->ref_qualifier,
		target.reference_qualifier()));

	if (!target.has_function_signature() && resolved_arg->function_signature.has_value()) {
		target.set_function_signature(*resolved_arg->function_signature);
	}

	const int resolved_size_bits = getTypeSpecSizeBits(target);
	if (resolved_size_bits > 0) {
		target.set_size_in_bits(resolved_size_bits);
	}
}

static bool dependentTemplatePlaceholderNamesMatch(
	const TypeSpecifierNode& instantiated_type,
	const TypeSpecifierNode& out_of_line_type,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params);

static bool isOutOfLineConstructorStubName(
	std::string_view member_name,
	std::string_view template_name,
	std::string_view template_base_name) {
	return !template_name.empty() &&
		(member_name == template_name ||
			(!template_base_name.empty() && member_name == template_base_name));
}

static bool typeSpecifierLooksLikeDependentSignaturePlaceholder(
	const TypeSpecifierNode& type_spec) {
	if (type_spec.type() == TypeCategory::Template) {
		return true;
	}
	if (typeSpecStillUsesDependentPlaceholder(type_spec)) {
		return true;
	}
	if (type_spec.type_index().is_valid()) {
		const ResolvedAliasTypeInfo resolved_alias =
			resolveAliasTypeInfo(type_spec.type_index());
		if (resolved_alias.terminal_type_info != nullptr &&
			(resolved_alias.terminal_type_info->isDependentPlaceholder() ||
			 resolved_alias.terminal_type_info->hasDependentQualifiedName())) {
			return true;
		}
	}
	return false;
}

struct SignatureValidationIndirection {
	size_t pointer_depth = 0;
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;
	CVQualifier cv_qualifier = CVQualifier::None;
	InlineVector<CVQualifier, 4> pointer_level_cv_qualifiers;
};

static void mergeAliasIndirectionForSignatureValidation(
	SignatureValidationIndirection& effective,
	const ResolvedAliasTypeInfo& resolved_alias,
	bool shift_top_level_cv_to_alias_pointer_level) {
	if (!resolved_alias.type_index.is_valid()) {
		return;
	}

	const size_t combined_pointer_depth =
		effective.pointer_depth + resolved_alias.pointer_depth;
	const bool should_shift_top_level_cv =
		shift_top_level_cv_to_alias_pointer_level &&
		effective.pointer_depth == 0 &&
		effective.reference_qualifier == ReferenceQualifier::None &&
		resolved_alias.pointer_depth > 0;

	if (should_shift_top_level_cv) {
		effective.pointer_level_cv_qualifiers.push_back(effective.cv_qualifier);
		for (size_t i = 1; i < resolved_alias.pointer_depth; ++i) {
			effective.pointer_level_cv_qualifiers.push_back(CVQualifier::None);
		}
		effective.cv_qualifier = resolved_alias.cv_qualifier;
	} else {
		effective.cv_qualifier = static_cast<CVQualifier>(
			static_cast<uint8_t>(effective.cv_qualifier) |
			static_cast<uint8_t>(resolved_alias.cv_qualifier));
	}

	effective.pointer_depth = combined_pointer_depth;
	effective.reference_qualifier = collapseReferenceQualifiers(
		resolved_alias.reference_qualifier,
		effective.reference_qualifier);
	while (effective.pointer_level_cv_qualifiers.size() < effective.pointer_depth) {
		effective.pointer_level_cv_qualifiers.push_back(CVQualifier::None);
	}
}

static SignatureValidationIndirection computeSignatureValidationIndirection(
	const TypeSpecifierNode& type_spec) {
	SignatureValidationIndirection effective;
	effective.pointer_depth = type_spec.pointer_depth();
	effective.reference_qualifier = type_spec.reference_qualifier();
	effective.cv_qualifier = type_spec.cv_qualifier();
	effective.pointer_level_cv_qualifiers.reserve(type_spec.pointer_levels().size());
	for (const PointerLevel& level : type_spec.pointer_levels()) {
		effective.pointer_level_cv_qualifiers.push_back(level.cv_qualifier);
	}

	if (const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_spec.type_index());
		resolved_alias.type_index.is_valid()) {
		mergeAliasIndirectionForSignatureValidation(
			effective,
			resolved_alias,
			true);
	}

	return effective;
}

static SignatureValidationIndirection computeTemplateArgSignatureValidationIndirection(
	const TypeInfo::TemplateArgInfo& arg_info) {
	SignatureValidationIndirection effective;
	effective.pointer_depth = arg_info.pointer_depth;
	effective.reference_qualifier = arg_info.ref_qualifier;
	effective.cv_qualifier = arg_info.cv_qualifier;
	effective.pointer_level_cv_qualifiers.reserve(arg_info.pointer_cv_qualifiers.size());
	for (CVQualifier level_cv : arg_info.pointer_cv_qualifiers) {
		effective.pointer_level_cv_qualifiers.push_back(level_cv);
	}

	if (const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(arg_info.type_index);
		resolved_alias.type_index.is_valid()) {
		mergeAliasIndirectionForSignatureValidation(
			effective,
			resolved_alias,
			true);
	}

	return effective;
}

static bool typeSpecifiersMatchForSignatureValidation(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs) {
	auto normalizeTypeIndex = [](const TypeSpecifierNode& type_spec) {
		TypeIndex effective = type_spec.type_index().withCategory(type_spec.type());
		if (!effective.is_valid()) {
			return effective;
		}
		ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(effective);
		if (resolved_alias.type_index.is_valid()) {
			return resolved_alias.type_index.withCategory(resolved_alias.typeEnum());
		}
		return effective;
	};

	const TypeIndex lhs_effective_type = normalizeTypeIndex(lhs);
	const TypeIndex rhs_effective_type = normalizeTypeIndex(rhs);
	const SignatureValidationIndirection lhs_effective_indirection =
		computeSignatureValidationIndirection(lhs);
	const SignatureValidationIndirection rhs_effective_indirection =
		computeSignatureValidationIndirection(rhs);
	if (lhs_effective_type.category() != rhs_effective_type.category() ||
		lhs_effective_type != rhs_effective_type ||
		lhs_effective_indirection.pointer_depth != rhs_effective_indirection.pointer_depth ||
		lhs_effective_indirection.reference_qualifier !=
			rhs_effective_indirection.reference_qualifier) {
		return false;
	}

	if (lhs_effective_indirection.pointer_depth > 0) {
		if (lhs_effective_indirection.cv_qualifier !=
			rhs_effective_indirection.cv_qualifier) {
			return false;
		}

		const auto& lhs_levels = lhs_effective_indirection.pointer_level_cv_qualifiers;
		const auto& rhs_levels = rhs_effective_indirection.pointer_level_cv_qualifiers;
		for (size_t i = 0; i < lhs_levels.size() && i < rhs_levels.size(); ++i) {
			if (lhs_levels[i] != rhs_levels[i]) {
				return false;
			}
		}
	}

	if (lhs_effective_indirection.reference_qualifier != ReferenceQualifier::None &&
		lhs_effective_indirection.cv_qualifier != rhs_effective_indirection.cv_qualifier) {
		return false;
	}

	return true;
}

static std::optional<TypeSpecifierNode> substituteOutOfLineSignatureType(
	Parser& parser,
	const TypeSpecifierNode& original_type,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle owner_type_name) {
	ASTNode substituted_type_node = parser.substituteTemplateParameters(
		ASTNode::emplace_node<TypeSpecifierNode>(original_type),
		template_params,
		template_args,
		owner_type_name);
	if (!substituted_type_node.is<TypeSpecifierNode>()) {
		return std::nullopt;
	}
	const TypeSpecifierNode& substituted_type = substituted_type_node.as<TypeSpecifierNode>();
	if (substituted_type.type() == TypeCategory::Template) {
		return std::nullopt;
	}
	if (substituted_type.type_index().is_valid()) {
		if (const TypeInfo* substituted_type_info = tryGetTypeInfo(substituted_type.type_index());
			substituted_type_info != nullptr && substituted_type_info->isDependentPlaceholder()) {
			return std::nullopt;
		}
	}
	return substituted_type;
}

static const TemplateParameterNode* findTemplateParameterByName(
	std::span<const TemplateParameterNode> template_params,
	std::string_view param_name,
	size_t* index_out = nullptr) {
	for (size_t i = 0; i < template_params.size(); ++i) {
		if (template_params[i].name() != param_name) {
			continue;
		}
		if (index_out != nullptr) {
			*index_out = i;
		}
		return &template_params[i];
	}
	return nullptr;
}

static std::string normalizeDependentPlaceholderSignatureSpelling(
	std::string_view spelling) {
	std::string normalized;
	normalized.reserve(spelling.size());

	for (size_t i = 0; i < spelling.size();) {
		char ch = spelling[i];
		if (ch == '$') {
			++i;
			while (i < spelling.size()) {
				if (spelling[i] == ':' &&
					i + 1 < spelling.size() &&
					spelling[i + 1] == ':') {
					break;
				}
				++i;
			}
			continue;
		}
		normalized.push_back(ch);
		++i;
	}

	return normalized;
}

static TypeIndex canonicalizeTemplateSignatureMatchTypeIndex(TypeIndex type_index) {
	if (!type_index.is_valid()) {
		return type_index;
	}
	if (const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_index);
		resolved_alias.type_index.is_valid()) {
		return resolved_alias.type_index.withCategory(resolved_alias.typeEnum());
	}
	return type_index;
}

static TypeIndex canonicalizeTemplateSignatureMatchTypeSpecifierIndex(
	const TypeSpecifierNode& type_spec) {
	const TypeCategory category = type_spec.type_index().is_valid()
		? type_spec.type_index().category()
		: type_spec.type();
	return canonicalizeTemplateSignatureMatchTypeIndex(
		type_spec.type_index().withCategory(category));
}

static bool shouldPreferTokenOwnerTypeInfoForSignatureRecovery(
	const TypeInfo* token_owner_type_info,
	const TypeInfo* index_owner_type_info) {
	if (token_owner_type_info == nullptr) {
		return false;
	}
	if (index_owner_type_info == nullptr) {
		return true;
	}
	if (StringTable::getStringView(token_owner_type_info->name()) !=
		StringTable::getStringView(index_owner_type_info->name())) {
		return true;
	}
	return is_struct_type(token_owner_type_info->typeEnum()) &&
		!is_struct_type(index_owner_type_info->typeEnum());
}

static const TypeInfo* resolveDependentMemberPlaceholderAgainstOwnerArtifact(
	Parser& parser,
	const TypeSpecifierNode& member_source_original_type,
	const TypeSpecifierNode& owner_artifact_substituted_type,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	static constexpr size_t kMemberChainReserve = 4;
	const TypeInfo* member_source_type_info = nullptr;
	if (member_source_original_type.type_index().is_valid()) {
		member_source_type_info = tryGetTypeInfo(member_source_original_type.type_index());
	}
	if (member_source_type_info == nullptr) {
		member_source_type_info = findTypeByName(
			StringTable::getOrInternStringHandle(
				member_source_original_type.token().value()));
	}

	const TypeInfo::DependentQualifiedNameRecord* dependent_record =
		member_source_type_info != nullptr
			? member_source_type_info->dependentQualifiedName()
			: nullptr;

	const TypeInfo* owner_artifact_type_info_from_index = nullptr;
	if (owner_artifact_substituted_type.type_index().is_valid()) {
		owner_artifact_type_info_from_index =
			tryGetTypeInfo(owner_artifact_substituted_type.type_index());
	}
	const TypeInfo* owner_artifact_type_info_from_token = findTypeByName(
		StringTable::getOrInternStringHandle(
			owner_artifact_substituted_type.token().value()));
	const bool prefer_token_owner_type_info =
		shouldPreferTokenOwnerTypeInfoForSignatureRecovery(
			owner_artifact_type_info_from_token,
			owner_artifact_type_info_from_index);
	const TypeInfo* owner_artifact_type_info =
		prefer_token_owner_type_info
			? owner_artifact_type_info_from_token
			: owner_artifact_type_info_from_index;
	if (owner_artifact_type_info == nullptr) {
		return nullptr;
	}

	const TypeIndex owner_artifact_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
		owner_artifact_type_info->registeredTypeIndex().withCategory(owner_artifact_type_info->typeEnum()));
	if (!owner_artifact_canonical.is_valid()) {
		return nullptr;
	}

	InlineVector<StringHandle, 4> member_chain_names;
	member_chain_names.reserve(kMemberChainReserve);

	bool use_dependent_member_chain =
		dependent_record != nullptr &&
		!dependent_record->member_chain.empty();

	if (use_dependent_member_chain) {
		TypeIndex expected_owner_canonical;
		if (dependent_record->owner_type.is_valid()) {
			expected_owner_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
				dependent_record->owner_type);
		}
		if (!expected_owner_canonical.is_valid() &&
			dependent_record->owner_name.isValid()) {
			size_t owner_template_param_index = 0;
			const TemplateParameterNode* owner_template_param = findTemplateParameterByName(
				template_params,
				StringTable::getStringView(dependent_record->owner_name),
				&owner_template_param_index);
			if (owner_template_param != nullptr &&
				owner_template_param->kind() == TemplateParameterKind::Type &&
				owner_template_param_index < template_args.size() &&
				!template_args[owner_template_param_index].is_value) {
				const TemplateTypeArg& owner_template_arg = template_args[owner_template_param_index];
				expected_owner_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
					owner_template_arg.type_index.withCategory(owner_template_arg.typeEnum()));
			}
		}
		if (!expected_owner_canonical.is_valid() &&
			dependent_record->owner_name.isValid()) {
			if (const TypeInfo* named_owner = findTypeByName(dependent_record->owner_name);
				named_owner != nullptr) {
				expected_owner_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
					named_owner->registeredTypeIndex().withCategory(named_owner->typeEnum()));
			}
		}
		if (expected_owner_canonical.is_valid() &&
			expected_owner_canonical != owner_artifact_canonical) {
			use_dependent_member_chain = false;
		}

		if (use_dependent_member_chain) {
			// Keep template-id chain entries here: they are resolved by the strict
			// concrete-owner path below and never fall through to the legacy string
			// lookup path.
			for (const TypeInfo::DependentQualifiedNameRecord::Member& member :
				 dependent_record->member_chain) {
				if (!member.name.isValid()) {
					return nullptr;
				}
				member_chain_names.push_back(member.name);
			}
		}
	}
	if (member_chain_names.empty()) {
		const std::string_view member_name = member_source_original_type.token().value();
		if (member_name.empty()) {
			return nullptr;
		}
		member_chain_names.push_back(StringTable::getOrInternStringHandle(member_name));
	}

	const std::string_view owner_artifact_name =
		StringTable::getStringView(owner_artifact_type_info->name());
	if (owner_artifact_name.empty()) {
		return nullptr;
	}

	if (use_dependent_member_chain) {
		const TypeIndex resolved_member_index =
			resolveDependentMemberTemplatePlaceholderFromConcreteOwner(
				member_source_original_type,
				template_params,
				template_args,
				[&parser](
					std::string_view template_name,
					std::span<const TemplateTypeArg> args,
					bool force_eager) {
					return parser.instantiateClassTemplateForSignatureReplay(
						template_name,
						args,
						force_eager);
				},
				owner_artifact_canonical);
		if (!resolved_member_index.is_valid() ||
			resolved_member_index == owner_artifact_canonical) {
			return nullptr;
		}
		const TypeInfo* resolved_member_type_info = tryGetTypeInfo(resolved_member_index);
		if (resolved_member_type_info == nullptr) {
			return nullptr;
		}
		const TypeIndex resolved_member_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
			resolved_member_type_info->registeredTypeIndex().withCategory(
				resolved_member_type_info->typeEnum()));
		if (!resolved_member_canonical.is_valid()) {
			return nullptr;
		}
		if (const TypeInfo* resolved_canonical_type_info = tryGetTypeInfo(resolved_member_canonical);
			resolved_canonical_type_info != nullptr) {
			return resolved_canonical_type_info;
		}
		return resolved_member_type_info;
	}

	StringBuilder qualified_member_name_builder;
	qualified_member_name_builder.append(owner_artifact_name);
	for (StringHandle member_name : member_chain_names) {
		qualified_member_name_builder.append("::");
		qualified_member_name_builder.append(StringTable::getStringView(member_name));
	}

	const std::string_view qualified_member_name =
		qualified_member_name_builder.commit();
	if (qualified_member_name.empty()) {
		return nullptr;
	}
	const TypeInfo* resolved_member_type_info = findTypeByName(
		StringTable::getOrInternStringHandle(qualified_member_name));
	if (resolved_member_type_info == nullptr) {
		return nullptr;
	}

	const TypeIndex resolved_member_canonical = canonicalizeTemplateSignatureMatchTypeIndex(
		resolved_member_type_info->registeredTypeIndex().withCategory(resolved_member_type_info->typeEnum()));
	if (!resolved_member_canonical.is_valid()) {
		return nullptr;
	}
	if (const TypeInfo* resolved_canonical_type_info = tryGetTypeInfo(resolved_member_canonical);
		resolved_canonical_type_info != nullptr) {
		return resolved_canonical_type_info;
	}
	return resolved_member_type_info;
}

static bool tryMatchDependentMemberPlaceholderOwnerArtifactSubstitution(
	Parser& parser,
	const TypeSpecifierNode& lhs_substituted_type,
	const TypeSpecifierNode& rhs_substituted_type,
	const TypeSpecifierNode& lhs_original_type,
	const TypeSpecifierNode& rhs_original_type,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args) {
	auto attempt_direction =
		[&](
			const TypeSpecifierNode& member_source_original_type,
			const TypeSpecifierNode& owner_artifact_substituted_type,
			const TypeSpecifierNode& concrete_member_substituted_type) {
			const TypeInfo* resolved_member_type_info =
				resolveDependentMemberPlaceholderAgainstOwnerArtifact(
					parser,
					member_source_original_type,
					owner_artifact_substituted_type,
					template_params,
					template_args);
			if (resolved_member_type_info == nullptr) {
				return false;
			}

			TypeSpecifierNode resolved_member_type = member_source_original_type;
			resolved_member_type.set_type_index(
				resolved_member_type_info->registeredTypeIndex().withCategory(
					resolved_member_type_info->typeEnum()));
			if (!typeSpecifiersMatchForSignatureValidation(
					resolved_member_type,
					concrete_member_substituted_type)) {
				return false;
			}

			const TypeIndex resolved_member_canonical =
				canonicalizeTemplateSignatureMatchTypeSpecifierIndex(resolved_member_type);
			const TypeIndex other_side_canonical =
				canonicalizeTemplateSignatureMatchTypeSpecifierIndex(concrete_member_substituted_type);
			return resolved_member_canonical.is_valid() &&
				resolved_member_canonical == other_side_canonical &&
				resolved_member_canonical.category() == other_side_canonical.category();
		};

	auto resolveTypeInfoForSignatureMatch = [](const TypeSpecifierNode& type_spec) -> const TypeInfo* {
		if (type_spec.type_index().is_valid()) {
			if (const TypeInfo* from_index = tryGetTypeInfo(type_spec.type_index());
				from_index != nullptr) {
				return from_index;
			}
		}
		return findTypeByName(
			StringTable::getOrInternStringHandle(type_spec.token().value()));
	};

	const TypeInfo* lhs_substituted_type_info =
		resolveTypeInfoForSignatureMatch(lhs_substituted_type);
	const TypeInfo* rhs_substituted_type_info =
		resolveTypeInfoForSignatureMatch(rhs_substituted_type);

	const bool lhs_is_owner_artifact =
		lhs_substituted_type_info != nullptr &&
		is_struct_type(lhs_substituted_type_info->typeEnum());
	const bool rhs_is_owner_artifact =
		rhs_substituted_type_info != nullptr &&
		is_struct_type(rhs_substituted_type_info->typeEnum());

	const bool lhs_looks_dependent =
		typeSpecifierLooksLikeDependentSignaturePlaceholder(lhs_original_type);
	const bool rhs_looks_dependent =
		typeSpecifierLooksLikeDependentSignaturePlaceholder(rhs_original_type);

	bool matched = false;
	if (lhs_is_owner_artifact && !rhs_is_owner_artifact) {
		if (lhs_looks_dependent) {
			matched = matched || attempt_direction(
				lhs_original_type,
				lhs_substituted_type,
				rhs_substituted_type);
		}
		if (rhs_looks_dependent) {
			matched = matched || attempt_direction(
				rhs_original_type,
				lhs_substituted_type,
				rhs_substituted_type);
		}
	}
	if (rhs_is_owner_artifact && !lhs_is_owner_artifact) {
		if (lhs_looks_dependent) {
			matched = matched || attempt_direction(
				lhs_original_type,
				rhs_substituted_type,
				lhs_substituted_type);
		}
		if (rhs_looks_dependent) {
			matched = matched || attempt_direction(
				rhs_original_type,
				rhs_substituted_type,
				lhs_substituted_type);
		}
	}

	return matched;
}

static bool dependentQualifiedTemplateArgMatchesForSignature(
	const TypeInfo::TemplateArgInfo& instantiated_arg,
	const TypeInfo::TemplateArgInfo& out_of_line_arg,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params) {
	const SignatureValidationIndirection instantiated_indirection =
		computeTemplateArgSignatureValidationIndirection(instantiated_arg);
	const SignatureValidationIndirection out_of_line_indirection =
		computeTemplateArgSignatureValidationIndirection(out_of_line_arg);

	if (instantiated_arg.is_value != out_of_line_arg.is_value ||
		instantiated_arg.is_pack != out_of_line_arg.is_pack ||
		instantiated_arg.is_array != out_of_line_arg.is_array ||
		instantiated_indirection.pointer_depth != out_of_line_indirection.pointer_depth ||
		instantiated_indirection.cv_qualifier != out_of_line_indirection.cv_qualifier ||
		instantiated_indirection.reference_qualifier != out_of_line_indirection.reference_qualifier ||
		instantiated_arg.is_template_template_arg != out_of_line_arg.is_template_template_arg ||
		instantiated_arg.template_name != out_of_line_arg.template_name ||
		instantiated_arg.member_pointer_kind != out_of_line_arg.member_pointer_kind ||
		instantiated_arg.member_class_name != out_of_line_arg.member_class_name ||
		instantiated_indirection.pointer_level_cv_qualifiers.size() !=
			out_of_line_indirection.pointer_level_cv_qualifiers.size() ||
		instantiated_arg.array_dimensions.size() != out_of_line_arg.array_dimensions.size()) {
		return false;
	}

	for (size_t i = 0; i < instantiated_indirection.pointer_level_cv_qualifiers.size(); ++i) {
		if (instantiated_indirection.pointer_level_cv_qualifiers[i] !=
			out_of_line_indirection.pointer_level_cv_qualifiers[i]) {
			return false;
		}
	}
	for (size_t i = 0; i < instantiated_arg.array_dimensions.size(); ++i) {
		if (instantiated_arg.array_dimensions[i] != out_of_line_arg.array_dimensions[i]) {
			return false;
		}
	}

	auto dependent_param_names_match = [&](StringHandle instantiated_name,
										 StringHandle out_of_line_name) {
		if (instantiated_name == out_of_line_name) {
			return true;
		}
		if (!instantiated_name.isValid() || !out_of_line_name.isValid()) {
			return false;
		}
		size_t instantiated_index = 0;
		size_t out_of_line_index = 0;
		const TemplateParameterNode* instantiated_param = findTemplateParameterByName(
			instantiated_template_params,
			StringTable::getStringView(instantiated_name),
			&instantiated_index);
		const TemplateParameterNode* out_of_line_param = findTemplateParameterByName(
			out_of_line_template_params,
			StringTable::getStringView(out_of_line_name),
			&out_of_line_index);
		if (instantiated_param == nullptr || out_of_line_param == nullptr) {
			return false;
		}
		return instantiated_index == out_of_line_index &&
			instantiated_param->kind() == out_of_line_param->kind() &&
			instantiated_param->is_variadic() == out_of_line_param->is_variadic();
	};

	if (!dependent_param_names_match(
			instantiated_arg.dependent_name,
			out_of_line_arg.dependent_name)) {
		return false;
	}

	// When both sides carry a valid dependent_name (i.e. they refer to a template
	// parameter by name), the dependent_name check above is the authoritative
	// comparison.  TypeIndex values for template parameters are context-specific
	// and can differ between the in-class declaration context and each OOL
	// definition context — do NOT reject the match based on those indices.
	const bool has_dependent_name =
		instantiated_arg.dependent_name.isValid() && out_of_line_arg.dependent_name.isValid();

	TypeIndex instantiated_type_index =
		canonicalizeTemplateSignatureMatchTypeIndex(instantiated_arg.type_index);
	TypeIndex out_of_line_type_index =
		canonicalizeTemplateSignatureMatchTypeIndex(out_of_line_arg.type_index);
	if (!has_dependent_name &&
		(instantiated_type_index != out_of_line_type_index ||
		 instantiated_type_index.category() != out_of_line_type_index.category())) {
		return false;
	}

	if (instantiated_arg.function_signature.has_value() !=
		out_of_line_arg.function_signature.has_value()) {
		return false;
	}
	if (instantiated_arg.function_signature.has_value() &&
		!FlashCpp::equalFunctionSignatureIdentity(
			*instantiated_arg.function_signature,
			*out_of_line_arg.function_signature)) {
		return false;
	}

	if (instantiated_arg.dependent_expr.has_value() !=
		out_of_line_arg.dependent_expr.has_value()) {
		return false;
	}
	if (instantiated_arg.dependent_expr.has_value() &&
		!FlashCpp::equalDependentExpressionIdentity(
			*instantiated_arg.dependent_expr,
			*out_of_line_arg.dependent_expr)) {
		return false;
	}

	if (instantiated_arg.is_value) {
		if (instantiated_arg.value != out_of_line_arg.value ||
			instantiated_arg.nttp_kind != out_of_line_arg.nttp_kind ||
			instantiated_arg.nttp_entity_name != out_of_line_arg.nttp_entity_name ||
			instantiated_arg.nttp_member_name != out_of_line_arg.nttp_member_name ||
			instantiated_arg.nttp_pointer_offset != out_of_line_arg.nttp_pointer_offset) {
			return false;
		}
	}

	return true;
}

static bool dependentQualifiedNameRecordsMatchForSignature(
	const TypeInfo::DependentQualifiedNameRecord& instantiated_record,
	const TypeInfo::DependentQualifiedNameRecord& out_of_line_record,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params) {
	using OwnerKind = TypeInfo::DependentQualifiedNameRecord::OwnerKind;
	// DependentInstantiation and UnknownSpecialization both represent a
	// dependent template instantiation; they differ only due to parse context
	// at which the type was recorded.  For OOL-signature matching purposes they
	// are equivalent and should be accepted as the same kind.
	auto owner_kind_compatible = [](OwnerKind a, OwnerKind b) -> bool {
		if (a == b) return true;
		auto is_dep_or_unknown = [](OwnerKind k) {
			return k == OwnerKind::DependentInstantiation ||
			       k == OwnerKind::UnknownSpecialization;
		};
		return is_dep_or_unknown(a) && is_dep_or_unknown(b);
	};
	if (!owner_kind_compatible(instantiated_record.owner_kind, out_of_line_record.owner_kind) ||
		instantiated_record.names_current_instantiation !=
			out_of_line_record.names_current_instantiation ||
		instantiated_record.member_chain.size() != out_of_line_record.member_chain.size() ||
		instantiated_record.owner_template_arguments.size() !=
			out_of_line_record.owner_template_arguments.size()) {
		return false;
	}

	if (instantiated_record.owner_type.is_valid() ||
		out_of_line_record.owner_type.is_valid()) {
		TypeIndex instantiated_owner =
			canonicalizeTemplateSignatureMatchTypeIndex(instantiated_record.owner_type);
		TypeIndex out_of_line_owner =
			canonicalizeTemplateSignatureMatchTypeIndex(out_of_line_record.owner_type);
		if (instantiated_owner != out_of_line_owner ||
			instantiated_owner.category() != out_of_line_owner.category()) {
			return false;
		}
	}

	if (instantiated_record.owner_name != out_of_line_record.owner_name) {
		size_t instantiated_owner_index = 0;
		size_t out_of_line_owner_index = 0;
		const TemplateParameterNode* instantiated_owner_param = findTemplateParameterByName(
			instantiated_template_params,
			StringTable::getStringView(instantiated_record.owner_name),
			&instantiated_owner_index);
		const TemplateParameterNode* out_of_line_owner_param = findTemplateParameterByName(
			out_of_line_template_params,
			StringTable::getStringView(out_of_line_record.owner_name),
			&out_of_line_owner_index);
		if (instantiated_owner_param == nullptr ||
			out_of_line_owner_param == nullptr ||
			instantiated_owner_index != out_of_line_owner_index ||
			instantiated_owner_param->kind() != out_of_line_owner_param->kind() ||
			instantiated_owner_param->is_variadic() != out_of_line_owner_param->is_variadic()) {
			return false;
		}
	}

	for (size_t arg_index = 0;
		 arg_index < instantiated_record.owner_template_arguments.size();
		 ++arg_index) {
		if (!dependentQualifiedTemplateArgMatchesForSignature(
				instantiated_record.owner_template_arguments[arg_index],
				out_of_line_record.owner_template_arguments[arg_index],
				instantiated_template_params,
				out_of_line_template_params)) {
			return false;
		}
	}

	for (size_t member_index = 0;
		 member_index < instantiated_record.member_chain.size();
		 ++member_index) {
		const auto& instantiated_member = instantiated_record.member_chain[member_index];
		const auto& out_of_line_member = out_of_line_record.member_chain[member_index];
		if (instantiated_member.name != out_of_line_member.name ||
			instantiated_member.has_template_arguments !=
				out_of_line_member.has_template_arguments ||
			instantiated_member.has_template_keyword !=
				out_of_line_member.has_template_keyword ||
			instantiated_member.template_arguments.size() !=
				out_of_line_member.template_arguments.size()) {
			return false;
		}

		for (size_t arg_index = 0;
			 arg_index < instantiated_member.template_arguments.size();
			 ++arg_index) {
			if (!dependentQualifiedTemplateArgMatchesForSignature(
					instantiated_member.template_arguments[arg_index],
					out_of_line_member.template_arguments[arg_index],
					instantiated_template_params,
					out_of_line_template_params)) {
				return false;
			}
		}
	}

	return true;
}

static const TypeInfo::DependentQualifiedNameRecord* dependentQualifiedNameRecordForSignatureMatch(
	const TypeInfo* type_info) {
	if (type_info == nullptr) {
		return nullptr;
	}
	if (type_info->hasDependentQualifiedName()) {
		return type_info->dependentQualifiedName();
	}
	ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(
		type_info->registeredTypeIndex().withCategory(type_info->typeEnum()));
	if (resolved_alias.terminal_type_info != nullptr &&
		resolved_alias.terminal_type_info->hasDependentQualifiedName()) {
		return resolved_alias.terminal_type_info->dependentQualifiedName();
	}
	return nullptr;
}

static bool dependentQualifiedNameHasMemberTemplateSegment(
	const TypeInfo::DependentQualifiedNameRecord* dependent_record) {
	if (dependent_record == nullptr) {
		return false;
	}
	for (const auto& member : dependent_record->member_chain) {
		if (member.has_template_arguments) {
			return true;
		}
	}
	return false;
}

static bool typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
	const TypeSpecifierNode& type_spec) {
	const TypeInfo* type_info = type_spec.type_index().is_valid()
		? tryGetTypeInfo(type_spec.type_index())
		: nullptr;
	return dependentQualifiedNameHasMemberTemplateSegment(
		dependentQualifiedNameRecordForSignatureMatch(type_info));
}

static bool typeIndexPreservesDependentMemberTemplateSignatureIdentity(
	TypeIndex type_index) {
	const TypeInfo* type_info = type_index.is_valid()
		? tryGetTypeInfo(type_index)
		: nullptr;
	return dependentQualifiedNameHasMemberTemplateSegment(
		dependentQualifiedNameRecordForSignatureMatch(type_info));
}

static bool dependentTemplatePlaceholderNamesMatch(
	const TypeSpecifierNode& instantiated_type,
	const TypeSpecifierNode& out_of_line_type,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params) {
	auto identity_name = [](const TypeSpecifierNode& type_spec) {
		if (type_spec.type_index().is_valid()) {
			if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
				std::string_view type_name = StringTable::getStringView(type_info->name());
				if (!type_name.empty()) {
					return type_name;
				}
			}
		}
		return type_spec.token().value();
	};

	const std::string_view instantiated_name = identity_name(instantiated_type);
	const std::string_view out_of_line_name = identity_name(out_of_line_type);

	const TypeInfo* instantiated_type_info = instantiated_type.type_index().is_valid()
		? tryGetTypeInfo(instantiated_type.type_index())
		: findTypeByName(StringTable::getOrInternStringHandle(instantiated_name));
	const TypeInfo* out_of_line_type_info = out_of_line_type.type_index().is_valid()
		? tryGetTypeInfo(out_of_line_type.type_index())
		: findTypeByName(StringTable::getOrInternStringHandle(out_of_line_name));

	const TypeInfo::DependentQualifiedNameRecord* instantiated_record =
		dependentQualifiedNameRecordForSignatureMatch(instantiated_type_info);
	const TypeInfo::DependentQualifiedNameRecord* out_of_line_record =
		dependentQualifiedNameRecordForSignatureMatch(out_of_line_type_info);
	if (instantiated_record != nullptr &&
		out_of_line_record != nullptr) {
		return dependentQualifiedNameRecordsMatchForSignature(
			*instantiated_record,
			*out_of_line_record,
			instantiated_template_params,
			out_of_line_template_params);
	}

	if (instantiated_name == out_of_line_name) {
		const std::string_view instantiated_token_name =
			instantiated_type.token().value();
		const std::string_view out_of_line_token_name =
			out_of_line_type.token().value();
		if (instantiated_token_name == out_of_line_token_name ||
			instantiated_token_name.empty() ||
			out_of_line_token_name.empty()) {
			return true;
		}
		size_t instantiated_token_index = 0;
		size_t out_of_line_token_index = 0;
		const TemplateParameterNode* instantiated_token_param =
			findTemplateParameterByName(
				instantiated_template_params,
				instantiated_token_name,
				&instantiated_token_index);
		const TemplateParameterNode* out_of_line_token_param =
			findTemplateParameterByName(
				out_of_line_template_params,
				out_of_line_token_name,
				&out_of_line_token_index);
		return instantiated_token_param != nullptr &&
			out_of_line_token_param != nullptr &&
			instantiated_token_index == out_of_line_token_index &&
			instantiated_token_param->kind() == out_of_line_token_param->kind() &&
			instantiated_token_param->is_variadic() ==
				out_of_line_token_param->is_variadic();
	}

	auto owner_matches_named_placeholder =
		[](const TypeInfo::DependentQualifiedNameRecord& dependent_record,
		   const TypeInfo* owner_candidate_type_info,
		   std::string_view owner_candidate_name) {
			const std::string_view dependent_owner_name =
				StringTable::getStringView(dependent_record.owner_name);
			if (!dependent_owner_name.empty() &&
				owner_candidate_name == dependent_owner_name) {
				return true;
			}
			if (owner_candidate_type_info != nullptr) {
				const std::string_view owner_candidate_type_name =
					StringTable::getStringView(owner_candidate_type_info->name());
				if (!owner_candidate_type_name.empty() &&
					owner_candidate_type_name == dependent_owner_name) {
					return true;
				}
			}
			return false;
		};

	if (instantiated_type_info != nullptr &&
		instantiated_type_info->isDependentPlaceholder()) {
		if (const auto* instantiated_placeholder_record =
				instantiated_type_info->dependentQualifiedName();
			instantiated_placeholder_record != nullptr &&
			owner_matches_named_placeholder(
				*instantiated_placeholder_record,
				out_of_line_type_info,
				out_of_line_name)) {
			return true;
		}
	}
	if (out_of_line_type_info != nullptr &&
		out_of_line_type_info->isDependentPlaceholder()) {
		if (const auto* out_of_line_placeholder_record =
				out_of_line_type_info->dependentQualifiedName();
			out_of_line_placeholder_record != nullptr &&
			owner_matches_named_placeholder(
				*out_of_line_placeholder_record,
				instantiated_type_info,
				instantiated_name)) {
			return true;
		}
	}

	if (normalizeDependentPlaceholderSignatureSpelling(instantiated_name) ==
		normalizeDependentPlaceholderSignatureSpelling(out_of_line_name)) {
		return true;
	}

	size_t instantiated_index = 0;
	size_t out_of_line_index = 0;
	const TemplateParameterNode* instantiated_param = findTemplateParameterByName(
		instantiated_template_params,
		instantiated_name,
		&instantiated_index);
	const TemplateParameterNode* out_of_line_param = findTemplateParameterByName(
		out_of_line_template_params,
		out_of_line_name,
		&out_of_line_index);
	return instantiated_param != nullptr &&
		out_of_line_param != nullptr &&
		instantiated_index == out_of_line_index &&
		instantiated_param->kind() == out_of_line_param->kind() &&
		instantiated_param->is_variadic() == out_of_line_param->is_variadic();
}

static bool shouldAcceptReplaySubstitutedSignatureMatch(
	const TypeSpecifierNode& substituted_instantiated_type,
	const TypeSpecifierNode& substituted_out_of_line_type,
	const TypeSpecifierNode& original_instantiated_type,
	const TypeSpecifierNode& original_out_of_line_type,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params) {
	if (!typeSpecifiersMatchForSignatureValidation(
			substituted_instantiated_type,
			substituted_out_of_line_type)) {
		return false;
	}

	const bool instantiated_preserves_member_template_identity =
		typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
			original_instantiated_type);
	const bool out_of_line_preserves_member_template_identity =
		typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
			original_out_of_line_type);
	if (instantiated_preserves_member_template_identity ||
		out_of_line_preserves_member_template_identity) {
		if (!instantiated_preserves_member_template_identity ||
			!out_of_line_preserves_member_template_identity) {
			return false;
		}
		return dependentTemplatePlaceholderNamesMatch(
			original_instantiated_type,
			original_out_of_line_type,
			instantiated_template_params,
			out_of_line_template_params);
	}

	if (!typeSpecifierLooksLikeDependentSignaturePlaceholder(original_instantiated_type) ||
		!typeSpecifierLooksLikeDependentSignaturePlaceholder(original_out_of_line_type)) {
		return true;
	}

	return dependentTemplatePlaceholderNamesMatch(
		original_instantiated_type,
		original_out_of_line_type,
		instantiated_template_params,
		out_of_line_template_params);
}

static void copyDefinitionParameterTypesForDependentMemberTemplateSegments(
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params) {
	if (instantiated_params.size() != definition_params.size()) {
		return;
	}

	for (size_t param_index = 0; param_index < instantiated_params.size(); ++param_index) {
		DeclarationNode* instantiated_decl =
			const_cast<DeclarationNode*>(
				get_decl_from_symbol(instantiated_params[param_index]));
		const DeclarationNode* definition_decl =
			get_decl_from_symbol(definition_params[param_index]);
		if (instantiated_decl == nullptr || definition_decl == nullptr) {
			continue;
		}

		const TypeSpecifierNode& definition_type =
			definition_decl->type_specifier_node();
		const TypeInfo* definition_type_info =
			tryGetTypeInfo(definition_type.type_index());
		if (definition_type_info == nullptr) {
			continue;
		}
		const TypeInfo::DependentQualifiedNameRecord* dependent_record =
			dependentQualifiedNameRecordForSignatureMatch(definition_type_info);
		if (dependent_record == nullptr) {
			continue;
		}
		for (const auto& member : dependent_record->member_chain) {
			if (!member.has_template_arguments) {
				continue;
			}
			instantiated_decl->set_type_node(definition_decl->type_specifier_node());
			break;
		}
	}
}

static void syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
	Parser& parser,
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args) {
	(void)parser;
	(void)outer_template_params;
	(void)outer_template_args;
	if (instantiated_params.size() != definition_params.size()) {
		return;
	}

	for (size_t param_index = 0; param_index < instantiated_params.size(); ++param_index) {
		DeclarationNode* instantiated_decl =
			const_cast<DeclarationNode*>(
				get_decl_from_symbol(instantiated_params[param_index]));
		const DeclarationNode* definition_decl =
			get_decl_from_symbol(definition_params[param_index]);
		if (instantiated_decl == nullptr || definition_decl == nullptr) {
			continue;
		}

		const TypeSpecifierNode& definition_type =
			definition_decl->type_specifier_node();
		const TypeInfo* definition_type_info =
			tryGetTypeInfo(definition_type.type_index());
		if (definition_type_info == nullptr) {
			continue;
		}

		const TypeInfo::DependentQualifiedNameRecord* dependent_record =
			dependentQualifiedNameRecordForSignatureMatch(definition_type_info);
		const bool has_member_template_segment =
			dependentQualifiedNameHasMemberTemplateSegment(dependent_record);
		if (!has_member_template_segment) {
			continue;
		}
		instantiated_decl->set_type_node(definition_type);
	}
}

static void preserveDependentMemberTemplateParameterPlaceholdersFromPattern(
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> pattern_params) {
	if (instantiated_params.size() != pattern_params.size()) {
		return;
	}

	for (size_t param_index = 0; param_index < instantiated_params.size(); ++param_index) {
		DeclarationNode* instantiated_decl =
			const_cast<DeclarationNode*>(
				get_decl_from_symbol(instantiated_params[param_index]));
		const DeclarationNode* pattern_decl =
			get_decl_from_symbol(pattern_params[param_index]);
		if (instantiated_decl == nullptr || pattern_decl == nullptr) {
			continue;
		}

		const TypeSpecifierNode& pattern_type =
			pattern_decl->type_specifier_node();
		const TypeSpecifierNode& instantiated_type =
			instantiated_decl->type_specifier_node();
		if (!typeSpecStillUsesDependentPlaceholder(instantiated_type)) {
			continue;
		}
		const TypeInfo* pattern_type_info =
			tryGetTypeInfo(pattern_type.type_index());
		if (pattern_type_info == nullptr) {
			continue;
		}
		const TypeInfo::DependentQualifiedNameRecord* dependent_record =
			dependentQualifiedNameRecordForSignatureMatch(pattern_type_info);
		if (dependent_record == nullptr) {
			continue;
		}
		const bool has_member_template_segment =
			dependentQualifiedNameHasMemberTemplateSegment(dependent_record);
		if (!has_member_template_segment) {
			continue;
		}

		instantiated_decl->set_type_node(pattern_decl->type_specifier_node());
	}
}

template <typename InstantiatedDeclNode, typename OutOfLineDeclNode>
static ReplaySignatureMatchResult declarationsMatchAfterTemplateSubstitution(
	Parser& parser,
	const InstantiatedDeclNode& instantiated_decl,
	const OutOfLineDeclNode& out_of_line_decl,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params) {
	if (instantiated_decl.parameter_nodes().size() != out_of_line_decl.parameter_nodes().size()) {
		return ReplaySignatureMatchResult::Mismatch;
	}

	for (size_t i = 0; i < instantiated_decl.parameter_nodes().size(); ++i) {
		const TypeSpecifierNode* instantiated_param =
			getDeclarationParamTypeNode(instantiated_decl.parameter_nodes()[i]);
		const TypeSpecifierNode* out_of_line_param =
			getDeclarationParamTypeNode(out_of_line_decl.parameter_nodes()[i]);
		if (instantiated_param == nullptr || out_of_line_param == nullptr) {
			return ReplaySignatureMatchResult::InsufficientEvidence;
		}

		const bool instantiated_is_template_param_placeholder =
			findTemplateParameterByName(
				instantiated_template_params,
				instantiated_param->token().value()) != nullptr;
		const bool out_of_line_is_template_param_placeholder =
			findTemplateParameterByName(
				out_of_line_template_params,
				out_of_line_param->token().value()) != nullptr;

		std::optional<TypeSpecifierNode> substituted_instantiated_param;
		if (!instantiated_is_template_param_placeholder) {
			substituted_instantiated_param = substituteOutOfLineSignatureType(
				parser,
				*instantiated_param,
				template_params,
				template_args,
				owner_type_name);
		}

		std::optional<TypeSpecifierNode> substituted_out_of_line_param;
		if (!out_of_line_is_template_param_placeholder) {
			substituted_out_of_line_param = substituteOutOfLineSignatureType(
				parser,
				*out_of_line_param,
				template_params,
				template_args,
				owner_type_name);
		}
		if (substituted_instantiated_param.has_value() ||
			substituted_out_of_line_param.has_value()) {
			const TypeSpecifierNode& lhs_type =
				substituted_instantiated_param.has_value()
					? *substituted_instantiated_param
					: *instantiated_param;
			const TypeSpecifierNode& rhs_type =
				substituted_out_of_line_param.has_value()
					? *substituted_out_of_line_param
					: *out_of_line_param;
			if (shouldAcceptReplaySubstitutedSignatureMatch(
					lhs_type,
					rhs_type,
					*instantiated_param,
					*out_of_line_param,
					instantiated_template_params,
					out_of_line_template_params)) {
				continue;
			}

			if (tryMatchDependentMemberPlaceholderOwnerArtifactSubstitution(
					parser,
					lhs_type,
					rhs_type,
					*instantiated_param,
					*out_of_line_param,
					template_params,
					template_args)) {
				continue;
			}

			// One-sided substitutions can produce owner-only artifacts for dependent
			// qualified names (e.g. `typename T::template AddPtr<int>::type`
			// materializing as `T` when member lookup is deferred). In that case,
			// fall through to the dependent-placeholder structural checks below
			// instead of rejecting immediately.
			if (!typeSpecifierLooksLikeDependentSignaturePlaceholder(*instantiated_param) &&
				!typeSpecifierLooksLikeDependentSignaturePlaceholder(*out_of_line_param)) {
				return ReplaySignatureMatchResult::Mismatch;
			}
		}

		if (!instantiated_is_template_param_placeholder &&
			!out_of_line_is_template_param_placeholder &&
			!typeSpecifierLooksLikeDependentSignaturePlaceholder(*instantiated_param) &&
			!typeSpecifierLooksLikeDependentSignaturePlaceholder(*out_of_line_param)) {
			if (!typeSpecifiersMatchForSignatureValidation(
					*instantiated_param,
					*out_of_line_param)) {
				return ReplaySignatureMatchResult::Mismatch;
			}
			continue;
		}
		const SignatureValidationIndirection instantiated_effective_indirection =
			computeSignatureValidationIndirection(*instantiated_param);
		const SignatureValidationIndirection out_of_line_effective_indirection =
			computeSignatureValidationIndirection(*out_of_line_param);
		if (instantiated_effective_indirection.pointer_depth !=
				out_of_line_effective_indirection.pointer_depth ||
			instantiated_effective_indirection.reference_qualifier !=
				out_of_line_effective_indirection.reference_qualifier ||
			instantiated_effective_indirection.cv_qualifier !=
				out_of_line_effective_indirection.cv_qualifier) {
			return ReplaySignatureMatchResult::Mismatch;
		}
		if (instantiated_effective_indirection.pointer_level_cv_qualifiers.size() !=
			out_of_line_effective_indirection.pointer_level_cv_qualifiers.size()) {
			return ReplaySignatureMatchResult::Mismatch;
		}
		for (size_t pointer_level_index = 0;
			 pointer_level_index <
			 instantiated_effective_indirection.pointer_level_cv_qualifiers.size();
			 ++pointer_level_index) {
			if (instantiated_effective_indirection.pointer_level_cv_qualifiers[pointer_level_index] !=
				out_of_line_effective_indirection.pointer_level_cv_qualifiers[pointer_level_index]) {
				return ReplaySignatureMatchResult::Mismatch;
			}
		}
		const bool dependent_placeholder_name_match = dependentTemplatePlaceholderNamesMatch(
			*instantiated_param,
			*out_of_line_param,
			instantiated_template_params,
			out_of_line_template_params);
		if (!dependent_placeholder_name_match) {
			return ReplaySignatureMatchResult::Mismatch;
		}
	}

	return ReplaySignatureMatchResult::Match;
}

static bool isMatchingMemberTemplate(
	const StructMemberFunctionDecl& member,
	std::string_view ool_func_name,
	size_t ool_inner_template_param_count,
	size_t ool_function_param_count) {
	if (!member.function_declaration.is<TemplateFunctionDeclarationNode>()) {
		return false;
	}
	const auto& template_func =
		member.function_declaration.as<TemplateFunctionDeclarationNode>();
	const FunctionDeclarationNode* func_decl =
		get_function_decl_node(member.function_declaration);
	if (func_decl == nullptr) {
		return false;
	}
	return func_decl->decl_node().identifier_token().value() == ool_func_name &&
		template_func.template_parameters().size() == ool_inner_template_param_count &&
		func_decl->parameter_nodes().size() == ool_function_param_count;
}

static ReplaySignatureMatchResult nestedOutOfLineMemberTemplateMatchesCandidate(
	Parser& parser,
	const ASTNode& candidate_member,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params) {
	if (!candidate_member.is<TemplateFunctionDeclarationNode>()) {
		return ReplaySignatureMatchResult::Mismatch;
	}

	const TemplateFunctionDeclarationNode& candidate_template =
		candidate_member.as<TemplateFunctionDeclarationNode>();
	std::span<const TemplateParameterNode> candidate_inner_template_params(
		candidate_template.template_parameters().data(),
		candidate_template.template_parameters().size());

	if (candidate_inner_template_params.size() != out_of_line_inner_template_params.size()) {
		return ReplaySignatureMatchResult::Mismatch;
	}

	ReplaySignatureMatchResult signature_match = declarationsMatchAfterTemplateSubstitution(
		parser,
		candidate_template.function_decl_node(),
		out_of_line_decl,
		outer_template_params,
		outer_template_args,
		owner_type_name,
		candidate_inner_template_params,
		out_of_line_inner_template_params);
	return signature_match;
}

static ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params) {
	if (candidate_inner_template_params.size() != out_of_line_inner_template_params.size()) {
		return ReplaySignatureMatchResult::Mismatch;
	}

	return declarationsMatchAfterTemplateSubstitution(
		parser,
		candidate,
		out_of_line_decl,
		outer_template_params,
		outer_template_args,
		owner_type_name,
		candidate_inner_template_params,
		out_of_line_inner_template_params);
}

static ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const ConstructorDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params) {
	if (candidate_inner_template_params.size() != out_of_line_inner_template_params.size()) {
		return ReplaySignatureMatchResult::Mismatch;
	}

	return declarationsMatchAfterTemplateSubstitution(
		parser,
		candidate,
		out_of_line_decl,
		outer_template_params,
		outer_template_args,
		owner_type_name,
		candidate_inner_template_params,
		out_of_line_inner_template_params);
}

void Parser::copyDefinitionParameterIdentifiers(
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params) {
	if (instantiated_params.size() != definition_params.size()) {
		return;
	}

	for (size_t param_index = 0; param_index < instantiated_params.size(); ++param_index) {
		DeclarationNode* instantiated_decl = Parser::get_decl_from_symbol_mut(instantiated_params[param_index]);
		const DeclarationNode* definition_decl = get_decl_from_symbol(definition_params[param_index]);
		if (!instantiated_decl || !definition_decl) {
			continue;
		}
		if (!definition_decl->identifier_token().value().empty()) {
			instantiated_decl->set_identifier_token(definition_decl->identifier_token());
		}
	}
}

void Parser::copyDefinitionParameterTypes(
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params) {
	if (instantiated_params.size() != definition_params.size()) {
		return;
	}

	for (size_t param_index = 0; param_index < instantiated_params.size(); ++param_index) {
		DeclarationNode* instantiated_decl = Parser::get_decl_from_symbol_mut(instantiated_params[param_index]);
		const DeclarationNode* definition_decl = get_decl_from_symbol(definition_params[param_index]);
		if (!instantiated_decl || !definition_decl) {
			continue;
		}
		instantiated_decl->set_type_node(definition_decl->type_specifier_node());
	}
}

void Parser::syncOutOfLineConstructorTemplateParameters(
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params) {
	// Keep the declaration-side type nodes untouched so inner template-parameter bindings
	// remain aligned even when the out-of-line definition renames those template parameters;
	// only identifiers need to follow the definition spelling for body replay.
	copyDefinitionParameterIdentifiers(instantiated_params, definition_params);
}

bool Parser::replayOutOfLineMemberBody(
	FunctionDeclarationNode& inst_func,
	StructDeclarationNode* instantiated_struct_decl,
	StringHandle instantiated_name,
	TypeIndex instantiated_type_index,
	std::span<const ASTNode> definition_scope_params,
	SaveHandle body_start,
	const TemplateDefinitionLookupContext& recorded_lookup_context,
	const Token& declaration_token,
	std::span<const TemplateParameterNode> substitution_template_params,
	std::span<const TemplateTypeArg> substitution_template_args,
	std::string_view log_context,
	std::string_view function_name) {
	SaveHandle saved_pos = save_token_position();
	// body_start belongs to the stored OOL registration metadata and may be reused
	// by other replay paths; only the temporary saved_pos is restored+discarded here.
	restore_lexer_position_only(body_start);

	bool parsed_and_substituted = false;
	{
		TemplateDefinitionLookupContext definition_lookup_context =
			ensureReplayDefinitionLookupContext(
				recorded_lookup_context,
				declaration_token,
				gSymbolTable.get_current_namespace_handle(),
				instantiated_name);
		ScopedDefinitionLookupContext ctx_scope(
			current_template_definition_lookup_context_,
			definition_lookup_context.is_valid()
				? &definition_lookup_context
				: nullptr);

		FlashCpp::FunctionParsingScopeGuard func_guard(
			*this,
			true,
			!inst_func.is_static(),
			instantiated_struct_decl,
			instantiated_name,
			instantiated_type_index,
			definition_scope_params,
			&inst_func);

		auto body_result = parse_function_body();
		if (!body_result.is_error() && body_result.node().has_value()) {
			try {
				ASTNode substituted_body = substituteTemplateParameters(
					*body_result.node(),
					substitution_template_params,
					substitution_template_args);
				inst_func.set_definition(substituted_body);
				finalize_function_after_definition(inst_func, true);
				parsed_and_substituted = true;
			} catch (const std::exception& e) {
				FLASH_LOG(
					Templates,
					Error,
					"Exception substituting OOL plain member body for ",
					log_context,
					" '",
					function_name,
					"': ",
					e.what());
			}
		} else {
			FLASH_LOG(
				Templates,
				Error,
				"Failed to parse OOL plain member body for ",
				log_context,
				": ",
				function_name);
		}
	}

	restore_lexer_position_only(saved_pos);
	discard_saved_token(saved_pos);
	return parsed_and_substituted;
}

static OutOfLineFunctionStubResolution findReplayedOutOfLineMemberInStructInfo(
	StructTypeInfo* struct_info,
	FunctionDeclarationNode& replayed_func) {
	OutOfLineFunctionStubResolution resolution;
	if (struct_info == nullptr) {
		return resolution;
	}

	OutOfLineFunctionStubResolution info_func_resolution =
		findMatchingFunctionInStructInfo(
			*struct_info,
			replayed_func,
			[](const FunctionDeclarationNode& info_func) {
				return !info_func.is_materialized();
			});
	if (info_func_resolution.func != nullptr || info_func_resolution.ambiguous) {
		return info_func_resolution;
	}

	for (auto& info_func : struct_info->member_functions) {
		if (info_func.is_constructor ||
			!info_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		auto& info_decl = info_func.function_decl.as<FunctionDeclarationNode>();
		FLASH_LOG(
			Templates,
			Debug,
			"StructTypeInfo replay sync candidate: name=",
			info_decl.decl_node().identifier_token().value(),
			", params=",
			static_cast<size_t>(info_decl.parameter_nodes().size()),
			", materialized=",
			info_decl.is_materialized());
		if (info_decl.is_materialized() ||
			info_decl.decl_node().identifier_token().value() !=
				replayed_func.decl_node().identifier_token().value() ||
			info_decl.parameter_nodes().size() != replayed_func.parameter_nodes().size() ||
			info_decl.is_static() != replayed_func.is_static() ||
			info_decl.is_const_member_function() != replayed_func.is_const_member_function() ||
			info_decl.is_volatile_member_function() != replayed_func.is_volatile_member_function()) {
			continue;
		}
		if (resolution.func != nullptr) {
			resolution.ambiguous = true;
			resolution.func = nullptr;
			return resolution;
		}
		resolution.func = &info_decl;
	}
	if (resolution.func != nullptr || resolution.ambiguous) {
		return resolution;
	}
	return info_func_resolution;
}

// Resolve any stored template arguments on a deferred base member-type chain after a class
// template's substitution maps are available.
// Returns std::nullopt when any pack expansion or concrete member argument cannot be resolved.
// The substitution maps only need to remain valid for the duration of this call.
// `materialize_type_arg` must accept `const TypeSpecifierNode&` and return an
// optionally more concrete `TemplateTypeArg` for nested template-instantiation
// arguments that need full deferred-base materialization.
// `substitute_expression` must accept `const ASTNode&` and return a substituted `ASTNode`.
// `evaluate_constant_expression` must accept `const ASTNode&` and return
// `std::optional<Parser::ConstantValue>` for non-type template arguments.
template <typename MaterializeTypeArgFn, typename SubstituteExpressionFn, typename EvaluateConstantExpressionFn>
static std::optional<std::vector<QualifiedTypeMemberAccess>> resolveDeferredBaseMemberTypeChain(
	std::span<const QualifiedTypeMemberAccess> member_type_chain,
	const TemplateArgSubstitutionMap& name_substitution_map,
	const TemplateArgPackSubstitutionMap& pack_substitution_map,
	MaterializeTypeArgFn&& materialize_type_arg,
	SubstituteExpressionFn&& substitute_expression,
	EvaluateConstantExpressionFn&& evaluate_constant_expression) {
	std::vector<QualifiedTypeMemberAccess> resolved_member_chain;
	resolved_member_chain.reserve(member_type_chain.size());

	for (const QualifiedTypeMemberAccess& member_access : member_type_chain) {
		QualifiedTypeMemberAccess resolved_member;
		resolved_member.member_name = member_access.member_name;
		resolved_member.has_template_arguments = member_access.has_template_arguments;
		if (member_access.has_template_arguments) {
			std::vector<TemplateTypeArg> resolved_args;
			resolved_args.reserve(member_access.template_argument_infos.size());
			for (const auto& member_arg_info : member_access.template_argument_infos) {
				if (member_arg_info.is_pack) {
					auto try_expand = [&](StringHandle pack_name) -> bool {
						auto it = pack_substitution_map.find(pack_name);
						if (it != pack_substitution_map.end()) {
							for (const TemplateTypeArg& pack_arg : it->second) {
								resolved_args.push_back(pack_arg);
							}
							return true;
						}
						return false;
					};

					bool expanded = false;
					if (member_arg_info.node.is<ExpressionNode>()) {
						const ExpressionNode& expr = member_arg_info.node.as<ExpressionNode>();
						if (const auto* template_parameter_reference = std::get_if<TemplateParameterReferenceNode>(&expr)) {
							expanded = try_expand(template_parameter_reference->param_name());
						} else if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
							expanded = try_expand(StringTable::getOrInternStringHandle(identifier->name()));
						}
					} else if (member_arg_info.node.is<TypeSpecifierNode>()) {
						TypeIndex idx = member_arg_info.node.as<TypeSpecifierNode>().type_index();
						if (idx.is_valid()) {
							if (const TypeInfo* idx_ti = tryGetTypeInfo(idx)) {
								expanded = try_expand(idx_ti->name_);
							}
						}
					}

					if (!expanded) {
						return std::nullopt;
					}
					continue;
				}

				bool member_arg_resolved = false;
				if (member_arg_info.node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& ts = member_arg_info.node.as<TypeSpecifierNode>();
					if (std::optional<TemplateTypeArg> resolved_type_arg =
							tryResolveDeferredBaseTypeArgFromMap(ts, name_substitution_map);
						resolved_type_arg.has_value()) {
						resolved_args.push_back(*resolved_type_arg);
						member_arg_resolved = true;
					}
					if (!member_arg_resolved) {
						if (std::optional<TemplateTypeArg> materialized_type_arg =
								materialize_type_arg(ts);
							materialized_type_arg.has_value()) {
							resolved_args.push_back(*materialized_type_arg);
							member_arg_resolved = true;
						}
					}
					if (!member_arg_resolved) {
						resolved_args.emplace_back(ts);
						member_arg_resolved = true;
					}
				} else if (member_arg_info.node.is<ExpressionNode>()) {
					const ExpressionNode& expr = member_arg_info.node.as<ExpressionNode>();
					if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						std::string_view pname = std::get<TemplateParameterReferenceNode>(expr).param_name().view();
						auto it = name_substitution_map.find(pname);
						if (it != name_substitution_map.end()) {
							resolved_args.push_back(it->second);
							member_arg_resolved = true;
						}
					} else if (std::holds_alternative<IdentifierNode>(expr)) {
						std::string_view iname = std::get<IdentifierNode>(expr).name();
						auto ident_subst_it = name_substitution_map.find(iname);
						if (ident_subst_it != name_substitution_map.end()) {
							resolved_args.push_back(ident_subst_it->second);
							member_arg_resolved = true;
						} else {
							StringHandle type_name_handle = StringTable::getOrInternStringHandle(iname);
							auto type_it = getTypesByNameMap().find(type_name_handle);
							if (type_it != getTypesByNameMap().end()) {
								TemplateTypeArg resolved_type_arg;
								resolved_type_arg.type_index = type_it->second->type_index_.withCategory(type_it->second->typeEnum());
								resolved_args.push_back(resolved_type_arg);
								member_arg_resolved = true;
							}
						}
					}

					if (!member_arg_resolved) {
						const ASTNode substituted_expr = substitute_expression(member_arg_info.node);
						if (auto evaluated_value = evaluate_constant_expression(substituted_expr)) {
							resolved_args.push_back(
								makeDeferredBaseValueArg(evaluated_value->value, evaluated_value->type_index));
							member_arg_resolved = true;
						}
					}
				}

				if (!member_arg_resolved) {
					return std::nullopt;
				}
			}
			// Deferred allocation: only emplace into gChunkedAnyStorage once the
			// entire loop succeeds. Early-return paths above leave template_arguments
			// as nullptr (relying on the `= nullptr` default member initializer in QualifiedTypeMemberAccess) and avoid touching gChunkedAnyStorage.
			resolved_member.template_arguments =
				&gChunkedAnyStorage.emplace_back<std::vector<TemplateTypeArg>>(std::move(resolved_args));
		}

		resolved_member_chain.push_back(std::move(resolved_member));
	}

	return resolved_member_chain;
}

// Compute the canonical instantiated lookup name for a member function.
// For conversion operators (operator with an identifier suffix, e.g. "operator value_type"),
// the substituted return type gives the canonical name (e.g. "operator int").
// For all other functions the original name is canonical.
StringHandle Parser::computeInstantiatedLookupName(
	StringHandle original_name,
	OverloadableOperator op_kind,
	const TypeSpecifierNode& substituted_return_type) {
	if (op_kind == OverloadableOperator::None) {
		std::string_view name = StringTable::getStringView(original_name);
		if (name.starts_with("operator ")) {
			std::string_view suffix = name.substr(9); // after "operator "
			// Conversion operators have a type-name suffix (starts with letter/underscore).
			// Non-conversion operators start with punctuation (==, !=, <, etc.).
			if (!suffix.empty() && (std::isalpha(static_cast<unsigned char>(suffix[0])) || static_cast<unsigned char>(suffix[0]) == '_')) {
				TypeIndex canonical_type_index = substituted_return_type.type_index();
				TypeCategory canonical_type = substituted_return_type.type();
				// Only alias-like categories need canonicalization here; pointer/reference
				// layers are stored separately on TypeSpecifierNode and are appended below.
				if ((canonical_type == TypeCategory::UserDefined || canonical_type == TypeCategory::TypeAlias) &&
					canonical_type_index.is_valid()) {
					const CanonicalTypeAlias canonical_alias = canonicalize_type_alias(canonical_type_index);
					canonical_type = canonical_alias.typeEnum();
					canonical_type_index = canonical_alias.resolvedTypeIndex();
				}

				std::string_view type_name = getTypeName(canonical_type);
				if (type_name.empty() && canonical_type_index.is_valid()) {
					if (const TypeInfo* rti = tryGetTypeInfo(canonical_type_index)) {
						type_name = StringTable::getStringView(rti->name());
					}
				}
				if (!type_name.empty()) {
					StringBuilder builder;
					builder.append("operator ");

					std::string base_cv = cvQualifierToString(substituted_return_type.cv_qualifier());
					if (!base_cv.empty()) {
						builder.append(base_cv).append(" ");
					}
					// Only prepend unsigned/signed when the canonical type_name
					// doesn't already encode signedness (e.g. "unsigned int").
					if (substituted_return_type.qualifier() == TypeQualifier::Unsigned &&
						!type_name.starts_with("unsigned ")) {
						builder.append("unsigned ");
					} else if (substituted_return_type.qualifier() == TypeQualifier::Signed &&
							   !type_name.starts_with("signed ")) {
						builder.append("signed ");
					}
					builder.append(type_name);
					for (const auto& ptr_level : substituted_return_type.pointer_levels()) {
						builder.append("*");
						std::string ptr_cv = cvQualifierToString(ptr_level.cv_qualifier);
						if (!ptr_cv.empty()) {
							builder.append(" ").append(ptr_cv);
						}
					}
					if (substituted_return_type.is_lvalue_reference()) {
						builder.append("&");
					} else if (substituted_return_type.is_rvalue_reference()) {
						builder.append("&&");
					}
					return StringTable::getOrInternStringHandle(builder.commit());
				}
			}
		}
	}
	return original_name;
}

SubstitutedMemberFunctionShell Parser::createSubstitutedMemberFunctionShell(
	const FunctionDeclarationNode& original_func,
	const ASTNode& original_return_type_node,
	const Token& fallback_return_token,
	StringHandle parent_struct_name,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	const StructDeclarationNode* owner_decl,
	TypeIndex instantiated_owner_type_index,
	TypeIndex override_return_type_index,
	OverloadableOperator operator_kind,
	StringHandle effective_name_override,
	StringHandle partial_pattern_owner_name,
	bool apply_bound_metadata_to_full_substitution,
	bool apply_resolved_index_to_full_substitution) {
	InlineVector<TemplateTypeArg, 4> template_args_inline;
	template_args_inline.reserve(template_args.size());
	for (const TemplateTypeArg& arg : template_args) {
		template_args_inline.push_back(arg);
	}

	const DeclarationNode& original_decl = original_func.decl_node();
	const TypeSpecifierNode& original_return_type = original_decl.type_specifier_node();
	TypeSpecifierNode substituted_return_type = buildSubstitutedTypeSpecifier(
		original_return_type,
		original_return_type_node,
		fallback_return_token,
		template_params,
		template_args_inline,
		[this](const ASTNode& node, const auto& params, const auto& args) {
			return substituteTemplateParameters(node, params, args);
		},
		[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
			return substitute_template_parameter(type_spec, params, args);
		},
		owner_decl,
		instantiated_owner_type_index,
		override_return_type_index,
		apply_bound_metadata_to_full_substitution,
		apply_resolved_index_to_full_substitution);
	if (partial_pattern_owner_name.isValid()) {
		clampPartialPatternTemplateParamIndirectionForOwner(
			substituted_return_type,
			original_return_type,
			partial_pattern_owner_name,
			template_params);
	}

	StringHandle effective_name = effective_name_override.isValid()
		? effective_name_override
		: computeInstantiatedLookupName(
			  original_decl.identifier_token().handle(),
			  operator_kind,
			  substituted_return_type);
	Token effective_identifier_token(
		Token::Type::Identifier,
		StringTable::getStringView(effective_name),
		original_decl.identifier_token().line(),
		original_decl.identifier_token().column(),
		original_decl.identifier_token().file_index());

	auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
		substituted_return_node,
		effective_identifier_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
		new_func_decl_ref,
		parent_struct_name);
	if (original_func.has_outer_template_bindings()) {
		InlineVector<TemplateParameterNode, 4> combined_params;
		InlineVector<TemplateTypeArg, 4> combined_args;
		combined_params.reserve(
			original_func.outer_template_param_names().size() + template_params.size());
		combined_args.reserve(
			original_func.outer_template_args().size() + template_args_inline.size());
		for (size_t i = 0; i < original_func.outer_template_param_names().size(); ++i) {
			combined_params.push_back(
				rebuildOuterTemplateParameter(
					original_func.outer_template_param_names()[i],
					original_func.outer_template_args()[i]));
			combined_args.push_back(toTemplateTypeArg(original_func.outer_template_args()[i]));
		}
		for (const auto& param : template_params) {
			combined_params.push_back(param);
		}
		for (const auto& arg : template_args_inline) {
			combined_args.push_back(arg);
		}
		setOuterTemplateBindingsFromParams(new_func_ref, combined_params, combined_args);
	} else {
		setOuterTemplateBindingsFromParams(new_func_ref, template_params, template_args_inline);
	}

	return {
		new_func_decl_node,
		&new_func_decl_ref,
		new_func_node,
		&new_func_ref,
		substituted_return_type,
		effective_name};
}

void Parser::substituteAndCopyMemberFunctionParameters(
	std::span<const ASTNode> original_params,
	FunctionDeclarationNode& target_node,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	const StructDeclarationNode* owner_decl,
	TypeIndex instantiated_owner_type_index,
	TypeIndex self_type_from_index,
	TypeIndex self_type_to_index,
	SubstitutedDefaultArgumentPolicy default_argument_policy,
	bool preserve_parameter_cv_qualifier,
	bool apply_bound_metadata_to_full_substitution,
	bool apply_resolved_index_to_full_substitution,
	bool preserve_dependent_member_template_signature_identity) {
	if (self_type_from_index.is_valid() != self_type_to_index.is_valid()) {
		throw InternalError("substituteAndCopyMemberFunctionParameters requires both self rewrite indices or neither.");
	}
	InlineVector<TemplateTypeArg, 4> template_args_inline;
	template_args_inline.reserve(template_args.size());
	for (const TemplateTypeArg& arg : template_args) {
		template_args_inline.push_back(arg);
	}

	TemplateArgSubstitutionMap name_substitution_map;
	TemplateArgPackSubstitutionMap pack_substitution_map;
	std::vector<std::string_view> template_param_order;
	bool substitution_maps_built = false;
	auto ensure_default_substitution_maps = [&]() {
		if (substitution_maps_built) {
			return;
		}
		buildTemplateArgSubstitutionMaps(
			template_params,
			template_args_inline,
			[](const TemplateParameterNode&, const TemplateTypeArg& arg) {
				return arg;
			},
			name_substitution_map,
			pack_substitution_map,
			&template_param_order);
		substitution_maps_built = true;
	};

	for (const auto& param : original_params) {
		if (!param.is<DeclarationNode>()) {
			target_node.add_parameter_node(param);
			continue;
		}

		const DeclarationNode& param_decl = param.as<DeclarationNode>();
		const TypeSpecifierNode& param_type_spec = param_decl.type_specifier_node();
		bool handled_as_pack = false;
		if (param_decl.is_parameter_pack() &&
			(param_type_spec.category() == TypeCategory::UserDefined ||
			 param_type_spec.category() == TypeCategory::TypeAlias ||
			 param_type_spec.category() == TypeCategory::Template)) {
			std::string_view type_name = param_type_spec.token().value();
			size_t non_variadic = 0;
			size_t pack_size = 0;
			bool found_pack = false;
			for (size_t i = 0; i < template_params.size(); ++i) {
				const TemplateParameterNode* tparam = &template_params[i];
				if (!tparam->is_variadic()) {
					non_variadic++;
					continue;
				}
				if (tparam->name() == type_name) {
					pack_size = template_args_inline.size() > non_variadic
						? template_args_inline.size() - non_variadic
						: 0;
					found_pack = true;
					break;
				}
			}
			if (found_pack) {
				if (pack_size != 0) {
					std::string_view original_name = param_decl.identifier_token().value();
					for (size_t pi = 0; pi < pack_size; ++pi) {
						const TemplateTypeArg& elem = template_args_inline[non_variadic + pi];
						TypeCategory elem_type = elem.typeEnum();
						TypeIndex elem_type_index = elem.type_index;
						TypeSpecifierNode substituted_type(
							elem_type,
							param_type_spec.qualifier(),
							getSubstitutedTypeSizeBits(elem_type_index.withCategory(elem_type)),
							param_decl.identifier_token(),
							param_type_spec.cv_qualifier());
						substituted_type.set_type_index(elem_type_index);
						for (const auto& pointer_level : param_type_spec.pointer_levels()) {
							substituted_type.add_pointer_level(pointer_level.cv_qualifier);
						}
						substituted_type.set_reference_qualifier(param_type_spec.reference_qualifier());
						if (elem.function_signature.has_value()) {
							substituted_type.set_function_signature(*elem.function_signature);
						}
						normalizeSubstitutedTypeSpec(substituted_type);
						StringBuilder name_builder;
						name_builder.append(original_name).append('_').append(pi);
						Token elem_token(
							Token::Type::Identifier,
							name_builder.commit(),
							param_decl.identifier_token().line(),
							param_decl.identifier_token().column(),
							param_decl.identifier_token().file_index());
						target_node.add_parameter_node(emplace_node<DeclarationNode>(
							emplace_node<TypeSpecifierNode>(substituted_type),
							elem_token));
					}
					pack_param_info_.push_back({original_name, 0, pack_size});
				}
				handled_as_pack = true;
			}
		}
		if (handled_as_pack) {
			continue;
		}

		const bool preserve_dependent_member_template_identity =
			preserve_dependent_member_template_signature_identity &&
			typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
				param_type_spec);
		TypeSpecifierNode substituted_param_type = buildSubstitutedTypeSpecifier(
			param_type_spec,
			param_decl.type_node(),
			param_decl.identifier_token(),
			template_params,
			template_args_inline,
			[this](const ASTNode& node, const auto& params, const auto& args) {
				return substituteTemplateParameters(node, params, args);
			},
			[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
				return substitute_template_parameter(type_spec, params, args);
			},
			owner_decl,
			instantiated_owner_type_index,
			TypeIndex{},
			apply_bound_metadata_to_full_substitution,
			apply_resolved_index_to_full_substitution);
		if (preserve_dependent_member_template_identity) {
			substituted_param_type = param_type_spec;
		}
		{
			ASTNode original_param_type_node = param_decl.type_node();
			TypeIndex dependent_member_type_index = substituted_param_type.type_index();
			dependent_member_type_index =
				resolveDependentMemberTemplateArtifactsForParam(
					*this,
					&original_param_type_node,
					param_type_spec,
					template_params,
					template_args_inline,
					dependent_member_type_index,
					!preserve_dependent_member_template_identity);
			if (preserve_dependent_member_template_identity &&
				!typeIndexPreservesDependentMemberTemplateSignatureIdentity(
					dependent_member_type_index)) {
				dependent_member_type_index = param_type_spec.type_index();
			}
			if (dependent_member_type_index.is_valid() &&
				dependent_member_type_index != substituted_param_type.type_index()) {
				substituted_param_type.set_type_index(
					dependent_member_type_index.withCategory(dependent_member_type_index.category()));
				substituted_param_type.set_category(dependent_member_type_index.category());
				if (!preserve_dependent_member_template_identity) {
					normalizeSubstitutedTypeSpec(substituted_param_type);
				}
			}
		}
		if (!preserve_parameter_cv_qualifier) {
			// Preserve cv-qualification that participates in pointee/referred type identity.
			// Only strip top-level cv on by-value parameters, matching function-parameter
			// adjustment rules while keeping pointer/reference signatures stable for
			// out-of-line replay attachment and mangling.
			const bool is_by_value_parameter =
				substituted_param_type.pointer_depth() == 0 &&
				!substituted_param_type.is_reference() &&
				!substituted_param_type.is_rvalue_reference();
			if (is_by_value_parameter) {
				substituted_param_type.set_cv_qualifier(CVQualifier::None);
			}
		}

		if (self_type_from_index.is_valid() &&
			self_type_to_index.is_valid() &&
			substituted_param_type.type_index() == self_type_from_index) {
			substituted_param_type.set_type_index(self_type_to_index.withCategory(self_type_to_index.category()));
			substituted_param_type.set_category(self_type_to_index.category());
			normalizeSubstitutedTypeSpec(substituted_param_type);
		}

		auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
		auto substituted_param_decl = emplace_node<DeclarationNode>(
			substituted_param_type_node,
			param_decl.identifier_token());
		if (param_decl.has_default_value()) {
			if (default_argument_policy == SubstitutedDefaultArgumentPolicy::SubstituteTemplateParameters) {
				ASTNode substituted_default = substituteTemplateParameters(
					param_decl.default_value(),
					template_params,
					template_args_inline);
				substituted_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
			} else if (default_argument_policy == SubstitutedDefaultArgumentPolicy::ExpressionSubstitutor) {
				ensure_default_substitution_maps();
				ExpressionSubstitutor substitutor(
					name_substitution_map,
					pack_substitution_map,
					*this,
					template_param_order);
				std::optional<ASTNode> substituted_default = substitutor.substitute(param_decl.default_value());
				if (substituted_default.has_value()) {
					substituted_param_decl.as<DeclarationNode>().set_default_value(*substituted_default);
				}
			}
		}
		target_node.add_parameter_node(substituted_param_decl);
	}
}

static int getTemplateArgumentSizeInBytes(const TemplateTypeArg& arg) {
	int size_in_bytes = get_type_size_bits(arg.category()) / 8;
	if (size_in_bytes > 0) {
		return size_in_bytes;
	}

	const TypeCategory category = arg.category();
	if ((category == TypeCategory::Struct || category == TypeCategory::UserDefined) &&
		arg.type_index.is_valid()) {
		if (const StructTypeInfo* si = tryGetStructTypeInfo(arg.type_index)) {
			return static_cast<int>(toSizeT(si->sizeInBytes()));
		}
	}

	return 8;
}

// Extract template parameter names (as StringHandles) from a list of
// TemplateParameterNode AST nodes.  Only collects names for the first
// `max_count` parameters to stay in sync with the argument vector.
template <typename ParamContainer>
static InlineVector<StringHandle, 4> collectParamNameHandles(
	const ParamContainer& template_params,
	size_t max_count) {
	InlineVector<StringHandle, 4> names;
	for (size_t i = 0; i < template_params.size() && i < max_count; ++i) {
		if (const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
			param != nullptr) {
			names.push_back(param->nameHandle());
		}
	}
	return names;
}

template <typename ParamContainer>
static InlineVector<TypeInfo::TemplateArgInfo, 4> collectEnrichedTemplateArgInfos(
	const ParamContainer& template_params,
	std::span<const TemplateTypeArg> template_args) {
	InlineVector<TypeInfo::TemplateArgInfo, 4> result;
	result.reserve(template_args.size());
	size_t param_index = 0;
	const TemplateParameterNode* active_param = nullptr;
	for (size_t i = 0; i < template_args.size(); ++i) {
		// Advance to the next non-null parameter, unless the current active_param is variadic
		while (active_param == nullptr && param_index < template_params.size()) {
			active_param = tryGetTemplateParameterNode(template_params[param_index]);
			if (active_param == nullptr) {
				++param_index;
			}
		}

		TemplateTypeArg arg = template_args[i];
		if (active_param != nullptr) {
			arg = enrichTemplateArgForParameter(*active_param, arg);
			// Advance past non-variadic params; hold variadic ones across remaining args
			if (!active_param->is_variadic()) {
				++param_index;
				active_param = nullptr;
			}
		}
		result.push_back(toTemplateArgInfo(arg));
	}
	return result;
}

ASTNode rebindStaticMemberInitializerFunctionCalls(
	const ASTNode& node,
	const StructTypeInfo* struct_info,
	bool set_qualified_name) {
	if (!struct_info || !node.has_value()) {
		return node;
	}

	auto recurse = [struct_info, set_qualified_name](const ASTNode& child) {
		return rebindStaticMemberInitializerFunctionCalls(child, struct_info, set_qualified_name);
	};

	if (auto rebound_node = RebindStaticMemberAst::tryRebindNonExpressionNode(
			node,
			recurse);
		rebound_node.has_value()) {
		return std::move(rebound_node.value());
	}

	if (auto rebound_node = RebindStaticMemberAst::tryRebindExpressionChildren(
			node,
			recurse);
		rebound_node.has_value()) {
		return std::move(rebound_node.value());
	}

	if (!node.is<ExpressionNode>()) {
		return node;
	}

	const auto& expr = node.as<ExpressionNode>();

	if (std::holds_alternative<CallExprNode>(expr)) {
		const auto& call = std::get<CallExprNode>(expr);
		ChunkedVector<ASTNode> rebound_args;
		for (const auto& arg : call.arguments()) {
			rebound_args.push_back(rebindStaticMemberInitializerFunctionCalls(arg, struct_info, set_qualified_name));
		}

		std::vector<ASTNode> rebound_template_args =
			transformCallTemplateArguments(
				call,
				recurse);

		const FunctionDeclarationNode* rebound_function = nullptr;
		const StructTypeInfo* rebound_owner = nullptr;
		if (call.called_from().kind().is_identifier()) {
			auto [found_function, found_owner] =
				RebindStaticMemberAst::findStaticMemberFunction(struct_info, call.called_from().handle());
			rebound_function = found_function;
			rebound_owner = found_owner;
		}

		CallExprNode rebound_call = [&]() {
			if (!call.has_receiver()) {
				if (rebound_function && rebound_function->get_definition().has_value()) {
					return makeResolvedCallExpr(*rebound_function, std::move(rebound_args), call.called_from());
				}
				return CallExprNode(call.callee(), std::move(rebound_args), call.called_from());
			}

			const auto* function_decl = call.callee().function_declaration_or_null();
			bool is_implicit_this_call = false;
			if (call.receiver().is<ExpressionNode>()) {
				const auto& object_expr = call.receiver().as<ExpressionNode>();
				is_implicit_this_call =
					std::holds_alternative<IdentifierNode>(object_expr) &&
					std::get<IdentifierNode>(object_expr).name() == "this";
			}

			const bool can_use_rebound_function =
				rebound_function && rebound_function->get_definition().has_value();
			if (is_implicit_this_call &&
				(can_use_rebound_function || (function_decl && function_decl->is_static()))) {
				if (can_use_rebound_function) {
					return makeResolvedCallExpr(*rebound_function, std::move(rebound_args), call.called_from());
				}
				return makeResolvedCallExpr(*function_decl, std::move(rebound_args), call.called_from());
			}

			ASTNode rebound_receiver = recurse(call.receiver());
			return CallExprNode(
				call.callee(),
				rebound_receiver,
				std::move(rebound_args),
				call.called_from());
		}();

		CallMetadataCopyOptions copy_options;
		copy_options.copy_template_arguments = false;
		copy_options.copy_qualified_name = false;
		copyCallMetadata(rebound_call, call, copy_options);

		if (!rebound_template_args.empty()) {
			rebound_call.set_template_arguments(std::move(rebound_template_args));
		}

		if (rebound_function && rebound_function->get_definition().has_value() &&
			rebound_function->has_mangled_name()) {
			rebound_call.set_mangled_name(rebound_function->mangled_name());
		} else if (call.has_mangled_name()) {
			rebound_call.set_mangled_name(call.mangled_name());
		}

		if (!rebound_function || !rebound_function->get_definition().has_value()) {
			if (call.has_qualified_name()) {
				rebound_call.set_qualified_name(call.qualified_name());
			}
		} else if (set_qualified_name && rebound_owner) {
			StringHandle qualified_handle = StringTable::getOrInternStringHandle(
				StringBuilder().append(rebound_owner->getName()).append("::"sv).append(call.called_from().handle()).commit());
			rebound_call.set_qualified_name(qualified_handle.view());
		}

		return ASTNode::emplace_node<ExpressionNode>(std::move(rebound_call));
	}

	return node;
}

static bool staticMemberInitializerContainsFunctionCall(const ASTNode& node) {
	if (!node.has_value() || !node.is<ExpressionNode>()) {
		return false;
	}

	return RebindStaticMemberAst::visitASTUntil(node, [](const ASTNode& current) {
		return current.is<CallExprNode>();
	});
}

template <typename TemplateParamsContainer>
static ConstExpr::EvaluationContext makeStaticMemberInitializerEvaluationContext(
	const SymbolTable& symbol_table,
	Parser& parser,
	const StructTypeInfo* struct_info,
	const TemplateParamsContainer& template_params,
	std::span<const TemplateTypeArg> template_args) {
	ConstExpr::EvaluationContext eval_ctx(symbol_table, parser);
	eval_ctx.storage_duration = ConstExpr::StorageDuration::Static;
	eval_ctx.struct_info = struct_info;
	if (struct_info && struct_info->own_type_index_.has_value()) {
		eval_ctx.struct_type_index = *struct_info->own_type_index_;
	}
	eval_ctx.template_args = template_args;
	eval_ctx.template_param_names.reserve(template_params.size());
	for (const auto& template_param : template_params) {
		if (const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(template_param);
			typed_param != nullptr) {
			eval_ctx.template_param_names.push_back(typed_param->name());
		}
	}
	return eval_ctx;
}

static std::optional<NormalizedInitializer> tryBuildConstantStaticMemberInitializer(
	const ConstExpr::EvalResult& eval_result,
	TypeIndex type_index,
	size_t size_in_bytes,
	ReferenceQualifier reference_qualifier,
	int pointer_depth) {
	if (!eval_result.success() || size_in_bytes == 0 ||
		reference_qualifier != ReferenceQualifier::None || pointer_depth != 0 ||
		eval_result.pointer_to_var.isValid()) {
		return std::nullopt;
	}

	NormalizedInitializer normalized;
	normalized.kind = NormalizedInitializer::Kind::ConstantBytes;
	normalized.constant_bytes.reserve(size_in_bytes);

	auto append_bytes = [&normalized](unsigned long long value, size_t byte_count) {
		for (size_t i = 0; i < byte_count; ++i) {
			normalized.constant_bytes.push_back(
				static_cast<char>((value >> (i * 8)) & 0xFF));
		}
	};
	auto append_object_bytes = [&normalized](const auto& object_value) {
		const auto* raw_bytes = reinterpret_cast<const unsigned char*>(&object_value);
		for (size_t i = 0; i < sizeof(object_value); ++i) {
			normalized.constant_bytes.push_back(static_cast<char>(raw_bytes[i]));
		}
	};

	switch (type_index.category()) {
	case TypeCategory::Float: {
		if (size_in_bytes != sizeof(float)) {
			return std::nullopt;
		}
		float value = static_cast<float>(eval_result.as_double());
		append_object_bytes(value);
		break;
	}
	case TypeCategory::Double: {
		if (size_in_bytes != sizeof(double)) {
			return std::nullopt;
		}
		double value = eval_result.as_double();
		append_object_bytes(value);
		break;
	}
	case TypeCategory::LongDouble: {
		// EvalResult stores floating values as double, so only materialize
		// long double bytes early on targets where long double matches double.
		// Otherwise return nullopt here and leave long double materialization to
		// the later code path that already handles target-specific storage.
		const bool has_expected_long_double_size = size_in_bytes == sizeof(long double);
		const bool long_double_matches_double_representation =
			sizeof(long double) == sizeof(double);
		if (!has_expected_long_double_size ||
			!long_double_matches_double_representation) {
			return std::nullopt;
		}
		long double value = static_cast<long double>(eval_result.as_double());
		append_object_bytes(value);
		break;
	}
	default:
		if (is_struct_type(type_index.category())) {
			return std::nullopt;
		}
		append_bytes(eval_result.as_uint_raw(), size_in_bytes);
		break;
	}

	if (normalized.constant_bytes.empty()) {
		return std::nullopt;
	}

	return normalized;
}

static void instantiateDeferredStaticInitializerCalls(
	const ASTNode& initializer,
	Parser& parser,
	const StructTypeInfo* struct_info) {
	if (!struct_info) {
		return;
	}

	auto& lazy_registry = LazyMemberInstantiationRegistry::getInstance();
	StringHandle owner_name = struct_info->getName();
	RebindStaticMemberAst::visitAST(initializer, [&](const ASTNode& current) {
		StringHandle member_name;
		bool needs_instantiation = false;

		if (current.is<CallExprNode>()) {
			const auto& call = current.as<CallExprNode>();
			if (call.called_from().kind().is_identifier()) {
				member_name = call.called_from().handle();
				needs_instantiation = true;
			}
		}

		LazyMemberKey member_key = LazyMemberKey::anyConst(owner_name, member_name);
		if (!needs_instantiation || !lazy_registry.needsInstantiation(member_key)) {
			return;
		}

		(void)parser.instantiateLazyMemberIfNeeded(member_key);
	});
}

template <typename TemplateParamsContainer>
static std::optional<NormalizedInitializer> tryEarlyNormalizeTemplateStaticMemberInitializer(
	std::optional<ASTNode>& initializer,
	const SymbolTable& symbol_table,
	Parser& parser,
	const StructTypeInfo* struct_info,
	const TemplateParamsContainer& template_params,
	std::span<const TemplateTypeArg> template_args,
	TypeIndex type_index,
	size_t size_in_bytes,
	ReferenceQualifier reference_qualifier,
	int pointer_depth) {
	if (!initializer.has_value()) {
		return std::nullopt;
	}

	initializer = rebindStaticMemberInitializerFunctionCalls(
		initializer.value(),
		struct_info,
		true);
	if (!initializer->is<ExpressionNode>()) {
		return std::nullopt;
	}

	instantiateDeferredStaticInitializerCalls(*initializer, parser, struct_info);

	if (initializer->is<ExpressionNode>()) {
		initializer = parser.substituteTemplateParameters(
			initializer.value(),
			template_params,
			template_args,
			struct_info->getName());
	}

	if (initializer->is<ExpressionNode>()) {
		TemplateEnvironment substitution_environment = buildTemplateEnvironment(
			std::span<const TemplateParameterNode>(template_params.data(), template_params.size()),
			template_args,
			nullptr);
		if (!substitution_environment.bindings.empty()) {
			ExpressionSubstitutor substitutor(substitution_environment, parser);
			substitutor.setCurrentOwnerTypeName(struct_info->getName());
			initializer = substitutor.substitute(initializer.value());
		}
	}

	const bool contains_function_call =
		staticMemberInitializerContainsFunctionCall(*initializer);
	ConstExpr::EvaluationContext eval_ctx = makeStaticMemberInitializerEvaluationContext(
		symbol_table,
		parser,
		struct_info,
		template_params,
		template_args);
	auto eval_result = ConstExpr::Evaluator::evaluate(*initializer, eval_ctx);
	if (!eval_result.success()) {
		FLASH_LOG(Templates, Debug,
				  "Failed early-normalizing static member initializer of type ",
				  initializer->type_name(),
				  ": ",
				  eval_result.error_message);
		return std::nullopt;
	}

	if (is_integer_type(type_index.category()) || type_index.category() == TypeCategory::Enum) {
		int64_t val = eval_result.as_int();
		std::string_view val_str = StringBuilder().append(val).commit();
		Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
		unsigned char literal_size = static_cast<unsigned char>(size_in_bytes * 8);
		TypeCategory literal_type = type_index.category() == TypeCategory::Enum
										? TypeCategory::Int
										: type_index.category();
		const char* log_message = contains_function_call
									 ? "Early-normalized function-call static member initializer to: "
									 : "Early-normalized static member initializer to: ";
		initializer = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(num_token, static_cast<unsigned long long>(val), literal_type, TypeQualifier::None, literal_size));
		FLASH_LOG(Templates, Debug, log_message, val);
	}

	return tryBuildConstantStaticMemberInitializer(
		eval_result,
		type_index,
		size_in_bytes,
		reference_qualifier,
		pointer_depth);
}

template <typename TemplateParamsContainer>
static void retryNormalizeTemplateStaticMembersAfterDeferredBodies(
	StructTypeInfo* struct_info,
	Parser* parser,
	const TemplateParamsContainer& template_params,
	std::span<const TemplateTypeArg> template_args) {
	if (!struct_info || !parser) {
		return;
	}

	for (auto& static_member : struct_info->static_members) {
		if (static_member.normalized_init.has_value() || !static_member.initializer.has_value()) {
			continue;
		}

		std::optional<ASTNode> initializer = static_member.initializer;
		std::optional<NormalizedInitializer> normalized_initializer =
			tryEarlyNormalizeTemplateStaticMemberInitializer(
				initializer,
				gSymbolTable,
				*parser,
				struct_info,
				template_params,
				template_args,
				static_member.type_index,
				static_member.size,
				static_member.reference_qualifier,
				static_member.pointer_depth);
		if (!normalized_initializer.has_value()) {
			continue;
		}

		static_member.initializer = std::move(initializer);
		static_member.normalized_init = std::move(normalized_initializer);
	}
}

static std::optional<StringHandle> findUnresolvedHardUseTypeSpecifier(const ASTNode& root) {
	std::optional<StringHandle> unresolved_name;

	auto check_type_spec = [&](const TypeSpecifierNode& type_spec) {
		if (unresolved_name.has_value()) {
			return;
		}

		const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
		if (typeSpecStillUsesDependentPlaceholder(type_spec)) {
			unresolved_name = type_info ? type_info->name() : type_spec.token().handle();
			return;
		}

		std::string_view type_name = type_info
			? StringTable::getStringView(type_info->name())
			: type_spec.token().value();
		if (type_spec.type() == TypeCategory::UserDefined &&
			type_name.find("::") != std::string_view::npos &&
			(type_info == nullptr ||
			 (!type_info->getStructInfo() && !type_info->getEnumInfo() && !type_info->isTypeAlias()))) {
			unresolved_name = type_info ? type_info->name() : type_spec.token().handle();
		}
	};

	AstTraversal::visitASTWithDecisions(root, [&](const ASTNode& current) {
		if (current.is<TypeSpecifierNode>()) {
			check_type_spec(current.as<TypeSpecifierNode>());
		} else if (current.is<DeclarationNode>()) {
			const DeclarationNode& decl = current.as<DeclarationNode>();
			if (decl.type_node().is<TypeSpecifierNode>()) {
				check_type_spec(decl.type_specifier_node());
			}
		} else if (current.is<VariableDeclarationNode>()) {
			const DeclarationNode& decl = current.as<VariableDeclarationNode>().declaration();
			if (decl.type_node().is<TypeSpecifierNode>()) {
				check_type_spec(decl.type_specifier_node());
			}
		}
		return unresolved_name.has_value()
			? AstTraversal::VisitDecision::Stop
			: AstTraversal::VisitDecision::Continue;
	});

	return unresolved_name;
}

static const StructDeclarationNode* getCachedTemplateStructDecl(const ASTNode* cached_node) {
	if (!cached_node || !cached_node->is<StructDeclarationNode>()) {
		return nullptr;
	}
	return &cached_node->as<StructDeclarationNode>();
}

std::optional<ASTNode> Parser::try_instantiate_class_template(std::string_view template_name, std::span<const TemplateTypeArg> template_args, bool force_eager) {
#if WITH_PARSER_RUNTIME_STATS
	FLASHCPP_PARSER_RUNTIME_PHASE(ClassTemplateInstantiation);
#endif
	PROFILE_TEMPLATE_INSTANTIATION(std::string(template_name));

	// Resolve template template parameter aliases: when inside a template function body
	// re-parse, "Container" may be a template template parameter bound to a concrete
	// template (e.g., "MyVec").  Look up the substitution and redirect.
	// Must be done before the early cache check so the cache key uses the resolved name.
	{
		StringHandle name_handle = StringTable::getOrInternStringHandle(template_name);
		for (const auto& subst : template_param_substitutions_) {
			if (subst.is_template_template_param && subst.param_name == name_handle &&
				subst.concrete_template_name.isValid()) {
				std::string_view concrete_name = StringTable::getStringView(subst.concrete_template_name);
				FLASH_LOG(Templates, Debug, "Redirecting template template param '", template_name,
						  "' -> '", concrete_name, "'");
				return try_instantiate_class_template(concrete_name, template_args, force_eager);
			}
		}
	}

	// Resolve relative/unqualified namespace prefix so the cache key is canonical.
	// Must be done before the early cache check for the same reason as above.
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		std::vector<StringHandle> namespace_components;
		std::string_view namespace_path = template_name.substr(0, last_colon);
		size_t component_start = 0;
		while (component_start < namespace_path.size()) {
			size_t separator = namespace_path.find("::", component_start);
			std::string_view component = separator == std::string_view::npos
											 ? namespace_path.substr(component_start)
											 : namespace_path.substr(component_start, separator - component_start);
			namespace_components.push_back(StringTable::getOrInternStringHandle(component));
			component_start = separator == std::string_view::npos ? namespace_path.size() : separator + 2;
		}

		auto resolve_relative_namespace = [&](NamespaceHandle start) -> NamespaceHandle {
			NamespaceHandle current = start;
			for (StringHandle component : namespace_components) {
				current = gNamespaceRegistry.lookupNamespace(current, component);
				if (!current.isValid()) {
					return current;
				}
			}
			return current;
		};

		StringHandle identifier_handle = StringTable::getOrInternStringHandle(template_name.substr(last_colon + 2));
		for (NamespaceHandle probe = gSymbolTable.get_current_namespace_handle(); probe.isValid();
			 probe = gNamespaceRegistry.getParent(probe)) {
			NamespaceHandle resolved_namespace = resolve_relative_namespace(probe);
			if (resolved_namespace.isValid()) {
				template_name = StringTable::getStringView(
					gNamespaceRegistry.buildQualifiedIdentifier(resolved_namespace, identifier_handle));
				break;
			}
			if (probe.isGlobal()) {
				break;
			}
		}
	}

	// Completed cache hits are not new instantiation work; return them before
	// consuming parser instantiation depth.  The name is fully resolved above.
	{
		std::string_view normalized_template_name = template_name;
		bool is_nested_member_class_template = false;
		if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
			normalized_template_name = template_name.substr(last_colon + 2);
			std::string_view owner_name = template_name.substr(0, last_colon);
			if (auto owner_template_opt = gTemplateRegistry.lookupTemplate(owner_name);
				owner_template_opt.has_value() &&
				owner_template_opt->is<TemplateClassDeclarationNode>()) {
				is_nested_member_class_template = true;
			}
		}
		bool can_use_raw_cache_key = true;
		if (auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
			template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
			const auto& raw_params = template_opt->as<TemplateClassDeclarationNode>().template_parameters();
			if (template_args.size() < raw_params.size()) {
				can_use_raw_cache_key = false;
			}
		}
		if (is_nested_member_class_template) {
			can_use_raw_cache_key = false;
		}
		if (can_use_raw_cache_key) {
			StringHandle template_name_handle = StringTable::getOrInternStringHandle(normalized_template_name);
			FlashCpp::TemplateInstantiationKey cache_key =
				FlashCpp::makeInstantiationKey(template_name_handle, template_args);
			auto cached = gTemplateRegistry.getInstantiation(cache_key);
			if (cached.has_value()) {
				const StructDeclarationNode* cached_struct = getCachedTemplateStructDecl(&cached.value());
				bool current_wants_full = (template_instantiation_mode_ != TemplateInstantiationMode::ShapeOnly);
				bool cached_is_shape_only = cached_struct && cached_struct->is_shape_only();
				bool cached_failed_substitution = cached_struct && cached_struct->is_failed_substitution();
				if (!(current_wants_full && cached_is_shape_only)) {
#if WITH_PARSER_RUNTIME_STATS
					if (runtime_stats_enabled_) {
						++runtime_stats_.class_template_instantiation_cache_hits;
					}
#endif
					FLASH_LOG_FORMAT(Templates, Debug, "Early cache hit for '{}' with {} args", template_name, template_args.size());
					if (cached_is_shape_only || cached_failed_substitution) {
						return cached;
					}
					return std::nullopt;
				}
			}
		}
	}

	// Push a parser-level instantiation context for provenance tracking and backtraces.
	// The mode snapshot is taken at call time (before any inner mode changes).
	StringHandle template_name_handle_for_ctx = StringTable::getOrInternStringHandle(template_name);
	ScopedParserInstantiationContext inst_ctx_guard(*this, template_instantiation_mode_, template_name_handle_for_ctx);

	// Nesting depth limit: prevents stack overflow from infinite recursive template
	// instantiation chains (e.g. mutually-recursive SFINAE constraints in libstdc++ iterators).
	// This is independent from the total-call iteration_count below, which cannot prevent
	// deep recursion.  Equivalent to GCC's -ftemplate-depth.
	//
	// NOTE: each nesting frame here pulls in substantial stack (parser state + template
	// instantiation context + base-class substitution); empirically on Linux with a 16MB
	// thread stack, the process starts SIGSEGV'ing on the stack guard page around depth
	// 50-60 when parsing real libstdc++ headers.  Keep the guard well below that so we
	// emit a diagnostic before the kernel kills us.
	static thread_local size_t s_instantiation_nesting_depth = 0;
	static thread_local bool s_instantiation_depth_warned = false;
	static constexpr size_t MAX_INSTANTIATION_NESTING_DEPTH = 40;
	++s_instantiation_nesting_depth;
	// Iteration counters are file-scope thread_locals reset once per parse() call;
	// see resetTemplateInstantiationCounters().  The NestingGuard here only manages
	// the depth counter and its associated "warned" flag.
	struct NestingGuard {
		~NestingGuard() {
			if (--s_instantiation_nesting_depth == 0) {
				// Reset the per-instantiation-tree "warned" flag when the outermost
				// instantiation unwinds, so the next instantiation tree (which
				// represents a genuinely new context) gets a fresh diagnostic.
				s_instantiation_depth_warned = false;
			}
		}
	} nesting_guard;
	if (s_instantiation_nesting_depth > MAX_INSTANTIATION_NESTING_DEPTH) {
		std::string_view error_msg = StringBuilder()
			.append("Max template instantiation depth (")
			.append(static_cast<uint64_t>(MAX_INSTANTIATION_NESTING_DEPTH))
			.append(") exceeded for '")
			.append(template_name)
			.append("'. Possible recursive template instantiation.")
			.commit();
		if (!s_instantiation_depth_warned) {
			FLASH_LOG(Templates, Error, error_msg);
			s_instantiation_depth_warned = true;
		}
		if (force_eager) {
			throw CompileError(std::string(error_msg));
		}
		return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
	}

	// Iteration guard: prevents retry storms for the entire compilation.
	// Once the limit is hit, g_template_inst_iteration_limit_tripped stays true for
	// the rest of this parse() call; it is only reset by resetTemplateInstantiationCounters()
	// at the start of the next parse() call.
	if (g_template_inst_iteration_limit_tripped) {
		if (force_eager) {
			throw CompileError("Template instantiation iteration limit was already exceeded earlier in this translation unit");
		}
		return failTemplateInstantiation(
			"Template instantiation iteration limit was already exceeded earlier in this translation unit",
			nullptr,
			std::nullopt);
	}
	g_template_inst_iteration_count++;
	if (g_template_inst_iteration_count > g_template_inst_max_iterations) {
		std::string_view error_msg = StringBuilder()
			.append("Template instantiation iteration limit exceeded (")
			.append(static_cast<int64_t>(g_template_inst_max_iterations))
			.append("). Last template: '")
			.append(template_name)
			.append("' with ")
			.append(static_cast<uint64_t>(template_args.size()))
			.append(" args. Possible infinite loop.")
			.commit();
		FLASH_LOG(Templates, Error, error_msg);
		g_template_inst_iteration_limit_tripped = true;
		if (force_eager) {
			throw CompileError(std::string(error_msg));
		}
		return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
	}

	// Log entry to help debug which call sites are causing issues
	FLASH_LOG(Templates, Debug, "try_instantiate_class_template: template='", template_name,
			  "', args=", template_args.size(), ", force_eager=", force_eager);

	// Early check: verify this is actually a class template before proceeding
	// This prevents errors when function templates like 'declval' are passed to this function
	{
		if (gTemplateRegistry.lookup_alias_template(template_name).has_value()) {
			std::string_view alias_template_name = template_name;
			std::string_view resolved_name = instantiate_and_register_base_template(alias_template_name, template_args);
			if (!resolved_name.empty()) {
				const StringHandle resolved_handle = StringTable::getOrInternStringHandle(resolved_name);
				if (const TypeInfo* resolved_info = findTypeByName(resolved_handle)) {
					std::string_view alias_instantiated_name = get_instantiated_class_name(template_name, template_args);
					StringHandle alias_instantiated_handle = StringTable::getOrInternStringHandle(alias_instantiated_name);
					TypeIndex resolved_registered_index =
						resolved_info->registeredTypeIndex().withCategory(resolved_info->typeEnum());
					TypeSpecifierNode alias_type_spec(
						resolved_registered_index,
						resolved_info->sizeInBits(),
						Token(),
						CVQualifier::None,
						ReferenceQualifier::None);
					if (const TypeSpecifierNode* existing_alias_spec = resolved_info->aliasTypeSpecifier()) {
						alias_type_spec = *existing_alias_spec;
					}

					TypeInfo* alias_info = nullptr;
					const TypeInfo* alias_semantic_source =
						isConcreteAliasSemanticSource(resolved_info)
							? resolved_info
							: nullptr;
					auto existing_it = getTypesByNameMap().find(alias_instantiated_handle);
					if (existing_it != getTypesByNameMap().end() && existing_it->second != nullptr) {
						alias_info = existing_it->second;
						update_type_alias_copy(
							*alias_info,
							resolved_registered_index,
							resolved_info->sizeInBits().value,
							&alias_type_spec,
							alias_semantic_source);
					} else {
						if (alias_semantic_source != nullptr) {
							alias_info = &add_type_alias_copy(
								alias_instantiated_handle,
								resolved_registered_index,
								resolved_info->sizeInBits().value,
								alias_type_spec,
								*alias_semantic_source);
						} else {
							alias_info = &add_type_alias_copy(
								alias_instantiated_handle,
								resolved_registered_index,
								resolved_info->sizeInBits().value,
								alias_type_spec);
						}
					}

					if (alias_info != nullptr) {
						auto template_args_info = toTemplateArgInfoList(template_args);
						alias_info->setTemplateInstantiationInfo(
							QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
							template_args_info);
					}
				}
			}
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for alias template '{}'", template_name);
			return std::nullopt;
		}

		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (template_opt.has_value() && !template_opt->is<TemplateClassDeclarationNode>()) {
			// This is not a class template (probably a function template) - skip silently
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for non-class template '{}'", template_name);
			return std::nullopt;
		}
	}

	// Early check: skip concepts - they are not class templates and should not be instantiated here
	// Concepts like same_as, convertible_to are stored in the concept registry, not the template registry
	{
		// Try both unqualified and with std:: prefix
		if (gConceptRegistry.hasConcept(template_name)) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for concept '{}'", template_name);
			return std::nullopt;
		}
		// Also check without namespace prefix (e.g., "std::same_as" -> "same_as")
		size_t last_colon_pos = template_name.rfind("::");
		if (last_colon_pos != std::string_view::npos) {
			std::string_view simple_name = template_name.substr(last_colon_pos + 2);
			if (gConceptRegistry.hasConcept(simple_name)) {
				FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for concept '{}'", template_name);
				return std::nullopt;
			}
		}
	}

	// Check if any template arguments are dependent (contain template parameters)
	// If so, we cannot instantiate the template yet - it's a dependent type
	for (const auto& arg : template_args) {
		if (arg.is_dependent) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping instantiation of {} - template arguments are dependent", template_name);

			// Register a placeholder TypeInfo for the dependent instantiated name
			// so that extractBaseTemplateName() can identify it via TypeInfo metadata
			// without needing string parsing (find('$')).
			std::string_view inst_name = get_instantiated_class_name(template_name, template_args);
			StringHandle inst_handle = StringTable::getOrInternStringHandle(inst_name);
			if (getTypesByNameMap().find(inst_handle) == getTypesByNameMap().end()) {
				TypeInfo& type_info = add_empty_type_entry();
				type_info.fallback_size_bits_ = 0;
				type_info.is_incomplete_instantiation_ = true;
				type_info.placeholder_kind_ = DependentPlaceholderKind::DependentArgs;
				type_info.name_ = inst_handle;
				auto template_args_info = toTemplateArgInfoList(template_args);
				InlineVector<StringHandle, 4> placeholder_param_names;
				if (auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
					template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					placeholder_param_names = collectParamNameHandles(
						template_opt->as<TemplateClassDeclarationNode>().template_parameters(),
						template_args.size());
				}
				type_info.setTemplateInstantiationInfo(
					QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
					template_args_info);
				type_info.setInstantiationContext(
					std::move(placeholder_param_names),
					template_args_info,
					nullptr);
				getTypesByNameMap()[inst_handle] = &type_info;
				FLASH_LOG_FORMAT(Templates, Debug, "Registered dependent placeholder '{}' with base template '{}'", inst_name, template_name);
			}

			// Return success (nullopt) but don't actually instantiate
			// The type will be resolved during actual template instantiation
			return std::nullopt;
		}
	}

	{
		auto exact_spec = gTemplateRegistry.lookupExactSpecialization(template_name, template_args);
		if (exact_spec.has_value()) {
			FLASH_LOG(Templates, Debug, "Found exact specialization for ", template_name, " with ", template_args.size(), " args before cache lookup");
			return instantiate_full_specialization(template_name, template_args, *exact_spec);
		}
	}

	// Check TypeIndex-based instantiation cache for O(1) lookup
	// This uses TypeIndex instead of string keys to avoid ambiguity with type names containing underscores
	std::string_view normalized_template_name = template_name;
	bool is_nested_member_class_template = false;
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		normalized_template_name = template_name.substr(last_colon + 2);
		std::string_view owner_name = template_name.substr(0, last_colon);
		if (auto owner_template_opt = gTemplateRegistry.lookupTemplate(owner_name);
			owner_template_opt.has_value() &&
			owner_template_opt->is<TemplateClassDeclarationNode>()) {
			is_nested_member_class_template = true;
		}
	}
	StringHandle template_name_handle = StringTable::getOrInternStringHandle(normalized_template_name);
	FlashCpp::TemplateInstantiationKey cache_key =
		FlashCpp::makeInstantiationKey(template_name_handle, template_args);
	bool current_wants_full = (template_instantiation_mode_ != TemplateInstantiationMode::ShapeOnly);
	bool can_use_raw_cache_key = true;
	if (auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
		const auto& raw_params = template_opt->as<TemplateClassDeclarationNode>().template_parameters();
		if (template_args.size() < raw_params.size()) {
			can_use_raw_cache_key = false;
		}
	}
	if (is_nested_member_class_template) {
		can_use_raw_cache_key = false;
	}
	if (can_use_raw_cache_key) {
		auto cached = gTemplateRegistry.getInstantiation(cache_key);
		if (cached.has_value()) {
			// If the cached entry was produced under ShapeOnly mode but the current
			// request is a full materialization (HardUse or SoftProbe), treat the
			// cache as a miss so we re-enter instantiation and fully materialise the
			// struct.  This is safe because ShapeOnly entries are always committed to
			// the cache, so a concurrent ShapeOnly path will still hit them.
			const ASTNode* cached_node = cached.has_value() ? &cached.value() : nullptr;
			const StructDeclarationNode* cached_struct = getCachedTemplateStructDecl(cached_node);
			bool cached_is_shape_only = cached_struct && cached_struct->is_shape_only();
			bool cached_failed_substitution = cached_struct && cached_struct->is_failed_substitution();
			if (current_wants_full && cached_is_shape_only) {
				FLASH_LOG_FORMAT(Templates, Debug,
					"Cache hit for '{}' is ShapeOnly but current mode is full — re-entering instantiation",
					template_name);
				// Fall through to re-instantiate
			} else {
#if WITH_PARSER_RUNTIME_STATS
				if (runtime_stats_enabled_) {
					++runtime_stats_.class_template_instantiation_cache_hits;
				}
#endif
				FLASH_LOG_FORMAT(Templates, Debug, "Cache hit for '{}' with {} args", template_name, template_args.size());
				if (cached_is_shape_only || cached_failed_substitution) {
					return cached;
				}
				return std::nullopt; // Already instantiated and committed globally
			}
		}
	}

	// Build InstantiationKey for cycle detection
	// Note: Caching is handled by getTypesByNameMap() check later in the function
	FlashCpp::InstantiationKey inst_key = FlashCpp::InstantiationQueue::makeKey(template_name, template_args);

	// Create RAII guard for in-progress tracking (handles cycle detection)
	auto in_progress_guard = FlashCpp::gInstantiationQueue.makeInProgressGuard(inst_key);
	if (!in_progress_guard.isActive()) {
		FLASH_LOG_FORMAT(Templates, Warning, "InstantiationQueue: cycle detected for '{}'", template_name);
		// Re-entering the same specialization while its outer instantiation is still
		// materializing it can create duplicate partially-initialized TypeInfo slots.
		// Returning nullopt here tells the caller to keep using the in-flight outer
		// instantiation, which will publish the completed specialization once it finishes.
		return std::nullopt;
	}

	// Track whether this is a normal implicit instantiation or an explicit-instantiation
	// definition that must force member materialization (C++20 [temp.explicit]).
	bool is_implicit_instantiation = !force_eager;

	// Helper lambda delegates to extracted member function for non-type template parameter substitution
	auto substitute_template_param_in_initializer = [this](
														std::string_view param_name,
														std::span<const TemplateTypeArg> args,
														const auto& params) -> std::optional<ASTNode> {
		InlineVector<TemplateParameterNode, 4> typed_params;
		typed_params.reserve(params.size());
		for (const auto& param : params) {
			if (const TemplateParameterNode* typed_param = tryGetTemplateParameterNode(param);
				typed_param != nullptr) {
				typed_params.push_back(*typed_param);
			}
		}
		return substitute_nontype_template_param(param_name, args, std::span<const TemplateParameterNode>(typed_params.data(), typed_params.size()));
	};

	// Helper lambda to substitute template parameters in member default initializers.
	// Use the generic substitution walk so compound expressions like `N + 0`
	// and nested template-dependent expressions are handled uniformly.
	auto substitute_default_initializer = [&](
											  const std::optional<ASTNode>& default_init,
											  std::span<const TemplateTypeArg> args,
											  const auto& params) -> std::optional<ASTNode> {
		if (!default_init.has_value()) {
			return std::nullopt;
		}

		ASTNode substituted_default = substituteTemplateParameters(default_init.value(), params, args);
		if (!substituted_default.is<ExpressionNode>()) {
			return substituted_default;
		}

		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		eval_ctx.template_args.assign(args.begin(), args.end());
		eval_ctx.template_param_names.reserve(params.size());
		for (const auto& param : params) {
			if (const TemplateParameterNode* template_param = tryGetTemplateParameterNode(param);
				template_param != nullptr) {
				eval_ctx.template_param_names.push_back(template_param->name());
			}
		}

		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default, eval_ctx);
		if (!eval_result.success()) {
			return substituted_default;
		}

		if (const auto* bool_value = std::get_if<bool>(&eval_result.value)) {
			Token bool_token(
				Token::Type::Keyword,
				*bool_value ? "true"sv : "false"sv,
				0,
				0,
				0);
			return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, *bool_value));
		}

		if (const auto* unsigned_value = std::get_if<unsigned long long>(&eval_result.value)) {
			TypeCategory value_category = eval_result.exact_type.has_value()
				? eval_result.exact_type->category()
				: TypeCategory::UnsignedLongLong;
			const int size_bits = eval_result.exact_type.has_value()
				? get_type_size_bits(eval_result.exact_type->category())
				: 64;
			Token literal_token(
				Token::Type::Literal,
				StringBuilder().append(static_cast<uint64_t>(*unsigned_value)).commit(),
				0,
				0,
				0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(
					literal_token,
					*unsigned_value,
					value_category,
					TypeQualifier::None,
					size_bits));
		}

		if (const auto* signed_value = std::get_if<long long>(&eval_result.value)) {
			TypeCategory value_category = eval_result.exact_type.has_value()
				? eval_result.exact_type->category()
				: TypeCategory::LongLong;
			const int size_bits = eval_result.exact_type.has_value()
				? get_type_size_bits(eval_result.exact_type->category())
				: 64;
			Token literal_token(
				Token::Type::Literal,
				StringBuilder().append(static_cast<int64_t>(*signed_value)).commit(),
				0,
				0,
				0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(
					literal_token,
					static_cast<unsigned long long>(*signed_value),
					value_category,
					TypeQualifier::None,
					size_bits));
		}

		return substituted_default;
	};

	auto get_substituted_type_size_bytes = [&](TypeIndex substituted_type_index) -> size_t {
		ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(substituted_type_index);
		TypeIndex size_type_index = resolved_alias.type_index.is_valid()
										? resolved_alias.type_index.withCategory(resolved_alias.typeEnum())
										: substituted_type_index;
		if (resolved_alias.pointer_depth > 0 ||
			resolved_alias.reference_qualifier != ReferenceQualifier::None ||
			size_type_index.category() == TypeCategory::FunctionPointer ||
			size_type_index.category() == TypeCategory::MemberFunctionPointer ||
			size_type_index.category() == TypeCategory::MemberObjectPointer) {
			return 8;
		}

		if (size_type_index.is_valid()) {
			if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(size_type_index)) {
				return toSizeT(struct_info->sizeInBytes());
			}
			if (const TypeInfo* type_info = tryGetTypeInfo(size_type_index)) {
				if (type_info->hasStoredSize()) {
					return toSizeT(type_info->sizeInBytes());
				}
			}
		}

		return get_type_size_bits(size_type_index.category()) / 8;
	};
	auto get_substituted_type_size_bits = [&](TypeIndex substituted_type_index) -> int {
		return static_cast<int>(get_substituted_type_size_bytes(substituted_type_index) * 8);
	};

	// Helper: substitute template parameter types in a function/constructor parameter list
	// and add the substituted parameters to a target node.
	// Consolidates the ~25-line pattern repeated across all instantiation paths.
	struct SelfTypeRewriteInfo {
		TypeIndex from_type_index;
		TypeIndex to_type_index;
	};
	auto makeImplicitCtorSelfTypeRewrite = [&](StringHandle pattern_struct_name, TypeIndex instantiated_struct_type_index)
		-> std::optional<SelfTypeRewriteInfo> {
		if (!instantiated_struct_type_index.is_valid()) {
			return std::nullopt;
		}
		auto it = getTypesByNameMap().find(pattern_struct_name);
		if (it == getTypesByNameMap().end() || it->second == nullptr) {
			return std::nullopt;
		}
		TypeIndex pattern_struct_type_index = it->second->registeredTypeIndex();
		if (!pattern_struct_type_index.is_valid()) {
			return std::nullopt;
		}
		pattern_struct_type_index.setCategory(TypeCategory::Struct);
		instantiated_struct_type_index.setCategory(TypeCategory::Struct);
		return SelfTypeRewriteInfo{pattern_struct_type_index, instantiated_struct_type_index};
	};
	auto substituteAndCopyParams = [&](
									   std::span<const ASTNode> orig_params,
									   auto& target_node,
									   const auto& tmpl_params,
									   const auto& tmpl_args,
									   std::optional<SelfTypeRewriteInfo> self_type_rewrite = std::nullopt) {
		for (const auto& param : orig_params) {
			if (!param.is<DeclarationNode>()) {
				target_node.add_parameter_node(param);
				continue;
			}
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_specifier_node();

			// Handle variadic pack parameters (e.g. "Args... args").
			// These must be expanded into N individual parameters (args_0, args_1, ...) where N
			// is the pack size, and pack_param_info_ must be populated so that pack expansions
			// in the body and initializers (e.g. adder.sum(args...) or Wrapper(100, args...))
			// produce the right renamed identifiers rather than N copies of the same name.
			// Handle variadic pack parameters (e.g. "Args... args").
			// Expand into N individual parameters (args_0, args_1, ...) and populate
			// pack_param_info_ so body/initializer pack expansions produce correct names.
			bool handled_as_pack = false;
			if (param_decl.is_parameter_pack() && (param_type_spec.category() == TypeCategory::UserDefined || param_type_spec.category() == TypeCategory::TypeAlias || param_type_spec.category() == TypeCategory::Template)) {
				std::string_view type_name = param_type_spec.token().value();
				size_t non_variadic = 0;
				size_t pack_size = 0;
				bool found_pack = false;
				for (size_t i = 0; i < tmpl_params.size(); ++i) {
					const TemplateParameterNode* tparam = tryGetTemplateParameterNode(tmpl_params[i]);
					if (tparam == nullptr)
						continue;
					if (!tparam->is_variadic()) {
						non_variadic++;
						continue;
					}
					if (tparam->name() == type_name) {
						pack_size = tmpl_args.size() > non_variadic ? tmpl_args.size() - non_variadic : 0;
						found_pack = true;
						break;
					}
				}
				if (found_pack) {
					if (pack_size == 0)
						continue; // Empty pack — omit this parameter entirely.
					// Expand into N parameters: args_0, args_1, ...
					std::string_view orig_name = param_decl.identifier_token().value();
					for (size_t pi = 0; pi < pack_size; ++pi) {
						const TemplateTypeArg& elem = tmpl_args[non_variadic + pi];
						TypeCategory elem_type = elem.typeEnum();
						TypeIndex elem_type_index = elem.type_index;
						TypeSpecifierNode sub_type(
							elem_type, param_type_spec.qualifier(),
							get_substituted_type_size_bits(elem_type_index.withCategory(elem_type)),
							param_decl.identifier_token(), param_type_spec.cv_qualifier());
						sub_type.set_type_index(elem_type_index);
						for (const auto& pl : param_type_spec.pointer_levels())
							sub_type.add_pointer_level(pl.cv_qualifier);
						sub_type.set_reference_qualifier(param_type_spec.reference_qualifier());
						if (elem.function_signature.has_value()) {
							sub_type.set_function_signature(*elem.function_signature);
						}
						normalizeSubstitutedTypeSpec(sub_type);
						StringBuilder name_builder;
						name_builder.append(orig_name).append('_').append(pi);
						Token elem_token(Token::Type::Identifier, name_builder.commit(),
										 param_decl.identifier_token().line(),
										 param_decl.identifier_token().column(),
										 param_decl.identifier_token().file_index());
						target_node.add_parameter_node(emplace_node<DeclarationNode>(
							emplace_node<TypeSpecifierNode>(sub_type), elem_token));
					}
					// Register the pack so expandPackExpansionArgs renames args→args_0, args_1…
					pack_param_info_.push_back({orig_name, 0, pack_size});
					handled_as_pack = true;
				}
			}
			if (handled_as_pack)
				continue;
			// Use substituteTemplateParameters as the primary substitution to correctly
			// propagate pointer depth from template args (e.g. I→int* gives ptr_depth=1).
			// The TypeIndex from substitute_template_parameter+self_type_rewrite is applied
			// on top to handle self-referential types.
			ASTNode original_param_type_node = param_decl.type_node();
			const bool preserve_dependent_member_template_identity =
				typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
					param_type_spec);
			ASTNode full_substituted_param_node = substituteTemplateParameters(
				original_param_type_node, tmpl_params, tmpl_args);
			TypeIndex param_type_index = substitute_template_parameter(
				param_type_spec, tmpl_params, tmpl_args);
			if (self_type_rewrite.has_value() &&
				param_type_index == self_type_rewrite->from_type_index) {
				param_type_index = self_type_rewrite->to_type_index;
			}
			param_type_index = resolveDependentMemberPlaceholderFromOwnerArtifact(
				original_param_type_node,
				param_type_spec,
				param_type_index);
			param_type_index = resolveDependentMemberTemplateArtifactsForParam(
				*this,
				&original_param_type_node,
				param_type_spec,
				tmpl_params,
				tmpl_args,
				param_type_index,
				!preserve_dependent_member_template_identity);
			if (preserve_dependent_member_template_identity &&
				!typeIndexPreservesDependentMemberTemplateSignatureIdentity(
					param_type_index)) {
				param_type_index = param_type_spec.type_index();
			}
			TypeSpecifierNode substituted_param_type = preserve_dependent_member_template_identity
				? param_type_spec
				: full_substituted_param_node.is<TypeSpecifierNode>()
					  ? full_substituted_param_node.as<TypeSpecifierNode>()
					  : TypeSpecifierNode(
							param_type_index.category(),
							param_type_spec.qualifier(),
							get_substituted_type_size_bits(param_type_index),
							param_decl.identifier_token(),
							param_type_spec.cv_qualifier());
			// Apply the resolved TypeIndex (self-type rewrite or alias resolution).
			if (param_type_index.is_valid()) {
				substituted_param_type.set_type_index(
					param_type_index.withCategory(param_type_index.category()));
				substituted_param_type.set_category(param_type_index.category());
			}
			if (!full_substituted_param_node.is<TypeSpecifierNode>()) {
				for (const auto& ptr_level : param_type_spec.pointer_levels()) {
					substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
				}
				substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());
			}
			if (param_type_spec.has_function_signature()) {
				substituted_param_type.set_function_signature(param_type_spec.function_signature());
			} else if (param_type_index.category() == TypeCategory::FunctionPointer ||
					   param_type_index.category() == TypeCategory::MemberFunctionPointer) {
				if (const auto* arg = findTemplateArgByName(
						param_type_spec.token().value(),
						tmpl_params,
						tmpl_args)) {
					if (arg->function_signature.has_value()) {
						substituted_param_type.set_function_signature(*arg->function_signature);
					}
				}
			}
			if (!preserve_dependent_member_template_identity) {
				normalizeSubstitutedTypeSpec(substituted_param_type);
			}
			auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
			auto substituted_param_decl = emplace_node<DeclarationNode>(
				substituted_param_type_node, param_decl.identifier_token());
			if (param_decl.has_default_value()) {
				ASTNode substituted_default = substituteTemplateParameters(
					param_decl.default_value(), tmpl_params, tmpl_args);
				substituted_param_decl.as<DeclarationNode>().set_default_value(substituted_default);
			}
			target_node.add_parameter_node(substituted_param_decl);
		}
	};

	// Helper: substitute a single initializer argument, expanding PackExpansionExprNode
	// into multiple arguments when present.  Appends result(s) to `out`.
	auto substituteInitArg = [&](
								 const ASTNode& arg,
								 std::vector<ASTNode>& out,
								 const auto& tmpl_params,
								 const auto& tmpl_args) {
		if (arg.is<ExpressionNode>()) {
			const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
			if (const auto* pack_exp = std::get_if<PackExpansionExprNode>(&arg_expr)) {
				ChunkedVector<ASTNode> expanded;
				if (expandPackExpansionArgs(*pack_exp, tmpl_params, tmpl_args, expanded)) {
					for (size_t ei = 0; ei < expanded.size(); ++ei) {
						out.push_back(expanded[ei]);
					}
					return;
				}
			}
		}
		out.push_back(substituteTemplateParameters(arg, tmpl_params, tmpl_args));
	};

	// Helper: substitute and copy all constructor initializers (member, base, delegating)
	// from an original ConstructorDeclarationNode to a new one.
	auto substituteAndCopyInitializers = [&](
											 const ConstructorDeclarationNode& orig_ctor,
											 ConstructorDeclarationNode& new_ctor,
											 const auto& tmpl_params,
											 const auto& tmpl_args) {
		for (const auto& [name, expr] : orig_ctor.member_initializers()) {
			new_ctor.add_member_initializer(name, substituteTemplateParameters(expr, tmpl_params, tmpl_args));
		}
		for (const auto& init : orig_ctor.base_initializers()) {
			std::vector<ASTNode> substituted_args;
			substituted_args.reserve(init.arguments.size());
			for (const auto& arg : init.arguments) {
				substituteInitArg(arg, substituted_args, tmpl_params, tmpl_args);
			}
			new_ctor.add_base_initializer(
				resolveBaseInitializerNameForTemplateArgs(init.getBaseClassName(), tmpl_params, tmpl_args),
				std::move(substituted_args));
		}
		if (orig_ctor.delegating_initializer().has_value()) {
			const auto& del_init = *orig_ctor.delegating_initializer();
			std::vector<ASTNode> substituted_del_args;
			substituted_del_args.reserve(del_init.arguments.size());
			for (const auto& arg : del_init.arguments) {
				substituteInitArg(arg, substituted_del_args, tmpl_params, tmpl_args);
			}
			new_ctor.set_delegating_initializer(std::move(substituted_del_args));
		}
	};

	// Helper lambda to evaluate a fold expression with concrete pack values and create an AST node
	// Uses ConstExpr::evaluate_fold_expression for the actual computation
	auto evaluate_fold_expression = [this](std::string_view op, std::span<const int64_t> pack_values) -> std::optional<ASTNode> {
		auto result = ConstExpr::evaluate_fold_expression(op, pack_values);
		if (!result.has_value()) {
			return std::nullopt;
		}

		FLASH_LOG(Templates, Debug, "Evaluated fold expression to: ", *result);

		// Create a bool literal for && and ||, numeric for others
		if (op == "&&" || op == "||") {
			Token bool_token(Token::Type::Keyword, *result ? "true"sv : "false"sv, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				BoolLiteralNode(bool_token, *result != 0));
		} else {
			std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(*result)).commit();
			Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(num_token, static_cast<unsigned long long>(*result), TypeCategory::Int, TypeQualifier::None, 64));
		}
	};

	// Helper lambda to resolve a dependent qualified type (like wrapper_void::type)
	// to its actual type after substituting template arguments.
	// Returns a resolved TemplateTypeArg if successful, nullopt otherwise.
	auto resolve_dependent_qualified_type = [&](
												std::string_view type_name,
												const TemplateTypeArg& actual_arg) -> std::optional<TemplateTypeArg> {
		// Check if this is a qualified type (contains ::)
		auto double_colon_pos = type_name.find("::");
		if (double_colon_pos == std::string_view::npos) {
			return std::nullopt;
		}

		// Extract base template name and member name
		std::string_view base_part = type_name.substr(0, double_colon_pos);
		std::string_view member_name = type_name.substr(double_colon_pos + 2);

		FLASH_LOG(Templates, Debug, "Resolving dependent type: ", type_name,
				  " -> base='", base_part, "', member='", member_name, "'");

		// Check if base_part contains a placeholder using TypeInfo-based detection
		auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(base_part);
		if (!is_dependent_placeholder) {
			return std::nullopt;
		}

		// Build the instantiated template name using hash-based naming
		std::string_view instantiated_base_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{actual_arg});

		// Try to instantiate the template if not already done
		std::vector<TemplateTypeArg> base_template_args = {actual_arg};
		try_instantiate_class_template(template_base_name, base_template_args);

		// Build the full qualified name (e.g., "wrapper_int::type")
		StringBuilder qualified_name_builder;
		qualified_name_builder.append(instantiated_base_name)
			.append("::")
			.append(member_name);
		std::string_view qualified_name = qualified_name_builder.commit();

		FLASH_LOG(Templates, Debug, "Looking up resolved type: ", qualified_name);

		// Look up the member type
		auto resolved_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(qualified_name));
		if (resolved_type_it == getTypesByNameMap().end()) {
			return std::nullopt;
		}

		const TypeInfo* resolved_type_info = resolved_type_it->second;

		// Get the resolved type, following aliases if needed
		TypeCategory resolved_base_type = resolved_type_info->resolvedType();
		TypeIndex resolved_type_index = resolved_type_info->type_index_;

		// Check if this is an alias to a concrete type
		if (resolved_type_info->resolvedType() == TypeCategory::UserDefined &&
			resolved_type_index != resolved_type_info->type_index_ &&
			resolved_type_index.is_valid()) {
			if (const TypeInfo* aliased_type = tryGetTypeInfo(resolved_type_index)) {
				resolved_base_type = aliased_type->resolvedType();
				resolved_type_index = aliased_type->type_index_;
			}
		}

		TemplateTypeArg resolved_arg;
		resolved_arg.type_index = resolved_type_index.withCategory(resolved_base_type);

		FLASH_LOG(Templates, Debug, "Resolved dependent type to: type=",
				  static_cast<int>(resolved_base_type), ", index=", resolved_type_index);

		return resolved_arg;
	};

	auto substituteDefaultTemplateArg = [&](const ASTNode& default_node,
											const auto& params,
											std::span<const TemplateTypeArg> current_args) -> ASTNode {
		if (current_args.empty()) {
			return default_node;
		}
		return substituteTemplateParameters(default_node, params, toInlineTemplateArgs(current_args));
	};

	auto collectTemplateParamNames = [](const auto& params) {
		std::vector<std::string_view> names;
		names.reserve(params.size());
		for (const auto& param_node : params) {
			const TemplateParameterNode* param = tryGetTemplateParameterNode(param_node);
			if (param == nullptr) {
				continue;
			}
			names.push_back(param->name());
		}
		return names;
	};

	auto tryAppendEvaluatedTemplateValue = [&](std::vector<TemplateTypeArg>& out_args,
											   const ASTNode& expr_node,
											   std::string_view log_context) -> bool {
		if (!expr_node.is<ExpressionNode>()) {
			return false;
		}

		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, eval_ctx);
		if (!eval_result.success()) {
			return false;
		}

		out_args.push_back(templateTypeArgFromEvalResult(eval_result));

		FLASH_LOG(Templates, Debug, "Evaluated ", log_context, " via ConstExprEvaluator: ", eval_result.as_int());
		return true;
	};

	// Helper lambda to resolve a deferred bitfield width from non-type template parameters
	auto resolve_bitfield_width = [&](
									  const StructMemberDecl& member_decl,
									  const auto& params,
									  std::span<const TemplateTypeArg> args) -> std::optional<size_t> {
		if (member_decl.bitfield_width.has_value())
			return member_decl.bitfield_width;
		if (!member_decl.bitfield_width_expr.has_value())
			return std::nullopt;
		std::unordered_map<TypeIndex, TemplateTypeArg> type_sub_map;
		std::unordered_map<std::string_view, int64_t> nontype_sub_map;
		for (size_t pi = 0; pi < params.size() && pi < args.size(); ++pi) {
			const TemplateParameterNode* tparam = tryGetTemplateParameterNode(params[pi]);
			if (tparam == nullptr)
				continue;
			if (tparam->kind() == TemplateParameterKind::NonType && args[pi].is_value)
				nontype_sub_map[tparam->name()] = args[pi].value;
		}
		ASTNode substituted = substitute_template_params_in_expression(
			*member_decl.bitfield_width_expr, type_sub_map, nontype_sub_map, StringHandle{});
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
		if (eval_result.success() && eval_result.as_int() >= 0)
			return static_cast<size_t>(eval_result.as_int());
		return std::nullopt;
	};

	auto resolve_array_dimensions = [&](
										const DeclarationNode& decl,
										const auto& params,
										std::span<const TemplateTypeArg> args) -> std::vector<size_t> {
		std::vector<size_t> resolved_dims;
		if (!decl.is_array()) {
			return resolved_dims;
		}
		std::unordered_map<TypeIndex, TemplateTypeArg> type_sub_map;
		std::unordered_map<std::string_view, int64_t> nontype_sub_map;
		for (size_t pi = 0; pi < params.size() && pi < args.size(); ++pi) {
			const TemplateParameterNode* tparam = tryGetTemplateParameterNode(params[pi]);
			if (tparam == nullptr)
				continue;
			if (tparam->kind() == TemplateParameterKind::NonType && args[pi].is_value) {
				nontype_sub_map[tparam->name()] = args[pi].value;
			}
		}
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		for (const auto& dim_expr : decl.array_dimensions()) {
			ASTNode substituted = substitute_template_params_in_expression(dim_expr, type_sub_map, nontype_sub_map, StringHandle{});
			auto eval_result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
			if (eval_result.success() && eval_result.as_int() > 0) {
				resolved_dims.push_back(static_cast<size_t>(eval_result.as_int()));
			}
		}
		return resolved_dims;
	};

	auto buildSubstitutedTypeAliasSpecifier = [&](
									 const TypeAliasDecl& type_alias,
									 TypeIndex substituted_type_index,
									 TypeCategory substituted_type,
									 const auto& params,
									 const auto& args,
									 StringHandle current_owner_type_name) -> std::optional<TypeSpecifierNode> {
		if (!type_alias.type_node.is<TypeSpecifierNode>()) {
			return std::nullopt;
		}

		if (const TypeInfo* substituted_type_info = tryGetTypeInfo(substituted_type_index);
			substituted_type_info != nullptr &&
			substituted_type_info->isDependentMemberType() &&
			substituted_type_info->hasDependentQualifiedName()) {
			if (const TypeInfo* resolved_dependent_type =
					resolveDependentMemberTypeSemantic(
						*substituted_type_info,
						params,
						args,
						current_owner_type_name);
				resolved_dependent_type != nullptr) {
				substituted_type_index = resolved_dependent_type->registeredTypeIndex();
				substituted_type = resolved_dependent_type->typeEnum();
			}
		}

		ResolvedAliasTypeInfo resolved_alias_target = resolveAliasTypeInfo(substituted_type_index);
		if (resolved_alias_target.type_index.is_valid()) {
			substituted_type_index = resolved_alias_target.type_index;
			substituted_type = resolved_alias_target.typeEnum();
		}

		if (const TypeInfo* substituted_type_info = tryGetTypeInfo(substituted_type_index);
			substituted_type_info != nullptr && substituted_type_info->isTemplateInstantiation()) {
			std::string_view base_template_name =
				StringTable::getStringView(substituted_type_info->baseTemplateName());
			StringHandle base_template_handle =
				StringTable::getOrInternStringHandle(base_template_name);
			forEachNonPackTemplateParamArgBinding(
				params,
				args,
				[&](const TemplateParameterNode& param, const TemplateTypeArg& concrete_arg, size_t) {
					if (base_template_name.empty() ||
						param.nameHandle() != base_template_handle ||
						!concrete_arg.is_template_template_arg ||
						!concrete_arg.template_name_handle.isValid()) {
						return;
					}
					std::vector<TemplateTypeArg> concrete_template_args =
						materializeTemplateArgs(*substituted_type_info, params, args);
					std::string_view concrete_template_name =
						StringTable::getStringView(concrete_arg.template_name_handle);
					std::string_view instantiated_name =
						get_instantiated_class_name(concrete_template_name, concrete_template_args);
					if (auto instantiated_node =
							try_instantiate_class_template(concrete_template_name, concrete_template_args);
						instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
						instantiated_name =
							StringTable::getStringView(instantiated_node->as<StructDeclarationNode>().name());
					}
					if (const TypeInfo* instantiated_type_info =
							findTypeByName(StringTable::getOrInternStringHandle(instantiated_name))) {
						substituted_type_index = instantiated_type_info->registeredTypeIndex();
						substituted_type = instantiated_type_info->typeEnum();
					}
				});
		}

		const TypeSpecifierNode alias_decl_pattern_spec =
			type_alias.type_node.as<TypeSpecifierNode>();
		TypeSpecifierNode substituted_type_spec = alias_decl_pattern_spec;
		substituted_type_spec.set_type_index(substituted_type_index.withCategory(substituted_type));
		substituted_type_spec.set_category(substituted_type);
		std::optional<TemplateTypeArg> rebound_arg;
		// Function-pointer aliases carry their dependent parameter use inside the
		// signature; the existing alias-resolution path below preserves that full
		// signature, while direct rebinding here is only for aliases shaped like T,
		// T*, T&, and cv-qualified variants.
		StringHandle alias_target_name = alias_decl_pattern_spec.has_function_signature()
											 ? StringHandle{}
											 : getAliasTargetNameHandle(alias_decl_pattern_spec);
		if (alias_target_name.isValid()) {
			forEachNonPackTemplateParamArgBinding(
				params,
				args,
				[&](const TemplateParameterNode& param, const TemplateTypeArg& concrete_arg, size_t) {
					if (!rebound_arg.has_value() &&
						!concrete_arg.is_value &&
						param.nameHandle() == alias_target_name) {
						rebound_arg = rebindDependentTemplateTypeArg(
							concrete_arg,
							TemplateTypeArg(alias_decl_pattern_spec));
					}
				});
		}
		if (rebound_arg.has_value()) {
			substituted_type_spec = makeTypeSpecifierFromTemplateTypeArg(*rebound_arg, substituted_type_spec.token());
			substituted_type_index = substituted_type_spec.type_index();
			substituted_type = substituted_type_spec.type();
		}
		if (substituted_type_spec.has_function_signature()) {
			auto substitute_signature_type_index = [&](TypeIndex signature_type_index) {
				const TypeInfo* signature_type_info = tryGetTypeInfo(signature_type_index);
				if (signature_type_info == nullptr) {
					return signature_type_index;
				}
				TypeIndex substituted_signature_index = signature_type_index;
				forEachNonPackTemplateParamArgBinding(
					params,
					args,
					[&](const TemplateParameterNode& param, const TemplateTypeArg& concrete_arg, size_t) {
						if (substituted_signature_index != signature_type_index ||
							concrete_arg.is_value ||
							param.nameHandle() != signature_type_info->name()) {
							return;
						}
						if (concrete_arg.type_index.is_valid()) {
							substituted_signature_index =
								concrete_arg.type_index.withCategory(concrete_arg.typeEnum());
							return;
						}
						TypeIndex native_index = nativeTypeIndex(concrete_arg.typeEnum());
						substituted_signature_index = native_index.is_valid()
							? native_index.withCategory(concrete_arg.typeEnum())
							: TypeIndex{0, concrete_arg.typeEnum()};
					});
				return substituted_signature_index;
			};
			FunctionSignature substituted_signature = substituted_type_spec.function_signature();
			substituted_signature.return_type_index =
				substitute_signature_type_index(substituted_signature.return_type_index);
			for (TypeIndex& parameter_type_index : substituted_signature.parameter_type_indices) {
				parameter_type_index = substitute_signature_type_index(parameter_type_index);
			}
			substituted_type_spec.set_function_signature(substituted_signature);
		}

		FLASH_LOG(Templates, Debug, "buildSubstitutedTypeAliasSpecifier: alias_name=", StringTable::getStringView(type_alias.alias_name),
				  ", array_dimensions.size()=", type_alias.array_dimensions.size(),
				  ", resolved_alias_target.isArray()=", resolved_alias_target.isArray());

		// Only process array dimensions that are directly part of this alias definition
		// (e.g., "using type = char[N]" has array dimensions from N).
		// Do NOT include array dimensions from the resolved alias target, as those
		// will be picked up by resolveAliasTypeInfo when it traverses the alias chain.
		if (type_alias.array_dimensions.empty()) {
			return substituted_type_spec;
		}

		std::vector<size_t> resolved_dims;
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		for (const auto& dim_expr : type_alias.array_dimensions) {
			FLASH_LOG(Templates, Debug, "buildSubstitutedTypeAliasSpecifier: substituting dim_expr, params.size()=", params.size(),
					  ", args.size()=", args.size());
			ASTNode substituted_dim = substituteTemplateParameters(dim_expr, params, args);
			auto eval_result = ConstExpr::Evaluator::evaluate(substituted_dim, eval_ctx);
			FLASH_LOG(Templates, Debug, "buildSubstitutedTypeAliasSpecifier: evaluated dim, success=", eval_result.success(),
					  ", value=", eval_result.success() ? eval_result.as_int() : -999);
			if (!eval_result.success() || eval_result.as_int() <= 0) {
				return std::nullopt;
			}
			resolved_dims.push_back(static_cast<size_t>(eval_result.as_int()));
		}
		substituted_type_spec.set_array_dimensions(resolved_dims);
		return substituted_type_spec;
	};
	auto trySubstituteIntrinsicTypeAlias = [&](
		const TypeSpecifierNode& alias_type_spec,
		const auto& params,
		const auto& args,
		TypeCategory& substituted_type,
		TypeIndex& substituted_type_index,
		int& substituted_size) {
		std::string_view alias_token_name = alias_type_spec.token().value();
		if (!isEncodedUnderlyingTypeIntrinsic(alias_token_name)) {
			return false;
		}

		TypeIndex subst_type_index = substitute_template_parameter(
			alias_type_spec,
			params,
			args);
		if (subst_type_index.category() == alias_type_spec.type() &&
			subst_type_index == alias_type_spec.type_index()) {
			return false;
		}

		substituted_type = subst_type_index.category();
		substituted_type_index = subst_type_index;
		substituted_size = is_primitive_type(substituted_type)
			? get_type_size_bits(substituted_type)
			: 0;
		return true;
	};

	auto instantiateAndResolveBaseName = [&](
		std::string_view base_template_name,
		std::span<TemplateTypeArg> base_args,
		bool force_eager_instantiation) -> std::string_view {
		auto result = try_instantiate_class_template(base_template_name, base_args, force_eager_instantiation);
		if (result.has_value() && result->is<StructDeclarationNode>()) {
			if (shouldCommitTemplateInstantiationArtifacts()) {
				registerAndNormalizeLateMaterializedTopLevelNode(*result);
			}
		}
		std::string_view resolved_name = get_instantiated_class_name(base_template_name, base_args);
		if ((!result.has_value() || !result->is<StructDeclarationNode>()) && !base_template_name.empty()) {
			auto registry_hit = gTemplateRegistry.getInstantiation(
				StringTable::getOrInternStringHandle(base_template_name), base_args);
			if (registry_hit.has_value() && registry_hit->is<StructDeclarationNode>()) {
				resolved_name = StringTable::getStringView(
					registry_hit->as<StructDeclarationNode>().name());
			}
		}
		return resolved_name;
	};

	auto tryAppendStaticMemberValueFromTypeName = [&](
		std::vector<TemplateTypeArg>& out_args,
		std::string_view type_name,
		std::string_view member_name,
		const auto& params,
		std::span<const TemplateTypeArg> current_args,
		std::string_view log_context) -> bool {
		auto try_append_from_type_info = [&](const TypeInfo& candidate_info) -> bool {
			const StructTypeInfo* struct_info = candidate_info.getStructInfo();
			if (!struct_info) {
				return false;
			}

			auto try_append_from_member = [&](const StructStaticMember& static_member) -> bool {
				if (!static_member.initializer.has_value()) {
					return false;
				}
				if (tryAppendEvaluatedTemplateValue(out_args, *static_member.initializer, log_context)) {
					return true;
				}
				if (!static_member.initializer->is<ExpressionNode>()) {
					return false;
				}
				const ExpressionNode& init_expr = static_member.initializer->as<ExpressionNode>();
				if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&init_expr)) {
					out_args.push_back(TemplateTypeArg(bool_literal->value() ? 1LL : 0LL, TypeCategory::Bool));
					return true;
				}
				if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&init_expr)) {
					const auto& value = numeric_literal->value();
					if (const auto* ull_value = std::get_if<unsigned long long>(&value)) {
						out_args.push_back(TemplateTypeArg(static_cast<int64_t>(*ull_value)));
						return true;
					}
				}
				return false;
			};

			// Use findStaticMemberRecursive to also check inherited members from base classes.
			// This handles patterns like is_enum<T>::value where value comes from integral_constant<bool,V>.
			StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
			auto [found_member, found_owner] = struct_info->findStaticMemberRecursive(member_name_handle);
			if (found_member) {
				return try_append_from_member(*found_member);
			}

			// Fallback: iterate direct members (in case findStaticMemberRecursive missed something)
			for (const auto& static_member : struct_info->static_members) {
				if (static_member.getName() != member_name_handle) {
					continue;
				}
				return try_append_from_member(static_member);
			}

			return false;
		};

		auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(type_name));
		if (type_it == getTypesByNameMap().end() || type_it->second == nullptr) {
			return false;
		}

		const TypeInfo* candidate_info = type_it->second;
		if (candidate_info->isTemplateInstantiation()) {
			std::string_view base_template_name = StringTable::getStringView(candidate_info->baseTemplateName());
			if (!base_template_name.empty()) {
				std::vector<TemplateTypeArg> concrete_args =
					materializeTemplateArgs(*candidate_info, params, current_args);
				std::string_view instantiated_name =
					instantiateAndResolveBaseName(base_template_name, concrete_args, false);
				auto instantiated_it =
					getTypesByNameMap().find(StringTable::getOrInternStringHandle(instantiated_name));
				if (instantiated_it != getTypesByNameMap().end() && instantiated_it->second != nullptr) {
					candidate_info = instantiated_it->second;
				}
			}
		}

		return try_append_from_type_info(*candidate_info);
	};

	// 1) Full/Exact specialization lookup
	// If there is an exact specialization registered for (template_name, template_args),
	// it always wins over partial specializations and the primary template.
	// Note: This also handles empty template args (e.g., template<> struct Tuple<> {})
	{
		auto exact_spec = gTemplateRegistry.lookupExactSpecialization(template_name, template_args);
		if (exact_spec.has_value()) {
			FLASH_LOG(Templates, Debug, "Found exact specialization for ", template_name, " with ", template_args.size(), " args");
			// Instantiate the exact specialization
			return instantiate_full_specialization(template_name, template_args, *exact_spec);
		}
	}

	// Generate the instantiated class name first
	auto instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args));

	// Check if we already have this instantiation
	auto existing_type = getTypesByNameMap().find(instantiated_name);
	if (existing_type != getTypesByNameMap().end()) {
		auto cached_reg = gTemplateRegistry.getInstantiation(cache_key);
		const ASTNode* cached_node = cached_reg.has_value() ? &cached_reg.value() : nullptr;
		const StructDeclarationNode* cached_struct = getCachedTemplateStructDecl(cached_node);
		if (cached_struct && cached_struct->needs_shape_only_upgrade(current_wants_full)) {
			FLASH_LOG_FORMAT(Templates, Debug,
				"Type-map hit for '{}' is backed by a ShapeOnly instantiation — re-entering full instantiation",
				StringTable::getStringView(instantiated_name));
		} else {
		PROFILE_TEMPLATE_CACHE_HIT(std::string(template_name));
		return std::nullopt;
		}
	}
	PROFILE_TEMPLATE_CACHE_MISS(std::string(template_name));

	// Fill in default template arguments BEFORE pattern matching (void_t SFINAE fix)
	// This is critical for patterns like: template<typename T, typename = void> struct has_type;
	// with specialization: template<typename T> struct has_type<T, void_t<typename T::type>>;
	// When has_type<WithType> is instantiated, we need to fill in the default (void) before pattern matching.
	std::vector<TemplateTypeArg> filled_args_for_pattern_match(template_args.begin(), template_args.end());
	{
		auto primary_template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (primary_template_opt.has_value() && primary_template_opt->is<TemplateClassDeclarationNode>()) {
			const TemplateClassDeclarationNode& primary_template = primary_template_opt->as<TemplateClassDeclarationNode>();
			const auto& primary_params = primary_template.template_parameters();
			InlineVector<TemplateParameterNode, 4> typed_primary_params = primary_params;

			// Fill in defaults for missing arguments
			for (size_t i = filled_args_for_pattern_match.size(); i < primary_params.size(); ++i) {
				const TemplateParameterNode* param = tryGetTemplateParameterNode(primary_params[i]);
				if (param == nullptr)
					continue;

				// Skip variadic parameters
				if (param->is_variadic())
					continue;

				// Check if parameter has a default
				if (!param->has_default())
					break; // No default = can't fill in

				// Get the default value
				const ASTNode& default_node = param->default_value();

				if (param->kind() == TemplateParameterKind::Type && default_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
					bool handled_type_default = false;

					// Simple case: default is void
					if (default_type.category() == TypeCategory::Void) {
						TemplateTypeArg void_arg;
						void_arg.type_index = nativeTypeIndex(TypeCategory::Void);
						filled_args_for_pattern_match.push_back(void_arg);
						FLASH_LOG(Templates, Debug, "Filled in default argument for param ", i, ": void");
						continue;
					}

					// Check if default is an alias template like void_t
					// by looking at the token value
					Token default_token = default_type.token();
					std::string_view alias_name = default_token.value();

					// Look up if this is an alias template
					auto alias_opt = gTemplateRegistry.lookup_alias_template(alias_name);
					if (alias_opt.has_value()) {
						const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();

						// Check if the alias target type is void (like void_t)
						const TypeSpecifierNode& alias_type_spec = alias_node.target_type();
						if (alias_type_spec.category() == TypeCategory::Void) {
							// void_t-like alias: fill in void here, SFINAE check happens in pattern matching
							TemplateTypeArg void_arg;
							void_arg.type_index = nativeTypeIndex(TypeCategory::Void);
							filled_args_for_pattern_match.push_back(void_arg);
							FLASH_LOG(Templates, Debug, "Filled in void_t alias default for param ", i, ": void");
							continue;
						}
					}

					// Check if this is a dependent qualified type (like wrapper<T>::type)
					// that needs resolution based on already-filled template arguments
					if ((default_type.category() == TypeCategory::UserDefined || default_type.category() == TypeCategory::TypeAlias || default_type.category() == TypeCategory::Template) && default_type.type_index().is_valid()) {
						if (const TypeInfo* default_type_info = tryGetTypeInfo(default_type.type_index())) {
							std::string_view default_type_name = StringTable::getStringView(default_type_info->name());

							// Try to resolve using each filled argument
							for (size_t arg_idx = 0; arg_idx < filled_args_for_pattern_match.size(); ++arg_idx) {
								auto resolved = resolve_dependent_qualified_type(default_type_name, filled_args_for_pattern_match[arg_idx]);
								if (resolved.has_value()) {
									filled_args_for_pattern_match.push_back(*resolved);
									handled_type_default = true;
									break;
								}
							}
						}
					}

					if (!handled_type_default) {
						ASTNode substituted_default_node = substituteDefaultTemplateArg(default_node, primary_params, filled_args_for_pattern_match);
						if (substituted_default_node.is<TypeSpecifierNode>()) {
							filled_args_for_pattern_match.push_back(TemplateTypeArg(substituted_default_node.as<TypeSpecifierNode>()));
							FLASH_LOG(Templates, Debug, "Filled in substituted default type argument for param ", i);
							handled_type_default = true;
						}
					}

					if (!handled_type_default) {
						// For other default types, use the type as-is
						filled_args_for_pattern_match.push_back(TemplateTypeArg(default_type));
						FLASH_LOG(Templates, Debug, "Filled in default type argument for param ", i);
					}
				} else if (param->kind() == TemplateParameterKind::NonType && default_node.is<ExpressionNode>()) {
					// Handle non-type template parameter defaults like is_arithmetic<T>::value
					size_t size_before = filled_args_for_pattern_match.size();
					ASTNode substituted_default_node = substituteNonTypeDefaultExpression(
						default_node,
						primary_params,
						filled_args_for_pattern_match);
					const ASTNode& expr_source = substituted_default_node.is<ExpressionNode>() ? substituted_default_node : default_node;
					const ExpressionNode& expr = expr_source.as<ExpressionNode>();

					if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
						const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);

						if (!qual_id.namespace_handle().isGlobal()) {
							std::string_view type_name = gNamespaceRegistry.getName(qual_id.namespace_handle());
							std::string_view member_name = qual_id.name();
							if (!filled_args_for_pattern_match.empty()) {
								if (tryAppendStaticMemberValueFromTypeName(
										filled_args_for_pattern_match,
										type_name,
										member_name,
										primary_params,
										filled_args_for_pattern_match,
										"pattern-match non-type default qualified static member")) {
									FLASH_LOG(Templates, Debug, "Resolved dependent qualified identifier: ",
											  type_name, "::", member_name);
								}
							}
						}
					} else if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&expr)) {
						const NumericLiteralNode& lit = *numeric_literal;
						const auto& val = lit.value();
						if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
							filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(*ull_val)));
						}
					} else if (const auto* bool_literal_ptr = std::get_if<BoolLiteralNode>(&expr)) {
						const BoolLiteralNode& lit = *bool_literal_ptr;
						filled_args_for_pattern_match.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, TypeCategory::Bool));
					} else if (std::holds_alternative<SizeofExprNode>(expr)) {
						// Handle sizeof(T) as a default value
						const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr);
						if (sizeof_node.is_type()) {
							// sizeof(type) - evaluate the type size
							const ASTNode& type_node = sizeof_node.type_or_expr();
							if (type_node.is<TypeSpecifierNode>()) {
								TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();

								// Check if this is a template parameter that needs substitution
								bool found_substitution = false;
								std::string_view type_name;

								// Try to get the type name from the token first (most reliable for template params)
								if (type_spec.token().type() == Token::Type::Identifier) {
									type_name = type_spec.token().value();
								} else if ((type_spec.category() == TypeCategory::UserDefined || type_spec.category() == TypeCategory::TypeAlias || type_spec.category() == TypeCategory::Template) && type_spec.type_index().is_valid()) {
									// Fall back to gTypeInfo for fully resolved types
									if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
										type_name = StringTable::getStringView(type_info->name());
									}
								}

								if (!type_name.empty()) {
									// Check if this is one of the template parameters we've already filled
									for (size_t j = 0; j < primary_params.size() && j < filled_args_for_pattern_match.size(); ++j) {
										if (const TemplateParameterNode* prev_param = tryGetTemplateParameterNode(primary_params[j]);
											prev_param != nullptr) {
											if (prev_param->name() == type_name) {
												// Found the matching template parameter - use its filled value
												const TemplateTypeArg& filled_arg = filled_args_for_pattern_match[j];
												if (filled_arg.category() != TypeCategory::Invalid) {
													const int size_in_bytes = getTemplateArgumentSizeInBytes(filled_arg);

													if (size_in_bytes > 0) {
														filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
														FLASH_LOG(Templates, Debug, "Filled in sizeof(", type_name, ") default: ", size_in_bytes, " bytes");
														found_substitution = true;
														break;
													}
												}
											}
										}
									}
								}

								if (!found_substitution) {
									// Direct type (not a template parameter)
									int size_in_bits = type_spec.size_in_bits();
									int size_in_bytes = (size_in_bits + 7) / 8; // Round up to bytes
									filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
									FLASH_LOG(Templates, Debug, "Filled in sizeof default: ", size_in_bytes, " bytes");
								}
							}
						}
					}

					if (filled_args_for_pattern_match.size() == size_before) {
						std::vector<std::string_view> primary_param_names = collectTemplateParamNames(primary_params);
						if (auto evaluated_default = substituteAndEvaluateNonTypeDefault(
								default_node,
								primary_params,
								std::span<const TemplateTypeArg>(
									filled_args_for_pattern_match.data(),
									filled_args_for_pattern_match.size()),
								std::span<const std::string_view>(
									primary_param_names.data(),
									primary_param_names.size()));
							evaluated_default.has_value()) {
							filled_args_for_pattern_match.push_back(*evaluated_default);
						}
					}
					if (filled_args_for_pattern_match.size() == size_before) {
						InlineVector<TemplateTypeArg, 4> retry_args =
							toInlineTemplateArgs(filled_args_for_pattern_match);
						NamespaceHandle declaration_namespace = NamespaceHandle{};
						if (template_name.find("::") != std::string_view::npos) {
							declaration_namespace =
								QualifiedIdentifier::fromQualifiedName(
									template_name,
									NamespaceRegistry::GLOBAL_NAMESPACE)
									.namespace_handle;
						}
						if (tryAppendDefaultTemplateArg(
								*param,
								std::span<const TemplateParameterNode>(
									typed_primary_params.data(),
									typed_primary_params.size()),
								retry_args,
								declaration_namespace)) {
							filled_args_for_pattern_match.assign(
								retry_args.begin(),
								retry_args.end());
						}
					}

					if (filled_args_for_pattern_match.size() == size_before) {
						throw CompileError(std::string(StringBuilder()
							.append("Could not evaluate non-type template default for parameter ")
							.append(std::to_string(i))
							.append(" of '")
							.append(template_name)
							.append("'")
							.commit()));
					}
				}
			}
		}
	}

	// Regenerate instantiated name with filled-in defaults
	// This is needed when defaults are dependent types that get resolved (e.g., typename wrapper<T>::type -> int)
	if (filled_args_for_pattern_match.size() > template_args.size()) {
		StringHandle original_instantiated_name = instantiated_name;
		instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, filled_args_for_pattern_match));
		FLASH_LOG(Templates, Debug, "Regenerated instantiated name with defaults: ", StringTable::getStringView(instantiated_name));

		// Check again if we already have this instantiation (with filled-in defaults)
		auto existing_type_with_defaults = getTypesByNameMap().find(instantiated_name);
		if (existing_type_with_defaults != getTypesByNameMap().end()) {
			if (original_instantiated_name != instantiated_name) {
				getTypesByNameMap()[original_instantiated_name] = existing_type_with_defaults->second;
			}
			auto cached_reg = gTemplateRegistry.getInstantiation(cache_key);
			const ASTNode* cached_node = cached_reg.has_value() ? &cached_reg.value() : nullptr;
			const StructDeclarationNode* cached_struct = getCachedTemplateStructDecl(cached_node);
			if (cached_struct && cached_struct->needs_shape_only_upgrade(current_wants_full)) {
				FLASH_LOG(Templates, Debug, "Found ShapeOnly instantiation with filled-in defaults; re-entering full instantiation");
			} else {
				FLASH_LOG(Templates, Debug, "Found existing instantiation with filled-in defaults");
				return std::nullopt;
			}
		}
	}

	// First, check if there's an exact specialization match
	// Try to match a specialization pattern and get the substitution mapping
	{
		PROFILE_TEMPLATE_SPECIALIZATION_MATCH();
		std::unordered_map<std::string, TemplateTypeArg> param_substitutions;
		FLASH_LOG(Templates, Debug, "Looking for pattern match for ", template_name, " with ", filled_args_for_pattern_match.size(), " args (after default fill-in)");
		auto pattern_match_opt = gTemplateRegistry.matchSpecializationPattern(template_name, filled_args_for_pattern_match);
		if (pattern_match_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "Found pattern match!");
			// Found a matching pattern - we need to instantiate it with concrete types
			ASTNode& pattern_node = *pattern_match_opt;

			// Handle both StructDeclarationNode (top-level partial specialization) and
			// TemplateClassDeclarationNode (member template partial specialization)
			StructDeclarationNode* pattern_struct_ptr = nullptr;
			if (pattern_node.is<StructDeclarationNode>()) {
				pattern_struct_ptr = &pattern_node.as<StructDeclarationNode>();
			} else if (pattern_node.is<TemplateClassDeclarationNode>()) {
				// Member template partial specialization - extract the inner struct
				pattern_struct_ptr = &pattern_node.as<TemplateClassDeclarationNode>().class_decl_node();
			} else {
				FLASH_LOG(Templates, Error, "Pattern node is not a StructDeclarationNode or TemplateClassDeclarationNode");
				return std::nullopt;
			}

			StructDeclarationNode& pattern_struct = *pattern_struct_ptr;
			FLASH_LOG(Templates, Debug, "Pattern struct name: ", pattern_struct.name());

			// Register the mapping from instantiated name to pattern name
			// This allows member alias lookup to find the correct specialization
			if (shouldCommitTemplateInstantiationArtifacts()) {
				gTemplateRegistry.register_instantiation_pattern(
					instantiated_name,
					pattern_struct.name(),
					StringTable::getOrInternStringHandle(template_name));
			}

			// Get template parameters from the pattern (partial specialization), NOT the primary template
			// The pattern stores its own template parameters (e.g., <typename First, typename... Rest>)
			InlineVector<TemplateParameterNode, 4> pattern_template_params;
			auto patterns_it = gTemplateRegistry.specialization_patterns_.find(template_name);
			if (patterns_it != gTemplateRegistry.specialization_patterns_.end()) {
				// Find the matching pattern to get its template params
				for (const auto& pattern : patterns_it->second) {
					// Handle both StructDeclarationNode and TemplateClassDeclarationNode patterns
					const StructDeclarationNode* spec_struct_ptr = nullptr;
					if (pattern.specialized_node.is<StructDeclarationNode>()) {
						spec_struct_ptr = &pattern.specialized_node.as<StructDeclarationNode>();
					} else if (pattern.specialized_node.is<TemplateClassDeclarationNode>()) {
						spec_struct_ptr = &pattern.specialized_node.as<TemplateClassDeclarationNode>().class_decl_node();
					}
					if (spec_struct_ptr && spec_struct_ptr == &pattern_struct) {
						pattern_template_params = pattern.template_params;
						break;
					}
				}
			}

			// Fall back to primary template params if pattern params not found
			if (pattern_template_params.empty()) {
				// Check ALL template overloads to find one with named parameters
				// Forward declarations like `template<typename...> class tuple;` register with
				// anonymous names (e.g., __anon_type_64), while definitions have real names (e.g., _Elements).
				// Prefer the definition's parameters for correct sizeof...() resolution.
				const auto* all_tmpls = gTemplateRegistry.lookupAllTemplates(template_name);
				if (all_tmpls) {
					const TemplateClassDeclarationNode* best = nullptr;
					for (const auto& tmpl_node : *all_tmpls) {
						if (tmpl_node.is<TemplateClassDeclarationNode>()) {
							const auto& tmpl_class = tmpl_node.as<TemplateClassDeclarationNode>();
							if (!best) {
								best = &tmpl_class;
							} else {
								// Prefer template with named parameters (not __anon_type_)
								bool current_has_anon = false;
								bool best_has_anon = false;
								for (const auto& param : tmpl_class.template_parameters()) {
									if (param.name().starts_with("__anon_type_"))
										current_has_anon = true;
								}
								for (const auto& param : best->template_parameters()) {
									if (param.name().starts_with("__anon_type_"))
										best_has_anon = true;
								}
								if (best_has_anon && !current_has_anon)
									best = &tmpl_class;
							}
						}
					}
					if (best) {
						pattern_template_params = best->template_parameters();
					}
				}
			}
			const auto& template_params = pattern_template_params;

			InlineVector<TemplateTypeArg, 4> pattern_args_for_member_copy;
			if (patterns_it != gTemplateRegistry.specialization_patterns_.end()) {
				for (const auto& pattern : patterns_it->second) {
					const StructDeclarationNode* spec_struct_ptr = nullptr;
					if (pattern.specialized_node.is<StructDeclarationNode>()) {
						spec_struct_ptr = &pattern.specialized_node.as<StructDeclarationNode>();
					} else if (pattern.specialized_node.is<TemplateClassDeclarationNode>()) {
						spec_struct_ptr = &pattern.specialized_node.as<TemplateClassDeclarationNode>().class_decl_node();
					}
					if (spec_struct_ptr &&
						(spec_struct_ptr == &pattern_struct || spec_struct_ptr->name() == pattern_struct.name())) {
						pattern_args_for_member_copy = pattern.pattern_args;
						break;
					}
				}
			}
			std::vector<TemplateTypeArg> template_args_for_member_copy_storage;
			if (!pattern_args_for_member_copy.empty()) {
				template_args_for_member_copy_storage.reserve(template_params.size());
				// Use filled_args_for_pattern_match (primary-template-aligned, with defaults filled
				// in) as the source of concrete values and as the loop upper bound.  Using the raw
				// template_args span instead caused a deduction failure when the leading pattern
				// arguments are concrete (e.g. enable_if<true, T>) and the instantiation relies on
				// default arguments (e.g. enable_if<true> -> T=void): the loop would terminate
				// before reaching the dependent position, leaving the storage empty and falling
				// back to the wrong arg vector.
				std::span<const TemplateTypeArg> concrete_args = filled_args_for_pattern_match;
				for (size_t template_param_slot = 0; template_param_slot < template_params.size(); ++template_param_slot) {
					std::optional<TemplateTypeArg> deduced_arg;
					for (size_t pattern_idx = 0;
						 pattern_idx < pattern_args_for_member_copy.size() && pattern_idx < concrete_args.size();
						 ++pattern_idx) {
						const TemplateTypeArg& pattern_arg = pattern_args_for_member_copy[pattern_idx];
						if (!pattern_arg.is_dependent) {
							continue;
						}

						size_t dependent_param_index = 0;
						for (size_t i = 0; i < pattern_idx; ++i) {
							if (pattern_args_for_member_copy[i].is_dependent) {
								dependent_param_index++;
							}
						}

						if (dependent_param_index == template_param_slot) {
							if (template_params[template_param_slot].is_variadic()) {
								for (size_t pack_idx = pattern_idx; pack_idx < concrete_args.size(); ++pack_idx) {
									template_args_for_member_copy_storage.push_back(
										deduceArgFromPattern(concrete_args[pack_idx], pattern_arg));
								}
								deduced_arg.reset();
								break;
							}
							deduced_arg = deduceArgFromPattern(concrete_args[pattern_idx], pattern_arg);
							break;
						}
					}

					if (deduced_arg.has_value()) {
						template_args_for_member_copy_storage.push_back(*deduced_arg);
					}
				}
			}
			std::span<const TemplateTypeArg> template_args_for_member_copy =
				template_args_for_member_copy_storage.empty() ? template_args : std::span<const TemplateTypeArg>(template_args_for_member_copy_storage);
			// Push class template pack info for specialization path
			ClassTemplatePackGuard spec_pack_guard(class_template_pack_stack_);
			bool has_spec_pack_info = false;
			{
				std::vector<ClassTemplatePackInfo> pack_infos;
				size_t non_variadic_count = 0;
				for (size_t i = 0; i < template_params.size(); ++i) {
					const auto& tparam = template_params[i];
					if (tparam.is_variadic()) {
						size_t pack_size = template_args.size() >= non_variadic_count
											   ? template_args.size() - non_variadic_count
											   : 0;
						pack_infos.push_back({tparam.name(), pack_size});
					} else {
						non_variadic_count++;
					}
				}
				if (!pack_infos.empty()) {
					spec_pack_guard.push(std::move(pack_infos));
					has_spec_pack_info = true;
				}
			}

			// Create a new struct with the instantiated name
			// Copy members from the pattern, substituting template parameters
			// For now, if members use template parameters, we substitute them

			// Resolve namespace from template_name (declaration-site, not instantiation-site).
			// For qualified names like "std::vector", extract namespace from the name itself.
			// For unqualified names like "Vec", derive from pattern_struct.name() which may
			// store the qualified form "math::Vec" (same approach as the primary template path
			// which uses class_decl.name()).
			NamespaceHandle spec_decl_ns = gSymbolTable.get_current_namespace_handle();
			{
				if (template_name.find("::") != std::string_view::npos) {
					spec_decl_ns = QualifiedIdentifier::fromQualifiedName(template_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
				} else {
					std::string_view decl_name = StringTable::getStringView(pattern_struct.name());
					if (size_t pos = decl_name.rfind("::"); pos != std::string_view::npos) {
						spec_decl_ns = QualifiedIdentifier::fromQualifiedName(decl_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
					}
				}
			}

			// Create struct type info first
			TypeInfo& struct_type_info = add_struct_type(instantiated_name, spec_decl_ns);

			// Store template instantiation metadata for O(1) lookup (Phase 6)
			auto template_args_info = toTemplateArgInfoList(template_args);
			struct_type_info.setTemplateInstantiationInfo(
				QualifiedIdentifier::fromQualifiedName(template_name, spec_decl_ns),
				template_args_info);

 // Populate type-owned instantiation context
			struct_type_info.setInstantiationContext(
				collectParamNameHandles(template_params, template_args.size()),
				template_args_info, nullptr);

			// Register class template pack sizes in persistent registry for specializations
			// Only register if this specialization actually has variadic parameters
			if (has_spec_pack_info) {
				class_template_pack_registry_[instantiated_name] = class_template_pack_stack_.back();
			}

			auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, pattern_struct.default_access(), pattern_struct.is_union(), spec_decl_ns);

			// Handle base classes from the pattern
			// Base classes need to be instantiated with concrete template arguments
			FLASH_LOG(Templates, Debug, "Pattern has ", pattern_struct.base_classes().size(), " base classes");
			for (const auto& pattern_base : pattern_struct.base_classes()) {
				// IMPORTANT: pattern_base.name might be a string_view pointing to freed memory!
				// Convert to string immediately to avoid issues
				std::string base_name_str;
				try {
					base_name_str = std::string(pattern_base.name); // Convert to string to avoid string_view issues
				} catch (...) {
					FLASH_LOG(Templates, Error, "Failed to convert base class name to string!");
					continue;
				}

				// Check if base_name_str is valid (not empty and printable)
				if (base_name_str.empty()) {
					FLASH_LOG(Templates, Error, "Base class name is empty!");
					continue;
				}

				FLASH_LOG(Templates, Debug, "Processing base class: ", base_name_str);

				// NEW: Check if the base class IS a template parameter name (like T1, T2)
				// If so, substitute it with the corresponding template argument
				// This handles patterns like: template<typename T1, typename T2> struct __or_<T1, T2> : T1 { };
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					const TemplateParameterNode& param = template_params[i];
					if (param.name() == base_name_str) {
						// The base class is a template parameter - substitute with the corresponding argument
						const TemplateTypeArg& arg = template_args[i];

						// Get the concrete type name for this argument
						std::string substituted_name = arg.toString();
						FLASH_LOG(Templates, Debug, "Substituting base class template parameter '", base_name_str,
								  "' with '", substituted_name, "'");
						base_name_str = substituted_name;
						break;
					}
				}

				// WORKAROUND: If the base class name is an incomplete template instantiation, it was instantiated
				// during pattern parsing with template parameters. We need to re-instantiate
				// it with the concrete template arguments.
				// Use TypeInfo metadata to detect incomplete instantiations and extract the base template name.
				StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_name_str);
				auto incomplete_type_it = getTypesByNameMap().find(base_name_handle);
				bool base_is_incomplete = incomplete_type_it != getTypesByNameMap().end() && incomplete_type_it->second->is_incomplete_instantiation_;
				if (base_is_incomplete && incomplete_type_it->second->isTemplateInstantiation()) {
					std::string_view base_template_name = StringTable::getStringView(
						incomplete_type_it->second->baseTemplateName());

					// For partial specialization like Tuple<First, Rest...> : Tuple<Rest...>
					// The base class uses Rest... (the variadic pack), which corresponds to
					// all template args EXCEPT the first one (First)

					// Check if this pattern uses a variadic pack for the base
					bool base_uses_variadic_pack = false;
					size_t first_variadic_index = template_params.size();
					for (size_t i = 0; i < template_params.size(); ++i) {
						if (template_params[i].is_variadic()) {
							first_variadic_index = i;
							base_uses_variadic_pack = true;
							break;
						}
					}

					std::vector<TemplateTypeArg> base_template_args;
					if (base_uses_variadic_pack && template_args.size() > first_variadic_index) {
						// Skip the non-variadic parameters (First) and use Rest...
						// For Tuple<int>: template_args = [int], first_variadic_index = 1
						// So base_template_args = [] (empty)
						// For Tuple<int, float>: template_args = [int, float], first_variadic_index = 1
						// So base_template_args = [float]
						for (size_t i = first_variadic_index; i < template_args.size(); ++i) {
							base_template_args.push_back(template_args[i]);
						}
					} else if (base_uses_variadic_pack) {
						// Empty variadic pack - base_template_args stays empty
						// For Tuple<int>: template_args = [int], first_variadic_index = 1
						// base_template_args = [] (Tuple<>)
					} else {
						throw InternalError("Base template argument mapping missing for non-variadic base instantiation");
					}

					FLASH_LOG(Templates, Debug, "Base class instantiation: ", base_template_name, " with ", base_template_args.size(), " args");
					base_name_str = std::string(instantiateAndResolveBaseName(base_template_name, base_template_args, false));
					FLASH_LOG(Templates, Debug, "Base class resolved to: ", base_name_str);
				}

				// Convert string_view to permanent string using StringTable
				StringHandle base_class_handle = StringTable::getOrInternStringHandle(base_name_str);
				std::string_view base_class_name = StringTable::getStringView(base_class_handle);

				// Look up the base class type
				auto base_type_it = getTypesByNameMap().find(base_class_handle);
				if (base_type_it != getTypesByNameMap().end()) {
					const TypeInfo* base_type_info = base_type_it->second;
					struct_info->addBaseClass(base_class_name, base_type_info->type_index_, pattern_base.access, pattern_base.is_virtual);
				} else {
					FLASH_LOG(Templates, Error, "Base class ", base_class_name, " not found in getTypesByNameMap()");
				}
			}

			// Handle deferred template base classes from the pattern (added by parse_member_struct_template_base_class_list)
			if (!pattern_struct.deferred_template_base_classes().empty()) {
				FLASH_LOG_FORMAT(Templates, Debug, "Pattern '{}' has {} deferred template base classes",
								 StringTable::getStringView(pattern_struct.name()),
								 pattern_struct.deferred_template_base_classes().size());

				TemplateArgSubstitutionMap spec_name_subst_map;
				TemplateArgPackSubstitutionMap spec_pack_subst_map;
				buildTemplateArgSubstitutionMaps(
					template_params,
					template_args_for_member_copy,
					[](const TemplateParameterNode&, const TemplateTypeArg& arg) {
						return arg;
					},
					spec_name_subst_map,
					spec_pack_subst_map);

				for (const auto& deferred_base : pattern_struct.deferred_template_base_classes()) {
					std::string_view base_tpl_name = StringTable::getStringView(deferred_base.base_template_name);
					FLASH_LOG_FORMAT(Templates, Debug, "Processing deferred template base '{}' ({} args)",
									 base_tpl_name, deferred_base.template_arguments.size());
					DeferredBaseReplayContextScope deferred_base_replay_scope(
						*this,
						deferred_base,
						pattern_struct.name(),
						template_params,
						template_args_for_member_copy);

					DeferredBasePackExpansionBindingInfo base_pack_bindings =
						collectDeferredBasePackExpansionBindings(deferred_base, spec_pack_subst_map);
					if (base_pack_bindings.invalid) {
						FLASH_LOG(Templates, Warning, "Deferred base '", base_tpl_name,
								  "' has mismatched pack sizes in pack expansion");
						continue;
					}

					size_t expansion_count = base_pack_bindings.pack_bindings.empty()
						? 1
						: base_pack_bindings.expansion_count;
					for (size_t expansion_index = 0; expansion_index < expansion_count; ++expansion_index) {
						TemplateArgSubstitutionMap spec_subst_map =
							makeDeferredBaseExpansionSubstitutionMap(
								spec_name_subst_map,
								base_pack_bindings,
								expansion_index);

						std::vector<TemplateTypeArg> resolved_args;
						bool resolution_failed = false;
						for (const auto& arg_info : deferred_base.template_arguments) {
						if (arg_info.is_pack) {
							// Expand pack argument – empty packs are valid (base<>)
							auto try_expand = [&](StringHandle pack_name) -> bool {
								auto it = spec_pack_subst_map.find(pack_name);
								if (it != spec_pack_subst_map.end()) {
									resolved_args.insert(resolved_args.end(), it->second.begin(), it->second.end());
									return true;
								}
								return false;
							};

							bool expanded = false;
							if (arg_info.node.is<ExpressionNode>()) {
								const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
								if (const auto* template_parameter_reference = std::get_if<TemplateParameterReferenceNode>(&expr)) {
									expanded = try_expand(template_parameter_reference->param_name());
								} else if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
									StringHandle h = StringTable::getOrInternStringHandle(identifier->name());
									expanded = try_expand(h);
								}
							} else if (arg_info.node.is<TypeSpecifierNode>()) {
								TypeIndex idx = arg_info.node.as<TypeSpecifierNode>().type_index();
								if (idx.is_valid()) {
									if (const TypeInfo* idx_ti = tryGetTypeInfo(idx)) {
										expanded = try_expand(idx_ti->name_);
									}
								}
							}
							if (!expanded) {
								// Pack name not found in substitution map – skip this base class
								FLASH_LOG(Templates, Warning, "Could not resolve pack for deferred base '", base_tpl_name, "'");
								resolution_failed = true;
								break;
							}
							continue;
						}

						// Non-pack argument: try name substitution
						bool resolved = false;
						if (arg_info.node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& ts = arg_info.node.as<TypeSpecifierNode>();
							if (std::optional<TemplateTypeArg> substituted_arg =
									tryResolveDeferredBaseTypeArgFromMap(ts, spec_subst_map);
								substituted_arg.has_value()) {
								resolved_args.push_back(*substituted_arg);
								resolved = true;
							}
							if (!resolved) {
								if (const TypeInfo* ts_type_info = tryGetTypeInfo(ts.type_index());
									ts_type_info != nullptr && ts_type_info->isTemplateInstantiation()) {
									std::vector<TemplateTypeArg> instantiated_args =
										materializeTemplateArgsExpandingPacks(
											*ts_type_info,
											template_params,
											template_args_for_member_copy);
									std::string_view instantiated_base_name = instantiateAndResolveBaseName(
										StringTable::getStringView(ts_type_info->baseTemplateName()),
										instantiated_args,
										true);
									const TypeInfo* instantiated_type_info =
										findTypeByName(StringTable::getOrInternStringHandle(instantiated_base_name));
									if (instantiated_type_info != nullptr) {
										resolved_args.push_back(resolveTypeInfoToTemplateArg(*instantiated_type_info, ts));
										resolved = true;
									}
								}
							}
							if (!resolved) {
								resolved_args.emplace_back(ts);
							}
						} else if (arg_info.node.is<ExpressionNode>()) {
							const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
							if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
								std::string_view pname = std::get<TemplateParameterReferenceNode>(expr).param_name().view();
								auto it = spec_subst_map.find(pname);
								if (it != spec_subst_map.end()) {
									resolved_args.push_back(it->second);
									resolved = true;
								}
							} else if (std::holds_alternative<IdentifierNode>(expr)) {
								// Identifier that may refer to a type in the substitution map or in getTypesByNameMap()
								std::string_view iname = std::get<IdentifierNode>(expr).name();
								auto sit = spec_subst_map.find(iname);
								if (sit != spec_subst_map.end()) {
									resolved_args.push_back(sit->second);
									resolved = true;
								} else {
									StringHandle h = StringTable::getOrInternStringHandle(iname);
									auto type_it = getTypesByNameMap().find(h);
									if (type_it != getTypesByNameMap().end()) {
										TemplateTypeArg a;
										a.type_index = type_it->second->type_index_.withCategory(type_it->second->typeEnum());
										resolved_args.push_back(a);
										resolved = true;
									}
								}
							}
							if (!resolved) {
								// Helper: build a non-type value TemplateTypeArg
								auto makeValueArg = [](int64_t value, TypeIndex type_index) {
									return makeDeferredBaseValueArg(value, type_index);
								};

								// Non-type value argument - try to convert to TemplateTypeArg
								if (std::holds_alternative<NumericLiteralNode>(expr)) {
									const NumericLiteralNode& num_lit = std::get<NumericLiteralNode>(expr);
									NumericLiteralValue nv = num_lit.value();
									int64_t int_val = std::holds_alternative<unsigned long long>(nv)
														   ? static_cast<int64_t>(std::get<unsigned long long>(nv))
														   : static_cast<int64_t>(std::get<double>(nv));
									resolved_args.push_back(makeValueArg(
										int_val,
										makeDeferredBaseValueTypeIndex(num_lit.type(), TypeIndex{})));
									resolved = true;
								} else if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&expr)) {
									resolved_args.push_back(makeValueArg(
										bool_literal->value() ? 1 : 0,
										makeDeferredBaseValueTypeIndex(TypeCategory::Bool, TypeIndex{})));
									resolved = true;
								} else {
									ASTNode substituted_expr = substituteTemplateParameters(
										arg_info.node,
										template_params,
										template_args);
									auto evaluated_value = try_evaluate_constant_expression(substituted_expr);
									if (evaluated_value.has_value()) {
										resolved_args.push_back(makeValueArg(evaluated_value->value, evaluated_value->type_index));
										resolved = true;
									} else {
										// Unresolvable expression argument - cannot safely instantiate
										FLASH_LOG(Templates, Warning,
												  "Could not resolve expression arg for deferred base '",
												  base_tpl_name,
												  "' after substitution/evaluation (node type=",
												  arg_info.node.type_name(),
												  ") - skipping");
										resolution_failed = true;
									}
								}
							}
						}
					}

						if (resolution_failed) {
							FLASH_LOG(Templates, Warning, "Could not resolve args for deferred base '", base_tpl_name, "' - skipping");
							continue;
						}

						std::string_view base_inst_name = instantiateAndResolveBaseName(base_tpl_name, resolved_args, true);
						std::string_view final_base_name = base_inst_name;
						const TypeInfo* final_base_type = nullptr;
						if (!deferred_base.member_type_chain.empty()) {
							auto resolved_member_chain = resolveDeferredBaseMemberTypeChain(
								deferred_base.member_type_chain,
								spec_subst_map,
								spec_pack_subst_map,
								[&](const TypeSpecifierNode& type_spec) {
									return tryMaterializeDeferredBaseTypeArg(
										type_spec,
										template_params,
										template_args_for_member_copy,
										[&](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
											std::vector<TemplateTypeArg> mutable_concrete_base_args(
												concrete_base_args.begin(),
												concrete_base_args.end());
											return instantiateAndResolveBaseName(
												concrete_base_template_name,
												mutable_concrete_base_args,
												true);
										});
								},
								[&](const ASTNode& node) {
									return substituteTemplateParameters(node, template_params, template_args);
								},
								[&](const ASTNode& node) {
									return try_evaluate_constant_expression(node);
								});
							if (!resolved_member_chain.has_value()) {
								FLASH_LOG(Templates, Warning, "Could not resolve member template args for deferred base '", base_tpl_name, "' - skipping");
								continue;
							}
							final_base_type =
								resolveBaseClassMemberTypeChain(base_inst_name, *resolved_member_chain);
							if (final_base_type == nullptr) {
								StringBuilder unresolved_base_builder;
								unresolved_base_builder.append(base_inst_name);
								for (const QualifiedTypeMemberAccess& member_access : *resolved_member_chain) {
									unresolved_base_builder.append("::");
									unresolved_base_builder.append(StringTable::getStringView(member_access.member_name));
									if (member_access.has_template_arguments) {
										unresolved_base_builder.append("<...>");
									}
								}
								FLASH_LOG(Templates, Warning, "Deferred template base alias not found after instantiation: '",
										  unresolved_base_builder.commit(), "'");
								continue;
							}
							final_base_type = materializeDeferredBasePlaceholderIfNeeded(
								final_base_type,
								template_params,
								template_args,
								[&instantiateAndResolveBaseName](std::string_view concrete_base_template_name, std::span<TemplateTypeArg> concrete_base_args) {
									return instantiateAndResolveBaseName(concrete_base_template_name, concrete_base_args, true);
								});
							final_base_name = StringTable::getStringView(final_base_type->name());
						} else {
							StringHandle base_inst_handle = StringTable::getOrInternStringHandle(base_inst_name);
							auto base_it = getTypesByNameMap().find(base_inst_handle);
							if (base_it != getTypesByNameMap().end()) {
								final_base_type = base_it->second;
							}
						}

						if (final_base_type != nullptr) {
							struct_info->addBaseClass(final_base_name, final_base_type->type_index_, deferred_base.access, deferred_base.is_virtual);
							FLASH_LOG_FORMAT(Templates, Debug, "Added deferred template base '{}' -> '{}'", base_tpl_name, final_base_name);
						} else {
							FLASH_LOG(Templates, Warning, "Deferred template base '", base_inst_name, "' not found after instantiation");
						}
					}
				}
			}

			// Copy members from pattern
			FLASH_LOG(Templates, Debug, "Pattern struct '", pattern_struct.name(), "' has ", pattern_struct.members().size(), " members");
			for (const auto& member_decl : pattern_struct.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				FLASH_LOG(Templates, Debug, "Copying member: ", decl.identifier_token().value(),
						  " has_initializer=", member_decl.default_initializer.has_value());
				const TypeSpecifierNode& type_spec = decl.type_specifier_node();
				TypeSpecifierNode resolved_dependent_member_type_spec = type_spec;
				const TypeSpecifierNode* effective_type_spec = &type_spec;
				if (type_spec.type_index().is_valid()) {
					if (const TypeInfo* member_type_info = tryGetTypeInfo(type_spec.type_index());
						member_type_info != nullptr &&
						member_type_info->isDependentMemberType() &&
						member_type_info->hasDependentQualifiedName()) {
						ASTNode substituted_type_node = substituteTemplateParameters(
							ASTNode::emplace_node<TypeSpecifierNode>(type_spec),
							template_params,
							template_args_for_member_copy,
							struct_info->getName());
						if (substituted_type_node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& substituted_type =
								substituted_type_node.as<TypeSpecifierNode>();
							bool substituted_still_dependent = false;
							if (substituted_type.type_index().is_valid()) {
								if (const TypeInfo* substituted_type_info =
										tryGetTypeInfo(substituted_type.type_index())) {
									substituted_still_dependent =
										substituted_type_info->isDependentPlaceholder();
								}
							}
							if (!substituted_still_dependent) {
								resolved_dependent_member_type_spec = substituted_type;
								effective_type_spec = &resolved_dependent_member_type_spec;
							}
						}
					}
				}

				// For pattern specializations, member types need substitution!
				// The pattern has T* (Type::UserDefined with ptr_depth=1)
				// We need to substitute T with the concrete type (e.g., int)

				// For pattern specializations, member types need substitution!
				// Use substitute_template_parameter to properly match template parameters by name
				TypeIndex member_type_index = substitute_template_parameter(
					*effective_type_spec, template_params, template_args_for_member_copy);
				size_t ptr_depth = effective_type_spec->pointer_depth();
				ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(member_type_index);
				std::vector<size_t> resolved_array_dimensions = resolve_array_dimensions(
					decl, template_params, template_args_for_member_copy);
				if (resolved_array_dimensions.empty()) {
					resolved_array_dimensions = resolved_member_alias.array_dimensions;
				}
				bool is_array_member = !resolved_array_dimensions.empty();
				TypeIndex stored_member_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : member_type_index;

				// Calculate member size accounting for pointer depth
				TypeIndex member_size_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : member_type_index;
				size_t member_size = get_substituted_type_size_bytes(member_size_type_index);
				if (ptr_depth > 0 || effective_type_spec->is_reference() || effective_type_spec->is_rvalue_reference()) {
					// Pointers and references are always 8 bytes (64-bit)
					member_size = 8;
				}
				for (size_t dim_size : resolved_array_dimensions) {
					member_size *= dim_size;
				}
				// Calculate member alignment
				// For pointers and references, use 8-byte alignment (pointer alignment on x64)
				size_t member_alignment = get_type_alignment(member_type_index.category(), member_size);
				if (ptr_depth > 0 || effective_type_spec->is_reference() || effective_type_spec->is_rvalue_reference()) {
					member_alignment = 8; // Pointer/reference alignment on x64
				} else if (const StructTypeInfo* member_struct_info = tryGetStructTypeInfo(member_size_type_index)) {
					member_alignment = member_struct_info->alignment;
				}

				ReferenceQualifier ref_qual = effective_type_spec->reference_qualifier();

				// Substitute template parameters in default member initializers
				std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
					member_decl.default_initializer, template_args_for_member_copy, template_params);

				// For function pointer members instantiated from a template parameter (e.g., F func
				// where F=int(*)(int)), the pattern TypeSpecifierNode won't have a function_signature
				// — it only carries the placeholder.  Retrieve it from the matching TemplateTypeArg.
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				struct_info->addMember(
					member_name_handle,
					stored_member_type_index,
					member_size,
					member_alignment,
					member_decl.access,
					substituted_default_initializer,
					ref_qual,
					ref_qual != ReferenceQualifier::None ? get_substituted_type_size_bits(member_type_index) : 0,
					is_array_member,
					std::move(resolved_array_dimensions),
					static_cast<int>(ptr_depth),
					resolve_bitfield_width(member_decl, template_params, template_args_for_member_copy),
					resolveTemplateFunctionPointerSignature(
						*effective_type_spec,
						member_type_index,
						template_params,
						template_args_for_member_copy),
					member_decl.is_no_unique_address);
			}

			SourceMemberIdentityMaps struct_info_source_member_identity_maps;

			// Copy member functions from pattern
			for (StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
				if (mem_func.is_constructor) {
					// Handle constructor - create a substituted copy so StructTypeInfo
					// has correct parameter types for name mangling and codegen.
					const ConstructorDeclarationNode& orig_ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name, orig_ctor.name());
					setOuterTemplateBindingsFromParams(new_ctor_ref, template_params, template_args_for_member_copy);
					if (!orig_ctor.is_materialized() || orig_ctor.has_template_body_position()) {
						new_ctor_ref.set_template_parameters(orig_ctor.template_parameters());
					}
					if (orig_ctor.has_template_body_position()) {
						new_ctor_ref.set_template_body_position(orig_ctor.template_body_position());
						if (orig_ctor.has_template_initializer_list_position()) {
							new_ctor_ref.set_template_initializer_list_position(
								orig_ctor.template_initializer_list_position());
						}
					}
					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyParams(
						orig_ctor.parameter_nodes(),
						new_ctor_ref,
						template_params,
						template_args_for_member_copy,
						orig_ctor.is_implicit()
							? makeImplicitCtorSelfTypeRewrite(
								pattern_struct.name(),
								struct_type_info.registeredTypeIndex().withCategory(TypeCategory::Struct))
							: std::nullopt);
					substituteAndCopyInitializers(
						orig_ctor,
						new_ctor_ref,
						template_params,
						template_args_for_member_copy);
					if (orig_ctor.is_materialized()) {
						new_ctor_ref.set_definition(substituteTemplateParameters(
							*orig_ctor.get_definition(),
							template_params,
							template_args_for_member_copy));
					}
					pack_param_info_.resize(saved_pack_info);
					new_ctor_ref.set_is_implicit(orig_ctor.is_implicit());
					new_ctor_ref.set_is_explicitly_defaulted(orig_ctor.is_explicitly_defaulted());
					new_ctor_ref.set_noexcept(orig_ctor.is_noexcept());
					struct_info->addConstructor(new_ctor_node, mem_func.access);
					registerSourceMemberStubIdentity(
						struct_info_source_member_identity_maps,
						mem_func.function_declaration,
						new_ctor_node);
				} else if (mem_func.is_destructor) {
					// Handle destructor
					struct_info->addDestructor(
						mem_func.function_declaration,
						mem_func.access,
						mem_func.is_virtual);
				} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
					// Member function template (e.g., template<typename _Up, typename... _Args> void construct(...))
					// Add as-is without return type substitution - the template will handle it when instantiated
					const TemplateFunctionDeclarationNode& tmpl_func = mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
					const FunctionDeclarationNode& inner_func = tmpl_func.function_decl_node();
					StringHandle func_name_handle = inner_func.decl_node().identifier_token().handle();
					struct_info->addMemberFunction(
						func_name_handle,
						mem_func.function_declaration,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				} else {
					const FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
					const DeclarationNode& orig_decl = orig_func.decl_node();

					SubstitutedMemberFunctionShell shell = createSubstitutedMemberFunctionShell(
						orig_func,
						orig_decl.type_node(),
						orig_decl.identifier_token(),
						instantiated_name,
						template_params,
						template_args_for_member_copy,
						nullptr,
						TypeIndex{}, // No self-owner rewrite for this specialization member-copy path
						TypeIndex{}, // No pre-resolved return TypeIndex override
						mem_func.operator_kind,
						StringHandle{},	   // Compute effective lookup name from substituted return type/operator kind
						pattern_struct.name(), // Enable partial-pattern pointer-depth clamp
						false,				   // Do not re-apply bound metadata to full substitution
						false);			   // Do not force resolved TypeIndex onto full AST substitutions
					ASTNode new_func_node = shell.function_node;
					FunctionDeclarationNode& new_func_ref = *shell.function;

					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyMemberFunctionParameters(
						orig_func.parameter_nodes(),
						new_func_ref,
						template_params,
						template_args_for_member_copy,
						nullptr,
						TypeIndex{},
						TypeIndex{},
						TypeIndex{},
						SubstitutedDefaultArgumentPolicy::SubstituteTemplateParameters,
						true,  // Preserve declared parameter cv-qualifier in this path
						false, // Do not re-apply bound metadata to full substitution
						false, // Do not force resolved TypeIndex onto full AST substitutions
						false);// Plain member path does not need dependent member-template signature preservation

					copy_function_properties(new_func_ref, orig_func);
					// Ensure is_const_member_function is set from pattern so propagateAstProperties derives cv_qualifier.
					new_func_ref.set_is_const_member_function(mem_func.is_const());
					new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
					if (orig_func.is_materialized()) {
						ASTNode substituted_body = substituteTemplateParameters(
							*orig_func.get_definition(),
							template_params,
							template_args_for_member_copy);
						if (orig_func.is_static()) {
							substituted_body = rebindStaticMemberInitializerFunctionCalls(
								substituted_body,
								struct_info.get(),
								true);
						}
						new_func_ref.set_definition(substituted_body);
					}
					pack_param_info_.resize(saved_pack_info);

					// Add the function to the struct info (with substituted signature)
					struct_info->addMemberFunction(
						shell.effective_name,
						new_func_node,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
					// cv_qualifier and is_noexcept are now auto-derived by propagateAstProperties
				}
			}

			struct_info->needs_default_constructor = !struct_info->hasAnyConstructor();

			// Copy deleted special member function flags from the pattern AST node
			// This is especially important for partial specializations where deleted constructors
			// are tracked in the AST node but not yet in StructTypeInfo
			FLASH_LOG(Templates, Debug, "Checking pattern AST node for deleted constructors: default=",
					  pattern_struct.has_deleted_default_constructor(), ", copy=",
					  pattern_struct.has_deleted_copy_constructor(), ", move=",
					  pattern_struct.has_deleted_move_constructor());
			if (pattern_struct.has_deleted_default_constructor()) {
				struct_info->has_deleted_default_constructor = true;
				struct_info->has_deleted_constructor = true;
				FLASH_LOG(Templates, Debug, "Copied has_deleted_default_constructor from pattern AST node");
			}
			if (pattern_struct.has_deleted_copy_constructor()) {
				struct_info->has_deleted_copy_constructor = true;
				struct_info->has_deleted_constructor = true;
			}
			if (pattern_struct.has_deleted_move_constructor()) {
				struct_info->has_deleted_move_constructor = true;
				struct_info->has_deleted_constructor = true;
			}

			// Also copy deleted constructor flags from the pattern's StructTypeInfo (if available)
			// Get the pattern's StructTypeInfo
			auto pattern_type_it = getTypesByNameMap().find(pattern_struct.name());
			if (pattern_type_it != getTypesByNameMap().end()) {
				const TypeInfo* pattern_type_info = pattern_type_it->second;
				const StructTypeInfo* pattern_struct_info = pattern_type_info->getStructInfo();
				if (pattern_struct_info) {
					// Copy deleted constructor flags from pattern
					if (pattern_struct_info->has_deleted_default_constructor) {
						struct_info->has_deleted_default_constructor = true;
					}
					if (pattern_struct_info->has_deleted_copy_constructor) {
						struct_info->has_deleted_copy_constructor = true;
					}
					if (pattern_struct_info->has_deleted_move_constructor) {
						struct_info->has_deleted_move_constructor = true;
					}
					if (pattern_struct_info->has_deleted_copy_assignment) {
						struct_info->has_deleted_copy_assignment = true;
					}
					if (pattern_struct_info->has_deleted_move_assignment) {
						struct_info->has_deleted_move_assignment = true;
					}
					if (pattern_struct_info->has_deleted_constructor) {
						struct_info->has_deleted_constructor = true;
					}
					if (pattern_struct_info->has_deleted_destructor) {
						struct_info->has_deleted_destructor = true;
					}
					FLASH_LOG(Templates, Debug, "Copied deleted constructor flags from pattern StructTypeInfo: default=",
							  pattern_struct_info->has_deleted_default_constructor, ", copy=",
							  pattern_struct_info->has_deleted_copy_constructor);
					synthesize_implicit_copy_constructor_if_needed(
						*struct_info,
						struct_type_info.registeredTypeIndex().withCategory(TypeCategory::Struct),
						instantiated_name);

					FLASH_LOG(Templates, Debug, "Copying ", pattern_struct_info->static_members.size(), " static members from pattern");
					for (const auto& static_member : pattern_struct_info->static_members) {
						FLASH_LOG(Templates, Debug, "Copying static member: ", static_member.getName());

						// Check if initializer contains sizeof...(pack_name) and substitute with pack size
						std::optional<ASTNode> substituted_initializer = static_member.initializer;
						if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
							const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
							FLASH_LOG(Templates, Debug, "Static member initializer is an expression, checking for sizeof...");

							// Calculate pack size for substitution
							auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
								FLASH_LOG(Templates, Debug, "Looking for pack: ", pack_name);
								for (size_t i = 0; i < template_params.size(); ++i) {
									const TemplateParameterNode& tparam = template_params[i];
									FLASH_LOG(Templates, Debug, "  Checking param ", tparam.name(), " is_variadic=", tparam.is_variadic() ? "true" : "false");
									if (tparam.name() == pack_name && tparam.is_variadic()) {
										size_t non_variadic_count = 0;
										for (const auto& param : template_params) {
											if (!param.is_variadic()) {
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
									NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), TypeCategory::Int, TypeQualifier::None, 32));
							};

							if (const auto* sizeof_pack_ptr = std::get_if<SizeofPackNode>(&expr)) {
								// Direct sizeof... expression
								const SizeofPackNode& sizeof_pack = *sizeof_pack_ptr;
								if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
									substituted_initializer = make_pack_size_literal(*pack_size);
								}
							} else if (std::holds_alternative<StaticCastNode>(expr)) {
								// Handle static_cast<T>(sizeof...(Ts)) patterns
								const StaticCastNode& cast_node = std::get<StaticCastNode>(expr);
								if (cast_node.expr().is<ExpressionNode>()) {
									const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
									if (const auto* cast_sizeof_pack_ptr = std::get_if<SizeofPackNode>(&cast_inner)) {
										const SizeofPackNode& sizeof_pack = *cast_sizeof_pack_ptr;
										if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
											substituted_initializer = make_pack_size_literal(*pack_size);
										}
									}
								}
							} else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
								// Binary expression like "1 + sizeof...(Rest)" - need to substitute sizeof...
								const BinaryOperatorNode& bin_expr = std::get<BinaryOperatorNode>(expr);

								// Helper to extract pack size from various expression forms
								auto try_extract_pack_size = [&](const ExpressionNode& e) -> std::optional<size_t> {
									if (const auto* inner_sizeof_pack_ptr = std::get_if<SizeofPackNode>(&e)) {
										const SizeofPackNode& sizeof_pack = *inner_sizeof_pack_ptr;
										return calculate_pack_size(sizeof_pack.pack_name());
									}
									// Handle static_cast<T>(sizeof...(Ts))
									if (std::holds_alternative<StaticCastNode>(e)) {
										const StaticCastNode& cast_node = std::get<StaticCastNode>(e);
										if (cast_node.expr().is<ExpressionNode>()) {
											const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
											if (const auto* inner_sizeof_pack_ptr2 = std::get_if<SizeofPackNode>(&cast_inner)) {
												const SizeofPackNode& sizeof_pack = *inner_sizeof_pack_ptr2;
												return calculate_pack_size(sizeof_pack.pack_name());
											}
										}
									}
									return std::nullopt;
								};

								// Helper to extract numeric value from expression
								auto try_extract_numeric = [](const ExpressionNode& e) -> std::optional<unsigned long long> {
									if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&e)) {
										const NumericLiteralNode& num = *numeric_literal;
										auto val = num.value();
										return std::holds_alternative<unsigned long long>(val)
												   ? std::get<unsigned long long>(val)
												   : static_cast<unsigned long long>(std::get<double>(val));
									}
									return std::nullopt;
								};

								// Helper to evaluate a binary expression
								auto evaluate_binary = [](std::string_view op, unsigned long long lhs, unsigned long long rhs) -> unsigned long long {
									if (op == "+")
										return lhs + rhs;
									if (op == "-")
										return lhs - rhs;
									if (op == "*")
										return lhs * rhs;
									if (op == "/")
										return rhs != 0 ? lhs / rhs : 0;
									return 0;
								};

								// Try to evaluate the top-level binary expression
								if (bin_expr.get_lhs().is<ExpressionNode>() && bin_expr.get_rhs().is<ExpressionNode>()) {
									const ExpressionNode& lhs_expr = bin_expr.get_lhs().as<ExpressionNode>();
									const ExpressionNode& rhs_expr = bin_expr.get_rhs().as<ExpressionNode>();

									// Case 1: LHS is pack_size_expr, RHS is numeric
									if (auto lhs_pack = try_extract_pack_size(lhs_expr)) {
										if (auto rhs_num = try_extract_numeric(rhs_expr)) {
											unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_pack, *rhs_num);
											substituted_initializer = make_pack_size_literal(result);
										}
									}
									// Case 2: LHS is numeric, RHS is pack_size_expr
									else if (auto lhs_num = try_extract_numeric(lhs_expr)) {
										if (auto rhs_pack = try_extract_pack_size(rhs_expr)) {
											unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_num, *rhs_pack);
											substituted_initializer = make_pack_size_literal(result);
										}
									}
									// Case 3: LHS is nested binary expression, RHS is numeric
									// Handles patterns like (static_cast<int>(sizeof...(Ts)) * 2) + 40
									else if (std::holds_alternative<BinaryOperatorNode>(lhs_expr)) {
										const BinaryOperatorNode& nested_bin = std::get<BinaryOperatorNode>(lhs_expr);
										if (nested_bin.get_lhs().is<ExpressionNode>() && nested_bin.get_rhs().is<ExpressionNode>()) {
											const ExpressionNode& nested_lhs = nested_bin.get_lhs().as<ExpressionNode>();
											const ExpressionNode& nested_rhs = nested_bin.get_rhs().as<ExpressionNode>();

											std::optional<unsigned long long> nested_result;
											if (auto nlhs_pack = try_extract_pack_size(nested_lhs)) {
												if (auto nrhs_num = try_extract_numeric(nested_rhs)) {
													nested_result = evaluate_binary(nested_bin.op(), *nlhs_pack, *nrhs_num);
												}
											} else if (auto nlhs_num = try_extract_numeric(nested_lhs)) {
												if (auto nrhs_pack = try_extract_pack_size(nested_rhs)) {
													nested_result = evaluate_binary(nested_bin.op(), *nlhs_num, *nrhs_pack);
												}
											}

											if (nested_result) {
												if (auto rhs_num = try_extract_numeric(rhs_expr)) {
													unsigned long long result = evaluate_binary(bin_expr.op(), *nested_result, *rhs_num);
													substituted_initializer = make_pack_size_literal(result);
												}
											}
										}
									}
								}
							}
							// Handle template parameter reference substitution (e.g., static constexpr T value = v;)
							if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
								const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
								FLASH_LOG(Templates, Debug, "Static member initializer contains template parameter reference: ", tparam_ref.param_name());
								if (auto subst = substitute_template_param_in_initializer(tparam_ref.param_name().view(), template_args, template_params)) {
									substituted_initializer = subst;
									FLASH_LOG(Templates, Debug, "Substituted static member initializer template parameter '", tparam_ref.param_name(), "'");
								}
							}
							// Handle IdentifierNode that might be a template parameter
							else if (std::holds_alternative<IdentifierNode>(expr)) {
								const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
								std::string_view id_name = id_node.name();
								FLASH_LOG(Templates, Debug, "Static member initializer contains IdentifierNode: ", id_name);
								if (auto subst = substitute_template_param_in_initializer(id_name, template_args, template_params)) {
									substituted_initializer = subst;
									FLASH_LOG(Templates, Debug, "Substituted static member initializer identifier '", id_name, "' (template parameter)");
								}
							}
							// Handle FoldExpressionNode (e.g., static constexpr bool value = (Bs && ...);)
							else if (std::holds_alternative<FoldExpressionNode>(expr)) {
								const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
								std::string_view pack_name = fold.pack_name();
								std::string_view op = fold.op();
								FLASH_LOG(Templates, Debug, "Static member initializer contains fold expression with pack: ", pack_name, " op: ", op);

								// Find the parameter pack in template parameters
								std::optional<size_t> pack_param_idx;
								for (size_t p = 0; p < template_params.size(); ++p) {
									const TemplateParameterNode& tparam = template_params[p];
									if (tparam.name() == pack_name && tparam.is_variadic()) {
										pack_param_idx = p;
										break;
									}
								}

								if (pack_param_idx.has_value()) {
									// Collect the values from the variadic pack arguments
									std::vector<int64_t> pack_values;
									bool all_values_found = true;

									// For variadic packs, arguments after non-variadic parameters are the pack values
									size_t non_variadic_count = 0;
									for (const auto& param : template_params) {
										if (!param.is_variadic()) {
											non_variadic_count++;
										}
									}

									for (size_t i = non_variadic_count; i < template_args.size() && all_values_found; ++i) {
										if (template_args[i].is_value) {
											pack_values.push_back(template_args[i].value);
											FLASH_LOG(Templates, Debug, "Pack value[", i - non_variadic_count, "] = ", template_args[i].value);
										} else {
											all_values_found = false;
										}
									}

									if (all_values_found && !pack_values.empty()) {
										auto fold_result = evaluate_fold_expression(op, pack_values);
										if (fold_result.has_value()) {
											substituted_initializer = *fold_result;
										}
									}
								}
							}
							// Handle TernaryOperatorNode where the condition is a template parameter (e.g., IsArith ? 42 : TypeIndex{})
							else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
								const TernaryOperatorNode& ternary = std::get<TernaryOperatorNode>(expr);
								const ASTNode& cond_node = ternary.condition();

								// Check if condition is a template parameter reference or identifier
								if (cond_node.is<ExpressionNode>()) {
									const ExpressionNode& cond_expr = cond_node.as<ExpressionNode>();
									std::optional<int64_t> cond_value;

									if (std::holds_alternative<TemplateParameterReferenceNode>(cond_expr)) {
										const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(cond_expr);
										FLASH_LOG(Templates, Debug, "Ternary condition is template parameter: ", tparam_ref.param_name());

										// Look up the parameter value
										for (size_t p = 0; p < template_params.size(); ++p) {
											const TemplateParameterNode& tparam = template_params[p];
											if (tparam.name() == tparam_ref.param_name() && tparam.kind() == TemplateParameterKind::NonType) {
												if (p < template_args.size() && template_args[p].is_value) {
													cond_value = template_args[p].value;
													FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
												}
												break;
											}
										}
									} else if (std::holds_alternative<IdentifierNode>(cond_expr)) {
										const IdentifierNode& id_node = std::get<IdentifierNode>(cond_expr);
										std::string_view id_name = id_node.name();
										FLASH_LOG(Templates, Debug, "Ternary condition is identifier: ", id_name);

										// Look up the identifier as a template parameter
										for (size_t p = 0; p < template_params.size(); ++p) {
											const TemplateParameterNode& tparam = template_params[p];
											if (tparam.name() == id_name && tparam.kind() == TemplateParameterKind::NonType) {
												if (p < template_args.size() && template_args[p].is_value) {
													cond_value = template_args[p].value;
													FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
												}
												break;
											}
										}
									}

									// If we found the condition value, evaluate the ternary
									if (cond_value.has_value()) {
										const ASTNode& result_branch = (*cond_value != 0) ? ternary.true_expr() : ternary.false_expr();

										if (result_branch.is<ExpressionNode>()) {
											const ExpressionNode& result_expr = result_branch.as<ExpressionNode>();
											if (std::holds_alternative<NumericLiteralNode>(result_expr)) {
												const NumericLiteralNode& lit = std::get<NumericLiteralNode>(result_expr);
												const auto& val = lit.value();
												unsigned long long num_val = std::holds_alternative<unsigned long long>(val)
																				 ? std::get<unsigned long long>(val)
																				 : static_cast<unsigned long long>(std::get<double>(val));

												// Create a new numeric literal with the evaluated result
												std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(num_val)).commit();
												Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
												substituted_initializer = emplace_node<ExpressionNode>(
													NumericLiteralNode(num_token, num_val, lit.type(), lit.qualifier(), lit.sizeInBits()));
												FLASH_LOG(Templates, Debug, "Evaluated ternary to: ", num_val);
											}
										}
									}
								}
							}
						}

 // Substitute template parameters in the static member initializer.
 // The sizeof... special cases above handle specific patterns, but
 // general expressions like {static_cast<T>(20), static_cast<T>(22)}
 // still need template parameter substitution (e.g., T → int).
						if (substituted_initializer.has_value()) {
							substituted_initializer = substituteTemplateParameters(
							substituted_initializer.value(), template_params, template_args);
							FLASH_LOG(Templates, Debug, "Substituted template parameters in static member '",
									  static_member.getName(), "' initializer");
						}

						// Phase 7B: Intern static member name and use StringHandle overload
						StringHandle static_member_name_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(static_member.getName()));

 // Substitute nested type references in static member type.
 // For static members whose type is a nested class of the pattern template
 // (e.g., `static constexpr Payload data`), we need to remap the type_index
 // to the instantiated nested class and use its finalized size.
						TypeIndex substituted_type_index = static_member.type_index;
						size_t substituted_size = static_member.size;
						if (static_member.type_index.category() == TypeCategory::Struct) {
							TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
							original_type_spec.set_type_index(static_member.type_index);
							TypeIndex new_type_index = substitute_template_parameter(
								original_type_spec, template_params, template_args);
							if (new_type_index != static_member.type_index) {
								substituted_type_index = new_type_index;
								substituted_size = get_substituted_type_size_bytes(new_type_index);
							} else if (substituted_size == 0) {
 // The type wasn't a template parameter, but might be a nested class
 // whose instantiated form is registered under the qualified name.
								StringBuilder qualified_nested_sb;
								if (const TypeInfo* orig_ti = tryGetTypeInfo(static_member.type_index)) {
									std::string_view orig_name = StringTable::getStringView(orig_ti->name());
 // Replace pattern prefix with instantiated prefix
 // e.g., "Container::Payload" -> "Container$hash::Payload"
									std::string_view pattern_prefix = template_name;
									if (orig_name.starts_with(pattern_prefix) &&
										orig_name.size() > pattern_prefix.size() &&
										orig_name[pattern_prefix.size()] == ':') {
										qualified_nested_sb.append(StringTable::getStringView(instantiated_name))
											.append(orig_name.substr(pattern_prefix.size()));
										StringHandle qualified_handle = StringTable::getOrInternStringHandle(qualified_nested_sb.commit());
										auto type_it = getTypesByNameMap().find(qualified_handle);
										if (type_it != getTypesByNameMap().end()) {
											substituted_type_index = type_it->second->type_index_;
											if (const StructTypeInfo* nested_si = type_it->second->getStructInfo()) {
												substituted_size = toSizeT(nested_si->sizeInBytes());
											}
										}
									}
								}
							}
						}

						if (static_member.is_array) {
							for (size_t dim_size : static_member.array_dimensions) {
								substituted_size *= dim_size;
							}
						}
						struct_info->addStaticMember(
							static_member_name_handle,
							substituted_type_index,
							substituted_size,
							static_member.alignment,
							static_member.access,
							substituted_initializer,
							static_member.cv_qualifier,
							static_member.reference_qualifier,
							static_member.pointer_depth,
							static_member.is_array,
							static_member.array_dimensions,
							static_member.declaration,
							static_member.initializer_position,
							static_member.initializerDefinitionLookupContext(),
							static_member.is_constexpr);
					}
				}
			}

			// Also copy static members from the pattern AST node (for member template partial specializations)
			// These may not have been added to StructTypeInfo yet
			auto build_effective_partial_spec_template_bindings =
				[&](std::span<const TemplateTypeArg> local_template_args) {
				InlineVector<TemplateParameterNode, 4> effective_template_params;
				std::vector<TemplateTypeArg> effective_template_args;
				if (const OuterTemplateBinding* outer_binding =
						gTemplateRegistry.getOuterTemplateBinding(template_name)) {
					const bool has_outer_params = !outer_binding->params.empty();
					effective_template_params.reserve(
						(has_outer_params
							 ? outer_binding->params.size()
							 : outer_binding->param_names.size()) +
						template_params.size());
					effective_template_args.reserve(
						(has_outer_params
							 ? outer_binding->all_args.size()
							 : outer_binding->param_args.size()) +
						local_template_args.size());
					if (has_outer_params) {
						for (const ASTNode& outer_param_node :
							 outer_binding->params) {
							if (const TemplateParameterNode* outer_param =
									tryGetTemplateParameterNode(
										outer_param_node)) {
								effective_template_params.push_back(*outer_param);
							}
						}
						effective_template_args.insert(
							effective_template_args.end(),
							outer_binding->all_args.begin(),
							outer_binding->all_args.end());
					} else {
						for (size_t i = 0;
							 i < outer_binding->param_names.size() &&
							 i < outer_binding->param_args.size();
							 ++i) {
							effective_template_params.push_back(
								rebuildOuterTemplateParameter(
									outer_binding->param_names[i],
									outer_binding->param_args[i]));
							effective_template_args.push_back(
								outer_binding->param_args[i]);
						}
					}
				} else {
					effective_template_params.reserve(template_params.size());
					effective_template_args.reserve(local_template_args.size());
				}
				for (const TemplateParameterNode& template_param :
					 template_params) {
					effective_template_params.push_back(template_param);
				}
				effective_template_args.insert(
					effective_template_args.end(),
					local_template_args.begin(),
					local_template_args.end());
				return std::pair<
					InlineVector<TemplateParameterNode, 4>,
					std::vector<TemplateTypeArg>>(
					std::move(effective_template_params),
					std::move(effective_template_args));
			};
			auto substitute_in_class_static_initializer_replay_first =
				[&](
					const auto& static_member,
					std::span<const TemplateParameterNode>
						template_params_for_substitution,
					std::span<const TemplateTypeArg> template_args_for_substitution,
					auto&& fallback_substitution) -> std::optional<ASTNode> {
				if (!static_member.initializer.has_value()) {
					return std::nullopt;
				}

				std::optional<ASTNode> substituted_initializer;
				try {
					if (static_member.initializer_position.has_value() &&
						static_member.declaration.has_value()) {
						const DeclarationNode* declaration =
							get_decl_from_symbol(*static_member.declaration);
						if (declaration != nullptr) {
							SaveHandle current_pos = save_token_position();
							ScopedLexerPositionRestore lexer_restore(*this, current_pos);

							TemplateInstantiationContext substitution_context =
								buildTemplateInstantiationContext(
									template_params_for_substitution,
									template_args_for_substitution,
									nullptr,
									currentTemplateSubstitutionFailurePolicy());
							substitution_context.current_instantiation_name =
								instantiated_name;

							TemplateDefinitionLookupContext definition_lookup_context =
								ensureReplayDefinitionLookupContext(
									static_member.initializerDefinitionLookupContext(),
									declaration->identifier_token(),
									spec_decl_ns,
									instantiated_name);
							substitution_context.definition_lookup_context =
								definition_lookup_context.is_valid()
									? &definition_lookup_context
									: nullptr;

							FlashCpp::TemplateDepthGuard guard_template_depth(parsing_template_depth_);
							parsing_template_depth_ = 1;
							ScopedDefinitionLookupContext ctx_scope(
								current_template_definition_lookup_context_,
								substitution_context.definition_lookup_context);

							InlineVector<StringHandle, 4> template_param_names;
							InlineVector<TemplateParameterKind, 4> template_param_kinds;
							InlineVector<TypeCategory, 4> non_type_categories;
							buildTemplateParameterReplayState(
								template_params_for_substitution,
								template_param_names,
								template_param_kinds,
								non_type_categories);
							FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
							setCurrentTemplateParameters(
								template_param_names,
								template_param_kinds,
								non_type_categories);

							restore_lexer_position_only(*static_member.initializer_position);

							DeclarationNode declaration_copy = *declaration;
							TypeSpecifierNode& type_spec =
								declaration_copy.type_specifier_node();

							std::optional<ASTNode> reparsed_initializer;
							if (peek() == "="_tok) {
								reparsed_initializer = parse_copy_initialization(
									declaration_copy,
									type_spec);
							} else if (peek() == "{"_tok) {
								ParseResult init_result = parse_brace_initializer(type_spec);
								if (!init_result.is_error() &&
									init_result.node().has_value()) {
									reparsed_initializer = *init_result.node();
								}
							}

							if (reparsed_initializer.has_value()) {
								substituted_initializer = substituteTemplateParameters(
									*reparsed_initializer,
									substitution_context);
							}
						}
					}
				} catch (const std::exception& ex) {
					FLASH_LOG(
						Templates,
						Debug,
						"Replay substitution failed for AST static member initializer: ",
						ex.what(),
						" — falling back to AST substitution");
					substituted_initializer.reset();
				}

				if (!substituted_initializer.has_value()) {
					substituted_initializer = fallback_substitution();
				}

				return substituted_initializer;
			};

			auto [effective_member_copy_template_params, effective_member_copy_template_args_storage] =
				build_effective_partial_spec_template_bindings(
					template_args_for_member_copy);
			std::span<const TemplateTypeArg> effective_member_copy_template_args(
				effective_member_copy_template_args_storage.data(),
				effective_member_copy_template_args_storage.size());

			if (!pattern_struct.static_members().empty()) {
				FLASH_LOG(Templates, Debug, "Copying ", pattern_struct.static_members().size(), " static members from pattern AST node");
				for (const auto& static_member : pattern_struct.static_members()) {
					FLASH_LOG(Templates, Debug, "Copying static member from AST: ", StringTable::getStringView(static_member.name));

					// Check if already added from StructTypeInfo
					if (struct_info->findStaticMember(static_member.name) != nullptr) {
						continue; // Already added
					}

					// Substitute type if it's a template parameter
					// Create a TypeSpecifierNode from the static member's type info to use substitute_template_parameter
					TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
					original_type_spec.set_type_index(static_member.type_index);

					// Use substitute_template_parameter for consistent template parameter matching
					TypeIndex substituted_type_index = substitute_template_parameter(
						original_type_spec,
						effective_member_copy_template_params,
						effective_member_copy_template_args);

					size_t substituted_size = get_substituted_type_size_bytes(substituted_type_index);
					if (static_member.is_array) {
						for (size_t dim_size : static_member.array_dimensions) {
							substituted_size *= dim_size;
						}
					}
					if (static_member.is_array) {
						for (size_t dim_size : static_member.array_dimensions) {
							substituted_size *= dim_size;
						}
					}

					// Substitute template parameters in the static member initializer.
					// Replay from source first so definition-context lookup metadata can be restored.
					std::optional<ASTNode> substituted_initializer =
						substitute_in_class_static_initializer_replay_first(
							static_member,
							effective_member_copy_template_params,
							effective_member_copy_template_args,
							[&]() -> std::optional<ASTNode> {
								std::optional<ASTNode> fallback_initializer = static_member.initializer;
								if (!static_member.initializer.has_value()) {
									return fallback_initializer;
								}

								TemplateEnvironment substitution_environment =
									buildTemplateEnvironment(
										std::span<const TemplateParameterNode>(
											effective_member_copy_template_params.data(),
											effective_member_copy_template_params.size()),
										effective_member_copy_template_args,
										nullptr);

								// Use ExpressionSubstitutor to substitute template parameters in
								// the initializer.
								if (!substitution_environment.bindings.empty()) {
									ExpressionSubstitutor substitutor(
										substitution_environment,
										*this);
									substitutor.setCurrentOwnerTypeName(struct_info->getName());
									fallback_initializer = substitutor.substitute(
										static_member.initializer.value());
									FLASH_LOG(
										Templates,
										Debug,
										"Substituted template parameters in static member initializer");
								}
								return fallback_initializer;
							});

					struct_info->addStaticMember(
						static_member.name,
						substituted_type_index,
						substituted_size,
						static_member.alignment,
						static_member.access,
						substituted_initializer,
						static_member.cv_qualifier,
						static_member.reference_qualifier,
						static_member.pointer_depth,
						static_member.is_array,
						static_member.array_dimensions,
						static_member.declaration,
						static_member.initializer_position,
						static_member.initializerDefinitionLookupContext(),
						static_member.is_constexpr);
				}
			}

			// Finalize the struct layout
			bool finalize_success;
			if (!pattern_struct.base_classes().empty()) {
				finalize_success = struct_info->finalizeWithBases();
			} else {
				finalize_success = struct_info->finalize();
			}

			// Check for semantic errors during finalization
			if (!finalize_success) {
				// Log error and return nullopt - compilation will continue but template instantiation fails
				FLASH_LOG(Parser, Error, struct_info->getFinalizationError());
				return std::nullopt;
			}
			struct_type_info.setStructInfo(std::move(struct_info));
			if (struct_type_info.getStructInfo()) {
				struct_type_info.fallback_size_bits_ = struct_type_info.getStructInfo()->sizeInBits().value;
			}

			// Register type aliases from the pattern with qualified names
			// We need the pattern_args to map template parameters to template arguments
			InlineVector<TemplateTypeArg, 4> pattern_args;
			auto patterns_it_for_alias = gTemplateRegistry.specialization_patterns_.find(template_name);
			if (patterns_it_for_alias != gTemplateRegistry.specialization_patterns_.end()) {
				for (const auto& pattern : patterns_it_for_alias->second) {
					// Handle both StructDeclarationNode and TemplateClassDeclarationNode patterns
					const StructDeclarationNode* spec_struct_ptr_alias = nullptr;
					if (pattern.specialized_node.is<StructDeclarationNode>()) {
						spec_struct_ptr_alias = &pattern.specialized_node.as<StructDeclarationNode>();
					} else if (pattern.specialized_node.is<TemplateClassDeclarationNode>()) {
						spec_struct_ptr_alias = &pattern.specialized_node.as<TemplateClassDeclarationNode>().class_decl_node();
					}
					if (spec_struct_ptr_alias &&
						(spec_struct_ptr_alias == &pattern_struct || spec_struct_ptr_alias->name() == pattern_struct.name())) {
						pattern_args = pattern.pattern_args;
						break;
					}
				}
			}

			// Partial specializations receive raw concrete instantiation args
			// (e.g. [int, double] for Box<int, double>) while template_params describe
			// only the specialization's deduced parameters (e.g. [T] for Box<int, T>).
			// Build one arg vector aligned with template_params and reuse it across the
			// sema-owned AST copy path so bindings stay positional.
			std::vector<TemplateTypeArg> template_args_for_pattern_storage;
			if (!pattern_args.empty()) {
				template_args_for_pattern_storage.reserve(template_params.size());
				// Use filled_args_for_pattern_match (primary-template-aligned, with defaults filled
				// in) as the concrete-arg source and loop bound.  The raw template_args span only
				// covers explicitly supplied arguments; when a leading pattern argument is concrete
				// (e.g. enable_if<true, T>) and the trailing dependent argument maps to a
				// defaulted primary-template parameter, the old bound cut the loop short before the
				// dependent position, leaving the storage empty and causing the fallback below to
				// yield an incorrect specialization-aligned arg vector.
				std::span<const TemplateTypeArg> concrete_args = filled_args_for_pattern_match;
				for (size_t template_param_slot = 0; template_param_slot < template_params.size(); ++template_param_slot) {
					std::optional<TemplateTypeArg> deduced_arg;
					for (size_t pattern_idx = 0; pattern_idx < pattern_args.size() && pattern_idx < concrete_args.size(); ++pattern_idx) {
						const TemplateTypeArg& pattern_arg = pattern_args[pattern_idx];
						if (!pattern_arg.is_dependent) {
							continue;
						}

						size_t dependent_param_index = 0;
						for (size_t i = 0; i < pattern_idx; ++i) {
							if (pattern_args[i].is_dependent) {
								dependent_param_index++;
							}
						}

						if (dependent_param_index == template_param_slot) {
							if (template_params[template_param_slot].is_variadic()) {
								for (size_t pack_idx = pattern_idx; pack_idx < concrete_args.size(); ++pack_idx) {
									template_args_for_pattern_storage.push_back(
										deduceArgFromPattern(concrete_args[pack_idx], pattern_arg));
								}
								deduced_arg.reset();
								break;
							}
							deduced_arg = deduceArgFromPattern(concrete_args[pattern_idx], pattern_arg);
							break;
						}
					}

					if (deduced_arg.has_value()) {
						template_args_for_pattern_storage.push_back(*deduced_arg);
					}
				}
			}
			std::span<const TemplateTypeArg> template_args_for_pattern =
				template_args_for_pattern_storage.empty() ? template_args : std::span<const TemplateTypeArg>(template_args_for_pattern_storage);
			StructTypeInfo* instantiated_struct_info_mut = struct_info.get();
			const StructTypeInfo* instantiated_struct_info = instantiated_struct_info_mut;
			auto [effective_pattern_template_params, effective_pattern_template_args_storage] =
				build_effective_partial_spec_template_bindings(
					template_args_for_pattern);
			std::span<const TemplateTypeArg> effective_pattern_template_args(
				effective_pattern_template_args_storage.data(),
				effective_pattern_template_args_storage.size());

			for (const auto& type_alias : pattern_struct.type_aliases()) {
				// Build the qualified name: enable_if_true_int::type
				auto qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder()
																					 .append(instantiated_name)
																					 .append("::")
																					 .append(type_alias.alias_name));

				// Check if already registered
				if (getTypesByNameMap().find(qualified_alias_name) != getTypesByNameMap().end()) {
					continue; // Already registered
				}

				// Get the type information from the alias
				const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();

				// For partial specializations, we may need to substitute template parameters
				// For example, if pattern has "using type = T;" and we're instantiating with int,
				// we need to substitute T -> int
				TypeCategory substituted_type = alias_type_spec.type();
				TypeIndex substituted_type_index = alias_type_spec.type_index();
				int substituted_size = alias_type_spec.size_in_bits();

				trySubstituteIntrinsicTypeAlias(
					alias_type_spec,
					template_params,
					template_args_for_pattern,
					substituted_type,
					substituted_type_index,
					substituted_size);

				// Check if the alias type is a template parameter that needs substitution
				if ((alias_type_spec.category() == TypeCategory::UserDefined || alias_type_spec.category() == TypeCategory::TypeAlias || alias_type_spec.category() == TypeCategory::Template) &&
					!filled_args_for_pattern_match.empty() &&
					!pattern_args.empty()) {
					bool alias_refers_to_template_parameter = false;
					StringHandle alias_target_name{};
					if (const TypeInfo* alias_target_info = tryGetTypeInfo(alias_type_spec.type_index())) {
						alias_target_name = alias_target_info->name();
					}
					StringHandle alias_token_name = alias_type_spec.token().handle();
					auto applyTemplateArgAliasSubstitution = [&](size_t pattern_idx, std::string_view parameter_name) {
						const TemplateTypeArg& concrete_arg = filled_args_for_pattern_match[pattern_idx];
						substituted_type = concrete_arg.typeEnum();
						substituted_type_index = concrete_arg.type_index;
						if (!is_struct_type(substituted_type)) {
							substituted_size = get_type_size_bits(substituted_type);
						} else {
							substituted_size = 0;
							if (const TypeInfo* sub_ti = tryGetTypeInfo(substituted_type_index)) {
								substituted_size = sub_ti->sizeInBits().value;
							}
						}
						FLASH_LOG(Templates, Debug, "Substituted template parameter '",
								  parameter_name,
								  "' at pattern position ", pattern_idx, " with type=", static_cast<int>(substituted_type));
					};
					for (const TemplateParameterNode& template_param_node : template_params) {
						StringHandle param_name = template_param_node.nameHandle();
						if (param_name == alias_token_name || param_name == alias_target_name) {
							alias_refers_to_template_parameter = true;
							break;
						}
					}
					if (!alias_refers_to_template_parameter) {
						goto substitution_done;
					}

					// The alias_type_spec.type_index() identifies which template parameter this is
					// We need to find which pattern_arg corresponds to this template parameter,
					// then map to the corresponding template_arg

					// For enable_if<true, T>:
					// - pattern_args = [true (is_value=true), T (is_value=false, is_dependent=true)]
					// - template_params = [T] (template parameter at index 0)
					// - filled_args_for_pattern_match = [true (is_value=true), int (is_value=false)]
					// - The alias "using type = T" has T which is template_params[0]
					// - T appears at pattern_args[1]
					// - So we substitute with filled_args_for_pattern_match[1] = int

					// Find which template parameter index this alias type corresponds to
					for (size_t pattern_idx = 0; pattern_idx < pattern_args.size() && pattern_idx < filled_args_for_pattern_match.size(); ++pattern_idx) {
						const TemplateTypeArg& pattern_arg = pattern_args[pattern_idx];
						if (!pattern_arg.is_value && pattern_arg.is_dependent && pattern_arg.dependent_name.isValid()) {
							StringHandle pattern_param_name = pattern_arg.dependent_name;
							if (pattern_param_name == alias_token_name || pattern_param_name == alias_target_name) {
								applyTemplateArgAliasSubstitution(
									pattern_idx,
									StringTable::getStringView(pattern_param_name));
								goto substitution_done;
							}
						}
					}

					for (size_t param_idx = 0; param_idx < template_params.size(); ++param_idx) {
						// Find which pattern_arg position this template parameter appears at
						for (size_t pattern_idx = 0; pattern_idx < pattern_args.size() && pattern_idx < filled_args_for_pattern_match.size(); ++pattern_idx) {
							const TemplateTypeArg& pattern_arg = pattern_args[pattern_idx];

							// Check if this pattern_arg is a template parameter (not a concrete value/type)
							if (!pattern_arg.is_value && pattern_arg.is_dependent) {
								// This is a template parameter position
								// Check if it's the parameter we're looking for
								// We can match by counting dependent parameters
								size_t dependent_param_index = 0;
								for (size_t i = 0; i < pattern_idx; ++i) {
									if (!pattern_args[i].is_value && pattern_args[i].is_dependent) {
										dependent_param_index++;
									}
								}

								if (dependent_param_index == param_idx) {
									// Found it! Substitute with template_args[pattern_idx]
									applyTemplateArgAliasSubstitution(
										pattern_idx,
										template_params[param_idx].name());
									goto substitution_done;
								}
							}
						}
					}
				substitution_done:;
				}

				const TypeInfo* alias_semantic_source =
					tryGetTypeInfo(TypeIndex{substituted_type_index});
				std::optional<TypeSpecifierNode> resolved_alias_type_spec_override;
				{
					ASTNode resolved_alias_type_node =
						emplace_node<TypeSpecifierNode>(alias_type_spec);
					if (resolveDependentMemberAlias(
							resolved_alias_type_node,
							template_params,
							template_args_for_pattern) == DependentAliasResolutionStatus::Resolved) {
						const TypeSpecifierNode& resolved_alias_type_spec =
							resolved_alias_type_node.as<TypeSpecifierNode>();
						substituted_type = resolved_alias_type_spec.type();
						substituted_type_index = resolved_alias_type_spec.type_index();
						substituted_size = resolved_alias_type_spec.size_in_bits();
						alias_semantic_source =
							tryGetTypeInfo(resolved_alias_type_spec.type_index());
						resolved_alias_type_spec_override = resolved_alias_type_spec;
					}
				}
				if (const TypeInfo* dependent_alias_target_info =
						tryGetTypeInfo(TypeIndex{substituted_type_index});
					dependent_alias_target_info != nullptr &&
					dependent_alias_target_info->isDependentMemberType() &&
					dependent_alias_target_info->hasDependentQualifiedName()) {
					if (const TypeInfo* resolved_dependent_type =
							resolveDependentMemberTypeSemantic(
								*dependent_alias_target_info,
								template_params,
								template_args_for_pattern,
								instantiated_name);
						resolved_dependent_type != nullptr) {
						substituted_type = resolved_dependent_type->typeEnum();
						substituted_type_index =
							resolved_dependent_type->registeredTypeIndex().withCategory(
								resolved_dependent_type->typeEnum());
						substituted_size = resolved_dependent_type->sizeInBits().value;
						alias_semantic_source = resolved_dependent_type;
					}
				}

				if (const TypeInfo* concrete_member_info =
						materializeInstantiatedMemberAliasTarget(
							alias_type_spec,
							template_params,
							template_args_for_pattern);
					concrete_member_info != nullptr) {
					substituted_type = concrete_member_info->typeEnum();
					substituted_type_index =
						concrete_member_info->registeredTypeIndex().withCategory(
							concrete_member_info->typeEnum());
					substituted_size = concrete_member_info->sizeInBits().value;
					alias_semantic_source = concrete_member_info;
				}
				if (!isConcreteAliasSemanticSource(alias_semantic_source)) {
					alias_semantic_source = nullptr;
				}

				if (const TypeInfo* alias_target_info = tryGetTypeInfo(TypeIndex{substituted_type_index});
					alias_target_info != nullptr && alias_target_info->isTemplateInstantiation()) {
					std::vector<TemplateTypeArg> concrete_alias_args;
					concrete_alias_args.reserve(alias_target_info->templateArgs().size());
					bool has_unresolved_alias_arg = false;
					for (const auto& stored_arg : alias_target_info->templateArgs()) {
						TemplateTypeArg concrete_arg = toTemplateTypeArg(stored_arg);
						if (concrete_arg.is_value && concrete_arg.is_dependent && concrete_arg.dependent_name.isValid()) {
							const StructStaticMember* referenced_static_member =
								instantiated_struct_info ? instantiated_struct_info->findStaticMember(concrete_arg.dependent_name) : nullptr;
							if (referenced_static_member && referenced_static_member->initializer.has_value()) {
								ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
								eval_ctx.struct_info = instantiated_struct_info;
								eval_ctx.storage_duration = ConstExpr::StorageDuration::Static;
								auto eval_result = ConstExpr::Evaluator::evaluate(
									*referenced_static_member->initializer,
									eval_ctx);
								if (eval_result.success()) {
									concrete_arg.value = eval_result.as_int();
									concrete_arg.is_dependent = false;
									concrete_arg.dependent_name = StringHandle{};
									concrete_arg.dependent_expr.reset();
								}
							}
						}
						if (concrete_arg.is_dependent) {
							has_unresolved_alias_arg = true;
						}
						concrete_alias_args.push_back(std::move(concrete_arg));
					}
					if (!has_unresolved_alias_arg) {
						std::string_view alias_template_name =
							StringTable::getStringView(alias_target_info->baseTemplateName());
						if (!alias_template_name.empty()) {
							AliasTemplateMaterializationResult materialized_alias_target =
								materializeTemplateInstantiationForLookup(
									alias_template_name,
									concrete_alias_args);
							if (materialized_alias_target.resolved_type_info) {
								const TypeInfo* concrete_alias_target =
									materialized_alias_target.resolved_type_info;
								substituted_type = concrete_alias_target->typeEnum();
								substituted_type_index =
									concrete_alias_target->registeredTypeIndex().withCategory(
										concrete_alias_target->typeEnum());
								substituted_size = concrete_alias_target->sizeInBits().value;
								alias_semantic_source = concrete_alias_target;
							}
						}
					}
				}

				// Register the type alias globally with its qualified name
				std::optional<TypeSpecifierNode> substituted_alias_type_spec = buildSubstitutedTypeAliasSpecifier(
					type_alias, TypeIndex{substituted_type_index}, substituted_type, template_params, template_args_for_pattern, instantiated_name);
				bool use_resolved_alias_override =
					resolved_alias_type_spec_override.has_value();
				if (use_resolved_alias_override) {
					const TypeSpecifierNode& override_spec =
						resolved_alias_type_spec_override.value();
					if (override_spec.type_index().is_valid() &&
						typeIndexContainsDependentPlaceholder(
							override_spec.type_index())) {
						use_resolved_alias_override = false;
					} else if (const TypeInfo* override_info =
								   tryGetTypeInfo(override_spec.type_index());
							   override_info != nullptr &&
							   (override_info->isTemplatePlaceholder() ||
								override_info->isDependentPlaceholder())) {
						use_resolved_alias_override = false;
					}
				}
				const TypeSpecifierNode& alias_registration_type_spec =
					use_resolved_alias_override
						? resolved_alias_type_spec_override.value()
						: (substituted_alias_type_spec.has_value()
							   ? substituted_alias_type_spec.value()
							   : alias_type_spec);
				FLASH_LOG(
					Parser,
					Debug,
					"Class alias register(pattern): name='",
					StringTable::getStringView(qualified_alias_name),
					"', substituted_type=",
					static_cast<int>(substituted_type),
					", substituted_index=",
					TypeIndex{substituted_type_index}.index(),
					", reg_spec_type=",
					static_cast<int>(alias_registration_type_spec.type()),
					", reg_spec_index=",
					alias_registration_type_spec.type_index().index());
				auto& alias_type_info =
					alias_semantic_source != nullptr
						? add_type_alias_copy(
							  qualified_alias_name,
							  TypeIndex{substituted_type_index},
							  substituted_size,
							  alias_registration_type_spec,
							  *alias_semantic_source)
						: add_type_alias_copy(
							  qualified_alias_name,
							  TypeIndex{substituted_type_index},
							  substituted_size,
							  alias_registration_type_spec);
				if (alias_registration_type_spec.is_array()) {
					std::span<const size_t> alias_array_dimensions = alias_registration_type_spec.array_dimensions();
					size_t element_size = calculateResolvedMemberSizeAndAlignment(alias_registration_type_spec, substituted_type_index).size;
					substituted_size = static_cast<int>(element_size * std::accumulate(
						alias_array_dimensions.begin(),
						alias_array_dimensions.end(),
						size_t{1},
						std::multiplies<size_t>()) * 8);
					alias_type_info.fallback_size_bits_ = substituted_size;
				}
				(void)alias_type_info;

				// If this alias refers to an unscoped enum, track its TypeIndex so that
				// Struct::Enumerator qualified access (e.g. Tagged<int>::None) works in codegen.
				if (substituted_type == TypeCategory::Enum && substituted_type_index.is_valid()) {
					if (const TypeInfo* enum_ti = tryGetTypeInfo(substituted_type_index)) {
						const EnumTypeInfo* enum_info = enum_ti->getEnumInfo();
						if (enum_info && !enum_info->is_scoped && instantiated_struct_info_mut) {
							instantiated_struct_info_mut->addNestedEnumIndex(substituted_type_index);
						}
					}
				}

				FLASH_LOG(Templates, Debug, "Registered type alias from pattern: ", qualified_alias_name,
						  " -> type=", static_cast<int>(substituted_type),
						  ", type_index=", substituted_type_index);
			}

			// Create an AST node for the instantiated struct so member functions can be code-generated
			auto instantiated_struct = emplace_node<StructDeclarationNode>(
				instantiated_name,
				false // is_class
			);
			StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
			setOuterTemplateBindingsFromParams(
				instantiated_struct_ref,
				effective_pattern_template_params,
				effective_pattern_template_args);

			// Copy data members
			for (const auto& member_decl : pattern_struct.members()) {
				ASTNode substituted_member_decl = substituteTemplateParameters(
					member_decl.declaration, template_params, template_args_for_pattern);
				std::optional<ASTNode> substituted_default_initializer = member_decl.default_initializer.has_value()
																			 ? std::optional<ASTNode>(substituteTemplateParameters(
																				   *member_decl.default_initializer, template_params, template_args_for_pattern))
																			 : std::nullopt;
				std::optional<ASTNode> substituted_bitfield_width_expr = member_decl.bitfield_width_expr.has_value()
																			 ? std::optional<ASTNode>(substituteTemplateParameters(
																				   *member_decl.bitfield_width_expr, template_params, template_args_for_pattern))
																			 : std::nullopt;
				instantiated_struct_ref.add_member(
					substituted_member_decl,
					member_decl.access,
					substituted_default_initializer,
					member_decl.bitfield_width,
					substituted_bitfield_width_expr,
					member_decl.is_no_unique_address);
			}
			for (const auto& static_member : pattern_struct.static_members()) {
				TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
				original_type_spec.set_type_index(static_member.type_index);
				TypeIndex substituted_type_index = substitute_template_parameter(
					original_type_spec,
					effective_pattern_template_params,
					effective_pattern_template_args);
				size_t substituted_size = get_substituted_type_size_bytes(substituted_type_index);
				if (static_member.is_array) {
					for (size_t dim_size : static_member.array_dimensions) {
						substituted_size *= dim_size;
					}
				}
				std::optional<ASTNode> substituted_initializer =
					substitute_in_class_static_initializer_replay_first(
						static_member,
						effective_pattern_template_params,
						effective_pattern_template_args,
						[&]() -> std::optional<ASTNode> {
							return static_member.initializer.has_value()
									   ? std::optional<ASTNode>(substituteTemplateParameters(
											 *static_member.initializer,
											 effective_pattern_template_params,
											 effective_pattern_template_args))
									   : std::nullopt;
						});
				instantiated_struct_ref.addStaticMember(
					static_member.name,
					substituted_type_index,
					substituted_size,
					static_member.alignment,
					static_member.access,
					substituted_initializer,
					static_member.cv_qualifier,
					static_member.reference_qualifier,
					static_member.pointer_depth,
					static_member.is_array,
					static_member.array_dimensions,
					static_member.declaration,
					static_member.initializer_position,
					static_member.initializerDefinitionLookupContext(),
					static_member.is_constexpr);
			}

			// Partial-spec replay identity map: source member declaration -> instantiated stub.
			// Used to attach nested out-of-line member-template bodies replay-first, mirroring
			// the primary-template path.
			SourceMemberIdentityMaps source_member_identity_maps;

			// Copy member functions to AST node WITH CORRECT PARENT STRUCT NAME
			// This is critical - we need to create new FunctionDeclarationNodes with instantiated_name as parent
			for (StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
				if (mem_func.is_constructor) {
					// Handle constructor - it's a ConstructorDeclarationNode
					const ConstructorDeclarationNode& orig_ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();

					// Create a NEW ConstructorDeclarationNode with the instantiated struct name
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name, // Set correct parent struct name
						orig_ctor.name() // Constructor name (same as template name)
					);
					setOuterTemplateBindingsFromParams(new_ctor_ref, template_params, template_args_for_pattern);
					if (!orig_ctor.is_materialized() || orig_ctor.has_template_body_position()) {
						new_ctor_ref.set_template_parameters(orig_ctor.template_parameters());
					}
					if (orig_ctor.has_template_body_position()) {
						new_ctor_ref.set_template_body_position(orig_ctor.template_body_position());
						if (orig_ctor.has_template_initializer_list_position()) {
							new_ctor_ref.set_template_initializer_list_position(
								orig_ctor.template_initializer_list_position());
						}
					}

					// Copy parameters with template parameter substitution
					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyParams(
						orig_ctor.parameter_nodes(),
						new_ctor_ref,
						template_params,
						template_args_for_pattern,
						orig_ctor.is_implicit()
							? makeImplicitCtorSelfTypeRewrite(
								pattern_struct.name(),
								struct_type_info.registeredTypeIndex().withCategory(TypeCategory::Struct))
							: std::nullopt);

					// Copy initializers (member, base, delegating)
					substituteAndCopyInitializers(orig_ctor, new_ctor_ref, template_params, template_args_for_pattern);

					// Copy definition if present (with template parameter substitution)
					if (orig_ctor.is_materialized()) {
						new_ctor_ref.set_definition(substituteTemplateParameters(*orig_ctor.get_definition(), template_params, template_args_for_pattern));
					}
					pack_param_info_.resize(saved_pack_info);
					new_ctor_ref.set_is_implicit(orig_ctor.is_implicit());
					new_ctor_ref.set_is_explicitly_defaulted(orig_ctor.is_explicitly_defaulted());

					instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);
					registerSourceMemberStubIdentity(
						source_member_identity_maps,
						mem_func.function_declaration,
						new_ctor_node);
					if (!orig_ctor.is_materialized() &&
						shouldCommitTemplateInstantiationArtifacts()) {
						const TemplateEnvironmentSnapshot* outer_parent_snapshot =
							instantiated_struct_ref.has_outer_template_bindings()
								? &instantiated_struct_ref.outer_template_environment_snapshot()
								: nullptr;
						StringHandle lazy_registry_key = registerLazyConstructorStub(
							new_ctor_ref,
							new_ctor_node,
							StringTable::getOrInternStringHandle(template_name),
							instantiated_name,
							mem_func.access,
							effective_pattern_template_params,
							effective_pattern_template_args,
							outer_parent_snapshot);
						if (lazy_registry_key.isValid() &&
							instantiated_struct_info_mut != nullptr) {
							OutOfLineConstructorStubResolution info_ctor_resolution =
								findMatchingConstructorInStructInfo(
									*instantiated_struct_info_mut,
									new_ctor_ref,
									[](const ConstructorDeclarationNode&) {
										return true;
									});
							if (info_ctor_resolution.ctor != nullptr) {
								info_ctor_resolution.ctor->set_lazy_member_registry_key(
									lazy_registry_key);
							}
						}
					}
				} else if (mem_func.is_destructor) {
					// Handle destructor
					instantiated_struct_ref.add_destructor(mem_func.function_declaration, mem_func.access, mem_func.is_virtual);
				} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
					const TemplateFunctionDeclarationNode& template_func =
						mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
					const FunctionDeclarationNode& func_decl =
						template_func.function_declaration().as<FunctionDeclarationNode>();
					const DeclarationNode& decl_node = func_decl.decl_node();

					bool needs_substitution = false;
					{
						const auto& rtype = decl_node.type_specifier_node();
						if (rtype.category() == TypeCategory::UserDefined ||
							rtype.category() == TypeCategory::TypeAlias ||
							rtype.category() == TypeCategory::Template) {
							needs_substitution = true;
						}
					}
					if (!needs_substitution) {
						for (const auto& param : func_decl.parameter_nodes()) {
							if (!param.is<DeclarationNode>()) {
								continue;
							}
							const auto& ptype = param.as<DeclarationNode>().type_specifier_node();
							if (ptype.category() == TypeCategory::UserDefined ||
								ptype.category() == TypeCategory::TypeAlias ||
								ptype.category() == TypeCategory::Template) {
								needs_substitution = true;
								break;
							}
						}
					}

					// Build combined outer+class-level params/args when the pattern function
					// already carries outer template bindings from an enclosing scope.
					auto buildMergedOuterAndClassParams = [&](
						InlineVector<TemplateParameterNode, 4>& out_params,
						InlineVector<TemplateTypeArg, 4>& out_args) {
						if (!func_decl.has_outer_template_bindings()) {
							return;
						}
						out_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						out_args.reserve(
							func_decl.outer_template_args().size() + template_args_for_pattern.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							out_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							out_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							out_params.push_back(param);
						}
						for (const auto& arg : template_args_for_pattern) {
							out_args.push_back(arg);
						}
					};
					auto buildMergedOuterTemplateBinding = [&](OuterTemplateBinding& out_binding) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						buildMergedOuterAndClassParams(combined_params, combined_args);
						if (!combined_params.empty()) {
							collectOuterTemplateBinding(combined_params, combined_args, out_binding);
						} else {
							collectOuterTemplateBinding(template_params, template_args_for_pattern, out_binding);
						}
					};
					auto applyMergedOuterTemplateBindings = [&](FunctionDeclarationNode& target_func) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						buildMergedOuterAndClassParams(combined_params, combined_args);
						if (!combined_params.empty()) {
							setOuterTemplateBindingsFromParams(target_func, combined_params, combined_args);
						} else {
							setOuterTemplateBindingsFromParams(target_func, template_params, template_args_for_pattern);
						}
					};
					auto registerMemberTemplateInRegistry = [&](
						ASTNode new_template_func_node,
						StringHandle member_name_handle,
						std::string_view member_name_view) {
						StringBuilder qualified_name_builder;
						qualified_name_builder
							.append(StringTable::getStringView(instantiated_name))
							.append("::")
							.append(member_name_view);
						StringHandle qualified_name_handle =
							StringTable::getOrInternStringHandle(qualified_name_builder.commit());
						gTemplateRegistry.registerTemplate(qualified_name_handle, new_template_func_node);
						gTemplateRegistry.registerTemplate(member_name_handle, new_template_func_node);
						OuterTemplateBinding outer_binding;
						buildMergedOuterTemplateBinding(outer_binding);
						gTemplateRegistry.registerOuterTemplateBinding(
							qualified_name_handle, std::move(outer_binding));
					};

					if (needs_substitution) {
						const TypeSpecifierNode& return_type_spec = decl_node.type_specifier_node();
						ASTNode substituted_return_type_node = substituteTemplateParameters(
							decl_node.type_node(), template_params, template_args_for_pattern);
						TypeIndex ret_type_index = substitute_template_parameter(
							return_type_spec, template_params, template_args_for_pattern);
						ret_type_index = resolveOwnerAliasTypeIndex(
							[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
								return substitute_template_parameter(type_spec, params, args);
							},
							pattern_struct,
							return_type_spec,
							template_params,
							template_args_for_pattern,
							ret_type_index);

						ASTNode new_return_type = substituted_return_type_node.is<TypeSpecifierNode>()
							? substituted_return_type_node
							: emplace_node<TypeSpecifierNode>(
								  ret_type_index.category(), return_type_spec.qualifier(),
								  get_type_size_bits(ret_type_index.category()), return_type_spec.token(), return_type_spec.cv_qualifier());
						auto& new_return_spec = new_return_type.as<TypeSpecifierNode>();
						new_return_spec.set_category(ret_type_index.category());
						if (ret_type_index.is_valid()) {
							new_return_spec.set_type_index(ret_type_index.withCategory(ret_type_index.category()));
						}
						if (!substituted_return_type_node.is<TypeSpecifierNode>()) {
							for (const auto& pl : return_type_spec.pointer_levels())
								new_return_spec.add_pointer_level(pl.cv_qualifier);
							new_return_spec.set_reference_qualifier(return_type_spec.reference_qualifier());
						}
						if (return_type_spec.has_function_signature()) {
							new_return_spec.set_function_signature(return_type_spec.function_signature());
						} else if (new_return_spec.type_index().category() == TypeCategory::FunctionPointer ||
								   new_return_spec.type_index().category() == TypeCategory::MemberFunctionPointer) {
							if (const auto* arg = findTemplateArgByName(
									return_type_spec.token().value(),
									template_params,
									template_args_for_pattern)) {
								if (arg->function_signature.has_value()) {
									new_return_spec.set_function_signature(*arg->function_signature);
								}
							}
						}
						normalizeSubstitutedTypeSpec(new_return_spec);

						auto [new_decl_node, new_decl_ref] = emplace_node_ref<DeclarationNode>(
							new_return_type, decl_node.identifier_token());
						auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
							new_decl_ref,
							instantiated_name);
						applyMergedOuterTemplateBindings(new_func_ref);

						size_t saved_pack_info = pack_param_info_.size();
						substituteAndCopyMemberFunctionParameters(
							func_decl.parameter_nodes(),
							new_func_ref,
							template_params,
							template_args_for_pattern,
							&pattern_struct,
							TypeIndex{},
							TypeIndex{},
							TypeIndex{},
							SubstitutedDefaultArgumentPolicy::SubstituteTemplateParameters,
							true,
							false,
							true,
							true);
						pack_param_info_.resize(saved_pack_info);
						syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
							*this,
							std::span<ASTNode>(
								new_func_ref.parameter_nodes().data(),
								new_func_ref.parameter_nodes().size()),
							std::span<const ASTNode>(
								func_decl.parameter_nodes().data(),
								func_decl.parameter_nodes().size()),
							template_params,
							template_args_for_pattern);
						preserveDependentMemberTemplateParameterPlaceholdersFromPattern(
							std::span<ASTNode>(
								new_func_ref.parameter_nodes().data(),
								new_func_ref.parameter_nodes().size()),
							std::span<const ASTNode>(
								func_decl.parameter_nodes().data(),
								func_decl.parameter_nodes().size()));

						copy_function_properties(new_func_ref, func_decl);
						new_func_ref.set_is_const_member_function(mem_func.is_const());
						new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
						if (func_decl.is_materialized())
							new_func_ref.set_definition(*func_decl.get_definition());
						if (func_decl.has_template_body_position())
							new_func_ref.set_template_body_position(func_decl.template_body_position());
						if (func_decl.has_definition_lookup_context())
							new_func_ref.set_definition_lookup_context(func_decl.definition_lookup_context());
						if (func_decl.has_trailing_return_type_position())
							new_func_ref.set_trailing_return_type_position(func_decl.trailing_return_type_position());
						new_func_ref.set_is_template_pattern(true);

						auto new_template_func = emplace_node<TemplateFunctionDeclarationNode>(
							template_func.template_parameters(),
							new_func_node,
							template_func.requires_clause());

						if (mem_func.operator_kind != OverloadableOperator::None) {
							instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_template_func, mem_func.access);
							struct_type_info.getStructInfo()->addOperatorOverload(mem_func.operator_kind, new_template_func, mem_func.access,
																 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
						} else {
							instantiated_struct_ref.add_member_function(new_template_func, mem_func.access);
							struct_type_info.getStructInfo()->addMemberFunction(
								decl_node.identifier_token().handle(),
								new_template_func,
								mem_func.access,
								mem_func.is_virtual,
								mem_func.is_pure_virtual,
								mem_func.is_override,
								mem_func.is_final);
						}

						registerMemberTemplateInRegistry(
							new_template_func,
							decl_node.identifier_token().handle(),
							decl_node.identifier_token().value());
						registerSourceMemberStubIdentity(
							source_member_identity_maps,
							mem_func.function_declaration,
							new_template_func);
					} else {
						auto [new_decl_node_no_subst, new_decl_ref_no_subst] = emplace_node_ref<DeclarationNode>(
							decl_node.type_node(),
							decl_node.identifier_token());
						auto [new_func_node_no_subst, new_func_ref_no_subst] = emplace_node_ref<FunctionDeclarationNode>(
							new_decl_ref_no_subst,
							instantiated_name);
						applyMergedOuterTemplateBindings(new_func_ref_no_subst);

						for (const auto& param : func_decl.parameter_nodes()) {
							new_func_ref_no_subst.add_parameter_node(param);
						}

						copy_function_properties(new_func_ref_no_subst, func_decl);
						new_func_ref_no_subst.set_is_const_member_function(mem_func.is_const());
						new_func_ref_no_subst.set_is_volatile_member_function(mem_func.is_volatile());
						if (func_decl.is_materialized())
							new_func_ref_no_subst.set_definition(*func_decl.get_definition());
						if (func_decl.has_template_body_position())
							new_func_ref_no_subst.set_template_body_position(func_decl.template_body_position());
						if (func_decl.has_definition_lookup_context())
							new_func_ref_no_subst.set_definition_lookup_context(func_decl.definition_lookup_context());
						if (func_decl.has_trailing_return_type_position())
							new_func_ref_no_subst.set_trailing_return_type_position(func_decl.trailing_return_type_position());
						new_func_ref_no_subst.set_is_template_pattern(true);

						auto new_template_func_no_subst = emplace_node<TemplateFunctionDeclarationNode>(
							template_func.template_parameters(),
							new_func_node_no_subst,
							template_func.requires_clause());

						if (mem_func.operator_kind != OverloadableOperator::None) {
							instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_template_func_no_subst, mem_func.access);
							struct_type_info.getStructInfo()->addOperatorOverload(mem_func.operator_kind, new_template_func_no_subst, mem_func.access,
																 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
						} else {
							instantiated_struct_ref.add_member_function(
								new_template_func_no_subst,
								mem_func.access);
							struct_type_info.getStructInfo()->addMemberFunction(
								decl_node.identifier_token().handle(),
								new_template_func_no_subst,
								mem_func.access,
								mem_func.is_virtual,
								mem_func.is_pure_virtual,
								mem_func.is_override,
								mem_func.is_final);
						}

						registerMemberTemplateInRegistry(
							new_template_func_no_subst,
							decl_node.identifier_token().handle(),
							decl_node.identifier_token().value());
						registerSourceMemberStubIdentity(
							source_member_identity_maps,
							mem_func.function_declaration,
							new_template_func_no_subst);
					}
				} else {
					FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
					const DeclarationNode& orig_decl = orig_func.decl_node();
					const TypeSpecifierNode& orig_return_type = orig_decl.type_specifier_node();

					TypeIndex return_type_index = substitute_template_parameter(
						orig_return_type, template_params, template_args_for_pattern);
					return_type_index = resolveOwnerAliasTypeIndex(
						[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
							return substitute_template_parameter(type_spec, params, args);
						},
						pattern_struct,
						orig_return_type,
						template_params,
						template_args_for_pattern,
						return_type_index);

					SubstitutedMemberFunctionShell shell = createSubstitutedMemberFunctionShell(
						orig_func,
						ASTNode(&orig_return_type),
						orig_decl.identifier_token(),
						instantiated_name,
						template_params,
						template_args_for_pattern,
						&pattern_struct,
						TypeIndex{},		 // No self-owner rewrite for this path
						return_type_index,	 // Return TypeIndex already alias-resolved above
						mem_func.operator_kind,
						StringHandle{},	   // Compute effective lookup name from substituted return type/operator kind
						pattern_struct.name(), // Enable partial-pattern pointer-depth clamp
						false,				   // Do not re-apply bound metadata to full substitution
						true);				   // Force resolved TypeIndex onto full AST substitutions
					ASTNode new_func_node = shell.function_node;

					// Copy all parameters and definition
					FunctionDeclarationNode& new_func = *shell.function;
					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyMemberFunctionParameters(
						orig_func.parameter_nodes(),
						new_func,
						template_params,
						template_args_for_pattern,
						&pattern_struct,
						TypeIndex{},
						TypeIndex{},
						TypeIndex{},
						SubstitutedDefaultArgumentPolicy::SubstituteTemplateParameters,
						true,  // Preserve declared parameter cv-qualifier in this path
						false, // Do not re-apply bound metadata to full substitution
						true,  // Force resolved TypeIndex onto full AST substitutions
						false); // Plain member path does not need dependent member-template signature preservation
					std::unordered_map<std::string_view, TemplateTypeArg> deduced_args;
					for (size_t i = 0; i < template_params.size() && i < template_args_for_pattern.size(); ++i) {
						std::string_view pname = template_params[i].name();
						deduced_args[pname] = template_args_for_pattern[i];
					}
					if (orig_func.is_materialized()) {
						FLASH_LOG(Templates, Debug, "Copying function definition to new function");
						ASTNode substituted_body = *orig_func.get_definition();
						if (!template_args_for_pattern.empty()) {
							substituted_body = substituteTemplateParameters(
								*orig_func.get_definition(),
								template_params,
								template_args_for_pattern);
						}
						if (orig_func.is_static()) {
							const TypeInfo* rebound_type_info = findTypeByName(instantiated_name);
							substituted_body = rebindStaticMemberInitializerFunctionCalls(
								substituted_body,
								rebound_type_info ? rebound_type_info->getStructInfo() : nullptr,
								true);
						}
						new_func.set_definition(substituted_body);
					} else if (orig_func.has_template_body_position()) {
						// Member struct template partial specializations store function bodies
						// as deferred template body positions — re-parse the body now with
						// concrete template arguments so the definition is available at codegen.
						FLASH_LOG(Templates, Debug, "Re-parsing deferred function body from template body position");

						// Reuse the already-aligned template args so deferred body re-parsing
						// sees the deduced specialization parameters, not the raw concrete
						// instantiation arguments.
						FlashCpp::TemplateParameterScope template_scope;
						for (const auto& [param_name, deduced_arg] : deduced_args) {
							TypeCategory concrete_type = deduced_arg.typeEnum();
							auto& type_info = add_template_param_type(StringTable::getOrInternStringHandle(param_name), concrete_type, get_type_size_bits(concrete_type));
							type_info.reference_qualifier_ = deduced_arg.is_rvalue_reference() ? ReferenceQualifier::RValueReference
																							   : (deduced_arg.is_lvalue_reference() ? ReferenceQualifier::LValueReference : ReferenceQualifier::None);
							template_scope.addParameter(&type_info);
						}

						SaveHandle current_pos = save_token_position();

						restore_lexer_position_only(orig_func.template_body_position());

						// Use FunctionParsingScopeGuard for full member-function context:
						// scope, current_function_, member context push, 'this' injection,
						// and parameter registration — matching the normal delayed-body path.
						{
							FlashCpp::FunctionParsingScopeGuard func_guard(
								*this,
								true,
								!orig_func.is_static(),
								&instantiated_struct_ref,
								instantiated_name,
								struct_type_info.type_index_,
								new_func.parameter_nodes(),
								&new_func);

							auto block_result = parse_function_body();

							if (!block_result.is_error() && block_result.node().has_value()) {
								// Substitute template parameters in the parsed body
								ASTNode substituted_body = substituteTemplateParameters(
									*block_result.node(),
									template_params,
									template_args_for_pattern);
								if (orig_func.is_static()) {
									const TypeInfo* rebound_type_info = findTypeByName(instantiated_name);
									substituted_body = rebindStaticMemberInitializerFunctionCalls(
										substituted_body,
										rebound_type_info ? rebound_type_info->getStructInfo() : nullptr,
										true);
								}
								new_func.set_definition(substituted_body);
							}
						} // func_guard dtor: pops member ctx, restores current_function_, exits scope

						restore_lexer_position_only(current_pos);
						discard_saved_token(current_pos);
					} else {
						FLASH_LOG(Templates, Debug, "Original function has NO definition - may need delayed parsing");
					}

					copy_function_properties(new_func, orig_func);
					pack_param_info_.resize(saved_pack_info);
					if (new_func.is_materialized()) {
						finalize_function_after_definition(new_func);
					}

					instantiated_struct_ref.add_member_function(
						new_func_node,
						mem_func.access,
						mem_func.is_virtual, mem_func.is_pure_virtual,
						mem_func.is_override, mem_func.is_final,
						mem_func.cv_qualifier);
					registerSourceMemberStubIdentity(
						source_member_identity_maps,
						mem_func.function_declaration,
						new_func_node);
				}
			}

			const std::string_view template_base_name = extractBaseTemplateName(template_name);
			auto out_of_line_members =
				gTemplateRegistry.getOutOfLineMemberFunctions(template_name);
			auto is_out_of_line_member_record_equivalent =
				[](const OutOfLineMemberFunction& lhs, const OutOfLineMemberFunction& rhs) {
					if (lhs.body_start != rhs.body_start ||
						lhs.initializer_list_start != rhs.initializer_list_start ||
						lhs.has_initializer_list != rhs.has_initializer_list ||
						lhs.inner_template_params.size() != rhs.inner_template_params.size() ||
						lhs.template_params.size() != rhs.template_params.size() ||
						lhs.function_node.type_name() != rhs.function_node.type_name()) {
						return false;
					}

					if (lhs.function_node.is<FunctionDeclarationNode>() &&
						rhs.function_node.is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& lhs_func =
							lhs.function_node.as<FunctionDeclarationNode>();
						const FunctionDeclarationNode& rhs_func =
							rhs.function_node.as<FunctionDeclarationNode>();
						return lhs_func.decl_node().identifier_token().value() ==
								   rhs_func.decl_node().identifier_token().value() &&
							   lhs_func.parameter_nodes().size() ==
								   rhs_func.parameter_nodes().size();
					}

					return true;
				};
			auto append_unique_out_of_line_members =
				[&](const std::vector<OutOfLineMemberFunction>& source_members) {
					for (const auto& source_member : source_members) {
						bool already_present = false;
						for (const auto& existing_member : out_of_line_members) {
							if (is_out_of_line_member_record_equivalent(
									existing_member,
									source_member)) {
								already_present = true;
								break;
							}
						}
						if (!already_present) {
							out_of_line_members.push_back(source_member);
						}
					}
				};
			if (!template_base_name.empty() && template_base_name != template_name) {
				auto base_out_of_line_members =
					gTemplateRegistry.getOutOfLineMemberFunctions(template_base_name);
				append_unique_out_of_line_members(base_out_of_line_members);
			}

			FLASH_LOG(Templates, Debug, "Processing ", out_of_line_members.size(),
				" out-of-line member functions for ", template_name,
				" (base fallback: ", template_base_name, ")");
			for (const auto& out_of_line_member : out_of_line_members) {
				if (out_of_line_member.inner_template_params.empty()) {
					// Plain (non-template) OOL member on a partial specialization: resolve the
					// source declaration first, then map source-member -> instantiated stub
					// replay-first before parsing/substituting the body.
					if (!out_of_line_member.function_node.is<FunctionDeclarationNode>()) {
						continue;
					}
					const FunctionDeclarationNode& plain_ool_func =
						out_of_line_member.function_node.as<FunctionDeclarationNode>();
					const DeclarationNode& plain_ool_decl = plain_ool_func.decl_node();
					const std::string_view plain_ool_name = plain_ool_decl.identifier_token().value();
					// Skip ctor stubs — handled by the ctor path below.
					const bool is_plain_ctor_stub = isOutOfLineConstructorStubName(
						plain_ool_name,
						template_name,
						template_base_name);
					if (is_plain_ctor_stub) {
						continue;
					}
					OutOfLineMemberStubResolution member_resolution =
						findPlainOutOfLineMemberStubByIdentity(
						*this,
						source_member_identity_maps,
						std::span<const StructMemberFunctionDecl>(
							pattern_struct.member_functions().data(),
							pattern_struct.member_functions().size()),
						plain_ool_func,
						std::span<const TemplateParameterNode>(
							template_params.data(),
							template_params.size()),
						std::span<const TemplateTypeArg>(
							template_args_for_pattern.data(),
							template_args_for_pattern.size()),
						instantiated_name);
					FunctionDeclarationNode* inst_func = member_resolution.func;
					if (inst_func != nullptr) {
						// Copy definition-site parameter names so the body can reference them.
						copyDefinitionParameterIdentifiers(
							inst_func->parameter_nodes(),
							plain_ool_func.parameter_nodes());

						const std::span<const ASTNode> inst_func_params =
							inst_func->parameter_nodes();
						if (replayOutOfLineMemberBody(
								*inst_func,
								&instantiated_struct_ref,
								instantiated_name,
								struct_type_info.type_index_,
								inst_func_params,
								out_of_line_member.body_start,
								out_of_line_member.definition_lookup_context,
								plain_ool_decl.identifier_token(),
								std::span<const TemplateParameterNode>(
									template_params.data(),
									template_params.size()),
								std::span<const TemplateTypeArg>(
									template_args_for_pattern.data(),
									template_args_for_pattern.size()),
								"partial-spec",
								plain_ool_name)) {
							OutOfLineFunctionStubResolution info_func_resolution =
								findReplayedOutOfLineMemberInStructInfo(
									struct_type_info.getStructInfo(),
									*inst_func);
							if (info_func_resolution.ambiguous) {
								FLASH_LOG(
									Templates,
									Error,
									"Ambiguous StructTypeInfo sync for partial-spec plain out-of-line member '",
									plain_ool_name,
									"' in instantiated class '",
									instantiated_name,
									"'");
							} else if (info_func_resolution.func != nullptr &&
								inst_func->is_materialized()) {
								copyDefinitionParameterIdentifiers(
									info_func_resolution.func->parameter_nodes(),
									plain_ool_func.parameter_nodes());
								info_func_resolution.func->set_definition(*inst_func->get_definition());
								finalize_function_after_definition(*info_func_resolution.func, true);
							}
							registerLateMaterializedOwningStructRoot(instantiated_name);
							normalizePendingSemanticRoots();
							FLASH_LOG(Templates, Debug,
								"Parsed and substituted OOL plain member body "
								"for partial-spec: ", plain_ool_name);
						} else {
							std::string error_msg = std::string(StringBuilder()
								.append("Failed to attach body for partial-spec plain out-of-line member '")
								.append(plain_ool_name)
								.append("' for instantiated class '")
								.append(instantiated_name)
								.append("'")
								.commit());
							if (force_eager) {
								throw CompileError(error_msg);
							}
							return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
						}
					} else {
						StringBuilder error_builder;
						if (member_resolution.ambiguous) {
							error_builder
								.append("Could not uniquely match partial-spec plain out-of-line member '");
						} else if (member_resolution.insufficient_evidence) {
							error_builder
								.append("Could not match partial-spec plain out-of-line member '");
						} else {
							error_builder
								.append("Could not attach partial-spec plain out-of-line member '");
						}
						error_builder
							.append(plain_ool_name)
							.append("' for instantiated class '")
							.append(instantiated_name);
						if (!member_resolution.insufficient_evidence &&
							!member_resolution.ambiguous) {
							error_builder.append("' via source-member identity mapping");
						} else {
							error_builder.append("'");
						}
						std::string error_msg = std::string(error_builder.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					continue;
				}

				const FunctionDeclarationNode& ool_func =
					out_of_line_member.function_node.as<FunctionDeclarationNode>();
				const DeclarationNode& ool_decl = ool_func.decl_node();
				std::string_view ool_func_name = ool_decl.identifier_token().value();
				const bool out_of_line_ctor_stub = isOutOfLineConstructorStubName(
					ool_func_name,
					template_name,
					template_base_name);
				if (!out_of_line_ctor_stub) {
					// Non-ctor OOL member function template in a partial specialization:
					// replay-first attachment against source members, then source-member->stub
					// identity resolution (mirrors the primary-template path).
					std::vector<const StructMemberFunctionDecl*> relevant_source_member_templates;
					const size_t ool_inner_template_param_count =
						out_of_line_member.inner_template_params.size();
					const size_t ool_function_param_count = ool_func.parameter_nodes().size();
					for (const StructMemberFunctionDecl& source_member :
						 pattern_struct.member_functions()) {
						if (isMatchingMemberTemplate(
								source_member,
								ool_func_name,
								ool_inner_template_param_count,
								ool_function_param_count)) {
							relevant_source_member_templates.push_back(&source_member);
						}
					}
					FunctionDeclarationNode* inst_func_decl = nullptr;
					bool saw_insufficient_replay_evidence = false;
					bool saw_ambiguous_replay_match = false;
					for (const StructMemberFunctionDecl* source_member :
						 relevant_source_member_templates) {
						const FunctionDeclarationNode* source_func_decl =
							get_function_decl_node(source_member->function_declaration);
						if (source_func_decl == nullptr) {
							continue;
						}
						ASTNode* matched_stub = findSourceMemberStubByIdentity(
							source_member_identity_maps,
							source_member->function_declaration);
						if (matched_stub == nullptr || !matched_stub->is<TemplateFunctionDeclarationNode>()) {
							continue;
						}
						ReplaySignatureMatchResult signature_match =
							nestedOutOfLineMemberTemplateMatchesCandidate(
								*this,
								source_member->function_declaration,
								ool_func,
								std::span<const TemplateParameterNode>(
									template_params.data(),
									template_params.size()),
								std::span<const TemplateTypeArg>(
									template_args_for_pattern.data(),
									template_args_for_pattern.size()),
								instantiated_name,
								std::span<const TemplateParameterNode>(
									out_of_line_member.inner_template_params.data(),
									out_of_line_member.inner_template_params.size()));
						if (signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
							saw_insufficient_replay_evidence = true;
							continue;
						}
						if (signature_match != ReplaySignatureMatchResult::Match) {
							continue;
						}
						FunctionDeclarationNode* matched_template_func_decl =
							get_function_decl_node_mut(*matched_stub);
						if (matched_template_func_decl == nullptr) {
							continue;
						}
						if (inst_func_decl != nullptr &&
							inst_func_decl != matched_template_func_decl) {
							saw_ambiguous_replay_match = true;
							break;
						}
						inst_func_decl = matched_template_func_decl;
					}
					if (saw_ambiguous_replay_match) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not uniquely match partial-spec nested out-of-line member template '")
							.append(ool_func_name)
							.append("' in instantiated class '")
							.append(instantiated_name)
							.append("'")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					if (inst_func_decl != nullptr) {
						inst_func_decl->set_template_body_position(out_of_line_member.body_start);
						copyDefinitionParameterIdentifiers(
							inst_func_decl->parameter_nodes(),
							ool_func.parameter_nodes());
						copyDefinitionParameterTypesForDependentMemberTemplateSegments(
							inst_func_decl->parameter_nodes(),
							ool_func.parameter_nodes());
						inst_func_decl->set_definition_lookup_context(
							out_of_line_member.definition_lookup_context);
						{
							StringBuilder qualified_name_builder;
							qualified_name_builder
								.append(StringTable::getStringView(instantiated_name))
								.append("::")
								.append(ool_func_name);
							StringHandle qualified_name_handle =
								StringTable::getOrInternStringHandle(qualified_name_builder.commit());
							OuterTemplateBinding ool_outer_binding;
							collectOuterTemplateBinding(
								template_params, template_args_for_pattern, ool_outer_binding);
							gTemplateRegistry.registerOuterTemplateBinding(
								qualified_name_handle, std::move(ool_outer_binding));
						}
						FLASH_LOG(
							Templates,
							Debug,
							"Set body position on partial-spec nested template member: ",
							ool_func_name);
					} else if (saw_insufficient_replay_evidence) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not match partial-spec nested out-of-line member template '")
							.append(ool_func_name)
							.append("' for instantiated class '")
							.append(instantiated_name)
							.append("' to exactly one declaration after template substitution")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					} else {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not attach partial-spec nested out-of-line member template '")
							.append(ool_func_name)
							.append("' for instantiated class '")
							.append(instantiated_name)
							.append("' via source-member identity mapping")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					continue;
				}

				size_t relevant_source_ctor_template_count = 0;
				for (const StructMemberFunctionDecl& source_member :
					 pattern_struct.member_functions()) {
					if (!source_member.function_declaration.is<ConstructorDeclarationNode>()) {
						continue;
					}
					const auto& source_ctor =
						source_member.function_declaration.as<ConstructorDeclarationNode>();
					if (source_ctor.template_parameters().size() !=
							out_of_line_member.inner_template_params.size() ||
						source_ctor.parameter_nodes().size() !=
							ool_func.parameter_nodes().size()) {
						continue;
					}
					++relevant_source_ctor_template_count;
				}
				if (relevant_source_ctor_template_count == 0) {
					continue;
				}

				OutOfLineConstructorStubResolution ctor_resolution =
					findOutOfLineConstructorTemplateStubByIdentity(
						*this,
						source_member_identity_maps,
						std::span<const StructMemberFunctionDecl>(
							pattern_struct.member_functions().data(),
							pattern_struct.member_functions().size()),
						ool_func,
						std::span<const TemplateParameterNode>(
							template_params.data(),
							template_params.size()),
						std::span<const TemplateTypeArg>(
							template_args_for_pattern.data(),
							template_args_for_pattern.size()),
						instantiated_name,
						std::span<const TemplateParameterNode>(
							out_of_line_member.inner_template_params.data(),
							out_of_line_member.inner_template_params.size()));
				if (ctor_resolution.ambiguous) {
					std::string error_msg = std::string(StringBuilder()
						.append("Could not uniquely match partial-spec out-of-line constructor template '")
						.append(ool_func_name)
						.append("' in instantiated class '")
						.append(instantiated_name)
						.append("'")
						.commit());
					if (force_eager) {
						throw CompileError(error_msg);
					}
					return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
				}
				if (ctor_resolution.ctor == nullptr) {
					StringBuilder error_builder;
					if (ctor_resolution.insufficient_evidence) {
						error_builder
							.append("Could not match partial-spec out-of-line constructor template '");
					} else {
						error_builder
							.append("Could not attach partial-spec out-of-line constructor template '");
					}
					error_builder
						.append(ool_func_name)
						.append("' for instantiated class '")
						.append(instantiated_name);
					if (!ctor_resolution.insufficient_evidence) {
						error_builder.append("' via source-member identity mapping");
					} else {
						error_builder.append("'");
					}
					std::string error_msg = std::string(error_builder.commit());
					if (force_eager) {
						throw CompileError(error_msg);
					}
					return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
				}

				ConstructorDeclarationNode& ctor_decl = *ctor_resolution.ctor;
				setOutOfLineConstructorTemplateReplayMetadata(
					ctor_decl,
					out_of_line_member);
				syncOutOfLineConstructorTemplateParameters(
					ctor_decl.parameter_nodes(),
					ool_func.parameter_nodes());

				if (StructTypeInfo* ctor_instantiated_struct_info = struct_type_info.getStructInfo();
					ctor_instantiated_struct_info != nullptr) {
					OutOfLineConstructorStubResolution info_ctor_resolution =
						findOutOfLineConstructorTemplateStubByIdentity(
							*this,
							struct_info_source_member_identity_maps,
							std::span<const StructMemberFunctionDecl>(
								pattern_struct.member_functions().data(),
								pattern_struct.member_functions().size()),
							ool_func,
							std::span<const TemplateParameterNode>(
								template_params.data(),
								template_params.size()),
							std::span<const TemplateTypeArg>(
								template_args_for_pattern.data(),
								template_args_for_pattern.size()),
							instantiated_name,
							std::span<const TemplateParameterNode>(
								out_of_line_member.inner_template_params.data(),
								out_of_line_member.inner_template_params.size()));

					if (info_ctor_resolution.ctor != nullptr) {
						setOutOfLineConstructorTemplateReplayMetadata(
							*info_ctor_resolution.ctor,
							out_of_line_member);
						syncOutOfLineConstructorTemplateParameters(
							info_ctor_resolution.ctor->parameter_nodes(),
							ool_func.parameter_nodes());
					} else if (info_ctor_resolution.ambiguous) {
						FLASH_LOG(
							Templates,
							Error,
							"Ambiguous StructTypeInfo constructor-template sync for partial-spec out-of-line constructor template '",
							ool_func_name,
							"' in instantiated class '",
							instantiated_name,
							"'");
					}
				}

				FLASH_LOG(
					Templates,
					Debug,
					"Set deferred body position on out-of-line constructor template: ",
					ool_func_name);
			}

			// Re-evaluate deferred static_asserts with substituted template parameters
			FLASH_LOG(Templates, Debug, "Checking ", pattern_struct.deferred_static_asserts().size(),
					  " deferred static_asserts for instantiation");

			for (const auto& deferred_assert : pattern_struct.deferred_static_asserts()) {
				FLASH_LOG(Templates, Debug, "Re-evaluating deferred static_assert during template instantiation");

				// Build template parameter name to type mapping for substitution
				auto sub_map = buildSubstitutionParamMap(template_params, template_args_for_pattern);

				// Create substitution context with template parameter mappings
				ExpressionSubstitutor substitutor(sub_map.param_map, *this, sub_map.param_order);

				// Substitute template parameters in the condition expression
				ASTNode substituted_expr = substitutor.substitute(deferred_assert.condition_expr);

				// Evaluate the substituted expression
				ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
				eval_ctx.struct_node = &instantiated_struct_ref;
				populateDeferredStaticAssertEvalTemplateBindings(
					eval_ctx,
					buildTemplateEnvironment(template_params, template_args_for_pattern, nullptr),
					sub_map.param_order,
					sub_map.param_map);

				auto eval_result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_ctx);

				if (!eval_result.success()) {
					std::string error_msg = buildDeferredStaticAssertInstantiationError(
						eval_result.error_message, deferred_assert.message, true);
					if (!is_implicit_instantiation) {
						throw CompileError(error_msg);
					}
					FLASH_LOG(Templates, Error, error_msg);
					return std::nullopt;
				}

				// Check if the assertion failed
				if (!eval_result.as_bool()) {
					std::string error_msg = buildDeferredStaticAssertInstantiationError(
						std::string_view(), deferred_assert.message, false);
					if (!is_implicit_instantiation) {
						throw CompileError(error_msg);
					}
					FLASH_LOG(Templates, Error, error_msg);
					return std::nullopt;
				}

				FLASH_LOG(Templates, Debug, "Deferred static_assert passed during template instantiation");
			}

			// Mark instantiation complete with the type index
			FlashCpp::gInstantiationQueue.markComplete(inst_key, struct_type_info.type_index_);
			in_progress_guard.dismiss(); // Don't remove from in_progress in destructor

			// Tag the struct with its materialization level before caching.
			// Commit point: state transition NotMaterialized|ShapeOnly -> ShapeOnly|Materialized.
			// ShapeOnly   -> mode is ShapeOnly (shape probe, no commit side effects).
			// Materialized -> mode is HardUse or SoftProbe (full commit, all artifacts registered).
			if (template_instantiation_mode_ == TemplateInstantiationMode::ShapeOnly) {
				instantiated_struct.as<StructDeclarationNode>().mark_shape_only();
			} else {
				instantiated_struct.as<StructDeclarationNode>().mark_materialized();
			}

			// Register in cache for O(1) lookup on future instantiations
			gTemplateRegistry.registerInstantiation(cache_key, instantiated_struct);

			return instantiated_struct; // Return the struct node for code generation
		}
	}

	// No specialization found - use the primary template
	ASTNode template_node;
	{
		PROFILE_TEMPLATE_LOOKUP();
		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (!template_opt.has_value()) {
			// If we're inside a template body, the template might be referencing itself
			// (self-referential templates like __ratio_add_impl). In this case, the template
			// hasn't been registered yet because we're still parsing its body.
			// Check if the name matches the struct currently being defined.
			if (isTemplateParameterTrackingActive()) {
				// Check struct_parsing_context_stack_ for self-reference
				for (auto it = struct_parsing_context_stack_.rbegin(); it != struct_parsing_context_stack_.rend(); ++it) {
					std::string_view struct_name = it->struct_name;
					// Compare with both unqualified and potentially qualified names
					if (struct_name == template_name) {
						FLASH_LOG_FORMAT(Templates, Debug, "Self-referential template '{}' in body - deferring", template_name);
						return std::nullopt;
					}
					// Also try stripping namespace prefix from struct_name
					size_t colon_pos = struct_name.rfind("::");
					if (colon_pos != std::string_view::npos) {
						std::string_view unqualified = struct_name.substr(colon_pos + 2);
						if (unqualified == template_name) {
							FLASH_LOG_FORMAT(Templates, Debug, "Self-referential template '{}' in body - deferring", template_name);
							return std::nullopt;
						}
					}
				}
			}
			// Fallback: if template_name is of the form "Owner::Member", check if
		// Owner's base classes have a template named "Base::Member".  This
		// handles the case where Derived<T> inherits a member template from
		// Base<T> and the caller says "Derived$hash::Inner".
		if (size_t sep = template_name.rfind("::"); sep != std::string_view::npos) {
				std::string_view owner_name = template_name.substr(0, sep);
				std::string_view member_name = template_name.substr(sep + 2);
				StringHandle owner_handle = StringTable::getOrInternStringHandle(owner_name);
				const TypeInfo* owner_type_info = findTypeByName(owner_handle);
				if (owner_type_info != nullptr) {
					// Try via StructTypeInfo (works if Derived is already fully instantiated).
					if (const StructTypeInfo* owner_struct = owner_type_info->getStructInfo()) {
						for (const BaseClassSpecifier& base_spec : owner_struct->base_classes) {
							if (base_spec.is_deferred || base_spec.name.empty()) {
								continue;
							}
							std::string base_member = std::string(StringBuilder()
								.append(base_spec.name)
								.append("::")
								.append(member_name)
								.commit());
							if (auto base_opt = gTemplateRegistry.lookupTemplate(base_member); base_opt.has_value()) {
								FLASH_LOG_FORMAT(Templates, Debug,
									"Inherited member template lookup: '{}' -> '{}'",
									template_name, base_member);
								template_opt = base_opt;
								break;
							}
							// Also try the original (uninstantiated) base template name.
							if (const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index)) {
								std::string_view base_orig = StringTable::getStringView(base_type_info->baseTemplateName());
								if (!base_orig.empty() && base_orig != base_spec.name) {
									std::string orig_base_member = std::string(StringBuilder()
										.append(base_orig)
										.append("::")
										.append(member_name)
										.commit());
									if (auto orig_opt = gTemplateRegistry.lookupTemplate(orig_base_member);
										orig_opt.has_value()) {
										FLASH_LOG_FORMAT(Templates, Debug,
											"Inherited member template lookup (orig): '{}' -> '{}'",
											template_name, orig_base_member);
										template_opt = orig_opt;
										break;
									}
								}
							}
						}
					}
					// Try via the original (uninstantiated) template declaration — this
					// works even when Derived's StructTypeInfo is not yet built (e.g.,
					// when evaluating a static constexpr initializer during Derived's own
					// instantiation).
					if (!template_opt.has_value()) {
						std::string_view owner_orig = StringTable::getStringView(owner_type_info->baseTemplateName());
						if (owner_orig.empty()) {
							// Fall back to stripping $hash suffix
							if (size_t dollar = owner_name.find('$'); dollar != std::string_view::npos) {
								owner_orig = owner_name.substr(0, dollar);
							}
						}
						if (!owner_orig.empty()) {
							if (auto orig_tmpl_opt = gTemplateRegistry.lookupTemplate(owner_orig);
								orig_tmpl_opt.has_value() &&
								orig_tmpl_opt->is<TemplateClassDeclarationNode>()) {
								const StructDeclarationNode& orig_decl =
									orig_tmpl_opt->as<TemplateClassDeclarationNode>().class_decl_node();
								for (const DeferredTemplateBaseClassSpecifier& dtbs :
									 orig_decl.deferred_template_base_classes()) {
									std::string_view base_tmpl =
										StringTable::getStringView(dtbs.base_template_name);
									if (base_tmpl.empty()) {
										continue;
									}
									std::string base_member = std::string(StringBuilder()
										.append(base_tmpl)
										.append("::")
										.append(member_name)
										.commit());
									if (auto base_opt = gTemplateRegistry.lookupTemplate(base_member);
										base_opt.has_value()) {
										FLASH_LOG_FORMAT(Templates, Debug,
											"Inherited member template lookup (deferred): '{}' -> '{}'",
											template_name, base_member);
										template_opt = base_opt;
										break;
									}
								}
							}
						}
					}
				}
		}
		if (!template_opt.has_value()) {
			std::string error_msg = std::string(StringBuilder()
				.append("No primary class template found for '")
				.append(template_name)
				.append("'")
				.commit());
			if (force_eager) {
				throw CompileError(error_msg);
			}
			FLASH_LOG(Templates, Error, error_msg);
			return std::nullopt; // No template with this name
		}
		}
		template_node = *template_opt;
	}

	if (!template_node.is<TemplateClassDeclarationNode>()) {
		std::string error_msg = std::string(StringBuilder()
			.append("Template '")
			.append(template_name)
			.append("' is not a class template")
			.commit());
		if (force_eager) {
			throw CompileError(error_msg);
		}
		FLASH_LOG(Templates, Error, error_msg);
		return std::nullopt; // Not a class template
	}

	const TemplateClassDeclarationNode& template_class = template_node.as<TemplateClassDeclarationNode>();
	const auto& template_params = template_class.template_parameters();
	InlineVector<TemplateParameterNode, 4> template_params_typed;
	template_params_typed.reserve(template_params.size());
	for (const TemplateParameterNode& template_param : template_params) {
		template_params_typed.push_back(template_param);
	}
	const StructDeclarationNode& class_decl = template_class.class_decl_node();

	// Count non-variadic parameters
	size_t non_variadic_param_count = 0;
	bool has_parameter_pack = false;

	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
		if (param == nullptr) {
			continue;
		}
		if (param->is_variadic()) {
			has_parameter_pack = true;
		} else {
			non_variadic_param_count++;
		}
	}

	// Push class template pack info for sizeof...() resolution in member function templates
	// This RAII guard ensures the pack info is available during the entire instantiation scope
	ClassTemplatePackGuard class_pack_guard(class_template_pack_stack_);
	if (has_parameter_pack) {
		std::vector<ClassTemplatePackInfo> pack_infos;
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
			if (param != nullptr && param->is_variadic()) {
				size_t pack_size = template_args.size() >= non_variadic_param_count
									   ? template_args.size() - non_variadic_param_count
									   : 0;
				pack_infos.push_back({param->name(), pack_size});
				FLASH_LOG(Templates, Debug, "Registered class template pack '", param->name(), "' with size ", pack_size);
			}
		}
		if (!pack_infos.empty()) {
			class_pack_guard.push(std::move(pack_infos));
		}
	}

	// Verify we have the right number of template arguments
	// For variadic templates: args.size() >= non_variadic_param_count
	// For non-variadic templates: args.size() <= template_params.size()
	if (has_parameter_pack) {
		// With parameter pack, we need at least the non-variadic parameters
		if (template_args.size() < non_variadic_param_count) {
			FLASH_LOG(Templates, Error, "Too few arguments for variadic template (got ", template_args.size(),
					  ", need at least ", non_variadic_param_count, ")");
			return std::nullopt;
		}
		// The rest of the arguments go into the parameter pack
	} else {
		// Non-variadic template: allow fewer arguments if remaining parameters have defaults
		if (template_args.size() > template_params.size()) {
			return std::nullopt; // Too many template arguments
		}
	}

	// Create a mutable copy of template_args to fill in defaults
	std::vector<TemplateTypeArg> filled_template_args(template_args.begin(), template_args.end());
	auto default_arg_source_namespace = [&]() -> NamespaceHandle {
		if (template_name.find("::") != std::string_view::npos) {
			return QualifiedIdentifier::fromQualifiedName(
				template_name,
				NamespaceRegistry::GLOBAL_NAMESPACE)
				.namespace_handle;
		}

		std::string_view decl_name = StringTable::getStringView(class_decl.name());
		if (size_t pos = decl_name.rfind("::"); pos != std::string_view::npos) {
			return QualifiedIdentifier::fromQualifiedName(
				decl_name,
				NamespaceRegistry::GLOBAL_NAMESPACE)
				.namespace_handle;
		}

		return gSymbolTable.get_current_namespace_handle();
	}();
	const OuterTemplateBinding* outer_binding =
		gTemplateRegistry.getOuterTemplateBinding(template_name);
	std::optional<OuterTemplateBinding> synthesized_outer_binding;
	if (outer_binding == nullptr) {
		if (const size_t owner_sep = template_name.rfind("::");
			owner_sep != std::string_view::npos && owner_sep > 0) {
			StringHandle owner_name_handle = StringTable::getOrInternStringHandle(
				template_name.substr(0, owner_sep));
			synthesized_outer_binding = buildOuterBindingForOwner(owner_name_handle);
		}
		if ((!synthesized_outer_binding.has_value() ||
			 synthesized_outer_binding->param_names.empty()) &&
			current_instantiation_ctx_ != nullptr &&
			current_instantiation_ctx_->origin_name.isValid()) {
			synthesized_outer_binding = buildOuterBindingForOwner(
				current_instantiation_ctx_->origin_name);
		}
		if (!synthesized_outer_binding.has_value() ||
			synthesized_outer_binding->param_names.empty()) {
			StringHandle contextual_owner{};
			if (!struct_parsing_context_stack_.empty()) {
				contextual_owner = StringTable::getOrInternStringHandle(
					struct_parsing_context_stack_.back().struct_name);
			} else if (!member_function_context_stack_.empty()) {
				contextual_owner = member_function_context_stack_.back().struct_name;
			}
			if (contextual_owner.isValid()) {
				synthesized_outer_binding = buildOuterBindingForOwner(contextual_owner);
			}
		}
		if (synthesized_outer_binding.has_value() &&
			!synthesized_outer_binding->param_names.empty()) {
			outer_binding = &*synthesized_outer_binding;
		}
	}
	if (outer_binding == nullptr) {
		if (const size_t owner_sep = template_name.rfind("::");
			owner_sep != std::string_view::npos && owner_sep > 0) {
			const std::string_view owner_template_name =
				template_name.substr(0, owner_sep);
			if (auto owner_template_node =
					gTemplateRegistry.lookupTemplate(owner_template_name);
				owner_template_node.has_value() &&
				owner_template_node->is<TemplateClassDeclarationNode>()) {
				const auto& owner_template_params =
					owner_template_node->as<TemplateClassDeclarationNode>()
						.template_parameters();
				OuterTemplateBinding substitution_outer_binding;
				for (const TemplateParameterNode& owner_param : owner_template_params) {
					for (const TemplateParamSubstitution& substitution :
						 template_param_substitutions_) {
						if (substitution.param_name != owner_param.nameHandle()) {
							continue;
						}
						TemplateTypeArg substitution_arg;
						if (substitution.is_type_param) {
							substitution_arg = substitution.substituted_type;
						} else if (substitution.is_template_template_param &&
								   substitution.concrete_template_name.isValid()) {
							substitution_arg = TemplateTypeArg::makeTemplate(
								substitution.concrete_template_name);
						} else if (substitution.is_value_param) {
							if (substitution.typed_value_identity.has_value()) {
								substitution_arg = TemplateTypeArg::makeValueIdentity(
									*substitution.typed_value_identity);
							} else {
								substitution_arg = TemplateTypeArg(
									substitution.value,
									substitution.value_type);
							}
						} else {
							continue;
						}
						substitution_outer_binding.params.push_back(
							ASTNode::emplace_node<TemplateParameterNode>(owner_param));
						substitution_outer_binding.param_names.push_back(
							owner_param.nameHandle());
						substitution_outer_binding.param_args.push_back(
							substitution_arg);
						substitution_outer_binding.all_args.push_back(
							substitution_arg);
						break;
					}
				}
				if (!substitution_outer_binding.param_names.empty()) {
					synthesized_outer_binding = std::move(substitution_outer_binding);
					outer_binding = &*synthesized_outer_binding;
				}
			}
		}
	}
	auto buildDefaultEvalSubstitutionInputs =
		[&](std::span<const TemplateTypeArg> current_args) {
		InlineVector<TemplateParameterNode, 4> substitution_params;
		InlineVector<TemplateTypeArg, 4> substitution_args;
		substitution_params.reserve(template_params.size() + 4);
		substitution_args.reserve(current_args.size() + 4);

		size_t outer_prefix_count = 0;
		bool template_params_already_include_outer_prefix = false;
		if (outer_binding != nullptr) {
			outer_prefix_count = std::min(
				outer_binding->param_names.size(),
				template_params.size());
			template_params_already_include_outer_prefix = (outer_prefix_count > 0);
			for (size_t prefix_index = 0; prefix_index < outer_prefix_count; ++prefix_index) {
				const TemplateParameterNode* template_param_node =
					tryGetTemplateParameterNode(template_params[prefix_index]);
				if (template_param_node == nullptr ||
					template_param_node->nameHandle() != outer_binding->param_names[prefix_index]) {
					template_params_already_include_outer_prefix = false;
					break;
				}
			}
		}

		if (outer_binding != nullptr && !template_params_already_include_outer_prefix) {
			if (!outer_binding->params.empty()) {
				for (const ASTNode& outer_param_node : outer_binding->params) {
					if (const TemplateParameterNode* outer_param =
							tryGetTemplateParameterNode(outer_param_node);
						outer_param != nullptr) {
						substitution_params.push_back(*outer_param);
					}
				}
			} else {
				for (size_t i = 0;
					 i < outer_binding->param_names.size() &&
					 i < outer_binding->param_args.size();
					 ++i) {
					substitution_params.push_back(
						rebuildOuterTemplateParameter(
							outer_binding->param_names[i],
							outer_binding->param_args[i]));
				}
			}
		}
		for (const TemplateParameterNode& template_param : template_params) {
			substitution_params.push_back(template_param);
		}

		if (outer_binding != nullptr) {
			if (template_params_already_include_outer_prefix &&
				current_args.size() + outer_prefix_count == template_params.size()) {
				for (size_t i = 0;
					 i < outer_prefix_count &&
					 i < outer_binding->param_args.size();
					 ++i) {
					substitution_args.push_back(outer_binding->param_args[i]);
				}
			} else if (!template_params_already_include_outer_prefix) {
				for (const TemplateTypeArg& outer_arg : outer_binding->all_args) {
					substitution_args.push_back(outer_arg);
				}
				if (outer_binding->all_args.empty()) {
					for (const TemplateTypeArg& outer_arg : outer_binding->param_args) {
						substitution_args.push_back(outer_arg);
					}
				}
			}
		}
		for (const TemplateTypeArg& current_arg : current_args) {
			substitution_args.push_back(current_arg);
		}

		return std::pair<InlineVector<TemplateParameterNode, 4>, InlineVector<TemplateTypeArg, 4>>(
			std::move(substitution_params),
			std::move(substitution_args));
	};

	// Fill in default arguments for missing parameters
	for (size_t i = filled_template_args.size(); i < template_params.size(); ++i) {
		const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
		if (param == nullptr) {
			continue;
		}
		// Skip variadic parameters - they're allowed to be empty
		if (param->is_variadic()) {
			continue;
		}

		if (!param->has_default()) {
			FLASH_LOG(Templates, Error, "Template '", template_name, "': Param ", i, " has no default (got ",
					  template_args.size(), " args, need ", template_params.size(), "), returning nullopt");
			return std::nullopt; // Missing required template argument
		}

		// Track size before processing to detect if a value was pushed.
		// Every non-variadic iteration MUST push exactly one element so that
		// filled_template_args[j] stays in sync with template_params[j].
		size_t size_before = filled_template_args.size();

		// Use the default value
		if (param->kind() == TemplateParameterKind::Type) {
			// For type parameters with defaults, extract the type
			const ASTNode& default_node = param->default_value();
			if (default_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();

				// Check if this is a dependent qualified type (like wrapper<T>::type)
				// that needs resolution based on already-filled template arguments
				bool resolved = false;
				if ((default_type.category() == TypeCategory::UserDefined || default_type.category() == TypeCategory::TypeAlias || default_type.category() == TypeCategory::Template) && default_type.type_index().is_valid()) {
					if (const TypeInfo* default_type_info = tryGetTypeInfo(default_type.type_index())) {
						std::string_view default_type_name = StringTable::getStringView(default_type_info->name());

						// Try to resolve using each filled argument
						for (size_t arg_idx = 0; arg_idx < filled_template_args.size(); ++arg_idx) {
							auto resolved_type = resolve_dependent_qualified_type(default_type_name, filled_template_args[arg_idx]);
							if (resolved_type.has_value()) {
								filled_template_args.push_back(*resolved_type);
								resolved = true;
								break;
							}
						}
					}
				}

				if (!resolved) {
					ASTNode substituted_default_node = substituteDefaultTemplateArg(default_node, template_params, filled_template_args);
					if (substituted_default_node.is<TypeSpecifierNode>()) {
						filled_template_args.push_back(TemplateTypeArg(substituted_default_node.as<TypeSpecifierNode>()));
						resolved = true;
					}
				}

				if (!resolved) {
					filled_template_args.push_back(TemplateTypeArg(default_type));
				}
			}
		} else if (param->kind() == TemplateParameterKind::NonType) {
			// For non-type parameters with defaults, evaluate the expression
			const ASTNode& default_node = param->default_value();
			FLASH_LOG(Templates, Debug, "Processing non-type param default, is_expression=", default_node.is<ExpressionNode>());

			// Substitute template parameters in the default expression
			ASTNode substituted_default_node = default_node;
			if (!filled_template_args.empty() && default_node.is<ExpressionNode>()) {
				substituted_default_node = substituteNonTypeDefaultExpression(
					default_node,
					template_params,
					filled_template_args);
				FLASH_LOG(Templates, Debug, "Substituted template parameters in non-type default expression");
			}

			if (substituted_default_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = substituted_default_node.as<ExpressionNode>();
				FLASH_LOG(Templates, Debug, "Expression node type index: ", expr.index());
				if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
					const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
					FLASH_LOG(Templates, Debug, "Processing QualifiedIdentifierNode for non-type default");

					// Handle dependent static member access like is_arithmetic_void::value or is_arithmetic__Tp::value
					// namespace handle name = template instantiation name (e.g., is_arithmetic_void or is_arithmetic__Tp)
					// name() = member name (e.g., value)
					if (!qual_id.namespace_handle().isGlobal()) {
						std::string_view type_name = gNamespaceRegistry.getName(qual_id.namespace_handle());
						std::string_view member_name = qual_id.name();
						FLASH_LOG(Templates, Debug, "Non-global qualified id: type='", type_name, "', member='", member_name, "'");

						// Check if type_name contains a template parameter placeholder
						// Use TypeInfo-based detection for template instantiation placeholders
						auto [is_dependent, template_base_name] = isDependentTemplatePlaceholder(type_name);

						// Additional check: if not detected as template instantiation, check for param-like suffixes
						if (!is_dependent && !filled_template_args.empty()) {
							// Check if type_name ends with what looks like a template parameter
							// Mangling: template_name + "_" + param_name
							// For param "_Tp", this becomes: template_name + "_" + "_Tp" = template_name + "__Tp"
							size_t last_underscore = type_name.rfind('_');
							FLASH_LOG(Templates, Debug, "Checking for dependent param in type='", type_name, "', last_underscore=", last_underscore);
							if (last_underscore != std::string_view::npos && last_underscore > 0) {
								std::string_view suffix = type_name.substr(last_underscore + 1);
								FLASH_LOG(Templates, Debug, "Suffix='", suffix, "'");

								// Check if suffix looks like a template parameter
								// Template parameters typically start with uppercase (Tp, T, U) or underscore (_Tp)
								bool looks_like_param = false;
								if (!suffix.empty() && (std::isupper(static_cast<unsigned char>(suffix[0])) ||
														suffix[0] == '_')) {
									looks_like_param = true;
								}

								// Special case: if suffix is empty and the character before last_underscore is also '_',
								// then the param starts with '_'. Try splitting earlier.
								if (suffix.empty() && last_underscore > 0 && type_name[last_underscore - 1] == '_') {
									// Double underscore: template_name + "__" + rest_of_param
									// Try finding the underscore before the double underscore
									size_t prev_underscore = type_name.rfind('_', last_underscore - 1);
									if (prev_underscore != std::string_view::npos) {
										template_base_name = type_name.substr(0, prev_underscore);
										is_dependent = true;
										FLASH_LOG(Templates, Debug, "Double underscore detected, template_base_name='", template_base_name, "'");
									}
								} else if (looks_like_param) {
									// Check if there's a double underscore pattern (param starts with _)
									// Pattern: "template__Param" where Param starts with _
									// We split at position of second _, giving suffix without leading _
									// Check if previous char is also underscore
									if (last_underscore > 0 && type_name[last_underscore - 1] == '_') {
										// Yes, double underscore. Template name ends before the first of the two underscores
										template_base_name = type_name.substr(0, last_underscore - 1);
										is_dependent = true;
									} else {
										// Single underscore separator
										template_base_name = type_name.substr(0, last_underscore);
										is_dependent = true;
									}
								}

								// Already set is_dependent=true above, just log
								if (!template_base_name.empty()) {
									FLASH_LOG(Templates, Debug, "Looks like template param! template_base_name='", template_base_name, "'");
								}
							}
						}

						if (is_dependent && !filled_template_args.empty()) {
							if (tryAppendStaticMemberValueFromTypeName(
									filled_template_args,
									type_name,
									member_name,
									template_params,
									filled_template_args,
									"dependent qualified static member")) {
								FLASH_LOG(Templates, Debug, "Resolved dependent qualified identifier: ",
										  type_name, "::", member_name);
							}
						}
					}
				}
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
					const auto& val = lit.value();
					if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
						int64_t int_val = static_cast<int64_t>(*ull_val);
						filled_template_args.push_back(TemplateTypeArg(int_val));
					} else if (const auto* d_val = std::get_if<double>(&val)) {
						int64_t int_val = static_cast<int64_t>(*d_val);
						filled_template_args.push_back(TemplateTypeArg(int_val));
					}
				} else if (const auto* bool_literal = std::get_if<BoolLiteralNode>(&expr)) {
					// Handle boolean literals
					const BoolLiteralNode& lit = *bool_literal;
					filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, TypeCategory::Bool));
				} else if (std::holds_alternative<MemberAccessNode>(expr)) {
					// Handle dependent expressions like is_arithmetic<T>::value
					const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
					std::string_view member_name = member_access.member_name();

					FLASH_LOG(Templates, Debug, "Processing MemberAccess for non-type default: member='", member_name, "'");

					// Check if the object is a type/template instantiation
					ASTNode object_node = member_access.object();
					if (object_node.is<ExpressionNode>()) {
						const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();

						// The object might be an IdentifierNode referencing a template
						if (std::holds_alternative<IdentifierNode>(obj_expr)) {
							const IdentifierNode& obj_id = std::get<IdentifierNode>(obj_expr);
							std::string_view obj_name = obj_id.name();

							FLASH_LOG(Templates, Debug, "MemberAccess object is IdentifierNode: '", obj_name, "'");

							// Check if this identifier has template arguments stored separately
							// For now, look for a type that was parsed as a dependent template instantiation
							// The type name might be stored like "is_arithmetic$hash" for is_arithmetic<T>

							// Try looking up as a dependent template instantiation
							// Build the instantiated name using filled_template_args
							if (!filled_template_args.empty()) {
								if (tryAppendStaticMemberValueFromTypeName(
										filled_template_args,
										obj_name,
										member_name,
										template_params,
										filled_template_args,
										"dependent member-access static member")) {
									FLASH_LOG(Templates, Debug, "Resolved static member '", member_name,
											  "' from instantiated type '", obj_name, "'");
								}
							}
						}
					}
				} else if (std::holds_alternative<SizeofExprNode>(expr)) {
					// Handle sizeof(T) as a default value
					const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr);
					if (sizeof_node.is_type()) {
						// sizeof(type) - evaluate the type size
						const ASTNode& type_node = sizeof_node.type_or_expr();
						if (type_node.is<TypeSpecifierNode>()) {
							TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();

							// Check if this is a template parameter that needs substitution
							bool found_substitution = false;
							std::string_view sizeof_type_name;

							// Try to get the type name from the token first (most reliable for template params)
							if (type_spec.token().type() == Token::Type::Identifier) {
								sizeof_type_name = type_spec.token().value();
							} else if ((type_spec.category() == TypeCategory::UserDefined || type_spec.category() == TypeCategory::TypeAlias || type_spec.category() == TypeCategory::Template) && type_spec.type_index().is_valid()) {
								// Fall back to gTypeInfo for fully resolved types
								if (const TypeInfo* sizeof_type_info = tryGetTypeInfo(type_spec.type_index())) {
									sizeof_type_name = StringTable::getStringView(sizeof_type_info->name());
								}
							}

							if (!sizeof_type_name.empty()) {
								// Check if this is one of the template parameters we've already filled
								for (size_t j = 0; j < template_params.size() && j < filled_template_args.size(); ++j) {
									if (const TemplateParameterNode* prev_param = tryGetTemplateParameterNode(template_params[j]);
										prev_param != nullptr) {
										if (prev_param->name() == sizeof_type_name) {
											// Found the matching template parameter - use its filled value
											const TemplateTypeArg& filled_arg = filled_template_args[j];
											if (filled_arg.category() != TypeCategory::Invalid) {
												const int size_in_bytes = getTemplateArgumentSizeInBytes(filled_arg);
												if (size_in_bytes > 0) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
													FLASH_LOG(Templates, Debug, "Filled in sizeof(", sizeof_type_name, ") default for instantiation: ", size_in_bytes, " bytes");
													found_substitution = true;
													break;
												}
											}
										}
									}
								}
							}

							if (!found_substitution) {
								// Direct type (not a template parameter)
								int size_in_bits = type_spec.size_in_bits();
								int size_in_bytes = (size_in_bits + 7) / 8; // Round up to bytes
								filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
								FLASH_LOG(Templates, Debug, "Filled in sizeof default for instantiation: ", size_in_bytes, " bytes");
							}
						}
					}
				}
			}

			// NonType fallback: if no handler above pushed a value, retry through the shared
			// template-default substitution/evaluation helper so ConstExpr sees the instantiated
			// template bindings instead of a context-free expression.
			if (filled_template_args.size() == size_before) {
				auto [substitution_params, substitution_args] =
					buildDefaultEvalSubstitutionInputs(
						std::span<const TemplateTypeArg>(
							filled_template_args.data(),
							filled_template_args.size()));
				std::vector<std::string_view> template_param_names =
					collectTemplateParamNames(substitution_params);
				if (auto evaluated_default = substituteAndEvaluateNonTypeDefault(
						default_node,
						substitution_params,
						std::span<const TemplateTypeArg>(
							substitution_args.data(),
							substitution_args.size()),
						std::span<const std::string_view>(
							template_param_names.data(),
							template_param_names.size()));
					evaluated_default.has_value()) {
					filled_template_args.push_back(*evaluated_default);
				}
			}
		} else if (param->kind() == TemplateParameterKind::Template && param->has_default()) {
			const ASTNode& default_node = param->default_value();
			if (default_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
				StringHandle tpl_name_handle = StringTable::getOrInternStringHandle(default_type.token().value());
				if (const TypeInfo* type_info = tryGetTypeInfo(default_type.type_index())) {
					tpl_name_handle = type_info->name();
				}
				filled_template_args.push_back(TemplateTypeArg::makeTemplate(tpl_name_handle));
			}
		}

		// Retry unresolved defaults through the shared default-argument materialization
		// path before falling into catch-all placeholder/error handling.
		if (filled_template_args.size() == size_before &&
			(param->kind() == TemplateParameterKind::Type ||
			 param->kind() == TemplateParameterKind::NonType)) {
			InlineVector<TemplateTypeArg, 4> retry_args = toInlineTemplateArgs(filled_template_args);
			const bool appended_default =
				outer_binding != nullptr
					? tryAppendMemberDefaultTemplateArg(
						  *param,
						  template_params,
						  outer_binding,
						  retry_args)
					: tryAppendDefaultTemplateArg(
						  *param,
						  std::span<const TemplateParameterNode>(
							  template_params.data(),
							  template_params.size()),
						  retry_args,
						  default_arg_source_namespace);
			if (appended_default) {
				filled_template_args.assign(retry_args.begin(), retry_args.end());
			}
		}

		// Catch-all: ensure filled_template_args grows by exactly 1 per non-variadic
		// parameter so that filled_template_args[j] stays in sync with template_params[j].
		// This covers: Type defaults whose node isn't TypeSpecifierNode, NonType defaults
		// that no handler could evaluate, and any other unhandled parameter kind.
		if (filled_template_args.size() == size_before) {
			if (param->kind() == TemplateParameterKind::Type) {
				throw CompileError(
					"Could not resolve type template default for parameter " +
					std::to_string(i) +
					" of '" +
					std::string(template_name) +
					"'");
			} else if (param->kind() == TemplateParameterKind::Template) {
				throw InternalError(
					std::string("Could not resolve template-template default for param ") +
					std::to_string(i) + " of '" + std::string(template_name) + "'");
			} else {
				throw CompileError(std::string(StringBuilder()
					.append("Could not evaluate non-type template default for parameter ")
					.append(std::to_string(i))
					.append(" of '")
					.append(template_name)
					.append("'")
					.commit()));
			}
		}
	}

	// Use the filled template args for the rest of the function
	std::span<const TemplateTypeArg> template_args_to_use = filled_template_args;
	InlineVector<TemplateParameterNode, 4> effective_template_params;
	InlineVector<TemplateTypeArg, 4> effective_template_args;
	bool template_params_already_include_outer_prefix = false;
	size_t outer_prefix_count = 0;
	if (outer_binding != nullptr) {
		outer_prefix_count = std::min(
			outer_binding->param_names.size(),
			template_params.size());
		template_params_already_include_outer_prefix = (outer_prefix_count > 0);
		for (size_t i = 0; i < outer_prefix_count; ++i) {
			const TemplateParameterNode* param_node =
				tryGetTemplateParameterNode(template_params[i]);
			if (param_node == nullptr ||
				param_node->nameHandle() != outer_binding->param_names[i]) {
				template_params_already_include_outer_prefix = false;
				break;
			}
		}
	}

	if (outer_binding != nullptr && !template_params_already_include_outer_prefix) {
		for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
			StringHandle outer_name = outer_binding->param_names[i];
			const TemplateTypeArg& outer_arg = outer_binding->param_args[i];
			Token outer_token(Token::Type::Identifier, StringTable::getStringView(outer_name), 0, 0, 0);
			if (outer_arg.is_value) {
				TypeSpecifierNode outer_type(
					outer_arg.type_index.withCategory(outer_arg.typeEnum()),
					get_type_size_bits(outer_arg.typeEnum()),
					outer_token,
					CVQualifier::None,
					ReferenceQualifier::None);
				effective_template_params.push_back(TemplateParameterNode(outer_name, outer_type, outer_token));
			} else {
				TemplateParameterNode outer_param(outer_name, outer_token);
				outer_param.set_registered_type_index(outer_arg.type_index.withCategory(outer_arg.typeEnum()));
				effective_template_params.push_back(outer_param);
			}
			effective_template_args.push_back(outer_arg);
		}
	}
	for (const TemplateParameterNode& template_param : template_params) {
		effective_template_params.push_back(template_param);
	}
	if (outer_binding != nullptr &&
		template_params_already_include_outer_prefix &&
		template_args_to_use.size() + outer_prefix_count == template_params.size()) {
		for (size_t i = 0; i < outer_prefix_count && i < outer_binding->param_args.size(); ++i) {
			effective_template_args.push_back(outer_binding->param_args[i]);
		}
	}
	for (const TemplateTypeArg& template_arg : template_args_to_use) {
		effective_template_args.push_back(template_arg);
	}
	std::vector<TemplateTypeArg> effective_template_args_vector(
		effective_template_args.begin(),
		effective_template_args.end());
	auto normalized_cache_key = FlashCpp::makeInstantiationKey(
		template_name_handle,
		std::span<const TemplateTypeArg>(
			effective_template_args_vector.data(),
			effective_template_args_vector.size()));
	cache_key = normalized_cache_key;
	if (!can_use_raw_cache_key) {
		if (auto normalized_cached = gTemplateRegistry.getInstantiation(normalized_cache_key); normalized_cached.has_value()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Cache hit for '{}' with {} normalized args", template_name, template_args_to_use.size());
			return std::nullopt;
		}
	}

	// Build substitution maps for dependent template entities (used by deferred bases and decltype bases)
	TemplateArgSubstitutionMap name_substitution_map;
	TemplateArgPackSubstitutionMap pack_substitution_map;
	std::vector<std::string_view> template_param_order;
	bool substitution_maps_initialized = false;
	auto ensure_substitution_maps = [&]() {
		if (substitution_maps_initialized) {
			return;
		}

		buildTemplateArgSubstitutionMaps(
			template_params,
			template_args_to_use,
			[&](const TemplateParameterNode& tparam, const TemplateTypeArg& arg) {
				return enrichTemplateArgForParameter(tparam, arg);
			},
			name_substitution_map,
			pack_substitution_map,
			&template_param_order);

		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
			if (tparam == nullptr) {
				continue;
			}
			std::string_view param_name = tparam->name();
			if (tparam->is_variadic()) {
				auto pack_it = pack_substitution_map.find(
					StringTable::getOrInternStringHandle(param_name));
				if (pack_it != pack_substitution_map.end()) {
					FLASH_LOG(Templates, Debug, "Added pack substitution: ",
							  param_name, " -> ", pack_it->second.size(), " arguments");
				}
				break;
			}
			auto subst_it = name_substitution_map.find(param_name);
			if (subst_it != name_substitution_map.end()) {
				const TemplateTypeArg& arg_to_insert = subst_it->second;
				FLASH_LOG(Templates, Debug, "Added substitution: ", param_name,
						  " -> base_type=", static_cast<int>(arg_to_insert.typeEnum()),
						  " type_index=", arg_to_insert.type_index,
						  " is_value=", arg_to_insert.is_value,
						  " is_ttp=", arg_to_insert.is_template_template_arg);
			}
		}

		substitution_maps_initialized = true;
	};

	auto resolveConcreteBaseType = [&](TemplateTypeArg& concrete_arg) -> const TypeInfo* {
		if (concrete_arg.is_value || !concrete_arg.type_index.is_valid()) {
			return nullptr;
		}

		const TypeInfo* concrete_type = tryGetTypeInfo(concrete_arg.type_index);
		if (!concrete_type) {
			return nullptr;
		}

		auto materialize_template_placeholder = [&](bool inner_force_eager) {
			if (!concrete_type->isTemplateInstantiation() ||
				(concrete_type->getStructInfo() && concrete_type->getStructInfo()->sizeInBytes().is_set())) {
				return false;
			}

			std::string_view base_template_name = StringTable::getStringView(concrete_type->baseTemplateName());
			if (base_template_name.empty()) {
				return false;
			}

			std::vector<TemplateTypeArg> concrete_base_args =
				materializeTemplateArgs(*concrete_type, template_params, template_args_to_use);
			std::string_view instantiated_base_name =
				instantiateAndResolveBaseName(base_template_name, concrete_base_args, inner_force_eager);
			auto instantiated_base_it =
				getTypesByNameMap().find(StringTable::getOrInternStringHandle(instantiated_base_name));
			if (instantiated_base_it == getTypesByNameMap().end()) {
				return false;
			}

			concrete_type = instantiated_base_it->second;
			concrete_arg.type_index = FlashCpp::canonicalizeTemplateIdentityTypeIndex(
				concrete_type->registeredTypeIndex().withCategory(concrete_type->typeEnum()));
			return true;
		};

		materialize_template_placeholder(false);

		// Deep alias chains show up in template-heavy code; keep enough headroom
		// before stopping alias resolution.
		size_t alias_unwrap_iterations_remaining = kMaxAliasUnwrapIterations;
		while (concrete_type && !concrete_type->isStruct() && alias_unwrap_iterations_remaining-- > 0) {
			if (materialize_template_placeholder(false) && concrete_type->isStruct()) {
				break;
			}

			const TypeInfo* underlying_type = tryGetTypeInfo(concrete_type->type_index_);
			if (!underlying_type || underlying_type == concrete_type) {
				break;
			}

			concrete_type = underlying_type;
			concrete_arg.type_index = FlashCpp::canonicalizeTemplateIdentityTypeIndex(
				concrete_type->registeredTypeIndex().withCategory(concrete_type->typeEnum()));
		}

		if (concrete_type && concrete_type->getStructInfo()) {
			concrete_arg.type_index = FlashCpp::canonicalizeTemplateIdentityTypeIndex(
				concrete_type->registeredTypeIndex().withCategory(TypeCategory::Struct));
		}

		return concrete_type;
	};

	auto canonicalizeRecordedBase = [&](const TypeInfo* base_type_info, TypeIndex base_type_index)
		-> std::pair<const TypeInfo*, TypeIndex> {
		TypeIndex canonical_index = base_type_index;
		if (canonical_index.is_valid()) {
			canonical_index = canonicalize_type_alias(canonical_index).resolvedTypeIndex();
		}
		if (const TypeInfo* canonical_type = tryGetTypeInfo(canonical_index);
			canonical_type != nullptr && canonical_type->getStructInfo() != nullptr) {
			return {
				canonical_type,
				FlashCpp::canonicalizeTemplateIdentityTypeIndex(
					canonical_type->registeredTypeIndex().withCategory(TypeCategory::Struct))
			};
		}
		if (base_type_info != nullptr && base_type_info->getStructInfo() != nullptr) {
			return {
				base_type_info,
				FlashCpp::canonicalizeTemplateIdentityTypeIndex(
					base_type_info->registeredTypeIndex().withCategory(TypeCategory::Struct))
			};
		}
		return {base_type_info, base_type_index};
	};

	// Generate the instantiated class name (again, with filled args)
	instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args_to_use));

	// Check if we already have this instantiation (after filling defaults)
	existing_type = getTypesByNameMap().find(instantiated_name);
	if (existing_type != getTypesByNameMap().end()) {
		auto cached_reg = gTemplateRegistry.getInstantiation(cache_key);
		const ASTNode* cached_node = cached_reg.has_value() ? &cached_reg.value() : nullptr;
		const StructDeclarationNode* cached_struct = getCachedTemplateStructDecl(cached_node);
		if (cached_struct && cached_struct->needs_shape_only_upgrade(current_wants_full)) {
			FLASH_LOG(Templates, Debug, "Type already exists from ShapeOnly instantiation, continuing to upgrade");
		} else {
			FLASH_LOG(Templates, Debug, "Type already exists, returning nullopt");
			// Already instantiated, return the existing struct node
			// We need to find the struct node in the AST
			// For now, just return nullopt and let the type lookup handle it
			return std::nullopt;
		}
	}

	// Resolve the namespace where the template was DECLARED, not where it's being instantiated.
	// For "std::vector", this returns the NamespaceHandle for "std".
	// For unqualified names like "Vec", derive from class_decl.name() which may be "math::Vec".
	NamespaceHandle decl_ns = gSymbolTable.get_current_namespace_handle();
	{
		if (template_name.find("::") != std::string_view::npos) {
			decl_ns = QualifiedIdentifier::fromQualifiedName(template_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
		} else {
			std::string_view decl_name = StringTable::getStringView(class_decl.name());
			if (size_t pos = decl_name.rfind("::"); pos != std::string_view::npos) {
				decl_ns = QualifiedIdentifier::fromQualifiedName(decl_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
			}
		}
	}

	// Create a new struct type for the instantiation (but don't create AST node for template instantiations)
	TypeInfo& struct_type_info = add_struct_type(instantiated_name, decl_ns);

	// Store template instantiation metadata for O(1) lookup (Phase 6)
	// This allows us to check if a type is a template instantiation without parsing the name
	// QualifiedIdentifier captures both the namespace and unqualified name.
	auto template_args_info = collectEnrichedTemplateArgInfos(template_params, template_args_to_use);
	struct_type_info.setTemplateInstantiationInfo(
		QualifiedIdentifier::fromQualifiedName(template_name, decl_ns),
		template_args_info);

	// Populate type-owned instantiation context so that codegen and constexpr
	// evaluation can resolve template bindings without relying on ambient parser state.
	// Use effective (outer + local) bindings when available; this avoids partial
	// substitution in nested member-template contexts.
	InlineVector<TypeInfo::TemplateArgInfo, 4> instantiation_context_args_info =
		collectEnrichedTemplateArgInfos(
			effective_template_params,
			std::span<const TemplateTypeArg>(
				effective_template_args.data(),
				effective_template_args.size()));
	struct_type_info.setInstantiationContext(
		collectParamNameHandles(
			effective_template_params,
			effective_template_args.size()),
		instantiation_context_args_info,
		nullptr);

	// Register class template pack sizes in persistent registry for member function template lookup
	if (has_parameter_pack) {
		std::vector<ClassTemplatePackInfo> pack_infos;
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode* param = tryGetTemplateParameterNode(template_params[i]);
			if (param != nullptr && param->is_variadic()) {
				size_t pack_size = template_args_to_use.size() >= non_variadic_param_count
									   ? template_args_to_use.size() - non_variadic_param_count
									   : 0;
				pack_infos.push_back({param->name(), pack_size});
			}
		}
		if (!pack_infos.empty()) {
			class_template_pack_registry_[instantiated_name] = std::move(pack_infos);
		}
	}

	// Create StructTypeInfo
	auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, AccessSpecifier::Public, class_decl.is_union(), decl_ns);

	// Handle base classes from the primary template
	// Base classes need to be instantiated with concrete template arguments
	FLASH_LOG(Templates, Debug, "Primary template has ", class_decl.base_classes().size(), " base classes");
	for (const auto& base : class_decl.base_classes()) {
		std::string_view base_class_name = base.name;
		FLASH_LOG(Templates, Debug, "Processing primary template base class: ", base_class_name);

		// Check if this base class is deferred (a template parameter)
		if (base.is_deferred) {
			FLASH_LOG(Templates, Debug, "Base class '", base_class_name, "' is deferred - resolving with concrete type");

			auto try_add_concrete_base = [&](TemplateTypeArg concrete_arg) -> bool {
				TemplateTypeArg original_arg = concrete_arg;
				const TypeInfo* concrete_type = resolveConcreteBaseType(concrete_arg);
				if ((!concrete_type || !concrete_type->getStructInfo()) &&
					!original_arg.is_value &&
					original_arg.type_index.is_valid()) {
					if (const TypeInfo* unresolved_type = tryGetTypeInfo(original_arg.type_index)) {
						if (unresolved_type->isTemplateInstantiation()) {
							std::string_view base_template_name = StringTable::getStringView(unresolved_type->baseTemplateName());
							if (!base_template_name.empty()) {
								std::vector<TemplateTypeArg> concrete_base_args =
									materializeTemplateArgs(*unresolved_type, template_params, template_args_to_use);
								std::string_view instantiated_base_name =
									instantiateAndResolveBaseName(base_template_name, concrete_base_args, true);
								auto instantiated_base_it =
									getTypesByNameMap().find(StringTable::getOrInternStringHandle(instantiated_base_name));
								if (instantiated_base_it != getTypesByNameMap().end()) {
									concrete_type = instantiated_base_it->second;
									concrete_arg.type_index = concrete_type->registeredTypeIndex();
									concrete_arg.type_index.setCategory(concrete_type->typeEnum());
								}
							}
						}
					}
				}

				if (concrete_arg.type_index.index() >= getTypeInfoCount()) {
					FLASH_LOG(Templates, Error, "Template argument for base class has invalid type_index: ", concrete_arg.type_index);
					return false;
				}
				if (!concrete_type || !concrete_type->getStructInfo()) {
					if (const TypeInfo* fallback_type = tryGetTypeInfo(concrete_arg.type_index)) {
						FLASH_LOG(Templates, Debug, "Deferred base '", base_class_name,
								  "' did not resolve to a concrete struct (candidate='",
								  StringTable::getStringView(fallback_type->name()), "')");
					}
					return false;
				}
				if (concrete_type->struct_info_ && concrete_type->struct_info_->is_final) {
					FLASH_LOG(Templates, Error, "Cannot inherit from final class '", concrete_type->name_, "'");
					return false;
				}

				auto [recorded_base_type, recorded_base_index] =
					canonicalizeRecordedBase(concrete_type, concrete_arg.type_index);
				struct_info->addBaseClass(
					StringTable::getStringView(recorded_base_type->name()),
					recorded_base_index,
					base.access,
					base.is_virtual);
				FLASH_LOG(Templates, Debug, "Resolved deferred base '", base_class_name,
						  "' to concrete type '", StringTable::getStringView(recorded_base_type->name()),
						  "' with type_index=", recorded_base_index);
				return true;
			};

			// Use name_substitution_map (which correctly handles all param kinds) to resolve
			ensure_substitution_maps();
			auto subst_it = name_substitution_map.find(base_class_name);
			bool found = false;
			if (subst_it != name_substitution_map.end()) {
				TemplateTypeArg concrete_arg = subst_it->second;
				found = try_add_concrete_base(concrete_arg);
			}

			if (!found) {
				// Check if this is a variadic pack parameter (e.g., struct Combined : Bases...)
				// Pack params are in pack_substitution_map, not name_substitution_map
				ensure_substitution_maps();
				StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class_name);
				auto pack_it = pack_substitution_map.find(base_name_handle);
				if (pack_it != pack_substitution_map.end()) {
					for (const TemplateTypeArg& pack_arg : pack_it->second) {
						TemplateTypeArg resolved_pack_arg = pack_arg;
						found = try_add_concrete_base(resolved_pack_arg) || found;
					}
				}
			}
			if (!found && base.type_index.is_valid()) {
				TemplateTypeArg deferred_base_arg;
				deferred_base_arg.type_index = base.type_index;
				if (const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index)) {
					deferred_base_arg.type_index = base_type_info->registeredTypeIndex();
					deferred_base_arg.type_index.setCategory(base_type_info->typeEnum());
				}
				found = try_add_concrete_base(deferred_base_arg);
				if (!found) {
					if (const TypeInfo* unresolved_base = tryGetTypeInfo(base.type_index)) {
						struct_info->addBaseClass(
							StringTable::getStringView(unresolved_base->name()),
							unresolved_base->registeredTypeIndex(),
							base.access,
							base.is_virtual);
						FLASH_LOG(Templates, Debug, "Preserved unresolved deferred base '", base_class_name,
								  "' as placeholder '", StringTable::getStringView(unresolved_base->name()), "'");
						found = true;
					}
				}
			}
			if (!found) {
				FLASH_LOG(Templates, Warning, "Could not resolve deferred base class: ", base_class_name);
			}
		} else {
			// Regular (non-deferred) base class
			// Look up the base class type (may need to resolve type aliases)
			auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(base_class_name));
			if (base_type_it != getTypesByNameMap().end()) {
				const TypeInfo* base_type_info = base_type_it->second;
				auto [recorded_base_type, recorded_base_index] =
					canonicalizeRecordedBase(base_type_info, base_type_info->registeredTypeIndex().withCategory(base_type_info->typeEnum()));
				struct_info->addBaseClass(
					StringTable::getStringView(recorded_base_type->name()),
					recorded_base_index,
					base.access,
					base.is_virtual);
				FLASH_LOG(Templates, Debug, "Added base class: ", StringTable::getStringView(recorded_base_type->name()),
						  " with type_index=", recorded_base_index);
			} else {
				FLASH_LOG(Templates, Warning, "Base class ", base_class_name, " not found in getTypesByNameMap()");
			}
		}
	}

	// Handle deferred template base classes (with dependent template arguments)
	FLASH_LOG_FORMAT(Templates, Debug, "Template '{}' has {} deferred template base classes",
					 StringTable::getStringView(class_decl.name()), class_decl.deferred_template_base_classes().size());
	if (!class_decl.deferred_template_base_classes().empty()) {
		ensure_substitution_maps();
		auto identifier_matches = [](std::string_view haystack, std::string_view needle) {
			size_t pos = haystack.find(needle);
			auto is_ident_char = [](char ch) {
				return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
			};
			while (pos != std::string_view::npos) {
				bool start_ok = (pos == 0) || !is_ident_char(haystack[pos - 1]);
				bool end_ok = (pos + needle.size() >= haystack.size()) || !is_ident_char(haystack[pos + needle.size()]);
				if (start_ok && end_ok) {
					return true;
				}
				pos = haystack.find(needle, pos + 1);
			}
			return false;
		};

		for (const auto& deferred_base : class_decl.deferred_template_base_classes()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Processing deferred template base '{}' with {} template args",
							 StringTable::getStringView(deferred_base.base_template_name), deferred_base.template_arguments.size());
			DeferredBaseReplayContextScope deferred_base_replay_scope(
				*this,
				deferred_base,
				class_decl.name(),
				template_params,
				template_args_to_use);

			const InlineVector<TemplateParameterNode, 4>* deferred_base_template_params = nullptr;
			if (auto base_template_node = gTemplateRegistry.lookupTemplate(deferred_base.base_template_name);
				base_template_node.has_value() && base_template_node->is<TemplateClassDeclarationNode>()) {
				deferred_base_template_params = &base_template_node->as<TemplateClassDeclarationNode>().template_parameters();
			}
			auto makeTypeIndexForDeferredBaseNttp = [&](size_t arg_index, std::span<const TemplateTypeArg> resolved_args) {
				TypeIndex target_index{};
				if (deferred_base_template_params != nullptr &&
					arg_index < deferred_base_template_params->size()) {
					const TemplateParameterNode& target_param = (*deferred_base_template_params)[arg_index];
					if (target_param.kind() == TemplateParameterKind::NonType &&
						target_param.has_type()) {
						const TypeSpecifierNode& param_type = target_param.type_specifier_node();
						std::string_view param_type_name = param_type.token().value();
						if (param_type_name.empty() &&
							param_type.type_index().is_valid()) {
							if (const TypeInfo* param_type_info = tryGetTypeInfo(param_type.type_index())) {
								param_type_name = StringTable::getStringView(param_type_info->name());
							}
						}
						for (size_t prior_index = 0;
							 prior_index < arg_index &&
							 prior_index < deferred_base_template_params->size() &&
							 prior_index < resolved_args.size();
							 ++prior_index) {
							const TemplateParameterNode& prior_param = (*deferred_base_template_params)[prior_index];
							if (prior_param.kind() == TemplateParameterKind::Type &&
								prior_param.name() == param_type_name) {
								target_index = resolved_args[prior_index].type_index.withCategory(resolved_args[prior_index].typeEnum());
								break;
							}
						}
						if (target_index.category() == TypeCategory::Invalid &&
							(param_type.category() == TypeCategory::UserDefined ||
							 param_type.category() == TypeCategory::Template ||
							 typeIndexContainsDependentPlaceholder(param_type.type_index()))) {
							const TemplateTypeArg* sole_prior_type_arg = nullptr;
							for (size_t prior_index = 0;
								 prior_index < arg_index &&
								 prior_index < deferred_base_template_params->size() &&
								 prior_index < resolved_args.size();
								 ++prior_index) {
								const TemplateParameterNode& prior_param = (*deferred_base_template_params)[prior_index];
								if (prior_param.kind() != TemplateParameterKind::Type ||
									resolved_args[prior_index].is_value) {
									continue;
								}
								if (sole_prior_type_arg != nullptr) {
									sole_prior_type_arg = nullptr;
									break;
								}
								sole_prior_type_arg = &resolved_args[prior_index];
							}
							if (sole_prior_type_arg != nullptr) {
								target_index = sole_prior_type_arg->type_index.withCategory(sole_prior_type_arg->typeEnum());
							}
						}
						if (target_index.category() == TypeCategory::Invalid) {
							TypeCategory target_category = param_type.category();
							target_index = param_type.type_index();
							if (target_index.is_valid()) {
								if (const TypeInfo* target_type_info = tryGetTypeInfo(target_index)) {
									if (target_type_info->typeEnum() != TypeCategory::Invalid) {
										target_category = target_type_info->typeEnum();
									}
								}
								target_index = target_index.withCategory(target_category);
							} else if (TypeIndex native_index = nativeTypeIndex(target_category); native_index.is_valid()) {
								target_index = native_index.withCategory(target_category);
							} else {
								target_index = TypeIndex{0, target_category};
							}
						}
					}
				}
				return target_index;
			};
			DeferredBasePackExpansionBindingInfo base_pack_bindings =
				collectDeferredBasePackExpansionBindings(deferred_base, pack_substitution_map);
			if (base_pack_bindings.invalid) {
				FLASH_LOG(Templates, Warning, "Deferred base '",
						  StringTable::getStringView(deferred_base.base_template_name),
						  "' has mismatched pack sizes in pack expansion");
				continue;
			}

			size_t expansion_count = base_pack_bindings.pack_bindings.empty()
				? 1
				: base_pack_bindings.expansion_count;
			for (size_t expansion_index = 0; expansion_index < expansion_count; ++expansion_index) {
				TemplateArgSubstitutionMap subst_map =
					makeDeferredBaseExpansionSubstitutionMap(
						name_substitution_map,
						base_pack_bindings,
						expansion_index);

				std::vector<TemplateTypeArg> resolved_args;
				bool unresolved_arg = false;
				for (size_t deferred_arg_index = 0; deferred_arg_index < deferred_base.template_arguments.size(); ++deferred_arg_index) {
					const auto& arg_info = deferred_base.template_arguments[deferred_arg_index];
					// Pack expansion handling
					if (arg_info.is_pack) {
						// If the argument node references a template parameter pack, expand it
						if (arg_info.node.is<ExpressionNode>()) {
							const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
							if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
								StringHandle pack_name = std::get<TemplateParameterReferenceNode>(expr).param_name();
								auto pack_it = pack_substitution_map.find(pack_name);
								if (pack_it != pack_substitution_map.end()) {
									resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
									continue;
								} else if (!template_args_to_use.empty()) {
									resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
									continue;
								}
							}
							// Also handle IdentifierNode - it may represent a pack parameter that wasn't converted to TemplateParameterReferenceNode
							else if (std::holds_alternative<IdentifierNode>(expr)) {
								const IdentifierNode& id = std::get<IdentifierNode>(expr);
								StringHandle pack_name = StringTable::getOrInternStringHandle(id.name());
								auto pack_it = pack_substitution_map.find(pack_name);
								if (pack_it != pack_substitution_map.end()) {
									resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
									continue;
								} else if (!template_args_to_use.empty()) {
									resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
									continue;
								}
							}
						} else if (arg_info.node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& type_spec = arg_info.node.as<TypeSpecifierNode>();
							TypeIndex idx = type_spec.type_index();
							if (const TypeInfo* idx_ti = tryGetTypeInfo(idx)) {
								StringHandle pack_name = idx_ti->name_;
								auto pack_it = pack_substitution_map.find(pack_name);
								if (pack_it != pack_substitution_map.end()) {
									resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
									continue;
								} else if (!template_args_to_use.empty()) {
									resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
									continue;
								}
							}
						}
					}

					// Resolve dependent type arguments
					if (arg_info.node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& type_spec = arg_info.node.as<TypeSpecifierNode>();
						TypeCategory resolved_type = type_spec.type();
						TypeIndex resolved_index = type_spec.type_index();
						bool resolved = false;
						auto is_concrete_materialized_arg =
							[](const TemplateTypeArg& candidate_arg) {
							if (candidate_arg.is_dependent ||
								candidate_arg.dependent_name.isValid() ||
								candidate_arg.dependent_expr.has_value()) {
								return false;
							}
							if (candidate_arg.is_value || !candidate_arg.type_index.is_valid()) {
								return true;
							}
							const TypeInfo* candidate_type_info =
								tryGetTypeInfo(candidate_arg.type_index);
							if (candidate_type_info == nullptr) {
								return false;
							}
							if (candidate_type_info->isTemplatePlaceholder() ||
								candidate_type_info->isDependentPlaceholder() ||
								(candidate_type_info->isDependentMemberType() &&
								 candidate_type_info->hasDependentQualifiedName())) {
								return false;
							}
							return true;
						};

						if ((is_struct_type(resolved_type)) && resolved_index.is_valid()) {
							if (const TypeInfo* resolved_ti = tryGetTypeInfo(resolved_index)) {
								std::string_view type_name = StringTable::getStringView(resolved_ti->name());
								auto subst_it = subst_map.find(type_name);
								if (subst_it != subst_map.end()) {
									TemplateTypeArg subst = rebindDependentTemplateTypeArg(
										subst_it->second,
										TemplateTypeArg(type_spec));
									subst.is_pack = arg_info.is_pack;
									resolved_args.push_back(subst);
									resolved = true;
								} else if (type_name.find("::") != std::string_view::npos) {
									// Dependent member alias of a template instantiation
									// (e.g., `__remove_cv<_Tp>::type` reaching us as a placeholder
									// `__remove_cv$<dep>::type`). Re-materialize the dependent base
									// template with the current outer substitutions, then resolve the
									// member alias against the concrete instantiation. This produces
									// the terminal underlying type (e.g., `int`) so the deferred base
									// receives a concrete-typed template argument and matches any
									// exact specialization registered for that concrete type.
									if (const TypeInfo* concrete_member_alias =
											materializeInstantiatedMemberAliasTarget(
												type_spec,
												template_params,
												template_args_to_use)) {
										TemplateTypeArg member_arg = resolveTypeInfoToTemplateArg(*concrete_member_alias, type_spec);
										if (is_concrete_materialized_arg(member_arg)) {
											member_arg.is_pack = arg_info.is_pack;
											resolved_args.push_back(member_arg);
											resolved = true;
											FLASH_LOG_FORMAT(Templates, Debug,
												"Resolved deferred base member alias '{}' to terminal type_index={}",
												type_name, member_arg.type_index);
										}
									}
								}
								if (!resolved) {
									const StructTypeInfo* resolved_struct_info = resolved_ti->getStructInfo();
									if (resolved_ti->isTemplateInstantiation() &&
										(!resolved_struct_info || !resolved_struct_info->sizeInBytes().is_set())) {
										StringHandle qualified_base_template_name =
											gNamespaceRegistry.buildQualifiedIdentifier(
												resolved_ti->sourceNamespace(),
												resolved_ti->baseTemplateName());
										std::vector<TemplateTypeArg> instantiated_args =
											materializeTemplateArgsExpandingPacks(*resolved_ti, template_params, template_args_to_use);
										auto alias_template_entry =
											gTemplateRegistry.lookup_alias_template(qualified_base_template_name);
										if (!alias_template_entry.has_value()) {
											alias_template_entry = gTemplateRegistry.lookup_alias_template(
												resolved_ti->baseTemplateName());
										}
										if (alias_template_entry.has_value() && alias_template_entry->is<TemplateAliasNode>()) {
											const TemplateAliasNode& alias_node = alias_template_entry->as<TemplateAliasNode>();
											if (const TypeInfo* concrete_member_alias =
													materializeInstantiatedMemberAliasTarget(
														alias_node.target_type_node(),
														alias_node.template_parameters(),
														instantiated_args)) {
												TemplateTypeArg inst_arg = resolveTypeInfoToTemplateArg(*concrete_member_alias, type_spec);
												if (is_concrete_materialized_arg(inst_arg)) {
													inst_arg.is_pack = arg_info.is_pack;
													resolved_args.push_back(inst_arg);
													resolved = true;
													FLASH_LOG_FORMAT(Templates, Debug,
														"Resolved deferred base alias-template member argument '{}' via alias target",
														type_name);
												}
											}
											if (resolved) {
												continue;
											}
											if (std::optional<TemplateTypeArg> rebound_arg =
													tryRebindAliasTargetTemplateArg(alias_node, instantiated_args);
												rebound_arg.has_value()) {
												TemplateTypeArg inst_arg = *rebound_arg;
												inst_arg.is_pack = arg_info.is_pack;
												resolved_args.push_back(inst_arg);
												resolved = true;
												FLASH_LOG_FORMAT(Templates, Debug,
													"Resolved deferred base alias-template argument '{}' via alias target metadata",
													type_name);
											}
										}
										if (resolved) {
											continue;
										}
										std::string_view inst_name = instantiateAndResolveBaseName(
											StringTable::getStringView(qualified_base_template_name),
											instantiated_args,
											false);
										if (inst_name.empty() &&
											qualified_base_template_name != resolved_ti->baseTemplateName()) {
											inst_name = instantiateAndResolveBaseName(
												StringTable::getStringView(resolved_ti->baseTemplateName()),
												instantiated_args,
												false);
										}
										auto inst_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(inst_name));
										if (inst_it != getTypesByNameMap().end()) {
											TemplateTypeArg inst_arg;
											inst_arg.type_index = inst_it->second->registeredTypeIndex();
											inst_arg.type_index.setCategory(inst_it->second->typeEnum());
											inst_arg.pointer_depth = type_spec.pointer_depth();
											inst_arg.ref_qualifier = type_spec.reference_qualifier();
											inst_arg.cv_qualifier = type_spec.cv_qualifier();
											resolved_args.push_back(inst_arg);
											resolved = true;
											FLASH_LOG_FORMAT(Templates, Debug, "Resolved deferred base placeholder '{}' to '{}'",
															 type_name, inst_name);
										}
									}

									// Check if this is a template class that needs to be instantiated with substituted args
									// For example: is_integral<T> where T needs to be substituted with int
									auto template_entry = gTemplateRegistry.lookupTemplate(type_name);
									if (!resolved && template_entry.has_value()) {
										// This is a template class - try to instantiate it with our template args
										// The template args for the nested template should be our current template args
										// (e.g., is_integral<T> with T=int should become is_integral_int)
										FLASH_LOG(Templates, Debug, "Nested template lookup found '", type_name,
												  "', attempting instantiation with ", template_args_to_use.size(), " args");
										auto instantiated = try_instantiate_class_template(type_name, template_args_to_use);
										if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
											if (shouldCommitTemplateInstantiationArtifacts()) {
												registerAndNormalizeLateMaterializedTopLevelNode(*instantiated);
											}
										}
										std::string_view inst_name = get_instantiated_class_name(type_name, template_args_to_use);
										auto inst_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(inst_name));
										if (inst_it != getTypesByNameMap().end()) {
											TemplateTypeArg inst_arg;
											inst_arg.type_index = inst_it->second->type_index_.withCategory(TypeCategory::Struct);
											inst_arg.pointer_depth = type_spec.pointer_depth();
											inst_arg.ref_qualifier = type_spec.reference_qualifier();
											inst_arg.cv_qualifier = type_spec.cv_qualifier();
											resolved_args.push_back(inst_arg);
											resolved = true;
											FLASH_LOG_FORMAT(Templates, Debug, "Resolved nested template '{}' to '{}'", type_name, inst_name);
										}
									}

									if (!resolved) {
										for (const auto& subst_entry : subst_map) {
											if (identifier_matches(type_name, subst_entry.first)) {
												TemplateTypeArg subst = rebindDependentTemplateTypeArg(
													subst_entry.second,
													TemplateTypeArg(type_spec));
												subst.is_pack = arg_info.is_pack;
												resolved_args.push_back(subst);
												resolved = true;
												break;
											}
										}
									}
								}
							}
						}

						// Fallback: use the type specifier as-is
						if (!resolved) {
							if (std::optional<TemplateTypeArg> substituted_arg =
									tryMaterializeDeferredBaseTypeArg(
										type_spec,
										template_params,
										template_args_to_use,
										[this](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
											return instantiate_and_register_base_template(concrete_base_template_name, concrete_base_args);
										});
								substituted_arg.has_value()) {
								substituted_arg->is_pack = arg_info.is_pack;
								resolved_args.push_back(*substituted_arg);
								resolved = true;
							}
						}
						if (!resolved) {
							resolved_args.emplace_back(type_spec);
							resolved_args.back().is_pack = arg_info.is_pack;
						}
						continue;
					}

					if (arg_info.node.is<ExpressionNode>()) {
						const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();

						// Handle TemplateParameterReferenceNode - substitute template parameter with actual type
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
							std::string_view param_name = tparam_ref.param_name().view();
							auto subst_it = subst_map.find(param_name);
							if (subst_it != subst_map.end()) {
								TemplateTypeArg subst_arg = subst_it->second;
								subst_arg.is_pack = arg_info.is_pack;
								resolved_args.push_back(subst_arg);
								FLASH_LOG_FORMAT(Templates, Debug, "Substituted template parameter '{}' with type_index {} in deferred base",
												 param_name, subst_it->second.type_index);
								continue;
							} else {
								FLASH_LOG_FORMAT(Templates, Debug, "Template parameter '{}' not found in substitution map", param_name);
								// Template parameter not found in substitution - this is an unresolved dependency
								unresolved_arg = true;
								break;
							}
						}

						// Handle IdentifierNode - NTTP params are stored as IdentifierNode by
						// parse_explicit_template_arguments; treat the same as TemplateParameterReferenceNode.
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id_node = std::get<IdentifierNode>(expr);
							std::string_view id_name = id_node.name();
							auto subst_it = subst_map.find(id_name);
							if (subst_it != subst_map.end()) {
								TemplateTypeArg subst_arg = subst_it->second;
								subst_arg.is_pack = arg_info.is_pack;
								resolved_args.push_back(subst_arg);
								FLASH_LOG_FORMAT(Templates, Debug, "Substituted identifier '{}' in deferred base via name_substitution_map", id_name);
								continue;
							}
							// Try as a concrete global type name
							StringHandle h = StringTable::getOrInternStringHandle(id_name);
							auto type_it = getTypesByNameMap().find(h);
							if (type_it != getTypesByNameMap().end()) {
								TemplateTypeArg a;
								a.type_index = type_it->second->type_index_.withCategory(type_it->second->typeEnum());
								a.is_pack = arg_info.is_pack;
								resolved_args.push_back(a);
								FLASH_LOG_FORMAT(Templates, Debug, "Resolved identifier '{}' as concrete type in deferred base", id_name);
								continue;
							}
							FLASH_LOG_FORMAT(Templates, Debug, "Identifier '{}' unresolved in deferred base – falling through", id_name);
						}

						auto makeEvaluatedDeferredBaseValueArg = [&](const auto& constant_value) {
							TypeIndex target_index = makeTypeIndexForDeferredBaseNttp(deferred_arg_index, resolved_args);
							if (isPlaceholderNttpTypeIndex(target_index)) {
								target_index = makeDeferredBaseValueTypeIndex(
									constant_value.type,
									constant_value.type_index);
							}
							TemplateTypeArg val_arg;
							val_arg.is_value = true;
							val_arg.value = constant_value.value;
							val_arg.type_index = target_index;
							val_arg.is_pack = arg_info.is_pack;
							return val_arg;
						};

						// Special handling for TypeTraitExprNode - need to substitute template parameters
						if (std::holds_alternative<TypeTraitExprNode>(expr)) {
							const TypeTraitExprNode& trait_expr = std::get<TypeTraitExprNode>(expr);
							auto substituteTraitTypeSpecifier = [&](const ASTNode& trait_type_node) {
								const TypeSpecifierNode& original_type_spec =
									trait_type_node.as<TypeSpecifierNode>();
								TypeCategory original_base_type = original_type_spec.type();
								TypeIndex original_type_index = original_type_spec.type_index();
								TypeSpecifierNode substituted_type_specifier = original_type_spec;

								if ((is_struct_type(original_base_type) ||
									 original_base_type == TypeCategory::UserDefined ||
									 original_base_type == TypeCategory::TypeAlias ||
									 original_base_type == TypeCategory::Template) &&
									original_type_index.is_valid()) {
									if (const TypeInfo* original_type_info = tryGetTypeInfo(original_type_index)) {
										std::string_view original_type_name =
											StringTable::getStringView(original_type_info->name());
										auto subst_it = subst_map.find(original_type_name);
										if (subst_it != subst_map.end()) {
											const TemplateTypeArg& subst = subst_it->second;
											substituted_type_specifier.set_type_index(
												subst.type_index.withCategory(subst.typeEnum()));
											FLASH_LOG_FORMAT(
												Templates,
												Debug,
												"Substituted type '{}' with type_index {} for type trait evaluation",
												original_type_name,
												subst.type_index);
										}
									}
								}

								return substituted_type_specifier;
							};

							// Create a substituted version of the type trait
							if (trait_expr.has_type()) {
								TypeSpecifierNode substituted_type_spec =
									substituteTraitTypeSpecifier(trait_expr.type_node());

								// Create substituted type trait node and evaluate
								ASTNode subst_type_node = emplace_node<TypeSpecifierNode>(substituted_type_spec);
								ASTNode subst_trait_node;
								if (trait_expr.has_second_type()) {
									ASTNode substituted_second_type = emplace_node<TypeSpecifierNode>(
										substituteTraitTypeSpecifier(trait_expr.second_type_node()));
									subst_trait_node = emplace_node<ExpressionNode>(
										TypeTraitExprNode(
											trait_expr.kind(),
											subst_type_node,
											substituted_second_type,
											trait_expr.trait_token()));
								} else {
									subst_trait_node = emplace_node<ExpressionNode>(
										TypeTraitExprNode(trait_expr.kind(), subst_type_node, trait_expr.trait_token()));
								}

								// Try to evaluate the type trait first. Many traits (is_enum, is_function,
								// is_pointer, etc.) only check TypeCategory and can succeed even when
								// the type's template arguments are "dependent" at a deeper level.
								if (auto value = try_evaluate_constant_expression(subst_trait_node)) {
									resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
									continue;
								}

								if (typeSpecStillUsesDependentPlaceholder(substituted_type_spec)) {
									StringHandle dependent_anchor;
									if (const TypeInfo* dependent_ti = tryGetTypeInfo(substituted_type_spec.type_index());
										dependent_ti != nullptr &&
										dependent_ti->name().isValid()) {
										dependent_anchor = dependent_ti->name();
									}
									TemplateTypeArg dependent_value =
										TemplateTypeArg::makeDependentValue(
											dependent_anchor,
											TypeCategory::Bool,
											0,
											subst_trait_node);
									dependent_value.is_pack = arg_info.is_pack;
									resolved_args.push_back(std::move(dependent_value));
									continue;
								}
							}
						} else if (std::holds_alternative<CallExprNode>(expr) && !std::get<CallExprNode>(expr).has_receiver()) {
							// Handle constexpr function calls like: call_is_nt<Result>(typename Result::__invoke_type{})
							// These need template parameter substitution before evaluation
							const CallExprNode& func_call = std::get<CallExprNode>(expr);

							FLASH_LOG(Templates, Debug, "Processing CallExprNode in deferred base argument");

							// Check if the function has template arguments that need substitution
							bool has_dependent_template_args = false;
							std::vector<TemplateTypeArg> substituted_func_template_args;

							if (func_call.has_template_arguments()) {
								for (const ASTNode& targ_node : func_call.template_arguments()) {
									if (targ_node.is<ExpressionNode>()) {
										const ExpressionNode& targ_expr = targ_node.as<ExpressionNode>();
										if (std::holds_alternative<TemplateParameterReferenceNode>(targ_expr)) {
											const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(targ_expr);
											std::string_view param_name = tparam_ref.param_name().view();
											auto subst_it = subst_map.find(param_name);
											if (subst_it != subst_map.end()) {
												substituted_func_template_args.push_back(subst_it->second);
												FLASH_LOG_FORMAT(Templates, Debug, "Substituted function template arg '{}' with type_index {}",
																 param_name, subst_it->second.type_index);
											} else {
												has_dependent_template_args = true;
											}
										} else if (std::holds_alternative<IdentifierNode>(targ_expr)) {
											const auto& id = std::get<IdentifierNode>(targ_expr);
											auto subst_it = subst_map.find(id.name());
											if (subst_it != subst_map.end()) {
												substituted_func_template_args.push_back(subst_it->second);
												FLASH_LOG_FORMAT(Templates, Debug, "Substituted function template arg identifier '{}' with type_index {}",
																 id.name(), subst_it->second.type_index);
											} else {
												has_dependent_template_args = true;
											}
										} else {
											// Keep the argument as-is for other expression types
											has_dependent_template_args = true;
										}
									} else if (targ_node.is<TypeSpecifierNode>()) {
										const TypeSpecifierNode& type_spec = targ_node.as<TypeSpecifierNode>();
										if ((type_spec.category() == TypeCategory::UserDefined || type_spec.category() == TypeCategory::TypeAlias || type_spec.category() == TypeCategory::Template) && type_spec.type_index().is_valid()) {
											if (const TypeInfo* targ_ti = tryGetTypeInfo(type_spec.type_index())) {
												std::string_view type_name = StringTable::getStringView(targ_ti->name());
												auto subst_it = subst_map.find(type_name);
												if (subst_it != subst_map.end()) {
													substituted_func_template_args.push_back(subst_it->second);
												} else {
													// Keep as-is
													substituted_func_template_args.emplace_back(type_spec);
												}
											} else {
												substituted_func_template_args.emplace_back(type_spec);
											}
										} else {
											substituted_func_template_args.emplace_back(type_spec);
										}
									}
								}
							}

							// If we successfully substituted all template arguments, try to instantiate and call the function
							if (!has_dependent_template_args && !substituted_func_template_args.empty()) {
								std::string_view func_name = func_call.called_from().value();
								FLASH_LOG_FORMAT(Templates, Debug, "Trying to instantiate constexpr function '{}' with {} template args",
												 func_name, substituted_func_template_args.size());

								// Try to instantiate the template function
								auto instantiated_func = try_instantiate_template_explicit(func_name, substituted_func_template_args);

								if (instantiated_func.has_value()) {
									FLASH_LOG_FORMAT(Templates, Debug, "try_instantiate_template_explicit returned node, is FunctionDeclarationNode: {}",
													 instantiated_func->is<FunctionDeclarationNode>());
								} else {
									FLASH_LOG(Templates, Debug, "try_instantiate_template_explicit returned nullopt");
								}

								if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
									const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();

									FLASH_LOG_FORMAT(Templates, Debug, "Instantiated function: is_constexpr={}, has_definition={}",
													 func_decl.is_constexpr(), func_decl.is_materialized());

									// Check if the function is constexpr
									if (func_decl.is_constexpr()) {
										// For constexpr functions that return a constant value, we can evaluate them
										// Look for a simple return statement with a constant value
										// This is a simplified constexpr evaluation - full constexpr requires an interpreter

										// For now, if the function body is just "return true;" or "return false;", we can evaluate it
										// This handles the common type_traits pattern
										if (func_decl.is_materialized()) {
											const ASTNode& body_node = *func_decl.get_definition();
											FLASH_LOG_FORMAT(Templates, Debug, "Function body is BlockNode: {}", body_node.is<BlockNode>());
											if (body_node.is<BlockNode>()) {
												const BlockNode& block = body_node.as<BlockNode>();
												FLASH_LOG_FORMAT(Templates, Debug, "Block has {} statements", block.get_statements().size());
												if (block.get_statements().size() == 1) {
													const ASTNode& stmt = block.get_statements()[0];
													FLASH_LOG_FORMAT(Templates, Debug, "First statement is ReturnStatementNode: {}", stmt.is<ReturnStatementNode>());
													if (stmt.is<ReturnStatementNode>()) {
														const ReturnStatementNode& ret_stmt = stmt.as<ReturnStatementNode>();
														if (ret_stmt.expression().has_value()) {
															// Try to evaluate the return expression as a constant
															if (auto ret_value = try_evaluate_constant_expression(*ret_stmt.expression())) {
																FLASH_LOG_FORMAT(Templates, Debug, "Evaluated constexpr function call to value {}", ret_value->value);
																resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*ret_value));
																continue;
															}
														}
													}
												}
											}
										}
									}
								}
							}

							// Fallback: try as variable template before giving up on constexpr evaluation.
							// For references like `is_integral_v<T>` where is_integral_v is a variable template,
							// try_instantiate_template_explicit (function-template path) returns nullopt, and the
							// generic try_evaluate_constant_expression fallback below would re-evaluate the
							// ORIGINAL node whose template args still reference the unsubstituted template
							// parameter (e.g., Ty), leading to inconsistent concrete_args during partial-spec
							// lookup. Instantiate the variable template directly with the already-substituted
							// function template args so the initializer is evaluated in a fully concrete context.
							if (!has_dependent_template_args && !substituted_func_template_args.empty()) {
								std::string_view func_name = func_call.called_from().value();
								auto var_template_node = try_instantiate_variable_template(func_name, substituted_func_template_args, nullptr);
								if (var_template_node.has_value() && var_template_node->is<VariableDeclarationNode>()) {
									const VariableDeclarationNode& var_decl = var_template_node->as<VariableDeclarationNode>();
									if (var_decl.initializer().has_value()) {
										if (auto value = try_evaluate_constant_expression(*var_decl.initializer())) {
											FLASH_LOG_FORMAT(Templates, Debug, "Evaluated variable template '{}' to value {}",
															 func_name, value->value);
											resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
											continue;
										}
									}
								}
							}

							ExpressionSubstitutor substitutor(subst_map, *this, template_param_order);
							ASTNode substituted_node = substitutor.substitute(arg_info.node);
							if (auto value = try_evaluate_constant_expression(substituted_node)) {
								FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted deferred base call to value {}", value->value);
								resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
								continue;
							}

							FLASH_LOG(Templates, Debug,
									  "Could not constexpr-evaluate substituted deferred base call argument; leaving deferred base unresolved");
						} else if (std::holds_alternative<BinaryOperatorNode>(expr) || std::holds_alternative<TernaryOperatorNode>(expr)) {
							// Handle binary/ternary operator expressions like: R1<T>::num < R2<T>::num
							// These need template parameter substitution before evaluation
							FLASH_LOG(Templates, Debug, "Processing BinaryOperatorNode/TernaryOperatorNode in deferred base argument");

							// Use ExpressionSubstitutor to substitute template parameters
							ExpressionSubstitutor substitutor(subst_map, *this, template_param_order);
							ASTNode substituted_node = substitutor.substitute(arg_info.node);

							// Now try to evaluate the substituted expression
							if (auto value = try_evaluate_constant_expression(substituted_node)) {
								FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted binary/ternary operator to value {}", value->value);
								resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
								continue;
							} else {
								FLASH_LOG(Templates, Debug, "Failed to evaluate substituted binary/ternary operator");
							}
						} else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
							// Handle unary operator expressions like: -Num<T>::num
							// These need template parameter substitution before evaluation
							FLASH_LOG(Templates, Debug, "Processing UnaryOperatorNode in deferred base argument");

							// Use ExpressionSubstitutor to substitute template parameters
							ExpressionSubstitutor substitutor(subst_map, *this, template_param_order);
							ASTNode substituted_node = substitutor.substitute(arg_info.node);

							// Now try to evaluate the substituted expression
							if (auto value = try_evaluate_constant_expression(substituted_node)) {
								FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted unary operator to value {}", value->value);
								resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
								continue;
							} else {
								FLASH_LOG(Templates, Debug, "Failed to evaluate substituted unary operator");
							}
						} else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
							FLASH_LOG(Templates, Debug, "Processing QualifiedIdentifierNode in deferred base argument");
							ExpressionSubstitutor substitutor(subst_map, *this, template_param_order);
							ASTNode substituted_node = substitutor.substitute(arg_info.node);
							if (auto value = try_evaluate_constant_expression(substituted_node)) {
								FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted qualified identifier to value {}", value->value);
								resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
								continue;
							} else {
								FLASH_LOG(Templates, Debug, "Failed to evaluate substituted qualified identifier");
							}
						} else {
							// Try to evaluate non-type template argument after substitution
							if (auto value = try_evaluate_constant_expression(arg_info.node)) {
								resolved_args.push_back(makeEvaluatedDeferredBaseValueArg(*value));
								continue;
							}
						}
					}

					// This is expected for dependent types in template metaprogramming.
					// Leave the base unresolved instead of instantiating it with wrong arguments.
					FLASH_LOG(Templates, Debug, "Could not resolve deferred template base argument for '",
							  StringTable::getStringView(deferred_base.base_template_name), "'; skipping base instantiation");
					unresolved_arg = true;
					break;
				}

				if (unresolved_arg) {
					// Cannot resolve all template arguments for the base class - skip it
					// Don't try to instantiate with wrong arguments as it will cause errors/crashes
					FLASH_LOG(Templates, Debug, "Skipping deferred base '",
							  StringTable::getStringView(deferred_base.base_template_name),
							  "' due to unresolved template arguments");
					continue;
				}

				std::string_view base_template_name = StringTable::getStringView(deferred_base.base_template_name);
				std::string_view final_base_name = base_template_name;
				if (!deferred_base.member_type_chain.empty()) {
					std::string_view outer_instantiated_name = instantiate_and_register_base_template(base_template_name, resolved_args);
					if (!outer_instantiated_name.empty()) {
						base_template_name = outer_instantiated_name;
					}

					ensure_substitution_maps();
					ExpressionSubstitutor member_template_arg_substitutor(
						subst_map,
						pack_substitution_map,
						*this,
						template_param_order);
					auto resolved_member_chain = resolveDeferredBaseMemberTypeChain(
						deferred_base.member_type_chain,
						subst_map,
						pack_substitution_map,
						[&](const TypeSpecifierNode& type_spec) {
							return tryMaterializeDeferredBaseTypeArg(
								type_spec,
								template_params,
								template_args_to_use,
								[this](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
									return instantiate_and_register_base_template(concrete_base_template_name, concrete_base_args);
								});
						},
						[&](const ASTNode& node) {
							return member_template_arg_substitutor.substitute(node);
						},
						[&](const ASTNode& node) {
							return try_evaluate_constant_expression(node);
						});
					if (!resolved_member_chain.has_value()) {
						FLASH_LOG(Templates, Debug, "Deferred template base member args not fully resolved for '",
								  StringTable::getStringView(deferred_base.base_template_name), "'");
						continue;
					}
					const TypeInfo* resolved_type =
						resolveBaseClassMemberTypeChain(base_template_name, *resolved_member_chain);
					if (resolved_type == nullptr) {
						StringBuilder unresolved_base_builder;
						unresolved_base_builder.append(base_template_name);
						for (const QualifiedTypeMemberAccess& member_access : *resolved_member_chain) {
							unresolved_base_builder.append("::");
							unresolved_base_builder.append(StringTable::getStringView(member_access.member_name));
							if (member_access.has_template_arguments) {
								unresolved_base_builder.append("<...>");
							}
						}
						std::string_view unresolved_base_name = unresolved_base_builder.commit();
						FLASH_LOG(Templates, Debug, "Deferred template base alias not found: ",
								  unresolved_base_name,
								  " (this may be expected for SFINAE/dependent template arguments)");
						continue;
					}

					resolved_type = materializeDeferredBasePlaceholderIfNeeded(
						resolved_type,
						template_params,
						template_args_to_use,
						[this](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
							std::string_view mutable_template_name = concrete_base_template_name;
							return instantiate_and_register_base_template(mutable_template_name, concrete_base_args);
						});

					final_base_name = StringTable::getStringView(resolved_type->name());
					struct_info->addBaseClass(
						final_base_name,
						resolved_type->type_index_,
						deferred_base.access,
						deferred_base.is_virtual);
					continue;
				}

				AliasTemplateMaterializationResult materialized_base =
					materializeTemplateInstantiationForLookup(base_template_name, resolved_args);
				if (materialized_base.resolved_type_info != nullptr) {
					final_base_name = StringTable::getStringView(materialized_base.resolved_type_info->name());
				} else if (!materialized_base.instantiated_name.empty()) {
					final_base_name = materialized_base.instantiated_name;
				}

				auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(final_base_name));
				if (base_type_it != getTypesByNameMap().end()) {
					struct_info->addBaseClass(final_base_name, base_type_it->second->type_index_, deferred_base.access, deferred_base.is_virtual);
				} else {
					FLASH_LOG(Templates, Warning, "Deferred template base type not found: ", final_base_name);
				}
			}
		}
	}

	// Handle deferred base classes (decltype bases) from the primary template
	// These need to be evaluated with concrete template arguments
	FLASH_LOG(Templates, Debug, "Primary template has ", class_decl.deferred_base_classes().size(), " deferred base classes");
	for (const auto& deferred_base : class_decl.deferred_base_classes()) {
		FLASH_LOG(Templates, Debug, "Processing deferred decltype base class");
		DeferredBaseReplayContextScope deferred_base_replay_scope(
			*this,
			deferred_base,
			class_decl.name(),
			template_params,
			template_args_to_use);

		auto try_add_deferred_base_from_type = [&](const TypeInfo* candidate_base_type) -> bool {
			if (candidate_base_type == nullptr) {
				return false;
			}

			candidate_base_type = materializeDeferredBasePlaceholderIfNeeded(
				candidate_base_type,
				template_params,
				template_args_to_use,
				[this](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
					std::string_view mutable_template_name = concrete_base_template_name;
					return instantiate_and_register_base_template(mutable_template_name, concrete_base_args);
				});

			if (candidate_base_type == nullptr || candidate_base_type->typeEnum() != TypeCategory::Struct) {
				return false;
			}

			std::string_view base_class_name = StringTable::getStringView(candidate_base_type->name());
			struct_info->addBaseClass(
				base_class_name,
				candidate_base_type->type_index_,
				deferred_base.access,
				deferred_base.is_virtual);
			FLASH_LOG(Templates, Debug, "Added deferred base class: ", base_class_name,
					  " with type_index=", candidate_base_type->type_index_);
			return true;
		};

		// The deferred base contains an expression that needs to be evaluated
		// with concrete template arguments to determine the actual base class
		if (!deferred_base.decltype_expression.is<TypeSpecifierNode>()) {
			// Build maps from template parameter NAME to concrete type for substitution
			// Note: We can't use type_index because template parameters are cleaned up after parsing
			ensure_substitution_maps();

			// Use ExpressionSubstitutor to perform template parameter substitution
			FLASH_LOG(Templates, Debug, "Using ExpressionSubstitutor to substitute template parameters in decltype expression");
			ExpressionSubstitutor substitutor(name_substitution_map, pack_substitution_map, *this, template_param_order);
			ASTNode substituted_expr = substitutor.substitute(deferred_base.decltype_expression);

			auto type_spec_opt = get_expression_type(substituted_expr);
			if (type_spec_opt.has_value()) {
				const TypeSpecifierNode& base_type_spec = *type_spec_opt;

				// Get the type information from the evaluated expression
				TypeCategory base_type = base_type_spec.type();
				TypeIndex base_type_index = base_type_spec.type_index();

				// Look up the base class type by its type index
				if (base_type == TypeCategory::Struct) {
					if (const TypeInfo* base_type_info = tryGetTypeInfo(base_type_index)) {
						if (!try_add_deferred_base_from_type(base_type_info)) {
							FLASH_LOG(Templates, Warning, "Deferred base class type did not materialize to a concrete struct: ",
									  StringTable::getStringView(base_type_info->name()));
						}
					} else {
						FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
						FLASH_LOG(Templates, Warning, "This likely means template parameter substitution in decltype expressions is needed");
						FLASH_LOG(Templates, Warning, "For decltype(base_trait<T>()), we need to instantiate base_trait with concrete type");
					}
				} else {
					FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
					FLASH_LOG(Templates, Warning, "This likely means template parameter substitution in decltype expressions is needed");
					FLASH_LOG(Templates, Warning, "For decltype(base_trait<T>()), we need to instantiate base_trait with concrete type");
				}
			} else {
				FLASH_LOG(Templates, Warning, "Could not evaluate deferred decltype base class expression");
			}
		} else if (deferred_base.decltype_expression.is<TypeSpecifierNode>()) {
			// Legacy path - if it's already a TypeSpecifierNode
			const TypeSpecifierNode& base_type_spec = deferred_base.decltype_expression.as<TypeSpecifierNode>();

			// Get the type information from the decltype expression
			TypeCategory base_type = base_type_spec.type();
			TypeIndex base_type_index = base_type_spec.type_index();

			// Look up the base class type by its type index
			if (base_type == TypeCategory::Struct) {
				if (const TypeInfo* base_type_info = tryGetTypeInfo(base_type_index)) {
					if (!try_add_deferred_base_from_type(base_type_info)) {
						FLASH_LOG(Templates, Warning, "Deferred legacy base type did not materialize to a concrete struct: ",
								  StringTable::getStringView(base_type_info->name()));
					}
				} else {
					FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
				}
			} else {
				FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
			}
		} else {
			FLASH_LOG(Templates, Warning, "Deferred base class expression is neither ExpressionNode nor TypeSpecifierNode");
		}
	}

	// Copy members from the template, substituting template parameters with concrete types
	for (const auto& member_decl : class_decl.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_specifier_node();

		// Substitute template parameter if the member type is a template parameter
		TypeIndex member_type_index = substitute_template_parameter(
			type_spec, template_params, template_args_to_use);

		// WORKAROUND: If member type is a Struct or UserDefined that is actually a template (not an instantiation),
		// try to instantiate it with the current template arguments.
		// This handles cases like:
		//   template<typename T> struct TC { T val; };
		//   template<typename T> struct TD { TC<T> c; };
		// where TC<T> is stored as a dependent placeholder with Type::UserDefined.
		// We need to instantiate TC with the concrete args when instantiating TD.
		if (const TypeInfo* member_type_info = (is_struct_type(member_type_index.category())) ? tryGetTypeInfo(member_type_index) : nullptr) {
			std::string_view member_struct_name = StringTable::getStringView(member_type_info->name());
			auto materializeMemberTemplateArgs = [&]() {
				return materializeTemplateArgs(*member_type_info, template_params, template_args_to_use);
			};

			FLASH_LOG(Templates, Debug, "Member type_info: name='", member_struct_name,
					  "', isTemplateInstantiation=", member_type_info->isTemplateInstantiation(),
					  ", hasStructInfo=", (member_type_info->getStructInfo() != nullptr),
					  ", total_size=", member_type_info->getStructInfo() ? toSizeT(member_type_info->getStructInfo()->total_size) : 0);

			// Phase 6: Use TypeInfo::isTemplateInstantiation() instead of parsing $
			// Check if this is a template instantiation that needs instantiation
			// A template needs instantiation if it's a placeholder (no struct_info or total_size == 0)
			bool needs_instantiation = false;
			if (member_type_info->isTemplateInstantiation()) {
				// This is a template instantiation - check if it's already fully instantiated
				// Need to instantiate if: no struct_info OR struct_info exists but size is 0
				if (!member_type_info->getStructInfo() || !member_type_info->getStructInfo()->sizeInBytes().is_set()) {
					// Not yet instantiated - get the base template name and instantiate
					member_struct_name = StringTable::getStringView(member_type_info->baseTemplateName());
					needs_instantiation = true;
					FLASH_LOG(Templates, Debug, "Member needs instantiation (placeholder with size=0 or no struct_info): base_name='", member_struct_name, "'");
				} else {
					FLASH_LOG(Templates, Debug, "Member already instantiated: ", member_struct_name, ", size=",
							  member_type_info->getStructInfo() ? toSizeT(member_type_info->getStructInfo()->total_size) : 0);
				}
			} else {
				FLASH_LOG(Templates, Debug, "Member already instantiated (non-template struct): ", member_struct_name);
			}

			if (needs_instantiation) {
				// Try to instantiate with the current template arguments
				std::vector<TemplateTypeArg> member_template_args = materializeMemberTemplateArgs();
				FLASH_LOG(Templates, Debug, "Instantiating member template: ", member_struct_name, " with ", member_template_args.size(), " args");
				try_instantiate_class_template(member_struct_name, member_template_args);

				// If instantiation succeeded, look up the instantiated type
				std::string_view inst_name_view = get_instantiated_class_name(member_struct_name, member_template_args);
				std::string inst_name(inst_name_view);
				const std::string_view original_member_name = StringTable::getStringView(member_type_info->name());
				bool resolved_member_placeholder = false;
				if (size_t member_sep = original_member_name.rfind("::"); member_sep != std::string_view::npos) {
					StringHandle qualified_member_handle = StringTable::getOrInternStringHandle(
						StringBuilder()
							.append(inst_name_view)
							.append("::")
							.append(original_member_name.substr(member_sep + 2))
							.commit());
					auto qualified_member_it = getTypesByNameMap().find(qualified_member_handle);
					if (qualified_member_it != getTypesByNameMap().end()) {
						member_type_index = qualified_member_it->second->registeredTypeIndex();
						member_type_index.setCategory(qualified_member_it->second->typeEnum());
						resolved_member_placeholder = true;
					}
				}
				if (!resolved_member_placeholder) {
					auto inst_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(inst_name));
					if (inst_type_it != getTypesByNameMap().end()) {
					// Update member_type_index to point to the instantiated type
						member_type_index = inst_type_it->second->registeredTypeIndex();
						// Update member_type to match the instantiated type's actual type
						// This ensures codegen knows it's a struct type (fixes Type::UserDefined issue)
						member_type_index.setCategory(inst_type_it->second->typeEnum());
					}
				}
			}
		}

		// After template refactoring, instantiated templates may have Type::UserDefined
		// but gTypeInfo correctly stores them as Type::Struct. Synchronize member_type.
		if (member_type_index.is_valid()) {
			if (const TypeInfo* member_fix_info = tryGetTypeInfo(member_type_index)) {
				if (member_fix_info->getStructInfo() && member_type_index.category() == TypeCategory::UserDefined) {
					// Fix Type::UserDefined to Type::Struct for instantiated templates
					member_type_index.setCategory(member_fix_info->typeEnum());
				}
			}
		}

		// Handle array size substitution for non-type template parameters
		std::optional<ASTNode> substituted_array_size;
		if (decl.is_array()) {
			if (decl.array_size().has_value()) {
				ASTNode array_size_node = *decl.array_size();

				// The array size might be stored directly or wrapped in different node types
				// Try to extract the identifier or value from various possible representations
				std::optional<std::string_view> identifier_name;
				std::optional<int64_t> literal_value;

				// Check if it's an ExpressionNode
				if (array_size_node.is<ExpressionNode>()) {
					const ExpressionNode& expr = array_size_node.as<ExpressionNode>();
					if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&expr)) {
						const IdentifierNode& ident = *identifier_ptr;
						identifier_name = ident.name();
					} else if (const auto* template_parameter_reference = std::get_if<TemplateParameterReferenceNode>(&expr)) {
						const TemplateParameterReferenceNode& tparam_ref = *template_parameter_reference;
						identifier_name = tparam_ref.param_name().view();
					} else if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&expr)) {
						const NumericLiteralNode& lit = *numeric_literal;
						const auto& val = lit.value();
						if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
							literal_value = static_cast<int64_t>(*ull_val);
						}
					}
				}
				// Check if it's a direct IdentifierNode (shouldn't happen, but be safe)
				else if (array_size_node.is<IdentifierNode>()) {
					const IdentifierNode& ident = array_size_node.as<IdentifierNode>();
					identifier_name = ident.name();
				}

				// If we found an identifier, try to substitute it with a non-type template parameter value
				if (identifier_name.has_value()) {
					// Try to find which non-type template parameter this is
					for (size_t i = 0; i < template_params.size(); ++i) {
						const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
						if (tparam != nullptr &&
							tparam->kind() == TemplateParameterKind::NonType &&
							tparam->name() == *identifier_name) {
							// Found the non-type parameter - substitute with the actual value
							if (i < template_args_to_use.size() && template_args_to_use[i].is_value) {
								// Create a numeric literal node with the substituted value
								int64_t val = template_args_to_use[i].value;
								Token num_token(Token::Type::Literal, StringBuilder().append(val).commit(), 0, 0, 0);
								auto num_literal = emplace_node<ExpressionNode>(
									NumericLiteralNode(num_token, static_cast<unsigned long long>(val), TypeCategory::Int, TypeQualifier::None, 32));
								substituted_array_size = num_literal;
								break;
							}
						}
					}
				}
			} else {
				FLASH_LOG(Templates, Error, "Array does NOT have array_size!");
			}

			// If we didn't substitute, keep the original array size
			if (!substituted_array_size.has_value()) {
				substituted_array_size = decl.array_size();
			}
		}

		// Create the substituted type specifier
		// IMPORTANT: Preserve the base CV qualifier from the original type!
		// For example: const T* should become const int* when T=int
		auto substituted_type = emplace_node<TypeSpecifierNode>(
			member_type_index,
			get_type_size_bits(member_type_index.category()),
			Token(),
			type_spec.cv_qualifier(), // Preserve const/volatile qualifier
			ReferenceQualifier::None);

		// Copy pointer levels from the original type specifier
		auto& substituted_type_spec = substituted_type.as<TypeSpecifierNode>();
		const TemplateTypeArg* resolved_member_arg =
			findResolvedTypeTemplateArg(type_spec, template_params, template_args_to_use);
		for (const auto& ptr_level : type_spec.pointer_levels()) {
			substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
		}

		// Preserve reference qualifiers from the original type
		substituted_type_spec.set_reference_qualifier(type_spec.reference_qualifier());
		applyResolvedTemplateArgTypeMetadata(substituted_type_spec, resolved_member_arg);

		// Add to the instantiated struct
		// new_struct_ref.add_member(new_member_decl, member_decl.access, member_decl.default_initializer);

		// Calculate member size - for arrays, multiply element size by array size
		ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(member_type_index);
		std::vector<size_t> resolved_array_dimensions = resolve_array_dimensions(decl, template_params, template_args_to_use);
		if (resolved_array_dimensions.empty()) {
			resolved_array_dimensions = resolved_member_alias.array_dimensions;
		}
		bool is_array_member = !resolved_array_dimensions.empty();
		TypeIndex member_size_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : member_type_index;
		TypeIndex stored_member_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : member_type_index;
		size_t member_size;
		if (is_array_member) {
			// Compute per-element size, looking up struct sizes from gTypeInfo
			member_size = calculateResolvedMemberSizeAndAlignment(substituted_type_spec, member_size_type_index).size;
			for (size_t dim_size : resolved_array_dimensions) {
				member_size *= dim_size;
			}
		} else if (substituted_array_size.has_value()) {
			throw InternalError("Array dimensions should be resolved before layout substitution");
		} else {
			member_size = calculateResolvedMemberSizeAndAlignment(substituted_type_spec, member_size_type_index).size;
			if (!substituted_type_spec.is_pointer() && !substituted_type_spec.is_reference() && !substituted_type_spec.is_rvalue_reference() && member_type_index.category() == TypeCategory::Struct) {
				if (const TypeInfo* member_type_info = tryGetTypeInfo(member_type_index);
					member_type_info && member_type_info->getStructInfo()) {
					FLASH_LOG_FORMAT(Templates, Debug, "Primary template: Found struct member '{}' with type_index={}, total_size={} bytes, struct name={}",
									 decl.identifier_token().value(), member_type_index, member_size,
									 StringTable::getStringView(member_type_info->name()));
				} else {
					FLASH_LOG_FORMAT(Templates, Debug, "Primary template: Struct member '{}' type_index={} not found in gTypeInfo, using default size={} bytes",
									 decl.identifier_token().value(), member_type_index, member_size);
				}
			}
		}
		// Calculate member alignment
		// For pointers and references, use 8-byte alignment (pointer alignment on x64)
		size_t member_alignment = get_type_alignment(member_size_type_index.category(), member_size);
		if (substituted_type_spec.is_pointer() || substituted_type_spec.is_reference() || substituted_type_spec.is_rvalue_reference()) {
			member_alignment = 8; // Pointer/reference alignment on x64
		} else if (const StructTypeInfo* member_struct_info = tryGetStructTypeInfo(member_size_type_index)) {
			member_alignment = member_struct_info->alignment;
		}
		ReferenceQualifier ref_qual = substituted_type_spec.reference_qualifier();

		// For reference members, we need to pass the size of the referenced type, not the pointer size
		size_t referenced_size_bits = 0;
		if (ref_qual != ReferenceQualifier::None) {
			referenced_size_bits = get_type_size_bits(member_type_index.category());
		}

		// Substitute template parameters in default member initializers
		std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
			member_decl.default_initializer, template_args_to_use, template_params);

		// For function pointer members instantiated from a template parameter (e.g., F func
		// where F=int(*)(int)), the pattern TypeSpecifierNode won't have a function_signature
		// — it only carries the placeholder.  Retrieve it from the matching TemplateTypeArg.
		// Phase 7B: Intern member name and use StringHandle overload
		StringHandle member_name_handle = decl.identifier_token().handle();
		struct_info->addMember(
			member_name_handle,
			stored_member_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			substituted_default_initializer,
			ref_qual,
			referenced_size_bits,
			is_array_member,
			std::move(resolved_array_dimensions),
			static_cast<int>(substituted_type_spec.pointer_depth()),
			resolve_bitfield_width(member_decl, template_params, template_args_to_use),
			resolveTemplateFunctionPointerSignature(substituted_type_spec, member_type_index, template_params, template_args_to_use),
			member_decl.is_no_unique_address);
	}

	// Skip member function instantiation - we only need type information for nested classes
	// Member functions will be instantiated on-demand when called

	// Copy static members from the primary template with template parameter substitution
	// This handles cases like:
	//   template<bool... Bs> struct __and_ { static constexpr bool value = (Bs && ...); };
	// where the fold expression needs to be evaluated with the actual template arguments
	//
	// Note: Static members can be in two places:
	// 1. class_decl.static_members() - AST node storage
	// 2. StructTypeInfo for the template - type system storage
	// We need to check both.

	// First, try to get static members from the template's StructTypeInfo
	auto template_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(template_name));
	if (template_type_it == getTypesByNameMap().end()) {
		std::string_view template_name_view = template_name;
		size_t last_colon = template_name_view.rfind("::");
		if (last_colon != std::string_view::npos) {
			template_type_it = getTypesByNameMap().find(
				StringTable::getOrInternStringHandle(template_name_view.substr(last_colon + 2)));
		}
	}
	const StructTypeInfo* template_struct_info = nullptr;
	if (template_type_it != getTypesByNameMap().end() && template_type_it->second->getStructInfo()) {
		template_struct_info = template_type_it->second->getStructInfo();
		for (TypeIndex nested_enum_index : template_struct_info->getNestedEnumIndices()) {
			struct_info->addNestedEnumIndex(nested_enum_index);
		}
	}

	auto push_replay_member_context =
		[this](StringHandle instantiated_class_name, StructTypeInfo* instantiated_struct_info) {
		TypeIndex struct_type_index{};
		if (auto type_it = getTypesByNameMap().find(instantiated_class_name);
			type_it != getTypesByNameMap().end()) {
			struct_type_index = type_it->second->type_index_;
		}
		member_function_context_stack_.push_back(
			{instantiated_class_name, struct_type_index, nullptr, instantiated_struct_info});
		return ScopeGuard([this]() {
			if (!member_function_context_stack_.empty()) {
				member_function_context_stack_.pop_back();
			}
		});
	};

	auto try_reparse_in_class_static_initializer =
		[&](
			const auto& static_member,
			const auto& template_params,
			std::span<const TemplateTypeArg> template_args,
			StringHandle owning_instantiated_name,
			NamespaceHandle fallback_definition_namespace,
			bool requires_replay_metadata) -> std::optional<ASTNode> {
		if (!static_member.initializer_position.has_value() ||
			!static_member.declaration.has_value()) {
			if (requires_replay_metadata) {
				throw InternalError(
					"Template static member initializer replay metadata missing for dependent initializer");
			}
			return std::nullopt;
		}

		const DeclarationNode* declaration =
			get_decl_from_symbol(*static_member.declaration);
		if (declaration == nullptr) {
			throw InternalError(
				"Template static member initializer replay declaration is missing or invalid");
		}

		SaveHandle current_pos = save_token_position();
		ScopedLexerPositionRestore lexer_restore(*this, current_pos);

		TemplateInstantiationContext substitution_context =
			buildTemplateInstantiationContext(
				template_params,
				template_args,
				nullptr,
				currentTemplateSubstitutionFailurePolicy());
		substitution_context.current_instantiation_name =
			owning_instantiated_name;

		TemplateDefinitionLookupContext definition_lookup_context =
			ensureReplayDefinitionLookupContext(
				static_member.initializerDefinitionLookupContext(),
				declaration->identifier_token(),
				fallback_definition_namespace,
				owning_instantiated_name);
		substitution_context.definition_lookup_context =
			definition_lookup_context.is_valid()
				? &definition_lookup_context
				: nullptr;

		FlashCpp::TemplateDepthGuard guard_template_depth(parsing_template_depth_);
		// Replay parsing should preserve template-context token classification even when
		// instantiation-time callers are outside template parsing (depth 0).
		parsing_template_depth_ = 1;
		ScopedDefinitionLookupContext ctx_scope(
			current_template_definition_lookup_context_,
			substitution_context.definition_lookup_context);

		InlineVector<StringHandle, 4> template_param_names;
		InlineVector<TemplateParameterKind, 4> template_param_kinds;
		InlineVector<TypeCategory, 4> non_type_categories;
		buildTemplateParameterReplayState(
			template_params,
			template_param_names,
			template_param_kinds,
			non_type_categories);
		FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
		setCurrentTemplateParameters(
			template_param_names,
			template_param_kinds,
			non_type_categories);

		auto member_ctx_scope =
			push_replay_member_context(owning_instantiated_name, struct_info.get());

		restore_lexer_position_only(*static_member.initializer_position);

		ASTNode declaration_copy = *static_member.declaration;
		DeclarationNode* declaration_copy_ptr =
			get_decl_from_symbol_mut(declaration_copy);
		if (declaration_copy_ptr == nullptr) {
			throw InternalError(
				"Template static member initializer replay failed to materialize mutable declaration");
		}
		DeclarationNode& declaration_copy_ref = *declaration_copy_ptr;
		TypeSpecifierNode& type_spec = declaration_copy_ref.type_specifier_node();

		std::optional<ASTNode> reparsed_initializer;
		if (peek() == "="_tok) {
			reparsed_initializer = parse_copy_initialization(
				declaration_copy_ref,
				type_spec);
		} else if (peek() == "{"_tok) {
			ParseResult init_result = parse_brace_initializer(type_spec);
			if (!init_result.is_error() && init_result.node().has_value()) {
				reparsed_initializer = *init_result.node();
			}
		} else {
			return std::nullopt;
		}

		if (!reparsed_initializer.has_value()) {
			return std::nullopt;
		}

		return substituteTemplateParameters(
			*reparsed_initializer,
			substitution_context);
	};

	auto substitute_in_class_static_initializer_replay_first =
		[&](
			const auto& static_member,
			const auto& template_params,
			std::span<const TemplateTypeArg> template_args,
			StringHandle owning_instantiated_name,
			NamespaceHandle fallback_definition_namespace,
			const auto& fallback_template_args) -> std::optional<ASTNode> {
		if (!static_member.initializer.has_value()) {
			return std::nullopt;
		}

		const bool requires_replay_metadata =
			static_initializer_requires_replay_metadata(
				static_member.initializer,
				std::span<const TemplateParameterNode>(
					template_params.data(),
					template_params.size()));
		const bool has_replay_metadata =
			static_member.initializer_position.has_value() &&
			static_member.declaration.has_value();
		std::optional<ASTNode> substituted_initializer;
		try {
			substituted_initializer =
				try_reparse_in_class_static_initializer(
					static_member,
					template_params,
					template_args,
					owning_instantiated_name,
					fallback_definition_namespace,
					requires_replay_metadata);
		} catch (const std::exception& ex) {
			FLASH_LOG(Templates, Error,
					  "Replay substitution failed for in-class static member: ",
					  ex.what());
			if (has_replay_metadata || requires_replay_metadata) {
				throw;
			}
		}

		if (!substituted_initializer.has_value()) {
			if (requires_replay_metadata) {
				throw InternalError(
					"Dependent template static member initializer requires replay metadata");
			}
			substituted_initializer = substituteTemplateParameters(
				*static_member.initializer,
				template_params,
				fallback_template_args,
				owning_instantiated_name);
		}

		return substituted_initializer;
	};

	// Process static members from StructTypeInfo (preferred source)
	if (template_struct_info && !template_struct_info->static_members.empty()) {
		FLASH_LOG(Templates, Debug, "Processing ", template_struct_info->static_members.size(), " static members from primary template StructTypeInfo");

		// Helper to check if an initializer needs complex substitution
		// Returns true for fold expressions, sizeof...(pack), template parameter references, etc.
		auto needs_complex_substitution = [](const std::optional<ASTNode>& initializer) -> bool {
			if (!initializer.has_value() || !initializer->is<ExpressionNode>()) {
				return false;
			}
			const ExpressionNode& expr = initializer->as<ExpressionNode>();

			// Check for expression types that require template parameter substitution
			if (std::holds_alternative<FoldExpressionNode>(expr))
				return true;
			if (std::holds_alternative<SizeofPackNode>(expr))
				return true;
			if (std::holds_alternative<TemplateParameterReferenceNode>(expr))
				return true;

			// Check for static_cast wrapping sizeof...
			if (const auto* static_cast_node = std::get_if<StaticCastNode>(&expr)) {
				const StaticCastNode& cast_node = *static_cast_node;
				if (cast_node.expr().is<ExpressionNode>()) {
					const ExpressionNode& inner = cast_node.expr().as<ExpressionNode>();
					if (std::holds_alternative<SizeofPackNode>(inner))
						return true;
				}
			}

			// Check for BinaryOperatorNode that might contain sizeof...
			if (std::holds_alternative<BinaryOperatorNode>(expr))
				return true;

			// Check for TernaryOperatorNode (condition might be template param)
			if (std::holds_alternative<TernaryOperatorNode>(expr))
				return true;

			// Check for IdentifierNode (might be a template parameter)
			// Note: We'd need context to determine this, so be conservative
			if (std::holds_alternative<IdentifierNode>(expr))
				return true;
			// Qualified ids like T::value can also be template-parameter dependent.
			if (std::holds_alternative<QualifiedIdentifierNode>(expr))
				return true;
			if (RebindStaticMemberAst::visitASTUntil(*initializer, [](const ASTNode& node) {
					if (!node.is<CallExprNode>()) {
						return false;
					}
					const CallExprNode& call = node.as<CallExprNode>();
					return call.has_dependent_qualified_lookup_record() ||
						   call.dependent_unqualified_lookup_record().has_value();
				})) {
				return true;
			}

			return false;
		};

		auto find_ast_static_member_decl = [&](StringHandle member_name) -> const StaticMemberDecl* {
			for (const auto& candidate : class_decl.static_members()) {
				if (candidate.name == member_name) {
					return &candidate;
				}
			}
			return nullptr;
		};

		auto ensure_instantiated_static_member_type = [&](TypeIndex& member_type_index) {
			if (const TypeInfo* member_type_info = (is_struct_type(member_type_index.category())) ? tryGetTypeInfo(member_type_index) : nullptr) {
				std::string_view member_struct_name = StringTable::getStringView(member_type_info->name());
				bool needs_instantiation = false;
				if (member_type_info->isTemplateInstantiation()) {
					if (!member_type_info->getStructInfo() || !member_type_info->getStructInfo()->sizeInBytes().is_set()) {
						member_struct_name = StringTable::getStringView(member_type_info->baseTemplateName());
						needs_instantiation = true;
					}
				}

				if (needs_instantiation) {
					auto inst_result = try_instantiate_class_template(member_struct_name, template_args_to_use);
					(void)inst_result;

					std::string_view inst_name_view = get_instantiated_class_name(member_struct_name, template_args_to_use);
					auto inst_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(inst_name_view));
					if (inst_type_it != getTypesByNameMap().end()) {
						member_type_index = inst_type_it->second->type_index_;
						member_type_index.setCategory(inst_type_it->second->typeEnum());
					}
				}
			}

			if (member_type_index.is_valid()) {
				if (const TypeInfo* member_fix_info = tryGetTypeInfo(member_type_index)) {
					if (member_fix_info->getStructInfo() && member_type_index.category() == TypeCategory::UserDefined) {
						member_type_index.setCategory(member_fix_info->typeEnum());
					}
				}
			}
		};

		auto compute_substituted_static_member_layout =
			[&](const StructStaticMember& static_member) -> std::tuple<TypeIndex, size_t, size_t, bool, std::vector<size_t>> {
			TypeIndex substituted_type_index = static_member.type_index;
			size_t substituted_size = static_member.size;
			size_t substituted_alignment = static_member.alignment;
			bool is_array_member = static_member.is_array;
			std::vector<size_t> resolved_array_dimensions = static_member.array_dimensions;

			if (const StaticMemberDecl* ast_static_member = find_ast_static_member_decl(static_member.getName());
				ast_static_member && ast_static_member->declaration.has_value() &&
				ast_static_member->declaration->is<DeclarationNode>()) {
				const DeclarationNode& decl = ast_static_member->declaration->as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_specifier_node();
				substituted_type_index = substitute_template_parameter(
					type_spec, effective_template_params, effective_template_args);
				ensure_instantiated_static_member_type(substituted_type_index);
				ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(substituted_type_index);
				resolved_array_dimensions = resolve_array_dimensions(
					decl, effective_template_params, effective_template_args);
				if (resolved_array_dimensions.empty()) {
					resolved_array_dimensions = resolved_member_alias.array_dimensions;
				}
				is_array_member = !resolved_array_dimensions.empty();

				TypeSpecifierNode substituted_type_spec(
					substituted_type_index.category(),
					type_spec.qualifier(),
					get_type_size_bits(substituted_type_index.category()),
					Token{},
					type_spec.cv_qualifier());
				substituted_type_spec.set_type_index(substituted_type_index);
				for (const auto& ptr_level : type_spec.pointer_levels()) {
					substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
				}

				if (is_array_member) {
					size_t total_elements = 1;
					for (size_t dim_size : resolved_array_dimensions) {
						total_elements *= dim_size;
					}
					size_t element_size = calculateResolvedMemberSizeAndAlignment(
						substituted_type_spec, substituted_type_index).size;
					substituted_size = element_size * total_elements;
				} else {
					substituted_size = calculateResolvedMemberSizeAndAlignment(
						substituted_type_spec, substituted_type_index).size;
				}

				substituted_alignment = get_type_alignment(substituted_type_index.category(), substituted_size);
				if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
					substituted_alignment = sizeof(void*);
				} else if (const StructTypeInfo* member_struct_info = tryGetStructTypeInfo(substituted_type_index)) {
					substituted_alignment = member_struct_info->alignment;
				}
			} else {
				TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
				original_type_spec.set_type_index(static_member.type_index);
				substituted_type_index = substitute_template_parameter(
					original_type_spec, effective_template_params, effective_template_args);
				ensure_instantiated_static_member_type(substituted_type_index);
				substituted_size = get_substituted_type_size_bytes(substituted_type_index);
				if (is_array_member) {
					for (size_t dim_size : resolved_array_dimensions) {
						substituted_size *= dim_size;
					}
				}
			}

			return {substituted_type_index, substituted_size, substituted_alignment, is_array_member, std::move(resolved_array_dimensions)};
		};

		for (const auto& static_member : template_struct_info->static_members) {
			FLASH_LOG(Templates, Debug, "Copying static member: ", StringTable::getStringView(static_member.getName()));

			// Check if this static member should be lazily instantiated
			bool member_needs_complex_substitution = needs_complex_substitution(static_member.initializer);
			bool member_refs_current_template_identifier = false;
			if (static_member.initializer.has_value()) {
				member_refs_current_template_identifier = RebindStaticMemberAst::visitASTUntil(
					*static_member.initializer,
					[&](const ASTNode& node) {
						return node.is<IdentifierNode>() &&
							node.as<IdentifierNode>().name() == template_name;
					});
			}
			bool member_is_constexpr = static_member.is_constexpr;
			if (!member_is_constexpr &&
				static_member.declaration.has_value() &&
				static_member.declaration->is<VariableDeclarationNode>()) {
				member_is_constexpr =
					static_member.declaration->as<VariableDeclarationNode>().is_constexpr();
			}

			if (is_implicit_instantiation &&
				member_needs_complex_substitution &&
				!member_refs_current_template_identifier &&
				!member_is_constexpr) {
				// Register for lazy instantiation instead of processing now
				FLASH_LOG(Templates, Debug, "Registering static member '", static_member.getName(),
						  "' for lazy instantiation");

				LazyStaticMemberInfo lazy_info;
				lazy_info.class_template_name = StringTable::getOrInternStringHandle(template_name);
				lazy_info.instantiated_class_name = instantiated_name;
				lazy_info.member_name = static_member.getName();
				lazy_info.type_index = static_member.type_index;
				lazy_info.size = static_member.size;
				lazy_info.alignment = static_member.alignment;
				lazy_info.access = static_member.access;
				lazy_info.declaration = static_member.declaration;
				lazy_info.initializer = static_member.initializer;
				lazy_info.initializer_position = static_member.initializer_position;
				if (static_member.initializerDefinitionLookupContext().is_valid()) {
					lazy_info.definition_lookup_context =
						static_member.initializerDefinitionLookupContext();
				} else if (current_template_definition_lookup_context_ != nullptr &&
					current_template_definition_lookup_context_->is_valid()) {
					lazy_info.definition_lookup_context =
						*current_template_definition_lookup_context_;
				} else if (static_member.declaration.has_value() &&
						   static_member.declaration->is<DeclarationNode>()) {
					const Token& declaration_token =
						static_member.declaration->as<DeclarationNode>().identifier_token();
					lazy_info.definition_lookup_context.setDefinitionToken(
						declaration_token);
					lazy_info.definition_lookup_context.definition_namespace =
						struct_info->getNamespaceHandle();
				}
				lazy_info.definition_lookup_context.current_instantiation_name =
					instantiated_name;
				lazy_info.cv_qualifier = static_member.cv_qualifier;
				lazy_info.reference_qualifier = static_member.reference_qualifier;
				lazy_info.is_array = static_member.is_array;
				lazy_info.array_dimensions = static_member.array_dimensions;
				lazy_info.pointer_depth = static_member.pointer_depth;
				lazy_info.is_constexpr = static_member.is_constexpr;
				lazy_info.template_params = effective_template_params;
				lazy_info.template_args.assign(
					effective_template_args.begin(),
					effective_template_args.end());
				lazy_info.outer_template_environment_snapshot = buildTemplateEnvironmentSnapshotFromBindings(
					effective_template_params,
					effective_template_args,
					nullptr);
				lazy_info.needs_substitution = true;

				LazyStaticMemberRegistry::getInstance().registerLazyStaticMember(lazy_info);

				// Still add the member to struct_info for name lookup, but without initializer
				// Type substitution is still done eagerly (for sizeof, alignof, etc.)
				auto [substituted_type_index, substituted_size, substituted_alignment, is_array_member, resolved_array_dimensions] =
					compute_substituted_static_member_layout(static_member);

				// Add with nullopt initializer - will be filled in during lazy instantiation
				struct_info->addStaticMember(
					static_member.getName(),
					substituted_type_index,
					substituted_size,
					substituted_alignment,
					static_member.access,
					std::nullopt, // Initializer will be computed lazily
					static_member.cv_qualifier,
					static_member.reference_qualifier,
					static_member.pointer_depth,
					is_array_member,
					resolved_array_dimensions,
					static_member.declaration,
					static_member.initializer_position,
					static_member.initializerDefinitionLookupContext(),
					static_member.is_constexpr);

				continue; // Skip the eager processing below
			}

			// Eager processing path (when lazy is disabled or not needed)
			std::optional<ASTNode> substituted_initializer =
				substitute_in_class_static_initializer_replay_first(
					static_member,
					effective_template_params,
					std::span<const TemplateTypeArg>(
						effective_template_args_vector.data(),
						effective_template_args_vector.size()),
					instantiated_name,
					struct_info->getNamespaceHandle(),
					effective_template_args);

			auto [substituted_type_index, substituted_size, substituted_alignment, is_array_member, resolved_array_dimensions] =
				compute_substituted_static_member_layout(static_member);
			std::optional<NormalizedInitializer> normalized_initializer =
				tryEarlyNormalizeTemplateStaticMemberInitializer(
					substituted_initializer,
					gSymbolTable,
					*this,
					struct_info.get(),
					effective_template_params,
					effective_template_args_vector,
					substituted_type_index,
					substituted_size,
					static_member.reference_qualifier,
					static_member.pointer_depth);

			FLASH_LOG(Templates, Debug, "Static member type substitution: original type=", (int)static_member.memberType(),
					  " -> substituted type=", (int)substituted_type_index.category(), ", size=", substituted_size);

			struct_info->addStaticMember(
				static_member.getName(),
				substituted_type_index,
				substituted_size,
				substituted_alignment,
				static_member.access,
				substituted_initializer,
				static_member.cv_qualifier,
				static_member.reference_qualifier,
				static_member.pointer_depth,
				is_array_member,
				resolved_array_dimensions,
				static_member.declaration,
				static_member.initializer_position,
				static_member.initializerDefinitionLookupContext(),
				static_member.is_constexpr);
			if (normalized_initializer.has_value()) {
				if (StructStaticMember* instantiated_static_member =
						struct_info->findStaticMember(static_member.getName())) {
					instantiated_static_member->normalized_init = std::move(normalized_initializer);
				}
			}
		}
	}
	if (!class_decl.static_members().empty() &&
		(!template_struct_info || template_struct_info->static_members.empty())) {
		for (const auto& static_member : class_decl.static_members()) {
			TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
			original_type_spec.set_type_index(static_member.type_index);
			TypeIndex substituted_type_index = substitute_template_parameter(
				original_type_spec, effective_template_params, effective_template_args);
			size_t substituted_size = get_substituted_type_size_bytes(substituted_type_index);
			if (static_member.is_array) {
				for (size_t dim_size : static_member.array_dimensions) {
					substituted_size *= dim_size;
				}
			}
			std::optional<ASTNode> substituted_initializer;
			substituted_initializer =
				substitute_in_class_static_initializer_replay_first(
					static_member,
					effective_template_params,
					std::span<const TemplateTypeArg>(
						effective_template_args_vector.data(),
						effective_template_args_vector.size()),
					instantiated_name,
					struct_info->getNamespaceHandle(),
					effective_template_args);
			struct_info->addStaticMember(
				static_member.name,
				substituted_type_index,
				substituted_size,
				static_member.alignment,
				static_member.access,
				substituted_initializer,
				static_member.cv_qualifier,
				static_member.reference_qualifier,
				static_member.pointer_depth,
				static_member.is_array,
				static_member.array_dimensions,
				static_member.declaration,
				static_member.initializer_position,
				static_member.initializerDefinitionLookupContext(),
				static_member.is_constexpr);
		}
	}
	std::vector<ASTNode> instantiated_nested_class_nodes;
	instantiated_nested_class_nodes.reserve(class_decl.nested_classes().size());

	// Copy nested classes from the template with template parameter substitution
	for (const auto& nested_class : class_decl.nested_classes()) {
		if (nested_class.is<StructDeclarationNode>()) {
			const StructDeclarationNode& nested_struct = nested_class.as<StructDeclarationNode>();
			auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(instantiated_name).append("::"sv).append(nested_struct.name()));

			// Create a new StructTypeInfo for the nested class
			auto nested_struct_info = std::make_unique<StructTypeInfo>((qualified_name), nested_struct.default_access(), nested_struct.is_union(), decl_ns);
			auto instantiated_nested_struct = emplace_node<StructDeclarationNode>(
				nested_struct.name(),
				nested_struct.is_class(),
				nested_struct.is_union());
			StructDeclarationNode& instantiated_nested_struct_ref = instantiated_nested_struct.as<StructDeclarationNode>();
			setOuterTemplateBindingsFromParams(
				instantiated_nested_struct_ref,
				effective_template_params,
				effective_template_args);

			auto add_nested_base_class = [&](const TypeInfo& base_type_info,
											 TypeIndex base_type_index,
											 AccessSpecifier access,
											 bool is_virtual) {
				auto [recorded_base_type, recorded_base_index] =
					canonicalizeRecordedBase(&base_type_info, base_type_index);
				nested_struct_info->addBaseClass(
					StringTable::getStringView(recorded_base_type->name()),
					recorded_base_index,
					access,
					is_virtual);
				instantiated_nested_struct_ref.add_base_class(
					StringTable::getStringView(recorded_base_type->name()),
					recorded_base_index,
					access,
					is_virtual);
			};

			for (const auto& base : nested_struct.base_classes()) {
				std::string_view base_class_name = base.name;
				if (base.is_deferred) {
					ensure_substitution_maps();
					auto try_add_concrete_nested_base = [&](TemplateTypeArg concrete_arg) -> bool {
						const TypeInfo* concrete_type = resolveConcreteBaseType(concrete_arg);
						if (!concrete_type || !concrete_type->getStructInfo()) {
							return false;
						}
						if (concrete_type->getStructInfo()->is_final) {
							FLASH_LOG(Templates, Error, "Cannot inherit from final class '", concrete_type->name(), "'");
							return false;
						}
						add_nested_base_class(
							*concrete_type,
							concrete_arg.type_index.withCategory(concrete_type->typeEnum()),
							base.access,
							base.is_virtual);
						return true;
					};

					bool found = false;
					if (auto subst_it = name_substitution_map.find(base_class_name);
						subst_it != name_substitution_map.end()) {
						found = try_add_concrete_nested_base(subst_it->second);
					}
					if (!found) {
						StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class_name);
						if (auto pack_it = pack_substitution_map.find(base_name_handle);
							pack_it != pack_substitution_map.end()) {
							for (const TemplateTypeArg& pack_arg : pack_it->second) {
								found = try_add_concrete_nested_base(pack_arg) || found;
							}
						}
					}
					if (!found && base.type_index.is_valid()) {
						if (const TypeInfo* unresolved_base = tryGetTypeInfo(base.type_index)) {
							nested_struct_info->addBaseClass(
								StringTable::getStringView(unresolved_base->name()),
								unresolved_base->registeredTypeIndex(),
								base.access,
								base.is_virtual,
								true);
							instantiated_nested_struct_ref.add_base_class(
								StringTable::getStringView(unresolved_base->name()),
								unresolved_base->registeredTypeIndex(),
								base.access,
								base.is_virtual,
								true);
						}
					}
				} else {
					auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(base_class_name));
					if (base_type_it != getTypesByNameMap().end() &&
						base_type_it->second != nullptr &&
						base_type_it->second->getStructInfo() != nullptr) {
						add_nested_base_class(
							*base_type_it->second,
							base_type_it->second->registeredTypeIndex().withCategory(base_type_it->second->typeEnum()),
							base.access,
							base.is_virtual);
					}
				}
			}

			if (!nested_struct.deferred_template_base_classes().empty()) {
				ensure_substitution_maps();
				for (const auto& deferred_base : nested_struct.deferred_template_base_classes()) {
					DeferredBaseReplayContextScope deferred_base_replay_scope(
						*this,
						deferred_base,
						nested_struct.name(),
						template_params,
						template_args_to_use);

					DeferredBasePackExpansionBindingInfo base_pack_bindings =
						collectDeferredBasePackExpansionBindings(deferred_base, pack_substitution_map);
					if (base_pack_bindings.invalid) {
						continue;
					}

					const size_t expansion_count = base_pack_bindings.pack_bindings.empty()
						? 1
						: base_pack_bindings.expansion_count;
					InlineVector<const TypeInfo*, 4> resolved_base_types;
					bool defer_entire_base = false;
					for (size_t expansion_index = 0; expansion_index < expansion_count; ++expansion_index) {
						TemplateArgSubstitutionMap subst_map =
							makeDeferredBaseExpansionSubstitutionMap(
								name_substitution_map,
								base_pack_bindings,
								expansion_index);
						std::vector<TemplateTypeArg> resolved_args;
						bool unresolved_arg = false;

						for (const auto& arg_info : deferred_base.template_arguments) {
							if (arg_info.node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& type_spec = arg_info.node.as<TypeSpecifierNode>();
								if (std::optional<TemplateTypeArg> mapped_arg =
										tryResolveDeferredBaseTypeArgFromMap(type_spec, subst_map);
									mapped_arg.has_value()) {
									mapped_arg->is_pack = arg_info.is_pack;
									resolved_args.push_back(*mapped_arg);
									continue;
								}
								if (std::optional<TemplateTypeArg> materialized_arg =
										tryMaterializeDeferredBaseTypeArg(
											type_spec,
											template_params,
											template_args_to_use,
											[this](std::string_view concrete_base_template_name, std::span<const TemplateTypeArg> concrete_base_args) {
												std::string_view mutable_template_name = concrete_base_template_name;
												return instantiate_and_register_base_template(mutable_template_name, concrete_base_args);
											});
									materialized_arg.has_value()) {
									materialized_arg->is_pack = arg_info.is_pack;
									resolved_args.push_back(*materialized_arg);
									continue;
								}

								TypeIndex resolved_type_index =
									substitute_template_parameter(type_spec, template_params, template_args_to_use);
								if (!resolved_type_index.is_valid()) {
									unresolved_arg = true;
									break;
								}

								TemplateTypeArg resolved_arg;
								TypeCategory resolved_category = resolved_type_index.category();
								if (const TypeInfo* resolved_type_info = tryGetTypeInfo(resolved_type_index);
									resolved_type_info != nullptr &&
									resolved_type_info->typeEnum() != TypeCategory::Invalid) {
									resolved_category = resolved_type_info->typeEnum();
								}
								resolved_arg.type_index = resolved_type_index.withCategory(resolved_category);
								resolved_arg.pointer_depth = type_spec.pointer_depth();
								resolved_arg.ref_qualifier = type_spec.reference_qualifier();
								resolved_arg.cv_qualifier = type_spec.cv_qualifier();
								resolved_arg.is_pack = arg_info.is_pack;
								resolved_args.push_back(resolved_arg);
								continue;
							}

							if (arg_info.node.is<ExpressionNode>()) {
								const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
								if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&expr)) {
									auto subst_it = subst_map.find(tparam_ref->param_name().view());
									if (subst_it == subst_map.end()) {
										unresolved_arg = true;
										break;
									}
									TemplateTypeArg resolved_arg = subst_it->second;
									resolved_arg.is_pack = arg_info.is_pack;
									resolved_args.push_back(resolved_arg);
									continue;
								}
								if (const auto* identifier = std::get_if<IdentifierNode>(&expr)) {
									auto subst_it = subst_map.find(identifier->name());
									if (subst_it != subst_map.end()) {
										TemplateTypeArg resolved_arg = subst_it->second;
										resolved_arg.is_pack = arg_info.is_pack;
										resolved_args.push_back(resolved_arg);
										continue;
									}
								}
							}

							unresolved_arg = true;
							break;
						}

						if (unresolved_arg || resolved_args.empty()) {
							defer_entire_base = true;
							break;
						}

						AliasTemplateMaterializationResult materialized_base =
							materializeTemplateInstantiationForLookup(
								StringTable::getStringView(deferred_base.base_template_name),
								resolved_args);
						const TypeInfo* final_base_type = materialized_base.resolved_type_info;
						std::string_view final_base_name = materialized_base.instantiated_name;
						if (final_base_type == nullptr && !final_base_name.empty()) {
							auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(final_base_name));
							if (base_type_it != getTypesByNameMap().end()) {
								final_base_type = base_type_it->second;
							}
						}
						if (final_base_type == nullptr || final_base_type->getStructInfo() == nullptr) {
							defer_entire_base = true;
							break;
						}

						resolved_base_types.push_back(final_base_type);
					}

					if (defer_entire_base) {
						nested_struct_info->has_deferred_base_classes = true;
						nested_struct_info->deferred_template_bases.push_back(
							StructTypeInfo::DeferredTemplateBaseEntry(deferred_base));
						instantiated_nested_struct_ref.add_deferred_template_base_class(
							deferred_base.base_template_name,
							deferred_base.template_arguments,
							deferred_base.member_type_chain,
							deferred_base.access,
							deferred_base.is_virtual,
							deferred_base.is_pack_expansion,
							deferred_base.replay_position,
							deferred_base.replay_definition_lookup_context,
							deferred_base.replay_template_parameters);
						continue;
					}

					for (const TypeInfo* resolved_base_type : resolved_base_types) {
						add_nested_base_class(
							*resolved_base_type,
							resolved_base_type->registeredTypeIndex().withCategory(resolved_base_type->typeEnum()),
							deferred_base.access,
							deferred_base.is_virtual);
					}
				}
			}

			// Copy and substitute members from the nested class
			for (const auto& member_decl : nested_struct.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_specifier_node();
				TypeIndex substituted_type_index = substitute_template_parameter(
					type_spec, template_params, template_args_to_use);
				ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(substituted_type_index);
				std::vector<size_t> resolved_array_dimensions = resolve_array_dimensions(
					decl, template_params, template_args_to_use);
				if (resolved_array_dimensions.empty()) {
					resolved_array_dimensions = resolved_member_alias.array_dimensions;
				}
				bool is_array_member = !resolved_array_dimensions.empty();
				TypeIndex member_size_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : substituted_type_index;
				TypeIndex stored_member_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : substituted_type_index;

				TypeSpecifierNode substituted_type_spec(
					substituted_type_index.category(),
					type_spec.qualifier(),
					get_type_size_bits(substituted_type_index.category()),
					Token(), // Empty token
					CVQualifier::None);
				substituted_type_spec.set_type_index(substituted_type_index);

				for (const auto& ptr_level : type_spec.pointer_levels()) {
					substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
				}

				size_t member_size;
				if (is_array_member) {
					size_t total_elements = 1;
					for (size_t dim_size : resolved_array_dimensions) {
						total_elements *= dim_size;
					}

					size_t element_size = calculateResolvedMemberSizeAndAlignment(
											  substituted_type_spec, member_size_type_index)
											  .size;
					member_size = element_size * total_elements;
				} else if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
					member_size = 8;
				} else {
					member_size = calculateResolvedMemberSizeAndAlignment(
									  substituted_type_spec, member_size_type_index)
									  .size;
				}
				size_t member_alignment = get_type_alignment(member_size_type_index.category(), member_size);
				if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
					member_alignment = 8;
				} else if (const StructTypeInfo* member_struct_info = tryGetStructTypeInfo(member_size_type_index)) {
					member_alignment = member_struct_info->alignment;
				}

				ReferenceQualifier ref_qual = substituted_type_spec.reference_qualifier();
				size_t referenced_size_bits = ref_qual != ReferenceQualifier::None
												  ? get_type_size_bits(substituted_type_index.category())
												  : 0;
				std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
					member_decl.default_initializer, template_args_to_use, template_params);
				std::optional<ASTNode> substituted_bitfield_width_expr = member_decl.bitfield_width_expr.has_value()
																			 ? std::optional<ASTNode>(substituteTemplateParameters(
																				   *member_decl.bitfield_width_expr, template_params, template_args_to_use))
																			 : std::nullopt;

				StringHandle member_name_handle = decl.identifier_token().handle();
				nested_struct_info->addMember(
					member_name_handle,
					stored_member_type_index,
					member_size,
					member_alignment,
					member_decl.access,
					substituted_default_initializer,
					ref_qual,
					referenced_size_bits,
					is_array_member,
					resolved_array_dimensions,
					static_cast<int>(substituted_type_spec.pointer_depth()),
					resolve_bitfield_width(member_decl, template_params, template_args_to_use),
					resolveTemplateFunctionPointerSignature(type_spec, substituted_type_spec.type_index(), template_params, template_args_to_use),
					member_decl.is_no_unique_address);

				ASTNode substituted_member_decl = substituteTemplateParameters(
					member_decl.declaration, template_params, template_args_to_use);
				instantiated_nested_struct_ref.add_member(
					substituted_member_decl,
					member_decl.access,
					substituted_default_initializer,
					resolve_bitfield_width(member_decl, template_params, template_args_to_use),
					substituted_bitfield_width_expr,
					member_decl.is_no_unique_address);
			}

			auto copy_nested_static_members = [&](const StructTypeInfo& original_struct_info) {
				for (const auto& static_member : original_struct_info.static_members) {
					TypeSpecifierNode original_type_spec(static_member.memberType(), TypeQualifier::None, static_member.size * 8, Token{}, CVQualifier::None);
					original_type_spec.set_type_index(static_member.type_index);
					TypeIndex substituted_type_index = substitute_template_parameter(
						original_type_spec, template_params, template_args_to_use);
					size_t substituted_size = get_substituted_type_size_bytes(substituted_type_index);
					std::optional<ASTNode> substituted_initializer =
						substitute_in_class_static_initializer_replay_first(
							static_member,
							template_params,
							template_args_to_use,
							qualified_name,
							nested_struct_info->getNamespaceHandle(),
							template_args_to_use);
					nested_struct_info->addStaticMember(
						static_member.getName(),
						substituted_type_index,
						substituted_size,
						static_member.alignment,
						static_member.access,
						substituted_initializer,
						static_member.cv_qualifier,
						static_member.reference_qualifier,
						static_member.pointer_depth,
						static_member.is_array,
						static_member.array_dimensions,
						static_member.declaration,
						static_member.initializer_position,
						static_member.initializerDefinitionLookupContext(),
						static_member.is_constexpr);
					instantiated_nested_struct_ref.addStaticMember(
						static_member.getName(),
						substituted_type_index,
						substituted_size,
						static_member.alignment,
						static_member.access,
						substituted_initializer,
						static_member.cv_qualifier,
						static_member.reference_qualifier,
						static_member.pointer_depth,
						static_member.is_array,
						static_member.array_dimensions,
						static_member.declaration,
						static_member.initializer_position,
						static_member.initializerDefinitionLookupContext(),
						static_member.is_constexpr);
				}
			};

			StringBuilder original_nested_name_builder;
			original_nested_name_builder.append(template_name).append("::"sv).append(nested_struct.name());
			std::string_view original_nested_name = original_nested_name_builder.commit();
			auto nested_out_of_line_members = gTemplateRegistry.getOutOfLineMemberFunctions(original_nested_name);
			auto original_nested_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(original_nested_name));
			if (original_nested_it != getTypesByNameMap().end() && original_nested_it->second->getStructInfo()) {
				copy_nested_static_members(*original_nested_it->second->getStructInfo());
			} else {
				auto simple_nested_it = getTypesByNameMap().find(nested_struct.name());
				if (simple_nested_it != getTypesByNameMap().end() && simple_nested_it->second->getStructInfo()) {
					copy_nested_static_members(*simple_nested_it->second->getStructInfo());
				}
			}

			const std::span<const StructMemberFunctionDecl> nested_source_member_functions =
				nested_struct.member_functions();

			SourceMemberIdentityMaps nested_source_member_identity_maps;
			for (const StructMemberFunctionDecl& source_member : nested_source_member_functions) {
				registerSourceMemberStubIdentity(
					nested_source_member_identity_maps,
					source_member.function_declaration,
					source_member.function_declaration);
			}

			for (const auto& out_of_line_member : nested_out_of_line_members) {
				if (!out_of_line_member.inner_template_params.empty() &&
					out_of_line_member.function_node.is<ConstructorDeclarationNode>()) {
					const ConstructorDeclarationNode& out_of_line_ctor_decl =
						out_of_line_member.function_node.as<ConstructorDeclarationNode>();
					OutOfLineConstructorStubResolution ctor_resolution =
						findOutOfLineConstructorTemplateStubByIdentity(
							*this,
							nested_source_member_identity_maps,
							std::span<const StructMemberFunctionDecl>(
								nested_source_member_functions.data(),
								nested_source_member_functions.size()),
							out_of_line_ctor_decl,
							std::span<const TemplateParameterNode>(
								template_params.data(),
								template_params.size()),
							std::span<const TemplateTypeArg>(
								template_args_to_use.data(),
								template_args_to_use.size()),
							qualified_name,
							std::span<const TemplateParameterNode>(
								out_of_line_member.inner_template_params.data(),
								out_of_line_member.inner_template_params.size()));

					if (ctor_resolution.ambiguous) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not uniquely match nested out-of-line constructor template '")
							.append(out_of_line_ctor_decl.name())
							.append("' in instantiated class '")
							.append(qualified_name)
							.append("'")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					} else if (ctor_resolution.ctor != nullptr) {
						ConstructorDeclarationNode& ctor_decl = *ctor_resolution.ctor;
						setOutOfLineConstructorTemplateReplayMetadata(
							ctor_decl,
							out_of_line_member);
						syncOutOfLineConstructorTemplateParameters(
							ctor_decl.parameter_nodes(),
							out_of_line_ctor_decl.parameter_nodes());
					} else {
						StringBuilder error_builder;
						if (ctor_resolution.insufficient_evidence) {
							error_builder
								.append("Could not match nested out-of-line constructor template '");
						} else {
							error_builder
								.append("Could not attach nested out-of-line constructor template '");
						}
						error_builder
							.append(out_of_line_ctor_decl.name())
							.append("' for instantiated class '")
							.append(qualified_name);
						if (!ctor_resolution.insufficient_evidence) {
							error_builder.append("' via source-member identity mapping");
						} else {
							error_builder.append("'");
						}
						std::string error_msg = std::string(error_builder.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					continue;
				}

				const bool out_of_line_ctor_stub =
					out_of_line_member.function_node.is<FunctionDeclarationNode>() &&
					out_of_line_member.function_node.as<FunctionDeclarationNode>().decl_node().identifier_token().value() ==
						nested_struct.name().view();
				if (out_of_line_ctor_stub &&
					out_of_line_member.function_node.is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& out_of_line_ctor_stub_decl =
						out_of_line_member.function_node.as<FunctionDeclarationNode>();
					OutOfLineConstructorStubResolution ctor_resolution =
						findOutOfLineConstructorTemplateStubByIdentity(
							*this,
							nested_source_member_identity_maps,
							std::span<const StructMemberFunctionDecl>(
								nested_source_member_functions.data(),
								nested_source_member_functions.size()),
							out_of_line_ctor_stub_decl,
							std::span<const TemplateParameterNode>(
								template_params.data(),
								template_params.size()),
							std::span<const TemplateTypeArg>(
								template_args_to_use.data(),
								template_args_to_use.size()),
							qualified_name,
							std::span<const TemplateParameterNode>(
								out_of_line_member.inner_template_params.data(),
								out_of_line_member.inner_template_params.size()));
					if (ctor_resolution.ambiguous) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not uniquely match nested out-of-line constructor template '")
							.append(out_of_line_ctor_stub_decl.decl_node().identifier_token().value())
							.append("' in instantiated class '")
							.append(qualified_name)
							.append("'")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					if (ctor_resolution.ctor == nullptr) {
						StringBuilder error_builder;
						if (ctor_resolution.insufficient_evidence) {
							error_builder
								.append("Could not match nested out-of-line constructor template '");
						} else {
							error_builder
								.append("Could not attach nested out-of-line constructor template '");
						}
						error_builder
							.append(out_of_line_ctor_stub_decl.decl_node().identifier_token().value())
							.append("' for instantiated class '")
							.append(qualified_name);
						if (!ctor_resolution.insufficient_evidence) {
							error_builder.append("' via source-member identity mapping");
						} else {
							error_builder.append("'");
						}
						std::string error_msg = std::string(error_builder.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}

					ConstructorDeclarationNode& ctor_decl = *ctor_resolution.ctor;
					if (!ctor_decl.is_materialized() &&
						!ctor_decl.has_template_body_position()) {
						setOutOfLineConstructorTemplateReplayMetadata(
							ctor_decl,
							out_of_line_member);
						syncOutOfLineConstructorTemplateParameters(
							ctor_decl.parameter_nodes(),
							out_of_line_ctor_stub_decl.parameter_nodes());
					}

					if (nested_struct_info != nullptr) {
						OutOfLineConstructorStubResolution info_ctor_resolution =
							findMatchingConstructorInStructInfo(
								*nested_struct_info,
								ctor_decl,
								[](const ConstructorDeclarationNode& info_ctor) {
									return !info_ctor.has_template_body_position();
								});
						if (info_ctor_resolution.ctor != nullptr) {
							setOutOfLineConstructorTemplateReplayMetadata(
								*info_ctor_resolution.ctor,
								out_of_line_member);
							syncOutOfLineConstructorTemplateParameters(
								info_ctor_resolution.ctor->parameter_nodes(),
								out_of_line_ctor_stub_decl.parameter_nodes());
						} else if (info_ctor_resolution.ambiguous) {
							FLASH_LOG(
								Templates,
								Error,
								"Ambiguous StructTypeInfo constructor-template sync for nested out-of-line constructor template '",
								out_of_line_ctor_stub_decl.decl_node().identifier_token().value(),
								"' in instantiated class '",
								StringTable::getStringView(qualified_name),
								"'");
						}
					}
					continue;
				}

				if (!out_of_line_member.inner_template_params.empty() &&
					out_of_line_member.function_node.is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& ool_func =
						out_of_line_member.function_node.as<FunctionDeclarationNode>();
					std::string_view ool_func_name =
						ool_func.decl_node().identifier_token().value();

					// Collect same-name/same-shape candidates, then require a positive
					// substituted-signature match before replay attachment.
					InlineVector<const StructMemberFunctionDecl*, 4> same_name_candidates;
					const size_t ool_inner_template_param_count =
						out_of_line_member.inner_template_params.size();
					const size_t ool_function_param_count = ool_func.parameter_nodes().size();
					for (const StructMemberFunctionDecl& mem_func : nested_struct.member_functions()) {
						const FunctionDeclarationNode* nested_func_decl =
							get_function_decl_node(mem_func.function_declaration);
						if (nested_func_decl == nullptr) {
							continue;
						}
						if (!nested_func_decl->has_any_body_source() &&
							isMatchingMemberTemplate(
								mem_func,
								ool_func_name,
								ool_inner_template_param_count,
								ool_function_param_count)) {
							same_name_candidates.push_back(&mem_func);
						}
					}

					FunctionDeclarationNode* matched_nested_func_decl = nullptr;
					bool saw_insufficient_replay_evidence = false;
					bool saw_ambiguous_replay_match = false;
					for (const StructMemberFunctionDecl* source_member : same_name_candidates) {
						ASTNode* matched_stub = findSourceMemberStubByIdentity(
							nested_source_member_identity_maps,
							source_member->function_declaration);
						if (matched_stub == nullptr ||
							!matched_stub->is<TemplateFunctionDeclarationNode>()) {
							continue;
						}
						ReplaySignatureMatchResult signature_match =
							nestedOutOfLineMemberTemplateMatchesCandidate(
								*this,
								source_member->function_declaration,
								ool_func,
								std::span<const TemplateParameterNode>(
									template_params.data(),
									template_params.size()),
								std::span<const TemplateTypeArg>(
									template_args_to_use.data(),
									template_args_to_use.size()),
								qualified_name,
								std::span<const TemplateParameterNode>(
									out_of_line_member.inner_template_params.data(),
									out_of_line_member.inner_template_params.size()));
						if (signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
							saw_insufficient_replay_evidence = true;
							continue;
						}
						if (signature_match != ReplaySignatureMatchResult::Match) {
							continue;
						}
						FunctionDeclarationNode* matched_template_func_decl =
							get_function_decl_node_mut(*matched_stub);
						if (matched_template_func_decl == nullptr) {
							continue;
						}
						if (matched_nested_func_decl != nullptr &&
							matched_nested_func_decl != matched_template_func_decl) {
							saw_ambiguous_replay_match = true;
							break;
						}
						matched_nested_func_decl = matched_template_func_decl;
					}

					if (saw_ambiguous_replay_match) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not uniquely match nested out-of-line member template '")
							.append(ool_func_name)
							.append("' for nested struct '")
							.append(nested_struct.name().view())
							.append("' in instantiated class '")
							.append(StringTable::getStringView(qualified_name))
							.append("'")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
					if (matched_nested_func_decl != nullptr) {
						matched_nested_func_decl->set_template_body_position(out_of_line_member.body_start);
						copyDefinitionParameterIdentifiers(
							matched_nested_func_decl->parameter_nodes(),
							ool_func.parameter_nodes());
						copyDefinitionParameterTypesForDependentMemberTemplateSegments(
							matched_nested_func_decl->parameter_nodes(),
							ool_func.parameter_nodes());
						matched_nested_func_decl->set_definition_lookup_context(
							out_of_line_member.definition_lookup_context);
						FLASH_LOG(
							Templates,
							Debug,
							"Set body position on nested struct template member: ",
							ool_func_name);
					} else if (saw_insufficient_replay_evidence) {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not match nested out-of-line member template '")
							.append(ool_func_name)
							.append("' for nested struct '")
							.append(nested_struct.name().view())
							.append("' in instantiated class '")
							.append(StringTable::getStringView(qualified_name))
							.append("' to exactly one declaration after template substitution")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					} else {
						std::string error_msg = std::string(StringBuilder()
							.append("Could not attach nested out-of-line member template '")
							.append(ool_func_name)
							.append("' for nested struct '")
							.append(nested_struct.name().view())
							.append("' in instantiated class '")
							.append(StringTable::getStringView(qualified_name))
							.append("'")
							.commit());
						if (force_eager) {
							throw CompileError(error_msg);
						}
						return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
					}
				}
			}

			// Finalize the nested struct layout
			const bool nested_finalize_success =
				!nested_struct_info->base_classes.empty()
					? nested_struct_info->finalizeWithBases()
					: nested_struct_info->finalize();
			if (!nested_finalize_success) {
				// Log error and return nullopt - compilation will continue but template instantiation fails
				FLASH_LOG(Parser, Error, nested_struct_info->getFinalizationError());
				return std::nullopt;
			}

			// Register member functions of the nested struct for lazy instantiation.
			// These are the methods inside Inner (e.g., Inner::get()) — they are not
			// top-level member functions of the parent template and would otherwise
			// never be registered, causing link errors when called.
			const TemplateEnvironmentSnapshot* outer_parent_snapshot =
				instantiated_nested_struct_ref.has_outer_template_bindings()
					? &instantiated_nested_struct_ref.outer_template_environment_snapshot()
					: nullptr;
			registerNestedMemberFunctionsForLazy(
				nested_struct,
				*nested_struct_info,
				instantiated_name,
				qualified_name,
				effective_template_params,
				effective_template_args,
				outer_parent_snapshot,
				shouldCommitTemplateInstantiationArtifacts());
			if (shouldCommitTemplateInstantiationArtifacts()) {
				OuterTemplateBinding nested_member_outer_binding;
				collectOuterTemplateBinding(
					effective_template_params,
					effective_template_args,
					nested_member_outer_binding);
				for (const StructMemberFunctionDecl& nested_member_func :
					 nested_struct.member_functions()) {
					if (!nested_member_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
						continue;
					}
					const FunctionDeclarationNode* nested_func_decl =
						get_function_decl_node(nested_member_func.function_declaration);
					if (nested_func_decl == nullptr) {
						continue;
					}
					StringHandle nested_member_qualified_name =
						StringTable::getOrInternStringHandle(
							StringBuilder()
								.append(StringTable::getStringView(qualified_name))
								.append("::")
								.append(nested_func_decl->decl_node().identifier_token().value())
								.commit());
					gTemplateRegistry.registerTemplate(
						nested_member_qualified_name,
						nested_member_func.function_declaration);
					gTemplateRegistry.registerOuterTemplateBinding(
						nested_member_qualified_name,
						nested_member_outer_binding);
				}
			}

			// Mark nested struct as needing a trivial default constructor when it
			// has no explicit constructors.  Without this, default member
			// initializers (e.g. `int tag = N;`) are never applied because
			// generateTrivialDefaultConstructors() skips types without the flag.
			if (!nested_struct_info->hasAnyConstructor()) {
				nested_struct_info->needs_default_constructor = true;
			}

			// Register the nested class in the type system
			auto& nested_type_info = add_instantiated_type(qualified_name, TypeCategory::Struct, 0); // Placeholder size
			nested_type_info.setStructInfo(std::move(nested_struct_info));
			if (nested_type_info.getStructInfo()) {
				nested_type_info.fallback_size_bits_ = nested_type_info.getStructInfo()->sizeInBits().value;
			}

 // Propagate the outer type's instantiation context to the nested type.
 // The nested type isn't itself a template instantiation, but it lives inside
 // one and needs the enclosing template's bindings for constexpr evaluation.
			nested_type_info.setInstantiationContext(
				collectParamNameHandles(
					effective_template_params,
					effective_template_args.size()),
				collectEnrichedTemplateArgInfos(
					effective_template_params,
					effective_template_args),
				struct_type_info.instantiationContext());

			struct_info->addNestedClass(nested_type_info.getStructInfo());
			FLASH_LOG(Templates, Debug, "Registered nested class: ", StringTable::getStringView(qualified_name));
			if (shouldCommitTemplateInstantiationArtifacts()) {
				OuterTemplateBinding nested_alias_outer_binding;
				collectOuterTemplateBinding(
					effective_template_params,
					effective_template_args,
					nested_alias_outer_binding);
				StringBuilder nested_alias_prefix_builder;
				nested_alias_prefix_builder.append(original_nested_name);
				nested_alias_prefix_builder.append("::");
				std::string_view nested_alias_lookup_prefix = nested_alias_prefix_builder.commit();
				for (const auto& base_alias_name : gTemplateRegistry.get_alias_templates_with_prefix(nested_alias_lookup_prefix)) {
					std::string_view member_name = std::string_view(base_alias_name).substr(nested_alias_lookup_prefix.size());
					StringHandle inst_alias_name = StringTable::getOrInternStringHandle(
						StringBuilder()
							.append(qualified_name)
							.append("::")
							.append(member_name)
							.commit());
					auto alias_opt = gTemplateRegistry.lookup_alias_template(base_alias_name);
					if (alias_opt.has_value()) {
						registerAliasTemplateWithOuterBinding(
							inst_alias_name,
							*alias_opt,
							&nested_alias_outer_binding);
					}
				}
			}
			instantiated_nested_class_nodes.push_back(instantiated_nested_struct);
		}
	}

	// Process out-of-line nested class definitions
	// These are patterns like: template<typename T> struct Outer<T>::Inner { T data; };
	// The definition was saved during parsing and is now re-parsed with template parameter substitution.
	auto ool_nested_classes = gTemplateRegistry.getOutOfLineNestedClasses(template_name);
	FLASH_LOG(Templates, Debug, "Processing ", ool_nested_classes.size(), " out-of-line nested class definitions for ", template_name);
	for (const auto& ool_nested : ool_nested_classes) {
		// Full specializations (template<>) store concrete args — skip if they don't match
		// this instantiation's arguments (e.g., Wrapper<int>::Nested shouldn't apply to Wrapper<float>).
		if (!ool_nested.specialization_args.empty() &&
			(ool_nested.specialization_args.size() != template_args_to_use.size() ||
			 !std::equal(ool_nested.specialization_args.begin(), ool_nested.specialization_args.end(),
						 template_args_to_use.begin()))) {
			continue;
		}

		std::string_view nested_name = StringTable::getStringView(ool_nested.nested_class_name);
		auto qualified_name = StringTable::getOrInternStringHandle(
			StringBuilder().append(instantiated_name).append("::"sv).append(nested_name));

		// Check if already registered - skip only if it has actual members (from inline definition)
		// Forward-declared nested classes are registered with no members; those need to be replaced.
		auto existing_it = getTypesByNameMap().find(qualified_name);
		if (existing_it != getTypesByNameMap().end()) {
			TypeInfo* existing_nested_type = existing_it->second;
			if (existing_nested_type->getStructInfo() && !existing_nested_type->getStructInfo()->members.empty()) {
				FLASH_LOG(Templates, Debug, "Out-of-line nested class already has members: ", StringTable::getStringView(qualified_name));
				continue;
			}
			FLASH_LOG(Templates, Debug, "Replacing forward-declared nested class: ", StringTable::getStringView(qualified_name));
		}

		// Save current lexer position and parser state
		SaveHandle saved_pos = save_token_position();
		FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
		setCurrentTemplateParamNames(ool_nested.template_param_names);
		FlashCpp::TemplateDepthGuard guard_template_body(parsing_template_depth_);
		FlashCpp::ScopedState guard_template_class(parsing_template_class_);
		parsing_template_class_ = true;
		FlashCpp::ScopedState guard_delayed_bodies(delayed_function_bodies_);

		// Restore lexer to the saved position (at the struct/class keyword).
		// Push the instantiated template onto struct_parsing_context_stack_ so that
		// parse_struct_declaration() builds the correct qualified name (e.g., "Wrapper$hash::Nested")
		restore_lexer_position_only(ool_nested.body_start);
		const size_t saved_pack_alignment = context_.getCurrentPackAlignment();
		auto restore_pack_alignment = ScopeGuard([this, saved_pack_alignment]() {
			context_.setPackAlignment(saved_pack_alignment);
		});
		context_.setPackAlignment(ool_nested.pack_alignment);

		struct_parsing_context_stack_.push_back({StringTable::getStringView(instantiated_name),
												 nullptr, // struct_node — not needed; parse_struct_declaration() creates its own
												 struct_info.get(),
												 decl_ns,
												 {}});

		// Reuse parse_struct_declaration() which handles everything: type registration,
		// base class parsing, constructors, destructors, using declarations, member
		// functions, data members, layout computation, StructTypeInfo finalization, etc.
		auto nested_result = parse_struct_declaration();

		struct_parsing_context_stack_.pop_back();

		if (nested_result.is_error()) {
			FLASH_LOG(Templates, Warning, "Failed to parse out-of-line nested class: ",
					  StringTable::getStringView(qualified_name));
		} else {
			FLASH_LOG(Templates, Debug, "Parsed out-of-line nested class via parse_struct_declaration(): ",
					  StringTable::getStringView(qualified_name));
			auto resolved_nested_it = getTypesByNameMap().find(qualified_name);
			auto extractNestedStructAndInfo = [&]() -> std::pair<StructDeclarationNode*, StructTypeInfo*> {
				if (!nested_result.node().has_value() || !nested_result.node()->is<StructDeclarationNode>()) {
					return {nullptr, nullptr};
				}
				if (resolved_nested_it == getTypesByNameMap().end() ||
					!resolved_nested_it->second ||
					!resolved_nested_it->second->getStructInfo()) {
					return {nullptr, nullptr};
				}
				return {&nested_result.node()->as<StructDeclarationNode>(), resolved_nested_it->second->getStructInfo()};
			};
			auto [parsed_nested_struct, parsed_nested_info] = extractNestedStructAndInfo();
			if (parsed_nested_struct && parsed_nested_info) {
				const size_t member_count = std::min(parsed_nested_struct->members().size(), parsed_nested_info->members.size());
				bool adjusted_member_types = false;

				for (size_t member_idx = 0; member_idx < member_count; ++member_idx) {
					const auto& member_decl = parsed_nested_struct->members()[member_idx];
					if (!member_decl.declaration.is<DeclarationNode>()) {
						continue;
					}

					const auto& decl = member_decl.declaration.as<DeclarationNode>();
					const auto& type_spec = decl.type_specifier_node();
					TypeIndex substituted_type_index = substitute_template_parameter(
						type_spec, template_params, template_args_to_use);
					ResolvedAliasTypeInfo resolved_member_alias = resolveAliasTypeInfo(substituted_type_index);
					TypeIndex resolved_member_type_index = resolved_member_alias.isArray() ? resolved_member_alias.type_index : substituted_type_index;
					std::vector<size_t> resolved_array_dimensions = resolve_array_dimensions(
						decl, template_params, template_args_to_use);
					if (resolved_array_dimensions.empty()) {
						resolved_array_dimensions = resolved_member_alias.array_dimensions;
					}
					const bool is_array_member = !resolved_array_dimensions.empty();

					TypeSpecifierNode substituted_type_spec(
						substituted_type_index,
						get_type_size_bits(substituted_type_index.category()),
						type_spec.token(),
						type_spec.cv_qualifier(),
						type_spec.reference_qualifier());
					const TemplateTypeArg* resolved_member_arg =
						findResolvedTypeTemplateArg(type_spec, template_params, template_args_to_use);
					for (const auto& ptr_level : type_spec.pointer_levels()) {
						substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
					}
					applyResolvedTemplateArgTypeMetadata(substituted_type_spec, resolved_member_arg);
					if (is_array_member) {
						substituted_type_spec.set_array(true);
						substituted_type_spec.set_array_dimensions(resolved_array_dimensions);
					}

					auto& stored_member = parsed_nested_info->members[member_idx];
					stored_member.type_index = resolved_member_type_index;
					stored_member.reference_qualifier = substituted_type_spec.reference_qualifier();
					stored_member.pointer_depth = static_cast<int>(substituted_type_spec.pointer_depth());
					stored_member.is_array = is_array_member;
					stored_member.array_dimensions = resolved_array_dimensions;
					stored_member.referenced_size_bits = stored_member.reference_qualifier != ReferenceQualifier::None
														 ? get_substituted_type_size_bits(substituted_type_index)
														 : 0;

					if (substituted_type_spec.is_pointer() || substituted_type_spec.is_reference() || substituted_type_spec.is_rvalue_reference()) {
						stored_member.size = 8;
						stored_member.alignment = 8;
					} else {
						stored_member.size = calculateResolvedMemberSizeAndAlignment(
							substituted_type_spec, resolved_member_type_index).size;
						if (is_array_member) {
							for (size_t dim_size : resolved_array_dimensions) {
								stored_member.size *= dim_size;
							}
						}
						stored_member.alignment = get_type_alignment(resolved_member_type_index.category(), stored_member.size);
						if (const StructTypeInfo* member_struct_info = tryGetStructTypeInfo(resolved_member_type_index)) {
							stored_member.alignment = member_struct_info->alignment;
						}
					}

					adjusted_member_types = true;
				}

				if (adjusted_member_types) {
					parsed_nested_info->recalculateLayout();
					resolved_nested_it->second->fallback_size_bits_ = static_cast<int>(toSizeT(parsed_nested_info->sizeInBytes()) * 8);
				}
			}
		}

		restore_lexer_position_only(saved_pos);
	}

	// Fix up struct members whose types were unresolved nested classes.
	// During member processing above, nested classes haven't been instantiated yet,
	// so members of type "Wrapper::Nested" (with size 0) remain unresolved.
	// Now that nested classes are registered as "Wrapper$hash::Nested", update those members.
	{
		StructTypeInfo* si = struct_info.get();
		if (si) {
			bool had_fixup = false;
			for (auto& member : si->members) {
				if (member.size == 0) {
					if (const TypeInfo* mem_type_info = tryGetTypeInfo(member.type_index)) {
						std::string_view mem_type_name = StringTable::getStringView(mem_type_info->name());
						// Check if this is a nested class of the current template (e.g., "Wrapper::Nested")
						if (mem_type_name.starts_with(template_name) && mem_type_name.size() > template_name.size() + 2 &&
							mem_type_name.substr(template_name.size(), 2) == "::") {
							std::string_view nested_name = mem_type_name.substr(template_name.size() + 2);
							StringBuilder sb;
							StringHandle resolved_handle = StringTable::getOrInternStringHandle(
								sb.append(instantiated_name).append("::").append(nested_name).commit());
							auto resolved_it = getTypesByNameMap().find(resolved_handle);
							if (resolved_it != getTypesByNameMap().end()) {
								const TypeInfo* resolved_type = resolved_it->second;
								member.type_index = resolved_type->type_index_;
								if (resolved_type->getStructInfo()) {
									member.size = toSizeT(resolved_type->getStructInfo()->sizeInBytes());
									member.alignment = resolved_type->getStructInfo()->alignment;
								}
								had_fixup = true;
								FLASH_LOG(Templates, Debug, "Fixed nested class member '", StringTable::getStringView(member.name),
										  "': ", mem_type_name, " -> ", StringTable::getStringView(resolved_handle),
										  " (size=", member.size, ")");
							}
						}
					}
				}
			}

			// Recalculate struct layout from scratch after nested class member fixup
			if (had_fixup) {
				si->recalculateLayout();
				struct_type_info.fallback_size_bits_ = si->sizeInBits().value;
				FLASH_LOG(Templates, Debug, "Re-laid out struct ", instantiated_name,
						  " after nested class fixup, total_size=", si->total_size);
			}
		}
	}

	// Copy type aliases from the template with template parameter substitution
	const bool has_unresolved_primary_template_args =
		std::ranges::any_of(
			template_args_to_use,
			[](const TemplateTypeArg& template_arg) {
				auto lacksConcreteTypeIdentity = [](TypeIndex type_index) {
					if (!type_index.is_valid()) {
						if (TypeIndex native_index = nativeTypeIndex(type_index.category()); native_index.is_valid()) {
							return false;
						}
						return true;
					}
					if (typeIndexContainsDependentPlaceholder(type_index)) {
						return true;
					}
					return false;
				};
				if (template_arg.is_template_template_arg) {
					return !template_arg.template_name_handle.isValid();
				}
				const bool is_dependent = template_arg.is_dependent ||
					template_arg.dependent_name.isValid() ||
					template_arg.dependent_expr.has_value();
				if (is_dependent) {
					return true;
				}
				if (template_arg.is_value) {
					return false;
				}
				TypeIndex arg_type_index =
					template_arg.type_index.withCategory(template_arg.typeEnum());
				return lacksConcreteTypeIdentity(arg_type_index);
			});
	for (const auto& type_alias : class_decl.type_aliases()) {
		if (has_unresolved_primary_template_args) {
			continue;
		}
		auto qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder().append(instantiated_name).append("::"sv).append(type_alias.alias_name));

		// Get the aliased type and substitute template parameters
		const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();

		// Create a substituted type specifier
		TypeCategory substituted_type = alias_type_spec.type();
		TypeIndex substituted_type_index = alias_type_spec.type_index();
		int substituted_size = alias_type_spec.size_in_bits();

		trySubstituteIntrinsicTypeAlias(
			alias_type_spec,
			effective_template_params,
			effective_template_args,
			substituted_type,
			substituted_type_index,
			substituted_size);

		// Substitute template parameters in the alias type.
		// Alias targets like `using type = T;` are typically registered as
		// UserDefined/TypeAlias placeholders rather than concrete Struct types, so
		// they need the same substitution path as struct-like aliases here.
		if (is_struct_type(substituted_type) ||
			substituted_type == TypeCategory::UserDefined ||
			substituted_type == TypeCategory::TypeAlias ||
			substituted_type == TypeCategory::Template) {
			TypeIndex type_idx = alias_type_spec.type_index();
			if (const TypeInfo* type_info = tryGetTypeInfo(type_idx)) {
				std::string_view type_name = StringTable::getStringView(type_info->name());

				// Check for self-referential type alias (e.g., "using type = bool_constant;" inside bool_constant template)
				// When the template is instantiated (e.g., bool_constant_true), this should point to the instantiated type
				if (type_name == template_name) {
					// Self-referential type alias - point to the instantiated type
					auto inst_it = getTypesByNameMap().find(instantiated_name);
					if (inst_it != getTypesByNameMap().end()) {
						// Use the type_index_ field directly instead of pointer arithmetic
						// Pointer arithmetic on deque elements is undefined behavior
						TypeIndex inst_idx = inst_it->second->type_index_;
						substituted_type_index = inst_idx;
						FLASH_LOG(Templates, Debug, "Self-referential type alias '", StringTable::getStringView(type_alias.alias_name),
								  "' now points to instantiated type '", instantiated_name, "' (index ", inst_idx, ")");
					}
				} else {
					// Use substitute_template_parameter for consistent template parameter matching
					TypeIndex subst_type_index = substitute_template_parameter(
						alias_type_spec, effective_template_params, effective_template_args);

					// Only apply substitution if the type was actually a template parameter
					if (subst_type_index.category() != alias_type_spec.type() || subst_type_index != alias_type_spec.type_index()) {
						substituted_type = subst_type_index.category();
						substituted_type_index = subst_type_index;
						substituted_size = get_type_size_bits(substituted_type);
					}
				}
			}
		}

		const TypeInfo* alias_semantic_source =
			tryGetTypeInfo(TypeIndex{substituted_type_index});
		std::optional<TypeSpecifierNode> resolved_alias_type_spec_override;
		{
			ASTNode resolved_alias_type_node =
				emplace_node<TypeSpecifierNode>(alias_type_spec);
			if (resolveDependentMemberAlias(
					resolved_alias_type_node,
					effective_template_params,
					effective_template_args) == DependentAliasResolutionStatus::Resolved) {
				const TypeSpecifierNode& resolved_alias_type_spec =
					resolved_alias_type_node.as<TypeSpecifierNode>();
				substituted_type = resolved_alias_type_spec.type();
				substituted_type_index = resolved_alias_type_spec.type_index();
				substituted_size = resolved_alias_type_spec.size_in_bits();
				alias_semantic_source =
					tryGetTypeInfo(resolved_alias_type_spec.type_index());
				resolved_alias_type_spec_override = resolved_alias_type_spec;
			}
		}
		if (const TypeInfo* dependent_alias_target_info =
				tryGetTypeInfo(TypeIndex{substituted_type_index});
			dependent_alias_target_info != nullptr &&
			dependent_alias_target_info->isDependentMemberType() &&
			dependent_alias_target_info->hasDependentQualifiedName()) {
			if (const TypeInfo* resolved_dependent_type =
					resolveDependentMemberTypeSemantic(
						*dependent_alias_target_info,
						effective_template_params,
						effective_template_args,
						instantiated_name);
				resolved_dependent_type != nullptr) {
				substituted_type = resolved_dependent_type->typeEnum();
				substituted_type_index =
					resolved_dependent_type->registeredTypeIndex().withCategory(
						resolved_dependent_type->typeEnum());
				substituted_size = resolved_dependent_type->sizeInBits().value;
				alias_semantic_source = resolved_dependent_type;
			}
		}

		if (const TypeInfo* concrete_member_info =
				materializeInstantiatedMemberAliasTarget(
					alias_type_spec,
					effective_template_params,
					effective_template_args);
			concrete_member_info != nullptr) {
			substituted_type = concrete_member_info->typeEnum();
			substituted_type_index =
				concrete_member_info->registeredTypeIndex().withCategory(
					concrete_member_info->typeEnum());
			substituted_size = concrete_member_info->sizeInBits().value;
			alias_semantic_source = concrete_member_info;
		}
		if (!isConcreteAliasSemanticSource(alias_semantic_source)) {
			alias_semantic_source = nullptr;
		}

		// Ensure size is computed for primitive type aliases that were substituted from template parameters.
		// When 'typedef T value_type' in a template is instantiated with T=long, the alias type category
		// is correctly set to Long but size_in_bits may still be 0 from the unsubstituted pattern.
		if (substituted_size == 0 && is_primitive_type(substituted_type)) {
			substituted_size = get_type_size_bits(substituted_type);
		}
		// Also ensure the TypeIndex carries the correct category for primitive type aliases.
		// The substituted_type_index may still point at the template parameter placeholder entry
		// with an incorrect category, so stamp it with the resolved primitive category.
		if (is_primitive_type(substituted_type) && substituted_type_index.category() != substituted_type) {
			substituted_type_index = substituted_type_index.withCategory(substituted_type);
		}

		// Register the type alias in getTypesByNameMap()
		std::optional<TypeSpecifierNode> substituted_alias_type_spec = buildSubstitutedTypeAliasSpecifier(
			type_alias, TypeIndex{substituted_type_index}, substituted_type, effective_template_params, effective_template_args, instantiated_name);
		bool use_resolved_alias_override =
			resolved_alias_type_spec_override.has_value();
		if (use_resolved_alias_override) {
			const TypeSpecifierNode& override_spec =
				resolved_alias_type_spec_override.value();
			if (override_spec.type_index().is_valid() &&
				typeIndexContainsDependentPlaceholder(
					override_spec.type_index())) {
				use_resolved_alias_override = false;
			} else if (const TypeInfo* override_info =
						   tryGetTypeInfo(override_spec.type_index());
					   override_info != nullptr &&
					   (override_info->isTemplatePlaceholder() ||
						override_info->isDependentPlaceholder())) {
				use_resolved_alias_override = false;
			}
		}
		const TypeSpecifierNode& alias_registration_type_spec =
			use_resolved_alias_override
				? resolved_alias_type_spec_override.value()
				: (substituted_alias_type_spec.has_value()
					   ? substituted_alias_type_spec.value()
					   : alias_type_spec);
		FLASH_LOG(
			Parser,
			Debug,
			"Class alias register(primary): name='",
			StringTable::getStringView(qualified_alias_name),
			"', tmpl_args=",
			template_args_to_use.size(),
			"', substituted_type=",
			static_cast<int>(substituted_type),
			", substituted_index=",
			TypeIndex{substituted_type_index}.index(),
			", reg_spec_type=",
			static_cast<int>(alias_registration_type_spec.type()),
			", reg_spec_index=",
			alias_registration_type_spec.type_index().index());
		for (size_t alias_arg_index = 0; alias_arg_index < template_args_to_use.size(); ++alias_arg_index) {
			const TemplateTypeArg& alias_arg = template_args_to_use[alias_arg_index];
			const TypeInfo* alias_arg_info =
				(!alias_arg.is_value && alias_arg.type_index.is_valid())
					? tryGetTypeInfo(alias_arg.type_index)
					: nullptr;
			FLASH_LOG(
				Parser,
				Debug,
				"  primary_arg[",
				alias_arg_index,
				"]: is_value=",
				alias_arg.is_value ? 1 : 0,
				", is_dep=",
				alias_arg.is_dependent ? 1 : 0,
				", type=",
				static_cast<int>(alias_arg.typeEnum()),
				", type_name='",
				(alias_arg_info != nullptr
					 ? StringTable::getStringView(alias_arg_info->name())
					 : std::string_view()),
				"', ref=",
				static_cast<int>(alias_arg.ref_qualifier),
				", ptr=",
				static_cast<int>(alias_arg.pointer_depth));
		}
		auto& alias_type_info =
			alias_semantic_source != nullptr
				? add_type_alias_copy(
					  qualified_alias_name,
					  TypeIndex{substituted_type_index},
					  substituted_size,
					  alias_registration_type_spec,
					  *alias_semantic_source)
				: add_type_alias_copy(
					  qualified_alias_name,
					  TypeIndex{substituted_type_index},
					  substituted_size,
					  alias_registration_type_spec);
		if (alias_registration_type_spec.is_array()) {
			std::span<const size_t> alias_array_dimensions = alias_registration_type_spec.array_dimensions();
			size_t element_size = calculateResolvedMemberSizeAndAlignment(alias_registration_type_spec, substituted_type_index).size;
			substituted_size = static_cast<int>(element_size * std::accumulate(
				alias_array_dimensions.begin(),
				alias_array_dimensions.end(),
				size_t{1},
				std::multiplies<size_t>()) * 8);
			alias_type_info.fallback_size_bits_ = substituted_size;
		}
		if (substituted_type == TypeCategory::Enum && substituted_type_index.is_valid()) {
			if (const TypeInfo* enum_alias_ti = tryGetTypeInfo(substituted_type_index)) {
				if (const EnumTypeInfo* enum_info = enum_alias_ti->getEnumInfo()) {
					alias_type_info.setEnumInfo(std::make_unique<EnumTypeInfo>(*enum_info));
				}
			}
		}
		// Use insert_or_assign so that a stale placeholder entry (e.g., from a
		// prior partial instantiation that pointed at TTT$hash) is overwritten
		// with the concrete type (e.g., MakeMid$hash).
		getTypesByNameMap().insert_or_assign(qualified_alias_name, &alias_type_info);
		FLASH_LOG_FORMAT(Templates, Debug, "Registered type alias '{}' -> type={}, type_index={}",
						 StringTable::getStringView(qualified_alias_name), static_cast<int>(substituted_type), substituted_type_index);

		// If this alias refers to an unscoped enum, track its TypeIndex so that
		// Struct::Enumerator qualified access works in codegen.
		if (substituted_type == TypeCategory::Enum && substituted_type_index.is_valid()) {
			if (const TypeInfo* enum_nested_ti = tryGetTypeInfo(substituted_type_index)) {
				const EnumTypeInfo* enum_info = enum_nested_ti->getEnumInfo();
				if (enum_info && !enum_info->is_scoped) {
					struct_info->addNestedEnumIndex(substituted_type_index);
				}
			}
		}
	}

	auto initializer_contains_qualified_identifier =
		[&](const std::optional<ASTNode>& initializer) {
			if (!initializer.has_value()) {
				return false;
			}
			if (initializer->is<ExpressionNode>() &&
				std::holds_alternative<QualifiedIdentifierNode>(
					initializer->as<ExpressionNode>())) {
				return true;
			}
			return RebindStaticMemberAst::visitASTUntil(
				*initializer,
				[&](const ASTNode& node) {
					if (!node.is<ExpressionNode>()) {
						return false;
					}
					const ExpressionNode& expr = node.as<ExpressionNode>();
					const auto* qualified_id = std::get_if<QualifiedIdentifierNode>(&expr);
					if (qualified_id == nullptr) {
						return false;
					}
					return true;
				});
		};

	auto retry_static_member_normalization_from_pattern_initializers =
		[&](const auto& pattern_static_members) {
			for (const auto& pattern_static_member : pattern_static_members) {
				StructStaticMember* instantiated_static_member =
					struct_info->findStaticMember(pattern_static_member.name);
				if (instantiated_static_member == nullptr ||
					instantiated_static_member->normalized_init.has_value() ||
					!initializer_contains_qualified_identifier(pattern_static_member.initializer)) {
					continue;
				}

				std::optional<ASTNode> retry_initializer =
					substitute_in_class_static_initializer_replay_first(
						pattern_static_member,
						effective_template_params,
						std::span<const TemplateTypeArg>(
							effective_template_args_vector.data(),
							effective_template_args_vector.size()),
						instantiated_name,
						struct_info->getNamespaceHandle(),
						effective_template_args);
				std::optional<NormalizedInitializer> normalized_initializer =
					tryEarlyNormalizeTemplateStaticMemberInitializer(
						retry_initializer,
						gSymbolTable,
						*this,
						struct_info.get(),
						effective_template_params,
						effective_template_args_vector,
						instantiated_static_member->type_index,
						instantiated_static_member->size,
						instantiated_static_member->reference_qualifier,
						instantiated_static_member->pointer_depth);
				if (!normalized_initializer.has_value()) {
					continue;
				}

				instantiated_static_member->initializer = std::move(retry_initializer);
				instantiated_static_member->normalized_init = std::move(normalized_initializer);
				FLASH_LOG(Templates, Debug, "Retry-normalized static member initializer after alias registration: ",
						  StringTable::getStringView(pattern_static_member.name));
			}
		};
	if (template_struct_info && !template_struct_info->static_members.empty()) {
		retry_static_member_normalization_from_pattern_initializers(
			template_struct_info->static_members);
	} else if (!class_decl.static_members().empty()) {
		retry_static_member_normalization_from_pattern_initializers(
			class_decl.static_members());
	}

	auto remapInstantiatedNestedTypeIndex = [&](TypeIndex original_type_index) -> TypeIndex {
		if (!original_type_index.is_valid()) {
			return original_type_index;
		}
		const TypeInfo* original_type_info = tryGetTypeInfo(original_type_index);
		if (original_type_info == nullptr) {
			return original_type_index;
		}

		std::string_view original_name = StringTable::getStringView(original_type_info->name());
		std::string_view suffix;
		if (original_name.starts_with(template_name)) {
			size_t qualified_sep = original_name.find("::", template_name.size());
			if (qualified_sep != std::string_view::npos &&
				(qualified_sep == template_name.size() || original_name[template_name.size()] == '$')) {
				suffix = original_name.substr(qualified_sep);
			}
		}
		if (suffix.empty()) {
			return original_type_index;
		}

		StringHandle remapped_handle = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(StringTable::getStringView(instantiated_name))
				.append(suffix)
				.commit());
		auto remapped_it = getTypesByNameMap().find(remapped_handle);
		if (remapped_it == getTypesByNameMap().end() || remapped_it->second == nullptr) {
			return original_type_index;
		}
		const TypeInfo* remapped_type_info = remapped_it->second;
		return remapped_type_info->registeredTypeIndex().withCategory(remapped_type_info->typeEnum());
	};

	auto fixupNestedTypeInSpecifier = [&](TypeSpecifierNode& type_spec) {
		TypeIndex remapped_index = remapInstantiatedNestedTypeIndex(type_spec.type_index());
		if (remapped_index == type_spec.type_index()) {
			return;
		}
		type_spec.set_type_index(remapped_index);
		if (type_spec.is_pointer() || type_spec.is_function_pointer() ||
			type_spec.is_member_function_pointer() || type_spec.is_member_object_pointer() ||
			type_spec.is_reference() || type_spec.is_rvalue_reference()) {
			type_spec.set_size_in_bits(64);
			return;
		}
		if (const TypeInfo* remapped_info = tryGetTypeInfo(remapped_index)) {
			if (remapped_info->hasStoredSize()) {
				type_spec.set_size_in_bits(static_cast<int>(remapped_info->sizeInBits().value));
				return;
			}
		}
		type_spec.set_size_in_bits(get_type_size_bits(remapped_index.category()));
	};

	// Phase C fixup: update member-function signatures that still refer to
	// template-pattern nested types (e.g., Wrapper::Nested or Wrapper$hash::Nested)
	// after nested classes were materialized as InstantiatedName::Nested.
	for (auto& mem_func : struct_info->member_functions) {
		if (!mem_func.function_decl.is<FunctionDeclarationNode>()) {
			continue;
		}
		FunctionDeclarationNode& func_decl = mem_func.function_decl.as<FunctionDeclarationNode>();
		fixupNestedTypeInSpecifier(func_decl.decl_node().type_specifier_node());
		for (ASTNode& param_node : func_decl.parameter_nodes()) {
			if (param_node.is<DeclarationNode>()) {
				fixupNestedTypeInSpecifier(param_node.as<DeclarationNode>().type_specifier_node());
			}
		}
	}

	// Finalize the struct layout
	// Phase C fixup: update static members whose type references a nested class that
 // was registered after the static members were initially copied from the pattern.
 // At copy time (line ~1686), nested types hadn't been instantiated yet, so their
 // type_index and size were still from the unfinalized pattern.
	for (auto& sm : struct_info->static_members) {
		if (sm.size == 0 && sm.type_index.category() == TypeCategory::Struct) {
			TypeIndex remapped_index = remapInstantiatedNestedTypeIndex(sm.type_index);
			if (remapped_index != sm.type_index) {
				sm.type_index = remapped_index;
				if (const StructTypeInfo* nested_si = tryGetStructTypeInfo(remapped_index)) {
					sm.size = toSizeT(nested_si->sizeInBytes());
				}
			}
		}
	}

	bool finalize_success;
	if (!struct_info->base_classes.empty()) {
		finalize_success = struct_info->finalizeWithBases();
	} else {
		finalize_success = struct_info->finalize();
	}

	// Check for semantic errors during finalization
	if (!finalize_success) {
		// Log error and return nullopt - compilation will continue but template instantiation fails
		FLASH_LOG(Parser, Error, struct_info->getFinalizationError());
		return std::nullopt;
	}

	// Store struct info in type info
	struct_type_info.setStructInfo(std::move(struct_info));

	// Update fallback_size_bits_ from the finalized struct's total size
	if (struct_type_info.getStructInfo()) {
		struct_type_info.fallback_size_bits_ = struct_type_info.getStructInfo()->sizeInBits().value;
	}

	// Register member template aliases with the instantiated name
	// Member template aliases were registered during parsing with the primary template name (e.g., "__conditional::type")
	// We need to re-register them with the instantiated name (e.g., "__conditional_1::type")
	// This allows lookups like __conditional<true>::type<Args> to work correctly
	{
		// Build the template prefix string (e.g., "__conditional::")
		StringBuilder prefix_builder;
		std::string_view template_prefix = prefix_builder.append(template_name).append("::").preview();

		// Get all alias templates from the registry with this prefix
		std::vector<std::string_view> base_aliases_to_copy = gTemplateRegistry.get_alias_templates_with_prefix(template_prefix);
		prefix_builder.reset();

		// Now register each one with the instantiated name
		OuterTemplateBinding alias_outer_binding;
		collectOuterTemplateBinding(template_params, template_args_to_use, alias_outer_binding);
		for (const auto& base_alias_name : base_aliases_to_copy) {
			// Extract the member name (everything after "template_name::")
			std::string_view member_name = std::string_view(base_alias_name).substr(template_prefix.size());

			// Build the new qualified name with the instantiated struct name
			StringHandle inst_alias_name = StringTable::getOrInternStringHandle(
				StringBuilder()
					.append(instantiated_name)
					.append("::")
					.append(member_name)
					.commit());

			// Look up the original alias node
			auto alias_opt = gTemplateRegistry.lookup_alias_template(base_alias_name);
			if (alias_opt.has_value()) {
				// Re-register with the instantiated name
				registerAliasTemplateWithOuterBinding(
					inst_alias_name,
					*alias_opt,
					&alias_outer_binding);
				// Also register the outer binding for the un-hashed base alias name
				// so that lookups by base template name (e.g. "Provider::Node::Apply"
				// as stored in TypeInfo::baseTemplateName()) can find the outer binding
				// (e.g. T=int) even when the alias TypeInfo records the un-hashed form.
				// Only do this when all outer params are concrete (non-dependent); a
				// dependent-param instantiation (e.g. Provider<T>) must not overwrite
				// a concrete binding that was registered earlier.
				const bool all_concrete_args = !anyTemplateArgIsStructurallyDependent(
					std::span<const TemplateTypeArg>(
						alias_outer_binding.param_args.begin(),
						alias_outer_binding.param_args.end()));
				if (all_concrete_args) {
					StringHandle base_alias_handle =
						StringTable::getOrInternStringHandle(base_alias_name);
					// Don't overwrite an already-registered concrete binding: the first
					// concrete registration (from the intended instantiation, e.g.
					// Provider<int> from Outer<int>) wins over later spurious ones.
					if (gTemplateRegistry.getOuterTemplateBinding(base_alias_handle) == nullptr) {
						gTemplateRegistry.registerOuterTemplateBinding(
							base_alias_handle,
							alias_outer_binding);
					}
				}
			}
		}
	}

	// Get a pointer to the moved struct_info for later use
	StructTypeInfo* struct_info_ptr = struct_type_info.getStructInfo();

	// Create an AST node for the instantiated struct so member declarations and
	// member functions can be sema-normalized/code-generated.
	auto instantiated_struct = emplace_node<StructDeclarationNode>(
		instantiated_name,
		false // is_class
	);
	StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
	setOuterTemplateBindingsFromParams(instantiated_struct_ref, template_params, template_args_to_use);

	for (const auto& member_decl : class_decl.members()) {
		ASTNode substituted_member_decl = substituteTemplateParameters(
			member_decl.declaration, template_params, template_args_to_use);
		std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
			member_decl.default_initializer, template_args_to_use, template_params);
		std::optional<ASTNode> substituted_bitfield_width_expr = member_decl.bitfield_width_expr.has_value()
																	 ? std::optional<ASTNode>(substituteTemplateParameters(
																		   *member_decl.bitfield_width_expr, template_params, template_args_to_use))
																	 : std::nullopt;
		instantiated_struct_ref.add_member(
			substituted_member_decl,
			member_decl.access,
			substituted_default_initializer,
			member_decl.bitfield_width,
			substituted_bitfield_width_expr,
			member_decl.is_no_unique_address);
	}
	for (const auto& static_member : struct_info_ptr->static_members) {
		instantiated_struct_ref.addStaticMember(
			static_member.name,
			static_member.type_index,
			static_member.size,
			static_member.alignment,
			static_member.access,
			static_member.initializer,
			static_member.cv_qualifier,
			static_member.reference_qualifier,
			static_member.pointer_depth,
			static_member.is_array,
			static_member.array_dimensions,
			static_member.declaration,
			static_member.initializer_position,
			static_member.initializerDefinitionLookupContext(),
			static_member.is_constexpr);
	}

	for (auto& nested_class_node : instantiated_nested_class_nodes) {
		if (nested_class_node.is<StructDeclarationNode>()) {
			nested_class_node.as<StructDeclarationNode>().set_enclosing_class(&instantiated_struct_ref);
		}
		instantiated_struct_ref.add_nested_class(nested_class_node);
	}

	std::vector<StructMemberFunctionDecl> effective_member_functions;
	bool effective_has_constructor = false;
	for (const StructMemberFunctionDecl& member_function : class_decl.member_functions()) {
		effective_member_functions.push_back(member_function);
		effective_has_constructor = effective_has_constructor || member_function.is_constructor;
	}
	if (effective_member_functions.empty() &&
		template_struct_info != nullptr &&
		!template_struct_info->member_functions.empty()) {
		effective_member_functions.reserve(template_struct_info->member_functions.size());
		for (const StructMemberFunction& member_function : template_struct_info->member_functions) {
			StructMemberFunctionDecl member_function_decl(
				member_function.function_decl,
				member_function.access,
				member_function.is_constructor,
				member_function.is_destructor,
				member_function.operator_kind);
			member_function_decl.is_virtual = member_function.is_virtual;
			member_function_decl.is_pure_virtual = member_function.is_pure_virtual;
			member_function_decl.is_override = member_function.is_override;
			member_function_decl.is_final = member_function.is_final;
			member_function_decl.cv_qualifier = member_function.cv_qualifier;
			member_function_decl.is_noexcept = member_function.is_noexcept;
			effective_member_functions.push_back(member_function_decl);
			effective_has_constructor = effective_has_constructor || member_function_decl.is_constructor;
		}
	}

	// Log lazy instantiation status (already determined earlier in the function)
	if (is_implicit_instantiation) {
		FLASH_LOG(Templates, Debug, "Using LAZY instantiation for ", instantiated_name, " - registering ",
				  effective_member_functions.size(), " member functions for on-demand instantiation");
	} else if (force_eager) {
		FLASH_LOG(Templates, Debug, "Using EAGER instantiation for ", instantiated_name, " (forced by explicit instantiation) - instantiating ",
				  effective_member_functions.size(), " member functions immediately");
	}

	// Slice 3: map from original template member node (by raw pointer) to the instantiated stub node.
	// Used by deferred-body replay and nested out-of-line attachment to avoid name-based scanning.
	SourceMemberIdentityMaps source_member_identity_maps;
	TypeIndex instantiated_member_owner_type_index{};
	if (auto instantiated_owner_it = getTypesByNameMap().find(instantiated_name);
		instantiated_owner_it != getTypesByNameMap().end()) {
		instantiated_member_owner_type_index = instantiated_owner_it->second->type_index_;
	}

	auto remapInstantiatedNestedTypeInSpec = [&](TypeSpecifierNode& type_spec) {
		if (!type_spec.type_index().is_valid()) {
			return;
		}
		const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index());
		if (type_info == nullptr) {
			return;
		}
		std::string_view type_name_sv = StringTable::getStringView(type_info->name());
		std::string_view nested_suffix;
		if (type_name_sv.starts_with(template_name)) {
			size_t qualified_sep = type_name_sv.find("::", template_name.size());
			if (qualified_sep != std::string_view::npos &&
				(qualified_sep == template_name.size() || type_name_sv[template_name.size()] == '$')) {
				nested_suffix = type_name_sv.substr(qualified_sep);
			}
		}
		if (nested_suffix.empty()) {
			return;
		}
		StringHandle remapped_name = StringTable::getOrInternStringHandle(
			StringBuilder()
				.append(StringTable::getStringView(instantiated_name))
				.append(nested_suffix)
				.commit());
		auto remapped_it = getTypesByNameMap().find(remapped_name);
		if (remapped_it == getTypesByNameMap().end() || remapped_it->second == nullptr) {
			return;
		}
		const TypeInfo* remapped_info = remapped_it->second;
		type_spec.set_type_index(remapped_info->registeredTypeIndex().withCategory(remapped_info->typeEnum()));
		if (!(type_spec.is_pointer() || type_spec.is_function_pointer() ||
			  type_spec.is_member_function_pointer() || type_spec.is_member_object_pointer() ||
			  type_spec.is_reference() || type_spec.is_rvalue_reference())) {
			if (remapped_info->hasStoredSize()) {
				type_spec.set_size_in_bits(static_cast<int>(remapped_info->sizeInBits().value));
			}
		}
	};

	// Copy member functions from the template
	for (const StructMemberFunctionDecl& mem_func : effective_member_functions) {

		if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = mem_func.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func_decl.decl_node();

			// For lazy instantiation, register function for later instantiation instead of instantiating now.
			// Namespaced implicit instantiations intentionally use the same stub path; the
			// PHASE 2 deferred-body replay block later in this function decides whether their
			// deferred bodies should be materialized immediately.
			if (is_implicit_instantiation &&
				func_decl.has_any_body_source()) {
				// Register this member function for lazy instantiation
				LazyMemberFunctionInfo lazy_info;
				{
					auto& id = lazy_info.identity;
					id.original_member_node = mem_func.function_declaration;
					id.template_owner_name = StringTable::getOrInternStringHandle(template_name);
					id.instantiated_owner_name = instantiated_name;
					id.original_lookup_name = decl.identifier_token().handle();
					id.operator_kind = mem_func.operator_kind;
					id.is_operator = mem_func.operator_kind != OverloadableOperator::None;
					id.is_const_method = mem_func.is_const();
					id.cv_qualifier = mem_func.cv_qualifier;
					id.kind = DeferredMemberIdentity::Kind::Function;
				}
				lazy_info.template_params = effective_template_params;
				lazy_info.template_args.assign(
					effective_template_args.begin(),
					effective_template_args.end());
				const TemplateEnvironmentSnapshot* outer_parent_snapshot =
					instantiated_struct_ref.has_outer_template_bindings()
						? &instantiated_struct_ref.outer_template_environment_snapshot()
						: nullptr;
				lazy_info.outer_template_environment_snapshot = buildTemplateEnvironmentSnapshotFromBindings(
					effective_template_params,
					effective_template_args,
					outer_parent_snapshot);
				mergeMissingLazyOuterBindings(
					lazy_info.template_params,
					lazy_info.template_args,
					lazy_info.outer_template_environment_snapshot);
				lazy_info.access = mem_func.access;
				lazy_info.is_virtual = mem_func.is_virtual;
				lazy_info.is_pure_virtual = mem_func.is_pure_virtual;
				lazy_info.is_override = mem_func.is_override;
				lazy_info.is_final = mem_func.is_final;

				// Create function declaration with signature but WITHOUT body
				// This allows the function to be found during name lookup, but defers code generation

				SubstitutedMemberFunctionShell shell = createSubstitutedMemberFunctionShell(
					func_decl,
					decl.type_node(),
					decl.identifier_token(),
					instantiated_name,
					effective_template_params,
					effective_template_args,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					mem_func.operator_kind,
					StringHandle{},
					StringHandle{},
					false, // Do not re-apply bound metadata to full substitution
					true); // Force resolved TypeIndex onto full AST substitutions

				// Slice 2: fill canonical instantiated lookup name (conversion operator renaming)
				lazy_info.identity.instantiated_lookup_name = shell.effective_name;

				StringHandle lazy_registry_key{};

				if (shouldCommitTemplateInstantiationArtifacts()) {
					lazy_registry_key =
						LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_info));
				}

				FLASH_LOG(Templates, Debug, "Registered lazy member function: ",
						  instantiated_name, "::", decl.identifier_token().value());

				ASTNode new_func_node = shell.function_node;
				FunctionDeclarationNode& new_func_ref = *shell.function;
				remapInstantiatedNestedTypeInSpec(new_func_ref.decl_node().type_specifier_node());
				if (lazy_registry_key.isValid()) {
					new_func_ref.set_lazy_member_registry_key(lazy_registry_key);
				}
				if (func_decl.has_definition_lookup_context()) {
					new_func_ref.set_definition_lookup_context(func_decl.definition_lookup_context());
				}
				size_t saved_pack_info = pack_param_info_.size();
				substituteAndCopyMemberFunctionParameters(
					func_decl.parameter_nodes(),
					new_func_ref,
					effective_template_params,
					effective_template_args,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					TypeIndex{},
					SubstitutedDefaultArgumentPolicy::ExpressionSubstitutor,
					true,  // Preserve declared parameter cv-qualifier in this path
					false, // Do not re-apply bound metadata to full substitution
					true,  // Force resolved TypeIndex onto full AST substitutions
					false); // Plain member path does not need dependent member-template signature preservation
				pack_param_info_.resize(saved_pack_info);

				// Copy function properties but DO NOT set definition. Delay mangling until
				// a body/finalized signature exists to avoid caching stale self-type encodings.
				copy_function_properties(new_func_ref, func_decl);
				new_func_ref.set_is_const_member_function(mem_func.is_const());
				new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());

				// Add the signature-only function to the instantiated struct
				if (mem_func.operator_kind != OverloadableOperator::None) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}

				// Also add to struct_info so it can be found during codegen
				if (mem_func.operator_kind != OverloadableOperator::None) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_kind, new_func_node, mem_func.access,
														 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					StringHandle func_name_handle = shell.effective_name;
					struct_info_ptr->addMemberFunction(
						func_name_handle,
						new_func_node,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				}
				// cv_qualifier and is_noexcept are now auto-derived by propagateAstProperties

				// Slice 3: record lazy-path stub for identity-map lookup during deferred-body replay.
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_func_node);

				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(instantiated_name))
					.append("::")
					.append(shell.effective_name);
				StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
				OuterTemplateBinding lazy_member_outer_binding;
				collectOuterTemplateBinding(effective_template_params, effective_template_args, lazy_member_outer_binding);
				if (shouldCommitTemplateInstantiationArtifacts()) {
					gTemplateRegistry.registerOuterTemplateBinding(qualified_name_handle, std::move(lazy_member_outer_binding));
				}

				// Skip to next function - body will be instantiated on-demand
				continue;
			}

			// EAGER INSTANTIATION PATH (original code)
			// If the function has a definition or deferred body, we need to substitute template parameters
			if (func_decl.has_any_body_source()) {
				SubstitutedMemberFunctionShell shell = createSubstitutedMemberFunctionShell(
					func_decl,
					decl.type_node(),
					decl.identifier_token(),
					instantiated_name,
					effective_template_params,
					effective_template_args,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					mem_func.operator_kind,
					StringHandle{},
					StringHandle{},
					false, // Do not re-apply bound metadata to full substitution
					true); // Force resolved TypeIndex onto full AST substitutions
				ASTNode new_func_node = shell.function_node;
				FunctionDeclarationNode& new_func_ref = *shell.function;
				StringHandle effective_name = shell.effective_name;
				remapInstantiatedNestedTypeInSpec(new_func_ref.decl_node().type_specifier_node());

				size_t saved_pack_info = pack_param_info_.size();
				substituteAndCopyMemberFunctionParameters(
					func_decl.parameter_nodes(),
					new_func_ref,
					effective_template_params,
					effective_template_args,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					TypeIndex{},
					SubstitutedDefaultArgumentPolicy::ExpressionSubstitutor,
					true,  // Preserve declared parameter cv-qualifier in this path
					false, // Do not re-apply bound metadata to full substitution
					true,  // Force resolved TypeIndex onto full AST substitutions
					false); // Plain member path does not need dependent member-template signature preservation

				// Get the function body - either from definition or by re-parsing from saved position
				std::optional<ASTNode> body_to_substitute;

				if (func_decl.is_materialized()) {
					// Use the already-parsed definition
					FLASH_LOG(Templates, Debug, "Function has definition, using parsed body");
					body_to_substitute = func_decl.get_definition();
				} else if (func_decl.has_template_body_position()) {
					// Re-parse the function body from saved position
					// This is needed for member struct templates where body parsing is deferred
					FLASH_LOG(Templates, Debug, "Function has template body position, re-parsing");

					// Set up template parameter types in the type system for body parsing
					FlashCpp::TemplateParameterScope template_scope;
					InlineVector<StringHandle, 4> param_names;
					param_names.reserve(effective_template_params.size());
					for (const auto& tparam_node : effective_template_params) {
						if (const TemplateParameterNode* tparam = tryGetTemplateParameterNode(tparam_node);
							tparam != nullptr) {
							param_names.push_back(tparam->nameHandle());
						}
					}

					registerTypeParamsInScope(param_names, effective_template_args, template_scope, true);

					// Save current position and parsing context
					SaveHandle current_pos = save_token_position();

					// Restore to the function body start
					restore_lexer_position_only(func_decl.template_body_position());

					// Use FunctionParsingScopeGuard for full member-function context:
					// scope, current_function_, member context push, 'this' injection,
					// and parameter registration — matching the normal delayed-body path.
					{
						FlashCpp::FunctionParsingScopeGuard func_guard(
							*this,
							true,
							!func_decl.is_static(),
							&instantiated_struct_ref,
							instantiated_name,
							struct_type_info.type_index_,
							new_func_ref.parameter_nodes(),
							&new_func_ref);

						// Parse the function body (handles function-try-blocks too)
						auto block_result = parse_function_body();

						if (!block_result.is_error() && block_result.node().has_value()) {
							body_to_substitute = block_result.node();
						}
					} // func_guard dtor: pops member ctx, restores current_function_, exits scope

					// Restore original position
					restore_lexer_position_only(current_pos);
					discard_saved_token(current_pos);
				}

				// Substitute template parameters in the function body
				if (body_to_substitute.has_value()) {
					FLASH_LOG(Templates, Debug, "About to substitute template parameters in function body for struct: ", StringTable::getStringView(instantiated_name));

					// Push struct parsing context so that get_class_template_pack_size can find pack info in the registry
					// This is needed for sizeof...(Pack) to work in eager member function body substitution
					StructParsingContext struct_ctx;
					struct_ctx.struct_name = StringTable::getStringView(instantiated_name);
					struct_ctx.struct_node = nullptr;
					struct_ctx.local_struct_info = nullptr;
					struct_parsing_context_stack_.push_back(struct_ctx);

					FLASH_LOG(Templates, Debug, "Pushed struct context: ", struct_ctx.struct_name);

					try {
						ASTNode substituted_body = substituteTemplateParameters(
							*body_to_substitute,
							effective_template_params,
							effective_template_args);
						if (force_eager) {
							if (std::optional<StringHandle> unresolved_type =
									findUnresolvedHardUseTypeSpecifier(substituted_body);
								unresolved_type.has_value()) {
								throw CompileError(std::string(StringBuilder()
									.append("Explicit instantiation left unresolved dependent type '")
									.append(unresolved_type->isValid()
										? StringTable::getStringView(*unresolved_type)
										: std::string_view("<unknown>"))
									.append("' in member function '")
									.append(decl.identifier_token().value())
									.append("'")
									.commit()));
							}
						}
						new_func_ref.set_definition(substituted_body);
						FLASH_LOG(Templates, Debug, "Successfully substituted function body");
					} catch (const std::exception& e) {
						struct_parsing_context_stack_.pop_back(); // Clean up on error
						FLASH_LOG(Templates, Error, "Exception during template parameter substitution for function ",
								  decl.identifier_token().value(), ": ", e.what());
						throw;
					} catch (...) {
						struct_parsing_context_stack_.pop_back(); // Clean up on error
						FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for function ",
								  decl.identifier_token().value());
						throw;
					}

					// Pop struct parsing context
					struct_parsing_context_stack_.pop_back();
					FLASH_LOG(Templates, Debug, "Popped struct context");
				}

				// Copy function specifiers from original
				copy_function_properties(new_func_ref, func_decl);
				new_func_ref.set_is_const_member_function(mem_func.is_const());
				new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
				if (new_func_ref.is_materialized()) {
					finalize_function_after_definition(new_func_ref);
				}
				pack_param_info_.resize(saved_pack_info);

				// Add the substituted function to the instantiated struct
				if (mem_func.operator_kind != OverloadableOperator::None) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}

				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				if (mem_func.operator_kind != OverloadableOperator::None) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_kind, new_func_node, mem_func.access,
														 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					FLASH_LOG(Templates, Debug, "Adding member function '", StringTable::getStringView(effective_name),
							  "' to struct_info for ", instantiated_name, ", parent_struct_name='", new_func_ref.parent_struct_name(), "'");
					struct_info_ptr->addMemberFunction(
						effective_name,
						new_func_node,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				}
				// cv_qualifier and is_noexcept are now auto-derived by propagateAstProperties

				// Slice 3: record eager-with-definition stub for identity-map lookup.
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_func_node);
			} else {
				// No definition, but still need to substitute parameter types and return type

				SubstitutedMemberFunctionShell shell = createSubstitutedMemberFunctionShell(
					func_decl,
					decl.type_node(),
					decl.identifier_token(),
					instantiated_name,
					template_params,
					template_args_to_use,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					mem_func.operator_kind,
					StringHandle{},
					StringHandle{},
					false, // Do not re-apply bound metadata to full substitution
					true); // Force resolved TypeIndex onto full AST substitutions
				ASTNode new_func_node = shell.function_node;
				FunctionDeclarationNode& new_func_ref = *shell.function;
				StringHandle effective_name = shell.effective_name;
				remapInstantiatedNestedTypeInSpec(new_func_ref.decl_node().type_specifier_node());

				size_t saved_pack_info = pack_param_info_.size();
				substituteAndCopyMemberFunctionParameters(
					func_decl.parameter_nodes(),
					new_func_ref,
					template_params,
					template_args_to_use,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					TypeIndex{},
					SubstitutedDefaultArgumentPolicy::None,
					false, // Declaration-only path: strip only top-level by-value cv-qualifiers
					false, // Do not re-apply bound metadata to full substitution
					true,  // Force resolved TypeIndex onto full AST substitutions
					false); // Plain member path does not need dependent member-template signature preservation
				pack_param_info_.resize(saved_pack_info);

				// Copy other function properties
				copy_function_properties(new_func_ref, func_decl);
				new_func_ref.set_is_const_member_function(mem_func.is_const());
				new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
				if (new_func_ref.is_materialized()) {
					finalize_function_after_definition(new_func_ref);
				}

				// Resolve auto trailing return type for declaration-only member functions.
				// Functions with bodies get their return type resolved via deduce_and_update_auto_return_type,
				// but declaration-only functions never execute that path. Re-parse the trailing return type
				// with concrete template arguments so decltype(StructTemplate<T>::func()) resolves correctly.
				if (new_func_ref.has_trailing_return_type_position() &&
					isPlaceholderAutoType(new_func_ref.decl_node().type_specifier_node().type())) {
					FlashCpp::ScopedState guard_ptb(parsing_template_depth_);
					FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
					FlashCpp::ScopedState guard_sfinae_map(sfinae_type_map_);
					ScopedParserInstantiationContext guard_inst_mode(*this, TemplateInstantiationMode::SoftProbe, StringHandle{});
					parsing_template_depth_ = 0;
					clearCurrentTemplateParameters();
					sfinae_type_map_.clear();

					SaveHandle saved_pos = save_token_position();
					auto restore_lexer = ScopeGuard([&]() {
						restore_lexer_position_only(saved_pos);
						discard_saved_token(saved_pos);
					});
					restore_lexer_position_only(new_func_ref.trailing_return_type_position());
					advance();  // consume '->'

					FlashCpp::FunctionParsingScopeGuard func_guard(
						*this,
						true,
						!new_func_ref.is_static(),
						&instantiated_struct_ref,
						instantiated_name,
						struct_type_info.type_index_,
						new_func_ref.parameter_nodes(),
						&new_func_ref);

					FlashCpp::TemplateParameterScope template_scope;
					registerTypeParamsInScope(template_params, template_args_to_use, template_scope, &sfinae_type_map_);

					auto return_type_result = parse_type_specifier();

					if (!return_type_result.is_error() &&
						return_type_result.node().has_value() &&
						return_type_result.node()->is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& resolved_type = return_type_result.node()->as<TypeSpecifierNode>();
						new_func_ref.decl_node().set_type_node(resolved_type);
						FLASH_LOG_FORMAT(Templates, Debug,
							"Resolved auto trailing return type for declaration-only member function '{}'",
							new_func_ref.decl_node().identifier_token().value());
					}
				}

				// Add the substituted function to the instantiated struct
				if (mem_func.operator_kind != OverloadableOperator::None) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}

				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				if (mem_func.operator_kind != OverloadableOperator::None) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_kind, new_func_node, mem_func.access,
														 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					struct_info_ptr->addMemberFunction(
						effective_name,
						new_func_node,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				}
				// cv_qualifier and is_noexcept are now auto-derived by propagateAstProperties

				// Slice 3: record no-definition stub for identity-map lookup.
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_func_node);
			}
		} else if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& ctor_decl = mem_func.function_declaration.as<ConstructorDeclarationNode>();

			// NOTE: Constructors are ALWAYS eagerly instantiated (not lazy)
			// because they're needed for object creation

			// EAGER INSTANTIATION PATH (original code)
			if (ctor_decl.has_any_body_source() || ctor_decl.has_template_parameters()) {
				try {
					// Create a new constructor declaration with substituted body
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name,
						instantiated_name);
					setOuterTemplateBindingsFromParams(new_ctor_ref, template_params, template_args_to_use);
					new_ctor_ref.set_template_parameters(ctor_decl.template_parameters());
					if (ctor_decl.has_template_body_position()) {
						new_ctor_ref.set_template_body_position(ctor_decl.template_body_position());
						if (ctor_decl.has_template_initializer_list_position()) {
							new_ctor_ref.set_template_initializer_list_position(ctor_decl.template_initializer_list_position());
						}
					}

					// Ensure template_param_order is populated (used by ExpressionSubstitutor later)
					if (template_param_order.empty()) {
						for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
							if (const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
								tparam != nullptr) {
								template_param_order.push_back(tparam->name());
							}
						}
					}

					// Substitute and copy parameters — may add function-pack info to pack_param_info_
					// so that pack expansions in the body and initializers resolve correctly.
					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyParams(ctor_decl.parameter_nodes(), new_ctor_ref, template_params, template_args_to_use);

					std::optional<ASTNode> substituted_body;
					if (ctor_decl.is_materialized()) {
						substituted_body = substituteTemplateParameters(
							*ctor_decl.get_definition(),
							template_params,
							template_args_to_use);
					}

					// Copy all initializers (member, base, delegating) with template parameter substitution
					substituteAndCopyInitializers(ctor_decl, new_ctor_ref, template_params, template_args_to_use);
					new_ctor_ref.set_is_implicit(ctor_decl.is_implicit());
					new_ctor_ref.set_is_explicitly_defaulted(ctor_decl.is_explicitly_defaulted());
					new_ctor_ref.set_noexcept(ctor_decl.is_noexcept());
					if (substituted_body.has_value()) {
						new_ctor_ref.set_definition(*substituted_body);
					}
					pack_param_info_.resize(saved_pack_info);

					// Add the substituted constructor to the instantiated struct AST node
					instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);

					// Also add to struct_info so it can be found during codegen
					struct_info_ptr->addConstructor(new_ctor_node, mem_func.access);

					// Slice 3: record constructor-with-definition stub.
					registerSourceMemberStubIdentity(
						source_member_identity_maps,
						mem_func.function_declaration,
						new_ctor_node);

					// Register lazy constructor stubs with deferred bodies in the lazy registry
					// for on-demand materialization when odr-used. The stub carries
					// template_body_position (and template_initializer_list_position for ctors
					// with member-initializers) propagated from the original above.
					if (is_implicit_instantiation &&
						!ctor_decl.is_materialized() &&
						shouldCommitTemplateInstantiationArtifacts()) {
						const TemplateEnvironmentSnapshot* outer_parent_snapshot =
							instantiated_struct_ref.has_outer_template_bindings()
								? &instantiated_struct_ref.outer_template_environment_snapshot()
								: nullptr;
						registerLazyConstructorStub(
							new_ctor_ref,
							new_ctor_node,
							StringTable::getOrInternStringHandle(template_name),
							instantiated_name,
							mem_func.access,
							effective_template_params,
							effective_template_args,
							outer_parent_snapshot);
					}
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception during template parameter substitution for constructor ",
							  ctor_decl.name(), ": ", e.what());
					throw;
				} catch (...) {
					FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for constructor ",
							  ctor_decl.name());
					throw;
				}
			} else {
				// No inline body yet (neither materialized nor has_template_body_position).
				// The body will arrive later via:
				//   (a) deferred-body-replay  (template_class.deferred_bodies() non-empty), or
				//   (b) out-of-line body matching (below in this function), or
				//   (c) lazy instantiation triggered at the call site.
				//
				// We MUST create a FRESH ConstructorDeclarationNode with the instantiated
				// struct name and with substituted (concrete) parameter types.
				// Reusing the original template-pattern node was the root cause of link
				// failures: the original carries template-parameter names (e.g. "T") in
				// both its own name field and its parameter types, so the IR emitter would
				// produce a symbol like Buffer$hash::Buffer$hash(T) instead of
				// Buffer$hash::Buffer$hash(int).
				try {
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name,
						instantiated_name);
					setOuterTemplateBindingsFromParams(new_ctor_ref, template_params, template_args_to_use);

					// Ensure template_param_order is populated (used by ExpressionSubstitutor)
					if (template_param_order.empty()) {
						for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
							if (const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
								tparam != nullptr) {
								template_param_order.push_back(tparam->name());
							}
						}
					}

					// Substitute and copy parameters so the stub has concrete types.
					size_t saved_pack_info = pack_param_info_.size();
					substituteAndCopyParams(ctor_decl.parameter_nodes(), new_ctor_ref, template_params, template_args_to_use);
					pack_param_info_.resize(saved_pack_info);

					new_ctor_ref.set_is_implicit(ctor_decl.is_implicit());
					new_ctor_ref.set_is_explicitly_defaulted(ctor_decl.is_explicitly_defaulted());
					new_ctor_ref.set_noexcept(ctor_decl.is_noexcept());
					new_ctor_ref.set_explicit(ctor_decl.is_explicit());
					new_ctor_ref.set_constexpr(ctor_decl.is_constexpr());
					if (ctor_decl.has_requires_clause()) {
						new_ctor_ref.set_requires_clause(*ctor_decl.requires_clause());
					}

					// Add the fresh stub to the instantiated struct AST node and to struct_info.
					instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);
					struct_info_ptr->addConstructor(new_ctor_node, mem_func.access);

					// Slice 3: map original template-pattern key to fresh instantiated stub,
					// so deferred-body-replay can find and materialise it later.
					registerSourceMemberStubIdentity(
						source_member_identity_maps,
						mem_func.function_declaration,
						new_ctor_node);
					if (is_implicit_instantiation &&
						!ctor_decl.is_materialized() &&
						shouldCommitTemplateInstantiationArtifacts()) {
						const TemplateEnvironmentSnapshot* outer_parent_snapshot =
							instantiated_struct_ref.has_outer_template_bindings()
								? &instantiated_struct_ref.outer_template_environment_snapshot()
								: nullptr;
						registerLazyConstructorStub(
							new_ctor_ref,
							new_ctor_node,
							StringTable::getOrInternStringHandle(template_name),
							instantiated_name,
							mem_func.access,
							effective_template_params,
							effective_template_args,
							outer_parent_snapshot);
					}
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception creating no-body constructor stub for ",
							  ctor_decl.name(), ": ", e.what());
					throw;
				}
			}
		} else if (mem_func.function_declaration.is<DestructorDeclarationNode>()) {
			const DestructorDeclarationNode& dtor_decl = mem_func.function_declaration.as<DestructorDeclarationNode>();

			// NOTE: Destructors are ALWAYS eagerly instantiated (not lazy)
			// because they're needed for object destruction

			// EAGER INSTANTIATION PATH (original code)
			if (dtor_decl.is_materialized()) {
				try {
					ASTNode substituted_body = substituteTemplateParameters(
						*dtor_decl.get_definition(),
						template_params,
						template_args_to_use);

					// Create a new destructor declaration with substituted body
					StringHandle specialized_dtor_name = StringTable::getOrInternStringHandle(StringBuilder()
																								  .append("~")
																								  .append(instantiated_name));
					auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
						instantiated_name,
						specialized_dtor_name);
					setOuterTemplateBindingsFromParams(new_dtor_ref, template_params, template_args_to_use);

					new_dtor_ref.set_has_noexcept_specifier(dtor_decl.has_noexcept_specifier());
					if (dtor_decl.has_noexcept_expression()) {
						ASTNode substituted_noexcept = substituteTemplateParameters(
							*dtor_decl.noexcept_expression(),
							template_params,
							template_args_to_use);
						new_dtor_ref.set_noexcept_expression(substituted_noexcept);
						ConstExpr::EvaluationContext ctx(gSymbolTable, *this);
						ctx.struct_info = struct_info_ptr;
						ctx.struct_type_index =
							struct_type_info.registeredTypeIndex().withCategory(TypeCategory::Struct);
						ctx.template_args = template_args_to_use;
						for (const TemplateParameterNode& template_param : template_params) {
							ctx.template_param_names.push_back(template_param.name());
						}
						auto eval = ConstExpr::Evaluator::evaluate(substituted_noexcept, ctx);
						if (!eval.success()) {
							throw CompileError("Failed to evaluate instantiated destructor noexcept-expression");
						}
						new_dtor_ref.set_noexcept(eval.as_bool());
					} else {
						new_dtor_ref.set_noexcept(dtor_decl.is_noexcept());
					}

					new_dtor_ref.set_definition(substituted_body);

					// Add the substituted destructor to the instantiated struct
					instantiated_struct_ref.add_destructor(new_dtor_node, mem_func.access);

					// Also add to struct_info so hasDestructor() returns true during codegen
					struct_info_ptr->addDestructor(new_dtor_node, mem_func.access, mem_func.is_virtual);

					// Slice 3: record destructor-with-definition stub.
					registerSourceMemberStubIdentity(
						source_member_identity_maps,
						mem_func.function_declaration,
						new_dtor_node);
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception during template parameter substitution for destructor ",
							  dtor_decl.name(), ": ", e.what());
					throw;
				} catch (...) {
					FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for destructor ",
							  dtor_decl.name());
					throw;
				}
			} else {
				// Destructor body is deferred (not yet materialized): create a new stub
				// that carries the template-body position so the deferred body replay can
				// find and re-parse it with concrete template arguments.
				StringHandle specialized_dtor_name = StringTable::getOrInternStringHandle(StringBuilder()
																							  .append("~")
																							  .append(instantiated_name));
				auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
					instantiated_name,
					specialized_dtor_name);
				setOuterTemplateBindingsFromParams(new_dtor_ref, template_params, template_args_to_use);
				new_dtor_ref.set_has_noexcept_specifier(dtor_decl.has_noexcept_specifier());
				new_dtor_ref.set_noexcept(dtor_decl.is_noexcept());
				if (dtor_decl.has_template_body_position()) {
					new_dtor_ref.set_template_body_position(dtor_decl.template_body_position());
				}

				instantiated_struct_ref.add_destructor(new_dtor_node, mem_func.access);
				struct_info_ptr->addDestructor(new_dtor_node, mem_func.access, mem_func.is_virtual);
				// Slice 3: record the deferred stub so the deferred-body replay can find it.
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_dtor_node);
			}
		} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
			// Member template functions need outer template parameters substituted
			// while keeping inner template parameters (e.g., auto → _T0) unchanged.
			// For example, in template<class _It, class _Sent> struct subrange:
			//   subrange(convertible_to<_It> auto __i, _Sent __s)
			// becomes a TemplateFunctionDeclarationNode with inner param _T0.
			// When instantiating subrange<int*, sentinel>, we need to substitute
			// _It→int* and _Sent→sentinel in the parameters, keeping _T0 as-is.
			const TemplateFunctionDeclarationNode& template_func =
				mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();

			FLASH_LOG(Templates, Debug, "Copying member template function to instantiated class with outer param substitution");

			const FunctionDeclarationNode& func_decl =
				template_func.function_declaration().as<FunctionDeclarationNode>();
			const DeclarationNode& decl_node = func_decl.decl_node();

			// Substitute outer class template parameters in function parameter types
			// so that e.g. _Sent becomes sentinel_t when the class is instantiated
			bool needs_substitution = false;
			// Check return type
			{
				const auto& rtype = decl_node.type_specifier_node();
				if ((rtype.category() == TypeCategory::UserDefined || rtype.category() == TypeCategory::TypeAlias || rtype.category() == TypeCategory::Template)) {
					needs_substitution = true;
				}
			}
			// Check parameter types
			if (!needs_substitution) {
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& ptype = param.as<DeclarationNode>().type_specifier_node();
						if ((ptype.category() == TypeCategory::UserDefined || ptype.category() == TypeCategory::TypeAlias || ptype.category() == TypeCategory::Template)) {
							needs_substitution = true;
							break;
						}
					}
				}
			}

			if (needs_substitution) {
				// Create a new inner function with substituted non-auto parameter types
				const TypeSpecifierNode& return_type_spec = decl_node.type_specifier_node();
				ASTNode substituted_return_type_node = substituteTemplateParameters(
					decl_node.type_node(), template_params, template_args_to_use);
				TypeIndex ret_type_index = substitute_template_parameter(
					return_type_spec, template_params, template_args_to_use);
				ret_type_index = resolveOwnerAliasTypeIndex(
					[this](const TypeSpecifierNode& type_spec, const auto& params, const auto& args) {
						return substitute_template_parameter(type_spec, params, args);
					},
					class_decl,
					return_type_spec,
					template_params,
					template_args_to_use,
					ret_type_index);
				ret_type_index = resolveDependentMemberPlaceholderFromOwnerArtifact(
					return_type_spec,
					ret_type_index);

				ASTNode new_return_type = substituted_return_type_node.is<TypeSpecifierNode>()
					? substituted_return_type_node
					: emplace_node<TypeSpecifierNode>(
						  ret_type_index.category(), return_type_spec.qualifier(),
						  get_type_size_bits(ret_type_index.category()), return_type_spec.token(), return_type_spec.cv_qualifier());
				auto& new_return_spec = new_return_type.as<TypeSpecifierNode>();
				new_return_spec.set_category(ret_type_index.category());
				if (ret_type_index.is_valid()) {
					new_return_spec.set_type_index(ret_type_index.withCategory(ret_type_index.category()));
				}
				if (!substituted_return_type_node.is<TypeSpecifierNode>()) {
					for (const auto& pl : return_type_spec.pointer_levels())
						new_return_spec.add_pointer_level(pl.cv_qualifier);
					new_return_spec.set_reference_qualifier(return_type_spec.reference_qualifier());
				}
				if (return_type_spec.has_function_signature()) {
					new_return_spec.set_function_signature(return_type_spec.function_signature());
				} else if (new_return_spec.type_index().category() == TypeCategory::FunctionPointer ||
						   new_return_spec.type_index().category() == TypeCategory::MemberFunctionPointer) {
					if (const auto* arg = findTemplateArgByName(
							return_type_spec.token().value(),
							template_params,
							template_args_to_use)) {
						if (arg->function_signature.has_value()) {
							new_return_spec.set_function_signature(*arg->function_signature);
						}
					}
				}
				normalizeSubstitutedTypeSpec(new_return_spec);

				auto buildMergedOuterTemplateBinding = [&](OuterTemplateBinding& out_binding) {
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						combined_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						combined_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							combined_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							combined_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							combined_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							combined_args.push_back(arg);
						}
						collectOuterTemplateBinding(combined_params, combined_args, out_binding);
						return;
					}
					if (outer_binding != nullptr) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						combined_params.reserve(
							outer_binding->param_names.size() + template_params.size());
						combined_args.reserve(
							outer_binding->param_args.size() + template_args_to_use.size());
						if (!outer_binding->params.empty()) {
							for (const ASTNode& outer_param_node : outer_binding->params) {
								if (const TemplateParameterNode* outer_param =
										tryGetTemplateParameterNode(outer_param_node);
									outer_param != nullptr) {
									combined_params.push_back(*outer_param);
								}
							}
						} else {
							for (size_t i = 0;
								 i < outer_binding->param_names.size() &&
								 i < outer_binding->param_args.size();
								 ++i) {
								combined_params.push_back(
									rebuildOuterTemplateParameter(
										outer_binding->param_names[i],
										outer_binding->param_args[i]));
							}
						}
						for (const TemplateTypeArg& outer_arg :
							 outer_binding->all_args.empty()
								? outer_binding->param_args
								: outer_binding->all_args) {
							combined_args.push_back(outer_arg);
						}
						for (const auto& param : template_params) {
							combined_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							combined_args.push_back(arg);
						}
						collectOuterTemplateBinding(combined_params, combined_args, out_binding);
						return;
					}
					collectOuterTemplateBinding(template_params, template_args_to_use, out_binding);
				};
				auto applyMergedOuterTemplateBindings = [&](FunctionDeclarationNode& target_func) {
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						combined_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						combined_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							combined_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							combined_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							combined_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							combined_args.push_back(arg);
						}
						setOuterTemplateBindingsFromParams(target_func, combined_params, combined_args);
						return;
					}
					OuterTemplateBinding merged_outer_binding;
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						merged_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						merged_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							merged_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							merged_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else if (outer_binding != nullptr) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						if (!outer_binding->params.empty()) {
							for (const ASTNode& outer_param_node : outer_binding->params) {
								if (const TemplateParameterNode* outer_param =
										tryGetTemplateParameterNode(outer_param_node);
									outer_param != nullptr) {
									merged_params.push_back(*outer_param);
								}
							}
						} else {
							for (size_t i = 0;
								 i < outer_binding->param_names.size() &&
								 i < outer_binding->param_args.size();
								 ++i) {
								merged_params.push_back(
									rebuildOuterTemplateParameter(
										outer_binding->param_names[i],
										outer_binding->param_args[i]));
							}
						}
						for (const TemplateTypeArg& outer_arg :
							 outer_binding->all_args.empty()
								? outer_binding->param_args
								: outer_binding->all_args) {
							merged_args.push_back(outer_arg);
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else {
						collectOuterTemplateBinding(
							template_params,
							template_args_to_use,
							merged_outer_binding);
					}
					InlineVector<TemplateParameterNode, 4> combined_params;
					InlineVector<TemplateTypeArg, 4> combined_args;
					if (!merged_outer_binding.params.empty()) {
						for (const ASTNode& param_node : merged_outer_binding.params) {
							if (const TemplateParameterNode* typed_param =
									tryGetTemplateParameterNode(param_node);
								typed_param != nullptr) {
								combined_params.push_back(*typed_param);
							}
						}
					}
					for (const TemplateTypeArg& arg :
						 merged_outer_binding.all_args.empty()
							? merged_outer_binding.param_args
							: merged_outer_binding.all_args) {
						combined_args.push_back(arg);
					}
					setOuterTemplateBindingsFromParams(target_func, combined_params, combined_args);
				};

				auto [new_decl_node, new_decl_ref] = emplace_node_ref<DeclarationNode>(
					new_return_type, decl_node.identifier_token());
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_decl_ref,
					instantiated_name);
				applyMergedOuterTemplateBindings(new_func_ref);

				size_t saved_pack_info = pack_param_info_.size();
				substituteAndCopyMemberFunctionParameters(
					func_decl.parameter_nodes(),
					new_func_ref,
					template_params,
					template_args_to_use,
					&class_decl,
					instantiated_member_owner_type_index,
					TypeIndex{},
					TypeIndex{},
					SubstitutedDefaultArgumentPolicy::SubstituteTemplateParameters,
					true,
					false,
					true,
					true);
				pack_param_info_.resize(saved_pack_info);
				syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
					*this,
					std::span<ASTNode>(
						new_func_ref.parameter_nodes().data(),
						new_func_ref.parameter_nodes().size()),
					std::span<const ASTNode>(
						func_decl.parameter_nodes().data(),
						func_decl.parameter_nodes().size()),
					template_params,
					template_args_to_use);
				preserveDependentMemberTemplateParameterPlaceholdersFromPattern(
					std::span<ASTNode>(
						new_func_ref.parameter_nodes().data(),
						new_func_ref.parameter_nodes().size()),
					std::span<const ASTNode>(
						func_decl.parameter_nodes().data(),
						func_decl.parameter_nodes().size()));

				// Copy function specifiers
				copy_function_properties(new_func_ref, func_decl);
				new_func_ref.set_is_const_member_function(mem_func.is_const());
				new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
				if (func_decl.is_materialized())
					new_func_ref.set_definition(*func_decl.get_definition());
				if (func_decl.has_template_body_position())
					new_func_ref.set_template_body_position(func_decl.template_body_position());
				if (func_decl.has_definition_lookup_context())
					new_func_ref.set_definition_lookup_context(func_decl.definition_lookup_context());
				// Copy trailing return type position for SFINAE resolution
				if (func_decl.has_trailing_return_type_position())
					new_func_ref.set_trailing_return_type_position(func_decl.trailing_return_type_position());

				// Create new TemplateFunctionDeclarationNode with inner template params
				new_func_ref.set_is_template_pattern(true);
				auto new_template_func = emplace_node<TemplateFunctionDeclarationNode>(
					template_func.template_parameters(),
					new_func_node,
					template_func.requires_clause());

				if (mem_func.operator_kind != OverloadableOperator::None) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_template_func, mem_func.access);
					struct_info_ptr->addOperatorOverload(mem_func.operator_kind, new_template_func, mem_func.access,
														 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					instantiated_struct_ref.add_member_function(new_template_func, mem_func.access);
					struct_info_ptr->addMemberFunction(
						decl_node.identifier_token().handle(),
						new_template_func,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				}

				// Register with qualified name
				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(instantiated_name))
					.append("::")
					.append(decl_node.identifier_token().value());
				StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());

				gTemplateRegistry.registerTemplate(qualified_name_handle, new_template_func);
				gTemplateRegistry.registerTemplate(decl_node.identifier_token().handle(), new_template_func);
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_template_func);

				// Register outer template parameter bindings
				{
					OuterTemplateBinding template_func_outer_binding;
					buildMergedOuterTemplateBinding(template_func_outer_binding);
					gTemplateRegistry.registerOuterTemplateBinding(qualified_name_handle, std::move(template_func_outer_binding));
					FLASH_LOG(Templates, Debug, "Registered outer template bindings for ", StringTable::getStringView(qualified_name_handle));
				}
			} else {
				// Even when no outer substitution is needed in the member template signature,
				// we must still rebind ownership to the instantiated class.
				auto applyMergedOuterTemplateBindings = [&](FunctionDeclarationNode& target_func) {
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> combined_params;
						InlineVector<TemplateTypeArg, 4> combined_args;
						combined_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						combined_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							combined_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							combined_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							combined_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							combined_args.push_back(arg);
						}
						setOuterTemplateBindingsFromParams(target_func, combined_params, combined_args);
						return;
					}
					OuterTemplateBinding merged_outer_binding;
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						merged_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						merged_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							merged_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							merged_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else if (outer_binding != nullptr) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						if (!outer_binding->params.empty()) {
							for (const ASTNode& outer_param_node : outer_binding->params) {
								if (const TemplateParameterNode* outer_param =
										tryGetTemplateParameterNode(outer_param_node);
									outer_param != nullptr) {
									merged_params.push_back(*outer_param);
								}
							}
						} else {
							for (size_t i = 0;
								 i < outer_binding->param_names.size() &&
								 i < outer_binding->param_args.size();
								 ++i) {
								merged_params.push_back(
									rebuildOuterTemplateParameter(
										outer_binding->param_names[i],
										outer_binding->param_args[i]));
							}
						}
						for (const TemplateTypeArg& outer_arg :
							 outer_binding->all_args.empty()
								? outer_binding->param_args
								: outer_binding->all_args) {
							merged_args.push_back(outer_arg);
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else {
						collectOuterTemplateBinding(
							template_params,
							template_args_to_use,
							merged_outer_binding);
					}
					InlineVector<TemplateParameterNode, 4> combined_params;
					InlineVector<TemplateTypeArg, 4> combined_args;
					if (!merged_outer_binding.params.empty()) {
						for (const ASTNode& param_node : merged_outer_binding.params) {
							if (const TemplateParameterNode* typed_param =
									tryGetTemplateParameterNode(param_node);
								typed_param != nullptr) {
								combined_params.push_back(*typed_param);
							}
						}
					}
					for (const TemplateTypeArg& arg :
						 merged_outer_binding.all_args.empty()
							? merged_outer_binding.param_args
							: merged_outer_binding.all_args) {
						combined_args.push_back(arg);
					}
					setOuterTemplateBindingsFromParams(target_func, combined_params, combined_args);
				};
				auto [new_decl_node, new_decl_ref] = emplace_node_ref<DeclarationNode>(
					decl_node.type_node(),
					decl_node.identifier_token());
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_decl_ref,
					instantiated_name);
				applyMergedOuterTemplateBindings(new_func_ref);

				for (const auto& param : func_decl.parameter_nodes()) {
					new_func_ref.add_parameter_node(param);
				}

				copy_function_properties(new_func_ref, func_decl);
				new_func_ref.set_is_const_member_function(mem_func.is_const());
				new_func_ref.set_is_volatile_member_function(mem_func.is_volatile());
				if (func_decl.is_materialized())
					new_func_ref.set_definition(*func_decl.get_definition());
				if (func_decl.has_template_body_position())
					new_func_ref.set_template_body_position(func_decl.template_body_position());
				if (func_decl.has_definition_lookup_context())
					new_func_ref.set_definition_lookup_context(func_decl.definition_lookup_context());
				if (func_decl.has_trailing_return_type_position())
					new_func_ref.set_trailing_return_type_position(func_decl.trailing_return_type_position());
				new_func_ref.set_is_template_pattern(true);

				auto new_template_func = emplace_node<TemplateFunctionDeclarationNode>(
					template_func.template_parameters(),
					new_func_node,
					template_func.requires_clause());

				if (mem_func.operator_kind != OverloadableOperator::None) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_kind, new_template_func, mem_func.access);
					struct_info_ptr->addOperatorOverload(mem_func.operator_kind, new_template_func, mem_func.access,
														 mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					instantiated_struct_ref.add_member_function(
						new_template_func,
						mem_func.access);
					struct_info_ptr->addMemberFunction(
						decl_node.identifier_token().handle(),
						new_template_func,
						mem_func.access,
						mem_func.is_virtual,
						mem_func.is_pure_virtual,
						mem_func.is_override,
						mem_func.is_final);
				}

				// Register with qualified name
				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(instantiated_name))
					.append("::")
					.append(decl_node.identifier_token().value());
				StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());

				gTemplateRegistry.registerTemplate(qualified_name_handle, new_template_func);
				gTemplateRegistry.registerTemplate(decl_node.identifier_token().handle(), new_template_func);
				registerSourceMemberStubIdentity(
					source_member_identity_maps,
					mem_func.function_declaration,
					new_template_func);

				// Register outer template parameter bindings
				{
					OuterTemplateBinding merged_outer_binding;
					if (func_decl.has_outer_template_bindings()) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						merged_params.reserve(
							func_decl.outer_template_param_names().size() + template_params.size());
						merged_args.reserve(
							func_decl.outer_template_args().size() + template_args_to_use.size());
						for (size_t i = 0; i < func_decl.outer_template_param_names().size(); ++i) {
							merged_params.push_back(
								rebuildOuterTemplateParameter(
									func_decl.outer_template_param_names()[i],
									func_decl.outer_template_args()[i]));
							merged_args.push_back(toTemplateTypeArg(func_decl.outer_template_args()[i]));
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else if (outer_binding != nullptr) {
						InlineVector<TemplateParameterNode, 4> merged_params;
						InlineVector<TemplateTypeArg, 4> merged_args;
						if (!outer_binding->params.empty()) {
							for (const ASTNode& outer_param_node : outer_binding->params) {
								if (const TemplateParameterNode* outer_param =
										tryGetTemplateParameterNode(outer_param_node);
									outer_param != nullptr) {
									merged_params.push_back(*outer_param);
								}
							}
						} else {
							for (size_t i = 0;
								 i < outer_binding->param_names.size() &&
								 i < outer_binding->param_args.size();
								 ++i) {
								merged_params.push_back(
									rebuildOuterTemplateParameter(
										outer_binding->param_names[i],
										outer_binding->param_args[i]));
							}
						}
						for (const TemplateTypeArg& outer_arg :
							 outer_binding->all_args.empty()
								? outer_binding->param_args
								: outer_binding->all_args) {
							merged_args.push_back(outer_arg);
						}
						for (const auto& param : template_params) {
							merged_params.push_back(param);
						}
						for (const auto& arg : template_args_to_use) {
							merged_args.push_back(arg);
						}
						collectOuterTemplateBinding(merged_params, merged_args, merged_outer_binding);
					} else {
						collectOuterTemplateBinding(
							template_params,
							template_args_to_use,
							merged_outer_binding);
					}
					gTemplateRegistry.registerOuterTemplateBinding(
						qualified_name_handle,
						std::move(merged_outer_binding));
					FLASH_LOG(Templates, Debug, "Registered outer template bindings for ", StringTable::getStringView(qualified_name_handle));
				}
			}
		} else {
			FLASH_LOG(Templates, Error, "Unknown member function type in template instantiation: ",
					  mem_func.function_declaration.type_name());
			throw InternalError("Unhandled member function declaration kind during template instantiation");
		}
	}

	// Process out-of-line member function definitions for the template
	auto out_of_line_members = gTemplateRegistry.getOutOfLineMemberFunctions(template_name);
	const std::string_view template_base_name = extractBaseTemplateName(template_name);
	FLASH_LOG(Templates, Debug, "Processing ", out_of_line_members.size(), " out-of-line member functions for ", template_name);

	for (const auto& out_of_line_member : out_of_line_members) {
		// Check if this is a nested template (member function template of a class template)
		// Pattern: template<typename T> template<typename U> T Container<T>::convert(U u) { ... }
		if (!out_of_line_member.inner_template_params.empty()) {
			const FunctionDeclarationNode& ool_func = out_of_line_member.function_node.as<FunctionDeclarationNode>();
			const DeclarationNode& ool_decl = ool_func.decl_node();
			std::string_view ool_func_name = ool_decl.identifier_token().value();
			const bool out_of_line_ctor_stub = isOutOfLineConstructorStubName(
				ool_func_name,
				template_name,
				template_base_name);

			FLASH_LOG(Templates, Debug, "Processing nested template out-of-line member: ", ool_func_name);

			bool found = false;
			const size_t ool_inner_template_param_count =
				out_of_line_member.inner_template_params.size();
			const size_t ool_function_param_count = ool_func.parameter_nodes().size();
			if (out_of_line_ctor_stub) {
				OutOfLineConstructorStubResolution ctor_resolution =
					findOutOfLineConstructorTemplateStubByIdentity(
						*this,
						source_member_identity_maps,
						std::span<const StructMemberFunctionDecl>(
							effective_member_functions.data(),
							effective_member_functions.size()),
						ool_func,
						std::span<const TemplateParameterNode>(
							out_of_line_member.template_params.data(),
							out_of_line_member.template_params.size()),
						std::span<const TemplateTypeArg>(
							template_args_to_use.data(),
							template_args_to_use.size()),
						instantiated_name,
						std::span<const TemplateParameterNode>(
							out_of_line_member.inner_template_params.data(),
							out_of_line_member.inner_template_params.size()));
				if (ctor_resolution.ambiguous) {
					std::string error_msg = std::string(StringBuilder()
						.append("Could not uniquely match out-of-line constructor template '")
						.append(ool_func_name)
						.append("' in instantiated class '")
						.append(instantiated_name)
						.append("'")
						.commit());
					if (force_eager) {
						throw CompileError(error_msg);
					}
					return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
				}

				if (ctor_resolution.ctor == nullptr) {
					StringBuilder error_builder;
					if (ctor_resolution.insufficient_evidence) {
						error_builder
							.append("Could not match out-of-line constructor template stub '");
					} else {
						error_builder
							.append("Could not attach out-of-line constructor template stub '");
					}
					error_builder
						.append(ool_func_name)
						.append("' for instantiated class '")
						.append(instantiated_name);
					if (!ctor_resolution.insufficient_evidence) {
						error_builder.append("' via source-member identity mapping");
					} else {
						error_builder.append("'");
					}
					std::string error_msg = std::string(error_builder.commit());
					if (force_eager) {
						throw CompileError(error_msg);
					}
					return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
				}

				ConstructorDeclarationNode& ctor_decl = *ctor_resolution.ctor;
				setOutOfLineConstructorTemplateReplayMetadata(
					ctor_decl,
					out_of_line_member);
				syncOutOfLineConstructorTemplateParameters(
					ctor_decl.parameter_nodes(),
					ool_func.parameter_nodes());

				if (struct_info_ptr != nullptr) {
					OutOfLineConstructorStubResolution info_ctor_resolution =
						findMatchingConstructorInStructInfo(
							*struct_info_ptr,
							ctor_decl,
							[](const ConstructorDeclarationNode& info_ctor) {
								return !info_ctor.has_template_body_position();
							});

					if (info_ctor_resolution.ctor != nullptr) {
						setOutOfLineConstructorTemplateReplayMetadata(
							*info_ctor_resolution.ctor,
							out_of_line_member);
						syncOutOfLineConstructorTemplateParameters(
							info_ctor_resolution.ctor->parameter_nodes(),
							ool_func.parameter_nodes());
					} else if (info_ctor_resolution.ambiguous) {
						std::string error_msg = std::string(StringBuilder()
							.append("Ambiguous StructTypeInfo constructor-template sync for out-of-line constructor template '")
							.append(ool_func_name)
							.append("' in instantiated class '")
							.append(instantiated_name)
							.append("'")
							.commit());
						if (force_eager) {
							throw InternalError(error_msg);
						}
						FLASH_LOG(Templates, Error, error_msg);
					}
				}

				FLASH_LOG(
					Templates,
					Debug,
					"Set deferred body position on out-of-line constructor template: ",
					ool_func_name);
				continue;
			}

			// Replay-first attachment path: match against source template members and
			// resolve to instantiated stubs via source_member_to_stub identity mapping.
			FunctionDeclarationNode* inst_func_decl = nullptr;
			bool saw_insufficient_replay_evidence = false;
			bool saw_ambiguous_replay_match = false;
			for (const auto& source_member : effective_member_functions) {
				if (!isMatchingMemberTemplate(
						source_member,
						ool_func_name,
						ool_inner_template_param_count,
						ool_function_param_count)) {
					continue;
				}
				ASTNode* matched_stub = findSourceMemberStubByIdentity(
					source_member_identity_maps,
					source_member.function_declaration);
				if (matched_stub == nullptr) {
					continue;
				}
				if (!matched_stub->is<TemplateFunctionDeclarationNode>()) {
					continue;
				}
				FunctionDeclarationNode* matched_template_func_decl =
					get_function_decl_node_mut(*matched_stub);
				if (matched_template_func_decl == nullptr) {
					continue;
				}
				if (matched_template_func_decl->has_template_body_position()) {
					continue;
				}
				ReplaySignatureMatchResult signature_match =
					nestedOutOfLineMemberTemplateMatchesCandidate(
						*this,
						source_member.function_declaration,
						ool_func,
						std::span<const TemplateParameterNode>(
							out_of_line_member.template_params.data(),
							out_of_line_member.template_params.size()),
						std::span<const TemplateTypeArg>(
							template_args_to_use.data(),
							template_args_to_use.size()),
						instantiated_name,
						std::span<const TemplateParameterNode>(
							out_of_line_member.inner_template_params.data(),
							out_of_line_member.inner_template_params.size()));
				if (signature_match == ReplaySignatureMatchResult::InsufficientEvidence) {
					saw_insufficient_replay_evidence = true;
					continue;
				}
				if (signature_match != ReplaySignatureMatchResult::Match) {
					continue;
				}
				if (inst_func_decl != nullptr &&
					inst_func_decl != matched_template_func_decl) {
					saw_ambiguous_replay_match = true;
					break;
				}
				inst_func_decl = matched_template_func_decl;
			}

			if (saw_ambiguous_replay_match) {
				std::string error_msg = std::string(StringBuilder()
					.append("Could not uniquely match nested out-of-line member template '")
					.append(ool_func_name)
					.append("' in instantiated class '")
					.append(instantiated_name)
					.append("'")
					.commit());
				if (force_eager) {
					throw CompileError(error_msg);
				}
				return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
			}

			if (inst_func_decl != nullptr) {
				// Set the body position and definition-time lookup context from the
				// out-of-line definition so two-phase lookup uses the definition namespace.
				inst_func_decl->set_template_body_position(out_of_line_member.body_start);
				copyDefinitionParameterIdentifiers(
					inst_func_decl->parameter_nodes(),
					ool_func.parameter_nodes());
				copyDefinitionParameterTypesForDependentMemberTemplateSegments(
					inst_func_decl->parameter_nodes(),
					ool_func.parameter_nodes());
				auto outer_params_span = std::span<const TemplateParameterNode>(
					out_of_line_member.template_params.data(),
					out_of_line_member.template_params.size());
				auto outer_args_span = std::span<const TemplateTypeArg>(
					template_args_to_use.data(),
					template_args_to_use.size());
				syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
					*this,
					inst_func_decl->parameter_nodes(),
					ool_func.parameter_nodes(),
					outer_params_span,
					outer_args_span);
				inst_func_decl->set_definition_lookup_context(out_of_line_member.definition_lookup_context);
				if (struct_info_ptr != nullptr) {
					OutOfLineFunctionStubResolution info_func_resolution =
						findMatchingFunctionInStructInfo(
							*struct_info_ptr,
							*inst_func_decl,
							[](const FunctionDeclarationNode& info_func) {
								return !info_func.has_template_body_position();
							});
					if (info_func_resolution.ambiguous) {
						std::string error_msg = std::string(StringBuilder()
							.append("Ambiguous StructTypeInfo sync for out-of-line member template '")
							.append(ool_func_name)
							.append("' in instantiated class '")
							.append(instantiated_name)
							.append("'")
							.commit());
						if (force_eager) {
							throw InternalError(error_msg);
						}
						FLASH_LOG(Templates, Error, error_msg);
					} else if (info_func_resolution.func != nullptr) {
						info_func_resolution.func->set_template_body_position(out_of_line_member.body_start);
						copyDefinitionParameterIdentifiers(
							info_func_resolution.func->parameter_nodes(),
							ool_func.parameter_nodes());
						copyDefinitionParameterTypes(
							info_func_resolution.func->parameter_nodes(),
							inst_func_decl->parameter_nodes());
						syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
							*this,
							info_func_resolution.func->parameter_nodes(),
							ool_func.parameter_nodes(),
							outer_params_span,
							outer_args_span);
						info_func_resolution.func->set_definition_lookup_context(
							out_of_line_member.definition_lookup_context);
					}
				}
				FLASH_LOG(
					Templates,
					Debug,
					"Set body position on nested template member: ",
					ool_func_name,
					", stub=",
					reinterpret_cast<uintptr_t>(inst_func_decl),
					", body_pos=",
					static_cast<size_t>(out_of_line_member.body_start));
				found = true;
			}

			if (!found && saw_insufficient_replay_evidence) {
				std::string error_msg = std::string(StringBuilder()
					.append("Could not match nested out-of-line member template '")
					.append(ool_func_name)
					.append("' for instantiated class '")
					.append(instantiated_name)
					.append("' to exactly one declaration after template substitution")
					.commit());
				if (force_eager) {
					throw CompileError(error_msg);
				}
				return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
			}

			if (!found) {
				std::string error_msg = std::string(StringBuilder()
					.append("Could not attach nested out-of-line member template '")
					.append(ool_func_name)
					.append("' for instantiated class '")
					.append(instantiated_name)
					.append("' via source-member identity mapping")
					.commit());
				if (force_eager) {
					throw CompileError(error_msg);
				}
				return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
			}

			continue;
		}

		// The function_node should be a FunctionDeclarationNode
		if (!out_of_line_member.function_node.is<FunctionDeclarationNode>()) {
			FLASH_LOG(Templates, Error, "Out-of-line member function_node is not a FunctionDeclarationNode, type: ", out_of_line_member.function_node.type_name());
			continue;
		}

		const FunctionDeclarationNode& func_decl = out_of_line_member.function_node.as<FunctionDeclarationNode>();
		const DeclarationNode& decl = func_decl.decl_node();

		FLASH_LOG(Templates, Debug, "  Looking for replay-first match of out-of-line '", decl.identifier_token().value(),
				  "' via source-member identity");

		// Replay-first plain-member attachment path: match the out-of-line definition
		// against source declarations, then resolve directly to the instantiated stub
		// through source-member -> stub identity.
		bool found_match = false;
		OutOfLineMemberStubResolution plain_member_resolution =
			findPlainOutOfLineMemberStubByIdentity(
				*this,
				source_member_identity_maps,
				std::span<const StructMemberFunctionDecl>(
					effective_member_functions.data(),
					effective_member_functions.size()),
				func_decl,
				std::span<const TemplateParameterNode>(
					out_of_line_member.template_params.data(),
					out_of_line_member.template_params.size()),
				std::span<const TemplateTypeArg>(
					template_args_to_use.data(),
					template_args_to_use.size()),
				instantiated_name);
		if (FunctionDeclarationNode* inst_func = plain_member_resolution.func;
			inst_func != nullptr) {
			const std::span<const ASTNode> inst_func_params = inst_func->parameter_nodes();
			std::vector<ASTNode> definition_scope_params(
				inst_func_params.begin(),
				inst_func_params.end());
			const auto& definition_params = func_decl.parameter_nodes();
			if (definition_scope_params.size() == definition_params.size()) {
				for (size_t param_index = 0; param_index < definition_scope_params.size(); ++param_index) {
					DeclarationNode* scope_decl =
						get_decl_from_symbol_mut(definition_scope_params[param_index]);
					const DeclarationNode* def_decl =
						get_decl_from_symbol(definition_params[param_index]);
					if (!scope_decl || !def_decl) {
						continue;
					}
					if (!def_decl->identifier_token().value().empty()) {
						scope_decl->set_identifier_token(def_decl->identifier_token());
					}
				}
			}
			found_match = replayOutOfLineMemberBody(
				*inst_func,
				&instantiated_struct_ref,
				instantiated_name,
				struct_type_info.type_index_,
				std::span<const ASTNode>(
					definition_scope_params.data(),
					definition_scope_params.size()),
				out_of_line_member.body_start,
				out_of_line_member.definition_lookup_context,
				decl.identifier_token(),
				std::span<const TemplateParameterNode>(
					out_of_line_member.template_params.data(),
					out_of_line_member.template_params.size()),
				std::span<const TemplateTypeArg>(
					template_args_to_use.data(),
					template_args_to_use.size()),
				"primary-template",
				decl.identifier_token().value());
			if (found_match) {
				OutOfLineFunctionStubResolution info_func_resolution =
					findReplayedOutOfLineMemberInStructInfo(
						struct_info_ptr,
						*inst_func);
				if (info_func_resolution.ambiguous) {
					std::string error_msg = std::string(StringBuilder()
						.append("Ambiguous StructTypeInfo sync for out-of-line member '")
						.append(decl.identifier_token().value())
						.append("' in instantiated class '")
						.append(instantiated_name)
						.append("'")
						.commit());
					if (force_eager) {
						throw InternalError(error_msg);
					}
					FLASH_LOG(Templates, Error, error_msg);
				} else if (info_func_resolution.func != nullptr &&
					inst_func->is_materialized()) {
					FLASH_LOG(
						Templates,
						Debug,
						"Syncing replayed out-of-line member into StructTypeInfo: ",
						decl.identifier_token().value());
					copyDefinitionParameterIdentifiers(
						info_func_resolution.func->parameter_nodes(),
						func_decl.parameter_nodes());
					copyDefinitionParameterTypes(
						info_func_resolution.func->parameter_nodes(),
						inst_func->parameter_nodes());
					info_func_resolution.func->set_definition(*inst_func->get_definition());
					finalize_function_after_definition(*info_func_resolution.func, true);
				} else {
					FLASH_LOG(
						Templates,
						Debug,
						"Skipped StructTypeInfo sync for replayed out-of-line member '",
						decl.identifier_token().value(),
						"' (matched=",
						info_func_resolution.func != nullptr,
						", materialized=",
						inst_func->is_materialized(),
						")");
				}
				registerLateMaterializedOwningStructRoot(instantiated_name);
				normalizePendingSemanticRoots();
			}
		}
		if (found_match) {
			continue;
		}

		const bool out_of_line_ctor_stub = isOutOfLineConstructorStubName(
			decl.identifier_token().value(),
			template_name,
			template_base_name);
		if (!out_of_line_ctor_stub &&
			plain_member_resolution.ambiguous) {
			std::string error_msg = std::string(StringBuilder()
				.append("Could not uniquely match out-of-line member '")
				.append(decl.identifier_token().value())
				.append("' in instantiated class '")
				.append(instantiated_name)
				.append("'")
				.commit());
			if (force_eager) {
				throw CompileError(error_msg);
			}
			return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
		}
		if (!out_of_line_ctor_stub &&
			plain_member_resolution.insufficient_evidence) {
			std::string error_msg = std::string(StringBuilder()
				.append("Could not match out-of-line member '")
				.append(decl.identifier_token().value())
				.append("' for instantiated class '")
				.append(instantiated_name)
				.append("' to exactly one declaration after template substitution")
				.commit());
			if (force_eager) {
				throw CompileError(error_msg);
			}
			return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
		}
		if (out_of_line_ctor_stub) {
			OutOfLineConstructorStubResolution ctor_resolution =
				findPlainOutOfLineConstructorStubByIdentity(
					*this,
					source_member_identity_maps,
					std::span<const StructMemberFunctionDecl>(
						effective_member_functions.data(),
						effective_member_functions.size()),
					func_decl,
					std::span<const TemplateParameterNode>(
						out_of_line_member.template_params.data(),
						out_of_line_member.template_params.size()),
					std::span<const TemplateTypeArg>(
						template_args_to_use.data(),
						template_args_to_use.size()),
					instantiated_name);
			if (ctor_resolution.ambiguous) {
				std::string ambiguity_msg = std::string(StringBuilder()
					.append("Could not uniquely match out-of-line constructor '")
					.append(decl.identifier_token().value())
					.append("' in instantiated class '")
					.append(instantiated_name)
					.append("'")
					.commit());
				if (force_eager) {
					throw CompileError(ambiguity_msg);
				}
				return failTemplateInstantiation(ambiguity_msg, nullptr, std::nullopt);
			}
			if (ctor_resolution.ctor == nullptr) {
				StringBuilder error_builder;
				if (ctor_resolution.insufficient_evidence) {
					error_builder
						.append("Could not match out-of-line constructor stub '");
				} else {
					error_builder
						.append("Could not attach out-of-line constructor stub '");
				}
				error_builder
					.append(decl.identifier_token().value())
					.append("' for instantiated class '")
					.append(instantiated_name);
				if (!ctor_resolution.insufficient_evidence) {
					error_builder.append("' via source-member identity mapping");
				} else {
					error_builder.append("'");
				}
				std::string error_msg = std::string(error_builder.commit());
				if (force_eager) {
					throw CompileError(error_msg);
				}
				return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
			}

			if (ctor_resolution.ctor != nullptr) {
				ConstructorDeclarationNode& ctor = *ctor_resolution.ctor;
				copyDefinitionParameterIdentifiers(
					ctor.parameter_nodes(),
					func_decl.parameter_nodes());
				// Save current position
				SaveHandle saved_pos = save_token_position();

				// Restore to the out-of-line definition position
				// For constructors with initializer lists, restore to ':' so parse_function_body
				// can parse the initializer list; otherwise restore to '{'.
				if (out_of_line_member.has_initializer_list && out_of_line_member.initializer_list_start != 0) {
					restore_lexer_position_only(out_of_line_member.initializer_list_start);
				} else {
					restore_lexer_position_only(out_of_line_member.body_start);
				}

				// Use setup_member_function_context for full member context:
				// member context push, member-function registration, and 'this' injection.
				// Constructors use ConstructorDeclarationNode (not FunctionDeclarationNode),
				// so we manage the scope manually but delegate context to the shared helper.
				TemplateDefinitionLookupContext definition_lookup_context =
					ensureReplayDefinitionLookupContext(
						out_of_line_member.definition_lookup_context,
						decl.identifier_token(),
						gSymbolTable.get_current_namespace_handle(),
						instantiated_name);
				ScopedDefinitionLookupContext ctx_scope(
					current_template_definition_lookup_context_,
					definition_lookup_context.is_valid()
						? &definition_lookup_context
						: nullptr);

				gSymbolTable.enter_scope(ScopeType::Function);
				register_parameters_in_scope(ctor.parameter_nodes());
				setup_member_function_context(
					&instantiated_struct_ref,
					instantiated_name,
					struct_type_info.type_index_,
					true);

				// Parse constructor initializer list if present (before parsing body)
				if (out_of_line_member.has_initializer_list) {
					if (peek() == ":"_tok) {
						advance();  // consume ':'

						// Parse initializers until we hit '{', ';', or 'try'
						while (true) {
							TokenKind next_token = peek();
							if (next_token == "{"_tok || next_token == ";"_tok || next_token == "try"_tok || next_token.is_eof()) {
								break;
							}

							// Parse initializer name (could be base class or member)
							auto init_name_token = advance();
							if (!init_name_token.kind().is_identifier()) {
								FLASH_LOG(Templates, Error, "Expected member or base class name in constructor initializer list, got '",
										  init_name_token.value(), "'");
								break;
							}

							std::string_view init_name = init_name_token.value();

							// Replay the same qualified mem-initializer spelling accepted by the
							// shared constructor-initializer parsers (e.g. N::Base<T>(...)).
							init_name = consume_qualified_name_suffix(init_name);

							// Check for template arguments: Base<T>(...) in base class initializer
							if (peek() == "<"_tok) {
								skip_template_arguments();
							}

							// Expect '(' or '{'
							bool is_paren = peek() == "("_tok;
							bool is_brace = peek() == "{"_tok;
							if (!is_paren && !is_brace) {
								FLASH_LOG(Templates, Error, "Expected '(' or '{' after initializer name in constructor, got '",
										  peek_info().value(), "'");
								break;
							}

							advance();  // consume '(' or '{'
							TokenKind close_kind = ")"_tok;
							if (!is_paren) {
								close_kind = "}"_tok;
							}

							std::vector<ASTNode> init_args;
							if (peek() != close_kind) {
								while (true) {
									ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
									if (arg_result.is_error()) {
										break;
									}
									if (auto arg_node = arg_result.node()) {
										init_args.push_back(*arg_node);
									}
									if (peek() != ","_tok) {
										break;
									}
									advance();
								}
							}

							if (!consume(close_kind)) {
								FLASH_LOG(Templates, Error, "Expected ", is_paren ? "')'" : "'}'",
										  " after initializer arguments, got '", peek_info().value(), "'");
								break;
							}

							// Substitute template parameters in initializer arguments
							std::vector<ASTNode> substituted_args;
							for (const auto& arg : init_args) {
								try {
									ASTNode substituted_arg = substituteTemplateParameters(
										arg,
										out_of_line_member.template_params,
										template_args_to_use);
									substituted_args.push_back(substituted_arg);
								} catch (const std::exception& e) {
									FLASH_LOG(Templates, Error, "Exception during template parameter substitution in constructor initializer: ", e.what());
								}
							}

							// Determine if this is a base class or member initializer
							bool is_base_init = false;
							StringHandle matched_base_name;
							StringHandle init_name_handle = StringTable::getOrInternStringHandle(init_name);
							if (struct_type_info.struct_info_) {
								// Check if this is a base class by looking at the struct's base classes
								for (const auto& base : struct_type_info.struct_info_->base_classes) {
									std::string_view base_name = base.name;
									bool base_names_match =
										(base_name == init_name || extractBaseTemplateName(base_name) == init_name);
									if (!base_names_match) {
										if (const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
											base_type_info && base_type_info->isTemplateInstantiation()) {
											StringHandle qualified_base_template_name =
												gNamespaceRegistry.buildQualifiedIdentifier(
													base_type_info->sourceNamespace(),
													base_type_info->baseTemplateName());
											base_names_match =
												StringTable::getStringView(qualified_base_template_name) == init_name;
										}
									}
									if (base_names_match) {
										is_base_init = true;
										matched_base_name = StringTable::getOrInternStringHandle(base_name);
										break;
									}
								}

								if (!is_base_init && struct_type_info.struct_info_->has_deferred_base_classes) {
									for (const auto& deferred_base_entry : struct_type_info.struct_info_->deferred_template_bases) {
										if (deferred_base_entry.base_template_name == init_name_handle) {
											is_base_init = true;
											matched_base_name = deferred_base_entry.base_template_name;
											break;
										}
									}
								}
							}

							if (is_base_init) {
								ctor.add_base_initializer(matched_base_name, std::move(substituted_args));
							}

							if (!is_base_init) {
								auto make_initializer_list = [&](InitializerListNode::InitializationStyle style) -> ASTNode {
									auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode(style));
									for (const auto& arg : substituted_args) {
										init_list_ref.add_initializer(arg);
									}
									return init_list_node;
								};

								// It's a member initializer
								if (is_brace) {
									ctor.add_member_initializer(init_name, make_initializer_list(InitializerListNode::InitializationStyle::Brace));
								} else if (substituted_args.size() > 1) {
									ctor.add_member_initializer(init_name, make_initializer_list(InitializerListNode::InitializationStyle::Paren));
								} else if (!substituted_args.empty()) {
									ctor.add_member_initializer(init_name, substituted_args[0]);
								}
							}

							// Continue if there's a comma
							if (peek() != ","_tok) {
								break;
							}
							advance();  // consume ','
						}
					}
				}

				// Parse the function body (handles function-try-blocks too)
				// Pass true for is_ctor_or_dtor so constructor function-try-blocks
				// get the C++20 [except.handle]/15 implicit rethrow at catch handler ends.
				auto body_result = parse_function_body(true /* is_ctor_or_dtor */);
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
				restore_lexer_position_only(saved_pos);

				if (body_result.is_error() || !body_result.node().has_value()) {
					FLASH_LOG(Templates, Error, "Failed to parse out-of-line constructor body for ",
							  decl.identifier_token().value());
					continue;
				}

				try {
					ASTNode substituted_body = substituteTemplateParameters(
						*body_result.node(),
						out_of_line_member.template_params,
						template_args_to_use);
					ctor.set_definition(substituted_body);
					// Also update the StructTypeInfo's copy (used by codegen)
					if (struct_type_info.struct_info_) {
						OutOfLineConstructorStubResolution info_ctor_resolution =
							findMatchingConstructorInStructInfo(
								*struct_type_info.struct_info_,
								ctor,
								[](const ConstructorDeclarationNode& info_ctor) {
									return !info_ctor.is_materialized();
								});
						if (info_ctor_resolution.ctor != nullptr) {
							copyDefinitionParameterIdentifiers(
								info_ctor_resolution.ctor->parameter_nodes(),
								func_decl.parameter_nodes());
							info_ctor_resolution.ctor->set_definition(substituted_body);
						} else if (info_ctor_resolution.ambiguous) {
							std::string error_msg = std::string(StringBuilder()
								.append("Ambiguous StructTypeInfo constructor sync for out-of-line constructor '")
								.append(decl.identifier_token().value())
								.append("' in instantiated class '")
								.append(instantiated_name)
								.append("'")
								.commit());
							if (force_eager) {
								throw InternalError(error_msg);
							}
							FLASH_LOG(Templates, Error, error_msg);
						}
					}
					// The out-of-line constructor body is replayed after the
					// instantiated class root may already have gone through semantic analysis.
					// Re-enqueue the owning struct so constructor-call and
					// initializer annotations are produced from the identity-resolved
					// body before IR generation, instead of relying on codegen-side
					// overload recovery.
					registerLateMaterializedOwningStructRoot(instantiated_name);
					normalizePendingSemanticRoots();
					found_match = true;
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception during template parameter substitution for out-of-line constructor ",
							  decl.identifier_token().value(), ": ", e.what());
				}
			}
		}

		if (!found_match) {
			std::string error_msg = std::string(StringBuilder()
				.append("Out-of-line member function '")
				.append(decl.identifier_token().value())
				.append("' could not be attached in instantiated struct '")
				.append(instantiated_name)
				.append("'")
				.commit());
			if (force_eager) {
				throw CompileError(error_msg);
			}
			return failTemplateInstantiation(error_msg, nullptr, std::nullopt);
		}
	}

	// Process out-of-line static member variable definitions for the template
	auto out_of_line_vars = gTemplateRegistry.getOutOfLineMemberVariables(template_name);

	for (const auto& out_of_line_var : out_of_line_vars) {
		// Substitute template parameters in the initializer
		std::optional<ASTNode> substituted_initializer = out_of_line_var.initializer;
		bool out_of_line_is_constexpr = false;
		if (out_of_line_var.declaration.has_value() && out_of_line_var.declaration->is<VariableDeclarationNode>()) {
			out_of_line_is_constexpr = out_of_line_var.declaration->as<VariableDeclarationNode>().is_constexpr();
		}
		if (out_of_line_var.initializer.has_value()) {
			auto try_reparse_out_of_line_static_initializer = [&]() -> bool {
				std::span<const TemplateParameterNode> replay_template_params;
				if (!out_of_line_var.replay_template_params.empty()) {
					replay_template_params = std::span<const TemplateParameterNode>(
						out_of_line_var.replay_template_params.data(),
						out_of_line_var.replay_template_params.size());
				} else {
					replay_template_params = std::span<const TemplateParameterNode>(
						out_of_line_var.template_params.data(),
						out_of_line_var.template_params.size());
				}
				const bool requires_replay_metadata =
					static_initializer_requires_replay_metadata(
						out_of_line_var.initializer,
						replay_template_params);
				if (!out_of_line_var.initializer_position.has_value() ||
					!out_of_line_var.declaration.has_value()) {
					if (requires_replay_metadata) {
						throw InternalError(
							"Out-of-line template static member initializer replay metadata missing for dependent initializer");
					}
					return false;
				}

				SaveHandle current_pos = save_token_position();
				ScopedLexerPositionRestore lexer_restore(*this, current_pos);

				TemplateInstantiationContext substitution_context =
					buildTemplateInstantiationContext(
						out_of_line_var.template_params,
						template_args_to_use,
						nullptr,
						currentTemplateSubstitutionFailurePolicy());

				const DeclarationNode* declaration_ptr =
					get_decl_from_symbol(*out_of_line_var.declaration);
				if (declaration_ptr == nullptr) {
					throw InternalError(
						"Out-of-line template static member initializer replay declaration is missing or invalid");
				}

				TemplateDefinitionLookupContext definition_lookup_context =
					ensureReplayDefinitionLookupContext(
						out_of_line_var.definition_lookup_context,
						declaration_ptr->identifier_token(),
						struct_info_ptr->getNamespaceHandle(),
						instantiated_name);
				const TemplateDefinitionLookupContext* active_definition_lookup_context = nullptr;
				if (definition_lookup_context.is_valid()) {
					active_definition_lookup_context = &definition_lookup_context;
				}
				substitution_context.definition_lookup_context =
					active_definition_lookup_context;

				FlashCpp::TemplateDepthGuard guard_template_depth(parsing_template_depth_);
				parsing_template_depth_ = 1;
				ScopedDefinitionLookupContext ctx_scope(
					current_template_definition_lookup_context_,
					substitution_context.definition_lookup_context);

				FlashCpp::ScopedState guard_param_names(currentTemplateParamState());
				InlineVector<StringHandle, 4> template_param_names;
				InlineVector<TemplateParameterKind, 4> template_param_kinds;
				InlineVector<TypeCategory, 4> non_type_categories;
				buildTemplateParameterReplayState(
					replay_template_params,
					template_param_names,
					template_param_kinds,
					non_type_categories);
				setCurrentTemplateParameters(
					template_param_names,
					template_param_kinds,
					non_type_categories);

				auto member_ctx_scope =
					push_replay_member_context(instantiated_name, struct_info_ptr);

				restore_lexer_position_only(*out_of_line_var.initializer_position);
				ASTNode declaration_copy = *out_of_line_var.declaration;
				DeclarationNode* declaration_copy_ptr =
					get_decl_from_symbol_mut(declaration_copy);
				if (declaration_copy_ptr == nullptr) {
					throw InternalError(
						"Out-of-line template static member initializer replay failed to materialize mutable declaration");
				}
				DeclarationNode& declaration_copy_ref = *declaration_copy_ptr;
				TypeSpecifierNode& type_spec = declaration_copy_ref.type_specifier_node();

				std::optional<ASTNode> reparsed_initializer;
				if (peek() == "="_tok) {
					reparsed_initializer = parse_copy_initialization(
						declaration_copy_ref,
						type_spec);
				} else if (peek() == "{"_tok) {
					ParseResult init_parse_result = parse_brace_initializer(type_spec);
					if (!init_parse_result.is_error() && init_parse_result.node().has_value()) {
						reparsed_initializer = *init_parse_result.node();
					}
				}

				if (!reparsed_initializer.has_value()) {
					return false;
				}
				if (!consume(";"_tok)) {
					return false;
				}

				substituted_initializer = substituteTemplateParameters(
					*reparsed_initializer,
					substitution_context);
				return true;
			};

			try {
				if (!try_reparse_out_of_line_static_initializer()) {
					substituted_initializer = substituteTemplateParameters(
						*out_of_line_var.initializer,
						out_of_line_var.template_params,
						template_args_to_use);
				}
			} catch (const std::exception& e) {
				FLASH_LOG(Templates, Error, "Exception during template parameter substitution for static member ",
						  out_of_line_var.member_name, ": ", e.what());
				throw;
			}
		}

		// Add the static member to the instantiated struct (or update if it already exists)
		if (out_of_line_var.type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_spec = out_of_line_var.type_node.as<TypeSpecifierNode>();
			auto [member_size, member_alignment] =
				calculateResolvedMemberSizeAndAlignment(type_spec, type_spec.type_index());
			bool is_array = false;
			std::vector<size_t> array_dimensions;

			StringHandle static_member_name_handle = out_of_line_var.member_name;

			// Check if this static member was already added (e.g., from primary template processing)
			// If it exists, update the initializer; otherwise add a new member
			StructStaticMember* existing_member = struct_info_ptr->findStaticMember(static_member_name_handle);
			if (existing_member != nullptr) {
				if (out_of_line_is_constexpr) {
					existing_member->is_constexpr = true;
				}
				if (out_of_line_var.declaration.has_value()) {
					existing_member->setDeclaration(*out_of_line_var.declaration);
				}
				if (out_of_line_var.initializer_position.has_value()) {
					existing_member->setInitializerPosition(*out_of_line_var.initializer_position);
				}
				existing_member->setInitializerDefinitionLookupContext(
					out_of_line_var.definition_lookup_context);
				// Member already exists - update the initializer with the out-of-line definition
				if (substituted_initializer.has_value()) {
					existing_member->initializer = substituted_initializer;
					existing_member->normalized_init.reset();
					FLASH_LOG(Templates, Debug, "Updated out-of-line static member initializer for ", out_of_line_var.member_name,
							  " in instantiated struct ", instantiated_name);
				}
			} else {
				if (type_spec.is_array()) {
					is_array = true;
					for (size_t dim_size : type_spec.array_dimensions()) {
						array_dimensions.push_back(dim_size);
						member_size *= dim_size;
					}
				}
				struct_info_ptr->addStaticMember(
					static_member_name_handle,
					type_spec.type_index(),
					member_size,
					member_alignment,
					AccessSpecifier::Public,
					substituted_initializer,
					type_spec.cv_qualifier(),
					type_spec.reference_qualifier(),
					/* ptr_depth */ static_cast<int>(type_spec.pointer_depth()),
					is_array,
					array_dimensions,
					out_of_line_var.declaration,
					out_of_line_var.initializer_position,
					out_of_line_var.definition_lookup_context,
					out_of_line_is_constexpr);

				FLASH_LOG(Templates, Debug, "Added out-of-line static member ", out_of_line_var.member_name,
						  " to instantiated struct ", instantiated_name);
			}
		}
	}

	// Copy static members from the primary template
	// Get the primary template's StructTypeInfo
	auto primary_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(template_name));
	if (primary_type_it != getTypesByNameMap().end()) {
		const TypeInfo* primary_type_info = primary_type_it->second;
		const StructTypeInfo* primary_struct_info = primary_type_info->getStructInfo();
		if (primary_struct_info) {
			for (const auto& static_member : primary_struct_info->static_members) {

				// Check if initializer contains sizeof...(pack_name) and substitute with pack size
				std::optional<ASTNode> substituted_initializer = static_member.initializer;
				if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
					const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();

					// Calculate pack size for substitution
					auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
						for (size_t i = 0; i < template_params.size(); ++i) {
							const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[i]);
							if (tparam != nullptr && tparam->name() == pack_name && tparam->is_variadic()) {
								size_t non_variadic_count = 0;
								for (const auto& param : template_params) {
									const TemplateParameterNode* inner_param = tryGetTemplateParameterNode(param);
									if (inner_param != nullptr && !inner_param->is_variadic()) {
										non_variadic_count++;
									}
								}
								return template_args_to_use.size() - non_variadic_count;
							}
						}
						return std::nullopt;
					};

					// Helper to create a numeric literal from pack size
					auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
						std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
						Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
						return emplace_node<ExpressionNode>(
							NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), TypeCategory::Int, TypeQualifier::None, 32));
					};

					if (const auto* sizeof_pack_ptr = std::get_if<SizeofPackNode>(&expr)) {
						// Direct sizeof... expression
						const SizeofPackNode& sizeof_pack = *sizeof_pack_ptr;
						if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
							substituted_initializer = make_pack_size_literal(*pack_size);
						}
					} else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
						// Binary expression like "1 + sizeof...(Rest)" - need to substitute sizeof...
						const BinaryOperatorNode& bin_expr = std::get<BinaryOperatorNode>(expr);

						// Helper to extract pack size from various expression forms (including static_cast)
						auto try_extract_pack_size = [&](const ExpressionNode& e) -> std::optional<size_t> {
							if (const auto* inner_sizeof_pack_ptr = std::get_if<SizeofPackNode>(&e)) {
								const SizeofPackNode& sizeof_pack = *inner_sizeof_pack_ptr;
								return calculate_pack_size(sizeof_pack.pack_name());
							}
							// Handle static_cast<T>(sizeof...(Ts))
							if (std::holds_alternative<StaticCastNode>(e)) {
								const StaticCastNode& cast_node = std::get<StaticCastNode>(e);
								if (cast_node.expr().is<ExpressionNode>()) {
									const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
									if (const auto* inner_sizeof_pack_ptr2 = std::get_if<SizeofPackNode>(&cast_inner)) {
										const SizeofPackNode& sizeof_pack = *inner_sizeof_pack_ptr2;
										return calculate_pack_size(sizeof_pack.pack_name());
									}
								}
							}
							return std::nullopt;
						};

						// Helper to extract numeric value from expression
						auto try_extract_numeric = [](const ExpressionNode& e) -> std::optional<unsigned long long> {
							if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&e)) {
								const NumericLiteralNode& num = *numeric_literal;
								auto val = num.value();
								return std::holds_alternative<unsigned long long>(val)
										   ? std::get<unsigned long long>(val)
										   : static_cast<unsigned long long>(std::get<double>(val));
							}
							return std::nullopt;
						};

						// Helper to evaluate a binary expression
						auto evaluate_binary = [](std::string_view op, unsigned long long lhs, unsigned long long rhs) -> unsigned long long {
							if (op == "+")
								return lhs + rhs;
							if (op == "-")
								return lhs - rhs;
							if (op == "*")
								return lhs * rhs;
							if (op == "/")
								return rhs != 0 ? lhs / rhs : 0;
							return 0;
						};

						// Try to evaluate the top-level binary expression
						if (bin_expr.get_lhs().is<ExpressionNode>() && bin_expr.get_rhs().is<ExpressionNode>()) {
							const ExpressionNode& lhs_expr = bin_expr.get_lhs().as<ExpressionNode>();
							const ExpressionNode& rhs_expr = bin_expr.get_rhs().as<ExpressionNode>();

							// Case 1: LHS is pack_size_expr (direct or via static_cast), RHS is numeric
							if (auto lhs_pack = try_extract_pack_size(lhs_expr)) {
								if (auto rhs_num = try_extract_numeric(rhs_expr)) {
									unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_pack, *rhs_num);
									substituted_initializer = make_pack_size_literal(result);
								}
							}
							// Case 2: LHS is numeric, RHS is pack_size_expr
							else if (auto lhs_num = try_extract_numeric(lhs_expr)) {
								if (auto rhs_pack = try_extract_pack_size(rhs_expr)) {
									unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_num, *rhs_pack);
									substituted_initializer = make_pack_size_literal(result);
								}
							}
							// Case 3: LHS is nested binary expression (e.g., static_cast<int>(sizeof...(Ts)) * 2), RHS is numeric
							// Handles patterns like (static_cast<int>(sizeof...(Ts)) * 2) + 40
							else if (std::holds_alternative<BinaryOperatorNode>(lhs_expr)) {
								const BinaryOperatorNode& nested_bin = std::get<BinaryOperatorNode>(lhs_expr);
								if (nested_bin.get_lhs().is<ExpressionNode>() && nested_bin.get_rhs().is<ExpressionNode>()) {
									const ExpressionNode& nested_lhs = nested_bin.get_lhs().as<ExpressionNode>();
									const ExpressionNode& nested_rhs = nested_bin.get_rhs().as<ExpressionNode>();

									std::optional<unsigned long long> nested_result;
									if (auto nlhs_pack = try_extract_pack_size(nested_lhs)) {
										if (auto nrhs_num = try_extract_numeric(nested_rhs)) {
											nested_result = evaluate_binary(nested_bin.op(), *nlhs_pack, *nrhs_num);
										}
									} else if (auto nlhs_num = try_extract_numeric(nested_lhs)) {
										if (auto nrhs_pack = try_extract_pack_size(nested_rhs)) {
											nested_result = evaluate_binary(nested_bin.op(), *nlhs_num, *nrhs_pack);
										}
									}

									if (nested_result) {
										if (auto rhs_num = try_extract_numeric(rhs_expr)) {
											unsigned long long result = evaluate_binary(bin_expr.op(), *nested_result, *rhs_num);
											substituted_initializer = make_pack_size_literal(result);
										}
									}
								}
							}
						}
					}
					// Handle template parameter reference substitution using shared helper lambda
					else if (std::holds_alternative<TemplateParameterReferenceNode>(expr) ||
							 std::holds_alternative<IdentifierNode>(expr)) {
						std::string_view param_name;
						if (const auto* template_parameter_reference = std::get_if<TemplateParameterReferenceNode>(&expr)) {
							param_name = template_parameter_reference->param_name().view();
						} else {
							param_name = std::get<IdentifierNode>(expr).name();
						}

						// Use shared helper lambda defined at function scope
						if (auto subst = substitute_template_param_in_initializer(param_name, template_args_to_use, template_params_typed)) {
							substituted_initializer = subst;
							FLASH_LOG(Templates, Debug, "Substituted static member initializer template parameter '", param_name, "'");
						}
					}
					// Handle TernaryOperatorNode where the condition is a template parameter (e.g., IsArith ? 42 : TypeIndex{})
					else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
						const TernaryOperatorNode& ternary = std::get<TernaryOperatorNode>(expr);
						const ASTNode& cond_node = ternary.condition();

						// Check if condition is a template parameter reference or identifier
						if (cond_node.is<ExpressionNode>()) {
							const ExpressionNode& cond_expr = cond_node.as<ExpressionNode>();
							std::optional<int64_t> cond_value;

							if (std::holds_alternative<TemplateParameterReferenceNode>(cond_expr)) {
								const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(cond_expr);
								FLASH_LOG(Templates, Debug, "Ternary condition is template parameter: ", tparam_ref.param_name());

								// Look up the parameter value
								for (size_t p = 0; p < template_params.size(); ++p) {
									const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[p]);
									if (tparam != nullptr &&
										tparam->name() == tparam_ref.param_name() &&
										tparam->kind() == TemplateParameterKind::NonType) {
										if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
											cond_value = template_args_to_use[p].value;
											FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
										}
										break;
									}
								}
							} else if (std::holds_alternative<IdentifierNode>(cond_expr)) {
								const IdentifierNode& id_node = std::get<IdentifierNode>(cond_expr);
								std::string_view id_name = id_node.name();
								FLASH_LOG(Templates, Debug, "Ternary condition is identifier: ", id_name);

								// Look up the identifier as a template parameter
								for (size_t p = 0; p < template_params.size(); ++p) {
									const TemplateParameterNode* tparam = tryGetTemplateParameterNode(template_params[p]);
									if (tparam != nullptr &&
										tparam->name() == id_name &&
										tparam->kind() == TemplateParameterKind::NonType) {
										if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
											cond_value = template_args_to_use[p].value;
											FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
										}
										break;
									}
								}
							}

							// If we found the condition value, evaluate the ternary
							if (cond_value.has_value()) {
								const ASTNode& result_branch = (*cond_value != 0) ? ternary.true_expr() : ternary.false_expr();

								if (result_branch.is<ExpressionNode>()) {
									const ExpressionNode& result_expr = result_branch.as<ExpressionNode>();
									if (std::holds_alternative<NumericLiteralNode>(result_expr)) {
										const NumericLiteralNode& lit = std::get<NumericLiteralNode>(result_expr);
										const auto& val = lit.value();
										unsigned long long num_val = std::holds_alternative<unsigned long long>(val)
																		 ? std::get<unsigned long long>(val)
																		 : static_cast<unsigned long long>(std::get<double>(val));

										// Create a new numeric literal with the evaluated result
										std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(num_val)).commit();
										Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
										substituted_initializer = emplace_node<ExpressionNode>(
											NumericLiteralNode(num_token, num_val, lit.type(), lit.qualifier(), lit.sizeInBits()));
										FLASH_LOG(Templates, Debug, "Evaluated ternary to: ", num_val);
									}
								}
							}
						}
					}
				}

				// Use struct_info_ptr instead of struct_info (which was moved)
				// Phase 7B: Intern static member name and use StringHandle overload
				StringHandle static_member_name_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(static_member.getName()));

				// Check if this static member was already added (e.g., by lazy instantiation path)
				// If it exists but has no initializer, update it with the substituted initializer
				// This ensures lazy instantiation registrations get their initializers filled in
				StructStaticMember* existing_member = struct_info_ptr->findStaticMember(static_member_name_handle);
				if (existing_member != nullptr) {
					// Member already exists - update only lazy placeholders that were added without an initializer.
					if (substituted_initializer.has_value() && !existing_member->initializer.has_value()) {
						existing_member->initializer = substituted_initializer;
						if (static_member.declaration.has_value()) {
							existing_member->setDeclaration(*static_member.declaration);
						}
						if (static_member.initializer_position.has_value()) {
							existing_member->setInitializerPosition(*static_member.initializer_position);
						}
						existing_member->setInitializerDefinitionLookupContext(
							static_member.initializerDefinitionLookupContext());
						existing_member->normalized_init.reset();
					}
					// Skip adding duplicate
				} else {
					struct_info_ptr->addStaticMember(
						static_member_name_handle,
						static_member.type_index,
						static_member.size,
						static_member.alignment,
						static_member.access,
						substituted_initializer, // Use substituted initializer if sizeof... was replaced
						static_member.cv_qualifier,
						static_member.reference_qualifier,
						static_member.pointer_depth,
						static_member.is_array,
						static_member.array_dimensions,
						static_member.declaration,
						static_member.initializer_position,
						static_member.initializerDefinitionLookupContext(),
						static_member.is_constexpr);
				}
			}
		}
	}

	// PHASE 2: Parse deferred template member function bodies (two-phase lookup)
	// Explicit instantiations still materialize deferred bodies immediately.
	// Implicit instantiations register signature-only stubs in the lazy registry
	// above; reparsing every deferred body here would eagerly instantiate unused
	// members and incorrectly diagnose dependent bodies such as std::pair::swap
	// for const keys.
	// Exception: if any deferred body is a constructor or destructor (special
	// members that require eager replay regardless of explicit/implicit status),
	// replay is required so the constructor/destructor bodies are materialized.
	bool has_implicit_special = false;
	for (const auto& deferred : template_class.deferred_bodies()) {
		if (deferred.identity.kind == DeferredMemberIdentity::Kind::Constructor ||
			deferred.identity.kind == DeferredMemberIdentity::Kind::Destructor) {
			has_implicit_special = true;
			break;
		}
	}
	const bool skip_deferred_body_replay = is_implicit_instantiation && !has_implicit_special;
	if (!skip_deferred_body_replay && !template_class.deferred_bodies().empty()) {
		FLASH_LOG(Templates, Debug, "Parsing ", template_class.deferred_bodies().size(),
				  " deferred template member function bodies for ", instantiated_name);

		// Save current position before parsing deferred bodies
		// We need to restore this after parsing so the parser continues from the correct location
		SaveHandle saved_pos = save_token_position();
		FLASH_LOG(Templates, Debug, "Saved current position: ", saved_pos);

		// Parse each deferred body
		// Note: parse_delayed_function_body internally restores to body_start, parses, then leaves position at end of body
		for (const auto& deferred : template_class.deferred_bodies()) {
			FLASH_LOG(Templates, Debug, "About to parse body for ", deferred.identity.original_lookup_name, " at position ", deferred.body_start);

			// Find the corresponding member function in the instantiated struct
			FunctionDeclarationNode* target_func = nullptr;
			ConstructorDeclarationNode* target_ctor = nullptr;
			DestructorDeclarationNode* target_dtor = nullptr;

			// Invariant (Slice 3+): every DeferredTemplateMemberBody has a valid
			// original_member_node that was inserted into source_member_to_stub during
			// stub creation.  Identity-map lookup must always succeed here.
			// Slice 5: the fallback name scan has been removed.

			// Slice 3: try identity-map lookup first (source-member → instantiated stub).
			{
				const void* src_key = sourceMemberAstNodeKey(deferred.identity.original_member_node);
				if (src_key) {
					auto it = source_member_identity_maps.by_node.find(src_key);
					if (it != source_member_identity_maps.by_node.end()) {
						ASTNode& stub = it->second;
						if (stub.is<FunctionDeclarationNode>())
							target_func = &stub.as<FunctionDeclarationNode>();
						else if (stub.is<ConstructorDeclarationNode>())
							target_ctor = &stub.as<ConstructorDeclarationNode>();
						else if (stub.is<DestructorDeclarationNode>())
							target_dtor = &stub.as<DestructorDeclarationNode>();
					}
				}
			}

			if (!target_func && !target_ctor && !target_dtor) {
				std::string error_msg = std::string(StringBuilder()
					.append("Could not find member function ")
					.append(deferred.identity.original_lookup_name)
					.append(" in instantiated struct ")
					.append(instantiated_name)
					.commit());
				if (force_eager) {
					throw InternalError(error_msg);
				}
				FLASH_LOG(Templates, Error, error_msg);
				continue;
			}

			// Restore position to the function body
			restore_token_position(deferred.body_start);

			// Convert DeferredTemplateMemberBody back to DelayedFunctionBody for parsing
			DelayedFunctionBody delayed;
			delayed.func_node = target_func;
			delayed.body_start = deferred.body_start;
			delayed.initializer_list_start = deferred.initializer_list_start;
			delayed.has_initializer_list = deferred.has_initializer_list;
			delayed.struct_name = instantiated_name; // Use INSTANTIATED name, not template name
			delayed.struct_type_index = struct_type_info.type_index_; // Now valid!
			delayed.struct_node = &instantiated_struct_ref; // Use instantiated struct
			delayed.is_constructor = (deferred.identity.kind == DeferredMemberIdentity::Kind::Constructor);
			delayed.is_destructor = (deferred.identity.kind == DeferredMemberIdentity::Kind::Destructor);
			delayed.ctor_node = target_ctor;
			delayed.dtor_node = target_dtor;
			// Use template argument names for template parameter substitution
			for (const auto& param_name : deferred.template_param_names) {
				delayed.template_param_names.push_back(param_name);
			}

			// Set up template parameter substitution context
			// Map template parameter names to actual types and values
			setCurrentTemplateParamNames(delayed.template_param_names);

			// Build substitutions from the normalized template environment so value
			// parameters keep full typed identity metadata (pointer/reference/function
			// pointer NTTP payload) instead of only integral values.
			template_param_substitutions_.clear();
			TemplateEnvironment deferred_body_environment = buildTemplateEnvironment(
				effective_template_params,
				effective_template_args,
				nullptr);
			populateTemplateParamSubstitutions(
				template_param_substitutions_,
				deferred_body_environment);

			FLASH_LOG(Templates, Debug, "About to parse deferred body for ", deferred.identity.original_lookup_name);

			// Parse the body
			std::optional<ASTNode> body;
			auto result = parse_delayed_function_body(delayed, body);

			FLASH_LOG(Templates, Debug, "Finished parse_delayed_function_body, result.is_error()=", result.is_error());

			clearCurrentTemplateParameters();
			template_param_substitutions_.clear(); // Clear substitutions after parsing

			if (result.is_error()) {
				std::string error_msg = std::string(StringBuilder()
					.append("Failed to parse deferred template body for ")
					.append(deferred.identity.original_lookup_name)
					.append(": ")
					.append(result.error_message())
					.commit());
				if (force_eager) {
					throw CompileError(error_msg);
				}
				FLASH_LOG(Templates, Error, error_msg);
				continue;
			}

			FLASH_LOG(Templates, Debug, "Successfully parsed deferred template body for ", deferred.identity.original_lookup_name);
		}

		FLASH_LOG(Templates, Debug, "Finished parsing all deferred bodies");

		// Restore the position we saved before parsing deferred bodies
		// This ensures the parser continues from the correct location after template instantiation
		FLASH_LOG(Templates, Debug, "About to restore to saved position: ", saved_pos);

		// Check if the saved position is still valid
		if (saved_pos >= saved_tokens_.size() || !saved_tokens_[saved_pos].has_value()) {
			FLASH_LOG(Templates, Error, "Saved position ", saved_pos, " not found in saved_tokens_!");
		} else {
			FLASH_LOG(Templates, Debug, "Saved position ", saved_pos, " found, restoring...");
			restore_lexer_position_only(saved_pos);
			FLASH_LOG(Templates, Debug, "Restored to saved position");
		}
	}

	retryNormalizeTemplateStaticMembersAfterDeferredBodies(
		struct_info_ptr,
		this,
		effective_template_params,
		effective_template_args);

	FLASH_LOG(Templates, Debug, "About to return instantiated_struct for ", instantiated_name);

	// Check if the template class has any constructors
	// If not, mark that we need to generate a default one for the instantiation
	struct_info_ptr->needs_default_constructor = !effective_has_constructor;
	FLASH_LOG(Templates, Debug, "Instantiated struct ", instantiated_name, " has_constructor=", effective_has_constructor,
			  ", needs_default_constructor=", struct_info_ptr->needs_default_constructor);

	// Propagate deleted constructor flags from the template pattern to the instantiated struct.
	// Deleted constructors are not added to member_functions (they use continue).
	// Check both the StructDeclarationNode (set by Parser_Templates_Class.cpp specialization paths)
	// and the template pattern's StructTypeInfo (set by Parser_Decl_StructEnum.cpp for primary templates).
	if (class_decl.has_deleted_default_constructor()) {
		struct_info_ptr->has_deleted_default_constructor = true;
		struct_info_ptr->has_deleted_constructor = true;
	}
	if (class_decl.has_deleted_copy_constructor()) {
		struct_info_ptr->has_deleted_copy_constructor = true;
		struct_info_ptr->has_deleted_constructor = true;
	}
	if (class_decl.has_deleted_move_constructor()) {
		struct_info_ptr->has_deleted_move_constructor = true;
		struct_info_ptr->has_deleted_constructor = true;
	}
	// Also check the template pattern's StructTypeInfo (primary template path stores flags there).
	{
		auto tpl_ti = getTypesByNameMap().find(StringTable::getOrInternStringHandle(template_name));
		if (tpl_ti != getTypesByNameMap().end()) {
			if (const StructTypeInfo* tpl_si = tpl_ti->second->getStructInfo()) {
			if (tpl_si->has_deleted_constructor)
				struct_info_ptr->has_deleted_constructor = true;
			if (tpl_si->has_deleted_default_constructor)
				struct_info_ptr->has_deleted_default_constructor = true;
			if (tpl_si->has_deleted_copy_constructor)
				struct_info_ptr->has_deleted_copy_constructor = true;
			if (tpl_si->has_deleted_move_constructor)
				struct_info_ptr->has_deleted_move_constructor = true;
			}
		}
	}
	const TypeIndex self_type_index =
		struct_info_ptr->own_type_index_.value_or(struct_type_info.registeredTypeIndex().withCategory(TypeCategory::Struct));
	synthesize_implicit_copy_constructor_if_needed(
		*struct_info_ptr,
		self_type_index.withCategory(TypeCategory::Struct),
		instantiated_name);

	// Re-evaluate deferred static_asserts with substituted template parameters
	FLASH_LOG(Templates, Debug, "Checking deferred static_asserts for struct '", class_decl.name(),
			  "': found ", class_decl.deferred_static_asserts().size(), " deferred asserts");

	for (const auto& deferred_assert : class_decl.deferred_static_asserts()) {
		FLASH_LOG(Templates, Debug, "Re-evaluating deferred static_assert during template instantiation");

		// Build template parameter name to type mapping for substitution
		ensure_substitution_maps();

		// Create substitution context with template parameter mappings
		ExpressionSubstitutor substitutor(
			name_substitution_map,
			pack_substitution_map,
			*this,
			template_param_order);

		// Substitute template parameters in the condition expression
		ASTNode substituted_expr = substitutor.substitute(deferred_assert.condition_expr);

		// Evaluate the substituted expression
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable, *this);
		eval_ctx.struct_node = &instantiated_struct.as<StructDeclarationNode>();
		populateDeferredStaticAssertEvalTemplateBindings(
			eval_ctx,
			buildTemplateEnvironment(template_params, template_args_to_use, nullptr),
			template_param_order,
			name_substitution_map);

		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_ctx);

		if (!eval_result.success()) {
			std::string error_msg = buildDeferredStaticAssertInstantiationError(
				eval_result.error_message, deferred_assert.message, true);
			if (force_eager) {
				throw CompileError(error_msg);
			}
			FLASH_LOG(Templates, Error, error_msg);
			return std::nullopt;
		}

		// Check if the assertion failed
		if (!eval_result.as_bool()) {
			std::string error_msg = buildDeferredStaticAssertInstantiationError(
				std::string_view(), deferred_assert.message, false);
			if (force_eager) {
				throw CompileError(error_msg);
			}
			FLASH_LOG(Templates, Error, error_msg);
			return std::nullopt;
		}

		FLASH_LOG(Templates, Debug, "Deferred static_assert passed during template instantiation");
	}

	// Mark instantiation complete with the type index
	FlashCpp::gInstantiationQueue.markComplete(inst_key, struct_type_info.type_index_);
	in_progress_guard.dismiss(); // Don't remove from in_progress in destructor

	// Tag the struct with its materialization level before caching.
	// Commit point: state transition NotMaterialized|ShapeOnly -> ShapeOnly|Materialized.
	// ShapeOnly   -> mode is ShapeOnly (shape probe, no commit side effects).
	// Materialized -> mode is HardUse or SoftProbe (full commit, all artifacts registered).
	if (template_instantiation_mode_ == TemplateInstantiationMode::ShapeOnly) {
		instantiated_struct.as<StructDeclarationNode>().mark_shape_only();
	} else {
		instantiated_struct.as<StructDeclarationNode>().mark_materialized();
	}

	// Register in cache for O(1) lookup on future instantiations
	gTemplateRegistry.registerInstantiation(cache_key, instantiated_struct);

	// Return the instantiated struct node for code generation
	return instantiated_struct;
}

// Try to instantiate a member function template during a member function call
// This is called when parsing obj.method(args) where method is a template
