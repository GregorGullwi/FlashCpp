#include "AllocationTracker.h"
#include "Log.h"
#include <atomic>
#include <iomanip>

#if FLASHCPP_TRACK_ALLOCATIONS
#include <cstdlib>
#include <new>
#endif

#if FLASHCPP_TRACK_ALLOCATION_STACKS
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#elif defined(__linux__) || defined(__APPLE__)
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif
#endif // FLASHCPP_TRACK_ALLOCATION_STACKS

namespace FlashCpp {

namespace {

std::atomic<bool> g_allocation_tracking_enabled{false};
std::atomic<uint64_t> g_allocation_count{0};
std::atomic<uint64_t> g_deallocation_count{0};
std::atomic<uint64_t> g_bytes_allocated{0};
std::atomic<uint64_t> g_bytes_deallocated{0};
std::atomic<uint64_t> g_current_live_bytes{0};
std::atomic<uint64_t> g_peak_live_bytes{0};

void updatePeakLive(uint64_t live_bytes) {
	uint64_t peak = g_peak_live_bytes.load(std::memory_order_relaxed);
	while (live_bytes > peak &&
		   !g_peak_live_bytes.compare_exchange_weak(peak, live_bytes, std::memory_order_relaxed)) {
	}
}

#if FLASHCPP_TRACK_ALLOCATION_STACKS

constexpr size_t kStackCaptureFrames = 16;
constexpr size_t kStackSkipFrames = 4;
constexpr size_t kTopSitesByCount = 25;
constexpr size_t kTopSitesByBytes = 15;

struct StackSite {
	std::array<void*, kStackCaptureFrames> frames{};
	size_t frame_count = 0;
	uint64_t allocation_count = 0;
	uint64_t bytes_allocated = 0;
};

std::mutex g_stack_sites_mutex;
std::unordered_map<uint64_t, StackSite> g_stack_sites;
thread_local bool g_recording_allocation_stack = false;

uint64_t hashStackFrames(const void* const* frames, size_t frame_count) {
	uint64_t hash = 14695981039346656037ULL;
	for (size_t i = 0; i < frame_count; ++i) {
		hash ^= static_cast<uint64_t>(reinterpret_cast<uintptr_t>(frames[i]));
		hash *= 1099511628211ULL;
	}
	return hash;
}

bool stackFramesEqual(const StackSite& left, const StackSite& right) {
	if (left.frame_count != right.frame_count) {
		return false;
	}
	return std::memcmp(left.frames.data(), right.frames.data(),
					   left.frame_count * sizeof(void*)) == 0;
}

void captureAllocationStackFrames(void** frames_out, size_t& frame_count_out) {
	frame_count_out = 0;
#if defined(_WIN32)
	USHORT captured = CaptureStackBackTrace(
		static_cast<DWORD>(kStackSkipFrames),
		static_cast<DWORD>(kStackCaptureFrames),
		frames_out,
		nullptr);
	frame_count_out = static_cast<size_t>(captured);
#elif defined(__linux__) || defined(__APPLE__)
	void* raw_frames[kStackSkipFrames + kStackCaptureFrames];
	const int raw_count = backtrace(raw_frames, static_cast<int>(std::size(raw_frames)));
	if (raw_count <= static_cast<int>(kStackSkipFrames)) {
		return;
	}
	const int usable_count = raw_count - static_cast<int>(kStackSkipFrames);
	const int capture_count =
		usable_count > static_cast<int>(kStackCaptureFrames) ? static_cast<int>(kStackCaptureFrames)
															: usable_count;
	std::memcpy(frames_out, raw_frames + kStackSkipFrames, static_cast<size_t>(capture_count) * sizeof(void*));
	frame_count_out = static_cast<size_t>(capture_count);
#endif
}

void recordAllocationStack(std::size_t size) {
	if (g_recording_allocation_stack) {
		return;
	}
	struct ReentrancyGuard {
		ReentrancyGuard() { g_recording_allocation_stack = true; }
		~ReentrancyGuard() { g_recording_allocation_stack = false; }
	} reentrancy_guard;

	void* frames[kStackCaptureFrames];
	size_t frame_count = 0;
	captureAllocationStackFrames(frames, frame_count);
	if (frame_count == 0) {
		return;
	}

	StackSite probe;
	probe.frame_count = frame_count;
	std::memcpy(probe.frames.data(), frames, frame_count * sizeof(void*));
	const uint64_t stack_hash = hashStackFrames(probe.frames.data(), probe.frame_count);

	std::lock_guard<std::mutex> lock(g_stack_sites_mutex);
	StackSite& site = g_stack_sites[stack_hash];
	if (site.frame_count == 0) {
		site = probe;
	} else if (!stackFramesEqual(site, probe)) {
		// Extremely unlikely hash collision: keep the first site and still count bytes.
	}
	++site.allocation_count;
	site.bytes_allocated += static_cast<uint64_t>(size);
}

#if defined(_WIN32)
void formatWindowsFrame(void* address, size_t frame_index, char* buffer, size_t buffer_size) {
	HANDLE process = GetCurrentProcess();
	DWORD64 displacement = 0;

	alignas(SYMBOL_INFO) unsigned char symbol_storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
	auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYM_NAME;

	size_t written = 0;
	const int prefix_written =
		std::snprintf(buffer, buffer_size, "  [%zu] 0x%p", frame_index, address);
	if (prefix_written < 0) {
		return;
	}
	written = static_cast<size_t>(prefix_written);
	if (written >= buffer_size) {
		buffer[buffer_size - 1] = '\0';
		return;
	}

	if (SymFromAddr(process, reinterpret_cast<DWORD64>(address), &displacement, symbol)) {
		const int symbol_written = std::snprintf(buffer + written, buffer_size - written, " %s +0x%llx",
												 symbol->Name, static_cast<unsigned long long>(displacement));
		if (symbol_written > 0) {
			written += static_cast<size_t>(symbol_written);
			if (written >= buffer_size) {
				buffer[buffer_size - 1] = '\0';
				return;
			}
		}

		DWORD line_displacement = 0;
		IMAGEHLP_LINE64 line = {};
		line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
		if (SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(address), &line_displacement, &line)) {
			std::snprintf(buffer + written, buffer_size - written, " (%s:%lu)", line.FileName, line.LineNumber);
		}
	}
}
#elif defined(__linux__) || defined(__APPLE__)
void formatPosixFrame(void* address, size_t frame_index, char* buffer, size_t buffer_size) {
	Dl_info info = {};
	const char* symbol_name = "??";
	if (dladdr(address, &info) != 0 && info.dli_sname != nullptr) {
		symbol_name = info.dli_sname;
	}

	int status = 0;
	char* demangled = abi::__cxa_demangle(symbol_name, nullptr, nullptr, &status);
	const char* display_name = (status == 0 && demangled != nullptr) ? demangled : symbol_name;
	std::snprintf(buffer, buffer_size, "  [%zu] %p %s", frame_index, address, display_name);
	if (demangled != nullptr) {
		std::free(demangled);
	}
}
#endif

void printResolvedStackSite(const StackSite& site, uint64_t total_allocations, uint64_t total_bytes) {
	const double count_pct =
		total_allocations > 0 ? (static_cast<double>(site.allocation_count) * 100.0 /
								 static_cast<double>(total_allocations))
							  : 0.0;
	const double bytes_pct = total_bytes > 0
								 ? (static_cast<double>(site.bytes_allocated) * 100.0 /
									static_cast<double>(total_bytes))
								 : 0.0;
	const double mean_bytes = site.allocation_count > 0
								  ? static_cast<double>(site.bytes_allocated) /
										static_cast<double>(site.allocation_count)
								  : 0.0;

	FLASH_LOG(General, Info, "  allocations=", site.allocation_count, " (", std::fixed, std::setprecision(2),
			  count_pct, "%), bytes=", site.bytes_allocated, " (", bytes_pct, "%), mean=", std::setprecision(1),
			  mean_bytes, " bytes");

#if defined(_WIN32)
	HANDLE process = GetCurrentProcess();
	SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
	static bool symbols_initialized = false;
	if (!symbols_initialized) {
		symbols_initialized = SymInitialize(process, nullptr, TRUE);
	}
#endif

	char frame_buffer[512];
	for (size_t i = 0; i < site.frame_count; ++i) {
#if defined(_WIN32)
		formatWindowsFrame(site.frames[i], i, frame_buffer, sizeof(frame_buffer));
#elif defined(__linux__) || defined(__APPLE__)
		formatPosixFrame(site.frames[i], i, frame_buffer, sizeof(frame_buffer));
#else
		std::snprintf(frame_buffer, sizeof(frame_buffer), "  [%zu] %p", i, site.frames[i]);
#endif
		FLASH_LOG(General, Info, frame_buffer);
	}
}

void printAllocationStackSites(const AllocationTracker::Snapshot& stats) {
	struct DisableStackRecording {
		DisableStackRecording() { g_recording_allocation_stack = true; }
		~DisableStackRecording() { g_recording_allocation_stack = false; }
	} disable_stack_recording;

	size_t site_count = 0;
	{
		std::lock_guard<std::mutex> lock(g_stack_sites_mutex);
		site_count = g_stack_sites.size();
	}

	std::vector<StackSite> site_copy;
	site_copy.reserve(site_count);
	{
		std::lock_guard<std::mutex> lock(g_stack_sites_mutex);
		site_copy.reserve(g_stack_sites.size());
		for (const auto& entry : g_stack_sites) {
			if (entry.second.allocation_count > 0) {
				site_copy.push_back(entry.second);
			}
		}
	}

	std::vector<const StackSite*> sites;
	sites.reserve(site_copy.size());
	for (const StackSite& site : site_copy) {
		sites.push_back(&site);
	}

	if (sites.empty()) {
		FLASH_LOG(General, Info, "Allocation stack sites: no samples captured");
		return;
	}

	FLASH_LOG(General, Info, "\n=== Allocation Stack Sites (operator new/delete) ===");
	FLASH_LOG(General, Info, "Unique stack patterns: ", sites.size(),
			  " (skip ", kStackSkipFrames, " allocator frames, capture ", kStackCaptureFrames, ")");

	std::sort(sites.begin(), sites.end(), [](const StackSite* left, const StackSite* right) {
		if (left->allocation_count != right->allocation_count) {
			return left->allocation_count > right->allocation_count;
		}
		return left->bytes_allocated > right->bytes_allocated;
	});

	FLASH_LOG(General, Info, "\nTop sites by allocation count:");
	const size_t count_limit = std::min(kTopSitesByCount, sites.size());
	for (size_t i = 0; i < count_limit; ++i) {
		FLASH_LOG(General, Info, "#", i + 1);
		printResolvedStackSite(*sites[i], stats.allocation_count, stats.bytes_allocated);
	}

	std::sort(sites.begin(), sites.end(), [](const StackSite* left, const StackSite* right) {
		if (left->bytes_allocated != right->bytes_allocated) {
			return left->bytes_allocated > right->bytes_allocated;
		}
		return left->allocation_count > right->allocation_count;
	});

	FLASH_LOG(General, Info, "\nTop sites by bytes allocated:");
	const size_t bytes_limit = std::min(kTopSitesByBytes, sites.size());
	for (size_t i = 0; i < bytes_limit; ++i) {
		FLASH_LOG(General, Info, "#", i + 1);
		printResolvedStackSite(*sites[i], stats.allocation_count, stats.bytes_allocated);
	}
}

#endif // FLASHCPP_TRACK_ALLOCATION_STACKS

} // namespace

