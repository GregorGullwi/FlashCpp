#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "AggregateElementCursor.h"
#include "AstNodeTypes_Stmt.h"
#include "OverloadResolution.h"

namespace AggregateInitialization {

struct ClauseRange {
	size_t begin = 0;
	size_t end = 0;

	bool empty() const {
		return begin == end;
	}
};

enum class InitializationForm : uint8_t {
	None,
	Braced,
	WholeExpression,
	BraceElided,
};

struct ElementAssignment {
	ClauseRange range;
	InitializationForm form = InitializationForm::None;
};

struct BasePartition {
	std::vector<ElementAssignment> base_elements;
	std::vector<ElementAssignment> member_elements;
	size_t member_begin = 0;
	std::optional<size_t> empty_element_clause;
	std::optional<size_t> excess_clause;
};

inline bool acceptsWholeExpression(
	const AggregateElements::Element& element,
	const TypeSpecifierNode& source_type,
	const ASTNode& clause) {
	if (!element.is_aggregate) {
		return true;
	}
	if (element.is_array) {
		return false;
	}
	return buildConversionPlan(source_type, AggregateElements::makeTypeSpecifier(element), &clause).is_valid;
}

struct ClauseSettlement {
	std::optional<AggregateElements::Element> element;
	bool empty_element_requires_braces = false;
};

template <typename AppertainsWhole>
ClauseSettlement settleClause(
	AggregateElements::ClauseCursor& cursor,
	bool is_braced,
	AppertainsWhole&& appertains_whole) {
	for (;;) {
		std::optional<AggregateElements::Element> element = cursor.current();
		if (!element.has_value()) {
			return {};
		}
		const bool appertains = is_braced || !element->is_aggregate || appertains_whole(*element);
		if (!appertains) {
			if (element->is_aggregate && !element->has_elements) {
				cursor.consumeCurrent();
				return ClauseSettlement{element, true};
			}
			cursor.descendCurrent();
			continue;
		}
		cursor.consumeCurrent();
		return ClauseSettlement{element, false};
	}
}

template <typename AppertainsWhole>
BasePartition partitionBases(
	const StructTypeInfo& root_struct_info,
	const InitializerListNode& init_list,
	AppertainsWhole&& appertains_whole) {
	BasePartition partition;
	partition.base_elements.resize(root_struct_info.base_classes.size());
	partition.member_elements.resize(root_struct_info.members.size());
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
				partition.member_elements[member_index] = ElementAssignment{
					ClauseRange{clause_index, clause_index + 1},
					init_list.initializers()[clause_index].is<InitializerListNode>()
						? InitializationForm::Braced
						: InitializationForm::WholeExpression};
				break;
			}
			continue;
		}

		const ASTNode& clause = init_list.initializers()[clause_index];
		const bool is_braced = clause.is<InitializerListNode>();
		auto clause_appertains = [&](const AggregateElements::Element& element) {
			return appertains_whole(element, clause);
		};
		const ClauseSettlement settlement = settleClause(cursor, is_braced, clause_appertains);
		if (!settlement.element.has_value()) {
			partition.excess_clause = clause_index;
			return partition;
		}
		if (settlement.empty_element_requires_braces) {
			partition.empty_element_clause = clause_index;
			return partition;
		}

		const AggregateElements::Element& element = *settlement.element;
		InitializationForm form = InitializationForm::BraceElided;
		if (element.is_root_element) {
			form = is_braced ? InitializationForm::Braced : InitializationForm::WholeExpression;
		}
		if (element.root_element.has_value() && element.root_element->kind == AggregateElements::RootElementKind::Base) {
			ElementAssignment& assignment = partition.base_elements[element.root_element->index];
			if (assignment.range.empty()) {
				assignment = ElementAssignment{ClauseRange{clause_index, clause_index + 1}, form};
			} else {
				assignment.range.end = clause_index + 1;
			}
		} else {
			if (!member_seen) {
				partition.member_begin = clause_index;
				member_seen = true;
			}
			if (element.root_element.has_value()) {
				ElementAssignment& assignment = partition.member_elements[element.root_element->index];
				if (assignment.range.empty()) {
					assignment = ElementAssignment{ClauseRange{clause_index, clause_index + 1}, form};
				} else {
					assignment.range.end = clause_index + 1;
				}
			}
		}
	}

	if (!member_seen) {
		partition.member_begin = init_list.size();
	}
	return partition;
}

} // namespace AggregateInitialization
