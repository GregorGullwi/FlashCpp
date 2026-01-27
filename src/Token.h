#pragma once

#include <string_view>

class Token {
public:
	enum class Type {
		Uninitialized,
		Identifier,
		Keyword,
		Literal,
		StringLiteral,
		CharacterLiteral,
		Operator,
		Punctuator,
		EndOfFile
	};

	Token() = default;
	Token(Type type, std::string_view value, size_t line, size_t column,
		size_t file_index)	// If you change this constructor, update the deleted std::string overload below
		: type_(type), value_(value), line_(line), column_(column),
		file_index_(file_index) {}

	// Deleted std::string overload so we don't accidentally pass temporary strings so we get dangling views
	Token(Type type, std::string value, size_t line, size_t column,
		size_t file_index) = delete;

	Type type() const { return type_; }
	std::string_view value() const { return value_; }
	size_t line() const { return line_; }
	size_t column() const { return column_; }
	size_t file_index() const { return file_index_; }

private:
	Type type_ = Type::Uninitialized;
	std::string_view value_;
	size_t line_ = 0;
	size_t column_ = 0;
	size_t file_index_ = 0;
};
