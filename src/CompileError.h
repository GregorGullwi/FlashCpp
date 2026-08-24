#pragma once

#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "Diagnostics.h"

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

	// Bridge constructor: takes ownership of an engine-reported diagnostic.
	// what() stays the rendered message so existing catch sites keep working;
	// engine-aware catch sites print the located, note-attached rendering.
	static CompileError fromStructuredDiagnostic(Diagnostic diagnostic) {
		std::string rendered_message = renderDiagnosticMessage(
			diagnostic.message_template, diagnostic.arguments);
		return CompileError(std::move(rendered_message), std::move(diagnostic), BridgeTag{});
	}

	const Diagnostic* structuredDiagnostic() const noexcept {
		return has_structured_ ? &structured_ : nullptr;
	}

private:
	struct BridgeTag {};

	CompileError(std::string rendered_message, Diagnostic diagnostic, BridgeTag)
		: std::runtime_error(std::move(rendered_message)),
		  has_structured_(true),
		  structured_(std::move(diagnostic)) {}

	bool has_structured_ = false;
	Diagnostic structured_{};
};

// Single choke point for diagnostics that are recorded through the engine and
// then cross an exception boundary. The engine keeps the accumulated record;
// the returned error carries a value copy so catch sites can render located,
// note-attached output without touching compiler state.
inline CompileError makeStructuredCompileError(
	DiagnosticEngine& engine,
	DiagnosticId id,
	DiagnosticSeverity severity,
	SourceLocation location,
	std::string_view message_template,
	std::span<const DiagnosticArgument> arguments) {
	uint32_t diagnostic_index = engine.report(id, severity, location, message_template, arguments);
	return CompileError::fromStructuredDiagnostic(engine.diagnostic(diagnostic_index));
}

// Internal codegen limitation error - distinct from CompileError so convert()
// can catch these specifically, attach function context, and surface them as
// CompileError while letting semantic CompileError instances propagate.
// Examples: unsupported types, register allocation failures, unimplemented features.
class InternalError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};
