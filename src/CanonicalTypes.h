#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>
#include <thread>
#include <vector>
#include <unordered_map>

#include "ArenaAccounting.h"
#include "ChunkedAnyVector.h"
#include "Log.h"
#include "CompileError.h"
#include "FrontendIds.h"
#include "TypeQualifiers.h"

enum class CanonicalBuiltinKind : uint8_t {
	Void, Bool, Char, SignedChar, UnsignedChar, WChar, Char8, Char16, Char32,
	Short, UnsignedShort, Int, UnsignedInt, Long, UnsignedLong,
	LongLong, UnsignedLongLong, Float, Double, LongDouble, Nullptr,
	Count,
};

enum class CanonicalTypeKind : uint8_t {
	Builtin, Qualified, Pointer, LValueReference, RValueReference, Array,
};

enum class CanonicalTypeNodeFlags : uint8_t {
	None = 0,
	KnownArrayBound = 1 << 0,
};

// An immutable structural node. A child is a canonical identity in this table,
// never an AST pointer, spelling, legacy TypeIndex, or telemetry key.
struct CanonicalTypeNode {
	TypeId child;
	CanonicalTypeKind kind;
	CanonicalBuiltinKind builtin;
	CVQualifier qualifiers;
	CanonicalTypeNodeFlags flags;
	uint64_t array_extent;
	friend bool operator==(CanonicalTypeNode, CanonicalTypeNode) = default;
};

static_assert(std::is_trivially_copyable_v<CanonicalTypeNode>);

struct CanonicalTypeArenaStats {
	uint64_t used_bytes;
	uint64_t reserved_bytes;
};

class CanonicalTypeTransaction;

