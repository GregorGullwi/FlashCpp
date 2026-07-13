#pragma once

#include "AstNodeTypes_DeclNodes.h"
#include "InlineVector.h"
#include <algorithm>
#include <ranges>
#include <span>

struct ConstAwareMemberCandidateSet {
	InlineVector<const StructMemberFunction*, 8> preferred;
	InlineVector<const StructMemberFunction*, 8> compatible;
};

struct DefinitionPreferredMemberOverloadSet {
	InlineVector<ASTNode, 8> all;
	InlineVector<ASTNode, 8> definition_preferred;
	const FunctionDeclarationNode* first = nullptr;
	const FunctionDeclarationNode* first_definition = nullptr;
};

inline void appendUniqueMemberFunctionCandidate(
	InlineVector<const StructMemberFunction*, 8>& target,
	const StructMemberFunction* candidate) {
	if (candidate == nullptr) {
		return;
	}
	if (std::ranges::none_of(target, [&](const StructMemberFunction* existing) {
		return existing == candidate;
	})) {
		target.push_back(candidate);
	}
}

inline void appendUniqueOverloadNode(
	InlineVector<ASTNode, 8>& target,
	const ASTNode& candidate) {
	if (!candidate.raw_pointer()) {
		return;
	}
	if (std::ranges::none_of(target, [&](const ASTNode& existing) {
		return existing.raw_pointer() == candidate.raw_pointer();
	})) {
		target.push_back(candidate);
	}
}

inline bool sameMemberCandidateSet(
	std::span<const StructMemberFunction* const> lhs,
	std::span<const StructMemberFunction* const> rhs) {
	return std::ranges::equal(lhs, rhs);
}

template <typename VisitFn>
void visitStructHierarchyDepthFirst(
	const StructTypeInfo* root_struct_info,
	VisitFn&& visit) {
	InlineVector<const StructTypeInfo*, 8> visited;
	auto recurse = [&](const StructTypeInfo* current_struct_info,
		const auto& self) -> void {
		if (current_struct_info == nullptr ||
			std::ranges::find(visited, current_struct_info) != visited.end()) {
			return;
		}
		visited.push_back(current_struct_info);
		if (!visit(*current_struct_info)) {
			return;
		}
		for (const BaseClassSpecifier& base_spec : current_struct_info->base_classes) {
			self(tryGetStructTypeInfo(base_spec.type_index), self);
		}
	};
	recurse(root_struct_info, recurse);
}

inline void appendUniqueMemberFunctionOverloadNodes(
	InlineVector<ASTNode, 8>& target,
	std::span<const StructMemberFunction* const> members) {
	for (const StructMemberFunction* member : members) {
		if (member == nullptr) {
			continue;
		}
		const ASTNode& function_decl = member->function_decl;
		if (std::ranges::none_of(target, [&](const ASTNode& existing) {
			return existing.raw_pointer() == function_decl.raw_pointer();
		})) {
			target.push_back(function_decl);
		}
	}
}

template <typename AcceptFn>
DefinitionPreferredMemberOverloadSet collectVisibleMemberFunctionOverloadNodes(
	const StructTypeInfo* root_struct_info,
	StringHandle member_name_handle,
	bool stop_at_first_local_name_set,
	AcceptFn&& accept_candidate) {
	DefinitionPreferredMemberOverloadSet result;
	if (root_struct_info == nullptr) {
		return result;
	}

	visitStructHierarchyDepthFirst(root_struct_info, [&](const StructTypeInfo& current_struct_info) {
		bool found_local_overload = false;
		for (const auto& member_func : current_struct_info.member_functions) {
			if (member_func.getName() != member_name_handle ||
				!member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}

			const auto& func_decl =
				member_func.function_decl.as<FunctionDeclarationNode>();
			if (!accept_candidate(member_func, func_decl)) {
				continue;
			}

			found_local_overload = true;
			if (result.first == nullptr) {
				result.first = &func_decl;
			}
			appendUniqueOverloadNode(result.all, member_func.function_decl);
			if (func_decl.get_definition().has_value()) {
				if (result.first_definition == nullptr) {
					result.first_definition = &func_decl;
				}
				appendUniqueOverloadNode(
					result.definition_preferred,
					member_func.function_decl);
			}
		}

		if (found_local_overload && stop_at_first_local_name_set) {
			return false;
		}
		return true;
	});
	return result;
}

