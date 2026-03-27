#include "TypeTraitEvaluator.h"

namespace TypeTraitEval {

inline bool isScalarType(TypeCategory cat, bool is_reference, size_t pointer_depth) {
	if (is_reference) return false;
	if (pointer_depth > 0) return true;  // Pointers are scalar
	return (cat == TypeCategory::Bool || cat == TypeCategory::Char || cat == TypeCategory::Short ||
	        cat == TypeCategory::Int || cat == TypeCategory::Long || cat == TypeCategory::LongLong ||
	        cat == TypeCategory::UnsignedChar || cat == TypeCategory::UnsignedShort ||
	        cat == TypeCategory::UnsignedInt || cat == TypeCategory::UnsignedLong ||
	        cat == TypeCategory::UnsignedLongLong || cat == TypeCategory::Float ||
	        cat == TypeCategory::Double || cat == TypeCategory::LongDouble || cat == TypeCategory::Enum ||
	        cat == TypeCategory::Nullptr || cat == TypeCategory::MemberObjectPointer ||
	        cat == TypeCategory::MemberFunctionPointer);
}

inline bool isIntegral(TypeCategory cat) {
	return (cat == TypeCategory::Bool || cat == TypeCategory::Char ||
	        cat == TypeCategory::UnsignedChar || cat == TypeCategory::Short ||
	        cat == TypeCategory::UnsignedShort || cat == TypeCategory::Int ||
	        cat == TypeCategory::UnsignedInt || cat == TypeCategory::Long ||
	        cat == TypeCategory::UnsignedLong || cat == TypeCategory::LongLong ||
	        cat == TypeCategory::UnsignedLongLong);
}

inline bool isFloatingPoint(TypeCategory cat) {
	return (cat == TypeCategory::Float || cat == TypeCategory::Double || cat == TypeCategory::LongDouble);
}

inline bool isSigned(TypeCategory cat) {
	return (cat == TypeCategory::Char || cat == TypeCategory::Short || cat == TypeCategory::Int ||
	        cat == TypeCategory::Long || cat == TypeCategory::LongLong);
}

inline bool isUnsigned(TypeCategory cat) {
	return (cat == TypeCategory::Bool || cat == TypeCategory::UnsignedChar || cat == TypeCategory::UnsignedShort ||
	        cat == TypeCategory::UnsignedInt || cat == TypeCategory::UnsignedLong ||
	        cat == TypeCategory::UnsignedLongLong);
}


} // namespace TypeTraitEval

bool isStructNothrowDestructible(const StructTypeInfo* struct_info) {
	if (!struct_info) return true;

	// If there is an explicit user-defined destructor AND it carries an explicit
	// noexcept specifier (bare noexcept or noexcept(expr)), the is_noexcept()
	// flag was eagerly evaluated at parse time — trust it directly.
	// If the destructor has NO explicit noexcept specifier (or is = default),
	// its effective noexcept status is determined by bases/members, just as for
	// an implicit destructor (C++20 [except.spec]/7, [class.dtor]/3).
	const auto* dtor = struct_info->findDestructor();
	if (dtor && dtor->function_decl.is<DestructorDeclarationNode>()) {
		const auto& dtor_node = dtor->function_decl.as<DestructorDeclarationNode>();
		if (dtor_node.has_noexcept_specifier()) {
			return dtor_node.is_noexcept();
		}
		// Fall through to base/member check below
	}

	// No explicit destructor, or destructor without a noexcept specifier:
	// the effective noexcept status depends on base classes and members.
	for (const auto& base : struct_info->base_classes) {
		if (base.is_deferred || base.type_index.index() >= getTypeInfoCount()) continue;
		const StructTypeInfo* base_struct = getTypeInfo(base.type_index).getStructInfo();
		if (!isStructNothrowDestructible(base_struct))
			return false;
	}
	for (const auto& member : struct_info->members) {
		// Only struct/class-typed members (not pointers or references) have destructors
		if ((!is_struct_type(member.type_index.category())) ||
		    member.pointer_depth > 0 || member.is_reference()) continue;
		if (member.type_index.index() >= getTypeInfoCount()) continue;
		const StructTypeInfo* mem_struct = getTypeInfo(member.type_index).getStructInfo();
		if (!isStructNothrowDestructible(mem_struct))
			return false;
	}
	return true;
}

