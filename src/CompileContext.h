#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

class CompileContext {
public:
	const std::optional<std::string>& getInputFile() const {
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

	const std::vector<std::string>& getIncludeDirs() const {
		return includeDirs_;
	}

	void addIncludeDir(std::string_view includeDir) {
		includeDirs_.emplace_back(includeDir);
	}

	const std::vector<std::string>& getDependencies() const {
		return dependencies_;
	}

	void addDependency(const std::string& dependency) {
		dependencies_.push_back(dependency);
	}

	// Getter and setter for the -E preprocessor option
	bool isPreprocessorOnlyMode() const {
		return preprocessorOnlyMode_;
	}

	void setPreprocessorOnlyMode(bool preprocessorOnlyMode) {
		preprocessorOnlyMode_ = preprocessorOnlyMode;
	}

private:
	std::vector<std::string> includeDirs_;
	std::optional<std::string> inputFile_;
	std::string_view outputFile_;
	bool verboseMode_ = false;
	bool preprocessorOnlyMode_ = false; // Added member variable for -E option
	std::vector<std::string> dependencies_;
};
