#pragma once

#include <unordered_map>
#include <unordered_set>
#include <string_view>
#include <stack>
#include <optional>
#include <span>
#include <vector>
#include <functional>
#include <algorithm>
#include <limits>
#include "InlineVector.h"
#include "AstNodeTypes.h"
#include "Token.h"
#include "StackString.h"
#include "Log.h"
#include "StringBuilder.h"
#include "FrontendIds.h"
#include "CompileError.h"
#include "NamespaceRegistry.h"
#include "TemplateRegistry.h"

enum class ScopeType {
	Global,
	Function,
	Block,
	Namespace,
};

struct ScopeHandle {
	size_t scope_level = 0;
};

struct Scope {
	Scope() = default;
	Scope(ScopeType scopeType, uint32_t depth)
		: scope_type(scopeType), depth(depth), scope_handle{.scope_level = depth} {}
	Scope(ScopeType scopeType, uint32_t depth, StringHandle ns_name)
		: scope_type(scopeType), depth(depth), scope_handle{.scope_level = depth}, namespace_name(ns_name) {}

	Scope(Scope&&) noexcept = default;
	Scope& operator=(Scope&&) noexcept = default;
	Scope(const Scope&) = default;
	Scope& operator=(const Scope&) = default;

	ScopeType scope_type = ScopeType::Block;
	ScopeId scope_id;
	ScopeId parent_scope_id;
	uint32_t depth = 1;
	// Changed to support function overloading: each name can map to multiple symbols (for overloaded functions)
	// Use string_view keys with a dedicated ChunkedStringAllocator in SymbolTable to avoid copies
	// while ensuring strings remain valid for the lifetime of the symbol table.
	// String views are interned using the symbol table's allocator before being used as keys.
	std::unordered_map<std::string_view, std::vector<ASTNode>> symbols;
	ScopeHandle scope_handle;
	StringHandle namespace_name;	 // Only used for Namespace scopes
	NamespaceHandle namespace_handle = NamespaceHandle{NamespaceHandle::INVALID_HANDLE};	 // Only used for Namespace scopes

	// Using directives: namespaces to search when looking up unqualified names (stored as handles)
	std::vector<NamespaceHandle> using_directive_paths;

	// Using declarations: specific symbols imported from namespaces
	// Maps: local_name -> (namespace_handle, original_name)
	std::unordered_map<std::string_view, std::pair<NamespaceHandle, std::string_view>> using_declarations_handles;

	// Namespace aliases: Maps alias -> target namespace handle
	std::unordered_map<std::string_view, NamespaceHandle> namespace_aliases;
};

// Helper function to extract parameter types from a FunctionDeclarationNode
inline std::vector<TypeIndex> extractParameterTypes(const ASTNode& node) {
	std::vector<TypeIndex> param_types;

	if (!node.is<DeclarationNode>()) {
		return param_types;
	}

	// For function declarations, we need to look up the FunctionDeclarationNode
	// This is a simplified version - in practice, we'd need to traverse the AST
	// to find the associated FunctionDeclarationNode
	return param_types;
}

// Helper function to check if two function signatures match
inline bool signaturesMatch(std::span<const TypeIndex> sig1, std::span<const TypeIndex> sig2) {
	if (sig1.size() != sig2.size()) {
		return false;
	}

	for (size_t i = 0; i < sig1.size(); ++i) {
		if (sig1[i] != sig2[i] || sig1[i].category() != sig2[i].category()) {
			return false;
		}
	}

	return true;
}

// Count the minimum required arguments for a function.
//
// C++ permits a defaulted parameter to be followed by a function parameter pack,
// so we must ignore any trailing pack first and then discount trailing defaults.
inline size_t countMinRequiredArgs(const FunctionDeclarationNode& func) {
	const auto& params = func.parameter_nodes();
	size_t min_required = params.size();
	size_t i = params.size();

	// A trailing function parameter pack may follow a defaulted parameter.
	while (i > 0) {
		if (!params[i - 1].is<DeclarationNode>()) {
			break;
		}
		const auto& param_decl = params[i - 1].as<DeclarationNode>();
		if (!param_decl.is_parameter_pack()) {
			break;
		}
		min_required--;
		--i;
	}

	// Walk the remaining suffix of trailing default arguments.
	while (i > 0) {
		if (!params[i - 1].is<DeclarationNode>()) {
			break;
		}
		const auto& param_decl = params[i - 1].as<DeclarationNode>();
		if (!param_decl.has_default_value()) {
			break;
		}
		min_required--;
		--i;
	}
	return min_required;
}

inline size_t countMaxAcceptedArgs(const FunctionDeclarationNode& func) {
	if (func.is_variadic()) {
		return std::numeric_limits<size_t>::max();
	}
	return func.parameter_nodes().size();
}

inline bool functionAcceptsArgumentCount(
	const FunctionDeclarationNode& func,
	size_t argument_count) {
	return argument_count >= countMinRequiredArgs(func) &&
		argument_count <= countMaxAcceptedArgs(func);
}

// Records SymbolTable mutations from insert() for rollback on the parser
// DeclarationBuilder shadow path when publication rejects after insert.
struct SymbolTableInsertUndo {
	enum class Op : uint8_t {
		RemoveNamespaceEntry,
		PopNamespaceOverload,
		RestoreNamespaceNode,
		RemoveGlobalScopeEntry,
		PopGlobalScopeOverload,
		RestoreGlobalScopeNode,
		RemoveGlobalNamespaceMirrorEntry,
		PopGlobalNamespaceMirrorOverload,
		RestoreGlobalNamespaceMirrorNode,
	};

	struct Entry {
		Op op = Op::RemoveNamespaceEntry;
		ScopeId scope_id;
		NamespaceHandle namespace_handle{};
		StringHandle name_key{};
		std::string_view scope_key{};
		std::size_t index = 0;
		ASTNode previous_node{};
	};

	std::vector<Entry> entries;

	bool hasChanges() const {
		return !entries.empty();
	}
};

namespace SymbolTableDetail {

inline void stampLexicalScopeOnDeclaration(ASTNode& node, ScopeId scope_id) {
	if (!scope_id) {
		return;
	}
	if (FunctionDeclarationNode* func = get_function_decl_node_mut(node)) {
		func->decl_node().set_lexical_scope_id(scope_id);
		return;
	}
	if (node.is<VariableDeclarationNode>()) {
		node.as<VariableDeclarationNode>().declaration().set_lexical_scope_id(scope_id);
		return;
	}
	if (node.is<DeclarationNode>()) {
		node.as<DeclarationNode>().set_lexical_scope_id(scope_id);
	}
}

} // namespace SymbolTableDetail

class SymbolTable {
public:
	void setDiagnosticEngine(DiagnosticEngine& diagnostics) {
		diagnostics_ = &diagnostics;
	}

	ScopeId currentScopeId() const {
		return scopes_[current_scope_index_].scope_id;
	}

	ScopeId lastLookupScopeId() const {
		return last_lookup_scope_id_;
	}

	ScopeId lastDeclaringScopeId() const {
		return last_declaring_scope_id_;
	}

	std::size_t scopeCount() const {
		return scopes_.size();
	}

	std::size_t activeScopeDepth() const {
		return scopes_[current_scope_index_].depth;
	}

	// Lookup by ScopeId for external/unvalidated inputs. Absence is a normal outcome
	// (stale id, foreign context, never-created scope). Persistent scopes assign
	// ScopeId as index + 1, so this is O(1).
	const Scope* findScopeById(ScopeId scope_id) const {
		if (!scope_id || scope_id.value > scopes_.size()) {
			return nullptr;
		}
		const Scope& scope = scopes_[scope_id.value - 1];
		if (scope.scope_id != scope_id) {
			return nullptr;
		}
		return &scope;
	}

	// Lookup for ScopeIds produced by this SymbolTable (parent links, last-declaring
	// site, current scope). A missing or mismatched record is an internal invariant
	// failure, not a recoverable publication rejection.
	const Scope& scopeById(ScopeId scope_id) const {
		if (!scope_id || scope_id.value > scopes_.size()) {
			throw InternalError("SymbolTable: ScopeId out of range");
		}
		const Scope& scope = scopes_[scope_id.value - 1];
		if (scope.scope_id != scope_id) {
			throw InternalError("SymbolTable: ScopeId does not match scope record");
		}
		return scope;
	}

	bool insert([[maybe_unused]] const std::string& identifier, [[maybe_unused]] ASTNode node) {
		assert(false && "Use StringBuilder to pass a string_view to SymbolTable::insert, don't use std::string");
		return false;
	}

	bool insert(StringHandle identifierHandle, ASTNode node) {
		return insert(StringTable::getStringView(identifierHandle), node); // This should probably be the other way around
	}

	bool insertWithUndo(StringHandle identifierHandle, ASTNode node, SymbolTableInsertUndo& undo) {
		return insertWithUndo(StringTable::getStringView(identifierHandle), node, undo);
	}

	// Insert using QualifiedIdentifier.
	// Inserts under the unqualified name in the current scope.
	// Note: namespace_symbols_ insertion is handled by the existing insert(string_view)
	// pathway when the scope type is Namespace or Global.
	bool insert(QualifiedIdentifier qi, ASTNode node) {
		return insert(StringTable::getStringView(qi.identifier_handle), node);
	}

	bool insert(std::string_view identifier, ASTNode node) {
		return insertCore(identifier, node, nullptr);
	}

	bool insertWithUndo(std::string_view identifier, ASTNode node, SymbolTableInsertUndo& undo) {
		undo.entries.clear();
		return insertCore(identifier, node, &undo);
	}

