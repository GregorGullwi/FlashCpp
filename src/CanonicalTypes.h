#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <thread>
#include <vector>
#include <unordered_map>

#include "ArenaAccounting.h"
#include "ChunkedAnyVector.h"
#include "Log.h"
#include "CompileError.h"
#include "FrontendIds.h"
#include "TypeQualifiers.h"

enum class CanonicalBuiltinKind : uint8_t {
	Void, Bool, Char, SignedChar, UnsignedChar, WChar, Char8, Char16, Char32,
	Short, UnsignedShort, Int, UnsignedInt, Long, UnsignedLong,
	LongLong, UnsignedLongLong, Float, Double, LongDouble, Nullptr,
	Count,
};

enum class CanonicalTypeKind : uint8_t {
	Builtin, Qualified, Pointer, LValueReference, RValueReference, Array,
	Function, FunctionParam, Record, MemberObjectPointer, MemberFunctionPointer,
	Enum, TemplateParameter, TemplateSpecialization, TemplateArg,
	DependentName, DependentTemplateMember, NameBytes,
};

enum class CanonicalTypeNodeFlags : uint8_t {
	None = 0,
	KnownArrayBound = 1 << 0,
	VariadicFunction = 1 << 1,
	NoexceptFunction = 1 << 2,
	FunctionLValueRef = 1 << 3,
	FunctionRValueRef = 1 << 4,
	FunctionDllImport = 1 << 5,
	FunctionDllExport = 1 << 6,
	DependentNoexceptFunction = 1 << 7,
};

// Stored in CanonicalTypeNode::builtin for Function nodes only.
enum class CanonicalCallingConvention : uint8_t {
	Default = 0,
	Cdecl,
	Stdcall,
	Fastcall,
	Vectorcall,
	Thiscall,
	Clrcall,
	Count,
};

enum class CanonicalDllLinkage : uint8_t {
	None = 0,
	Import,
	Export,
};

inline CanonicalTypeNodeFlags operator|(CanonicalTypeNodeFlags a, CanonicalTypeNodeFlags b) {
	return static_cast<CanonicalTypeNodeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CanonicalTypeNodeFlags& operator|=(CanonicalTypeNodeFlags& a, CanonicalTypeNodeFlags b) {
	return a = a | b;
}
inline bool hasCanonicalTypeNodeFlag(CanonicalTypeNodeFlags flags, CanonicalTypeNodeFlags bit) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(bit)) != 0;
}

inline uint64_t packFunctionArrayExtent(TypeId param_link, ExprId dependent_noexcept) {
	return static_cast<uint64_t>(param_link.value) |
		(static_cast<uint64_t>(dependent_noexcept.value) << 32);
}

inline TypeId unpackFunctionParamLink(uint64_t array_extent) {
	return TypeId{static_cast<uint32_t>(array_extent)};
}

inline ExprId unpackFunctionDependentNoexcept(uint64_t array_extent) {
	return ExprId{static_cast<uint32_t>(array_extent >> 32)};
}

inline uint64_t packTemplateParameterExtent(TemplateDeclId template_decl, uint32_t parameter_index) {
	return static_cast<uint64_t>(template_decl.value) |
		(static_cast<uint64_t>(parameter_index) << 32);
}

inline TemplateDeclId unpackTemplateParameterDecl(uint64_t array_extent) {
	return TemplateDeclId{static_cast<uint32_t>(array_extent)};
}

inline uint32_t unpackTemplateParameterIndex(uint64_t array_extent) {
	return static_cast<uint32_t>(array_extent >> 32);
}

inline uint64_t packDependentTemplateMemberExtent(TypeId name_link, TypeId arg_link) {
	return static_cast<uint64_t>(name_link.value) |
		(static_cast<uint64_t>(arg_link.value) << 32);
}

inline TypeId unpackDependentTemplateMemberName(uint64_t array_extent) {
	return TypeId{static_cast<uint32_t>(array_extent)};
}

inline TypeId unpackDependentTemplateMemberArgs(uint64_t array_extent) {
	return TypeId{static_cast<uint32_t>(array_extent >> 32)};
}

// An immutable structural node. A child is a canonical identity in this table,
// never an AST pointer, spelling, legacy TypeIndex, or telemetry key.
// FunctionParam links store the parameter TypeId in array_extent and the next
// link in child. Member pointers store the owner TypeId in array_extent and the
// pointee in child. Opaque Record and Enum nodes store EntityId in array_extent.
// Function nodes pack parameter-list TypeId in the low 32 bits of array_extent
// and optional dependent-noexcept ExprId in the high 32 bits. TemplateParameter
// nodes pack TemplateDeclId in the low 32 bits and parameter index in the high
// 32 bits. TemplateSpecialization stores TemplateDeclId in array_extent and the
// first TemplateArg link in child. TemplateArg links store the argument TypeId
// in array_extent and the next TemplateArg link in child (type-only this slice).
// DependentName stores its qualifier in child and a NameBytes link in array_extent.
// DependentTemplateMember stores its qualifier in child and packs a NameBytes
// link (low 32) with a TemplateArg chain head (high 32) in array_extent — the
// unresolved member template-id form without a published TemplateDeclId.
// NameBytes is an internal content link: child is the next link, builtin holds
// the byte count (1..8), and array_extent packs identifier bytes little-endian.
// It is not a type or a spelling-handle identity. All links share node rollback.
struct CanonicalTypeNode {
	TypeId child;
	CanonicalTypeKind kind;
	CanonicalBuiltinKind builtin;
	CVQualifier qualifiers;
	CanonicalTypeNodeFlags flags;
	uint64_t array_extent;
	friend bool operator==(CanonicalTypeNode, CanonicalTypeNode) = default;
};

static_assert(std::is_trivially_copyable_v<CanonicalTypeNode>);
static_assert(sizeof(CanonicalTypeNode) == 16);

struct CanonicalTypeArenaStats {
	uint64_t used_bytes;
	uint64_t reserved_bytes;
};

enum class CanonicalRecordLayoutFlags : uint8_t {
	None = 0,
	Union = 1 << 0,
};

enum class CanonicalEnumLayoutFlags : uint8_t {
	None = 0,
	Scoped = 1 << 0,
};

// Complete-object layout is separate from immutable canonical type identity.
// It is keyed by the published EntityId and contains no spelling, TypeIndex,
// AST pointer, or parser-owned state. This snapshot proves that a nominal type
// has a complete object representation suitable for fixed-bound array formation.
// Member and base field schemas are published separately against the same EntityId.
struct CanonicalRecordLayout {
	EntityId entity;
	uint32_t size_bytes;
	uint32_t layout_data_size_bytes;
	uint32_t non_virtual_size_bytes;
	uint16_t alignment;
	uint16_t member_count;
	uint16_t direct_base_count;
	CanonicalRecordLayoutFlags flags;
	uint8_t reserved = 0;
	friend bool operator==(CanonicalRecordLayout, CanonicalRecordLayout) = default;
};

struct CanonicalEnumLayout {
	EntityId entity;
	TypeId underlying_type;
	uint32_t size_bytes;
	uint16_t enumerator_count;
	CanonicalEnumLayoutFlags flags;
	uint8_t reserved = 0;
	friend bool operator==(CanonicalEnumLayout, CanonicalEnumLayout) = default;
};

enum class CanonicalAccess : uint8_t {
	Public = 0,
	Protected = 1,
	Private = 2,
};

enum class CanonicalRecordMemberFlags : uint8_t {
	None = 0,
	Bitfield = 1 << 0,
	NoUniqueAddress = 1 << 1,
};

enum class CanonicalRecordBaseFlags : uint8_t {
	None = 0,
	Virtual = 1 << 0,
};

// Data-member schema entry. Spelling is deliberately absent: identity is the
// owning EntityId, declaration order, TypeId, and layout facts.
struct CanonicalRecordMember {
	TypeId type;
	uint32_t offset_bytes;
	uint32_t size_bytes;
	uint8_t bit_width;
	uint8_t bit_offset;
	CanonicalAccess access;
	CanonicalRecordMemberFlags flags;
	friend bool operator==(CanonicalRecordMember, CanonicalRecordMember) = default;
};

struct CanonicalRecordBase {
	EntityId entity;
	uint32_t offset_bytes;
	CanonicalAccess access;
	CanonicalRecordBaseFlags flags;
	uint16_t reserved = 0;
	uint32_t reserved2 = 0;
	friend bool operator==(CanonicalRecordBase, CanonicalRecordBase) = default;
};

// Nested type-member schema entry for tip lookup after substitution.
// Identifier content is a NameBytes link (structural content), never StringHandle.
struct CanonicalNamedTypeMember {
	TypeId type;
	TypeId name;
	uint32_t reserved = 0;
	uint32_t reserved2 = 0;
	friend bool operator==(CanonicalNamedTypeMember, CanonicalNamedTypeMember) = default;
};

// Publication input: spelling is lookup content only; stored as NameBytes.
struct CanonicalNamedTypeMemberSpec {
	std::string_view name;
	TypeId type;
};

inline CanonicalRecordMemberFlags operator|(CanonicalRecordMemberFlags a,
	CanonicalRecordMemberFlags b) {
	return static_cast<CanonicalRecordMemberFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CanonicalRecordMemberFlags& operator|=(CanonicalRecordMemberFlags& a,
	CanonicalRecordMemberFlags b) {
	return a = a | b;
}
inline bool hasCanonicalRecordMemberFlag(CanonicalRecordMemberFlags flags,
	CanonicalRecordMemberFlags bit) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(bit)) != 0;
}

