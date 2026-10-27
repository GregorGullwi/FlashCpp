#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace FlashCpp {

// Vector-backed open addressing keeps entries contiguous and avoids the
// per-entry allocations made by node-based unordered containers. A non-zero
// InlineSlotCapacity keeps small tables entirely in the object.
template<typename Key, typename Value, typename Hash, typename Equal, size_t InlineSlotCapacity = 0>
class FlatHashTable {
public:
	template<typename Callback>
	bool forEachValue(const Key& key, Callback&& callback) const {
		const size_t count = slotCount();
		if (count == 0) {
			return false;
		}
		const Slot* slots = slotData();
		size_t index = bucketIndex(key, count);
		for (size_t probes = 0; probes < count; ++probes) {
			const Slot& slot = slots[index];
			if (slot.state == SlotState::Empty) {
				return false;
			}
			if (slot.state == SlotState::Occupied && equal_(slot.key, key) &&
				callback(slot.value)) {
				return true;
			}
			index = (index + 1) & (count - 1);
		}
		return false;
	}

	bool contains(const Key& key) const {
		return forEachValue(key, [](const Value&) { return true; });
	}

	void insert(const Key& key, const Value& value) {
		ensureCapacity();
		insertInto(slotData(), slotCount(), key, value);
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
		const size_t count = slotCount();
		if (count == 0) {
			return false;
		}
		Slot* slots = slotData();
		size_t index = bucketIndex(key, count);
		for (size_t probes = 0; probes < count; ++probes) {
			Slot& slot = slots[index];
			if (slot.state == SlotState::Empty) {
				return false;
			}
			if (slot.state == SlotState::Occupied && equal_(slot.key, key)) {
				slot.state = SlotState::Tombstone;
				--size_;
				++tombstone_count_;
				return true;
			}
			index = (index + 1) & (count - 1);
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

	static constexpr size_t initialSlotCount() {
		return InlineSlotCapacity >= 8 ? 8 : 16;
	}

	static uint64_t mixHash(uint64_t value) {
		value ^= value >> 30;
		value *= 0xbf58476d1ce4e5b9ULL;
		value ^= value >> 27;
		value *= 0x94d049bb133111ebULL;
		value ^= value >> 31;
		return value;
	}

	size_t bucketIndex(const Key& key, size_t slot_count) const {
		return static_cast<size_t>(mixHash(static_cast<uint64_t>(hash_(key)))) & (slot_count - 1);
	}

	Slot* slotData() {
		return using_inline_slots_ ? inline_slots_.data() : heap_slots_.data();
	}

	const Slot* slotData() const {
		return using_inline_slots_ ? inline_slots_.data() : heap_slots_.data();
	}

	size_t slotCount() const {
		return using_inline_slots_ ? inline_slot_count_ : heap_slots_.size();
	}

	void insertInto(Slot* slots, size_t slot_count, const Key& key, const Value& value) {
		size_t index = bucketIndex(key, slot_count);
		size_t first_tombstone = slot_count;
		while (true) {
			Slot& slot = slots[index];
			if (slot.state == SlotState::Tombstone && first_tombstone == slot_count) {
				first_tombstone = index;
			} else if (slot.state == SlotState::Empty) {
				if (first_tombstone != slot_count) {
					index = first_tombstone;
					--tombstone_count_;
				}
				slots[index] = Slot{key, value, SlotState::Occupied};
				return;
			}
			index = (index + 1) & (slot_count - 1);
		}
	}

	void initializeSlots(size_t capacity) {
		if constexpr (InlineSlotCapacity > 0) {
			if (capacity <= InlineSlotCapacity) {
				using_inline_slots_ = true;
				inline_slot_count_ = capacity;
				for (size_t index = 0; index < capacity; ++index) {
					inline_slots_[index] = Slot{};
				}
				heap_slots_.clear();
				return;
			}
		}
		using_inline_slots_ = false;
		heap_slots_.assign(capacity, Slot{});
	}

	void ensureCapacity() {
		const size_t count = slotCount();
		if (count == 0) {
			initializeSlots(initialSlotCount());
			return;
		}
		if ((size_ + tombstone_count_ + 1) * 2 <= count) {
			return;
		}
		const size_t capacity = (size_ + 1) * 2 > count ? count * 2 : count;
		rehash(capacity);
	}

	void rehash(size_t capacity) {
		std::vector<Slot> replacement(capacity);
		const Slot* old_slots = slotData();
		const size_t old_count = slotCount();
		for (size_t index = 0; index < old_count; ++index) {
			const Slot& slot = old_slots[index];
			if (slot.state == SlotState::Occupied) {
				insertInto(replacement.data(), replacement.size(), slot.key, slot.value);
			}
		}
		if constexpr (InlineSlotCapacity > 0) {
			if (capacity <= InlineSlotCapacity) {
				using_inline_slots_ = true;
				inline_slot_count_ = capacity;
				for (size_t index = 0; index < capacity; ++index) {
					inline_slots_[index] = replacement[index];
				}
				heap_slots_.clear();
				tombstone_count_ = 0;
				return;
			}
		}
		using_inline_slots_ = false;
		heap_slots_ = std::move(replacement);
		tombstone_count_ = 0;
	}

	Hash hash_{};
	Equal equal_{};
	std::array<Slot, InlineSlotCapacity> inline_slots_{};
	std::vector<Slot> heap_slots_;
	size_t inline_slot_count_ = 0;
	bool using_inline_slots_ = false;
	size_t size_ = 0;
	size_t tombstone_count_ = 0;
};

template<typename Key, typename Hash, typename Equal, size_t InlineSlotCapacity = 0>
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
	FlatHashTable<Key, uint8_t, Hash, Equal, InlineSlotCapacity> entries_;
};

} // namespace FlashCpp
