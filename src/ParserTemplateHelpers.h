#pragma once

// Auto-generated internal header containing helper functions for template instantiation
// Extracted from Parser_Templates_Inst_ClassTemplate.cpp lines 1-4007

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


inline void buildTemplateParameterReplayState(
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
inline TypeIndex resolveDependentMemberTemplateArtifactsForParam(
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
inline ASTNode buildMaterializedParamTypeNode(
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

struct SourceMemberStructInfoIndexMaps {
	std::unordered_map<const void*, size_t> by_node;
	std::unordered_map<uint64_t, size_t> by_location;
};

enum class ReplaySignatureMatchResult {
	Match,
	Mismatch,
	InsufficientEvidence
};

template <typename InstantiatedDeclNode, typename OutOfLineDeclNode>
inline ReplaySignatureMatchResult declarationsMatchAfterTemplateSubstitution(
	Parser& parser,
	const InstantiatedDeclNode& instantiated_decl,
	const OutOfLineDeclNode& out_of_line_decl,
	std::span<const TemplateParameterNode> template_params,
	std::span<const TemplateTypeArg> template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params);
inline const TypeSpecifierNode* getDeclarationParamTypeNode(const ASTNode& param);
inline bool typeSpecifiersMatchForSignatureValidation(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs);
inline ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const FunctionDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params);
inline ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
	Parser& parser,
	const ConstructorDeclarationNode& candidate,
	const ConstructorDeclarationNode& out_of_line_decl,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name,
	std::span<const TemplateParameterNode> candidate_inner_template_params,
	std::span<const TemplateParameterNode> out_of_line_inner_template_params);

inline const void* sourceMemberAstNodeKey(const ASTNode& node) {
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

inline void mixReplaySourceTokenKey(uint64_t& key, const Token& token) {
	auto mix = [&](uint64_t value) {
		key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
	};
	mix(static_cast<uint64_t>(token.file_index()));
	mix(static_cast<uint64_t>(token.line()));
	mix(static_cast<uint64_t>(token.column()));
	mix(static_cast<uint64_t>(token.handle().handle));
}

inline void mixReplaySourceParamKey(uint64_t& key, const ASTNode& param) {
	if (!param.is<DeclarationNode>()) {
		Token fallback_token(
			Token::Type::Identifier,
			std::string_view(param.type_name()),
			0,
			0,
			0);
		mixReplaySourceTokenKey(key, fallback_token);
		return;
	}

	const DeclarationNode& decl = param.as<DeclarationNode>();
	mixReplaySourceTokenKey(key, decl.type_specifier_node().token());
	if (decl.type_specifier_node().type_index().is_valid()) {
		auto mix = [&](uint64_t value) {
			key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
		};
		mix(static_cast<uint64_t>(decl.type_specifier_node().type_index().index()));
		mix(static_cast<uint64_t>(decl.type_specifier_node().pointer_depth()));
		mix(static_cast<uint64_t>(decl.type_specifier_node().reference_qualifier()));
	}
}

inline std::optional<uint64_t> declarationReplaySourceKey(
	const FunctionDeclarationNode& decl,
	size_t template_param_count) {
	uint64_t key = 0xCBF29CE484222325ull;
	mixReplaySourceTokenKey(key, decl.decl_node().identifier_token());
	auto mix = [&](uint64_t value) {
		key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
	};
	mix(static_cast<uint64_t>(decl.parameter_nodes().size()));
	mix(static_cast<uint64_t>(template_param_count));
	for (const ASTNode& param : decl.parameter_nodes()) {
		mixReplaySourceParamKey(key, param);
	}
	return key;
}

inline std::optional<uint64_t> declarationReplaySourceKey(
	const ConstructorDeclarationNode& decl,
	size_t template_param_count) {
	uint64_t key = 0xCBF29CE484222325ull;
	mixReplaySourceTokenKey(key, decl.name_token());
	auto mix = [&](uint64_t value) {
		key ^= value + 0x9E3779B97F4A7C15ull + (key << 6) + (key >> 2);
	};
	mix(static_cast<uint64_t>(decl.parameter_nodes().size()));
	mix(static_cast<uint64_t>(template_param_count));
	for (const ASTNode& param : decl.parameter_nodes()) {
		mixReplaySourceParamKey(key, param);
	}
	return key;
}

inline std::optional<uint64_t> sourceMemberLocationKey(const ASTNode& member) {
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

inline void registerSourceMemberStubIdentity(
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

inline ASTNode* findSourceMemberStubByIdentity(
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

inline void registerSourceMemberStructInfoIndex(
	SourceMemberStructInfoIndexMaps& index_maps,
	const ASTNode& source_member,
	size_t struct_info_index) {
	auto register_key = [&](const void* key) {
		if (key != nullptr) {
			index_maps.by_node[key] = struct_info_index;
		}
	};
	register_key(sourceMemberAstNodeKey(source_member));
	if (source_member.is<TemplateFunctionDeclarationNode>()) {
		register_key(sourceMemberAstNodeKey(
			source_member.as<TemplateFunctionDeclarationNode>().function_declaration()));
	}
	if (std::optional<uint64_t> location_key = sourceMemberLocationKey(source_member);
		location_key.has_value()) {
		index_maps.by_location[*location_key] = struct_info_index;
	}
}

inline FunctionDeclarationNode* findStructInfoFunctionBySourceMemberIdentity(
	StructTypeInfo* struct_info,
	const SourceMemberStructInfoIndexMaps& index_maps,
	const ASTNode& source_member) {
	if (struct_info == nullptr) {
		return nullptr;
	}

	auto find_index = [&](const void* key) -> std::optional<size_t> {
		if (key == nullptr) {
			return std::nullopt;
		}
		auto it = index_maps.by_node.find(key);
		if (it == index_maps.by_node.end()) {
			return std::nullopt;
		}
		return it->second;
	};

	std::optional<size_t> index = find_index(sourceMemberAstNodeKey(source_member));
	if (!index.has_value() &&
		source_member.is<TemplateFunctionDeclarationNode>()) {
		index = find_index(sourceMemberAstNodeKey(
			source_member.as<TemplateFunctionDeclarationNode>().function_declaration()));
	}
	if (!index.has_value()) {
		if (std::optional<uint64_t> location_key = sourceMemberLocationKey(source_member);
			location_key.has_value()) {
			auto it = index_maps.by_location.find(*location_key);
			if (it != index_maps.by_location.end()) {
				index = it->second;
			}
		}
	}
	if (!index.has_value() ||
		*index >= struct_info->member_functions.size()) {
		return nullptr;
	}

	ASTNode& function_decl = struct_info->member_functions[*index].function_decl;
	return get_function_decl_node_mut(function_decl);
}

struct OutOfLineMemberStubResolution {
	FunctionDeclarationNode* func = nullptr;
	const ASTNode* source_member = nullptr;
	bool ambiguous = false;
	bool insufficient_evidence = false;
};

inline OutOfLineMemberStubResolution findPlainOutOfLineMemberStubByIdentity(
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
	const ASTNode* matched_source_member = nullptr;

	for (size_t source_member_index = 0;
		 source_member_index < source_members.size();
		 ++source_member_index) {
		const StructMemberFunctionDecl& source_member = source_members[source_member_index];
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
		matched_source_member = &source_member.function_declaration;
	}

	if (resolved_matches.size() == 1) {
		resolution.func = resolved_matches.front();
		resolution.source_member = matched_source_member;
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

inline OutOfLineConstructorStubResolution findPlainOutOfLineConstructorStubByIdentity(
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
inline OutOfLineConstructorStubResolution findOutOfLineConstructorTemplateStubByIdentity(
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

inline bool constructorDeclarationsHaveEquivalentInstantiatedSignature(
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

inline bool functionDeclarationsHaveEquivalentInstantiatedSignature(
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
inline OutOfLineConstructorStubResolution findMatchingConstructorInStructInfo(
	StructTypeInfo& struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	EligibleFn&& is_candidate_eligible) {
	OutOfLineConstructorStubResolution resolution;
	const std::optional<uint64_t> target_source_key =
		declarationReplaySourceKey(
			ctor_decl,
			ctor_decl.template_parameters().size());

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

	if (target_source_key.has_value()) {
		for (auto& info_func : struct_info.member_functions) {
			if (!info_func.is_constructor ||
				!info_func.function_decl.is<ConstructorDeclarationNode>()) {
				continue;
			}

			auto& info_ctor = info_func.function_decl.as<ConstructorDeclarationNode>();
			if (!is_candidate_eligible(info_ctor)) {
				continue;
			}
			const std::optional<uint64_t> info_source_key =
				declarationReplaySourceKey(
					info_ctor,
					info_ctor.template_parameters().size());
			if (!info_source_key.has_value() ||
				*info_source_key != *target_source_key) {
				continue;
			}
			if (resolution.ctor != nullptr) {
				resolution.ambiguous = true;
				resolution.ctor = nullptr;
				return resolution;
			}
			resolution.ctor = &info_ctor;
		}
		if (resolution.ctor != nullptr || resolution.ambiguous) {
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
inline OutOfLineFunctionStubResolution findMatchingFunctionInStructInfo(
	StructTypeInfo& struct_info,
	const FunctionDeclarationNode& func_decl,
	EligibleFn&& is_candidate_eligible) {
	OutOfLineFunctionStubResolution resolution;
	const std::optional<uint64_t> target_source_key =
		declarationReplaySourceKey(func_decl, 0);

	for (auto& info_func : struct_info.member_functions) {
		if (info_func.is_constructor) {
			continue;
		}
		FunctionDeclarationNode* info_decl =
			get_function_decl_node_mut(info_func.function_decl);
		if (info_decl == nullptr) {
			continue;
		}
		if (info_decl == &func_decl) {
			if (is_candidate_eligible(*info_decl)) {
				resolution.func = info_decl;
			}
			return resolution;
		}
	}

	if (target_source_key.has_value()) {
		for (auto& info_func : struct_info.member_functions) {
			if (info_func.is_constructor) {
				continue;
			}

			FunctionDeclarationNode* info_decl =
				get_function_decl_node_mut(info_func.function_decl);
			if (info_decl == nullptr) {
				continue;
			}
			if (!is_candidate_eligible(*info_decl)) {
				continue;
			}
			const std::optional<uint64_t> info_source_key =
				declarationReplaySourceKey(*info_decl, 0);
			if (!info_source_key.has_value() ||
				*info_source_key != *target_source_key) {
				continue;
			}
			if (resolution.func != nullptr) {
				resolution.ambiguous = true;
				resolution.func = nullptr;
				return resolution;
			}
			resolution.func = info_decl;
		}
		if (resolution.func != nullptr || resolution.ambiguous) {
			return resolution;
		}
	}

	for (auto& info_func : struct_info.member_functions) {
		if (info_func.is_constructor) {
			continue;
		}

		FunctionDeclarationNode* info_decl =
			get_function_decl_node_mut(info_func.function_decl);
		if (info_decl == nullptr ||
			!is_candidate_eligible(*info_decl) ||
			!functionDeclarationsHaveEquivalentInstantiatedSignature(
				*info_decl,
				func_decl)) {
			continue;
		}
		if (resolution.func != nullptr) {
			resolution.ambiguous = true;
			return resolution;
		}
		resolution.func = info_decl;
	}

	return resolution;
}

inline void setOutOfLineConstructorTemplateReplayMetadata(
	ConstructorDeclarationNode& ctor_decl,
	const OutOfLineMemberFunction& out_of_line_member) {
	ctor_decl.set_template_body_position(out_of_line_member.body_start);
	if (out_of_line_member.has_initializer_list) {
		ctor_decl.set_template_initializer_list_position(
			out_of_line_member.initializer_list_start);
	}
}

inline StringHandle registerLazyConstructorStub(
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
	TemplateDefinitionLookupContext buildLookupContext(
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
	void setTemplateParameters(
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

inline void registerAliasTemplateWithOuterBinding(
	StringHandle alias_name,
	const ASTNode& alias_node,
	const OuterTemplateBinding* outer_binding) {
	gTemplateRegistry.register_alias_template(alias_name, alias_node);
	if (outer_binding != nullptr) {
		gTemplateRegistry.registerOuterTemplateBinding(alias_name, *outer_binding);
	}
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
inline void buildTemplateArgSubstitutionMaps(
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

inline std::string buildDeferredStaticAssertInstantiationError(
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
inline void populateDeferredStaticAssertEvalTemplateBindings(
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
inline DeferredBasePackExpansionBindingInfo collectDeferredBasePackExpansionBindings(
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
inline TemplateArgSubstitutionMap makeDeferredBaseExpansionSubstitutionMap(
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

inline TypeIndex makeDeferredBaseValueTypeIndex(TypeCategory category, TypeIndex type_index) {
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

inline bool isPlaceholderNttpTypeIndex(TypeIndex type_index) {
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

inline TemplateTypeArg makeDeferredBaseValueArg(int64_t value, TypeIndex type_index) {
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
inline std::optional<TemplateTypeArg> tryResolveDeferredBaseTypeArgFromMap(
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
inline std::vector<TemplateTypeArg> materializeTemplateArgsExpandingPacks(
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

inline const TypeSpecifierNode* getDeclarationParamTypeNode(const ASTNode& param) {
	if (!param.is<DeclarationNode>()) {
		return nullptr;
	}
	const ASTNode& type_node = param.as<DeclarationNode>().type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return nullptr;
	}
	return &type_node.as<TypeSpecifierNode>();
}

inline void clampPartialPatternTemplateParamIndirectionForOwner(
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

inline const TemplateTypeArg* findResolvedTypeTemplateArg(
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

inline void applyResolvedTemplateArgTypeMetadata(TypeSpecifierNode& target, const TemplateTypeArg* resolved_arg) {
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

inline bool dependentTemplatePlaceholderNamesMatch(
	const TypeSpecifierNode& instantiated_type,
	const TypeSpecifierNode& out_of_line_type,
	std::span<const TemplateParameterNode> instantiated_template_params,
	std::span<const TemplateParameterNode> out_of_line_template_params);

inline bool isOutOfLineConstructorStubName(
	std::string_view member_name,
	std::string_view template_name,
	std::string_view template_base_name) {
	return !template_name.empty() &&
		(member_name == template_name ||
			(!template_base_name.empty() && member_name == template_base_name));
}

inline bool typeSpecifierLooksLikeDependentSignaturePlaceholder(
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

inline void mergeAliasIndirectionForSignatureValidation(
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

inline SignatureValidationIndirection computeSignatureValidationIndirection(
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

inline SignatureValidationIndirection computeTemplateArgSignatureValidationIndirection(
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

inline bool typeSpecifiersMatchForSignatureValidation(
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

inline std::optional<TypeSpecifierNode> substituteOutOfLineSignatureType(
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

inline const TemplateParameterNode* findTemplateParameterByName(
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

inline std::string normalizeDependentPlaceholderSignatureSpelling(
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

inline TypeIndex canonicalizeTemplateSignatureMatchTypeIndex(TypeIndex type_index) {
	if (!type_index.is_valid()) {
		return type_index;
	}
	if (const ResolvedAliasTypeInfo resolved_alias = resolveAliasTypeInfo(type_index);
		resolved_alias.type_index.is_valid()) {
		return resolved_alias.type_index.withCategory(resolved_alias.typeEnum());
	}
	return type_index;
}

inline TypeIndex canonicalizeTemplateSignatureMatchTypeSpecifierIndex(
	const TypeSpecifierNode& type_spec) {
	const TypeCategory category = type_spec.type_index().is_valid()
		? type_spec.type_index().category()
		: type_spec.type();
	return canonicalizeTemplateSignatureMatchTypeIndex(
		type_spec.type_index().withCategory(category));
}

inline bool shouldPreferTokenOwnerTypeInfoForSignatureRecovery(
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

inline const TypeInfo* resolveDependentMemberPlaceholderAgainstOwnerArtifact(
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

inline bool tryMatchDependentMemberPlaceholderOwnerArtifactSubstitution(
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

inline bool dependentQualifiedTemplateArgMatchesForSignature(
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

inline bool dependentQualifiedNameRecordsMatchForSignature(
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

inline const TypeInfo::DependentQualifiedNameRecord* dependentQualifiedNameRecordForSignatureMatch(
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

inline bool dependentQualifiedNameHasMemberTemplateSegment(
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

inline bool typeSpecifierPreservesDependentMemberTemplateSignatureIdentity(
	const TypeSpecifierNode& type_spec) {
	const TypeInfo* type_info = type_spec.type_index().is_valid()
		? tryGetTypeInfo(type_spec.type_index())
		: nullptr;
	return dependentQualifiedNameHasMemberTemplateSegment(
		dependentQualifiedNameRecordForSignatureMatch(type_info));
}

inline bool typeIndexPreservesDependentMemberTemplateSignatureIdentity(
	TypeIndex type_index) {
	const TypeInfo* type_info = type_index.is_valid()
		? tryGetTypeInfo(type_index)
		: nullptr;
	return dependentQualifiedNameHasMemberTemplateSegment(
		dependentQualifiedNameRecordForSignatureMatch(type_info));
}

inline bool dependentTemplatePlaceholderNamesMatch(
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

inline bool shouldAcceptReplaySubstitutedSignatureMatch(
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

inline void copyDefinitionParameterTypesForDependentMemberTemplateSegments(
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

inline void syncReplayAttachedMemberTemplateParameterTypesFromDefinition(
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

inline void preserveDependentMemberTemplateParameterPlaceholdersFromPattern(
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
inline ReplaySignatureMatchResult declarationsMatchAfterTemplateSubstitution(
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

inline bool isMatchingMemberTemplate(
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

inline ReplaySignatureMatchResult nestedOutOfLineMemberTemplateMatchesCandidate(
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

inline ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
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

inline ReplaySignatureMatchResult outOfLineConstructorTemplateMatchesCandidate(
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

inline void materializeReplayAttachedFunctionParameterTypesFromDefinition(
	Parser& parser,
	std::span<ASTNode> instantiated_params,
	std::span<const ASTNode> definition_params,
	std::span<const TemplateParameterNode> outer_template_params,
	std::span<const TemplateTypeArg> outer_template_args,
	StringHandle owner_type_name) {
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

		std::optional<TypeSpecifierNode> substituted_definition_type =
			substituteOutOfLineSignatureType(
				parser,
				definition_decl->type_specifier_node(),
				outer_template_params,
				outer_template_args,
				owner_type_name);
		if (!substituted_definition_type.has_value()) {
			continue;
		}

		instantiated_decl->set_type_node(*substituted_definition_type);
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

inline OutOfLineFunctionStubResolution findReplayedOutOfLineMemberInStructInfo(
	StructTypeInfo* struct_info,
	FunctionDeclarationNode& replayed_func) {
	if (struct_info == nullptr) {
		return {};
	}

	return findMatchingFunctionInStructInfo(
		*struct_info,
		replayed_func,
		[](const FunctionDeclarationNode& info_func) {
			return !info_func.is_materialized();
		});
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
inline std::optional<std::vector<QualifiedTypeMemberAccess>> resolveDeferredBaseMemberTypeChain(
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

inline int getTemplateArgumentSizeInBytes(const TemplateTypeArg& arg) {
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
inline InlineVector<StringHandle, 4> collectParamNameHandles(
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
inline InlineVector<TypeInfo::TemplateArgInfo, 4> collectEnrichedTemplateArgInfos(
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

inline bool staticMemberInitializerContainsFunctionCall(const ASTNode& node) {
	if (!node.has_value() || !node.is<ExpressionNode>()) {
		return false;
	}

	return RebindStaticMemberAst::visitASTUntil(node, [](const ASTNode& current) {
		return current.is<CallExprNode>();
	});
}

template <typename TemplateParamsContainer>
ConstExpr::EvaluationContext makeStaticMemberInitializerEvaluationContext(
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

inline std::optional<NormalizedInitializer> tryBuildConstantStaticMemberInitializer(
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

inline void instantiateDeferredStaticInitializerCalls(
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
inline std::optional<NormalizedInitializer> tryEarlyNormalizeTemplateStaticMemberInitializer(
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
inline void retryNormalizeTemplateStaticMembersAfterDeferredBodies(
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

inline std::optional<StringHandle> findUnresolvedHardUseTypeSpecifier(const ASTNode& root) {
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

inline const StructDeclarationNode* getCachedTemplateStructDecl(const ASTNode* cached_node) {
	if (!cached_node || !cached_node->is<StructDeclarationNode>()) {
		return nullptr;
	}
	return &cached_node->as<StructDeclarationNode>();
}