inline CanonicalRecordBaseFlags operator|(CanonicalRecordBaseFlags a, CanonicalRecordBaseFlags b) {
	return static_cast<CanonicalRecordBaseFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CanonicalRecordBaseFlags& operator|=(CanonicalRecordBaseFlags& a,
	CanonicalRecordBaseFlags b) {
	return a = a | b;
}
inline bool hasCanonicalRecordBaseFlag(CanonicalRecordBaseFlags flags,
	CanonicalRecordBaseFlags bit) {
	return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(bit)) != 0;
}

static_assert(std::is_trivially_copyable_v<CanonicalRecordLayout>);
static_assert(std::is_trivially_copyable_v<CanonicalEnumLayout>);
static_assert(std::is_trivially_copyable_v<CanonicalRecordMember>);
static_assert(std::is_trivially_copyable_v<CanonicalRecordBase>);
static_assert(std::is_trivially_copyable_v<CanonicalNamedTypeMember>);
static_assert(sizeof(CanonicalRecordLayout) == 24);
static_assert(sizeof(CanonicalEnumLayout) == 16);
static_assert(sizeof(CanonicalRecordMember) == 16);
static_assert(sizeof(CanonicalRecordBase) == 16);
static_assert(sizeof(CanonicalNamedTypeMember) == 16);

class CanonicalTypeTransaction;

// Boundary 3A type table. IDs are local to one FrontendContext and are not
// portable hashes or ABI names. Equal requests in that context return one ID,
// regardless of request order. Opaque Record and Enum nodes are EntityId-keyed;
// records remain member-pointer owners, while full record and enum layout remain
// later families. Spelling-backed nominal names stay outside this table until
// EntityId publication lands.
// One mutex protects publication and reads; keep this boundary until the real
// structural-request trace passes the parallel-experiment handoff gates.
class CanonicalTypeTable {
	friend class CanonicalTypeTransaction;

public:
	CanonicalTypeTable() = default;
	explicit CanonicalTypeTable(SemanticArenaAccounting& accounting) : accounting_(&accounting) {}
	CanonicalTypeTable(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable& operator=(const CanonicalTypeTable&) = delete;
	CanonicalTypeTable(CanonicalTypeTable&&) = delete;
	CanonicalTypeTable& operator=(CanonicalTypeTable&&) = delete;

	TypeId builtin(CanonicalBuiltinKind kind) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (kind >= CanonicalBuiltinKind::Count) {
			throw InternalError("canonical type: invalid builtin kind");
		}
		return internUnlocked({
			.child = TypeId{},
			.kind = CanonicalTypeKind::Builtin,
			.builtin = kind,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	TypeId qualify(TypeId type, CVQualifier qualifiers) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		CanonicalTypeNode input = nodeUnlocked(type);
		if (static_cast<uint8_t>(qualifiers) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
			throw InternalError("canonical type: invalid cv qualifiers");
		}
		if (isInternalLink(input.kind)) {
			throw InternalError("canonical type: qualify function parameter link");
		}
		// [dcl.ref]: cv-qualification introduced through a reference typedef is
		// ignored. Referent qualification remains on the child node.
		if (qualifiers == CVQualifier::None || isReference(input.kind)) {
			return type;
		}
		// [dcl.fct]: cv-qualifiers on a function type are part of that type.
		if (input.kind == CanonicalTypeKind::Function) {
			input.qualifiers |= qualifiers;
			return internUnlocked(input);
		}
		std::vector<CanonicalTypeNode> arrays;
		while (input.kind == CanonicalTypeKind::Array) {
			arrays.push_back(input);
			type = input.child;
			input = nodeUnlocked(type);
		}
		if (input.kind == CanonicalTypeKind::Function) {
			input.qualifiers |= qualifiers;
			type = internUnlocked(input);
		} else {
			if (input.kind == CanonicalTypeKind::Qualified) {
				qualifiers |= input.qualifiers;
				type = input.child;
			}
			type = internUnlocked({
				.child = type,
				.kind = CanonicalTypeKind::Qualified,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = qualifiers,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = 0,
			});
		}
		for (auto array = arrays.rbegin(); array != arrays.rend(); ++array) {
			array->child = type;
			type = internUnlocked(*array);
		}
		return type;
	}

	TypeId pointer(TypeId pointee) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto kind = nodeUnlocked(pointee).kind;
		if (isReference(kind) || isInternalLink(kind)) {
			throw InternalError("canonical type: invalid pointer pointee");
		}
		return internUnlocked({
			.child = pointee,
			.kind = CanonicalTypeKind::Pointer,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	TypeId array(TypeId element, size_t extent) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (extent == 0) {
			throw InternalError("canonical type: zero array bound");
		}
		return arrayUnlocked(element, static_cast<uint64_t>(extent), CanonicalTypeNodeFlags::KnownArrayBound);
	}

	TypeId arrayOfUnknownBound(TypeId element) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return arrayUnlocked(element, 0, CanonicalTypeNodeFlags::None);
	}

	TypeId reference(TypeId referent, ReferenceQualifier qualifier) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		CanonicalTypeNode input = nodeUnlocked(referent);
		if (isInternalLink(input.kind)) {
			throw InternalError("canonical type: reference to function parameter link");
		}
		if (qualifier != ReferenceQualifier::LValueReference && qualifier != ReferenceQualifier::RValueReference) {
			throw InternalError("canonical type: invalid reference qualifier");
		}
		auto kind = qualifier == ReferenceQualifier::LValueReference
			? CanonicalTypeKind::LValueReference : CanonicalTypeKind::RValueReference;
		if (isReference(input.kind)) {
			// [dcl.ref] reference collapsing: only && combined with && stays &&.
			if (input.kind == CanonicalTypeKind::LValueReference) {
				kind = CanonicalTypeKind::LValueReference;
			}
			referent = input.child;
			input = nodeUnlocked(referent);
		}
		const auto base = input.kind == CanonicalTypeKind::Qualified ? nodeUnlocked(input.child) : input;
		if (base.kind == CanonicalTypeKind::Builtin && base.builtin == CanonicalBuiltinKind::Void) {
			throw InternalError("canonical type: reference to void");
		}
		return internUnlocked({
			.child = referent,
			.kind = kind,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}

	// Free-function and cv/ref-qualified function types. Calling convention is
	// stored in the unused builtin byte for Function nodes; dllimport/dllexport
	// use dedicated flag bits. Dependent noexcept(expr) identity is an opaque
	// ExprId packed beside the parameter-list link; plain noexcept remains a flag.
	TypeId function(TypeId return_type, std::span<const TypeId> parameters, bool is_variadic,
		CVQualifier function_cv, ReferenceQualifier function_ref, bool is_noexcept,
		CanonicalCallingConvention calling_convention, CanonicalDllLinkage dll_linkage,
		ExprId dependent_noexcept) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (static_cast<uint8_t>(function_cv) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
			throw InternalError("canonical type: invalid function cv qualifiers");
		}
		if (function_ref != ReferenceQualifier::None &&
			function_ref != ReferenceQualifier::LValueReference &&
			function_ref != ReferenceQualifier::RValueReference) {
			throw InternalError("canonical type: invalid function ref qualifier");
		}
		if (calling_convention >= CanonicalCallingConvention::Count) {
			throw InternalError("canonical type: invalid calling convention");
		}
		if (is_noexcept && dependent_noexcept) {
			throw InternalError("canonical type: plain noexcept cannot combine with dependent noexcept");
		}
		const CanonicalTypeNode return_node = nodeUnlocked(return_type);
		if (return_node.kind == CanonicalTypeKind::Function ||
			isInternalLink(return_node.kind) ||
			return_node.kind == CanonicalTypeKind::Array) {
			throw InternalError("canonical type: invalid function return type");
		}
		CanonicalTypeNodeFlags flags = CanonicalTypeNodeFlags::None;
		if (is_variadic) {
			flags |= CanonicalTypeNodeFlags::VariadicFunction;
		}
		if (is_noexcept) {
			flags |= CanonicalTypeNodeFlags::NoexceptFunction;
		}
		if (dependent_noexcept) {
			flags |= CanonicalTypeNodeFlags::DependentNoexceptFunction;
		}
		if (function_ref == ReferenceQualifier::LValueReference) {
			flags |= CanonicalTypeNodeFlags::FunctionLValueRef;
		} else if (function_ref == ReferenceQualifier::RValueReference) {
			flags |= CanonicalTypeNodeFlags::FunctionRValueRef;
		}
		if (dll_linkage == CanonicalDllLinkage::Import) {
			flags |= CanonicalTypeNodeFlags::FunctionDllImport;
		} else if (dll_linkage == CanonicalDllLinkage::Export) {
			flags |= CanonicalTypeNodeFlags::FunctionDllExport;
		}
		TypeId param_link{};
		for (size_t index = parameters.size(); index-- > 0;) {
			const TypeId parameter = parameters[index];
			const CanonicalTypeNode parameter_node = nodeUnlocked(parameter);
			if (parameter_node.kind == CanonicalTypeKind::Function ||
				isInternalLink(parameter_node.kind) ||
				parameter_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: undecayed function parameter type");
			}
			param_link = internUnlocked({
				.child = param_link,
				.kind = CanonicalTypeKind::FunctionParam,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = parameter.value,
			});
		}
		return internUnlocked({
			.child = return_type,
			.kind = CanonicalTypeKind::Function,
			.builtin = static_cast<CanonicalBuiltinKind>(calling_convention),
			.qualifiers = function_cv,
			.flags = flags,
			.array_extent = packFunctionArrayExtent(param_link, dependent_noexcept),
		});
	}

	// Opaque class identity for member-pointer owners. Complete-object layout and
	// field schemas are published separately by EntityId; EntityId remains this
	// node's only key.
	TypeId record(EntityId entity) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!entity) {
			throw InternalError("canonical type: invalid record EntityId");
		}
		return internUnlocked({
			.child = TypeId{},
			.kind = CanonicalTypeKind::Record,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = entity.value,
		});
	}

