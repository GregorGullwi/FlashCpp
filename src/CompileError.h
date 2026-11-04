#pragma once

#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "DiagnosticSnapshot.h"

// Semantic compilation error - distinct from InternalError so convert() can let
// these propagate unchanged while still catching internal codegen limitations.
// At the IR-to-object boundary, InternalError is rethrown as CompileError with
// the enclosing function name attached.
//
// Transitional bridge toward DiagnosticEngine-only reporting: a CompileError
// may carry a structured diagnostic recorded through the engine while keeping
// what() as the rendered message text. Legacy string construction remains the
// dominant path during migration; every legacy construction counts toward the
// always-available outside-engine inventory in Diagnostics.h.
class CompileError : public std::runtime_error {
public:
	explicit CompileError(const char* message)
		: std::runtime_error(message) {
		recordDiagnosticEmittedOutsideEngine();
	}

	explicit CompileError(std::string message)
		: std::runtime_error(message) {
		recordDiagnosticEmittedOutsideEngine();
	}

	CompileError(const CompileError&) = default;
	CompileError& operator=(const CompileError& other);
	CompileError(CompileError&& other) noexcept;
	CompileError& operator=(CompileError&& other) noexcept;

	// Capture while the record is published. The resulting error owns its text
	// and notes independently of the reporting engine.
	static CompileError fromStructuredDiagnostic(const DiagnosticEngine& engine, uint32_t diagnostic_index);

	// Note indices are local to structuredNotes(), not to a DiagnosticEngine.
	const Diagnostic* structuredDiagnostic() const noexcept {
		return structured_ ? &structured_->diagnostic() : nullptr;
	}
	std::span<const DiagnosticNote> structuredNotes() const noexcept {
		return structured_ ? structured_->notes() : std::span<const DiagnosticNote>{};
	}
	std::string renderStructuredDiagnostic(const std::deque<std::string>& file_paths) const;

private:
	struct BridgeTag {};
	CompileError(std::string rendered_message, const DiagnosticEngine& engine, uint32_t diagnostic_index, BridgeTag);
	std::optional<OwnedDiagnosticSnapshot> structured_;
};

// Single choke point for diagnostics that are recorded through the engine and
// then cross an exception boundary. The engine keeps the accumulated record;
// the returned error owns a snapshot so catch sites can render located,
// note-attached output independently of the reporting engine.
inline CompileError makeStructuredCompileError(DiagnosticEngine& engine, DiagnosticId id, DiagnosticSeverity severity, SourceLocation location,
	std::string_view message_template, std::span<const DiagnosticArgument> arguments) {
	uint32_t diagnostic_index = engine.report(id, severity, location, message_template, arguments);
	return CompileError::fromStructuredDiagnostic(engine, diagnostic_index);
}

// Internal codegen limitation error - distinct from CompileError so convert()
// can catch these specifically, attach function context, and surface them as
// CompileError while letting semantic CompileError instances propagate.
// Examples: unsupported types, register allocation failures, unimplemented features.
class InternalError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
