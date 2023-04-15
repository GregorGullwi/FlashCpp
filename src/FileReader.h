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

#include "CompileContext.h"
#include "FileTree.h"

using namespace std::string_view_literals;

struct DefineDirective {
	std::string name;
	std::vector<std::string> args;
	std::string body;
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
};

static std::unordered_map<Operator, int> precedence_table = {
	{Operator::And, 1},
	{Operator::Or, 1},
	{Operator::Greater, 2},
	{Operator::Less, 2},
	{Operator::Equals, 2},
	{Operator::NotEquals, 2},
	{Operator::LessEquals, 2},
	{Operator::GreaterEquals, 2},
	{Operator::Not, 3},
};

static Operator string_to_operator(const std::string& op) {
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
	throw std::invalid_argument("Invalid operator: " + op);
}

struct CharInfo {
	Operator op;
	bool is_multi_char;
};

static std::unordered_map<char, CharInfo> char_info_table = {
	{'(', {Operator::OpenParen, false}},
	{')', {Operator::OpenParen, false}}, // Placeholder, will be handled in code
	{'!', {Operator::Not, true}},
	{'&', {Operator::And, true}},
	{'|', {Operator::Or, true}},
	{'>', {Operator::Greater, true}},
	{'<', {Operator::Less, true}},
	{'=', {Operator::Equals, true}},
};

