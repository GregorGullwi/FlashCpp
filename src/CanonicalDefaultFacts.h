#pragma once

#include "AstNodeTypes.h"
#include "CanonicalTypes.h"

// Default-argument exception facts. The definitions live in
// TypeTraitEvaluator.cpp so they can use the canonical constructor-selection
// service without pulling OverloadResolution.h into CanonicalTypeAdapter.h.
bool isCanonicalNothrowLiteralDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer);
bool isCanonicalKnownThrowingDefault(CanonicalTypeTable& table, const ASTNode& initializer);
bool isCanonicalKnownThrowingConversionDefault(CanonicalTypeTable& table, TypeId parameter_type, const ASTNode& initializer);
