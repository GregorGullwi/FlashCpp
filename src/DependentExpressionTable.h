#pragma once

#include "AstNodeTypes.h"
#include "ChunkedAnyVector.h"
#include "CompileError.h"
#include "FrontendIds.h"
#include "TemplateExpressionEquivalence.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Context-local structural identity for dependent unevaluated expressions.
// ExprId slots are stable for the lifetime of the owning FrontendContext; this
// table does not participate in canonical-type transactions.
class DependentExpressionTable {
public:
	static constexpr size_t kExprArenaChunkSize = 64;

	ExprId intern(const ASTNode& node) {
		if (!node.has_value()) {
			throw InternalError("dependent expression: empty ASTNode");
		}
		const size_t hash = FlashCpp::hashDependentExpressionIdentity(node);
		std::vector<uint32_t>& bucket = buckets_[hash];
		for (const uint32_t existing : bucket) {
			if (FlashCpp::equalDependentExpressionIdentity(nodes_[existing - 1u], node)) {
				return ExprId{existing};
			}
		}
		nodes_.push_back(node);
		const uint32_t id = static_cast<uint32_t>(nodes_.size());
		if (id == 0) {
			throw InternalError("dependent expression: ExprId overflow");
		}
		bucket.push_back(id);
		return ExprId{id};
	}

	size_t size() const {
		return nodes_.size();
	}

private:
	ChunkedVector<ASTNode, kExprArenaChunkSize> nodes_;
	std::unordered_map<size_t, std::vector<uint32_t>> buckets_;
};