static size_t findMatchingClosingParen(const std::string& str, size_t opening_pos) {
	int nesting = 1;
	size_t pos = opening_pos + 1;
	while (pos < str.size() && nesting > 0) {
		if (str[pos] == '(') {
			nesting++;
		}
		else if (str[pos] == ')') {
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

static std::vector<std::string> splitArgs(const std::string& argsStr) {
	std::vector<std::string> args;
	const std::size_t second_arg_start = argsStr.find(',');
	if (second_arg_start == std::string::npos) {
		args.push_back(argsStr);
		return args;
	}

	args.push_back(argsStr.substr(0, second_arg_start));
	std::string arg;
	size_t i = argsStr.find_first_not_of(' ', second_arg_start + 1);
	const size_t argsSize = argsStr.size();
	while (i < argsSize) {
		char c = argsStr[i];
		if (c == ',' && arg.size() > 0) {
			args.push_back(arg);
			arg.clear();
			i = argsStr.find_first_not_of(' ', i + 1);
		}
		else if (c == '(') {
			size_t closingPos = findMatchingClosingParen(argsStr, i);
			if (closingPos == std::string::npos) {
				throw std::runtime_error("Unmatched opening parenthesis in macro argument list");
			}
			arg += argsStr.substr(i + 1, closingPos - i - 1);
			i = closingPos + 1;
		}
		else if (arg.size() == 0 && (c == ' ' || c == '\t')) {
			i++;
		}
		else if (c == ')') {
			break;
		}
		else {
			arg += c;
			i++;
		}
	}
	if (arg.size() > 0) {
		args.push_back(arg);
	}
	return args;
}

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
	size_t pos = 0;
	while ((pos = str.find(from, pos)) != std::string::npos) {
		str.replace(pos, from.length(), to);
		pos += to.length();
	}
}

class FileReader {
public:
    FileReader(const CompileContext& settings, FileTree& tree) : settings_(settings), tree_(tree) {
		addBuiltinDefines();
	}
	
	size_t find_first_non_whitespace_after_hash(const std::string& str) {
		size_t pos = str.find('#');
		if (pos == std::string::npos) {
			return pos;
		}
		return str.find_first_not_of(" \t", pos + 1);
	}

	bool readFile(std::string_view file) {
		if (proccessedHeaders_.find(std::string(file)) != proccessedHeaders_.end())
			return true;

		if (settings_.isVerboseMode()) {
			std::cout << "readFile " << file << std::endl;
		}

		ScopedFileStack filestack(filestack_, file);

		std::ifstream stream(file.data());
		if (!stream.is_open()) {
			return false;
		}

		tree_.addFile(file);

		stream.seekg(0, std::ios::end);
		std::streampos file_size = stream.tellg();
		stream.seekg(0, std::ios::beg);
		std::string file_content(file_size, '\0');
		stream.read(file_content.data(), file_size);

		return processFileContent(file_content);
	}

	bool processFileContent(const std::string& file_content) {
		std::istringstream stream(file_content);
		std::string line;
		bool in_comment = false;
		std::stack<bool> skipping_stack;
		skipping_stack.push(false); // Initial state: not skipping

		long line_number_fallback = 1;
		long& line_number = !filestack_.empty() ? filestack_.top().line_number : line_number_fallback;
		long prev_line_number = -1;
		const bool isPreprocessorOnlyMode = settings_.isPreprocessorOnlyMode();
		while (std::getline(stream, line)) {
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
				} else {
					continue;
				}
			}
			
			size_t start_comment_pos = line.find("/*");
			if (start_comment_pos != std::string::npos) {
				size_t end_comment_pos = line.find("*/", start_comment_pos);
				if (end_comment_pos != std::string::npos) {
					line.erase(start_comment_pos, end_comment_pos - start_comment_pos + 2);
				} else {
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
				} else if (line.find("#if", 0) == 0) {
					// Nesting, push a new skipping state
					skipping_stack.push(true);
				} else if (line.find("#else", 0) == 0) {
					skipping_stack.top() = !skipping_stack.top();
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
			
			if (size_t filePos = line.find("__FILE__"); filePos != std::string::npos) {
				line.replace(filePos, 8, "\"" + std::string(filestack_.top().file_name) + "\"");
			}
			if (line.find("__LINE__") != std::string::npos) {
				size_t line_num = std::count(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>(), '\n') + 1;
					line.replace(line.find("__LINE__"), 7, std::to_string(line_num));
			}

			if (line.find("#include", 0) == 0) {
				if (!processIncludeDirective(line, filestack_.top().file_name)) {
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
				if (defines_.count(symbol)) {
					skipping_stack.push(false);
				} else {
					skipping_stack.push(true);
				}
			}
			else if (line.find("#ifndef", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#ifndef"sv.length());
				std::string symbol;
				iss >> symbol;
				if (defines_.count(symbol)) {
					skipping_stack.push(true);
				} else {
					skipping_stack.push(false);
				}
			}
			else if (line.find("#if", 0) == 0) {
				std::istringstream iss(line);
				iss.seekg("#if"sv.length());
				long expression_result = evaluate_expression(iss);
				skipping_stack.push(expression_result == 0);
			}
			else if (line.find("#else", 0) == 0) {
				skipping_stack.top() = !skipping_stack.top();
			}
			else if (line.find("#endif", 0) == 0) {
				if (!skipping_stack.empty()) {
					skipping_stack.pop();
				} else {
					std::cerr << "Unmatched #endif directive" << std::endl;
					return false;
				}
			}
			else if (line.find("#undef") == 0) {
				std::istringstream iss(line.substr(7));
				std::string symbol;
				iss >> symbol;
				defines_.erase(symbol);
			}
			else if (line.find("#pragma once", 0) == 0) {
				proccessedHeaders_.insert(std::string(filestack_.top().file_name));
			}
			else {
				if (line.size() > 0 && (line.find_last_of(' ') != line.size() - 1))
					line = expandMacros(line);

				std::cout << line << "\n";

				result_.append(line).append("\n");
			}
		}
		
		return true;
	}

	const std::string& get_result() const {
		return result_;
	}

private:
	static bool is_seperator_character(char c) {
		return ((c == ' ') | (c == ',') | (c == '#') | (c == ')') | (c == '(')) != 0;
	}
	std::string expandMacros(const std::string& input) const {
		std::string output = input;
		bool expanded = true;
		size_t last_expanded_pos = 0;
		while (expanded) {
			expanded = false;
			size_t last_char_index = output.size() - 1;
			for (const auto& [pattern, directive] : defines_) {
				size_t pos = output.find(pattern, last_expanded_pos);
				if (pos != std::string::npos) {
					if (pos > 0 && !is_seperator_character(output[pos - 1])) {
						continue;
					}
					size_t pattern_end = pos + pattern.size();
					if (pattern_end < last_char_index && !is_seperator_character(output[pattern_end])) {
						continue;
					}
					std::string replace_str = directive.body;
					if (!directive.args.empty()) {
						size_t args_start = output.find_first_of('(', pos);
						size_t args_end = findMatchingClosingParen(output, args_start);
						std::vector<std::string> args = splitArgs(output.substr(args_start + 1, args_end - args_start - 1));
						if (args.size() != directive.args.size()) {
							continue;
						}
						for (size_t i = 0; i < args.size(); ++i) {
							// Handle string concatenation macro token (#)
							replaceAll(replace_str, "#" + directive.args[i], "\"" + args[i] + "\"");
							// Replace macro arguments
							replaceAll(replace_str, directive.args[i], args[i]);

						}
						// Handle token-pasting operator (##) after replacing all the arguments
						size_t paste_pos;
						while ((paste_pos = replace_str.find("##")) != std::string::npos) {
							// Find whitespaces before ##
							size_t ws_before = paste_pos;
							while (ws_before > 0 && std::isspace(replace_str[ws_before - 1])) {
								--ws_before;
							}

							// Find whitespaces after ##
							size_t ws_after = paste_pos + 2;
							ws_after = replace_str.find_first_not_of(' ', ws_after);

							// Concatenate the string without whitespaces and ##
							replace_str = replace_str.substr(0, ws_before) + replace_str.substr(ws_after);
						}

						pattern_end = args_end + 1;
					}

					output = output.replace(output.begin() + pos, output.begin() + pattern_end, replace_str);
					last_char_index = output.size() - 1;
					expanded = true;
					last_expanded_pos = pos;
					//break;
				}
			}
		}
		return output;
	}

	void apply_operator(std::stack<long>& values, std::stack<Operator>& ops) {
		if (ops.empty() || values.size() < 1) {
			std::cerr << "Internal compiler error, values don't match the ops!" << std::endl;
			return;
		}

		Operator op = ops.top();
		if (op == Operator::OpenParen) {
			ops.pop();
			return;
		}

		if (op == Operator::Not) {
			auto value = values.top();
			values.pop();
			values.push(!value);
		} else if (values.size() >= 2) {
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
				default:
					std::cerr << "Internal compiler error, unknown operator!" << std::endl;
					break;
			}
		}

		ops.pop();
	}

	long evaluate_expression(std::istringstream& iss) {
		std::stack<long> values;
		std::stack<Operator> ops;

		std::string op_str;

		while (iss) {
			char c = iss.peek();
			if (isdigit(c)) {
				std::string str_value;
				iss >> str_value;
				long value = stol(str_value);
				values.push(value);
			}
			else if (char_info_table.count(c)) {
				CharInfo info = char_info_table[c];
				op_str = iss.get(); // Consume the operator

				// Handle multi-character operators
				if (info.is_multi_char && (iss.peek() == '=' || (c != '!' && iss.peek() == c))) {
					op_str += iss.get();
				}

				const Operator op = string_to_operator(op_str);
				
				if (c == '(') {
					ops.push(op);
				} else if (c == ')') {
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
			} else if (isalpha(c) || c == '_') {
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
				} else if (keyword.find("defined") == 0) {
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
						iss.ignore(std::numeric_limits<std::streamsize>::max(), ')'); // Ignore characters until the ')'
						symbol.erase(std::remove(symbol.begin(), symbol.end(), ')'), symbol.end());
					}

					const bool value = defines_.count(symbol) > 0;
					values.push(value);
				} else if (auto it = defines_.find(keyword); it != defines_.end()) {
					// convert the value to an int
					const auto& body = it->second.body;
					if (body.size() == 1) {
						long value = stol(body);
						values.push(value);
					} else {
						if (settings_.isVerboseMode()) {
							std::cout << "Checking unknown keyword value in #if directive: " << keyword << std::endl;
						}
						values.push(0);
					}
				} else {
					if (settings_.isVerboseMode()) {
						std::cout << "Checking unknown keyword in #if directive: " << keyword << std::endl;
					}
					values.push(0);
				}
			} else {
				c = iss.get();
			}
		}

		while (!ops.empty()) {
			apply_operator(values, ops);
		}
		
		if (values.size() == 0) {
			std::cerr << "Internal compiler error, mismatched operator in file " << filestack_.top().file_name << ":" << filestack_.top().line_number;
			return 0;
		}

		return values.top();
	}

	bool processIncludeDirective(const std::string& line, const std::string_view& current_file) {
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
		for (const auto& include_dir : settings_.getIncludeDirs()) {
			std::string include_file(include_dir);
			include_file.append("/");
			include_file.append(filename);
			if (readFile(include_file)) {
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
	
	void handleDefine(std::istringstream& iss) {
		DefineDirective define;

		// Parse the name
		iss >> define.name;

		// Check for the presence of a macro argument list
		std::string rest_of_line;
		iss.ignore(100, ' ');
		std::getline(iss, rest_of_line);
		size_t open_paren = define.name.find("(");

		if (open_paren != std::string::npos) {
			rest_of_line = define.name.substr(open_paren) + rest_of_line;
			define.name = define.name.substr(0, open_paren);
		}

		if (!rest_of_line.empty()) {
			open_paren = rest_of_line.find("(");
			if (open_paren != std::string::npos && rest_of_line.find_first_not_of(' ') == open_paren) {
				size_t close_paren = rest_of_line.find(")", open_paren);

				if (close_paren == std::string::npos) {
					std::cerr << "Missing closing parenthesis in macro argument list for " << define.name << std::endl;
					return;
				}

				std::string arg_list = rest_of_line.substr(open_paren + 1, close_paren - open_paren - 1);

				// Tokenize the argument list
				std::istringstream arg_stream(arg_list);
				std::string token;
				while (std::getline(arg_stream, token, ',')) {
					// Remove leading and trailing whitespace
					auto start = std::find_if_not(token.begin(), token.end(), [](unsigned char c) { return std::isspace(c); });
					auto end = std::find_if_not(token.rbegin(), token.rend(), [](unsigned char c) { return std::isspace(c); }).base();

					define.args.push_back(std::string(start, end));
				}

				// Save the macro body after the closing parenthesis
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' ', close_paren + 1));
			} else {
				rest_of_line.erase(0, rest_of_line.find_first_not_of(' '));
			}
		}

		define.body = std::move(rest_of_line);

		// Add the parsed define to the map
		defines_[define.name] = define;
	}


	
	void addBuiltinDefines() {
		// Add __cplusplus with the value corresponding to the C++ standard in use
		defines_["__cplusplus"] = { "__cplusplus", {}, { "201703L" } };
		defines_["_LIBCPP_LITTLE_ENDIAN"] = { "_LIBCPP_LITTLE_ENDIAN" };
	}
	
	struct CurrentFile {
		std::string_view file_name;
		long line_number = 0;
	};
						  
	struct ScopedFileStack {
		ScopedFileStack(std::stack<CurrentFile>& filestack, std::string_view file) : filestack_(filestack) {
			filestack_.push({ file });
		}
		~ScopedFileStack() {
			filestack_.pop();
		}
		
		std::stack<CurrentFile>& filestack_;
	};

    const CompileContext& settings_;
    FileTree& tree_;
    std::unordered_map<std::string, DefineDirective> defines_;
	std::unordered_set<std::string> proccessedHeaders_;
	std::stack<CurrentFile> filestack_;
	std::string result_;
};
