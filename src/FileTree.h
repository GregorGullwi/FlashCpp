#pragma once

#include <string_view>
#include <unordered_set>
#include <unordered_map>

class FileTree {
public:
    const std::unordered_set<std::string_view>& getFiles() const {
        return files_;
    }

    void addFile(std::string_view file) {
        files_.insert(file);
    }

    const std::unordered_set<std::string_view>& getDependencies(std::string_view file) const {
        return dependencies_.at(file);
    }

    void addDependency(std::string_view file, std::string_view dependency) {
        dependencies_[file].insert(dependency);
    }

    FileTree& reset() {
        files_.clear();
        dependencies_.clear();
        return *this;
    }

private:
    std::unordered_set<std::string_view> files_;
    std::unordered_map<std::string_view, std::unordered_set<std::string_view>> dependencies_;
};