#define FLASHCPP_CANONICAL_IDENTITY_HARNESS 1

#include "CanonicalTypeTests.h"

#include <memory>
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

// The identity-pair check needs one alias to compare a typedef with its
// underlying type. A parser-owned alias would come from AstNodeTypes.cpp, which
// this harness does not link, so keep a minimal registry for the single shape
// the check publishes. The registry is empty otherwise and every lookup misses.
namespace {

std::vector<TypeInfo*>& harnessTypeRegistry() {
	static std::vector<TypeInfo*> registry(1, nullptr);
	return registry;
}

} // namespace

const TypeInfo* tryGetTypeInfo(TypeIndex idx) {
	const std::vector<TypeInfo*>& registry = harnessTypeRegistry();
	if (!idx.is_valid() || idx.index() >= registry.size()) {
		return nullptr;
	}
	return registry[idx.index()];
}

size_t getTypeInfoCount() {
	return harnessTypeRegistry().size();
}

void tryBindPublishedMemberClassEntity(TypeSpecifierNode&) {}

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

// The harness has no AstNodeTypes.cpp registry; native builtins project to the
// category-only TypeIndex the structural type nodes already use.
TypeIndex nativeTypeIndex(TypeCategory category) {
	return TypeIndex{0, category};
}

namespace CanonicalTypeTests {

TypeIndex publishHarnessAliasType(const TypeSpecifierNode& underlying) {
	std::vector<TypeInfo*>& registry = harnessTypeRegistry();
	const TypeIndex alias_index{static_cast<uint32_t>(registry.size()), TypeCategory::TypeAlias};
	TypeInfo* info = new TypeInfo(StringTable::getOrInternStringHandle("HarnessAlias"),
		underlying.type_index(), underlying.size_in_bits());
	info->registered_type_index_ = alias_index;
	info->is_type_alias_ = true;
	info->alias_type_spec_ = std::make_unique<TypeSpecifierNode>(underlying);
	registry.push_back(info);
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
