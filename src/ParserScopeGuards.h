// ===== src/ParserScopeGuards.h (header-only) =====
// Phase 3: RAII Scope Guards for Parser
//
// These classes provide automatic cleanup of parser state, replacing manual
// cleanup patterns with exception-safe RAII wrappers.

#pragma once
#include "SymbolTable.h"
#include "ParserTypes.h"
#include <vector>
#include <string_view>

// Forward declarations
class Parser;
struct TypeInfo;

namespace FlashCpp {

// =============================================================================
// TemplateParameterScope
// =============================================================================
// RAII guard for template parameter type registration.
// Automatically removes temporary template parameter types from gTypesByName
// when the scope exits (success or failure).
//
// Usage:
//   TemplateParameterScope scope;
//   for (auto& param : template_params) {
//       scope.addParameter(param_name, type_info_ptr);
//   }
//   // ... parse template body ...
//   // Parameters automatically removed when scope exits

class TemplateParameterScope {
public:
	TemplateParameterScope() = default;
	
	~TemplateParameterScope() {
		// Remove all registered template parameter types from the global type map
		for (const auto* type_info : registered_types_) {
			if (type_info) {
				gTypesByName.erase(type_info->name_);
			}
		}
	}
	
	// Prevent copying (scope is unique)
	TemplateParameterScope(const TemplateParameterScope&) = delete;
	TemplateParameterScope& operator=(const TemplateParameterScope&) = delete;
	
	// Allow moving
	TemplateParameterScope(TemplateParameterScope&& other) noexcept
		: registered_types_(std::move(other.registered_types_)) {
		other.registered_types_.clear();
	}
	TemplateParameterScope& operator=(TemplateParameterScope&& other) noexcept {
		if (this != &other) {
			// Clean up current registrations first
			for (const auto* type_info : registered_types_) {
				if (type_info) {
					gTypesByName.erase(type_info->name_);
				}
			}
			registered_types_ = std::move(other.registered_types_);
			other.registered_types_.clear();
		}
		return *this;
	}
	
	// Register a template parameter type for automatic cleanup
	void addParameter(TypeInfo* type_info) {
		if (type_info) {
			registered_types_.push_back(type_info);
		}
	}
	
	// Get the list of registered types (for iteration if needed)
	const std::vector<TypeInfo*>& registeredTypes() const {
		return registered_types_;
	}
	
	// Check if any parameters are registered
	bool empty() const { return registered_types_.empty(); }
	
	// Dismiss the guard (don't clean up - caller takes responsibility)
	void dismiss() { registered_types_.clear(); }

private:
	std::vector<TypeInfo*> registered_types_;
};


// =============================================================================
// SymbolTableScope
// =============================================================================
// RAII guard for symbol table scope management.
// Automatically exits the scope when the guard is destroyed.
//
// Usage:
//   SymbolTableScope scope(ScopeType::Function);
//   // ... add symbols, parse function body ...
//   // Scope automatically exited when guard destroyed

class SymbolTableScope {
public:
	explicit SymbolTableScope(ScopeType type) : active_(true) {
		gSymbolTable.enter_scope(type);
	}
	
	~SymbolTableScope() {
		if (active_) {
			gSymbolTable.exit_scope();
		}
	}
	
	// Prevent copying
	SymbolTableScope(const SymbolTableScope&) = delete;
	SymbolTableScope& operator=(const SymbolTableScope&) = delete;
	
	// Allow moving
	SymbolTableScope(SymbolTableScope&& other) noexcept : active_(other.active_) {
		other.active_ = false;
	}
	SymbolTableScope& operator=(SymbolTableScope&& other) noexcept {
		if (this != &other) {
			if (active_) {
				gSymbolTable.exit_scope();
			}
			active_ = other.active_;
			other.active_ = false;
		}
		return *this;
	}
	
	// Dismiss the guard (don't exit scope - caller takes responsibility)
	void dismiss() { active_ = false; }
	
