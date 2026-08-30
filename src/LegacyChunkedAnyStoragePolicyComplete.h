#pragma once

#include "AstNodeTypes_Expr.h"
#include "LegacyChunkedAnyStoragePolicy.h"

// Completes the guarded-storage policy after ExpressionNode is defined.
// Include from CompilerIncludes.h after AstNodeTypes.h, not from AstNodeTypes.h.

template<typename T>
inline constexpr bool isLegacyChunkedAnyStorageType =
	detail::isLegacyChunkedAnyStorageTypeFor<std::decay_t<T>> ||
	std::is_same_v<std::decay_t<T>, ExpressionNode>;

template<typename T>
struct LegacyChunkedAnyStorageTraits<T, true> {
	static constexpr bool allowed = isLegacyChunkedAnyStorageType<std::decay_t<T>>;
};

template<typename T, bool EnforceLegacyAstAllowList>
constexpr void requireLegacyAstChunkedAnyEmplaceAllowed() {
	if constexpr (EnforceLegacyAstAllowList) {
		static_assert(
			LegacyChunkedAnyStorageTraits<std::decay_t<T>, true>::allowed,
			"Type is not on the legacy ChunkedAnyVector allow-list. "
			"Allocate new semantic objects in FrontendContext typed arenas instead.");
	}
}
