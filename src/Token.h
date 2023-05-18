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
		Operator,
		Punctuator,
		EndOfFile
	};

	Token() = default;
	Token(Type type, std::string_view value, size_t line, size_t column,
		size_t file_index)
		: type_(type), value_(value), line_(line), column_(column),
		file_index_(file_index) {}

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
