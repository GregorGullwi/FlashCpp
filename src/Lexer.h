#pragma once

#include <cctype>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>
#include <string>
#include <algorithm>

#include "Token.h"

struct TokenPosition {
	size_t cursor_;
	size_t line_;
	size_t column_;
	size_t current_file_index_;
};

class Lexer {
public:
	explicit Lexer(std::string_view source)
		: source_(source), source_size_(source.size()), cursor_(0), line_(1),
		column_(1), current_file_index_(0) {
		file_paths_.push_back("<unknown>");
	}

	Token next_token() {
		size_t num_characters_left = source_size_ - cursor_;
		while (cursor_ < source_size_) {
			char c = source_[cursor_];

			if (std::isspace(c)) {
				consume_whitespace();
			}
			else if (c == '#') {
				consume_file_info();
			}
			else if (c == '/' && num_characters_left >= 2) {
				if (source_[cursor_ + 1] == '/') {
					consume_single_line_comment();
					continue;
				}
				else if (source_[cursor_ + 1] == '*') {
					consume_multi_line_comment();
					continue;
				}
				else {
					// Handle division operator
					return consume_operator();
				}
			}
			else if (std::isalpha(c) || c == '_') {
				return consume_identifier_or_keyword();
			}
			else if (std::isdigit(c)) {
				return consume_literal();	// Positive number
			}
			else if (c == '-' && num_characters_left >= 1 && std::isdigit(source_[cursor_ + 1])) {
				return consume_literal();	// Negative number
			}
			else if (c == '.' && num_characters_left >= 1 && std::isdigit(source_[cursor_ + 1])) {
				return consume_literal();	// Floating-point literal starting with decimal point (e.g., .5f)
			}
			else if (c == '\"') {
				return consume_string_literal();
			}
			else if (is_operator(c)) {
				return consume_operator();
			}
			else if (is_punctuator(c)) {
				return consume_punctuator();
			}
			else {
				++cursor_;
				++column_;
			}
		}

		return Token(Token::Type::EndOfFile, "", line_, column_,
			current_file_index_);
	}

	const std::vector<std::string>& file_paths() const { return file_paths_; }

	TokenPosition save_token_position() {
		return { cursor_, line_, column_, current_file_index_ };
	}
	void restore_token_position(const TokenPosition& token_position) {
		cursor_ = token_position.cursor_;
		line_ = token_position.line_;
		column_ = token_position.column_;
		current_file_index_ = token_position.current_file_index_;
	}

	std::string_view get_source() const { return source_; }

private:
	std::string_view source_;
	size_t source_size_;
	size_t cursor_;
	size_t line_;
	size_t column_;
	size_t current_file_index_;
	std::vector<std::string> file_paths_;

	void consume_single_line_comment() {
		// Skip the '//'
		cursor_ += 2;
		column_ += 2;

		// Consume until end of line or end of file
		while (cursor_ < source_size_ && source_[cursor_] != '\n') {
			++cursor_;
			++column_;
		}
	}

	void consume_multi_line_comment() {
		// Skip the '/*'
		cursor_ += 2;
		column_ += 2;

		// Consume until '*/' or end of file
		while (cursor_ < source_size_) {
			if (source_[cursor_] == '\n') {
				++line_;
				column_ = 1;
			}
			else if (source_[cursor_] == '*' && cursor_ + 1 < source_size_ && source_[cursor_ + 1] == '/') {
				cursor_ += 2;  // Skip the '*/'
				column_ += 2;
				return;
			}
			else {
				++column_;
			}
			++cursor_;
		}
	}

	void consume_whitespace() {
		while (cursor_ < source_size_ && std::isspace(source_[cursor_])) {
			if (source_[cursor_] == '\n') {
				++line_;
				column_ = 1;
			}
			else {
				++column_;
			}
			++cursor_;
		}
	}

	void consume_file_info() {
		++cursor_; // Skip the '#' character
		++column_;

		size_t line_number = 0;
		while (cursor_ < source_size_ && std::isdigit(source_[cursor_])) {
			line_number = line_number * 10 + static_cast<size_t>(source_[cursor_] - '0');
			++cursor_;
			++column_;
		}

		if (cursor_ < source_size_ && std::isspace(source_[cursor_])) {
			consume_whitespace();
		}

		size_t start = cursor_;
		while (cursor_ < source_size_ && source_[cursor_] != '\n') {
			++cursor_;
			++column_;
		}

		std::string_view file_path = source_.substr(start, cursor_ - start);
		auto it = std::find(file_paths_.begin(), file_paths_.end(), file_path);
		if (it == file_paths_.end()) {
			file_paths_.push_back(std::string(file_path));
			current_file_index_ = file_paths_.size() - 1;
		}
		else {
			current_file_index_ = std::distance(file_paths_.begin(), it);
		}

		line_ = line_number;
	}

