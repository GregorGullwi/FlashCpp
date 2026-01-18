#pragma once

#include <atomic>
#include <cstdint>
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
inline std::atomic<uint64_t> g_diagnosticVersion{ 0 };
inline char g_diagnosticFilePath[kMaxDiagnosticPathLength] = "";

inline void updateDiagnosticLocation(std::string_view file, size_t line, size_t column) {
	g_diagnosticVersion.fetch_add(1, std::memory_order_acq_rel);
	g_diagnosticLine.store(line, std::memory_order_relaxed);
	g_diagnosticColumn.store(column, std::memory_order_relaxed);

	if (file.empty()) {
		g_diagnosticFilePath[0] = '\0';
		g_diagnosticVersion.fetch_add(1, std::memory_order_release);
		return;
	}

	size_t max_len = kMaxDiagnosticPathLength - 1;
	size_t copy_len = file.size() < max_len ? file.size() : max_len;
	std::snprintf(g_diagnosticFilePath, copy_len + 1, "%.*s",
		static_cast<int>(copy_len), file.data());
	g_diagnosticVersion.fetch_add(1, std::memory_order_release);
}

inline DiagnosticLocation getDiagnosticLocation() {
	uint64_t start_version = g_diagnosticVersion.load(std::memory_order_acquire);
	if (start_version & 1U) {
		return { "<unknown>", 0, 0 };
	}
	size_t line = g_diagnosticLine.load(std::memory_order_relaxed);
	size_t column = g_diagnosticColumn.load(std::memory_order_relaxed);
	uint64_t end_version = g_diagnosticVersion.load(std::memory_order_acquire);
	if (start_version != end_version) {
		return { "<unknown>", 0, 0 };
	}
	return {
		g_diagnosticFilePath,
		line,
		column
	};
}

} // namespace FlashCpp
