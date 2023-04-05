#include <fstream>
#include <string>
#include <vector>
#include <sstream>

#include "CompileContext.h"
#include "FileTree.h"

#pragma once

struct DefineDirective {
	std::string_view name;
	std::vector<std::string_view> args;
	std::vector<std::string> body;
};

class FileReader {
public:
    FileReader(const CompileContext& settings, FileTree& tree) : settings_(settings), tree_(tree) {}

    bool readFile(std::string_view file) {
        std::ifstream stream(file.data());
        if (!stream.is_open()) {
            std::cerr << "Failed to open file: " << file << std::endl;
            return false;
        }

        tree_.addFile(file);

        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("#include", 0) == 0) {
                std::istringstream iss(line);
                std::string token;
                iss >> token;
                if (iss.eof() || token != "#include") {
                    continue;
                }
                iss >> token;
                if (token.size() < 2 || (token.front() != '"' && token.front() != '<') || (token.front() == '"' && token.back() != '"') || (token.front() == '<' && token.back() != '>')) {
                    continue;
                }
                std::string filename(token.substr(1, token.size() - 2));
                bool found = false;
                for (const auto& include_dir : settings_.getIncludeDirs()) {
                    std::string include_file(include_dir);
                    include_file.append("/");
                    include_file.append(filename);
                    if (readFile(include_file)) {
                        tree_.addDependency(file, include_file);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::cerr << "Failed to include file: " << filename << std::endl;
                    return false;
                }
			}
			else if (line.find("#define", 0) == 0) {
				std::istringstream iss(line.substr(7)); // Skip the "#define"
				handleDefine(iss);
			}
        }

        //settings_.addDependency(file);

        return true;
    }

private:
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
    std::map<std::string_view, DefineDirective> defines_;
};
