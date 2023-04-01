#include <string_view>
#include <vector>

#pragma once

class CompileContext {
public:
	const std::optional<std::string_view>& getInputFile() const {
		return inputFile_;
	}

	void setInputFile(std::string_view inputFile) {
		inputFile_ = inputFile;
	}

	const std::string_view getOutputFile() const {
		return outputFile_;
	}

	void setOutputFile(std::string_view outputFile) {
		outputFile_ = outputFile;
	}

	bool isVerboseMode() const {
		return verboseMode_;
	}

	void setVerboseMode(bool verboseMode) {
		verboseMode_ = verboseMode;
	}

	const std::vector<std::string_view>& getIncludeDirs() const {
		return includeDirs_;
	}

	void addIncludeDir(std::string_view includeDir) {
		includeDirs_.push_back(includeDir);
	}

	const std::vector<std::string>& getDependencies() const {
		return dependencies_;
	}

	void addDependency(const std::string& dependency) {
		dependencies_.push_back(dependency);
	}

private:
	std::vector<std::string_view> includeDirs_;
	std::optional<std::string_view> inputFile_;
	std::string_view outputFile_;
	bool verboseMode_ = false;
	std::vector<std::string> dependencies_;
};
