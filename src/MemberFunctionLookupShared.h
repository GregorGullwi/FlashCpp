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

inline bool sameMemberCandidateSet(
	std::span<const StructMemberFunction* const> lhs,
	std::span<const StructMemberFunction* const> rhs) {
	return std::ranges::equal(lhs, rhs);
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

	InlineVector<const StructTypeInfo*, 8> visited;
	auto recurse = [&](const StructTypeInfo* current_struct_info, const auto& self) -> void {
		if (current_struct_info == nullptr) {
			return;
		}
		if (std::ranges::find(visited, current_struct_info) != visited.end()) {
			return;
		}
		visited.push_back(current_struct_info);

		bool found_local_overload = false;
		for (const auto& member_func : current_struct_info->member_functions) {
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
			return;
		}

		for (const auto& base_spec : current_struct_info->base_classes) {
			if (const StructTypeInfo* base_struct =
					tryGetStructTypeInfo(base_spec.type_index)) {
				self(base_struct, self);
			}
		}
	};

	recurse(root_struct_info, recurse);
	return result;
}
