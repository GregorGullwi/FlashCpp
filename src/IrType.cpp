#include "IrType.h"

#include "CompileError.h"

#include <string>

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
	case TypeCategory::Struct:
	case TypeCategory::UserDefined:
		return IrType::Struct;

		// TypeAlias must not reach this pure-category overload — the
		// toIrType(TypeIndex) overload resolves aliases through getTypeInfo()
		// first.  Hitting this case means a TypeAlias category leaked into a
		// code path that only has a TypeCategory (no TypeIndex to resolve from),
		// which is a compiler bug.
	case TypeCategory::TypeAlias:
		assert(false && "TypeCategory::TypeAlias must be resolved before reaching toIrType(TypeCategory)");
		return IrType::Void;

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

		// Auto still appears on a few parser-created internal temporaries; keep
		// the historical lowering until those call sites carry concrete type info.
	case TypeCategory::Auto:
		return IrType::Integer;
	case TypeCategory::DeclTypeAuto:
	case TypeCategory::Template:
		throw InternalError("Unresolved semantic type reached IR type conversion: category " + std::to_string(static_cast<int>(cat)));
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
	case IrType::Void:
		return "Void";
	case IrType::Integer:
		return "Integer";
	case IrType::Float:
		return "Float";
	case IrType::Double:
		return "Double";
	case IrType::LongDouble:
		return "LongDouble";
	case IrType::Struct:
		return "Struct";
	case IrType::FunctionPointer:
		return "FunctionPointer";
	case IrType::MemberFunctionPointer:
		return "MemberFunctionPointer";
	case IrType::MemberObjectPointer:
		return "MemberObjectPointer";
	case IrType::Nullptr:
		return "Nullptr";
	}
	return "Unknown";
}
