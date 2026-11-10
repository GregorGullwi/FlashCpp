#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"

// Default-argument exception facts. The definitions live in
// TypeTraitEvaluator.cpp so they can use the canonical constructor-selection
// service without pulling OverloadResolution.h into CanonicalTypeAdapter.h.
bool isCanonicalNothrowLiteralDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer);
bool isCanonicalKnownThrowingDefault(CanonicalTypeTable& table, const ASTNode& initializer);
bool isCanonicalKnownThrowingConversionDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer);

// Canonical, cached expression-level exception fact. The expression is interned
// to a stable ExprId, an existing fact is consumed first, and a positive result
// is published to the canonical expression-fact table. Unknown stays absent.
CanonicalDefaultException canonicalExpressionExceptionFact(CanonicalTypeTable& table, const ASTNode& expression);
