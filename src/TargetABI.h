#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "AstNodeTypes.h"
#include "CompileError.h"
#include "SizeTypes.h"
#include "TypeTraitEvaluator.h"

enum class SysVRegisterClass : uint8_t {
	None,
	Integer,
	Sse,
	Memory,
};

struct SysVAbiValueLayout {
	std::array<SysVRegisterClass, 2> eightbytes{SysVRegisterClass::None, SysVRegisterClass::None};
	uint8_t eightbyte_count = 0;
	bool contains_x87 = false;

	bool isMemory() const {
		return eightbytes[0] == SysVRegisterClass::Memory;
	}

	size_t integerRegisterCount() const {
		size_t count = 0;
		for (size_t i = 0; i < eightbyte_count; ++i) {
			count += eightbytes[i] == SysVRegisterClass::Integer;
		}
		return count;
	}

	size_t sseRegisterCount() const {
		size_t count = 0;
		for (size_t i = 0; i < eightbyte_count; ++i) {
			count += eightbytes[i] == SysVRegisterClass::Sse;
		}
		return count;
	}
};

namespace TargetABI {
namespace Detail {

inline SysVRegisterClass mergeSysVRegisterClasses(SysVRegisterClass lhs, SysVRegisterClass rhs) {
	if (lhs == rhs || rhs == SysVRegisterClass::None) {
		return lhs;
	}
	if (lhs == SysVRegisterClass::None) {
		return rhs;
	}
	if (lhs == SysVRegisterClass::Memory || rhs == SysVRegisterClass::Memory) {
		return SysVRegisterClass::Memory;
	}
	if (lhs == SysVRegisterClass::Integer || rhs == SysVRegisterClass::Integer) {
		return SysVRegisterClass::Integer;
	}
	return SysVRegisterClass::Sse;
}

inline bool isSysVIntegerCategory(TypeCategory category) {
	switch (category) {
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
	case TypeCategory::Enum:
	case TypeCategory::Nullptr:
	case TypeCategory::FunctionPointer:
	case TypeCategory::MemberFunctionPointer:
	case TypeCategory::MemberObjectPointer:
		return true;
	default:
		return false;
	}
}

inline void markSysVEightbytes(SysVAbiValueLayout& layout, size_t offset, size_t size,
							  SysVRegisterClass register_class) {
	if (size == 0) {
		return;
	}
	const size_t first_eightbyte = offset / 8;
	const size_t last_eightbyte = (offset + size - 1) / 8;
	if (last_eightbyte >= layout.eightbytes.size()) {
		layout.eightbytes[0] = SysVRegisterClass::Memory;
		layout.eightbyte_count = 0;
		return;
	}
	for (size_t i = first_eightbyte; i <= last_eightbyte; ++i) {
		layout.eightbytes[i] = mergeSysVRegisterClasses(layout.eightbytes[i], register_class);
	}
}

inline size_t naturalAlignment(TypeIndex type_index, size_t object_size) {
	const TypeIndex canonical_type_index = canonicalize_type_alias(type_index).resolvedTypeIndex();
	if (const TypeInfo* type_info = tryGetTypeInfo(canonical_type_index)) {
		if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
			if (!struct_info->layout_is_complete || struct_info->alignment == 0) {
				throw InternalError("SysV ABI classification requires complete aggregate alignment metadata");
			}
			return struct_info->alignment;
		}
	}
	return object_size > 8 ? 8 : std::max<size_t>(object_size, 1);
}

inline bool classifySysVObjectAtOffset(SysVAbiValueLayout& layout, TypeIndex type_index,
								  size_t object_offset, size_t object_size,
								  int pointer_depth, bool is_reference);

inline bool classifySysVStructAtOffset(SysVAbiValueLayout& layout, const StructTypeInfo& struct_info,
								  size_t object_offset) {
	if (!struct_info.layout_is_complete) {
		throw InternalError("SysV ABI classification requires complete aggregate layout");
	}
	if (toSizeT(struct_info.total_size) > 16) {
		return false;
	}

	for (const BaseClassSpecifier& base : struct_info.base_classes) {
		const TypeInfo* base_type_info = tryGetTypeInfo(base.type_index);
		const StructTypeInfo* base_struct_info = base_type_info ? base_type_info->getStructInfo() : nullptr;
		if (!base_struct_info ||
			!classifySysVObjectAtOffset(layout, base.type_index, object_offset + base.offset,
				toSizeT(base_struct_info->total_size),
				/*pointer_depth=*/0, /*is_reference=*/false)) {
			return false;
		}
	}

	for (const StructMember& member : struct_info.members) {
		const size_t member_offset = object_offset + member.offset;
		if (member.bitfield_width.has_value()) {
			if ((member_offset % naturalAlignment(member.type_index, member.size)) != 0) {
				return false;
			}
			markSysVEightbytes(layout, member_offset, member.size, SysVRegisterClass::Integer);
			continue;
		}

		if (member.is_array) {
			size_t element_count = 1;
			for (size_t dimension : member.array_dimensions) {
				element_count *= dimension;
			}
			if (element_count == 0 || member.size % element_count != 0) {
				throw InternalError("SysV ABI classification requires complete array extent metadata");
			}
			const size_t element_size = member.size / element_count;
			for (size_t i = 0; i < element_count; ++i) {
				if (!classifySysVObjectAtOffset(layout, member.type_index,
						member_offset + i * element_size, element_size,
						member.pointer_depth, member.is_reference())) {
					return false;
				}
			}
			continue;
		}

		if (!classifySysVObjectAtOffset(layout, member.type_index, member_offset, member.size,
				member.pointer_depth, member.is_reference())) {
			return false;
		}
	}
	return true;
}

inline bool classifySysVObjectAtOffset(SysVAbiValueLayout& layout, TypeIndex type_index,
								  size_t object_offset, size_t object_size,
								  int pointer_depth, bool is_reference) {
	if (pointer_depth > 0 || is_reference) {
		if ((object_offset % 8) != 0) {
			return false;
		}
		markSysVEightbytes(layout, object_offset, 8, SysVRegisterClass::Integer);
		return !layout.isMemory();
	}

	const TypeIndex canonical_type_index = canonicalize_type_alias(type_index).resolvedTypeIndex();
	TypeCategory category = canonical_type_index.category();
	if (category == TypeCategory::Invalid) {
		category = type_index.category();
	}

	if ((object_offset % naturalAlignment(canonical_type_index, object_size)) != 0) {
		return false;
	}
	if (isSysVIntegerCategory(category)) {
		markSysVEightbytes(layout, object_offset, object_size, SysVRegisterClass::Integer);
		return !layout.isMemory();
	}
	if (category == TypeCategory::Float || category == TypeCategory::Double) {
		markSysVEightbytes(layout, object_offset, object_size, SysVRegisterClass::Sse);
		return !layout.isMemory();
	}
	if (category == TypeCategory::LongDouble) {
		layout.contains_x87 = true;
		return false;
	}
	if (const TypeInfo* type_info = tryGetTypeInfo(canonical_type_index)) {
		if (const StructTypeInfo* struct_info = type_info->getStructInfo()) {
			return classifySysVStructAtOffset(layout, *struct_info, object_offset);
		}
	}
	throw InternalError("SysV ABI classification reached a type without canonical runtime metadata (index=" +
		std::to_string(canonical_type_index.index()) + ", category=" +
		std::to_string(static_cast<int>(category)) + ", size=" +
		std::to_string(object_size) + ")");
}

} // namespace Detail

