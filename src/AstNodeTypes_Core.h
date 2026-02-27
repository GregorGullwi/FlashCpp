#pragma once

#include <climits>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <deque>
#include <unordered_map>
#include <any>
#include <optional>
#include <memory>

#include "Token.h"
#include "ChunkedAnyVector.h"
#include "StackString.h"
#include "Lexer.h"
#include "StringTable.h"
#include "NamespaceRegistry.h"
#include "InlineVector.h"

// SaveHandle type for parser save/restore operations
// Matches Parser::SaveHandle typedef in Parser.h
using SaveHandle = size_t;

// Deferred template member function body information
// Used to store template member function bodies for parsing during instantiation
struct DeferredTemplateMemberBody {
	StringHandle function_name;           // Name of the function (for matching during instantiation)
	StringHandle struct_name;             // Name of the struct (from token, persistent)
	SaveHandle body_start;                // Handle to saved position at '{'
	SaveHandle initializer_list_start;    // Handle to saved position at ':' for constructor initializer list
	size_t struct_type_index;             // Type index (will be 0 for templates during definition)
	bool has_initializer_list;            // True if constructor has an initializer list
	bool is_constructor;                  // Special handling for constructors
	bool is_destructor;                   // Special handling for destructors
	bool is_const_method;                 // True if this is a const member function
	std::vector<StringHandle> template_param_names; // Template parameter names (copied, not views)
};

// Forward declarations
struct TemplateTypeArg;

class ASTNode {
public:
	ASTNode() = default;

	template <typename T> ASTNode(T* node) : node_(node) {}
	// Accepts const T* by stripping const; safe because ASTNode stores pointers into globally-owned
	// non-const storage (gChunkedAnyStorage / gTypeInfo). Used when visiting const variant alternatives.
	template <typename T> ASTNode(const T* node) : node_(const_cast<T*>(node)) {}

	template <typename T, typename... Args>
	static ASTNode emplace_node(Args&&... args) {
		T& t = gChunkedAnyStorage.emplace_back<T>(std::forward<Args>(args)...);
		return ASTNode(&t);
	}

	template <typename T> bool is() const {
		if (!node_.has_value()) return false;
		return node_.type() == typeid(T*);
	}

	template <typename T> T& as() {
		return *std::any_cast<T*>(node_);
	}

	template <typename T> const T& as() const {
		return *std::any_cast<T*>(node_);
	}

	auto type_name() const {
		return node_.type().name();
	}

	bool has_value() const {
		return node_.has_value();
	}
	
	// Direct access to underlying std::any (for debugging/workarounds)
	const std::any& get_any() const {
		return node_;
	}

private:
	std::any node_;
};