void AllocationTracker::setEnabled(bool enabled) {
	g_allocation_tracking_enabled.store(enabled, std::memory_order_relaxed);
}

bool AllocationTracker::isEnabled() {
	return g_allocation_tracking_enabled.load(std::memory_order_relaxed);
}

void AllocationTracker::recordAllocation(std::size_t size) {
	if (!isEnabled()) {
		return;
	}
	++g_allocation_count;
	g_bytes_allocated.fetch_add(static_cast<uint64_t>(size), std::memory_order_relaxed);
	const uint64_t live_bytes =
		g_current_live_bytes.fetch_add(static_cast<uint64_t>(size), std::memory_order_relaxed) +
		static_cast<uint64_t>(size);
	updatePeakLive(live_bytes);
#if FLASHCPP_TRACK_ALLOCATION_STACKS
	recordAllocationStack(size);
#endif
}

void AllocationTracker::recordDeallocation(std::size_t size) {
	if (!isEnabled()) {
		return;
	}
	++g_deallocation_count;
	g_bytes_deallocated.fetch_add(static_cast<uint64_t>(size), std::memory_order_relaxed);
	uint64_t live_bytes = g_current_live_bytes.load(std::memory_order_relaxed);
	const uint64_t requested = static_cast<uint64_t>(size);
	while (live_bytes < requested &&
		   !g_current_live_bytes.compare_exchange_weak(live_bytes, 0, std::memory_order_relaxed)) {
	}
	if (live_bytes >= requested) {
		g_current_live_bytes.fetch_sub(requested, std::memory_order_relaxed);
	}
}

