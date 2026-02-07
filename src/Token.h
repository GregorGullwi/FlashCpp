#pragma once

#include <string_view>
#include "TokenKind.h"
#include "TokenTable.h"
#include "StringTable.h"

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
		file_index_(file_index), kind_(compute_kind(type, value)),
		handle_(intern_value(type, value)) {}

	// Deleted std::string overload so we don't accidentally pass temporary strings so we get dangling views
	Token(Type type, std::string value, size_t line, size_t column,
		size_t file_index) = delete;

	Type type() const { return type_; }
	std::string_view value() const { return value_; }
	StringHandle handle() const { return handle_; }
	size_t line() const { return line_; }
	size_t column() const { return column_; }
	size_t file_index() const { return file_index_; }
	TokenKind kind() const { return kind_; }

private:
	Type type_ = Type::Uninitialized;
	std::string_view value_;
	size_t line_ = 0;
	size_t column_ = 0;
	size_t file_index_ = 0;
	TokenKind kind_;
	StringHandle handle_;

	// Compute TokenKind from Token::Type and spelling
	static TokenKind compute_kind(Type type, std::string_view value) {
		switch (type) {
		case Type::Identifier:     return TokenKind::ident();
		case Type::Literal:        return TokenKind::literal();
		case Type::StringLiteral:  return TokenKind::string_literal();
		case Type::CharacterLiteral: return TokenKind::char_literal();
		case Type::EndOfFile:      return TokenKind::eof();
		case Type::Uninitialized:  return TokenKind::eof();
		case Type::Keyword:
		case Type::Operator:
		case Type::Punctuator: {
			TokenKind k = spell_to_kind(value);
			if (!k.is_eof()) return k;
			// Fallback for tokens not in the table
			if (type == Type::Keyword)    return TokenKind(TokenKind::Category::Keyword, 0xFFFF);
			if (type == Type::Operator)   return TokenKind(TokenKind::Category::Operator, 0xFFFF);
			return TokenKind(TokenKind::Category::Punctuator, 0xFFFF);
		}
		}
		return TokenKind::eof();
	}

	// Intern the token value into a StringHandle for fast lookups.
	// Only identifiers and literals benefit — keywords/operators/punctuators
	// are compared via TokenKind and don't need handle-based lookups.
	static StringHandle intern_value(Type type, std::string_view value) {
		switch (type) {
		case Type::Identifier:
		case Type::Literal:
		case Type::StringLiteral:
		case Type::CharacterLiteral:
		case Type::Keyword:
			// Intern these since they're frequently looked up in symbol tables
			if (!value.empty())
				return StringTable::getOrInternStringHandle(value);
			return StringHandle{};
		default:
			return StringHandle{};
		}
	}
};
