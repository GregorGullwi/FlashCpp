#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <ctime>
#include <iomanip>

#include "CompileContext.h"
#include "FileTree.h"

#pragma once

struct DefineDirective {
	std::string name;
	std::vector<std::string> args;
	std::vector<std::string> body;
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
		
		std::cout << "readFile " << file << std::endl;
		
		std::ifstream stream(file.data());
		if (!stream.is_open()) {
			std::cerr << "Failed to open file: " << file << std::endl;
			return false;
		}

		tree_.addFile(file);

		std::string line;
		bool in_comment = false;
		std::stack<bool> skipping_stack;
		skipping_stack.push(false); // Initial state: not skipping

		while (std::getline(stream, line)) {
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
			}

			if (skipping) {
				if (line.find("#endif", 0) == 0) {
					skipping_stack.pop();
				} else if (line.find("#ifdef", 0) == 0 || line.find("#ifndef", 0) == 0) {
					// Nesting, push a new skipping state
					skipping_stack.push(true);
				}
				continue;
			}

			size_t comment_pos = line.find("//");
			if (comment_pos != std::string::npos) {
				continue;
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
				int expression_result = !evaluate_expression(iss);
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
	void apply_operator(std::stack<int>& values, std::stack<char>& ops) {
		if (ops.empty() || values.size() < 1) {
			return;
		}

		char op = ops.top();
		ops.pop();

		if (op == '!') {
			int value = values.top();
			values.pop();
			values.push(!value);
		} else if (values.size() >= 2) {
			int right = values.top();
			values.pop();
			int left = values.top();
			values.pop();

			if (op == '&') {
				values.push(left && right);
			} else if (op == '|') {
				values.push(left || right);
			} else if (op == '<') {
				values.push(left < right);
			} else if (op == '>') {
				values.push(left > right);
			} else if (op == '=') {
				values.push(left == right);
			} else if (op == '!') {
				values.push(left != right);
			} else if (op == '<' && ops.top() == '=') {
				ops.pop();
				values.push(left <= right);
			} else if (op == '>' && ops.top() == '=') {
				ops.pop();
				values.push(left >= right);
			}
		}
	}

	int evaluate_expression(std::istringstream& iss) {
		std::stack<int> values;
		std::stack<char> ops;
		
		auto precedence = [](char op) {
			if (op == '&') return 1;
			if (op == '|') return 0;
			if (op == '>' || op == '<' || op == '=' || op == '!') return 2;
			return -1;
		};

		while (iss) {
			char c = iss.peek();
			if (isdigit(c)) {
				int value;
				iss >> value;
				values.push(value);
			}
			else if (c == '(' || c == ')' || c == '!' || c == '&' || c == '|' || c == '>' || c == '<' || c == '=' || c == '!') {
				iss.get(); // Consume the operator
				if (c == ')') {
					while (!ops.empty() && ops.top() != '(') {
						apply_operator(values, ops);
					}
					if (!ops.empty() && ops.top() == '(') {
						ops.pop(); // Remove the '(' from the stack
					}
				}
				else {
					while (!ops.empty() && precedence(c) <= precedence(ops.top())) {
						apply_operator(values, ops);
					}
					ops.push(c);
				}
			} else if (isalpha(c)) {
				std::string keyword;
				iss >> keyword;
				bool negate = false;
				if (keyword.find("defined(") == 0) {
					keyword = keyword.substr(8);
					keyword.erase(keyword.end()-1);
					const bool value = defines_.count(keyword) > 0;
					values.push(negate ? !value : value);
				} else {
					std::cerr << "Unknown keyword in #if directive: !" << keyword << std::endl;
					return 0;
				}
			} else {
				c = iss.get();
			}
		}

		while (!ops.empty()) {
			apply_operator(values, ops);
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

		std::cout << "Adding #define " << name << std::endl;

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
		// For example, C++17
		defines_["__cplusplus"] = { "__cplusplus", {}, { "201703L" } };
	}

    const CompileContext& settings_;
    FileTree& tree_;
    std::unordered_map<std::string, DefineDirective> defines_;
	std::unordered_set<std::string> proccessedHeaders_;
};
