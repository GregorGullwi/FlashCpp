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
	// Changed to support function overloading: each name can map to multiple symbols (for overloaded functions)
	std::unordered_map<std::string_view, std::vector<ASTNode>> symbols;
	ScopeHandle scope_handle;
	StringType<> namespace_name;  // Only used for Namespace scopes

	// Using directives: namespaces to search when looking up unqualified names
	std::vector<NamespacePath> using_directives;

	// Using declarations: specific symbols imported from namespaces
	// Maps: local_name -> (namespace_path, original_name)
	std::unordered_map<std::string_view, std::pair<NamespacePath, std::string_view>> using_declarations;

	// Namespace aliases: Maps alias -> target namespace path
	std::unordered_map<std::string_view, NamespacePath> namespace_aliases;
};

// Helper function to extract parameter types from a FunctionDeclarationNode
inline std::vector<Type> extractParameterTypes(const ASTNode& node) {
	std::vector<Type> param_types;

	if (!node.is<DeclarationNode>()) {
		return param_types;
	}

	// For function declarations, we need to look up the FunctionDeclarationNode
	// This is a simplified version - in practice, we'd need to traverse the AST
	// to find the associated FunctionDeclarationNode
	return param_types;
}

// Helper function to check if two function signatures match
inline bool signaturesMatch(const std::vector<Type>& sig1, const std::vector<Type>& sig2) {
	if (sig1.size() != sig2.size()) {
		return false;
	}

	for (size_t i = 0; i < sig1.size(); ++i) {
		if (sig1[i] != sig2[i]) {
			return false;
		}
	}

	return true;
}

class SymbolTable {
public:
	bool insert(std::string_view identifier, ASTNode node) {
		auto& current_scope = symbol_table_stack_.back();
		auto it = current_scope.symbols.find(identifier);

		// If this is a new identifier, create a new vector
		if (it == current_scope.symbols.end()) {
			current_scope.symbols[identifier] = std::vector<ASTNode>{node};
		} else {
			// Identifier exists - check if we can add this as an overload
			auto& existing_nodes = it->second;

			// For non-function symbols (variables, etc.), don't allow duplicates
			if (!node.is<DeclarationNode>() && !node.is<FunctionDeclarationNode>()) {
				// Check if any existing symbol has a different type
				if (!existing_nodes.empty() && existing_nodes[0].type_name() != node.type_name()) {
					return false;
				}
				// Don't allow duplicate non-function symbols
				return false;
			}

			// For function declarations, allow overloading
			// Check if a function with the same signature already exists
			if (node.is<FunctionDeclarationNode>()) {
				const auto& new_func = node.as<FunctionDeclarationNode>();
				const auto& new_params = new_func.parameter_nodes();

				// Check if a function with the same signature already exists
				for (size_t i = 0; i < existing_nodes.size(); ++i) {
					if (existing_nodes[i].is<FunctionDeclarationNode>()) {
						const auto& existing_func = existing_nodes[i].as<FunctionDeclarationNode>();
						const auto& existing_params = existing_func.parameter_nodes();

						// Check if parameter counts match
						if (new_params.size() == existing_params.size()) {
							// Check if all parameter types match
							bool all_match = true;
							for (size_t j = 0; j < new_params.size(); ++j) {
								const auto& new_param_type = new_params[j].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								const auto& existing_param_type = existing_params[j].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();

								if (new_param_type.type() != existing_param_type.type() ||
								    new_param_type.pointer_depth() != existing_param_type.pointer_depth()) {
									all_match = false;
									break;
								}
							}

							if (all_match) {
								// Same signature found - replace forward declaration with definition if needed
								// If the new one has a definition and the existing one doesn't, replace it
								if (new_func.get_definition().has_value() && !existing_func.get_definition().has_value()) {
									existing_nodes[i] = node;
								}
								// Otherwise, it's a duplicate declaration - just ignore it
								return true;
							}
						}
					}
				}
			}

			// No matching signature found - add as new overload
			existing_nodes.push_back(node);
		}

		// If we're in a namespace, also add to the persistent namespace map
		if (current_scope.scope_type == ScopeType::Namespace) {
			// Build the namespace path without string concatenation
			NamespacePath ns_path = build_current_namespace_path();
			auto& ns_symbols = namespace_symbols_[ns_path];
			StringType<32> key(identifier);

			auto ns_it = ns_symbols.find(key);
			if (ns_it == ns_symbols.end()) {
				ns_symbols[key] = std::vector<ASTNode>{node};
			} else {
				ns_it->second.push_back(node);
			}
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
		// Build the current namespace path once (for efficiency)
		NamespacePath current_ns_path;
		for (const auto& scope : symbol_table_stack_) {
			if (scope.scope_type == ScopeType::Namespace) {
				current_ns_path.push_back(scope.namespace_name);
			}
		}

		for (auto stackIt = symbol_table_stack_.rbegin() + (get_current_scope_handle().scope_level - scope_limit_handle.scope_level); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;

			// First, check direct symbols in this scope
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end() && !symbolIt->second.empty()) {
				// Return the first match for backward compatibility
				return symbolIt->second[0];
			}

			// Second, check using declarations in this scope
			auto usingIt = scope.using_declarations.find(identifier);
			if (usingIt != scope.using_declarations.end()) {
				const auto& [namespace_path, original_name] = usingIt->second;
				// Look up the symbol in the target namespace
				auto result = lookup_qualified(namespace_path, original_name);
				if (result.has_value()) {
					return result;
				}
			}

			// Third, check using directives in this scope
			for (const auto& using_ns : scope.using_directives) {
				auto result = lookup_qualified(using_ns, identifier);
				if (result.has_value()) {
					return result;
				}
			}
		}

		// If we're inside a namespace and haven't found the symbol yet,
		// check the persistent namespace map for namespace-scoped variables
		if (!current_ns_path.empty()) {
			auto ns_it = namespace_symbols_.find(current_ns_path);
			if (ns_it != namespace_symbols_.end()) {
				for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
					if (key == std::string(identifier)) {
#else
					if (key.view() == identifier) {
#endif
						if (!value_vec.empty()) {
							return value_vec[0];
						}
					}
				}
			}
		}

		return std::nullopt;
	}

