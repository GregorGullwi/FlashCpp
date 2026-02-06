#pragma once

#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <stack>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <variant>
#include <chrono>
#include <cstddef> // for std::max_align_t and std::size_t
#include <cstdio> // for std::snprintf
#include <time.h>

#include "CompileContext.h"
#include "FileTree.h"

using namespace std::string_view_literals;

// Maps a line in the preprocessed output to its original source location
// Defined here to avoid circular dependency (Lexer.h also uses this)
// The vector index IS the preprocessed line number (0-based: line_map_[line-1])
struct SourceLineMapping {
	size_t source_file_index;  // Index into the file_paths vector
	size_t source_line;        // Line number in original source file (1-based)
	size_t parent_line;        // Line in parent file where #include appeared (0 = no parent/main file)
};

// List of all separator characters for the preprocessor
// These are characters that can appear before/after identifiers without being part of them
constexpr char separator_chars[] = {
	' ', '\t', '\n', '\r',                      // Whitespace
	'!', '#', '%', '&', '(', ')', '*', '+',     // Operators and delimiters
	',', '-', '/', ':', ';', '<', '=', '>',     // More operators
	'?', '[', ']', '^', '{', '|', '}', '~'      // Brackets, braces, and bitwise
};

// Constexpr function to build the separator bitset at compile-time
constexpr uint64_t build_separator_bitset_chunk(int chunk_index) {
	uint64_t result = 0;
	for (char c : separator_chars) {
		unsigned char uc = static_cast<unsigned char>(c);
		if (uc >= chunk_index * 64 && uc < (chunk_index + 1) * 64) {
			result |= (1ULL << (uc % 64));
		}
	}
	return result;
}

struct DefineDirective {
	std::string body;
	std::vector<std::string> args;
	bool is_function_like = false;  // True if this is a function-like macro (even if it has no named args, like ...)
	
	// Constructor that allows specifying is_function_like
	DefineDirective(std::string body_val = "", std::vector<std::string> args_val = {}, bool function_like = false)
		: body(std::move(body_val)), args(std::move(args_val)), is_function_like(function_like) {}
};

struct FunctionDirective {
	std::function<std::string()> getBody;
};

struct Directive {
	std::variant<DefineDirective, FunctionDirective> directive_;

	Directive() = default;
	Directive(const Directive& other) = default;
	Directive(Directive&& other) = default;
	Directive(DefineDirective&& define_directive) : directive_(std::move(define_directive)) {}
	Directive(FunctionDirective&& function_directive) : directive_(std::move(function_directive)) {}
	Directive(std::function<std::string()> func) : directive_(FunctionDirective{ func }) {}

	Directive& operator=(const Directive& other) {
		if (this != &other) {
			directive_ = other.directive_;
		}
		return *this;
	}

	Directive& operator=(Directive&& other) noexcept {
		if (this != &other) {
			directive_ = std::move(other.directive_);
		}
		return *this;
	}

	std::string getBody() {
		return std::visit([](auto&& arg) {
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, DefineDirective>) {
				return arg.body;
			}
			else if constexpr (std::is_same_v<T, FunctionDirective>) {
				return arg.getBody();
			}
			else {
				static_assert(always_false<T>::value, "non-exhaustive visitor!");
			}
		}, directive_);
	}

	// Template function to get the specific type from content
	template <typename T>
	T* get_if() {
		return std::get_if<T>(&directive_);
	}

	template <typename T>
	const T* get_if() const {
		return std::get_if<T>(&directive_);
	}

private:
	template <typename T>
	struct always_false : std::false_type {};
};


struct CurrentFile {
	std::string_view file_name;
	long line_number = 0;
	std::string timestamp;  // File modification timestamp for __TIMESTAMP__
	long included_at_line = 0;  // Line number in parent file where this was #included (0 for main)
};

enum class Operator {
	And,
	Or,
	Greater,
	Less,
	Equals,
	NotEquals,
	LessEquals,
	GreaterEquals,
	Not,
	OpenParen,
	CloseParen,
	// Arithmetic operators
	Add,
	Subtract,
	Multiply,
	Divide,
	Modulo,
	// Bitwise operators
	LeftShift,
	RightShift,
	BitwiseAnd,
	BitwiseOr,
	BitwiseXor,
	BitwiseNot,
};

