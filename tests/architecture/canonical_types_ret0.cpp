#include "CanonicalTypeTests.h"

// This host regression links the adapter without AstNodeTypes.cpp.
// importCanonicalMemberPointer calls tryBindPublishedTypeEntity to recover a nominal
// pointee from the legacy type registry, which this translation unit does not provide.
// Pointees here already carry an EntityId or stay unpublished.
// The identity-independence probes in CanonicalTypeTests.h run in this same
// translation unit, which links no Parser and no SemanticAnalysis state. They can
// therefore rebuild canonical identity only from structural inputs, never from a
// parser stack or a member-context stack.
void tryBindPublishedTypeEntity(TypeSpecifierNode&) {}

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
