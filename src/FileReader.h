#include <fstream>
#include <string>
#include <vector>
#include <sstream>

#include "CompileContext.h"
#include "FileTree.h"

#pragma once

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
            if (line.rfind("#include", 0) == 0) {
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
                std::string_view filename(token.substr(1, token.size() - 2));
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
        }

        //settings_.addDependency(file);

        return true;
    }

private:
    const CompileContext& settings_;
    FileTree& tree_;
};