inline SysVAbiValueLayout classifySysVValue(TypeIndex type_index, SizeInBits size_in_bits,
									 int pointer_depth, bool is_reference) {
	SysVAbiValueLayout layout;
	if (pointer_depth > 0 || is_reference) {
		layout.eightbytes[0] = SysVRegisterClass::Integer;
		layout.eightbyte_count = 1;
		return layout;
	}
	if (!size_in_bits.is_set() || size_in_bits.value % 8 != 0) {
		throw InternalError("SysV ABI classification requires an exact byte-sized type");
	}
	const size_t size_in_bytes = static_cast<size_t>(size_in_bits.value / 8);
	if (size_in_bytes == 0 || size_in_bytes > 16 ||
		!Detail::classifySysVObjectAtOffset(
			layout, type_index, 0, size_in_bytes,
			/*pointer_depth=*/0, /*is_reference=*/false)) {
		layout.eightbytes = {SysVRegisterClass::Memory, SysVRegisterClass::Memory};
		layout.eightbyte_count = 0;
		return layout;
	}
	layout.eightbyte_count = static_cast<uint8_t>((size_in_bytes + 7) / 8);
	// C++ empty classes usually have size 1 and no fields, so classification leaves
	// every eightbyte as NO_CLASS/None. SysV/Itanium practice still passes such
	// non-zero-sized empty aggregates in INTEGER registers. Trailing padding
	// None eightbytes after a real Integer/Sse eightbyte must stay None so they
	// do not consume an extra register (see test_external_abi trailing padding).
	bool any_non_none = false;
	for (size_t i = 0; i < layout.eightbyte_count; ++i) {
		if (layout.eightbytes[i] != SysVRegisterClass::None) {
			any_non_none = true;
			break;
		}
	}
	if (!any_non_none && layout.eightbyte_count > 0) {
		layout.eightbytes[0] = SysVRegisterClass::Integer;
	}
	return layout;
}

} // namespace TargetABI

