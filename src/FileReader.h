#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>

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
    FileReader(const CompileContext& settings, FileTree& tree) : settings_(settings), tree_(tree) {}

	bool readFile(std::string_view file) {
		std::cout << "readFile " << file << std::endl;
		
		if (proccessedHeaders_.find(std::string(file.begin(), file.end()))
			return true;
		
		std::ifstream stream(file.data());
		if (!stream.is_open()) {
			std::cerr << "Failed to open file: " << file << std::endl;
			return false;
		}

		tree_.addFile(file);

		std::string line;
		bool skipping = false;
		bool in_comment = false;
		while (std::getline(stream, line)) {
			if (line.size() == 0) {
				continue;
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

			if (skipping) {
				if (line.find("#endif", 0) == 0) {
					skipping = false;
				}
				continue;
			}

			size_t comment_pos = line.find("//");
			if (comment_pos != std::string::npos) {
				continue;
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
				std::string symbol(line.substr(6));
				if (defines_.count(symbol)) {
					skipping = false;
				} else {
					skipping = true;
				}
			}
			else if (line.find("#ifndef", 0) == 0) {
				std::string symbol(line.substr(7));
				if (defines_.count(symbol)) {
					skipping = true;
				} else {
					skipping = false;
				}
			}
			else if (line.find("#else", 0) == 0) {
				skipping = !skipping;
			}
			else if (line.find("#endif", 0) == 0) {
				skipping = false;
			}
			else if (line.find("#pragma once", 0) == 0) {
				proccessedHeaders_.insert(std::string(file.begin(), file.end()));
			}
			else {
				// Handle other directives
			}
		}

		return true;
	}

private:
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

    const CompileContext& settings_;
    FileTree& tree_;
    std::unordered_map<std::string, DefineDirective> defines_;
	std::unordered_set<std::string> proccessedHeaders_;
};
