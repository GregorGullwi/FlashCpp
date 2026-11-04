#pragma once

#include <deque>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Diagnostics.h"

// Flat exception payload: note indices address this snapshot's own note list.
// One byte buffer owns all non-interned text; copies rebind views by field order.
class OwnedDiagnosticSnapshot {
public:
	OwnedDiagnosticSnapshot(const DiagnosticEngine& engine, uint32_t diagnostic_index);
	OwnedDiagnosticSnapshot(const OwnedDiagnosticSnapshot& other);
	OwnedDiagnosticSnapshot& operator=(const OwnedDiagnosticSnapshot& other);
	OwnedDiagnosticSnapshot(OwnedDiagnosticSnapshot&& other) noexcept;
	OwnedDiagnosticSnapshot& operator=(OwnedDiagnosticSnapshot&& other) noexcept;

	const Diagnostic& diagnostic() const noexcept { return diagnostic_; }
	std::span<const DiagnosticNote> notes() const noexcept { return notes_; }
	std::string render(const std::deque<std::string>& file_paths) const;

private:
	static_assert(std::is_nothrow_move_constructible_v<Diagnostic>);
	static_assert(std::is_nothrow_move_assignable_v<Diagnostic>);

	static const Diagnostic& checkedDiagnostic(const DiagnosticEngine& engine, uint32_t diagnostic_index);
	void captureText();
	void rebindTextViews();
	void clearMovedFrom() noexcept;

	template <typename Visitor>
	void forEachText(Visitor&& visit) {
		visit(diagnostic_.message_template);
		for (auto& argument : diagnostic_.arguments) {
			if (argument.kind == DiagnosticArgument::Kind::Text) visit(argument.text_value);
		}
		for (auto& note : notes_) {
			visit(note.message_template);
			for (auto& argument : note.arguments) {
				if (argument.kind == DiagnosticArgument::Kind::Text) visit(argument.text_value);
			}
		}
	}

	Diagnostic diagnostic_;
	std::vector<DiagnosticNote> notes_;
	std::vector<char> text_;
};