AllocationTracker::Snapshot AllocationTracker::snapshot() {
	Snapshot snapshot;
	snapshot.allocation_count = g_allocation_count.load(std::memory_order_relaxed);
	snapshot.deallocation_count = g_deallocation_count.load(std::memory_order_relaxed);
	snapshot.bytes_allocated = g_bytes_allocated.load(std::memory_order_relaxed);
	snapshot.bytes_deallocated = g_bytes_deallocated.load(std::memory_order_relaxed);
	snapshot.current_live_bytes = g_current_live_bytes.load(std::memory_order_relaxed);
	snapshot.peak_live_bytes = g_peak_live_bytes.load(std::memory_order_relaxed);
	return snapshot;
}

void AllocationTracker::printStats() {
#if FLASHCPP_TRACK_ALLOCATIONS
	if (!isEnabled()) {
		FLASH_LOG(General, Info, "Global allocation tracking: disabled (rebuild with FLASHCPP_TRACK_ALLOCATIONS=1)");
		return;
	}
	const Snapshot stats = snapshot();
	FLASH_LOG(General, Info, "Global allocation tracking (operator new/delete):");
	FLASH_LOG(General, Info, "  allocations=", stats.allocation_count,
			  ", deallocations=", stats.deallocation_count,
			  ", bytes allocated=", stats.bytes_allocated,
			  ", bytes deallocated=", stats.bytes_deallocated);
	FLASH_LOG(General, Info, "  current live bytes=", stats.current_live_bytes,
			  ", peak live bytes=", stats.peak_live_bytes);
#if FLASHCPP_TRACK_ALLOCATION_STACKS
	printAllocationStackSites(stats);
#else
	FLASH_LOG(General, Info,
			  "  stack sites: unavailable (rebuild with FLASHCPP_TRACK_ALLOCATION_STACKS=1)");
#endif
#else
	FLASH_LOG(General, Info, "Global allocation tracking: unavailable (rebuild with FLASHCPP_TRACK_ALLOCATIONS=1)");
#endif
}

} // namespace FlashCpp

