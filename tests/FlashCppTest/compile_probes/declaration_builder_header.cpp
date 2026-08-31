// Host-compiler probe: DeclarationBuilder.h must compile without complete AST
// types. Instantiating sizeof and the telemetry accessors must not require
// TypeSpecifierNode to be complete. Compiled as a standalone translation unit.

#include "DeclarationBuilder.h"

int probe_declaration_builder_header_independence() {
	return static_cast<int>(sizeof(DeclarationBuilder));
}

std::size_t probe_declaration_builder_header_accessors(const DeclarationBuilder& builder) {
	return builder.declarationCount() + builder.entityCount() + builder.telemetryDeclaratorInternCount();
}
