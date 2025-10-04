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

struct ScopeHandle {
	size_t scope_level = 0;
};

struct SymbolScopeHandle {
	ScopeHandle scope_handle;
	std::string_view identifier;
};

struct Scope {
	Scope() = default;
	Scope(ScopeType scopeType, size_t scope_level) : scope_type(scopeType), scope_handle{ .scope_level = scope_level } {}

	ScopeType scope_type = ScopeType::Block;
	std::unordered_map<std::string_view, ASTNode> symbols;
	ScopeHandle scope_handle;
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

	ScopeHandle get_current_scope_handle() const {
		return ScopeHandle{ .scope_level = symbol_table_stack_.size() };
	}

	bool contains(std::string_view identifier) const {
		return lookup(identifier).has_value();
	}

	std::optional<ASTNode> lookup(std::string_view identifier) const {
		return lookup(identifier, get_current_scope_handle());
	}

	std::optional<ASTNode> lookup(std::string_view identifier, ScopeHandle scope_limit_handle) const {
		for (auto stackIt = symbol_table_stack_.rbegin() + (get_current_scope_handle().scope_level - scope_limit_handle.scope_level); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end()) {
				return symbolIt->second;
			}
		}

		return std::nullopt;
	}

	std::optional<SymbolScopeHandle> get_scope_handle(std::string_view identifier) const {
		return get_scope_handle(identifier, get_current_scope_handle());
	}

 	std::optional<SymbolScopeHandle> get_scope_handle(std::string_view identifier, ScopeHandle scope_limit_handle) const {
 		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
 			const Scope& scope = *stackIt;
 			auto symbolIt = scope.symbols.find(identifier);
 			if (symbolIt != scope.symbols.end()) {
 				return SymbolScopeHandle{ .scope_handle = scope.scope_handle, .identifier = identifier };
 			}
 		}
 
 		return std::nullopt;
 	}

	void enter_scope(ScopeType scopeType) {
		symbol_table_stack_.emplace_back(Scope(scopeType, symbol_table_stack_.size()));
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global, 0 ) };
};

static inline SymbolTable gSymbolTable;