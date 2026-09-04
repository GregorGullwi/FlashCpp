#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>
#include <unordered_map>

#include "ChunkedAnyVector.h"
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
	Builtin, Qualified, Pointer, LValueReference, RValueReference,
};

// An immutable structural node. A child is a canonical identity in this table,
// never an AST pointer, spelling, legacy TypeIndex, or telemetry key.
struct CanonicalTypeNode {
	TypeId child;
	CanonicalTypeKind kind;
	CanonicalBuiltinKind builtin;
	CVQualifier qualifiers;
	uint8_t reserved;
	friend bool operator==(CanonicalTypeNode, CanonicalTypeNode) = default;
};

static_assert(sizeof(CanonicalTypeNode) == 8);
static_assert(std::is_trivially_copyable_v<CanonicalTypeNode>);

struct CanonicalTypeArenaStats {
	uint64_t used_bytes;
	uint64_t reserved_bytes;
};

// Boundary 3A's first family. IDs are local to one FrontendContext and are not
// portable hashes or ABI names. Equal requests in that context return one ID,
// regardless of request order. No production parser path is migrated yet;
// DeclarationBuilder's flat bridge remains explicitly telemetry-only until the
// later 3A declarator adapter can represent every family in a signature.
// One mutex protects publication and reads; keep this boundary until the real
// structural-request trace passes the parallel-experiment handoff gates.
class CanonicalTypeTable {
public:
	CanonicalTypeTable() = default;
	CanonicalTypeTable(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable& operator=(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable(CanonicalTypeTable&&) = delete;
	CanonicalTypeTable& operator=(CanonicalTypeTable&&) = delete;

	TypeId builtin(CanonicalBuiltinKind kind) {
		std::lock_guard lock(mutex_);
		if (kind >= CanonicalBuiltinKind::Count) {
			throw InternalError("canonical type: invalid builtin kind");
		}
		return internUnlocked({TypeId{}, CanonicalTypeKind::Builtin, kind, CVQualifier::None, 0});
	}

	TypeId qualify(TypeId type, CVQualifier qualifiers) {
		std::lock_guard lock(mutex_);
		const CanonicalTypeNode input = nodeUnlocked(type);
		if (static_cast<uint8_t>(qualifiers) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
			throw InternalError("canonical type: invalid cv qualifiers");
		}
		// [dcl.ref]: cv-qualification introduced through a reference typedef is
		// ignored. Referent qualification remains on the child node.
		if (qualifiers == CVQualifier::None || isReference(input.kind)) {
			return type;
		}
		if (input.kind == CanonicalTypeKind::Qualified) {
			qualifiers |= input.qualifiers;
			type = input.child;
		}
		return internUnlocked({type, CanonicalTypeKind::Qualified, CanonicalBuiltinKind::Void, qualifiers, 0});
	}

	TypeId pointer(TypeId pointee) {
		std::lock_guard lock(mutex_);
		if (isReference(nodeUnlocked(pointee).kind)) {
			throw InternalError("canonical type: pointer to reference");
		}
		return internUnlocked({pointee, CanonicalTypeKind::Pointer, CanonicalBuiltinKind::Void, CVQualifier::None, 0});
	}

	TypeId reference(TypeId referent, ReferenceQualifier qualifier) {
		std::lock_guard lock(mutex_);
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
		return internUnlocked({referent, kind, CanonicalBuiltinKind::Void, CVQualifier::None, 0});
	}

	CanonicalTypeNode node(TypeId id) const {
		std::lock_guard lock(mutex_);
		return nodeUnlocked(id);
	}

	size_t size() const {
		std::lock_guard lock(mutex_);
		return nodes_.size();
	}

	CanonicalTypeArenaStats arenaStats() const {
		std::lock_guard lock(mutex_);
		return {nodes_.usedBytes(), nodes_.reservedBytes()};
	}

private:
	struct NodeHash {
		size_t operator()(CanonicalTypeNode node) const {
			const uint64_t key = static_cast<uint64_t>(node.child.value)
				| (static_cast<uint64_t>(node.kind) << 32)
				| (static_cast<uint64_t>(node.builtin) << 40)
				| (static_cast<uint64_t>(node.qualifiers) << 48);
			return std::hash<uint64_t>{}(key);
		}
	};

	static bool isReference(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::LValueReference || kind == CanonicalTypeKind::RValueReference;
	}

	CanonicalTypeNode nodeUnlocked(TypeId id) const {
		if (!id || id.value > nodes_.size()) {
			throw InternalError("canonical type: TypeId is outside this table");
		}
		return nodes_[id.value - 1];
	}

	TypeId internUnlocked(CanonicalTypeNode node) {
		const auto existing = ids_.find(node);
		if (existing != ids_.end()) {
			return existing->second;
		}
		if (nodes_.size() >= std::numeric_limits<uint32_t>::max()) {
			throw InternalError("canonical type: TypeId space exhausted");
		}
		const TypeId id{static_cast<uint32_t>(nodes_.size() + 1)};
		nodes_.push_back(node);
		try {
			ids_.emplace(node, id);
		} catch (...) {
			nodes_.pop_back();
			throw;
		}
		return id;
	}

	// The shallow architectural corpus measures 31 records: 32 slots (256 bytes).
	// Deep-nesting probes deliberately spill; no source-depth-sized inline array.
	static constexpr uint32_t kChunkSize = 32;
	mutable std::mutex mutex_;
	ChunkedVector<CanonicalTypeNode, kChunkSize> nodes_;
	std::unordered_map<CanonicalTypeNode, TypeId, NodeHash> ids_;
};
