#include "CanonicalTypeTests.h"

ChunkedStringAllocator gChunkedStringAllocator;

int main() {
	try {
		return CanonicalTypeTests::run();
	} catch (const InternalError& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
