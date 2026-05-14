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

inline int queryCompleteTypeInfoObjectSizeBits(const TypeInfo& type_info) {
	if (const StructTypeInfo* struct_info = type_info.getStructInfo()) {
		if (struct_info->hasCompleteObjectLayout()) {
			return static_cast<int>(struct_info->sizeInBits().value);
		}
		// Some template-instantiation paths preserve the already-computed object
		// size in the TypeInfo fallback slot while the StructTypeInfo is still a
		// lazy/incomplete placeholder.  The pre-refactor sizing path consulted
		// TypeInfo::sizeInBits() directly and therefore accepted this legacy
		// fallback; keep that behavior for codegen queries instead of diagnosing
		// such aliases as incomplete.
		return type_info.fallback_size_bits_ > 0 ? type_info.fallback_size_bits_ : 0;
	}
	return type_info.hasStoredSize()
		? static_cast<int>(type_info.sizeInBits().value)
		: 0;
}

inline int queryTemplateArgInfoObjectSizeBits(const TypeInfo::TemplateArgInfo& arg_info) {
	if (arg_info.is_value) {
		return 0;
	}
	if (arg_info.pointer_depth > 0 ||
		arg_info.ref_qualifier != ReferenceQualifier::None ||
		arg_info.function_signature.has_value()) {
		return 64;
	}
	if (arg_info.is_array) {
		int element_size_bits = 0;
		TypeInfo::TemplateArgInfo element_arg = arg_info;
		element_arg.is_array = false;
		element_arg.array_dimensions.clear();
		element_size_bits = queryTemplateArgInfoObjectSizeBits(element_arg);
		if (element_size_bits <= 0 || arg_info.array_dimensions.empty()) {
			return element_size_bits;
		}
		size_t element_count = 1;
		for (size_t extent : arg_info.array_dimensions) {
			element_count *= extent;
		}
		return static_cast<int>(element_size_bits * element_count);
	}
	const TypeCategory category = arg_info.category();
	if (is_builtin_type(category)) {
		return get_type_size_bits(category);
	}
	if (const TypeInfo* arg_type_info = tryGetTypeInfo(arg_info.type_index)) {
		const int arg_size_bits = queryCompleteTypeInfoObjectSizeBits(*arg_type_info);
		if (arg_size_bits > 0) {
			return arg_size_bits;
		}
	}
	if (!needs_type_index(category)) {
		return get_type_size_bits(category);
	}
	return 0;
}

inline int queryInstantiationContextBindingSizeBits(
	const TypeInfo::InstantiationContext* context,
	StringHandle name) {
	for (const TypeInfo::InstantiationContext* current = context;
		 current != nullptr;
		 current = current->parent) {
		for (const auto& binding : current->bindings) {
			if (binding.name == name && !binding.args.empty()) {
				const int binding_size_bits = queryTemplateArgInfoObjectSizeBits(binding.args.front());
				if (binding_size_bits > 0) {
					return binding_size_bits;
				}
			}
		}
		for (size_t i = 0; i < current->param_names.size() && i < current->param_args.size(); ++i) {
			if (current->param_names[i] == name) {
				const int binding_size_bits = queryTemplateArgInfoObjectSizeBits(current->param_args[i]);
				if (binding_size_bits > 0) {
					return binding_size_bits;
				}
			}
		}
	}
	return 0;
}

inline int queryDependentPlaceholderLegacySizeBits(const TypeInfo& type_info) {
	if (!type_info.isDependentPlaceholder()) {
		return 0;
	}
	if (const int context_size_bits =
			queryInstantiationContextBindingSizeBits(type_info.instantiationContext(), type_info.name());
		context_size_bits > 0) {
		return context_size_bits;
	}
	if (type_info.fallback_size_bits_ > 0) {
		return type_info.fallback_size_bits_;
	}
	const TypeCategory category = type_info.typeEnum();
	if (is_builtin_type(category)) {
		return get_type_size_bits(category);
	}
	// A dependent placeholder reaching codegen is normally a substitution bug, but
	// several legacy instantiation paths intentionally deferred placeholder sizing
	// and let lowering use an int-sized object slot until the concrete alias/member
	// was materialized.  Preserve that behavior for unresolved dependent
	// placeholders so the shared helper does not turn deferrable template cases
	// into hard internal errors.
	return 32;
}

inline void applyResolvedAliasShapeForSizeQuery(TypeSpecifierNode& target, const ResolvedAliasTypeInfo& alias_info) {
	if (alias_info.cv_qualifier != CVQualifier::None) {
		target.add_cv_qualifier(alias_info.cv_qualifier);
	}
	target.add_pointer_levels(static_cast<int>(alias_info.pointer_depth));
	if (alias_info.reference_qualifier == ReferenceQualifier::LValueReference) {
		target.set_reference_qualifier(ReferenceQualifier::LValueReference);
	} else if (alias_info.reference_qualifier == ReferenceQualifier::RValueReference &&
			   target.reference_qualifier() == ReferenceQualifier::None) {
		target.set_reference_qualifier(ReferenceQualifier::RValueReference);
	}
	if (!alias_info.array_dimensions.empty()) {
		const std::span<const size_t> target_dimensions = target.array_dimensions();
		std::vector<size_t> array_dimensions(target_dimensions.begin(), target_dimensions.end());
		array_dimensions.insert(
			array_dimensions.end(),
			alias_info.array_dimensions.begin(),
			alias_info.array_dimensions.end());
		target.set_array_dimensions(array_dimensions);
	}
	if (!target.has_function_signature() && alias_info.function_signature.has_value()) {
		target.set_function_signature(*alias_info.function_signature);
	}
}

