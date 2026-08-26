#pragma once

#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "InlineVector.h"
#include "SourceLocation.h"
#include "StringTable.h"
#include "StringBuilder.h"

// Structured diagnostic infrastructure (front-end rearchitecture, boundary 0).
//
// Ownership contract:
// - DiagnosticEngine instances accumulate Diagnostic records by value. Stored
//   records own message and argument text through stable engine-side storage,
//   notes (via pool indices), and a value-copy of the active
//   template-instantiation context captured at report time.
// - Nothing stored here escapes by pointer or reference except const views
//   whose validity matches the engine lifetime.
// - DiagnosticId values are stable forever and never derived from message
//   text. New IDs must take unused numbers inside their family block so a
//   later fixer registry keyed by DiagnosticId stays collision-free.
//
// Numeric ID block allocation:
//   0        reserved (None)
//   1001..1099  declarator and type-id syntax family
//   1051..1079  note-level IDs belonging to the declarator family
//   1101..1199  lexical numeric-literal family
//   1201..1299  constant-expression evaluation family
//   2000..2099  reserved for template instantiation family
//   3000+    unallocated

enum class DiagnosticSeverity : uint8_t {
	Note,
	Warning,
	Error,
	Fatal,
};

inline const char* diagnosticSeverityTag(DiagnosticSeverity severity) {
	switch (severity) {
	case DiagnosticSeverity::Note:
		return "note";
	case DiagnosticSeverity::Warning:
		return "warning";
	case DiagnosticSeverity::Fatal:
		return "fatal error";
	case DiagnosticSeverity::Error:
	default:
		return "error";
	}
}

enum class DiagnosticId : uint32_t {
	None = 0,

	// Declarator / type-id family (1001..1099).
	PointerToReferenceType = 1001,
	MultipleAsmSuffixesOnDeclarator = 1002,
	ExpectedCloseBracketAfterArraySize = 1003,

	// Notes attached to declarator-family diagnostics (1051..1079).
	NoteToMatchOpeningBracket = 1051,

	// Lexical numeric-literal family (1101..1199).
	HexFloatRequiresBinaryExponent = 1101,
	InvalidIntegerLiteralSuffix = 1102,

	// Constant-expression evaluation family (1201..1299).
	ConstantExpressionDivisionByZero = 1201,
	ConstantExpressionModuloByZero = 1202,
	ConstantExpressionShiftCountTooLarge = 1203,
	ConstantExpressionShiftOperationInvalid = 1204,
	ConstantExpressionSignedIntegerOverflow = 1205,
	ConstantExpressionIndeterminateValueRead = 1206,
	ConstantExpressionPointerCreationOutsideObject = 1207,
	ConstantExpressionOutOfBoundsAccess = 1208,
	ConstantExpressionPointerSubtractionDifferentObjects = 1209,
	ConstantExpressionRelationalComparisonDifferentObjects = 1210,
	ConstantExpressionUseAfterFree = 1211,
	ConstantExpressionArrayIndexOutOfBounds = 1212,
	ConstantExpressionNullPointerDereference = 1213,
	ConstantExpressionPointerPlusPointer = 1214,
};

inline std::string_view diagnosticIdName(DiagnosticId id) {
	switch (id) {
	case DiagnosticId::PointerToReferenceType:
		return "PointerToReferenceType";
	case DiagnosticId::MultipleAsmSuffixesOnDeclarator:
		return "MultipleAsmSuffixesOnDeclarator";
	case DiagnosticId::ExpectedCloseBracketAfterArraySize:
		return "ExpectedCloseBracketAfterArraySize";
	case DiagnosticId::NoteToMatchOpeningBracket:
		return "NoteToMatchOpeningBracket";
	case DiagnosticId::HexFloatRequiresBinaryExponent:
		return "HexFloatRequiresBinaryExponent";
	case DiagnosticId::InvalidIntegerLiteralSuffix:
		return "InvalidIntegerLiteralSuffix";
	case DiagnosticId::ConstantExpressionDivisionByZero:
		return "ConstantExpressionDivisionByZero";
	case DiagnosticId::ConstantExpressionModuloByZero:
		return "ConstantExpressionModuloByZero";
	case DiagnosticId::ConstantExpressionShiftCountTooLarge:
		return "ConstantExpressionShiftCountTooLarge";
	case DiagnosticId::ConstantExpressionShiftOperationInvalid:
		return "ConstantExpressionShiftOperationInvalid";
	case DiagnosticId::ConstantExpressionSignedIntegerOverflow:
		return "ConstantExpressionSignedIntegerOverflow";
	case DiagnosticId::ConstantExpressionIndeterminateValueRead:
		return "ConstantExpressionIndeterminateValueRead";
	case DiagnosticId::ConstantExpressionPointerCreationOutsideObject:
		return "ConstantExpressionPointerCreationOutsideObject";
	case DiagnosticId::ConstantExpressionOutOfBoundsAccess:
		return "ConstantExpressionOutOfBoundsAccess";
	case DiagnosticId::ConstantExpressionPointerSubtractionDifferentObjects:
		return "ConstantExpressionPointerSubtractionDifferentObjects";
	case DiagnosticId::ConstantExpressionRelationalComparisonDifferentObjects:
		return "ConstantExpressionRelationalComparisonDifferentObjects";
	case DiagnosticId::ConstantExpressionUseAfterFree:
		return "ConstantExpressionUseAfterFree";
	case DiagnosticId::ConstantExpressionArrayIndexOutOfBounds:
		return "ConstantExpressionArrayIndexOutOfBounds";
	case DiagnosticId::ConstantExpressionNullPointerDereference:
		return "ConstantExpressionNullPointerDereference";
	case DiagnosticId::ConstantExpressionPointerPlusPointer:
		return "ConstantExpressionPointerPlusPointer";
	case DiagnosticId::None:
	default:
		return "None";
	}
}

