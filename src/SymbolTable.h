#pragma once

#include <unordered_map>
#include <string_view>
#include <stack>
#include <optional>
#include "AstNodeTypes.h"

class SymbolTable {
public:
	bool insert(std::string_view identifier, const TypeInfo* type_info) {
		auto existing_type = lookup(identifier);
		if (existing_type && existing_type != type_info)
			return false;

		symbol_table_stack_.back().emplace(identifier, type_info);
		return true;
	}

	bool contains(std::string_view identifier) const {
		return lookup(identifier) != nullptr;
	}

	const TypeInfo* lookup(std::string_view identifier) const {
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			auto symbolIt = scope.find(identifier);
			if (symbolIt != scope.end()) {
				return symbolIt->second;
			}
		}

		return nullptr;
	}

	void enter_scope() {
		symbol_table_stack_.emplace_back(Scope());
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

private:
	using Scope = std::unordered_map<std::string_view, const TypeInfo*>;
	std::vector<Scope> symbol_table_stack_;
};

static inline SymbolTable gSymbolTable;