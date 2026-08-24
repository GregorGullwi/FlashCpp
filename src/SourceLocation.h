#pragma once

#include <cstdint>
#include "Token.h"

// Single shared source position record used by AST nodes and diagnostics.
// Extracted from AstNodeTypes_TypeSystem.h so diagnostic infrastructure can
// depend on source positions without pulling in the full AST type system.
//
// Fields are 32-bit: line/column counts beyond 2^32 and translation-unit sets
// beyond ~4 billion files are not supported inputs, and the compact layout
// keeps location-bearing records small. Values arrive from Token's size_t
// accessors through explicit casts in the factories below.
struct SourceLocation {
	static constexpr uint32_t kInvalidFileIndex = UINT32_MAX;

	uint32_t line = 0;
	uint32_t column = 0;
	uint32_t file_index = kInvalidFileIndex;

	static SourceLocation fromToken(const Token& token) {
		return SourceLocation{
			static_cast<uint32_t>(token.line()),
			static_cast<uint32_t>(token.column()),
			static_cast<uint32_t>(token.file_index())};
	}

	static SourceLocation fromParts(size_t line_value, size_t column_value, size_t file_index_value) {
		return SourceLocation{
			static_cast<uint32_t>(line_value),
			static_cast<uint32_t>(column_value),
			static_cast<uint32_t>(file_index_value)};
	}

	bool is_valid() const {
		return line != 0 && file_index != kInvalidFileIndex;
	}
};

// Half-open textual span between two source positions. A range is present
// when its begin position is valid; fix-it style consumers key off the range
// endpoints rather than inferring them from message text.
struct SourceRange {
	SourceLocation begin{};
	SourceLocation end{};

	static SourceRange fromLocations(SourceLocation begin_value, SourceLocation end_value) {
		return SourceRange{begin_value, end_value};
	}

	bool is_valid() const {
		return begin.is_valid();
	}
};
