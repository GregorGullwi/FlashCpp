// SEH filters and exceptional __finally entries receive the CRT establisher as
// RSP for FrameReg=0 realigned frames. Locals must still resolve correctly.

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0

int main() {
	alignas(64) int local = 11;
	int result = 0;
	int* p = 0;

	if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
		return 10;
	}

	__try {
		__try {
			*p = 123;
			result = 99;
		} __finally {
			if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
				result = 20;
			} else {
				result += local;
			}
		}
		result = 99;
	} __except (local == 11 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		if ((reinterpret_cast<unsigned long long>(&local) % 64) != 0) {
			return 21;
		}
		result += local;
	}

	if (result != 22) {
		return result;
	}
	return 0;
}