	Token consume_identifier_or_keyword() {
		size_t start = cursor_;
		++cursor_;
		++column_;

		while (cursor_ < source_size_ &&
			(std::isalnum(source_[cursor_]) || source_[cursor_] == '_')) {
			++cursor_;
			++column_;
		}

		std::string_view value = source_.substr(start, cursor_ - start);
		if (is_keyword(value)) {
			return Token(Token::Type::Keyword, value, line_, column_,
				current_file_index_);
		}
		else {
			return Token(Token::Type::Identifier, value, line_, column_,
				current_file_index_);
		}
	}

	Token consume_literal() {
		size_t start = cursor_;
		char first_char = source_[cursor_];
		++cursor_;
		++column_;

		// Check if this is a floating-point literal starting with decimal point (e.g., .5f)
		if (first_char == '.') {
			// Consume fractional part
			while (cursor_ < source_size_ && (std::isdigit(source_[cursor_]) || source_[cursor_] == '\'')) {
				++cursor_;
				++column_;
			}
			// Skip to exponent/suffix handling below
		}
		// Check for prefix (hex, octal, binary)
		else if (source_[cursor_] == 'x') {
			// Hexadecimal literal
			++cursor_;
			++column_;

			while (cursor_ < source_size_ && (std::isxdigit(source_[cursor_]) || source_[cursor_] == '\'')) {
				++cursor_;
				++column_;
			}
		}
		else if (source_[cursor_] == '0') {
			// Octal or binary literal
			++cursor_;
			++column_;

			if (cursor_ < source_size_ && (source_[cursor_] == 'b' || source_[cursor_] == 'B')) {
				// Binary literal
				++cursor_;
				++column_;

				while (cursor_ < source_size_ && (source_[cursor_] == '0' || source_[cursor_] == '1' || source_[cursor_] == '\'')) {
					++cursor_;
					++column_;
				}
			}
			else {
				// Octal literal
				while (cursor_ < source_size_ && (std::isdigit(source_[cursor_]) || source_[cursor_] == '\'')) {
					++cursor_;
					++column_;
				}
			}
		}
		else {
			// Decimal literal
			while (cursor_ < source_size_ && (std::isdigit(source_[cursor_]) || source_[cursor_] == '\'')) {
				++cursor_;
				++column_;
			}
		}

		// Check for decimal point (floating-point literal) - only if we didn't start with one
		if (first_char != '.' && cursor_ < source_size_ && source_[cursor_] == '.') {
			++cursor_;
			++column_;

			// Consume fractional part
			while (cursor_ < source_size_ && (std::isdigit(source_[cursor_]) || source_[cursor_] == '\'')) {
				++cursor_;
				++column_;
			}
		}

		// Check for exponent (e.g., 1.5e10, 3e-5)
		if (cursor_ < source_size_ && (source_[cursor_] == 'e' || source_[cursor_] == 'E')) {
			++cursor_;
			++column_;

			// Optional sign
			if (cursor_ < source_size_ && (source_[cursor_] == '+' || source_[cursor_] == '-')) {
				++cursor_;
				++column_;
			}

			// Exponent digits
			while (cursor_ < source_size_ && std::isdigit(source_[cursor_])) {
				++cursor_;
				++column_;
			}
		}

		// Handle suffixes: ul for integers, f/F for float, l/L for long/long double
		static constexpr std::string_view suffixCharacters = "ulfULF";
		while (cursor_ < source_size_ && suffixCharacters.find(source_[cursor_]) != std::string::npos) {
			++cursor_;
			++column_;
		}

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::Literal, value, line_, column_, current_file_index_);
	}

	Token consume_string_literal() {
		size_t start = cursor_;
		++cursor_;
		++column_;

		while (cursor_ < source_size_ && source_[cursor_] != '\"') {
			if (source_[cursor_] == '\\') {
				// Handle escape sequences if needed
				// or should that be part of the preprocessor?
			}
			++cursor_;
			++column_;
		}

		if (cursor_ < source_size_ && source_[cursor_] == '\"') {
			++cursor_;
			++column_;
		}
		else {
			// Handle unterminated string literal error
			// ...
		}

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::StringLiteral, value, line_, column_,
			current_file_index_);
	}

	bool is_keyword(std::string_view value) const {
		static const std::unordered_set<std::string_view> keywords = {
			"alignas",      "alignof",   "and",           "and_eq",
			"asm",          "auto",      "bitand",        "bitor",
			"bool",         "break",     "case",          "catch",
			"char",         "char8_t",   "char16_t",      "char32_t",
			"class",        "compl",     "concept",       "const",
			"constexpr",    "consteval", "constinit",     "const_cast",
			"continue",     "decltype",  "default",       "delete",
			"do",           "double",    "dynamic_cast",  "else",
			"enum",         "explicit",  "export",        "extern",
			"false",        "float",     "for",           "friend",
			"goto",         "if",        "inline",        "int",
			"long",         "mutable",   "namespace",     "new",
			"noexcept",     "not",       "not_eq",        "nullptr",
			"operator",     "or",        "or_eq",         "private",
			"protected",    "public",    "register",      "reinterpret_cast",
			"requires",     "return",    "short",         "signed",
			"sizeof",       "static",    "static_assert", "static_cast",
			"struct",       "switch",    "template",      "this",
			"thread_local", "throw",     "true",          "try",
			"typedef",      "typeid",    "typename",      "union",
			"unsigned",     "using",     "virtual",       "void",
			"volatile",     "wchar_t",   "while",         "xor",
			"xor_eq" };

		return keywords.count(value) > 0;
	}

	bool is_operator(char c) const {
		static const std::unordered_set<char> operators = { '+', '-', '*', '/', '%',
														   '^', '&', '|', '~', '!',
														   '=', '<', '>', '?', ':' };

		return operators.count(c) > 0;
	}

	Token consume_operator() {
		size_t start = cursor_;
		char first_char = source_[start];
		++cursor_;
		++column_;

		// Handle multi-character operators using branchless switch optimization
		if (cursor_ < source_size_) {
			char second_char = source_[cursor_];

			switch (first_char) {
				case '-': { // -> -- -=
					int advance = (second_char == '>') | (second_char == '-') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '+': { // ++ +=
					int advance = (second_char == '+') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '<': { // << <= <<=
					int advance = (second_char == '<') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					// Branchless check for three-character operator <<=
					int is_shift = (second_char == '<');
					int has_third = (cursor_ < source_size_);
					char third_char = has_third ? source_[cursor_] : '\0';
					int advance3 = is_shift & (third_char == '=');
					cursor_ += advance3;
					column_ += advance3;
					break;
				}
				case '>': { // >> >= >>=
					int advance = (second_char == '>') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					// Branchless check for three-character operator >>=
					int is_shift = (second_char == '>');
					int has_third = (cursor_ < source_size_);
					char third_char = has_third ? source_[cursor_] : '\0';
					int advance3 = is_shift & (third_char == '=');
					cursor_ += advance3;
					column_ += advance3;
					break;
				}
				case '=': { // ==
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '!': { // !=
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '&': { // && &=
					int advance = (second_char == '&') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '|': { // || |=
					int advance = (second_char == '|') | (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '*': { // *=
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '/': { // /=
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '%': { // %=
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				case '^': { // ^=
					int advance = (second_char == '=');
					cursor_ += advance;
					column_ += advance;
					break;
				}
				default:
					// Single-character operator, no action needed
					break;
			}
		}

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::Operator, value, line_, column_,
			current_file_index_);
	}

	bool is_punctuator(char c) const {
		static const std::unordered_set<char> punctuators = {
			'(', ')', '[', ']', '{', '}', '.', ',', ';', ':', '#' };

		return punctuators.count(c) > 0;
	}

	Token consume_double_colon_punctuator() {
		size_t start = cursor_;
		cursor_ += 2;
		column_ += 2;

		std::string_view value = source_.substr(start, 2);
		return Token(Token::Type::Punctuator, value, line_, column_,
			current_file_index_);
	}

	Token consume_punctuator() {
		size_t start = cursor_;
		++cursor_;
		++column_;

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::Punctuator, value, line_, column_,
			current_file_index_);
	}
};
