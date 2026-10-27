#include "CanonicalTypeTests.h"

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