bool isPseudoDestructorCallNoexcept(const PseudoDestructorCallNode& pseudo_dtor, const SymbolTable& symbols) {
	// Try to resolve the actual type from the object expression's declaration
	// (not the type_name() token) so that template specializations like
	// Wrapper<int> resolve to the correct instantiated type.
	if (pseudo_dtor.object().is<ExpressionNode>()) {
		const ExpressionNode& obj_expr = pseudo_dtor.object().as<ExpressionNode>();
		if (const auto* obj_id = std::get_if<IdentifierNode>(&obj_expr)) {
			auto symbol = symbols.lookup(obj_id->name());
			if (symbol.has_value()) {
				const DeclarationNode* decl = get_decl_from_symbol(*symbol);
				if (decl && decl->type_node().is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = decl->type_node().as<TypeSpecifierNode>();
					if (type_spec.type_index().is_valid() && type_spec.type_index().index() < getTypeInfoCount()) {
						const StructTypeInfo* struct_info = getTypeInfo(type_spec.type_index()).getStructInfo();
						if (struct_info) {
							return isStructNothrowDestructible(struct_info);
						}
					}
				}
			}
		}
	}
	// Fallback: look up by type name token (works for non-template types)
	std::string_view type_name = pseudo_dtor.type_name();
	auto it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(type_name));
	if (it != getTypesByNameMap().end()) {
		const StructTypeInfo* struct_info = it->second->getStructInfo();
		if (struct_info) {
			return isStructNothrowDestructible(struct_info);
		}
	}
	return true;  // Scalar types: pseudo-destructor is a no-op, always noexcept
}

TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	Type base_type,
	[[maybe_unused]] TypeIndex type_idx,
	bool is_reference,
	bool is_rvalue_reference,
	bool is_lvalue_reference,
	size_t pointer_depth,
	CVQualifier cv_qualifier,
	bool is_array,
	std::optional<size_t> array_size,
	// Additional type info from gTypeInfo - caller provides these
	[[maybe_unused]] const TypeInfo* type_info,
	const StructTypeInfo* struct_info
) {
	using namespace TypeTraitEval;
	const TypeCategory cat = typeToCategory(base_type);
	bool result = false;
	
	switch (kind) {
		case TypeTraitKind::IsConstantEvaluated:
			// In compile-time context, return true; in runtime context, return false
			// This is context-dependent, so caller should handle this case specially
			return TypeTraitResult::failure();
			
		case TypeTraitKind::IsCompleteOrUnbounded:
			// __is_complete_or_unbounded evaluates to true if either:
			// 1. T is a complete type, or
			// 2. T is an unbounded array type (e.g. int[])
			// It evaluates to false for:
			// - Incomplete class types
			// - void
			// - Bounded array types with incomplete element types
			
			// Check for void - always incomplete
			if (cat == TypeCategory::Void && pointer_depth == 0 && !is_reference) {
				return TypeTraitResult::success_false();
			}
			
			// Check for unbounded array - always returns true
			if (is_array && (!array_size.has_value() || *array_size == 0)) {
				return TypeTraitResult::success_true();
			}
			
			// Check for incomplete class/struct types (struct_info is null for incomplete types)
			if (is_struct_type(cat) &&
			    !struct_info && pointer_depth == 0 && !is_reference) {
				return TypeTraitResult::success_false();
			}
			
			// All other types are considered complete
			return TypeTraitResult::success_true();
			
		case TypeTraitKind::IsVoid:
			result = (cat == TypeCategory::Void && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsNullptr:
			result = (cat == TypeCategory::Nullptr && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsIntegral:
			result = isIntegral(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFloatingPoint:
			result = isFloatingPoint(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsArray:
			result = is_array && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsPointer:
			result = (pointer_depth > 0) && !is_reference;
			break;
			
		case TypeTraitKind::IsLvalueReference:
			result = is_lvalue_reference || (is_reference && !is_rvalue_reference);
			break;
			
		case TypeTraitKind::IsRvalueReference:
			result = is_rvalue_reference;
			break;
			
		case TypeTraitKind::IsMemberObjectPointer:
			result = (cat == TypeCategory::MemberObjectPointer && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsMemberFunctionPointer:
			result = (cat == TypeCategory::MemberFunctionPointer && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsEnum:
			result = (cat == TypeCategory::Enum && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsUnion:
			result = struct_info && struct_info->is_union && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsClass:
			result = is_struct_type(cat) &&
			         struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFunction:
			result = (cat == TypeCategory::Function && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsReference:
			result = is_reference || is_rvalue_reference;
			break;
			
		case TypeTraitKind::IsArithmetic:
			result = isArithmeticType(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFundamental:
			result = isFundamentalType(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsObject:
			result = (cat != TypeCategory::Function) && (cat != TypeCategory::Void) && !is_reference && !is_rvalue_reference;
			break;
			
		case TypeTraitKind::IsScalar:
			result = isScalarType(cat, is_reference, pointer_depth);
			break;
			
		case TypeTraitKind::IsCompound:
			result = !(isFundamentalType(cat) && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsConst:
			result = (cv_qualifier == CVQualifier::Const || cv_qualifier == CVQualifier::ConstVolatile);
			break;
			
		case TypeTraitKind::IsVolatile:
			result = (cv_qualifier == CVQualifier::Volatile || cv_qualifier == CVQualifier::ConstVolatile);
			break;
			
		case TypeTraitKind::IsSigned:
			result = isSigned(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsUnsigned:
			result = isUnsigned(cat) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsBoundedArray:
			result = is_array && array_size.has_value() && *array_size > 0 && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsUnboundedArray:
			result = is_array && (!array_size.has_value() || *array_size == 0) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsPolymorphic:
			result = struct_info && struct_info->has_vtable && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFinal:
			result = struct_info && struct_info->is_final && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsAbstract:
			result = struct_info && struct_info->is_abstract && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsEmpty:
			if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				result = struct_info->members.empty() && !struct_info->has_vtable;
			}
			break;
			
		case TypeTraitKind::IsAggregate:
			if (struct_info && !is_reference && pointer_depth == 0) {
				// Check aggregate conditions
				bool has_user_constructors = false;
				for (const auto& func : struct_info->member_functions) {
					if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
						const ConstructorDeclarationNode& ctor = func.function_decl.as<ConstructorDeclarationNode>();
						if (!ctor.is_implicit()) {
							has_user_constructors = true;
							break;
						}
					}
				}
				
				bool no_virtual = !struct_info->has_vtable;
				bool all_public = true;
				for (const auto& member : struct_info->members) {
					if (member.access == AccessSpecifier::Private || 
					    member.access == AccessSpecifier::Protected) {
						all_public = false;
						break;
					}
				}
				
				result = !has_user_constructors && no_virtual && all_public;
			} else if (is_array && !is_reference && pointer_depth == 0) {
				result = true;
			}
			break;
			
		case TypeTraitKind::IsStandardLayout:
			if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				result = !struct_info->has_vtable;
				if (result && struct_info->members.size() > 1) {
					AccessSpecifier first_access = struct_info->members[0].access;
					for (const auto& member : struct_info->members) {
						if (member.access != first_access) {
							result = false;
							break;
						}
					}
				}
			} else if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			}
			break;
			
		case TypeTraitKind::HasUniqueObjectRepresentations:
			result = (isIntegral(cat) && cat != TypeCategory::Bool
			          && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsTriviallyCopyable:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = !struct_info->has_vtable;
			}
			break;
			
		case TypeTraitKind::IsTrivial:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
			}
			break;
			
		case TypeTraitKind::IsPod:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				bool is_pod = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
				if (is_pod && struct_info->members.size() > 1) {
					AccessSpecifier first_access = struct_info->members[0].access;
					for (const auto& member : struct_info->members) {
						if (member.access != first_access) {
							is_pod = false;
							break;
						}
					}
				}
				result = is_pod;
			}
			break;
			
		case TypeTraitKind::IsLiteralType:
			if (isScalarType(cat, is_reference, pointer_depth) || is_reference) {
				result = true;
			} else if (struct_info && pointer_depth == 0) {
				result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
			}
			break;
			
		case TypeTraitKind::IsDestructible:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = true;  // Assume destructible unless proven otherwise
			}
			break;
			
		case TypeTraitKind::IsTriviallyDestructible:
		case TypeTraitKind::HasTrivialDestructor:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				if (!struct_info->is_union) {
					result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
				} else {
					result = true;  // Unions are trivially destructible if all members are
				}
			}
			break;
			
		case TypeTraitKind::IsNothrowDestructible:
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = isStructNothrowDestructible(struct_info);
			}
			break;
			
		case TypeTraitKind::HasVirtualDestructor:
			if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				result = struct_info->has_vtable && struct_info->hasUserDefinedDestructor();
				// If no explicit destructor but has vtable, check base classes
				if (!result && struct_info->has_vtable && !struct_info->base_classes.empty()) {
					for (const auto& base : struct_info->base_classes) {
						if (base.type_index.index() < getTypeInfoCount()) {
							const TypeInfo& base_type_info = getTypeInfo(base.type_index);
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (base_struct_info && base_struct_info->has_vtable) {
								result = true;
								break;
							}
						}
					}
				}
			}
			break;
			
		case TypeTraitKind::IsConstructible:
		case TypeTraitKind::IsTriviallyConstructible:
		case TypeTraitKind::IsNothrowConstructible:
			// These need variadic type arguments, return failure for simple evaluation
			if (isScalarType(cat, is_reference, pointer_depth)) {
				result = true;  // Scalars are always default constructible
			} else if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				if (kind == TypeTraitKind::IsConstructible) {
					result = !struct_info->hasUserDefinedConstructor() || struct_info->hasConstructor();
				} else {
					result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
				}
			}
			break;
			
		// Binary traits and variadic traits need special handling with second type
		case TypeTraitKind::IsBaseOf:
		case TypeTraitKind::IsSame:
		case TypeTraitKind::IsConvertible:
		case TypeTraitKind::IsNothrowConvertible:
		case TypeTraitKind::IsAssignable:
		case TypeTraitKind::IsTriviallyAssignable:
		case TypeTraitKind::IsNothrowAssignable:
		case TypeTraitKind::IsLayoutCompatible:
		case TypeTraitKind::IsPointerInterconvertibleBaseOf:
			// These need the second type argument, return failure
			return TypeTraitResult::failure();
			
		case TypeTraitKind::UnderlyingType:
			// This returns a type, not a bool, so handle specially
			return TypeTraitResult::failure();
			
		default:
			return TypeTraitResult::failure();
	}
	
	return TypeTraitResult{true, result};
}

// Convenience overload that takes a TypeSpecifierNode directly
// This extracts all the necessary fields and calls the main evaluateTypeTrait function
TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
) {
	return evaluateTypeTrait(
		kind,
		categoryToType(type_spec.category()),
		type_spec.type_index(),
		type_spec.is_reference(),
		type_spec.is_rvalue_reference(),
		type_spec.is_lvalue_reference(),
		type_spec.pointer_depth(),
		type_spec.cv_qualifier(),
		type_spec.is_array(),
		type_spec.array_size(),
		type_info,
		struct_info
	);
}