#if FLASHCPP_TRACK_ALLOCATIONS

namespace {

struct AllocationSizeHeader {
	std::size_t size;
};

constexpr std::size_t allocationHeaderSize() {
	return (sizeof(AllocationSizeHeader) + alignof(std::max_align_t) - 1) &
		   ~(alignof(std::max_align_t) - 1);
}

void* allocateTracked(std::size_t size) {
	const std::size_t total_size = allocationHeaderSize() + size;
	unsigned char* raw = static_cast<unsigned char*>(std::malloc(total_size));
	if (raw == nullptr) {
		throw std::bad_alloc();
	}
	auto* header = reinterpret_cast<AllocationSizeHeader*>(raw);
	header->size = total_size;
	FlashCpp::AllocationTracker::recordAllocation(total_size);
	return raw + allocationHeaderSize();
}

void deallocateTracked(void* ptr) noexcept {
	if (ptr == nullptr) {
		return;
	}
	unsigned char* raw = static_cast<unsigned char*>(ptr) - allocationHeaderSize();
	auto* header = reinterpret_cast<AllocationSizeHeader*>(raw);
	FlashCpp::AllocationTracker::recordDeallocation(header->size);
	std::free(raw);
}

} // namespace

void* operator new(std::size_t size) {
	return allocateTracked(size);
}

void* operator new[](std::size_t size) {
	return allocateTracked(size);
}

void operator delete(void* ptr) noexcept {
	deallocateTracked(ptr);
}

void operator delete[](void* ptr) noexcept {
	deallocateTracked(ptr);
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept {
	deallocateTracked(ptr);
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept {
	deallocateTracked(ptr);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	try {
		return allocateTracked(size);
	} catch (...) {
		return nullptr;
	}
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	try {
		return allocateTracked(size);
	} catch (...) {
		return nullptr;
	}
}

#endif // FLASHCPP_TRACK_ALLOCATIONS
