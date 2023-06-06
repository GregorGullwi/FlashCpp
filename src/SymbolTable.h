#pragma once

#include <unordered_map>
#include <string_view>
#include <stack>
#include <optional>
#include "AstNodeTypes.h"

class SymbolTable {
public:
	bool insert(std::string_view identifier, ASTNode node) {
		auto existing_type = lookup(identifier);
		if (existing_type && existing_type->type_name() != node.type_name())
			return false;

		symbol_table_stack_.back().emplace(identifier, node);
		return true;
	}

	bool contains(std::string_view identifier) const {
		return lookup(identifier).has_value();
	}

	std::optional<ASTNode> lookup(std::string_view identifier) const {
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			auto symbolIt = scope.find(identifier);
			if (symbolIt != scope.end()) {
				return symbolIt->second;
			}
		}

		return {};
	}

	void enter_scope() {
		symbol_table_stack_.emplace_back(Scope());
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

private:
	using Scope = std::unordered_map<std::string_view, ASTNode>;
	std::vector<Scope> symbol_table_stack_ = { Scope() };
};

static inline SymbolTable gSymbolTable;