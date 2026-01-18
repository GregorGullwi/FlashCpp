#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace FlashCpp {

constexpr size_t kMaxDiagnosticPathLength = 512;

struct DiagnosticLocation {
	const char* file;
	size_t line;
	size_t column;
};

inline std::atomic<size_t> g_diagnosticLine{ 0 };
inline std::atomic<size_t> g_diagnosticColumn{ 0 };
inline char g_diagnosticFilePath[kMaxDiagnosticPathLength] = "";

inline void updateDiagnosticLocation(std::string_view file, size_t line, size_t column) {
	g_diagnosticLine.store(line, std::memory_order_relaxed);
	g_diagnosticColumn.store(column, std::memory_order_relaxed);

	if (file.empty()) {
		g_diagnosticFilePath[0] = '\0';
		return;
	}

	size_t max_len = kMaxDiagnosticPathLength - 1;
	size_t copy_len = file.size() < max_len ? file.size() : max_len;
	std::snprintf(g_diagnosticFilePath, kMaxDiagnosticPathLength, "%.*s",
		static_cast<int>(copy_len), file.data());
}

inline DiagnosticLocation getDiagnosticLocation() {
	return {
		g_diagnosticFilePath,
		g_diagnosticLine.load(std::memory_order_relaxed),
		g_diagnosticColumn.load(std::memory_order_relaxed)
	};
}

} // namespace FlashCpp
