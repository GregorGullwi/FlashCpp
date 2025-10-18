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
	Namespace,
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
	Scope(ScopeType scopeType, size_t scope_level, std::string namespace_name)
		: scope_type(scopeType), scope_handle{ .scope_level = scope_level }, namespace_name(std::move(namespace_name)) {}

	ScopeType scope_type = ScopeType::Block;
	std::unordered_map<std::string_view, ASTNode> symbols;
	ScopeHandle scope_handle;
	std::string namespace_name;  // Only used for Namespace scopes
};

class SymbolTable {
public:
	bool insert(std::string_view identifier, ASTNode node) {
		auto existing_type = lookup(identifier);
		if (existing_type && existing_type->type_name() != node.type_name())
			return false;

		symbol_table_stack_.back().symbols.emplace(identifier, node);

		// If we're in a namespace, also add to the persistent namespace map
		if (symbol_table_stack_.back().scope_type == ScopeType::Namespace) {
			// Get the full namespace path for nested namespaces
			std::string full_ns_path = get_full_namespace_path();
			namespace_symbols_[full_ns_path].emplace(std::string(identifier), node);
		}

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

	void enter_namespace(const std::string& namespace_name) {
		symbol_table_stack_.emplace_back(Scope(ScopeType::Namespace, symbol_table_stack_.size(), namespace_name));
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

	// Get the full namespace path (e.g., "A::B" for nested namespaces)
	// This must be defined before lookup_qualified since it's used there
	std::string get_full_namespace_path() const {
		std::string path;
		for (const auto& scope : symbol_table_stack_) {
			if (scope.scope_type == ScopeType::Namespace) {
				if (!path.empty()) {
					path += "::";
				}
				path += scope.namespace_name;
			}
		}
		return path;
	}

	// Lookup a qualified identifier (e.g., "std::print" or "A::B::func")
	std::optional<ASTNode> lookup_qualified(const std::vector<std::string>& namespaces, std::string_view identifier) const {
		if (namespaces.empty()) {
			return std::nullopt;
		}

		// Build the full namespace path by joining all namespace parts
		// For "A::B::func", namespaces = ["A", "B"], so we build "A::B"
		std::string full_namespace_path;
		for (size_t i = 0; i < namespaces.size(); ++i) {
			if (i > 0) {
				full_namespace_path += "::";
			}
			full_namespace_path += namespaces[i];
		}

		// Look up the namespace in our persistent namespace map
		auto ns_it = namespace_symbols_.find(full_namespace_path);
		if (ns_it == namespace_symbols_.end()) {
			return std::nullopt;
		}

		// Look for the identifier in the namespace
		auto symbol_it = ns_it->second.find(std::string(identifier));
		if (symbol_it != ns_it->second.end()) {
			return symbol_it->second;
		}

		return std::nullopt;
	}

	// Get the current namespace name (empty if not in a namespace)
	std::string get_current_namespace() const {
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			if (stackIt->scope_type == ScopeType::Namespace) {
				return stackIt->namespace_name;
			}
		}
		return "";
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global, 0 ) };
	// Persistent map of namespace contents (namespace_name -> (symbol_name -> ASTNode))
	std::unordered_map<std::string, std::unordered_map<std::string, ASTNode>> namespace_symbols_;
};

static inline SymbolTable gSymbolTable;