// Boundary 3A's first family. IDs are local to one FrontendContext and are not
// portable hashes or ABI names. Equal requests in that context return one ID,
// regardless of request order. The declarator adapter imports supported families
// into this table. Declaration merging remains explicitly telemetry-only until
// later 3A work can represent every family in a signature.
// One mutex protects publication and reads; keep this boundary until the real
// structural-request trace passes the parallel-experiment handoff gates.
class CanonicalTypeTable {
	friend class CanonicalTypeTransaction;

public:
	CanonicalTypeTable() = default;
	explicit CanonicalTypeTable(SemanticArenaAccounting& accounting) : accounting_(&accounting) {}
	CanonicalTypeTable(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable& operator=(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable(CanonicalTypeTable&&) = delete;
	CanonicalTypeTable& operator=(CanonicalTypeTable&&) = delete;

	TypeId builtin(CanonicalBuiltinKind kind) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (kind >= CanonicalBuiltinKind::Count) {
			throw InternalError("canonical type: invalid builtin kind");
		}
		return internUnlocked({
			.child = TypeId{},
			.kind = CanonicalTypeKind::Builtin,
			.builtin = kind,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	TypeId qualify(TypeId type, CVQualifier qualifiers) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		CanonicalTypeNode input = nodeUnlocked(type);
		if (static_cast<uint8_t>(qualifiers) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
			throw InternalError("canonical type: invalid cv qualifiers");
		}
		// [dcl.ref]: cv-qualification introduced through a reference typedef is
		// ignored. Referent qualification remains on the child node.
		if (qualifiers == CVQualifier::None || isReference(input.kind)) {
			return type;
		}
		std::vector<CanonicalTypeNode> arrays;
		while (input.kind == CanonicalTypeKind::Array) {
			arrays.push_back(input);
			type = input.child;
			input = nodeUnlocked(type);
		}
		if (input.kind == CanonicalTypeKind::Qualified) {
			qualifiers |= input.qualifiers;
			type = input.child;
		}
		type = internUnlocked({
			.child = type,
			.kind = CanonicalTypeKind::Qualified,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = qualifiers,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
		for (auto array = arrays.rbegin(); array != arrays.rend(); ++array) {
			array->child = type;
			type = internUnlocked(*array);
		}
		return type;
	}

	TypeId pointer(TypeId pointee) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (isReference(nodeUnlocked(pointee).kind)) {
			throw InternalError("canonical type: pointer to reference");
		}
		return internUnlocked({
			.child = pointee,
			.kind = CanonicalTypeKind::Pointer,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	TypeId array(TypeId element, size_t extent) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (extent == 0) {
			throw InternalError("canonical type: zero array bound");
		}
		return arrayUnlocked(element, static_cast<uint64_t>(extent), CanonicalTypeNodeFlags::KnownArrayBound);
	}

	TypeId arrayOfUnknownBound(TypeId element) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return arrayUnlocked(element, 0, CanonicalTypeNodeFlags::None);
	}

	TypeId reference(TypeId referent, ReferenceQualifier qualifier) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		CanonicalTypeNode input = nodeUnlocked(referent);
		if (qualifier != ReferenceQualifier::LValueReference && qualifier != ReferenceQualifier::RValueReference) {
			throw InternalError("canonical type: invalid reference qualifier");
		}
		auto kind = qualifier == ReferenceQualifier::LValueReference
			? CanonicalTypeKind::LValueReference : CanonicalTypeKind::RValueReference;
		if (isReference(input.kind)) {
			// [dcl.ref] reference collapsing: only && combined with && stays &&.
			if (input.kind == CanonicalTypeKind::LValueReference) {
				kind = CanonicalTypeKind::LValueReference;
			}
			referent = input.child;
			input = nodeUnlocked(referent);
		}
		const auto base = input.kind == CanonicalTypeKind::Qualified ? nodeUnlocked(input.child) : input;
		if (base.kind == CanonicalTypeKind::Builtin && base.builtin == CanonicalBuiltinKind::Void) {
			throw InternalError("canonical type: reference to void");
		}
		return internUnlocked({
			.child = referent,
			.kind = kind,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	TypeId withoutTopLevelQualifiers(TypeId id) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(id);
		return input.kind == CanonicalTypeKind::Qualified ? input.child : id;
	}

	CanonicalTypeNode node(TypeId id) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return nodeUnlocked(id);
	}

	size_t size() const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return live_count_;
	}

	CanonicalTypeArenaStats arenaStats() const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return {static_cast<uint64_t>(live_count_) * sizeof(CanonicalTypeNode), nodes_.reservedBytes()};
	}

private:
	struct NodeHash {
		size_t operator()(CanonicalTypeNode node) const {
			const uint64_t key = static_cast<uint64_t>(node.child.value)
				| (static_cast<uint64_t>(node.kind) << 32)
				| (static_cast<uint64_t>(node.builtin) << 40)
				| (static_cast<uint64_t>(node.qualifiers) << 48)
				| (static_cast<uint64_t>(node.flags) << 56);
			const size_t first = std::hash<uint64_t>{}(key);
			const size_t second = std::hash<uint64_t>{}(node.array_extent);
			return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
		}
	};

	static bool isReference(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::LValueReference || kind == CanonicalTypeKind::RValueReference;
	}

	TypeId arrayUnlocked(TypeId element, uint64_t extent, CanonicalTypeNodeFlags flags) {
		CanonicalTypeNode element_node = nodeUnlocked(element);
		if (element_node.kind == CanonicalTypeKind::Qualified) {
			element_node = nodeUnlocked(element_node.child);
		}
		if (isReference(element_node.kind) ||
			(element_node.kind == CanonicalTypeKind::Builtin && element_node.builtin == CanonicalBuiltinKind::Void) ||
			(element_node.kind == CanonicalTypeKind::Array && element_node.flags != CanonicalTypeNodeFlags::KnownArrayBound)) {
			throw InternalError("canonical type: invalid array element type");
		}
		return internUnlocked({
			.child = element,
			.kind = CanonicalTypeKind::Array,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = flags,
			.array_extent = extent,
		});
	}

	CanonicalTypeNode nodeUnlocked(TypeId id) const {
		if (!id || id.value > live_count_) {
			throw InternalError("canonical type: TypeId is outside this table");
		}
		return nodes_[id.value - 1];
	}

	TypeId internUnlocked(CanonicalTypeNode node) {
		traceRequestUnlocked(node);
		const auto existing = ids_.find(node);
		if (existing != ids_.end()) {
			return existing->second;
		}
		if (live_count_ >= std::numeric_limits<uint32_t>::max()) {
			throw InternalError("canonical type: TypeId space exhausted");
		}
		const TypeId id{static_cast<uint32_t>(live_count_ + 1)};
		if (live_count_ == nodes_.size()) {
			nodes_.push_back(node);
		} else {
			// Reuse discarded slots; repeated failed probes must not repeatedly
			// reserve new chunks from ChunkedVector's monotonic allocator.
			nodes_[live_count_] = node;
		}
		++live_count_;
		noteArenaBytes();
		try {
			ids_.emplace(node, id);
		} catch (...) {
			--live_count_;
			noteArenaBytes();
			throw;
		}
		return id;
	}

	void noteArenaBytes() {
		if (accounting_ != nullptr) {
			accounting_->update(SemanticArenaComponent::Types, static_cast<uint64_t>(live_count_) * sizeof(CanonicalTypeNode), nodes_.reservedBytes());
		}
	}

	void checkTransactionThread() const {
		if (!transaction_marks_.empty() && transaction_owner_ != std::this_thread::get_id()) {
			throw InternalError("canonical type transaction belongs to another thread");
		}
	}

	size_t beginTransaction() {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		transaction_marks_.push_back(live_count_);
		transaction_owner_ = std::this_thread::get_id();
		return transaction_marks_.size();
	}

	void finishTransaction(size_t depth, bool commit) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (depth == 0 || depth != transaction_marks_.size()) {
			throw InternalError("canonical type transactions must finish in nesting order");
		}
		if (!commit) {
			while (live_count_ > transaction_marks_.back()) {
				ids_.erase(nodes_[live_count_ - 1]);
				--live_count_;
			}
			noteArenaBytes();
		}
		transaction_marks_.pop_back();
	}

	void traceRequestUnlocked(CanonicalTypeNode node) const {
		if (!FLASH_LOG_ENABLED(Types, Trace)) {
			return;
		}
		// Trace-local structural spelling only: never serialize numeric TypeIds.
		// Each slash-separated node is kind,builtin,cv,known-bound,extent.
		StringBuilder shape;
		for (;;) {
			shape.append(static_cast<uint64_t>(node.kind)).append(',');
			shape.append(static_cast<uint64_t>(node.builtin)).append(',');
			shape.append(static_cast<uint64_t>(node.qualifiers)).append(',');
			shape.append(static_cast<uint64_t>(node.flags)).append(',');
			shape.append(node.array_extent);
			if (!node.child) {
				break;
			}
			shape.append('/');
			node = nodeUnlocked(node.child);
		}
		FLASH_LOG(Types, Trace, "canonical-request-v2 ", shape.commit());
	}

	// The shallow architectural corpus measures 42 records: 64 slots (1,024 bytes).
	// Deep-nesting probes deliberately spill; no source-depth-sized inline array.
	static constexpr uint32_t kChunkSize = 64;
	size_t live_count_ = 0;
	SemanticArenaAccounting* accounting_ = nullptr;
	std::vector<size_t> transaction_marks_;
	std::thread::id transaction_owner_;
	mutable std::mutex mutex_;
	ChunkedVector<CanonicalTypeNode, kChunkSize> nodes_;
	std::unordered_map<CanonicalTypeNode, TypeId, NodeHash> ids_;
};

// Checkpoints publish only when the surrounding transaction commits. Nested
// commit preserves the outer checkpoint; outer rollback discards both scopes.
// IDs from a discarded probe must not escape it, as with declaration IDs.
class CanonicalTypeTransaction {
public:
	explicit CanonicalTypeTransaction(CanonicalTypeTable& table)
		: table_(table), depth_(table.beginTransaction()) {}
	~CanonicalTypeTransaction() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, false);
		}
	}
	CanonicalTypeTransaction(const CanonicalTypeTransaction&) = delete;
	CanonicalTypeTransaction& operator=(const CanonicalTypeTransaction&) = delete;
	void commit() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, true);
			depth_ = 0;
		}
	}
	void rollback() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, false);
			depth_ = 0;
		}
	}
private:
	CanonicalTypeTable& table_;
	size_t depth_;
};

static_assert(sizeof(CanonicalTypeTransaction) == 2 * sizeof(void*));
