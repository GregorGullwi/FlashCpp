#pragma once

#include <unordered_map>
#include <string_view>
#include <stack>
#include <optional>
#include "AstNodeTypes.h"

enum class ScopeType {
	Global,
	Function,
	Block,
};

struct Scope {
	Scope() = default;
	Scope(ScopeType scopeType) : scope_type(scopeType) {}

	ScopeType scope_type = ScopeType::Block;
	std::unordered_map<std::string_view, ASTNode> symbols;
};

class SymbolTable {
public:
	bool insert(std::string_view identifier, ASTNode node) {
		auto existing_type = lookup(identifier);
		if (existing_type && existing_type->type_name() != node.type_name())
			return false;

		symbol_table_stack_.back().symbols.emplace(identifier, node);
		return true;
	}

	ScopeType get_current_scope_type() const {
		return symbol_table_stack_.back().scope_type;
	}

	bool contains(std::string_view identifier) const {
		return lookup(identifier).has_value();
	}

	std::optional<ASTNode> lookup(std::string_view identifier) const {
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end()) {
				return symbolIt->second;
			}
		}

		return {};
	}

	void enter_scope(ScopeType scopeType) {
		symbol_table_stack_.emplace_back(Scope(scopeType));
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global) };
};

static inline SymbolTable gSymbolTable;