inline ConcreteTypeSizeQueryResult queryConcreteAliasResolvedTypeSizeBitsImpl(const TypeSpecifierNode& type_spec, int depth_remaining) {
	ConcreteTypeSizeQueryResult result;
	if (depth_remaining <= 0) {
		return result;
	}

	const int resolved_size = getTypeSpecSizeBits(type_spec);
	if (resolved_size > 0) {
		result.size_bits = resolved_size;
		return result;
	}

	if (!type_spec.type_index().is_valid()) {
		if (type_spec.type_index().category() == TypeCategory::UserDefined ||
			type_spec.type() == TypeCategory::UserDefined) {
			result.size_bits = 64;
		}
		return result;
	}

	if (const TypeInfo* direct_type_info = tryGetTypeInfo(type_spec.type_index())) {
		if (const TypeSpecifierNode* alias_type_spec = direct_type_info->aliasTypeSpecifier()) {
			ConcreteTypeSizeQueryResult alias_spec_result =
				queryConcreteAliasResolvedTypeSizeBitsImpl(*alias_type_spec, depth_remaining - 1);
			if (alias_spec_result.hasSize() || alias_spec_result.hasIncompleteAliasType()) {
				return alias_spec_result;
			}
		}

		const int direct_type_info_size_bits = queryCompleteTypeInfoObjectSizeBits(*direct_type_info);
		if (direct_type_info_size_bits > 0) {
			result.size_bits = direct_type_info_size_bits;
			return result;
		}
		if (const int dependent_placeholder_size_bits =
				queryDependentPlaceholderLegacySizeBits(*direct_type_info);
			dependent_placeholder_size_bits > 0) {
			result.size_bits = dependent_placeholder_size_bits;
			return result;
		}
	}

	const ResolvedAliasTypeInfo alias_info = resolveAliasTypeInfo(type_spec.type_index());
	if (!alias_info.type_index.is_valid()) {
		return result;
	}

	TypeSpecifierNode alias_resolved_type = type_spec;
	alias_resolved_type.set_type_index(alias_info.type_index.withCategory(alias_info.typeEnum()));
	alias_resolved_type.set_category(alias_info.typeEnum());
	applyResolvedAliasShapeForSizeQuery(alias_resolved_type, alias_info);
	const int alias_spec_size_bits = getTypeSpecSizeBits(alias_resolved_type);
	if (alias_spec_size_bits > 0) {
		result.size_bits = alias_spec_size_bits;
		return result;
	}

	if (const TypeInfo* alias_type_info = alias_info.terminal_type_info ? alias_info.terminal_type_info : tryGetTypeInfo(alias_info.type_index)) {
		if (const TypeSpecifierNode* alias_type_spec = alias_type_info->aliasTypeSpecifier()) {
			ConcreteTypeSizeQueryResult alias_spec_result =
				queryConcreteAliasResolvedTypeSizeBitsImpl(*alias_type_spec, depth_remaining - 1);
			if (alias_spec_result.hasSize() || alias_spec_result.hasIncompleteAliasType()) {
				return alias_spec_result;
			}
		}

		const int alias_size_bits = queryCompleteTypeInfoObjectSizeBits(*alias_type_info);
		if (alias_size_bits > 0) {
				result.size_bits = alias_size_bits;
				return result;
		}
		if (const int dependent_placeholder_size_bits =
				queryDependentPlaceholderLegacySizeBits(*alias_type_info);
			dependent_placeholder_size_bits > 0) {
			result.size_bits = dependent_placeholder_size_bits;
			return result;
		}
	}

	if (const StructTypeInfo* alias_struct_info = tryGetStructTypeInfo(alias_info.type_index)) {
		if (alias_struct_info->hasCompleteObjectLayout()) {
			const int struct_size_bits = static_cast<int>(alias_struct_info->sizeInBits().value);
			result.size_bits = struct_size_bits;
			return result;
		}
		if (const TypeInfo* incomplete_alias_info = tryGetTypeInfo(alias_info.type_index)) {
			const int fallback_size_bits =
				incomplete_alias_info->fallback_size_bits_ > 0
					? incomplete_alias_info->fallback_size_bits_
					: 64;
			result.size_bits = fallback_size_bits;
			return result;
		}
		result.incomplete_alias_type_index = alias_info.type_index;
	}

	return result;
}