	bool insertCore(std::string_view identifier, ASTNode node, SymbolTableInsertUndo* undo) {
		last_declaring_scope_id_ = scopes_[current_scope_index_].scope_id;
		const ScopeId insert_scope_id = last_declaring_scope_id_;
		SymbolTableDetail::stampLexicalScopeOnDeclaration(node, insert_scope_id);
		auto& current_scope = scopes_[current_scope_index_];
		const bool ns_scope = current_scope.scope_type == ScopeType::Namespace;
		const bool global_scope = current_scope.scope_type == ScopeType::Global;

		// For namespace scopes, namespace_symbols_ is the source of truth for
		// redeclaration / overload detection across reopened blocks. Global and
		// other scopes still use the local symbols map.
		std::vector<ASTNode>* existing_ptr = nullptr;
		NamespaceHandle ns_handle{};
		StringHandle ns_key{};
		std::string_view scope_map_key = identifier;
		if (ns_scope) {
			ns_handle = get_current_namespace_handle();
			auto& ns_symbols = namespace_symbols_[ns_handle];
			ns_key = StringTable::getOrInternStringHandle(identifier);
			auto ns_it = ns_symbols.find(ns_key);
			if (ns_it != ns_symbols.end()) {
				existing_ptr = &ns_it->second;
			}
		} else {
			auto it = current_scope.symbols.find(identifier);
			if (it != current_scope.symbols.end()) {
				existing_ptr = &it->second;
				scope_map_key = it->first;
			}
		}

		// If this is a new identifier, create a new vector
		if (!existing_ptr) {
			if (ns_scope) {
				// Namespace scopes store members only in namespace_symbols_; do not
				// mirror into scope.symbols (namespace-scope using-declarations also
				// merge into namespace_symbols_; see materialize_using_declaration_symbols).
				namespace_symbols_[ns_handle][ns_key] = std::vector<ASTNode>{node};
				pushInsertUndo(
					undo,
					SymbolTableInsertUndo::Op::RemoveNamespaceEntry,
					insert_scope_id,
					ns_handle,
					ns_key,
					std::string_view{},
					0,
					ASTNode{});
				return true;
			}
			std::string_view key = intern_string(identifier);
			current_scope.symbols[key] = std::vector<ASTNode>{node};
			pushInsertUndo(
				undo,
				SymbolTableInsertUndo::Op::RemoveGlobalScopeEntry,
				insert_scope_id,
				NamespaceHandle{NamespaceHandle::INVALID_HANDLE},
				StringHandle{},
				key,
				0,
				ASTNode{});
			if (global_scope) {
				ns_handle = get_current_namespace_handle();
				ns_key = StringTable::getOrInternStringHandle(identifier);
				/* Global scope keys existence off scope.symbols, so namespace_symbols_ can
				   in principle already hold entries this scope never saw. Append rather than
				   assign so such entries are never silently dropped. */
				auto& ns_symbols = namespace_symbols_[ns_handle];
				auto ns_it = ns_symbols.find(ns_key);
				if (ns_it == ns_symbols.end()) {
					ns_symbols[ns_key] = std::vector<ASTNode>{node};
					pushInsertUndo(
						undo,
						SymbolTableInsertUndo::Op::RemoveGlobalNamespaceMirrorEntry,
						insert_scope_id,
						ns_handle,
						ns_key,
						std::string_view{},
						0,
						ASTNode{});
				} else {
					ns_it->second.push_back(node);
					pushInsertUndo(
						undo,
						SymbolTableInsertUndo::Op::PopGlobalNamespaceMirrorOverload,
						insert_scope_id,
						ns_handle,
						ns_key,
						std::string_view{},
						0,
						ASTNode{});
				}
			}
			return true;
		}

		// Identifier exists - check if we can add this as an overload
		auto& existing_nodes = *existing_ptr;

		// For non-function symbols (variables, etc.), don't allow duplicates in the same scope
		// This includes DeclarationNode and VariableDeclarationNode
		// Use helper function to check for both FunctionDeclarationNode and TemplateFunctionDeclarationNode
		if (!is_function_or_template_function(node)) {
			// Check if any existing symbol has a different type
			if (!existing_nodes.empty() && existing_nodes[0].type_name() != node.type_name()) {
				return false;
			}
			// Don't allow duplicate non-function symbols in the same scope
			return false;
		}

		// For function declarations (including template functions), allow overloading
		// Check if a function with the same signature already exists
		const FunctionDeclarationNode* new_func = get_function_decl_node(node);
		if (new_func) {
			const auto& new_params = new_func->parameter_nodes();

			// Check if a function with the same signature already exists
			for (size_t i = 0; i < existing_nodes.size(); ++i) {
				const FunctionDeclarationNode* existing_func = get_function_decl_node(existing_nodes[i]);
				if (existing_func) {
					const auto& existing_params = existing_func->parameter_nodes();

					// Check if parameter counts match
					if (new_params.size() == existing_params.size() &&
						new_func->is_variadic() == existing_func->is_variadic()) {
						// Check if all parameter types match
						bool all_match = true;
						for (size_t j = 0; j < new_params.size(); ++j) {
							const auto& new_param_type = new_params[j].as<DeclarationNode>().type_specifier_node();
							const auto& existing_param_type = existing_params[j].as<DeclarationNode>().type_specifier_node();

							if (!new_param_type.matches_signature(existing_param_type)) {
								all_match = false;
								break;
							}
						}

						// Also check return types for template specializations
						// (e.g., get<0> returns int, get<1> returns double - different specializations)
						if (all_match) {
							const auto& new_return_type = new_func->decl_node().type_specifier_node();
							const auto& existing_return_type = existing_func->decl_node().type_specifier_node();
							if (!new_return_type.matches_signature(existing_return_type)) {
								all_match = false;  // Different return types = different specializations
							}
						}

						if (all_match) {
							const bool new_is_template =
								node.is<TemplateFunctionDeclarationNode>();
							const bool existing_is_template =
								existing_nodes[i].is<TemplateFunctionDeclarationNode>();
							if (new_is_template != existing_is_template) {
								continue;
							}
							// Same signature found - replace forward declaration with definition if needed
							// If the new one has a definition and the existing one doesn't, replace it
							if (new_func->is_materialized() && !existing_func->is_materialized()) {
								const ASTNode previous_node = existing_nodes[i];
								existing_nodes[i] = node;

								if (ns_scope) {
									pushInsertUndo(
										undo,
										SymbolTableInsertUndo::Op::RestoreNamespaceNode,
										insert_scope_id,
										ns_handle,
										ns_key,
										std::string_view{},
										i,
										previous_node);
								} else {
									pushInsertUndo(
										undo,
										SymbolTableInsertUndo::Op::RestoreGlobalScopeNode,
										insert_scope_id,
										NamespaceHandle{NamespaceHandle::INVALID_HANDLE},
										StringHandle{},
										scope_map_key,
										i,
										previous_node);
								}

								if (global_scope) {
									// Global still uses scope.symbols as primary; mirror into namespace_symbols_
									NamespaceHandle mirror_ns = get_current_namespace_handle();
									auto& ns_symbols = namespace_symbols_[mirror_ns];
									StringHandle key = StringTable::getOrInternStringHandle(identifier);

									auto ns_it = ns_symbols.find(key);
									if (ns_it != ns_symbols.end()) {
										for (size_t k = 0; k < ns_it->second.size(); ++k) {
											const FunctionDeclarationNode* ns_func = get_function_decl_node(ns_it->second[k]);
											if (ns_func) {
												const auto& ns_params = ns_func->parameter_nodes();

												if (ns_params.size() == new_params.size() &&
													ns_func->is_variadic() == new_func->is_variadic()) {
													bool params_match = true;
													for (size_t m = 0; m < ns_params.size(); ++m) {
														const auto& ns_param_type = ns_params[m].as<DeclarationNode>().type_specifier_node();
														const auto& new_param_type = new_params[m].as<DeclarationNode>().type_specifier_node();
														if (!ns_param_type.matches_signature(new_param_type)) {
															params_match = false;
															break;
														}
													}
													if (params_match) {
														const ASTNode mirror_previous = ns_it->second[k];
														ns_it->second[k] = node;
														pushInsertUndo(
															undo,
															SymbolTableInsertUndo::Op::RestoreGlobalNamespaceMirrorNode,
															insert_scope_id,
															mirror_ns,
															key,
															std::string_view{},
															k,
															mirror_previous);
														break;
													}
												}
											}
										}
									}
								}
								// Namespace scopes: existing_nodes already aliases namespace_symbols_
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

		if (ns_scope) {
			pushInsertUndo(
				undo,
				SymbolTableInsertUndo::Op::PopNamespaceOverload,
				insert_scope_id,
				ns_handle,
				ns_key,
				std::string_view{},
				0,
				ASTNode{});
			// existing_nodes already aliases namespace_symbols_; nothing else to update.
			return true;
		}

		pushInsertUndo(
			undo,
			SymbolTableInsertUndo::Op::PopGlobalScopeOverload,
			insert_scope_id,
			NamespaceHandle{NamespaceHandle::INVALID_HANDLE},
			StringHandle{},
			scope_map_key,
			0,
			ASTNode{});

		// Global scope: also add to the persistent namespace map
		if (global_scope) {
			NamespaceHandle mirror_ns = get_current_namespace_handle();
			auto& ns_symbols = namespace_symbols_[mirror_ns];
			StringHandle key = StringTable::getOrInternStringHandle(identifier);

			auto ns_it = ns_symbols.find(key);
			if (ns_it == ns_symbols.end()) {
				ns_symbols[key] = std::vector<ASTNode>{node};
				pushInsertUndo(
					undo,
					SymbolTableInsertUndo::Op::RemoveGlobalNamespaceMirrorEntry,
					insert_scope_id,
					mirror_ns,
					key,
					std::string_view{},
					0,
					ASTNode{});
			} else {
				ns_it->second.push_back(node);
				pushInsertUndo(
					undo,
					SymbolTableInsertUndo::Op::PopGlobalNamespaceMirrorOverload,
					insert_scope_id,
					mirror_ns,
					key,
					std::string_view{},
					0,
					ASTNode{});
			}
		}

		return true;
	}

	void rollbackInsert(const SymbolTableInsertUndo& undo) {
		for (std::size_t reverse_index = undo.entries.size(); reverse_index > 0; --reverse_index) {
			const SymbolTableInsertUndo::Entry& entry = undo.entries[reverse_index - 1];
			switch (entry.op) {
			case SymbolTableInsertUndo::Op::RemoveNamespaceEntry: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					ns_it->second.erase(entry.name_key);
					if (ns_it->second.empty()) {
						namespace_symbols_.erase(ns_it);
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::PopNamespaceOverload: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					auto sym_it = ns_it->second.find(entry.name_key);
					if (sym_it != ns_it->second.end() && !sym_it->second.empty()) {
						sym_it->second.pop_back();
						if (sym_it->second.empty()) {
							ns_it->second.erase(sym_it);
						}
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::RestoreNamespaceNode: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					auto sym_it = ns_it->second.find(entry.name_key);
					if (sym_it != ns_it->second.end() && entry.index < sym_it->second.size()) {
						sym_it->second[entry.index] = entry.previous_node;
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::RemoveGlobalScopeEntry: {
				if (!entry.scope_id || entry.scope_id.value > scopes_.size()) {
					break;
				}
				scopes_[entry.scope_id.value - 1].symbols.erase(entry.scope_key);
				break;
			}
			case SymbolTableInsertUndo::Op::PopGlobalScopeOverload: {
				if (!entry.scope_id || entry.scope_id.value > scopes_.size()) {
					break;
				}
				auto& scope_symbols = scopes_[entry.scope_id.value - 1].symbols;
				auto sym_it = scope_symbols.find(entry.scope_key);
				if (sym_it != scope_symbols.end() && !sym_it->second.empty()) {
					sym_it->second.pop_back();
					if (sym_it->second.empty()) {
						scope_symbols.erase(sym_it);
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::RestoreGlobalScopeNode: {
				if (!entry.scope_id || entry.scope_id.value > scopes_.size()) {
					break;
				}
				auto& scope_symbols = scopes_[entry.scope_id.value - 1].symbols;
				auto sym_it = scope_symbols.find(entry.scope_key);
				if (sym_it != scope_symbols.end() && entry.index < sym_it->second.size()) {
					sym_it->second[entry.index] = entry.previous_node;
				}
				break;
			}
			case SymbolTableInsertUndo::Op::RemoveGlobalNamespaceMirrorEntry: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					ns_it->second.erase(entry.name_key);
					if (ns_it->second.empty()) {
						namespace_symbols_.erase(ns_it);
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::PopGlobalNamespaceMirrorOverload: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					auto sym_it = ns_it->second.find(entry.name_key);
					if (sym_it != ns_it->second.end() && !sym_it->second.empty()) {
						sym_it->second.pop_back();
						if (sym_it->second.empty()) {
							ns_it->second.erase(sym_it);
						}
					}
				}
				break;
			}
			case SymbolTableInsertUndo::Op::RestoreGlobalNamespaceMirrorNode: {
				auto ns_it = namespace_symbols_.find(entry.namespace_handle);
				if (ns_it != namespace_symbols_.end()) {
					auto sym_it = ns_it->second.find(entry.name_key);
					if (sym_it != ns_it->second.end() && entry.index < sym_it->second.size()) {
						sym_it->second[entry.index] = entry.previous_node;
					}
				}
				break;
			}
			}
		}
	}

	void rollbackInsert(SymbolTableInsertUndo& undo) {
		rollbackInsert(static_cast<const SymbolTableInsertUndo&>(undo));
		undo.entries.clear();
	}

	// Replace a non-function symbol in the current scope with a new node.
	// Used to implement C++20 [basic.scope.pdecl]: a variable's name is visible in its own
	// initializer, so a stub is pre-inserted before parsing the initializer and then replaced
	// with the fully-initialised VariableDeclarationNode once parsing completes.
	bool replace_variable(std::string_view identifier, ASTNode new_node) {
		SymbolTableDetail::stampLexicalScopeOnDeclaration(
			new_node, scopes_[current_scope_index_].scope_id);
		auto& current_scope = scopes_[current_scope_index_];
		if (current_scope.scope_type == ScopeType::Namespace) {
			NamespaceHandle ns_handle = get_current_namespace_handle();
			StringHandle key = StringTable::getOrInternStringHandle(identifier);
			auto ns_map_it = namespace_symbols_.find(ns_handle);
			if (ns_map_it == namespace_symbols_.end()) {
				return false;
			}
			auto it = ns_map_it->second.find(key);
			if (it == ns_map_it->second.end() || it->second.empty()) {
				return false;
			}
			if (is_function_or_template_function(it->second[0])) {
				return false;
			}
			it->second[0] = new_node;
			return true;
		}
		auto it = current_scope.symbols.find(identifier);
		if (it == current_scope.symbols.end() || it->second.empty()) {
			return false;
		}
		if (is_function_or_template_function(it->second[0])) {
			return false;
		}
		it->second[0] = new_node;
		return true;
	}

	// Insert a symbol into the global scope (scope_level 0) regardless of current scope
	// This is useful for variable template instantiations that happen during function parsing
	bool insertGlobal(std::string_view identifier, ASTNode node) {
		if (scopes_.empty()) {
			return false;  // No global scope exists
		}

		auto& global_scope = scopes_[0];	 // Global scope is always at index 0
		last_declaring_scope_id_ = global_scope.scope_id;
		SymbolTableDetail::stampLexicalScopeOnDeclaration(node, global_scope.scope_id);
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
		return scopes_[current_scope_index_].scope_type;
	}

	std::optional<ScopeType> get_scope_type(ScopeHandle handle) const {
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			if (scope.depth == handle.scope_level) {
				return scope.scope_type;
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		return std::nullopt;
	}

	ScopeHandle get_current_scope_handle() const {
		return ScopeHandle{.scope_level = scopes_[current_scope_index_].depth};
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
			return std::nullopt;	 // We'll handle this in the caller
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
		last_lookup_scope_id_ = scopes_[current_scope_index_].scope_id;
		NamespaceHandle namespace_handle = get_current_namespace_handle();
		NamespaceHandle scope_namespace = namespace_handle;

		const uint32_t limit_depth = static_cast<uint32_t>(scope_limit_handle.scope_level);
		std::size_t scope_index = current_scope_index_;
		const uint32_t scopes_to_skip = scopes_[current_scope_index_].depth > limit_depth
			? scopes_[current_scope_index_].depth - static_cast<uint32_t>(limit_depth)
			: 0;
		for (uint32_t skip = 0; skip < scopes_to_skip; ++skip) {
			if (!scopes_[scope_index].parent_scope_id) {
				return std::nullopt;
			}
			scope_index = scopes_[scope_index].parent_scope_id.value - 1;
		}

		while (scope_index < scopes_.size()) {
			const Scope& scope = scopes_[scope_index];

			// For namespace scopes, probe namespace_symbols_ before the local symbols map.
			// Members and namespace-scope using-declarations live in namespace_symbols_;
			// scope.symbols holds block/function-scope using-declarations only.
			// Advance scope_namespace exactly once per Namespace scope regardless of hit/miss.
			if (scope.scope_type == ScopeType::Namespace) {
				if (!scope_namespace.isGlobal()) {
					StringHandle key = StringTable::getOrInternStringHandle(identifier);
					auto result = lookup_qualified_first(scope_namespace, key);
					if (result.has_value()) {
						return result;
					}
				}
				scope_namespace = gNamespaceRegistry.getParent(scope_namespace);
			}

			// Check direct symbols in this scope. Using declarations are materialized
			// here so they correctly participate in shadowing and overload formation.
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end() && !symbolIt->second.empty()) {
				// Return the first match for backward compatibility
				return symbolIt->second[0];
			}

			// Fall back to unresolved using declarations in this scope.
			auto using_handle_it = scope.using_declarations_handles.find(identifier);
			if (using_handle_it != scope.using_declarations_handles.end()) {
				const auto& [using_namespace_handle, original_name] = using_handle_it->second;
				auto result = lookup_qualified(using_namespace_handle, original_name);
				if (result.has_value()) {
					return result;
				}
			}

			// Check using directives in this scope
			auto using_result = lookup_using_directives_first(scope, identifier);
			if (using_result.has_value()) {
				return using_result;
			}

			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}

		return std::nullopt;
	}

	// Overload that accepts template parameters (eliminates global callback)
	std::optional<ASTNode> lookup(StringHandle identifier,
								  ScopeHandle scope_limit_handle,
								  const InlineVector<StringHandle, 4>* template_params) const {
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
		last_lookup_scope_id_ = scopes_[current_scope_index_].scope_id;
		NamespaceHandle namespace_handle = get_current_namespace_handle();
		NamespaceHandle scope_namespace = namespace_handle;

		const uint32_t limit_depth = static_cast<uint32_t>(scope_limit_handle.scope_level);
		std::size_t scope_index = current_scope_index_;
		const uint32_t scopes_to_skip = scopes_[current_scope_index_].depth > limit_depth
			? scopes_[current_scope_index_].depth - static_cast<uint32_t>(limit_depth)
			: 0;
		for (uint32_t skip = 0; skip < scopes_to_skip; ++skip) {
			if (!scopes_[scope_index].parent_scope_id) {
				return {};
			}
			scope_index = scopes_[scope_index].parent_scope_id.value - 1;
		}

		while (scope_index < scopes_.size()) {
			const Scope& scope = scopes_[scope_index];

			// For namespace scopes, probe namespace_symbols_ before the local symbols map.
			// Members and namespace-scope using-declarations live in namespace_symbols_;
			// scope.symbols holds block/function-scope using-declarations only.
			// Advance scope_namespace exactly once per Namespace scope regardless of hit/miss.
			if (scope.scope_type == ScopeType::Namespace) {
				if (!scope_namespace.isGlobal()) {
					StringHandle key = StringTable::getOrInternStringHandle(identifier);
					auto namespace_results = lookup_qualified_all(scope_namespace, key);
					if (!namespace_results.empty()) {
						return namespace_results;
					}
				}
				scope_namespace = gNamespaceRegistry.getParent(scope_namespace);
			}

			// Check direct symbols in this scope. Using declarations are materialized
			// into this map so ordinary lookup sees the full reachable overload set.
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end()) {
				return symbolIt->second;
			}

			// Fall back to unresolved using declarations in this scope.
			auto using_handle_it = scope.using_declarations_handles.find(identifier);
			if (using_handle_it != scope.using_declarations_handles.end()) {
				const auto& [using_namespace_handle, original_name] = using_handle_it->second;
				auto result = lookup_qualified_all(using_namespace_handle, original_name);
				if (!result.empty()) {
					return result;
				}
			}

			// Check using directives in this scope
			auto using_result = lookup_all_using_directives_first(scope, identifier);
			if (!using_result.empty()) {
				return using_result;
			}

			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}

		return {};
	}

	// Lookup all overloads in a specific namespace
	std::vector<ASTNode> lookup_qualified_all(NamespaceHandle namespace_handle, std::string_view identifier) const {
		StringHandle key = StringTable::getOrInternStringHandle(identifier);
		return lookup_qualified_all(namespace_handle, key);
	}

	std::vector<ASTNode> lookup_qualified_all(NamespaceHandle namespace_handle, StringHandle identifier) const {
		if (!namespace_handle.isValid()) {
			return {};
		}
		std::vector<ASTNode> result;
		gNamespaceRegistry.forEachInlineChildNamespace(namespace_handle, [&](NamespaceHandle visible_ns) {
			auto ns_it = namespace_symbols_.find(visible_ns);
			if (ns_it == namespace_symbols_.end()) {
				return;
			}
			auto symbol_it = ns_it->second.find(identifier);
			if (symbol_it == ns_it->second.end() || symbol_it->second.empty()) {
				return;
			}
			if (result.empty()) {
				result = symbol_it->second;
				return;
			}
			if (!is_pure_function_set(result) || !is_pure_function_set(symbol_it->second)) {
				// C++20 [namespace.qual]: if the same name is found in two
				// different namespaces of the inline namespace set and the
				// declarations do not denote the same entity and are not both
				// function/function-template sets, the result is ill-formed.
				throwAmbiguousQualifiedLookup(identifier);
			}
			append_unique_function_overloads(result, symbol_it->second);
		});
		return result;
	}

	template <typename StringContainer>
	std::vector<ASTNode> lookup_qualified_all(const StringContainer& namespaces, std::string_view identifier) const {
		return lookup_qualified_all(resolve_namespace_handle_impl(namespaces), identifier);
	}

	// Returns true if name exists in adl_only_symbols_ (across any namespace).
	// Used to emit a proper compile error when a hidden friend is called without
	// an argument that triggers ADL (C++20 [basic.lookup.argdep]).
	// O(1) thanks to the dedicated adl_only_function_names_ set.
	bool is_adl_only_function_name(std::string_view name) const {
		StringHandle key = StringTable::getOrInternStringHandle(name);
		return adl_only_function_names_.find(key) != adl_only_function_names_.end();
	}

	// Register a symbol directly into a namespace's persistent symbol table.
	// Used for hidden friend functions defined inside class bodies so that
	// lookup_adl() can find them even though the current scope is the struct scope.
	// When adl_only is true the symbol is stored in adl_only_symbols_ instead of
	// namespace_symbols_, making it invisible to ordinary unqualified lookup while
	// still reachable via lookup_adl() (C++20 [basic.lookup.argdep]).
	// When adl_only is false, the symbol is stored in namespace_symbols_
	// and is visible to both ordinary unqualified lookup and ADL.
	void insert_into_namespace(NamespaceHandle ns, StringHandle name_handle, ASTNode node, bool adl_only) {
		if (!ns.isValid())
			return;
		auto& target = adl_only ? adl_only_symbols_[ns] : namespace_symbols_[ns];
		auto it = target.find(name_handle);
		if (it == target.end()) {
			target[name_handle] = std::vector<ASTNode>{node};
		} else {
			it->second.push_back(node);
		}
		if (adl_only) {
			adl_only_function_names_.insert(name_handle);
		}
	}

	// Search only adl_only_symbols_ (hidden friends) for the given function name
	// in associated namespaces of the argument types.  Unlike lookup_adl(), this
	// does NOT search namespace_symbols_, so it is safe to call when the caller
	// has already collected namespace_symbols_ candidates via lookup_all().
	// This avoids duplicate candidates that would cause false ambiguity.
	std::vector<ASTNode> lookup_adl_only(std::string_view func_name,
										 std::span<const TypeSpecifierNode> arg_types) const {
		std::vector<ASTNode> result;
		std::unordered_set<NamespaceHandle> visited;
		StringHandle key = StringTable::getOrInternStringHandle(func_name);

		auto collect_from_ns = [&](NamespaceHandle ns) {
			if (!ns.isValid() || visited.count(ns))
				return;
			// Expand ns to include inline-linked relatives per C++20 [basic.lookup.argdep]/2.
			gNamespaceRegistry.forEachInlineLinkedNamespace(ns, [&](NamespaceHandle related) {
				if (!visited.insert(related).second)
					return;
				auto adl_it = adl_only_symbols_.find(related);
				if (adl_it == adl_only_symbols_.end())
					return;
				auto sym_it = adl_it->second.find(key);
				if (sym_it == adl_it->second.end())
					return;
				result.insert(result.end(), sym_it->second.begin(), sym_it->second.end());
			});
		};

		std::unordered_set<size_t> visited_types;
		for (const auto& arg_type : arg_types) {
			TypeIndex ti = arg_type.type_index();
			const TypeInfo* type_info = tryGetTypeInfo(ti);
			if (!type_info)
				continue;
			if (const StructTypeInfo* si = type_info->getStructInfo()) {
				visited_types.insert(ti.index());
				collect_struct_associated_namespaces(si, collect_from_ns, visited_types);
			} else if (type_info->getEnumInfo()) {
				// C++20 [basic.lookup.argdep]/2: the associated namespace of an
				// enumeration type is its innermost enclosing namespace.
				// Use TypeInfo::namespace_handle_ which is set by add_enum_type().
				collect_from_ns(type_info->namespaceHandle());
			}
		}
		return result;
	}

	// Returns the set of namespace handles that are eligible for operator template lookup.
	// Includes:
	//   1. The global namespace (always eligible via ordinary unqualified lookup).
	//   2. Namespaces present in the current scope chain (ordinary unqualified lookup).
	//   3. Using-directive targets reachable from the current scope chain.
	//   4. Associated namespaces of each argument type (ADL, C++20 [basic.lookup.argdep]/2).
	// Non-inline nested namespaces of an associated namespace are intentionally excluded:
	// e.g. std::rel_ops is not associated with std::pair, so operator templates defined
	// there must not be found when the operands are of type pair<X,Y>.
	std::unordered_set<NamespaceHandle> get_adl_eligible_namespaces(
		std::span<const TypeSpecifierNode> arg_types) const {
		std::unordered_set<NamespaceHandle> result;
		// 1. Global namespace is always eligible.
		result.insert(NamespaceRegistry::GLOBAL_NAMESPACE);
		// 2 & 3. Active scope chain: each namespace frame and its using directives.
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			if (scope.namespace_handle.isValid()) {
				result.insert(scope.namespace_handle);
			}
			for (const auto& using_ns : scope.using_directive_paths) {
				if (using_ns.isValid()) {
					result.insert(using_ns);
				}
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		// 4. Associated namespaces of argument types, expanded for inline namespace transparency.
		// Per C++20 [basic.lookup.argdep]/2: if an associated namespace is an inline namespace
		// its enclosing namespace is also included (parent transparency), and if an associated
		// namespace has inline namespaces those are also included (child transparency).
		std::unordered_set<size_t> visited_types;
		auto add_ns = [&](NamespaceHandle ns) {
			if (!ns.isValid() || result.count(ns))
				return;
			gNamespaceRegistry.forEachInlineLinkedNamespace(ns, [&](NamespaceHandle related) {
				result.insert(related);
			});
		};
		for (const auto& arg_type : arg_types) {
			TypeIndex ti = arg_type.type_index();
			if (const TypeInfo* type_info = tryGetTypeInfo(ti)) {
				if (const StructTypeInfo* si = type_info->getStructInfo()) {
					if (!visited_types.insert(ti.index()).second) continue;
					collect_struct_associated_namespaces(si, add_ns, visited_types);
				} else if (type_info->getEnumInfo()) {
					add_ns(type_info->namespaceHandle());
				}
			}
		}
		return result;
	}

	// Collect ADL candidates per C++20 [basic.lookup.argdep].
	// For each argument type that is a struct/class, searches the namespace in which
	// the struct was declared, plus namespaces of all its base classes (recursively).
	// Suppressed when ordinary lookup already found a blocking non-function declaration.
	// Also searches adl_only_symbols_ so that hidden friends defined inside class bodies
	// are reachable via ADL but not via ordinary unqualified lookup.
	std::vector<ASTNode> lookup_adl(std::string_view func_name,
									std::span<const TypeSpecifierNode> arg_types) const {
		std::vector<ASTNode> result;
		std::unordered_set<NamespaceHandle> visited;
		// Intern once; reused by both lookup_qualified_all and the adl_only_symbols_ search.
		StringHandle key = StringTable::getOrInternStringHandle(func_name);

		auto search_ns = [&](NamespaceHandle ns) {
			if (!ns.isValid() || visited.count(ns))
				return;
			// Expand ns to include inline-linked relatives per C++20 [basic.lookup.argdep]/2.
			// Search each related namespace directly (not via lookup_qualified_all) to avoid
			// double-expansion: forEachInlineLinkedNamespace already visits all inline relatives,
			// so calling lookup_qualified_all here would re-visit their inline children and
			// produce duplicate candidates.
			gNamespaceRegistry.forEachInlineLinkedNamespace(ns, [&](NamespaceHandle related) {
				if (!visited.insert(related).second)
					return;
				// Search regular namespace symbols — direct lookup, no child expansion.
				auto ns_it = namespace_symbols_.find(related);
				if (ns_it != namespace_symbols_.end()) {
					auto sym_it = ns_it->second.find(key);
					if (sym_it != ns_it->second.end()) {
						result.insert(result.end(), sym_it->second.begin(), sym_it->second.end());
					}
				}
				// Also search ADL-only (hidden friend) symbols
				auto adl_it = adl_only_symbols_.find(related);
				if (adl_it != adl_only_symbols_.end()) {
					auto sym_it = adl_it->second.find(key);
					if (sym_it != adl_it->second.end()) {
						result.insert(result.end(), sym_it->second.begin(), sym_it->second.end());
					}
				}
			});
		};

		std::unordered_set<size_t> visited_types;
		for (const auto& arg_type : arg_types) {
			TypeIndex ti = arg_type.type_index();
			if (const TypeInfo* type_info = tryGetTypeInfo(ti)) {
				if (const StructTypeInfo* si = type_info->getStructInfo()) {
					visited_types.insert(ti.index());
					collect_struct_associated_namespaces(si, search_ns, visited_types);
				} else if (type_info->getEnumInfo()) {
					// C++20 [basic.lookup.argdep]/2: the associated namespace of an
					// enumeration type is its innermost enclosing namespace.
					// Use TypeInfo::namespace_handle_ which is set by add_enum_type().
					search_ns(type_info->namespaceHandle());
				}
			}
		}
		return result;
	}

	// Resolve function overload based on argument types (full type info)
	// Returns the best matching function declaration, or nullopt if no match or ambiguous
	std::optional<ASTNode> lookup_function(std::string_view identifier, std::span<const TypeSpecifierNode> arg_types) const {
		return lookup_function(identifier, arg_types, get_current_scope_handle());
	}

	// Helper: Check if a parameter's full type matches an expected TypeSpecifierNode
	// Compares base type, type_index (for struct types), pointer depth, and reference qualifier
	static bool parameterMatchesType(const ASTNode& param, const TypeSpecifierNode& expected) {
		if (!param.is<DeclarationNode>())
			return false;
		const auto& type_node = param.as<DeclarationNode>().type_node();
		if (!type_node.is<TypeSpecifierNode>())
			return false;
		const auto& actual = type_node.as<TypeSpecifierNode>();
		// Base type must match
		if (actual.type() != expected.type())
			return false;
		// For struct types, type_index must also match
		if (actual.category() == TypeCategory::Struct && actual.type_index() != expected.type_index())
			return false;
		// Pointer depth must match (e.g. int vs int* vs int**)
		if (actual.pointer_depth() != expected.pointer_depth())
			return false;
		// Reference qualifier must match (e.g. int& vs int&& vs int)
		if (actual.reference_qualifier() != expected.reference_qualifier())
			return false;
		return true;
	}

	std::optional<ASTNode> lookup_function(std::string_view identifier, std::span<const TypeSpecifierNode> arg_types, ScopeHandle scope_limit_handle) const {
		// Get all overloads
		auto overloads = lookup_all(identifier, scope_limit_handle);
		if (overloads.empty()) {
			return std::nullopt;
		}

		// If only one overload, return it
		if (overloads.size() == 1) {
			return overloads[0];
		}

		// Multiple overloads - find the best match based on argument types
		// Try exact type match (accounting for default arguments)
		for (const auto& overload : overloads) {
			if (!overload.is<FunctionDeclarationNode>())
				continue;
			const auto& func_decl = overload.as<FunctionDeclarationNode>();
			const auto& params = func_decl.parameter_nodes();
			size_t min_required = countMinRequiredArgs(func_decl);
			if (arg_types.size() < min_required || arg_types.size() > params.size())
				continue;

			bool exact_match = true;
			for (size_t i = 0; i < arg_types.size(); ++i) {
				if (!parameterMatchesType(params[i], arg_types[i])) {
					exact_match = false;
					break;
				}
			}
			if (exact_match)
				return overload;
		}

		// Try parameter count match only (allows implicit conversions)
		// Also accounts for default arguments
		for (const auto& overload : overloads) {
			if (!overload.is<FunctionDeclarationNode>())
				continue;
			const auto& func_decl = overload.as<FunctionDeclarationNode>();
			const auto& params = func_decl.parameter_nodes();
			size_t min_required = countMinRequiredArgs(func_decl);
			if (arg_types.size() >= min_required && arg_types.size() <= params.size()) {
				return overload;
			}
		}

		// No viable overload found — return nullopt (ambiguous or no-match is ill-formed)
		return std::nullopt;
	}

	// Return the ScopeType of the scope that directly contains this identifier.
	// Searches scope.symbols (innermost first), then falls back to namespace_symbols_.
	// Returns nullopt if not found anywhere.
	std::optional<ScopeType> get_scope_type_of_symbol(std::string_view identifier) const {
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			auto symbolIt = scope.symbols.find(identifier);
			if (symbolIt != scope.symbols.end() && !symbolIt->second.empty()) {
				return scope.scope_type;
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		// Symbol may be in namespace_symbols_ (e.g. found via using-directive) but not
		// materialised into any scope's symbols map. Treat that as namespace-scope.
		StringHandle key = StringTable::getOrInternStringHandle(identifier);
		for (const auto& [ns_handle, symbols] : namespace_symbols_) {
			if (symbols.find(key) != symbols.end()) {
				return ScopeType::Namespace;
			}
		}
		return std::nullopt;
	}

	void enter_scope(ScopeType scopeType) {
		const Scope& parent = scopes_[current_scope_index_];
		const uint32_t new_depth = parent.depth + 1;
		const ScopeId parent_id = parent.scope_id;
		const ScopeId new_scope_id = ScopeId{static_cast<uint32_t>(scopes_.size() + 1)};
		scopes_.emplace_back(Scope(scopeType, new_depth));
		Scope& scope = scopes_.back();
		scope.scope_id = new_scope_id;
		scope.parent_scope_id = parent_id;
		current_scope_index_ = scopes_.size() - 1;
	}

	void enter_namespace(NamespaceHandle ns_handle) {
		const Scope& parent = scopes_[current_scope_index_];
		const uint32_t new_depth = parent.depth + 1;
		const ScopeId parent_id = parent.scope_id;
		const ScopeId new_scope_id = ScopeId{static_cast<uint32_t>(scopes_.size() + 1)};
		Scope scope(ScopeType::Namespace, new_depth);
		scope.scope_id = new_scope_id;
		scope.parent_scope_id = parent_id;
		scope.namespace_handle = ns_handle;
		if (ns_handle.isValid() && !ns_handle.isGlobal()) {
			gNamespaceRegistry.markDeclared(ns_handle);
			const NamespaceEntry& entry = gNamespaceRegistry.getEntry(ns_handle);
			scope.namespace_name = entry.name;
			// Namespace members live in namespace_symbols_; lookup/lookup_all probe that
			// map for Namespace scopes, and insert() uses it as the source of truth for
			// redeclarations across reopened blocks. Do not preload into scope.symbols.
		}
		scopes_.push_back(std::move(scope));
		current_scope_index_ = scopes_.size() - 1;
	}

	void enter_namespace(std::string_view namespace_name) {
		NamespaceHandle parent_handle = get_current_namespace_handle();
		StringHandle name_handle = StringTable::getOrInternStringHandle(namespace_name);
		NamespaceHandle ns_handle = gNamespaceRegistry.getOrCreateNamespace(parent_handle, name_handle);
		if (!ns_handle.isValid()) {
			FLASH_LOG(Symbols, Error, "Namespace handle creation failed for '", namespace_name, "'");
			const Scope& parent = scopes_[current_scope_index_];
			const uint32_t new_depth = parent.depth + 1;
			const ScopeId parent_id = parent.scope_id;
			const ScopeId new_scope_id = ScopeId{static_cast<uint32_t>(scopes_.size() + 1)};
			scopes_.emplace_back(Scope(ScopeType::Namespace, new_depth, name_handle));
			Scope& scope = scopes_.back();
			scope.scope_id = new_scope_id;
			scope.parent_scope_id = parent_id;
			current_scope_index_ = scopes_.size() - 1;
			return;
		}
		enter_namespace(ns_handle);
	}

	void exit_scope() {
		if (current_scope_index_ == 0) {
			return;
		}
		const Scope& current = scopes_[current_scope_index_];
		if (!current.parent_scope_id) {
			current_scope_index_ = 0;
			return;
		}
		current_scope_index_ = current.parent_scope_id.value - 1;
	}

	// Add a using directive to the current scope
	void add_using_directive(std::span<const StringType<>> namespace_path) {
		if (scopes_.empty())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		NamespaceHandle namespace_handle = resolve_namespace_handle_impl(namespace_path);
		if (namespace_handle.isValid()) {
			current_scope.using_directive_paths.push_back(namespace_handle);
		}
	}

	void add_using_directive(NamespaceHandle namespace_handle) {
		if (scopes_.empty())
			return;
		if (!namespace_handle.isValid())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		current_scope.using_directive_paths.push_back(namespace_handle);
	}

	// Add a using declaration to the current scope
	void add_using_declaration(std::string_view local_name, std::span<const StringType<>> namespace_path, std::string_view original_name) {
		if (scopes_.empty())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		std::string_view key = intern_string(local_name);
		std::string_view orig_name = intern_string(original_name);
		NamespaceHandle namespace_handle = resolve_namespace_handle_impl(namespace_path);
		if (!namespace_handle.isValid()) {
			// Failed to resolve a namespace handle for this declaration; skip insertion.
			FLASH_LOG(Symbols, Error, "Using declaration handle creation failed for '", local_name, "' (invalid namespace path)");
			return;
		}
		update_or_insert(current_scope.using_declarations_handles, key, std::make_pair(namespace_handle, orig_name));

		// Materialize the referenced declaration(s) into the current scope so they
		// participate in ordinary lookup and overload-set formation. At namespace
		// scope this merges into namespace_symbols_ ([namespace.udecl]); elsewhere
		// into scope.symbols.
		materialize_using_declaration_symbols(current_scope, key, namespace_handle, orig_name);
	}

	void add_using_declaration(std::string_view local_name, NamespaceHandle namespace_handle, std::string_view original_name) {
		if (scopes_.empty())
			return;
		if (!namespace_handle.isValid())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		std::string_view key = intern_string(local_name);
		std::string_view orig_name = intern_string(original_name);
		update_or_insert(current_scope.using_declarations_handles, key, std::make_pair(namespace_handle, orig_name));

		// Materialize the referenced declaration(s) into the current scope so they
		// participate in ordinary lookup and overload-set formation. At namespace
		// scope this merges into namespace_symbols_ ([namespace.udecl]); elsewhere
		// into scope.symbols.
		materialize_using_declaration_symbols(current_scope, key, namespace_handle, orig_name);
	}

	// Add a namespace alias to the current scope
	void add_namespace_alias(std::string_view alias, std::span<const StringType<>> target_namespace) {
		if (scopes_.empty())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		std::string_view key = intern_string(alias);
		NamespaceHandle target_handle = resolve_namespace_handle_impl(target_namespace);
		if (!target_handle.isValid()) {
			StringBuilder target_name;
			for (size_t i = 0; i < target_namespace.size(); ++i) {
				if (i > 0) {
					target_name.append("::");
				}
#if USE_OLD_STRING_APPROACH
				target_name.append(target_namespace[i]);
#else
				target_name.append(target_namespace[i].view());
#endif
			}
			FLASH_LOG(Symbols, Error, "Namespace alias handle creation failed for '", alias, "' -> '", target_name.commit(), "'");
			return;
		}
		update_or_insert(current_scope.namespace_aliases, key, target_handle);
	}

	void add_namespace_alias(std::string_view alias, NamespaceHandle target_namespace) {
		if (scopes_.empty())
			return;
		if (!target_namespace.isValid())
			return;

		Scope& current_scope = scopes_[current_scope_index_];
		std::string_view key = intern_string(alias);
		update_or_insert(current_scope.namespace_aliases, key, target_namespace);
	}

	// Lookup a qualified identifier (e.g., "std::print" or "A::B::func")
	// Takes a span/vector of namespace components instead of building a concatenated string
	// If namespaces is empty, looks in the global namespace (for ::identifier syntax)
	std::optional<ASTNode> lookup_qualified(NamespaceHandle namespace_handle, std::string_view identifier) const {
		StringHandle key = StringTable::getOrInternStringHandle(identifier);
		return lookup_qualified(namespace_handle, key);
	}

	std::optional<ASTNode> lookup_qualified(NamespaceHandle namespace_handle, StringHandle identifier) const {
		return lookup_qualified_first(namespace_handle, identifier);
	}

	bool has_namespace_symbols(NamespaceHandle namespace_handle) const {
		if (!namespace_handle.isValid()) {
			return false;
		}
		return namespace_symbols_.find(namespace_handle) != namespace_symbols_.end();
	}

	// Look up a symbol using QualifiedIdentifier.
	// If the QualifiedIdentifier has a namespace, uses lookup_qualified.
	// Otherwise, falls back to regular unqualified lookup.
	std::optional<ASTNode> lookup_qualified(QualifiedIdentifier qi) const {
		if (qi.namespace_handle.isValid()) {
			return lookup_qualified(qi.namespace_handle, qi.identifier_handle);
		}
		return lookup(StringTable::getStringView(qi.identifier_handle), get_current_scope_handle());
	}

	template <typename StringContainer>
	std::optional<ASTNode> lookup_qualified(const StringContainer& namespaces, std::string_view identifier) const {
		return lookup_qualified(resolve_namespace_handle_impl(namespaces), identifier);
	}

	// Get the current namespace name (empty if not in a namespace)
	std::string_view get_current_namespace() const {
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			if (scope.scope_type == ScopeType::Namespace) {
				if (!scope.namespace_name.isValid()) {
					return "";
				}
				return StringTable::getStringView(scope.namespace_name);
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		return "";
	}

	NamespaceHandle get_current_namespace_handle() const {
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			if (scope.scope_type == ScopeType::Namespace) {
				return scope.namespace_handle;
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		return NamespaceRegistry::GLOBAL_NAMESPACE;
	}

	// Find the namespace where a specific function declaration is registered.
	// Used by codegen to restore proper namespace context in deferred generation paths.
	std::optional<NamespaceHandle> find_namespace_of_function(const FunctionDeclarationNode& function_node) const {
		auto matches_function = [&](const FunctionDeclarationNode& candidate) -> bool {
			if (&candidate == &function_node) {
				return true;
			}
			if (candidate.is_member_function() != function_node.is_member_function()) {
				return false;
			}
			if (candidate.parent_struct_name() != function_node.parent_struct_name()) {
				return false;
			}
			if (candidate.parameter_nodes().size() != function_node.parameter_nodes().size()) {
				return false;
			}
			if (candidate.decl_node().identifier_token().value() != function_node.decl_node().identifier_token().value()) {
				return false;
			}
			if (candidate.has_mangled_name() && function_node.has_mangled_name()) {
				return candidate.mangled_name() == function_node.mangled_name();
			}
			return true;
		};

		// Match by identity/signature and require uniqueness across namespaces.
		std::optional<NamespaceHandle> matched_namespace;
		bool ambiguous = false;

		auto search_map = [&](const std::unordered_map<NamespaceHandle, std::unordered_map<StringHandle, std::vector<ASTNode>>>& sym_map) {
			for (const auto& [ns_handle, symbol_map] : sym_map) {
				for (const auto& symbol_entry : symbol_map) {
					const auto& nodes = symbol_entry.second;
					for (const auto& candidate : nodes) {
						const FunctionDeclarationNode* func_node = nullptr;
						if (candidate.is<FunctionDeclarationNode>()) {
							func_node = &candidate.as<FunctionDeclarationNode>();
						} else if (candidate.is<TemplateFunctionDeclarationNode>()) {
							const auto& tmpl = candidate.as<TemplateFunctionDeclarationNode>();
							if (tmpl.function_declaration().is<FunctionDeclarationNode>()) {
								func_node = &tmpl.function_declaration().as<FunctionDeclarationNode>();
							}
						}
						if (func_node && matches_function(*func_node)) {
							if (matched_namespace.has_value() && *matched_namespace != ns_handle) {
								ambiguous = true;
								break;
							}
							matched_namespace = ns_handle;
						}
					}
					if (ambiguous)
						break;
				}
				if (ambiguous)
					break;
			}
		};

		search_map(namespace_symbols_);
		if (!ambiguous)
			search_map(adl_only_symbols_);
		if (!ambiguous && matched_namespace.has_value()) {
			return matched_namespace;
		}
		return std::nullopt;
	}

	// Get all using directives from the current scope and all enclosing scopes as handles.
	std::vector<NamespaceHandle> get_current_using_directive_handles() const {
		std::vector<NamespaceHandle> result;
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			for (const auto& using_dir : scope.using_directive_paths) {
				if (using_dir.isValid()) {
					result.push_back(using_dir);
				}
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		return result;
	}

	// Get all using declarations from the current scope and all enclosing scopes as handles.
	// Inner scopes shadow outer scopes - declarations in nested scopes override parent scopes for the same local_name.
	// Returns a map of local_name -> (namespace_handle, original_name)
	std::unordered_map<std::string_view, std::pair<NamespaceHandle, std::string_view>> get_current_using_declaration_handles() const {
		std::unordered_map<std::string_view, std::pair<NamespaceHandle, std::string_view>> result;
		std::vector<std::size_t> active_chain;
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			active_chain.push_back(scope_index);
			if (!scopes_[scope_index].parent_scope_id) {
				break;
			}
			scope_index = scopes_[scope_index].parent_scope_id.value - 1;
		}
		for (std::size_t chain_index = 0; chain_index < active_chain.size(); ++chain_index) {
			const Scope& scope = scopes_[active_chain[chain_index]];
			for (const auto& [local_name, target_info] : scope.using_declarations_handles) {
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
		scopes_.clear();
		scopes_.emplace_back(Scope(ScopeType::Global, 1));
		Scope& global_scope = scopes_.back();
		global_scope.scope_id = ScopeId{1};
		global_scope.parent_scope_id = ScopeId{};
		current_scope_index_ = 0;
		last_lookup_scope_id_ = ScopeId{};
		last_declaring_scope_id_ = ScopeId{};
		namespace_symbols_.clear();
		adl_only_symbols_.clear();
		adl_only_function_names_.clear();
		interned_strings_.clear();
		// Recreate the string allocator to fully release all memory
		string_allocator_ = ChunkedStringAllocator(64 * 1024);
	}

private:
	static Scope makeInitialGlobalScope() {
		Scope global_scope(ScopeType::Global, 1);
		global_scope.scope_id = ScopeId{1};
		global_scope.parent_scope_id = ScopeId{};
		return global_scope;
	}

	std::vector<Scope> scopes_ = {makeInitialGlobalScope()};
	std::size_t current_scope_index_ = 0;
	mutable ScopeId last_lookup_scope_id_;
	ScopeId last_declaring_scope_id_;
	// Persistent map of namespace contents
	// Uses NamespaceHandle as key to avoid string concatenation
	// Maps: namespace_handle -> (symbol_name -> vector<ASTNode>) to support overloading
	std::unordered_map<NamespaceHandle, std::unordered_map<StringHandle, std::vector<ASTNode>>> namespace_symbols_;
	// ADL-only symbols (hidden friends): not visible to ordinary unqualified lookup,
	// only reachable via lookup_adl() (C++20 [basic.lookup.argdep]).
	std::unordered_map<NamespaceHandle, std::unordered_map<StringHandle, std::vector<ASTNode>>> adl_only_symbols_;
	// Flat set of all ADL-only function name handles for O(1) is_adl_only_function_name() queries.
	std::unordered_set<StringHandle> adl_only_function_names_;

	// Dedicated string allocator for symbol table keys
	// Ensures string_view keys remain valid for the lifetime of the symbol table
	ChunkedStringAllocator string_allocator_{64 * 1024};	 // 64 KB chunks for symbol names

	void pushInsertUndo(
		SymbolTableInsertUndo* undo,
		SymbolTableInsertUndo::Op op,
		ScopeId scope_id,
		NamespaceHandle namespace_handle,
		StringHandle name_key,
		std::string_view scope_key,
		std::size_t index,
		ASTNode previous_node) const {
		if (undo == nullptr) {
			return;
		}
		SymbolTableInsertUndo::Entry entry{};
		entry.op = op;
		entry.scope_id = scope_id;
		entry.namespace_handle = namespace_handle;
		entry.name_key = name_key;
		entry.scope_key = scope_key;
		entry.index = index;
		entry.previous_node = previous_node;
		undo->entries.push_back(entry);
	}

	// Set to track all interned strings for fast O(1) deduplication
	std::unordered_set<std::string_view> interned_strings_;
	DiagnosticEngine* diagnostics_ = nullptr;

	[[noreturn]] void throwAmbiguousQualifiedLookup(StringHandle identifier) const {
		const std::string message =
			"ambiguous lookup: '" + std::string(StringTable::getStringView(identifier)) +
			"' found in multiple inline namespaces";
		if (diagnostics_ != nullptr) {
			throw makeStructuredCompileError(
				*diagnostics_,
				DiagnosticId::AmbiguousQualifiedLookup,
				DiagnosticSeverity::Error,
				SourceLocation(),
				message,
				{});
		}
		throw CompileError(message);
	}

	// Recursively collect associated namespaces from a struct/class type and all
	// of its base classes (C++20 [basic.lookup.argdep]/2: "the associated classes
	// of a class type include ... all of its base classes").
	// The visited set (keyed by TypeIndex) prevents infinite loops from diamond
	// inheritance and avoids redundant work.
	template <typename NsCallback>
	static void collect_struct_associated_namespaces(const StructTypeInfo* si,
													 NsCallback&& ns_callback,
													 std::unordered_set<size_t>& visited_types) {
		if (!si)
			return;
		ns_callback(si->namespace_handle);
		for (const auto& base : si->base_classes) {
			const TypeInfo* base_ti = tryGetTypeInfo(base.type_index);
			if (!base_ti)
				continue;
			if (!visited_types.insert(base.type_index.index()).second)
				continue;
			const StructTypeInfo* bsi = base_ti->getStructInfo();
			collect_struct_associated_namespaces(bsi, ns_callback, visited_types);
		}
	}

	// Intern a string_view by checking if it already exists, or allocate it
	// Returns a string_view that is guaranteed to remain valid
	std::string_view intern_string(std::string_view str) {
		// Check if this string has already been interned (O(1) lookup)
		auto it = interned_strings_.find(str);
		if (it != interned_strings_.end()) {
			return *it;	// Return existing interned string
		}

		// String not found, allocate it using StringBuilder and add to set
		StringBuilder sb(string_allocator_);
		std::string_view interned = sb.append(str).commit();
		interned_strings_.insert(interned);
		return interned;
	}

	// First-match qualified lookup without allocating an overload vector.
	// Mirrors lookup_qualified_all's inline-child walk and ambiguity rule, but
	// returns only the first node (same as lookup_qualified historically did via [0]).
	std::optional<ASTNode> lookup_qualified_first(NamespaceHandle namespace_handle, StringHandle identifier) const {
		if (!namespace_handle.isValid()) {
			return std::nullopt;
		}
		const std::vector<ASTNode>* first_nodes = nullptr;
		gNamespaceRegistry.forEachInlineChildNamespace(namespace_handle, [&](NamespaceHandle visible_ns) {
			auto ns_it = namespace_symbols_.find(visible_ns);
			if (ns_it == namespace_symbols_.end()) {
				return;
			}
			auto symbol_it = ns_it->second.find(identifier);
			if (symbol_it == ns_it->second.end() || symbol_it->second.empty()) {
				return;
			}
			if (!first_nodes) {
				first_nodes = &symbol_it->second;
				return;
			}
			if (!is_pure_function_set(*first_nodes) || !is_pure_function_set(symbol_it->second)) {
				// C++20 [namespace.qual]: same ambiguity rule as lookup_qualified_all.
				throwAmbiguousQualifiedLookup(identifier);
			}
			// Pure function sets across inline namespaces merge into one overload set;
			// the first match's [0] is unchanged, so nothing further is needed here.
		});
		if (!first_nodes) {
			return std::nullopt;
		}
		return (*first_nodes)[0];
	}

	template <typename Map, typename Key, typename Value>
	void update_or_insert(Map& map, const Key& key, Value value) {
		auto it = map.find(key);
		if (it != map.end()) {
			it->second = std::move(value);
		} else {
			map.emplace(key, std::move(value));
		}
	}

	static bool is_pure_function_set(std::span<const ASTNode> nodes) {
		return !nodes.empty() && std::all_of(nodes.begin(), nodes.end(), [](const ASTNode& node) {
			return is_function_or_template_function(node);
		});
	}

	static bool contains_same_function_decl(std::span<const ASTNode> nodes, const ASTNode& candidate) {
		const FunctionDeclarationNode* candidate_func = get_function_decl_node(candidate);
		if (!candidate_func) {
			return false;
		}
		for (const auto& existing : nodes) {
			if (get_function_decl_node(existing) == candidate_func) {
				return true;
			}
		}
		return false;
	}

	static void append_unique_function_overloads(std::vector<ASTNode>& existing_nodes, std::span<const ASTNode> new_nodes) {
		for (const auto& node : new_nodes) {
			if (!contains_same_function_decl(existing_nodes, node)) {
				existing_nodes.push_back(node);
			}
		}
	}

	void materialize_using_declaration_symbols(Scope& current_scope, std::string_view key, NamespaceHandle namespace_handle, std::string_view original_name) {
		auto resolved_nodes = lookup_qualified_all(namespace_handle, original_name);
		if (resolved_nodes.empty()) {
			return;
		}

		// C++20 [namespace.udecl]: a using-declaration at namespace scope makes the
		// name a member of that namespace. Merge into namespace_symbols_ so ordinary
		// lookup (which probes that map first for Namespace scopes) sees the full
		// overload set. Block/function scopes stay local to scope.symbols.
		if (current_scope.scope_type == ScopeType::Namespace) {
			NamespaceHandle dest_ns = get_current_namespace_handle();
			StringHandle ns_key = StringTable::getOrInternStringHandle(key);
			auto& ns_symbols = namespace_symbols_[dest_ns];
			auto ns_it = ns_symbols.find(ns_key);
			if (ns_it == ns_symbols.end()) {
				ns_symbols[ns_key] = std::move(resolved_nodes);
				return;
			}

			if (!is_pure_function_set(ns_it->second) || !is_pure_function_set(resolved_nodes)) {
				return;
			}

			append_unique_function_overloads(ns_it->second, resolved_nodes);
			return;
		}

		auto sym_it = current_scope.symbols.find(key);
		if (sym_it == current_scope.symbols.end()) {
			current_scope.symbols[key] = std::move(resolved_nodes);
			return;
		}

		if (!is_pure_function_set(sym_it->second) || !is_pure_function_set(resolved_nodes)) {
			return;
		}

		append_unique_function_overloads(sym_it->second, resolved_nodes);
	}

	// Using directives are stored as handles and checked in insertion order.
	// The first match is returned to match the legacy lookup behavior.
	std::optional<ASTNode> lookup_using_directives_first(const Scope& scope, std::string_view identifier) const {
		for (const auto& using_ns : scope.using_directive_paths) {
			auto result = lookup_qualified(using_ns, identifier);
			if (result.has_value()) {
				return result;
			}
		}
		return std::nullopt;
	}

	// Returns the first non-empty result to match the legacy lookup_all behavior.
	std::vector<ASTNode> lookup_all_using_directives_first(const Scope& scope, std::string_view identifier) const {
		std::vector<ASTNode> merged_overloads;
		for (const auto& using_ns : scope.using_directive_paths) {
			auto result = lookup_qualified_all(using_ns, identifier);
			if (result.empty()) {
				continue;
			}

			if (merged_overloads.empty()) {
				merged_overloads = std::move(result);
				continue;
			}

			if (!is_pure_function_set(merged_overloads) || !is_pure_function_set(result)) {
				return merged_overloads;
			}

			append_unique_function_overloads(merged_overloads, result);
		}
		return merged_overloads;
	}

	NamespaceHandle get_or_create_namespace_handle_from_path(std::span<const StringType<>> namespaces) const {
		NamespaceHandle current = NamespaceRegistry::GLOBAL_NAMESPACE;
		for (const auto& ns : namespaces) {
#if USE_OLD_STRING_APPROACH
			StringHandle name_handle = StringTable::getOrInternStringHandle(ns);
#else
			StringHandle name_handle = StringTable::getOrInternStringHandle(ns.view());
#endif
			current = gNamespaceRegistry.getOrCreateNamespace(current, name_handle);
			if (!current.isValid()) {
				return current;
			}
		}
		return current;
	}

public:
	// Exposed for parser namespace-handle resolution while preserving SymbolTable alias rules.
	// Set force_global=true for explicitly global-qualified names (e.g., ::ns::identifier).
	NamespaceHandle resolve_namespace_handle(std::span<const StringType<>> namespaces, bool force_global = false) const {
		if (namespaces.empty()) {
			return NamespaceRegistry::GLOBAL_NAMESPACE;
		}
		return resolve_namespace_handle_impl(namespaces, force_global);
	}

	NamespaceHandle resolve_namespace_handle(std::string_view qualified_namespace, bool force_global) const {
		if (qualified_namespace.empty()) {
			return NamespaceRegistry::GLOBAL_NAMESPACE;
		}

		bool effective_force_global = force_global;
		if (qualified_namespace.starts_with("::")) {
			effective_force_global = true;
			qualified_namespace.remove_prefix(2);
		}

		if (qualified_namespace.empty()) {
			return NamespaceRegistry::GLOBAL_NAMESPACE;
		}

		InlineVector<std::string_view, 4> namespace_components;
		size_t start = 0;
		while (true) {
			size_t pos = qualified_namespace.find("::", start);
			std::string_view component = (pos == std::string_view::npos)
											 ? qualified_namespace.substr(start)
											 : qualified_namespace.substr(start, pos - start);
			if (!component.empty()) {
				namespace_components.push_back(component);
			}
			if (pos == std::string_view::npos) {
				break;
			}
			start = pos + 2;
		}

		if (namespace_components.empty()) {
			return NamespaceRegistry::GLOBAL_NAMESPACE;
		}

		return resolve_namespace_handle_impl(namespace_components, effective_force_global);
	}

	NamespaceHandle resolve_namespace_handle(std::string_view qualified_namespace) const {
		return resolve_namespace_handle(qualified_namespace, false);
	}

private:
	template <typename StringContainer>
	NamespaceHandle resolve_namespace_handle_impl(const StringContainer& namespaces, bool force_global = false) const {
		if (namespaces.empty()) {
			return NamespaceRegistry::GLOBAL_NAMESPACE;
		}

		std::string_view first_component(namespaces[0]);
		if (auto alias_handle = resolve_namespace_alias_handle(first_component); alias_handle.has_value()) {
			return append_namespace_components(*alias_handle, namespaces, 1);
		}

		// Try resolving relative to the current namespace first (C++ unqualified lookup).
		// E.g., inside namespace outer, "inner::type" should resolve to "outer::inner::type".
		// Walk up the namespace hierarchy: in std::__cxx11, try std::__cxx11::inner, then std::inner.
		// Skip this for explicitly global-qualified names (::ns::identifier).
		if (!force_global) {
			NamespaceHandle current_ns = get_current_namespace_handle();
			StringHandle first_handle = StringTable::getOrInternStringHandle(first_component);
			while (!current_ns.isGlobal()) {
				NamespaceHandle child = gNamespaceRegistry.lookupNamespace(current_ns, first_handle);
				if (child.isValid()) {
					return append_namespace_components(child, namespaces, 1);
				}
				current_ns = gNamespaceRegistry.getParent(current_ns);
			}
		}

		return append_namespace_components(NamespaceRegistry::GLOBAL_NAMESPACE, namespaces, 0);
	}

	template <typename StringContainer>
	NamespaceHandle append_namespace_components(NamespaceHandle current,
												const StringContainer& namespaces,
												size_t start_index) const {
		for (size_t i = start_index; i < namespaces.size(); ++i) {
			StringHandle name_handle = StringTable::getOrInternStringHandle(std::string_view(namespaces[i]));
			current = gNamespaceRegistry.getOrCreateNamespace(current, name_handle);
			if (!current.isValid()) {
				return current;
			}
		}
		return current;
	}

	std::optional<NamespaceHandle> resolve_namespace_alias_handle(std::string_view alias) const {
		for (std::size_t scope_index = current_scope_index_; scope_index < scopes_.size();) {
			const Scope& scope = scopes_[scope_index];
			auto alias_handle_it = scope.namespace_aliases.find(alias);
			if (alias_handle_it != scope.namespace_aliases.end()) {
				return alias_handle_it->second;
			}
			if (!scope.parent_scope_id) {
				break;
			}
			scope_index = scope.parent_scope_id.value - 1;
		}
		return std::nullopt;
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
 * @brief Build a qualified name from a container of string-like types
 * (e.g., from QualifiedIdentifierNode::namespaces()) and a final identifier.
 * 
 * Note: This template exists separately from buildQualifiedName(...) because
 * the element type access differs based on USE_OLD_STRING_APPROACH preprocessor setting.
 * The duplication is intentional to handle both std::string and StackString<> element types.
 * 
 * Example: buildQualifiedNameFromStrings({"std", "chrono"}, "seconds") -> "std::chrono::seconds"
 */
template <typename StringContainer>
inline std::string_view buildQualifiedNameFromStrings(const StringContainer& namespaces, std::string_view name) {
	if (namespaces.empty()) {
		return name;
	}

	std::vector<StringHandle> components;
	components.reserve(namespaces.size() + 1);
	for (const auto& ns : namespaces) {
#if USE_OLD_STRING_APPROACH
		components.push_back(StringTable::getOrInternStringHandle(ns));
#else
		components.push_back(StringTable::getOrInternStringHandle(ns.view()));
#endif
	}
	components.push_back(StringTable::getOrInternStringHandle(name));
	StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(components);
	return StringTable::getStringView(qualified_handle);
}

/**
 * @brief Build a qualified name from a NamespaceHandle and a final identifier.
 * 
 * Example: buildQualifiedNameFromHandle(handle_for_std, "print") -> "std::print"
 */
inline std::string_view buildQualifiedNameFromHandle(NamespaceHandle ns_handle, std::string_view name) {
	StringHandle name_handle = StringTable::getOrInternStringHandle(name);
	StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(ns_handle, name_handle);
	return StringTable::getStringView(qualified_handle);
}

// Forward declaration for use in validateQualifiedNamespace
extern SymbolTable gSymbolTable;

/**
 * @brief Validate that a namespace handle refers to a known scope (declared namespace or class name).
 * Returns true if the namespace is valid (global, declared as namespace, or known as a class/struct type),
 * false if the identifier is completely unknown.
 *
 * @param in_template_context If true, also accepts qualifiers that are symbols in scope
 *        (e.g., type aliases like "using pointer = _Ptr;" in a template struct body).
 */
inline bool validateQualifiedNamespace(NamespaceHandle ns_handle, [[maybe_unused]] const Token& error_token,
									   bool in_template_context = false) {
	if (!ns_handle.isValid() || ns_handle.isGlobal()) {
		return true;
	}
	// Check if the root namespace was explicitly declared
	NamespaceHandle root = gNamespaceRegistry.getRootNamespace(ns_handle);
	if (gNamespaceRegistry.isDeclared(root)) {
		return true;
	}
	// Also accept if the root name refers to a known type (struct/class with static members)
	std::string_view root_name = gNamespaceRegistry.getName(root);
	StringHandle root_handle = StringTable::getOrInternStringHandle(root_name);
	if (getTypesByNameMap().find(root_handle) != getTypesByNameMap().end()) {
		return true;
	}
	// In template contexts, accept qualifiers that are symbols in scope (e.g., type aliases)
	if (in_template_context) {
		return true;
	}
	// Also accept if the root name is a known symbol (e.g., a type alias like "using pointer = _Ptr;")
	if (gSymbolTable.lookup(root_name).has_value()) {
		return true;
	}
	return false;
}

/**
 * @brief Build a full qualified name by joining all namespace components with "::".
 * This is used when you have multiple components and want to join them all.
 * 
 * Example: buildFullQualifiedName({"A", "B", "C"}) -> "A::B::C"
 * (Note: different from buildQualifiedName which appends "::" after each namespace before the name)
 */
template <typename StringContainer>
inline std::string_view buildFullQualifiedName(const StringContainer& components) {
	if (components.empty()) {
		return "";
	}

	std::vector<StringHandle> handles;
	handles.reserve(components.size());
	for (const auto& component : components) {
#if USE_OLD_STRING_APPROACH
		handles.push_back(StringTable::getOrInternStringHandle(component));
#else
		handles.push_back(StringTable::getOrInternStringHandle(component.view()));
#endif
	}
	StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(handles);
	return StringTable::getStringView(qualified_handle);
}

extern SymbolTable gSymbolTable;
