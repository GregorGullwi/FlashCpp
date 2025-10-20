#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <optional>
#include <stack>
#include "ChunkedAnyVector.h"

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
		outputFile_ = std::string(outputFile);
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

	// #pragma pack state management
	// Returns the current pack alignment value (0 = no packing, use natural alignment)
	size_t getCurrentPackAlignment() const {
		return currentPackAlignment_;
	}

	// Set the current pack alignment (0 = reset to default, n = pack to n bytes)
	void setPackAlignment(size_t alignment) {
		currentPackAlignment_ = alignment;
	}

	// Push current pack alignment onto stack (for #pragma pack(push))
	void pushPackAlignment() {
		packAlignmentStack_.push(currentPackAlignment_);
	}

	// Push a specific pack alignment onto stack (for #pragma pack(push, n))
	void pushPackAlignment(size_t alignment) {
		packAlignmentStack_.push(currentPackAlignment_);
		currentPackAlignment_ = alignment;
	}

	// Pop pack alignment from stack (for #pragma pack(pop))
	void popPackAlignment() {
		if (!packAlignmentStack_.empty()) {
			currentPackAlignment_ = packAlignmentStack_.top();
			packAlignmentStack_.pop();
		}
		// If stack is empty, keep current value (matches MSVC behavior)
	}

	// Store a string literal for __PRETTY_FUNCTION__
	// Returns a string_view that remains valid for the lifetime of CompileContext
	// Only called when these identifiers are actually used in the code
	std::string_view storeFunctionNameLiteral(std::string_view function_name) {
		function_name_literals_.push_back(std::string(function_name));
		return function_name_literals_.back();
	}

	// Check if a specific header was included
	bool hasIncludedHeader(std::string_view header_name) const {
		for (const auto& dep : dependencies_) {
			// Check if the dependency ends with the header name
			// This handles both <new> and "new" style includes
			if (dep.size() >= header_name.size()) {
				size_t pos = dep.rfind(header_name);
				if (pos != std::string::npos && pos + header_name.size() == dep.size()) {
					return true;
				}
			}
		}
		return false;
	}

private:
	std::vector<std::string> includeDirs_;
	std::optional<std::string> inputFile_;
	std::string outputFile_;
	bool verboseMode_ = false;
	bool preprocessorOnlyMode_ = false; // Added member variable for -E option
	std::vector<std::string> dependencies_;

	// #pragma pack state
	size_t currentPackAlignment_ = 0;  // 0 = no packing (use natural alignment)
	std::stack<size_t> packAlignmentStack_;  // Stack for push/pop operations

	// Storage for function name string literals (__FUNCTION__, __func__, __PRETTY_FUNCTION__)
	// Using std::deque instead of std::vector to avoid invalidating string_views on reallocation
	// deque guarantees that references/pointers to elements remain valid when adding new elements
	std::deque<std::string> function_name_literals_;
};
