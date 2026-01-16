#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ChunkedString.h"
#include "StringTable.h"

template<typename T1, typename T2>
struct PairHash {
	size_t operator()(const std::pair<T1, T2>& pair) const {
		size_t h1 = std::hash<T1>{}(pair.first);
		size_t h2 = std::hash<T2>{}(pair.second);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

struct NamespaceHandle {
	static constexpr uint16_t INVALID_HANDLE = UINT16_MAX;
	uint16_t index = INVALID_HANDLE;

	bool isValid() const { return index != INVALID_HANDLE; }
	bool isGlobal() const { return index == 0; }

	bool operator==(NamespaceHandle other) const { return index == other.index; }
	bool operator!=(NamespaceHandle other) const { return index != other.index; }
	bool operator<(NamespaceHandle other) const { return index < other.index; }
};

struct NamespaceEntry {
	StringHandle name;
	StringHandle qualified_name;
	NamespaceHandle parent;
	uint8_t depth;
};

namespace std {
	template<>
	struct hash<NamespaceHandle> {
		size_t operator()(NamespaceHandle handle) const noexcept {
			return static_cast<size_t>(handle.index);
		}
	};
}

class NamespaceRegistry {
public:
	static constexpr NamespaceHandle GLOBAL_NAMESPACE = NamespaceHandle{0};
	static constexpr size_t DEFAULT_CAPACITY = 1024;

	NamespaceRegistry() {
		entries_.reserve(DEFAULT_CAPACITY);

		NamespaceEntry global;
		global.name = StringHandle{};
		global.parent = NamespaceHandle{NamespaceHandle::INVALID_HANDLE};
		global.qualified_name = StringHandle{};
		global.depth = 0;
		entries_.push_back(global);
		max_size_reached_ = 1;
	}

	bool exceededInitialCapacity() const { return max_size_reached_ > DEFAULT_CAPACITY; }
	size_t currentSize() const { return entries_.size(); }
	size_t maxSizeReached() const { return max_size_reached_; }

	NamespaceHandle getOrCreateNamespace(NamespaceHandle parent_handle, StringHandle name) {
		auto key = std::make_pair(parent_handle, name);
		auto it = namespace_map_.find(key);
		if (it != namespace_map_.end()) {
			return it->second;
		}

		if (entries_.size() >= static_cast<size_t>(NamespaceHandle::INVALID_HANDLE)) {
			assert(false && "Namespace registry capacity exceeded (65535 entries)");
			return NamespaceHandle{NamespaceHandle::INVALID_HANDLE};
		}

		NamespaceEntry entry;
		entry.name = name;
		entry.parent = parent_handle;
		entry.depth = parent_handle.isValid() ? getEntry(parent_handle).depth + 1 : 1;
		entry.qualified_name = buildQualifiedNameForEntry(parent_handle, name);

		NamespaceHandle new_handle{static_cast<uint16_t>(entries_.size())};
		entries_.push_back(entry);
		namespace_map_[key] = new_handle;

		if (entries_.size() > max_size_reached_) {
			max_size_reached_ = entries_.size();
		}

		return new_handle;
	}

	NamespaceHandle getOrCreatePath(NamespaceHandle start, std::initializer_list<std::string_view> components) {
		NamespaceHandle current = start;
		for (std::string_view component : components) {
			StringHandle name_handle = StringTable::getOrInternStringHandle(component);
			current = getOrCreateNamespace(current, name_handle);
			if (!current.isValid()) {
				return current;
			}
		}
		return current;
	}

	NamespaceHandle getOrCreatePath(NamespaceHandle start, const std::vector<StringHandle>& components) {
		NamespaceHandle current = start;
		for (StringHandle name_handle : components) {
			current = getOrCreateNamespace(current, name_handle);
			if (!current.isValid()) {
				return current;
			}
		}
		return current;
	}

	const NamespaceEntry& getEntry(NamespaceHandle handle) const {
		assert(handle.isValid() && handle.index < entries_.size());
		return entries_[handle.index];
	}

	std::string_view getQualifiedName(NamespaceHandle handle) const {
		if (!handle.isValid() || handle.isGlobal()) return "";
		return StringTable::getStringView(getEntry(handle).qualified_name);
	}

	NamespaceHandle getParent(NamespaceHandle handle) const {
		if (!handle.isValid() || handle.isGlobal()) return GLOBAL_NAMESPACE;
		return getEntry(handle).parent;
	}

	StringHandle buildQualifiedIdentifier(NamespaceHandle ns_handle, StringHandle identifier) const {
		if (!ns_handle.isValid() || ns_handle.isGlobal()) {
			return identifier;
		}

		const NamespaceEntry& entry = getEntry(ns_handle);
		StringBuilder sb;
		sb.append(StringTable::getStringView(entry.qualified_name));
		sb.append("::");
		sb.append(StringTable::getStringView(identifier));
		return StringTable::createStringHandle(sb);
	}

	bool isAncestorOf(NamespaceHandle potential_ancestor, NamespaceHandle child) const {
		NamespaceHandle current = child;
		while (current.isValid() && !current.isGlobal()) {
			if (current == potential_ancestor) return true;
			current = getParent(current);
		}
		return potential_ancestor.isGlobal();
	}

	std::vector<NamespaceHandle> getAncestors(NamespaceHandle handle) const {
		std::vector<NamespaceHandle> result;
		NamespaceHandle current = handle;
		while (current.isValid() && !current.isGlobal()) {
			result.push_back(current);
			current = getParent(current);
		}
		return result;
	}

private:
	StringHandle buildQualifiedNameForEntry(NamespaceHandle parent, StringHandle name) {
		if (!parent.isValid() || parent.isGlobal()) {
			return name;
		}

		const NamespaceEntry& parent_entry = getEntry(parent);
		StringBuilder sb;
		sb.append(StringTable::getStringView(parent_entry.qualified_name));
		sb.append("::");
		sb.append(StringTable::getStringView(name));
		return StringTable::createStringHandle(sb);
	}

	std::vector<NamespaceEntry> entries_;
	size_t max_size_reached_ = 0;
	std::unordered_map<std::pair<NamespaceHandle, StringHandle>, NamespaceHandle,
		PairHash<NamespaceHandle, StringHandle>> namespace_map_;
};

extern NamespaceRegistry gNamespaceRegistry;