// A pointer-to-member is a scalar integer-class value on both x64 ABIs: one
// eightbyte independent of its pointee category and pointee cv. The
// compatibility projection can spell one as the pointee category plus a pointer
// level or as an ordered member-pointer declarator, so ABI consumers classify
// the type instead of reading the pointee category alone.
inline bool isAbiMemberPointerType(const TypeSpecifierNode& type) {
	if (type.is_member_object_pointer_type() || type.is_member_function_pointer()) {
		return true;
	}
	if (!type.type_index().is_valid()) {
		return false;
	}
	const TypeCategory resolved_category = canonicalize_type_alias(type.type_index()).resolvedTypeIndex().category();
	return resolved_category == TypeCategory::MemberObjectPointer || resolved_category == TypeCategory::MemberFunctionPointer;
}

// Scalar register class shared by both x64 ABIs: only a genuine floating-point
// object value travels in an SSE register. A pointer, a reference, and a
// pointer-to-member are integer-class, so a member pointer is never classified
// by the category of its pointee.
inline bool isAbiSseRegisterScalar(TypeCategory category, bool is_integer_class_value) {
	return !is_integer_class_value && isFloatingPointType(category);
}

// Aggregate return classification is the single decision point for function
// definitions and every call kind. The SysV and Win64 paths share the size and
// layout plumbing but consult their own record-fact predicate: Itanium's
// non-trivial-for-calls rule for SysV, and the Microsoft POD-like return rule
// for Win64.
static constexpr int SYSV_STRUCT_RETURN_THRESHOLD = 128;	 // Linux/SysV ABI: structs > 16 bytes returned via hidden pointer
static constexpr int WIN64_STRUCT_RETURN_THRESHOLD = 64;	 // Windows x64: only 1, 2, 4, and 8 byte records use RAX

inline int getStructReturnThreshold(bool is_llp64) {
	return is_llp64 ? WIN64_STRUCT_RETURN_THRESHOLD : SYSV_STRUCT_RETURN_THRESHOLD;
}

// Microsoft x64 widens the scalar return to RAX only for exactly 1, 2, 4, or 8
// byte records. A 3-byte or 16-byte record uses the caller-provided slot even
// when its object representation is otherwise trivial.
inline bool isWin64DirectAggregateReturnSize(int size_in_bits) {
	return size_in_bits == 8 || size_in_bits == 16 || size_in_bits == 32 || size_in_bits == 64;
}

inline bool returnsStructByValue(TypeCategory type, int pointer_depth, bool is_reference) {
	return is_struct_type(type) && pointer_depth == 0 && !is_reference;
}

