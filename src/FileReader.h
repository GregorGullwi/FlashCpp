#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <stack>

#include "CompileContext.h"
#include "FileTree.h"

#pragma once

struct DefineDirective {
	std::string name;
	std::vector<std::string> args;
	std::vector<std::string> body;
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

		std::string line;
		bool in_comment = false;
		std::stack<bool> skipping_stack;
		skipping_stack.push(false); // Initial state: not skipping

		long& line_number = filestack_.top().line_number;
		while (std::getline(stream, line)) {
			++line_number;
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
			
			if (line.size() == 0) {
				continue;
			}
			
			if (skipping_stack.size() == 0) {
				std::cerr << "Internal compiler error in file " << file << ":" << line_number << std::endl;
				return false;
			}
			const bool skipping = skipping_stack.top();
			
			// Find the position of the '#' character
			size_t directive_pos = line.find('#');
			if (directive_pos != std::string::npos) {
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
				line.replace(filePos, 8, "\"" + std::string(file) + "\"");
			}
			if (line.find("__LINE__") != std::string::npos) {
				size_t line_num = std::count(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>(), '\n') + 1;
					line.replace(line.find("__LINE__"), 7, std::to_string(line_num));
			}

			if (line.find("#include", 0) == 0) {
				if (!processIncludeDirective(line, file)) {
					return false;
				}
			}
			else if (line.find("#define", 0) == 0) {
				std::istringstream iss(line.substr(7)); // Skip the "#define"
				handleDefine(iss);
			}
			else if (line.find("#ifdef", 0) == 0) {
				std::istringstream iss(line.substr(6));
				std::string symbol;
				iss >> symbol;
				if (defines_.count(symbol)) {
					skipping_stack.push(false);
				} else {
					skipping_stack.push(true);
				}
			}
			else if (line.find("#ifndef", 0) == 0) {
				std::istringstream iss(line.substr(7));
				std::string symbol;
				iss >> symbol;
				if (defines_.count(symbol)) {
					skipping_stack.push(true);
				} else {
					skipping_stack.push(false);
				}
			}
			else if (line.find("#if", 0) == 0) {
				std::istringstream iss(line.substr(3));
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
			else if (line.find("#pragma once", 0) == 0) {
				proccessedHeaders_.insert(std::string(file));
			}
			else {
				// Handle other directives
			}
		}

		return true;
	}

private:
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
				if (keyword.find("defined(") == 0) {
					keyword = keyword.substr(8);
					keyword.erase(keyword.end()-1);
					const bool value = defines_.count(keyword) > 0;
					values.push(value);
				} else if (auto it = defines_.find(keyword); it != defines_.end()) {
					// convert the value to an int
					const auto& body = it->second.body;
					if (body.size() == 1) {
						long value = stol(body[0]);
						values.push(value);
					} else {
						if (settings_.isVerboseMode()) {
							std::cout << "Cheking unknown keyword value in #if directive: " << keyword << std::endl;
						}
						values.push(0);
					}
				} else {
					if (settings_.isVerboseMode()) {
						std::cout << "Cheking unknown keyword in #if directive: " << keyword << std::endl;
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
		std::string name;
		iss >> name;
		if (name.empty()) {
			return;
		}

		if (settings_.isVerboseMode()) {
			std::cout << "Adding #define " << name << std::endl;
		}

		DefineDirective directive;
		directive.name = name;

		if (iss.peek() == '(') {
			std::string arg;
			while (std::getline(iss, arg, ',')) {
				iss.get(); // Consume the comma or opening parenthesis
				directive.args.push_back(arg);
				if (iss.peek() == ')') {
					iss.get(); // Consume the closing parenthesis
					break;
				}
			}
		}

		std::string line;
		while (std::getline(iss, line)) {
			if (line.rfind("\\", 0) == 0) {
				directive.body.push_back(line.substr(0, line.size() - 1));
			}
			else {
				directive.body.push_back(line);
				break;
			}
		}

		defines_[directive.name] = directive;
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
};