	// Opaque enum identity. Underlying-type layout is published separately by
	// EntityId; EntityId remains this node's only key.
	TypeId enumeration(EntityId entity) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!entity) {
			throw InternalError("canonical type: invalid enum EntityId");
		}
		return internUnlocked({
			.child = TypeId{},
			.kind = CanonicalTypeKind::Enum,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = entity.value,
		});
	}

	// Opaque type-template-parameter identity. Spelling is never part of this
	// key; callers publish TemplateDeclId + parameter index separately from the
	// legacy StringHandle binding used for lookup.
	TypeId templateParameter(TemplateDeclId template_decl, uint32_t parameter_index) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!template_decl) {
			throw InternalError("canonical type: invalid template-parameter TemplateDeclId");
		}
		return internUnlocked({
			.child = TypeId{},
			.kind = CanonicalTypeKind::TemplateParameter,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = packTemplateParameterExtent(template_decl, parameter_index),
		});
	}

	// Type-only class-template specialization identity: primary TemplateDeclId
	// plus a linked list of argument TypeIds. NTTP / template-template / pack
	// arguments are out of scope; callers must not invent TemplateDeclIds.
	TypeId templateSpecialization(TemplateDeclId primary, std::span<const TypeId> arguments) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!primary) {
			throw InternalError("canonical type: invalid specialization TemplateDeclId");
		}
		TypeId arg_link{};
		for (size_t index = arguments.size(); index-- > 0;) {
			const TypeId argument = arguments[index];
			const CanonicalTypeNode argument_node = nodeUnlocked(argument);
			if (isInternalLink(argument_node.kind) ||
				argument_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: invalid template specialization argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::TemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.value,
			});
		}
		return internUnlocked({
			.child = arg_link,
			.kind = CanonicalTypeKind::TemplateSpecialization,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = primary.value,
		});
	}

	// Plain identifier members of unknown specializations. Qualifiers may be a
	// published type parameter, a type-only TemplateSpecialization (Primary<Args>),
	// or a prior dependent-name-family node. The caller supplies one normalized
	// identifier token, not a qualified spelling to recover or a lookup result
	// ([temp.dep.type]).
	TypeId dependentName(TypeId qualifier, std::string_view identifier) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!isDependentQualifierKind(nodeUnlocked(qualifier).kind)) {
			throw InternalError("canonical type: unsupported dependent-name qualifier");
		}
		return internUnlocked({
			.child = qualifier,
			.kind = CanonicalTypeKind::DependentName,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = packIdentifierBytesUnlocked(identifier).value,
		});
	}

	// Unresolved member template-id (T::Foo<Args> or Primary<Args>::Foo<U>)
	// without a published member TemplateDeclId. Identity is qualifier +
	// identifier content + type-only argument TypeIds. Qualifiers may include a
	// type-only TemplateSpecialization. ::template is a parse disambiguator and
	// is not stored.
	TypeId dependentTemplateMember(
		TypeId qualifier,
		std::string_view identifier,
		std::span<const TypeId> arguments) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!isDependentQualifierKind(nodeUnlocked(qualifier).kind)) {
			throw InternalError("canonical type: unsupported dependent template-member qualifier");
		}
		TypeId arg_link{};
		for (size_t index = arguments.size(); index-- > 0;) {
			const TypeId argument = arguments[index];
			const CanonicalTypeNode argument_node = nodeUnlocked(argument);
			if (isInternalLink(argument_node.kind) ||
				argument_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: invalid dependent template-member argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::TemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.value,
			});
		}
		return internUnlocked({
			.child = qualifier,
			.kind = CanonicalTypeKind::DependentTemplateMember,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = packDependentTemplateMemberExtent(
				packIdentifierBytesUnlocked(identifier), arg_link),
		});
	}

	TypeId dependentNameQualifier(TypeId type) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(type);
		if (input.kind != CanonicalTypeKind::DependentName &&
			input.kind != CanonicalTypeKind::DependentTemplateMember) {
			throw InternalError("canonical type: expected dependent name");
		}
		return input.child;
	}

	std::string dependentNameIdentifier(TypeId type) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(type);
		TypeId name_link{};
		if (input.kind == CanonicalTypeKind::DependentName) {
			name_link = TypeId{static_cast<uint32_t>(input.array_extent)};
		} else if (input.kind == CanonicalTypeKind::DependentTemplateMember) {
			name_link = unpackDependentTemplateMemberName(input.array_extent);
		} else {
			throw InternalError("canonical type: expected dependent name");
		}
		std::string identifier;
		TypeId cursor = name_link;
		while (cursor) {
			const auto bytes = nodeUnlocked(cursor);
			for (uint8_t index = 0; index < static_cast<uint8_t>(bytes.builtin); ++index) {
				identifier.push_back(static_cast<char>((bytes.array_extent >> (index * 8)) & 0xff));
			}
			cursor = bytes.child;
		}
		return identifier;
	}

	TypeId dependentTemplateMemberArguments(TypeId type) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(type);
		if (input.kind != CanonicalTypeKind::DependentTemplateMember) {
			throw InternalError("canonical type: expected dependent template member");
		}
		return unpackDependentTemplateMemberArgs(input.array_extent);
	}

	// Structural substitution for one published primary's type parameters.
	// Replaces TemplateParameter(env, i) with args[i], rebuilds Spec / DependentName
	// / DependentTemplateMember / cv / pointer / array / reference wrappers, and
	// leaves unresolved member tips as DependentName-family nodes (no lookup).
	// Function and member-pointer walks, NTTP/pack args, and production wiring
	// stay deferred. Iterative: logical depth does not map to native call depth.
	TypeId substitute(TypeId type, TemplateDeclId env, std::span<const TypeId> args) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return substituteUnlocked(type, env, args);
	}

	TypeId memberObjectPointer(TypeId owner, TypeId pointee) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const TypeId record_owner = recordOwnerUnlocked(owner);
		const CanonicalTypeNode pointee_node = nodeUnlocked(pointee);
		if (pointee_node.kind == CanonicalTypeKind::Function ||
			isInternalLink(pointee_node.kind) ||
			(pointee_node.kind == CanonicalTypeKind::Builtin &&
				pointee_node.builtin == CanonicalBuiltinKind::Void)) {
			throw InternalError("canonical type: invalid member object pointee");
		}
		return internUnlocked({
			.child = pointee,
			.kind = CanonicalTypeKind::MemberObjectPointer,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = record_owner.value,
		});
	}

	TypeId memberFunctionPointer(TypeId owner, TypeId function) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const TypeId record_owner = recordOwnerUnlocked(owner);
		if (nodeUnlocked(function).kind != CanonicalTypeKind::Function) {
			throw InternalError("canonical type: member function pointee must be a function type");
		}
		return internUnlocked({
			.child = function,
			.kind = CanonicalTypeKind::MemberFunctionPointer,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = record_owner.value,
		});
	}

	TypeId withoutTopLevelQualifiers(TypeId id) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(id);
		return input.kind == CanonicalTypeKind::Qualified ? input.child : id;
	}

	CanonicalTypeNode node(TypeId id) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return nodeUnlocked(id);
	}

	TypeId functionParameterType(TypeId param_link) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(param_link);
		if (input.kind != CanonicalTypeKind::FunctionParam) {
			throw InternalError("canonical type: TypeId is not a function parameter link");
		}
		if (input.array_extent == 0 || input.array_extent > live_count_) {
			throw InternalError("canonical type: function parameter TypeId is outside this table");
		}
		return TypeId{static_cast<uint32_t>(input.array_extent)};
	}

	TypeId functionParameterNext(TypeId param_link) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(param_link);
		if (input.kind != CanonicalTypeKind::FunctionParam) {
			throw InternalError("canonical type: TypeId is not a function parameter link");
		}
		return input.child;
	}

	TypeId functionParameters(TypeId function) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(function);
		if (input.kind != CanonicalTypeKind::Function) {
			throw InternalError("canonical type: TypeId is not a function type");
		}
		const TypeId param_link = unpackFunctionParamLink(input.array_extent);
		if (!param_link) {
			return {};
		}
		if (param_link.value > live_count_) {
			throw InternalError("canonical type: function parameter list is outside this table");
		}
		return param_link;
	}

	ExprId functionDependentNoexcept(TypeId function) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(function);
		if (input.kind != CanonicalTypeKind::Function) {
			throw InternalError("canonical type: TypeId is not a function type");
		}
		const ExprId dependent = unpackFunctionDependentNoexcept(input.array_extent);
		const bool flagged = hasCanonicalTypeNodeFlag(
			input.flags, CanonicalTypeNodeFlags::DependentNoexceptFunction);
		if (flagged != static_cast<bool>(dependent)) {
			throw InternalError("canonical type: dependent noexcept flag/extent mismatch");
		}
		return dependent;
	}

	TypeId memberPointerOwner(TypeId member_pointer) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(member_pointer);
		if (input.kind != CanonicalTypeKind::MemberObjectPointer &&
			input.kind != CanonicalTypeKind::MemberFunctionPointer) {
			throw InternalError("canonical type: TypeId is not a member pointer");
		}
		if (input.array_extent == 0 || input.array_extent > live_count_) {
			throw InternalError("canonical type: member pointer owner is outside this table");
		}
		return TypeId{static_cast<uint32_t>(input.array_extent)};
	}

	TypeId memberPointerPointee(TypeId member_pointer) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(member_pointer);
		if (input.kind != CanonicalTypeKind::MemberObjectPointer &&
			input.kind != CanonicalTypeKind::MemberFunctionPointer) {
			throw InternalError("canonical type: TypeId is not a member pointer");
		}
		return input.child;
	}

	EntityId recordEntity(TypeId record) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(record);
		if (input.kind != CanonicalTypeKind::Record) {
			throw InternalError("canonical type: TypeId is not a record");
		}
		return EntityId{static_cast<uint32_t>(input.array_extent)};
	}

	EntityId enumEntity(TypeId enumeration) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(enumeration);
		if (input.kind != CanonicalTypeKind::Enum) {
			throw InternalError("canonical type: TypeId is not an enum");
		}
		return EntityId{static_cast<uint32_t>(input.array_extent)};
	}

	TemplateDeclId templateParameterDecl(TypeId parameter) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(parameter);
		if (input.kind != CanonicalTypeKind::TemplateParameter) {
			throw InternalError("canonical type: TypeId is not a template parameter");
		}
		return unpackTemplateParameterDecl(input.array_extent);
	}

	uint32_t templateParameterIndex(TypeId parameter) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(parameter);
		if (input.kind != CanonicalTypeKind::TemplateParameter) {
			throw InternalError("canonical type: TypeId is not a template parameter");
		}
		return unpackTemplateParameterIndex(input.array_extent);
	}

	TemplateDeclId templateSpecializationDecl(TypeId specialization) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(specialization);
		if (input.kind != CanonicalTypeKind::TemplateSpecialization) {
			throw InternalError("canonical type: TypeId is not a template specialization");
		}
		return TemplateDeclId{static_cast<uint32_t>(input.array_extent)};
	}

	TypeId templateSpecializationArguments(TypeId specialization) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(specialization);
		if (input.kind != CanonicalTypeKind::TemplateSpecialization) {
			throw InternalError("canonical type: TypeId is not a template specialization");
		}
		if (!input.child) {
			return TypeId{};
		}
		if (input.child.value > live_count_) {
			throw InternalError("canonical type: invalid template argument link");
		}
		return input.child;
	}

	TypeId templateArgumentType(TypeId arg_link) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(arg_link);
		if (input.kind != CanonicalTypeKind::TemplateArg) {
			throw InternalError("canonical type: TypeId is not a template argument link");
		}
		return TypeId{static_cast<uint32_t>(input.array_extent)};
	}

	TypeId templateArgumentNext(TypeId arg_link) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto input = nodeUnlocked(arg_link);
		if (input.kind != CanonicalTypeKind::TemplateArg) {
			throw InternalError("canonical type: TypeId is not a template argument link");
		}
		return input.child;
	}

	void publishRecordLayout(CanonicalRecordLayout layout) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!layout.entity || layout.size_bytes == 0 || layout.alignment == 0 ||
			(layout.alignment & (layout.alignment - 1u)) != 0 ||
			layout.layout_data_size_bytes > layout.size_bytes ||
			layout.non_virtual_size_bytes > layout.size_bytes) {
			throw InternalError("canonical type: invalid complete record layout");
		}
		publishLayoutUnlocked(record_layouts_, live_record_layout_count_, record_layout_ids_, layout,
			"canonical type: conflicting record layout publication");
		noteArenaBytes();
	}

	void publishEnumLayout(CanonicalEnumLayout layout) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!layout.entity || !layout.underlying_type || layout.size_bytes == 0) {
			throw InternalError("canonical type: invalid complete enum layout");
		}
		const CanonicalTypeNode underlying = nodeUnlocked(layout.underlying_type);
		if (underlying.kind != CanonicalTypeKind::Builtin ||
			underlying.builtin == CanonicalBuiltinKind::Void ||
			underlying.builtin == CanonicalBuiltinKind::Float ||
			underlying.builtin == CanonicalBuiltinKind::Double ||
			underlying.builtin == CanonicalBuiltinKind::LongDouble ||
			underlying.builtin == CanonicalBuiltinKind::Nullptr) {
			throw InternalError("canonical type: enum underlying type is not an integer builtin");
		}
		publishLayoutUnlocked(enum_layouts_, live_enum_layout_count_, enum_layout_ids_, layout,
			"canonical type: conflicting enum layout publication");
		noteArenaBytes();
	}

	bool hasRecordLayout(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return entity && record_layout_ids_.contains(entity.value);
	}

	bool hasEnumLayout(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return entity && enum_layout_ids_.contains(entity.value);
	}

	CanonicalRecordLayout recordLayout(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto found = record_layout_ids_.find(entity.value);
		if (!entity || found == record_layout_ids_.end()) {
			throw InternalError("canonical type: record has no complete layout");
		}
		return record_layouts_[found->second];
	}

	CanonicalEnumLayout enumLayout(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const auto found = enum_layout_ids_.find(entity.value);
		if (!entity || found == enum_layout_ids_.end()) {
			throw InternalError("canonical type: enum has no complete layout");
		}
		return enum_layouts_[found->second];
	}

	void publishRecordFieldSchema(EntityId entity,
		std::span<const CanonicalRecordMember> members,
		std::span<const CanonicalRecordBase> bases) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!entity) {
			throw InternalError("canonical type: invalid record field schema entity");
		}
		const auto layout_found = record_layout_ids_.find(entity.value);
		if (layout_found == record_layout_ids_.end()) {
			throw InternalError("canonical type: record field schema requires complete layout");
		}
		const CanonicalRecordLayout layout = record_layouts_[layout_found->second];
		if (members.size() != layout.member_count || bases.size() != layout.direct_base_count) {
			throw InternalError("canonical type: record field schema count mismatch");
		}
		for (const CanonicalRecordMember& member : members) {
			validateRecordMemberUnlocked(member);
		}
		for (const CanonicalRecordBase& base : bases) {
			validateRecordBaseUnlocked(base);
		}
		const auto existing = record_field_schema_ids_.find(entity.value);
		if (existing != record_field_schema_ids_.end()) {
			const CanonicalRecordFieldSchemaHeader header =
				record_field_schema_headers_[existing->second];
			if (header.member_count != members.size() || header.base_count != bases.size()) {
				throw InternalError("canonical type: conflicting record field schema publication");
			}
			for (size_t index = 0; index < members.size(); ++index) {
				if (record_members_[header.member_begin + index] != members[index]) {
					throw InternalError("canonical type: conflicting record field schema publication");
				}
			}
			for (size_t index = 0; index < bases.size(); ++index) {
				if (record_bases_[header.base_begin + index] != bases[index]) {
					throw InternalError("canonical type: conflicting record field schema publication");
				}
			}
			return;
		}
		const uint32_t member_begin = static_cast<uint32_t>(live_record_member_count_);
		const uint32_t base_begin = static_cast<uint32_t>(live_record_base_count_);
		if (static_cast<uint64_t>(member_begin) + members.size() >
				std::numeric_limits<uint32_t>::max() ||
			static_cast<uint64_t>(base_begin) + bases.size() >
				std::numeric_limits<uint32_t>::max()) {
			throw InternalError("canonical type: record field schema arena exhausted");
		}
		for (const CanonicalRecordMember& member : members) {
			appendSchemaEntryUnlocked(record_members_, live_record_member_count_, member);
		}
		for (const CanonicalRecordBase& base : bases) {
			appendSchemaEntryUnlocked(record_bases_, live_record_base_count_, base);
		}
		const CanonicalRecordFieldSchemaHeader header{
			.entity = entity,
			.member_begin = member_begin,
			.base_begin = base_begin,
			.member_count = static_cast<uint16_t>(members.size()),
			.base_count = static_cast<uint16_t>(bases.size()),
		};
		const size_t header_index = live_record_field_schema_count_;
		appendSchemaEntryUnlocked(record_field_schema_headers_, live_record_field_schema_count_,
			header);
		try {
			record_field_schema_ids_.emplace(entity.value, header_index);
		} catch (...) {
			live_record_field_schema_count_ = header_index;
			live_record_member_count_ = member_begin;
			live_record_base_count_ = base_begin;
			noteArenaBytes();
			throw;
		}
		noteArenaBytes();
	}

	bool hasRecordFieldSchema(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return entity && record_field_schema_ids_.contains(entity.value);
	}

	CanonicalRecordMember recordMemberAt(EntityId entity, size_t index) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const CanonicalRecordFieldSchemaHeader header = fieldSchemaHeaderUnlocked(entity);
		if (index >= header.member_count) {
			throw InternalError("canonical type: record member index out of range");
		}
		return record_members_[header.member_begin + index];
	}

	CanonicalRecordBase recordBaseAt(EntityId entity, size_t index) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		const CanonicalRecordFieldSchemaHeader header = fieldSchemaHeaderUnlocked(entity);
		if (index >= header.base_count) {
			throw InternalError("canonical type: record base index out of range");
		}
		return record_bases_[header.base_begin + index];
	}

	// Publish nested type members (typedef / using / nested class targets) keyed by
	// EntityId. Identifier content is NameBytes; StringHandle is never stored.
	// Equal republish is idempotent; conflicting content throws. Layout is not
	// required — this schema is independent of CanonicalRecordMember field layout.
	void publishRecordNamedTypeMembers(EntityId entity,
		std::span<const CanonicalNamedTypeMemberSpec> members) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (!entity) {
			throw InternalError("canonical type: invalid named type-member schema entity");
		}
		std::vector<CanonicalNamedTypeMember> packed;
		packed.reserve(members.size());
		for (const CanonicalNamedTypeMemberSpec& member : members) {
			if (isInternalLink(nodeUnlocked(member.type).kind)) {
				throw InternalError("canonical type: internal link is not a named type-member type");
			}
			const TypeId name_link = packIdentifierBytesUnlocked(member.name);
			for (const CanonicalNamedTypeMember& prior : packed) {
				if (prior.name == name_link) {
					throw InternalError("canonical type: duplicate named type-member identifier");
				}
			}
			packed.push_back({
				.type = member.type,
				.name = name_link,
			});
		}
		const auto existing = named_type_member_schema_ids_.find(entity.value);
		if (existing != named_type_member_schema_ids_.end()) {
			const CanonicalNamedTypeMemberSchemaHeader header =
				named_type_member_schema_headers_[existing->second];
			if (header.member_count != packed.size()) {
				throw InternalError("canonical type: conflicting named type-member schema");
			}
			for (size_t index = 0; index < packed.size(); ++index) {
				if (named_type_members_[header.member_begin + index] != packed[index]) {
					throw InternalError("canonical type: conflicting named type-member schema");
				}
			}
			return;
		}
		const uint32_t member_begin = static_cast<uint32_t>(live_named_type_member_count_);
		if (static_cast<uint64_t>(member_begin) + packed.size() >
			std::numeric_limits<uint32_t>::max()) {
			throw InternalError("canonical type: named type-member schema arena exhausted");
		}
		for (const CanonicalNamedTypeMember& member : packed) {
			appendSchemaEntryUnlocked(named_type_members_, live_named_type_member_count_, member);
		}
		const CanonicalNamedTypeMemberSchemaHeader header{
			.entity = entity,
			.member_begin = member_begin,
			.member_count = static_cast<uint16_t>(packed.size()),
			.reserved = 0,
			.reserved2 = 0,
		};
		const size_t header_index = live_named_type_member_schema_count_;
		appendSchemaEntryUnlocked(named_type_member_schema_headers_,
			live_named_type_member_schema_count_, header);
		try {
			named_type_member_schema_ids_.emplace(entity.value, header_index);
		} catch (...) {
			live_named_type_member_schema_count_ = header_index;
			live_named_type_member_count_ = member_begin;
			noteArenaBytes();
			throw;
		}
		noteArenaBytes();
	}

	bool hasRecordNamedTypeMembers(EntityId entity) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return entity && named_type_member_schema_ids_.contains(entity.value);
	}

	std::optional<TypeId> tryLookupNamedTypeMember(EntityId entity, std::string_view name) const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return tryLookupNamedTypeMemberUnlocked(entity, name);
	}

	// Collapse plain DependentName chains when every step is Record + published
	// named type-member. Miss / DependentTemplateMember / non-Record qualifier
	// leaves the tip unchanged. No StringHandle identity and no SymbolTable.
	TypeId tryResolveDependentTip(TypeId type) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return tryResolveDependentTipUnlocked(type);
	}

	size_t size() const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return live_count_;
	}

	CanonicalTypeArenaStats arenaStats() const {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		return {usedBytesUnlocked(), reservedBytesUnlocked()};
	}

