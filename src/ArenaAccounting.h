#pragma once

#include <array>
#include <cstdint>
#include <mutex>

struct DomainByteStats {
	uint64_t current_bytes = 0;
	uint64_t peak_bytes = 0;
	uint64_t reserved_bytes = 0;
	uint64_t peak_reserved_bytes = 0;
};

enum class SemanticArenaComponent : uint8_t { Declarations, Types };

// Updated at mutation time, including rollback. Component peaks cannot be
// added: their allocations need not have coexisted. No lock here calls an arena.
class SemanticArenaAccounting {
public:
	void update(SemanticArenaComponent component, uint64_t used, uint64_t reserved) {
		std::lock_guard lock(mutex_);
		const auto index = static_cast<size_t>(component);
		stats_.current_bytes -= used_[index];
		stats_.current_bytes += used;
		stats_.reserved_bytes -= reserved_[index];
		stats_.reserved_bytes += reserved;
		used_[index] = used;
		reserved_[index] = reserved;
		if (stats_.current_bytes > stats_.peak_bytes) {
			stats_.peak_bytes = stats_.current_bytes;
		}
		if (stats_.reserved_bytes > stats_.peak_reserved_bytes) {
			stats_.peak_reserved_bytes = stats_.reserved_bytes;
		}
	}

	DomainByteStats snapshot() const {
		std::lock_guard lock(mutex_);
		return stats_;
	}

private:
	mutable std::mutex mutex_;
	std::array<uint64_t, 2> used_{};
	std::array<uint64_t, 2> reserved_{};
	DomainByteStats stats_;
};