// Shared sema/codegen query for places that require a concrete object size after
// aliases have been chased to their terminal type.  The helper sizes the full
// type denoted by an alias, including alias-owned pointer/reference/function and
// array shape.  Dependent-member placeholders that have been materialized to a
// concrete alias TypeSpecifier are resolved through that TypeSpecifier.  For
// deferred dependent/alias placeholders that still have no complete layout, the
// query preserves the legacy fallback size instead of hard-failing codegen.
inline ConcreteTypeSizeQueryResult queryConcreteAliasResolvedTypeSizeBits(const TypeSpecifierNode& type_spec) {
	return queryConcreteAliasResolvedTypeSizeBitsImpl(type_spec, 64);
}

inline TypeSpecifierNode makeStaticMemberSizeQueryTypeSpecifier(const StructStaticMember& member) {
	TypeSpecifierNode member_type(
		member.type_index.withCategory(member.memberType()),
		SizeInBits{static_cast<int>(member.size * 8)},
		Token{},
		member.cv_qualifier,
		member.reference_qualifier);
	member_type.add_pointer_levels(member.pointer_depth);
	if (member.is_array) {
		member_type.set_array_dimensions(member.array_dimensions);
	}
	return member_type;
}

inline int queryConcreteAliasResolvedStaticMemberSizeBits(const StructStaticMember& member) {
	TypeSpecifierNode member_type = makeStaticMemberSizeQueryTypeSpecifier(member);
	const int size_bits = queryConcreteAliasResolvedTypeSizeBits(member_type).size_bits;
	if (size_bits > 0) {
		return size_bits;
	}
	return get_type_size_bits(resolve_type_alias(member.type_index));
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

	StringBuilder message;
	message.append("Type with no runtime size reached codegen in ")
		.append(context)
		.append(" (type=")
		.append(static_cast<int64_t>(type_spec.type()))
		.append(", type_index=")
		.append(type_spec.type_index().is_valid()
					? static_cast<int64_t>(type_spec.type_index().index())
					: static_cast<int64_t>(-1))
		.append(", type_index_category=")
		.append(static_cast<int64_t>(type_spec.type_index().category()))
		.append(", pointer_depth=")
		.append(static_cast<int64_t>(type_spec.pointer_depth()));
	if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
		message.append(", type_name=")
			.append(type_info->name())
			.append(", is_alias=")
			.append(type_info->isTypeAlias() ? static_cast<int64_t>(1) : static_cast<int64_t>(0))
			.append(", is_dependent=")
			.append(type_info->isDependentPlaceholder() ? static_cast<int64_t>(1) : static_cast<int64_t>(0))
			.append(", has_alias_spec=")
			.append(type_info->aliasTypeSpecifier() ? static_cast<int64_t>(1) : static_cast<int64_t>(0));
	}
	message.append(")");
	throw InternalError(std::string(message.commit()));
	/*throw InternalError(std::string(StringBuilder()
										.append("Type with no runtime size reached codegen in ")
										.append(context)
										.append(" (type=")
										.append(static_cast<int64_t>(type_spec.type()))
										.append(", type_index=")
										.append(type_spec.type_index().is_valid()
													? static_cast<int64_t>(type_spec.type_index().index())
													: static_cast<int64_t>(-1))
										.append(", type_index_category=")
										.append(static_cast<int64_t>(type_spec.type_index().category()))
										.append(", pointer_depth=")
										.append(static_cast<int64_t>(type_spec.pointer_depth()))
										.append(")")
										.commit()));*/
}

inline TypeCategory requireConcreteAliasResolvedCodegenTypeCategory(const TypeSpecifierNode& type_spec, std::string_view context) {
	TypeCategory type = type_spec.type();
	if (!isPlaceholderAutoType(type)) {
		return type;
	}
	if (type_spec.type_index().is_valid()) {
		if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
			TypeCategory resolved_type = type_info->typeEnum();
			if (!isPlaceholderAutoType(resolved_type) && resolved_type != TypeCategory::Invalid) {
				return resolved_type;
			}
		}
	}
	throw InternalError(std::string(StringBuilder()
										.append("Unresolved placeholder type reached codegen in ")
										.append(context)
										.append(" (type=")
										.append(static_cast<int64_t>(type))
										.append(")")
										.commit()));
}

inline TypeSpecifierNode makeConcreteAliasResolvedCodegenSizeQueryType(const TypeSpecifierNode& type_spec, std::string_view context) {
	TypeSpecifierNode resolved_type_spec = type_spec;
	resolved_type_spec.set_category(requireConcreteAliasResolvedCodegenTypeCategory(type_spec, context));
	return resolved_type_spec;
}

inline int requireConcreteAliasResolvedCodegenSizeBits(const TypeSpecifierNode& type_spec, std::string_view context) {
	TypeSpecifierNode resolved_type_spec = makeConcreteAliasResolvedCodegenSizeQueryType(type_spec, context);
	return requireConcreteAliasResolvedTypeSizeBits(resolved_type_spec, context);
}
