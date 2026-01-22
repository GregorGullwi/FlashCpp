#pragma once

#include <string_view>
#include <unordered_set>
#include <unordered_map>

class FileTree {
public:
	const std::unordered_set<std::string>& getFiles() const {
		return files_;
	}

	void addFile(std::string_view file) {
		files_.insert(std::string(file));
	}

	const std::unordered_set<std::string>& getDependencies(std::string_view file) const {
		return dependencies_.at(std::string(file));
	}

	// Get all dependencies across all files
	std::unordered_set<std::string> getAllDependencies() const {
		std::unordered_set<std::string> all_deps;
		for (const auto& [file, deps] : dependencies_) {
			for (const auto& dep : deps) {
				all_deps.insert(dep);
			}
		}
		return all_deps;
	}

	void addDependency(std::string_view file, std::string_view dependency) {
		dependencies_[std::string(file)].insert(std::string(dependency));
	}

	FileTree& reset() {
		files_.clear();
		dependencies_.clear();
		return *this;
	}

private:
	std::unordered_set<std::string> files_;
	// Note: Using std::string (not string_view) as key is intentional.
	// addDependency() receives string_view parameters that may point to temporary strings.
	// Using string_view as the key would cause heap-use-after-free when the temporary is destroyed.
	std::unordered_map<std::string, std::unordered_set<std::string>> dependencies_;
};