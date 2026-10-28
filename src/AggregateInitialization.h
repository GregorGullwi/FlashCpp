#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "AggregateElementCursor.h"
#include "AstNodeTypes_Stmt.h"

namespace AggregateInitialization {

struct ClauseRange {
	size_t begin = 0;
	size_t end = 0;

	bool empty() const {
		return begin == end;
	}
};

struct BasePartition {
	std::vector<ClauseRange> base_clauses;
	std::vector<ClauseRange> member_clauses;
	std::vector<bool> base_appertains_whole;
	size_t member_begin = 0;
	std::optional<size_t> empty_element_clause;
	std::optional<size_t> excess_clause;
};

template <typename AppertainsWhole>
BasePartition partitionBases(
	const StructTypeInfo& root_struct_info,
	const InitializerListNode& init_list,
	AppertainsWhole&& appertains_whole) {
	BasePartition partition;
	partition.base_clauses.resize(root_struct_info.base_classes.size());
	partition.member_clauses.resize(root_struct_info.members.size());
	partition.base_appertains_whole.resize(root_struct_info.base_classes.size(), false);
	AggregateElements::ClauseCursor cursor(root_struct_info);
	bool member_seen = false;

	for (size_t clause_index = 0; clause_index < init_list.size(); ++clause_index) {
		if (init_list.is_designated(clause_index)) {
			if (!member_seen) {
				partition.member_begin = clause_index;
				member_seen = true;
			}
			const StringHandle designated_name = init_list.member_name(clause_index);
			for (size_t member_index = 0; member_index < root_struct_info.members.size(); ++member_index) {
				if (root_struct_info.members[member_index].getName() != designated_name) {
					continue;
				}
				partition.member_clauses[member_index] = ClauseRange{clause_index, clause_index + 1};
				break;
			}
			continue;
		}

		const ASTNode& clause = init_list.initializers()[clause_index];
		const bool is_braced = clause.is<InitializerListNode>();
		for (;;) {
			const std::optional<AggregateElements::Element> element = cursor.current();
			if (!element.has_value()) {
				partition.excess_clause = clause_index;
				return partition;
			}
			const bool appertains = is_braced || !element->is_aggregate || appertains_whole(*element, clause);
			if (!appertains) {
				if (element->is_root_element && element->is_aggregate && !element->has_elements) {
					partition.empty_element_clause = clause_index;
					return partition;
				}
				cursor.descendCurrent();
				continue;
			}

			if (element->root_base_index.has_value()) {
				ClauseRange& range = partition.base_clauses[*element->root_base_index];
				if (range.empty()) {
					range.begin = clause_index;
				}
				range.end = clause_index + 1;
				if (!is_braced && element->is_root_element && element->is_aggregate) {
					partition.base_appertains_whole[*element->root_base_index] = true;
				}
			} else {
				if (!member_seen) {
					partition.member_begin = clause_index;
					member_seen = true;
				}
				if (element->root_member_index.has_value()) {
					ClauseRange& range = partition.member_clauses[*element->root_member_index];
					if (range.empty()) {
						range.begin = clause_index;
					}
					range.end = clause_index + 1;
				}
			}
			cursor.consumeCurrent();
			break;
		}
	}

	if (!member_seen) {
		partition.member_begin = init_list.size();
	}
	return partition;
}

} // namespace AggregateInitialization