inline uint32_t diagnosticIdNumber(DiagnosticId id) {
	return static_cast<uint32_t>(id);
}

// Machine-consumable identity tag appended to every rendered primary and note
// line: "[<Name>#<number>]". The trailing bracket token is the stable
// assertion anchor for tooling and future runner diagnostic assertions; the
// leading "path:line:col:" prefix carries the deterministic location. Neither
// requires parsing message prose.
inline void appendDiagnosticIdTag(StringBuilder& builder, DiagnosticId id) {
	builder.append(" [");
	builder.append(diagnosticIdName(id));
	builder.append('#');
	builder.append(static_cast<int64_t>(diagnosticIdNumber(id)));
	builder.append(']');
}

// One structured substitution slot for a message template. Templates use "{}"
// placeholders consumed left to right. Text arguments passed to report() are
// copied into stable DiagnosticEngine storage before the record is retained.
struct DiagnosticArgument {
	enum class Kind : uint8_t {
		SignedInteger,
		UnsignedInteger,
		Text,
		InternedText,
	};

	Kind kind = Kind::SignedInteger;

	union Value {
		int64_t signed_value;
		uint64_t unsigned_value;
		StringHandle interned_text;

		Value() : signed_value(0) {}
	} value{};

	std::string_view text_value{};

	static DiagnosticArgument signedInteger(int64_t argument_value) {
		DiagnosticArgument argument;
		argument.kind = Kind::SignedInteger;
		argument.value.signed_value = argument_value;
		return argument;
	}

	static DiagnosticArgument unsignedInteger(uint64_t argument_value) {
		DiagnosticArgument argument;
		argument.kind = Kind::UnsignedInteger;
		argument.value.unsigned_value = argument_value;
		return argument;
	}

	static DiagnosticArgument text(std::string_view argument_text) {
		DiagnosticArgument argument;
		argument.kind = Kind::Text;
		argument.text_value = argument_text;
		return argument;
	}

	static DiagnosticArgument internedText(StringHandle argument_handle) {
		DiagnosticArgument argument;
		argument.kind = Kind::InternedText;
		argument.value.interned_text = argument_handle;
		return argument;
	}
};

// Secondary explanation anchored to its own source position. Stored in the
// engine-owned note pool; diagnostics reference notes by pool index.
struct DiagnosticNote {
	DiagnosticId id = DiagnosticId::None;
	SourceLocation location{};
	std::string_view message_template{};
	InlineVector<DiagnosticArgument, 2> arguments;
};

// One level of active template-instantiation nesting captured with each
// diagnostic. Depth is implied by frame order (outermost first); namespaces
// are lexical-scope context and intentionally not part of this record.
struct TemplateInstantiationFrame {
	StringHandle template_name{};
	SourceLocation point_of_instantiation{};
};

struct Diagnostic {
	DiagnosticId id = DiagnosticId::None;
	DiagnosticSeverity severity = DiagnosticSeverity::Error;
	SourceLocation location{};
	SourceRange range{};
	std::string_view message_template{};
	InlineVector<DiagnosticArgument, 3> arguments;
	InlineVector<uint32_t, 2> note_indices;
	InlineVector<TemplateInstantiationFrame, 4> instantiation_context;

