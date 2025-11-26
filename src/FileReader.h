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
	std::cerr << "Invalid operator " << op;
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

static size_t findMatchingClosingParen(std::string_view sv, size_t opening_pos) {
	int nesting = 1;
	size_t pos = opening_pos + 1;
	while (pos < sv.size() && nesting > 0) {
		if (sv[pos] == '(') {
			nesting++;
		}
		else if (sv[pos] == ')') {
			nesting--;
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

    size_t n = argsStr.size();
    size_t i = 0;

    // Skip leading whitespace
    while (i < n && (argsStr[i] == ' ' || argsStr[i] == '\t')) i++;

    size_t arg_start = i;
    int paren_depth = 0;

    for (; i < n; ++i) {
        char c = argsStr[i];

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
        if (c == ',' && paren_depth == 0) {
            // Argument ended
            size_t end = i;

            // Trim trailing whitespace
            while (end > arg_start && (argsStr[end - 1] == ' ' || argsStr[end - 1] == '\t'))
                end--;

            args.emplace_back(argsStr.substr(arg_start, end - arg_start));

            // Move to next argument
            i = argsStr.find_first_not_of(" \t", i + 1);
            if (i == std::string_view::npos)
                return args;

            arg_start = i;
        }
    }

    // Final argument (if any)
    if (arg_start < n) {
        size_t end = i;
        while (end > arg_start && (argsStr[end - 1] == ' ' || argsStr[end - 1] == '\t'))
            end--;
        args.emplace_back(argsStr.substr(arg_start, end - arg_start));
    }

    return args;
}


static void replaceAll(std::string& str, const std::string_view from, const std::string_view to) {
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.length(), to);
		pos += to.length();
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

class FileReader {
public:
	static constexpr size_t default_result_size = 1024 * 1024;
	FileReader(CompileContext& settings, FileTree& tree) : settings_(settings), tree_(tree) {
		addBuiltinDefines();
		result_.reserve(default_result_size);
	}
	
	// Get the line mapping for source location tracking
	const std::vector<SourceLineMapping>& get_line_map() const {
		return line_map_;
	}
	
	// Get the file paths vector for looking up source files from line map
	const std::vector<std::string>& get_file_paths() const {
		return file_paths_;
	}

	size_t find_first_non_whitespace_after_hash(const std::string& str) {
		size_t pos = str.find('#');
		if (pos == std::string::npos) {
			return pos;
		}
		return str.find_first_not_of(" \t", pos + 1);
	}

	bool readFile(std::string_view file, long included_at_line = 0) {
		if (proccessedHeaders_.find(std::string(file)) != proccessedHeaders_.end())
			return true;

		// Check for excessive include nesting depth (prevents infinite recursion)
		constexpr size_t MAX_INCLUDE_DEPTH = 200;
		if (filestack_.size() >= MAX_INCLUDE_DEPTH) {
			std::cerr << "Error: Include nesting depth exceeded " << MAX_INCLUDE_DEPTH << " (possible circular include)" << std::endl;
			return false;
		}

		if (settings_.isVerboseMode()) {
			std::cout << "readFile " << file << " (depth: " << filestack_.size() << ")" << std::endl;
		}

		// Save the current file index and parent line to restore when we return from this file
		size_t saved_file_index = current_file_index_;
		size_t saved_parent_line = current_parent_line_;
		
		// Register this file in the file_paths_ vector and update current index
		current_file_index_ = get_or_add_file_path(file);
		
		// The parent line is the preprocessed line number corresponding to the #include directive
		// in the parent file. We need to find that in the line_map.
		// For the main file, included_at_line is 0, so current_parent_line_ stays 0
		if (included_at_line > 0 && !line_map_.empty()) {
			// Find the most recent line_map entry for the parent file at the include line
			// We search backwards because we just processed that line
			for (size_t i = line_map_.size(); i > 0; --i) {
				if (line_map_[i - 1].source_file_index == saved_file_index &&
				    line_map_[i - 1].source_line == static_cast<size_t>(included_at_line)) {
					current_parent_line_ = i; // 1-based preprocessed line number
					break;
				}
			}
		} else {
			current_parent_line_ = 0;  // Main file has no parent
		}

		ScopedFileStack filestack(filestack_, file, included_at_line);

		std::ifstream stream(file.data());
		if (!stream.is_open()) {
			current_file_index_ = saved_file_index;  // Restore on error
			current_parent_line_ = saved_parent_line;
			return false;
		}

		tree_.addFile(file);

		stream.seekg(0, std::ios::end);
		std::streampos file_size = stream.tellg();
		stream.seekg(0, std::ios::beg);
		std::string file_content(file_size, '\0');
		stream.read(file_content.data(), file_size);

		bool result = preprocessFileContent(file_content);
		
		// Restore the previous file index and parent line when returning
		current_file_index_ = saved_file_index;
		current_parent_line_ = saved_parent_line;
		
		return result;
	}

	bool preprocessFileContent(const std::string& file_content) {
		std::istringstream stream(file_content);
		std::string line;
		bool in_comment = false;
		std::stack<bool> skipping_stack;
		skipping_stack.push(false); // Initial state: not skipping
		// Track whether any condition in an #if/#elif chain has been true
		// This is needed for proper #elif handling
		std::stack<bool> condition_was_true_stack;
		condition_was_true_stack.push(false);

		long line_number_fallback = 1;
		long& line_number = !filestack_.empty() ? filestack_.top().line_number : line_number_fallback;
		long prev_line_number = -1;
		const bool isPreprocessorOnlyMode = settings_.isPreprocessorOnlyMode();
		size_t line_counter = 0;  // Add counter for debugging
		while (std::getline(stream, line)) {
			line_counter++;
			if (settings_.isVerboseMode() && line_counter % 100 == 0) {
				std::cout << "Processing line " << line_counter << " in " << filestack_.top().file_name << std::endl;
			}
			size_t first_none_tab = line.find_first_not_of('\t');
			if (first_none_tab != std::string::npos && first_none_tab != 0)
				line.erase(line.begin(), line.begin() + first_none_tab);

			++line_number;

			if (isPreprocessorOnlyMode && prev_line_number != line_number - 1) {
				std::cout << "# " << line_number << " \"" << filestack_.top().file_name << "\"\n";
				prev_line_number = line_number;
			}
			else {
				++prev_line_number;
			}

			if (in_comment) {
				size_t end_comment_pos = line.find("*/");
				if (end_comment_pos != std::string::npos) {
					in_comment = false;
					line = line.substr(end_comment_pos + 2);
				}
				else {
					continue;
				}
			}

			size_t start_comment_pos = line.find("/*");
			if (start_comment_pos != std::string::npos) {
				size_t end_comment_pos = line.find("*/", start_comment_pos);
				if (end_comment_pos != std::string::npos) {
					line.erase(start_comment_pos, end_comment_pos - start_comment_pos + 2);
				}
				else {
					in_comment = true;
					continue;
				}
			}

			if (skipping_stack.size() == 0) {
				std::cerr << "Internal compiler error in file " << filestack_.top().file_name << ":" << line_number << std::endl;
				return false;
			}
			const bool skipping = skipping_stack.top();

			// Find the position of the '#' character
			size_t directive_pos = line.find('#');
			if (directive_pos != std::string::npos) {
				size_t first_non_space = line.find_first_not_of(' ', 0);
				if (first_non_space > 0) {
					line.erase(0, first_non_space);
					directive_pos -= first_non_space;
				}
				// Find the position of the first non-whitespace character after the '#'
				size_t next_pos = find_first_non_whitespace_after_hash(line);
				if (next_pos != std::string::npos && (next_pos != directive_pos + 1)) {
					// Remove whitespaces between '#' and the directive
					line = line.substr(0, directive_pos + 1) + line.substr(next_pos);
				}

				size_t i;
				while ((i = line.rfind('\\')) && (i == line.size() - 1)) {
					std::string next_line;
					std::getline(stream, next_line);
					line.erase(line.end() - 1);
					line.append(next_line);
					++line_number;
				}
			}

			if (skipping) {
				if (line.find("#endif", 0) == 0) {
					skipping_stack.pop();
					condition_was_true_stack.pop();
				}
				else if (line.find("#if", 0) == 0) {
					// Nesting: #if, #ifdef, #ifndef all start with "#if"
					// Push a new skipping state for any nested conditional
					// Mark condition_was_true as true to prevent #else/#elif from activating
					// (since we're skipping due to an outer condition, not this one)
					skipping_stack.push(true);
					condition_was_true_stack.push(true);  // Changed from false
				}
				else if (line.find("#elif", 0) == 0) {
					// If we're skipping and haven't found a true condition yet, evaluate #elif
					if (!condition_was_true_stack.top()) {
						std::string condition = line.substr(5);  // Skip "#elif"
						condition = expandMacrosForConditional(condition);
						std::istringstream iss(condition);
						long expression_result = evaluate_expression(iss);
						if (expression_result != 0) {
							skipping_stack.top() = false;  // Stop skipping
							condition_was_true_stack.top() = true;  // Mark that we found a true condition
						}
					}
					// If a previous condition was true, keep skipping
				}
				else if (line.find("#else", 0) == 0) {
					// Only stop skipping if no previous condition was true
					if (!condition_was_true_stack.top()) {
						skipping_stack.top() = false;
						condition_was_true_stack.top() = true;
					}
				}
				continue;
			}

			size_t comment_pos = line.find("//");
			if (comment_pos != std::string::npos) {
				if (comment_pos == 0) {
					continue;
				}
				line = line.substr(0, comment_pos);
			}

			if (line.find("#include", 0) == 0) {
				// Record the #include line in line_map BEFORE processing it
				// so that the included file can find its parent
				append_line_with_tracking("// " + line);  // Comment it out in output
				
				if (!processIncludeDirective(line, filestack_.top().file_name, line_number)) {
					return false;
				}
				// Reset prev_line_number so we print the next row
				prev_line_number = 0;
			}
			else if (line.find("#define", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#define"sv.length());
				handleDefine(iss);
			}
			else if (line.find("#ifdef", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#ifdef"sv.length());
				std::string symbol;
				iss >> symbol;
				bool is_defined = defines_.count(symbol) > 0;
				skipping_stack.push(!is_defined);
				condition_was_true_stack.push(is_defined);
			}
			else if (line.find("#ifndef", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#ifndef"sv.length());
				std::string symbol;
				iss >> symbol;
				bool is_defined = defines_.count(symbol) > 0;
				skipping_stack.push(is_defined);
				condition_was_true_stack.push(!is_defined);
			}
			else if (line.find("#if", 0) == 0) {
				// Extract and expand macros in the condition before evaluation
				std::string condition = line.substr(3);  // Skip "#if"
				condition = expandMacrosForConditional(condition);
				std::istringstream iss(condition);
				long expression_result = evaluate_expression(iss);
				bool condition_true = (expression_result != 0);
				skipping_stack.push(!condition_true);
				condition_was_true_stack.push(condition_true);
			}
			else if (line.find("#elif", 0) == 0) {
				if (skipping_stack.empty() || condition_was_true_stack.empty()) {
					std::cerr << "Unmatched #elif directive" << std::endl;
					return false;
				}
				// If a previous condition was true, start skipping
				if (condition_was_true_stack.top()) {
					skipping_stack.top() = true;
				}
				else {
					// Evaluate the #elif condition
					std::string condition = line.substr(5);  // Skip "#elif"
					condition = expandMacrosForConditional(condition);
					std::istringstream iss(condition);
					long expression_result = evaluate_expression(iss);
					if (expression_result != 0) {
						skipping_stack.top() = false;  // Stop skipping
						condition_was_true_stack.top() = true;  // Mark condition as true
					}
				}
			}
			else if (line.find("#else", 0) == 0) {
				if (skipping_stack.empty() || condition_was_true_stack.empty()) {
					std::cerr << "Unmatched #else directive" << std::endl;
					return false;
				}
				// Only execute #else block if no previous condition was true
				if (condition_was_true_stack.top()) {
					skipping_stack.top() = true;  // Start skipping
				}
				else {
					skipping_stack.top() = false;  // Stop skipping
					condition_was_true_stack.top() = true;  // Mark that we're in a true block
				}
			}
			else if (line.find("#endif", 0) == 0) {
				if (!skipping_stack.empty()) {
					skipping_stack.pop();
					condition_was_true_stack.pop();
				}
				else {
					std::cerr << "Unmatched #endif directive" << std::endl;
					return false;
				}
			}
			else if (line.find("#error", 0) == 0) {
				std::string message = line.substr(6);
				// Trim leading whitespace
				size_t first_non_space = message.find_first_not_of(" \t");
				if (first_non_space != std::string::npos) {
					message = message.substr(first_non_space);
				}
				std::cerr << "Error: " << message << std::endl;
				return false;
			}
			else if (line.find("#warning", 0) == 0) {
				std::string message = line.substr(8);
				// Trim leading whitespace
				size_t first_non_space = message.find_first_not_of(" \t");
				if (first_non_space != std::string::npos) {
					message = message.substr(first_non_space);
				}
				std::cerr << "Warning: " << message << std::endl;
			}
			else if (line.find("#undef") == 0) {
				std::istringstream iss(line);
				iss.seekg("#undef"sv.length());
				std::string symbol;
				iss >> symbol;
				defines_.erase(symbol);
			}
			else if (line.find("#pragma once", 0) == 0) {
				proccessedHeaders_.insert(std::string(filestack_.top().file_name));
			}
			else if (line.find("#pragma pack", 0) == 0) {
				processPragmaPack(line);
				// Pass through the pragma pack directive to the parser
				append_line_with_tracking(line);
			}
			else if (line.find("#pragma", 0) == 0) {
				// Skip other #pragma directives (like #pragma GCC visibility)
				// These are compiler-specific and don't need to be passed to the parser
				// Just output a blank line to preserve line numbers
				append_line_with_tracking("");
			}
			else if (line.find("#line", 0) == 0) {
				processLineDirective(line);
			}
			else {
				if (line.size() > 0 && (line.find_last_of(' ') != line.size() - 1))
					line = expandMacros(line);

				if (isPreprocessorOnlyMode) {
					std::cout << line << "\n";
				}

				append_line_with_tracking(line);
			}
		}

		return true;
	}

	void push_file_to_stack(const CurrentFile& current_file) {
		filestack_.emplace(current_file);
	}

	const std::string& get_result() const {
		return result_;
	}
	
	// Append a line to the result and record its source location
	void append_line_with_tracking(const std::string& line) {
		// Record the mapping before appending
		if (!filestack_.empty()) {
			const auto& current_file = filestack_.top();
			
			line_map_.push_back({
				current_file_index_,  // Current file
				static_cast<size_t>(current_file.line_number),  // Line in current file
				current_parent_line_  // Preprocessed line where current file was #included
			});
		}
		
		result_.append(line).append("\n");
		current_output_line_++;
	}
	
	// Get the index of a file path (must already exist)
	size_t get_file_path_index(std::string_view file_path) const {
		auto it = std::find(file_paths_.begin(), file_paths_.end(), file_path);
		if (it != file_paths_.end()) {
			return std::distance(file_paths_.begin(), it);
		}
		// Should never happen if readFile() properly registers files
		return 0;
	}
	
	// Add a file path if it doesn't already exist, return its index
	size_t get_or_add_file_path(std::string_view file_path) {
		auto it = std::find(file_paths_.begin(), file_paths_.end(), file_path);
		if (it != file_paths_.end()) {
			return std::distance(file_paths_.begin(), it);
		}
		size_t new_index = file_paths_.size();
		file_paths_.push_back(std::string(file_path));
		return new_index;
	}

private:
	// Separator bitset - 32 bytes (4 × uint64_t), one bit per character
	// Generated at compile-time by looping over separator_chars array
	static constexpr std::array<uint64_t, 4> separator_bitset = {
		build_separator_bitset_chunk(0),  // Chars 0-63
		build_separator_bitset_chunk(1),  // Chars 64-127
		build_separator_bitset_chunk(2),  // Chars 128-191
		build_separator_bitset_chunk(3)   // Chars 192-255
	};

	static constexpr bool is_seperator_character(char c) {
		unsigned char uc = static_cast<unsigned char>(c);
		return (separator_bitset[uc >> 6] >> (uc & 0x3F)) & 1;
	}
	// Helper function to check if a position is inside a string literal (static version)
	// Handles both regular strings "..." and raw strings R"(...)" or R"delim(...)delim"
	static bool is_inside_string_literal(const std::string& str, size_t pos) {
		bool inside_string = false;
		bool inside_raw_string = false;
		bool escaped = false;
		std::string_view raw_delimiter;

		for (size_t i = 0; i < pos && i < str.size(); ++i) {
			if (inside_raw_string) {
				// Inside a raw string - look for )" followed by delimiter and "
				if (str[i] == ')' && i + 1 + raw_delimiter.size() < str.size()) {
					if (str[i + 1] == '"') {
						// Check if delimiter matches (empty delimiter case)
						if (raw_delimiter.empty()) {
							inside_raw_string = false;
							i += 1; // Skip the closing "
							continue;
						}
					} else if (i + 1 + raw_delimiter.size() < str.size()) {
						// Check if delimiter matches
						bool delimiter_matches = true;
						for (size_t j = 0; j < raw_delimiter.size(); ++j) {
							if (str[i + 1 + j] != raw_delimiter[j]) {
								delimiter_matches = false;
								break;
							}
						}
						if (delimiter_matches && str[i + 1 + raw_delimiter.size()] == '"') {
							inside_raw_string = false;
							i += raw_delimiter.size() + 1; // Skip delimiter and closing "
							continue;
						}
					}
				}
			} else if (inside_string) {
				// Inside a regular string
				if (escaped) {
					escaped = false;
					continue;
				}

				if (str[i] == '\\') {
					escaped = true;
				} else if (str[i] == '"') {
					inside_string = false;
				}
			} else {
				// Outside any string - check for string start
				// Check for raw string literal: R"delim(
				if (str[i] == 'R' && i + 2 < str.size() && str[i + 1] == '"') {
					inside_raw_string = true;
					raw_delimiter = std::string_view{};
					i += 2; // Skip R"

					// Extract delimiter (characters between " and ()
					while (i < str.size() && str[i] != '(') {
						++i;
					}
					raw_delimiter = std::string_view(str.data() + 2, i - 2);
					// i now points to '(', continue from next character
					continue;
				}
				// Check for regular string literal
				else if (str[i] == '"') {
					inside_string = true;
				}
			}
		}

		return inside_string || inside_raw_string;
	}

	// Expand macros for #if/#elif expressions, preserving identifiers inside defined()
	std::string expandMacrosForConditional(const std::string& input) {
		if (settings_.isVerboseMode()) {
			std::cerr << "expandMacrosForConditional input: '" << input << "'" << std::endl;
		}
		
		std::string result;
		size_t pos = 0;
		
		while (pos < input.size()) {
			// Look for "defined" keyword
			size_t defined_pos = input.find("defined", pos);
			
			if (defined_pos == std::string::npos) {
				// No more "defined" keywords, expand the rest
				if (settings_.isVerboseMode() && pos < input.size()) {
					std::cerr << "  Expanding rest: '" << input.substr(pos) << "'" << std::endl;
				}
				result += expandMacros(input.substr(pos));
				break;
			}
			
			// Check if this is actually the "defined" keyword (not part of another identifier)
			bool is_keyword = true;
			if (defined_pos > 0) {
				char prev = input[defined_pos - 1];
				if (std::isalnum(prev) || prev == '_') {
					is_keyword = false;
				}
			}
			if (is_keyword && defined_pos + 7 < input.size()) {
				char next = input[defined_pos + 7];
				if (std::isalnum(next) || next == '_') {
					is_keyword = false;
				}
			}
			
			if (!is_keyword) {
				// Not actually "defined" keyword, just expand and continue
				result += expandMacros(input.substr(pos, defined_pos - pos + 1));
				pos = defined_pos + 1;
				continue;
			}
			
			// Expand everything before "defined"
			if (settings_.isVerboseMode() && defined_pos > pos) {
				std::cerr << "  Expanding before 'defined': '" << input.substr(pos, defined_pos - pos) << "'" << std::endl;
			}
			result += expandMacros(input.substr(pos, defined_pos - pos));
			result += "defined";  // Add the keyword itself
			
			// Skip past "defined"
			pos = defined_pos + 7; // length of "defined"
			
			// Skip whitespace
			while (pos < input.size() && std::isspace(input[pos])) {
				result += input[pos++];
			}
			
			// Check if there's a '('
			bool has_paren = (pos < input.size() && input[pos] == '(');
			if (has_paren) {
				result += "(";
				pos++;
				// Skip whitespace after '('
				while (pos < input.size() && std::isspace(input[pos])) {
					result += input[pos++];
				}
			}
			
			// Extract the identifier (don't expand it!)
			size_t ident_start = pos;
			while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) {
				pos++;
			}
			result += input.substr(ident_start, pos - ident_start);
			
			// Skip whitespace
			while (pos < input.size() && std::isspace(input[pos])) {
				result += input[pos++];
			}
			
			// If there was a '(', expect a ')'
			if (has_paren && pos < input.size() && input[pos] == ')') {
				result += ")";
				pos++;
			}
		}
		
		if (settings_.isVerboseMode()) {
			std::cerr << "expandMacrosForConditional output: '" << result << "'" << std::endl;
		}
		
		return result;
	}

	std::string expandMacros(const std::string& input, std::unordered_set<std::string> expanding_macros = {}) {
		std::string output = input;

		// Check if we're inside a multiline raw string from a previous line
		// If so, we need to check if this line ends the raw string
		if (inside_multiline_raw_string_) {
			// Scan this line to see if it contains the closing delimiter
			std::string closing = ")" + multiline_raw_delimiter_ + "\"";
			size_t close_pos = output.find(closing);
			if (close_pos != std::string::npos) {
				// Raw string ends on this line
				inside_multiline_raw_string_ = false;
				multiline_raw_delimiter_.clear();
			}
			// Don't expand macros on this line since we're inside a raw string
			return output;
		}

		// Check if this line starts a multiline raw string
		// Scan for R"delimiter( patterns
		size_t raw_start = 0;
		while ((raw_start = output.find("R\"", raw_start)) != std::string::npos) {
			size_t delim_start = raw_start + 2;
			size_t paren_pos = output.find('(', delim_start);
			if (paren_pos != std::string::npos) {
				std::string delimiter = output.substr(delim_start, paren_pos - delim_start);
				std::string closing = ")" + delimiter + "\"";
				size_t close_pos = output.find(closing, paren_pos);
				if (close_pos == std::string::npos) {
					// Raw string starts but doesn't end on this line
					inside_multiline_raw_string_ = true;
					multiline_raw_delimiter_ = delimiter;
					// Don't expand macros on this line
					return output;
				}
			}
			raw_start++;
		}

		//bool expanded = true;
		size_t loop_guard = 1000;
		size_t scan_pos = 0;  // Track where we are in the scan
		
		// Debug counter for tracking iterations
		size_t iteration_count = 0;
		size_t last_debug_pos = 0;
		
		while (scan_pos < output.size() && loop_guard-- > 0) {
			iteration_count++;
			
			// Debug: Print every 100 iterations or when scan_pos jumps significantly
			if (settings_.isVerboseMode() && (iteration_count % 100 == 0 || scan_pos - last_debug_pos > 1000)) {
				std::cerr << "Expansion iteration " << iteration_count << " at scan_pos " << scan_pos 
				          << " / " << output.size() << " (loop_guard=" << loop_guard << ")" << std::endl;
				std::cerr << "  Context: " << output.substr(scan_pos, std::min<size_t>(80, output.size() - scan_pos)) << std::endl;
				last_debug_pos = scan_pos;
			}
			
			//expanded = false;
			size_t last_char_index = output.size() - 1;
			
			// Try to find any macro starting from scan_pos
			size_t best_pos = std::string::npos;
			std::string best_pattern;
			const Directive* best_directive = nullptr;
			size_t best_pattern_end = 0;
			
			for (const auto& [pattern, directive] : defines_) {
				// Skip if this macro is currently being expanded (prevent recursion)
				if (expanding_macros.count(pattern) > 0) {
					continue;
				}
				
				// Search for the pattern starting from scan_pos
				size_t search_pos = scan_pos;
				size_t pos = std::string::npos;

				while ((search_pos = output.find(pattern, search_pos)) != std::string::npos) {
					// Skip if the pattern is inside a string literal
					if (is_inside_string_literal(output, search_pos)) {
						search_pos++;
						continue;
					}

					if (search_pos > 0 && !is_seperator_character(output[search_pos - 1])) {
						search_pos++;
						continue;
					}
					size_t pattern_end = search_pos + pattern.size();
					if (pattern_end < last_char_index && !is_seperator_character(output[pattern_end])) {
						search_pos++;
						continue;
					}

					// Found a valid occurrence
					pos = search_pos;
					break;
				}

				// Keep track of the earliest match
				if (pos != std::string::npos && (best_pos == std::string::npos || pos < best_pos)) {
					best_pos = pos;
					best_pattern = pattern;
					best_directive = &directive;
					best_pattern_end = pos + pattern.size();
				}
			}

			if (best_pos != std::string::npos) {
				size_t pattern_end = best_pattern_end;

				// Debug: Print when we find a macro to expand
				if (settings_.isVerboseMode()) {
					std::cerr << "Found macro '" << best_pattern << "' at position " << best_pos << std::endl;
					size_t context_start = (best_pos >= 20) ? (best_pos - 20) : 0;
					size_t context_len = std::min<size_t>(80, output.size() - context_start);
					std::cerr << "  Before: " << output.substr(context_start, context_len) << std::endl;
				}

				std::string replace_str;
				if (auto* defineDirective = best_directive->get_if<DefineDirective>()) {
					replace_str = defineDirective->body;

					size_t args_start = output.find_first_of('(', pattern_end);
					if (args_start != std::string::npos && output.find_first_not_of(' ', pattern_end) == args_start) {
						size_t args_end = findMatchingClosingParen(output, args_start);
						std::vector<std::string_view> args = splitArgs(std::string_view(output).substr(args_start + 1, args_end - args_start - 1));

						// NOTE: Arguments should NOT be pre-expanded here.
						// They will be expanded during the final recursive expansion with proper protection.
						// Pre-expanding them causes issues with recursive macro definitions like SAL macros.

						// Calculate variadic arguments
							std::string va_args_str;
							bool has_variadic_args = false;
							if (args.size() > defineDirective->args.size()) {
								has_variadic_args = true;
								for (size_t i = defineDirective->args.size(); i < args.size(); ++i) {
									va_args_str += args[i];
									if (i < args.size() - 1) {
										va_args_str += ", ";
									}
								}
							}

							// Handle __VA_OPT__ (C++20 feature)
							// __VA_OPT__(content) expands to content if __VA_ARGS__ is non-empty, otherwise empty
							size_t va_opt_pos = 0;
							while ((va_opt_pos = replace_str.find("__VA_OPT__", va_opt_pos)) != std::string::npos) {
								// Find the opening parenthesis
								size_t opt_paren_start = replace_str.find('(', va_opt_pos + 10);
								if (opt_paren_start != std::string::npos) {
									size_t opt_paren_end = findMatchingClosingParen(replace_str, opt_paren_start);
									if (opt_paren_end != std::string::npos) {
										std::string opt_content = replace_str.substr(opt_paren_start + 1, opt_paren_end - opt_paren_start - 1);
										// Replace __VA_OPT__(content) with content if variadic args exist, otherwise empty
										std::string replacement = has_variadic_args ? opt_content : "";
										replace_str.replace(va_opt_pos, opt_paren_end - va_opt_pos + 1, replacement);
										// Continue searching from the replacement position
										va_opt_pos += replacement.length();
									} else {
										break;
									}
								} else {
									break;
								}
							}

							// Handling the __VA_ARGS__ macro
							size_t va_args_pos = replace_str.find("__VA_ARGS__");
							if (va_args_pos != std::string::npos) {
								replace_str.replace(va_args_pos, 11, va_args_str);
							}

						if (!defineDirective->args.empty()) {
							if (args.size() < defineDirective->args.size()) {
								// Not enough arguments, skip this macro and move forward
								scan_pos = best_pos + 1;
								continue;
							}
							for (size_t i = 0; i < defineDirective->args.size(); ++i) {
									// Handle stringification operator (#) - but NOT token pasting (##)
									// We need to avoid matching # when it's part of ##
									size_t stringify_pos = 0;
									while ((stringify_pos = replace_str.find("#" + defineDirective->args[i], stringify_pos)) != std::string::npos) {
										// Check if this # is part of ##
										if (stringify_pos > 0 && replace_str[stringify_pos - 1] == '#') {
											// This is part of ##, skip it
											stringify_pos += 1;
											continue;
										}
										// Check if the next character after the argument is also #
										size_t arg_end = stringify_pos + 1 + defineDirective->args[i].length();
										if (arg_end < replace_str.length() && replace_str[arg_end] == '#') {
											// This is part of ##, skip it
											stringify_pos += 1;
											continue;
										}
										// This is a real stringification operator
										replace_str.replace(stringify_pos, 1 + defineDirective->args[i].length(), std::format("\"{0}\"", args[i]));
										stringify_pos += args[i].length() + 2; // Skip past the replaced string
									}
								// Replace macro arguments (non-stringified)
								replaceAll(replace_str, defineDirective->args[i], args[i]);
							}
						}

						pattern_end = args_end + 1;
					}
				}
				else if (auto* function_directive = best_directive->get_if<FunctionDirective>()) {
					replace_str = function_directive->getBody();
				}

				// Add this macro to the expanding set to prevent recursion
				auto new_expanding = expanding_macros;
				new_expanding.insert(best_pattern);
				
				// Recursively expand macros in the replacement text
				replace_str = expandMacros(replace_str, new_expanding);

				// Debug: Print replacement
				if (settings_.isVerboseMode()) {
					std::cerr << "  Replacement: " << replace_str.substr(0, std::min<size_t>(100, replace_str.size())) << std::endl;
				}

				output = output.replace(output.begin() + best_pos, output.begin() + pattern_end, replace_str);
				
				// Move scan position to after the replacement
				// This prevents rescanning the replacement we just made
				scan_pos = best_pos + replace_str.size();
				
				// Debug: Print new scan position
				if (settings_.isVerboseMode()) {
					std::cerr << "  After expansion, scan_pos moved to " << scan_pos;
					if (replace_str.size() >= best_pattern.size()) {
						std::cerr << " (jumped forward by " << (replace_str.size() - best_pattern.size()) << ")" << std::endl;
					} else {
						std::cerr << " (jumped backward by " << (best_pattern.size() - replace_str.size()) << ")" << std::endl;
					}
				}
				
				//expanded = true;
			} else {
				// No more macros found from scan_pos, move forward
				scan_pos++;
			}
		}

		if (loop_guard == 0) {
			std::cerr << "Warning: Macro expansion limit reached for line (possible infinite recursion): " << input.substr(0, 100) << std::endl;
		}

		// Handle token-pasting operator (##) after replacing all the arguments
		size_t paste_pos;
		while ((paste_pos = output.find("##")) != std::string::npos) {
			// Find whitespaces before ##
			size_t ws_before = paste_pos;
			while (ws_before > 0 && std::isspace(output[ws_before - 1])) {
				--ws_before;
			}

			// Find whitespaces after ##
			size_t ws_after = paste_pos + 2;
			ws_after = output.find_first_not_of(' ', ws_after);

			// Concatenate the string without whitespaces and ##
			output = output.substr(0, ws_before) + output.substr(ws_after);
		}

		return output;
	}

	void apply_operator(std::stack<long>& values, std::stack<Operator>& ops) {
		if (ops.empty() || values.size() < 1) {
			if (settings_.isVerboseMode()) {
				std::cerr << "apply_operator: ops.empty()=" << ops.empty() 
				          << " values.size()=" << values.size() << std::endl;
			}
			std::cerr << "Internal compiler error, values don't match the ops!" << std::endl;
			return;
		}

		Operator op = ops.top();
		if (settings_.isVerboseMode()) {
			std::cerr << "Applying operator (values.size=" << values.size() << ")" << std::endl;
		}
		
		if (op == Operator::OpenParen) {
			ops.pop();
			return;
		}

		// Unary operators
		if (op == Operator::Not) {
			auto value = values.top();
			values.pop();
			values.push(!value);
		}
		else if (op == Operator::BitwiseNot) {
			auto value = values.top();
			values.pop();
			values.push(~value);
		}
		else if (values.size() >= 2) {
			auto right = values.top();
			values.pop();
			auto left = values.top();
			values.pop();

			switch (op) {
			case Operator::And:
				values.push(left && right);
				break;
			case Operator::Or:
				values.push(left || right);
				break;
			case Operator::Less:
				values.push(left < right);
				break;
			case Operator::Greater:
				values.push(left > right);
				break;
			case Operator::Equals:
				values.push(left == right);
				break;
			case Operator::NotEquals:
				values.push(left != right);
				break;
			case Operator::LessEquals:
				values.push(left <= right);
				break;
			case Operator::GreaterEquals:
				values.push(left >= right);
				break;
			// Arithmetic operators
			case Operator::Add:
				values.push(left + right);
				break;
			case Operator::Subtract:
				values.push(left - right);
				break;
			case Operator::Multiply:
				values.push(left * right);
				break;
			case Operator::Divide:
				if (right != 0) {
					values.push(left / right);
				} else {
					std::cerr << "Warning: Division by zero in preprocessor expression" << std::endl;
					values.push(0);
				}
				break;
			case Operator::Modulo:
				if (right != 0) {
					values.push(left % right);
				} else {
					std::cerr << "Warning: Modulo by zero in preprocessor expression" << std::endl;
					values.push(0);
				}
				break;
			// Bitwise operators
			case Operator::LeftShift:
				values.push(left << right);
				break;
			case Operator::RightShift:
				values.push(left >> right);
				break;
			case Operator::BitwiseAnd:
				values.push(left & right);
				break;
			case Operator::BitwiseOr:
				values.push(left | right);
				break;
			case Operator::BitwiseXor:
				values.push(left ^ right);
				break;
			default:
				std::cerr << "Internal compiler error, unknown operator!" << std::endl;
				break;
			}
		}

		ops.pop();
	}

	long evaluate_expression(std::istringstream& iss) {
		if (settings_.isVerboseMode()) {
			// Save position and read entire expression for debug
			auto pos = iss.tellg();
			std::string debug_expr;
			std::getline(iss, debug_expr);
			iss.clear();
			iss.seekg(pos);
			std::cerr << "Evaluating expression: '" << debug_expr << "'" << std::endl;
		}
		
		// Check if expression is empty (all whitespace) - treat as 0
		auto start_pos = iss.tellg();
		iss >> std::ws;  // Skip whitespace
		if (iss.eof() || iss.peek() == EOF) {
			if (settings_.isVerboseMode()) {
				std::cerr << "  Empty expression, returning 0" << std::endl;
			}
			return 0;
		}
		iss.seekg(start_pos);  // Reset to start
		
		std::stack<long> values;
		std::stack<Operator> ops;

		std::string op_str;
		size_t eval_loop_guard = 10000;  // Add loop guard

		while (iss && eval_loop_guard-- > 0) {
			char c = iss.peek();
			if (isdigit(c)) {
				std::string str_value;
				iss >> str_value;
				long value = stol(str_value);
				values.push(value);
				if (settings_.isVerboseMode()) {
					std::cerr << "  Pushed value: " << value << " (values.size=" << values.size() << ")" << std::endl;
				}
			}
			else if (auto it = char_info_table.find(c); it != char_info_table.end()) {
				CharInfo info = it->second;
				op_str = iss.get(); // Consume the operator

				// Handle multi-character operators
				if (info.is_multi_char && (iss.peek() == '=' || (c != '!' && iss.peek() == c))) {
					op_str += iss.get();
				}

				const Operator op = string_to_operator(op_str);
				
				if (settings_.isVerboseMode()) {
					std::cerr << "  Found operator: '" << op_str << "' (values.size=" << values.size() << ", ops.size=" << ops.size() << ")" << std::endl;
				}

				if (c == '(') {
					ops.push(op);
				}
				else if (c == ')') {
					while (!ops.empty() && ops.top() != Operator::OpenParen) {
						apply_operator(values, ops);
					}
					if (!ops.empty() && ops.top() == Operator::OpenParen) {
						ops.pop(); // Remove the '(' from the stack
					}
				}
				else {
					while (!ops.empty() && op != Operator::Not && precedence_table[op] <= precedence_table[ops.top()]) {
						apply_operator(values, ops);
					}
					ops.push(op);
				}
			}
			else if (isalpha(c) || c == '_') {
				std::string keyword;
				iss >> keyword;
				if (keyword.find("__") == 0) {	// __ is reserved for the compiler
					if (keyword.find("__has_include") == 0) {
						long exists = 0;
						std::string_view include_name(keyword.data() + "__has_include(<"sv.length());
						include_name.remove_suffix(2); // Remove trailing >)
						for (const auto& include_dir : settings_.getIncludeDirs()) {
							std::string include_file(include_dir);
							include_file.append("/");
							include_file.append(include_name);
							if (std::filesystem::exists(include_file)) {
								exists = 1;
								break;
							}
						}
						values.push(exists);
					}
					else {
						// Unknown __ identifier (like __cpp_exceptions, __SANITIZE_THREAD__, etc.)
						// Treat as 0 (undefined) per C++ preprocessor rules
						if (settings_.isVerboseMode()) {
							std::cout << "Unknown __ identifier in #if directive: " << keyword << " (treating as 0)" << std::endl;
						}
						values.push(0);
					}
				}
				else if (keyword.find("defined") == 0) {
					std::string symbol;
					bool has_parenthesis = false;

					if (keyword == "defined") {
						if (iss.peek() == '(') {
							iss.ignore(); // Consume the '('
							has_parenthesis = true;
						}
						iss >> symbol;
					}
					else { // "defined(" is part of the keyword string
						has_parenthesis = true;
						if (keyword.size() > "defined("sv.length())
							symbol = keyword.substr(8);
						else
							iss >> symbol;
					}

					if (has_parenthesis) {
						// The symbol may have ')' at the end if it was read by >> operator
						// Remove ')' from the symbol, but don't call ignore() because >> already consumed it
						symbol.erase(std::remove(symbol.begin(), symbol.end(), ')'), symbol.end());
					}

					const bool value = defines_.count(symbol) > 0;
					values.push(value);
					if (settings_.isVerboseMode()) {
						std::cerr << "  Pushed defined() result: " << value << " (symbol='" << symbol << "', values.size=" << values.size() << ")" << std::endl;
						// Don't print stream state here anymore since it was misleading
					}
				}
				else if (auto it = defines_.find(keyword); it != defines_.end()) {
					// convert the value to an int
					const auto& body = it->second.getBody();
					if (body.size() == 1) {
						long value = stol(body);
						values.push(value);
					}
					else {
						if (settings_.isVerboseMode()) {
							std::cout << "Checking unknown keyword value in #if directive: " << keyword << std::endl;
						}
						values.push(0);
					}
				}
				else {
					if (settings_.isVerboseMode()) {
						std::cout << "Checking unknown keyword in #if directive: " << keyword << std::endl;
					}
					values.push(0);
				}
			}
			else {
				c = iss.get();
			}
		}

		while (!ops.empty()) {
			apply_operator(values, ops);
		}

		if (eval_loop_guard == 0) {
			std::cerr << "Error: Expression evaluation loop limit reached (possible infinite loop in #if)" << std::endl;
			return 0;
		}

		if (values.size() == 0) {
			std::cerr << "Internal compiler error, mismatched operator in file " << filestack_.top().file_name << ":" << filestack_.top().line_number;
			if (settings_.isVerboseMode()) {
				std::cerr << " - values stack is empty, ops.size()=" << ops.size() << std::endl;
			} else {
				std::cerr << std::endl;
			}
			return 0;
		}

		if (settings_.isVerboseMode()) {
			std::cerr << "Expression result: " << values.top() << " (values.size=" << values.size() << ", ops.size=" << ops.size() << ")" << std::endl;
		}

		return values.top();
	}

	bool processIncludeDirective(const std::string& line, const std::string_view& current_file, long include_line_number) {
		std::istringstream iss(line);
		std::string token;
		iss >> token;
		if (iss.eof() || token != "#include") {
			return true;
		}
		iss >> token;
		if (token.size() < 2 || (token.front() != '"' && token.front() != '<') || (token.front() == '"' && token.back() != '"') || (token.front() == '<' && token.back() != '>')) {
			return true;
		}
		std::string filename(token.substr(1, token.size() - 2));
		bool found = false;
		if (settings_.isVerboseMode()) {
			std::cerr << "Looking for include file: " << filename << std::endl;
		}
		for (const auto& include_dir : settings_.getIncludeDirs()) {
			std::filesystem::path include_path(include_dir);
			include_path /= filename;  // Use /= operator which handles path separators correctly
			std::string include_file = include_path.string();
			if (settings_.isVerboseMode()) {
				std::cerr << "  Checking path: " << include_file << " - exists: " << std::filesystem::exists(include_file) << std::endl;
			}
			// Check if the file exists before trying to read it
			// This distinguishes between "file not found" and "file found but had preprocessing error"
			if (std::filesystem::exists(include_file)) {
				// File exists, try to read and preprocess it
				if (settings_.isVerboseMode()) {
					std::cerr << "Found include file, attempting to read: " << include_file << std::endl;
				}
				if (!readFile(include_file, include_line_number)) {
					// Preprocessing failed (e.g., #error directive)
					// Return false to propagate the error up
					if (settings_.isVerboseMode()) {
						std::cerr << "readFile returned false for: " << include_file << std::endl;
					}
					return false;
				}
				tree_.addDependency(current_file, include_file);
				found = true;
				break;
			}
		}
		if (!found) {
			std::cerr << "Failed to include file: " << filename << std::endl;
			return false;
		}
		return true;
	}

	void processPragmaPack(std::string_view line) {
		// Parse #pragma pack directives
		// Supported formats:
		//   #pragma pack()           - reset to default (no packing)
		//   #pragma pack(n)          - set pack alignment to n (1, 2, 4, 8, 16)
		//   #pragma pack(push)       - push current alignment onto stack
		//   #pragma pack(push, n)    - push current alignment and set to n
		//   #pragma pack(pop)        - pop alignment from stack

		// Find the opening parenthesis
		size_t open_paren = line.find('(');
		if (open_paren == std::string::npos) {
			// No parenthesis - ignore (malformed pragma)
			return;
		}

		size_t close_paren = line.find(')', open_paren);
		if (close_paren == std::string::npos) {
			// No closing parenthesis - ignore (malformed pragma)
			return;
		}

		// Extract content between parentheses
		std::string_view content = line.substr(open_paren + 1, close_paren - open_paren - 1);

		// Trim whitespace
		auto trim_start = content.find_first_not_of(" \t"sv);
		auto trim_end = content.find_last_not_of(" \t"sv);
		if (trim_start != std::string_view::npos && trim_end != std::string_view::npos) {
			content = content.substr(trim_start, trim_end - trim_start + 1);
		} else {
			content = {};
		}

		// Handle empty parentheses: #pragma pack()
		if (content.empty()) {
			settings_.setPackAlignment(0);  // Reset to default (no packing)
			return;
		}

		// Check for push/pop
		if (content == "push"sv) {
			settings_.pushPackAlignment();
			return;
		}

		if (content == "pop"sv) {
			settings_.popPackAlignment();
			return;
		}

		// Check for "push, n" format
		size_t comma_pos = content.find(',');
		if (comma_pos != std::string_view::npos) {
			std::string_view first_part = content.substr(0, comma_pos);
			std::string_view second_part = content.substr(comma_pos + 1);

			// Trim both parts
			auto trim_first_start = first_part.find_first_not_of(" \t");
			auto trim_first_end = first_part.find_last_not_of(" \t");
			if (trim_first_start != std::string_view::npos && trim_first_end != std::string_view::npos) {
				first_part = first_part.substr(trim_first_start, trim_first_end - trim_first_start + 1);
			}

			auto trim_second_start = second_part.find_first_not_of(" \t");
			auto trim_second_end = second_part.find_last_not_of(" \t");
			if (trim_second_start != std::string_view::npos && trim_second_end != std::string_view::npos) {
				second_part = second_part.substr(trim_second_start, trim_second_end - trim_second_start + 1);
			}

			if (first_part == "push") {
				// Parse the alignment value
				try {
					size_t alignment = 0;
					std::from_chars(second_part.data(), second_part.data() + second_part.size(), alignment);
					// Validate alignment (must be 0, 1, 2, 4, 8, or 16)
					if (alignment == 0 || alignment == 1 || alignment == 2 ||
					    alignment == 4 || alignment == 8 || alignment == 16) {
						settings_.pushPackAlignment(alignment);
					}
					// Invalid alignment values are silently ignored (matches MSVC behavior)
				} catch (...) {
					// Invalid number - ignore
				}
			}
			return;
		}

		// Otherwise, try to parse as a single number: #pragma pack(n)
		try {
			size_t alignment = 0;
			std::from_chars(content.data(), content.data() + content.size(), alignment);
			// Validate alignment (must be 0, 1, 2, 4, 8, or 16)
			if (alignment == 0 || alignment == 1 || alignment == 2 ||
			    alignment == 4 || alignment == 8 || alignment == 16) {
				settings_.setPackAlignment(alignment);
			}
			// Invalid alignment values are silently ignored (matches MSVC behavior)
		} catch (...) {
			// Invalid number - ignore
		}
	}

	void processLineDirective(const std::string& line) {
		// #line directive format:
		// #line line_number
		// #line line_number "filename"
		std::istringstream iss(line);
		iss.seekg("#line"sv.length());

		long new_line_number;
		iss >> new_line_number;

		if (iss.fail()) {
			std::cerr << "Invalid #line directive: expected line number" << std::endl;
			return;
		}

		// Update the current line number (will be incremented on next line)
		if (!filestack_.empty()) {
			filestack_.top().line_number = new_line_number - 1;
		}

		// Check if there's a filename
		std::string filename;
		iss >> std::ws;  // Skip whitespace
		if (!iss.eof()) {
			std::getline(iss, filename);
			// Remove quotes if present
			if (filename.size() >= 2 && filename.front() == '"' && filename.back() == '"') {
				filename = filename.substr(1, filename.size() - 2);
			}
			// Update the filename
			if (!filestack_.empty()) {
				// We need to store the filename somewhere persistent
				// For now, we'll just update the file_name in the stack
				// Note: This is a bit tricky because file_name is a string_view
				// In a real implementation, we'd need to manage the lifetime properly
				// For now, we'll skip updating the filename to avoid lifetime issues
				// TODO: Properly handle filename updates in #line directives
			}
		}
	}

	void handleDefine(std::istringstream& iss) {
		DefineDirective define;

		// Parse the name
		std::string name;
		iss >> name;

		// Check for the presence of a macro argument list
		std::string rest_of_line;
		std::getline(iss.ignore(100, ' '), rest_of_line);
		size_t open_paren = name.find("(");

		if (open_paren != std::string::npos) {
			rest_of_line.insert(0, std::string_view(name.data() + open_paren));
			name.erase(open_paren);
		}

		if (!rest_of_line.empty()) {
			open_paren = rest_of_line.find("(");
			if (open_paren != std::string::npos && rest_of_line.find_first_not_of(' ') == open_paren) {
				size_t close_paren = rest_of_line.find(")", open_paren);

				if (close_paren == std::string::npos) {
					std::cerr << "Missing closing parenthesis in macro argument list for " << name << std::endl;
					return;
				}

				std::string arg_list = rest_of_line.substr(open_paren + 1, close_paren - open_paren - 1);

				// Tokenize the argument list
				std::istringstream arg_stream(arg_list);
				std::string token;
				bool found_variadic_args = false;
				while (std::getline(arg_stream, token, ',')) {
					// Remove leading and trailing whitespace
					auto start = std::find_if_not(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c); });
					auto end = std::find_if_not(token.rbegin(), token.rend(), [](unsigned char c) { return std::isspace(c); }).base();
					token = std::string(start, end);

					if (token == "..." && !found_variadic_args) {
						found_variadic_args = true;
					}
					else if (token == "..." && found_variadic_args) {
						std::cerr << "Duplicate variadic arguments '...' detected in macro argument list for " << name << std::endl;
						return;
					}
					else {
						define.args.push_back(std::move(token));
						token = std::string();	// it's undefined behavior to move a string and then use it again
					}
				}

				// Save the macro body after the closing parenthesis
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' ', close_paren + 1));
			}
			else {
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' '));
			}
		}

		define.body = std::move(rest_of_line);

		// Add the parsed define to the map
		defines_[name] = std::move(define);
	}

	void addBuiltinDefines() {
		// Add __cplusplus with the value corresponding to the C++ standard in use
		defines_["__cplusplus"] = DefineDirective{ "202002L", {} };  // C++20
		defines_["__STDC_HOSTED__"] = DefineDirective{ "1", {} };
		defines_["__STDCPP_THREADS__"] = DefineDirective{ "1", {} };
		defines_["_LIBCPP_LITTLE_ENDIAN"] = DefineDirective{};
		
		// MSVC C++ standard version feature flags (cumulative)
		defines_["_HAS_CXX17"] = DefineDirective{ "1", {} };  // C++17 features available
		defines_["_HAS_CXX20"] = DefineDirective{ "1", {} };  // C++20 features available
		defines_["_MSVC_LANG"] = DefineDirective{ "202002L", {} };  // MSVC language version (C++20)

		// FlashCpp compiler identification
		defines_["__FLASHCPP__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION_MAJOR__"] = DefineDirective{ "0", {} };
		defines_["__FLASHCPP_VERSION_MINOR__"] = DefineDirective{ "1", {} };
		defines_["__FLASHCPP_VERSION_PATCH__"] = DefineDirective{ "0", {} };

		// Windows platform macros
		defines_["_WIN32"] = DefineDirective{ "1", {} };
		defines_["_WIN64"] = DefineDirective{ "1", {} };
		defines_["_MSC_VER"] = DefineDirective{ "1944", {} };  // MSVC 2022 (match clang behavior)
		defines_["_MSC_FULL_VER"] = DefineDirective{ "194435217", {} };  // MSVC 2022 full version
		defines_["_MSC_BUILD"] = DefineDirective{ "1", {} };
		defines_["_MSC_EXTENSIONS"] = DefineDirective{ "1", {} };  // Enable MSVC extensions
		
		// MSVC STL macros
		defines_["_HAS_EXCEPTIONS"] = DefineDirective{ "1", {} };  // Exception handling enabled
		defines_["_CPPRTTI"] = DefineDirective{ "1", {} };  // RTTI enabled
		defines_["_NATIVE_WCHAR_T_DEFINED"] = DefineDirective{ "1", {} };  // wchar_t is native type
		defines_["_WCHAR_T_DEFINED"] = DefineDirective{ "1", {} };
		
		// Additional common MSVC macros
		defines_["_INTEGRAL_MAX_BITS"] = DefineDirective{ "64", {} };
		defines_["_MT"] = DefineDirective{ "1", {} };  // Multithreaded
		defines_["_DLL"] = DefineDirective{ "1", {} };  // Using DLL runtime

		// Architecture macros
		defines_["__x86_64__"] = DefineDirective{ "1", {} };
		defines_["__amd64__"] = DefineDirective{ "1", {} };
		defines_["__amd64"] = DefineDirective{ "1", {} };
		defines_["_M_X64"] = DefineDirective{ "100", {} };  // MSVC-style
		defines_["_M_AMD64"] = DefineDirective{ "100", {} };

		// Compiler builtin type macros - values depend on compiler mode
		// MSVC (default): Windows x64 types
		// GCC/Clang: Linux x64 types
		if (settings_.isMsvcMode()) {
			// MSVC x64 builtin types
			defines_["__SIZE_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__PTRDIFF_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__WCHAR_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__INTMAX_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINTMAX_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INTPTR_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINTPTR_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT_LEAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_LEAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_LEAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_LEAST64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT_LEAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_LEAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_LEAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_LEAST64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__INT_FAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_FAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_FAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_FAST64_TYPE__"] = DefineDirective{ "__int64", {} };
			defines_["__UINT_FAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_FAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_FAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_FAST64_TYPE__"] = DefineDirective{ "unsigned __int64", {} };
			defines_["__SIG_ATOMIC_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__CHAR16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__CHAR32_TYPE__"] = DefineDirective{ "unsigned int", {} };
		} else if (settings_.isGccMode()) {
			// GCC/Clang x64 builtin types (Linux/macOS)
			defines_["__SIZE_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__PTRDIFF_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__WCHAR_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INTMAX_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINTMAX_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__INTPTR_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINTPTR_TYPE__"] = DefineDirective{ "long unsigned int", {} };
			defines_["__INT8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__INT_LEAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_LEAST16_TYPE__"] = DefineDirective{ "short", {} };
			defines_["__INT_LEAST32_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__INT_LEAST64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT_LEAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_LEAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_LEAST32_TYPE__"] = DefineDirective{ "unsigned int", {} };
			defines_["__UINT_LEAST64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__INT_FAST8_TYPE__"] = DefineDirective{ "signed char", {} };
			defines_["__INT_FAST16_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__INT_FAST32_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__INT_FAST64_TYPE__"] = DefineDirective{ "long int", {} };
			defines_["__UINT_FAST8_TYPE__"] = DefineDirective{ "unsigned char", {} };
			defines_["__UINT_FAST16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__UINT_FAST32_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__UINT_FAST64_TYPE__"] = DefineDirective{ "unsigned long int", {} };
			defines_["__SIG_ATOMIC_TYPE__"] = DefineDirective{ "int", {} };
			defines_["__CHAR16_TYPE__"] = DefineDirective{ "unsigned short", {} };
			defines_["__CHAR32_TYPE__"] = DefineDirective{ "unsigned int", {} };
		}

		defines_["__FILE__"] = FunctionDirective{ [this]() -> std::string {
			// Use std::filesystem to normalize path separators for cross-platform compatibility
			// This converts backslashes to forward slashes on all platforms
			std::filesystem::path file_path(filestack_.top().file_name);
			std::string normalized_path = file_path.generic_string();
			return "\"" + normalized_path + "\"";
		} };
		defines_["__LINE__"] = FunctionDirective{ [this]() -> std::string {
			return std::to_string(filestack_.top().line_number);
		} };
		defines_["__COUNTER__"] = FunctionDirective{ [this]() -> std::string {
			return std::to_string(counter_value_++);
		} };

		defines_["__DATE__"] = FunctionDirective{ [] {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm_now = localtime_safely(&time_t_now);
			char buffer[12];
			std::strftime(buffer, sizeof(buffer), "\"%b %d %Y\"", &tm_now);
			return std::string(buffer);
		} };

		defines_["__TIME__"] = FunctionDirective{ [] {
			auto now = std::chrono::system_clock::now();
			auto time_t_now = std::chrono::system_clock::to_time_t(now);
			std::tm tm_now = localtime_safely(&time_t_now);
			char buffer[10];
			std::strftime(buffer, sizeof(buffer), "\"%H:%M:%S\"", &tm_now);
			return std::string(buffer);
		} };

		// __TIMESTAMP__ - file modification time
		defines_["__TIMESTAMP__"] = FunctionDirective{ [this]() -> std::string {
			if (!filestack_.empty()) {
				return filestack_.top().timestamp;
			}
			return "\"??? ??? ?? ??:??:?? ????\"";
		} };

		// __INCLUDE_LEVEL__ - nesting depth of includes (0 for main file)
		defines_["__INCLUDE_LEVEL__"] = FunctionDirective{ [this]() -> std::string {
			// Stack size - 1 because the main file is at level 0
			return std::to_string(filestack_.size() > 0 ? filestack_.size() - 1 : 0);
		} };

		// __FUNCTION__ (MSVC extension)
		defines_["__FUNCTION__"] = DefineDirective("__func__", {});

		// __PRETTY_FUNCTION__ (GCC extension) and __func__ (C++11 standard)
		// These are NOT preprocessor macros - they are compiler builtins handled by the parser
		// The parser will replace them with string literals containing the current function name
		// when they appear inside a function body
		//

		defines_["__STDCPP_DEFAULT_NEW_ALIGNMENT__"] = FunctionDirective{ [] {
			constexpr std::size_t default_new_alignment = alignof(std::max_align_t);
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%zuU", default_new_alignment);
			return std::string(buffer);
		} };
	}

	struct ScopedFileStack {
		ScopedFileStack(std::stack<CurrentFile>& filestack, std::string_view file, long included_at_line = 0) : filestack_(filestack) {
			// Get file modification timestamp
			std::string timestamp_str;
			try {
				auto ftime = std::filesystem::last_write_time(file);
				auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
					ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
				auto time_t_now = std::chrono::system_clock::to_time_t(sctp);
				std::tm tm_now = localtime_safely(&time_t_now);
				char buffer[32];
				std::strftime(buffer, sizeof(buffer), "\"%a %b %d %H:%M:%S %Y\"", &tm_now);
				timestamp_str = buffer;
			} catch (...) {
				// If we can't get the timestamp, use a default
				timestamp_str = "\"??? ??? ?? ??:??:?? ????\"";
			}
			filestack_.push({ file, 0, timestamp_str, included_at_line });
		}
		~ScopedFileStack() {
			filestack_.pop();
		}

		std::stack<CurrentFile>& filestack_;
	};

	CompileContext& settings_;
	FileTree& tree_;
	std::unordered_map<std::string, Directive> defines_;
	std::unordered_set<std::string> proccessedHeaders_;
	std::stack<CurrentFile> filestack_;
	std::string result_;
	std::vector<std::string> file_paths_;  // Unique list of source file paths
	std::vector<SourceLineMapping> line_map_;  // Maps preprocessed lines to source locations
	size_t current_output_line_ = 1;  // Track current line number in preprocessed output
	size_t current_file_index_ = 0;  // Track current file index (updated when switching files)
	size_t current_parent_line_ = 0;  // Track the preprocessed line where current file was #included (0 for main)
	unsigned long long counter_value_ = 0;

	// State for tracking multiline raw string literals
	bool inside_multiline_raw_string_ = false;
	std::string multiline_raw_delimiter_;
};
