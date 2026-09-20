#pragma once

#include <algorithm>
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
#include "InlineVector.h"
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
	Enum, TemplateParameter, TemplateSpecialization, AliasTemplateSpecialization, TemplateArg,
	DependentName, DependentTemplateMember, NameBytes,
	// Internal Spec/DTM argument-list links carrying opaque payload identities.
	NonTypeTemplateArg, TemplateTemplateArg, DependentTemplateTemplateArg,
	// A qualified member-alias use with published declaration identity:
	// child is the owner qualifier, array_extent packs the alias TemplateDeclId
	// (low 32) with a type-only TemplateArg chain head (high 32). Appended last
	// so the numeric identities of the existing kinds are unchanged.
	DependentMemberAlias,
};

enum class CanonicalTemplateArgKind : uint8_t {
	Type = 0,
	NonType = 1,
	Template = 2,
	DependentTemplate = 3,
};

// Mixed specialization argument. Type uses TypeId, NonType uses opaque ExprId
// identity (no constant folding here), and Template uses a published primary
// class TemplateDeclId. DependentTemplate uses the owning published class
// TemplateDeclId plus its template-parameter index. Concrete packs expand into
// this ordered list.
struct CanonicalTemplateArgument {
	CanonicalTemplateArgKind kind;
	TypeId type;
	ExprId expr;
	TemplateDeclId template_decl;
	uint32_t template_parameter_index;