	bool has_range() const {
		return range.is_valid();
	}
};

// Always-available inventory of diagnostics still emitted outside this engine
// (legacy CompileError strings and ParseResult errors). Directional evidence
// for boundary 11 burn-down; never compile-time gated.
inline uint64_t gDiagnosticsEmittedOutsideEngine = 0;

inline void recordDiagnosticEmittedOutsideEngine() {
	++gDiagnosticsEmittedOutsideEngine;
}

inline uint64_t diagnosticsEmittedOutsideEngineCount() {
	return gDiagnosticsEmittedOutsideEngine;
}

// Append the message template with sequential "{}" placeholder substitution.
// Contract: templates carry exactly one placeholder per argument. A missing
// argument renders nothing for its placeholder rather than guessing content.
inline void appendFormattedDiagnosticMessage(
	StringBuilder& builder,
	std::string_view message_template,
	std::span<const DiagnosticArgument> arguments) {
	size_t next_argument = 0;
	size_t cursor = 0;
	while (cursor < message_template.size()) {
		size_t placeholder = message_template.find("{}", cursor);
		if (placeholder == std::string_view::npos) {
			builder.append(message_template.substr(cursor));
			return;
		}
		builder.append(message_template.substr(cursor, placeholder - cursor));
		if (next_argument < arguments.size()) {
			const DiagnosticArgument& argument = arguments[next_argument];
			switch (argument.kind) {
			case DiagnosticArgument::Kind::SignedInteger:
				builder.append(argument.value.signed_value);
				break;
			case DiagnosticArgument::Kind::UnsignedInteger:
				builder.append(argument.value.unsigned_value);
				break;
			case DiagnosticArgument::Kind::InternedText:
				builder.append(argument.value.interned_text);
				break;
			case DiagnosticArgument::Kind::Text:
				builder.append(argument.text_value);
				break;
			}
			++next_argument;
		}
		cursor = placeholder + 2;
	}
}

inline std::string renderDiagnosticMessage(
	std::string_view message_template,
	std::span<const DiagnosticArgument> arguments) {
	StringBuilder builder;
	appendFormattedDiagnosticMessage(builder, message_template, arguments);
	return std::string(builder.commit());
}

class DiagnosticEngine {
public:
	// RAII frame for nested template-instantiation context. Frames snapshot
	// into reported diagnostics by value, so unwinding cannot corrupt them.
	class TemplateContextGuard {
	public:
		TemplateContextGuard(DiagnosticEngine& owner, StringHandle name, SourceLocation point_of_instantiation)
			: engine_(owner), active_(true) {
			engine_.pushTemplateContext(name, point_of_instantiation);
		}

		~TemplateContextGuard() {
			if (active_) {
				engine_.popTemplateContext();
			}
		}

		TemplateContextGuard(const TemplateContextGuard&) = delete;
		TemplateContextGuard& operator=(const TemplateContextGuard&) = delete;

	private:
		DiagnosticEngine& engine_;
		bool active_;
	};

	DiagnosticEngine() = default;
	DiagnosticEngine(const DiagnosticEngine&) = delete;
	DiagnosticEngine& operator=(const DiagnosticEngine&) = delete;

	uint32_t report(DiagnosticId id, DiagnosticSeverity severity, SourceLocation location,
					std::string_view message_template, std::span<const DiagnosticArgument> arguments) {
		Diagnostic diagnostic;
		diagnostic.id = id;
		diagnostic.severity = severity;
		diagnostic.location = location;
		diagnostic.message_template = storeText(message_template);
		copyArguments(diagnostic.arguments, arguments);
		snapshotInstantiationContext(diagnostic);
		return append(std::move(diagnostic));
	}

	uint32_t reportWithRange(DiagnosticId id, DiagnosticSeverity severity, SourceLocation location,
							 SourceRange range, std::string_view message_template,
							 std::span<const DiagnosticArgument> arguments) {
		uint32_t index = report(id, severity, location, message_template, arguments);
		diagnostics_[index].range = range;
		return index;
	}

	void attachNote(uint32_t diagnostic_index, DiagnosticId id, SourceLocation location,
					std::string_view message_template, std::span<const DiagnosticArgument> arguments) {
		DiagnosticNote note;
		note.id = id;
		note.location = location;
		note.message_template = storeText(message_template);
		copyArguments(note.arguments, arguments);
		uint32_t note_index = static_cast<uint32_t>(note_pool_.size());
		note_pool_.push_back(std::move(note));
		diagnostics_[diagnostic_index].note_indices.push_back(note_index);
	}

