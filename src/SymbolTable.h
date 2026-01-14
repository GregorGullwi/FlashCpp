#pragma once

#include <unordered_map>
#include <string_view>
#include <stack>
#include <optional>
#include <vector>
#include <functional>
#include "AstNodeTypes.h"
#include "Token.h"
#include "StackString.h"
#include "Log.h"
#include "ChunkedString.h"

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
	// Use string_view keys with a dedicated ChunkedStringAllocator in SymbolTable to avoid copies
	// while ensuring strings remain valid for the lifetime of the symbol table.
	// String views are interned using the symbol table's allocator before being used as keys.
	std::unordered_map<std::string_view, std::vector<ASTNode>> symbols;
	ScopeHandle scope_handle;
	StringType<> namespace_name;  // Only used for Namespace scopes

	// Using directives: namespaces to search when looking up unqualified names
	std::vector<NamespacePath> using_directives;

	// Using declarations: specific symbols imported from namespaces
	// Maps: local_name -> (namespace_path, original_name)
	// Use string_view with dedicated allocator for both key and original_name
	std::unordered_map<std::string_view, std::pair<NamespacePath, std::string_view>> using_declarations;

	// Namespace aliases: Maps alias -> target namespace path
	// Use string_view keys with dedicated allocator
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
	bool insert([[maybe_unused]] const std::string& identifier, [[maybe_unused]] ASTNode node) {
		assert(false && "Use StringBuilder to pass a string_view to SymbolTable::insert, don't use std::string");
		return false;
	}

	bool insert(StringHandle identifierHandle, ASTNode node) {
		return insert(StringTable::getStringView(identifierHandle), node);	// This should probably be the other way around
	}

	bool insert(std::string_view identifier, ASTNode node) {
		auto& current_scope = symbol_table_stack_.back();
		// First, try to find the identifier without interning
		auto it = current_scope.symbols.find(identifier);

		// If this is a new identifier, intern it and create a new vector
		if (it == current_scope.symbols.end()) {
			std::string_view key = intern_string(identifier);
			current_scope.symbols[key] = std::vector<ASTNode>{node};
		} else {
			// Identifier exists - check if we can add this as an overload
			auto& existing_nodes = it->second;

			// For non-function symbols (variables, etc.), don't allow duplicates in the same scope
			// This includes DeclarationNode and VariableDeclarationNode
			if (!node.is<FunctionDeclarationNode>()) {
				// Check if any existing symbol has a different type
				if (!existing_nodes.empty() && existing_nodes[0].type_name() != node.type_name()) {
					return false;
				}
				// Don't allow duplicate non-function symbols in the same scope
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

								if (!new_param_type.matches_signature(existing_param_type)) {
									all_match = false;
									break;
								}
							}

							// Also check return types for template specializations
							// (e.g., get<0> returns int, get<1> returns double - different specializations)
							if (all_match) {
								const auto& new_return_type = new_func.decl_node().type_node().as<TypeSpecifierNode>();
								const auto& existing_return_type = existing_func.decl_node().type_node().as<TypeSpecifierNode>();
								if (!new_return_type.matches_signature(existing_return_type)) {
									all_match = false;  // Different return types = different specializations
								}
							}

							if (all_match) {
								// Same signature found - replace forward declaration with definition if needed
								// If the new one has a definition and the existing one doesn't, replace it
								if (new_func.get_definition().has_value() && !existing_func.get_definition().has_value()) {
									existing_nodes[i] = node;
									
									// Also update the namespace_symbols_ map if we're in a namespace or global scope
									if (current_scope.scope_type == ScopeType::Namespace || current_scope.scope_type == ScopeType::Global) {
										NamespacePath ns_path = build_current_namespace_path();
										auto& ns_symbols = namespace_symbols_[ns_path];
										StringType<32> key(identifier);
										
										auto ns_it = ns_symbols.find(key);
										if (ns_it != ns_symbols.end()) {
											// Find and replace the matching node in namespace_symbols_
											for (size_t k = 0; k < ns_it->second.size(); ++k) {
												if (ns_it->second[k].is<FunctionDeclarationNode>()) {
													const auto& ns_func = ns_it->second[k].as<FunctionDeclarationNode>();
													const auto& ns_params = ns_func.parameter_nodes();
													
													// Check if this is the same signature
													if (ns_params.size() == new_params.size()) {
														bool params_match = true;
														for (size_t m = 0; m < ns_params.size(); ++m) {
															const auto& ns_param_type = ns_params[m].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
															const auto& new_param_type = new_params[m].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
															if (!ns_param_type.matches_signature(new_param_type)) {
																params_match = false;
																break;
															}
														}
														if (params_match) {
															ns_it->second[k] = node;
															break;
														}
													}
												}
											}
										}
									}
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

		// If we're in a namespace or global scope, also add to the persistent namespace map
		// For global scope, use an empty namespace path (represents ::identifier)
		if (current_scope.scope_type == ScopeType::Namespace || current_scope.scope_type == ScopeType::Global) {
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

	// Insert a symbol into the global scope (scope_level 0) regardless of current scope
	// This is useful for variable template instantiations that happen during function parsing
	bool insertGlobal(std::string_view identifier, ASTNode node) {
		if (symbol_table_stack_.empty()) {
			return false;  // No global scope exists
		}

		auto& global_scope = symbol_table_stack_[0];  // Global scope is always at index 0
		// First, try to find the identifier without interning
		auto it = global_scope.symbols.find(identifier);

		// If this is a new identifier, intern it and create a new vector
		if (it == global_scope.symbols.end()) {
			std::string_view key = intern_string(identifier);
			global_scope.symbols[key] = std::vector<ASTNode>{node};
			return true;
		}

		// Identifier exists - for global variables, don't allow duplicates
		// (We could enhance this to handle overloading if needed)
		return false;
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

	bool contains(StringHandle identifierHandle) const {
		return lookup(StringTable::getStringView(identifierHandle)).has_value();
	}

	std::optional<ASTNode> lookup(std::string_view identifier) const {
		return lookup(identifier, get_current_scope_handle());
	}

	std::optional<ASTNode> lookup(StringHandle identifierHandle) const {
		return lookup(StringTable::getStringView(identifierHandle), get_current_scope_handle());
	}

	std::optional<ASTNode> lookup_with_template_check(StringHandle identifier, std::function<bool(std::string_view)> is_template_param) const {
		return lookup_with_template_check(StringTable::getStringView(identifier), std::move(is_template_param));
	}
	// Lookup with template parameter checking callback
	std::optional<ASTNode> lookup_with_template_check(std::string_view identifier, std::function<bool(std::string_view)> is_template_param) const {
		// First check if it's a template parameter
		if (is_template_param(identifier)) {
			// Return a special marker - the caller should create a TemplateParameterReferenceNode
			return std::nullopt;  // We'll handle this in the caller
		}

		// Otherwise, do normal lookup
		return lookup(identifier);
	}

	// Check if an identifier is a template parameter (called from Parser)
	bool is_template_parameter(std::string_view name) const;
	bool is_template_parameter(StringHandle name) const {
		return is_template_parameter(StringTable::getStringView(name));
	}

	std::optional<ASTNode> lookup(StringHandle identifier, ScopeHandle scope_limit_handle) const {
		return lookup(StringTable::getStringView(identifier), scope_limit_handle);
	}

	std::optional<ASTNode> lookup(std::string_view identifier, ScopeHandle scope_limit_handle) const {
		// Build the full current namespace path first
		NamespacePath full_ns_path;
		for (const auto& scope : symbol_table_stack_) {
			if (scope.scope_type == ScopeType::Namespace) {
				full_ns_path.push_back(scope.namespace_name);
			}
		}
		
		// Track which namespace paths we've already checked in namespace_symbols_
		std::vector<NamespacePath> checked_ns_paths;

		for (auto stackIt = symbol_table_stack_.rbegin() + (get_current_scope_handle().scope_level - scope_limit_handle.scope_level); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;

			// First, check using declarations in this scope (they introduce names into the current scope)
			auto usingIt = scope.using_declarations.find(identifier);
			if (usingIt != scope.using_declarations.end()) {
				const auto& [namespace_path, original_name] = usingIt->second;
				// Look up the symbol in the target namespace
				// If namespace_path is empty, it means global namespace (::name)
				std::optional<ASTNode> result;
				if (namespace_path.empty()) {
					// Global namespace - look up in the global scope (root of namespace_symbols_)
					// For global namespace, we look in the empty namespace path
					NamespacePath empty_path;
					auto ns_it = namespace_symbols_.find(empty_path);
					if (ns_it != namespace_symbols_.end()) {
						for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
							if (key == original_name) {
#else
							if (key.view() == original_name) {
#endif
								if (!value_vec.empty()) {
									result = value_vec[0];
									break;
								}
							}
						}
					}
				} else {
					result = lookup_qualified(namespace_path, original_name);
				}
				if (result.has_value()) {
					return result;
				}
			}

			// Second, check direct symbols in this scope
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end() && !symbolIt->second.empty()) {
				// Return the first match for backward compatibility
				return symbolIt->second[0];
			}

			// Third, check using directives in this scope
			for (const auto& using_ns : scope.using_directives) {
				auto result = lookup_qualified(using_ns, identifier);
				if (result.has_value()) {
					return result;
				}
			}

			// If we're in a namespace scope, also check namespace_symbols_ for symbols
			// from other blocks of the same namespace (e.g., reopened namespace blocks)
			// This needs to happen BEFORE checking parent/global scopes.
			// We check the current accumulated namespace path, then pop one component
			// to check progressively outer namespace paths in subsequent iterations.
			if (scope.scope_type == ScopeType::Namespace && !full_ns_path.empty()) {
				// Only check if we haven't already checked this path
				bool already_checked = false;
				for (const auto& checked : checked_ns_paths) {
					if (checked == full_ns_path) {
						already_checked = true;
						break;
					}
				}
				
				if (!already_checked) {
					checked_ns_paths.push_back(full_ns_path);
					auto ns_it = namespace_symbols_.find(full_ns_path);
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
				
				// Pop the last namespace from the path to check progressively outer
				// namespace scopes in subsequent iterations. This is intentional:
				// when we encounter a namespace scope during reverse iteration, we want
				// to check namespace_symbols_ for that specific namespace level.
				full_ns_path.pop_back();
			}
		}

		return std::nullopt;
	}

	// Overload that accepts template parameters (eliminates global callback)
	std::optional<ASTNode> lookup(StringHandle identifier, 
								 ScopeHandle scope_limit_handle,
								 const std::vector<StringHandle>* template_params) const {
		// Check if this is a template parameter
		if (template_params) {
			auto it = std::find(template_params->begin(), template_params->end(), identifier);
			if (it != template_params->end()) {
				// This is a template parameter - create a TemplateParameterReferenceNode
				FLASH_LOG(Symbols, Debug, "SymbolTable lookup found template parameter '", identifier, 
				          "' in provided template params, creating TemplateParameterReferenceNode");
				Token token(Token::Type::Identifier, StringTable::getStringView(identifier), 0, 0, 0);
				return ASTNode::emplace_node<TemplateParameterReferenceNode>(identifier, token);
			}
		}

		// Otherwise, use the regular lookup
		return lookup(identifier, scope_limit_handle);
	}

	// New method to get all overloads of a function
	std::vector<ASTNode> lookup_all(std::string_view identifier) const {
		return lookup_all(identifier, get_current_scope_handle());
	}

	std::vector<ASTNode> lookup_all(std::string_view identifier, ScopeHandle scope_limit_handle) const {
		// Build the full current namespace path first
		NamespacePath full_ns_path;
		for (const auto& scope : symbol_table_stack_) {
			if (scope.scope_type == ScopeType::Namespace) {
				full_ns_path.push_back(scope.namespace_name);
			}
		}
		
		// Track which namespace paths we've already checked
		std::vector<NamespacePath> checked_ns_paths;
		
		for (auto stackIt = symbol_table_stack_.rbegin() + (get_current_scope_handle().scope_level - scope_limit_handle.scope_level); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			const Scope& scope = *stackIt;
			
			// First, check using declarations in this scope
			auto usingIt = scope.using_declarations.find(identifier);
			if (usingIt != scope.using_declarations.end()) {
				const auto& [namespace_path, original_name] = usingIt->second;
				auto result = lookup_qualified_all(namespace_path, original_name);
				if (!result.empty()) {
					return result;
				}
			}
			
			// Second, check direct symbols in this scope
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end()) {
				return symbolIt->second;
			}
			
			// Third, check using directives in this scope
			for (const auto& using_ns : scope.using_directives) {
				auto result = lookup_qualified_all(using_ns, identifier);
				if (!result.empty()) {
					return result;
				}
			}
			
			// If we're in a namespace scope, also check namespace_symbols_ for symbols
			// from other blocks of the same namespace (e.g., reopened namespace blocks).
			// We check the current accumulated namespace path, then pop one component
			// to check progressively outer namespace paths in subsequent iterations.
			if (scope.scope_type == ScopeType::Namespace && !full_ns_path.empty()) {
				// Only check if we haven't already checked this path
				bool already_checked = false;
				for (const auto& checked : checked_ns_paths) {
					if (checked == full_ns_path) {
						already_checked = true;
						break;
					}
				}
				
				if (!already_checked) {
					checked_ns_paths.push_back(full_ns_path);
					auto ns_it = namespace_symbols_.find(full_ns_path);
					if (ns_it != namespace_symbols_.end()) {
						for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
							if (key == std::string(identifier)) {
#else
							if (key.view() == identifier) {
#endif
								return value_vec;
							}
						}
					}
				}
				
				// Pop the last namespace from the path to check progressively outer
				// namespace scopes in subsequent iterations. This is intentional:
				// when we encounter a namespace scope during reverse iteration, we want
				// to check namespace_symbols_ for that specific namespace level.
				full_ns_path.pop_back();
			}
		}

		return {};
	}

	// Lookup all overloads in a specific namespace
	template<typename StringContainer>
	std::vector<ASTNode> lookup_qualified_all(const StringContainer& namespaces, std::string_view identifier) const {
		if (namespaces.empty()) {
			// Global namespace - look in the empty namespace path
			NamespacePath empty_path;
			auto ns_it = namespace_symbols_.find(empty_path);
			if (ns_it != namespace_symbols_.end()) {
				for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
					if (key == std::string(identifier)) {
#else
					if (key.view() == identifier) {
#endif
						return value_vec;
					}
				}
			}
			return {};
		}

		// Build namespace path from the components
		NamespacePath ns_path;
		ns_path.reserve(namespaces.size());
		for (const auto& ns : namespaces) {
			ns_path.emplace_back(StringType<>(std::string_view(ns)));
		}

		// Look up in namespace symbols
		auto ns_it = namespace_symbols_.find(ns_path);
		if (ns_it == namespace_symbols_.end()) {
			return {};
		}

		// Look for the identifier in the namespace
		for (const auto& [key, value_vec] : ns_it->second) {
#if USE_OLD_STRING_APPROACH
			if (key == std::string(identifier)) {
#else
			if (key.view() == identifier) {
#endif
				return value_vec;
			}
		}
		return {};
	}

	// Resolve function overload based on argument types
	// Returns the best matching function declaration, or nullopt if no match or ambiguous
	std::optional<ASTNode> lookup_function(std::string_view identifier, const std::vector<Type>& arg_types) const {
		return lookup_function(identifier, arg_types, get_current_scope_handle());
	}

	std::optional<ASTNode> lookup_function(std::string_view identifier, [[maybe_unused]] const std::vector<Type>& arg_types, ScopeHandle scope_limit_handle) const {
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

 	std::optional<SymbolScopeHandle> get_scope_handle(std::string_view identifier, [[maybe_unused]] ScopeHandle scope_limit_handle) const {
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
		// Check if key already exists
		auto it = current_scope.using_declarations.find(local_name);
		if (it != current_scope.using_declarations.end()) {
			// Key exists, reuse it
			std::string_view orig_name = intern_string(original_name);  // Still need to intern the value
			it->second = std::make_pair(namespace_path, orig_name);
		} else {
			// New key, intern both key and value
			std::string_view key = intern_string(local_name);
			std::string_view orig_name = intern_string(original_name);
			current_scope.using_declarations[key] = std::make_pair(namespace_path, orig_name);
		}

		// Also materialize the referenced symbol into the current scope for faster/unambiguous lookup
		auto resolved = lookup_qualified(namespace_path, original_name);
		if (resolved.has_value()) {
			std::string_view key = intern_string(local_name);
			auto sym_it = current_scope.symbols.find(key);
			if (sym_it == current_scope.symbols.end()) {
				current_scope.symbols[key] = std::vector<ASTNode>{ resolved.value() };
			}
		}
	}

	// Add a namespace alias to the current scope
	void add_namespace_alias(std::string_view alias, const std::vector<StringType<>>& target_namespace) {
		if (symbol_table_stack_.empty()) return;

		Scope& current_scope = symbol_table_stack_.back();
		// Check if key already exists
		auto it = current_scope.namespace_aliases.find(alias);
		if (it != current_scope.namespace_aliases.end()) {
			// Key exists, reuse it and just update the value
			it->second = target_namespace;
		} else {
			// New key, intern it
			std::string_view key = intern_string(alias);
			current_scope.namespace_aliases[key] = target_namespace;
		}
	}

	// Merge all symbols from an inline namespace into its parent namespace map
	void merge_inline_namespace(const NamespacePath& inline_path, const NamespacePath& parent_path) {
		auto inline_it = namespace_symbols_.find(inline_path);
		if (inline_it == namespace_symbols_.end()) {
			return;
		}

		auto& parent_symbols = namespace_symbols_[parent_path];
		for (const auto& [key, vec] : inline_it->second) {
			auto& dest_vec = parent_symbols[key];
			dest_vec.insert(dest_vec.end(), vec.begin(), vec.end());
		}
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
	// If namespaces is empty, looks in the global namespace (for ::identifier syntax)
	template<typename StringContainer>
	std::optional<ASTNode> lookup_qualified(const StringContainer& namespaces, std::string_view identifier) const {
		// If namespaces is empty, look in the global namespace
		if (namespaces.empty()) {
			// Look in the global namespace (empty namespace path)
			NamespacePath empty_path;
			auto ns_it = namespace_symbols_.find(empty_path);
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

	// Get all using directives from the current scope and all enclosing scopes
	std::vector<NamespacePath> get_current_using_directives() const {
		std::vector<NamespacePath> result;
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			for (const auto& using_dir : stackIt->using_directives) {
				result.push_back(using_dir);
			}
		}
		return result;
	}

	// Get all using declarations from the current scope and all enclosing scopes
	// Returns a map of local_name -> (namespace_path, original_name)
	std::unordered_map<std::string_view, std::pair<std::vector<StringType<>>, std::string_view>> get_current_using_declarations() const {
		std::unordered_map<std::string_view, std::pair<std::vector<StringType<>>, std::string_view>> result;
		for (auto stackIt = symbol_table_stack_.rbegin(); stackIt != symbol_table_stack_.rend(); ++stackIt) {
			for (const auto& [local_name, target_info] : stackIt->using_declarations) {
				// Only add if not already present (inner scopes shadow outer scopes)
				if (result.find(local_name) == result.end()) {
					result[local_name] = target_info;
				}
			}
		}
		return result;
	}

	// Lookup a nested class by qualified name (e.g., "Outer::Inner")
	std::optional<ASTNode> lookup_nested_class(std::string_view outer_class, StringHandle inner_class) const {
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
		interned_strings_.clear();
		// Recreate the string allocator to fully release all memory
		string_allocator_ = ChunkedStringAllocator(64 * 1024);
	}

private:
	std::vector<Scope> symbol_table_stack_ = { Scope(ScopeType::Global, 0 ) };
	// Persistent map of namespace contents
	// Uses NamespacePath (vector of components) as key to avoid string concatenation
	// Maps: namespace_path -> (symbol_name -> vector<ASTNode>) to support overloading
	std::unordered_map<NamespacePath, std::unordered_map<StringType<32>, std::vector<ASTNode>>, NamespacePathHash, NamespacePathEqual> namespace_symbols_;
	
	// Dedicated string allocator for symbol table keys
	// Ensures string_view keys remain valid for the lifetime of the symbol table
	ChunkedStringAllocator string_allocator_{64 * 1024};  // 64 KB chunks for symbol names
	
	// Set to track all interned strings for fast O(1) deduplication
	std::unordered_set<std::string_view> interned_strings_;

	// Intern a string_view by checking if it already exists, or allocate it
	// Returns a string_view that is guaranteed to remain valid
	std::string_view intern_string(std::string_view str) {
		// Check if this string has already been interned (O(1) lookup)
		auto it = interned_strings_.find(str);
		if (it != interned_strings_.end()) {
			return *it;  // Return existing interned string
		}
		
		// String not found, allocate it using StringBuilder and add to set
		StringBuilder sb(string_allocator_);
		std::string_view interned = sb.append(str).commit();
		interned_strings_.insert(interned);
		return interned;
	}
};

// ============================================================================
// Qualified Name Builder Utilities
// ============================================================================
// These functions consolidate the repeated pattern of building qualified names
// by joining namespace components with "::" and appending a final identifier.
// 
// They replace scattered code like:
//   StringBuilder sb;
//   for (const auto& ns : namespace_path) { sb.append(ns).append("::"); }
//   sb.append(name);
//   std::string_view qualified = sb.commit();
// ============================================================================

/**
 * @brief Build a qualified name from a NamespacePath and a final identifier.
 * Returns a string_view committed to the global chunked string allocator.
 * 
 * Example: buildQualifiedName({"std", "chrono"}, "seconds") -> "std::chrono::seconds"
 */
inline std::string_view buildQualifiedName(const NamespacePath& namespace_path, std::string_view name) {
	if (namespace_path.empty()) {
		return name;
	}
	
	StringBuilder sb;
	for (const auto& ns : namespace_path) {
#if USE_OLD_STRING_APPROACH
		sb.append(ns).append("::");
#else
		sb.append(ns.view()).append("::");
#endif
	}
	sb.append(name);
	return sb.commit();
}

/**
 * @brief Build a qualified name from a NamespacePath and a StringHandle identifier.
 */
inline std::string_view buildQualifiedName(const NamespacePath& namespace_path, StringHandle name) {
	return buildQualifiedName(namespace_path, StringTable::getStringView(name));
}

inline StringHandle buildQualifiedNameHandle(const NamespacePath& namespace_path, std::string_view name) {
	return StringTable::getOrInternStringHandle(buildQualifiedName(namespace_path, name));
}

inline StringHandle buildQualifiedNameHandle(const NamespacePath& namespace_path, StringHandle name) {
	return buildQualifiedNameHandle(namespace_path, StringTable::getStringView(name));
}

/**
 * @brief Build a qualified name from a container of string-like types
 * (e.g., from QualifiedIdentifierNode::namespaces()) and a final identifier.
 * 
 * Note: This template exists separately from buildQualifiedName(NamespacePath, ...) because
 * the element type access differs based on USE_OLD_STRING_APPROACH preprocessor setting.
 * The duplication is intentional to handle both std::string and StackString<> element types.
 * 
 * Example: buildQualifiedNameFromStrings({"std", "chrono"}, "seconds") -> "std::chrono::seconds"
 */
template<typename StringContainer>
inline std::string_view buildQualifiedNameFromStrings(const StringContainer& namespaces, std::string_view name) {
	if (namespaces.empty()) {
		return name;
	}
	
	StringBuilder sb;
	for (const auto& ns : namespaces) {
#if USE_OLD_STRING_APPROACH
		sb.append(ns).append("::");
#else
		sb.append(ns.view()).append("::");
#endif
	}
	sb.append(name);
	return sb.commit();
}

/**
 * @brief Build a full qualified name by joining all namespace components with "::".
 * This is used when you have multiple components and want to join them all.
 * 
 * Example: buildFullQualifiedName({"A", "B", "C"}) -> "A::B::C"
 * (Note: different from buildQualifiedName which appends "::" after each namespace before the name)
 */
template<typename StringContainer>
inline std::string_view buildFullQualifiedName(const StringContainer& components) {
	if (components.empty()) {
		return "";
	}
	
	if (components.size() == 1) {
#if USE_OLD_STRING_APPROACH
		return std::string_view(components[0]);
#else
		return components[0].view();
#endif
	}
	
	StringBuilder sb;
	bool first = true;
	for (const auto& component : components) {
		if (!first) {
			sb.append("::");
		}
		first = false;
#if USE_OLD_STRING_APPROACH
		sb.append(component);
#else
		sb.append(component.view());
#endif
	}
	return sb.commit();
}

extern SymbolTable gSymbolTable;
