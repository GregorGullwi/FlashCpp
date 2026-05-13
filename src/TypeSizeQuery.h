#pragma once

#include "AstNodeTypes.h"
#include "CompileError.h"
#include "StringBuilder.h"

#include <string_view>

struct ConcreteTypeSizeQueryResult {
	int size_bits = 0;
	TypeIndex incomplete_alias_type_index{};

	bool hasSize() const { return size_bits > 0; }
	bool hasIncompleteAliasType() const { return incomplete_alias_type_index.is_valid(); }
};

// Shared sema/codegen query for places that require a concrete object size after
// aliases have been chased to their terminal type.  The fallback order preserves
// the historical codegen behavior used by reference and return-value lowering:
//
//   1. TypeInfo/getTypeSpecSizeBits size (covers stored TypeInfo sizes and
//      scalar aliases);
//   2. direct struct layout size for the current TypeIndex;
//   3. alias terminal TypeInfo size;
//   4. TypeSpecifier-size after rebinding to the alias terminal TypeIndex;
//   5. alias terminal struct layout size, diagnosing incomplete alias structs.
inline ConcreteTypeSizeQueryResult queryConcreteAliasResolvedTypeSizeBits(const TypeSpecifierNode& type_spec) {
	ConcreteTypeSizeQueryResult result;

	const int resolved_size = getTypeSpecSizeBits(type_spec);
	if (resolved_size > 0) {
		result.size_bits = resolved_size;
		return result;
	}

	if (!type_spec.type_index().is_valid()) {
		return result;
	}

	if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(type_spec.type_index())) {
		const int struct_size_bits = static_cast<int>(struct_info->sizeInBits().value);
		if (struct_size_bits > 0) {
			result.size_bits = struct_size_bits;
			return result;
		}
	}

	const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(type_spec.type_index());
	if (!alias_info.type_index.is_valid()) {
		return result;
	}

	if (const TypeInfo* alias_type_info = tryGetTypeInfo(alias_info.type_index)) {
		const int alias_size_bits = static_cast<int>(alias_type_info->sizeInBits().value);
		if (alias_size_bits > 0) {
			result.size_bits = alias_size_bits;
			return result;
		}
	}

	TypeSpecifierNode alias_resolved_type = type_spec;
	alias_resolved_type.set_type_index(alias_info.type_index);
	alias_resolved_type.set_category(alias_info.typeEnum());
	const int alias_spec_size_bits = getTypeSpecSizeBits(alias_resolved_type);
	if (alias_spec_size_bits > 0) {
		result.size_bits = alias_spec_size_bits;
		return result;
	}

	if (const StructTypeInfo* alias_struct_info = tryGetStructTypeInfo(alias_info.type_index)) {
		const int struct_size_bits = static_cast<int>(alias_struct_info->sizeInBits().value);
		if (struct_size_bits > 0) {
			result.size_bits = struct_size_bits;
			return result;
		}
		result.incomplete_alias_type_index = alias_info.type_index;
	}

	return result;
}

inline int requireConcreteAliasResolvedTypeSizeBits(const TypeSpecifierNode& type_spec, std::string_view context) {
	const ConcreteTypeSizeQueryResult query = queryConcreteAliasResolvedTypeSizeBits(type_spec);
	if (query.hasSize()) {
		return query.size_bits;
	}

	if (query.hasIncompleteAliasType()) {
		throw CompileError(std::string(StringBuilder()
											.append("Incomplete or unspecialized alias type (type_index=")
											.append(static_cast<int64_t>(query.incomplete_alias_type_index.index()))
											.append(") in ")
											.append(context)
											.commit()));
	}

	throw InternalError(std::string(StringBuilder()
										.append("Type with no runtime size reached codegen in ")
										.append(context)
										.append(" (type=")
										.append(static_cast<int64_t>(type_spec.type()))
										.append(", pointer_depth=")
										.append(static_cast<int64_t>(type_spec.pointer_depth()))
										.append(")")
										.commit()));
}
