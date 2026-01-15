#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <deque>
#include <optional>
#include <stack>
#include <unordered_map>
#include "ChunkedAnyVector.h"
#include "Log.h"

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
		if (isVerboseMode()) {
			FLASH_LOG(General, Info, "Adding include directory: ", includeDir);
		}
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

	// Getter and setter for disabling access control checking
	bool isAccessControlDisabled() const {
		return disableAccessControl_;
	}

	void setDisableAccessControl(bool disable) {
		disableAccessControl_ = disable;
	}

	// Compiler compatibility mode - controls which compiler's builtin macros to use
	// Default is MSVC for Windows builds
	enum class CompilerMode {
		MSVC,    // Microsoft Visual C++ (default)
		GCC      // GCC/Clang (Linux/macOS)
	};

	CompilerMode getCompilerMode() const {
		return compilerMode_;
	}

	void setCompilerMode(CompilerMode mode) {
		compilerMode_ = mode;
	}

	bool isMsvcMode() const {
		return compilerMode_ == CompilerMode::MSVC;
	}

	bool isGccMode() const {
		return compilerMode_ == CompilerMode::GCC;
	}

	// Name mangling style - controls which ABI to use for name mangling
	// Can be set independently from output format for cross-compilation
	enum class ManglingStyle {
		MSVC,      // Microsoft Visual C++ name mangling
		Itanium    // Itanium C++ ABI name mangling (Linux/Unix)
	};

	ManglingStyle getManglingStyle() const {
		return manglingStyle_;
	}

	void setManglingStyle(ManglingStyle style) {
		manglingStyle_ = style;
	}

	bool isMsvcMangling() const {
		return manglingStyle_ == ManglingStyle::MSVC;
	}

	bool isItaniumMangling() const {
		return manglingStyle_ == ManglingStyle::Itanium;
	}

	// Target data model - controls the size of 'long' and related types
	// Windows uses LLP64: long is 32-bit, long long is 64-bit
	// Linux/Unix uses LP64: long is 64-bit, long long is 64-bit
	enum class DataModel {
		LLP64,     // Windows x64: long = 32 bits (COFF)
		LP64       // Linux/Unix x64: long = 64 bits (ELF)
	};

	DataModel getDataModel() const {
		return dataModel_;
	}

	void setDataModel(DataModel model) {
		dataModel_ = model;
	}

	bool isLLP64() const {
		return dataModel_ == DataModel::LLP64;
	}

	bool isLP64() const {
		return dataModel_ == DataModel::LP64;
	}

	// Get the size of 'long' in bits based on the target data model
	// Windows (LLP64): long = 32 bits
	// Linux (LP64): long = 64 bits
	int getLongSizeBits() const {
		return isLLP64() ? 32 : 64;
	}

	// Get the size of 'long' in bytes based on the target data model
	int getLongSizeBytes() const {
		return getLongSizeBits() / 8;
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

	// Push with named identifier (for #pragma pack(push, identifier))
	void pushPackAlignment(std::string_view identifier) {
		packAlignmentStack_.push(currentPackAlignment_);
		// Named state stores the current alignment (not changing it)
		namedPackStates_[std::string(identifier)] = currentPackAlignment_;
	}

	// Push with named identifier and alignment (for #pragma pack(push, identifier, n))
	void pushPackAlignment(std::string_view identifier, size_t alignment) {
		packAlignmentStack_.push(currentPackAlignment_);
		// Named state stores the NEW alignment we're setting
		namedPackStates_[std::string(identifier)] = alignment;
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

	// Pop to a named pack state (for #pragma pack(pop, identifier))
	void popPackAlignment(std::string_view identifier) {
		auto it = namedPackStates_.find(std::string(identifier));
		if (it != namedPackStates_.end()) {
			// Restore to the named state
			currentPackAlignment_ = it->second;
			// Pop the stack until we get back to that state (or empty)
			// This matches MSVC behavior of unwinding to the named label
			while (!packAlignmentStack_.empty() && packAlignmentStack_.top() != it->second) {
				packAlignmentStack_.pop();
			}
			if (!packAlignmentStack_.empty()) {
				packAlignmentStack_.pop();
			}
			namedPackStates_.erase(it);
		} else {
			// Named state not found, just do a regular pop
			popPackAlignment();
		}
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

	// Getter and setter for lazy template instantiation mode
	// When enabled, template member functions are instantiated only when used (C++ standard behavior)
	// When disabled, all template members are instantiated eagerly (for testing/debugging)
	bool isLazyTemplateInstantiationEnabled() const {
		return enableLazyTemplateInstantiation_;
	}

	void setLazyTemplateInstantiation(bool enable) {
		enableLazyTemplateInstantiation_ = enable;
	}

private:
	std::vector<std::string> includeDirs_;
	std::optional<std::string> inputFile_;
	std::string outputFile_;
	bool verboseMode_ = false;
	bool preprocessorOnlyMode_ = false; // Added member variable for -E option
	bool disableAccessControl_ = false; // Disable access control checking (for debugging)
	bool enableLazyTemplateInstantiation_ = true; // Enable lazy template member instantiation (default: on)
	CompilerMode compilerMode_ = CompilerMode::MSVC;  // Default to MSVC mode
	ManglingStyle manglingStyle_ = 
#if defined(_WIN32) || defined(_WIN64)
		ManglingStyle::MSVC;  // Windows default
#else
		ManglingStyle::Itanium;  // Linux/Unix default
#endif
	DataModel dataModel_ = 
#if defined(_WIN32) || defined(_WIN64)
		DataModel::LLP64;  // Windows default (long = 32 bits)
#else
		DataModel::LP64;   // Linux/Unix default (long = 64 bits)
#endif
	std::vector<std::string> dependencies_;

	// #pragma pack state
	size_t currentPackAlignment_ = 0;  // 0 = no packing (use natural alignment)
	std::stack<size_t> packAlignmentStack_;  // Stack for push/pop operations
	std::unordered_map<std::string, size_t> namedPackStates_;  // Named pack states for labels

	// Storage for function name string literals (__FUNCTION__, __func__, __PRETTY_FUNCTION__)
	// Using std::deque instead of std::vector to avoid invalidating string_views on reallocation
	// deque guarantees that references/pointers to elements remain valid when adding new elements
	std::deque<std::string> function_name_literals_;
};
