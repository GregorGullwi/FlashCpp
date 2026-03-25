#include "IrType.h"

IrType toIrType(Type semantic_type) {
	switch (semantic_type) {
		// All integer-like types map to IrType::Integer
		case Type::Bool:
		case Type::Char:
		case Type::UnsignedChar:
		case Type::WChar:
		case Type::Char8:
		case Type::Char16:
		case Type::Char32:
		case Type::Short:
		case Type::UnsignedShort:
		case Type::Int:
		case Type::UnsignedInt:
		case Type::Long:
		case Type::UnsignedLong:
		case Type::LongLong:
		case Type::UnsignedLongLong:
			return IrType::Integer;

		// Enums lower to Integer — width/signedness stay in the existing metadata
		// fields instead of multiplying IrType variants by size and signedness.
		case Type::Enum:
			return IrType::Integer;

		// Floating point types
		case Type::Float:
			return IrType::Float;
		case Type::Double:
			return IrType::Double;
		case Type::LongDouble:
			return IrType::LongDouble;

		// Aggregate / user-defined types
		case Type::Struct:
		case Type::UserDefined:
			return IrType::Struct;

		// Pointer-like types
		case Type::FunctionPointer:
			return IrType::FunctionPointer;
		case Type::MemberFunctionPointer:
			return IrType::MemberFunctionPointer;
		case Type::MemberObjectPointer:
			return IrType::MemberObjectPointer;

		// Special types
		case Type::Nullptr:
			return IrType::Nullptr;
		case Type::Void:
			return IrType::Void;

		// These must not reach IR — they must be resolved before codegen.
		// During the transition period, we still tolerate some semantic-only
		// forms to preserve existing runtime behavior until their lowering moves
		// earlier in the pipeline (ideally a semantic pass, not parser/codegen).
		case Type::Auto:
		case Type::DeclTypeAuto:
			return IrType::Integer;
		case Type::Template:
			return IrType::Void;
		case Type::Function:
			return IrType::FunctionPointer;
		case Type::Invalid:
		case Type::Count_:    // Sentinel — never a real type value
			return IrType::Void;
	}
	assert(false && "Unknown Type value in toIrType");
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