private:
	struct CanonicalRecordFieldSchemaHeader {
		EntityId entity;
		uint32_t member_begin;
		uint32_t base_begin;
		uint16_t member_count;
		uint16_t base_count;
		friend bool operator==(CanonicalRecordFieldSchemaHeader,
			CanonicalRecordFieldSchemaHeader) = default;
	};
	static_assert(sizeof(CanonicalRecordFieldSchemaHeader) == 16);

	struct CanonicalNamedTypeMemberSchemaHeader {
		EntityId entity;
		uint32_t member_begin;
		uint16_t member_count;
		uint16_t reserved;
		uint32_t reserved2;
		friend bool operator==(CanonicalNamedTypeMemberSchemaHeader,
			CanonicalNamedTypeMemberSchemaHeader) = default;
	};
	static_assert(sizeof(CanonicalNamedTypeMemberSchemaHeader) == 16);

	struct TransactionMark {
		size_t node_count;
		size_t record_layout_count;
		size_t enum_layout_count;
		size_t record_field_schema_count;
		size_t record_member_count;
		size_t record_base_count;
		size_t named_type_member_schema_count;
		size_t named_type_member_count;
	};

	struct NodeHash {
		size_t operator()(CanonicalTypeNode node) const {
			const uint64_t key = static_cast<uint64_t>(node.child.value)
				| (static_cast<uint64_t>(node.kind) << 32)
				| (static_cast<uint64_t>(node.builtin) << 40)
				| (static_cast<uint64_t>(node.qualifiers) << 48)
				| (static_cast<uint64_t>(node.flags) << 56);
			const size_t first = std::hash<uint64_t>{}(key);
			const size_t second = std::hash<uint64_t>{}(node.array_extent);
			return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
		}
	};

	static bool isReference(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::LValueReference || kind == CanonicalTypeKind::RValueReference;
	}

	static bool isInternalLink(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::FunctionParam || kind == CanonicalTypeKind::TemplateArg ||
			kind == CanonicalTypeKind::NameBytes;
	}

	static bool isDependentQualifierKind(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::TemplateParameter ||
			kind == CanonicalTypeKind::TemplateSpecialization ||
			kind == CanonicalTypeKind::DependentName ||
			kind == CanonicalTypeKind::DependentTemplateMember;
	}

	static bool isDependentNameFamily(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::DependentName ||
			kind == CanonicalTypeKind::DependentTemplateMember;
	}

	TypeId packIdentifierBytesUnlocked(std::string_view identifier) {
		if (identifier.empty() || identifier.find('\0') != std::string_view::npos) {
			throw InternalError("canonical type: invalid dependent identifier");
		}
		TypeId name_link{};
		for (size_t end = identifier.size(); end != 0;) {
			const size_t begin = ((end - 1) / 8) * 8;
			uint64_t bytes = 0;
			for (size_t index = begin; index < end; ++index) {
				bytes |= static_cast<uint64_t>(static_cast<unsigned char>(identifier[index])) << ((index - begin) * 8);
			}
			name_link = internUnlocked({
				.child = name_link,
				.kind = CanonicalTypeKind::NameBytes,
				.builtin = static_cast<CanonicalBuiltinKind>(end - begin),
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = bytes,
			});
			end = begin;
		}
		return name_link;
	}

	TypeId rebuildTemplateArgListUnlocked(std::span<const TypeId> arguments) {
		TypeId arg_link{};
		for (size_t index = arguments.size(); index-- > 0;) {
			const TypeId argument = arguments[index];
			const CanonicalTypeNode argument_node = nodeUnlocked(argument);
			if (isInternalLink(argument_node.kind) ||
				argument_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: invalid substituted template argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::TemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.value,
			});
		}
		return arg_link;
	}

	TypeId substituteUnlocked(TypeId type, TemplateDeclId env, std::span<const TypeId> args) {
		if (!type) {
			throw InternalError("canonical type: invalid substitute TypeId");
		}
		if (!env) {
			throw InternalError("canonical type: invalid substitute TemplateDeclId");
		}
		for (const TypeId argument : args) {
			if (!argument) {
				throw InternalError("canonical type: invalid substitute argument TypeId");
			}
			const CanonicalTypeKind argument_kind = nodeUnlocked(argument).kind;
			if (isInternalLink(argument_kind)) {
				throw InternalError("canonical type: substitute argument is an internal link");
			}
		}

		struct Frame {
			TypeId id;
			bool building;
		};
		std::vector<Frame> stack;
		std::unordered_map<uint32_t, TypeId> memo;
		stack.push_back({type, false});
		std::vector<TypeId> rebuilt_args;

		while (!stack.empty()) {
			Frame frame = stack.back();
			if (const auto existing = memo.find(frame.id.value); existing != memo.end()) {
				stack.pop_back();
				continue;
			}
			const CanonicalTypeNode node = nodeUnlocked(frame.id);
			if (!frame.building) {
				switch (node.kind) {
				case CanonicalTypeKind::Builtin:
				case CanonicalTypeKind::Record:
				case CanonicalTypeKind::Enum:
					memo.emplace(frame.id.value, frame.id);
					stack.pop_back();
					continue;
				case CanonicalTypeKind::TemplateParameter: {
					const TemplateDeclId decl = unpackTemplateParameterDecl(node.array_extent);
					if (decl != env) {
						memo.emplace(frame.id.value, frame.id);
						stack.pop_back();
						continue;
					}
					const uint32_t index = unpackTemplateParameterIndex(node.array_extent);
					if (index >= args.size()) {
						throw InternalError("canonical type: substitute argument index out of range");
					}
					memo.emplace(frame.id.value, args[index]);
					stack.pop_back();
					continue;
				}
				case CanonicalTypeKind::Function:
				case CanonicalTypeKind::MemberObjectPointer:
				case CanonicalTypeKind::MemberFunctionPointer:
					throw InternalError("canonical type: substitute does not walk function or member-pointer types");
				case CanonicalTypeKind::FunctionParam:
				case CanonicalTypeKind::TemplateArg:
				case CanonicalTypeKind::NameBytes:
					throw InternalError("canonical type: substitute root cannot be an internal link");
				case CanonicalTypeKind::Qualified:
				case CanonicalTypeKind::Pointer:
				case CanonicalTypeKind::LValueReference:
				case CanonicalTypeKind::RValueReference:
				case CanonicalTypeKind::Array:
				case CanonicalTypeKind::DependentName:
					stack.back().building = true;
					stack.push_back({node.child, false});
					continue;
				case CanonicalTypeKind::TemplateSpecialization:
				case CanonicalTypeKind::DependentTemplateMember: {
					stack.back().building = true;
					TypeId arg_link = node.kind == CanonicalTypeKind::TemplateSpecialization
						? node.child
						: unpackDependentTemplateMemberArgs(node.array_extent);
					while (arg_link) {
						const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
						if (arg_node.kind != CanonicalTypeKind::TemplateArg) {
							throw InternalError("canonical type: corrupt template argument link");
						}
						stack.push_back({TypeId{static_cast<uint32_t>(arg_node.array_extent)}, false});
						arg_link = arg_node.child;
					}
					if (node.kind == CanonicalTypeKind::DependentTemplateMember) {
						stack.push_back({node.child, false});
					}
					continue;
				}
				}
				throw InternalError("canonical type: unsupported substitute node kind");
			}

			stack.pop_back();
			TypeId rebuilt = frame.id;
			switch (node.kind) {
			case CanonicalTypeKind::Qualified: {
				const TypeId child = memo.at(node.child.value);
				if (child == node.child) {
					rebuilt = frame.id;
				} else {
					rebuilt = internUnlocked({
						.child = child,
						.kind = CanonicalTypeKind::Qualified,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = node.qualifiers,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = 0,
					});
				}
				break;
			}
			case CanonicalTypeKind::Pointer: {
				const TypeId child = memo.at(node.child.value);
				if (child == node.child) {
					rebuilt = frame.id;
				} else {
					rebuilt = internUnlocked({
						.child = child,
						.kind = CanonicalTypeKind::Pointer,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = 0,
					});
				}
				break;
			}
			case CanonicalTypeKind::LValueReference:
			case CanonicalTypeKind::RValueReference: {
				TypeId referent = memo.at(node.child.value);
				CanonicalTypeKind kind = node.kind;
				CanonicalTypeNode referent_node = nodeUnlocked(referent);
				if (isReference(referent_node.kind)) {
					if (referent_node.kind == CanonicalTypeKind::LValueReference) {
						kind = CanonicalTypeKind::LValueReference;
					}
					referent = referent_node.child;
					referent_node = nodeUnlocked(referent);
				}
				if (referent == node.child && kind == node.kind) {
					rebuilt = frame.id;
				} else {
					rebuilt = internUnlocked({
						.child = referent,
						.kind = kind,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = 0,
					});
				}
				break;
			}
			case CanonicalTypeKind::Array: {
				const TypeId child = memo.at(node.child.value);
				if (child == node.child) {
					rebuilt = frame.id;
				} else {
					rebuilt = internUnlocked({
						.child = child,
						.kind = CanonicalTypeKind::Array,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = node.flags,
						.array_extent = node.array_extent,
					});
				}
				break;
			}
			case CanonicalTypeKind::DependentName: {
				const TypeId qualifier = memo.at(node.child.value);
				if (qualifier == node.child) {
					rebuilt = frame.id;
				} else if (isInternalLink(nodeUnlocked(qualifier).kind)) {
					throw InternalError("canonical type: substituted dependent-name qualifier is an internal link");
				} else {
					// Concrete qualifiers remain DependentName tips until lookup.
					rebuilt = internUnlocked({
						.child = qualifier,
						.kind = CanonicalTypeKind::DependentName,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = node.array_extent,
					});
				}
				break;
			}
			case CanonicalTypeKind::TemplateSpecialization: {
				rebuilt_args.clear();
				TypeId arg_link = node.child;
				bool unchanged = true;
				while (arg_link) {
					const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
					const TypeId original = TypeId{static_cast<uint32_t>(arg_node.array_extent)};
					const TypeId substituted = memo.at(original.value);
					unchanged = unchanged && substituted == original;
					rebuilt_args.push_back(substituted);
					arg_link = arg_node.child;
				}
				if (unchanged) {
					rebuilt = frame.id;
				} else {
					rebuilt = internUnlocked({
						.child = rebuildTemplateArgListUnlocked(rebuilt_args),
						.kind = CanonicalTypeKind::TemplateSpecialization,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = node.array_extent,
					});
				}
				break;
			}
			case CanonicalTypeKind::DependentTemplateMember: {
				const TypeId qualifier = memo.at(node.child.value);
				rebuilt_args.clear();
				TypeId arg_link = unpackDependentTemplateMemberArgs(node.array_extent);
				bool unchanged = qualifier == node.child;
				while (arg_link) {
					const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
					const TypeId original = TypeId{static_cast<uint32_t>(arg_node.array_extent)};
					const TypeId substituted = memo.at(original.value);
					unchanged = unchanged && substituted == original;
					rebuilt_args.push_back(substituted);
					arg_link = arg_node.child;
				}
				if (unchanged) {
					rebuilt = frame.id;
				} else if (isInternalLink(nodeUnlocked(qualifier).kind)) {
					throw InternalError("canonical type: substituted dependent template-member qualifier is an internal link");
				} else {
					rebuilt = internUnlocked({
						.child = qualifier,
						.kind = CanonicalTypeKind::DependentTemplateMember,
						.builtin = CanonicalBuiltinKind::Void,
						.qualifiers = CVQualifier::None,
						.flags = CanonicalTypeNodeFlags::None,
						.array_extent = packDependentTemplateMemberExtent(
							unpackDependentTemplateMemberName(node.array_extent),
							rebuildTemplateArgListUnlocked(rebuilt_args)),
					});
				}
				break;
			}
			default:
				throw InternalError("canonical type: unexpected substitute build kind");
			}
			memo.emplace(frame.id.value, rebuilt);
		}
		return memo.at(type.value);
	}

	static bool isMemberPointer(CanonicalTypeKind kind) {
		return kind == CanonicalTypeKind::MemberObjectPointer ||
			kind == CanonicalTypeKind::MemberFunctionPointer;
	}

	TypeId recordOwnerUnlocked(TypeId owner) const {
		CanonicalTypeNode input = nodeUnlocked(owner);
		if (input.kind == CanonicalTypeKind::Qualified) {
			owner = input.child;
			input = nodeUnlocked(owner);
		}
		if (input.kind != CanonicalTypeKind::Record) {
			throw InternalError("canonical type: member pointer owner must be a record");
		}
		return owner;
	}

	TypeId arrayUnlocked(TypeId element, uint64_t extent, CanonicalTypeNodeFlags flags) {
		CanonicalTypeNode element_node = nodeUnlocked(element);
		if (element_node.kind == CanonicalTypeKind::Qualified) {
			element_node = nodeUnlocked(element_node.child);
		}
		if (isReference(element_node.kind) ||
			element_node.kind == CanonicalTypeKind::Function ||
			isInternalLink(element_node.kind) ||
			(element_node.kind == CanonicalTypeKind::Builtin && element_node.builtin == CanonicalBuiltinKind::Void) ||
			(element_node.kind == CanonicalTypeKind::Array && element_node.flags != CanonicalTypeNodeFlags::KnownArrayBound)) {
			throw InternalError("canonical type: invalid array element type");
		}
		return internUnlocked({
			.child = element,
			.kind = CanonicalTypeKind::Array,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = flags,
			.array_extent = extent,
		});
	}

	CanonicalTypeNode nodeUnlocked(TypeId id) const {
		if (!id || id.value > live_count_) {
			throw InternalError("canonical type: TypeId is outside this table");
		}
		return nodes_[id.value - 1];
	}

	TypeId internUnlocked(CanonicalTypeNode node) {
		traceRequestUnlocked(node);
		const auto existing = ids_.find(node);
		if (existing != ids_.end()) {
			return existing->second;
		}
		if (live_count_ >= std::numeric_limits<uint32_t>::max()) {
			throw InternalError("canonical type: TypeId space exhausted");
		}
		const TypeId id{static_cast<uint32_t>(live_count_ + 1)};
		if (live_count_ == nodes_.size()) {
			nodes_.push_back(node);
		} else {
			// Reuse discarded slots; repeated failed probes must not repeatedly
			// reserve new chunks from ChunkedVector's monotonic allocator.
			nodes_[live_count_] = node;
		}
		++live_count_;
		noteArenaBytes();
		try {
			ids_.emplace(node, id);
		} catch (...) {
			--live_count_;
			noteArenaBytes();
			throw;
		}
		return id;
	}

	template<typename Layout>
	void publishLayoutUnlocked(
		ChunkedVector<Layout, 16>& layouts,
		size_t& live_count,
		std::unordered_map<uint32_t, size_t>& ids,
		Layout layout,
		const char* conflict_message) {
		const auto existing = ids.find(layout.entity.value);
		if (existing != ids.end()) {
			if (layouts[existing->second] != layout) {
				throw InternalError(conflict_message);
			}
			return;
		}
		const size_t index = live_count;
		if (index == layouts.size()) {
			layouts.push_back(layout);
		} else {
			layouts[index] = layout;
		}
		try {
			ids.emplace(layout.entity.value, index);
		} catch (...) {
			throw;
		}
		++live_count;
	}

	template<typename Entry, uint32_t ChunkSize>
	void appendSchemaEntryUnlocked(ChunkedVector<Entry, ChunkSize>& entries, size_t& live_count,
		Entry entry) {
		if (live_count == entries.size()) {
			entries.push_back(entry);
		} else {
			entries[live_count] = entry;
		}
		++live_count;
	}

	void validateRecordMemberUnlocked(const CanonicalRecordMember& member) const {
		if (isInternalLink(nodeUnlocked(member.type).kind)) {
			throw InternalError("canonical type: internal link is not a record member type");
		}
		const bool is_bitfield =
			hasCanonicalRecordMemberFlag(member.flags, CanonicalRecordMemberFlags::Bitfield);
		// Zero-width bitfields (C++ layout alignment directives) keep the Bitfield
		// flag with bit_width == 0. Only non-bitfields must not carry widths/offsets.
		if (!is_bitfield && (member.bit_width != 0 || member.bit_offset != 0)) {
			throw InternalError("canonical type: non-bitfield member has bitfield fields");
		}
	}

	void validateRecordBaseUnlocked(const CanonicalRecordBase& base) const {
		if (!base.entity) {
			throw InternalError("canonical type: record base requires EntityId");
		}
	}

	CanonicalRecordFieldSchemaHeader fieldSchemaHeaderUnlocked(EntityId entity) const {
		const auto found = record_field_schema_ids_.find(entity.value);
		if (!entity || found == record_field_schema_ids_.end()) {
			throw InternalError("canonical type: record has no field schema");
		}
		return record_field_schema_headers_[found->second];
	}

	bool nameBytesEqualUnlocked(TypeId name_link, std::string_view identifier) const {
		size_t offset = 0;
		TypeId cursor = name_link;
		while (cursor) {
			const CanonicalTypeNode bytes = nodeUnlocked(cursor);
			if (bytes.kind != CanonicalTypeKind::NameBytes) {
				throw InternalError("canonical type: expected NameBytes link");
			}
			const uint8_t count = static_cast<uint8_t>(bytes.builtin);
			if (offset + count > identifier.size()) {
				return false;
			}
			for (uint8_t index = 0; index < count; ++index) {
				const unsigned char stored =
					static_cast<unsigned char>((bytes.array_extent >> (index * 8)) & 0xff);
				if (stored != static_cast<unsigned char>(identifier[offset + index])) {
					return false;
				}
			}
			offset += count;
			cursor = bytes.child;
		}
		return offset == identifier.size();
	}

	std::optional<TypeId> tryLookupNamedTypeMemberUnlocked(EntityId entity,
		std::string_view name) const {
		if (!entity || name.empty() || name.find('\0') != std::string_view::npos) {
			return std::nullopt;
		}
		const auto found = named_type_member_schema_ids_.find(entity.value);
		if (found == named_type_member_schema_ids_.end()) {
			return std::nullopt;
		}
		const CanonicalNamedTypeMemberSchemaHeader header =
			named_type_member_schema_headers_[found->second];
		for (uint16_t index = 0; index < header.member_count; ++index) {
			const CanonicalNamedTypeMember& member =
				named_type_members_[header.member_begin + index];
			if (nameBytesEqualUnlocked(member.name, name)) {
				return member.type;
			}
		}
		return std::nullopt;
	}

	TypeId tryResolveDependentTipUnlocked(TypeId type) {
		if (!type) {
			throw InternalError("canonical type: invalid dependent tip TypeId");
		}
		if (nodeUnlocked(type).kind != CanonicalTypeKind::DependentName) {
			return type;
		}
		std::vector<TypeId> name_links;
		TypeId cursor = type;
		while (nodeUnlocked(cursor).kind == CanonicalTypeKind::DependentName) {
			const CanonicalTypeNode node = nodeUnlocked(cursor);
			name_links.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
			cursor = node.child;
		}
		if (nodeUnlocked(cursor).kind != CanonicalTypeKind::Record) {
			return type;
		}
		TypeId current = cursor;
		for (size_t index = name_links.size(); index-- > 0;) {
			if (nodeUnlocked(current).kind != CanonicalTypeKind::Record) {
				return type;
			}
			const EntityId entity = EntityId{static_cast<uint32_t>(
				nodeUnlocked(current).array_extent)};
			const auto found = named_type_member_schema_ids_.find(entity.value);
			if (found == named_type_member_schema_ids_.end()) {
				return type;
			}
			const CanonicalNamedTypeMemberSchemaHeader header =
				named_type_member_schema_headers_[found->second];
			std::optional<TypeId> matched;
			for (uint16_t member_index = 0; member_index < header.member_count; ++member_index) {
				const CanonicalNamedTypeMember& member =
					named_type_members_[header.member_begin + member_index];
				if (member.name == name_links[index]) {
					matched = member.type;
					break;
				}
			}
			if (!matched.has_value()) {
				return type;
			}
			current = *matched;
		}
		return current;
	}

	uint64_t usedBytesUnlocked() const {
		return static_cast<uint64_t>(live_count_) * sizeof(CanonicalTypeNode) +
			static_cast<uint64_t>(live_record_layout_count_) * sizeof(CanonicalRecordLayout) +
			static_cast<uint64_t>(live_enum_layout_count_) * sizeof(CanonicalEnumLayout) +
			static_cast<uint64_t>(live_record_field_schema_count_) *
				sizeof(CanonicalRecordFieldSchemaHeader) +
			static_cast<uint64_t>(live_record_member_count_) * sizeof(CanonicalRecordMember) +
			static_cast<uint64_t>(live_record_base_count_) * sizeof(CanonicalRecordBase) +
			static_cast<uint64_t>(live_named_type_member_schema_count_) *
				sizeof(CanonicalNamedTypeMemberSchemaHeader) +
			static_cast<uint64_t>(live_named_type_member_count_) * sizeof(CanonicalNamedTypeMember);
	}

	uint64_t reservedBytesUnlocked() const {
		return nodes_.reservedBytes() + record_layouts_.reservedBytes() +
			enum_layouts_.reservedBytes() + record_field_schema_headers_.reservedBytes() +
			record_members_.reservedBytes() + record_bases_.reservedBytes() +
			named_type_member_schema_headers_.reservedBytes() +
			named_type_members_.reservedBytes();
	}

	void noteArenaBytes() {
		if (accounting_ != nullptr) {
			accounting_->update(SemanticArenaComponent::Types, usedBytesUnlocked(), reservedBytesUnlocked());
		}
	}

	void checkTransactionThread() const {
		if (!transaction_marks_.empty() && transaction_owner_ != std::this_thread::get_id()) {
			throw InternalError("canonical type transaction belongs to another thread");
		}
	}

	size_t beginTransaction() {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		transaction_marks_.push_back({
			live_count_,
			live_record_layout_count_,
			live_enum_layout_count_,
			live_record_field_schema_count_,
			live_record_member_count_,
			live_record_base_count_,
			live_named_type_member_schema_count_,
			live_named_type_member_count_,
		});
		transaction_owner_ = std::this_thread::get_id();
		return transaction_marks_.size();
	}

	void finishTransaction(size_t depth, bool commit) {
		std::lock_guard lock(mutex_);
		checkTransactionThread();
		if (depth == 0 || depth != transaction_marks_.size()) {
			throw InternalError("canonical type transactions must finish in nesting order");
		}
		if (!commit) {
			const TransactionMark mark = transaction_marks_.back();
			while (live_count_ > mark.node_count) {
				ids_.erase(nodes_[live_count_ - 1]);
				--live_count_;
			}
			while (live_record_layout_count_ > mark.record_layout_count) {
				record_layout_ids_.erase(record_layouts_[live_record_layout_count_ - 1].entity.value);
				--live_record_layout_count_;
			}
			while (live_enum_layout_count_ > mark.enum_layout_count) {
				enum_layout_ids_.erase(enum_layouts_[live_enum_layout_count_ - 1].entity.value);
				--live_enum_layout_count_;
			}
			while (live_record_field_schema_count_ > mark.record_field_schema_count) {
				record_field_schema_ids_.erase(
					record_field_schema_headers_[live_record_field_schema_count_ - 1].entity.value);
				--live_record_field_schema_count_;
			}
			live_record_member_count_ = mark.record_member_count;
			live_record_base_count_ = mark.record_base_count;
			while (live_named_type_member_schema_count_ > mark.named_type_member_schema_count) {
				named_type_member_schema_ids_.erase(
					named_type_member_schema_headers_[live_named_type_member_schema_count_ - 1]
						.entity.value);
				--live_named_type_member_schema_count_;
			}
			live_named_type_member_count_ = mark.named_type_member_count;
			noteArenaBytes();
		}
		transaction_marks_.pop_back();
	}

	void appendNodeTraceFields(StringBuilder& shape, CanonicalTypeNode node, uint64_t extent) const {
		shape.append(static_cast<uint64_t>(node.kind)).append(',');
		shape.append(static_cast<uint64_t>(node.builtin)).append(',');
		shape.append(static_cast<uint64_t>(node.qualifiers)).append(',');
		shape.append(static_cast<uint64_t>(node.flags)).append(',');
		shape.append(extent);
	}

	void appendTypeIdTrace(StringBuilder& shape, TypeId id) const {
		CanonicalTypeNode current = nodeUnlocked(id);
		for (;;) {
			if (isDependentNameFamily(current.kind)) {
				appendDependentNameFamilyTrace(shape, current);
				shape.append('/');
				current = nodeUnlocked(current.child);
				continue;
			}
			if (current.kind == CanonicalTypeKind::Function) {
				const TypeId param_link = unpackFunctionParamLink(current.array_extent);
				const ExprId dependent = unpackFunctionDependentNoexcept(current.array_extent);
				appendNodeTraceFields(
					shape,
					current,
					(param_link ? 1ull : 0ull) | (static_cast<uint64_t>(dependent.value) << 1));
				TypeId cursor = param_link;
				while (cursor) {
					const CanonicalTypeNode param = nodeUnlocked(cursor);
					shape.append('/');
					appendNodeTraceFields(shape, param, 0);
					shape.append('/');
					appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(param.array_extent)});
					cursor = param.child;
				}
				shape.append('/');
				current = nodeUnlocked(current.child);
				continue;
			}
			if (current.kind == CanonicalTypeKind::TemplateSpecialization) {
				appendNodeTraceFields(shape, current, current.array_extent);
				TypeId cursor = current.child;
				while (cursor) {
					const CanonicalTypeNode arg = nodeUnlocked(cursor);
					shape.append('/');
					appendNodeTraceFields(shape, arg, 0);
					shape.append('/');
					appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
					cursor = arg.child;
				}
				break;
			}
			if (isMemberPointer(current.kind)) {
				appendNodeTraceFields(shape, current, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(current.array_extent)});
				shape.append('/');
				current = nodeUnlocked(current.child);
				continue;
			}
			appendNodeTraceFields(shape, current,
				current.kind == CanonicalTypeKind::FunctionParam ||
					current.kind == CanonicalTypeKind::TemplateArg
					? 0
					: current.array_extent);
			if (!current.child || current.kind == CanonicalTypeKind::Builtin ||
				current.kind == CanonicalTypeKind::FunctionParam ||
				current.kind == CanonicalTypeKind::TemplateArg ||
				current.kind == CanonicalTypeKind::Record ||
				current.kind == CanonicalTypeKind::TemplateParameter) {
				break;
			}
			shape.append('/');
			current = nodeUnlocked(current.child);
		}
	}

	void appendDependentNameFamilyTrace(StringBuilder& shape, CanonicalTypeNode node) const {
		appendNodeTraceFields(shape, node, 0);
		TypeId name_link = node.kind == CanonicalTypeKind::DependentTemplateMember
			? unpackDependentTemplateMemberName(node.array_extent)
			: TypeId{static_cast<uint32_t>(node.array_extent)};
		TypeId cursor = name_link;
		while (cursor) {
			const auto bytes = nodeUnlocked(cursor);
			shape.append('/');
			appendNodeTraceFields(shape, bytes, bytes.array_extent);
			cursor = bytes.child;
		}
		if (node.kind == CanonicalTypeKind::DependentTemplateMember) {
			TypeId arg_cursor = unpackDependentTemplateMemberArgs(node.array_extent);
			while (arg_cursor) {
				const CanonicalTypeNode arg = nodeUnlocked(arg_cursor);
				shape.append('/');
				appendNodeTraceFields(shape, arg, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
				arg_cursor = arg.child;
			}
		}
	}

	void appendNodeTrace(StringBuilder& shape, CanonicalTypeNode node) const {
		if (isDependentNameFamily(node.kind)) {
			appendDependentNameFamilyTrace(shape, node);
			shape.append('/');
			appendTypeIdTrace(shape, node.child);
			return;
		}
		if (node.kind == CanonicalTypeKind::Function) {
			const TypeId param_link = unpackFunctionParamLink(node.array_extent);
			const ExprId dependent = unpackFunctionDependentNoexcept(node.array_extent);
			appendNodeTraceFields(
				shape,
				node,
				(param_link ? 1ull : 0ull) | (static_cast<uint64_t>(dependent.value) << 1));
			TypeId cursor = param_link;
			while (cursor) {
				const CanonicalTypeNode param = nodeUnlocked(cursor);
				shape.append('/');
				appendNodeTraceFields(shape, param, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(param.array_extent)});
				cursor = param.child;
			}
			shape.append('/');
			appendTypeIdTrace(shape, node.child);
			return;
		}
		if (node.kind == CanonicalTypeKind::TemplateSpecialization) {
			appendNodeTraceFields(shape, node, node.array_extent);
			TypeId cursor = node.child;
			while (cursor) {
				const CanonicalTypeNode arg = nodeUnlocked(cursor);
				shape.append('/');
				appendNodeTraceFields(shape, arg, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
				cursor = arg.child;
			}
			return;
		}
		if (node.kind == CanonicalTypeKind::FunctionParam ||
			node.kind == CanonicalTypeKind::TemplateArg) {
			appendNodeTraceFields(shape, node, 0);
			shape.append('/');
			appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(node.array_extent)});
			return;
		}
		if (isMemberPointer(node.kind)) {
			appendNodeTraceFields(shape, node, 0);
			shape.append('/');
			appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(node.array_extent)});
			shape.append('/');
			appendTypeIdTrace(shape, node.child);
			return;
		}
		appendNodeTraceFields(shape, node, node.array_extent);
		if (!node.child || node.kind == CanonicalTypeKind::Builtin ||
			node.kind == CanonicalTypeKind::Record ||
			node.kind == CanonicalTypeKind::TemplateParameter) {
			return;
		}
		shape.append('/');
		appendTypeIdTrace(shape, node.child);
	}

	void traceRequestUnlocked(CanonicalTypeNode node) const {
		if (!FLASH_LOG_ENABLED(Types, Trace)) {
			return;
		}
		// Trace-local structural spelling only: never serialize numeric TypeIds.
		// Each slash-separated node is kind,builtin,cv,flags,extent. Function
		// and member-pointer owner/param TypeIds expand as nested shapes.
		// Record extent carries EntityId, which is entity identity rather than a
		// TypeId slot.
		StringBuilder shape;
		appendNodeTrace(shape, node);
		FLASH_LOG(Types, Trace, "canonical-request-v2 ", shape.commit());
	}

	// The shallow architectural corpus measures records into 64-slot chunks
	// (1,024 node bytes). Deep-nesting probes deliberately spill.
	static constexpr uint32_t kChunkSize = 64;
	size_t live_count_ = 0;
	size_t live_record_layout_count_ = 0;
	size_t live_enum_layout_count_ = 0;
	size_t live_record_field_schema_count_ = 0;
	size_t live_record_member_count_ = 0;
	size_t live_record_base_count_ = 0;
	size_t live_named_type_member_schema_count_ = 0;
	size_t live_named_type_member_count_ = 0;
	SemanticArenaAccounting* accounting_ = nullptr;
	std::vector<TransactionMark> transaction_marks_;
	std::thread::id transaction_owner_;
	mutable std::mutex mutex_;
	ChunkedVector<CanonicalTypeNode, kChunkSize> nodes_;
	std::unordered_map<CanonicalTypeNode, TypeId, NodeHash> ids_;
	// Layout samples use 16 slots (384 record bytes / 256 enum bytes per chunk)
	// until a production corpus provides a larger measured complete-layout peak.
	ChunkedVector<CanonicalRecordLayout, 16> record_layouts_;
	ChunkedVector<CanonicalEnumLayout, 16> enum_layouts_;
	std::unordered_map<uint32_t, size_t> record_layout_ids_;
	std::unordered_map<uint32_t, size_t> enum_layout_ids_;
	// Field-schema samples: 16 headers, 32 members, 16 bases per chunk until a
	// production corpus measures a larger peak.
	ChunkedVector<CanonicalRecordFieldSchemaHeader, 16> record_field_schema_headers_;
	ChunkedVector<CanonicalRecordMember, 32> record_members_;
	ChunkedVector<CanonicalRecordBase, 16> record_bases_;
	std::unordered_map<uint32_t, size_t> record_field_schema_ids_;
	// Named type-member schema samples: 16 headers / 32 members per chunk until a
	// production nested-type corpus measures a larger peak.
	ChunkedVector<CanonicalNamedTypeMemberSchemaHeader, 16> named_type_member_schema_headers_;
	ChunkedVector<CanonicalNamedTypeMember, 32> named_type_members_;
	std::unordered_map<uint32_t, size_t> named_type_member_schema_ids_;
};

// Checkpoints publish only when the surrounding transaction commits. Nested
// commit preserves the outer checkpoint; outer rollback discards both scopes.
// IDs from a discarded probe must not escape it, as with declaration IDs.
class CanonicalTypeTransaction {
public:
	explicit CanonicalTypeTransaction(CanonicalTypeTable& table)
		: table_(table), depth_(table.beginTransaction()) {}
	~CanonicalTypeTransaction() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, false);
		}
	}
	CanonicalTypeTransaction(const CanonicalTypeTransaction&) = delete;
	CanonicalTypeTransaction& operator=(const CanonicalTypeTransaction&) = delete;
	void commit() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, true);
			depth_ = 0;
		}
	}
	void rollback() {
		if (depth_ != 0) {
			table_.finishTransaction(depth_, false);
			depth_ = 0;
		}
	}
private:
	CanonicalTypeTable& table_;
	size_t depth_;
};

static_assert(sizeof(CanonicalTypeTransaction) == 2 * sizeof(void*));