	// New method to get all overloads of a function
	std::vector<ASTNode> lookup_all(std::string_view identifier) const {
		return lookup_all(identifier, get_current_scope_handle());
	}

	std::vector<ASTNode> lookup_all(std::string_view identifier, ScopeHandle scope_limit_handle) const {
		for (auto stackIt = symbol_table_stack_.rbegin() + (get_current_scope_handle().scope_level - scope_limit_handle.scope_level); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end()) {
				return symbolIt->second;
			}
		}

		return {};
	}

	// Resolve function overload based on argument types
	// Returns the best matching function declaration, or nullopt if no match or ambiguous
	std::optional<ASTNode> lookup_function(std::string_view identifier, const std::vector<Type>& arg_types) const {
		return lookup_function(identifier, arg_types, get_current_scope_handle());
	}

	std::optional<ASTNode> lookup_function(std::string_view identifier, const std::vector<Type>& arg_types, ScopeHandle scope_limit_handle) const {
		// Get all overloads
		auto overloads = lookup_all(identifier, scope_limit_handle);
		if (overloads.empty()) {
			return std::nullopt;
		}

		// If only one overload, return it (for now, we'll add signature checking later)
		if (overloads.size() == 1) {
			return overloads[0];
		}

		// Multiple overloads - need to find the best match
		// For now, return the first one (we'll implement proper overload resolution later)
		// TODO: Implement proper overload resolution with exact match, implicit conversions, etc.
		return overloads[0];
	}

	std::optional<SymbolScopeHandle> get_scope_handle(std::string_view identifier) const {
		return get_scope_handle(identifier, get_current_scope_handle());
	}

 	std::optional<SymbolScopeHandle> get_scope_handle(std::string_view identifier, ScopeHandle scope_limit_handle) const {
 		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
 			const Scope& scope = *stackIt;
 			auto symbolIt = scope.symbols.find(identifier);
 			if (symbolIt != scope.symbols.end() && !symbolIt->second.empty()) {
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

	// Add a using directive to the current scope
	void add_using_directive(const std::vector<StringType<>>& namespace_path) {
		if (symbol_table_stack_.empty()) return;

		Scope& current_scope = symbol_table_stack_.back();
		current_scope.using_directives.push_back(namespace_path);
	}

	// Add a using declaration to the current scope
	void add_using_declaration(std::string_view local_name, const std::vector<StringType<>>& namespace_path, std::string_view original_name) {
		if (symbol_table_stack_.empty()) return;

		Scope& current_scope = symbol_table_stack_.back();
		current_scope.using_declarations[local_name] = std::make_pair(namespace_path, original_name);
	}

	// Add a namespace alias to the current scope
	void add_namespace_alias(std::string_view alias, const std::vector<StringType<>>& target_namespace) {
		if (symbol_table_stack_.empty()) return;

		Scope& current_scope = symbol_table_stack_.back();
		current_scope.namespace_aliases[alias] = target_namespace;
	}

	// Resolve a namespace alias (returns the target namespace path if alias exists)
	std::optional<NamespacePath> resolve_namespace_alias(std::string_view alias) const {
		// Search from current scope backwards
		for (auto it = symbol_table_stack_.rbegin(); it != symbol_table_stack_.rend(); ++it) {
			auto alias_it = it->namespace_aliases.find(alias);
			if (alias_it != it->namespace_aliases.end()) {
				return alias_it->second;
			}
		}
		return std::nullopt;
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

		// Check if the first component is a namespace alias
		if (!namespaces.empty()) {
			std::string_view first_component(namespaces[0]);
			auto alias_resolution = resolve_namespace_alias(first_component);
			if (alias_resolution.has_value()) {
				// Replace the first component with the resolved namespace path
				for (const auto& component : *alias_resolution) {
					ns_path.push_back(component);
				}
				// Add the remaining components
				for (size_t i = 1; i < namespaces.size(); ++i) {
					ns_path.emplace_back(StringType<>(std::string_view(namespaces[i])));
				}
			} else {
				// No alias, use the components as-is
				for (const auto& ns : namespaces) {
					ns_path.emplace_back(StringType<>(std::string_view(ns)));
				}
			}
		}

		// Look up the namespace in our persistent namespace map
		auto ns_it = namespace_symbols_.find(ns_path);
		if (ns_it == namespace_symbols_.end()) {
			return std::nullopt;
		}

		// Look for the identifier in the namespace
		// Use string_view for lookup to avoid creating StringType
		for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
			if (key == std::string(identifier)) {
#else
			if (key.view() == identifier) {
#endif
				// Return the first match for backward compatibility
				if (!value_vec.empty()) {
					return value_vec[0];
				}
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

	// Lookup a nested class by qualified name (e.g., "Outer::Inner")
	std::optional<ASTNode> lookup_nested_class(std::string_view outer_class, std::string_view inner_class) const {
		// First find the outer class
		auto outer = lookup(outer_class);
		if (!outer.has_value() || !outer->is<StructDeclarationNode>()) {
			return std::nullopt;
		}

		// Search nested classes
		const auto& struct_node = outer->as<StructDeclarationNode>();
		for (const auto& nested : struct_node.nested_classes()) {
			if (nested.is<StructDeclarationNode>()) {
				const auto& nested_struct = nested.as<StructDeclarationNode>();
				if (nested_struct.name() == inner_class) {
					return nested;
				}
			}
		}

		return std::nullopt;
	}

	void clear() {
		symbol_table_stack_.clear();
		symbol_table_stack_.emplace_back(Scope(ScopeType::Global, 0));
		namespace_symbols_.clear();
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global, 0 ) };
	// Persistent map of namespace contents
	// Uses NamespacePath (vector of components) as key to avoid string concatenation
	// Maps: namespace_path -> (symbol_name -> vector<ASTNode>) to support overloading
	std::unordered_map<NamespacePath, std::unordered_map<StringType<32>, std::vector<ASTNode>>, NamespacePathHash, NamespacePathEqual> namespace_symbols_;
};

extern SymbolTable gSymbolTable;