#pragma once

#include <cctype>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <variant>
#include <vector>
#include <string>
#include <algorithm>

#include "Token.h"
#include "FileReader.h"  // For SourceLineMapping definition

struct TokenPosition {
	size_t cursor_;
	size_t line_;
	size_t column_;
	size_t current_file_index_;
};

class Lexer {
public:
	explicit Lexer(std::string_view source, 
	               const std::vector<SourceLineMapping>& line_map = {},
	               const std::vector<std::string>& file_paths = {})
		: source_(source), source_size_(source.size()), cursor_(0), line_(1),
		column_(1), file_paths_(file_paths), line_map_(line_map) {
		if (file_paths_.empty()) {
			file_paths_.push_back("<unknown>");
		}
		update_file_index_from_line();
	}
	
	const std::vector<std::string>& file_paths() const {
		return file_paths_;
	}
	
	// Get the text of a specific line from the preprocessed source
	std::string get_line_text(size_t line_num) const {
		if (line_num == 0) return "";
		
		size_t current_line = 1;
		size_t line_start = 0;
		
		// Find the start of the requested line
		for (size_t i = 0; i < source_size_; ++i) {
			if (current_line == line_num) {
				line_start = i;
				break;
			}
			if (source_[i] == '\n') {
				current_line++;
			}
		}
		
		if (current_line != line_num) return "";
		
		// Find the end of the line
		size_t line_end = line_start;
		while (line_end < source_size_ && source_[line_end] != '\n') {
			line_end++;
		}
		
		return std::string(source_.substr(line_start, line_end - line_start));
	}

	Token next_token() {
		size_t num_characters_left = source_size_ - cursor_;
		while (cursor_ < source_size_) {
			char c = source_[cursor_];

			if (std::isspace(c)) {
				consume_whitespace();
			}
			else if (c == '#' && cursor_ + 1 < source_size_ && std::isdigit(source_[cursor_ + 1])) {
				// Only consume as file info if # is followed by a digit (line directive)
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
			else if (c == '\'') {
				return consume_character_literal();
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
	
	// Get current position for later restoration
	TokenPosition getCurrentPosition() const {
		return TokenPosition{cursor_, line_, column_, current_file_index_};
	}
	
	// Restore lexer to a previously saved position
	void restorePosition(const TokenPosition& pos) {
		cursor_ = pos.cursor_;
		line_ = pos.line_;
		column_ = pos.column_;
		current_file_index_ = pos.current_file_index_;
	}

private:
	std::string_view source_;
	size_t source_size_;
	size_t cursor_;
	size_t line_;
	size_t column_;
	size_t current_file_index_ = 0;
	mutable std::vector<std::string> file_paths_;  // Mutable to allow adding "<unknown>" in constructor
	const std::vector<SourceLineMapping>& line_map_;
	
	// Update current_file_index_ based on current line_
	void update_file_index_from_line() {
		if (line_map_.empty() || file_paths_.empty()) {
			current_file_index_ = 0;
			return;
		}
		
		// Vector index IS the line number (0-based, so line_ - 1)
		if (line_ > 0 && line_ <= line_map_.size()) {
			current_file_index_ = line_map_[line_ - 1].source_file_index;
		}
	}

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
				update_file_index_from_line();
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
				update_file_index_from_line();
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

	Token consume_character_literal() {
		size_t start = cursor_;
		++cursor_;  // Skip opening '
		++column_;

		// Character literals can contain:
		// - A single character: 'a'
		// - An escape sequence: '\n', '\t', '\0', '\\', '\''
		while (cursor_ < source_size_ && source_[cursor_] != '\'') {
			if (source_[cursor_] == '\\') {
				// Skip the backslash and the next character (escape sequence)
				++cursor_;
				++column_;
				if (cursor_ < source_size_) {
					++cursor_;
					++column_;
				}
			}
			else {
				++cursor_;
				++column_;
			}
		}

		if (cursor_ < source_size_ && source_[cursor_] == '\'') {
			++cursor_;  // Skip closing '
			++column_;
		}
		else {
			// Handle unterminated character literal error
			// ...
		}

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::CharacterLiteral, value, line_, column_,
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
			"false",        "final",     "float",         "for",
			"friend",       "goto",      "if",            "inline",
			"int",          "long",      "mutable",       "namespace",
			"new",          "noexcept",  "not",           "not_eq",
			"nullptr",      "operator",  "or",            "or_eq",
			"override",     "private",   "protected",     "public",
			"register",     "reinterpret_cast", "requires", "return",
			"short",        "signed",    "sizeof",        "static",
			"static_assert", "static_cast", "struct",     "switch",
			"template",     "this",      "thread_local",  "throw",
			"true",         "try",       "typedef",       "typeid",
			"typename",     "union",     "unsigned",      "using",
			"virtual",      "void",      "volatile",      "wchar_t",
			"while",        "xor",       "xor_eq",
			// Microsoft-specific type keywords
			"__int8",       "__int16",   "__int32",       "__int64",
			// Microsoft-specific type modifiers/qualifiers
			"__ptr32",      "__ptr64",   "__w64",         "__unaligned",
			"__uptr",       "__sptr" };

		return keywords.count(value) > 0;
	}

	bool is_operator(char c) const {
		static const std::unordered_set<char> operators = { '+', '-', '*', '/', '%',
														   '^', '&', '|', '~', '!',
														   '=', '<', '>', '?' };

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
		char first_char = source_[cursor_];

		// Check for :: (scope resolution operator)
		if (first_char == ':' && cursor_ + 1 < source_size_ && source_[cursor_ + 1] == ':') {
			cursor_ += 2;
			column_ += 2;
			std::string_view value = source_.substr(start, 2);
			return Token(Token::Type::Punctuator, value, line_, column_,
				current_file_index_);
		}

		// Check for ... (variadic parameter)
		if (first_char == '.' && cursor_ + 2 < source_size_ &&
			source_[cursor_ + 1] == '.' && source_[cursor_ + 2] == '.') {
			cursor_ += 3;
			column_ += 3;
			std::string_view value = source_.substr(start, 3);
			return Token(Token::Type::Punctuator, value, line_, column_,
				current_file_index_);
		}

		++cursor_;
		++column_;

		std::string_view value = source_.substr(start, cursor_ - start);
		return Token(Token::Type::Punctuator, value, line_, column_,
			current_file_index_);
	}
};
