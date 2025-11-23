#pragma once

#include "AstNodeTypes.h"
#include "TemplateRegistry.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

// Concept registry for storing and looking up C++20 concept declarations
// Concepts are named constraints that can be used to constrain template parameters
class ConceptRegistry {
public:
	ConceptRegistry() = default;

	// Register a concept declaration
	// concept_name: The name of the concept (e.g., "Integral", "Addable")
	// concept_node: The ConceptDeclarationNode AST node
	void registerConcept(std::string_view concept_name, ASTNode concept_node) {
		std::string key(concept_name);
		concepts_[key] = concept_node;
	}

	// Look up a concept by name
	// Returns the ConceptDeclarationNode if found, std::nullopt otherwise
	std::optional<ASTNode> lookupConcept(std::string_view concept_name) const {
		auto it = concepts_.find(concept_name);
		if (it != concepts_.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	// Check if a concept exists
	bool hasConcept(std::string_view concept_name) const {
		return concepts_.find(concept_name) != concepts_.end();
	}

	// Clear all concepts (for testing)
	void clear() {
		concepts_.clear();
	}

	// Get all concept names (for debugging)
	std::vector<std::string> getAllConceptNames() const {
		std::vector<std::string> names;
		names.reserve(concepts_.size());
		for (const auto& pair : concepts_) {
			names.push_back(pair.first);
		}
		return names;
	}

private:
	// Map from concept name to ConceptDeclarationNode
	// Using TransparentStringHash for heterogeneous lookup with string_view
	std::unordered_map<std::string, ASTNode, TransparentStringHash, std::equal_to<>> concepts_;
};

// Global concept registry
extern ConceptRegistry gConceptRegistry;