static std::unordered_map<Operator, int> precedence_table = {
	{Operator::Or, 1},             // ||
	{Operator::And, 2},            // &&
	{Operator::BitwiseOr, 3},      // |
	{Operator::BitwiseXor, 4},     // ^
	{Operator::BitwiseAnd, 5},     // &
	{Operator::Equals, 6},         // ==
	{Operator::NotEquals, 6},      // !=
	{Operator::Less, 7},           // <
	{Operator::Greater, 7},        // >
	{Operator::LessEquals, 7},     // <=
	{Operator::GreaterEquals, 7},  // >=
	{Operator::LeftShift, 8},      // <<
	{Operator::RightShift, 8},     // >>
	{Operator::Add, 9},            // +
	{Operator::Subtract, 9},       // -
	{Operator::Multiply, 10},      // *
	{Operator::Divide, 10},        // /
	{Operator::Modulo, 10},        // %
	{Operator::Not, 11},           // !
	{Operator::BitwiseNot, 11},    // ~
};

static Operator string_to_operator(std::string_view op) {
	if (op == "&&") return Operator::And;
	if (op == "||") return Operator::Or;
	if (op == ">") return Operator::Greater;
	if (op == "<") return Operator::Less;
	if (op == "==") return Operator::Equals;
	if (op == "!=") return Operator::NotEquals;
	if (op == "<=") return Operator::LessEquals;
	if (op == ">=") return Operator::GreaterEquals;
	if (op == "!") return Operator::Not;
	if (op == "(") return Operator::OpenParen;
	if (op == ")") return Operator::CloseParen;
	// Arithmetic operators
	if (op == "+") return Operator::Add;
	if (op == "-") return Operator::Subtract;
	if (op == "*") return Operator::Multiply;
	if (op == "/") return Operator::Divide;
	if (op == "%") return Operator::Modulo;
	// Bitwise operators
	if (op == "<<") return Operator::LeftShift;
	if (op == ">>") return Operator::RightShift;
	if (op == "&") return Operator::BitwiseAnd;
	if (op == "|") return Operator::BitwiseOr;
	if (op == "^") return Operator::BitwiseXor;
	if (op == "~") return Operator::BitwiseNot;
	FLASH_LOG(Lexer, Error, "Invalid operator ", op);
	throw std::invalid_argument(std::string("Invalid operator: ") + std::string(op));
}

struct CharInfo {
	Operator op;
	bool is_multi_char;
};

static std::unordered_map<char, CharInfo> char_info_table = {
	{'(', {Operator::OpenParen, false}},
	{')', {Operator::CloseParen, false}},
	{'!', {Operator::Not, true}},
	{'&', {Operator::BitwiseAnd, true}},  // Single & is bitwise, && is logical (handled by multi-char logic)
	{'|', {Operator::BitwiseOr, true}},   // Single | is bitwise, || is logical (handled by multi-char logic)
	{'>', {Operator::Greater, true}},
	{'<', {Operator::Less, true}},
	{'=', {Operator::Equals, true}},
	// Arithmetic operators
	{'+', {Operator::Add, false}},
	{'-', {Operator::Subtract, false}},
	{'*', {Operator::Multiply, false}},
	{'/', {Operator::Divide, false}},
	{'%', {Operator::Modulo, false}},
	{'^', {Operator::BitwiseXor, false}},
	{'~', {Operator::BitwiseNot, false}},
};

// Values follow the C++ feature-test macro convention (YYYYMM) for each attribute's availability
static const std::unordered_map<std::string_view, long> has_cpp_attribute_versions = {
	{ "deprecated", 201309 },
	{ "fallthrough", 201603 },
	{ "likely", 201803 },
	{ "unlikely", 201803 },
	{ "maybe_unused", 201603 },
	{ "no_unique_address", 201803 },
	{ "nodiscard", 201907 },
	{ "noreturn", 200809 },
};


// Check if a line has an incomplete macro invocation (unmatched parentheses)
// Returns true if there's an unmatched opening paren that could be from a macro
static bool hasIncompleteMacroInvocation(std::string_view line) {
	int paren_depth = 0;
	bool in_string = false;
	bool in_char = false;
	
	for (size_t i = 0; i < line.size(); ++i) {
		char c = line[i];
		
		// Handle escape sequences
		if ((in_string || in_char) && c == '\\' && i + 1 < line.size()) {
			i++;
			continue;
		}
		
		// Handle string literals
		if (!in_char && c == '"') {
			in_string = !in_string;
			continue;
		}
		
		// Handle char literals
		if (!in_string && c == '\'') {
			in_char = !in_char;
			continue;
		}
		
		if (!in_string && !in_char) {
			if (c == '(') paren_depth++;
			else if (c == ')') paren_depth--;
		}
	}
	
	return paren_depth > 0;  // Unmatched opening parens
}

