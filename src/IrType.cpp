#include "IrType.h"

IrType toIrType(TypeCategory cat) {
	switch (cat) {
		// All integer-like types map to IrType::Integer
		case TypeCategory::Bool:
		case TypeCategory::Char:
		case TypeCategory::UnsignedChar:
		case TypeCategory::WChar:
		case TypeCategory::Char8:
		case TypeCategory::Char16:
		case TypeCategory::Char32:
		case TypeCategory::Short:
		case TypeCategory::UnsignedShort:
		case TypeCategory::Int:
		case TypeCategory::UnsignedInt:
		case TypeCategory::Long:
		case TypeCategory::UnsignedLong:
		case TypeCategory::LongLong:
		case TypeCategory::UnsignedLongLong:
			return IrType::Integer;

		// Enums lower to Integer — width/signedness stay in the existing metadata
		// fields instead of multiplying IrType variants by size and signedness.
		case TypeCategory::Enum:
			return IrType::Integer;

		// Floating point types
		case TypeCategory::Float:
			return IrType::Float;
		case TypeCategory::Double:
			return IrType::Double;
		case TypeCategory::LongDouble:
			return IrType::LongDouble;

		// Aggregate / user-defined types
		// TypeAlias is included here because unresolved aliases that reach IR
		// were previously lowered via categoryToType(TypeAlias) → Type::UserDefined
		// → IrType::Struct.  Ideally aliases are resolved before codegen, but
		// during the transition period this preserves the existing behavior.
		case TypeCategory::Struct:
		case TypeCategory::UserDefined:
		case TypeCategory::TypeAlias:
			return IrType::Struct;

		// Pointer-like types
		case TypeCategory::FunctionPointer:
			return IrType::FunctionPointer;
		case TypeCategory::MemberFunctionPointer:
			return IrType::MemberFunctionPointer;
		case TypeCategory::MemberObjectPointer:
			return IrType::MemberObjectPointer;

		// Special types
		case TypeCategory::Nullptr:
			return IrType::Nullptr;
		case TypeCategory::Void:
			return IrType::Void;

		// These must not reach IR — they must be resolved before codegen.
		// During the transition period, we still tolerate some semantic-only
		// forms to preserve existing runtime behavior until their lowering moves
		// earlier in the pipeline (ideally a semantic pass, not parser/codegen).
		case TypeCategory::Auto:
		case TypeCategory::DeclTypeAuto:
			return IrType::Integer;
		case TypeCategory::Template:
			return IrType::Void;
		case TypeCategory::Function:
			return IrType::FunctionPointer;
		case TypeCategory::Invalid:
			return IrType::Void;
	}
	assert(false && "Unknown TypeCategory value in toIrType");
	return IrType::Void;
}

std::string_view irTypeName(IrType t) {
	switch (t) {
		case IrType::Void:                   return "Void";
		case IrType::Integer:                return "Integer";
		case IrType::Float:                  return "Float";
		case IrType::Double:                 return "Double";
		case IrType::LongDouble:             return "LongDouble";
		case IrType::Struct:                 return "Struct";
		case IrType::FunctionPointer:        return "FunctionPointer";
		case IrType::MemberFunctionPointer:  return "MemberFunctionPointer";
		case IrType::MemberObjectPointer:    return "MemberObjectPointer";
		case IrType::Nullptr:                return "Nullptr";
	}
	return "Unknown";
}
