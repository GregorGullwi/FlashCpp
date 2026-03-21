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
class StructDeclarationNode;
class FunctionDeclarationNode;

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
				gTypesByName.erase(type_info->name());
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
					gTypesByName.erase(type_info->name());
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
// FunctionParsingScopeGuard
// =============================================================================
// RAII guard that owns ALL per-function parser entry/exit work:
//
//  Construction:
//   - Enters a function scope in the symbol table
//   - Saves and overwrites current_function_
//   - For member functions: calls setup_member_function_context() which handles
//     context-stack push, member-function registration, and 'this' injection
//   - Registers function parameters in the symbol table
//
//  Destruction (in reverse order):
//   - For member functions: pops member_function_context_stack_
//   - Restores current_function_
//   - Exits the function scope (via SymbolTableScope)
//
// Usage:
//   FunctionParsingScopeGuard guard(parser, is_member,
//                                   struct_node, struct_name, struct_type_index,
//                                   params, current_function);
//   // ... parse function body ...
//   // All cleanup happens automatically on scope exit, including on every early return

class FunctionParsingScopeGuard {
public:
	FunctionParsingScopeGuard(
		Parser& parser,
		bool is_member,
		StructDeclarationNode* struct_node,
		StringHandle struct_name,
		TypeIndex struct_type_index,
		const std::vector<ASTNode>& params,
		const FunctionDeclarationNode* current_function
	);
	~FunctionParsingScopeGuard();

	// Non-copyable, non-movable
	FunctionParsingScopeGuard(const FunctionParsingScopeGuard&) = delete;
	FunctionParsingScopeGuard& operator=(const FunctionParsingScopeGuard&) = delete;

private:
	Parser& parser_;
	SymbolTableScope scope_;
	bool pop_member_ctx_;
	const FunctionDeclarationNode* saved_function_;
};



// =============================================================================
// Generic RAII guard that saves a value on construction and restores it on
// destruction.  Intended for parser state fields that must be temporarily
// overwritten during template body re-parsing and then restored on all exit
// paths (normal return, early return, exception).
//
// Usage:
//   {
//       ScopedState guard(current_template_param_names_);
//       current_template_param_names_.clear();
//       // ... populate and use ...
//   } // original value is automatically restored here
//
// Replaces the manual three-line pattern:
//   auto saved = std::move(field);
//   field.clear();
//   /* ... */
//   field = std::move(saved);

template<typename T>
class ScopedState {
public:
	// Unconditional save/restore (the common case).
	explicit ScopedState(T& field)
		: field_ref_(field), saved_state_(std::move(field)) {}

	// Conditional save/restore: when active==false the field is left untouched
	// and the destructor is a no-op.  No default T{} is constructed when inactive.
	//   ScopedState guard(field, !vec.empty());
	//   if (!vec.empty()) field = new_value;
	explicit ScopedState(T& field, bool active)
		: field_ref_(field)
		, saved_state_(active ? std::optional<T>{std::move(field)} : std::nullopt) {}

	~ScopedState() { if (saved_state_) field_ref_ = std::move(*saved_state_); }

	ScopedState(const ScopedState&) = delete;
	ScopedState& operator=(const ScopedState&) = delete;

private:
	T&              field_ref_;
	std::optional<T> saved_state_;
};

// =============================================================================
// TemplateDepthGuard
// =============================================================================
// RAII guard that increments a parsing_template_depth_ counter on construction
// and decrements it on destruction.  Use this in place of the old
//   parsing_template_depth_++  (via TemplateDepthGuard)
// pattern so that nested template contexts correctly stack.
//
// Usage:
//   FlashCpp::TemplateDepthGuard guard(parsing_template_depth_);
//   // parsing_template_depth_ is incremented; identifier lookup now treats
//   // names as potentially dependent.
//   // On scope exit, the counter is decremented automatically.
class TemplateDepthGuard {
public:
	explicit TemplateDepthGuard(size_t& depth) : depth_(depth) { depth_++; }
	~TemplateDepthGuard() { if (depth_ > 0) depth_--; }
	TemplateDepthGuard(const TemplateDepthGuard&) = delete;
	TemplateDepthGuard& operator=(const TemplateDepthGuard&) = delete;
private:
	size_t& depth_;
};

} // namespace FlashCpp