static size_t findMatchingClosingParen(std::string_view sv, size_t opening_pos) {
	int nesting = 1;
	size_t pos = opening_pos + 1;
	bool in_string = false;
	bool in_char = false;
	char string_delimiter = '\0';

	while (pos < sv.size() && nesting > 0) {
		char c = sv[pos];

		// Handle escape sequences
		if ((in_string || in_char) && c == '\\' && pos + 1 < sv.size()) {
			pos += 2; // Skip the backslash and the next character
			continue;
		}

		// Handle string literals
		if (!in_char && c == '"') {
			if (!in_string) {
				in_string = true;
				string_delimiter = '"';
			} else if (string_delimiter == '"') {
				in_string = false;
				string_delimiter = '\0';
			}
			pos++;
			continue;
		}

		// Handle character literals
		if (!in_string && c == '\'') {
			if (!in_char) {
				in_char = true;
			} else {
				in_char = false;
			}
			pos++;
			continue;
		}

		// Only count parentheses outside of string/char literals
		if (!in_string && !in_char) {
			if (c == '(') {
				nesting++;
			}
			else if (c == ')') {
				nesting--;
			}
		}
		pos++;
	}

	if (nesting == 0) {
		return pos - 1;
	}
	else {
		return std::string::npos;
	}
}

static std::vector<std::string_view> splitArgs(std::string_view argsStr) {
    std::vector<std::string_view> args;

    auto is_whitespace = [](char c) { return c == ' ' || c == '\t'; };

    size_t n = argsStr.size();
    size_t i = 0;

    // Skip leading whitespace
    while (i < n && is_whitespace(argsStr[i])) i++;

    size_t arg_start = i;
    int paren_depth = 0;
    int angle_depth = 0;  // Track template angle brackets
    bool in_string = false;
    bool in_char = false;
    char string_delimiter = '\0';

    for (; i < n; ++i) {
        char c = argsStr[i];

        // Handle escape sequences in strings and character literals
        if ((in_string || in_char) && c == '\\' && i + 1 < n) {
            i++; // Skip the next character
            continue;
        }

        // Handle string literals
        if (!in_char && c == '"') {
            if (!in_string) {
                in_string = true;
                string_delimiter = '"';
            } else if (string_delimiter == '"') {
                in_string = false;
                string_delimiter = '\0';
            }
            continue;
        }

        // Handle character literals
        if (!in_string && c == '\'') {
            if (!in_char) {
                in_char = true;
            } else {
                in_char = false;
            }
            continue;
        }

        // Only process parentheses, angle brackets, and commas outside of string/char literals
        if (!in_string && !in_char) {
            if (c == '(') {
                paren_depth++;
                continue;
            }
            if (c == ')') {
                if (paren_depth == 0) {
                    // Final argument ends here
                    break;
                }
                paren_depth--;
                continue;
            }
            // Track template angle brackets for correct comma handling
            // Note: angle_depth > 0 check prevents going negative for standalone > chars
            // that aren't template closers (e.g., comparison operators)
            if (c == '<') {
                angle_depth++;
                continue;
            }
            if (c == '>') {
                if (angle_depth > 0) {
                    angle_depth--;
                }
                continue;
            }
            if (c == ',' && paren_depth == 0 && angle_depth == 0) {
                // Argument ended - only if not inside parens or angle brackets
                size_t end = i;

                // Trim trailing whitespace
                while (end > arg_start && is_whitespace(argsStr[end - 1]))
                    end--;

                args.emplace_back(argsStr.substr(arg_start, end - arg_start));

                // Move to next argument (skip the comma and any whitespace)
                i++;
                while (i < n && is_whitespace(argsStr[i]))
                    i++;

                if (i >= n)
                    return args;

                arg_start = i;
                i--; // Decrement because the for loop will increment it
            }
        }
    }

    // Final argument (if any)
    if (arg_start < n) {
        size_t end = i;
        while (end > arg_start && is_whitespace(argsStr[end - 1]))
            end--;
        args.emplace_back(argsStr.substr(arg_start, end - arg_start));
    }

    return args;
}


static void replaceAll(std::string& str, const std::string_view from, const std::string_view to) {
	// Helper to check if a character is a separator (not part of an identifier)
	auto is_separator = [](char c) {
		return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
	};

	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		// Check if this is a complete identifier match (not part of a larger word)
		bool start_ok = (pos == 0) || is_separator(str[pos - 1]);
		bool end_ok = (pos + from.length() >= str.length()) || is_separator(str[pos + from.length()]);
		
		if (start_ok && end_ok) {
			// This is a complete identifier, replace it
			str.replace(pos, from.length(), to);
			pos += to.length();
		} else {
			// This is part of a larger identifier, skip it
			pos++;
		}
	}
}

static std::tm localtime_safely(const std::time_t* time) {
	std::tm tm_now;
#ifdef _WIN32
	localtime_s(&tm_now, time);
#else
	localtime_r(time, &tm_now);
#endif
	return tm_now;
}


// The FileReader class is split across multiple files for maintainability.
#include "FileReader_Core.cpp"     // Class declaration, readFile, preprocessFileContent, conditional macros
#include "FileReader_Macros.cpp"   // expandMacros, addBuiltinDefines, helper methods