inline bool returnsStructByValue(const TypeSpecifierNode& return_type) {
	return returnsStructByValue(
		return_type.type(), static_cast<int>(return_type.runtime_pointer_depth()), return_type.is_reference());
}

/// Aggregate return ABI disposition for a function or call result.
/// Dependent means the return type is not yet concrete enough to classify; concrete
/// function/call IR must never treat Dependent as Direct.
enum class ReturnAbiDisposition : uint8_t {
	Direct,
	Indirect,
	Dependent,
};

inline ReturnAbiDisposition planAggregateReturnDisposition(
	TypeIndex type_index,
	TypeCategory type,
	int pointer_depth,
	bool is_reference,
	int size_in_bits,
	bool is_llp64) {
	if (!returnsStructByValue(type, pointer_depth, is_reference)) {
		return ReturnAbiDisposition::Direct;
	}
	if (size_in_bits <= 0) {
		return ReturnAbiDisposition::Dependent;
	}

	if (is_llp64) {
		// Size alone settles every record outside the four direct-return widths.
		if (!isWin64DirectAggregateReturnSize(size_in_bits)) {
			return ReturnAbiDisposition::Indirect;
		}
	} else if (size_in_bits > SYSV_STRUCT_RETURN_THRESHOLD) {
		// SysV: any aggregate larger than two eightbytes is MEMORY regardless of layout.
		return ReturnAbiDisposition::Indirect;
	}

	if (!type_index.is_valid()) {
		return ReturnAbiDisposition::Dependent;
	}

	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
	if (!struct_info || !struct_info->hasCompleteObjectLayout()) {
		return ReturnAbiDisposition::Dependent;
	}

	if (is_llp64) {
		return isStructReturnedInWin64Registers(struct_info)
			? ReturnAbiDisposition::Direct
			: ReturnAbiDisposition::Indirect;
	}

	// Itanium C++ ABI: non-trivial copy/move construction or destruction makes
	// the class indirect for calls even when its scalar layout fits registers.
	if (!isStructTrivialForCalls(struct_info)) {
		return ReturnAbiDisposition::Indirect;
	}

	const SysVAbiValueLayout layout = TargetABI::classifySysVValue(
		type_index, SizeInBits{size_in_bits}, pointer_depth, is_reference);
	// x87 aggregate returns stay on the legacy direct path until X87/X87UP
	// return registers are modeled in the backend.
	if (layout.contains_x87) {
		return ReturnAbiDisposition::Direct;
	}
	return layout.isMemory() ? ReturnAbiDisposition::Indirect : ReturnAbiDisposition::Direct;
}

inline ReturnAbiDisposition planAggregateReturnDisposition(
	const TypeSpecifierNode& return_type, bool is_llp64) {
	return planAggregateReturnDisposition(
		return_type.type_index(),
		return_type.type(),
		static_cast<int>(return_type.runtime_pointer_depth()),
		return_type.is_reference(),
		getTypeSpecSizeBits(return_type),
		is_llp64);
}

inline bool needsHiddenReturnParam(
	TypeIndex type_index,
	TypeCategory type,
	int pointer_depth,
	bool is_reference,
	int size_in_bits,
	bool is_llp64) {
	const ReturnAbiDisposition disposition = planAggregateReturnDisposition(
		type_index, type, pointer_depth, is_reference, size_in_bits, is_llp64);
	switch (disposition) {
	case ReturnAbiDisposition::Direct:
		return false;
	case ReturnAbiDisposition::Indirect:
		return true;
	case ReturnAbiDisposition::Dependent:
		throw InternalError(
			"Concrete function/call IR requires a resolved aggregate return ABI plan");
	}
	throw InternalError("Unhandled aggregate return ABI disposition");
}

inline bool needsHiddenReturnParam(const TypeSpecifierNode& return_type, bool is_llp64) {
	return needsHiddenReturnParam(
		return_type.type_index(),
		return_type.type(),
		static_cast<int>(return_type.runtime_pointer_depth()),
		return_type.is_reference(),
		getTypeSpecSizeBits(return_type),
		is_llp64);
}
