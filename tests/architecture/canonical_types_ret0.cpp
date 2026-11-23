#define FLASHCPP_CANONICAL_IDENTITY_HARNESS 1

#include "CanonicalTypeTests.h"

#include <deque>
#include <vector>

// This host regression links the adapter without AstNodeTypes.cpp.
// importCanonicalMemberPointer calls tryBindPublishedTypeEntity to recover a nominal
// pointee from the legacy type registry, which this translation unit does not provide.
// Pointees here already carry an EntityId or stay unpublished.
// The identity-independence probes in CanonicalTypeTests.h run in this same
// translation unit, which links no Parser and no SemanticAnalysis state. They can
// therefore rebuild canonical identity only from structural inputs, never from a
// parser stack or a member-context stack.
void tryBindPublishedTypeEntity(TypeSpecifierNode&) {}

// The identity-pair check needs aliases to compare a typedef with its underlying
// type. A parser-owned alias would come from AstNodeTypes.cpp, which builds the
// whole type registry and is deliberately not linked here; register the rows
// with the same setters production registration uses. A deque keeps row
// addresses stable across growth and supports the index lookup below.
namespace {

std::deque<TypeInfo>& harnessTypeRegistry() {
	static std::deque<TypeInfo> registry;
	if (registry.empty()) {
		registry.emplace_back(); // reserved slot 0, so a TypeIndex index is a row index
	}
	return registry;
}

// Native indices sit far above any row the alias check can publish (one
// reserved row plus a handful of aliases), so the two ranges cannot collide.
constexpr uint32_t kHarnessNativeTypeIndexBase = 0x40000000u;

} // namespace

const TypeInfo* tryGetTypeInfo(TypeIndex idx) {
	const std::deque<TypeInfo>& registry = harnessTypeRegistry();
	if (!idx.is_valid() || idx.index() >= registry.size()) {
		return nullptr;
	}
	return &registry[idx.index()];
}

size_t getTypeInfoCount() {
	return harnessTypeRegistry().size();
}

void tryBindPublishedMemberClassEntity(TypeSpecifierNode&) {}

void journalLegacyTypeInfoInPlaceMutation(TypeIndex) {}

void journalLegacyTypeInfoInPlaceMutation(const TypeInfo&) {}

// FunctionSignature identity only consults dependent expression identity for a
// dependent noexcept clause. The identity-pair check uses signatures without
// one, and the parser-owned implementation is not linked here.
namespace FlashCpp {

bool equalDependentExpressionIdentity(const ASTNode&, const ASTNode&) {
	throw InternalError("dependent expression identity is unavailable in the canonical harness");
}

size_t hashDependentExpressionIdentity(const ASTNode&) {
	throw InternalError("dependent expression identity is unavailable in the canonical harness");
}

} // namespace FlashCpp

// Mirrors AstNodeTypes.cpp:nativeTypeIndex. The real helper reads the registry
// that initialize_native_types builds in AstNodeTypes.cpp, which this harness
// does not link, so native categories get a valid, distinct high index here and
// non-native categories keep the production TypeIndex{0, category} placeholder.
TypeIndex nativeTypeIndex(TypeCategory category) {
	switch (category) {
	case TypeCategory::Void:
	case TypeCategory::Bool:
	case TypeCategory::Char:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::UnsignedChar:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
	case TypeCategory::Float:
	case TypeCategory::Double:
	case TypeCategory::LongDouble:
	case TypeCategory::Auto:
	case TypeCategory::DeclTypeAuto:
	case TypeCategory::FunctionPointer:
	case TypeCategory::MemberFunctionPointer:
		return TypeIndex{kHarnessNativeTypeIndexBase + static_cast<uint32_t>(category), category};
	default:
		return TypeIndex{0, category};
	}
}

namespace CanonicalTypeTests {

TypeIndex publishHarnessAliasType(const TypeSpecifierNode& underlying) {
	std::deque<TypeInfo>& registry = harnessTypeRegistry();
	const TypeIndex alias_index{static_cast<uint32_t>(registry.size()), TypeCategory::TypeAlias};
	TypeInfo& info = registry.emplace_back(StringTable::getOrInternStringHandle("HarnessAlias"),
		underlying.type_index(), underlying.size_in_bits());
	// Production register_type_alias writes these two members directly; TypeInfo
	// exposes no setters for them. The alias specifier goes through its setter.
	info.registered_type_index_ = alias_index;
	info.is_type_alias_ = true;
	info.setAliasTypeSpecifier(underlying);
	return alias_index;
}

} // namespace CanonicalTypeTests

// Arrays exercised here already carry an EntityId. The type-index and
// class-template array import lives in the compiler translation unit.
CanonicalTypeImport importCanonicalNominalArrayWithoutDirectEntity(CanonicalTypeTable&,
	const TypeSpecifierNode&, CanonicalTypeImportContext) {
	return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
}

ChunkedStringAllocator gChunkedStringAllocator;
#ifdef _WIN32
TargetDataModel g_target_data_model = TargetDataModel::LLP64;
#else
TargetDataModel g_target_data_model = TargetDataModel::LP64;
#endif

int main() {
	try {
		return CanonicalTypeTests::run();
	} catch (const InternalError& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
