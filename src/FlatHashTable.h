#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace FlashCpp {

// Vector-backed open addressing keeps entries contiguous and avoids the
// per-entry allocations made by node-based unordered containers.
template<typename Key, typename Value, typename Hash, typename Equal>
class FlatHashTable {
public:
	template<typename Callback>
	bool forEachValue(const Key& key, Callback&& callback) const {
		if (slots_.empty()) {
			return false;
		}
		size_t index = hash_(key) & (slots_.size() - 1);
		for (size_t probes = 0; probes < slots_.size(); ++probes) {
			const Slot& slot = slots_[index];
			if (slot.state == SlotState::Empty) {
				return false;
			}
			if (slot.state == SlotState::Occupied && equal_(slot.key, key) &&
				callback(slot.value)) {
				return true;
			}
			index = (index + 1) & (slots_.size() - 1);
		}
		return false;
	}

	bool contains(const Key& key) const {
		return forEachValue(key, [](const Value&) { return true; });
	}

	void insert(const Key& key, const Value& value) {
		ensureCapacity();
		insertInto(slots_, key, value);
		++size_;
	}

	bool insertUnique(const Key& key, const Value& value) {
		if (contains(key)) {
			return false;
		}
		insert(key, value);
		return true;
	}

	bool eraseOne(const Key& key) {
		if (slots_.empty()) {
			return false;
		}
		size_t index = hash_(key) & (slots_.size() - 1);
		for (size_t probes = 0; probes < slots_.size(); ++probes) {
			Slot& slot = slots_[index];
			if (slot.state == SlotState::Empty) {
				return false;
			}
			if (slot.state == SlotState::Occupied && equal_(slot.key, key)) {
				slot.state = SlotState::Tombstone;
				--size_;
				++tombstone_count_;
				return true;
			}
			index = (index + 1) & (slots_.size() - 1);
		}
		return false;
	}

	size_t size() const {
		return size_;
	}

private:
	enum class SlotState : uint8_t {
		Empty,
		Occupied,
		Tombstone,
	};
	struct Slot {
		Key key{};
		Value value{};
		SlotState state = SlotState::Empty;
	};

	void insertInto(std::vector<Slot>& slots, const Key& key, const Value& value) {
		size_t index = hash_(key) & (slots.size() - 1);
		size_t first_tombstone = slots.size();
		while (true) {
			Slot& slot = slots[index];
			if (slot.state == SlotState::Tombstone && first_tombstone == slots.size()) {
				first_tombstone = index;
			} else if (slot.state == SlotState::Empty) {
				if (first_tombstone != slots.size()) {
					index = first_tombstone;
					--tombstone_count_;
			}
				slots[index] = Slot{key, value, SlotState::Occupied};
				return;
			}
			index = (index + 1) & (slots.size() - 1);
		}
	}

	void ensureCapacity() {
		if (slots_.empty()) {
			rehash(16);
			return;
		}
		if ((size_ + tombstone_count_ + 1) * 2 <= slots_.size()) {
			return;
		}
		const size_t capacity =
			(size_ + 1) * 2 > slots_.size()
				? slots_.size() * 2
				: slots_.size();
		rehash(capacity);
	}

	void rehash(size_t capacity) {
		replacement_slots_.clear();
		replacement_slots_.resize(capacity);
		for (const Slot& slot : slots_) {
			if (slot.state == SlotState::Occupied) {
				insertInto(replacement_slots_, slot.key, slot.value);
			}
		}
		slots_.swap(replacement_slots_);
		replacement_slots_.clear();
		tombstone_count_ = 0;
	}

	Hash hash_{};
	Equal equal_{};
	std::vector<Slot> slots_;
	std::vector<Slot> replacement_slots_;
	size_t size_ = 0;
	size_t tombstone_count_ = 0;
};

template<typename Key, typename Hash, typename Equal>
class FlatHashSet {
public:
	bool contains(const Key& key) const {
		return entries_.contains(key);
	}

	bool insert(const Key& key) {
		return entries_.insertUnique(key, uint8_t{});
	}

	bool erase(const Key& key) {
		return entries_.eraseOne(key);
	}

private:
	FlatHashTable<Key, uint8_t, Hash, Equal> entries_;
};

} // namespace FlashCpp
