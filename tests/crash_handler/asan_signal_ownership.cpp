#include "CrashHandler.h"

#include <csignal>
#include <cstddef>

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline))
#endif
static void exhaustStack(std::size_t depth) {
	volatile char frame[4096]{};
	frame[depth % sizeof(frame)] = static_cast<char>(depth);
	exhaustStack(depth + 1);
	(void)frame[0];
}

int main(int argc, char**) {
	if (!CrashHandler::install()) {
		return 2;
	}
	if (argc > 1) {
		raise(SIGSEGV);
		return 4;
	}
	exhaustStack(0);
	return 3;
}
