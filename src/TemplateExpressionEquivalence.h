#pragma once

#include "AstNodeTypes.h"
#include <cstddef>

namespace FlashCpp {

// Central semantic identity hooks for dependent unevaluated template expressions.
// Template instantiation keys and related NTTP identity logic should route through
// this layer instead of ad hoc AST serialization or local shape checks.
bool equalDependentExpressionIdentity(const ASTNode& lhs, const ASTNode& rhs);
size_t hashDependentExpressionIdentity(const ASTNode& node);

bool equalTemplateArgInfoListIdentity(
	std::span<const TypeInfo::TemplateArgInfo> lhs,
	std::span<const TypeInfo::TemplateArgInfo> rhs);
size_t hashTemplateArgInfoListIdentity(std::span<const TypeInfo::TemplateArgInfo> args);

} // namespace FlashCpp
