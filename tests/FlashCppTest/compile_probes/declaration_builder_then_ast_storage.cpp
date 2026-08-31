// Host-compiler probe: including DeclarationBuilder.h before AstNodeTypes.h
// must not freeze the primary guarded-storage trait. After AST types are
// complete, legacy emplace specializations must be visible.

#include "DeclarationBuilder.h"
#include "AstNodeTypes.h"

static_assert(LegacyChunkedAnyStorageTraits<IdentifierNode, true>::allowed);
static_assert(LegacyChunkedAnyStorageTraits<FunctionCallableTypes, true>::allowed);

int probe_declaration_builder_then_ast_guarded_storage() {
	requireLegacyAstChunkedAnyEmplaceAllowed<IdentifierNode, true>();
	requireLegacyAstChunkedAnyEmplaceAllowed<FunctionCallableTypes, true>();
	return 0;
}
