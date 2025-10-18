#pragma once

#include <unordered_map>
#include <string_view>
#include <stack>
#include <optional>
#include <vector>
#include "AstNodeTypes.h"
#include "StackString.h"

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

// Namespace path key - stores namespace components without concatenation
// Uses StringType (StackString or std::string depending on USE_OLD_STRING_APPROACH)
using NamespacePath = std::vector<StringType<>>;

// Hash function for NamespacePath to use in unordered_map
struct NamespacePathHash {
	size_t operator()(const NamespacePath& path) const {
		size_t hash = 0;
		for (const auto& component : path) {
			// Combine hashes using a simple but effective method
#if USE_OLD_STRING_APPROACH
			hash ^= std::hash<std::string>{}(component) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
#else
			hash ^= std::hash<std::string_view>{}(component.view()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
#endif
		}
		return hash;
	}
};

// Equality comparison for NamespacePath
struct NamespacePathEqual {
	bool operator()(const NamespacePath& lhs, const NamespacePath& rhs) const {
		if (lhs.size() != rhs.size()) return false;
		for (size_t i = 0; i < lhs.size(); ++i) {
#if USE_OLD_STRING_APPROACH
			if (lhs[i] != rhs[i]) return false;
#else
			if (lhs[i].view() != rhs[i].view()) return false;
#endif
		}
		return true;
	}
};

struct Scope {
	Scope() = default;
	Scope(ScopeType scopeType, size_t scope_level) : scope_type(scopeType), scope_handle{ .scope_level = scope_level } {}
	Scope(ScopeType scopeType, size_t scope_level, StringType<> namespace_name)
		: scope_type(scopeType), scope_handle{ .scope_level = scope_level }, namespace_name(std::move(namespace_name)) {}

	ScopeType scope_type = ScopeType::Block;
	std::unordered_map<std::string_view, ASTNode> symbols;
	ScopeHandle scope_handle;
	StringType<> namespace_name;  // Only used for Namespace scopes
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
			// Build the namespace path without string concatenation
			NamespacePath ns_path = build_current_namespace_path();
			namespace_symbols_[ns_path].emplace(StringType<32>(identifier), node);
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

	void enter_namespace(std::string_view namespace_name) {
		symbol_table_stack_.emplace_back(Scope(ScopeType::Namespace, symbol_table_stack_.size(), StringType<>(namespace_name)));
	}

	void exit_scope() {
		symbol_table_stack_.pop_back();
	}

	// Build the current namespace path as a vector of components
	// For nested namespaces A::B, returns ["A", "B"]
	NamespacePath build_current_namespace_path() const {
		NamespacePath path;
		for (const auto& scope : symbol_table_stack_) {
			if (scope.scope_type == ScopeType::Namespace) {
				path.push_back(scope.namespace_name);
			}
		}
		return path;
	}

	// Lookup a qualified identifier (e.g., "std::print" or "A::B::func")
	// Takes a span/vector of namespace components instead of building a concatenated string
	template<typename StringContainer>
	std::optional<ASTNode> lookup_qualified(const StringContainer& namespaces, std::string_view identifier) const {
		if (namespaces.empty()) {
			return std::nullopt;
		}

		// Build namespace path from the components
		// For "A::B::func", namespaces = ["A", "B"]
		NamespacePath ns_path;
		ns_path.reserve(namespaces.size());
		for (const auto& ns : namespaces) {
			ns_path.emplace_back(StringType<>(std::string_view(ns)));
		}

		// Look up the namespace in our persistent namespace map
		auto ns_it = namespace_symbols_.find(ns_path);
		if (ns_it == namespace_symbols_.end()) {
			return std::nullopt;
		}

		// Look for the identifier in the namespace
		// Use string_view for lookup to avoid creating StringType
		for (const auto& [key, value] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
			if (key == std::string(identifier)) {
#else
			if (key.view() == identifier) {
#endif
				return value;
			}
		}

		return std::nullopt;
	}

	// Get the current namespace name (empty if not in a namespace)
	std::string_view get_current_namespace() const {
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			if (stackIt->scope_type == ScopeType::Namespace) {
#if USE_OLD_STRING_APPROACH
				return stackIt->namespace_name;
#else
				return stackIt->namespace_name.view();
#endif
			}
		}
		return "";
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global, 0 ) };
	// Persistent map of namespace contents
	// Uses NamespacePath (vector of components) as key to avoid string concatenation
	// Maps: namespace_path -> (symbol_name -> ASTNode)
	std::unordered_map<NamespacePath, std::unordered_map<StringType<32>, ASTNode>, NamespacePathHash, NamespacePathEqual> namespace_symbols_;
};

static inline SymbolTable gSymbolTable;