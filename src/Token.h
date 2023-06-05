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
	Token(Type type, std::string_view value, size_t line, int32_t column,
		int32_t file_index)
		: value_(value.data()), line_(line), column_(column),
		file_index_(file_index), value_len_(static_cast<int16_t>(value.length())), type_(type) {}

	Type type() const { return type_; }
	std::string_view value() const { return value_; }
	size_t line() const { return line_; }
	size_t column() const { return column_; }
	size_t file_index() const { return file_index_; }

private:
	const char* value_ = nullptr;
	size_t line_ = 0;
	int32_t column_ = 0;
	int32_t file_index_ = 0;
	int16_t value_len_ = 0;
	Type type_ = Type::Uninitialized;
};