template <typename InstantiateOwnerFn, typename AcceptFn>
DefinitionPreferredMemberOverloadSet collectOwnerNamedMemberFunctionOverloads(
	std::string_view owner_name,
	std::string_view member_name,
	InstantiateOwnerFn&& instantiate_owner,
	AcceptFn&& accept_candidate) {
	DefinitionPreferredMemberOverloadSet result;
	if (owner_name.empty() || member_name.empty()) {
		return result;
	}

	const StringHandle owner_handle =
		StringTable::getOrInternStringHandle(owner_name);
	const StringHandle member_handle =
		StringTable::getOrInternStringHandle(member_name);
	instantiate_owner(owner_handle);

	if (const TypeInfo* owner_type_info = findTypeByName(owner_handle);
		owner_type_info != nullptr) {
		if (const StructTypeInfo* struct_info = owner_type_info->getStructInfo()) {
			result = collectVisibleMemberFunctionOverloadNodes(
				struct_info,
				member_handle,
				true,
				std::forward<AcceptFn>(accept_candidate));
		}
	}
	if (!result.all.empty()) {
		return result;
	}

	for (const ASTNode& candidate_node : gSymbolTable.lookup_all(member_name)) {
		const FunctionDeclarationNode* candidate_func =
			get_function_decl_node(candidate_node);
		if (candidate_func == nullptr) {
			continue;
		}

		const std::string_view parent_name =
			candidate_func->parent_struct_name();
		if (parent_name != owner_name &&
			(parent_name.empty() || !parent_name.ends_with(owner_name))) {
			continue;
		}

		if (result.first == nullptr) {
			result.first = candidate_func;
		}
		appendUniqueOverloadNode(result.all, candidate_node);
		if (candidate_func->get_definition().has_value()) {
			if (result.first_definition == nullptr) {
				result.first_definition = candidate_func;
			}
			appendUniqueOverloadNode(
				result.definition_preferred,
				candidate_node);
		}
	}

	return result;
}

template <typename AcceptFn>
ConstAwareMemberCandidateSet collectConstAwareVisibleMemberFunctionCandidates(
	const StructTypeInfo* root_struct_info,
	StringHandle member_name_handle,
	bool receiver_is_const,
	bool stop_at_first_local_name_set,
	AcceptFn&& accept_candidate) {
	ConstAwareMemberCandidateSet result;
	if (root_struct_info == nullptr) {
		return result;
	}

	visitStructHierarchyDepthFirst(root_struct_info, [&](const StructTypeInfo& current_struct_info) {
		bool found_local_overload = false;
		for (const auto& member_func : current_struct_info.member_functions) {
			if (member_func.is_constructor ||
				member_func.is_destructor ||
				member_func.getName() != member_name_handle ||
				!member_func.function_decl.is<FunctionDeclarationNode>()) {
				continue;
			}

			const auto& func_decl =
				member_func.function_decl.as<FunctionDeclarationNode>();
			if (!accept_candidate(member_func, func_decl)) {
				continue;
			}

			found_local_overload = true;
			if (func_decl.is_static()) {
				appendUniqueMemberFunctionCandidate(result.preferred, &member_func);
				appendUniqueMemberFunctionCandidate(result.compatible, &member_func);
				continue;
			}
			if (receiver_is_const) {
				if (!func_decl.is_const_member_function()) {
					continue;
				}
				appendUniqueMemberFunctionCandidate(result.preferred, &member_func);
				appendUniqueMemberFunctionCandidate(result.compatible, &member_func);
				continue;
			}

			appendUniqueMemberFunctionCandidate(result.compatible, &member_func);
			if (!func_decl.is_const_member_function()) {
				appendUniqueMemberFunctionCandidate(result.preferred, &member_func);
			}
		}

		if (found_local_overload && stop_at_first_local_name_set) {
			return false;
		}
		return true;
	});
	return result;
}
