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
#include <unordered_map>
#include <vector>

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

	// Only this batch's newly created identities are provisional. Required template
	// instantiation performed before publication is outside this identity transaction.
	std::vector<ExprId> internBatch(std::span<const ASTNode> expressions) {
		std::vector<ExprId> identities;
		std::vector<size_t> inserted_hashes;
		identities.reserve(expressions.size());
		inserted_hashes.reserve(expressions.size());
		try {
			for (const ASTNode& expression : expressions) {
				if (!expression.has_value()) throw InternalError("dependent expression: empty ASTNode");
				const size_t hash = FlashCpp::hashDependentExpressionIdentity(expression);
				const uint32_t previous_count = node_count_;
				const ExprId identity = internHashed(expression, hash);
				if (node_count_ != previous_count) inserted_hashes.push_back(hash);
				identities.push_back(identity);
			}
		} catch (...) {
			rollbackBatch(inserted_hashes);
			throw;
		}
		return identities;
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

	void rollbackBatch(std::span<const size_t> inserted_hashes) {
		for (auto hash = inserted_hashes.rbegin(); hash != inserted_hashes.rend(); ++hash) {
			const auto bucket = buckets_.find(*hash);
			if (bucket == buckets_.end() || bucket->second.empty() || bucket->second.back() != node_count_) {
				throw InternalError("dependent expression: invalid batch rollback order");
			}
			bucket->second.pop_back();
			if (bucket->second.empty()) buckets_.erase(bucket);
			nodes_[--node_count_] = ASTNode{};
		}
	}

	// Retain rolled-back arena slots for reuse; repeatedly failing batches must not
	// grow the monotonic allocator by discarding and reallocating chunks.
	ChunkedVector<ASTNode, kExprArenaChunkSize> nodes_;
	std::unordered_map<size_t, std::vector<uint32_t>> buckets_;
	uint32_t node_count_ = 0;
};
