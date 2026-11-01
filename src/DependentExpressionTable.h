#pragma once

#include "AstNodeTypes.h"
#include "ChunkedAnyVector.h"
#include "CompileError.h"
#include "FrontendIds.h"
#include "TemplateExpressionEquivalence.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

// Caller-owned temporary workspace; committed AST handles are copied into the table.
struct DependentExpressionBatchEntry {
	ASTNode expression;
	ExprId identity{};
};

static_assert(std::is_trivially_destructible_v<DependentExpressionBatchEntry>);

// Context-local structural identity for dependent unevaluated expressions.
// ExprId slots are stable for the lifetime of the owning FrontendContext; this
// table does not participate in canonical-type transactions.
class DependentExpressionTable {
public:
	static constexpr size_t kExprArenaChunkSize = 64;

	ExprId intern(const ASTNode& node) {
		if (!node.has_value()) throw InternalError("dependent expression: empty ASTNode");
		return internHashed(node, FlashCpp::hashDependentExpressionIdentity(node));
	}

	// Caller supplies empty identity slots. Only this batch's newly created IDs are
	// provisional; required template instantiation precedes this publication step.
	void internBatch(std::span<DependentExpressionBatchEntry> expressions) {
		for (const DependentExpressionBatchEntry& entry : expressions) {
			if (entry.identity) throw InternalError("dependent expression: batch output is not empty");
			if (!entry.expression.has_value()) throw InternalError("dependent expression: empty ASTNode");
		}
		const uint32_t original_count = node_count_;
		try {
			for (DependentExpressionBatchEntry& entry : expressions) entry.identity = intern(entry.expression);
		} catch (...) {
			rollbackBatch(original_count);
			for (DependentExpressionBatchEntry& entry : expressions) entry.identity = ExprId{};
			throw;
		}
	}

	size_t size() const {
		return node_count_;
	}

	const ASTNode& node(ExprId expression) const {
		if (!expression || expression.value > node_count_) {
			throw InternalError("dependent expression: invalid ExprId");
		}
		return nodes_[expression.value - 1u];
	}

private:
	ExprId internHashed(const ASTNode& node, size_t hash) {
		auto [bucket_it, inserted] = buckets_.try_emplace(hash);
		std::vector<uint32_t>& bucket = bucket_it->second;
		for (const uint32_t existing : bucket) {
			if (FlashCpp::equalDependentExpressionIdentity(nodes_[existing - 1u], node)) return ExprId{existing};
		}
		if (node_count_ == std::numeric_limits<uint32_t>::max()) {
			if (inserted) buckets_.erase(bucket_it);
			throw InternalError("dependent expression: ExprId overflow");
		}
		const uint32_t id = node_count_ + 1;
		try {
			bucket.push_back(id);
			try {
				if (node_count_ == nodes_.size()) nodes_.push_back(node);
				else nodes_[node_count_] = node;
			} catch (...) {
				bucket.pop_back();
				throw;
			}
		} catch (...) {
			if (inserted && bucket.empty()) buckets_.erase(bucket_it);
			throw;
		}
		++node_count_;
		return ExprId{id};
	}

	// Identity-publication failures are exceptional. Scanning buckets here avoids
	// allocating an insertion journal for every successful batch.
	void rollbackBatch(uint32_t original_count) {
		if (node_count_ == original_count) return;
		if (original_count > node_count_) throw InternalError("dependent expression: invalid batch rollback count");
		for (auto bucket = buckets_.begin(); bucket != buckets_.end();) {
			std::vector<uint32_t>& identities = bucket->second;
			while (!identities.empty() && identities.back() > original_count) identities.pop_back();
			if (identities.empty()) bucket = buckets_.erase(bucket);
			else ++bucket;
		}
		while (node_count_ > original_count) nodes_[--node_count_] = ASTNode{};
	}

	// Retain rolled-back arena slots for reuse; repeatedly failing batches must not
	// grow the monotonic allocator by discarding and reallocating chunks.
	ChunkedVector<ASTNode, kExprArenaChunkSize> nodes_;
	std::unordered_map<size_t, std::vector<uint32_t>> buckets_;
	uint32_t node_count_ = 0;
};
