// TypeTraitEvaluator.h - Shared type trait evaluation logic
// This file provides a unified implementation for evaluating type traits
// at both compile time (Parser.cpp) and code generation time (CodeGen.h)

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

inline bool isArithmeticType(Type type) {
	// Arithmetic types are Bool(1) through LongDouble(14)
	return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &&
	       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
}

inline bool isFundamentalType(Type type) {
	// Fundamental types are Void(0), Nullptr(28), or arithmetic types
	return (type == Type::Void) || (type == Type::Nullptr) || isArithmeticType(type);
}

inline bool isScalarType(Type type, bool is_reference, size_t pointer_depth) {
	if (is_reference) return false;
	if (pointer_depth > 0) return true;  // Pointers are scalar
	return (type == Type::Bool || type == Type::Char || type == Type::Short ||
	        type == Type::Int || type == Type::Long || type == Type::LongLong ||
	        type == Type::UnsignedChar || type == Type::UnsignedShort ||
	        type == Type::UnsignedInt || type == Type::UnsignedLong ||
	        type == Type::UnsignedLongLong || type == Type::Float ||
	        type == Type::Double || type == Type::LongDouble || type == Type::Enum ||
	        type == Type::Nullptr || type == Type::MemberObjectPointer ||
	        type == Type::MemberFunctionPointer);
}

inline bool isIntegral(Type type) {
	return (type == Type::Bool || type == Type::Char || 
	        type == Type::UnsignedChar || type == Type::Short ||
	        type == Type::UnsignedShort || type == Type::Int ||
	        type == Type::UnsignedInt || type == Type::Long ||
	        type == Type::UnsignedLong || type == Type::LongLong ||
	        type == Type::UnsignedLongLong);
}

inline bool isFloatingPoint(Type type) {
	return (type == Type::Float || type == Type::Double || type == Type::LongDouble);
}

inline bool isSigned(Type type) {
	return (type == Type::Char || type == Type::Short || type == Type::Int ||
	        type == Type::Long || type == Type::LongLong);
}

inline bool isUnsigned(Type type) {
	return (type == Type::Bool || type == Type::UnsignedChar || type == Type::UnsignedShort ||
	        type == Type::UnsignedInt || type == Type::UnsignedLong ||
	        type == Type::UnsignedLongLong);
}

} // namespace TypeTraitEval

// Main type trait evaluation function
// Returns a TypeTraitResult indicating whether evaluation succeeded and the result
inline TypeTraitResult evaluateTypeTrait(
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
	bool result = false;
	
	switch (kind) {
		case TypeTraitKind::IsConstantEvaluated:
			// In compile-time context, return true; in runtime context, return false
			// This is context-dependent, so caller should handle this case specially
			return TypeTraitResult::failure();
			
		case TypeTraitKind::IsVoid:
			result = (base_type == Type::Void && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsNullptr:
			result = (base_type == Type::Nullptr && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsIntegral:
			result = isIntegral(base_type) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFloatingPoint:
			result = isFloatingPoint(base_type) && !is_reference && pointer_depth == 0;
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
			result = (base_type == Type::MemberObjectPointer && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsMemberFunctionPointer:
			result = (base_type == Type::MemberFunctionPointer && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsEnum:
			result = (base_type == Type::Enum && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsUnion:
			result = struct_info && struct_info->is_union && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsClass:
			result = (base_type == Type::Struct || base_type == Type::UserDefined) && 
			         struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFunction:
			result = (base_type == Type::Function && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsReference:
			result = is_reference || is_rvalue_reference;
			break;
			
		case TypeTraitKind::IsArithmetic:
			result = isArithmeticType(base_type) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsFundamental:
			result = isFundamentalType(base_type) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsObject:
			result = (base_type != Type::Function) && (base_type != Type::Void) && !is_reference && !is_rvalue_reference;
			break;
			
		case TypeTraitKind::IsScalar:
			result = isScalarType(base_type, is_reference, pointer_depth);
			break;
			
		case TypeTraitKind::IsCompound:
			result = !(isFundamentalType(base_type) && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsConst:
			result = (cv_qualifier == CVQualifier::Const || cv_qualifier == CVQualifier::ConstVolatile);
			break;
			
		case TypeTraitKind::IsVolatile:
			result = (cv_qualifier == CVQualifier::Volatile || cv_qualifier == CVQualifier::ConstVolatile);
			break;
			
		case TypeTraitKind::IsSigned:
			result = isSigned(base_type) && !is_reference && pointer_depth == 0;
			break;
			
		case TypeTraitKind::IsUnsigned:
			result = isUnsigned(base_type) && !is_reference && pointer_depth == 0;
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
			} else if (isScalarType(base_type, is_reference, pointer_depth)) {
				result = true;
			}
			break;
			
		case TypeTraitKind::HasUniqueObjectRepresentations:
			result = ((base_type == Type::Char || base_type == Type::Short || base_type == Type::Int ||
			           base_type == Type::Long || base_type == Type::LongLong || base_type == Type::UnsignedChar ||
			           base_type == Type::UnsignedShort || base_type == Type::UnsignedInt ||
			           base_type == Type::UnsignedLong || base_type == Type::UnsignedLongLong)
			          && !is_reference && pointer_depth == 0);
			break;
			
		case TypeTraitKind::IsTriviallyCopyable:
			if (isScalarType(base_type, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = !struct_info->has_vtable;
			}
			break;
			
		case TypeTraitKind::IsTrivial:
			if (isScalarType(base_type, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
			}
			break;
			
		case TypeTraitKind::IsPod:
			if (isScalarType(base_type, is_reference, pointer_depth)) {
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
			if (isScalarType(base_type, is_reference, pointer_depth) || is_reference) {
				result = true;
			} else if (struct_info && pointer_depth == 0) {
				result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
			}
			break;
			
		case TypeTraitKind::IsDestructible:
			if (isScalarType(base_type, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = true;  // Assume destructible unless proven otherwise
			}
			break;
			
		case TypeTraitKind::IsTriviallyDestructible:
		case TypeTraitKind::HasTrivialDestructor:
			if (isScalarType(base_type, is_reference, pointer_depth)) {
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
			if (isScalarType(base_type, is_reference, pointer_depth)) {
				result = true;
			} else if (struct_info && !is_reference && pointer_depth == 0) {
				result = true;  // Most destructors are noexcept by default since C++11
			}
			break;
			
		case TypeTraitKind::HasVirtualDestructor:
			if (struct_info && !struct_info->is_union && !is_reference && pointer_depth == 0) {
				result = struct_info->has_vtable && struct_info->hasUserDefinedDestructor();
				// If no explicit destructor but has vtable, check base classes
				if (!result && struct_info->has_vtable && !struct_info->base_classes.empty()) {
					for (const auto& base : struct_info->base_classes) {
						if (base.type_index < gTypeInfo.size()) {
							const TypeInfo& base_type_info = gTypeInfo[base.type_index];
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
			if (isScalarType(base_type, is_reference, pointer_depth)) {
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
inline TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
) {
	return evaluateTypeTrait(
		kind,
		type_spec.type(),
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