	static CanonicalTemplateArgument makeType(TypeId type_id) {
		return CanonicalTemplateArgument{
			CanonicalTemplateArgKind::Type, type_id, ExprId{}, TemplateDeclId{}, 0};
	}
	static CanonicalTemplateArgument makeNonType(ExprId expr_id) {
		return CanonicalTemplateArgument{
			CanonicalTemplateArgKind::NonType, TypeId{}, expr_id, TemplateDeclId{}, 0};
	}
	static CanonicalTemplateArgument makeTemplate(TemplateDeclId template_decl_id) {
		return CanonicalTemplateArgument{
			CanonicalTemplateArgKind::Template, TypeId{}, ExprId{}, template_decl_id, 0};
	}
	static CanonicalTemplateArgument makeDependentTemplate(
		TemplateDeclId template_decl_id,
		uint32_t parameter_index) {
		return CanonicalTemplateArgument{
			CanonicalTemplateArgKind::DependentTemplate,
			TypeId{},
			ExprId{},
			template_decl_id,
			parameter_index};
	}
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

// A qualified member-alias use packs the published alias TemplateDeclId in the
// low 32 bits and the type-only argument chain head in the high 32 bits.
inline uint64_t packDependentMemberAliasExtent(TemplateDeclId member, TypeId arg_link) {
	return static_cast<uint64_t>(member.value) |
		(static_cast<uint64_t>(arg_link.value) << 32);
}

inline TemplateDeclId unpackDependentMemberAliasDecl(uint64_t array_extent) {
	return TemplateDeclId{static_cast<uint32_t>(array_extent)};
}

inline TypeId unpackDependentMemberAliasArgs(uint64_t array_extent) {
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
// 32 bits. TemplateSpecialization and AliasTemplateSpecialization store TemplateDeclId in array_extent and the
// first TemplateArg / NonTypeTemplateArg / TemplateTemplateArg /
// DependentTemplateTemplateArg link in child.
// TemplateArg links store a type TypeId in array_extent; NonTypeTemplateArg links
// store an opaque NTTP ExprId; TemplateTemplateArg links store a published primary
// class TemplateDeclId; DependentTemplateTemplateArg links pack an owning class
// TemplateDeclId plus its template-parameter index. DependentName stores its qualifier in child and a NameBytes link
// in array_extent. DependentTemplateMember stores its qualifier in child and
// packs a NameBytes link (low 32) with a type-only TemplateArg chain head
// (high 32) in array_extent — the unresolved member template-id form without a
// published TemplateDeclId.
// DependentMemberAlias stores its qualifier in child and packs a published
// alias TemplateDeclId (low 32) with a type-only TemplateArg chain head
// (high 32) in array_extent — the resolved-by-identity member template-id form.
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

	TypeId builtin(CanonicalBuiltinKind kind);

	TypeId qualify(TypeId type, CVQualifier qualifiers);

	TypeId pointer(TypeId pointee);

	TypeId array(TypeId element, size_t extent);

	TypeId arrayOfUnknownBound(TypeId element);

	TypeId reference(TypeId referent, ReferenceQualifier qualifier);

	// Free-function and cv/ref-qualified function types. Calling convention is
	// stored in the unused builtin byte for Function nodes; dllimport/dllexport
	// use dedicated flag bits. Dependent noexcept(expr) identity is an opaque
	// ExprId packed beside the parameter-list link; plain noexcept remains a flag.
	TypeId function(TypeId return_type, std::span<const TypeId> parameters, bool is_variadic,
		CVQualifier function_cv, ReferenceQualifier function_ref, bool is_noexcept,
		CanonicalCallingConvention calling_convention, CanonicalDllLinkage dll_linkage,
		ExprId dependent_noexcept);

	// Opaque class identity for member-pointer owners. Complete-object layout and
	// field schemas are published separately by EntityId; EntityId remains this
	// node's only key.
	TypeId record(EntityId entity);

	// Opaque enum identity. Underlying-type layout is published separately by
	// EntityId; EntityId remains this node's only key.
	TypeId enumeration(EntityId entity);

	// Opaque type-template-parameter identity. Spelling is never part of this
	// key; callers publish TemplateDeclId + parameter index separately from the
	// legacy StringHandle binding used for lookup.
	TypeId templateParameter(TemplateDeclId template_decl, uint32_t parameter_index);

	// Class-template specialization identity: primary TemplateDeclId plus a
	// linked list of type TypeIds, opaque NTTP ExprIds, and/or published primary
	// class TemplateDeclIds. Concrete packs expand into the ordered link list;
	// callers must not invent TemplateDeclIds.
	TypeId templateSpecialization(TemplateDeclId primary,
		std::span<const CanonicalTemplateArgument> arguments);

	// An unresolved alias template-id preserves its own declaration identity.
	// It may redirect to the alias target only after concrete substitution.
	TypeId aliasTemplateSpecialization(TemplateDeclId primary,
		std::span<const CanonicalTemplateArgument> arguments);

	TypeId aliasTemplateSpecialization(TemplateDeclId primary, std::span<const TypeId> arguments);

	// Direct alias patterns are keyed by declaration identity. The target is a
	// canonical type in that alias's parameter environment; it is never a
	// spelling or an AST pointer.
	void publishAliasTemplateTarget(TemplateDeclId primary, TypeId target,
		std::span<const CanonicalTemplateArgKind> parameter_kinds);

	void publishAliasTemplateTarget(TemplateDeclId primary, TemplateDeclId owner,
		TypeId target, std::span<const CanonicalTemplateArgKind> parameter_kinds);

	// Resolve a published direct member alias with its enclosing class-template
	// arguments and its own arguments. Both substitutions use the iterative
	// canonical worklist. Fail-closed: a partially dependent argument, a
	// non-Type owner/member argument layout, or an owner specialization that does
	// not cover every owner parameter reference the target performs returns
	// nullopt instead of substituting (a short layout would otherwise reach
	// substituteArgumentsUnlocked and throw). Extra owner arguments are ignored.
	std::optional<TypeId> resolveMemberAliasTarget(TemplateDeclId member,
		TypeId owner_specialization, std::span<const TypeId> alias_args);

	// Resolve a canonical qualified member-alias use by reading the alias
	// declaration identity it carries: the owner specialization is the node's
	// qualifier and the alias arguments are its type-only argument chain. This is
	// the auto-redirect entry point for DependentMemberAlias tips; a use without
	// published identity is not this kind and stays unresolved.
	std::optional<TypeId> resolveMemberAliasUse(TypeId use);

	std::optional<TypeId> aliasTemplateTarget(TemplateDeclId primary) const;

	// Type-only convenience overload for Spec identity.
	TypeId templateSpecialization(TemplateDeclId primary, std::span<const TypeId> arguments);

	// Plain identifier members of unknown specializations. Qualifiers may be a
	// published type parameter, a type-only TemplateSpecialization (Primary<Args>),
	// or a prior dependent-name-family node. The caller supplies one normalized
	// identifier token, not a qualified spelling to recover or a lookup result
	// ([temp.dep.type]).
	TypeId dependentName(TypeId qualifier, std::string_view identifier);

	// Unresolved member template-id (T::Foo<Args> or Primary<Args>::Foo<U>)
	// without a published member TemplateDeclId. Identity is qualifier +
	// identifier content + type-only argument TypeIds. Qualifiers may include a
	// type-only TemplateSpecialization. ::template is a parse disambiguator and
	// is not stored.
	TypeId dependentTemplateMember(
		TypeId qualifier,
		std::string_view identifier,
		std::span<const TypeId> arguments);

	// Qualified member-alias use with a published alias TemplateDeclId. Identity
	// is qualifier + alias declaration + type-only argument TypeIds; no spelling
	// and no AST pointer participate. The resolver redirects the node only when
	// the owner specialization and every argument are concrete ([temp.dep]).
	TypeId dependentMemberAlias(
		TypeId qualifier,
		TemplateDeclId member,
		std::span<const TypeId> arguments);

	TypeId dependentNameQualifier(TypeId type) const;

	std::string dependentNameIdentifier(TypeId type) const;

	TypeId dependentTemplateMemberArguments(TypeId type) const;

	TemplateDeclId dependentMemberAliasDecl(TypeId type) const;

	TypeId dependentMemberAliasArguments(TypeId type) const;

	// Structural substitution for one published primary's type parameters.
	// Replaces TemplateParameter(env, i) with args[i], rebuilds Spec / DependentName
	// / DependentTemplateMember / Function / member-pointer / cv / pointer / array /
	// reference wrappers, and leaves unresolved member tips as DependentName-family
	// nodes (no lookup). Function and member-pointer rebuilds preserve calling
	// convention, cv/ref qualifiers, variadic, plain noexcept, dll linkage, and the
	// opaque dependent-noexcept ExprId; a substituted shape that violates those
	// categories fails closed. Pack / template-template args and production wiring
	// stay deferred. Spec NTTP ExprId arguments are preserved opaquely through
	// substitute. Iterative: logical depth does not map to native call depth.
	//
	// Concrete direct alias specializations anywhere in the substituted graph are
	// redirected to their published targets; mixed non-type / template-template
	// argument layouts are interpreted positionally against the target's published
	// parameter kinds.
	TypeId substitute(TypeId type, TemplateDeclId env, std::span<const TypeId> args);

	TypeId memberObjectPointer(TypeId owner, TypeId pointee);

	TypeId memberFunctionPointer(TypeId owner, TypeId function);

	TypeId withoutTopLevelQualifiers(TypeId id) const;

	CanonicalTypeNode node(TypeId id) const;

	TypeId functionParameterType(TypeId param_link) const;

	TypeId functionParameterNext(TypeId param_link) const;

	TypeId functionParameters(TypeId function) const;

	ExprId functionDependentNoexcept(TypeId function) const;

	TypeId memberPointerOwner(TypeId member_pointer) const;

	TypeId memberPointerPointee(TypeId member_pointer) const;

	EntityId recordEntity(TypeId record) const;

	EntityId enumEntity(TypeId enumeration) const;

	TemplateDeclId templateParameterDecl(TypeId parameter) const;

	uint32_t templateParameterIndex(TypeId parameter) const;

	// Direct alias targets may capture parameters of the alias and its known
	// enclosing class template. An empty owner permits only alias parameters.
	bool dependsOnlyOnTemplateParameters(TypeId type, TemplateDeclId template_decl) const;

	bool dependsOnlyOnTemplateParameters(TypeId type, TemplateDeclId template_decl,
		TemplateDeclId owner_decl) const;

	TemplateDeclId templateSpecializationDecl(TypeId specialization) const;

	TypeId templateSpecializationArguments(TypeId specialization) const;

	TypeId templateArgumentType(TypeId arg_link) const;

	ExprId templateArgumentExpr(TypeId arg_link) const;

	TemplateDeclId templateArgumentTemplate(TypeId arg_link) const;

	TemplateDeclId templateArgumentDependentTemplateDecl(TypeId arg_link) const;

	uint32_t templateArgumentDependentTemplateIndex(TypeId arg_link) const;

	CanonicalTemplateArgKind templateArgumentKind(TypeId arg_link) const;

	bool templateArgumentIsType(TypeId arg_link) const;

	TypeId templateArgumentNext(TypeId arg_link) const;

	void publishRecordLayout(CanonicalRecordLayout layout);

	void publishEnumLayout(CanonicalEnumLayout layout);

	bool hasRecordLayout(EntityId entity) const;

	bool hasEnumLayout(EntityId entity) const;

	CanonicalRecordLayout recordLayout(EntityId entity) const;

	CanonicalEnumLayout enumLayout(EntityId entity) const;

	void publishRecordFieldSchema(EntityId entity,
		std::span<const CanonicalRecordMember> members,
		std::span<const CanonicalRecordBase> bases);

	bool hasRecordFieldSchema(EntityId entity) const;

	CanonicalRecordMember recordMemberAt(EntityId entity, size_t index) const;

	CanonicalRecordBase recordBaseAt(EntityId entity, size_t index) const;

	// Publish nested type members (typedef / using / nested class targets) keyed by
	// EntityId. Identifier content is NameBytes; StringHandle is never stored.
	// Equal republish is idempotent; conflicting content throws. Layout is not
	// required — this schema is independent of CanonicalRecordMember field layout.
	void publishRecordNamedTypeMembers(EntityId entity,
		std::span<const CanonicalNamedTypeMemberSpec> members);

	bool hasRecordNamedTypeMembers(EntityId entity) const;

	std::optional<TypeId> tryLookupNamedTypeMember(EntityId entity, std::string_view name) const;

	// Collapse plain DependentName chains when every step is Record + published
	// named type-member. Miss / DependentTemplateMember / non-Record qualifier
	// leaves the tip unchanged. No StringHandle identity and no SymbolTable.
	TypeId tryResolveDependentTip(TypeId type);

	size_t size() const;

	CanonicalTypeArenaStats arenaStats() const;

private:
	struct AliasTemplateTarget {
		TypeId target;
		TemplateDeclId owner;
		TemplateVector<CanonicalTemplateArgKind, 4> parameter_kinds;
	};
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

	static bool isReference(CanonicalTypeKind kind);

	static bool isInternalLink(CanonicalTypeKind kind);

	static bool isDependentQualifierKind(CanonicalTypeKind kind);

	static bool isDependentNameFamily(CanonicalTypeKind kind);

	// A concrete direct-alias argument must not still mention a template
	// parameter or a dependent name/member, and must not carry a dependent
	// template-template parameter. Wrappers, functions, member pointers, and
	// specialization argument links are walked iteratively; no parser or AST
	// state participates.
	bool isDependentAliasArgumentUnlocked(TypeId type) const;

	// Every TemplateParameter / dependent template-template reference in `type`
	// that belongs to `env` must have an index below `argument_count`. This is
	// the explicit arity/shape gate that keeps substituteArgumentsUnlocked from
	// throwing on a short environment. Wrappers, functions, member pointers, and
	// specialization argument links are walked iteratively; no parser state.
	bool templateParameterReferencesCoveredUnlocked(TypeId type, TemplateDeclId env,
		size_t argument_count) const;

	// Non-type parameter references have no canonical identity distinct from an
	// opaque NTTP literal, so a target that mentions any non-type argument cannot
	// be proven free of an alias parameter reference.
	bool containsNonTypeTemplateArgUnlocked(TypeId type) const;

	// Concrete direct alias specializations redirect to the published target
	// pattern anywhere in the type graph: wrappers, function / member-pointer
	// shapes, specialization type arguments, and nested array / qualified /
	// pointer / reference nodes. Every rebuilt child passes through the redirect,
	// so nested aliases normalize as well. Mixed type / non-type /
	// template-template argument layouts are matched positionally against the
	// target's published parameter kinds, and template-template placeholders are
	// replaced by the concrete TemplateDeclId. An explicit worklist keeps
	// nested-alias depth off the native stack; the scope each expansion carries
	// records the declaration IDs currently being expanded, so direct and nested
	// declaration-ID cycles leave the alias boundary instead of looping.
	// Dependent arguments, kind or arity mismatches, unresolved non-type targets,
	// unpublished declarations, and cycles keep the alias boundary.
	TypeId resolveNestedAliasGraphUnlocked(TypeId type);

	TypeId packIdentifierBytesUnlocked(std::string_view identifier);

	TypeId rebuildTemplateArgListUnlocked(std::span<const TypeId> arguments);

	TypeId rebuildMixedTemplateArgListUnlocked(std::span<const CanonicalTemplateArgument> arguments);

	TypeId substituteArgumentsUnlocked(TypeId type, TemplateDeclId env,
		std::span<const CanonicalTemplateArgument> args);

	static bool isMemberPointer(CanonicalTypeKind kind);

	TypeId recordOwnerUnlocked(TypeId owner) const;

	TypeId arrayUnlocked(TypeId element, uint64_t extent, CanonicalTypeNodeFlags flags);

	CanonicalTypeNode nodeUnlocked(TypeId id) const;

	TypeId internUnlocked(CanonicalTypeNode node);

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

	void validateRecordMemberUnlocked(const CanonicalRecordMember& member) const;

	void validateRecordBaseUnlocked(const CanonicalRecordBase& base) const;

	CanonicalRecordFieldSchemaHeader fieldSchemaHeaderUnlocked(EntityId entity) const {
		const auto found = record_field_schema_ids_.find(entity.value);
		if (!entity || found == record_field_schema_ids_.end()) {
			throw InternalError("canonical type: record has no field schema");
		}
		return record_field_schema_headers_[found->second];
	}

	bool nameBytesEqualUnlocked(TypeId name_link, std::string_view identifier) const;

	std::optional<TypeId> tryLookupNamedTypeMemberUnlocked(EntityId entity,
		std::string_view name) const;

	// Shared implementation behind resolveMemberAliasTarget and
	// resolveMemberAliasUse. The caller holds mutex_.
	std::optional<TypeId> resolveMemberAliasTargetUnlocked(TemplateDeclId member,
		TypeId owner_specialization, std::span<const TypeId> alias_args);

	// Read the declaration identity a DependentMemberAlias carries and redirect
	// through resolveMemberAliasTargetUnlocked. The caller holds mutex_.
	std::optional<TypeId> resolveMemberAliasUseUnlocked(TypeId use);

	TypeId tryResolveDependentTipUnlocked(TypeId type);

	uint64_t usedBytesUnlocked() const;

	uint64_t reservedBytesUnlocked() const;

	void noteArenaBytes();

	void checkTransactionThread() const;

	size_t beginTransaction();

	void finishTransaction(size_t depth, bool commit);

	void appendNodeTraceFields(StringBuilder& shape, CanonicalTypeNode node, uint64_t extent) const;

	void appendTypeIdTrace(StringBuilder& shape, TypeId id) const;

	void appendDependentNameFamilyTrace(StringBuilder& shape, CanonicalTypeNode node) const;

	void appendNodeTrace(StringBuilder& shape, CanonicalTypeNode node) const;

	void traceRequestUnlocked(CanonicalTypeNode node) const;

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
	std::unordered_map<uint32_t, AliasTemplateTarget> alias_template_targets_;
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
	void commit();
	void rollback();
private:
	CanonicalTypeTable& table_;
	size_t depth_;
};

static_assert(sizeof(CanonicalTypeTransaction) == 2 * sizeof(void*));
