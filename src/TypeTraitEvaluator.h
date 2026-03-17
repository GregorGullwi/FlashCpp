// TypeTraitEvaluator.h - Shared type trait evaluation logic
// This file provides a unified interface for evaluating type traits
// at both compile time (Parser.cpp) and code generation time (IrGenerator.h)

#pragma once

#include "AstNodeTypes.h"
#include <optional>

// Result type for type trait evaluation
struct TypeTraitResult {
	bool success;
	bool value;

	static TypeTraitResult success_true() { return {true, true}; }
	static TypeTraitResult success_false() { return {true, false}; }
	static TypeTraitResult failure() { return {false, false}; }
};

// Shared helper functions for type checking in an isolated namespace
// to avoid conflicts with similar functions in other headers
namespace TypeTraitEval {

bool isArithmeticType(Type type);
bool isFundamentalType(Type type);
bool isScalarType(Type type, bool is_reference, size_t pointer_depth);
bool isIntegral(Type type);
bool isFloatingPoint(Type type);
bool isSigned(Type type);
bool isUnsigned(Type type);

} // namespace TypeTraitEval

// Evaluate a DestructorDeclarationNode's effective noexcept status.
// Handles the implicit default (true) and evaluates an explicit noexcept(expr)
// using a minimal constexpr evaluation context.  Shared by TypeTraitEvaluator,
// IrGenerator, and ConstExprEvaluator so the logic lives in one place.
bool evaluateDestructorNoexcept(const DestructorDeclarationNode& dtor_node);

// Main type trait evaluation functions
TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	Type base_type,
	TypeIndex type_idx,
	bool is_reference,
	bool is_rvalue_reference,
	bool is_lvalue_reference,
	size_t pointer_depth,
	CVQualifier cv_qualifier,
	bool is_array,
	std::optional<size_t> array_size,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
);

TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
);
