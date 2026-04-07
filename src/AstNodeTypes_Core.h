#pragma once

#include <climits>
#include <any>
#include <cstddef>
#include <string>
#include <string_view>
#include <typeinfo>
#include <variant>
#include <vector>
#include <deque>
#include <unordered_map>
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

// Forward declarations
struct TemplateTypeArg;

class BadASTNodeCast : public std::bad_any_cast {
public:
	BadASTNodeCast(const std::type_info* from_type, const std::type_info& to_type)
		: message_(buildMessage(from_type, to_type)) {}

	const char* what() const noexcept override {
		return message_.c_str();
	}

private:
	static std::string buildMessage(const std::type_info* from_type, const std::type_info& to_type) {
		StringBuilder sb;
		sb.append("ASTNode bad cast from "sv);
		if (from_type != nullptr) {
			sb.append(from_type->name());
		} else {
			sb.append("<empty>"sv);
		}
		sb.append(" to "sv).append(to_type.name());
		return std::string(sb.commit());
	}

	std::string message_;
};

class ASTNode {
public:
	ASTNode() = default;

	template <typename T>
	ASTNode(T* node) : raw_ptr_(node), type_info_(node ? &typeid(T) : nullptr) {}
	// Accepts const T* by stripping const; safe because ASTNode stores pointers into globally-owned
	// non-const storage (gChunkedAnyStorage / gTypeInfo). Used when visiting const variant alternatives.
	template <typename T>
	ASTNode(const T* node) : raw_ptr_(const_cast<T*>(node)), type_info_(node ? &typeid(T) : nullptr) {}

	template <typename T, typename... Args>
	static ASTNode emplace_node(Args&&... args) {
		T& t = gChunkedAnyStorage.emplace_back<T>(std::forward<Args>(args)...);
		return ASTNode(&t);
	}

	template <typename T>
	bool is() const {
		return type_info_ == &typeid(T);
	}

	template <typename T>
	T& as() {
		if (!is<T>()) {
			throw BadASTNodeCast(type_info_, typeid(T));
		}
		return *static_cast<T*>(raw_ptr_);
	}

	template <typename T>
	const T& as() const {
		if (!is<T>()) {
			throw BadASTNodeCast(type_info_, typeid(T));
		}
		return *static_cast<const T*>(raw_ptr_);
	}

	auto type_name() const {
		return type_info_ ? type_info_->name() : "<empty>";
	}

	bool has_value() const {
		return raw_ptr_ != nullptr;
	}

	const void* raw_pointer() const {
		return raw_ptr_;
	}

private:
	void* raw_ptr_ = nullptr;
	const std::type_info* type_info_ = nullptr;
};