	void pushTemplateContext(StringHandle name, SourceLocation point_of_instantiation) {
		template_context_.push_back(TemplateInstantiationFrame{name, point_of_instantiation});
	}

	void popTemplateContext() {
		template_context_.pop_back();
	}

	size_t templateContextDepth() const {
		return template_context_.size();
	}

	std::span<const Diagnostic> diagnostics() const {
		return diagnostics_;
	}

	const Diagnostic& diagnostic(uint32_t index) const {
		return diagnostics_[index];
	}

	const DiagnosticNote& note(uint32_t pool_index) const {
		return note_pool_[pool_index];
	}

	size_t count(DiagnosticSeverity severity) const {
		return severity_counts_[static_cast<size_t>(severity)];
	}

	bool hasErrors() const {
		return count(DiagnosticSeverity::Error) > 0 || count(DiagnosticSeverity::Fatal) > 0;
	}

private:
	uint32_t append(Diagnostic diagnostic) {
		++severity_counts_[static_cast<size_t>(diagnostic.severity)];
		diagnostics_.push_back(std::move(diagnostic));
		return static_cast<uint32_t>(diagnostics_.size() - 1);
	}

	std::string_view storeText(std::string_view text) {
		text_storage_.emplace_back(text);
		return text_storage_.back();
	}

	template <size_t kInlineCapacity>
	void copyArguments(InlineVector<DiagnosticArgument, kInlineCapacity>& target,
					   std::span<const DiagnosticArgument> source) {
		for (const DiagnosticArgument& argument : source) {
			DiagnosticArgument stored_argument = argument;
			if (stored_argument.kind == DiagnosticArgument::Kind::Text) {
				stored_argument.text_value = storeText(stored_argument.text_value);
			}
			target.push_back(stored_argument);
		}
	}

	void snapshotInstantiationContext(Diagnostic& diagnostic) const {
		for (const TemplateInstantiationFrame& frame : template_context_) {
			diagnostic.instantiation_context.push_back(frame);
		}
	}

	std::vector<Diagnostic> diagnostics_;
	std::vector<DiagnosticNote> note_pool_;
	std::vector<TemplateInstantiationFrame> template_context_;
	std::deque<std::string> text_storage_;
	size_t severity_counts_[4] = {0, 0, 0, 0};
};

// Resolve a location to "path:line:col: " using the translation-unit file
// table. Unknown file indices render as "<unknown>" like existing reporters.
inline void appendDiagnosticLocationPrefix(StringBuilder& builder,
										   SourceLocation location,
										   const std::deque<std::string>& file_paths) {
	if (location.file_index < file_paths.size()) {
		builder.append(file_paths[location.file_index]);
	} else {
		builder.append("<unknown>");
	}
	builder.append(':');
	builder.append(static_cast<int64_t>(location.line));
	builder.append(':');
	builder.append(static_cast<int64_t>(location.column));
	builder.append(": ");
}

inline void appendRenderedDiagnosticLines(StringBuilder& builder,
										  const Diagnostic& diagnostic,
										  const DiagnosticEngine& engine,
										  const std::deque<std::string>& file_paths) {
	appendDiagnosticLocationPrefix(builder, diagnostic.location, file_paths);
	builder.append(diagnosticSeverityTag(diagnostic.severity));
	builder.append(": ");
	appendFormattedDiagnosticMessage(builder, diagnostic.message_template, diagnostic.arguments);
	appendDiagnosticIdTag(builder, diagnostic.id);

	for (const TemplateInstantiationFrame& frame : diagnostic.instantiation_context) {
		if (!frame.template_name.isValid()) {
			continue;
		}
		builder.append('\n');
		builder.append("  in instantiation of '");
		builder.append(frame.template_name);
		builder.append("' requested here");
	}

	for (uint32_t note_index : diagnostic.note_indices) {
		const DiagnosticNote& note = engine.note(note_index);
		builder.append('\n');
		if (note.location.is_valid()) {
			appendDiagnosticLocationPrefix(builder, note.location, file_paths);
		} else {
			builder.append("  ");
		}
		builder.append("note: ");
		appendFormattedDiagnosticMessage(builder, note.message_template, note.arguments);
		appendDiagnosticIdTag(builder, note.id);
	}
}

inline std::string renderDiagnostic(const Diagnostic& diagnostic,
									const DiagnosticEngine& engine,
									const std::deque<std::string>& file_paths) {
	StringBuilder builder;
	appendRenderedDiagnosticLines(builder, diagnostic, engine, file_paths);
	return std::string(builder.commit());
}