	// Check if guard is still active
	bool isActive() const { return active_; }

private:
	bool active_;
};


// =============================================================================
// FunctionScopeGuard
// =============================================================================
// RAII guard for function parsing scope management.
// Combines symbol table scope with function-specific state management.
//
// Features:
// - Enters a function scope in the symbol table
// - Optionally tracks the current function being parsed
// - Automatically cleans up on scope exit
//
// Usage:
//   FunctionScopeGuard guard(parser, ctx);
//   guard.addParameters(param_nodes);
//   // ... parse function body ...
//   // Everything cleaned up when guard destroyed

class FunctionScopeGuard {
public:
	FunctionScopeGuard(Parser& parser, const FunctionParsingContext& ctx)
		: parser_(parser)
		, ctx_(ctx)
		, scope_(ScopeType::Function)
		, previous_function_(nullptr)
		, active_(true) {
		// Store previous function pointer (will be restored in destructor)
		// Note: The actual implementation of this depends on Parser internals
		// and is deferred to when the Parser class definition is available
	}
	
	~FunctionScopeGuard() {
		// Note: The scope_ member will automatically call gSymbolTable.exit_scope()
		// Additional cleanup handled here if needed
	}
	
	// Prevent copying
	FunctionScopeGuard(const FunctionScopeGuard&) = delete;
	FunctionScopeGuard& operator=(const FunctionScopeGuard&) = delete;
	
	// Add function parameters to the symbol table
	void addParameters(const std::vector<ASTNode>& params);
	
	// Inject 'this' pointer for member functions
	// Implementation defined in Parser.h after Parser class (needs access to internals)
	void injectThisPointer();
	
	// Dismiss the guard
	void dismiss() {
		scope_.dismiss();
		active_ = false;
	}
	
	// Check if guard is still active
	bool isActive() const { return active_; }

private:
	Parser& parser_;
	FunctionParsingContext ctx_;
	SymbolTableScope scope_;
	void* previous_function_;  // FunctionDeclarationNode* - forward declared
	bool active_;
};


// =============================================================================
// CombinedTemplateAndFunctionScope
// =============================================================================
// RAII guard that combines template parameter scope with function scope.
// Useful for template function parsing where both scopes need cleanup.
//
// Usage:
//   CombinedTemplateAndFunctionScope scope(parser, ctx);
//   scope.addTemplateParameter(type_info);
//   scope.addFunctionParameters(param_nodes);
//   // Both scopes cleaned up when guard destroyed

class CombinedTemplateAndFunctionScope {
public:
	CombinedTemplateAndFunctionScope(Parser& parser, const FunctionParsingContext& ctx)
		: template_scope_()
		, function_scope_(parser, ctx) {
	}
	
	// Prevent copying
	CombinedTemplateAndFunctionScope(const CombinedTemplateAndFunctionScope&) = delete;
	CombinedTemplateAndFunctionScope& operator=(const CombinedTemplateAndFunctionScope&) = delete;
	
	// Add a template parameter type for automatic cleanup
	void addTemplateParameter(TypeInfo* type_info) {
		template_scope_.addParameter(type_info);
	}
	
	// Add function parameters to the symbol table
	void addFunctionParameters(const std::vector<ASTNode>& params) {
		function_scope_.addParameters(params);
	}
	
	// Inject 'this' pointer for member functions
	void injectThisPointer() {
		function_scope_.injectThisPointer();
	}
	
	// Get access to individual scopes if needed
	TemplateParameterScope& templateScope() { return template_scope_; }
	FunctionScopeGuard& functionScope() { return function_scope_; }
	
	// Dismiss both scopes
	void dismiss() {
		template_scope_.dismiss();
		function_scope_.dismiss();
	}

private:
	TemplateParameterScope template_scope_;
	FunctionScopeGuard function_scope_;
};

} // namespace FlashCpp
